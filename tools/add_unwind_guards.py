#!/usr/bin/env python3
"""Adds ScrStringGuard (src/engine/script/scr_unwind_guard.h) to our C functions that hold ScriptString locals
across a script wait, so an ended script thread releases them (issue #32: the small heap ran out).

Per function that may wait (directly: Scr_Wait* / Scr_Suspend* / wait helpers / a level-section title call /
a call commented as a wait; or by calling such a function of ours, by name):
  * `ScriptString a, b[4];` becomes `ScriptString a{}, b[4]{};` followed by `ScrStringGuard guard_a(&a, b);`
  * by-value ScriptString parameters (the callee owns them, as with MSVC) get a guard at the top of the body
  * releases of guarded strings (ScriptString_Release, scr_release, own_function<...>(&ScriptString_Release_entry))
    become scr_drop, which also empties them, so the unwind never releases one twice.
Strings used by value (copied bitwise into a call or another variable) are left alone and listed for review, as
are releases the script cannot read.

usage: tools/add_unwind_guards.py [--write] [FILES...]   (default: every src/engine .cpp; dry run without --write)
"""
import glob
import re
import sys

WAIT = re.compile(r"\bScr_(?:Wait|Suspend)\w*\s*\(|\bWaitTill\w*|\bwait_\w*\s*\(|//[^\n]*\bWait|Scr_WaitRecord"
                  r"|title_function<[^;]*?>\(0x00(?:4[1-9a-f]|[5-7][0-9a-f])[0-9a-f]{4}\)")
DECL = re.compile(r"^(?P<indent>[ \t]+)ScriptString\s+(?P<names>[A-Za-z_]\w*(?:\[[^\]]+\])?"
                  r"(?:\s*,\s*[A-Za-z_]\w*(?:\[[^\]]+\])?)*)\s*;(?P<comment>\s*//.*)?$")
RELEASE_CALLEE = re.compile(r"\bScriptString_Release\s*\(|\bscr_release\s*\(|"
                            r"own_function<[^;]*?>\(&ScriptString_Release_entry\)\s*\(|"
                            r"title_function<[^;]*?>\(0x0011d640\)\s*\(")
INCLUDE = '#include "scr_unwind_guard.h"\n'


def strip_code(text):
    """Comments and string/char literals blanked out (same length), for brace matching."""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            for k in range(i, j): out[k] = " "
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if out[k] != "\n": out[k] = " "
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            for k in range(i + 1, min(j, n)): out[k] = " "
            i = j + 1
        else:
            i += 1
    return "".join(out)


def functions(text):
    """(name, params, body_start, body_end) of top-level function definitions; body_* index the braces."""
    code = strip_code(text)
    result, depth, stmt_start, i = [], 0, 0, 0
    while i < len(code):
        c = code[i]
        if c == "{":
            if depth == 0:
                head = code[stmt_start:i]
                if "(" in head and not re.search(r"\b(namespace|struct|class|enum|union)\b|=\s*$", head):
                    end = match(code, i)
                    m = re.search(r"([~\w]+)\s*\(", head)
                    p0 = head.find("(")
                    p1 = match(head, p0, "(", ")")
                    result.append((m.group(1) if m else "?", head[p0 + 1:p1], i, end))
                    i = end + 1
                    stmt_start = i
                    continue
                if re.search(r"\bnamespace\b|extern\s*$", head):  # namespace bodies stay at depth 0
                    stmt_start = i + 1
                    i += 1
                    continue
            depth += 1
        elif c == "}":
            depth = max(0, depth - 1)
            if depth == 0: stmt_start = i + 1
        elif c == ";" and depth == 0:
            stmt_start = i + 1
        i += 1
    return result


def match(code, i, open_c="{", close_c="}"):
    depth = 0
    for j in range(i, len(code)):
        if code[j] == open_c: depth += 1
        elif code[j] == close_c:
            depth -= 1
            if depth == 0: return j
    return len(code) - 1


def bare_uses(body, name):
    """Uses of `name` other than &name, &name[..], name.rep, sizeof(name): copies by value."""
    uses = []
    for m in re.finditer(r"\b%s\b" % re.escape(name), body):
        before = body[:m.start()].rstrip()
        after = body[m.end():]
        if before.endswith("&") and not before.endswith("&&"): continue
        if re.match(r"\s*\.\s*rep\b", after): continue
        if before.endswith("sizeof("): continue
        line_start = body.rfind("\n", 0, m.start()) + 1
        line = body[line_start:body.find("\n", m.start())]
        if re.match(r"\s*ScriptString\b", line) and re.search(r"\b%s\s*(\[[^\]]*\])?\s*[,;{]" % re.escape(name), line):
            continue  # its declaration
        uses.append(line.strip())
    return uses


def release_calls(body):
    """(start, end, argument) of each release call in body."""
    code = strip_code(body)
    for m in RELEASE_CALLEE.finditer(code):
        open_paren = m.end() - 1
        close = match(code, open_paren, "(", ")")
        yield m.start(), close + 1, body[open_paren + 1:close].strip()


def released_name(arg):
    m = re.fullmatch(r"(?:reinterpret_cast<[^>]*>\(\s*)?&\s*([A-Za-z_]\w*)\s*(\[[^\]]*\])?\s*\)?", arg)
    return (m.group(1), m.group(2) or "") if m else (None, None)


