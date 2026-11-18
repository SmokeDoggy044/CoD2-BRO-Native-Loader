#!/usr/bin/env python3
"""host_unreached.py: original engine functions the own engine (cod_bro / --host) can still reach but has no C for.

Under --host every byte of the engine's code sections is INT3 (host_image.cpp), so a function without a C row crashes
the moment a mission reaches it. Replays only find the ones their missions reach; this lists them statically:
  - aligned 32-bit words in every non-code section (vtables, callback tables, level data) and
  - call/jmp/push/mov immediates in the level sections' code (objdump)
that point at a function start in a cleared code section (a Ghidra function, or a 16-byte aligned address after
INT3/ret padding = a gap function) with no row in src/engine/shared/functions.cpp.

With --source it lists code addresses written as literals in OUR C instead (src/**/*.cpp|h, comments and tools/data/unreached_reviewed.csv skipped):
handlers, ctors and callbacks our C passes by title address (Cmd_AddCommand(name, 0x...), material type setters) -
neither data words nor replays show those until a player's mission calls one (#24: water, server commands).

usage: tools/host_unreached.py [XBE] [--out FILE] [--all] [--source]
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


DEFAULT_XBE = ROOT.parent / "unpacked" / "default.xbe"


def collect(data, all_sdk=False):
    """{target: [(referencing address, section, kind)]}, the section lookup and the symbol names."""
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

    # Vtable-shaped runs: 3+ consecutive words in .rdata / .data pointing into .text, at least half of them with a
    # row. Their other members are methods whatever precedes them (0x0030ee80 follows a switch byte table).
    text = next(s for s in secs if s[0] == ".text")
    vt_members = set()
    for name, va, vsize, raw, rsize in secs:
        if name not in (".rdata", ".data"):
            continue
        words = struct.unpack_from(f"<{rsize // 4}I", data, raw)
        i = 0
        while i < len(words):
            j = i
            while j < len(words) and text[1] <= words[j] < text[1] + text[2]:
                j += 1
            if j - i >= 3 and 2 * sum(w in rows for w in words[i:j]) >= j - i:
                vt_members.update(words[i:j])
            i = max(j, i + 1)

    def candidate(a):
        sec = section_of(a)
        if sec not in CODE or a in rows or kept(a):
            return None
        if sec in SDK and not all_sdk:
            return None
        if a in names or a in vt_members:
            return sec
        # 16-byte aligned after padding or the end of a function: ret, ret N (C2 xx xx), jmp [reg+disp8] (FF 6x xx),
        # jmp rel32 (E9). Until 2026-11-18 only CC/C3/90 counted: a method after `ret 4` was missed (#27 crash).
        prev = byte_at(a - 1)
        if a % 16 == 0 and (prev in (0xCC, 0xC3, 0x90) or byte_at(a - 3) == 0xC2 or
                            (byte_at(a - 3) == 0xFF and byte_at(a - 2) & 0xF8 == 0x60) or byte_at(a - 5) == 0xE9):
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
    return refs, section_of, names, candidate


def source_refs(candidate):
    """{target: [(file:line, section, "src")]} for code-section literals outside comments in our C."""
    import csv
    reviewed = {int(r["address"], 16) for r in csv.DictReader(open(ROOT / "tools/data/unreached_reviewed.csv"))}
    refs = defaultdict(list)
    for f in sorted((ROOT / "src").rglob("*")):
        if f.suffix not in (".cpp", ".h", ".inc"):
            continue
        # static_init_generated.cpp keys each translated stub by its address; src/runtime is the loader (kernel path).
        if f.name == "static_init_generated.cpp" or "runtime" in f.relative_to(ROOT).parts[:2]:
            continue
        for n, line in enumerate(f.read_text(errors="replace").splitlines(), 1):
            code = line.split("//", 1)[0]
            for t in re.findall(r"\b0x([0-9a-fA-F]{8})\b", code):
                t = int(t, 16)
                sec = candidate(t)
                if sec and t not in reviewed:
                    refs[t].append((f"{f.relative_to(ROOT)}:{n}", sec, "src"))
    return refs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("xbe", nargs="?", default=str(DEFAULT_XBE))
    ap.add_argument("--out")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--source", action="store_true", help="literals in our C instead of data/level code")
    args = ap.parse_args()
    refs, section_of, names, candidate = collect(Path(args.xbe).read_bytes(), args.all)
    if args.source:
        refs = source_refs(candidate)
        out = [f"# host_unreached.py --source: {len(refs)} code addresses without C written as literals in src/"]
        for a in sorted(refs):
            r = refs[a]
            out.append(f"{a:08x} {section_of(a)} {names.get(a, '?')} refs={len(r)} at " + ",".join(x[0] for x in r[:4]))
        text = "\n".join(out) + "\n"
        (Path(args.out).write_text(text) if args.out else sys.stdout.write(text))
        return
    out = [f"# host_unreached.py: {len(refs)} functions without C reachable from data or level code"]
    for a in sorted(refs):
        r = refs[a]
        # Code references (call/jmp/push/mov in level code) first: a data word that happens to look like an
        # address must not hide a real call.
        code = [x for x in r if x[2] != "word"]
        first = (code or r)[0]
        kinds = ",".join(sorted({x[2] for x in r}))
        level = sum(1 for x in r if x[1] not in (".rdata", ".data", ".data1", "XON_RD", ".XTLID", "$$XTIMAGE", "$$XSIMAGE"))
        out.append(f"{a:08x} {section_of(a)} {names.get(a, '?')} refs={len(r)} code={len(code)} level={level} kinds={kinds} "
                   f"first={first[0]:08x}({first[1]},{first[2]})")
    text = "\n".join(out) + "\n"
    if args.out:
        Path(args.out).write_text(text)
        print(out[0])
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
