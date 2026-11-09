#!/usr/bin/env python3
"""host_inventory.py: what our engine C still needs from the route-B loader (plan 2026-11-07, phase 0.1).

The own engine host replaces the loader. Today the loader maps default.xbe at its original addresses, runs
the kernel/XAPI imitation, hooks Replaced functions and runs everything else as original x86. This scan
lists every dependency the host must take over:

  c_call     title functions our C calls by address (title_function / title_call_at), by owner state
  c_data     title globals our C reaches by address (title_global / text_at / literals), by section
  data_code  code addresses stored in the image's data sections (vtables, callback tables): calls through
             them land on original .text addresses, so each target needs C or a thunk there
  kernel     kernel imports (thunk table) and the modules whose functions call them
  sdk        calls into the SDK sections (D3D, DSOUND, XGRPH, ...) from code that stays original
  orig_call  original (non-C) .text functions that stay reachable: callees of what our C calls, closed
             over direct calls (mslib/crt/unnamed/set-aside), plus the level slots that point at them

"Stays original" after phase 0.2 = no C at all (mslib, crt, library, unnamed, named-only, set-aside, levels);
untested / inconclusive / mismatched rows have C and run as C in the own host (user 2026-11-07).

usage: tools/host_inventory.py [--out docs/formats/host_inventory]   (writes .md summary + .csv rows)
Reads the XBE, the exported symbols, progress.csv and the engine sources (no Ghidra needed).
"""
import argparse
import bisect
import collections
import csv
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fnpack  # noqa: E402

ROOT = fnpack.ROOT
KERNEL_KEY = 0x5B6D40B6  # retail thunk XOR key
HAS_C = {"replaced", "settled", "matched", "accepted", "untested", "inconclusive", "mismatched", "offline"}
LEVELS = {"PROLOGUE", "ORAN", "TUNISIA", "KASSPASS", "KASSRINE", "LIBEATOR", "GELA", "PIANLUPO", "TROINA",
          "OMAHEACH", "MONS1", "EILEDORF", "BUCHHOLZ", "DRAGEETH"}
LIT = re.compile(r"0x(00[0-9a-fA-F]{6})\b")
CALL_CTX = re.compile(r"(title_function<|title_call_at\w*\(|title_call\w*\()")
DATA_CTX = re.compile(r"(title_global<|text_at\(|title_mm\(|title_\w+\()")


def load_progress():
    rows = {}
    with open(fnpack.DATA / "progress.csv", newline="") as f:
        for r in csv.DictReader(f):
            rows[int(r["address"], 16)] = (r["name"], r["module"], r["state"])
    return rows


def kernel_thunks(image):
    va = struct.unpack_from("<I", image.data, 0x158)[0] ^ KERNEL_KEY
    names = {}
    for line in open(fnpack.DATA / "xboxkrnl_ordinals.txt"):
        parts = line.split()
        if len(parts) == 2:
            names[int(parts[0])] = parts[1]
    thunks = {}
    i = 0
    while True:
        (word,) = struct.unpack("<I", image.read(va + i * 4, 4) or b"\0\0\0\0")
        if not word:
            break
        thunks[va + i * 4] = names.get(word & 0x7FFFFFFF, f"ord{word & 0x7FFFFFFF}")
        i += 1
    return thunks


