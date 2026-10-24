#!/usr/bin/env python3
"""Runs --compare with console commands (loader --exec) to reach functions that only debug
commands and cvars call: one compare_batch.py run per command group, each over the
functions of MODULE that are still untested (--only --every 1), and a report of which
group reached which function.

Groups: the game's debug command table at 0x008ddae0 (nglstats, wireframe, fullbright,
toggleshader <cdname>, texturemip, ...; forwarded to the game like give/god), the r_
cvars the renderer actually reads (R_Register registers more, but r_fullbright,
r_LightScale, r_showtris and others are never read on the Xbox), and the debug-draw cvars.

usage: tools/exec_sweep.py [--module renderer] [--missions 4] [--seconds 110] [--groups a,b]
                           [--out DIR] [--list]
Result: DIR/<group>/ (compare_batch.py run folders) and DIR/report.txt.
"""
import argparse
import csv
import datetime
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PROGRESS = ROOT / "tools" / "data" / "progress.csv"
FUNCTIONS = ROOT / "tools" / "data" / "functions.csv"

SHADERS = ("cdairplanemetal cdbackground cdchar cddebug cddecal cdglass cdgun cdgunsight cdocean cdprelit "
           "cdpropeller cdriver cdsimple cdsimplealpha cdsimpleinstance cdsimpleprelit cdsimpleuvanim cdsky "
           "cdwater cdworld cdworldblend cdworldvertexlit").split()

# Commands in a group run 30 frames apart: the harness tests in 4 slots, and a call that
# finds them all busy is not tested (22 toggles in one frame: 2 tested).
SPACED = "; wait 30; "

GROUPS = {
    "shaders": SPACED.join(f"toggleshader {name}" for name in SHADERS),
    "textures": SPACED.join(("texturemip 1", "texturesize", "texturetiling", "fogswitch")),
    "stats": SPACED.join(("nglstats", "nglstatdisplay", "nglfpsdisplay", "profilenodes", "profileshaders",
                          "entity_stats", "memreport", "mempools")),
    "wireframe": "wireframe",
    "fullbright": "fullbright",
    "solidcolor": "solidcolor",
    "rcvars": ("r_nocull 1; r_lockpvs 1; r_singlecell 1; r_portalbevels 0; r_dynamiclight 0; r_testshadow 1; "
               "r_showSkeletons 1; r_showtricounts 1; r_cullXModels 0; r_outsideMapEnts 1; r_lodscale 2; r_lodbias 1"),
    "noentities": "r_drawentities 0",
    "norefresh": "r_norefresh 1",
    "scenefx": "SceneFX 1; RenderEnts 1",
    "debugdraw": ("fx_debug 1; fx_debugBolt 1; bg_stickyAimRender 1; debug_drones 1; debug_vehicles 1; "
                  "debug_squad_radius 1; debug_squad_overall_radius 1; debug_axis 1; debug_attrib 1; debug_drawcone 1; "
                  "debug_mortars 1; debug_mg_targets 1; debug_tankall 1; debug_stundraw 1"),
    # Shutdown and load paths: the renderer restarted, the level restarted, the server shut down - after
    # ~80 s (--exec runs while the mission is still loading).
    "vidrestart": "wait 2400; vid_restart",
    "maprestart": "wait 2400; map_restart",
    "disconnect": "wait 2400; disconnect",
}


def untested(module):
    subprocess.run([sys.executable, str(ROOT / "tools" / "progress.py")], stdout=subprocess.DEVNULL, check=True)
    with open(FUNCTIONS, newline="") as f:
        decompiled = {r["name"] for r in csv.DictReader(f)}  # kFunctions entries (the harness can test them)
    with open(PROGRESS, newline="") as f:
        return [r["name"] for r in csv.DictReader(f)
                if r["module"] == module and r["tested"] == "0" and r["name"] in decompiled]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--module", default="renderer")
    parser.add_argument("--missions", default="4")
    parser.add_argument("--seconds", type=int, default=110)
    parser.add_argument("--groups", default=",".join(GROUPS))
    parser.add_argument("--out", type=Path)
    parser.add_argument("--list", action="store_true")
    args = parser.parse_args()
    if args.list:
        for name, text in GROUPS.items():
            print(f"{name:12} {text}")
        return
    out = args.out or ROOT / "build" / "exec_sweep" / datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out.mkdir(parents=True, exist_ok=True)
    report = []
    before = set(untested(args.module))
    first = True
    for group in args.groups.split(","):
        names = untested(args.module)
        print(f"== {group}: {GROUPS[group]} ({len(names)} untested)", flush=True)
        command = [sys.executable, str(ROOT / "tools" / "compare_batch.py"), *names, "--only", "--every", "1",
                   "--missions", args.missions, "--seconds", str(args.seconds), "--exec", GROUPS[group],
                   "--out", str(out / group)]
        if not first:
            command.append("--no-build")
        first = False
        subprocess.run(command, check=False)
        reached = sorted(set(names) - set(untested(args.module)))
        report.append(f"{group:12} {len(reached):3} reached: {' '.join(reached) or '-'}")
        print(report[-1], flush=True)
    still = sorted(set(untested(args.module)) & before)
    report.append(f"\nstill untested ({len(still)}): {' '.join(still)}")
    (out / "report.txt").write_text("\n".join(report) + "\n")
    print(f"\nreport: {out / 'report.txt'}")


if __name__ == "__main__":
    main()
