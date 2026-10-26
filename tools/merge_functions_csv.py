#!/usr/bin/env python3
"""Resolves a merge conflict in tools/data/functions.csv: each function's counts become
base + (ours - base) + (theirs - base), so test results recorded on both branches add
up; the other columns (status, last_issue) come from the side whose counts changed
last (theirs when both changed).

usage: tools/merge_functions_csv.py [THEIRS]   (during a conflicted `git merge THEIRS`;
       default MERGE_HEAD) - writes the merged file and `git add`s it
"""
import csv
import io
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PATH = "tools/data/functions.csv"
COUNTS = ["calls", "tested", "matched", "drift", "mismatched", "inconclusive"]


def load(rev):
    text = subprocess.run(["git", "-C", str(ROOT), "show", f"{rev}:{PATH}"], capture_output=True, text=True,
                          check=True).stdout
    rows = list(csv.DictReader(io.StringIO(text)))
    return {row["address"]: row for row in rows}, list(rows[0].keys()) if rows else []


def main():
    theirs_rev = sys.argv[1] if len(sys.argv) > 1 else "MERGE_HEAD"
    base_rev = subprocess.run(["git", "-C", str(ROOT), "merge-base", "HEAD", theirs_rev], capture_output=True,
                              text=True, check=True).stdout.strip()
    base, _ = load(base_rev)
    ours, fields = load("HEAD")
    theirs, _ = load(theirs_rev)
    merged = {}
    for address in sorted(set(ours) | set(theirs)):
        a, b, o = ours.get(address), theirs.get(address), base.get(address)
        if a is None or b is None:
            merged[address] = dict(a or b)
            continue
        row = dict(b if b != o else a)
        for key in COUNTS:
            zero = int((o or {}).get(key) or 0)
            row[key] = str(int(a[key] or 0) + int(b[key] or 0) - zero)
        merged[address] = row
    # As the loader writes it: the last column (last_issue) always quoted.
    lines = [",".join(fields)]
    for address in sorted(merged):
        row = merged[address]
        last = '"' + row[fields[-1]].replace('"', '""') + '"'
        lines.append(",".join([row[key] for key in fields[:-1]] + [last]))
    (ROOT / PATH).write_text("\n".join(lines) + "\n")
    subprocess.run(["git", "-C", str(ROOT), "add", PATH], check=True)
    print(f"merged {len(merged)} rows ({PATH})")


if __name__ == "__main__":
    main()
