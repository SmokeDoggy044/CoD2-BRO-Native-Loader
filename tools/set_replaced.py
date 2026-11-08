#!/usr/bin/env python3
"""Switches a finished module to Replaced: every kFunctions row in src/engine/shared/functions.cpp whose progress state
(tools/data/progress.csv from tools/progress.py) is rewritten (matched/settled/offline/accepted) gets
Status::Replaced; the rest (untested, inconclusive, set-aside, host, mismatched) stay Compare.

usage: tools/set_replaced.py MODULE[,MODULE] [--dry-run] [--keep NAME,NAME]
Run tools/progress.py first. Afterwards: the skill's scripts/keepregs.py, then smoke both builds."""
import csv, re, sys

REWRITTEN = {"matched", "settled", "offline", "accepted"}
mods = set(sys.argv[1].split(","))
dry = "--dry-run" in sys.argv
keep = set(sys.argv[sys.argv.index("--keep") + 1].split(",")) if "--keep" in sys.argv else set()

state = {r["name"]: (r["module"], r["state"]) for r in csv.DictReader(open("tools/data/progress.csv"))}
path = "src/engine/shared/functions.cpp"
src = open(path).read()
switched = []

def flip(m):
    name = m.group(1)
    module, st = state.get(name, ("?", "?"))
    if module in mods and st in REWRITTEN and name not in keep:
        switched.append(name)
        return m.group(0).replace("Status::Compare}", "Status::Replaced}")
    return m.group(0)

out = re.sub(r'\{"(\w+)", 0x[0-9a-f]{8}, [^\n]*Status::Compare\}', flip, src)
print(f"{len(switched)} rows -> Replaced ({','.join(sorted(mods))})")
if not dry:
    open(path, "w").write(out)
