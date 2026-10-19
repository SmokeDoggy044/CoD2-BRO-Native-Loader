#!/usr/bin/env python3
"""Everything needed to start decompiling a function, in one compact text: size,
state, callers, callees, globals, strings, a guess at the calling convention, the
traps from the skill's decompiling notes, and the disassembly with names inline.
Reads the XBE and the exported symbols only (no Ghidra needed).

usage: tools/fnpack.py FUNCTION... [--asm DIR] [--no-asm]
  FUNCTION   hex address or symbol name
  --asm DIR  write each disassembly to DIR/<name>.asm (input for ssesym.py/psssym.py)
             instead of printing it
  --no-asm   no disassembly

The convention lines are a linear scan in address order, not a flow analysis: a
register read before any write on that scan is an argument candidate. Check those
against the disassembly where branches jump backwards or skip the writes.
"""
import argparse
import bisect
import csv
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "tools" / "data"
XBE = ROOT.parent / "unpacked" / "default.xbe"
SDK_SECTIONS = {"XGRPH", "XONLINE", "DSOUND", "XNET", "D3DX", "D3D", "XPP"}
DATA_SECTIONS = {".rdata", ".data", ".data1", "XON_RD", "DOLBY", ".bss"}

REG32 = {"eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp"}
SUB = {"al": "eax", "ah": "eax", "ax": "eax", "bl": "ebx", "bh": "ebx", "bx": "ebx", "cl": "ecx", "ch": "ecx",
       "cx": "ecx", "dl": "edx", "dh": "edx", "dx": "edx", "si": "esi", "di": "edi", "bp": "ebp", "sp": "esp"}
ARG_REGS = ["eax", "edx", "ecx", "ebx", "esi", "edi"] + [f"xmm{i}" for i in range(8)]
WRITE_ONLY = re.compile(r"^(mov|movzx|movsx|lea|pop|set\w+|movs[sd]|movap[sd]|movup[sd]|movaps|movups|movd|movq|"
                        r"movhlps|movlhps|movlp[sd]|movhp[sd]|cvt\w+|sqrtss|sqrtps|rcp[sp]s|rsqrt[sp]s|pshufd|"
                        r"xorps_self|fnstsw|stmxcsr)$")
NO_DEST = re.compile(r"^(cmp|test|comis[sd]|ucomis[sd]|push|j\w+|call|ret|bt|fld\w*|fild|fcom\w*|fucom\w*|ldmxcsr)$")
FLOAT_NOTES = [
    (r"^f(sin|cos|sincos)$", "x87 fsin/fcos: compiler.h helper (libm differs)"),
    (r"^fptan$", "x87 fptan: compiler.h helper"),
    (r"^fsqrt$", "x87 fsqrt: inside a comparison it needs the x87 helper"),
    (r"^rcp[sp]s$", "rcpss/rcpps estimate: platform_rcp_estimate"),
    (r"^rsqrt[sp]s$", "rsqrtss/rsqrtps estimate: platform_rsqrt_estimate"),
    (r"^u?comis[sd]$", "comiss branches: read jbe/jae per branch for NaN (Ghidra's C flips them)"),
    (r"^(min|max)[sp]s$", "minss/maxss: picks an operand on NaN, keep the operand order"),
    (r"^div[p]s$", "packed divps: compute the unused lanes too (MXCSR flags), platform_keep"),
    (r"^fild$", "fild: maybe MSVC unsigned-to-float (fild + fadd 2^32): platform_x87_unsigned_to_float"),
    (r"^fistp$", "fistp: _ftol-style truncation through int64"),
    (r"^andnps$|^andps$", "andps/andnps: fabs/sign masks - clear the sign bit, not std::fabs"),
]


