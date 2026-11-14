#!/usr/bin/env python3
"""A test texture pack (phase 1b checks): dump textures from a bridge.csv, enlarged and tinted, as a mod folder.

usage: tools/test_mod.py UNPACKED MANIFEST BRIDGE_CSV OUT_MOD_DIR [--scale 2] [--tint 1.0,0.6,1.0] [--limit N]

Writes OUT_MOD_DIR/mod.ini and textures/<content>.dds (DXT1 when the original is DXT1 and opaque, else DXT5, full
mip chain). Not a real pack: only to see that pack pictures load, show and match the right textures.
"""
import argparse
import csv
import io
import struct
from pathlib import Path

from PIL import Image

DDS_HEADER = 128


def dds_bytes(image, fmt):
    """One level as DXT blocks (Pillow's encoder; the header dropped)."""
    buf = io.BytesIO()
    image.save(buf, format="DDS", pixel_format=fmt)
    return buf.getvalue()[DDS_HEADER:]


def write_dds(path, image, fmt):
    levels, level = [], image
    while True:
        levels.append(dds_bytes(level, fmt))
        if level.width == 1 and level.height == 1:
            break
        level = level.resize((max(1, level.width // 2), max(1, level.height // 2)), Image.BOX)
    buf = io.BytesIO()
    image.save(buf, format="DDS", pixel_format=fmt)
    header = bytearray(buf.getvalue()[:DDS_HEADER])
    struct.pack_into("<I", header, 8, struct.unpack_from("<I", header, 8)[0] | 0x20000)  # DDSD_MIPMAPCOUNT
    struct.pack_into("<I", header, 28, len(levels))
    struct.pack_into("<I", header, 108, struct.unpack_from("<I", header, 108)[0] | 0x400008)  # complex | mipmap
    path.write_bytes(bytes(header) + b"".join(levels))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("unpacked", type=Path)
    p.add_argument("manifest", type=Path)
    p.add_argument("bridge", type=Path)
    p.add_argument("out", type=Path)
    p.add_argument("--scale", type=int, default=2)
    p.add_argument("--tint", default="1.0,0.6,1.0")
    p.add_argument("--limit", type=int, default=0)
    args = p.parse_args()
    tint = [float(x) for x in args.tint.split(",")]

    rows = {}
    with open(args.manifest, newline="") as f:
        for r in csv.DictReader(f):
            rows.setdefault(r["content_sha1"], r)
    (args.out / "textures").mkdir(parents=True, exist_ok=True)
    (args.out / "mod.ini").write_text("name=Test pack (tinted)\nversion=0\nauthor=tools/test_mod.py\nkind=textures\n")
    done = 0
    with open(args.bridge, newline="") as f:
        for b in csv.DictReader(f):
            r = rows[b["content_sha1"]]
            with open(args.unpacked / r["source"], "rb") as src:
                src.seek(int(r["offset"]))
                payload = src.read(int(r["size"]))
            try:
                image = Image.open(io.BytesIO(payload))
                image.load()
            except Exception as error:
                print(f"skipped {b['asset_path']}: {error}")
                continue
            image = image.convert("RGBA")
            image = image.resize((image.width * args.scale, image.height * args.scale), Image.BICUBIC)
            channels = list(image.split())
            for i in range(3):
                channels[i] = channels[i].point(lambda v, t=tint[i]: min(255, int(v * t)))
            image = Image.merge("RGBA", channels)
            opaque = r["format"] == "DXT1" and image.getextrema()[3][0] == 255
            write_dds(args.out / "textures" / f"{b['content']}.dds", image if not opaque else image.convert("RGB"),
                      "DXT1" if opaque else "DXT5")
            done += 1
            if args.limit and done >= args.limit:
                break
    print(f"{done} textures in {args.out}")


if __name__ == "__main__":
    main()
