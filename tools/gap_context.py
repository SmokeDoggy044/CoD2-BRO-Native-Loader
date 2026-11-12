#!/usr/bin/env python3
"""Where gap functions are reached from: vtable slots (with the RTTI class when the vtable has one,
else the named functions in the same vtable) and direct call/jmp sites (with the caller's name).

usage: tools/gap_context.py RTTI_TSV ADDRESS...   (or - to read addresses from stdin)
RTTI_TSV = output of the skill's scripts/msvc_rtti_vtables.py (vtable address <tab> class).
"""
import bisect
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
XBE = os.path.join(HERE, '..', '..', 'unpacked', 'default.xbe')
SYMS = os.path.join(HERE, 'data', 'default_xbe_symbols.txt')


def load_sections(d):
    base = struct.unpack_from('<I', d, 0x104)[0]
    n, sh = struct.unpack_from('<II', d, 0x11c)
    secs = []
    for i in range(n):
        o = sh - base + i * 0x38
        _fl, va, _vs, ra, rs, na = struct.unpack_from('<IIIIII', d, o)
        no = na - base
        name = d[no:d.index(b'\0', no)].decode()
        secs.append((name, va, ra, rs))
    return secs


def main():
    rtti = {}
    for line in open(sys.argv[1]):
        a, name = line.rstrip('\n').split('\t')
        rtti[int(a, 16)] = name
    args = sys.argv[2:]
    if args == ['-']:
        args = sys.stdin.read().split()
    want = {int(a, 16) for a in args}
    d = open(XBE, 'rb').read()
    secs = load_sections(d)
    syms = []
    for line in open(SYMS):
        if line.startswith('#'):
            continue
        p = line.split()
        syms.append((int(p[0], 16), p[1]))
    syms.sort()
    addrs = [s[0] for s in syms]
    names = dict(syms)

    def owner(va):
        i = bisect.bisect_right(addrs, va) - 1
        return f'{syms[i][1]}+{va - syms[i][0]:#x}' if i >= 0 else f'{va:08x}'

    text = [(va, ra, rs) for (n, va, ra, rs) in secs if n not in ('.rdata', '.data', '.data1', 'XON_RD', 'DOLBY')]
    data = [(va, ra, rs) for (n, va, ra, rs) in secs if n in ('.rdata', '.data', '.data1')]

    def is_code(v):
        return any(va <= v < va + rs for va, _ra, rs in text)

    def word(va):
        for sva, ra, rs in data:
            if sva <= va < sva + rs:
                return struct.unpack_from('<I', d, ra + va - sva)[0]
        return None

    hits = {a: [] for a in want}
    for sva, ra, rs in data:
        for off in range(0, rs - 3, 4):
            v = struct.unpack_from('<I', d, ra + off)[0]
            if v in want:
                hits[v].append(('data', sva + off))
    for sva, ra, rs in text:
        blob = d[ra:ra + rs]
        for i in range(len(blob) - 4):
            if blob[i] in (0xe8, 0xe9):
                t = (sva + i + 5 + struct.unpack_from('<i', blob, i + 1)[0]) & 0xffffffff
                if t in want:
                    hits[t].append(('call' if blob[i] == 0xe8 else 'jmp', sva + i))
    for a in sorted(want):
        print(f'{a:08x} {names.get(a, "?")}')
        for kind, at in hits[a][:8]:
            if kind != 'data':
                print(f'   {kind} from {owner(at)}')
                continue
            start = at
            while word(start - 4) is not None and is_code(word(start - 4)) and (start - 4) not in rtti:
                if start in rtti:
                    break
                start -= 4
            slot = (at - start) // 4
            cls = rtti.get(start)
            if not cls:
                near = []
                for k in range(0, 24):
                    w = word(start + 4 * k)
                    if w is None or not is_code(w):
                        break
                    n = names.get(w, '')
                    if n and not n.startswith('FUN_'):
                        near.append(f'{k}:{n}')
                cls = 'no RTTI; ' + ' '.join(near[:5])
            print(f'   vtable {start:08x} slot {slot} [{cls}]')
        if len(hits[a]) > 8:
            print(f'   ... {len(hits[a]) - 8} more')


if __name__ == '__main__':
    main()