def main():
    write = "--write" in sys.argv
    files = [a for a in sys.argv[1:] if not a.startswith("--")] or sorted(glob.glob("src/engine/**/*.cpp", recursive=True))
    texts = {f: open(f).read() for f in files}
    funcs = {f: functions(t) for f, t in texts.items()}
    # Which functions may wait: direct waits, then callers of those (by name) until nothing changes.
    bodies = {}
    for f, fl in funcs.items():
        for name, params, b0, b1 in fl:
            bodies.setdefault(name, []).append(texts[f][b0:b1 + 1])
    waits = {n for n, bs in bodies.items() if any(WAIT.search(b) for b in bs)}
    # Synchronous calls only: name(...) and own_function<...>(&name)(...); a function pointer handed to a thread
    # start (Scr_BeginThread*) does not make the caller wait.
    def called(b):
        code = strip_code(b)
        return set(re.findall(r"(?<![&\w.>])([A-Za-z_]\w*)\s*\(", code)) | set(
            re.findall(r"own_function<[^;]*?>\(&([A-Za-z_]\w*)\)\s*\(", code))
    calls = {n: set().union(*(called(b) for b in bs)) for n, bs in bodies.items()}
    changed = True
    while changed:
        changed = False
        for n, cs in calls.items():
            if n not in waits and cs & waits:
                waits.add(n)
                changed = True

    review, stats = [], {"functions": 0, "strings": 0, "params": 0, "drops": 0}
    for f in files:
        text = texts[f]
        edits = []  # (start, end, replacement) in text
        for name, params, b0, b1 in funcs[f]:
            if name not in waits: continue
            body = text[b0:b1 + 1]
            guarded = set()
            touched = False
            # Declarations.
            pos = 0
            for line in body.splitlines(keepends=True):
                m = DECL.match(line.rstrip("\n"))
                if m:
                    items = [s.strip() for s in m.group("names").split(",")]
                    keep, skip = [], []
                    for item in items:
                        n = item.split("[")[0]
                        uses = bare_uses(body, n)
                        (skip if uses else keep).append(item)
                        if uses: review.append(f"{f}: {name}: '{n}' used by value: {uses[0][:100]}")
                    if keep:
                        decl_items = [it + "{}" if it.split("[")[0] in [k.split("[")[0] for k in keep] else it
                                      for it in items]
                        args = ", ".join(("" if "[" in k else "&") + k.split("[")[0] for k in keep)
                        ind = m.group("indent")
                        new = (f"{ind}ScriptString {', '.join(decl_items)};{m.group('comment') or ''}\n"
                               f"{ind}ScrStringGuard guard_{keep[0].split('[')[0]}({args});\n")
                        edits.append((b0 + pos, b0 + pos + len(line), new))
                        guarded.update(k.split("[")[0] for k in keep)
                        stats["strings"] += len(keep)
                        touched = True
                pos += len(line)
            # By-value parameters.
            pnames = []
            for p in params.split(","):
                pm = re.fullmatch(r"\s*(?:const\s+)?ScriptString\s+([A-Za-z_]\w*)\s*", p)
                if pm: pnames.append(pm.group(1))
            if pnames:
                already = set(re.findall(r"ScrStringGuard\s+\w+\(([^;]*)\);", body))
                guarded_args = {a.strip().lstrip("&") for g in already for a in g.split(",")}
                guarded.update(p for p in pnames if p in guarded_args)
                pnames = [p for p in pnames if p not in guarded_args]
                ok = [p for p in pnames if not bare_uses(body, p)]
                for p in pnames:
                    if p not in ok: review.append(f"{f}: {name}: parameter '{p}' used by value")
                if ok:
                    guard = f"ScrStringGuard guard_{ok[0]}({', '.join('&' + p for p in ok)});"
                    if body.startswith("{\n"):
                        ind = re.match(r"[ \t]*", body[2:]).group(0) or "    "
                        edits.append((b0 + 2, b0 + 2, f"{ind}{guard}\n"))
                    else:  # a body on one line
                        edits.append((b0 + 1, b0 + 1, f" {guard}"))
                    guarded.update(ok)
                    stats["params"] += len(ok)
                    touched = True
            # Releases of guarded strings.
            for s, e, arg in release_calls(body):
                n, index = released_name(arg)
                if n in guarded:
                    edits.append((b0 + s, b0 + e, f"scr_drop(&{n}{index})"))
                    stats["drops"] += 1
                elif any(re.search(r"\b%s\b" % re.escape(g), arg) for g in guarded):
                    review.append(f"{f}: {name}: release not rewritten: {arg[:100]}")
            if touched: stats["functions"] += 1
        if not edits: continue
        edits.sort(key=lambda x: (x[0], x[1]))
        out, last = [], 0
        for s, e, r in edits:
            if s < last: raise SystemExit(f"{f}: overlapping edits at {s}")
            out.append(text[last:s]); out.append(r); last = e
        out.append(text[last:])
        new_text = "".join(out)
        if INCLUDE not in new_text:
            first_code = re.search(r"^(namespace|[A-Za-z_][\w:<>* ]*\()", new_text, re.M)
            limit = first_code.start() if first_code else len(new_text)
            includes = [m for m in re.finditer(r'^#include "[^"]+"\n', new_text, re.M) if m.start() < limit]
            at = includes[-1].end() if includes else 0
            new_text = new_text[:at] + INCLUDE + new_text[at:]
        if write: open(f, "w").write(new_text)
    print(f"may-wait functions: {len(waits)}; guarded: {stats['functions']} functions, {stats['strings']} locals, "
          f"{stats['params']} parameters; releases -> scr_drop: {stats['drops']}")
    print(f"review ({len(review)}):")
    for r in review: print("  " + r)


if __name__ == "__main__":
    main()
