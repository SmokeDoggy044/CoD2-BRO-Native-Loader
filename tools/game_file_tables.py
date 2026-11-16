#!/usr/bin/env python3
"""The launcher's game-file tables (src/launcher/game_files.txt): every file of a known edition of the game with
its size and SHA-1, from a disc image (XISO or a full image) or a folder of extracted files. Names, sizes and
checksums only - no game data.

usage: tools/game_file_tables.py OUT EDITION=SOURCE [EDITION=SOURCE ...]
  EDITION: an id (standard, collectors) and a name after a colon: standard:"Standard edition"
  SOURCE:  an image file or a folder

OUT format (src/launcher/disc_image.h reads it):
  edition <id> <sha1 of default.xbe> <name>
  <sha1> <size> <path with forward slashes>          (that edition's files, sorted by path)
"""
import hashlib
import os
import struct
import sys
from pathlib import Path

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
PARTITIONS = [0, 0x18300000, 0xFD90000, 0x2080000]  # XISO, XGD1, XGD2, XGD3 game partition offsets


def image_files(path):
    """(path, size, read(offset, n) source) of every file in an Xbox disc image."""
    f = open(path, "rb")
    base = None
    for start in PARTITIONS:
        f.seek(start + 32 * SECTOR)
        if f.read(20) == MAGIC:
            base = start
            break
    if base is None:
        sys.exit(f"{path}: no Xbox file system found")
    f.seek(base + 32 * SECTOR + 20)
    root_sector, root_size = struct.unpack("<II", f.read(8))
    out = []

    def walk(sector, size, prefix):
        if not size:
            return
        f.seek(base + sector * SECTOR)
        table = f.read(size)
        stack = [0]
        seen = set()
        while stack:
            off = stack.pop()
            if off in seen or off + 14 > len(table):
                continue
            seen.add(off)
            left, right, start, length, attr, nlen = struct.unpack_from("<HHIIBB", table, off)
            if left == 0xFFFF:
                continue
            name = table[off + 14:off + 14 + nlen].decode("latin-1")
            if left:
                stack.append(left * 4)
            if right:
                stack.append(right * 4)
            if attr & 0x10:
                walk(start, length, prefix + name + "/")
            else:
                out.append((prefix + name, length, base + start * SECTOR))
    walk(root_sector, root_size, "")
    return f, out


def sha1_range(f, offset, size):
    h = hashlib.sha1()
    f.seek(offset)
    while size:
        chunk = f.read(min(size, 1 << 22))
        h.update(chunk)
        size -= len(chunk)
    return h.hexdigest()


def rows_of(source):
    source = Path(source)
    if source.is_dir():
        rows = []
        for p in sorted(source.rglob("*")):
            if p.is_file():
                rel = p.relative_to(source).as_posix()
                rows.append((hashlib.sha1(p.read_bytes()).hexdigest(), p.stat().st_size, rel))
        return rows
    f, files = image_files(source)
    return [(sha1_range(f, off, size), size, name) for name, size, off in files]


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    lines = ["# Game files per edition: SHA-1, size, path. Made by tools/game_file_tables.py; names, sizes and",
             "# checksums only."]
    for spec in sys.argv[2:]:
        edition, _, source = spec.partition("=")
        eid, _, name = edition.partition(":")
        rows = sorted(rows_of(source), key=lambda r: r[2].lower())
        xbe = next(r for r in rows if r[2].lower() == "default.xbe")
        lines.append(f"edition {eid} {xbe[0]} {name or eid}")
        lines += [f"{sha} {size} {path}" for sha, size, path in rows]
        print(f"{eid}: {len(rows)} files, {sum(r[1] for r in rows) / 2**30:.2f} GiB, default.xbe {xbe[0][:8]}")
    Path(sys.argv[1]).write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
