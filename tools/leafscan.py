#!/usr/bin/env python3
"""Which decompiled functions tests/leaf_test.cpp can run offline from default.xbe's bytes:
code that needs nothing but memory - no indirect calls (kernel thunks, vtables), no
FS/port/privileged instructions, no calls into SDK sections that touch the hardware
(D3D, DSOUND, XONLINE, XNET, XPP), and callees that pass the same test. Globals are fine:
the test maps the image at its addresses and randomizes the ones read.

usage: tools/leafscan.py [--module all|renderer,game,...] [--states untested,inconclusive,mismatched,offline,unsorted]
                         [--emit tests/leaf_functions.inc]
  --emit FILE  write the eligible functions as the test's table (name, address, size,
               instruction count, globals read by the function and its callees)
"""
import argparse
import bisect
import csv
import re
import struct

import fnpack

ADDRESS = re.compile(r"0x([0-9a-f]{5,8})\b")
BAD = re.compile(r"^(in|out|ins\w|outs\w|cli|sti|hlt|int|int3|into|rdtsc|rdmsr|wrmsr|cpuid|wbinvd|invlpg|"
                 r"lgdt|lidt|sgdt|sidt|iret\w*|lock)\b")
HARDWARE_SECTIONS = {"D3D", "DSOUND", "XONLINE", "XNET", "XPP"}
WRITES = re.compile(r"^(mov\w*|add|sub|and|or|xor|inc|dec|neg|not|shl|shr|sar|rol|ror|adc|sbb|set\w+|"
                    r"fst\w*|fist\w*|fbstp|stos\w|cmpxchg|xchg|xadd|pop)$")


def reachable(rows, va):
    """Instructions reachable from the entry over direct branches (a symbol's range can hold
    another, unnamed function after the last RET); all of them if a switch table jumps."""
    index = {address: k for k, (address, _, _) in enumerate(rows)}
    seen, todo = set(), [va]
    while todo:
        address = todo.pop()
        while address in index and address not in seen:
            seen.add(address)
            _, length, text = rows[index[address]]
            mnemonic, operands = fnpack.split_operands(text)
            target = re.fullmatch(r"0x([0-9a-f]+)", ",".join(operands))
            if mnemonic.startswith("j") and not target:
                return len(rows)  # switch table
            if mnemonic.startswith("j") and target:
                todo.append(int(target.group(1), 16))
            if mnemonic in ("ret", "jmp", "iret"):
                break
            address += length
    return len(seen)


def scan(image, starts, va):
    i = bisect.bisect_right(starts, va)
    end = starts[i] if i < len(starts) else va + 0x400
    rows = fnpack.disassemble(image, va, min(end - va, 0x4000))
    size = rows[-1][0] + rows[-1][1] - va if rows else 0
    callees, problems, reads, writes, addressed, indexed = set(), set(), set(), set(), set(), set()
    for _, _, text in rows:
        mnemonic, operands = fnpack.split_operands(text)
        joined = ",".join(operands)
        if BAD.match(text) or "fs:" in joined:
            problems.add("fs:" if "fs:" in joined else mnemonic)
        if mnemonic == "call" or mnemonic.startswith("j"):
            m = re.fullmatch(r"0x([0-9a-f]+)", joined)
            if m:
                target = int(m.group(1), 16)
                if mnemonic == "call" or not va <= target < va + size:
                    callees.add(target)
            elif not (mnemonic == "jmp" and re.search(r"\*4\+0x", joined)):  # switch tables stay inside
                problems.add(f"indirect {mnemonic}")
            continue
        for k, op in enumerate(operands):
            memory = op[op.index("["):] if "[" in op else op[op.index("ds:"):] if "ds:" in op else None
            if memory is None or mnemonic == "lea":
                for m in ADDRESS.finditer(op):  # the address itself is taken
                    addressed.add(int(m.group(1), 16))
                continue
            for m in ADDRESS.finditer(memory):
                target = int(m.group(1), 16)
                if re.search(r"\[.*e[abcds][xip]", memory):
                    indexed.add(target)
                if target < 0x10000 or image.section(target) not in fnpack.DATA_SECTIONS - {".rdata"}:
                    continue
                (writes if k == 0 and WRITES.match(mnemonic) else reads).add(target)
                if k == 0 and mnemonic in ("add", "sub", "and", "or", "xor", "inc", "dec", "adc", "sbb"):
                    reads.add(target)
    return {"size": size, "count": reachable(rows, va), "callees": callees, "problems": problems,
            "reads": reads, "writes": writes, "addressed": addressed,
            "indexed": indexed}


