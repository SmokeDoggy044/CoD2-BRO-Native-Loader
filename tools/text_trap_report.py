#!/usr/bin/env python3
"""Summarise --text-trap logs (own engine host, 0.3): original functions still run, by module and state
(tools/data/progress.csv), with each one's first caller.

Usage: tools/text_trap_report.py LOG [LOG ...] [--rows]"""
import bisect
import csv
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
prog = {int(r['address'], 16): r for r in csv.DictReader(open(ROOT / 'tools/data/progress.csv'))}
syms = []
for line in (ROOT / 'tools/data/default_xbe_symbols.txt').read_text().splitlines():
    p = line.split()
    if len(p) >= 2 and not p[0].startswith('#'):
        syms.append((int(p[0], 16), p[1]))
syms.sort()
keys = [a for a, _ in syms]


def where(a):
    if a >= 0x02000000:
        return 'host'
    i = bisect.bisect_right(keys, a) - 1
    return syms[i][1] if i >= 0 else hex(a)


hits = {}
for log in [a for a in sys.argv[1:] if not a.startswith('--')]:
    for line in open(log):
        p = line.split()
        if len(p) == 3:
            hits.setdefault(int(p[0], 16), (p[1], int(p[2], 16)))
by = Counter()
for a in hits:
    r = prog.get(a)
    by[(r['module'], r['state']) if r else ('sdk', '-')] += 1
for (m, s), n in by.most_common():
    print(f'{n:5} {m} {s}')
print(f'{len(hits)} functions')
if '--rows' in sys.argv:
    for a, (name, caller) in sorted(hits.items()):
        r = prog.get(a)
        print(f'{a:08x} {name} [{r["module"] + " " + r["state"] if r else "sdk"}] from {caller:08x} {where(caller)}')
