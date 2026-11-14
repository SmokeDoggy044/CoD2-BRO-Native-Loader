#!/usr/bin/env python3
"""The lab: fixed capture points of the reference recordings, pictured under several setups and compared.

usage:
  tools/lab.py run OUT [--mission m0 ...] [--variant NAME="OPTIONS" ...] [--loader PATH] [--speed N] [--jobs N]
  tools/lab.py compare OUT [--baseline NAME]
  tools/lab.py crop OUT VARIANT MISSION FRAME X Y W H [--scale N]

run: every mission's recording (recordings/<m>/*.rep, the first) is replayed once per variant with
  --replay-no-draw --replay-shots (only the frames just before each picture are drawn), so a 8-minute mission takes a
  few minutes per variant, variants in parallel. Pictures land in OUT/<variant>/<mission>/shot_<frame>.bmp, logs next
  to them. Then compare runs.
  Capture points: tools/data/capture_points.csv (mission,frame,label); a mission without rows gets 8 frames spread
  over its recording. Variants: NAME="extra loader options"; default: original="--graphics original" and
  enhanced="--graphics enhanced". The first variant is the baseline.
compare: per capture point and variant against the baseline: share of pixels that differ (largest channel > 8),
  PSNR, sharpness (mean absolute Laplacian, variant / baseline), mean brightness change. Writes OUT/report.html
  (side by side + amplified difference, for the user), OUT/metrics.csv, and prints a short table.
crop: a cut-out of one picture as PNG (OUT/crops/...), to look at a detail without opening the whole frame.

Pictures are judged by their numbers first: opening an image sends it to the model's API.
"""
import argparse
import csv
import html
import os
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

import numpy as np
from PIL import Image

PORT = Path(__file__).resolve().parent.parent
POINTS = PORT / "tools/data/capture_points.csv"
DEFAULT_LOADER = PORT / "build/linux-loader-release/bin/cod_bro_loader"
DEFAULT_VARIANTS = ['original=--graphics original', 'enhanced=--graphics enhanced']


def recording_for(mission):
    reps = sorted((PORT / "recordings" / mission).glob("*.rep"))
    if not reps:
        sys.exit(f"no recording in recordings/{mission}")
    return reps[0]


def recording_info(rep):
    out = subprocess.run([sys.executable, "-I", str(PORT / "tools/replay_info.py"), str(rep)],
                         capture_output=True, text=True, check=True).stdout.split()
    return int(out[0]), out[1]


