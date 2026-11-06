#!/usr/bin/env python3
"""Generates C for level-section copies of engine template code that differ only in addresses.

Many engine functions are one-line calls into a shared body with the instance's addresses as literals, e.g.
    uint8_t* XB_THISCALL HashMap_Insert_2(HashMap* map, ...) { return hash_map_insert<uint32_t>(map, ..., 0x0017dc30, ...); }
A level row is a near twin of such an engine function F when the masked disassembly is equal
(build/all_body_hashes.csv "masked"). Every address literal in F's body is located in F's disassembly (all positions
holding that value); the level copy's values at those positions replace it. Every other differing operand must be
a call/branch target that is the same offset inside the function or an exact twin (tools/twin_register.py rule),
otherwise the row is skipped. Output:
  game/code/<file>_levels.inc   the wrappers, included at the end of F's source file <file>.cpp
  game/code/levels_twins.h      their declarations (functions.cpp includes it)
  functions.cpp entries (Status::Compare), functions.csv zero rows, manual_names.csv names for FUN_ rows

usage: tools/near_twins.py [--module prologue] [--dry-run] [--why]
  --why  print why each candidate was skipped
Run tools/levelsdup.py first. Rerun-safe: rows already in kFunctions are skipped; generated files are appended.
"""
import argparse
import csv
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import fnpack  # noqa: E402

ROOT = fnpack.ROOT
CODE = ROOT / "game" / "code"
FUNCTIONS_CPP = CODE / "functions.cpp"
FUNCTIONS_CSV = fnpack.DATA / "functions.csv"
MANUAL = fnpack.DATA / "manual_names.csv"
HEADER = CODE / "levels_twins.h"
HEX = re.compile(r"0x[0-9a-f]{5,}")
LIT = re.compile(r"0x[0-9a-fA-F]{5,8}u?\b")
BRANCH = re.compile(r"^(call|j\w+) 0x([0-9a-f]+)$")
ENTRY = (r'^    \{"(\w+)", (0x[0-9a-fA-F]+), (0x[0-9a-fA-F]+|0), (code\([^)]*\)), (\d+), (Result::\w+), '
         r'Status::(\w+)\},')


def definitions():
    """name -> (file, header text before '{', body text) for short free functions in game/code/*.cpp."""
    out = {}
    for path in sorted(CODE.glob("*.cpp")):
        text = path.read_text()
        for m in re.finditer(r"^(?![ /#}])[^\n;{}]*\bXB_\w+\s+(\w+)\(([^;{}]*?)\)\s*\{", text, re.M):
            start = m.end()
            depth, i = 1, start
            while depth and i < len(text):
                depth += {"{": 1, "}": -1}.get(text[i], 0)
                i += 1
            body = text[start:i - 1]
            if body.count("\n") <= 30:  # the operand check below is what makes a copy safe, not the length
                out.setdefault(m.group(1), (path, text[m.start():m.end() - 1].rstrip(), body))
    return out


NAMES = None


CONSTS = {}
# (engine callee, engine function the level callee is an exact twin of) -> rewrite of the wrapper body:
# the engine helper calls the first through C, a parameter selects the second.
CALL_SWAPS = {
    (0x0017cb40, 0x001e6330): ("hash_map_index_map(", lambda b: b[:b.rindex(");")] + ", hash_map_find_node_signed);"
                               + b[b.rindex(");") + 2:]),  # HashMap_Find -> HashMap_Find_2
    (0x000428d0, 0x002f5e10): ("", lambda b: re.sub(r"\b(list_clear|vector_tidy4)\((\w+)\)", r"\1(\2, true)", b)),  # Z_Free -> Scr_Free
}


