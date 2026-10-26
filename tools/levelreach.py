#!/usr/bin/env python3
"""Which levels' scripts can reach a function: the level sections (compiled level scripts,
loaded per mission) call the engine only through the import block at 0x00922140 (slot =
offset / 4, filled by 0x0030adb0 and its sub-inits; docs/naming.md "Script engine
interface"), as `call [0x00922xxx]`, and engine functions directly. For each function: the
builtins (slot targets) whose callee trees contain it - found by walking CALL/JMP callers and
code addresses stored by code (callbacks) upward - and the call sites of those slots plus the
direct calls from each level section ("+N direct"). A function
reached through a pointer in data (vtables) is marked: its callers are not followed.

usage: tools/levelreach.py FUNCTION... [--module renderer --state untested] [--depth 12]
Reads the XBE and the exported symbols only (no Ghidra needed).
"""
import argparse
import bisect
import collections
import csv
import re
import struct
import subprocess
import tempfile

import fnpack

BLOCK = 0x00922140
SLOT_INITS = (0x0030adb0, 0x003094a0, 0x00301df0, 0x003088b0)
LEVELS = ("PROLOGUE", "ORAN", "TUNISIA", "KASSPASS", "KASSRINE", "LIBEATOR", "GELA", "PIANLUPO", "TROINA",
          "OMAHEACH", "MONS1", "EILEDORF", "BUCHHOLZ", "DRAGEETH")
STORE = re.compile(r"^mov DWORD PTR \[e\w\w\+0x([0-9a-f]+)\],0x([0-9a-f]+)$")
SLOT_CALL = re.compile(r"call +DWORD PTR ds:0x(922[0-9a-f]{3})\b")


def objdump(image, start, size):
    """objdump text of a whole range (sections are large: one process each)."""
    with tempfile.NamedTemporaryFile(suffix=".bin") as tmp:
        tmp.write(image.read(start, size))
        tmp.flush()
        return subprocess.run(["objdump", "-D", "-b", "binary", "-mi386", "-Mintel", f"--adjust-vma={start:#x}",
                               tmp.name], capture_output=True, text=True, check=True).stdout


def main():
    parser = argparse.ArgumentParser(usage=__doc__)
    parser.add_argument("functions", nargs="*")
    parser.add_argument("--module")
    parser.add_argument("--state")
    parser.add_argument("--depth", type=int, default=12)
    args = parser.parse_args()
    functions, _ = fnpack.load_names()
    by_name = {n: a for a, n in functions.items()}
    starts = sorted(functions)
    image = fnpack.Image()
    code_sections = [s for s in image.sections if s[0] not in fnpack.DATA_SECTIONS]

    def owner(va):
        i = bisect.bisect_right(starts, va) - 1
        return starts[i] if i >= 0 else None

    targets = [by_name[f] if f in by_name else int(f, 16) for f in args.functions]
    if args.module or args.state:
        for row in csv.DictReader(open(fnpack.DATA / "progress.csv")):
            if (not args.module or row["module"] == args.module) and (not args.state or row["state"] == args.state):
                targets.append(int(row["address"], 16))

    # Slot -> builtin: the init functions' stores of code addresses into the block.
    slots = {}
    for init in SLOT_INITS:
        end = starts[bisect.bisect_right(starts, init)]
        for _, _, text in fnpack.disassemble(image, init, end - init):
            m = STORE.match(text)
            if m and image.section(int(m.group(2), 16)) not in fnpack.DATA_SECTIONS:
                slots[int(m.group(1), 16) // 4] = int(m.group(2), 16)
    builtin_slots = collections.defaultdict(list)
    for slot, function in slots.items():
        builtin_slots[function].append(slot)

    # Slot call sites per level section.
    level_calls = collections.defaultdict(collections.Counter)  # slot -> level -> sites
    for name, start, vsize, raw, raw_size in image.sections:
        if name in LEVELS:
            for m in SLOT_CALL.finditer(objdump(image, start, raw_size)):
                level_calls[(int(m.group(1), 16) - BLOCK) // 4][name] += 1

    # Callers: CALL/JMP rel32, and code addresses as immediates in code (callbacks) or data.
    callers = collections.defaultdict(set)
    data_pointer = set()
    for target, sites in image.code_refs(starts).items():
        for site, _ in sites:
            if owner(site) is not None:
                callers[target].add(owner(site))
    starts_set = set(starts)
    for name, start, vsize, raw, raw_size in image.sections:
        blob = image.data[raw:raw + raw_size]
        is_data = name in fnpack.DATA_SECTIONS
        for j in range(0, len(blob) - 3, 1 if not is_data else 4):
            (word,) = struct.unpack_from("<I", blob, j)
            if word in starts_set and word >= 0x11000:
                if is_data:
                    data_pointer.add(word)
                elif owner(start + j) is not None and owner(start + j) != word:
                    callers[word].add(owner(start + j))

    for target in targets:
        seen, frontier, found = {target}, [target], set()
        through_data = target in data_pointer
        direct = collections.Counter()
        for _ in range(args.depth):
            nxt = []
            for f in frontier:
                if f in builtin_slots:
                    found.add(f)
                through_data |= f in data_pointer
                for c in callers.get(f, ()):
                    if image.section(c) in LEVELS:
                        direct[image.section(c)] += 1
                    elif c not in seen:
                        seen.add(c)
                        nxt.append(c)
            frontier = nxt
        levels = collections.Counter(direct)
        for f in found:
            for slot in builtin_slots[f]:
                levels.update(level_calls.get(slot, {}))
        builtins = ", ".join(f"{functions.get(f, hex(f))}[{','.join(map(str, builtin_slots[f]))}]"
                             for f in sorted(found))
        level_text = " ".join(f"{name}:{n}" for name, n in sorted(levels.items())) or "no level"
        if direct:
            level_text += f" (+{sum(direct.values())} direct)"
        print(f"{target:08x} {functions.get(target, '?'):34} {level_text}"
              + (" (+data pointer)" if through_data else "") + (f"  via {builtins}" if builtins else ""))


if __name__ == "__main__":
    main()