def xbe_sections(data):
    base = struct.unpack_from("<I", data, 0x104)[0]
    count, table = struct.unpack_from("<II", data, 0x11C)
    for i in range(count):
        _, va, vsize, raw, raw_size, name_at = struct.unpack_from("<6I", data, table - base + i * 56)
        name = data[name_at - base:name_at - base + 16].split(b"\0")[0].decode(errors="replace")
        yield name, va, vsize, raw, raw_size


class Image:
    def __init__(self):
        self.data = XBE.read_bytes()
        self.sections = list(xbe_sections(self.data))

    def section(self, va):
        for name, start, vsize, raw, raw_size in self.sections:
            if start <= va < start + max(vsize, raw_size):
                return name
        return None

    def read(self, va, length):
        for _, start, vsize, raw, raw_size in self.sections:
            if start <= va < start + raw_size:
                return self.data[raw + va - start:raw + min(va - start + length, raw_size)]
        return b""

    def string_at(self, va):
        raw = self.read(va, 80)
        text = raw.split(b"\0")[0]
        if len(text) >= 3 and all(32 <= c < 127 or c in (9, 10) for c in text):
            return text.decode() + ("..." if len(text) == 80 else "")
        return None

    def code_refs(self, targets):
        """CALL/JMP rel32 sites per target, over the code sections."""
        hits = {t: [] for t in targets}
        for name, start, _, raw, raw_size in self.sections:
            if name in DATA_SECTIONS:
                continue
            blob = self.data[raw:raw + raw_size]
            for opcode in (0xE8, 0xE9):
                j = blob.find(bytes([opcode]))
                while 0 <= j < len(blob) - 4:
                    target = (start + j + 5 + struct.unpack_from("<i", blob, j + 1)[0]) & 0xFFFFFFFF
                    if target in hits:
                        hits[target].append((start + j, "call" if opcode == 0xE8 else "jmp"))
                    j = blob.find(bytes([opcode]), j + 1)
        return hits

    def pointer_refs(self, value):
        pattern = struct.pack("<I", value)
        out = []
        for name, start, _, raw, raw_size in self.sections:
            blob = self.data[raw:raw + raw_size]
            j = blob.find(pattern)
            while j >= 0:
                out.append((name, start + j))
                j = blob.find(pattern, j + 1)
        return out


def load_names():
    functions = {}
    for line in open(DATA / "default_xbe_symbols.txt"):
        parts = line.split()
        if len(parts) >= 2 and not line.startswith("#"):
            functions[int(parts[0], 16)] = parts[1]
    data = {}
    with open(DATA / "manual_names.csv", newline="") as f:
        for row in csv.DictReader(f):
            if row["kind"] == "data":
                data[int(row["address"], 16)] = row["name"]
    return functions, data


def load_states():
    states = {}
    if (DATA / "progress.csv").exists():
        with open(DATA / "progress.csv", newline="") as f:
            for row in csv.DictReader(f):
                states[int(row["address"], 16)] = row["state"]
    ours = {int(m.group(1), 16) for m in
            re.finditer(r'\{"\w+", 0x([0-9a-fA-F]+),', (ROOT / "game/code/functions.cpp").read_text())}
    return states, ours


def loader_patch_sites():
    sites = set()
    for path in (ROOT / "loader").glob("*.cpp"):
        sites.update(int(v, 16) for v in re.findall(r"0x00[0-9a-fA-F]{6}\b", path.read_text()))
    return sites


def disassemble(image, va, length):
    code = image.read(va, length)
    with tempfile.NamedTemporaryFile(suffix=".bin") as tmp:
        tmp.write(code)
        tmp.flush()
        out = subprocess.run(["objdump", "-D", "-b", "binary", "-mi386", "-Mintel", f"--adjust-vma={va:#x}",
                              tmp.name], capture_output=True, text=True, check=True).stdout
    rows = []
    for line in out.splitlines():
        m = re.match(r"\s*([0-9a-f]+):\s*((?:[0-9a-f]{2} )+)\s*(.*)", line)
        if m and m.group(3):
            rows.append((int(m.group(1), 16), len(m.group(2).split()), re.sub(r"\s+", " ", m.group(3)).strip()))
        elif m and rows:  # continuation of a long instruction's bytes
            address, size, text = rows[-1]
            rows[-1] = (address, size + len(m.group(2).split()), text)
    pad = next((i for i, row in enumerate(rows) if row[2] == "int3"), None)
    if pad:  # MSVC pads between functions with int3: an unnamed next function starts after it
        rows = rows[:pad]
    while rows and rows[-1][2] in ("nop", "xchg ax,ax", "lea esi,[esi+0x0]"):
        rows.pop()
    return rows


