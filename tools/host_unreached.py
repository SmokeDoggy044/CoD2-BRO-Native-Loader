#!/usr/bin/env python3
"""host_unreached.py: original engine functions the own engine (cod_bro / --host) can still reach but has no C for.

Under --host every byte of the engine's code sections is INT3 (host_image.cpp), so a function without a C row crashes
the moment a mission reaches it. Replays only find the ones their missions reach; this lists them statically:
  - aligned 32-bit words in every non-code section (vtables, callback tables, level data) and
  - call/jmp/push/mov immediates in the level sections' code (objdump)
that point at a function start in a cleared code section (a Ghidra function, or a 16-byte aligned address after
INT3/ret padding = a gap function) with no row in src/engine/shared/functions.cpp.

usage: tools/host_unreached.py [XBE] [--out FILE] [--all]
  XBE defaults to ../unpacked/default.xbe; --all also lists SDK-section targets (D3D, DSOUND, XPP, ...: most are
  SDK replacements, hooked by symbol, not rows).
Output (stdout or FILE): address, section, name, how many references, first referencing address + kind.
"""
import argparse
import re
import struct
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CODE = [".text", "XONLINE", "XNET", "D3DX", "DSOUND", "D3D", "XGRPH", "XPP"]
SDK = set(CODE) - {".text"}


def sections(data):
    base = struct.unpack_from("<I", data, 0x104)[0]
    count = struct.unpack_from("<I", data, 0x11C)[0]
    table = struct.unpack_from("<I", data, 0x120)[0] - base
    out = []
    for i in range(count):
        flags, va, vsize, raw, rsize, name_addr = struct.unpack_from("<6I", data, table + i * 56)
        n = name_addr - base
        name = data[n:data.index(b"\0", n)].decode()
        out.append((name, va, vsize, raw, rsize))
    return out


def ranges(path):
    out = []
    for line in path.read_text().splitlines():
        if line and not line.startswith("#"):
            b, e = line.split()[:2]
            out.append((int(b, 16), int(e, 16)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("xbe", nargs="?", default=str(ROOT.parent / "unpacked" / "default.xbe"))
    ap.add_argument("--out")
    ap.add_argument("--all", action="store_true")
    args = ap.parse_args()
    data = Path(args.xbe).read_bytes()
    secs = sections(data)

    def section_of(a):
        for name, va, vsize, raw, rsize in secs:
            if va <= a < va + vsize:
                return name
        return None

    def byte_at(a):
        for name, va, vsize, raw, rsize in secs:
            if va <= a < va + rsize:
                return data[raw + a - va]
        return None

    keep = ranges(ROOT / "tools/data/code_section_data.txt") + ranges(ROOT / "tools/data/level_eh_code.txt")
    keep.sort()

    def kept(a):
        return any(b <= a < e for b, e in keep)

    names = {}
    for line in (ROOT / "tools/data/default_xbe_symbols.txt").read_text().splitlines():
        if line and not line.startswith("#"):
            p = line.split()
            names[int(p[0], 16)] = p[1]
    rows = {int(m.group(1), 16) for m in
            re.finditer(r'\{"\w+", 0x([0-9a-f]{8}),', (ROOT / "src/engine/shared/functions.cpp").read_text())}

    def candidate(a):
        sec = section_of(a)
        if sec not in CODE or a in rows or kept(a):
            return None
        if sec in SDK and not args.all:
            return None
        if a in names:
            return sec
        prev = byte_at(a - 1)
        if a % 16 == 0 and prev in (0xCC, 0xC3, 0x90):
            return sec
        return None

    refs = defaultdict(list)
    # Words in every non-code section.
    for name, va, vsize, raw, rsize in secs:
        if name in CODE:
            continue
        for off in range(0, rsize - 3, 4):
            w = struct.unpack_from("<I", data, raw + off)[0]
            if candidate(w):
                refs[w].append((va + off, name, "word"))
    # Immediates and direct branch targets in non-code sections that hold code (the levels').
    for name, va, vsize, raw, rsize in secs:
        if name in CODE or name.startswith((".", "$$")) or name in ("XON_RD", "DOLBY", "dspimage"):
            continue
        blob = data[raw:raw + rsize]
        with tempfile.NamedTemporaryFile(suffix=".bin") as tmp:
            tmp.write(blob)
            tmp.flush()
            dis = subprocess.run(["objdump", "-D", "-b", "binary", "-m", "i386", "-M", "intel",
                                  f"--adjust-vma={va:#x}", tmp.name], capture_output=True, text=True).stdout
        for line in dis.splitlines():
            m = re.match(r"\s*([0-9a-f]+):\s+(?:[0-9a-f]{2} )+\s*(\w+)\s+(.*)", line)
            if not m:
                continue
            at, op, operands = int(m.group(1), 16), m.group(2), m.group(3)
            for t in re.findall(r"0x([0-9a-f]{5,8})\b", operands):
                t = int(t, 16)
                if candidate(t):
                    refs[t].append((at, name, op))
    out = [f"# host_unreached.py: {len(refs)} functions without C reachable from data or level code"]
    for a in sorted(refs):
        r = refs[a]
        out.append(f"{a:08x} {section_of(a)} {names.get(a, '?')} refs={len(r)} first={r[0][0]:08x}({r[0][1]},{r[0][2]})")
    text = "\n".join(out) + "\n"
    if args.out:
        Path(args.out).write_text(text)
        print(out[0])
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
