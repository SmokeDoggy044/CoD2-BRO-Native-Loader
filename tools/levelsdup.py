#!/usr/bin/env python3
"""Body hashes of every function, and the level-section functions that share a body.

The levels' compiler instantiated the same templates once per level. Two hashes per function:
  masked  disassembly with every address/large immediate masked except the engine-interface slots (0x00922xxx):
          copies that differ only in strings, hashes, globals or callees group together (template families)
  exact   only branch/call targets masked: same code apart from what it calls (tools/twin_register.py checks
          the callees)
Writes build/all_body_hashes.csv (every function) and build/levels_groups.csv (level rows grouped by masked
hash, largest group first), and prints a summary.
usage: tools/levelsdup.py [--module prologue]   (about 20 s; objdump per function, 16 threads)
"""
import argparse
import bisect
import collections
import csv
import hashlib
import re
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import fnpack  # noqa: E402

BUILD = fnpack.ROOT / "build"
HEX = re.compile(r"0x[0-9a-f]{5,}")
BRANCH = re.compile(r"^(call|j\w+) 0x[0-9a-f]+")


def main():
    parser = argparse.ArgumentParser(usage=__doc__)
    parser.add_argument("--module", default="prologue", help="summary line for this level")
    args = parser.parse_args()
    functions, _ = fnpack.load_names()
    starts = sorted(functions)
    image = fnpack.Image()
    rows = list(csv.DictReader(open(fnpack.DATA / "progress.csv")))

    def hashes(r):
        a = int(r["address"], 16)
        i = bisect.bisect_right(starts, a)
        n = min((starts[i] - a) if i < len(starts) else 0, 0x8000)
        masked, exact = [], []
        for _, _, t in fnpack.disassemble(image, a, n):
            masked.append(HEX.sub(lambda m: m.group(0) if m.group(0).startswith("0x922") else "X", t))
            exact.append(BRANCH.sub(r"\1 X", t))
        md5 = lambda lines: hashlib.md5("\n".join(lines).encode()).hexdigest()  # noqa: E731
        return r, n, md5(masked), md5(exact)

    with ThreadPoolExecutor(16) as pool:
        result = list(pool.map(hashes, rows))
    BUILD.mkdir(exist_ok=True)
    with open(BUILD / "all_body_hashes.csv", "w", newline="") as out:
        w = csv.writer(out)
        w.writerow(["address", "name", "section", "module", "state", "size", "masked", "exact"])
        for r, n, m, e in result:
            w.writerow([r["address"], r["name"], r["section"], r["module"], r["state"], n, m, e])

    groups = collections.defaultdict(list)
    for r, n, m, _ in result:
        if r["section"] == "levels":
            groups[m].append((r["module"], r["name"], int(r["address"], 16), n))
    groups = sorted(groups.values(), key=len, reverse=True)
    level_rows = sum(len(g) for g in groups)
    print(f"level functions {level_rows}, distinct bodies {len(groups)}, "
          f"copies beyond the first {level_rows - len(groups)}")
    print(f"distinct bytes {sum(g[0][3] for g in groups)} of {sum(x[3] for g in groups for x in g)}")
    mine = [g for g in groups if any(x[0] == args.module for x in g)]
    print(f"{args.module}: {len(mine)} bodies, {sum(1 for g in mine if any(x[0] != args.module for x in g))} "
          "shared with another level")
    with open(BUILD / "levels_groups.csv", "w", newline="") as out:
        w = csv.writer(out)
        w.writerow(["group", "size", "module", "address", "name"])
        for i, g in enumerate(groups):
            for module, name, a, n in g:
                w.writerow([i, n, module, f"{a:08x}", name])


if __name__ == "__main__":
    main()
