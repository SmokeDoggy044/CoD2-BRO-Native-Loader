#!/usr/bin/env python3
"""Decompiles level functions through the Claude API (Message Batches, half price), one request per function.

Round loop per run: every row still open goes into one batch; each answer's C is written into the worktree,
compiled (g++ syntax check of the run's file) and checked with `vc71match --facts --force-inline`; compile
errors and fact differences go back to the model as the next turn of that row's conversation. A row ends as
ok (facts same), explained (the model kept its code and explained the differences as known noise - the
coordinator reviews these), failed (out of rounds) or refused; failed/refused rows go back to the Claude Code
session agents. Rows are claimed in wt/claims.csv so the worker and the session agents never take the same
row (the coordinator claims the session agents' lists too: `worker.py claim FILE OWNER`).

Inputs per row: fnpack's pack (disassembly with names, callers, callees, globals, strings), the Ghidra export
(tools/ghidra/ExportDecompile.java output: .c/.sig/.glob/.str, our prototypes applied) and the declarations of
the callees we have C for. The system prompt (cached) holds the decompiling rules, the push brief and the
pilot's C as examples - the same rules the session agents get.

usage (run with build/api_venv/bin/python; credentials: ANTHROPIC_API_KEY, else the file
~/.config/cod_bro/anthropic_api_key (chmod 600, outside the repo), else an `ant auth login` profile):
  worker.py claim FILE OWNER                      claim the rows of a list for OWNER (session agents' lists)
  worker.py prepare RUN --worktree DIR --module M --rows FILE --raw DIR
                                                  claim the rows for this run and build their first prompts
  worker.py dry RUN                               token count of one request + cost estimate, nothing sent
  worker.py run RUN [--rounds 3] [--budget USD]   submit / wait / check rounds until done or out of budget
  worker.py status RUN                            per-row states and the cost so far
  worker.py recheck RUN [--no-retry]              rewrite + check every answer again offline (after a harness fix)
Run state: build/api_worker/RUN/state.json (the batch ids, every conversation, usage).
"""
import argparse
import csv
import datetime
import fcntl
import json
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
COD = next(p for p in ROOT.parents if (p / "wt").is_dir() and (p / "decomp").is_dir())  # cod_bro/
WT = COD / "wt"
CLAIMS = WT / "claims.csv"
RUNS = COD / "decomp" / "build" / "api_worker"  # run state always in the main tree
SKILL = next(p for p in ROOT.parents if (p / ".claude" / "skills" / "xbox-port").is_dir()) / ".claude" / "skills" / "xbox-port"
MODEL = "claude-opus-5-5"
EFFORT = "high"
MAX_TOKENS = 32000
# Opus 5.5 per million tokens, Batch API (half the standard rates)
PRICE = {"input": 2.0, "output": 10.0, "cache_write": 2.5, "cache_read": 0.1}

SCHEMA = {
    "type": "object",
    "properties": {
        "name": {"type": "string", "description": "the function's C name (keep the given one unless it is FUN_*)"},
        "c_code": {"type": "string", "description": "the complete C++ definition(s): the function and any "
                                                     "file-local static helpers it needs, nothing else"},
        "header_decl": {"type": "string", "description": "the declaration for the module header (one line)"},
        "stack_bytes": {"type": "integer", "description": "bytes of stack arguments the callee sees"},
        "result": {"type": "string", "enum": ["None", "Al", "Eax", "EdxEax", "St0"]},
        "noise_explanations": {"type": "string", "description": "only when you keep your code unchanged after "
                                                                "a check: why each remaining difference is known "
                                                                "noise; else empty"},
        "notes": {"type": "string", "description": "names learned, doubts, anything the coordinator must know"},
    },
    "required": ["name", "c_code", "header_decl", "stack_bytes", "result", "noise_explanations", "notes"],
    "additionalProperties": False,
}


