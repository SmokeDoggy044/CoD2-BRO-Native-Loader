#!/usr/bin/env python3
"""Prints a recording's frame count and the mission it started (--level from its recorded command line, '-' if none).

usage: tools/replay_info.py RECORDING   ->  "<frames> <mission>"
"""
import struct
import sys

d = open(sys.argv[1], "rb").read()
if d[:8] != b"XBREPLAY":
    sys.exit(f"{sys.argv[1]}: not a recording")
i, frames, mission = 12, 0, "-"
while i + 3 <= len(d):
    tag, length = d[i], struct.unpack_from("<H", d, i + 1)[0]
    if tag == ord("F"):
        frames += 1
    elif tag == ord("A") and frames == 0:
        args = d[i + 3:i + 3 + length].decode(errors="replace").split("\n")
        if "--level" in args and args.index("--level") + 1 < len(args):
            mission = args[args.index("--level") + 1]
    i += 3 + length
print(frames, mission)
