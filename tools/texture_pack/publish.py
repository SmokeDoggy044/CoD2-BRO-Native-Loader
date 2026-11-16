#!/usr/bin/env python3
"""Texture pack release files: built mod folders -> an index + data parts (zstd, then ChaCha20 per texture).

usage: tools/texture_pack/publish.py OUT --pack ID=MODDIR [--pack ...] [--release 0.1] [--stage publish|check|install]
                                     [--install-to MODS] [--unpacked DIR] [--manifest CSV] [--jobs N]

  publish   OUT/cod_bro_textures.index, OUT/cod_bro_textures.data.001 ... (each under --part-limit bytes, a file
            never crosses a part), OUT/SHA256SUMS.txt. Packs are laid out in the order given.
  check     every part's size + SHA-256, every stored file's SHA-256, then each file decrypted with the key from the
            dump, decompressed and compared byte for byte with the mod folder's DDS.
  install   the installed form the game and the launcher use: MODS/<id>/mod.ini + pack.idx + pack.dat (encrypted as
            stored). Developers' cross-check of the launcher's install (same bytes).

Keys (docs/developers/texture-packs.md): key = SHA-256("cod_bro texture key v1" || the original texture's bytes as
the title holds them) - the bytes whose render::hash_bytes is the file id (texture_bridge.py: DXT data as in the
DDS, uncompressed power-of-two textures Morton-swizzled). Surface maps use their colour texture's key; the nonce
(12 bytes: id little-endian, then the kind: 0 texture, 1 normal, 2 surface) keeps the streams apart. ChaCha20 with
a block counter starting at 0. OUT/keys.csv caches id -> key (game-derived: never published).

Index (text, one record per line, the same grammar as an installed pack.idx):
  cod_bro_textures 1
  release <version>
  part <name> <size> <sha256>                      (1-based numbers in file lines, in this order)
  pack <id>                                        (then its mod.ini lines and files)
  ini <mod.ini line>
  file <id> <t|n|s> <part> <offset> <stored size> <size> <sha256 of the stored bytes>
"""
import argparse
import csv
import hashlib
import os
import struct
import sys
from concurrent.futures import ProcessPoolExecutor
from pathlib import Path

import numpy as np
import zstandard
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms

TOOLS = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(TOOLS))
from texture_bridge import hash_bytes  # noqa: E402

ROOT = TOOLS.parent.parent  # cod_bro/
KEY_PREFIX = b"cod_bro texture key v1"
INDEX = "cod_bro_textures.index"
DATA = "cod_bro_textures.data.{:03d}"
KINDS = {"t": 0, "n": 1, "s": 2}
DDS_HEADER = 128
ZSTD_LEVEL = 19


# --- keys from the dump ---

def swizzle_np(level, width, height, texel):
    bits_w, bits_h = (width - 1).bit_length(), (height - 1).bit_length()
    xs, ys = np.zeros(width, np.int64), np.zeros(height, np.int64)
    bit, xb, yb = 0, 0, 0
    while xb < bits_w or yb < bits_h:
        if xb < bits_w:
            xs |= ((np.arange(width) >> xb) & 1) << bit
            bit += 1
            xb += 1
        if yb < bits_h:
            ys |= ((np.arange(height) >> yb) & 1) << bit
            bit += 1
            yb += 1
    dest = (ys[:, None] + xs[None, :]).ravel()
    src = np.frombuffer(level, np.uint8).reshape(width * height, texel)
    out = np.empty_like(src)
    out[dest] = src
    return out.tobytes()