def expand_constants(path, body):
    """kName of a `constexpr Type kName{...};` in the same file -> `Type{...}`, so its literals can be replaced."""
    if path not in CONSTS:
        CONSTS[path] = {m.group(2): f"{m.group(1)}{{{m.group(3)}}}" for m in
                        re.finditer(r"^constexpr (\w+) (k\w+)\{([^{}]*)\};", path.read_text(), re.M)}
    consts = CONSTS[path]
    return re.sub(r"\bk\w+\b", lambda m: consts.get(m.group(0), m.group(0)), body)


def rename_comments(body, sub):
    """A trailing // comment on a line with a replaced literal named the engine callee: name the new ones."""
    global NAMES
    if NAMES is None:
        NAMES = fnpack.load_names()[0]
    out = []
    for line in body.split("\n"):
        code, sep, _ = line.partition("//")
        hits = [sub[l] for l in LIT.findall(code) if l in sub]
        if sep and hits:
            line = code + "// " + ", ".join(NAMES.get(v, f"{v:#010x}") for v in hits)
        out.append(line)
    return "\n".join(out)


def main():
    parser = argparse.ArgumentParser(usage=__doc__)
    parser.add_argument("--module", default="prologue")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--why", action="store_true")
    args = parser.parse_args()

    rows = list(csv.DictReader(open(ROOT / "build" / "all_body_hashes.csv")))
    by_addr = {int(r["address"], 16): r for r in rows}
    image = fnpack.Image()
    text = FUNCTIONS_CPP.read_text()
    entries = {int(m.group(2), 16): m for m in re.finditer(ENTRY, text, re.M)}
    known_names = {m.group(1) for m in entries.values()}
    defs = definitions()
    engine = {}
    for a, r in by_addr.items():
        if r["section"] != "levels" and r["state"] == "replaced" and a in entries and int(r["size"]) >= 8:
            engine.setdefault(r["masked"], []).append(a)

    cache = {}

    def dis(va):
        if va not in cache:
            cache[va] = [t for _, _, t in fnpack.disassemble(image, va, int(by_addr[va]["size"]))]
        return cache[va]

    def catch_lines(va):
        """Indices of instructions in catch(...) { ...; throw; } blocks (rethrow = push 0, push 0, call
        _CxxThrowException); the C code leaves these unwind paths out."""
        lines, out = dis(va), set()
        for i in range(2, len(lines)):
            if lines[i] == "call 0x1f2302" and lines[i - 1] == lines[i - 2] == "push 0x0":
                j = i
                while j > 0 and not lines[j - 1].startswith(("jmp ", "ret")):
                    j -= 1
                out.update(range(j, i + 1))
        return out

    def operands(va):
        """[(value, is_branch, in_catch)] for every hex operand of 5+ digits, in order."""
        out, previous = [], ""
        catch = catch_lines(va)
        for n, t in enumerate(dis(va)):
            if previous == "push 0xffffffff" and t.startswith("push 0x"):
                t = "push EH"  # SEH handler address: each copy has its own, C code has none
            previous = t
            b = BRANCH.match(t)
            for h in HEX.findall(t):
                out.append((int(h, 16), bool(b), n in catch))
        return out

    memo = {}

    def same(a, b, depth=0):
        if a == b:
            return True
        if (a, b) in memo:
            return memo[(a, b)]
        ra, rb = by_addr.get(a), by_addr.get(b)
        if not ra or not rb or ra["exact"] != rb["exact"] or depth > 12:
            return False
        memo[(a, b)] = True
        ta, tb = dis(a), dis(b)
        ok = True
        for x, y in zip(ta, tb):
            mx, my = BRANCH.match(x), BRANCH.match(y)
            if mx and my:
                u, v = int(mx.group(2), 16), int(my.group(2), 16)
                inside = a <= u < a + int(ra["size"]) and b <= v < b + int(rb["size"]) and u - a == v - b
                ok = u == v or inside or same(u, v, depth + 1)
                if not ok:
                    break
        memo[(a, b)] = ok
        return ok

    def try_pair(x, f):
        """Wrapper body for level row x from engine function f, or (None, reason)."""
        m = entries[f]
        name = m.group(1)
        if name not in defs:
            return None, f"{name}: no short C definition"
        path, head, body = defs[name]
        args_entry = m.group(4) == f"code(&{name}_entry)" and f"SCRIPT_ARGS_ENTRY({name})\n" in path.read_text()
        if m.group(4) not in (f"code(&{name})", f"code({name})") and not args_entry:
            return None, f"{name}: asm entry {m.group(4)}"
        plain = set(LIT.findall(body))
        body = expand_constants(path, body)
        of, ox = operands(f), operands(x)
        if len(of) != len(ox):
            return None, f"{name}: operand count"
        sub = {}
        for lit in set(LIT.findall(body)):
            v = int(lit.rstrip("u"), 16)
            new = {ox[i][0] for i, (val, _, _) in enumerate(of) if val == v}
            if not new and lit not in plain:
                new = {0}  # a field of an expanded instance constant that this body never calls
            if not new:
                return None, f"{name}: literal {lit} not in the disassembly"
            if len(new) != 1:
                return None, f"{name}: literal {lit} maps to {len(new)} values"
            sub[lit] = new.pop()
        covered = {v for v in (int(k.rstrip("u"), 16) for k in sub)}
        zeroed = [k for k, v in sub.items() if v == 0]
        x_start, f_start = x, f
        swaps = []
        for (vf, br, eh), (vx, _, eh_x) in zip(of, ox):
            if vf == vx or vf in covered:
                continue
            if br and eh and eh_x:
                continue  # catch block: not in the C code
            swap = next((v for (cf, cg), v in CALL_SWAPS.items() if br and vf == cf and same(vx, cg)), None)
            if swap and swap[0] in body:
                swaps.append(swap[1])
                continue
            if br and ((f_start <= vf < f_start + int(by_addr[f]["size"]) and vx - x_start == vf - f_start)
                       or same(vx, vf)):
                continue
            return None, f"{name}: operand {vf:#x} -> {vx:#x} not a literal or twin"
        new_body = LIT.sub(lambda mm: ("0" if mm.group(0) in zeroed else f"{sub[mm.group(0)]:#010x}") + ("u" if mm.group(0).endswith("u") else "")
                           if mm.group(0) in sub else mm.group(0), body)
        for swap in dict.fromkeys(swaps):
            new_body = swap(new_body)
        return (path, head, rename_comments(new_body, sub), m, args_entry), None

    module = args.module.upper()
    have_names = {line.split(",", 1)[0] for line in open(MANUAL)}
    made, skipped = [], {}
    for a, r in sorted(by_addr.items()):
        if r["module"] != args.module or r["section"] != "levels" or a in entries:
            continue
        reasons = []
        for f in engine.get(r["masked"], []):
            got, why = try_pair(a, f)
            if got:
                made.append((a, r, got))
                break
            reasons.append(why)
        else:
            if reasons:
                skipped[a] = reasons
    if args.why:
        for a, reasons in skipped.items():
            print(f"skip {a:08x} {by_addr[a]['name']}: {reasons[0]}" + (f" (+{len(reasons) - 1})" if len(reasons) > 1 else ""))

    incs, decls, new_entries, new_csv, new_names = {}, [], [], [], []
    for a, r, (path, head, body, m, args_entry) in made:
        name = r["name"]
        if name.startswith("FUN_") or name in known_names or name in defs:
            base = f"{m.group(1)}_{module}"
            name, k = base, 2
            while name in known_names or name in defs:
                name, k = f"{base}_{k}", k + 1
            if f"{a:08x}" not in have_names:
                new_names.append(f'{a:08x},{name},function,high,"copy of {m.group(1)} ({int(m.group(2), 16):#010x}) '
                                 f'with this level\'s callees (tools/near_twins.py)"')
        known_names.add(name)
        new_head = re.sub(rf"\b{m.group(1)}\(", f"{name}(", head, count=1)
        if args_entry:  # SCRIPT_ARGS_ENTRY shim: it calls the C symbol, so the body needs C linkage
            incs.setdefault(path, []).append(f"// {a:#010x}, copy of {m.group(1)}\n"
                                             f"extern \"C\" PLATFORM_HIDDEN {new_head};\n"
                                             f"SCRIPT_ARGS_ENTRY({name})\n{new_head} {{{body}}}\n")
            decls.append((path, f'extern "C" void {name}_entry(void);'))
        else:
            incs.setdefault(path, []).append(f"// {a:#010x}, copy of {m.group(1)}\n{new_head} {{{body}}}\n")
            decls.append((path, new_head + ";"))
        entry = f"code(&{name}_entry)" if args_entry else f"code(&{name})"
        new_entries.append((a, f'    {{"{name}", {a:#010x}, 0, {entry}, {m.group(5)}, {m.group(6)}, '
                               f'Status::Compare}},  // near twin of {m.group(1)}'))
        new_csv.append(f'{a:08x},{name},compare,0,0,0,0,0,0,""')
        print(f"{a:08x} {name:50} ~ {m.group(1)}")
    print(f"{len(made)} near twins in {args.module}, {len(skipped)} skipped")
    if args.dry_run or not made:
        return

    for path, items in incs.items():
        inc = path.with_name(path.stem + "_levels.inc")
        if not inc.exists():
            inc.write_text("// Generated by tools/near_twins.py - level-section copies of this file's template code.\n"
                           "// Included at the end of " + path.name + " (inside namespace game).\n\n")
            src = path.read_text()
            at = src.rstrip().rfind("}  // namespace game")
            if at < 0:
                sys.exit(f"{path.name}: no closing '}}  // namespace game'")
            path.write_text(src[:at] + f'#include "{inc.name}"\n\n' + src[at:])
        with open(inc, "a") as f:
            f.write("\n".join(items))
    if not HEADER.exists():
        HEADER.write_text("#pragma once\n\n// Generated by tools/near_twins.py - declarations of the *_levels.inc "
                          "wrappers.\n\n// includes\n\nnamespace game {\n\n}  // namespace game\n")
    h = HEADER.read_text()
    for path, _ in decls:
        hdr = path.with_suffix(".h")
        line = f'#include "{hdr.name}"'
        if hdr.exists() and line not in h:
            h = h.replace("// includes\n", f"// includes\n{line}\n", 1)
    h = h.replace("\n}  // namespace game", "".join(f"\n{d}" for _, d in decls) + "\n\n}  // namespace game", 1)
    HEADER.write_text(h)

    if '#include "levels_twins.h"' not in text:
        text = text.replace('#include "scr_class.h"', '#include "levels_twins.h"\n#include "scr_class.h"', 1)
    lines = text.split("\n")
    for a, line in new_entries:
        idx = [(i, int(mm.group(1), 16)) for i, l in enumerate(lines)
               for mm in [re.match(r'\s+\{"\w+", 0x([0-9a-fA-F]+),', l)] if mm]
        at = next((i for i, x in idx if x > a), idx[-1][0] + 1)
        lines.insert(at, line)
    FUNCTIONS_CPP.write_text("\n".join(lines))
    csv_lines = FUNCTIONS_CSV.read_text().split("\n")
    head, body = csv_lines[0], [l for l in csv_lines[1:] if l]
    have = {l.split(",", 1)[0] for l in body}
    body = sorted(body + [l for l in new_csv if l.split(",", 1)[0] not in have], key=lambda l: l.split(",", 1)[0])
    FUNCTIONS_CSV.write_text("\n".join([head] + body) + "\n")
    if new_names:
        with open(MANUAL, "a") as f:
            f.write("\n".join(new_names) + "\n")


if __name__ == "__main__":
    main()
