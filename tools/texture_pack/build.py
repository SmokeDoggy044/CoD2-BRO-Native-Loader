#!/usr/bin/env python3
"""Phase 1d texture pack builder: dump textures -> PBRify 4x (headless) -> curation masks -> DXT + mips -> mod folder.

usage: tools/texture_pack/build.py WORK [--source REGEX] [--bridge CSV ...] [--limit N] [--stage all|select|upscale|
                                  encode|pack|maps|maps-pack] [--curation CSV] [--zip] [--pbr] ...

WORK holds every stage's output, so each stage resumes where it stopped and later stages rerun cheaply:
  WORK/select.csv           chosen textures: runtime key, dump row, size, alpha use, wrap per axis, skip reason
  WORK/upscaled/<sha1>.png  model output (RGB or RGBA), made by upscale.py under a torch Python (--torch-python;
                            default chaiNNer's portable Python in Tools/chaiNNer, which has torch + spandrel)
  WORK/mod/                 the mod folder: mod.ini, textures/<key>.dds, pack.csv (key -> asset path, for 1e/1.0c)
  WORK/<name>-<version>.zip with --zip (the mod folder, for distribution apart from the code repos)
  WORK/maps/<sha1>_{normal,roughness,height}.png   (--stage maps, phase 1h) raw PBRify 1x outputs of every world
                            texture (maps.py: all map models PBRify has, so the GPU pass never has to run again)
  WORK/maps_dds/<key>_{normal,roughness,height}.dds  the same encoded at the pack texture's size, full mips:
                            normal = BC5 (ATI2, X/Y, DirectX green, blurred 0.8 + normalised as PBRify's chain),
                            roughness/height = BC4 (ATI1). Not loaded by the game yet (phase 2 shaders use them).

Selection: unique dump textures (manifest.csv, by content), filtered by --source (regex on the pack path, e.g.
'^sp/prologue/' for the pilot) and/or --bridge (only textures a texture_bridge.py run saw drawn). --classes picks world
(3D surfaces, default) and/or ui (menus, HUD, button prompts: build them into their own WORK with a model for flat
art - PBRify smears them). Skipped, but listed in select.csv: the other class, lightmaps, font pages, textures under --min-size texels on a side (ramps, lookups, 1x1s) and
curation 'skip' rows (a curation action overrides the automatic skips).

Encoding: a colour fix first (the upscale's low frequencies matched to the original: the model brightens),
then DXT1 when the result is opaque, DXT1 originals with cut-out alpha get a hard alpha edge again (DXT5),
everything else DXT5 (DXT3 originals included); full mip chain, each level a 2x2 box of the one before (as the title's).
Sides over --max-size are scaled down. The key is the runtime content hash (render::hash_bytes) of what the title
holds, computed from the dump as texture_bridge.py does - so one pack fits both editions and both back ends.

Curation CSV (phase 1e; matched by asset_path or content_sha1): action = upscale | skip | conservative (half model,
half plain Lanczos), masks = 'x0 y0 x1 y1;...' in original texels: those regions keep the plain Lanczos enlargement
(e.g. insignia the model smears). Nothing here opens or shows a picture to the assistant.
"""
import argparse
import csv
import io
import os
import re
import shutil
import struct
import subprocess
import sys
import zipfile
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np
from PIL import Image

TOOLS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOLS))
from texture_bridge import candidates  # noqa: E402

ROOT = TOOLS.parent.parent  # cod_bro/
XBOX = ROOT.parent
DEFAULT_TORCH = XBOX / "Tools/chaiNNer/python/python/bin/python3"
DEFAULT_MODEL = XBOX / "Tools/PBRify_Remix/Models/4x-PBRify-UpscalerV4.safetensors"
PBR_MODELS = [XBOX / "Tools/PBRify_Remix/Models/1x-PBRify_NormalV3.pth",
              XBOX / "Tools/PBRify_Remix/Models/1x-PBRify_RoughnessV2.pth"]
MAP_MODELS = {"normal": XBOX / "Tools/PBRify_Remix/Models/1x-PBRify_NormalV3.pth",
              "roughness": XBOX / "Tools/PBRify_Remix/Models/1x-PBRify_RoughnessV2.pth",
              "height": XBOX / "Tools/PBRify_Remix/Models/1x-PBRify_Height.pth"}
