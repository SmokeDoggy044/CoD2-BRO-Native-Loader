#!/usr/bin/env python3
"""Per-function decompilation progress, per section and module.

Reads the symbol file, the XBE's section table, our name lists (manual_names.csv,
applied q3_name_proposals.csv), game/code/functions.cpp (decompiled functions) and
functions.csv (comparison results); modules come from tools/data/modules.csv.
Writes tools/data/progress.csv (one row per function) and prints a summary.

usage: tools/progress.py [--module NAME [--state STATE]] [--readme]
  --readme       also rewrite README.md's generated blocks (intro, progress, levels, excluded markers)
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
    ap.add_argument("--readme", action="store_true")
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
        if section == "levels":  # level-section copies of engine helpers stay with their mission
            module = sec.lower()
        else:
            module = next((m for p, m in prefixes if p.match(name)), None)
            module = module or next((m for lo, hi, m in ranges if lo <= a < hi), None) or "unassigned"
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
    named = sum(1 for r in rows if r[4] not in ("unnamed", "library"))
    done = sum(1 for r in rows if r[4] in REWRITTEN)
    total = len(rows)  # library functions in .text stay counted (user decision 2026-10-18)
    print(f"\ncounter: named {named}/{total} · matched {done}/{total}")
    if args.readme:
        excluded = sum(1 for a in syms if next((s for s, lo, hi in secs if lo <= a < hi), None) in EXCLUDED)
        write_readme(rows, excluded)


# README "By part of the game" rows: label -> progress.py modules
PARTS = [
    ("Renderer (scenes, materials, shaders, models, effects)", {"renderer"}),
    ("Game logic (entities, AI, animation, weapons, level-script runtime)", {"game", "xanim"}),
    ("Client game (HUD, view, effects on screen)", {"cgame"}),
    ("Engine core (memory, files, settings, commands, containers, math)", {"qcommon"}),
    ("Client and menus", {"client"}),
    ("Server, collision, compression", {"server", "collision", "zlib"}),
    ("Script engine (script VM, entity bindings, built-in script functions)", {"script"}),
    ("Level-script helpers in the engine code", {"levelscript"}),
    ("Sound, streaming, online", {"sound", "streaming", "online"}),
]


# README mission-scripts table: XBE level section -> mission
MISSIONS = [("PROLOGUE", "sp/prologue"), ("ORAN", "sp/oran"), ("TUNISIA", "sp/tunisia"),
            ("KASSPASS", "sp/kasspass"), ("KASSRINE", "sp/kassrine"), ("LIBEATOR", "sp/liberatr"),
            ("GELA", "sp/gela"), ("PIANLUPO", "sp/piano"), ("TROINA", "sp/troina"),
            ("OMAHEACH", "sp/omaha"), ("MONS1", "sp/mons"), ("EILEDORF", "sp/eilndrf"),
            ("BUCHHOLZ", "sp/buchholz"), ("DRAGEETH", "sp/dragonth")]


def replace_block(s, name, lines):
    a = s.index("\n", s.index(f"<!-- {name}:start")) + 1
    b = s.index(f"<!-- {name}:end -->")
    return s[:a] + "\n".join(lines) + "\n" + s[b:]


def write_readme(rows, excluded):
    import datetime
    n = lambda x: f"{x:,}"
    named_ = lambda rs: sum(1 for r in rs if r[4] not in ("unnamed", "library"))
    done_ = lambda rs: sum(1 for r in rs if r[4] in REWRITTEN)
    text = [r for r in rows if r[2] == ".text"]
    levels = [r for r in rows if r[2] == "levels"]
    nlevels = len({r[3] for r in levels})
    out = [f"*As of {datetime.date.today().isoformat()}. The numbers come from `tools/progress.py`, which tracks every",
           "function of the game.*", "",
           "| | Functions | Named | Rewritten | Left to rewrite |", "|---|---:|---:|---:|---:|"]
    for label, rs in ((f"Engine and game code (`.text`)", text), (f"Mission scripts ({nlevels} level sections)", levels)):
        out.append(f"| {label} | {n(len(rs))} | {n(named_(rs))} | {n(done_(rs))} | {n(len(rs) - done_(rs))} |")
    out.append(f"| **Total** | **{n(len(rows))}** | **{n(named_(rows))}** | **{n(done_(rows))}** | **{n(len(rows) - done_(rows))}** |")
    globals_ = set()
    for f in ("manual_names.csv", "cvar_globals.csv"):
        globals_ |= {int(r["address"], 16) for r in csv.DictReader(open(DATA / f)) if r["kind"] == "data"}
    out += ["", "**Named** means we know what the function does and it has a proper name.",
            "**Rewritten** means it has been turned into C++ and passed the in-game comparison",
            f"(below). We've also named {n(len(globals_))} of the game's global variables.", "",
            "### By part of the game", "",
            "| Part | Functions | Rewritten | Decompiled, waiting for a test | Named, not decompiled yet | Not named yet |",
            "|---|---:|---:|---:|---:|---:|"]
    assigned = set()
    for label, mods in PARTS + [("Not sorted into a part yet", None)]:
        if mods is None:
            rs = [r for r in text if r[3] not in assigned]
        else:
            assigned |= mods
            rs = [r for r in text if r[3] in mods]
        c = collections.Counter(r[4] for r in rs)
        waiting = c["untested"] + c["inconclusive"] + c["mismatched"]
        out.append(f"| {label} | {n(len(rs))} | {n(done_(rs))} | {n(waiting)} | {n(c['named'])} | {n(c['unnamed'])} |")
    lib = sum(1 for r in text if r[3] not in assigned and r[4] == "library")
    out += ["", f"{n(lib)} of the unsorted functions are Microsoft's own library code that was built into",
            "the game (the C runtime, the Xbox system library, the UIX menu library, the voice",
            "codec). We recognise them by comparing their bytes with the Xbox SDK's libraries, so",
            "they have their real names; they still count towards the total."]
    pct = round(100 * named_(rows) / len(rows))
    intro = [f"It's early days: about {pct}% of the functions have a name, and {n(done_(rows))} have been",
             "rewritten in C++. The renderer is the part we're working on now."]
    lv = ["Each mission has its own section of code: its level scripts (the objectives, enemy",
          f"waves, scripted events and dialogue) compiled down to machine code. All {n(len(levels))}",
          "of these functions were generated by the game's script compiler, so rather than",
          "rewriting them one by one, the plan is to build a translator that converts them",
          "automatically.", "",
          "| Section | Mission | Functions | Named |", "|---|---|---:|---:|"]
    for sec, mission in MISSIONS:
        rs = [r for r in levels if r[3] == sec.lower()]
        lv.append(f"| {sec} | `{mission}` | {n(len(rs))} | {n(named_(rs))} |")
    exc = [f"The game also contains about {n(round(excluded, -1))} functions from Microsoft's Xbox libraries:",
           "Direct3D, DirectSound, Xbox Live and networking, and a few others. We're leaving",
           "them out on purpose:"]
    readme = ROOT / "README.md"
    s = readme.read_text()
    for name, lines in (("intro", intro), ("progress", out), ("levels", lv), ("excluded", exc)):
        s = replace_block(s, name, lines)
    readme.write_text(s)
    print("README.md progress blocks updated")


if __name__ == "__main__":
    main()
