#!/usr/bin/env python3
"""Names the loader's own addresses in a --profile report.

Release packages are stripped, so the profiler logs our code as a file and offset
("cod_bro_loader_core.dll +0x68af7", "cod_bro_loader +0x72f49"). Given the unstripped
binaries of the same release (the build directory it was packaged from), this prints
the log with those offsets replaced by function names, and the samples summed per
function under each thread.

usage: resolve_profile.py [--summary] LOG BINARY [BINARY...]
  BINARY: the unstripped cod_bro_loader_core.dll (Windows) or cod_bro_loader (Linux)
  --summary: per report only its start, fps and threads, each busy thread's places
             summed over every module (system calls by name, offsets dropped)
"""

import bisect
import collections
import os
import re
import subprocess
import sys


def symbols(binary):
    """Sorted (offset from the image base, name) of every function in `binary`."""
    windows = binary.lower().endswith(".dll")
    nm = "i686-w64-mingw32-nm" if windows else "nm"
    base = 0
    if windows:
        headers = subprocess.run(["i686-w64-mingw32-objdump", "-p", binary], capture_output=True, text=True).stdout
        base = int(re.search(r"ImageBase\s+([0-9a-fA-F]+)", headers).group(1), 16)
    else:  # the first LOAD segment (0x08048000 for a non-PIE executable, 0 for PIE)
        headers = subprocess.run(["readelf", "-lW", binary], capture_output=True, text=True).stdout
        base = int(re.search(r"^\s*LOAD\s+0x[0-9a-f]+\s+0x([0-9a-f]+)", headers, re.M).group(1), 16)
    out = subprocess.run([nm, "-C", "-n", binary], capture_output=True, text=True, check=True).stdout
    table = []
    for line in out.splitlines():
        parts = line.split(" ", 2)
        if len(parts) == 3 and parts[1] in "tTwW" and parts[0].strip() and not parts[2].startswith(".L"):
            table.append((int(parts[0], 16) - base, parts[2]))
    return table


def main():
    args = sys.argv[1:]
    summary = bool(args) and args[0] == "--summary"
    if summary:
        args = args[1:]
    if len(args) < 2:
        sys.exit(__doc__)
    tables = {}
    for binary in args[1:]:
        table = symbols(binary)
        tables[os.path.basename(binary)] = ([offset for offset, _ in table], [name for _, name in table])

    place = re.compile(r"^(.*profile\s+)(\d+\.\d+)%(\s+)(\S+) \+0x([0-9a-f]+)(?: \(.*\))?$")

    def name(module, offset):
        offsets, names = tables[module]
        i = bisect.bisect_right(offsets, offset) - 1
        return names[i] if i >= 0 else f"+0x{offset:x}"

    any_place = re.compile(r"^.*profile\s+(\d+\.\d+)%\s+(.*)$")
    per_function = collections.Counter()
    everything = collections.Counter()
    thread = None

    def end_thread():
        if summary and thread:
            print_totals(thread, everything, 12, "all places, summed")
        elif per_function:
            print_totals(thread, per_function, 25, "our functions, summed")

    with open(args[0], encoding="utf-8", errors="replace") as log:
        for line in log:
            line = line.rstrip("\r\n")
            if " profile  " in line and ("thread " in line or "automatic" in line or "sampling" in line):
                end_thread()
                thread, per_function, everything = None, collections.Counter(), collections.Counter()
                if " profile  thread " in line:
                    thread = line
            m = place.match(line)
            if m and m.group(4) in tables:
                function = name(m.group(4), int(m.group(5), 16))
                per_function[function] += float(m.group(2))
                everything[f"{m.group(4)}: {function}"] += float(m.group(2))
                line = f"{m.group(1)}{m.group(2)}%{m.group(3)}{m.group(4)}: {function}"
            elif thread and (a := any_place.match(line)):
                where = re.sub(r" \+0x[0-9a-f]+", "", a.group(2))
                where = re.sub(r"\((\w+)\+0x[0-9a-f]+\)", r"(\1)", where)
                if where.startswith("igd") or "OpenAdapter" in where:  # driver: export names mean nothing
                    where = where.split(" ")[0]
                everything[where] += float(a.group(1))
            if not summary or (" profile  " in line and not any_place.match(line) and "profile  thread " not in line):
                print(line)
    end_thread()


def print_totals(thread, per_function, count, what):
    print(f"--- {what}: {thread.split('profile  ')[-1]}")
    for function, percent in per_function.most_common(count):
        print(f"  {percent:5.1f}%  {function}")


if __name__ == "__main__":
    main()