def title_bytes(payload):
    """What the title holds for this DDS (texture_bridge.candidates' last candidate)."""
    height, width = struct.unpack_from("<II", payload, 12)
    levels = max(1, struct.unpack_from("<I", payload, 28)[0])
    flags = struct.unpack_from("<I", payload, 80)[0]
    caps2 = struct.unpack_from("<I", payload, 112)[0]
    data = payload[DDS_HEADER:]
    if flags & 0x4 or caps2 & 0x200:
        return [data]
    bpp = struct.unpack_from("<I", payload, 88)[0] // 8
    if bpp not in (2, 4) or width & (width - 1) or height & (height - 1):
        return [data]
    pieces, offset, w, h = [], 0, width, height
    for _ in range(levels):
        n = w * h * bpp
        pieces.append(swizzle_np(data[offset:offset + n], w, h, bpp))
        offset += n
        w, h = max(1, w // 2), max(1, h // 2)
    return [data, b"".join(pieces)]


def key_job(item):
    path, offset, size = item
    with open(path, "rb") as f:
        f.seek(offset)
        payload = f.read(size)
    return [(f"{hash_bytes(b):016x}", hashlib.sha256(KEY_PREFIX + b).hexdigest()) for b in title_bytes(payload)]


def load_keys(args, wanted):
    cache = args.out / "keys.csv"
    keys = {}
    if cache.exists():
        with open(cache, newline="") as f:
            keys = {r["id"]: r["key"] for r in csv.DictReader(f)}
    if wanted <= keys.keys():
        return keys
    items, seen = [], set()
    with open(args.manifest, newline="") as f:
        for r in csv.DictReader(f):
            if r["content_sha1"] in seen:
                continue
            seen.add(r["content_sha1"])
            items.append((args.unpacked / r["source"], int(r["offset"]), int(r["size"])))
    print(f"keys: {len(items)} dump textures", flush=True)
    with ProcessPoolExecutor(args.jobs) as pool:
        for found in pool.map(key_job, items, chunksize=8):
            for i, k in found:
                keys.setdefault(i, k)
    with open(cache, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["id", "key"])
        for i in sorted(keys):
            w.writerow([i, keys[i]])
    return keys


# --- stored form ---

def nonce(file_id, kind):
    return struct.pack("<QI", int(file_id, 16), KINDS[kind])


def chacha(key_hex, file_id, kind, data):
    # cryptography's ChaCha20 nonce = 32-bit block counter (little-endian) + the 96-bit nonce
    c = Cipher(algorithms.ChaCha20(bytes.fromhex(key_hex), b"\0\0\0\0" + nonce(file_id, kind)), mode=None)
    return c.encryptor().update(data)


def store_job(item):
    path, file_id, kind, key = item
    raw = Path(path).read_bytes()
    stored = chacha(key, file_id, kind, zstandard.ZstdCompressor(level=ZSTD_LEVEL).compress(raw))
    return stored, len(raw), hashlib.sha256(stored).hexdigest()


def pack_files(mod):
    """(id, kind, path) of a built mod folder: textures/<id>.dds, maps/<id>_normal.dds, maps/<id>_surface.dds."""
    out = []
    for p in sorted((mod / "textures").glob("*.dds")):
        out.append((p.stem, "t", p))
    for p in sorted((mod / "maps").glob("*.dds")):
        file_id, _, kind = p.stem.partition("_")
        if kind in ("normal", "surface"):
            out.append((file_id, kind[0], p))
    return out


def parse_packs(args):
    packs = []
    for spec in args.pack:
        pid, _, folder = spec.partition("=")
        packs.append((pid, Path(folder)))
    return packs


def publish(args):
    packs = parse_packs(args)
    listing = {pid: pack_files(mod) for pid, mod in packs}
    keys = load_keys(args, {i for files in listing.values() for i, _, _ in files})
    missing = [(pid, i) for pid, files in listing.items() for i, _, _ in files if i not in keys]
    if missing:
        sys.exit(f"no dump texture for {len(missing)} files, e.g. {missing[:3]}")
    for old in args.out.glob("cod_bro_textures.data.*"):
        old.unlink()
    parts, lines = [], []
    part_file, part_size = None, 0

    def next_part():
        nonlocal part_file, part_size
        if part_file:
            part_file.close()
        parts.append(args.out / DATA.format(len(parts) + 1))
        part_file, part_size = open(parts[-1], "wb"), 0

    next_part()
    with ProcessPoolExecutor(args.jobs) as pool:
        for pid, mod in packs:
            files = listing[pid]
            lines.append(f"pack {pid}")
            for ini in (mod / "mod.ini").read_text().splitlines():
                if ini.strip():
                    lines.append(f"ini {ini.strip()}")
            jobs = [(str(p), i, k, keys[i]) for i, k, p in files]
            stored_total = 0
            for n, ((i, k, _), (stored, size, digest)) in enumerate(zip(files, pool.map(store_job, jobs, chunksize=4))):
                if part_size and part_size + len(stored) > args.part_limit:
                    next_part()
                lines.append(f"file {i} {k} {len(parts)} {part_size} {len(stored)} {size} {digest}")
                part_file.write(stored)
                part_size += len(stored)
                stored_total += len(stored)
                if (n + 1) % 500 == 0:
                    print(f"  {pid}: {n + 1}/{len(files)}", flush=True)
            print(f"{pid}: {len(files)} files, {stored_total / 2**20:.0f} MB stored", flush=True)
    part_file.close()
    head = ["cod_bro_textures 1", f"release {args.release}"]
    sums = []
    for p in parts:
        digest = sha256_file(p)
        head.append(f"part {p.name} {p.stat().st_size} {digest}")
        sums.append(f"{digest}  {p.name}")
    index = args.out / INDEX
    index.write_text("\n".join(head + lines) + "\n")
    sums.insert(0, f"{sha256_file(index)}  {INDEX}")
    (args.out / "SHA256SUMS.txt").write_text("\n".join(sums) + "\n")
    print(f"publish: {len(parts)} parts, index {index.stat().st_size / 1024:.0f} KB")


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(1 << 24):
            h.update(chunk)
    return h.hexdigest()


# --- reading an index ---

def read_index(path):
    idx = {"parts": [], "packs": []}
    for line in Path(path).read_text().splitlines():
        word, _, rest = line.partition(" ")
        if word == "cod_bro_textures":
            if rest != "1":
                sys.exit(f"{path}: format {rest}")
        elif word == "release":
            idx["release"] = rest
        elif word == "part":
            name, size, digest = rest.split()
            idx["parts"].append((name, int(size), digest))
        elif word == "pack":
            idx["packs"].append({"id": rest, "ini": [], "files": []})
        elif word == "ini":
            idx["packs"][-1]["ini"].append(rest)
        elif word == "file":
            i, k, part, off, stored, size, digest = rest.split()
            idx["packs"][-1]["files"].append((i, k, int(part), int(off), int(stored), int(size), digest))
    return idx


def check_job(item):
    part, off, stored, size, digest, key, i, k, source = item
    with open(part, "rb") as f:
        f.seek(off)
        data = f.read(stored)
    if hashlib.sha256(data).hexdigest() != digest:
        return f"{i} {k}: stored SHA-256 differs"
    try:
        raw = zstandard.ZstdDecompressor().decompress(chacha(key, i, k, data), max_output_size=size)
    except zstandard.ZstdError as e:
        return f"{i} {k}: does not decompress with the dump's key ({e})"
    if len(raw) != size or raw != Path(source).read_bytes():
        return f"{i} {k}: not the mod folder's file"
    return ""


def check(args):
    packs = dict(parse_packs(args))
    idx = read_index(args.out / INDEX)
    bad = 0
    for name, size, digest in idx["parts"]:
        p = args.out / name
        ok = p.stat().st_size == size and sha256_file(p) == digest
        print(f"part {name}: {'ok' if ok else 'DIFFERS'}", flush=True)
        bad += not ok
    keys = load_keys(args, {f[0] for pk in idx["packs"] for f in pk["files"]})
    for pk in idx["packs"]:
        mod = packs[pk["id"]]
        expected = {(i, k): p for i, k, p in pack_files(mod)}
        jobs = []
        for i, k, part, off, stored, size, digest in pk["files"]:
            jobs.append((args.out / idx["parts"][part - 1][0], off, stored, size, digest, keys[i], i, k,
                         expected.pop((i, k), "")))
        with ProcessPoolExecutor(args.jobs) as pool:
            errors = [e for e in pool.map(check_job, jobs, chunksize=8) if e]
        errors += [f"{i} {k}: in the mod folder, not in the index" for (i, k) in expected]
        for e in errors[:10]:
            print("  " + e)
        print(f"{pk['id']}: {len(jobs)} files, {'all equal' if not errors else f'{len(errors)} errors'}", flush=True)
        bad += len(errors)
    print("check: OK" if not bad else f"check: {bad} problems")
    return 1 if bad else 0


def install(args):
    idx = read_index(args.out / INDEX)
    for pk in idx["packs"]:
        target = args.install_to / pk["id"]
        target.mkdir(parents=True, exist_ok=True)
        (target / "mod.ini").write_text("\n".join(pk["ini"]) + "\n")
        lines, offset = [], 0
        with open(target / "pack.dat", "wb") as out:
            for i, k, part, off, stored, size, digest in pk["files"]:
                with open(args.out / idx["parts"][part - 1][0], "rb") as f:
                    f.seek(off)
                    out.write(f.read(stored))
                lines.append(f"file {i} {k} 1 {offset} {stored} {size} {digest}")
                offset += stored
        head = ["cod_bro_textures 1", f"release {idx['release']}",
                f"part pack.dat {offset} {sha256_file(target / 'pack.dat')}", f"pack {pk['id']}"]
        (target / "pack.idx").write_text("\n".join(head + [f"ini {x}" for x in pk["ini"]] + lines) + "\n")
        print(f"install: {target} ({len(lines)} files, {offset / 2**20:.0f} MB)")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("out", type=Path)
    p.add_argument("--pack", action="append", default=[], help="ID=MODDIR (world, ui, surface_maps), in order")
    p.add_argument("--release", default="0.1")
    p.add_argument("--stage", default="publish", choices=["publish", "check", "install"])
    p.add_argument("--install-to", type=Path)
    p.add_argument("--part-limit", type=int, default=1900 * 2**20)
    p.add_argument("--unpacked", type=Path, default=ROOT / "unpacked")
    p.add_argument("--manifest", type=Path, default=ROOT / "textures/original/manifest.csv")
    p.add_argument("--jobs", type=int, default=os.cpu_count())
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    if args.stage == "publish":
        publish(args)
    elif args.stage == "check":
        return check(args)
    else:
        if not args.install_to:
            sys.exit("--install-to MODS needed")
        install(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
