#!/usr/bin/env python3
"""Finds the Replaced function that breaks a run, by bisection with XBCMP_ORIGINALS.

Each step runs the loader unattended (--level, A presses as tools/smoke_test.sh) on a copy of
the saves, with half of the suspects kept original, and checks the log for FAIL (a regex,
default: a crash). A failing half keeps being split; a passing one is cleared.

usage: tools/bisect_replaced.py OUT_DIR [--module renderer] [--mission 4] [--seconds 30]
                                [--fail REGEX] [--loader PATH] [-- LOADER OPTIONS...]
Suspects: Replaced functions of the module (tools/data/progress.csv modules), all modules
with --module all. Prints each step and the culprit; logs in OUT_DIR/step_N.txt.
"""
import argparse
import csv
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

PORT = Path(__file__).resolve().parent.parent


def replaced_functions(module):
    modules = {r["name"]: r["module"] for r in csv.DictReader(open(PORT / "tools/data/progress.csv"))}
    names = re.findall(r'\{"(\w+)", 0x[0-9a-f]{8},.*Status::Replaced\}',
                       (PORT / "game/code/functions.cpp").read_text())
    return [n for n in names if module == "all" or modules.get(n) == module]


def run(out, step, originals, args, extra):
    data = out / "data"
    shutil.rmtree(data, ignore_errors=True)
    saves = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share")) / "cod_bro"
    shutil.copytree(saves, data, symlinks=True)
    press = ",".join(["a@10"] + [f"a@{t}" for t in range(14, 111, 4)])
    env = dict(os.environ, XBCMP_ORIGINALS=",".join(originals) if originals else "")
    if not originals:
        env.pop("XBCMP_ORIGINALS")
    log = out / f"step_{step}.txt"
    with open(log, "w") as f:
        subprocess.run(["timeout", "-s", "INT", "-k", "10", str(args.seconds), args.loader, "--data", str(data),
                        "--silent", "--level", str(args.mission), "--press", press, *extra],
                       stdout=f, stderr=subprocess.STDOUT, env=env)
    shutil.rmtree(data, ignore_errors=True)
    return re.search(args.fail, log.read_text(errors="replace")) is not None


def main():
    argv = sys.argv[1:]
    extra = []
    if "--" in argv:
        extra = argv[argv.index("--") + 1:]
        argv = argv[:argv.index("--")]
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("out")
    p.add_argument("--module", default="renderer")
    p.add_argument("--mission", type=int, default=4)
    p.add_argument("--seconds", type=int, default=30)
    p.add_argument("--fail", default=r"error crash")
    p.add_argument("--loader", default=str(PORT / "build/linux-loader/bin/cod_bro_loader"))
    args = p.parse_args(argv)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    suspects = replaced_functions(args.module)
    print(f"{len(suspects)} suspects")
    step = 0
    if not run(out, step, [], args, extra):
        print("no failure with every function replaced: nothing to bisect")
        return 1
    while len(suspects) > 1:
        step += 1
        half, rest = suspects[:len(suspects) // 2], suspects[len(suspects) // 2:]
        # Only `half` runs as C (with the cleared ones); the rest of the suspects keep the original.
        failed = run(out, step, rest, args, extra)
        print(f"step {step}: {len(half)} replaced, {len(rest)} original -> {'FAIL' if failed else 'pass'}", flush=True)
        suspects = half if failed else rest
    print(f"culprit: {suspects[0]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
