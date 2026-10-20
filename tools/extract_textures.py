#!/usr/bin/env python3
"""Extract the game's textures from the .cod packs (and loose .dds files) for upscaling.

Each DDS is written once per distinct content (the sp/ and mp/ packs share most
textures), named after its original asset path, as PNG (top mip level; what
chaiNNer/PBRify read) and optionally as the original DDS. manifest.csv lists every
occurrence - pack, entry, offset - and the content hash of the bytes after the
128-byte DDS header, which is how the port finds a texture at draw time.

Pack format: see text/kapf.h.

Usage: extract_textures.py <unpacked dir> <output dir> [--dds] [--no-png] [--jobs N]
"""
import argparse
import csv
import hashlib
import io
import os
import pathlib
import struct
import sys
from concurrent.futures import ProcessPoolExecutor

DDS_HEADER = 128


def read_string(data, offset):
    end = data.index(b"\0", offset)
    return data[offset:end].decode("latin-1")


def pack_entries(path):
    """Yields (name, asset path, offset, size) for every file in a KAPF pack."""
    data = path.read_bytes()
    if data[:4] != b"KAPF":
        print(f"warning: {path} is not a game pack", file=sys.stderr)
        return
    sections = struct.unpack_from("<I", data, 0x0C)[0]
    for i in range(sections):
        _, count, chunks, directory = struct.unpack_from("<HHII", data, 0x28 + i * 24)
        for e in range(count):
            name_at, path_at, size, chunk, offset = struct.unpack_from("<5I", data, directory + e * 20)
            chunk_offset = struct.unpack_from("<I", data, chunks + chunk * 28)[0]
            yield read_string(data, name_at), read_string(data, path_at), chunk_offset + offset, size


def output_stem(asset_path, name):
    """Relative output path without extension, from the original asset path.

    "E:\\asset_temp\\assets\\textures\\foo.dds" -> "assets/textures/foo".
    """
    parts = [p for p in asset_path.replace("\\", "/").split("/") if p]
    if parts and parts[0].endswith(":"):
        parts = parts[1:]
    if parts and parts[0].lower() == "asset_temp":
        parts = parts[1:]
    if not parts:
        parts = [name]
    parts[-1] = os.path.splitext(parts[-1])[0]
    return "/".join(p.replace(":", "_") for p in parts)


def dds_format(data):
    flags = struct.unpack_from("<I", data, 80)[0]
    if flags & 0x4:
        return data[84:88].decode("latin-1")
    return "RGB%d" % struct.unpack_from("<I", data, 88)[0]


def write_texture(job):
    """Writes one texture; returns (stem, error or "")."""
    stem, data, out_dir, png, dds = job
    target = out_dir / stem
    target.parent.mkdir(parents=True, exist_ok=True)
    if dds:
        target.with_suffix(".dds").write_bytes(data)
    if not png:
        return stem, ""
    try:
        from PIL import Image

        with Image.open(io.BytesIO(data)) as image:
            image.load()
            if image.mode not in ("RGB", "RGBA", "L", "LA"):
                image = image.convert("RGBA")
            image.save(target.with_suffix(".png"))
    except Exception as error:  # an odd format: keep the DDS so nothing is lost
        target.with_suffix(".dds").write_bytes(data)
        return stem, f"{type(error).__name__}: {error}"
    return stem, ""


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("unpacked", type=pathlib.Path, help="the game's unpacked folder")
    parser.add_argument("output", type=pathlib.Path)
    parser.add_argument("--dds", action="store_true", help="also write the original DDS files")
    parser.add_argument("--no-png", action="store_true", help="write DDS only")
    parser.add_argument("--jobs", type=int, default=os.cpu_count())
    args = parser.parse_args()

    def occurrences():
        """Yields (source, entry, asset path, offset, size, data) for every DDS."""
        for pack in sorted(args.unpacked.rglob("*")):
            if pack.suffix.lower() == ".cod":
                data = pack.read_bytes()
                for name, asset_path, offset, size in pack_entries(pack):
                    payload = data[offset:offset + size]
                    if payload[:4] == b"DDS ":
                        yield pack, name, asset_path, offset, size, payload
            elif pack.suffix.lower() == ".dds":
                payload = pack.read_bytes()
                if payload[:4] == b"DDS ":
                    relative = pack.relative_to(args.unpacked).as_posix()
                    yield pack, pack.name, "loose/" + relative, 0, len(payload), payload

    # One output per distinct content; a different texture under a taken name gets its
    # hash appended.
    by_hash, stems, jobs, rows = {}, {}, [], []
    for source, name, asset_path, offset, size, payload in occurrences():
        content = hashlib.sha1(payload[DDS_HEADER:]).hexdigest()
        if content not in by_hash:
            stem = output_stem(asset_path, name)
            if stem.lower() in stems:
                stem += "__" + content[:8]
            stems[stem.lower()] = content
            by_hash[content] = stem
            jobs.append((stem, payload, args.output, not args.no_png, args.dds or args.no_png))
        rows.append([source.relative_to(args.unpacked).as_posix(), name, asset_path, offset, size,
                     dds_format(payload), content, by_hash[content]])

    args.output.mkdir(parents=True, exist_ok=True)
    failed = []
    with ProcessPoolExecutor(max_workers=args.jobs) as pool:
        for stem, error in pool.map(write_texture, jobs, chunksize=16):
            if error:
                failed.append((stem, error))

    with open(args.output / "manifest.csv", "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["source", "entry", "asset_path", "offset", "size", "format", "content_sha1", "output"])
        writer.writerows(rows)

    print(f"{len(rows)} DDS occurrences, {len(jobs)} distinct textures -> {args.output}")
    for stem, error in failed:
        print(f"  not converted (DDS kept): {stem}: {error}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
