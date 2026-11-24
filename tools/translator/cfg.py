#!/usr/bin/env python3
"""cfg.py: recursive-descent decoder + control-flow graph for a level section (translator step 1).
usage: build/translator_venv/bin/python tools/translator/cfg.py [LEVEL] [--out FILE] [--list CLASS]
Starts at every exported function start in the section, follows jcc/jmp/fall-through (never decodes data the way a
linear sweep does), splits basic blocks at leaders and classifies each function by what the lifter needs:
  intleaf  integer core only, no calls          (prototype step 2)
  int      integer core + direct calls           (step 3: calls)
  sse/x87/fs/indirect/switch/string/other        (later steps; a function gets its hardest class)
Writes a per-function report (default build/translator/<level>_cfg.txt) and prints the class counts.
"""
import argparse
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

JCC = {"jo", "jno", "jb", "jae", "je", "jne", "jbe", "ja", "js", "jns", "jp", "jnp", "jl", "jge", "jle", "jg"}
JCXZ = {"jecxz", "jcxz", "loop", "loope", "loopne"}
SETCC = {"set" + c[1:] for c in JCC}
CMOV = {"cmov" + c[1:] for c in JCC}
# The integer core the step-2 lifter handles (flags lazily; jcc/setcc/cmov read them).
INT_CORE = {"mov", "movzx", "movsx", "lea", "add", "sub", "adc", "sbb", "and", "or", "xor", "not", "neg", "inc",
            "dec", "shl", "shr", "sar", "rol", "ror", "shld", "shrd", "imul", "mul", "idiv", "div", "cdq", "cwde",
            "cmp", "test", "jmp", "push", "pop", "ret", "nop", "xchg", "bt", "bswap", "leave",
            "movsb", "movsw", "movsd", "stosb", "stosw", "stosd"} | JCC | SETCC | CMOV
X87_PREFIX = ("f",)
SSE = {"movss", "addss", "subss", "mulss", "divss", "sqrtss", "minss", "maxss", "ucomiss", "comiss", "cvtsi2ss",
       "cvttss2si", "cvtss2si", "xorps", "andps", "andnps", "orps", "movaps", "movups", "rsqrtss", "rcpss", "movd",
       "shufps", "unpcklps", "movlps", "movhps", "cmpss", "cvtss2sd", "cvtsd2ss", "movsd", "addps", "mulps",
       "subps", "maxps", "minps", "movq", "movlhps", "movhlps", "cvtps2pd", "cvtdq2ps", "cvttps2dq", "pxor"}
RANK = ["intleaf", "int", "string", "sse", "x87", "flagsraw", "switch", "indirect", "fs", "other", "broken"]


class Fn:
    def __init__(self, start, name):
        self.start, self.name = start, name
        self.ins = {}          # address -> capstone insn
        self.leaders = {start}
        self.calls = set()     # direct call targets
        self.tails = set()     # jmp to another function start (tail call)
        self.notes = collections.Counter()
        self.mnemonics = collections.Counter()
        self.cls = "intleaf"
        self.end = start       # highest decoded byte + 1
        self.arg_pop = None    # ret imm16 (None = never returns through ret)

    def worse(self, cls):
        if RANK.index(cls) > RANK.index(self.cls):
            self.cls = cls


def decode(fn, image, md, lo, hi, starts):
    work = [fn.start]
    while work:
        pc = work.pop()
        while True:
            if pc in fn.ins:
                break
            if not lo <= pc < hi:
                fn.notes[f"leaves section at {pc:#x}"] += 1
                fn.worse("broken")
                break
            if pc != fn.start and pc in starts:  # fell into the next exported function
                fn.notes[f"falls into {starts[pc]}"] += 1
                fn.tails.add(pc)
                fn.worse("int")
                break
            ins = next(md.disasm(image.read(pc, 16), pc), None)
            if ins is None:
                fn.notes[f"undecodable at {pc:#x}"] += 1
                fn.worse("broken")
                break
            fn.ins[pc] = ins
            fn.end = max(fn.end, pc + ins.size)
            m = ins.mnemonic
            fn.mnemonics[m] += 1
            classify(fn, ins)
            nxt = pc + ins.size
            op0 = ins.operands[0] if ins.operands else None
            if m in JCC or m in JCXZ:
                tgt = op0.imm
                fn.leaders.update((tgt, nxt))
                work.append(tgt)
                pc = nxt
                continue
            if m == "jmp":
                if op0.type == x86.X86_OP_IMM:
                    tgt = op0.imm
                    if (tgt in starts and tgt != fn.start) or not lo <= tgt < hi:
                        fn.tails.add(tgt)  # tail call (into another level function or the engine)
                        fn.worse("int")
                    else:
                        fn.leaders.add(tgt)
                        work.append(tgt)
                else:
                    targets = switch_table(image, ins, lo, hi)
                    if targets:
                        fn.notes["switch table"] += 1
                        fn.worse("switch")
                        fn.leaders.update(targets)
                        work.extend(targets)
                    else:
                        fn.notes["indirect jmp"] += 1
                        fn.worse("indirect")
                break
            if m == "call":
                if op0.type == x86.X86_OP_IMM:
                    fn.calls.add(op0.imm)
                    fn.worse("int")
                else:
                    fn.notes["indirect call"] += 1
                    fn.worse("indirect")
                pc = nxt
                continue
            if m in ("ret", "retn"):
                pop = op0.imm if op0 is not None else 0
                if fn.arg_pop not in (None, pop):
                    fn.notes["mixed ret sizes"] += 1
                fn.arg_pop = pop
                break
            if m in ("int3", "hlt", "ud2", "int"):
                fn.notes[f"{m} reached"] += 1
                break
            pc = nxt


