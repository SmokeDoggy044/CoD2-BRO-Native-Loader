#!/usr/bin/env python3
"""own_calls.py: turn calls our C makes through an original address into direct calls to our own C
(own engine host, plan 2026-11-07 step 0.2).

`title_function<F>(0xADDR)` reaches a function that already has C only through the hook jump the loader
installs at ADDR. This rewrites each such call site to `own_function<F>(&Name)`: the same signature F,
the same symbol the hook table registers for ADDR (src/engine/shared/functions.cpp, `code(&Name)`), so
the call is a direct `call Name` with exactly the convention it had. The header declaring Name is added
to the file's includes when missing.

Not touched: title_call_at* (runs the callee at the original's stack position on purpose), address
literals used as values (comparisons, tables), and targets without C.

usage: tools/own_calls.py [--dry-run] [--only DIR]   prints a summary; files listed with --verbose
"""
import argparse
import collections
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENGINE = ROOT / "src/engine"
TABLE = ENGINE / "shared/functions.cpp"
ENTRY = re.compile(r'\{"(\w+)", 0x([0-9a-f]+), (?:0x[0-9a-f]+|0), code\((&?)([\w:]+)\), (\d+), '
                   r'Result::(\w+), Status::(\w+)\}')
CALL = re.compile(r"title_function<((?:[^<>]|<(?:[^<>]|<[^<>]*>)*>)*)>\(\s*0x([0-9a-fA-F]{8})\s*\)")
INCLUDE = re.compile(r'^#include "([^"]+)"', re.M)


def table():
    out = {}
    for m in ENTRY.finditer(TABLE.read_text()):
        out[int(m.group(2), 16)] = (m.group(3), m.group(4))
    return out


def declarations(names):
    """name -> header (basename) declaring it; None when several headers do (resolved per file)."""
    found = collections.defaultdict(set)
    want = set(names)
    word = re.compile(r"\b(\w+)\s*\(")
    for h in ENGINE.rglob("*.h"):
        text = h.read_text(errors="replace")
        for line in text.splitlines():
            s = line.split("//")[0].strip()
            mm = re.match(r"[A-Z][A-Z0-9_]+\((\w+)\)", s)  # FXCMD_BUILDER(Name)-style declaring macros
            if mm and mm.group(1) in want:
                found[mm.group(1)].add(h.name)
                continue
            if not s or s.startswith(("//", "#", "*", "return", "if", "for", "while")):
                continue
            if not s.endswith((";", "{", ",")) and "(" not in s:
                continue
            for m in word.finditer(s):
                n = m.group(1)
                if n in want and m.start() > 0 and re.search(r"[\w\*&>]\s+$|\*$|\($", s[:m.start()]):
                    found[n].add(h.name)
                    break
            for m in re.finditer(r"\b(\w+)\s*=", s):
                if m.group(1) in want and s.startswith(("inline", "constexpr", "extern", "static")):
                    found[m.group(1)].add(h.name)
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--only")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    tab = table()
    # a header the hook table includes stands for the internal headers it pulls in (scr_field.h ->
    # scr_field_instances.h, which is not meant to be included on its own)
    top = set(INCLUDE.findall(TABLE.read_text()))
    wrapper = {}
    for h in ENGINE.rglob("*.h"):
        if h.name in top:
            for inner in INCLUDE.findall(h.read_text()):
                if inner not in top:
                    wrapper.setdefault(inner, h.name)
    names = {n.split("::")[-1] for _, n in tab.values()}
    decl = declarations(names | {n[:-len("_entry")] for n in names if n.endswith("_entry")})
    stats = collections.Counter()
    missing = collections.Counter()
    changed = []
    for path in sorted(ENGINE.rglob("*")):
        if path.suffix not in (".cpp", ".h") or path == TABLE:
            continue
        if args.only and args.only not in str(path):
            continue
        text = path.read_text()
        includes = set(INCLUDE.findall(text))
        need = set()

        def sub(m):
            a = int(m.group(2), 16)
            if a not in tab:
                stats["no C"] += 1
                return m.group(0)
            amp, name = tab[a]
            base = name.split("::")[-1]
            line = text[text.rfind("\n", 0, m.start()) + 1:m.start()]
            if re.search(rf"\b{re.escape(base)}\b", m.group(1)) or re.search(rf"\b{re.escape(base)}\s*=\s*$", line):
                name = "game::" + name  # a signature alias or local shares the function's name
            headers = decl.get(base) or (decl.get(base[:-len("_entry")], set()) if base.endswith("_entry") else set())
            headers = {wrapper.get(h, h) for h in headers}
            if path.suffix == ".h" and path.name in headers or path.name.replace(".cpp", ".h") in headers:
                pass
            elif headers & includes:
                pass
            elif len(headers) == 1:
                need.add(next(iter(headers)))
            elif headers:
                need.add(sorted(headers)[0])
            else:
                missing[name] += 1
                stats["no declaration"] += 1
                return m.group(0)
            stats["rewritten"] += 1
            return f"own_function<{m.group(1)}>({amp}{name})"

        new = CALL.sub(sub, text)
        if new == text:
            continue
        need -= {path.name}
        if need:
            ns = re.search(r"^\s*(namespace\b|extern \"C\" \{)", new, re.M)  # stay outside namespace blocks
            last = [m for m in INCLUDE.finditer(new) if not ns or m.start() < ns.start()]
            at = last[-1].end() if last else 0
            new = new[:at] + "".join(f'\n#include "{h}"' for h in sorted(need)) + new[at:]
            stats["includes added"] += len(need)
        changed.append(path)
        if not args.dry_run:
            path.write_text(new)
    for k, v in stats.items():
        print(f"{k}: {v}")
    print(f"files: {len(changed)}")
    if missing:
        print("no declaration found:", " ".join(f"{n}({c})" for n, c in missing.most_common(40)))
    if args.verbose:
        for p in changed:
            print(p.relative_to(ROOT))


if __name__ == "__main__":
    main()
