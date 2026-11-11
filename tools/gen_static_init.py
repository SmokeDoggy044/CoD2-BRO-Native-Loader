#!/usr/bin/env python3
"""Generates src/engine/shared/static_init_generated.cpp: the title's C++ static initializers (the _cinit table
.CRT$XC at 0x00897250..0x008978cc, in order) for the own engine host (step 0.3).

Entries with C (a kFunctions row) are called through their original address (the host's jump into the C). The
rest are compiler-generated stubs: copies of constants between globals, atexit registrations of destructors,
constructor calls with register arguments, small fill loops. This script translates the straight-line x86 of
each into C (goto for its loops); what it cannot translate is listed in the output's header as MANUAL and called
through its original address, so it shows up in --text-trap logs until it gets C.

usage: tools/gen_static_init.py [--check]   (--check: print the summary only)
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SCRIPTS = Path('/mnt/ms1/Projects/Decomps/Xbox/.claude/skills/xbox-port/scripts')
sys.path.insert(0, str(SCRIPTS))
import xbe_reader  # noqa: E402

XBE = ROOT.parent / 'unpacked' / 'default.xbe'
OUT = ROOT / 'src/engine/shared/static_init_generated.cpp'
TABLE = (0x00897250, 0x008978cc)
ATEXIT = 0x001eed36
REG32 = ['eax', 'ecx', 'edx', 'ebx', 'esi', 'edi']
REG8 = {'al': 'eax', 'cl': 'ecx', 'dl': 'edx', 'bl': 'ebx'}
REG16 = {'ax': 'eax', 'cx': 'ecx', 'dx': 'edx', 'bx': 'ebx', 'si': 'esi', 'di': 'edi'}
SIZE = {'BYTE': 'uint8_t', 'WORD': 'uint16_t', 'DWORD': 'uint32_t'}


class Manual(Exception):
    pass


def disassemble(address):
    out = subprocess.run([sys.executable, str(SCRIPTS / 'xbedis.py'), str(XBE), hex(address), '0x400'],
                         capture_output=True, text=True).stdout
    rows = []
    for line in out.splitlines():
        parts = line.split('\t')
        if len(parts) < 3:
            continue
        rows.append((int(parts[0].strip().rstrip(':'), 16), re.sub(r'\s+', ' ', parts[2].strip())))
    return rows


def body_of(address):
    """Instructions up to the ret (or tail jmp) that ends the function; backward branches stay inside."""
    out = []
    for at, text in disassemble(address):
        out.append((at, text))
        if text.startswith('ret'):
            return out
        m = re.fullmatch(r'jmp (0x[0-9a-f]+)', text)
        if m and not address <= int(m.group(1), 16) < at:
            return out
    raise Manual('no end')


def operand(op, size='DWORD'):
    """C expression for a source operand (register, immediate or memory)."""
    op = op.strip()
    if op in REG32:
        return op
    if op in REG8:
        return f'static_cast<uint8_t>({REG8[op]})'
    if op in REG16:
        return f'static_cast<uint16_t>({REG16[op]})'
    m = re.fullmatch(r'(?:(BYTE|WORD|DWORD) PTR )?ds:(0x[0-9a-f]+)', op)
    if m:
        return f'title_global<{SIZE[m.group(1) or size]}>({m.group(2)})'
    m = re.fullmatch(r'(BYTE|WORD|DWORD) PTR \[(e[a-z]{2})([+-]0x[0-9a-f]+)?\]', op)
    if m and m.group(2) in REG32:
        return f'mem<{SIZE[m.group(1)]}>({m.group(2)} + {int(m.group(3) or "0", 16)}u)'
    if re.fullmatch(r'0x[0-9a-f]+', op):
        return op + 'u'
    raise Manual(f'operand {op}')


def destination(op, size_hint):
    op = op.strip()
    if op in REG32:
        return op, 'uint32_t'
    if op in REG8:
        raise Manual('8-bit register destination')
    m = re.fullmatch(r'(?:(BYTE|WORD|DWORD) PTR )?ds:(0x[0-9a-f]+)', op)
    if m:
        t = SIZE[m.group(1) or size_hint]
        return f'title_global<{t}>({m.group(2)})', t
    m = re.fullmatch(r'(BYTE|WORD|DWORD) PTR \[(e[a-z]{2})([+-]0x[0-9a-f]+)?\]', op)
    if m and m.group(2) in REG32:
        t = SIZE[m.group(1)]
        return f'mem<{t}>({m.group(2)} + {int(m.group(3) or "0", 16)}u)', t
    raise Manual(f'destination {op}')


def size_of(op):
    op = op.strip()
    if op in REG8:
        return 'BYTE'
    if op in REG16:
        return 'WORD'
    return 'DWORD'


def translate(address):
    body = body_of(address)
    targets = set()
    for _, text in body:
        m = re.fullmatch(r'(?:jne|je|jmp) (0x[0-9a-f]+)', text)
        if m and int(m.group(1), 16) in [a for a, _ in body]:
            targets.add(int(m.group(1), 16))
    lines, pushes, saved = [], [], []
    started = False
    for at, text in body:
        if at in targets:
            lines.append(f'L_{at:08x}:')
        mnem, _, rest = text.partition(' ')
        ops = [o.strip() for o in re.split(r',(?![^\[]*\])', rest)] if rest else []
        if mnem == 'lea' and re.fullmatch(r'esp,\[esp\+0x0\]|ebx,\[ebx\+0x0\]', rest.replace(' ', '')):
            continue  # alignment filler
        if mnem == 'push' and ops[0] in REG32 and not started:
            saved.append(ops[0])
            continue
        started = True
        if mnem == 'mov':
            src = operand(ops[1], size_of(ops[0]))
            if ops[0] in REG8:
                r = REG8[ops[0]]
                lines.append(f'{r} = ({r} & 0xffffff00u) | static_cast<uint8_t>({src});')
            elif ops[0] in REG16:
                r = REG16[ops[0]]
                lines.append(f'{r} = ({r} & 0xffff0000u) | static_cast<uint16_t>({src});')
            else:
                dst, t = destination(ops[0], size_of(ops[1]))
                lines.append(f'{dst} = static_cast<{t}>({src});')
        elif mnem == 'xor' and ops[0] == ops[1] and ops[0] in REG32:
            lines.append(f'{ops[0]} = 0;')
        elif mnem in ('inc', 'dec') and ops[0] in REG32:
            lines.append(f'{ops[0]} {"+" if mnem == "inc" else "-"}= 1; zf = {ops[0]} == 0;')
        elif mnem in ('add', 'sub') and ops[0] in REG32 and ops[0] != 'esp':
            lines.append(f'{ops[0]} {"+" if mnem == "add" else "-"}= {operand(ops[1])}; zf = {ops[0]} == 0;')
        elif mnem in ('add',) and ops[0] == 'esp':
            continue  # cdecl argument cleanup (the call helper restores ESP)
        elif mnem == 'pop' and ops[0] in REG32:
            if saved and ops[0] == saved[-1]:
                saved.pop()
            continue  # argument cleanup after a cdecl call, or a saved register
        elif mnem == 'push':
            pushes.append(operand(ops[0]))
        elif mnem == 'call' and re.fullmatch(r'0x[0-9a-f]+', ops[0]):
            target = int(ops[0], 16)
            if target == ATEXIT:
                if len(pushes) != 1:
                    raise Manual('atexit arguments')
                lines.append(f'eax = static_init_atexit({pushes[0]});')
            else:
                lines.append(f'eax = call_regs(0x{target:08x}, {{eax, ebx, ecx, edx, esi, edi}}{stack_args(pushes)});')
            pushes = []
        elif mnem == 'jne' and int(ops[0], 16) in targets:
            lines.append(f'if (!zf) goto L_{int(ops[0], 16):08x};')
        elif mnem == 'jmp' and int(ops[0], 16) not in targets:
            target = int(ops[0], 16)
            lines.append(f'call_regs(0x{target:08x}, {{eax, ebx, ecx, edx, esi, edi}}{stack_args(pushes)});')
            lines.append('return;')
        elif mnem == 'rep' and rest == 'stos DWORD PTR es:[edi],eax':
            lines.append('for (; ecx; --ecx, edi += 4) mem<uint32_t>(edi) = eax;')
        elif mnem == 'movss' or mnem in ('xorps', 'divss', 'mulss', 'addss', 'subss', 'cvtsi2ss'):
            lines.append(sse(mnem, ops))
        elif mnem == 'ret':
            lines.append('return;')
        else:
            raise Manual(f'{text}')
    if pushes:
        raise Manual('pushes left')
    return lines


def stack_args(pushes):
    """", {args}" with the last push first (args[0] = top of stack), or nothing."""
    return ', {' + ', '.join(reversed(pushes)) + '}' if pushes else ''


def sse(mnem, ops):
    def x(o):
        m = re.fullmatch(r'xmm([0-7])', o)
        return f'xmm[{m.group(1)}]' if m else None

    def mem_f(o):
        m = re.fullmatch(r'DWORD PTR ds:(0x[0-9a-f]+)', o)
        if not m:
            raise Manual(f'sse operand {o}')
        return f'title_global<float>({m.group(1)})'
    if mnem == 'movss':
        if x(ops[0]) and x(ops[1]):
            return f'{x(ops[0])} = {x(ops[1])};'
        if x(ops[0]):
            return f'{x(ops[0])} = {mem_f(ops[1])};'
        return f'{mem_f(ops[0])} = {x(ops[1])};'
    if mnem == 'xorps' and ops[0] == ops[1]:
        return f'{x(ops[0])} = 0.0f;'
    if mnem == 'cvtsi2ss':
        m = re.fullmatch(r'DWORD PTR ds:(0x[0-9a-f]+)', ops[1])
        if not m:
            raise Manual('cvtsi2ss operand')
        return f'{x(ops[0])} = static_cast<float>(title_global<int32_t>({m.group(1)}));'
    op = {'divss': '/', 'mulss': '*', 'addss': '+', 'subss': '-'}[mnem]
    rhs = x(ops[1]) or mem_f(ops[1])
    return f'{x(ops[0])} = {x(ops[0])} {op} {rhs};'


def main():
    xbe = xbe_reader.XBE(str(XBE))
    src = (ROOT / 'src/engine/shared/functions.cpp').read_text()
    with_c = {int(a, 16): n for n, a in re.findall(r'\{"(\w+)", 0x([0-9a-f]{8}),', src)}
    entries = [xbe.dw(a) for a in range(*TABLE, 4)]
    entries = [e for e in entries if e not in (0, 0xffffffff)]
    generated, manual, table = [], [], []
    for e in entries:
        if e in with_c:
            table.append(f'    {{0x{e:08x}, nullptr}},  // {with_c[e]}')
            continue
        try:
            lines = translate(e)
        except Manual as why:
            manual.append((e, str(why)))
            table.append(f'    {{0x{e:08x}, nullptr}},  // MANUAL: {why}')
            continue
        uses = ' '.join(lines)
        decl = ['uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0, esi = 0, edi = 0;']
        if 'xmm[' in uses:
            decl.append('float xmm[8] = {};')
        if 'zf' in uses:
            decl.append('bool zf = false;')
        body = '\n'.join('    ' + l if not l.startswith('L_') else l for l in decl + lines)
        generated.append(f'void si_{e:08x}() {{\n{body}\n}}\n')
        table.append(f'    {{0x{e:08x}, &si_{e:08x}}},')
    summary = (f'{len(entries)} initializers: {len(entries) - len(manual) - len(generated)} with C, '
               f'{len(generated)} generated, {len(manual)} manual')
    print(summary)
    for e, why in manual:
        print(f'  MANUAL {e:08x}: {why}')
    if '--check' in sys.argv:
        return
    head = f'''// Generated by tools/gen_static_init.py - do not edit. {summary}.
// The title's C++ static initializers (.CRT$XC, 0x{TABLE[0]:08x}..0x{TABLE[1]:08x}) in order, for the own engine host.
// Each generated si_ADDRESS is the translation of a compiler-generated stub at ADDRESS; entries without one are
// called through their original address (C behind the host's jump, or still the original: MANUAL).
// clang-format off
#include <cstdint>

#include "static_init.h"
#include "title_data.h"

extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL qc_call_regs(uint32_t function, const game::TitleRegs* regs,
                                                          uint32_t count, const uint32_t* args);

namespace game {{
namespace {{

template <typename T>
T& mem(uint32_t address) {{
    return *reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}}

template <size_t N>
uint32_t call_regs(uint32_t function, TitleRegs regs, const uint32_t (&args)[N]) {{
    return qc_call_regs(function, &regs, N, args);
}}
uint32_t call_regs(uint32_t function, TitleRegs regs) {{
    return qc_call_regs(function, &regs, 0, nullptr);
}}

'''
    tail = '''}  // namespace

const StaticInitializer kStaticInitializers[] = {
''' + '\n'.join(table) + '''
};
const size_t kStaticInitializerCount = sizeof(kStaticInitializers) / sizeof(kStaticInitializers[0]);

}  // namespace game
'''
    OUT.write_text(head + '\n'.join(generated) + tail)


main()
