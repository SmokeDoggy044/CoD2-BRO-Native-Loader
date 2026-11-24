#!/usr/bin/env python3
"""census.py: which x86 instructions the level code uses (what the translator must handle), per level.
usage: build/translator_venv/bin/python tools/translator/census.py [LEVEL...]   (default: all 14)
Function boundaries from the exported symbols (fnpack); a function ends at the next function start or its
section end. Prints mnemonic counts (sites and functions), operand kinds of note, and undecodable bytes.
"""
import bisect
import collections
import os
import sys

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, TOOLS)
os.chdir(os.path.dirname(TOOLS))
import capstone  # noqa: E402
from capstone import x86  # noqa: E402
import fnpack  # noqa: E402
import levelreach  # noqa: E402


def level_functions(image, functions, levels):
    """{level: [(start, end, name)]} for the level sections."""
    starts = sorted(functions)
    out = {}
    for name, start, vsize, raw, raw_size in image.sections:
        if name not in levels:
            continue
        end = start + max(vsize, raw_size)
        i = bisect.bisect_left(starts, start)
        rows = []
        while i < len(starts) and starts[i] < end:
            s = starts[i]
            e = starts[i + 1] if i + 1 < len(starts) and starts[i + 1] < end else end
            rows.append((s, e, functions[s]))
            i += 1
        out[name] = rows
    return out


def main():
    levels = sys.argv[1:] or list(levelreach.LEVELS)
    functions, _ = fnpack.load_names()
    image = fnpack.Image()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    sites = collections.Counter()
    users = collections.defaultdict(set)
    notes = collections.Counter()
    total_fn = total_ins = bad = 0
    for level, rows in level_functions(image, functions, levels).items():
        for start, end, name in rows:
            total_fn += 1
            code = image.read(start, end - start)
            pos = 0
            while pos < len(code):
                got = False
                for ins in md.disasm(code[pos:], start + pos):
                    got = True
                    pos += ins.size
                    total_ins += 1
                    sites[ins.mnemonic] += 1
                    users[ins.mnemonic].add(start)
                    if ins.prefix[0] in (0xF2, 0xF3) and ins.mnemonic.startswith(("movs", "stos", "cmps", "scas", "lods")):
                        notes["rep string op"] += 1
                    for op in ins.operands:
                        if op.type == x86.X86_OP_MEM and op.mem.segment != 0:
                            notes[f"segment {ins.reg_name(op.mem.segment)}"] += 1
                    if ins.mnemonic in ("call", "jmp") and ins.operands and ins.operands[0].type != x86.X86_OP_IMM:
                        notes[f"indirect {ins.mnemonic}"] += 1
                    if ins.mnemonic == "int3":  # padding: stop this function
                        pos = len(code)
                        break
                if not got:  # data or undecodable byte inside the range
                    bad += 1
                    pos += 1
    print(f"{total_fn} functions, {total_ins} instructions, {bad} undecodable bytes")
    print(f"{len(sites)} distinct mnemonics (sites / functions):")
    for m, n in sites.most_common():
        print(f"  {m:12s} {n:7d} {len(users[m]):6d}")
    print("notes:")
    for k, n in notes.most_common():
        print(f"  {k:24s} {n}")


if __name__ == "__main__":
    main()
