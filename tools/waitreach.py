#!/usr/bin/env python3
"""Which functions can reach a script thread switch (a wait, or starting a thread that runs at once).

C that is called from a script thread and reaches a wait stays original until the own engine's coroutines
(docs/research/status.md, script module rule). Seeds: the set-aside and classed "thread switch" rows
(tools/data/set_aside.csv, docs/*_classes.csv) and the Scr_Suspend*/Scr_Wait* primitives. Edges: CALL/JMP rel32
and `call [0x00922xxx]` engine-interface slots (slot -> builtin, as tools/levelreach.py). Byte scans: an E8/E9 or
FF 15 inside another instruction can add a false edge, so the "reaches" side is an over-approximation (safe for
deciding what stays original). Virtual calls are not followed: a function that switches only through a vtable
call (AeThreadFunctor*_Invoke) is not found - the coordinator checks vtable-calling rows apart.

usage: tools/waitreach.py [--module levelscript] [--list reach|safe] [--path NAME]
Reads the XBE and the exported symbols only (run tools/progress.py first for --module).
"""
import argparse
import bisect
import collections
import csv
import glob
import re
import struct

import fnpack
import levelreach

SEED_NAME = re.compile(r"^(Scr_Suspend\w*|Scr_Wait|Scr_WaitFrames|Scr_WaitTill\w*|Scr_CallThread\w*|Scr_ThreadEntry)$")


def main():
    parser = argparse.ArgumentParser(usage=__doc__)
    parser.add_argument("--module", default="levelscript")
    parser.add_argument("--list", choices=("reach", "safe"))
    parser.add_argument("--path", help="print one shortest path from NAME to a seed")
    args = parser.parse_args()
    functions, _ = fnpack.load_names()
    by_name = {n: a for a, n in functions.items()}
    starts = sorted(functions)
    image = fnpack.Image()

    def owner(va):
        i = bisect.bisect_right(starts, va) - 1
        return starts[i] if i >= 0 else None

    seeds = {a for a, n in functions.items() if SEED_NAME.match(n)}
    # the classes files are append-only: the last row per address is the current class
    last = {}
    for path in ["tools/data/set_aside.csv"] + sorted(glob.glob("docs/*_classes.csv")):
        for row in csv.reader(open(path)):
            if len(row) >= 3 and re.fullmatch(r"[0-9a-fA-F]{8}", row[0]):
                last[int(row[0], 16)] = row[2].replace("_", " ")
    seeds |= {a for a, cls in last.items() if cls == "thread switch"}

    # slot -> builtin (the engine-interface inits), as levelreach
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
            if owner(site) is not None:
                callers[target].add(owner(site))
    for name, start, _, raw, raw_size in image.sections:
        if name in fnpack.DATA_SECTIONS:
            continue
        blob = image.data[raw:raw + raw_size]
        j = blob.find(b"\xff\x15")
        while 0 <= j < len(blob) - 5:
            (ptr,) = struct.unpack_from("<I", blob, j + 2)
            slot = (ptr - levelreach.BLOCK) // 4
            if ptr >= levelreach.BLOCK and ptr % 4 == 0 and slot in slots and owner(start + j) is not None:
                callers[slots[slot]].add(owner(start + j))
            j = blob.find(b"\xff\x15", j + 1)

    # reverse BFS from the seeds; nxt[f] = callee one step closer to a seed
    nxt = {s: None for s in seeds}
    frontier = list(seeds)
    while frontier:
        new = []
        for f in frontier:
            for c in callers.get(f, ()):
                if c not in nxt:
                    nxt[c] = f
                    new.append(c)
        frontier = new

    if args.path:
        f = by_name.get(args.path) or int(args.path, 16)
        chain = []
        while f is not None and f in nxt:
            chain.append(functions.get(f, hex(f)))
            f = nxt[f]
        print(" -> ".join(chain) if chain else "no path")
        return

    rows = [r for r in csv.DictReader(open(fnpack.DATA / "progress.csv")) if r["module"] == args.module]
    reach = [r for r in rows if int(r["address"], 16) in nxt]
    safe = [r for r in rows if int(r["address"], 16) not in nxt]
    if args.list:
        for r in reach if args.list == "reach" else safe:
            print(r["address"], r["state"], r["name"])
        return
    for label, group in (("reaches a thread switch", reach), ("no switch found", safe)):
        states = collections.Counter(r["state"] for r in group)
        print(f"{label}: {len(group)}  " + " ".join(f"{s} {n}" for s, n in sorted(states.items())))


if __name__ == "__main__":
    main()
