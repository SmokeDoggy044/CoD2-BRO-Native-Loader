#!/usr/bin/env python3
"""Names the hits of the loader's --text-clear log (own engine host 0.3): "address caller" lines -> the
function each lies in (nearest symbol at or below, from tools/data/default_xbe_symbols.txt) and the caller's.

Usage: tools/text_clear_report.py LOG [LOG ...]   (several logs: merged, duplicates dropped)"""
import bisect
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
syms = {}
for line in (ROOT / 'tools/data/default_xbe_symbols.txt').read_text().splitlines():
    p = line.split()
    if len(p) >= 2 and not p[0].startswith('#'):
        try:
            syms.setdefault(int(p[0], 16), p[1])
        except ValueError:
            pass
addrs = sorted(syms)
XBE = (ROOT.parent / 'unpacked' / 'default.xbe').read_bytes()
HDR_BASE = 0x10000


def image_bytes(a, n):
    count, headers = struct.unpack_from('<II', XBE, 0x11c)
    for i in range(count):
        _f, va, vs, ra, rs, _n = struct.unpack_from('<IIIIII', XBE, headers - HDR_BASE + i * 0x38)
        if va <= a and a + n <= va + rs:
            return XBE[ra + a - va:ra + a - va + n]
    return b''


def is_call(ret):
    """A real call: host code (our C), or a return address right after a call instruction."""
    if ret >= 0x02000000:
        return True
    b = image_bytes(ret - 7, 7)
    if len(b) < 7:
        return False
    return b[2] == 0xE8 or (b[5] == 0xFF and (b[6] & 0x38) == 0x10) or (b[4] == 0xFF and (b[5] & 0x38) == 0x10) \
        or (b[1] == 0xFF and (b[2] & 0x38) == 0x10) or (b[0] == 0xFF and (b[1] & 0x38) == 0x10)


def name(a):
    if a >= 0x02000000:
        return 'host'
    i = bisect.bisect_right(addrs, a) - 1
    if i < 0:
        return '?'
    base = addrs[i]
    return syms[base] if a == base else f'{syms[base]}+0x{a - base:x}'


seen = {}
for log in sys.argv[1:]:
    for line in Path(log).read_text().splitlines():
        p = line.split()
        if len(p) >= 2:
            seen.setdefault(int(p[0], 16), int(p[1], 16))
calls = 0
for a, c in sorted(seen.items()):
    kind = 'call' if is_call(c) else 'cont'  # cont: execution went on past a restore boundary (not an entry)
    calls += kind == 'call'
    print(f'{a:08x} {kind} {name(a):40s} <- {c:08x} {name(c)}')
print(len(seen), 'spots,', calls, 'entered by a call')
