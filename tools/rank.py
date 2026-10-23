#!/usr/bin/env python3
"""Bottom-up ranking of a module's functions that are not C code yet.

Joins the offline naming export's callee lists (build/naming/meta/*.tsv, column 3) with
tools/data/progress.csv: each named/unnamed function of the module with the number of its
callees that are still title code (named/unnamed), those in the same module marked '*'.
Functions with 0 or 1 pending callees are ready. Refresh the export first when it is old
(tools/namepack.py status).

usage: tools/rank.py MODULE [MAX_PENDING]   (run from the fork root; worktrees use their own root)
"""
import csv
import glob
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    module = sys.argv[1] if len(sys.argv) > 1 else 'cgame'
    max_pending = int(sys.argv[2]) if len(sys.argv) > 2 else 99
    progress = {r['address']: r for r in csv.DictReader(open(os.path.join(ROOT, 'tools/data/progress.csv')))}
    meta = {}
    for path in glob.glob(os.path.join(ROOT, 'build/naming/meta/*.tsv')):
        for line in open(path):
            parts = line.rstrip('\n').split('\t')
            meta[parts[0]] = parts
    rows = []
    for address, row in progress.items():
        if row['module'] != module or row['state'] not in ('named', 'unnamed'):
            continue
        m = meta.get(address)
        if not m:
            rows.append((99, address, row['name'], 0, '?'))
            continue
        pending = []
        for callee in filter(None, m[2].split(',')):
            state = progress.get(callee)
            if state and state['state'] in ('named', 'unnamed'):
                pending.append(('*' if state['module'] == module else '') + state['name'] + '@' + state['module'])
        rows.append((len(pending), address, row['name'], int(m[1]), ' '.join(pending)))
    for pending, address, name, size, callees in sorted(rows):
        if pending <= max_pending:
            print(pending, address, name, size, callees[:200])
    return 0


if __name__ == '__main__':
    sys.exit(main())
