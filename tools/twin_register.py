#!/usr/bin/env python3
"""Registers level-section functions that are byte-for-byte copies of engine functions we already have in C.

The levels' compiler instantiated the same templates (HashMap, std::list/vector, thread functors) once per level.
A level row is a twin when its disassembly equals a Replaced engine function's with only CALL/JMP targets masked
(tools/levelsdup.py style, column "exact" of build/all_body_hashes.csv), and every direct callee pair is the same
address or itself a twin (checked recursively). Each twin gets the engine function's kFunctions entry (same code,
stack bytes and result) at its own address as Status::Compare, plus a functions.csv zero row. Unnamed rows are
named <engine name>_<MODULE> and appended to tools/data/manual_names.csv (apply in Ghidra afterwards).

usage: tools/twin_register.py [--module prologue] [--dry-run]
Needs build/all_body_hashes.csv (run tools/levelsdup.py first).
"""
import argparse
import bisect
import csv
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import fnpack  # noqa: E402

ROOT = fnpack.ROOT
FUNCTIONS_CPP = ROOT / "game" / "code" / "functions.cpp"
FUNCTIONS_CSV = fnpack.DATA / "functions.csv"
MANUAL = fnpack.DATA / "manual_names.csv"
CALL = re.compile(r"^(call|j\w+) 0x([0-9a-f]+)$")


def main():
    parser = argparse.ArgumentParser(usage=__doc__)
    parser.add_argument("--module", default="prologue")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    rows = list(csv.DictReader(open(ROOT / "build" / "all_body_hashes.csv")))
    by_addr = {int(r["address"], 16): r for r in rows}
    functions, _ = fnpack.load_names()
    starts = sorted(functions)
    image = fnpack.Image()
    text = FUNCTIONS_CPP.read_text()
    entries = {int(m.group(2), 16): m for m in re.finditer(
        r'^    \{"(\w+)", (0x[0-9a-fA-F]+), (0x[0-9a-fA-F]+|0), (code\([^)]*\)), (\d+), (Result::\w+), Status::(\w+)\},',
        text, re.M)}
    engine = {}  # exact hash -> Replaced engine addresses with a kFunctions entry
    for a, r in by_addr.items():
        if r["section"] != "levels" and r["state"] == "replaced" and a in entries and int(r["size"]) >= 8:
            engine.setdefault(r["exact"], []).append(a)

    def callees(va):
        n = int(by_addr[va]["size"])
        return [int(m.group(2), 16) for _, _, t in fnpack.disassemble(image, va, n) for m in [CALL.match(t)] if m]

    memo = {}

    def same(a, b, depth=0):
        """a (level code) behaves as b (engine code): equal masked body and equivalent callees."""
        if a == b:
            return True
        if (a, b) in memo:
            return memo[(a, b)]
        ra, rb = by_addr.get(a), by_addr.get(b)
        if not ra or not rb or ra["exact"] != rb["exact"] or depth > 12:
            return False
        memo[(a, b)] = True  # assume while recursing (mutual recursion)
        ca, cb = callees(a), callees(b)
        # branch targets: same address, twin callees, or the same offset inside the function
        ok = len(ca) == len(cb) and all(
            x == y or (x in by_addr and y in by_addr and same(x, y, depth + 1)) or
            (a <= x < a + int(ra["size"]) and b <= y < b + int(rb["size"]) and x - a == y - b)
            for x, y in zip(ca, cb))
        memo[(a, b)] = ok
        return ok

    found = []
    for a, r in sorted(by_addr.items()):
        if r["module"] != args.module or r["section"] != "levels" or a in entries:
            continue
        b = next((b for b in engine.get(r["exact"], []) if same(a, b)), None)
        if b is not None:
            found.append((a, r, b))

    module = args.module.upper()
    have_names = {line.split(",", 1)[0] for line in open(MANUAL)}
    new_entries, new_csv, new_names = [], [], []
    for a, r, b in found:
        m = entries[b]
        name = r["name"]
        if name.startswith("FUN_"):
            name = f"{m.group(1)}_{module}"
            if f"{a:08x}" not in have_names:
                new_names.append(f'{a:08x},{name},function,high,"copy of {m.group(1)} ({b:#010x}): '
                                 f'same body and callees (tools/twin_register.py)"')
        new_entries.append((a, f'    {{"{name}", {a:#010x}, 0, {m.group(4)}, {m.group(5)}, {m.group(6)}, '
                               f'Status::Compare}},  // twin of {m.group(1)}'))
        new_csv.append(f'{a:08x},{name},compare,0,0,0,0,0,0,""')
        print(f"{a:08x} {name:50} = {m.group(1)} {b:08x}")
    print(f"{len(found)} twins in {args.module}")
    if args.dry_run or not found:
        return

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
