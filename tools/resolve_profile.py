#!/usr/bin/env python3
"""Names the loader's own addresses in a --profile report.

Release packages are stripped, so the profiler logs our code as a file and offset
("cod_bro_loader_core.dll +0x68af7", "cod_bro_loader +0x72f49"). Given the unstripped
binaries of the same release (the build directory it was packaged from), this prints
the log with those offsets replaced by function names, and the samples summed per
function under each thread.

usage: resolve_profile.py LOG BINARY [BINARY...]
  BINARY: the unstripped cod_bro_loader_core.dll (Windows) or cod_bro_loader (Linux)
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
        if len(parts) == 3 and parts[1] in "tTwW" and parts[0].strip():
            table.append((int(parts[0], 16) - base, parts[2]))
    return table


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    tables = {}
    for binary in sys.argv[2:]:
        table = symbols(binary)
        tables[os.path.basename(binary)] = ([offset for offset, _ in table], [name for _, name in table])

    place = re.compile(r"^(.*profile\s+)(\d+\.\d+)%(\s+)(\S+) \+0x([0-9a-f]+)(?: \(.*\))?$")

    def name(module, offset):
        offsets, names = tables[module]
        i = bisect.bisect_right(offsets, offset) - 1
        return names[i] if i >= 0 else f"+0x{offset:x}"

    per_function = collections.Counter()
    thread = None
    with open(sys.argv[1], encoding="utf-8", errors="replace") as log:
        for line in log:
            line = line.rstrip("\r\n")
            if " profile  thread " in line:
                if per_function:
                    print_totals(thread, per_function)
                thread, per_function = line, collections.Counter()
            m = place.match(line)
            if m and m.group(4) in tables:
                function = name(m.group(4), int(m.group(5), 16))
                per_function[function] += float(m.group(2))
                line = f"{m.group(1)}{m.group(2)}%{m.group(3)}{m.group(4)}: {function}"
            print(line)
    if per_function:
        print_totals(thread, per_function)


def print_totals(thread, per_function):
    print(f"--- our functions, summed: {thread.split('profile  ')[-1]}")
    for function, percent in per_function.most_common(25):
        print(f"  {percent:5.1f}%  {function}")


if __name__ == "__main__":
    main()
