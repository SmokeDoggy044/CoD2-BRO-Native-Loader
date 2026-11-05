#!/usr/bin/env python3
"""Finds C calls into title code that pass 0 for a register the callee needs.

A callee needs a register when it reads it before writing it, or when it reaches a direct call
with the register unwritten and that callee needs it (registers passed through, e.g.
G_NotifyWithOwnHandle hands EDI to G_NotifyWithEntity). Linear scan, so conditional writes
count as writes: a hit is a candidate, check the disassembly.
Checks vehicle_call(...) (EAX, ECX, EDX, ESI, EDI) and TitleRegs{EAX, EBX, ECX, EDX, ESI, EDI}
literals whose call with a literal title address follows within 6 lines.

usage: tools/regpass.py [FILE.cpp...]   (default: game/code/*.cpp)"""
import glob
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import fnpack

img = fnpack.Image()
syms = sorted((int(l.split()[0], 16), l.split()[1]) for l in open(fnpack.DATA / "default_xbe_symbols.txt")
              if l.strip() and not l.startswith("#"))
starts = [a for a, _ in syms]
names = dict(syms)
REGS = {"eax": r"\b(eax|ax|al|ah)\b", "ebx": r"\b(ebx|bx|bl|bh)\b", "ecx": r"\b(ecx|cx|cl|ch)\b",
        "edx": r"\b(edx|dx|dl|dh)\b", "esi": r"\b(esi|si)\b", "edi": r"\b(edi|di)\b"}
memo = {}


def body(addr):
    import bisect
    i = bisect.bisect_right(starts, addr)
    end = starts[i] if i < len(starts) else addr + 0x400
    return fnpack.disassemble(img, addr, min(end - addr, 0x3000))


def needs(addr, depth=0):
    if addr in memo:
        return memo[addr]
    memo[addr] = set()
    written, need = set(), set()
    for _, _, text in body(addr):
        mnem, _, ops = text.partition(" ")
        ops = re.sub(r"<[^>]*>", "", ops)
        parts = [p.strip() for p in re.split(r",(?![^\[]*\])", ops)] if ops else []
        if mnem == "call" and depth < 4:
            m = re.fullmatch(r"0x([0-9a-f]+)", parts[0] if parts else "")
            if m:
                need |= {r for r in needs(int(m.group(1), 16), depth + 1) if r not in written}
            written |= {"eax", "ecx", "edx"}
            continue
        if mnem in ("ret", "jmp") and not (mnem == "jmp" and parts and re.fullmatch(r"0x[0-9a-f]+", parts[0])):
            break
        srcs = parts[1:] if mnem not in ("push", "cmp", "test", "xchg") else parts
        dst = parts[0] if parts else ""
        if mnem in ("xor", "sub") and len(parts) == 2 and parts[0] == parts[1]:
            srcs = []
        for r, pat in REGS.items():
            if r in written:
                continue
            reads = any(re.search(pat, s) for s in srcs) or ("[" in dst and re.search(pat, dst)) or \
                (mnem not in ("mov", "lea", "movzx", "movsx", "pop", "setne", "sete") and re.search(pat, dst) and "[" not in dst)
            if reads:
                need.add(r)
        for r, pat in REGS.items():
            if re.fullmatch(pat, dst) or (mnem == "pop" and re.fullmatch(pat, dst)):
                written.add(r)
    memo[addr] = need
    return need


def split_args(s):
    out, d, cur = [], 0, ""
    for ch in s:
        d += ch in "({[" ; d -= ch in ")}]"
        if ch == "," and d == 0:
            out.append(cur.strip()); cur = ""
        else:
            cur += ch
    out.append(cur.strip())
    return out


HITS = []
files = sys.argv[1:] or sorted(glob.glob(str(fnpack.ROOT / "game/code/*.cpp")))
for f in files:
    lines = open(f).read().split("\n")
    text = "\n".join(lines)
    for m in re.finditer(r"vehicle_call\((0x00[0-9a-f]{6}),([^;]*)\);", text):
        a = split_args(m.group(2))
        if len(a) < 5:
            continue
        regs = dict(zip(["eax", "ecx", "edx", "esi", "edi"], a[:5]))
        addr = int(m.group(1), 16)
        bad = sorted(r for r in needs(addr) if regs.get(r) == "0")
        if bad:
            HITS.append((f, text[:m.start()].count(chr(10)) + 1, addr, bad))
    for i, l in enumerate(lines):
        m = re.search(r"TitleRegs\s*(\w*)\s*\{([^}]*)\}", l)
        if not m:
            continue
        vals = split_args(m.group(2))
        if len(vals) != 6:
            continue
        regs = dict(zip(["eax", "ebx", "ecx", "edx", "esi", "edi"], vals))
        window = "\n".join(lines[i:i + 7])
        # the callee: the first literal after the TitleRegs that starts a named function (return addresses
        # passed to call_at helpers are mid-function)
        rest = window[len(l):] if m.group(1) else window[m.end():]
        cands = [int(x, 16) for x in re.findall(r"\b(0x00[0-9a-f]{6})\b", window[:len(l)][:m.start()] + " " + rest)]
        cands = [x for x in cands if x in names]
        if not cands:
            continue
        addr = cands[0]
        if not 0x11000 <= addr < 0x380000:
            continue
        bad = sorted(r for r in needs(addr) if regs.get(r) == "0")
        if bad:
            HITS.append((f, i + 1, addr, bad))


# Ground truth: the original of the C function around the hit. Its calls to the same callee: does it load
# the register (mov/lea/movzx/pop, not xor r,r) within the 8 instructions before the call?
src = (fnpack.ROOT / "game/code/functions.cpp").read_text()
row_addr = {n: int(a, 16) for n, a in re.findall(r'\{"(\w+)", 0x([0-9a-f]{8}),', src)}
DEF = re.compile(r"^[A-Za-z].*?\b(\w+)\(.*\)\s*(const)?\s*\{?\s*$")
for f, line, addr, bad in HITS:
    lines = open(f).read().split("\n")
    owner = None
    for j in range(line - 1, -1, -1):
        m = re.match(r"^[A-Za-z][^;=]*?\b(\w+)\(", lines[j])
        if m and not lines[j].startswith(("static", "namespace", "using", "PLATFORM", "#")) and m.group(1) in row_addr:
            owner = m.group(1); break
    if not owner:
        print(f"{Path(f).name}:{line} {names.get(addr, hex(addr))} gets 0 in {bad} (owner not found)")
        continue
    rows = body(row_addr[owner])
    sites = [k for k, (a, n, t) in enumerate(rows) if t.startswith("call") and f"0x{addr:x}" in t]
    loaded = set()
    for k in sites:
        for a, n, t in rows[max(0, k - 8):k]:
            mn, _, ops = t.partition(" ")
            dst = ops.split(",")[0].strip()
            for r in bad:
                if re.fullmatch(REGS[r], dst) and mn in ("mov", "lea", "movzx", "movsx", "pop") :
                    loaded.add(r)
    real = sorted(set(bad) & loaded)
    if real or not sites:
        tag = "REAL?" if real else "no direct call in the original"
        print(f"{Path(f).name}:{line} {owner} -> {names.get(addr, hex(addr))}: 0 in {bad}, original loads {real} [{tag}]")
