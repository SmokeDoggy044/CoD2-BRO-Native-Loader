#!/usr/bin/env python3
"""Provisional modules for unnamed .text functions (tools/data/module_hints.csv).

A function that no modules.csv prefix sorts (unnamed ones, so far) gets a module
from its neighbours and callers: the nearest sorted function on each side in
address order (same source file, as a rule) and the modules of its sorted .text
callers (functions sorted only by a hint are no evidence). The hint is written when the evidence agrees:
  callers+neighbours  every sorted caller and both neighbours name one module
  neighbours          both neighbours agree (no sorted callers, or callers differ)
  callers             callers agree on one module, neighbours do not
Functions in the Microsoft library ranges of modules.csv are skipped (the range
sorts them). Rows with source "manual" are kept as they are; "auto" rows are
rewritten on every run. A function's real module comes once it is named: the
prefix rule wins over the hint.

Reads tools/data/progress.csv (run tools/progress.py first) and the naming export
build/naming/meta (tools/ghidra/ExportNaming.java).

usage: tools/sort_unnamed.py [--dry-run]
"""
import argparse
import collections
import csv
import glob
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "tools" / "data"
HINTS = DATA / "module_hints.csv"
LIBRARY = "mslib"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    rows = [r for r in csv.DictReader(open(DATA / "progress.csv"))]
    module = {int(r["address"], 16): r["module"] for r in rows}
    text = [r for r in rows if r["section"] == ".text"]
    intext = {int(r["address"], 16) for r in text}  # level-section callers say nothing about an engine module
    callers = collections.defaultdict(set)
    for path in glob.glob(str(ROOT / "build" / "naming" / "meta" / "*.tsv")):
        for line in open(path):
            cols = line.rstrip("\n").split("\t")
            for c in cols[2].split(","):
                if c:
                    callers[int(c, 16)].add(int(cols[0], 16))

    old = list(csv.DictReader(open(HINTS))) if HINTS.exists() else []
    manual = {int(r["address"], 16): r for r in old if r["source"] == "manual"}
    hinted = {int(r["address"], 16) for r in old}  # sorted by a hint, not a rule: no evidence for others

    def sorted_(a):
        return a not in hinted and module.get(a) not in ("unassigned", LIBRARY, None)

    def neighbour(i, step):
        j = i + step
        while 0 <= j < len(text):
            if sorted_(int(text[j]["address"], 16)):
                return text[j]["module"]
            j += step
        return None

    out, kinds = [], collections.Counter()
    for i, r in enumerate(text):
        a = int(r["address"], 16)
        if a in manual or r["state"] != "unnamed" or (r["module"] != "unassigned" and a not in hinted):
            continue
        cm = {module.get(c) for c in callers[a] if c in intext and sorted_(c)}
        prev, nxt = neighbour(i, -1), neighbour(i, 1)
        if len(cm) == 1 and prev == nxt == next(iter(cm)):
            kind, m = "callers+neighbours", prev
        elif prev == nxt and prev:
            kind, m = "neighbours", prev
        elif len(cm) == 1:
            kind, m = "callers", next(iter(cm))
        else:
            kinds["no hint"] += 1
            continue
        kinds[kind] += 1
        ev = f"prev {prev}, next {nxt}, callers {','.join(sorted(cm)) or '-'}"
        out.append({"address": f"{a:08x}", "module": m, "source": "auto", "evidence": f"{kind}: {ev}"})

    for k, v in kinds.most_common():
        print(f"{v:5} {k}")
    print(f"{len(manual):5} manual (kept)")
    if args.dry_run:
        return
    allrows = sorted(list(manual.values()) + out, key=lambda r: r["address"])
    with open(HINTS, "w", newline="") as f:
        w = csv.DictWriter(f, ["address", "module", "source", "evidence"])
        w.writeheader()
        w.writerows(allrows)
    print(f"wrote {HINTS.relative_to(ROOT)}: {len(allrows)} rows")


if __name__ == "__main__":
    main()
