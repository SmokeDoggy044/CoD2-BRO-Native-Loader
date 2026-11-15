#!/usr/bin/env python3
"""The lab: fixed capture points of the reference recordings, pictured under several setups and compared.

usage:
  tools/lab.py run OUT [--mission m0 ...] [--variant NAME="OPTIONS" ...] [--loader PATH] [--speed N] [--jobs N]
  tools/lab.py compare OUT [--baseline NAME]
  tools/lab.py crop OUT VARIANT MISSION FRAME X Y W H [--scale N]
  tools/lab.py viewer OUT --model M [M ...] [--mission m0] [--at FRAME] [--steps 8] [--variant ...] [--loader PATH]
  tools/lab.py orbit OUT [--mission m2] [--recording PATH] [--at FRAME] [--sets turn orbit micro] [--variant ...]
  tools/lab.py reference [NAME]
  tools/lab.py neutral OUT [--reference NAME] [--mission m0 m2 m4] [--options "..."] [--loader PATH]

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
viewer: the object viewer's turntable (phase 1.0d): the mission's recording cut after frame --at (default 6000) and
  replayed (--replay-continue: live and without input from there), at --at + 10 F8 shows the model, the game pauses
  30 frames later and the model holds --steps angles (360/steps apart) 60 frames each, a picture at the end of each.
  Pictures in OUT/<variant>/<model>/shot_<frame>.bmp (labels.csv names the angles), then compare as for run, but
  only inside the model's box (what moves between the baseline's angles).
  Variants as for run; e.g. a="--mods off" b="--mods-dir DIR".
orbit (phase 2 tools): the recording cut after --at, replayed live, paused, and the camera taken through poses
  (--orbit, src/runtime/loader/orbit.cpp; rendering only): turn = 8 yaws in place, orbit = 8 yaws around the
  view at 150 and 400 units, 20 degrees down, micro = 8 steps of 0.25 units sideways (shimmer). Default variants
  off="--graphics enhanced --sun-shadows off --original-shadows off" (the baseline) and on="--graphics
  enhanced". Per pose against the baseline: shadow area (pixels darker by > 4 levels), speckle (shadow-mask
  pixels that disagree with their 3x3 majority, per mask pixel: acne/noise is high, clean edges low), dips
  (a pose whose area falls under half of both neighbours'; turn/orbit), shimmer (micro: mask pixels flipping
  between neighbouring steps, minus the baseline's own changes). OUT/orbit.csv, OUT/orbit.html, a table.
reference: copies the current build (DEFAULT_LOADER's folder, no tests) to build/lab_reference/NAME (default:
  the git commit), its RPATH pointed at itself, so it stays runnable after rebuilding: take one before a step.
neutral: the same options (default "--graphics enhanced") with the reference and the current build over the
  missions' capture points: every picture must be identical (max difference 0); prints PASS or FAIL per
  picture. Use it with the new feature switched off (and with neutral inputs such as flat normal maps).
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


def model_box(shots):
    """Viewer runs: where the model is - the pixels that change between the baseline's angles (the rest stands
    still while the game is paused), as x0, y0, x1, y1 with a margin; None if nothing moved."""
    if len(shots) < 2:
        return None
    first = load(shots[0])
    moved = np.zeros(first.shape[:2], bool)
    for other in shots[1:]:
        moved |= np.abs(load(other) - first).max(axis=2) > 8
    ys, xs = np.nonzero(moved)
    if not len(xs):
        return None
    m = 8
    h, w = moved.shape
    return max(0, xs.min() - m), max(0, ys.min() - m), min(w, xs.max() + 1 + m), min(h, ys.max() + 1 + m)


def cut_recording(rep, frames, path):
    """A copy of the recording that ends after `frames` frames ('F' records; see tools/replay_info.py)."""
    import struct
    data = rep.read_bytes()
    i, seen = 12, 0
    while i + 3 <= len(data):
        tag, length = data[i], struct.unpack_from("<H", data, i + 1)[0]
        if tag == ord("F"):
            seen += 1
            if seen > frames:
                break
        i += 3 + length
    path.write_bytes(data[:i])


VIEWER_STEP_FRAMES = 60
VIEWER_PAUSE_AFTER = 30  # src/runtime/loader/viewer.cpp kPauseAfter


def viewer_one(args, out, variant, model, rep, recorded_mission, cut):
    name, options = variant
    folder = out / name / model
    shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True)
    saves = rep.with_suffix(".saves")
    if not saves.is_dir():
        saves = rep.parent / "saves"
    data = folder / "data"
    shutil.copytree(saves, data, symlinks=True)
    press = args.at + 10
    shots = [press + VIEWER_PAUSE_AFTER + VIEWER_STEP_FRAMES * k + VIEWER_STEP_FRAMES - 1 for k in range(args.steps)]
    with open(folder / "labels.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["frame", "label"])
        w.writerows((frame, f"{model} {360 * k // args.steps} deg") for k, frame in enumerate(shots))
    level = [] if recorded_mission == "-" else ["--level", recorded_mission]
    command = [str(args.loader), "--data", str(data), "--silent", *level, "--replay", str(cut), "--replay-continue",
               "--replay-no-draw", "--replay-speed", str(args.speed),
               "--replay-shots", ",".join(map(str, shots)), str(folder), "--viewer", model, *options]
    env = dict(os.environ, COD_BRO_DEV_KEYS=f"8@{press}",
               COD_BRO_VIEWER_TURNTABLE=f"{args.steps},{VIEWER_STEP_FRAMES}")
    seconds = int(args.at / 60 / min(args.speed, 2) + 120)
    with open(folder / "log.txt", "w") as log:
        log.write(" ".join(command) + "\n")
        log.flush()
        try:
            code = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=seconds, env=env).returncode
        except subprocess.TimeoutExpired:
            code = "timeout"
    shutil.rmtree(data, ignore_errors=True)
    shown = "shown" if "viewer turntable done" in (folder / "log.txt").read_text(errors="replace") else "NOT shown"
    pictures = len(list(folder.glob("shot_*.bmp")))
    return f"{name:12} {model:24} exit {code}, {shown}, {pictures}/{len(shots)} pictures"


def cmd_viewer(args):
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    variants = parse_variants(args.variant)
    rep = recording_for(args.mission)
    frames, recorded_mission = recording_info(rep)
    if args.at + 1000 > frames:
        sys.exit(f"--at {args.at}: the recording has {frames} frames")
    cut = out / f"{args.mission}_cut_{args.at}.rep"
    cut_recording(rep, args.at, cut)
    with open(out / "variants.txt", "w") as f:
        f.writelines(f"{name}={' '.join(options)}\n" for name, options in variants)
    jobs = [(v, m) for m in args.model for v in variants]
    print(f"{len(jobs)} runs, {args.jobs} at a time")
    with ThreadPoolExecutor(args.jobs) as pool:
        for line in pool.map(lambda j: viewer_one(args, out, j[0], j[1], rep, recorded_mission, cut), jobs):
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
        box = None
        if (mission_dir / "labels.csv").exists():  # viewer runs: the turntable's angles
            with open(mission_dir / "labels.csv", newline="") as f:
                labels = {r["frame"]: r["label"] for r in csv.DictReader(f)}
            box = model_box(sorted(mission_dir.glob("shot_*.bmp")))
        else:
            labels = dict((str(f), l) for f, l in capture_points(mission, 10 ** 9)) if POINTS.exists() else {}
        for shot in sorted(mission_dir.glob("shot_*.bmp"), key=lambda p: int(p.stem[5:])):
            a = load(shot)
            if box:
                a = a[box[1]:box[3], box[0]:box[2]]
            frame = shot.stem[5:]
            for v in variants:
                if v == base:
                    continue
                other = out / v / mission / shot.name
                if not other.exists():
                    rows.append({"mission": mission, "frame": frame, "variant": v, "missing": True})
                    continue
                b = load(other)
                if box:
                    b = b[box[1]:box[3], box[0]:box[2]]
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


# --- Phase 2 tools: camera orbit, reference builds, neutral checks ---

ORBIT_SETTLE = 30  # src/runtime/loader/orbit.cpp kSettle
ORBIT_HOLD = 20
ORBIT_VARIANTS = ['off=--graphics enhanced --sun-shadows off --original-shadows off', 'on=--graphics enhanced']


def orbit_poses(sets):
    """(label, set, forward, left, up, yaw, pitch) per pose."""
    import math
    poses = []
    for s in sets:
        if s == "turn":
            poses += [(f"turn {a}", s, 0, 0, 0, a, "keep") for a in range(0, 360, 45)]
        elif s == "orbit":
            for d in (150, 400):
                p = math.radians(20)
                for a in range(0, 360, 45):
                    r = math.radians(a)
                    poses.append((f"orbit {d} {a}", f"orbit{d}", -d * math.cos(p) * math.cos(r),
                                  -d * math.cos(p) * math.sin(r), d * math.sin(p), a, 20))
        elif s == "micro":
            poses += [(f"micro {k}", s, 0, 0.25 * k, 0, 0, "keep") for k in range(8)]
        else:
            sys.exit(f"no pose set {s} (turn, orbit, micro)")
    return poses


def orbit_one(args, out, variant, rep, recorded_mission, cut, poses):
    name, options = variant
    folder = out / name / args.mission
    shutil.rmtree(folder, ignore_errors=True)
    folder.mkdir(parents=True)
    saves = rep.with_suffix(".saves")
    if not saves.is_dir():
        saves = rep.parent / "saves"
    data = folder / "data"
    shutil.copytree(saves, data, symlinks=True)
    start = args.at + 1  # the first live frame: frames run on the real clock until the pause takes hold
    first = start + ORBIT_SETTLE
    shots = [first + ORBIT_HOLD * k + ORBIT_HOLD - 1 for k in range(len(poses))]
    with open(folder / "orbit.txt", "w") as f:
        f.write(f"start {start}\nhold {ORBIT_HOLD}\n")
        for label, _, fw, lf, up, yaw, pitch in poses:
            f.write(f"{fw:.3f} {lf:.3f} {up:.3f} {yaw} {pitch}  # {label}\n")
    with open(folder / "poses.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["frame", "label", "set"])
        w.writerows((frame, p[0], p[1]) for frame, p in zip(shots, poses))
    level = [] if recorded_mission == "-" else ["--level", recorded_mission]
    command = [str(args.loader), "--data", str(data), "--silent", *level, "--replay", str(cut), "--replay-continue",
               "--replay-no-draw", "--replay-speed", str(args.speed),
               "--replay-shots", ",".join(map(str, shots)), str(folder), "--orbit", str(folder / "orbit.txt"),
               *options]
    env = dict(os.environ, COD_BRO_DEV_KEYS="1@1")  # F1: overlay hidden in the pictures
    seconds = int(args.at / 60 / min(args.speed, 2) + 180)
    with open(folder / "log.txt", "w") as log:
        log.write(" ".join(command) + "\n")
        log.flush()
        try:
            code = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=seconds, env=env).returncode
        except subprocess.TimeoutExpired:
            code = "timeout"
    shutil.rmtree(data, ignore_errors=True)
    done = "done" if "orbit done" in (folder / "log.txt").read_text(errors="replace") else "NOT done"
    pictures = len(list(folder.glob("shot_*.bmp")))
    return f"{name:12} {args.mission:4} exit {code}, orbit {done}, {pictures}/{len(shots)} pictures"


def shadow_mask(base, other):
    return luminance(other) < luminance(base) - 4


def speckle(mask):
    """Mask pixels whose 3x3 neighbourhood mostly disagrees with them, per mask pixel."""
    if not mask.any():
        return 0.0
    m = mask.astype(np.int8)
    p = np.pad(m, 1)
    s = sum(p[1 + dy:p.shape[0] - 1 + dy, 1 + dx:p.shape[1] - 1 + dx] for dy in (-1, 0, 1) for dx in (-1, 0, 1))
    majority = s >= 5
    return float((majority != mask).sum() / mask.sum())


def cmd_orbit(args):
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    variants = parse_variants(args.variant or ORBIT_VARIANTS)
    rep = Path(args.recording) if args.recording else recording_for(args.mission)
    frames, recorded_mission = recording_info(rep)
    if args.at + 200 > frames:
        sys.exit(f"--at {args.at}: the recording has {frames} frames")
    cut = out / f"{args.mission}_cut_{args.at}.rep"
    cut_recording(rep, args.at, cut)
    poses = orbit_poses(args.sets)
    with open(out / "variants.txt", "w") as f:
        f.writelines(f"{name}={' '.join(options)}\n" for name, options in variants)
    print(f"{len(variants)} runs of {len(poses)} poses, {args.jobs} at a time")
    with ThreadPoolExecutor(args.jobs) as pool:
        for line in pool.map(lambda v: orbit_one(args, out, v, rep, recorded_mission, cut, poses), variants):
            print(line)
    args.baseline = variants[0][0]
    orbit_compare(out, args.mission, args.baseline)


def orbit_compare(out, mission, base):
    variants = [line.split("=", 1)[0] for line in (out / "variants.txt").read_text().splitlines()]
    with open(out / base / mission / "poses.csv", newline="") as f:
        poses = list(csv.DictReader(f))
    rows = []
    for v in variants:
        if v == base:
            continue
        casters = {}
        log = out / v / mission / "log.txt"
        if log.exists():
            import re
            for m in re.finditer(r"orbit casters pose=(\d+) count=(\d+)", log.read_text(errors="replace")):
                casters[int(m.group(1))] = int(m.group(2))
        prev = None
        for i, p in enumerate(poses):
            a_path, b_path = out / base / mission / f"shot_{p['frame']}.bmp", out / v / mission / f"shot_{p['frame']}.bmp"
            if not a_path.exists() or not b_path.exists():
                rows.append({"variant": v, **p, "missing": True})
                prev = None
                continue
            a, b = load(a_path), load(b_path)
            mask = shadow_mask(a, b)
            row = {"variant": v, **p, "area": float(mask.mean() * 100), "speckle": speckle(mask), "shimmer": "",
                   "casters": casters.get(i, "")}
            if prev is not None and prev[0]["set"] == p["set"] == "micro":
                flips = float((mask != prev[1]).mean() * 100)
                own = float((np.abs(luminance(a) - luminance(prev[2])) > 4).mean() * 100)
                row["shimmer"] = max(0.0, flips - own)
            pair = out / "pairs" / f"{mission}_{p['frame']}_{base}_vs_{v}.png"
            pair.parent.mkdir(exist_ok=True)
            side_by_side(a, b, pair)
            row["pair"] = pair.relative_to(out).as_posix()
            rows.append(row)
            prev = (row, mask, a)
        # dips: a pose under half of both neighbours' area within its set (turn and orbit sets wrap round)
        for s in {r["set"] for r in rows if r["variant"] == v and r["set"] != "micro" and "area" in r}:
            ring = [r for r in rows if r["variant"] == v and r["set"] == s and "area" in r]
            for k, r in enumerate(ring):
                left, right = ring[k - 1]["area"], ring[(k + 1) % len(ring)]["area"]
                r["dip"] = "DIP" if len(ring) > 2 and r["area"] < 0.5 * min(left, right) else ""
    keys = ["variant", "frame", "label", "set", "casters", "area", "speckle", "shimmer", "dip"]
    with open(out / "orbit.csv", "w", newline="") as f:
        w = csv.DictWriter(f, keys, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)
    print(f"baseline {base}; area/shimmer in % of the picture, speckle per mask pixel (shown from 0.05% area), "
          f"casters = shadow caster draws in the pose's frame")
    print(f"{'variant':8} {'pose':16} {'casters':>7} {'area%':>6} {'speckle':>7} {'shimmer':>7}")
    for r in rows:
        if r.get("missing"):
            print(f"{r['variant']:8} {r['label']:16} MISSING")
            continue
        sh = f"{r['shimmer']:7.3f}" if r["shimmer"] != "" else " " * 7
        sp = f"{r['speckle']:7.3f}" if r["area"] >= 0.05 else "      -"
        print(f"{r['variant']:8} {r['label']:16} {str(r['casters']):>7} {r['area']:6.2f} {sp} {sh} {r.get('dip', '')}")
    with open(out / "orbit.html", "w") as f:
        f.write("<!doctype html><meta charset=utf-8><title>Lab orbit</title><style>body{font:14px sans-serif;"
                "background:#111;color:#ddd}img{max-width:100%}</style>")
        for r in rows:
            if "pair" in r:
                f.write(f"<h3>{html.escape(r['variant'])} {html.escape(r['label'])}: area {r['area']:.2f}%, "
                        f"speckle {r['speckle']:.3f} {r.get('dip', '')}</h3><img src='{r['pair']}'>")
    print(out / "orbit.html")


REFERENCES = PORT / "build/lab_reference"


def cmd_reference(args):
    name = args.name or subprocess.run(["git", "-C", str(PORT), "rev-parse", "--short", "HEAD"],
                                       capture_output=True, text=True, check=True).stdout.strip()
    source = DEFAULT_LOADER.parent
    target = REFERENCES / name
    shutil.rmtree(target, ignore_errors=True)
    target.mkdir(parents=True)
    for item in source.iterdir():
        if item.name.endswith("_test") or item.name in ("cod_bro", "cod_bro_launcher"):
            continue
        (shutil.copytree if item.is_dir() else shutil.copy2)(item, target / item.name)
    # The absolute RPATH (the build folder) -> $ORIGIN, padded: the copy loads its own game library.
    old = str(source).encode() + b":"
    loader = target / DEFAULT_LOADER.name
    data = loader.read_bytes()
    if data.count(old) != 1:
        sys.exit(f"RPATH {old!r} not found once in {loader}")
    loader.write_bytes(data.replace(old, b"$ORIGIN".ljust(len(old), b"\0")))
    print(f"reference {name}: {target}")


def cmd_neutral(args):
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    refs = sorted(REFERENCES.iterdir(), key=lambda p: p.stat().st_mtime) if REFERENCES.is_dir() else []
    ref = REFERENCES / args.reference if args.reference else (refs[-1] if refs else None)
    if not ref or not (ref / DEFAULT_LOADER.name).exists():
        sys.exit("no reference build: tools/lab.py reference first")
    options = args.options.split()
    runs = [("reference", ref / DEFAULT_LOADER.name), ("current", args.loader)]
    with open(out / "variants.txt", "w") as f:
        f.writelines(f"{n}={' '.join(options)}\n" for n, _ in runs)
    jobs = []
    for mission in args.mission:
        rep = recording_for(mission)
        frames, recorded_mission = recording_info(rep)
        points = capture_points(mission, frames)
        jobs += [((n, options), loader, mission, rep, frames, recorded_mission, points) for n, loader in runs]
    print(f"reference {ref.name}, {len(jobs)} runs, {args.jobs} at a time")

    def one(job):
        variant, loader, *rest = job
        a = argparse.Namespace(loader=loader, speed=args.speed)
        return run_one(a, out, variant, *rest)
    with ThreadPoolExecutor(args.jobs) as pool:
        for line in pool.map(one, jobs):
            print(line)
    failed = 0
    for mission in args.mission:
        for shot in sorted((out / "reference" / mission).glob("shot_*.bmp"), key=lambda p: int(p.stem[5:])):
            other = out / "current" / mission / shot.name
            if not other.exists():
                print(f"{mission} {shot.stem[5:]:>6} MISSING")
                failed += 1
                continue
            a, b = load(shot), load(other)
            d = np.abs(a - b).max(axis=2) if a.shape == b.shape else None
            if d is None or d.max() > 0:
                failed += 1
                what = "size differs" if d is None else f"{(d > 0).mean() * 100:.3f}% pixels, max {int(d.max())}"
                print(f"{mission} {shot.stem[5:]:>6} FAIL {what}")
            else:
                print(f"{mission} {shot.stem[5:]:>6} PASS")
    print("neutral: " + ("PASS" if not failed else f"FAIL ({failed} pictures)"))
    sys.exit(1 if failed else 0)


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
    v = sub.add_parser("viewer")
    v.add_argument("out")
    v.add_argument("--model", nargs="+", required=True)
    v.add_argument("--mission", default="m0")
    v.add_argument("--at", type=int, default=6000)
    v.add_argument("--steps", type=int, default=8)
    v.add_argument("--variant", action="append")
    v.add_argument("--loader", type=Path, default=DEFAULT_LOADER)
    v.add_argument("--speed", type=float, default=3)
    v.add_argument("--jobs", type=int, default=max(1, min(4, (os.cpu_count() or 4) // 4)))
    o = sub.add_parser("orbit")
    o.add_argument("out")
    o.add_argument("--mission", default="m2")
    o.add_argument("--recording", help="default: the mission's first recording")
    o.add_argument("--at", type=int, default=4200)
    o.add_argument("--sets", nargs="+", default=["turn", "orbit", "micro"])
    o.add_argument("--variant", action="append")
    o.add_argument("--loader", type=Path, default=DEFAULT_LOADER)
    o.add_argument("--speed", type=float, default=3)
    o.add_argument("--jobs", type=int, default=2)
    rf = sub.add_parser("reference")
    rf.add_argument("name", nargs="?")
    n = sub.add_parser("neutral")
    n.add_argument("out")
    n.add_argument("--reference")
    n.add_argument("--mission", nargs="+", default=["m0", "m2", "m4"])
    n.add_argument("--options", default="--graphics enhanced")
    n.add_argument("--loader", type=Path, default=DEFAULT_LOADER)
    n.add_argument("--speed", type=float, default=3)
    n.add_argument("--jobs", type=int, default=max(1, min(4, (os.cpu_count() or 4) // 4)))
    args = p.parse_args()
    {"run": cmd_run, "compare": cmd_compare, "crop": cmd_crop, "viewer": cmd_viewer, "orbit": cmd_orbit,
     "reference": cmd_reference, "neutral": cmd_neutral}[args.cmd](args)


if __name__ == "__main__":
    main()
