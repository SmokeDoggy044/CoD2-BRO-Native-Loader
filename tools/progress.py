#!/usr/bin/env python3
"""Per-function decompilation progress, per section and module.

Reads the symbol file, the XBE's section table, our name lists (manual_names.csv,
applied q3_name_proposals.csv), game/code/functions.cpp (decompiled functions) and
functions.csv (comparison results); modules come from tools/data/modules.csv.
Writes tools/data/progress.csv (one row per function) and prints a summary.

usage: tools/progress.py [--module NAME [--state STATE]]
  --module NAME  list that module's functions not rewritten yet (address, name, state)
  --state STATE  only those in one state (unnamed, named, decompiled, untested, ...)

States: unnamed; library (named by signature: C runtime, XAPI, thunks to them);
named (by us, not decompiled); untested (decompiled, no conclusive test yet);
inconclusive (tests ran, none conclusive); mismatched (mismatch or drift on
record); matched (matched, under functions.csv's 100 for settled); settled (100+
matched, none mismatched); replaced (runs instead of the original).
Rewritten = matched + settled + replaced (the progress counter's "matched").
"""
import argparse
import collections
import csv
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA = ROOT / "tools" / "data"
XBE = ROOT.parent / "unpacked" / "default.xbe"
EXCLUDED = {"XGRPH", "XONLINE", "DSOUND", "XNET", "D3DX", "D3D", "XPP"}  # Microsoft libraries
REWRITTEN = {"matched", "settled", "replaced"}


def sections():
    d = XBE.read_bytes()
    base = struct.unpack_from("<I", d, 0x104)[0]
    count, headers = struct.unpack_from("<II", d, 0x11C)
    out = []
    for i in range(count):
        _, va, vs, _, _, name_at = struct.unpack_from("<IIIIII", d, headers - base + i * 56)
        name = d[name_at - base:d.index(b"\0", name_at - base)].decode()
        out.append((name, va, va + vs))
    return out


def load_modules():
    prefixes, ranges = [], []
    with open(DATA / "modules.csv") as f:
        for line in f:
            if line.startswith("#") or line.startswith("kind,"):
                continue
            kind, pattern, module = next(csv.reader([line]))[:3]
            if kind == "prefix":
                prefixes.append((re.compile(pattern), module))
            else:
                lo, hi = (int(x, 16) for x in pattern.split("-"))
                ranges.append((lo, hi, module))
    return prefixes, ranges


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--module")
    ap.add_argument("--state")
    args = ap.parse_args()

    syms = {}
    for line in open(DATA / "default_xbe_symbols.txt"):
        if not line.startswith("#"):
            a, n = line.split(None, 1)
            syms[int(a, 16)] = n.strip()
    ours = {int(r["address"], 16) for r in csv.DictReader(open(DATA / "manual_names.csv")) if r["kind"] == "function"}
    for r in csv.DictReader(open(DATA / "q3_name_proposals.csv")):
        a = int(r["address"], 16)
        if syms.get(a) == r["name"]:
            ours.add(a)
    decompiled = {}
    for m in re.finditer(r'\{"(\w+)", 0x([0-9a-f]{8}),.*Status::(\w+)\}', (ROOT / "game/code/functions.cpp").read_text()):
        decompiled[int(m.group(2), 16)] = m.group(3)
    results = {int(r["address"], 16): r for r in csv.DictReader(open(DATA / "functions.csv"))}
    prefixes, ranges = load_modules()
    secs = sections()

    rows = []
    for a, name in sorted(syms.items()):
        sec = next((s for s, lo, hi in secs if lo <= a < hi), None)
        if sec is None or sec in EXCLUDED:
            continue
        section = ".text" if sec == ".text" else "levels"
        module = next((m for p, m in prefixes if p.match(name)), None)
        module = module or next((m for lo, hi, m in ranges if lo <= a < hi), None)
        module = module or (sec.lower() if section == "levels" else "unassigned")
        unnamed = name.startswith("FUN_") or name.startswith("thunk_FUN_")
        r = results.get(a)
        tested = matched = 0
        if a in decompiled:
            tested, matched = (int(r["tested"]), int(r["matched"])) if r else (0, 0)
            bad = r and (int(r["mismatched"]) or int(r["drift"]))
            if decompiled[a] == "Replaced":
                state = "replaced"
            elif bad:
                state = "mismatched"
            elif matched >= 100:
                state = "settled"
            elif matched:
                state = "matched"
            elif tested:
                state = "inconclusive"
            else:
                state = "untested"
        elif unnamed:
            state = "unnamed"
        elif a in ours:
            state = "named"
        else:
            state = "library"
        rows.append((a, name, section, module, state, tested, matched))

    with open(DATA / "progress.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["address", "name", "section", "module", "state", "tested", "matched"])
        for a, name, section, module, state, tested, matched in rows:
            w.writerow([f"{a:08x}", name, section, module, state, tested, matched])

    if args.module:
        for a, name, section, module, state, *_ in rows:
            if module == args.module and state not in REWRITTEN and (not args.state or state == args.state):
                print(f"{a:08x} {state:12} {name}")
        return

    def summary(key):
        groups = collections.defaultdict(collections.Counter)
        for row in rows:
            groups[key(row)][row[4]] += 1
        print(f"{'':12} {'total':>6} {'left':>6} {'rewritten':>9} {'untested':>8} {'inconcl.':>8} {'mismatch':>8} {'named':>6} {'library':>7} {'unnamed':>7}")
        for g, c in sorted(groups.items(), key=lambda kv: -sum(kv[1].values())):
            total = sum(c.values())
            done = sum(c[s] for s in REWRITTEN)
            print(f"{g:12} {total:6} {total - done:6} {done:9} {c['untested']:8} {c['inconclusive']:8} "
                  f"{c['mismatched']:8} {c['named']:6} {c['library']:7} {c['unnamed']:7}")
    summary(lambda r: r[2])
    print()
    summary(lambda r: r[3] if r[2] == ".text" else "levels")
    named = sum(1 for r in rows if r[2] == ".text" and r[4] not in ("unnamed", "library"))
    done = sum(1 for r in rows if r[4] in REWRITTEN)
    total = len(rows)  # library functions in .text stay counted (user decision 2026-10-18)
    print(f"\ncounter: named {named}/{total} · matched {done}/{total}")


if __name__ == "__main__":
    main()
