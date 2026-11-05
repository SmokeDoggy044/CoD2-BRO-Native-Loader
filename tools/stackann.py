#!/usr/bin/env python3
"""Annotate a fnpack .asm with the stack depth per instruction (flow-following).
usage: stackann.py FILE.asm  -> prints 'addr [D] text' with [esp+X] rewritten as [esp+X]{F}
F = frame offset from the entry ESP (args +4.., locals negative). Conflicting depths flagged !!."""
import re, sys
import os; sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fnpack
rows = []
for line in open(sys.argv[1]):
    m = re.match(r"([0-9a-f]{8}) (.*)", line.rstrip("\n"))
    if m: rows.append((int(m.group(1), 16), m.group(2)))
idx = {a: i for i, (a, _) in enumerate(rows)}
functions, _ = fnpack.load_names()
image = fnpack.Image()
pops = fnpack.pops_reader(image, sorted(functions))
def num(s):
    return int(s, 16) if s.startswith("0x") else int(s)
depth = {}
work = [(0, 0)]
conflict = set()
while work:
    i, d = work.pop()
    while i < len(rows):
        a, t = rows[i]
        if a in depth:
            if depth[a] != d: conflict.add(a)
            break
        depth[a] = d
        op = t.split(" ", 1)[0]; rest = t[len(op):].strip()
        nd = d
        if op == "push": nd = d + 4
        elif op == "pop": nd = d - 4
        elif op == "sub" and rest.startswith("esp,"): nd = d + num(rest[4:])
        elif op == "add" and rest.startswith("esp,"): nd = d - num(rest[4:])
        elif op == "lea" and rest.startswith("esp,[esp"):
            m = re.match(r"esp,\[esp([+-]0x[0-9a-f]+)\]", rest); nd = d - int(m.group(1), 16)
        elif op == "call":
            m = re.match(r"0x([0-9a-f]+)", rest)
            if m:
                p = pops(int(m.group(1), 16))
                nd = d - (p or 0)
                if p is None: rows[i] = (a, t + "   ;; ?? pops unknown")
        elif op == "ret": break
        if op == "jmp":
            m = re.match(r"0x([0-9a-f]+)", rest)
            if m and int(m.group(1), 16) in idx: work.append((idx[int(m.group(1), 16)], nd))
            break
        if op.startswith("j"):
            m = re.match(r"0x([0-9a-f]+)", rest)
            if m and int(m.group(1), 16) in idx: work.append((idx[int(m.group(1), 16)], nd))
        d = nd; i += 1
for a, t in rows:
    d = depth.get(a)
    if d is not None:
        t = re.sub(r"\[esp\+0x([0-9a-f]+)\]", lambda m: f"[esp+0x{m.group(1)}]{{{int(m.group(1),16)-d:+#x}}}", t)
        t = re.sub(r"\[esp\]", lambda m: f"[esp]{{{-d:+#x}}}", t)
    flag = " !!" if a in conflict else ""
    print(f"{a:08x} [{'' if d is None else hex(d)}]{flag} {t}")
