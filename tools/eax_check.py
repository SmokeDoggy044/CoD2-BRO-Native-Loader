#!/usr/bin/env python3
"""Lists kFunctions entries registered Result::None whose original keeps an EAX input.

Such a function (EAX read before written, no EAX write and no CALL before any ret) hands its
EAX argument back to the caller, and callers do rely on it (CoD BRO Actor_PopBlockingStates
keeps using EAX = actor after Actor_MergeStateTransitions). A C version returning void breaks
those callers once the function is Replaced: return the argument and register Result::Eax.

usage: tools/eax_check.py        (run from the fork root; uses tools/fnpack.py)
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    source = open(os.path.join(ROOT, 'game/code/functions.cpp')).read()
    entries = re.findall(r'\{"([A-Za-z0-9_]+)", 0x([0-9a-f]{8}), [^}]*Result::None', source)
    if not entries:
        return 0
    pack = subprocess.run([sys.executable, os.path.join(ROOT, 'tools/fnpack.py')] + ['0x' + a for _, a in entries],
                          capture_output=True, text=True, cwd=ROOT).stdout
    hits = []
    for block in pack.split('\n== ')[1:]:
        head = block.split('\n', 1)[0].split()
        inputs = re.search(r'register inputs \(read before written, linear scan\): (.*)', block)
        if not inputs or 'eax' not in inputs.group(1):
            continue
        if 'last eax/st0 write' in block:
            continue
        if re.search(r'\bcall\b', block.split('disassembly:')[-1]):
            continue
        hits.append(f'{head[1]} {head[0]}')
    print('\n'.join(hits))
    print(f'{len(hits)} of {len(entries)} Result::None entries keep an EAX input', file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main())
