"""Side-by-side peaks from tools/mem_sweep.sh: one column per run (its memory.txt), max % of each pool's size.

usage: python3 tools/mem_sweep_sum.py OUT
"""
import pathlib
import re
import sys


def parse(path):
    pools, size, space = {}, {}, {}
    in_table = False
    for line in path.read_text(errors="replace").splitlines():
        if line.startswith("pool "):
            in_table = True
            continue
        if in_table:
            if not line.strip():
                in_table = False
                continue
            name, rest = line[:30].strip(), line[30:].split()
            if len(rest) < 3:
                continue
            size[name] = int(rest[0])
            pools[name] = float(rest[-1])
            continue
        m = re.match(r"(least free|last picture) .*free\s+([\d.]+) MB, largest free block\s+([\d.]+) MB, "
                     r"committed\s+([\d.]+) MB, reserved\s+([\d.]+) MB", line)
        if m:
            key = m.group(1)
            space[f"{key}: free MB"] = float(m.group(2))
            space[f"{key}: largest free MB"] = float(m.group(3))
            space[f"{key}: committed MB"] = float(m.group(4))
            space[f"{key}: reserved MB"] = float(m.group(5))
        m = re.match(r"most committed at once:\s+([\d.]+) MB", line)
        if m:
            space["most committed MB"] = float(m.group(1))
    return pools, size, space


def main():
    out = pathlib.Path(sys.argv[1])
    runs = {}
    for path in sorted(out.glob("*/memory.txt")):
        runs[path.parent.name.replace("_playtest", "")] = parse(path)
    order = sorted(runs, key=lambda n: int(re.match(r"m(\d+)", n).group(1)) if re.match(r"m\d+", n) else 99)
    names, sizes = [], {}
    for run in order:
        for name, value in runs[run][1].items():
            if name not in names:
                names.append(name)
            sizes[name] = max(sizes.get(name, 0), value)
    print(f"{'pool (max % per run)':30s} {'size':>10s} " + " ".join(f"{r[:7]:>7s}" for r in order) + "    max")
    for name in names:
        cells = [runs[r][0].get(name) for r in order]
        best = max((c for c in cells if c is not None), default=0)
        print(f"{name:30s} {sizes[name]:10d} " +
              " ".join(f"{c:7.1f}" if c is not None else "      -" for c in cells) + f"  {best:5.1f}")
    keys = []
    for run in order:
        for key in runs[run][2]:
            if key not in keys:
                keys.append(key)
    if keys:
        print(f"\n{'address space':30s} {'':10s} " + " ".join(f"{r[:7]:>7s}" for r in order))
        for key in keys:
            cells = [runs[r][2].get(key) for r in order]
            print(f"{key:41s} " + " ".join(f"{c:7.0f}" if c is not None else "      -" for c in cells))


if __name__ == "__main__":
    main()
