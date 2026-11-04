#!/usr/bin/env python3
"""Group functions by shape: disassembly with push/cmp/mov 32-bit immediates masked.

usage: shapes.py LIST [--prefix ScrField] [--show N]
LIST: lines "address state bytes name" (progress-style). Prints groups by size, with
each group's differing immediates, so template-generated functions can be written once.
"""
import argparse, re, sys, collections
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
import fnpack

ap = argparse.ArgumentParser()
ap.add_argument("list"); ap.add_argument("--prefix", default=""); ap.add_argument("--show", type=int, default=0)
ap.add_argument("--members", action="store_true")
a = ap.parse_args()
image = fnpack.Image()
groups = collections.defaultdict(list)
HEX = re.compile(r"0x[0-9a-f]{5,8}\b")
for line in open(a.list):
    p = line.split()
    if len(p) < 4 or not p[3].startswith(a.prefix): continue
    va, size, name = int(p[0], 16), int(p[2]), p[3]
    rows = fnpack.disassemble(image, va, size)
    keys, imms = [], []
    for addr, n, text in rows:
        t = re.sub(r"<[^>]*>", "", text)
        if t.startswith(("call", "j")) and not t.startswith(("call DWORD", "jmp DWORD")):
            # keep the absolute call target, drop branch targets (relative layout)
            if t.startswith("call"): keys.append(t)
            else: keys.append(t.split()[0])
            continue
        found = HEX.findall(t)
        imm = [h for h in found if not (0x00800000 <= int(h, 16) < 0x00a00000)]  # keep globals
        for h in imm: t = t.replace(h, "IMM")
        imms += imm; keys.append(t)
    groups[tuple(keys)].append((name, va, imms))
order = sorted(groups.items(), key=lambda kv: -len(kv[1]))
total = 0
for i, (k, members) in enumerate(order):
    total += len(members)
    print(f"group {i}: {len(members)} functions, {len(k)} insns, e.g. {members[0][0]} {members[0][1]:08x} imms={members[0][2]}")
    if a.members: print("   " + " ".join(m[0] for m in members))
    if i < a.show:
        for t in k: print("     ", t)
print(f"{len(order)} groups, {total} functions")
