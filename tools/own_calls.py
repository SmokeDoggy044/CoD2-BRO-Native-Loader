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
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENGINE = ROOT / "src/engine"
TABLE = ENGINE / "shared/functions.cpp"
ENTRY = re.compile(r'\{"(\w+)", 0x([0-9a-f]+), (?:0x[0-9a-f]+|0), code\((&?)([\w:]+)\), (\d+), '
                   r'Result::(\w+), Status::(\w+)\}')
CALL = re.compile(r"title_function<((?:[^<>]|<(?:[^<>]|<[^<>]*>)*>)*)>\(\s*(?:0x([0-9a-fA-F]{8})|(k\w+))\s*\)")
CONST = re.compile(r"constexpr (?:uint32_t|auto|std::uint32_t) (k\w+) = 0x([0-9a-fA-F]{8})u?;")
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
    ap.add_argument("--helpers", action="store_true", help="pass 2: address-taking call helpers")
    ap.add_argument("--values", action="store_true", help="pass 3: addresses of our C used as values")
    args = ap.parse_args()
    if args.helpers:
        return helpers_pass(args)
    if args.values:
        return values_pass(args)

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
        consts = {n: int(a, 16) for n, a in CONST.findall(text)}
        need = set()

        def sub(m):
            if m.group(3):
                if m.group(3) not in consts:
                    return m.group(0)
                a = consts[m.group(3)]
            else:
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



# ---- pass 2 (--helpers): helpers that take the function as a 32-bit address --------------------------
FN_PARAM = re.compile(r"^(function|fn|func|target|callee|address|addr|entry|code|routine)$")
DEF = re.compile(r"(?:\b(?:uint32_t|void|int32_t|bool|float|auto|uint8_t\s*\*|char\s*\*)\s+|\]\s*)(\w+)\s*\(([^()]*)\)\s*"
                 r"(?:const\s*)?(?:->\s*[\w:\*]+\s*)?\{|\bauto\s+(\w+)\s*=\s*\[[^\]]*\]\s*\(([^()]*)\)|"
                 r"XB_CDECL\s+(\w+)\s*\(([^()]*)\)\s*;")
SKIP_HELPER = re.compile(r"(?i)thread|functor|title_function|own_function|own_address|slot")
# helpers that store the address (thread functors, command records) or were not checked: values, not calls
STORES = {"start_on", "run_to_node", "Cmd_AddCommand", "Cmd_AddServerCommand", "method", "script_call",
          "construct_screen"}


def helper_defs(text):
    out = {}
    for m in DEF.finditer(text):
        name = m.group(1) or m.group(3) or m.group(5)
        params = m.group(2) if m.group(1) else m.group(4) if m.group(3) else m.group(6)
        if not name or SKIP_HELPER.search(name) or name in STORES:
            continue
        names = [re.sub(r"=.*", "", p).strip().split(" ")[-1].lstrip("*&") for p in params.split(",") if p.strip()]
        idx = [i for i, n in enumerate(names) if FN_PARAM.match(n)]
        if len(idx) == 1:
            out.setdefault(name, idx[0])
    return out


def split_args(text, start):
    """Arguments of the call whose '(' is at text[start]: [(begin, end)], end index after ')'."""
    depth, i, args, begin = 0, start, [], start + 1
    while i < len(text):
        c = text[i]
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                args.append((begin, i))
                return args, i + 1
        elif c == "," and depth == 1:
            args.append((begin, i))
            begin = i + 1
        elif c == '"':
            i = text.index('"', i + 1) if '"' in text[i + 1:] else len(text)
        i += 1
    return None, None


