#!/usr/bin/env python3
"""Lists source lines whose x87 (long double) arithmetic GCC narrows to SSE when optimizing.

With optimization on, GCC rewrites (float)((long double)a OP (long double)b) for float a, b
into a float operation (MULSS/ADDSS/...): exact in one rounding, but it then uses MXCSR's
rounding, flush-to-zero and flags instead of the x87 control word the title's FMUL ran under
(release-only FLT_MAX vs inf, denormal vs 0). No flag turns this off (-frounding-math neither)
and -O0 never does it, so the debug build matches and the release build does not.

GCC narrows in the front end (a direct cast) and in the middle end (through a long double
variable, a long double return value). This compiles each game file twice into a scratch
directory with -g and without inlining (-O0 and -O2 -S) and prints the source lines with x87
arithmetic at -O0 that have more scalar SSE arithmetic at -O2 (c++filt the function names).
Fix such a line by passing one operand through platform_x87_ld (platform/compiler.h).

It also lists the lines whose signaling compare (COMISS, invalid flag on any NaN, as the
title's) -O2 made quiet: GCC folds !(a <= b) into an unordered compare (UCOMISS).

usage: tools/x87_narrowing.py [file.cpp ...]   (default: every file in game/code)
"""
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB = os.path.join(ROOT, "build/linux-loader-release/compile_commands.json")

SSE_ARITH = re.compile(r"^\s+(add|sub|mul|div)ss\b")
COMISS = re.compile(r"^\s+comiss\b")
UCOMISS = re.compile(r"^\s+ucomiss\b")
X87_ARITH = re.compile(r"^\s+(f(add|sub|subr|mul|div|divr)[spl]?|fi(add|sub|subr|mul|div|divr)[sl]?)\b")
FILE = re.compile(r'^\s+\.file\s+(\d+)\s+"([^"]+)"(?:\s+"([^"]+)")?')
LOC = re.compile(r"^\s+\.loc\s+(\d+)\s+(\d+)")
LABEL = re.compile(r"^([A-Za-z_][\w.$]*):")


def line_counts(command, source, opt, out_dir):
    """{(file, line): [x87 arithmetic, scalar SSE arithmetic, COMISS, UCOMISS, function]} from the -g -S output."""
    args = shlex.split(command)
    args = [a for a in args if not re.fullmatch(r"-O[0-9s]?|-g[0-9]?", a)]
    i = args.index("-o")
    del args[i:i + 2]
    out = os.path.join(out_dir, os.path.basename(source) + opt + ".s")
    args += [opt, "-g", "-fno-inline", "-S", "-o", out]
    subprocess.run(args, check=True, cwd=out_dir, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    files, counts, name, at = {}, {}, None, None
    with open(out, errors="replace") as f:
        for line in f:
            m = FILE.match(line)
            if m:
                files[m.group(1)] = os.path.join(m.group(2), m.group(3)) if m.group(3) else m.group(2)
                continue
            m = LOC.match(line)
            if m:
                at = (files.get(m.group(1), m.group(1)), int(m.group(2)))
                continue
            m = LABEL.match(line)
            if m and not m.group(1).startswith(".L"):
                name, at = m.group(1), None
                continue
            if at is None:
                continue
            kind = (0 if X87_ARITH.match(line) else 1 if SSE_ARITH.match(line) else
                    2 if COMISS.match(line) else 3 if UCOMISS.match(line) else None)
            if kind is not None:
                c = counts.setdefault(at, [0, 0, 0, 0, name])
                c[kind] += 1
    return counts


def main():
    entries = json.load(open(DB))
    wanted = [os.path.abspath(p) for p in sys.argv[1:]]
    jobs = [e for e in entries if "/game/code/" in e["file"] and e["file"].endswith(".cpp")
            and (not wanted or os.path.abspath(e["file"]) in wanted)]
    with tempfile.TemporaryDirectory() as tmp:
        def one(e):
            a = line_counts(e["command"], e["file"], "-O0", tmp)
            b = line_counts(e["command"], e["file"], "-O2", tmp)
            narrowed = [(at, a[at], b[at]) for at in a if a[at][0] and at in b and b[at][1] > a[at][1]]
            # A signaling compare (COMISS: invalid on any NaN) that -O2 made quiet (!(a <= b) -> UCOMISS).
            quiet = [(at, a[at], b[at]) for at in a if a[at][2] and at in b and b[at][3] > a[at][3]]
            return narrowed, quiet
        seen = set()
        with ThreadPoolExecutor(os.cpu_count()) as pool:
            for narrowed, quiet in pool.map(one, jobs):
                for (path, line), before, after in narrowed:
                    if (path, line, 0) not in seen:
                        seen.add((path, line, 0))
                        print(f"{os.path.relpath(path, ROOT)}:{line}\t{after[4]}\tnarrowed: x87 {before[0]} -> {after[0]}, "
                              f"SSE scalar {before[1]} -> {after[1]}")
                for (path, line), before, after in quiet:
                    if (path, line, 1) not in seen:
                        seen.add((path, line, 1))
                        print(f"{os.path.relpath(path, ROOT)}:{line}\t{after[4]}\tquiet compare: COMISS {before[2]} -> {after[2]}, "
                              f"UCOMISS {before[3]} -> {after[3]}")
    print(f"{len(seen)} lines", file=sys.stderr)


if __name__ == "__main__":
    main()
