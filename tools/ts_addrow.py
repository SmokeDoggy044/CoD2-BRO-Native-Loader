#!/usr/bin/env python3
"""ts_addrow.py ADDR NAME FILE SCORE FACTS [EXTRA] : append the thread_switch class row + set_aside row."""
import csv, os, subprocess, sys
R = os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/"  # the repo (worktree) this file is in
addr, name, file, score, facts = sys.argv[1:6]
extra = sys.argv[6] if len(sys.argv) > 6 else ""
path = subprocess.run(["python3", R + "tools/waitreach.py", "--path", name], capture_output=True, text=True,
                      cwd=R).stdout.strip().splitlines()[0]
ev = (f"waitreach path {path}; C in {file} reviewed against the disassembly, vc71match {score} "
      f"(--facts --force-inline: {facts}){'; ' + extra if extra else ''}, not registered (tested once script threads are coroutines)")
with open(R + "docs/levelscript_classes.csv", "a", newline="") as f:
    csv.writer(f, lineterminator="\n").writerow([addr, name, "thread_switch", ev])
with open(R + "tools/data/set_aside.csv", "a", newline="") as f:
    csv.writer(f, lineterminator="\n").writerow([addr, name, "thread switch",
        "levelscript thread body, reaches a script wait: C kept, not registered, tested once script threads are coroutines (user 11-05); " + ev])
print(addr, name, path)
