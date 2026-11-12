#!/usr/bin/env python3
"""Level EH code that LTCG placed in .text: the keep list for the own engine host.

Level functions (original x86, kept from the dump) with C++ EH register a handler stub that the linker put in .text
(`mov eax, FuncInfo; jmp ___CxxFrameHandler`, 0x0033xxxx-0x0034exxx), and that FuncInfo's unwind actions and catch
blocks (funclets: `lea ecx,[ebp-N]; jmp Dtor`, ...) are in .text too. They are level code like the level sections:
the host keeps their bytes (their jumps land on hooked functions) while the rest of .text is gone.

Finds them statically: every `push -1; push HANDLER; mov eax, fs:[0]` frame and every `mov eax, HANDLER; call
__EH_prolog` in the level sections, every handler stub in a code section, its FuncInfo (.rdata), the unwind map
actions and the catch handlers. Each piece is decoded up to its first jmp/ret; anything it cannot decode is reported
(exit 1) rather than guessed.

Usage: level_eh_code.py default.xbe OUT.txt   (OUT: "begin end" hex per line, like code_section_data.txt)
"""
import struct
import sys

BASE = 0x10000
CODE = {'.text', 'XONLINE', 'XNET', 'D3DX', 'DSOUND', 'D3D', 'XGRPH', 'XPP'}
NOT_LEVEL = CODE | {'.rdata', '.data', 'XON_RD', '.data1', 'DOLBY', '$$XTIMAGE', '$$XSIMAGE', 'dspimage', '.XTLID'}
CXX_FRAME_HANDLER = 0x001f1ec0
EH_PROLOG = 0x001f2930
EH_MAGIC = 0x19930520


def sections(data):
    count, table = struct.unpack_from('<II', data, 0x11C)
    for i in range(count):
        _, va, vsize, raw, rsize, name_addr = struct.unpack_from('<IIIIII', data, table - BASE + i * 56)
        name = data[name_addr - BASE:name_addr - BASE + 16].split(b'\0')[0].decode(errors='replace')
        yield name, va, vsize, data[raw:raw + rsize]


class Image:
    def __init__(self, data):
        self.secs = list(sections(data))

    def section(self, va):
        for name, start, vsize, raw in self.secs:
            if start <= va < start + vsize:
                return name, start, raw
        return None, 0, b''

    def read(self, va, n):
        _, start, raw = self.section(va)
        off = va - start
        return raw[off:off + n].ljust(n, b'\0')

    def u32(self, va):
        return struct.unpack('<I', self.read(va, 4))[0]


def rel32(img, va):
    return (va + 5 + struct.unpack('<i', img.read(va + 1, 4))[0]) & 0xffffffff