DDS_HEADER = 128
SELECT_FIELDS = ["key", "content_sha1", "asset_path", "source", "format", "width", "height", "alpha", "wrap_x",
                 "wrap_y", "class", "action", "masks", "skip"]


def read_payload(unpacked, row):
    with open(unpacked / row["source"], "rb") as f:
        f.seek(int(row["offset"]))
        return f.read(int(row["size"]))


def runtime_key(payload):
    """The hash the title's texture would have: swizzled for uncompressed power-of-two textures, else the DDS data."""
    found = dict(candidates(payload))
    return found.get("swizzled", found["dds"])


def seams(a):
    """Wrap per axis: the step across the edge is no larger than the texture's usual neighbour step."""
    rgb = a[..., :3].astype(np.float32)
    def axis(x):
        if x.shape[1] < 4:
            return False
        inner = np.abs(np.diff(x, axis=1)).mean()
        edge = np.abs(x[:, 0] - x[:, -1]).mean()
        return edge <= 2.0 * inner + 2.0
    return axis(rgb), axis(rgb.transpose(1, 0, 2))


def analyse(item):
    row, png, unpacked = item
    payload = read_payload(unpacked, row)
    a = np.asarray(Image.open(png).convert("RGBA"))
    alpha_min = int(a[..., 3].min())
    if alpha_min == 255:
        alpha = "none"
    elif row["format"] == "DXT1" or set(np.unique(a[..., 3])) <= {0, 255}:
        alpha = "cutout"
    else:
        alpha = "blend"
    wx, wy = seams(a)
    return {"key": f"{runtime_key(payload):016x}", "content_sha1": row["content_sha1"],
            "asset_path": row["asset_path"], "source": row["source"], "format": row["format"],
            "width": a.shape[1], "height": a.shape[0], "alpha": alpha, "wrap_x": int(wx), "wrap_y": int(wy)}


LIGHTMAP = re.compile(r"[\\/]lightmaps[\\/]", re.I)
FONT = re.compile(r"[\\/][^\\/]*(font|garamond|arialblack)[^\\/]*\.dds$", re.I)


UI = re.compile(r"[\\/](shell|hud)[\\/]|[\\/]textures[\\/]global[\\/]", re.I)


def texture_class(asset_path):
    """world = surfaces in the 3D scene (PBRify fits: user 11-14, all passed); ui = menus, HUD, button prompts
    (flat art and lettering the texture model smears: a pack of its own with another workflow)."""
    return "ui" if UI.search(asset_path) else "world"


def auto_skip(asset_path):
    """Not surface pictures: baked lighting (the model shifts it by up to 12 levels) and font pages (sharp text
    replaces them already). A curation row with an action overrides this."""
    if LIGHTMAP.search(asset_path):
        return "lightmap"
    if FONT.search(asset_path):
        return "font"
    return ""


def load_curation(path):
    out = {}
    if path:
        with open(path, newline="") as f:
            for r in csv.DictReader(f):
                for k in (r.get("asset_path"), r.get("content_sha1")):
                    if k:
                        out[k] = r
    return out


def select(args):
    rows = {}
    with open(args.manifest, newline="") as f:
        for r in csv.DictReader(f):
            if args.source and not re.search(args.source, r["source"]):
                continue
            rows.setdefault(r["content_sha1"], r)
    if args.bridge:
        seen = set()
        for b in args.bridge:
            with open(b, newline="") as f:
                seen.update(r["content_sha1"] for r in csv.DictReader(f))
        rows = {k: v for k, v in rows.items() if k in seen}
    items = sorted(rows.values(), key=lambda r: r["asset_path"])
    if args.limit:
        items = items[:args.limit]
    pngs = [(r, args.manifest.parent / (r["output"] + ".png"), args.unpacked) for r in items]
    curation = load_curation(args.curation)
    out = []
    with ProcessPoolExecutor(args.jobs) as pool:
        for s in pool.map(analyse, pngs, chunksize=8):
            c = curation.get(s["asset_path"]) or curation.get(s["content_sha1"]) or {}
            s["class"] = texture_class(s["asset_path"])
            s["action"] = c.get("action") or "upscale"
            s["masks"] = c.get("masks", "")
            s["skip"] = ("curation" if s["action"] == "skip" else "" if c.get("action") else
                         auto_skip(s["asset_path"]) or
                         (s["class"] if s["class"] not in args.classes.split(",") else "") or ("small" if min(s["width"], s["height"]) < args.min_size else ""))
            out.append(s)
    with open(args.work / "select.csv", "w", newline="") as f:
        w = csv.DictWriter(f, SELECT_FIELDS)
        w.writeheader()
        w.writerows(out)
    keys = {}
    for s in out:
        keys.setdefault(s["key"], []).append(s["asset_path"])
    clashes = sum(1 for v in keys.values() if len(v) > 1)
    used = [s for s in out if not s["skip"]]
    print(f"select: {len(out)} textures, {len(used)} to upscale ({sum(s['width'] * s['height'] for s in used) / 1e6:.1f}"
          f" Mpx), {len(out) - len(used)} skipped, {clashes} runtime keys shared by several dump rows")