def split_operands(text):
    mnemonic, _, rest = text.partition(" ")
    ops, depth, current = [], 0, ""
    for c in rest:
        if c == "," and depth == 0:
            ops.append(current)
            current = ""
            continue
        depth += c == "["
        depth -= c == "]"
        current += c
    if current:
        ops.append(current)
    return mnemonic, [o.strip() for o in ops]


def regs_in(operand):
    found = set()
    for token in re.findall(r"\b(e?[a-d]x|[a-d][lh]|e?[sd]i|e?[sb]p|xmm\d)\b", operand):
        found.add(SUB.get(token, token))
    return found


def convention(rows, callee_pops=None):
    """Registers read before written, stack arguments, ret N, the result's last writes.
    callee_pops(address) -> bytes a callee pops (ret N), or None when unknown: keeps
    counting stack slots past calls; without it the count stops at the first call."""
    written, read_first, saved = set(), [], set()
    stack_args, esp_delta, ebp_delta, esp_aligned = set(), 0, None, False
    ret_sizes, result_writes, last_eax = set(), [], None
    known = {}  # esp distance at branch targets, for the code after a ret or jmp
    for address, _, text in rows:
        mnemonic, ops = split_operands(text)
        mnemonic = mnemonic.replace("rep", "").strip() or mnemonic
        if esp_delta is None:
            esp_delta = known.get(address)
        if mnemonic.startswith("j") and ops and esp_delta is not None:
            try:
                known.setdefault(int(ops[0].split()[0], 16), esp_delta)
            except ValueError:
                pass
        if mnemonic.startswith("ret"):
            ret_sizes.add(int(ops[0], 16) if ops else 0)
            result_writes.append(last_eax)
            esp_delta = None
            continue
        if re.fullmatch(r"(mov (\w+),\2|lea (\w+),\[\3\+0x0\]|xchg ax,ax|nop.*)", text):
            continue  # alignment padding (mov edi,edi; lea ebx,[ebx+0x0])
        reads, writes = set(), set()
        same = len(ops) == 2 and ops[0] == ops[1] and mnemonic in ("xor", "sub", "xorps", "pxor", "subps", "sbb")
        for i, op in enumerate(ops):
            if same:
                writes |= regs_in(op)
                continue
            if "[" in op:
                reads |= regs_in(op)
                m = re.search(r"\[(esp|ebp)(?:\+(0x[0-9a-f]+))?\]", op)
                base = esp_delta if m and m.group(1) == "esp" and esp_aligned is False else \
                    ebp_delta if m and m.group(1) == "ebp" else None
                if m and base is not None and esp_delta is not None:
                    offset = int(m.group(2) or "0", 16) - base
                    if offset >= 4:
                        stack_args.add((offset - 4) // 4)
            elif i == 0 and not NO_DEST.match(mnemonic):
                writes |= regs_in(op)
                if not WRITE_ONLY.match(mnemonic):
                    reads |= regs_in(op)
            else:
                reads |= regs_in(op)
        if mnemonic == "push" and ops and ops[0] in REG32:
            saved.add(ops[0])
            reads.discard(ops[0])
        if mnemonic in ("cdq",):
            reads.add("eax")
            writes.add("edx")
        if mnemonic in ("movs", "stos", "movsd", "stosd", "movsb", "stosb", "movsw", "stosw"):
            reads |= {"esi", "edi", "ecx", "eax"} & ({"esi", "edi", "ecx"} if "movs" in mnemonic else {"edi", "ecx", "eax"})
        for reg in sorted(reads - written):
            if reg in ARG_REGS and reg not in (r for r, _ in read_first):
                read_first.append((reg, reg in saved))
        written |= writes
        if writes & {"eax"} or (mnemonic.startswith("f") and mnemonic.startswith(("fld", "fild", "fstp"))):
            last_eax = f"{address:08x} {text}"
        if mnemonic == "mov" and ops == ["ebp", "esp"]:
            ebp_delta = esp_delta
        elif ops and ops[0] == "esp" and mnemonic in ("and", "mov", "lea"):
            esp_aligned = True  # esp no longer a known distance from the entry
        elif ops and ops[0] == "ebp" and ebp_delta is not None and not (mnemonic == "push"):
            ebp_delta = None  # ebp reused as a general register
        if esp_delta is None:
            pass
        elif mnemonic == "jmp":
            esp_delta = None
        elif mnemonic == "push":
            esp_delta += 4
        elif mnemonic == "pop":
            esp_delta -= 4
        elif mnemonic in ("sub", "add") and ops and ops[0] == "esp" and re.match(r"0x[0-9a-f]+$", ops[1]):
            esp_delta += int(ops[1], 16) * (1 if mnemonic == "sub" else -1)
        elif mnemonic == "call":
            if esp_delta is not None:
                try:
                    pops = callee_pops(int(ops[0].split()[0], 16)) if callee_pops else None
                except ValueError:
                    pops = None  # indirect: unknown
                esp_delta = None if pops is None else esp_delta - pops
        if mnemonic == "call":
            written |= {"eax", "ecx", "edx"} | {f"xmm{i}" for i in range(8)}
    return read_first, sorted(stack_args), ret_sizes, result_writes


def pops_reader(image, starts):
    """callee_pops for convention(): a callee's ret N, from its own disassembly."""
    cache = {}

    def pops(target):
        if target not in cache:
            i = bisect.bisect_right(starts, target)
            end = starts[i] if i < len(starts) else target + 0x400
            rets = {int(split_operands(t)[1][0], 16) if split_operands(t)[1] else 0
                    for _, _, t in disassemble(image, target, min(end - target, 0x4000)) if t.startswith("ret")}
            cache[target] = rets.pop() if len(rets) == 1 else None
        return cache[target]
    return pops


def hook_problem(rows, va):
    for address, size, text in rows:
        if address >= va + 5:
            return None
        mnemonic, ops = split_operands(text)
        if mnemonic in ("call", "jmp", "ret") or mnemonic.startswith("loop"):
            return f"{mnemonic} at {address:08x} inside the first 5 bytes: the harness cannot hook it"
        if mnemonic.startswith("j") and ops:
            try:
                target = int(ops[0].split()[0], 16)
            except ValueError:
                continue
            if va <= target < va + 5:
                return f"branch at {address:08x} back into the first 5 bytes: the harness cannot hook it"
    return None


def name_of(functions, data, va):
    return functions.get(va) or data.get(va)


def pack(image, functions, data, sorted_starts, states, ours, patch_sites, va, asm_dir, no_asm):
    name = functions.get(va, f"FUN_{va:08x}")
    i = bisect.bisect_right(sorted_starts, va)
    end = sorted_starts[i] if i < len(sorted_starts) else va + 0x400
    rows = disassemble(image, va, min(end - va, 0x4000))
    size = rows[-1][0] + rows[-1][1] - va if rows else 0
    section = image.section(va)
    state = "ours (kFunctions)" if va in ours else states.get(va, "?")
    out = [f"== {name} {va:08x} [{section}] {size} bytes, {len(rows)} instructions, state {state}"]

    def owner(address):
        j = bisect.bisect_right(sorted_starts, address) - 1
        start = sorted_starts[j] if j >= 0 else 0
        return f"{functions.get(start, '?')}+{address - start:#x}"

    callers = CALLERS.get(va, [])
    data_refs = [(s, a) for s, a in image.pointer_refs(va) if s in DATA_SECTIONS]
    caller_text = ", ".join(f"{owner(a)}{'' if kind == 'call' else ' (jmp)'}" for a, kind in callers[:12])
    out.append(f"callers ({len(callers)}): {caller_text or 'none by CALL/JMP'}"
               + (f" ... +{len(callers) - 12}" if len(callers) > 12 else ""))
    if data_refs:
        out.append(f"pointer in data ({len(data_refs)}): "
                   + ", ".join(f"{s} {a:08x}{'<' + data[a] + '>' if a in data else ''}" for s, a in data_refs[:6])
                   + " (vtable or callback table?)")

    callees, globals_, strings, notes = {}, {}, {}, set()
    for address, _, text in rows:
        mnemonic, ops = split_operands(text)
        for value in (int(v, 16) for v in re.findall(r"0x([0-9a-f]{5,8})\b", text)):
            target_section = image.section(value)
            if not target_section:
                continue
            if mnemonic in ("call", "jmp") and target_section not in DATA_SECTIONS:
                if value < va or value >= va + size:
                    callees.setdefault(value, mnemonic)
            elif target_section in DATA_SECTIONS:
                s = image.string_at(value) if target_section in (".rdata", "XON_RD") else None
                if s is not None:
                    strings[value] = s
                else:
                    globals_.setdefault(value, target_section)
            elif target_section and value in functions:
                callees.setdefault(value, "pointer")
        for pattern, note in FLOAT_NOTES:
            if re.match(pattern, mnemonic):
                notes.add(note)
        if mnemonic == "call" and "ftol" in text.lower():
            notes.add("_ftol: (int32_t)(int64_t)f")
        if mnemonic.startswith("call") and ops and ops[0].startswith(("DWORD PTR [e", "e")):
            notes.add("indirect calls (vtable/callback): callees not listed")

    sse = sum(1 for _, _, t in rows if re.match(r"\w+(ss|ps|sd|pd)\b", t) or t.startswith(("cvt", "shufps")))
    x87 = sum(1 for _, _, t in rows if t.startswith("f"))
    if sse or x87:
        out.append(f"float: {sse} SSE, {x87} x87 instructions"
                   + (" - mostly straight-line SSE: lift with ssesym.py/psssym.py"
                      if sse > 60 and sse > len(rows) // 2 else ""))

    def fmt_callee(target, kind):
        callee = functions.get(target, f"?{target:08x}")
        section_name = image.section(target)
        tags = []
        if target in ours:
            tags.append("ours")
        elif states.get(target) in ("library",):
            tags.append("library")
        if section_name in SDK_SECTIONS:
            tags.append(section_name)
        if kind != "call":
            tags.append(kind)
        return callee + (f" ({', '.join(tags)})" if tags else "")

    if callees:
        out.append(f"callees ({len(callees)}): " + ", ".join(fmt_callee(t, k) for t, k in sorted(callees.items())))
        sdk = sorted({image.section(t) for t in callees} & SDK_SECTIONS)
        if sdk:
            notes.add(f"calls SDK code ({', '.join(sdk)}): recorded calls/stand-ins (compare-harness.md) or inconclusive")
        unnamed = [t for t in callees if t not in functions or functions[t].startswith("FUN_")]
        if unnamed:
            notes.add(f"{len(unnamed)} unnamed callee(s): name them first (naming.md)")
    if globals_:
        def fmt_global(a):
            label = data.get(a) or functions.get(a)
            if label:
                return f"{label}@{a:08x}"
            if globals_[a] == ".rdata":  # unnamed constants: show the value
                raw = image.read(a, 4)
                if len(raw) == 4:
                    f, = struct.unpack("<f", raw)
                    word, = struct.unpack("<I", raw)
                    return f"{a:08x}={f:g}f" if 1e-6 < abs(f) < 1e9 or word == 0 else f"{a:08x}={word:#x}"
            return f"?@{a:08x}"
        out.append(f"globals ({len(globals_)}): " + ", ".join(fmt_global(a) for a in sorted(globals_)))
    if strings:
        out.append("strings: " + "; ".join(f"{a:08x} \"{s}\"" for a, s in sorted(strings.items())[:10]))

    read_first, stack_args, ret_sizes, result_writes = convention(rows, POPS)
    regs = [f"{r}{' (pushed first: saved, maybe not an argument)' if s else ''}" for r, s in read_first]
    out.append("register inputs (read before written, linear scan): " + (", ".join(regs) or "none"))
    odd = [r for r, s in read_first if r in ("ebx", "esi", "edi") or r.startswith("xmm")]
    odd = [r for r in odd if not dict(read_first)[r]]
    if odd:
        notes.add(f"arguments in {'/'.join(odd)}: PLATFORM_ASM_ENTRY shim (compiler.h)")
    gcc = [r for r, s in read_first if r in ("eax", "edx", "ecx")]
    if gcc:
        out.append("  EAX/EDX/ECX in order = XB_REGS3/regparm; EDX-only = regparm(2) with a dummy first")
    ret_text = ", ".join(f"ret {n:#x}" if n else "ret" for n in sorted(ret_sizes)) or "no ret (tail jump?)"
    args_text = ", ".join(f"[esp+{4 + 4 * n:#x}]" for n in stack_args) or "none seen"
    out.append(f"stack: {ret_text}; argument slots read: {args_text}")
    distinct = sorted({w for w in result_writes if w})
    if distinct:
        out.append("last eax/st0 write before each ret: " + "; ".join(distinct[:4]))
    if problem := hook_problem(rows, va):
        notes.add(problem)
    if any(va <= site < va + size for site in patch_sites):
        notes.add("the loader patches an address inside it (loader/*.cpp): not compared with PC patches on")
    if notes:
        out.append("notes:")
        out += [f"  - {n}" for n in sorted(notes)]

    if not no_asm:
        def named(match):
            value = int(match.group(0), 16)
            label = name_of(functions, data, value)
            return match.group(0) + (f"<{label}>" if label else "")

        listing = [f"{a:08x} {re.sub(r'0x[0-9a-f]{5,8}', named, t)}" for a, _, t in rows]
        if asm_dir:
            asm_dir.mkdir(parents=True, exist_ok=True)
            path = asm_dir / f"{name}.asm"
            path.write_text("\n".join(listing) + "\n")
            out.append(f"disassembly: {path}")
        else:
            out.append("disassembly:")
            out += listing
    return "\n".join(out)


CALLERS = {}
POPS = None


def main():
    parser = argparse.ArgumentParser(usage=__doc__)
    parser.add_argument("functions", nargs="+")
    parser.add_argument("--asm", type=Path)
    parser.add_argument("--no-asm", action="store_true")
    args = parser.parse_args()
    functions, data = load_names()
    by_name = {n: a for a, n in functions.items()}
    targets = []
    for item in args.functions:
        if item in by_name:
            targets.append(by_name[item])
        else:
            try:
                targets.append(int(item, 16))
            except ValueError:
                sys.exit(f"{item}: no such symbol")
    image = Image()
    CALLERS.update(image.code_refs(targets))
    global POPS
    POPS = pops_reader(image, sorted(functions))
    states, ours = load_states()
    starts = sorted(functions)
    sites = loader_patch_sites()
    print("\n\n".join(pack(image, functions, data, starts, states, ours, sites, va, args.asm, args.no_asm)
                      for va in targets))


if __name__ == "__main__":
    main()
