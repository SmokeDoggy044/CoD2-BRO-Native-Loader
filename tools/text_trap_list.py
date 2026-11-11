#!/usr/bin/env python3
"""Every original non-level function entry (build/naming/meta, the Ghidra export) as "hexaddress name" for the
loader's --text-trap (own engine host, step 0.3). Level sections are left out: they stay original x86.

Usage: tools/text_trap_list.py OUT"""
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
XBE = ROOT.parent / 'unpacked' / 'default.xbe'
LEVELS = {'PROLOGUE', 'ORAN', 'TUNISIA', 'KASSPASS', 'KASSRINE', 'LIBEATOR', 'GELA', 'PIANLUPO', 'TROINA',
          'OMAHEACH', 'MONS1', 'EILEDORF', 'BUCHHOLZ', 'DRAGEETH'}


def sections():
    d = XBE.read_bytes()
    base = struct.unpack_from('<I', d, 0x104)[0]
    n, sh = struct.unpack_from('<II', d, 0x11c)
    out = []
    for i in range(n):
        o = sh - base + i * 0x38
        flags, va, vs, _ra, _rs, name_addr = struct.unpack_from('<IIIIII', d, o)
        p = name_addr - base
        name = d[p:d.index(b'\0', p)].decode()
        out.append((va, va + vs, name, flags))
    return out


def main():
    secs = sections()
    names = {}
    for line in (ROOT / 'tools/data/default_xbe_symbols.txt').read_text().splitlines():
        parts = line.split()
        if len(parts) >= 2 and not parts[0].startswith('#'):
            names.setdefault(int(parts[0], 16), parts[1])
    rows, by_section = [], {}
    for meta in sorted((ROOT / 'build/naming/meta').glob('*.tsv')):
        for line in meta.read_text().splitlines():
            a = int(line.split('\t', 1)[0], 16)
            sec = next((s for s in secs if s[0] <= a < s[1]), None)
            if not sec or sec[2] in LEVELS or sec[2] in ('.rdata', '.data', '.data1', 'XON_RD', '$$XTIMAG', '$$XSIMAG'):
                continue
            rows.append(f'{a:08x} {names.get(a, f"FUN_{a:08x}")}')
            by_section[sec[2]] = by_section.get(sec[2], 0) + 1
    Path(sys.argv[1]).write_text('\n'.join(rows) + '\n')
    print(len(rows), 'functions:', ', '.join(f'{k} {v}' for k, v in sorted(by_section.items())))


main()