def data_refs(image):
    """Code positions of every 32-bit word that is an address in a data section."""
    ranges = [(start, start + max(vsize, raw_size)) for name, start, vsize, _, raw_size in image.sections
              if name in fnpack.DATA_SECTIONS]
    lo, hi = min(r[0] for r in ranges), max(r[1] for r in ranges)
    refs = {}
    for name, start, _, raw, raw_size in image.sections:
        if name in fnpack.DATA_SECTIONS:
            continue
        blob = image.data[raw:raw + raw_size]
        for align in range(4):
            words = struct.iter_unpack("<I", blob[align:align + (len(blob) - align) // 4 * 4])
            for i, (value,) in enumerate(words):
                if lo <= value < hi:
                    refs.setdefault(value, []).append(start + align + 4 * i)
    return refs


def main():
    parser = argparse.ArgumentParser(usage=__doc__)
    parser.add_argument("--module", default="all")
    parser.add_argument("--states", default="untested,inconclusive,mismatched,offline,unsorted")
    parser.add_argument("--emit")
    args = parser.parse_args()
    wanted = set(args.states.split(","))
    functions, _ = fnpack.load_names()
    _, ours = fnpack.load_states()
    starts = sorted(set(functions) | ours)  # decompiled gap functions bound their neighbours too
    image = fnpack.Image()
    targets = []
    listed = set()
    with open(fnpack.DATA / "progress.csv", newline="") as f:
        for row in csv.DictReader(f):
            va = int(row["address"], 16)
            listed.add(va)
            if (args.module == "all" or row["module"] in args.module.split(",")) and row["state"] in wanted and va in ours:
                targets.append((va, row["name"], row["state"]))
    # Decompiled gap functions (no symbol, so not in progress.csv): state "unsorted".
    if args.module == "all" and "unsorted" in wanted:
        with open(fnpack.DATA / "functions.csv", newline="") as f:
            for row in csv.DictReader(f):
                va = int(row["address"], 16)
                if va in ours and va not in listed:
                    targets.append((va, row["name"], "unsorted"))
                    functions.setdefault(va, row["name"])
        targets.sort()
    cache = {}

    def info(va):
        if va not in cache:
            cache[va] = scan(image, starts, va)
        return cache[va]

    def blocker(va, depth=0, seen=None):
        """Why the function cannot run offline (itself or a callee), or None."""
        seen = set() if seen is None else seen
        if va in seen:
            return None
        seen.add(va)
        if image.section(va) in HARDWARE_SECTIONS:
            return f"calls SDK {functions.get(va, hex(va))}"
        if depth > 6:
            return "call depth"
        r = info(va)
        if r["problems"]:
            where = "" if depth == 0 else f" in {functions.get(va, hex(va))}"
            return ",".join(sorted(r["problems"])) + where
        for c in sorted(r["callees"]):
            why = blocker(c, depth + 1, seen)
            if why:
                return why
        return None

    refs = None

    def constant(g):
        """Read and never written: every instruction naming the address reads it, none takes it
        (lea, immediate) or writes it, and no address in the 16 bytes below or the 3 above is
        a base (taken, or indexed with a register) that could reach it. A heuristic: a structure
        whose base is further away and taken could still reach it. The test leaves such a global at the image's
        value (FastSinCos's rounding constant 0x008febd0 is in .data, not .rdata)."""
        nonlocal refs
        if refs is None:
            refs = data_refs(image)
        def site(p):
            i = bisect.bisect_right(starts, p) - 1
            if i < 0:
                return None
            r = info(starts[i])
            return r if r["size"] and p < starts[i] + r["size"] else None  # inside a known function

        for a in range(g - 0x10, g + 4):
            for p in refs.get(a, []) if a != g else []:
                r = site(p)
                if r is None or a in r["addressed"] or a in r["indexed"]:
                    return False
        for p in refs.get(g, []):
            r = site(p)
            if r is None or g in r["writes"] or g in r["addressed"] or g not in r["reads"]:
                return False
        return bool(refs.get(g))

    def tree_globals(va, seen=None):
        seen = set() if seen is None else seen
        if va in seen:
            return set()
        seen.add(va)
        r = info(va)
        out = set(r["reads"])
        for c in r["callees"]:
            out |= tree_globals(c, seen)
        return out

    eligible, blocked = [], []
    for va, name, state in targets:
        why = blocker(va)
        r = info(va)
        callee_text = ",".join(functions.get(c, f"{c:08x}") for c in sorted(r["callees"]))
        line = (f"{va:08x} {name:40s} {state:12s} {r['size']:5d}B"
                + (f" calls[{callee_text}]" if callee_text else "")
                + (f" reads{len(r['reads'])}" if r["reads"] else "")
                + (f" writes{len(r['writes'])}" if r["writes"] else ""))
        if why:
            blocked.append(f"{line} !{why}")
        else:
            eligible.append((va, name, line))
    print(f"== eligible: {len(eligible)}")
    print("\n".join(line for _, _, line in eligible))
    print(f"== blocked: {len(blocked)}")
    print("\n".join(blocked))
    if args.emit:
        out = ["// Generated by tools/leafscan.py --emit: functions tests/leaf_test.cpp runs offline.",
               "// name, address, size, instructions, globals read (randomized per input)"]
        with open(fnpack.DATA / "leaf_pending.csv", newline="") as f:
            pending = {row["name"] for row in csv.DictReader(f)}  # known mismatches being fixed
        for va, name, _ in eligible:
            if name in pending:
                continue
            r = info(va)
            globals_ = ", ".join(f"0x{g:08x}" for g in sorted(tree_globals(va)) if not constant(g))
            out.append(f'{{"{name}", 0x{va:08x}, {r["size"]}, {r["count"]}, {{{globals_}}}}},')
        with open(args.emit, "w") as f:
            f.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
