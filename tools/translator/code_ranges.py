#!/usr/bin/env python3
"""code_ranges.py: the instruction bytes of every level function (recursive descent, as cfg.py), merged into ranges.
usage: build/translator_venv/bin/python tools/translator/code_ranges.py [--out FILE]
Writes 'begin end' hex lines (default tools/data/level_code_ranges.txt). The own exe's x86 census
(COD_BRO_X86_CENSUS=1) fills these bytes with INT3 - only instructions, so jump tables and other data in the level
sections stay readable - and with them the level EH code, so any original x86 that still runs traps at its address.
"""
import argparse
import os
import sys

TOOLS = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, TOOLS)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.chdir(os.path.dirname(TOOLS))
import capstone  # noqa: E402
import cfg  # noqa: E402
import fnpack  # noqa: E402

LEVELS = ["PROLOGUE", "ORAN", "TUNISIA", "KASSPASS", "KASSRINE", "LIBEATOR", "GELA", "PIANLUPO", "TROINA", "OMAHEACH",
          "MONS1", "EILEDORF", "BUCHHOLZ", "DRAGEETH"]


def main():
    ap = argparse.ArgumentParser(usage=__doc__)
    ap.add_argument("--out", default="tools/data/level_code_ranges.txt")
    args = ap.parse_args()
    functions, _ = fnpack.load_names()
    image = fnpack.Image()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    spans = []
    for level in LEVELS:
        sec = next(s for s in image.sections if s[0] == level)
        lo, hi = sec[1], sec[1] + max(sec[2], sec[4])
        starts = {a: n for a, n in functions.items() if lo <= a < hi}
        count = 0
        for a in sorted(starts):
            fn = cfg.Fn(a, starts[a])
            cfg.decode(fn, image, md, lo, hi, starts)
            for addr, ins in fn.ins.items():
                spans.append((addr, addr + ins.size))
                count += 1
        print(f"{level}: {len(starts)} functions, {count} instructions")
    spans.sort()
    merged = []
    for b, e in spans:
        if merged and b <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], e)
        else:
            merged.append([b, e])
    with open(args.out, "w") as f:
        f.write("# begin end (hex): instruction bytes of the level functions (tools/translator/code_ranges.py);\n"
                "# the x86 census (COD_BRO_X86_CENSUS=1) fills them with INT3\n")
        for b, e in merged:
            f.write(f"{b:08x} {e:08x}\n")
    print(f"{len(merged)} ranges, {sum(e - b for b, e in merged)} bytes -> {args.out}")


if __name__ == "__main__":
    main()
