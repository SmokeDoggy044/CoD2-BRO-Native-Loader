#!/usr/bin/env python3
"""Lists functions from an address list with their size (up to the compiler's INT3 padding) and disassembly, for
planning gap-function batches (own engine host 0.3, build/trap/gap_create.txt).

Usage: tools/gap_list.py LIST MAX_BYTES [MIN_BYTES]   -> "address size | instr ; instr ; ..." """
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
xbe = (ROOT.parent / 'unpacked/default.xbe').read_bytes()
count, headers = struct.unpack_from('<II', xbe, 0x11c)
sections = [struct.unpack_from('<IIIIII', xbe, headers - 0x10000 + i * 0x38) for i in range(count)]


def image(a, n):
    for _f, va, _vs, ra, rs, _n in sections:
        if va <= a < va + rs:
            return xbe[ra + a - va:ra + a - va + n]
    return b''


def size(a):
    x = image(a, 0x4000)
    i = 16
    while i < len(x) and not (x[i - 1] == 0xCC and (x[i - 2] in (0xCC, 0xC3) or x[i - 4] == 0xC2 or
                                                    x[i - 6] == 0xE9 or x[i - 3] == 0xEB)):
        i += 16
    while i > 0 and x[i - 1] == 0xCC:
        i -= 1
    return i


def main():
    top = int(sys.argv[2])
    low = int(sys.argv[3]) if len(sys.argv) > 3 else 0
    with tempfile.NamedTemporaryFile(suffix='.bin') as tmp:
        for line in Path(sys.argv[1]).read_text().split():
            a = int(line, 16)
            s = size(a)
            if not low <= s <= top:
                continue
            Path(tmp.name).write_bytes(image(a, s))
            out = subprocess.run(['objdump', '-D', '-b', 'binary', '-mi386', '-Mintel', f'--adjust-vma={a:#x}', tmp.name],
                                 capture_output=True, text=True).stdout
            ins = [' '.join(l.split('\t')[-1].split()) for l in out.splitlines()
                   if '\t' in l and ':' in l and l.strip()[:1].isalnum()]
            print(f'{a:08x} {s:4d} | ' + ' ; '.join(ins))


main()
