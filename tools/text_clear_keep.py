#!/usr/bin/env python3
"""Data inside the code sections (own engine host 0.3): the linker merged constants (strings like "v", tables) into
.text and the SDK sections, and our C, the levels and the original code read them. Writes the ranges the loader's
--text-clear must keep (and the host must map) to tools/data/code_section_data.txt, from three Ghidra exports in
build/naming/ (tools/ghidra/ExportCodeSectionData.java).

A range: Ghidra's defined data, or a non-instruction address an instruction reads, up to the next instruction (at
most 64 KB). Addresses stored in data sections (vtables, initializer tables) and ones on a function boundary
(16-byte aligned after INT3 padding or a ret/jmp) count as code: undisassembled gap functions, which the check
must see."""
import bisect
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
N = ROOT / 'build/naming'
xbe = (ROOT.parent / 'unpacked/default.xbe').read_bytes()
count, headers = struct.unpack_from('<II', xbe, 0x11c)
sections = [struct.unpack_from('<IIIIII', xbe, headers - 0x10000 + i * 0x38) for i in range(count)]


def byte(a):
    for _f, va, vs, ra, rs, _n in sections:
        if va <= a < va + rs:
            return xbe[ra + a - va]
    return 0


def function_boundary(a):
    if a & 15 or byte(a - 1) != 0xCC:
        return False
    return byte(a - 2) in (0xCC, 0xC3) or byte(a - 4) == 0xC2 or byte(a - 6) == 0xE9 or byte(a - 3) == 0xEB


instr = [tuple(int(x, 16) for x in l.split()) for l in (N / 'code_section_instructions.txt').read_text().splitlines()]
starts = [a for a, _ in instr]


def next_instruction(a):
    i = bisect.bisect_right(starts, a)
    return starts[i] if i < len(starts) else a + 0x10000


ranges = []
for line in (N / 'code_section_data.txt').read_text().splitlines():
    a, length = line.split()[:2]
    ranges.append((int(a, 16), int(a, 16) + int(length)))
DATA_SECTIONS = [(va, va + vs) for _f, va, vs, _ra, _rs, na in sections
                 if xbe[na - 0x10000:na - 0x10000 + 8].split(b'\0')[0] in
                 (b'.rdata', b'.data', b'XON_RD', b'.data1', b'DOLBY', b'.XTLID')]
for line in (N / 'code_section_data_refs.txt').read_text().splitlines():
    a, _section, source = line.split()[:3]
    a, source = int(a, 16), int(source, 16)
    # Read by an instruction (original or level code): data. Stored in a data section (vtables, .CRT$XC
    # initializer tables): mostly code without a Ghidra function, so the check must see it.
    if function_boundary(a) or any(b <= source < e for b, e in DATA_SECTIONS):
        continue
    ranges.append((a, min(next_instruction(a), a + 0x10000)))
ranges.sort()
merged = []
for a, b in ranges:
    if merged and a <= merged[-1][1]:
        merged[-1][1] = max(merged[-1][1], b)
    else:
        merged.append([a, b])
out = ROOT / 'tools/data/code_section_data.txt'
out.write_text('# begin end (hex): data inside code sections, kept by --text-clear (tools/text_clear_keep.py)\n' +
               ''.join(f'{a:08x} {b:08x}\n' for a, b in merged))
print(len(merged), 'ranges,', sum(b - a for a, b in merged), 'bytes ->', out.relative_to(ROOT))