# ---------------------------------------------------------------------------------------------------- claims
def claims_update(rows, owner):
    """Claim addresses for owner; returns the ones that were free (or already ours)."""
    CLAIMS.touch()
    with open(CLAIMS, "r+") as f:
        fcntl.flock(f, fcntl.LOCK_EX)
        have = {l.split(",")[0]: l.split(",")[1] for l in f.read().split("\n")[1:] if l}
        got = [a for a in rows if have.get(a, owner) == owner]
        new = [a for a in got if a not in have]
        if f.tell() == 0:
            f.write("address,owner,date\n")
        stamp = datetime.date.today().isoformat()
        f.write("".join(f"{a},{owner},{stamp}\n" for a in new))
    return got


def claims_release(rows, owner, to):
    with open(CLAIMS, "r+") as f:
        fcntl.flock(f, fcntl.LOCK_EX)
        lines = f.read().split("\n")
        out = [lines[0]] + [(f"{l.split(',')[0]},{to},{l.split(',')[2]}" if l.split(",")[0] in rows
                             and l.split(",")[1] == owner else l) for l in lines[1:] if l]
        f.seek(0)
        f.truncate()
        f.write("\n".join(out) + "\n")


# ---------------------------------------------------------------------------------------------------- prompts
def system_prompt():
    rules = (Path(__file__).parent / "rules.md").read_text()
    parts = [rules,
             "# The project's decompiling notes (xbox-port skill, reference/decompiling.md)\n\n" +
             (SKILL / "reference" / "decompiling.md").read_text()]
    code = ROOT / "game" / "code"
    for name in ("title_data.h", "bro_scr_ai_common.h", "levels_prologue_a.h", "levels_prologue_a.cpp",
                 "levels_prologue_b.cpp"):
        parts.append(f"# Example / helper file game/code/{name}\n\n```cpp\n{(code / name).read_text()}\n```")
    return "\n\n".join(parts)


def run(cmd, cwd=ROOT, **kw):
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, **kw)


def callee_decls(pack, worktree):
    """Declarations of the callees fnpack marks as ours."""
    m = re.search(r"^callees \(\d+\): (.*)$", pack, re.M)
    names = re.findall(r"(\w+) \(ours\)", m.group(1)) if m else []
    out = []
    headers = sorted((worktree / "game" / "code").glob("*.h"))
    for n in names:
        for h in headers:
            hit = re.search(rf"^[^\n/#]*\b{n}\([^;]*\);", h.read_text(), re.M)
            if hit:
                out.append(f"{hit.group(0).strip()}  // {h.name}")
                break
    return out


def first_prompt(row, worktree, raw):
    pack = run(["python3", "tools/fnpack.py", row["address"]], cwd=worktree).stdout
    parts = [f"Decompile {row['name']} at 0x{row['address']} (module {row['module']}, {row['kind']} row: "
             + ("it can reach a script thread switch - C written now, not registered, checked statically only"
                if row["kind"] == "switch" else "no path to a thread switch - registered Status::Compare and "
                "tested in a compare run") + ").",
             "## fnpack\n```\n" + pack.strip() + "\n```"]
    a = row["address"]
    if (raw / f"{a}.c").exists():
        parts.append("## Ghidra decompile (our prototypes applied; level callees' prototypes are guesses)\n```c\n"
                     + (raw / f"{a}.c").read_text().strip() + "\n```")
        for ext, title in (("sig", "prototypes: own (decompiler's guess) + callees"), ("glob", "globals"),
                           ("str", "strings")):
            text = (raw / f"{a}.{ext}").read_text().strip() if (raw / f"{a}.{ext}").exists() else ""
            if text:
                parts.append(f"## Ghidra {title}\n```\n{text}\n```")
    decls = callee_decls(pack, worktree)
    if decls:
        parts.append("## Declarations of callees that have C\n```cpp\n" + "\n".join(decls) + "\n```")
    parts.append("Answer with the JSON object only.")
    return "\n\n".join(parts)


# ---------------------------------------------------------------------------------------------------- state
def load(run_name):
    return json.loads((RUNS / run_name / "state.json").read_text())