def helpers_pass(args):
    tab = table()
    names = {n.split("::")[-1] for _, n in tab.values()}
    decl = declarations(names | {n[:-len("_entry")] for n in names if n.endswith("_entry")})
    top = set(INCLUDE.findall(TABLE.read_text()))
    wrapper = {}
    for h in ENGINE.rglob("*.h"):
        if h.name in top:
            for inner in INCLUDE.findall(h.read_text()):
                if inner not in top:
                    wrapper.setdefault(inner, h.name)
    shared = {}
    for h in ENGINE.rglob("*.h"):
        for k, v in helper_defs(h.read_text()).items():
            shared.setdefault(k, v)
    stats = collections.Counter()
    per_helper = collections.Counter()
    for path in sorted(ENGINE.rglob("*")):
        if path.suffix not in (".cpp", ".h") or path == TABLE:
            continue
        if args.only and args.only not in str(path):
            continue
        text = path.read_text()
        local = dict(shared)
        local.update(helper_defs(text))
        if not local:
            continue
        call = re.compile(r"\b(" + "|".join(map(re.escape, sorted(local, key=len, reverse=True))) + r")\s*\(")
        includes = set(INCLUDE.findall(text))
        need = set()
        edits = []
        for m in call.finditer(text):
            line_start = text.rfind("\n", 0, m.start()) + 1
            line = text[line_start:m.start()]
            if "//" in line or re.search(r"(uint32_t|void|int32_t|bool|auto|float)\s+$|\]\s*$", line):
                continue  # a comment or the definition itself
            spans, _ = split_args(text, m.end() - 1)
            idx = local[m.group(1)]
            if not spans or idx >= len(spans):
                continue
            b, e = spans[idx]
            arg = text[b:e].strip()
            lit = re.fullmatch(r"0x([0-9a-fA-F]{8})u?", arg)
            if not lit or int(lit.group(1), 16) not in tab:
                continue
            amp, name = tab[int(lit.group(1), 16)]
            base = name.split("::")[-1]
            headers = decl.get(base) or (decl.get(base[:-len("_entry")], set()) if base.endswith("_entry") else set())
            headers = {wrapper.get(h, h) for h in headers}
            if not headers:
                stats["no declaration"] += 1
                continue
            if path.name not in headers and path.name.replace(".cpp", ".h") not in headers and not headers & includes:
                need.add(sorted(headers)[0])
            lead = text[b:e][:len(text[b:e]) - len(text[b:e].lstrip())]
            edits.append((b, e, f"{lead}own_address({amp}{name})"))
            per_helper[m.group(1)] += 1
        if not edits:
            continue
        for b, e, new in sorted(edits, reverse=True):
            text = text[:b] + new + text[e:]
        need -= {path.name}
        if need:
            ns = re.search(r"^\s*(namespace\b|extern \"C\" \{)", text, re.M)
            last = [m for m in INCLUDE.finditer(text) if not ns or m.start() < ns.start()]
            at = last[-1].end() if last else 0
            text = text[:at] + "".join(f'\n#include "{h}"' for h in sorted(need)) + text[at:]
        stats["rewritten"] += len(edits)
        stats["files"] += 1
        if not args.dry_run:
            path.write_text(text)
    print(dict(stats))
    print(" ".join(f"{k}:{v}" for k, v in per_helper.most_common()))


# callees whose argument only looks like a function address: a size that equals one (Heap_CreateFromZone
# 0x0004b000) and an identity compared inside (sample_root's `base`, 2026-11-07 review)
KEEP_CALLEES = re.compile(r"\b(Heap_CreateFromZone|sample_root)\s*\([^()]*$")


# ---- pass 3 (--values): addresses of our C used as values (slot tables, comparators, thread entries) ---
def values_pass(args):
    tab = table()
    names = {n.split("::")[-1] for _, n in tab.values()}
    decl = declarations(names | {n[:-len("_entry")] for n in names if n.endswith("_entry")})
    top = set(INCLUDE.findall(TABLE.read_text()))
    wrapper = {}
    for h in ENGINE.rglob("*.h"):
        if h.name in top:
            for inner in INCLUDE.findall(h.read_text()):
                if inner not in top:
                    wrapper.setdefault(inner, h.name)
    lit = re.compile(r"0x(00[0-9a-fA-F]{6})u?\b")
    stats = collections.Counter()
    for path in sorted(ENGINE.rglob("*")):
        if path.suffix not in (".cpp", ".h") or path == TABLE or path.parent.name == "shared":
            continue
        if args.only and args.only not in str(path):
            continue
        text = path.read_text()
        if "Generated by" in text[:400]:
            continue  # generator output (template arguments): change the generator instead
        includes = set(INCLUDE.findall(text))
        need = set()
        out = []
        for line in text.split("\n"):
            code, sep, comment = line.partition("//")
            if (not lit.search(code) or "constexpr" in code or '"' in code or code.lstrip().startswith("#")
                    or "static_assert" in code):
                out.append(line)
                continue

            def sub(m):
                a = int(m.group(1), 16)
                if a not in tab:
                    return m.group(0)
                before, after = code[:m.start()].rstrip(), code[m.end():].lstrip()
                if (before.endswith(("==", "!=", "case", "<", "<=", ">", ">=")) or after.startswith(("==", "!=", ">", "<"))
                        or re.search(r"(title_global|text_at|title_function|slot)\s*<[^>]*>?\s*\(\s*$", before)
                        or re.search(r"\b(title_global|text_at|text|title_mm)\s*\(\s*$", before)
                        or KEEP_CALLEES.search(before)):
                    stats["skipped (compare/data)"] += 1
                    return m.group(0)
                amp, name = tab[a]
                base = name.split("::")[-1]
                headers = decl.get(base) or (decl.get(base[:-len("_entry")], set()) if base.endswith("_entry") else set())
                headers = {wrapper.get(h, h) for h in headers}
                if not headers:
                    stats["no declaration"] += 1
                    return m.group(0)
                if path.name not in headers and path.name.replace(".cpp", ".h") not in headers and not headers & includes:
                    need.add(sorted(headers)[0])
                stats["rewritten"] += 1
                return f"own_address({amp}{name})"

            code = lit.sub(sub, code)
            out.append(code + sep + comment)
        new = "\n".join(out)
        if new == text:
            continue
        need -= {path.name}
        if need:
            ns = re.search(r"^\s*(namespace\b|extern \"C\" \{)", new, re.M)
            last = [m for m in INCLUDE.finditer(new) if not ns or m.start() < ns.start()]
            at = last[-1].end() if last else 0
            new = new[:at] + "".join(f'\n#include "{h}"' for h in sorted(need)) + new[at:]
        stats["files"] += 1
        if not args.dry_run:
            path.write_text(new)
    print(dict(stats))


if __name__ == "__main__":
    main()
