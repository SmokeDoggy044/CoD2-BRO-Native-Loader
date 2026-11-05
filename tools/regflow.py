#!/usr/bin/env python3
"""Flow-sensitive register-input check: for FUNC, which registers can be read before written on some
path (with the first read sites), following conditional branches and recursing into direct calls.
usage: tools/regflow.py FUNC [REG...]  (written by a register-review agent, 11-05)"""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fnpack  # noqa

img = fnpack.Image()
syms = sorted((int(l.split()[0], 16), l.split()[1]) for l in open(fnpack.DATA / "default_xbe_symbols.txt")
              if l.strip() and not l.startswith("#"))
names = dict(syms)
by_name = {n: a for a, n in syms}
starts = [a for a, _ in syms]
R = ["eax", "ebx", "ecx", "edx", "esi", "edi"]
PAT = {"eax": r"\b(eax|ax|al|ah)\b", "ebx": r"\b(ebx|bx|bl|bh)\b", "ecx": r"\b(ecx|cx|cl|ch)\b",
       "edx": r"\b(edx|dx|dl|dh)\b", "esi": r"\b(esi|si)\b", "edi": r"\b(edi|di)\b"}
memo = {}


def body(addr):
    import bisect
    i = bisect.bisect_right(starts, addr)
    end = starts[i] if i < len(starts) else addr + 0x400
    return fnpack.disassemble(img, addr, min(end - addr, 0x4000))


def regs_in(s):
    return {r for r, p in PAT.items() if re.search(p, s)}


def effects(text):
    """(reads, writes, kind, target)"""
    mnem, _, ops = text.partition(" ")
    ops = re.sub(r"<[^>]*>", "", ops)
    pre = ""
    if mnem in ("rep", "repz", "repnz", "repe", "repne"):
        pre, (mnem, _, ops) = mnem, ops.partition(" ")
    parts = [p.strip() for p in re.split(r",(?![^\[]*\])", ops)] if ops else []
    reads, writes = set(), set()
    for p in parts:
        if "[" in p:
            reads |= regs_in(p[p.index("["):])
    dst = parts[0] if parts else ""
    dreg = regs_in(dst) if "[" not in dst else set()
    src = parts[1:]
    for p in src:
        if "[" not in p:
            reads |= regs_in(p)
    # alignment no-ops (lea r,[r+0x0], mov r,r) neither read nor write
    if (mnem == "lea" and len(parts) == 2 and re.fullmatch(r"\[(\w+)\+0x0\]", parts[1].replace(" ", ""))
            and parts[1].strip("[]").split("+")[0] == parts[0]) or (mnem == "mov" and len(parts) == 2 and parts[0] == parts[1]):
        return set(), set(), "", None
    if mnem.startswith("j"):
        kind = "jmp" if mnem == "jmp" else "jcc"
        m = re.fullmatch(r"0x([0-9a-f]+)", dst)
        if not m:
            return reads | dreg, writes, "indirect", None
        return reads, writes, kind, int(m.group(1), 16)
    if mnem == "call":
        m = re.fullmatch(r"0x([0-9a-f]+)", dst)
        return reads | dreg, writes, "call", int(m.group(1), 16) if m else None
    if mnem == "ret":
        return reads | {"eax"} - {"eax"}, writes, "ret", None
    if mnem in ("xor", "sub", "sbb") and len(parts) == 2 and parts[0] == parts[1]:
        return set(), dreg, "", None
    if mnem in ("mov", "lea", "movzx", "movsx", "movss", "movsd", "movd", "cvttss2si", "cvtss2si") or mnem.startswith("set"):
        writes |= dreg
    elif mnem == "pop":
        writes |= dreg
    elif mnem == "push":
        pass  # saves and slot reserves; a pushed argument shows where the callee reads its stack slot
    elif mnem == "or" and len(parts) == 2 and parts[1] in ("0xffffffff",):
        writes |= dreg
    elif mnem in ("cmp", "test"):
        reads |= dreg
    elif mnem == "xchg":
        reads |= regs_in(ops); writes |= regs_in(ops)
    elif mnem == "cdq":
        reads.add("eax"); writes.add("edx")
    elif mnem in ("stos", "movs", "lods", "scas", "cmps"):
        if "stos" in mnem or mnem == "stos": reads |= {"edi", "eax"}
        if mnem == "movs": reads |= {"esi", "edi"}
        if mnem == "lods": reads.add("esi")
        if mnem in ("scas", "cmps"): reads |= {"edi", "eax"}
        if pre: reads.add("ecx")
    elif mnem in ("div", "idiv") and len(parts) == 1:
        reads |= {"eax", "edx"}; writes |= {"eax", "edx"}
    elif mnem in ("mul",) or (mnem == "imul" and len(parts) == 1):
        reads.add("eax"); writes |= {"eax", "edx"}
    elif mnem in ("fnstsw", "lahf"):
        writes.add("eax")
    elif mnem == "sahf":
        reads.add("eax")
    else:
        reads |= dreg; writes |= dreg
    return reads, writes, "", None


def analyse(addr, depth=0, want=None):
    """returns {reg: first_read_desc}"""
    if addr in memo:
        return memo[addr]
    memo[addr] = {}
    rows = body(addr)
    idx = {a: k for k, (a, _, _) in enumerate(rows)}
    need = {}
    seen = {}
    work = [(0, frozenset(R))]
    while work:
        k, unw = work.pop()
        while k < len(rows):
            a, _, t = rows[k]
            prev = seen.get(a)
            if prev is not None and unw <= prev:
                break
            seen[a] = (prev or frozenset()) | unw
            unw = seen[a]
            reads, writes, kind, tgt = effects(t)
            if t.startswith("push ") and t[5:] in unw and depth == 0:
                need.setdefault("push " + t[5:], f"{a:08x} {t}")
            for r in reads & unw:
                need.setdefault(r, f"{a:08x} {t}")
            if kind == "call":
                if tgt is not None and depth < 5:
                    sub = analyse(tgt, depth + 1)
                    for r, d in sub.items():
                        if r in unw:
                            need.setdefault(r, f"{a:08x} -> {names.get(tgt, hex(tgt))}: {d}")
                unw = unw - {"eax", "ecx", "edx"}
                k += 1
                continue
            unw = unw - writes
            if kind == "ret" or kind == "indirect":
                if kind == "indirect":
                    need.setdefault("?", f"{a:08x} indirect jump {t}")
                break
            if kind in ("jmp", "jcc"):
                if tgt in idx:
                    work.append((idx[tgt], unw))
                elif kind == "jmp":
                    # tail jump to another function
                    sub = analyse(tgt, depth + 1) if depth < 5 else {}
                    for r, d in sub.items():
                        if r in unw:
                            need.setdefault(r, f"{a:08x} tail -> {names.get(tgt, hex(tgt))}: {d}")
                if kind == "jmp":
                    break
            k += 1
    memo[addr] = need
    return need


if __name__ == "__main__":
    f = sys.argv[1]
    a = by_name.get(f) or int(f, 16)
    res = analyse(a)
    only = sys.argv[2:]
    for r, d in sorted(res.items()):
        if not only or r in only:
            print(f"{names.get(a, f)} needs {r}: {d}")
    if not res:
        print(f"{names.get(a, f)} needs nothing")