def save(state):
    path = RUNS / state["run"] / "state.json"
    tmp = path.with_suffix(".tmp")
    tmp.write_text(json.dumps(state, indent=1))
    tmp.replace(path)


def cost(usage):
    return (usage.get("input_tokens", 0) * PRICE["input"] + usage.get("output_tokens", 0) * PRICE["output"]
            + usage.get("cache_creation_input_tokens", 0) * PRICE["cache_write"]
            + usage.get("cache_read_input_tokens", 0) * PRICE["cache_read"]) / 1e6


def total_cost(state):
    return sum(cost(u) for r in state["rows"].values() for u in r["usage"])


# ---------------------------------------------------------------------------------------------------- files
def module_files(state):
    wt = Path(state["worktree"])
    stem = f"levels_{state['module']}_api"
    cpp, hdr = wt / "game" / "code" / f"{stem}.cpp", wt / "game" / "code" / f"{stem}.h"
    if not cpp.exists():
        hdr.write_text(f"#pragma once\n\n// Generated by tools/api_worker/worker.py (Claude API): {state['module']} "
                       "level functions.\n\n#include <cstdint>\n\n#include \"compiler.h\"\n#include \"script_string.h\"\n\n"
                       "namespace game {\n\n// DECLS\n\n}  // namespace game\n")
        includes = re.findall(r'^#include "[^"]+"$', (ROOT / "game/code/levels_prologue_a.cpp").read_text(), re.M)
        cpp.write_text(f'#include "{stem}.h"\n\n' + "\n".join(i for i in includes if "levels_prologue" not in i) +
                       "\n\nnamespace game {\n\nnamespace {\nusing bro_ai::pointer;  // inside an unnamed namespace: VC7.1 finds\n"
                       "using bro_ai::slot;     // them ambiguous at namespace scope (pilot run dra1)\n}  // namespace\n\n// BODIES\n\n"
                       "}  // namespace game\n")
        cmake = wt / "game" / "code" / "CMakeLists.txt"
        t = cmake.read_text()
        if f"{stem}.cpp" not in t:
            t = t.replace("    levels_prologue_a.cpp\n", f"    levels_prologue_a.cpp\n    {stem}.cpp\n", 1)
            cmake.write_text(t)
        fcpp = wt / "game" / "code" / "functions.cpp"
        t = fcpp.read_text()
        if f'#include "{stem}.h"' not in t:
            t = t.replace('#include "levels_prologue_a.h"', f'#include "levels_prologue_a.h"\n#include "{stem}.h"', 1)
            fcpp.write_text(t)
    return cpp, hdr


def put_block(path, marker, addr, text):
    t = path.read_text()
    begin, end = f"// api_worker BEGIN {addr}\n", f"// api_worker END {addr}\n"
    block = begin + text.rstrip() + "\n" + end
    if begin in t:
        t = t[:t.index(begin)] + block + t[t.index(end) + len(end):]
    else:
        t = t.replace(f"// {marker}\n", block + f"\n// {marker}\n", 1)
    path.write_text(t)


def drop_block(path, addr):
    t = path.read_text()
    begin, end = f"// api_worker BEGIN {addr}\n", f"// api_worker END {addr}\n"
    if begin in t:
        path.write_text(t[:t.index(begin)] + t[t.index(end) + len(end):])


def register(state, row, ans):
    """kFunctions entry (Compare) + functions.csv zero row for safe rows."""
    wt = Path(state["worktree"])
    fcpp = wt / "game" / "code" / "functions.cpp"
    t = fcpp.read_text()
    a = int(row["address"], 16)
    t = re.sub(rf'^    \{{"\w+", {a:#010x},.*\n', "", t, flags=re.M)
    line = (f'    {{"{ans["name"]}", {a:#010x}, 0, code(&{ans["name"]}), {ans["stack_bytes"]}, '
            f'Result::{ans["result"]}, Status::Compare}},  // api_worker')
    lines = t.split("\n")
    idx = [(i, int(m.group(1), 16)) for i, l in enumerate(lines) for m in [re.match(r'\s+\{"\w+", 0x([0-9a-fA-F]+),', l)] if m]
    at = next((i for i, x in idx if x > a), idx[-1][0] + 1)
    lines.insert(at, line)
    fcpp.write_text("\n".join(lines))
    fcsv = wt / "tools" / "data" / "functions.csv"
    rows = fcsv.read_text().split("\n")
    head, body = rows[0], [l for l in rows[1:] if l and not l.startswith(row["address"] + ",")]
    body.append(f'{row["address"]},{ans["name"]},compare,0,0,0,0,0,0,""')
    fcsv.write_text("\n".join([head] + sorted(body)) + "\n")