def read_select(work):
    with open(work / "select.csv", newline="") as f:
        return list(csv.DictReader(f))


def upscale(args):
    rows = [s for s in read_select(args.work) if not s["skip"]]
    jobs = args.work / "upscale_jobs.csv"
    by_sha1 = {}
    with open(args.manifest, newline="") as f:
        for r in csv.DictReader(f):
            by_sha1.setdefault(r["content_sha1"], r)
    with open(jobs, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["src", "dst", "alpha", "wrap_x", "wrap_y"])
        for s in rows:
            src = args.manifest.parent / (by_sha1[s["content_sha1"]]["output"] + ".png")
            w.writerow([src, args.work / "upscaled" / f"{s['content_sha1']}.png", int(s["alpha"] != "none"),
                        s["wrap_x"], s["wrap_y"]])
    cmd = [str(args.torch_python), "-I", str(Path(__file__).with_name("upscale.py")), str(args.model), str(jobs),
           "--tile", str(args.tile), "--batch", str(args.batch)]
    if args.pbr:
        cmd += ["--pbr", ",".join(str(m) for m in PBR_MODELS)]
    env = dict(os.environ, PYTHONWARNINGS="ignore")
    log = open(args.work / "upscale.log", "a")
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=log, text=True, env=env)
    done = 0
    for line in proc.stdout:
        log.write(line)
        log.flush()
        done += line.startswith("done ")
        if not line.startswith("done ") or done % args.progress == 0:
            print("upscale:", line.rstrip(), flush=True)
    if proc.wait():
        sys.exit(f"upscale.py failed ({proc.returncode}); see {args.work / 'upscale.log'}")


def dxt(image, fmt):
    buf = io.BytesIO()
    image.save(buf, format="DDS", pixel_format=fmt)
    return buf.getvalue()


def blur(a, sigma):
    """Separable Gaussian with wrapped edges (Pillow's blur takes no float images)."""
    r = max(1, int(3 * sigma + 0.5))
    k = np.exp(-0.5 * (np.arange(-r, r + 1) / sigma) ** 2)
    k /= k.sum()
    for axis in (0, 1):
        a = sum(w * np.roll(a, i - r, axis=axis) for i, w in enumerate(k))
    return a


def colour_fix(image, original, radius):
    """Pull the upscale's low frequencies back to the original's: the model brightens textures (prologue pilot:
    +0.76 levels on average, up to +3 on walls). Detail stays; shrunk back, the result matches the original."""
    w, h = image.size
    up = np.asarray(image, np.float32)
    orig = np.asarray(Image.open(original).convert(image.mode), np.float32)
    # per channel: Pillow resizes RGBA premultiplied, which zeroes the colour under transparent texels
    down = np.stack([np.asarray(ch.resize((orig.shape[1], orig.shape[0]), Image.BOX), np.float32)
                     for ch in image.split()], axis=2)
    # weighted by the original's alpha: the colour under transparent texels is arbitrary and must not leak into
    # visible edges (normalised convolution)
    weight = orig[..., 3] / 255.0 if up.shape[2] == 4 else np.ones(orig.shape[:2], np.float32)
    spread = blur(weight, max(radius, 0.5)) + 1e-4
    channels = []
    for c in range(up.shape[2]):
        diff = orig[..., c] - down[..., c]
        if c < 3:
            diff = blur(diff * weight, max(radius, 0.5)) / spread
        elif radius > 0:
            diff = blur(diff, radius)
        diff = Image.fromarray(np.ascontiguousarray(diff, np.float32))
        channels.append(np.asarray(diff.resize((w, h), Image.BICUBIC), np.float32))
    fixed = up + np.stack(channels, axis=2)
    return Image.fromarray(np.clip(fixed + 0.5, 0, 255).astype(np.uint8), image.mode)


