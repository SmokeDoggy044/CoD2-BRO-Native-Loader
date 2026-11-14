#!/usr/bin/env python3
"""Phase 1h GPU worker: every PBRify 1x map model over upscaled colour PNGs, headless, raw outputs kept.

usage (normally started by build.py --stage maps with a torch interpreter, e.g. chaiNNer's portable Python):
  maps.py JOBS.csv --model normal=PATH --model roughness=PATH --model height=PATH [--tile 256] [--margin 16]

JOBS.csv columns: src (upscaled PNG), dst (prefix: dst_<label>.png per model), wrap_x, wrap_y. Images with every
output present are skipped, so a stopped run resumes. Raw model outputs are stored (the GPU pass is the expensive
part; blur, normalising, encoding and derived maps such as cavity/AO happen later on the CPU, in build.py):
  normal    RGB 8 bit, as the model gives it (DirectX convention, green = -Y; not yet normalised or blurred)
  roughness grey 8 bit (mean of the model's 3 channels)
  height    grey 16 bit (mean of the 3 channels; parallax needs the extra precision)
Alpha is ignored (maps cover the whole texture; the colour's alpha still cuts out). Prints one line per image.
"""
import argparse
import csv
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))  # -I leaves the script's folder off sys.path

import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402
from upscale import load, run, to_u8  # noqa: E402


def save(image, dst):
    tmp = dst.with_suffix(".part.png")
    image.save(tmp, compress_level=1)
    tmp.rename(dst)


def output(label, a):
    if label == "normal":
        return Image.fromarray(to_u8(a), "RGB")
    grey = np.clip(a.mean(axis=2), 0, 1)
    if label == "height":
        return Image.fromarray((grey * 65535.0 + 0.5).astype(np.uint16), "I;16")
    return Image.fromarray(to_u8(grey), "L")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("jobs", type=Path)
    p.add_argument("--model", action="append", required=True, help="LABEL=PATH (1x model)")
    p.add_argument("--tile", type=int, default=256)
    p.add_argument("--margin", type=int, default=16)
    p.add_argument("--batch", type=int, default=2)
    args = p.parse_args()

    nets = [(label, *load(Path(path))) for label, path in (m.split("=", 1) for m in args.model)]
    with open(args.jobs, newline="") as f:
        jobs = list(csv.DictReader(f))

    def dst(job, label):
        prefix = Path(job["dst"])
        return prefix.with_name(f"{prefix.name}_{label}.png")

    todo = [j for j in jobs if not all(dst(j, label).exists() for label, _, _ in nets)]
    print(f"{len(jobs) - len(todo)} of {len(jobs)} already made", flush=True)
    for n, job in enumerate(todo, 1):
        start = time.time()
        rgb = np.ascontiguousarray(np.asarray(Image.open(job["src"]).convert("RGB"), np.float32) / 255.0)
        wx, wy = job["wrap_x"] == "1", job["wrap_y"] == "1"
        for label, net, scale in nets:
            target = dst(job, label)
            if target.exists():
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            save(output(label, run(net, scale, rgb, args.tile, args.margin, args.batch, wx, wy)), target)
        print(f"done {n}/{len(todo)} {Path(job['src']).name} {time.time() - start:.1f}s", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
