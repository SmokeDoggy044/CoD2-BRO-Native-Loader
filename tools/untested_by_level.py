#!/usr/bin/env python3
"""Which missions can reach the engine rows no run has compared yet (untested/inconclusive).

Runs tools/levelreach.py on those rows, drops the dead ones (levelreach --dead), and keeps
the rows a level's scripts reach (through a builtin slot or a direct call). Writes
tools/data/untested_by_level.csv (address, name, module, state, levels with call-site
counts) and prints, per mission, how many of these rows it reaches - the missions worth a
user-steered --compare-all run.

usage: tools/untested_by_level.py
"""
import collections
import csv
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
DATA = ROOT / "tools" / "data"
ROW = re.compile(r"^([0-9a-f]{8}) +(\S+) +(.*)$")
LEVEL = re.compile(r"\b([A-Z0-9]+):(\d+)\b")


def levelreach(*args):
    return subprocess.run([sys.executable, str(ROOT / "tools" / "levelreach.py"), *args],
                          capture_output=True, text=True, check=True).stdout.splitlines()


def main():
    progress = {r["address"]: r for r in csv.DictReader(open(DATA / "progress.csv"))}
    states = ("untested", "inconclusive")
    dead = {m.group(1) for s in states for line in levelreach("--dead", "--state", s)
            if (m := ROW.match(line))}
    rows = []
    for s in states:
        for line in levelreach("--state", s):
            m = ROW.match(line)
            if not m or m.group(1) in dead:
                continue
            address, name, text = m.groups()
            p = progress.get(address)
            if not p or p["section"] != ".text" or p["module"] in ("mslib", "crt"):
                continue
            if text.startswith("no level"):
                continue
            reach = text.split("  via ")[0]
            levels = {lv: int(n) for lv, n in LEVEL.findall(reach)}
            rows.append((address, name, p["module"], s, levels))

    with open(DATA / "untested_by_level.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["address", "name", "module", "state", "levels"])
        for address, name, module, state, levels in rows:
            w.writerow([address, name, module, state,
                        " ".join(f"{lv}:{n}" for lv, n in sorted(levels.items()))])

    per_level = collections.Counter(lv for *_, levels in rows for lv in levels)
    only = collections.Counter(next(iter(levels)) for *_, levels in rows if len(levels) == 1)
    print(f"{len(rows)} untested/inconclusive engine rows that level scripts reach")
    print(f"{'mission':10} {'rows':>5} {'only here':>9}")
    for lv, n in per_level.most_common():
        print(f"{lv:10} {n:5} {only[lv]:9}")
    every = sum(1 for *_, levels in rows if len(levels) >= 14)
    print(f"reached from all 14 missions: {every} (any mission's run can hit them)")


if __name__ == "__main__":
    main()
