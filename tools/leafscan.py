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
BAD = re.compile(r"^(in|out|ins\w|outs\w|cli|sti|hlt|int|into|rdtsc|rdmsr|wrmsr|cpuid|wbinvd|invlpg|"
                 r"lgdt|lidt|sgdt|sidt|iret\w*|lock)\b")  # int3 is fine: the original crashes, the input is skipped
HARDWARE_SECTIONS = {"D3D", "DSOUND", "XONLINE", "XNET", "XPP"}
# Callees the test replaces (tests/leaf_test.cpp kStandins): not scanned, no globals.
STANDINS = {0x00042cb0: "Z_Malloc", 0x001e763a: "XPhysicalAlloc", 0x001e76ba: "XPhysicalFree",
            0x001f114f: "_aligned_malloc", 0x001f113a: "_aligned_free",
            0x001eea95: "_vsprintf", 0x001ee901: "_sprintf", 0x001efce2: "__vsnprintf"}
# Globals that hold an optional hook, 0 in the image and in the game: never randomized, and the
# indirect branch they guard is not a blocker in these functions (Mem_AlignedFree's free hook).
PINNED = {0x009098ec}
PINNED_INDIRECT = {0x000939b0}
# Functions that call a function passed as an argument; their callers push it as an immediate
# (eh_vector_constructor_iterator(array, size, count, constructor), stdcall).
ARGUMENT_CALLERS = {0x00011240}
WRITES = re.compile(r"^(mov\w*|add|sub|and|or|xor|inc|dec|neg|not|shl|shr|sar|rol|ror|adc|sbb|set\w+|"
                    r"fst\w*|fist\w*|fbstp|stos\w|cmpxchg|xchg|xadd|pop)$")


def reachable(rows, va):
    """Addresses of the instructions reachable from the entry over direct branches (a symbol's
    range can hold another, unnamed function after the last RET); all of them if a switch table
    jumps."""
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
                return {address for address, _, _ in rows}  # switch table
            if mnemonic.startswith("j") and target:
                todo.append(int(target.group(1), 16))
            if mnemonic in ("ret", "jmp", "iret"):
                break
            address += length
    return seen


def call_table(image, operand):
    """call [table] or [reg*4+table] through a table in .rdata or .data (not randomized: the call is no
    global read, so the test keeps the image's value): its leading words that point into code (the
    NAL entropy modes, 0x008bb4a0). An index past them reads other data; such an input crashes
    the original (or calls whatever code address follows: rare)."""
    fixed = re.fullmatch(r"DWORD PTR ds:0x([0-9a-f]+)", operand)  # one slot (a vtable's, 0x007b4434)
    m = fixed or re.fullmatch(r"DWORD PTR \[e\w\w\*4\+0x([0-9a-f]+)\]", operand)
    if not m or image.section(int(m.group(1), 16)) not in (".rdata", ".data"):
        return set()
    targets, va = set(), int(m.group(1), 16)
    while True:
        raw = image.read(va, 4)
        if len(raw) < 4:  # .bss: filled at run time
            return targets
        (word,) = struct.unpack("<I", raw)
        if word < 0x10000 or image.section(word) in fnpack.DATA_SECTIONS or image.section(word) is None:
            return targets
        targets.add(word)
        if fixed:
            return targets
        va += 4


def scan(image, starts, va):
    i = bisect.bisect_right(starts, va)
    end = starts[i] if i < len(starts) else va + 0x400
    rows = fnpack.disassemble(image, va, min(end - va, 0x4000))
    size = rows[-1][0] + rows[-1][1] - va if rows else 0
    callees, problems, reads, writes, addressed, indexed = set(), set(), set(), set(), set(), set()
    live = reachable(rows, va)
    pushed = []  # immediates pushed since the last call
    for address, _, text in rows:
        if address not in live:
            continue
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
                if target in ARGUMENT_CALLERS:
                    code = {p for p in pushed if image.section(p) not in fnpack.DATA_SECTIONS | {None}}
                    callees |= code
                    if not code:
                        problems.add("indirect call (argument)")
                if mnemonic == "call":
                    pushed = []
            elif mnemonic == "call" and (table := call_table(image, joined)):
                callees |= table
            elif not (mnemonic == "jmp" and re.search(r"\*4\+0x", joined)):  # switch tables stay inside
                problems.add(f"indirect {mnemonic}")
            continue
        if mnemonic == "push" and (m := re.fullmatch(r"0x([0-9a-f]+)", joined)):
            pushed.append(int(m.group(1), 16))
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
    return {"size": size, "count": len(live), "callees": callees, "problems": problems,
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
        if va in STANDINS:
            return None
        if image.section(va) in HARDWARE_SECTIONS:
            return f"calls SDK {functions.get(va, hex(va))}"
        if depth > 12:
            return "call depth"
        r = info(va)
        problems = r["problems"] - ({"indirect jmp", "indirect call"} if va in PINNED_INDIRECT | ARGUMENT_CALLERS else set())
        if problems:
            where = "" if depth == 0 else f" in {functions.get(va, hex(va))}"
            return ",".join(sorted(problems)) + where
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
        if va in seen or va in STANDINS:
            return set()
        seen.add(va)
        r = info(va)
        out = set(r["reads"]) - PINNED
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
        # The test finds functions by kFunctions' name (Ghidra's can still be FUN_xxxxxxxx).
        table = {int(a, 16): n for n, a in
                 re.findall(r'\{"(\w+)", 0x([0-9a-fA-F]+),', (fnpack.ROOT / "game/code/functions.cpp").read_text())}
        for va, name, _ in eligible:
            name = table.get(va, name)
            if name in pending:
                continue
            r = info(va)
            globals_ = ", ".join(f"0x{g:08x}" for g in sorted(tree_globals(va)) if not constant(g))
            out.append(f'{{"{name}", 0x{va:08x}, {r["size"]}, {r["count"]}, {{{globals_}}}}},')
        with open(args.emit, "w") as f:
            f.write("\n".join(out) + "\n")


if __name__ == "__main__":
    main()