def scan_sources():
    """(address, kind, file) for each hex literal in the engine C: kind = call / data / literal."""
    out = []
    for path in sorted((ROOT / "src/engine").rglob("*")):
        if path.suffix not in (".cpp", ".h", ".c", ".S", ".s", ".inc"):
            continue
        rel = str(path.relative_to(ROOT))
        if rel.endswith("shared/functions.cpp"):
            continue  # the hook table (every C function's own address), not a dependency
        for line in path.read_text(errors="replace").splitlines():
            code = line.split("//")[0]
            for m in LIT.finditer(code):
                before = code[:m.start()]
                kind = ("call" if CALL_CTX.search(before[-60:]) else
                        "data" if DATA_CTX.search(before[-60:]) else "literal")
                out.append((int(m.group(1), 16), kind, rel))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="docs/formats/host_inventory")
    args = ap.parse_args()

    image = fnpack.Image()
    prog = load_progress()
    starts = sorted(prog)

    def owner(va):
        i = bisect.bisect_right(starts, va) - 1
        return starts[i] if i >= 0 else None

    def state_of(a):
        return prog[a][2] if a in prog else "?"

    def stays_original(a):
        return a in prog and prog[a][2] not in HAS_C

    rows = []  # kind, address, name, module, state, detail

    # 1. our C -> title addresses
    lits = scan_sources()
    c_calls = collections.defaultdict(set)
    c_data = collections.defaultdict(set)
    for a, kind, rel in lits:
        sec = image.section(a)
        if sec is None:
            continue
        if sec in fnpack.DATA_SECTIONS or sec.startswith("$$") or sec in ("dspimage", ".XTLID"):
            c_data[a].add(rel)
        elif kind != "data":
            c_calls[a].add(rel)
        else:
            c_data[a].add(rel)
    call_state = collections.Counter()
    call_targets = set()
    for a, files in sorted(c_calls.items()):
        sec = image.section(a)
        o = owner(a) if sec == ".text" else None
        if sec == ".text" and o == a:
            st = state_of(a)
            name, module = prog[a][0], prog[a][1]
        elif sec == ".text":
            continue  # inside a function: a jump-table / return address literal, not a call target
        else:
            st, name, module = ("level" if sec in LEVELS else "sdk"), sec, sec
        call_state[(module if st not in HAS_C else "(has C)", st)] += 1
        if st not in HAS_C:
            call_targets.add(a)
            rows.append(("c_call", a, name, module, st, " ".join(sorted(files))[:200]))
    data_sec = collections.Counter(image.section(a) for a in c_data)

    # 2. code pointers stored in data sections
    text_lo, text_hi = 0x11000, 0x11000 + 0x368e7c
    dc = collections.Counter()
    dc_targets = set()
    for name, start, _, raw, raw_size in image.sections:
        if name not in fnpack.DATA_SECTIONS:
            continue
        blob = image.data[raw:raw + raw_size]
        for off in range(0, len(blob) - 3, 4):
            (w,) = struct.unpack_from("<I", blob, off)
            if text_lo <= w < text_hi and w in prog:
                dc[(prog[w][1] if prog[w][2] not in HAS_C else "(has C)", prog[w][2] if prog[w][2] not in HAS_C else "C")] += 1
                dc_targets.add(w)
                if prog[w][2] not in HAS_C:
                    rows.append(("data_code", w, prog[w][0], prog[w][1], prog[w][2], f"{name}:{start + off:08x}"))
    dc_c = sum(1 for w in dc_targets if prog[w][2] in HAS_C)

    # 3. direct calls from original code, closed over what stays original and is reachable from our C
    calls = collections.defaultdict(set)  # caller start -> callee targets (any section)
    sdk_from = collections.Counter()
    kernel = kernel_thunks(image)
    kern_from = collections.defaultdict(collections.Counter)
    for name, start, _, raw, raw_size in image.sections:
        if name in fnpack.DATA_SECTIONS or name.startswith("$$") or name in ("dspimage", ".XTLID"):
            continue
        blob = image.data[raw:raw + raw_size]
        for j in range(len(blob) - 5):
            op = blob[j]
            if op in (0xE8, 0xE9):
                t = (start + j + 5 + struct.unpack_from("<i", blob, j + 1)[0]) & 0xFFFFFFFF
                if image.section(t) is None:
                    continue
                o = owner(start + j) if name == ".text" else None
                calls[o if o is not None else (name, start + j)].add(t)
            elif op == 0xFF and blob[j + 1] in (0x15, 0x25):
                (t,) = struct.unpack_from("<I", blob, j + 2)
                if t in kernel:
                    o = owner(start + j) if name == ".text" else None
                    mod = prog[o][1] if o in prog else name
                    kern_from[kernel[t]][mod] += 1

    reach = set()
    todo = list(call_targets | {w for w in dc_targets if stays_original(w)})
    while todo:
        a = todo.pop()
        if a in reach or image.section(a) in LEVELS:
            continue  # level code comes from the dump as it is (docs/formats/level_boundary.md)
        reach.add(a)
        for t in calls.get(a, ()):
            if t in prog and stays_original(t):
                todo.append(t)
            elif image.section(t) not in (".text", None):
                sdk_from[(image.section(t), prog[a][1] if a in prog else "?")] += 1
    reach_mod = collections.Counter((prog[a][1], prog[a][2]) for a in reach if a in prog)
    for a in sorted(reach):
        if a in prog and a not in call_targets:
            rows.append(("orig_call", a, prog[a][0], prog[a][1], prog[a][2], "reached"))
    for k, mods in sorted(kern_from.items()):
        rows.append(("kernel", 0, k, " ".join(f"{m}:{n}" for m, n in mods.most_common()), "", ""))

    # level slots that point at code without C
    slot_orig = []
    imports = (ROOT / "src/engine/game/g_scr_imports.cpp").read_text()
    own_slots = {int(m.group(1), 16) for m in re.finditer(r"\{(0x[0-9a-f]+), own_address\(", imports)}
    bpath = ROOT / "docs/formats/level_boundary.csv"
    if bpath.exists():
        with open(bpath, newline="") as f:
            for r in csv.DictReader(f):
                if r["kind"] == "slot" and re.fullmatch(r"[0-9a-f]{8}", r["detail"] or ""):
                    a = int(r["detail"], 16)
                    if int(r["address"]) * 4 in own_slots:  # our import table stores our C there (2026-11-08)
                        continue
                    if stays_original(a):
                        slot_orig.append((a, prog[a][0], prog[a][1], prog[a][2]))

    with open(args.out + ".csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["kind", "address", "name", "module", "state", "detail"])
        for k, a, n, m, s, d in rows:
            w.writerow([k, f"{a:08x}" if a else "", n, m, s, d])

    md = ["# Own engine host: what the C still takes from the loader",
          "",
          "Generated by `tools/host_inventory.py` (phase 0.1, 2026-11-07). Rows in `host_inventory.csv`.",
          "\"Stays original\" = no C at all after phase 0.2 (untested/inconclusive rows run as C).",
          "",
          f"## Our C calling title code by address ({sum(call_state.values())} distinct targets)",
          "", "| module | state | targets |", "|---|---|---|"]
    md += [f"| {m} | {s} | {n} |" for (m, s), n in sorted(call_state.items(), key=lambda x: -x[1])]
    md += ["", f"## Title data our C reaches by address ({len(c_data)} distinct literals)", "",
           "| section | addresses |", "|---|---|"]
    md += [f"| {s} | {n} |" for s, n in data_sec.most_common()]
    md += ["", "## Code addresses stored in data sections (vtables, callbacks)", "",
           f"{len(dc_targets)} distinct .text targets, {dc_c} of them have C (need a thunk/jump at the original "
           "address, or the tables rebuilt in C).", "", "| module | state | stored pointers |", "|---|---|---|"]
    md += [f"| {m} | {s} | {n} |" for (m, s), n in sorted(dc.items(), key=lambda x: -x[1])[:25]]
    md += ["", f"## Original code reachable from our C ({len(reach)} functions, closed over direct calls)", "",
           "| module | state | functions |", "|---|---|---|"]
    md += [f"| {m} | {s} | {n} |" for (m, s), n in reach_mod.most_common()]
    md += ["", "### SDK sections called from that original code", "", "| section | from module | call sites |",
           "|---|---|---|"]
    md += [f"| {s} | {m} | {n} |" for (s, m), n in sdk_from.most_common()]
    md += ["", f"## Level slots without C ({len(slot_orig)})", "", "| address | name | module | state |",
           "|---|---|---|---|"]
    md += [f"| {a:08x} | {n} | {m} | {s} |" for a, n, m, s in sorted(slot_orig)]
    md += ["", f"## Kernel imports ({len(kern_from)} used of {len(kernel)} imported)", "",
           "| import | calling modules (call sites) |", "|---|---|"]
    md += [f"| {k} | {', '.join(f'{m} {n}' for m, n in mods.most_common())} |" for k, mods in sorted(kern_from.items())]
    Path(args.out + ".md").write_text("\n".join(md) + "\n")
    print(f"c_call targets without C: {len(call_targets)}; data literals: {len(c_data)}; data code ptrs: "
          f"{len(dc_targets)} ({dc_c} with C); original reachable: {len(reach)}; level slots w/o C: {len(slot_orig)}; "
          f"kernel imports used: {len(kern_from)}/{len(kernel)}")


if __name__ == "__main__":
    main()
