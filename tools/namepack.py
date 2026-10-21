#!/usr/bin/env python3
"""Naming packets: everything needed to name a function in one compact text, from
the export of tools/ghidra/ExportNaming.java (build/naming/) and our name lists.

usage: tools/namepack.py rank [--module M] [--range LO-HI] [-n N]
       tools/namepack.py pack FUNCTION... [--lines N]
       tools/namepack.py next N [--module M] [--range LO-HI] [--lines N]
       tools/namepack.py fields STRUCT [--unknown] [-n N]
       tools/namepack.py status

  rank    unnamed functions, easiest first (score and why: strings, named
          callers/callees, Q3 proposal, struct fields, cvars)
  pack    the packet: callers, callees, strings, cvars, globals, struct fields
          touched, Q3 proposal, and the decompile with current names substituted
  next    packets for the N best-ranked unnamed functions
  fields  a struct's field usage: per offset its name (or ?), reads/writes, and
          the functions touching it, named ones first - names unknown fields and
          shows where the struct's users are
  status  how much of the program the export covers, and how old it is

The export is a snapshot: names come from the current symbol file and name lists,
but rerun ExportNaming over a range after struct changes (tools/data/structs.txt,
tools/ghidra/ApplyStructs.java) or big renames.
"""
import argparse
import collections
import csv
import re
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "tools" / "data"
EXPORT = ROOT / "build" / "naming"
DECL = re.compile(r"^(undefined\d?|u?int|u?short|u?char|byte|bool|float\d*|u?longlong|code|void|double|"
                  r"[A-Za-z_]\w*_t)\s*\**\s*[A-Za-z_]\w*(\s*\[\d+\])?;$")
TOKEN = re.compile(r"\b(FUN|DAT|PTR_DAT|PTR_FUN|LAB)_([0-9a-f]{8})\b")


class Program:
    def __init__(self):
        self.names = {}
        for line in open(DATA / "default_xbe_symbols.txt"):
            parts = line.split()
            if len(parts) >= 2 and not line.startswith("#"):
                self.names[int(parts[0], 16)] = parts[1]
        self.data = {}
        self.cvars = set()
        for file, is_cvar in (("manual_names.csv", False), ("cvar_globals.csv", True)):
            with open(DATA / file, newline="") as f:
                for row in csv.DictReader(f):
                    if row["kind"] == "data":
                        va = int(row["address"], 16)
                        self.data[va] = row["name"]
                        if is_cvar:
                            self.cvars.add(va)
        self.q3 = {}
        with open(DATA / "q3_name_proposals.csv", newline="") as f:
            for row in csv.DictReader(f):
                self.q3[int(row["address"], 16)] = row
        self.module = {}
        with open(DATA / "progress.csv", newline="") as f:
            for row in csv.DictReader(f):
                self.module[int(row["address"], 16)] = row["module"]
        self.meta = {}
        self.callers = collections.defaultdict(list)
        # Ranges may overlap (a part exported again): the newest file wins per function.
        by_age = lambda d: sorted((EXPORT / d).glob("*.tsv"), key=lambda f: f.stat().st_mtime)
        for path in by_age("meta"):
            for line in open(path, encoding="utf-8", errors="replace"):
                cols = line.rstrip("\n").split("\t")
                va = int(cols[0], 16)
                callees = [int(c, 16) for c in cols[2].split(",") if c]
                self.meta[va] = (int(cols[1]), callees, [s for s in cols[3].split("\x1f") if s],
                                 [int(d, 16) for d in cols[4].split(",") if d])
        for va, (_, callees, _, _) in self.meta.items():
            for c in callees:
                self.callers[c].append(va)
        self.fields = collections.defaultdict(list)  # function -> [(struct, offset, access, count)]
        for path in by_age("fields"):
            rows = collections.defaultdict(list)
            for line in open(path):
                va, struct, offset, access, count = line.split()
                rows[int(va, 16)].append((struct, int(offset, 16), access, int(count)))
            self.fields.update(rows)
        self.structs = load_structs()

    def name(self, va):
        return self.names.get(va) or self.data.get(va) or f"{va:08x}"

    def unnamed(self, va):
        return self.names.get(va, "FUN_").startswith("FUN_")

    def field_name(self, struct, offset):
        name = self.structs.get(struct, {}).get(offset)
        return f"{struct}.{name}" if name else f"{struct}+{offset:#x}"

    def substitute(self, text):
        def repl(m):
            va = int(m.group(2), 16)
            name = self.names.get(va) or self.data.get(va)
            return name if name and not name.startswith(("FUN_", "DAT_")) else m.group(0)
        return TOKEN.sub(repl, text)


def load_structs():
    structs, current = {}, None
    for raw in open(DATA / "structs.txt"):
        t = raw.split("#")[0].split()
        if not t:
            continue
        if t[0] == "struct":
            current = structs.setdefault(t[1], {})
        elif t[0] in ("global", "globals", "param", "return"):
            current = None
        elif current is not None:
            current[int(t[0], 16)] = t[1]
    return structs


def score(p, va):
    size, callees, strings, data = p.meta[va]
    named_callers = sum(1 for c in set(p.callers[va]) if not p.unnamed(c))
    named_callees = sum(1 for c in set(callees) if not p.unnamed(c))
    cvars = sum(1 for d in set(data) if d in p.cvars)
    known_fields = sum(1 for s, o, _, _ in p.fields.get(va, []) if o in p.structs.get(s, {}))
    parts = [("str", 2 * min(3, len(strings))), ("q3", 4 if va in p.q3 else 0),
             ("callers", 1.5 * min(4, named_callers)), ("callees", 0.5 * min(6, named_callees)),
             ("fields", min(3, known_fields)), ("cvars", min(2, cvars)),
             ("big", -2 if size > 1500 else 0)]
    return sum(v for _, v in parts), " ".join(f"{k}{v:+g}" for k, v in parts if v)


