#!/usr/bin/env python3
"""Runs a --compare batch and prints one line per function: what this batch's runs added
to functions.csv, then the first issue per function from the logs.

Builds the loader, starts one unattended run per mission a few seconds apart (three
DXVK startups at once once crashed one), waits for all of them, and compares
functions.csv with a copy taken before. --compare implies --pc-patches off; nothing
else is added (no settings.ini, no presentation options). Each run gets an empty data
folder, so the user's saves stay untouched.

usage: tools/compare_batch.py [FUNCTION ...] [options]
  FUNCTION       names or hex addresses to report (default: the kFunctions entries
                 added or changed in game/code/functions.cpp since HEAD; with none,
                 every function whose counts changed)
  --missions L   comma-separated missions (default 1,4,5,6)
  --seconds S    length of each run (default 200: mission 1's intro ends at ~95 s)
  --all          --compare-all (also tests settled functions: a module's final check)
  --only         XBCMP_ONLY=the reported functions (keeps testing past 100 matches)
  --every N      XBCMP_EVERY=N (test every N-th call instead of every 64th)
  --press LIST   scripted presses for every run (default A every 4 s from 10 s to 86 s:
                 past the movie and "Press A to start" into the level, checked for
                 missions 1/4/5/6 on 2026-10-19; 'none' for no presses)
  --exec CMDS    console commands/cvars for every run (loader --exec), e.g. "r_drawentities 0"
  --force SPEC   a call the title does not make, tested in every run (loader --force,
                 "NAME(ARGS)@SECONDS*COUNT"; repeatable): shutdown/free paths
  --screenshots N  save every N-th frame as DIR/m<N>/frame_<n>.bmp
  --preset P     build preset (default linux-loader)
  --out DIR      run folders (default build/compare/<date-time>)
  --no-build     skip the build
  --report DIR   only summarize the runs already in DIR (needs DIR/functions_before.csv)

Result: DIR/m<N>/stdout.txt per mission, DIR/functions_before.csv, DIR/summary.txt
(what is printed).
"""
import argparse
import csv
import datetime
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CSV = ROOT / "tools" / "data" / "functions.csv"
FUNCTIONS_CPP = ROOT / "game" / "code" / "functions.cpp"
COUNTS = ("tested", "matched", "drift", "mismatched", "inconclusive")
ENOUGH = 100  # Config::enough_matches

ENTRY = re.compile(r'\{"(\w+)", 0x([0-9a-fA-F]+),')
VERDICT = re.compile(r"\] \w+ +compare +(\S+) call (\d+): (float drift|MISMATCH|inconclusive): (.*)")
UNMOVABLE = re.compile(r"\] \w+ +compare +(\S+): cannot move the instruction at ([0-9a-f]+)")
TESTS = re.compile(r"\] \w+ +compare +(\d+) tests, ")
TROUBLE = re.compile(r"\] (error|fatal)|crash|unimplemented|exception|Segmentation", re.I)


def read_csv(path):
    if not path.exists():
        return {}
    with open(path, newline="") as f:
        return {r["address"].lower(): r for r in csv.DictReader(f)}


def registered():
    """name -> address of every kFunctions entry."""
    return {m.group(1): m.group(2).lower().zfill(8) for m in ENTRY.finditer(FUNCTIONS_CPP.read_text())}


def changed_entries():
    diff = subprocess.run(["git", "-C", str(ROOT), "diff", "-U0", "HEAD", "--", str(FUNCTIONS_CPP)],
                          capture_output=True, text=True).stdout
    return [m.group(1) for line in diff.splitlines() if line.startswith("+")
            for m in [ENTRY.search(line)] if m]


def resolve(items, names):
    by_address = {a: n for n, a in names.items()}
    out = []
    for item in items:
        if item in names:
            out.append((item, names[item]))
            continue
        try:
            address = f"{int(item, 16):08x}"
        except ValueError:
            sys.exit(f"{item}: not in kFunctions (game/code/functions.cpp)")
        out.append((by_address.get(address, item), address))
    return out