def unregister(state, row):
    wt = Path(state["worktree"])
    fcpp = wt / "game" / "code" / "functions.cpp"
    a = int(row["address"], 16)
    fcpp.write_text(re.sub(rf'^    \{{"\w+", {a:#010x},.*// api_worker\n', "", fcpp.read_text(), flags=re.M))


# ---------------------------------------------------------------------------------------------------- checks
def compile_check(state):
    """g++ -fsyntax-only of the run's file -> {address: [errors]} by marker line ranges."""
    wt = Path(state["worktree"])
    cpp, _ = module_files(state)
    cmd = next(e["command"] for e in json.load(open(wt / "build" / "linux-loader" / "compile_commands.json"))
               if "/game/code/" in e["file"])
    flags = [f for f in cmd.split() if f.startswith(("-I", "-D", "-std", "-m32"))]
    r = run(["c++", *flags, "-fsyntax-only", "-w", str(cpp)], cwd=wt)
    lines = cpp.read_text().split("\n")
    ranges, cur = [], None
    for i, l in enumerate(lines, 1):
        if l.startswith("// api_worker BEGIN "):
            cur = (l.split()[-1], i)
        elif l.startswith("// api_worker END ") and cur:
            ranges.append((cur[0], cur[1], i))
            cur = None
    out = {}
    for e in r.stderr.split("\n"):
        m = re.match(rf"{re.escape(str(cpp))}:(\d+):\d+: error: (.*)", e)
        if m:
            n = int(m.group(1))
            addr = next((a for a, s, t in ranges if s <= n <= t), "file")
            out.setdefault(addr, []).append(f"line {n}: {m.group(2)}")
    return out


def facts_check(state, rows):
    """vc71match --facts --force-inline for rows -> {address: (same, text)}."""
    if not rows:
        return {}
    wt = Path(state["worktree"])
    args = [f"{state['rows'][a]['answer']['name']}={'0x' + a}" for a in rows]
    import os
    env = dict(os.environ, XDK5849=str(COD.parent / "Tools" / "xdk5849" / "XDK" / "xbox"))
    r = run(["python3", "-I", "tools/vc71match/vc71match.py", *args, "--facts", "--force-inline"], cwd=wt,
            timeout=3600, env=env)
    out, cur, buf = {}, None, []
    by_name = {state["rows"][a]["answer"]["name"]: a for a in rows}
    for line in r.stdout.split("\n") + ["=== end"]:
        m = re.match(r"=== (\w+) 0x[0-9a-f]+ facts (same|differ)", line)
        if line.startswith("=== "):
            if cur:
                out[cur[0]] = (cur[1], "\n".join(buf))
            cur, buf = ((by_name.get(m.group(1)), m.group(2) == "same") if m else None), []
        elif cur and line.startswith("  "):
            buf.append(line)
    for a in rows:
        out.setdefault(a, (False, "vc71match printed no result (no-compile?):\n" + r.stdout[-1500:] + r.stderr[-800:]))
    return out


# ---------------------------------------------------------------------------------------------------- rounds
def request(state, addr, system):
    row = state["rows"][addr]
    return {"custom_id": f"r{state['round']}_{addr}",
            "params": {"model": MODEL, "max_tokens": MAX_TOKENS,
                       "system": [{"type": "text", "text": system, "cache_control": {"type": "ephemeral"}}],
                       "messages": row["messages"],
                       "output_config": {"effort": EFFORT, "format": {"type": "json_schema", "schema": SCHEMA}}}}