def candidates(p, args):
    lo, hi = 0, 1 << 32
    if args.range:
        lo, hi = (int(x, 16) for x in args.range.split("-"))
    out = []
    for va in p.meta:
        if not p.unnamed(va) or not lo <= va < hi:
            continue
        if args.module and p.module.get(va) != args.module:
            continue
        s, why = score(p, va)
        out.append((s, va, why))
    out.sort(key=lambda x: (-x[0], x[1]))
    return out


def clip(items, n):
    items = list(items)
    return ", ".join(items[:n]) + (f", ... (+{len(items) - n})" if len(items) > n else "")


def pack(p, va, lines):
    if va not in p.meta:
        return f"== {p.name(va)} {va:08x}: not in the export (rerun ExportNaming over its range)"
    size, callees, strings, data = p.meta[va]
    s, why = score(p, va)
    callers = sorted(set(p.callers[va]), key=lambda c: (p.unnamed(c), c))
    out = [f"== {p.name(va)} {va:08x} {size}B module:{p.module.get(va, '?')} score {s:g} ({why})"]
    out.append(f"callers({len(callers)}): {clip((p.name(c) for c in callers), 10)}")
    uniq = list(dict.fromkeys(callees))
    uniq.sort(key=lambda c: p.unnamed(c))
    out.append(f"callees({len(uniq)}): {clip((p.name(c) for c in uniq), 14)}")
    if strings:
        out.append("strings: " + clip((repr(x[:70]) for x in strings), 8))
    cvars = [p.data[d] for d in dict.fromkeys(data) if d in p.cvars]
    if cvars:
        out.append("cvars: " + clip(cvars, 10))
    globs = [p.name(d) for d in dict.fromkeys(data) if d not in p.cvars]
    if globs:
        out.append("globals: " + clip(sorted(globs, key=lambda g: g[0].isdigit()), 10))
    if va in p.fields:
        merged = collections.defaultdict(str)
        for st, o, a, _ in p.fields[va]:
            merged[(st, o)] += a
        out.append("fields: " + clip((f"{p.field_name(st, o)}({''.join(sorted(a))})"
                                      for (st, o), a in sorted(merged.items())), 16))
    if va in p.q3:
        q = p.q3[va]
        out.append(f"q3: {q['name']} ({q['pass']} {q['score']}; {q['source_file']}) {q['evidence'][:100]}")
    path = EXPORT / "decomp" / f"{va:08x}.c"
    if path.exists():
        body = [l.rstrip() for l in p.substitute(path.read_text(errors="replace")).splitlines()]
        body = [l for l in body if l.strip() and not DECL.match(l.strip())]
        out.append("--")
        out.extend(body[:lines])
        if len(body) > lines:
            out.append(f"  ... ({len(body) - lines} more lines)")
    return "\n".join(out)


def fields_report(p, struct, unknown_only, n):
    usage = collections.defaultdict(lambda: [collections.Counter(), set()])
    for va, rows in p.fields.items():
        for st, o, a, c in rows:
            if st == struct:
                usage[o][0][a] += c
                usage[o][1].add(va)
    known = p.structs.get(struct, {})
    users = {va for _, fns in usage.values() for va in fns}
    named = sum(1 for va in users if not p.unnamed(va))
    print(f"{struct}: {len(usage)} offsets used, {len(known)} named fields, {len(users)} functions "
          f"({len(users) - named} unnamed)")
    for o in sorted(usage):
        if unknown_only and o in known:
            continue
        counts, fns = usage[o]
        fns = sorted(fns, key=lambda va: (p.unnamed(va), va))
        acc = " ".join(f"{k}{counts[k]}" for k in "rwa" if counts[k])
        print(f"  {o:#06x} {known.get(o, '?'):18} {acc:14} {len(fns):4}  {clip((p.name(v) for v in fns), n)}")


def main():
    parser = argparse.ArgumentParser(usage=__doc__)
    parser.add_argument("command", choices=["rank", "pack", "next", "fields", "status"])
    parser.add_argument("items", nargs="*")
    parser.add_argument("--module")
    parser.add_argument("--range")
    parser.add_argument("--lines", type=int, default=60)
    parser.add_argument("--unknown", action="store_true")
    parser.add_argument("-n", type=int, default=30)
    args = parser.parse_args()
    p = Program()
    if not p.meta:
        sys.exit(f"no export in {EXPORT}: run tools/ghidra/ExportNaming.java first")
    if args.command == "status":
        files = list((EXPORT / "meta").glob("*.tsv"))
        oldest = min(f.stat().st_mtime for f in files)
        unnamed = sum(1 for va in p.meta if p.unnamed(va))
        print(f"{len(p.meta)} functions exported ({unnamed} unnamed) in {len(files)} ranges; "
              f"oldest range {(time.time() - oldest) / 3600:.1f} h old")
    elif args.command == "rank":
        for s, va, why in candidates(p, args)[:args.n]:
            print(f"{va:08x} {s:5g} {p.meta[va][0]:6}B {p.module.get(va, '?'):10} {why}")
    elif args.command == "next":
        count = int(args.items[0]) if args.items else 5
        print("\n\n".join(pack(p, va, args.lines) for _, va, _ in candidates(p, args)[:count]))
    elif args.command == "pack":
        by_name = {n: a for a, n in p.names.items()}
        print("\n\n".join(pack(p, by_name[i] if i in by_name else int(i, 16), args.lines) for i in args.items))
    else:
        fields_report(p, args.items[0], args.unknown, args.n if args.n != 30 else 6)


if __name__ == "__main__":
    main()
