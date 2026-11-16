#!/usr/bin/env python3
"""Unattended menu walks (engine improvements phase 4 tools).

  tools/menu_walk.py OUT [WALK] [--level N] [--loader PATH] [-- extra loader options]

WALK: a script in tools/data/menu_walks/ (name without .walk), a path, or inline steps (see
src/runtime/loader/menu_walk.h); default front_options. Runs on a copy of the save folder (no mods), writes
OUT/<shot>.bmp + .txt, OUT/walk.txt (every state), OUT/sheet.png (all pictures) and OUT/stdout.txt. Exit code: the
loader's (0 = walked to the end, 4 = a wait timed out: OUT/timeout.txt has the state).
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WALKS = ROOT / "tools" / "data" / "menu_walks"


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("out")
    p.add_argument("walk", nargs="?", default="front_options")
    p.add_argument("--level", type=int)
    p.add_argument("--loader", default=str(ROOT / "build" / "linux-loader-release" / "bin" / "cod_bro_loader"))
    p.add_argument("--seconds", type=int, default=240)
    p.add_argument("extra", nargs="*")
    a = p.parse_args()
    out = Path(a.out).resolve()
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)
    if (WALKS / f"{a.walk}.walk").exists():
        script = "@" + str(WALKS / f"{a.walk}.walk")
    elif Path(a.walk).exists():
        script = "@" + str(Path(a.walk).resolve())
    else:
        script = a.walk
    saves = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share")) / "cod_bro"
    data = out / "data"
    data.mkdir()
    for name in ("E", "Z", "settings.ini", "cache"):  # not mods/ (large; walks run without packs)
        src = saves / name
        if src.is_dir():
            shutil.copytree(src, data / name, symlinks=True)
        elif src.exists():
            shutil.copy2(src, data / name)
    cmd = [a.loader, "--data", str(data), "--silent", "--menu-walk", script, str(out)]
    if a.level is not None:
        cmd += ["--level", str(a.level)]
    cmd += a.extra
    with open(out / "stdout.txt", "w") as log:
        try:
            code = subprocess.run(cmd, stdout=log, stderr=subprocess.STDOUT, timeout=a.seconds).returncode
        except subprocess.TimeoutExpired:
            code = 124
    shutil.rmtree(data, ignore_errors=True)
    shots = sorted(out.glob("*.bmp"), key=lambda f: f.stat().st_mtime)
    if shots:
        subprocess.run(["magick", "montage", *map(str, shots), "-tile", "3x", "-geometry", "640x480+2+2",
                        "-title", "", "-label", "%t", str(out / "sheet.png")], check=False)
    print(f"{out}: exit {code}, {len(shots)} picture(s)")
    return code


if __name__ == "__main__":
    sys.exit(main())