def capture_points(mission, frames):
    rows = []
    if POINTS.exists():
        with open(POINTS, newline="") as f:
            rows = [(int(r["frame"]), r["label"]) for r in csv.DictReader(f) if r["mission"] == mission]
    if not rows:
        start = min(600, frames // 4)
        rows = [(start + (frames - 60 - start) * i // 7, f"auto {i + 1}") for i in range(8)]
    return sorted(rows)


def parse_variants(items):
    variants = []
    for item in items or DEFAULT_VARIANTS:
        name, _, options = item.partition("=")
        variants.append((name, options.split()))
    return variants


def run_one(args, out, variant, mission, rep, frames, recorded_mission, points):
    name, options = variant
    folder = out / name / mission
    shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True)
    saves = rep.with_suffix(".saves")
    if not saves.is_dir():
        saves = rep.parent / "saves"
    data = folder / "data"
    shutil.copytree(saves, data, symlinks=True)
    level = [] if recorded_mission == "-" else ["--level", recorded_mission]
    command = [str(args.loader), "--data", str(data), "--silent", *level, "--replay", str(rep),
               "--replay-no-draw", "--replay-speed", str(args.speed),
               "--replay-shots", ",".join(str(f) for f, _ in points), str(folder), *options]
    seconds = int(frames / 60 / min(args.speed, 2) + 60)
    with open(folder / "log.txt", "w") as log:
        log.write(" ".join(command) + "\n")
        log.flush()
        try:
            code = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=seconds).returncode
        except subprocess.TimeoutExpired:
            code = "timeout"
    shutil.rmtree(data, ignore_errors=True)
    shots = len(list(folder.glob("shot_*.bmp")))
    return f"{name:12} {mission:4} exit {code}, {shots}/{len(points)} pictures"


def cmd_run(args):
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    variants = parse_variants(args.variant)
    jobs = []
    for mission in args.mission:
        rep = recording_for(mission)
        frames, recorded_mission = recording_info(rep)
        points = capture_points(mission, frames)
        jobs += [(v, mission, rep, frames, recorded_mission, points) for v in variants]
    with open(out / "variants.txt", "w") as f:
        f.writelines(f"{name}={' '.join(options)}\n" for name, options in variants)
    print(f"{len(jobs)} runs, {args.jobs} at a time")
    with ThreadPoolExecutor(args.jobs) as pool:
        for line in pool.map(lambda j: run_one(args, out, *j), jobs):
            print(line)
    args.baseline = variants[0][0]
    cmd_compare(args)


def load(path):
    return np.asarray(Image.open(path).convert("RGB"), dtype=np.float32)


def luminance(a):
    return a @ np.array([0.2126, 0.7152, 0.0722], dtype=np.float32)


def sharpness(a):
    y = luminance(a)
    lap = 4 * y[1:-1, 1:-1] - y[:-2, 1:-1] - y[2:, 1:-1] - y[1:-1, :-2] - y[1:-1, 2:]
    return float(np.abs(lap).mean())


def metrics(a, b):
    if a.shape != b.shape:
        return None
    d = np.abs(a - b)
    mse = float((d ** 2).mean())
    sa = sharpness(a)
    return {"changed": float((d.max(axis=2) > 8).mean() * 100),
            "psnr": 99.0 if mse == 0 else float(10 * np.log10(255 ** 2 / mse)),
            "sharpness": sharpness(b) / sa if sa else 1.0,
            "brightness": float(luminance(b).mean() - luminance(a).mean())}


def side_by_side(a, b, path):
    diff = np.clip(np.abs(a - b).max(axis=2) * 4, 0, 255)
    heat = np.stack([diff, diff * 0.3, np.zeros_like(diff)], axis=2)
    Image.fromarray(np.concatenate([a, b, heat], axis=1).astype(np.uint8)).save(path, optimize=True)


def cmd_compare(args):
    out = Path(args.out).resolve()
    variants = [line.split("=", 1)[0] for line in (out / "variants.txt").read_text().splitlines()]
    base = args.baseline or variants[0]
    rows = []
    for mission_dir in sorted((out / base).iterdir()):
        if not mission_dir.is_dir():
            continue
        mission = mission_dir.name
        labels = dict((str(f), l) for f, l in capture_points(mission, 10 ** 9)) if POINTS.exists() else {}
        for shot in sorted(mission_dir.glob("shot_*.bmp"), key=lambda p: int(p.stem[5:])):
            a = load(shot)
            frame = shot.stem[5:]
            for v in variants:
                if v == base:
                    continue
                other = out / v / mission / shot.name
                if not other.exists():
                    rows.append({"mission": mission, "frame": frame, "variant": v, "missing": True})
                    continue
                b = load(other)
                m = metrics(a, b)
                pair = out / "pairs" / f"{mission}_{frame}_{base}_vs_{v}.png"
                pair.parent.mkdir(exist_ok=True)
                if m is not None:
                    side_by_side(a, b, pair)
                rows.append({"mission": mission, "frame": frame, "variant": v, "label": labels.get(frame, ""),
                             "pair": pair.relative_to(out).as_posix(), **(m or {"size_differs": True})})
    keys = ["mission", "frame", "label", "variant", "changed", "psnr", "sharpness", "brightness"]
    with open(out / "metrics.csv", "w", newline="") as f:
        w = csv.DictWriter(f, keys, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)
    print(f"baseline {base}")
    print(f"{'mission':7} {'frame':>6} {'variant':12} {'changed%':>8} {'psnr':>6} {'sharp':>6} {'bright':>7}")
    for r in rows:
        if "changed" in r:
            print(f"{r['mission']:7} {r['frame']:>6} {r['variant']:12} {r['changed']:8.2f} {r['psnr']:6.1f} "
                  f"{r['sharpness']:6.2f} {r['brightness']:+7.2f}")
        else:
            print(f"{r['mission']:7} {r['frame']:>6} {r['variant']:12} {'missing' if r.get('missing') else 'size differs'}")
    write_report(out, base, rows)


def write_report(out, base, rows):
    parts = [f"<!doctype html><meta charset=utf-8><title>Lab report</title><style>body{{font:14px sans-serif;"
             f"background:#151515;color:#ddd;margin:16px}}img{{max-width:100%}}td,th{{padding:2px 8px}}</style>"
             f"<h1>Lab report</h1><p>Left: {html.escape(base)} · middle: variant · right: difference x4</p>"]
    for r in rows:
        head = f"{r['mission']} frame {r['frame']} {html.escape(r.get('label', ''))} - {html.escape(r['variant'])}"
        if "changed" in r:
            head += (f": {r['changed']:.2f}% changed, PSNR {r['psnr']:.1f}, sharpness x{r['sharpness']:.2f}, "
                     f"brightness {r['brightness']:+.2f}")
            parts.append(f"<h3>{head}</h3><img loading=lazy src='{r['pair']}'>")
        else:
            parts.append(f"<h3>{head}: {'missing' if r.get('missing') else 'size differs'}</h3>")
    (out / "report.html").write_text("\n".join(parts))
    print(f"report: {out / 'report.html'}")


def cmd_crop(args):
    out = Path(args.out).resolve()
    shot = out / args.variant / args.mission / f"shot_{args.frame}.bmp"
    img = Image.open(shot).crop((args.x, args.y, args.x + args.w, args.y + args.h))
    if args.scale != 1:
        img = img.resize((args.w * args.scale, args.h * args.scale), Image.NEAREST)
    path = out / "crops" / f"{args.mission}_{args.frame}_{args.variant}_{args.x}_{args.y}_{args.w}x{args.h}.png"
    path.parent.mkdir(exist_ok=True)
    img.save(path)
    print(path)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("out")
    r.add_argument("--mission", nargs="+", default=["m0"])
    r.add_argument("--variant", action="append")
    r.add_argument("--loader", type=Path, default=DEFAULT_LOADER)
    r.add_argument("--speed", type=float, default=3)
    r.add_argument("--jobs", type=int, default=max(1, min(4, (os.cpu_count() or 4) // 4)))
    c = sub.add_parser("compare")
    c.add_argument("out")
    c.add_argument("--baseline")
    k = sub.add_parser("crop")
    k.add_argument("out")
    k.add_argument("variant")
    k.add_argument("mission")
    k.add_argument("frame", type=int)
    for name in ("x", "y", "w", "h"):
        k.add_argument(name, type=int)
    k.add_argument("--scale", type=int, default=1)
    args = p.parse_args()
    {"run": cmd_run, "compare": cmd_compare, "crop": cmd_crop}[args.cmd](args)


if __name__ == "__main__":
    main()
