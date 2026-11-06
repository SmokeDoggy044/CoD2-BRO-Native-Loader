#!/usr/bin/env python3
"""ts_addrow.py [--levels] ADDR NAME FILE SCORE FACTS [EXTRA] : append the thread_switch class row + set_aside row.
--levels: level-section rows (docs/levels_classes.csv) instead of levelscript."""
import csv, os, subprocess, sys
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/"  # the repo (worktree) this file is in
levels = "--levels" in sys.argv
if levels:
    sys.argv.remove("--levels")
addr, name, file, score, facts = sys.argv[1:6]
addr = addr.lower().removeprefix("0x").zfill(8)  # rows use 8 hex digits, no 0x
extra = sys.argv[6] if len(sys.argv) > 6 else ""
path = subprocess.run(["python3", R + "tools/waitreach.py", "--path", addr], capture_output=True, text=True,
                      cwd=R).stdout.strip().splitlines()[0]
ev = (f"waitreach path {path}; C in {file} reviewed against the disassembly, vc71match {score} "
      f"(--facts --force-inline: {facts}){'; ' + extra if extra else ''}, not registered (tested once script threads are coroutines)")
classes = R + ("docs/levels_classes.csv" if levels else "docs/levelscript_classes.csv")
if not os.path.exists(classes):
    with open(classes, "w") as f:
        f.write("address,name,class,evidence\n")
with open(classes, "a", newline="") as f:
    csv.writer(f, lineterminator="\n").writerow([addr, name, "thread_switch", ev])
with open(R + "tools/data/set_aside.csv", "a", newline="") as f:
    csv.writer(f, lineterminator="\n").writerow([addr, name, "thread switch",
        ("level script body" if levels else "levelscript thread body") + ", reaches a script wait: C kept, not registered, tested once script threads are coroutines (user 11-05); " + ev])
print(addr, name, path)
