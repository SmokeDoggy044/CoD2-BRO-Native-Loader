#!/usr/bin/env python3
"""Phase 1d GPU worker: runs a spandrel model (standard PBRify 4x) over a list of PNGs, headless.

usage (normally started by build.py with a torch interpreter, e.g. chaiNNer's portable Python):
  upscale.py MODEL JOBS.csv [--tile 128] [--margin 16] [--batch 4] [--pbr NORMAL_MODEL,ROUGHNESS_MODEL]

JOBS.csv columns: src (PNG), dst (PNG), alpha (0 = opaque, 1 = upscale alpha too), wrap_x, wrap_y (1 = the texture
repeats on that axis: pad with its other edge, else mirror). Existing dst files are skipped, so a stopped run resumes.

Colour goes through the model as RGB; alpha (when used) goes through the same model as grey and the mean of its
output is taken, as chaiNNer's chain does with split/merge transparency. Each image is padded before tiling so the
model sees the neighbouring texels of a repeating texture (no seams where it wraps). With --pbr, the upscaled colour
also goes through the 1x normal and roughness models (dst_normal.png, dst_roughness.png) - kept for a later PBR mod.
Prints one line per finished image ("done N/M name Ws") and nothing else, so its output stays small.
"""
import argparse
import csv
import sys
import time
from pathlib import Path

import numpy as np
import torch
from PIL import Image
from spandrel import ModelLoader


def load(path):
    model = ModelLoader().load_from_file(path)
    return model.model.cuda().eval(), model.scale


def pad(image, margin, wrap_x, wrap_y):
    """HxWxC float array padded by margin on each side (wrap or mirror per axis; mirror falls back for tiny sizes)."""
    h, w = image.shape[:2]
    my, mx = margin, margin
    out = np.pad(image, ((0, 0), (mx, mx), (0, 0)), mode="wrap" if wrap_x else ("reflect" if w > mx else "edge"))
    out = np.pad(out, ((my, my), (0, 0), (0, 0)), mode="wrap" if wrap_y else ("reflect" if h > my else "edge"))
    return out


@torch.inference_mode()
def run(net, scale, image, tile, margin, batch, wrap_x, wrap_y):
    """HxWx3 float32 [0,1] -> (H*scale)x(W*scale)x3, tiled with overlapping margins."""
    h, w = image.shape[:2]
    src = pad(image, margin, wrap_x, wrap_y)
    out = np.zeros((h * scale, w * scale, 3), np.float32)
    boxes = [(y, x, min(tile, h - y), min(tile, w - x)) for y in range(0, h, tile) for x in range(0, w, tile)]
    for i in range(0, len(boxes), batch):
        group = boxes[i:i + batch]
        # tiles of one batch need one size: edge tiles may be smaller, run them alone
        sizes = {(th, tw) for _, _, th, tw in group}
        parts = [group] if len(sizes) == 1 else [[b] for b in group]
        for part in parts:
            tiles = np.stack([src[y:y + th + 2 * margin, x:x + tw + 2 * margin] for y, x, th, tw in part])
            t = torch.from_numpy(tiles).permute(0, 3, 1, 2).cuda()
            r = net(t).clamp_(0, 1).permute(0, 2, 3, 1).cpu().numpy()
            m = margin * scale
            for (y, x, th, tw), res in zip(part, r):
                out[y * scale:(y + th) * scale, x * scale:(x + tw) * scale] = res[m:m + th * scale, m:m + tw * scale]
    return out


def to_u8(a):
    return (np.clip(a, 0, 1) * 255.0 + 0.5).astype(np.uint8)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("model", type=Path)
    p.add_argument("jobs", type=Path)
    p.add_argument("--tile", type=int, default=128)
    p.add_argument("--margin", type=int, default=16)
    p.add_argument("--batch", type=int, default=4)
    p.add_argument("--pbr", default="", help="NORMAL_MODEL,ROUGHNESS_MODEL (1x)")
    args = p.parse_args()

    net, scale = load(args.model)
    pbr = [load(Path(m)) for m in args.pbr.split(",")] if args.pbr else []
    with open(args.jobs, newline="") as f:
        jobs = list(csv.DictReader(f))
    todo = [j for j in jobs if not Path(j["dst"]).exists()]
    print(f"{len(jobs) - len(todo)} of {len(jobs)} already upscaled", flush=True)
    for n, job in enumerate(todo, 1):
        start = time.time()
        image = Image.open(job["src"]).convert("RGBA")
        a = np.asarray(image, np.float32) / 255.0
        wx, wy = job["wrap_x"] == "1", job["wrap_y"] == "1"
        rgb = run(net, scale, np.ascontiguousarray(a[..., :3]), args.tile, args.margin, args.batch, wx, wy)
        if job["alpha"] == "1":
            grey = np.ascontiguousarray(np.repeat(a[..., 3:4], 3, axis=2))
            alpha = run(net, scale, grey, args.tile, args.margin, args.batch, wx, wy).mean(axis=2, keepdims=True)
            result = Image.fromarray(to_u8(np.concatenate([rgb, alpha], axis=2)), "RGBA")
        else:
            result = Image.fromarray(to_u8(rgb), "RGB")
        dst = Path(job["dst"])
        dst.parent.mkdir(parents=True, exist_ok=True)
        tmp = dst.with_suffix(".part.png")
        result.save(tmp, compress_level=1)
        for (pnet, pscale), label in zip(pbr, ("normal", "roughness")):
            # the 1x models see the upscaled colour; tiles in output pixels
            out = run(pnet, pscale, rgb, args.tile * 2, args.margin, 1, wx, wy)
            Image.fromarray(to_u8(out), "RGB").save(dst.with_name(f"{dst.stem}_{label}.png"), compress_level=1)
        tmp.rename(dst)
        print(f"done {n}/{len(todo)} {dst.name} {time.time() - start:.1f}s", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
