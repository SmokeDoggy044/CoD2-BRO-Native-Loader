#!/usr/bin/env python3
"""Finds C calls into title code that pass 0 in a register the callee needs where the original passes a value.

The callee's needs: tools/regflow.py (flow-sensitive: branches, direct calls, saves and slot pushes are not reads).
Ground truth: the original of the enclosing kFunctions function, at each call to the same callee, the last write to
that register before the call (back across calls, which clobber only EAX/ECX/EDX; stopping at ret/jmp): a load
(mov/lea/movzx/...) is a candidate, `xor r,r` (0, as the C) or no write found is not, a pop is a restore.
Checks vehicle_call(...) (EAX, ECX, EDX, ESI, EDI) and TitleRegs{EAX, EBX, ECX, EDX, ESI, EDI} literals whose call
statement has the callee as a literal (indirect calls skipped).
11-05 review: all 82 hits of the first version were false; the pre-fix ClientThink_RunPendingTouch EDI bug is the
reference case this version must find.

usage: tools/regpass.py [FILE.cpp...]   (default: src/engine/*.cpp)"""
import glob
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import regflow

names = regflow.names
ORDER_TR = ["eax", "ebx", "ecx", "edx", "esi", "edi"]
ORDER_VC = ["eax", "ecx", "edx", "esi", "edi"]
LOW = {"eax": "al", "ebx": "bl", "ecx": "cl", "edx": "dl"}


def needs(addr):
    return {r for r in regflow.analyse(addr) if r in ORDER_TR}


def split_args(s):
    out, d, cur = [], 0, ""
    for ch in s:
        d += ch in "({["
        d -= ch in ")}]"
        if ch == "," and d == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    out.append(cur.strip())
    return out


def original_value(owner_addr, callee, reg):
    """'load' / 'zero' / None per call site of callee in the original owner."""
    rows = regflow.body(owner_addr)
    found = []
    for k, (a, _, t) in enumerate(rows):
        if not (t.startswith("call") and f"0x{callee:x}" in t):
            continue
        verdict = None
        for j in range(k - 1, max(-1, k - 41), -1):
            tt = rows[j][2]
            mn, _, ops = tt.partition(" ")
            if mn in ("ret", "jmp"):
                break
            if mn == "call":
                if reg in ("eax", "ecx", "edx"):
                    verdict = "clobbered"
                    break
                continue
            parts = [p.strip() for p in re.split(r",(?![^\[]*\])", ops)] if ops else []
            dst = parts[0] if parts else ""
            if dst in (reg, LOW.get(reg)):
                if mn in ("xor", "sub") and len(parts) == 2 and parts[0] == parts[1]:
                    verdict = "zero"
                elif mn == "pop":
                    verdict = "restore"
                elif mn in ("mov", "lea", "movzx", "movsx") or mn.startswith(("set", "cvt")):
                    verdict = "load" if not (mn == "mov" and parts[1] in ("0x0",)) else "zero"
                else:
                    verdict = "load"
                break
        found.append(verdict)
    return found


src = (regflow.fnpack.ROOT / "src/engine/shared/functions.cpp").read_text()
row_addr = {n: int(a, 16) for n, a in re.findall(r'\{"(\w+)", 0x([0-9a-f]{8}),', src)}
hits = []
files = sys.argv[1:] or sorted(glob.glob(str(regflow.fnpack.ROOT / "src/engine/*.cpp")))
for f in files:
    text = open(f).read()
    lines = text.split("\n")

    def owner_of(line):
        for j in range(line - 1, -1, -1):
            m = re.match(r"^[A-Za-z][^;=]*?\b(\w+)\(", lines[j])
            if m and m.group(1) in row_addr and not lines[j].startswith(("static", "using", "PLATFORM", "#")):
                return m.group(1)
        return None

    for m in re.finditer(r"vehicle_call\((0x00[0-9a-f]{6}),([^;]*)\);", text):
        a = split_args(m.group(2))
        if len(a) >= 5:
            hits.append((f, text[:m.start()].count("\n") + 1, int(m.group(1), 16), dict(zip(ORDER_VC, a[:5]))))
    for m in re.finditer(r"TitleRegs\s*(\w*)\s*\{([^}]*)\}", text):
        vals = split_args(m.group(2))
        if len(vals) != 6:
            continue
        line = text[:m.start()].count("\n") + 1
        if m.group(1):  # named: the call statement that passes &name within the next 6 lines
            tail = text[m.end():]
            c = re.search(r"([^;]*&" + m.group(1) + r"\b[^;]*;)", tail[:600])
            stmt = c.group(1) if c else ""
        else:  # inline: its own statement
            start = text.rfind(";", 0, m.start()) + 1
            stmt = text[start:text.find(";", m.end()) + 1]
            here = m.start() - start
        # the callee: the last function-start literal before the TitleRegs in its call (call_at(frame, return
        # address, callee, TitleRegs{..})), else the first after it
        at = here if not m.group(1) else stmt.find("&" + m.group(1))
        before = [int(x, 16) for x in re.findall(r"\b(0x00[0-9a-f]{6})\b", stmt[:max(at, 0)]) if int(x, 16) in names]
        after = [int(x, 16) for x in re.findall(r"\b(0x00[0-9a-f]{6})\b", stmt[max(at, 0):]) if int(x, 16) in names]
        if m.group(1):
            before = [int(x, 16) for x in re.findall(r"\b(0x00[0-9a-f]{6})\b", stmt) if int(x, 16) in names]
            after = []
        cands = before[-1:] or after[:1]
        if cands:
            hits.append((f, line, cands[0], dict(zip(ORDER_TR, vals))))

    for f2, line, callee, regs in [h for h in hits if h[0] == f]:
        zero = sorted(r for r, v in regs.items() if v == "0" and r in needs(callee))
        if not zero:
            continue
        owner = owner_of(line)
        if not owner:
            print(f"{Path(f).name}:{line} ? -> {names.get(callee, hex(callee))}: 0 in {zero} (owner not found)")
            continue
        # a C line cannot be mapped to one original call site: flag only when no site passes 0
        sites = {r: original_value(row_addr[owner], callee, r) for r in zero}
        real = [r for r in zero if "load" in sites[r] and "zero" not in sites[r]]
        if real:
            print(f"{Path(f).name}:{line} {owner} -> {names.get(callee, hex(callee))}: 0 in {real}, original loads it")