def submit(state, client):
    system = system_prompt()
    open_rows = [a for a, r in state["rows"].items() if r["state"] in ("pending", "retry")]
    if not open_rows:
        return None
    state["round"] += 1
    batch = client.messages.batches.create(requests=[request(state, a, system) for a in open_rows])
    state["batches"].append({"id": batch.id, "round": state["round"], "rows": open_rows})
    for a in open_rows:
        state["rows"][a]["state"] = "submitted"
    save(state)
    print(f"round {state['round']}: batch {batch.id}, {len(open_rows)} rows")
    return batch.id


def wait(client, batch_id):
    while True:
        b = client.messages.batches.retrieve(batch_id)
        if b.processing_status == "ended":
            return b
        print(f"  {b.processing_status}: {b.request_counts.processing} processing, "
              f"{b.request_counts.succeeded} done", flush=True)
        time.sleep(60)


def collect(state, client, batch_id):
    cpp, hdr = module_files(state)
    answered = []
    for res in client.messages.batches.results(batch_id):
        addr = res.custom_id.split("_", 1)[1]
        row = state["rows"][addr]
        if res.result.type != "succeeded":
            row["state"] = "retry" if row["round_count"] < state["max_rounds"] else "failed"
            row["log"].append(f"batch result {res.result.type}")
            continue
        msg = res.result.message
        row["usage"].append(msg.usage.to_dict())
        row["round_count"] += 1
        if msg.stop_reason == "refusal":
            row["state"] = "refused"
            row["log"].append(f"refusal: {msg.stop_details.category if msg.stop_details else '?'}")
            continue
        text = next((b.text for b in msg.content if b.type == "text"), "")
        try:
            ans = json.loads(text)
        except json.JSONDecodeError:
            row["state"] = "retry" if row["round_count"] < state["max_rounds"] else "failed"
            row["log"].append(f"no JSON (stop {msg.stop_reason})")
            row["messages"].append({"role": "assistant", "content": [b.to_dict() for b in msg.content]})
            row["messages"].append({"role": "user", "content": "Your answer was not a complete JSON object "
                                    f"(stop reason {msg.stop_reason}). Answer again with the JSON object only."})
            continue
        unchanged = row.get("answer") and row["answer"]["c_code"].strip() == ans["c_code"].strip()
        row["messages"].append({"role": "assistant", "content": [b.to_dict() for b in msg.content]})
        row["answer"], row["unchanged"] = ans, bool(unchanged)
        put_block(cpp, "BODIES", addr, f"// {'0x' + addr} (api_worker, {MODEL})\n" + ans["c_code"])
        put_block(hdr, "DECLS", addr, ans["header_decl"])
        if row["kind"] == "safe":
            register(state, row, ans)
        answered.append(addr)

    errors = compile_check(state)
    if "file" in errors:
        print("file-level compile errors (outside the generated blocks):", errors["file"][:5])
    to_check = []
    for a in answered:
        row = state["rows"][a]
        if a in errors:
            feedback(state, a, "It does not compile:\n" + "\n".join(errors[a][:20]))
        else:
            to_check.append(a)
    facts = facts_check(state, to_check)
    for a in to_check:
        row = state["rows"][a]
        same, text = facts[a]
        if same:
            row["state"] = "ok"
        elif row["unchanged"] and row["answer"]["noise_explanations"].strip():
            row["state"] = "explained"
            row["log"].append("facts differ, explained:\n" + text)
        else:
            feedback(state, a, "It compiles. vc71match --facts --force-inline (ordered title calls and image "
                     "constants of your C compiled with the title's compiler vs the XBE) differs:\n" + text +
                     "\nFix real differences. If a difference is known noise (see the rules: inlined ScrField_* "
                     "accessors, a function address loaded once and called twice, duplicated branch tails, helper "
                     "argument order), return the same c_code and explain each in noise_explanations.")
    save(state)