def switch_table(image, ins, lo, hi):
    """jmp dword ptr [reg*4 + table]: read targets until one leaves the section (MSVC places the table in code)."""
    op = ins.operands[0]
    if op.type != x86.X86_OP_MEM or op.mem.scale != 4 or op.mem.base != 0 or op.mem.disp == 0:
        return None
    table, out = op.mem.disp & 0xFFFFFFFF, []
    for i in range(256):
        sec = image.section(table + 4 * i)
        if sec is None:
            break
        (word,) = __import__("struct").unpack("<I", image.read(table + 4 * i, 4))
        if not lo <= word < hi:
            break
        out.append(word)
    return out or None


def classify(fn, ins):
    m = ins.mnemonic
    for op in ins.operands:
        if op.type == x86.X86_OP_MEM and op.mem.segment != 0:
            fn.notes["fs segment"] += 1
            fn.worse("fs")
    if m.startswith(("rep ", "repe ", "repne ")):
        fn.notes["rep string op"] += 1
        fn.worse("string")
        return
    if m in SSE:
        if not (m == "movsd" and not ins.operands):  # movsd without operands = string op
            fn.worse("sse")
            return
    if m in ("lahf", "sahf", "pushfd", "popfd"):
        fn.worse("flagsraw")
        return
    if m.startswith(X87_PREFIX) and m not in ("fs",):
        fn.worse("x87")
        return
    if m in INT_CORE or m in JCXZ or m in ("call", "ret", "int3"):
        return
    fn.notes[f"other: {m}"] += 1
    fn.worse("other")


def blocks(fn):
    """[(start, [insn...])] in address order; a block ends at a leader, a branch or a gap."""
    out, cur = [], None
    for a in sorted(fn.ins):
        ins = fn.ins[a]
        if cur is None or a in fn.leaders or a != cur[-1].address + cur[-1].size:
            cur = []
            out.append((a, cur))
        cur.append(ins)
        if ins.mnemonic in JCC or ins.mnemonic in JCXZ or ins.mnemonic in ("jmp", "ret", "int3"):
            cur = None
    return out


def main():
    ap = argparse.ArgumentParser(usage=__doc__)
    ap.add_argument("level", nargs="?", default="PROLOGUE")
    ap.add_argument("--out")
    ap.add_argument("--list", help="print the functions of one class")
    args = ap.parse_args()
    functions, _ = fnpack.load_names()
    _, ours = fnpack.load_states()
    image = fnpack.Image()
    sec = next(s for s in image.sections if s[0] == args.level)
    lo, hi = sec[1], sec[1] + max(sec[2], sec[4])
    starts = {a: n for a, n in functions.items() if lo <= a < hi}
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    fns = []
    for a in sorted(starts):
        fn = Fn(a, starts[a])
        decode(fn, image, md, lo, hi, starts)
        fns.append(fn)
    # Call targets outside the export list (functions the symbols missed).
    order = sorted(starts)
    missed = collections.Counter()
    for fn in fns:
        for t in fn.calls | fn.tails:
            if lo <= t < hi and t not in starts:
                missed[t] += 1
    # Overlap: an instruction decoded by two functions (shared tails / misplaced starts).
    owner = {}
    shared = collections.Counter()
    for fn in fns:
        for a in fn.ins:
            if a in owner and owner[a] is not fn:
                shared[fn.start] += 1
            owner[a] = fn
    counts = collections.Counter(fn.cls for fn in fns)
    ins_by = collections.Counter()
    for fn in fns:
        ins_by[fn.cls] += len(fn.ins)
    out = args.out or f"build/translator/{args.level.lower()}_cfg.txt"
    os.makedirs(os.path.dirname(out), exist_ok=True)
    with open(out, "w") as f:
        for fn in fns:
            bl = blocks(fn)
            f.write(f"{fn.start:#010x} {fn.name} class={fn.cls} ins={len(fn.ins)} blocks={len(bl)} "
                    f"end={fn.end:#x} ret={fn.arg_pop} calls={len(fn.calls)} tails={len(fn.tails)}"
                    f"{' C' if fn.start in ours else ''}{' shared=' + str(shared[fn.start]) if shared[fn.start] else ''}\n")
            for k, n in sorted(fn.notes.items()):
                f.write(f"    note {k} x{n}\n")
    print(f"{args.level}: {len(fns)} functions, {sum(len(f.ins) for f in fns)} instructions decoded -> {out}")
    for c in RANK:
        if counts[c]:
            print(f"  {c:9s} {counts[c]:5d} functions {ins_by[c]:7d} instructions")
    print(f"  call targets inside the section without a symbol: {len(missed)}")
    print(f"  functions sharing decoded bytes with another: {sum(1 for v in shared.values() if v)}")
    print(f"  already C rows: {sum(1 for fn in fns if fn.start in ours)}")
    other = collections.Counter()
    for fn in fns:
        for k in fn.notes:
            if k.startswith("other: "):
                other[k[7:]] += 1
    if other:
        print("  'other' mnemonics: " + ", ".join(f"{m} {n}" for m, n in other.most_common()))
    if args.list:
        for fn in fns:
            if fn.cls == args.list:
                print(f"{fn.start:#010x} {fn.name} ins={len(fn.ins)} ret={fn.arg_pop}")
    del order


if __name__ == "__main__":
    main()