def build(preset):
    run = subprocess.run(["cmake", "--build", "--preset", preset], cwd=ROOT, capture_output=True, text=True)
    if run.returncode == 0:
        return True
    lines = (run.stdout + run.stderr).splitlines()
    errors = [i for i, line in enumerate(lines) if re.search(r"error|undefined reference", line)]
    print(f"build failed ({preset}):")
    shown = set()
    for i in errors[:4]:
        for j in range(max(0, i - 2), min(len(lines), i + 4)):
            if j not in shown:
                shown.add(j)
                print("  " + lines[j][:200])
    if not errors:
        print("\n".join("  " + line[:200] for line in lines[-15:]))
    return False


def start_runs(args, out, reported):
    loader = ROOT / "build" / args.preset / "bin" / "cod_bro_loader"
    if not loader.exists():
        sys.exit(f"no loader at {loader}")
    env = dict(os.environ)
    if args.only:
        env["XBCMP_ONLY"] = ",".join(name for name, _ in reported)
    if args.every:
        env["XBCMP_EVERY"] = str(args.every)
    runs = []
    for i, mission in enumerate(args.missions):
        folder = out / f"m{mission}"
        shutil.rmtree(folder, ignore_errors=True)
        (folder / "data").mkdir(parents=True)
        command = ["timeout", "-s", "INT", "-k", "15", str(args.seconds), str(loader), "--data", str(folder / "data"),
                   "--silent", "--level", str(mission), "--compare-all" if args.all else "--compare"]
        if args.press != "none":
            command += ["--press", args.press]
        if args.exec:
            command += ["--exec", args.exec]
        for forced in args.force:
            command += ["--force", forced]
        if args.screenshots:
            command += ["--screenshots", str(args.screenshots), str(folder)]
        if i:
            time.sleep(args.stagger)
        log = open(folder / "stdout.txt", "w")
        runs.append((mission, subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, env=env), log))
    print(f"started missions {','.join(map(str, args.missions))} ({args.seconds} s each) in {out}", flush=True)
    for mission, process, log in runs:
        process.wait()
        log.close()
        (out / f"m{mission}" / "exit").write_text(f"{process.returncode}\n")
        shutil.rmtree(out / f"m{mission}" / "data", ignore_errors=True)


