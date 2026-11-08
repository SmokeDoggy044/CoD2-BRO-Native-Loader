#!/usr/bin/env python3
"""Resolves the [stack_guard] lines (src/engine/shared/stack_guard.h) of one or more loader logs.

usage: tools/stack_guard_report.py LOG... [--lib PATH]
C return addresses become our C function (addr2line on libcod_bro_game.so, using the marker
stack_guard_enter's run-time address against its address in the file); title addresses become the
containing default.xbe function (tools/data/default_xbe_symbols.txt). Prints each distinct finding
once with the logs it came from, and the lowest margin seen per log."""
import argparse
import bisect
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ap = argparse.ArgumentParser()
ap.add_argument("logs", nargs="+")
ap.add_argument("--lib", default=str(ROOT / "build/linux-loader/lib/libcod_bro_game.so"))
a = ap.parse_args()
lib = Path(a.lib)
if not lib.exists():
    lib = next((ROOT / "build/linux-loader").rglob("libcod_bro_game.so"))

syms = []
for line in open(ROOT / "tools/data/default_xbe_symbols.txt"):
    p = line.split()
    if len(p) >= 2 and not line.startswith("#"):
        syms.append((int(p[0], 16), p[1]))
syms.sort()
starts = [s for s, _ in syms]


def title(addr):
    i = bisect.bisect_right(starts, addr) - 1
    return f"{syms[i][1]}+{addr - syms[i][0]:#x}" if i >= 0 else f"{addr:08x}"


nm = subprocess.run(["nm", "-C", str(lib)], capture_output=True, text=True).stdout
marker_file = next(int(l.split()[0], 16) for l in nm.splitlines() if l.endswith("game::stack_guard_enter(unsigned int)"))


def c_function(ret, marker):
    off = ret - (marker - marker_file)
    out = subprocess.run(["addr2line", "-f", "-C", "-e", str(lib), f"{off:#x}"], capture_output=True, text=True).stdout.split("\n")
    return f"{out[0]} ({Path(out[1]).name})" if out and out[0] != "??" else f"lib+{off:#x}"


FIND = re.compile(r"\[stack_guard\] (.+?): C caller ret (\w+) calling title (\w+) at (\w+), C frames (\w+)\.\.(\w+), "
                  r"thread (\w+), detail (\w+), chain ([\w,]+) \(marker stack_guard_enter (\w+)\)")
MARGIN = re.compile(r"\[stack_guard\] new lowest margin (\w+): title at (\w+) above the C frames of ret (\w+) \(title (\w+)\) "
                    r"\(marker stack_guard_enter (\w+)\)")
findings, margins = {}, {}
for log in a.logs:
    for line in open(log, errors="replace"):
        if m := FIND.search(line):
            what, ret, fn, top, lo, hi, thread, detail, chain, marker = m.groups()
            up = " <- ".join(c_function(int(c, 16), int(marker, 16)).split(" (")[0] for c in chain.split(",") if int(c, 16))
            key = (what, c_function(int(ret, 16), int(marker, 16)) + (" <- " + up if up else ""), title(int(fn, 16)))
            findings.setdefault(key, []).append(f"{Path(log).parent.name}: thread {thread} detail {detail}")
        elif m := MARGIN.search(line):
            margin, at, ret, fn, marker = m.groups()
            margins[log] = (int(margin, 16), c_function(int(ret, 16), int(marker, 16)), title(int(fn, 16)))
for (what, c, t), where in sorted(findings.items()):
    print(f"{what}: {c} -> title {t}\n    {len(where)} log(s): {'; '.join(where[:6])}")
if not findings:
    print("no findings")
for log, (margin, c, t) in sorted(margins.items()):
    print(f"lowest margin {Path(log).parent.name}: {margin:#x} below title, C frames of {c} (calling {t})")
