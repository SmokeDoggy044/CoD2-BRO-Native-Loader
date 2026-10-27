#!/usr/bin/env python3
"""Show or edit CoD BRO profiles (the Profiles, Profiles1-3 save files) and re-sign them.

Each file is 16 KB: the slot record (version "9-21-05" at 0, then the active profile block the
game copies from 0x008dce30, so file offset = address - 0x008dce30 + 0x1c) and the save
signature in the last 0x20 bytes (20 used): HMAC-SHA1 over everything before it, keyed with the
kernel's XboxSignatureKey - 16 zero bytes in the native loader (a zero-filled kernel export).
Saves from a real console use that console's key and are not handled here.

usage: tools/profile_edit.py [SAVE_DIR]                  list the profiles
       tools/profile_edit.py [SAVE_DIR] --unlock-all FILE  every chapter and bonus (as the
                                                          chapter-select cheat does)
       tools/profile_edit.py [SAVE_DIR] --chapters N FILE  chapters unlocked (-1 none .. 14)
SAVE_DIR defaults to ~/.local/share/cod_bro; FILE is Profiles, Profiles1, ... Every write keeps
a copy of the old file in SAVE_DIR/profile_backups/ and checks the new signature.
"""

import argparse
import hashlib
import hmac
import struct
import sys
import time
from pathlib import Path

SIZE = 0x4000
SIGNED = SIZE - 0x20
KEY = bytes(16)  # XboxSignatureKey as the loader exports it

NAME = 0x1c                      # 0x008dce30, the profile name
CHAPTERS = 0x30                  # 0x008dce44 g_chaptersUnlocked (-1 none; the menu caps it at 13)
CURRENT = 0x34                   # 0x008dce48 g_currentChapter
ALL_UNLOCKED = 0x40              # 0x008dce54, every bonus regardless of the chapters
PLAY_TIME = (0x44, 0x48, 0x4c, 0x50)  # seconds, minutes, hours, days
PLAY_CHECK = (0xfc, 0x100)       # 0x008dcf10 = 1, 0x008dcf14 = the play time sum % 50


def signature(data):
    return hmac.new(KEY, data[:SIGNED], hashlib.sha1).digest()


def word(data, offset):
    return struct.unpack_from("<i", data, offset)[0]


def describe(path):
    data = path.read_bytes()
    if len(data) != SIZE:
        return f"{path.name}: {len(data)} bytes, not a profile file"
    name = data[NAME:NAME + 0x10].split(b"\0")[0].decode("latin-1")
    signed = "signed" if data[SIGNED:SIGNED + 20] == signature(data) else "BAD SIGNATURE"
    play = [word(data, o) for o in PLAY_TIME]
    return (f"{path.name}: name {name!r}, chapters unlocked {word(data, CHAPTERS)}, current "
            f"{word(data, CURRENT)}, all unlocked {data[ALL_UNLOCKED]}, play time "
            f"{play[3]}d {play[2]}h {play[1]}m {play[0]}s, {signed}")


def write(save_dir, path, data):
    data[SIGNED:SIGNED + 20] = signature(data)
    backups = Path(save_dir) / "profile_backups"  # outside E/ and Z/, so the game never lists it
    backups.mkdir(exist_ok=True)
    backup = backups / f"{path.name}-{time.strftime('%Y%m%d-%H%M%S')}"
    backup.write_bytes(path.read_bytes())
    path.write_bytes(data)
    print(f"backup: {backup}")
    print(describe(path))


def main():
    parser = argparse.ArgumentParser(usage=__doc__.split("usage: ", 1)[1].split("\n\n")[0])
    parser.add_argument("save_dir", nargs="?", default=str(Path.home() / ".local/share/cod_bro"))
    parser.add_argument("--unlock-all", metavar="FILE")
    parser.add_argument("--chapters", nargs=2, metavar=("N", "FILE"))
    args = parser.parse_args()

    files = sorted(Path(args.save_dir).glob("E/UDATA/41560051/*/Profiles*"))
    if not files:
        sys.exit(f"no Profiles files under {args.save_dir}")
    by_name = {f.name: f for f in files}

    target = args.unlock_all or (args.chapters and args.chapters[1])
    if not target:
        for f in files:
            print(describe(f))
        return
    if target not in by_name:
        sys.exit(f"{target}: not one of {', '.join(by_name)}")
    path = by_name[target]
    data = bytearray(path.read_bytes())
    if data[SIGNED:SIGNED + 20] != signature(data):
        sys.exit(f"{path}: signature does not match the loader's key; not editing")
    if args.unlock_all:
        struct.pack_into("<i", data, CHAPTERS, 14)
        data[ALL_UNLOCKED] = 1
    else:
        chapters = int(args.chapters[0])
        if not -1 <= chapters <= 14:
            sys.exit("chapters: -1 to 14")
        struct.pack_into("<i", data, CHAPTERS, chapters)
    write(args.save_dir, path, data)


if __name__ == "__main__":
    main()