def feedback(state, addr, text):
    row = state["rows"][addr]
    row["log"].append(text[:2000])
    if row["round_count"] >= state["max_rounds"]:
        row["state"] = "failed"
        return
    row["messages"].append({"role": "user", "content": text})
    row["state"] = "retry"


# ---------------------------------------------------------------------------------------------------- commands
def cmd_claim(args):
    rows = [l.split()[0] for l in open(args.file) if l.strip()]
    got = claims_update(rows, args.owner)
    print(f"{len(got)}/{len(rows)} claimed for {args.owner}")


def cmd_prepare(args):
    rundir = RUNS / args.run
    rundir.mkdir(parents=True, exist_ok=True)
    wt, raw = Path(args.worktree).resolve(), Path(args.raw).resolve()
    rows = [l.split() for l in open(args.rows) if l.strip()]
    owner = f"api:{args.run}"
    have_c = (wt / "game" / "code" / "functions.cpp").read_text().lower()
    free = set(claims_update([r[0] for r in rows if f"0x{r[0]}" not in have_c], owner))
    state = {"run": args.run, "worktree": str(wt), "module": args.module, "round": 0, "max_rounds": args.max_rounds,
             "batches": [], "rows": {}}
    for r in rows:
        a = r[0]
        if a not in free:
            continue
        row = {"address": a, "name": r[3] if len(r) > 3 else f"FUN_{a}", "module": args.module,
               "kind": r[-1] if r[-1] in ("safe", "switch") else "safe", "state": "pending", "round_count": 0,
               "usage": [], "log": []}
        row["messages"] = [{"role": "user", "content": first_prompt(row, wt, raw)}]
        state["rows"][a] = row
    save(state)
    print(f"{len(state['rows'])} rows prepared in {rundir} ({len(rows) - len(state['rows'])} skipped: C or claimed)")


def client_or_exit():
    try:
        import anthropic
    except ImportError:
        sys.exit("run with build/api_venv/bin/python (pip install anthropic there)")
    import os
    key_file = Path.home() / ".config" / "cod_bro" / "anthropic_api_key"
    if not os.environ.get("ANTHROPIC_API_KEY") and key_file.exists():
        return anthropic.Anthropic(api_key=key_file.read_text().strip())
    return anthropic.Anthropic()  # ANTHROPIC_API_KEY or an `ant auth login` profile


def cmd_dry(args):
    state = load(args.run)
    client = client_or_exit()
    system = system_prompt()
    a = next(iter(state["rows"]))
    req = request(state, a, system)["params"]
    n = client.messages.count_tokens(model=MODEL, system=req["system"], messages=req["messages"]).input_tokens
    sys_n = client.messages.count_tokens(model=MODEL, system=req["system"],
                                         messages=[{"role": "user", "content": "x"}]).input_tokens
    rows = len(state["rows"])
    per_row_in = n - sys_n
    est = (sys_n * PRICE["cache_write"] + (rows - 1) * sys_n * PRICE["cache_read"] + rows * per_row_in * PRICE["input"]
           + rows * 8000 * PRICE["output"]) / 1e6
    print(f"system prompt {sys_n} tokens, first row {per_row_in} tokens; {rows} rows: first round about "
          f"${est:.2f} if ~8k output tokens per row (thinking included) and the cache hits")


def cmd_run(args):
    state = load(args.run)
    client = client_or_exit()
    state["max_rounds"] = args.rounds
    for b in state["batches"]:  # a batch submitted before an interruption
        if any(state["rows"][a]["state"] == "submitted" for a in b["rows"]):
            wait(client, b["id"])
            collect(state, client, b["id"])
    while True:
        if args.budget is not None and total_cost(state) >= args.budget:
            print(f"budget reached (${total_cost(state):.2f})")
            break
        bid = submit(state, client)
        if not bid:
            break
        wait(client, bid)
        collect(state, client, bid)
        cmd_status(args, state)
    done = [a for a, r in state["rows"].items() if r["state"] in ("failed", "refused")]
    if done:
        for a in done:
            drop_block(module_files(state)[0], a)
            drop_block(module_files(state)[1], a)
            unregister(state, state["rows"][a])
        claims_release(done, f"api:{args.run}", "sessions")
        print(f"{len(done)} rows back to the session agents (claims owner 'sessions')")
    save(state)


