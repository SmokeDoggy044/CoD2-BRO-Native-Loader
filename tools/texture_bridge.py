#!/usr/bin/env python3
"""Phase 1a: maps the runtime texture hashes (COD_BRO_TEXTURE_LOG) to the dump's DDS files (manifest.csv).

usage: tools/texture_bridge.py UNPACKED MANIFEST LOG [LOG ...] [--out bridge.csv] [--unmatched N]

The runtime hash (render::hash_bytes, src/render/texture_hash.h) is over the texture's memory as the title holds
it: all levels in Xbox layout. For each distinct DDS of the dump this computes the same hash of what the title
would hold (DXT: the DDS data as is; A8R8G8B8: each level swizzled into Morton order) and matches it to the logged
hashes. Prints how many logged textures matched; writes content -> asset path rows (bridge.csv) for the pack builder.
"""
import argparse
import csv
import struct
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

MASK = (1 << 64) - 1
DDS_HEADER = 128


def hash_bytes(data, seed=0xCBF29CE484222325):
    size = len(data)
    h = (seed ^ (size * 0x9E3779B97F4A7C15)) & MASK
    whole = size - size % 8
    for (v,) in struct.iter_unpack("<Q", data[:whole]):
        h = ((h ^ v) * 0x100000001B3) & MASK
        h ^= h >> 29
    for b in data[whole:]:
        h = ((h ^ b) * 0x100000001B3) & MASK
    return h


def morton_swizzle(level, width, height, texel):
    """A linear level (rows of width texels) in the Xbox's swizzled order."""
    out = bytearray(len(level))
    bits_w, bits_h = (width - 1).bit_length(), (height - 1).bit_length()
    for y in range(height):
        for x in range(width):
            i, bit, xb, yb = 0, 0, 0, 0
            while xb < bits_w or yb < bits_h:
                if xb < bits_w:
                    i |= ((x >> xb) & 1) << bit
                    bit += 1
                    xb += 1
                if yb < bits_h:
                    i |= ((y >> yb) & 1) << bit
                    bit += 1
                    yb += 1
            s = (y * width + x) * texel
            out[i * texel:(i + 1) * texel] = level[s:s + texel]
    return bytes(out)


def candidates(payload):
    """(label, hash) of what the title may hold for this DDS."""
    height, width = struct.unpack_from("<II", payload, 12)
    levels = max(1, struct.unpack_from("<I", payload, 28)[0])
    flags = struct.unpack_from("<I", payload, 80)[0]
    caps2 = struct.unpack_from("<I", payload, 112)[0]
    data = payload[DDS_HEADER:]
    out = [("dds", hash_bytes(data))]
    if flags & 0x4 or caps2 & 0x200:  # FourCC (DXT) or cube: as is
        return out
    bpp = struct.unpack_from("<I", payload, 88)[0] // 8
    if bpp not in (2, 4) or width & (width - 1) or height & (height - 1):
        return out
    pieces, offset, w, h = [], 0, width, height
    for _ in range(levels):
        n = w * h * bpp
        pieces.append(morton_swizzle(data[offset:offset + n], w, h, bpp))
        offset += n
        w, h = max(1, w // 2), max(1, h // 2)
    out.append(("swizzled", hash_bytes(b"".join(pieces))))
    return out


def job(item):
    path, offset, size, sha1 = item
    with open(path, "rb") as f:
        f.seek(offset)
        payload = f.read(size)
    return sha1, candidates(payload)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("unpacked", type=Path)
    p.add_argument("manifest", type=Path)
    p.add_argument("logs", type=Path, nargs="+")
    p.add_argument("--out", type=Path, default=Path("bridge.csv"))
    p.add_argument("--unmatched", type=int, default=12, help="list this many unmatched logged textures")
    args = p.parse_args()

    by_sha1 = {}
    with open(args.manifest, newline="") as f:
        for r in csv.DictReader(f):
            by_sha1.setdefault(r["content_sha1"], r)
    items = [(args.unpacked / r["source"], int(r["offset"]), int(r["size"]), sha1) for sha1, r in by_sha1.items()]
    known = {}
    with ProcessPoolExecutor() as pool:
        for sha1, cands in pool.map(job, items, chunksize=8):
            for label, h in cands:
                known.setdefault(h, (sha1, label))

    logged = {}
    for log in args.logs:
        with open(log, newline="") as f:
            for r in csv.DictReader(f):
                logged[int(r["content"], 16)] = r
    matched = {h: known[h] for h in logged if h in known}
    by_kind = {}
    for h, r in logged.items():
        key = ("palette" if r["palette"] == "1" else "linear" if r["linear"] == "1" else
               "cube" if r.get("cube") == "1" else ["argb", "dxt1", "dxt3", "dxt5", "?"][int(r["kind"])])
        by_kind.setdefault(key, [0, 0])[0] += 1
        by_kind[key][1] += h in matched
    print(f"{len(logged)} logged textures, {len(matched)} matched to {len(by_sha1)} distinct dump textures")
    for key, (n, m) in sorted(by_kind.items()):
        print(f"  {key:8} {m:5} of {n:5}")
    with open(args.out, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["content", "content_sha1", "how", "asset_path", "width", "height", "levels", "format"])
        for h, (sha1, how) in sorted(matched.items(), key=lambda kv: by_sha1[kv[1][0]]["asset_path"]):
            r = logged[h]
            w.writerow([f"{h:016x}", sha1, how, by_sha1[sha1]["asset_path"], r["width"], r["height"], r["levels"],
                        by_sha1[sha1]["format"]])
    shown = 0
    for h, r in logged.items():
        if h in matched or shown >= args.unmatched:
            continue
        shown += 1
        print(f"  unmatched {h:016x} kind {r['kind']} {r['width']}x{r['height']} levels {r['levels']} size {r['size']}"
              f" linear {r['linear']} palette {r['palette']}")
    print(f"bridge: {args.out}")


if __name__ == "__main__":
    sys.exit(main())