def decode_piece(img, va):
    """End of a funclet/stub (every path followed up to its jmp/ret) and its jump targets, or (None, []) for a shape
    this decoder does not know."""
    end, targets, todo, seen = va, [], [va], set()
    while todo:
        at = todo.pop()
        for _ in range(24):
            if at in seen:
                break
            seen.add(at)
            b = img.read(at, 8)
            op = b[0]
            n = None
            if op == 0xe9:                                   # jmp rel32: end of this path
                targets.append(rel32(img, at))
                end = max(end, at + 5)
                break
            if op == 0xeb:                                   # jmp rel8
                todo.append((at + 2 + struct.unpack('<b', b[1:2])[0]) & 0xffffffff)
                end = max(end, at + 2)
                break
            if op in (0xc3, 0xcb):                           # ret
                end = max(end, at + 1)
                break
            if op == 0xc2:                                   # ret n
                end = max(end, at + 3)
                break
            if op == 0x0f and 0x80 <= b[1] <= 0x8f:          # jcc rel32: both paths
                todo.append((at + 6 + struct.unpack('<i', b[2:6])[0]) & 0xffffffff)
                n = 6
            elif 0x70 <= op <= 0x7f:                         # jcc rel8
                todo.append((at + 2 + struct.unpack('<b', b[1:2])[0]) & 0xffffffff)
                n = 2
            elif op in (0xb8, 0xb9, 0xba, 0x68, 0xe8, 0x25):  # mov reg, imm32 / push imm32 / call rel32 / and eax, imm32
                n = 5
            elif op == 0x6a:                                 # push imm8
                n = 2
            elif 0x50 <= op <= 0x5f:                         # push/pop reg
                n = 1
            elif op in (0x8b, 0x8d, 0x83, 0x81, 0xff, 0x89, 0xc7, 0x85, 0x33, 0x39, 0x3b, 0x80, 0xf6):
                modrm = b[1]
                mod, rm = modrm >> 6, modrm & 7
                n = 2
                if mod == 3:
                    pass
                elif rm == 4:
                    return None, []                          # SIB: not seen in funclets so far
                elif mod == 1:
                    n += 1
                elif mod == 2 or (mod == 0 and rm == 5):
                    n += 4
                if op in (0x83, 0x80):
                    n += 1
                elif op == 0x81 or op == 0xc7:
                    n += 4
                elif op == 0xf6 and (modrm >> 3) & 7 == 0:   # test r/m8, imm8
                    n += 1
            else:
                return None, []
            at += n
        else:
            return None, []
    return end - va, targets


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    data = open(sys.argv[1], 'rb').read()
    img = Image(data)
    # Any 32-bit value in a level section that points at a handler stub in a code section (push HANDLER in each
    # frame-setup order the compiler used, mov eax, HANDLER before __EH_prolog): the stub shape is strict enough.
    code_ranges = [(start, start + vsize) for name, start, vsize, _ in img.secs if name in CODE]
    handlers = {}  # handler -> first level reference
    for name, start, vsize, raw in img.secs:
        if name in NOT_LEVEL:
            continue
        for i in range(len(raw) - 3):
            v = struct.unpack_from('<I', raw, i)[0]
            if v in handlers or not any(b <= v < e for b, e in code_ranges):
                continue
            stub = img.read(v, 10)
            if stub[0] == 0xb8 and stub[5] == 0xe9 and rel32(img, v + 5) == CXX_FRAME_HANDLER:
                handlers[v] = start + i
    keep, problems, stubs, funclets = [], [], 0, 0
    jump_targets = set()
    for h, ref in sorted(handlers.items()):
        sec = img.section(h)[0]
        if sec not in CODE:
            continue  # a stub in the level section itself: kept anyway
        b = img.read(h, 10)
        if b[0] != 0xb8 or b[5] != 0xe9 or rel32(img, h + 5) != CXX_FRAME_HANDLER:
            problems.append(f'{h:08x} handler (from {ref:08x}) is not mov eax,FuncInfo; jmp ___CxxFrameHandler')
            continue
        keep.append((h, h + 10))
        stubs += 1
        info = struct.unpack('<I', b[1:5])[0]
        magic, max_state, unwind, ntry, trymap = struct.unpack('<IiIII', img.read(info, 20))
        if magic != EH_MAGIC:
            problems.append(f'{h:08x}: FuncInfo {info:08x} magic {magic:08x}')
            continue
        pieces = []
        for s in range(max_state):
            _, action = struct.unpack('<iI', img.read(unwind + s * 8, 8))
            if action:
                pieces.append(('unwind', action))
        for t in range(ntry):
            _, _, _, ncatch, harr = struct.unpack('<iiiiI', img.read(trymap + t * 20, 20))
            for c in range(ncatch):
                pieces.append(('catch', img.u32(harr + c * 16 + 12)))
        for kind, va in pieces:
            if img.section(va)[0] not in CODE:
                continue
            length, targets = decode_piece(img, va)
            if length is None:
                problems.append(f'{va:08x} {kind} funclet of FuncInfo {info:08x} (handler {h:08x}): unknown shape '
                                f'{img.read(va, 12).hex()}')
                continue
            keep.append((va, va + length))
            jump_targets.update(targets)
            funclets += 1
    # Funclets that jump to .text thunks into level code (`jmp LEVEL_FUNCTION`, the linker's): the thunk is level
    # code as well. Other targets must have C (hooked): listed in OUT_targets.txt.
    other = set()
    for t in sorted(jump_targets):
        if img.section(t)[0] not in CODE:
            continue
        b = img.read(t, 5)
        if b[0] == 0xe9 and img.section(rel32(img, t))[0] not in NOT_LEVEL:
            keep.append((t, t + 5))
        else:
            other.add(t)
    jump_targets = other
    keep = sorted(set(keep))
    merged = []
    for b, e in keep:
        if merged and b <= merged[-1][1]:
            merged[-1] = (merged[-1][0], max(e, merged[-1][1]))
        else:
            merged.append((b, e))
    with open(sys.argv[2], 'w') as out:
        out.write('# begin end (hex): level EH stubs and funclets in code sections (tools/level_eh_code.py)\n')
        for b, e in merged:
            out.write(f'{b:08x} {e:08x}\n')
    in_code = sorted(t for t in jump_targets if img.section(t)[0] in CODE)
    print(f'{stubs} handler stubs, {funclets} funclets, {len(merged)} ranges '
          f'({sum(e - b for b, e in merged)} bytes); {len(in_code)} jump targets need C (OUT_targets.txt)')
    with open(sys.argv[2].replace('.txt', '') + '_targets.txt', 'w') as out:
        out.writelines(f'{t:08x}\n' for t in in_code)
    for p in problems:
        print('PROBLEM', p)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