def cmd_recheck(args):
    """Rewrite every answered row from the state and check it again (no API calls): fixes rows a harness
    problem had marked wrongly. Rows that now pass become ok; the others get the new check as feedback and
    one more round (state retry) unless --no-retry."""
    state = load(args.run)
    cpp, hdr = module_files(state)
    t = cpp.read_text()
    if "using bro_ai::slot;\n\n" in t and "namespace {\nusing bro_ai::pointer" not in t:
        t = t.replace("using bro_ai::pointer;\nusing bro_ai::slot;\n",
                      "namespace {\nusing bro_ai::pointer;\nusing bro_ai::slot;\n}  // namespace\n", 1)
        cpp.write_text(t)
    rows = [a for a, r in state["rows"].items() if r.get("answer") and r["state"] != "refused"]
    for a in rows:
        row, ans = state["rows"][a], state["rows"][a]["answer"]
        put_block(cpp, "BODIES", a, f"// {'0x' + a} (api_worker, {MODEL})\n" + ans["c_code"])
        put_block(hdr, "DECLS", a, ans["header_decl"])
        if row["kind"] == "safe":
            register(state, row, ans)
    errors = compile_check(state)
    ok_compile = [a for a in rows if a not in errors]
    facts = facts_check(state, ok_compile)
    claims_update(rows, f"api:{args.run}")
    for a in rows:
        row = state["rows"][a]
        if a in errors:
            text = "It does not compile:\n" + "\n".join(errors[a][:20])
            same = False
        else:
            same, text = facts[a]
        row["log"].append("recheck: " + ("facts same" if same else text[:2000]))
        if same:
            row["state"] = "ok"
        elif args.no_retry:
            row["state"] = "failed"
        else:
            row["messages"].append({"role": "user", "content": "Correction: the previous check result was a "
                                    "harness problem on our side, not your code. The real check of your last "
                                    "version:\n" + text + "\nFix real differences; for known noise return the "
                                    "same c_code and explain it in noise_explanations."})
            row["state"], row["round_count"] = "retry", min(row["round_count"], state["max_rounds"] - 1)
    save(state)
    cmd_status(args, state)


def cmd_status(args, state=None):
    state = state or load(args.run)
    counts = {}
    for a, r in sorted(state["rows"].items()):
        counts[r["state"]] = counts.get(r["state"], 0) + 1
        if args.__dict__.get("verbose"):
            print(f"{a} {r['state']:9} rounds {r['round_count']} ${sum(cost(u) for u in r['usage']):.3f} {r['name']}")
    print(f"round {state['round']}: " + ", ".join(f"{v} {k}" for k, v in sorted(counts.items())) +
          f"; cost ${total_cost(state):.2f}")


def main():
    p = argparse.ArgumentParser(usage=__doc__)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("claim"); s.add_argument("file"); s.add_argument("owner")
    s = sub.add_parser("prepare"); s.add_argument("run"); s.add_argument("--worktree", required=True)
    s.add_argument("--module", required=True); s.add_argument("--rows", required=True)
    s.add_argument("--raw", required=True); s.add_argument("--max-rounds", type=int, default=3)
    s = sub.add_parser("dry"); s.add_argument("run")
    s = sub.add_parser("run"); s.add_argument("run"); s.add_argument("--rounds", type=int, default=3)
    s.add_argument("--budget", type=float)
    s = sub.add_parser("status"); s.add_argument("run"); s.add_argument("-v", "--verbose", action="store_true")
    s = sub.add_parser("recheck"); s.add_argument("run"); s.add_argument("--no-retry", action="store_true")
    args = p.parse_args()
    {"claim": cmd_claim, "prepare": cmd_prepare, "dry": cmd_dry, "run": cmd_run, "status": cmd_status,
     "recheck": cmd_recheck}[args.cmd](args)


if __name__ == "__main__":
    main()