def summarize(out, reported, names):
    lines = []
    before, after = read_csv(out / "functions_before.csv"), read_csv(CSV)
    by_address = {a: n for n, a in names.items()}

    def delta(address, key):
        new, old = after.get(address), before.get(address)
        return int(new[key]) - (int(old[key]) if old else 0) if new else 0

    if not reported:  # every function whose counts changed
        reported = [(by_address.get(a, r["name"]), a) for a, r in after.items()
                    if any(delta(a, k) for k in COUNTS)]
        reported.sort(key=lambda item: item[1])

    # Runs: how each ended, troubles, and the first issues per function.
    issues, unmovable, run_notes = {}, {}, []
    for folder in sorted(out.glob("m*"), key=lambda p: int(p.name[1:]) if p.name[1:].isdigit() else 0):
        log = folder / "stdout.txt"
        if not log.exists():
            continue
        exit_file = folder / "exit"
        status = exit_file.read_text().strip() if exit_file.exists() else "?"
        tests, trouble = 0, []
        for line in log.read_text(errors="replace").splitlines():
            if m := VERDICT.search(line):
                issues.setdefault(m.group(1), {}).setdefault(m.group(3), f"[{folder.name}] {m.group(4)}")
            elif m := UNMOVABLE.search(line):
                unmovable[m.group(1)] = m.group(2)
            elif m := TESTS.search(line):
                tests = int(m.group(1))
            elif TROUBLE.search(line) and " compare " not in line:
                trouble.append(line.strip())
        ended = {"124": "timeout", "0": "exited"}.get(status, f"EXIT {status}")
        note = f"{folder.name} {ended}, {tests} tests"
        if trouble:
            note += f", {len(trouble)} error lines (first: {trouble[0][:140]})"
        run_notes.append(note)
    lines.append("runs: " + "; ".join(run_notes))

    width = max([len(name) for name, _ in reported] + [8])
    lines.append(f"{'function':{width}} address   +test +match +mism +drift +inconc | match/mism/drift  state")
    quiet = []
    for name, address in reported:
        row = after.get(address)
        d = {k: delta(address, k) for k in COUNTS}
        if not any(d.values()):
            quiet.append((name, address, row))
            continue
        total = (int(row["matched"]), int(row["mismatched"]), int(row["drift"]))
        state = "MISMATCHED" if total[1] else "drift" if total[2] else \
            "settled" if total[0] >= ENOUGH else "matching" if total[0] else "inconclusive only"
        lines.append(f"{name:{width}} {address}  {d['tested']:5} {d['matched']:5} {d['mismatched']:5} "
                     f"{d['drift']:5} {d['inconclusive']:7} | {'/'.join(map(str, total)):>16}  {state}")
    for name, address, row in quiet:
        if name in unmovable:
            why = f"cannot hook (instruction at {unmovable[name]})"
        elif row and int(row["matched"]) >= ENOUGH and not int(row["mismatched"]) + int(row["drift"]) and not ARGS.all:
            why = f"settled before ({row['matched']} matched)"
        elif name not in names:
            why = "not in kFunctions"
        else:
            why = "not reached"
        lines.append(f"{name:{width}} {address}  -- {why}")

    shown = [(name, issues[name]) for name, _ in reported if name in issues]
    if shown:
        lines.append("first issue per function and kind (full lines in the run logs):")
        for name, kinds in shown:
            for kind in ("MISMATCH", "float drift", "inconclusive"):
                if kind in kinds:
                    lines.append(f"  {name} {kind}: {kinds[kind][:220]}")
    # --readme keeps the README's progress blocks current with every batch (merge conflicts in them:
    # take either side and rerun tools/progress.py --readme).
    progress = subprocess.run([sys.executable, str(ROOT / "tools" / "progress.py"), "--readme"], capture_output=True,
                              text=True)
    counter = [line for line in progress.stdout.splitlines() if line.startswith("counter:")]
    lines += counter
    text = "\n".join(lines)
    (out / "summary.txt").write_text(text + "\n")
    print(text)


def main():
    global ARGS
    parser = argparse.ArgumentParser(usage=__doc__)
    parser.add_argument("functions", nargs="*")
    parser.add_argument("--missions", default="1,4,5,6")
    parser.add_argument("--seconds", type=int, default=200)
    parser.add_argument("--stagger", type=float, default=5)
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--only", action="store_true")
    parser.add_argument("--every", type=int)
    parser.add_argument("--press", default=",".join(f"a@{t}" for t in range(10, 90, 4)))
    parser.add_argument("--exec")
    parser.add_argument("--force", action="append", default=[])
    parser.add_argument("--screenshots", type=int)
    parser.add_argument("--preset", default="linux-loader")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--report", type=Path)
    ARGS = args = parser.parse_args()
    args.missions = [int(m) for m in args.missions.split(",")]

    names = registered()
    reported = resolve(args.functions or changed_entries(), names)
    if args.report:
        summarize(args.report, reported, names)
        return
    if args.only and not reported:
        sys.exit("--only needs functions (none given, none changed since HEAD)")
    if not args.no_build and not build(args.preset):
        sys.exit(1)
    out = args.out or ROOT / "build" / "compare" / datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out.mkdir(parents=True, exist_ok=True)
    shutil.copy(CSV, out / "functions_before.csv")
    if reported:
        print(f"reporting {len(reported)}: {' '.join(name for name, _ in reported)}", flush=True)
    start_runs(args, out, reported)
    summarize(out, reported, names)


if __name__ == "__main__":
    main()
