#!/usr/bin/env python3
"""ownreach.py: per list row, whether its OWN body (fnpack disassembly) calls something that reaches a switch."""
import sys, re, os, bisect, collections, csv, glob, struct
R = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))), "tools")  # repo tools/
sys.path.insert(0, R); os.chdir(os.path.dirname(R))
import fnpack, levelreach, waitreach
S = os.path.dirname(os.path.abspath(__file__))
functions, _ = fnpack.load_names(); starts = sorted(functions); image = fnpack.Image()
by_name = {n: a for a, n in functions.items()}
def owner(va):
    i = bisect.bisect_right(starts, va) - 1
    return starts[i] if i >= 0 else None
seeds = {a for a, n in functions.items() if waitreach.SEED_NAME.match(n)}
last = {}
for path in ["tools/data/set_aside.csv"] + sorted(glob.glob("docs/*_classes.csv")):
    for row in csv.reader(open(path)):
        if len(row) >= 3 and re.fullmatch(r"[0-9a-fA-F]{8}", row[0]):
            last[int(row[0], 16)] = row[2].replace("_", " ")
seeds |= {a for a, c in last.items() if c == "thread switch"}
slots = {}
for init in levelreach.SLOT_INITS:
    end = starts[bisect.bisect_right(starts, init)]
    for _, _, text in fnpack.disassemble(image, init, end - init):
        m = levelreach.STORE.match(text)
        if m and image.section(int(m.group(2), 16)) not in fnpack.DATA_SECTIONS:
            slots[int(m.group(1), 16) // 4] = int(m.group(2), 16)
callers = collections.defaultdict(set)
for target, sites in image.code_refs(starts).items():
    for site, _ in sites:
        if owner(site) is not None: callers[target].add(owner(site))
for name, start, _, raw, raw_size in image.sections:
    if name in fnpack.DATA_SECTIONS: continue
    blob = image.data[raw:raw + raw_size]; j = blob.find(b"\xff\x15")
    while 0 <= j < len(blob) - 5:
        (ptr,) = struct.unpack_from("<I", blob, j + 2); sl = (ptr - levelreach.BLOCK) // 4
        if ptr >= levelreach.BLOCK and ptr % 4 == 0 and sl in slots and owner(start + j) is not None:
            callers[slots[sl]].add(owner(start + j))
        j = blob.find(b"\xff\x15", j + 1)
nxt = {s: None for s in seeds}; fr = list(seeds)
while fr:
    new = []
    for f in fr:
        for c in callers.get(f, ()):
            if c not in nxt: nxt[c] = f; new.append(c)
    fr = new
def chain(f):
    out = []
    while f is not None and f in nxt: out.append(functions.get(f, hex(f))); f = nxt[f]
    return " -> ".join(out)
for line in open(S + "/list.txt"):
    a, n = line.split()
    txt = open(f"{S}/fp/{a}.txt").read().split("disassembly:")[1]
    hits = []
    for m in re.finditer(r"call\s+(?:0x([0-9a-f]+)|DWORD PTR ds:0x([0-9a-f]+))", txt):
        if m.group(1):
            t = int(m.group(1), 16)
        else:
            t = slots.get((int(m.group(2), 16) - levelreach.BLOCK) // 4)
        if t in nxt: hits.append(chain(t))
    for m in re.finditer(r"jmp\s+0x([0-9a-f]+)", txt):
        t = int(m.group(1), 16)
        if t in functions and t in nxt: hits.append("jmp " + chain(t))
    print(a, n, "switch" if hits else "SAFE", (sorted(set(hits), key=len)[0] if hits else ""))