def next_level(level, cutout):
    """A 2x2 box of the level before (as the title's own mips; Lanczos levels were crisper and shimmered)."""
    level = level.reduce(2) if level.width > 1 and level.height > 1 else level.resize(
        (max(1, level.width // 2), max(1, level.height // 2)), Image.BOX)
    if cutout:
        r, g, b, a = level.split()
        level = Image.merge("RGBA", (r, g, b, a.point(lambda v: 255 if v >= 128 else 0)))
    return level


def encode_one(item):
    s, src, original, dst, max_size, fix_radius = item
    image = Image.open(src)
    image.load()
    w, h = image.size
    if fix_radius >= 0:
        image = colour_fix(image, original, fix_radius)
    if s["action"] == "conservative" or s["masks"]:
        plain = Image.open(original).convert(image.mode).resize((w, h), Image.LANCZOS)
        if s["action"] == "conservative":
            image = Image.blend(image, plain, 0.5)
        sx, sy = w / int(s["width"]), h / int(s["height"])
        for box in filter(None, s["masks"].split(";")):
            x0, y0, x1, y1 = (int(v) for v in box.split())
            region = (round(x0 * sx), round(y0 * sy), round(x1 * sx), round(y1 * sy))
            image.paste(plain.crop(region), region[:2])
    if max(w, h) > max_size:
        f = max_size / max(w, h)
        image = image.resize((max(1, round(w * f)), max(1, round(h * f))), Image.LANCZOS)
    if s["alpha"] == "cutout":
        r, g, b, a = image.split()
        image = Image.merge("RGBA", (r, g, b, a.point(lambda v: 255 if v >= 128 else 0)))
    fmt = "DXT1" if image.mode == "RGB" else "DXT5"
    levels, top = [], image
    level = top
    while True:
        levels.append(dxt(level, fmt)[DDS_HEADER:])
        if level.width == 1 and level.height == 1:
            break
        level = next_level(level, s["alpha"] == "cutout")
    header = bytearray(dxt(top, fmt)[:DDS_HEADER])
    struct.pack_into("<I", header, 8, struct.unpack_from("<I", header, 8)[0] | 0x20000)  # DDSD_MIPMAPCOUNT
    struct.pack_into("<I", header, 28, len(levels))
    struct.pack_into("<I", header, 108, struct.unpack_from("<I", header, 108)[0] | 0x400008)  # complex | mipmap
    tmp = dst.with_suffix(".part")
    tmp.write_bytes(bytes(header) + b"".join(levels))
    tmp.rename(dst)
    return s["key"], fmt, image.width, image.height, dst.stat().st_size


def encode(args):
    rows = [s for s in read_select(args.work) if not s["skip"]]
    by_sha1 = {}
    with open(args.manifest, newline="") as f:
        for r in csv.DictReader(f):
            by_sha1.setdefault(r["content_sha1"], r)
    out = args.work / "mod" / "textures"
    out.mkdir(parents=True, exist_ok=True)
    stamp = args.work / "encode_stamp.txt"
    settings = f"encoder=3 max_size={args.max_size} colour_fix={args.colour_fix} mips=box curation={args.curation and Path(args.curation).stat().st_mtime}\n"
    redo = not stamp.exists() or stamp.read_text() != settings
    items, missing, done = [], 0, set()
    for s in rows:
        src = args.work / "upscaled" / f"{s['content_sha1']}.png"
        dst = out / f"{s['key']}.dds"
        if not src.exists():
            missing += 1
            continue
        if s["key"] in done:
            continue
        done.add(s["key"])
        if redo or not dst.exists() or dst.stat().st_mtime < src.stat().st_mtime:
            items.append((s, src, args.manifest.parent / (by_sha1[s["content_sha1"]]["output"] + ".png"), dst,
                          args.max_size, args.colour_fix))
    stale = {p.stem for p in out.glob("*.dds")} - done
    for k in stale:
        (out / f"{k}.dds").unlink()
    with ProcessPoolExecutor(args.jobs) as pool:
        results = list(pool.map(encode_one, items, chunksize=4))
    stamp.write_text(settings)
    total = sum(p.stat().st_size for p in out.glob("*.dds"))
    print(f"encode: {len(items)} written, {len(done) - len(items)} up to date, {len(stale)} stale removed, "
          f"{missing} not upscaled yet; pack {len(done)} textures, {total / 2**20:.0f} MB")


MAP_LABELS = ("normal", "roughness", "height")


def map_rows(args):
    """World-class textures in the pack (never UI/menu/HUD: user 11-14), optionally narrowed by --source."""
    return [s for s in read_select(args.work) if not s["skip"] and s.get("class", "world") == "world" and
            (not args.source or re.search(args.source, s["source"]))]


def maps(args):
    rows = map_rows(args)
    jobs = args.work / "maps_jobs.csv"
    with open(jobs, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["src", "dst", "wrap_x", "wrap_y"])
        for s in rows:
            src = args.work / "upscaled" / f"{s['content_sha1']}.png"
            if src.exists():
                w.writerow([src, args.work / "maps" / s["content_sha1"], s["wrap_x"], s["wrap_y"]])
    cmd = [str(args.torch_python), "-I", str(Path(__file__).with_name("maps.py")), str(jobs), "--tile", "256"]
    for label, model in MAP_MODELS.items():
        cmd += ["--model", f"{label}={model}"]
    env = dict(os.environ, PYTHONWARNINGS="ignore")
    log = open(args.work / "maps.log", "a")
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=log, text=True, env=env)
    done = 0
    for line in proc.stdout:
        log.write(line)
        log.flush()
        done += line.startswith("done ")
        if not line.startswith("done ") or done % args.progress == 0:
            print("maps:", line.rstrip(), flush=True)
    if proc.wait():
        sys.exit(f"maps.py failed ({proc.returncode}); see {args.work / 'maps.log'}")
    maps_encode(args, rows)


def bc4_blocks(a):
    """HxW float [0,255] (H, W multiples of 4) -> BC4 blocks (8-value mode: max, min, 6 between)."""
    h, w = a.shape
    b = a.reshape(h // 4, 4, w // 4, 4).transpose(0, 2, 1, 3).reshape(-1, 16)
    hi = np.round(b.max(axis=1))
    lo = np.round(b.min(axis=1))
    weights = np.array([0, 7, 1, 2, 3, 4, 5, 6], np.float32)  # index -> share of `lo` in sevenths
    palette = (hi[:, None] * (7 - weights) + lo[:, None] * weights) / 7.0
    index = np.abs(b[:, :, None] - palette[:, None, :]).argmin(axis=2).astype(np.uint64)
    index[hi == lo] = 0
    bits = (index << (3 * np.arange(16, dtype=np.uint64))).sum(axis=1).astype(np.uint64)
    out = np.zeros((len(b), 8), np.uint8)
    out[:, 0] = hi
    out[:, 1] = lo
    for i in range(6):
        out[:, 2 + i] = (bits >> np.uint64(8 * i)) & np.uint64(0xff)
    return out.tobytes()


def pad4(a):
    h, w = a.shape[:2]
    return np.pad(a, ((0, -h % 4), (0, -w % 4)) + ((0, 0),) * (a.ndim - 2), mode="edge")


def dds_header(w, h, levels, fourcc, top_size):
    hdr = bytearray(128)
    struct.pack_into("<4sIIIIIII", hdr, 0, b"DDS ", 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000, h, w,
                     top_size, 0, levels)
    struct.pack_into("<II4s", hdr, 76, 32, 0x4, fourcc)
    struct.pack_into("<I", hdr, 108, 0x1000 | 0x400000 | 0x8)
    return bytes(hdr)


def box_half(a):
    """Next mip: mean of 2x2 (1x2 once a side is 1; an odd last row/column is dropped)."""
    h, w = a.shape[:2]
    if h > 1:
        a = 0.5 * (a[0:h - 1:2] + a[1:h:2])
    if w > 1:
        a = 0.5 * (a[:, 0:w - 1:2] + a[:, 1:w:2])
    return a


def unit(n):
    return n / np.maximum(np.linalg.norm(n, axis=2, keepdims=True), 1e-6)


def encode_map(item):
    label, src, dst, size, blur_sigma = item
    image = Image.open(src)
    if image.size != size:
        image = image.convert("RGB" if label == "normal" else "F").resize(size, Image.LANCZOS)
    a = np.asarray(image, np.float32)
    if label == "normal":
        a = unit(a / 127.5 - 1.0)
        if blur_sigma > 0:
            a = unit(blur(a, blur_sigma))
    elif label == "height":
        a = a * (255.0 / 65535.0) if a.max() > 255.0 or image.mode in ("I;16", "I") else a
    levels = []
    while True:
        if label == "normal":
            x = pad4(np.clip(a[..., 0] * 127.5 + 127.5, 0, 255))
            y = pad4(np.clip(a[..., 1] * 127.5 + 127.5, 0, 255))
            xb, yb = bc4_blocks(x), bc4_blocks(y)
            levels.append(b"".join(xb[i:i + 8] + yb[i:i + 8] for i in range(0, len(xb), 8)))
        else:
            levels.append(bc4_blocks(pad4(np.clip(a, 0, 255))))
        if a.shape[0] == 1 and a.shape[1] == 1:
            break
        a = box_half(a)
        if label == "normal":
            a = unit(a)
    header = dds_header(size[0], size[1], len(levels), b"ATI2" if label == "normal" else b"ATI1", len(levels[0]))
    tmp = dst.with_suffix(".part")
    tmp.write_bytes(header + b"".join(levels))
    tmp.rename(dst)
    return dst.stat().st_size


def maps_encode(args, rows):
    out = args.work / "maps_dds"
    out.mkdir(parents=True, exist_ok=True)
    items, missing, seen = [], 0, set()
    for s in rows:
        if s["key"] in seen:
            continue
        seen.add(s["key"])
        up = args.work / "upscaled" / f"{s['content_sha1']}.png"
        srcs = {label: args.work / "maps" / f"{s['content_sha1']}_{label}.png" for label in MAP_LABELS}
        if not up.exists() or not all(p.exists() for p in srcs.values()):
            missing += 1
            continue
        w, h = Image.open(up).size
        if max(w, h) > args.max_size:  # as encode(): the pack texture's size
            f = args.max_size / max(w, h)
            w, h = max(1, round(w * f)), max(1, round(h * f))
        for label, src in srcs.items():
            dst = out / f"{s['key']}_{label}.dds"
            if not dst.exists() or dst.stat().st_mtime < src.stat().st_mtime:
                items.append((label, src, dst, (w, h), args.normal_blur))
    with ProcessPoolExecutor(args.jobs) as pool:
        list(pool.map(encode_map, items, chunksize=4))
    total = sum(p.stat().st_size for p in out.glob("*.dds"))
    print(f"maps: {len(items)} map files written, {missing} textures without maps yet; {len(seen) - missing} textures"
          f" x {len(MAP_LABELS)} maps, {total / 2**20:.0f} MB")


def maps_pack(args):
    """The 'surface maps' mod (phase 2.2, its own mod beside the texture pack - user 11-15): WORK/maps_mod/ with
    mod.ini (kind=surface-maps) and maps/<key>_normal.dds (BC5, as maps_dds; hard-linked) and maps/<key>_surface.dds
    (BC5: R = roughness, G = height - the two BC4 files' blocks interleaved, no re-encoding; the game has two texture
    slots for maps)."""
    src = args.work / "maps_dds"
    mod = args.work / "maps_mod"
    out = mod / "maps"
    out.mkdir(parents=True, exist_ok=True)
    (mod / "mod.ini").write_text(f"name={args.name} surface maps\nversion={args.version}\nauthor={args.author}\n"
                                 f"kind=surface-maps\nsource=Call of Duty 2: Big Red One (Xbox) retail dump; maps "
                                 f"from PBRify's map models (CC0)\n")
    keys = sorted({f.name[:16] for f in src.glob("*_normal.dds")})
    written = 0
    for key in keys:
        normal, rough, height = (src / f"{key}_{k}.dds" for k in ("normal", "roughness", "height"))
        if not (rough.exists() and height.exists()):
            continue
        dst = out / f"{key}_normal.dds"
        if not dst.exists():
            try:
                dst.hardlink_to(normal)
            except OSError:
                shutil.copyfile(normal, dst)
        dst = out / f"{key}_surface.dds"
        if dst.exists() and dst.stat().st_mtime >= max(rough.stat().st_mtime, height.stat().st_mtime):
            continue
        r, h = rough.read_bytes(), height.read_bytes()
        if r[:128] != h[:128] or len(r) != len(h):
            print(f"maps-pack: {key}: roughness and height differ in size; skipped")
            continue
        w, hh, levels = struct.unpack_from("<I", r, 16)[0], struct.unpack_from("<I", r, 12)[0], \
            struct.unpack_from("<I", r, 28)[0]
        blocks_r, blocks_h = r[128:], h[128:]
        body = bytearray(len(blocks_r) * 2)
        for i in range(0, len(blocks_r), 8):
            body[2 * i:2 * i + 8] = blocks_r[i:i + 8]
            body[2 * i + 8:2 * i + 16] = blocks_h[i:i + 8]
        top = ((w + 3) // 4) * ((hh + 3) // 4) * 16
        tmp = dst.with_suffix(".part")
        tmp.write_bytes(dds_header(w, hh, levels, b"ATI2", top) + bytes(body))
        tmp.rename(dst)
        written += 1
    total = sum(p.stat().st_size for p in out.glob("*.dds"))
    print(f"maps-pack: {mod} ({len(keys)} textures, {written} surface files written, {total / 2**20:.0f} MB)")


def pack(args):
    rows = read_select(args.work)
    mod = args.work / "mod"
    mod.mkdir(parents=True, exist_ok=True)
    (mod / "mod.ini").write_text(f"name={args.name}\nversion={args.version}\nauthor={args.author}\nkind=textures\n"
                                 f"source=Call of Duty 2: Big Red One (Xbox) retail dump; upscaled with "
                                 f"{Path(args.model).stem} (CC0)\n")
    have = {p.stem for p in (mod / "textures").glob("*.dds")}
    with open(mod / "pack.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["key", "content_sha1", "asset_path", "width", "height", "alpha", "action", "in_pack"])
        for s in rows:
            w.writerow([s["key"], s["content_sha1"], s["asset_path"], s["width"], s["height"], s["alpha"],
                        s["skip"] and f"skip:{s['skip']}" or s["action"], int(s["key"] in have)])
    print(f"pack: {mod} ({len(have)} textures)")
    if args.zip:
        safe = re.sub(r"[^A-Za-z0-9_.-]+", "_", args.name)
        target = args.work / f"{safe}-{args.version}.zip"
        with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
            for p in sorted(mod.rglob("*")):
                if p.is_file():
                    z.write(p, Path(safe) / p.relative_to(mod))
        print(f"zip: {target} ({target.stat().st_size / 2**20:.0f} MB)")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("work", type=Path)
    p.add_argument("--stage", default="all", choices=["all", "select", "upscale", "encode", "pack", "maps",
                                                              "maps-pack"])
    p.add_argument("--unpacked", type=Path, default=ROOT / "unpacked")
    p.add_argument("--manifest", type=Path, default=ROOT / "textures/original/manifest.csv")
    p.add_argument("--source", default="", help="regex on the dump pack path")
    p.add_argument("--bridge", type=Path, nargs="*", default=[])
    p.add_argument("--limit", type=int, default=0)
    p.add_argument("--classes", default="world", help="texture classes to build: world, ui (comma-separated)")
    p.add_argument("--min-size", type=int, default=16)
    p.add_argument("--max-size", type=int, default=4096)
    p.add_argument("--curation", type=Path)
    p.add_argument("--colour-fix", type=float, default=1.0,
                   help="blur radius (original texels) of the low-frequency colour match; -1 = off")
    p.add_argument("--torch-python", type=Path, default=DEFAULT_TORCH)
    p.add_argument("--model", type=Path, default=DEFAULT_MODEL)
    p.add_argument("--tile", type=int, default=128)
    p.add_argument("--batch", type=int, default=4)
    p.add_argument("--pbr", action="store_true", help="also make normal + roughness maps (WORK/upscaled, not packed)")
    p.add_argument("--normal-blur", type=float, default=0.8, help="Gaussian blur of normal maps (PBRify chain: 0.8)")
    p.add_argument("--jobs", type=int, default=os.cpu_count())
    p.add_argument("--progress", type=int, default=50, help="print every Nth upscaled texture")
    p.add_argument("--name", default="PBRify 4x textures")
    p.add_argument("--version", default="0.1")
    p.add_argument("--author", default="cod_bro texture pack builder")
    p.add_argument("--zip", action="store_true")
    args = p.parse_args()
    args.work.mkdir(parents=True, exist_ok=True)
    stages = ["select", "upscale", "encode", "pack"] if args.stage == "all" else [args.stage]
    if "select" in stages and args.stage == "all" and (args.work / "select.csv").exists() and not (
            args.source or args.bridge or args.limit or args.curation):
        stages.remove("select")  # keep an earlier selection when nothing new was asked for
    for stage in stages:
        globals()[stage.replace("-", "_")](args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
