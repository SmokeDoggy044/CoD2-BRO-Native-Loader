#!/usr/bin/env python3
"""Compares two game-state dumps from the loader's --replay-dump (record/replay developer mode).

usage: tools/replay_diff.py DIR_A DIR_B FRAME [--limit N]

Each DIR holds frame_<FRAME>_level.bin, _clients.bin and _entities.bin (u32 index, u32 address, 0x460 bytes per
entity). Prints the differing 32-bit words: level locals offset, client number + offset, entity index + offset
(with each side's value as hex and float), and entities that exist on one side only.
"""
import argparse
import struct
from pathlib import Path

ENTITY_SIZE = 0x460
CLIENT_SIZE = 0x640


def words(data):
    return struct.unpack(f"<{len(data) // 4}I", data[: len(data) // 4 * 4])


def as_float(word):
    return struct.unpack("<f", struct.pack("<I", word))[0]


def diff_block(name, a, b, out, base=0):
    for i, (x, y) in enumerate(zip(words(a), words(b))):
        if x != y:
            out.append(f"{name} +0x{base + i * 4:x}: {x:08x} ({as_float(x):.6g}) / {y:08x} ({as_float(y):.6g})")
    if len(a) != len(b):
        out.append(f"{name}: sizes differ ({len(a)} / {len(b)} bytes)")


def entities(path):
    data = path.read_bytes()
    result = {}
    step = 8 + ENTITY_SIZE
    for at in range(0, len(data) - step + 1, step):
        index, address = struct.unpack_from("<II", data, at)
        result[index] = (address, data[at + 8 : at + step])
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("a", type=Path)
    parser.add_argument("b", type=Path)
    parser.add_argument("frame", type=int)
    parser.add_argument("--limit", type=int, default=60, help="differing words to print (default 60)")
    args = parser.parse_args()
    stem = f"frame_{args.frame}_"
    out = []
    diff_block("level", (args.a / f"{stem}level.bin").read_bytes(), (args.b / f"{stem}level.bin").read_bytes(), out)
    ca = (args.a / f"{stem}clients.bin").read_bytes()
    cb = (args.b / f"{stem}clients.bin").read_bytes()
    for n in range(max(len(ca), len(cb)) // CLIENT_SIZE):
        diff_block(f"client {n}", ca[n * CLIENT_SIZE : (n + 1) * CLIENT_SIZE],
                   cb[n * CLIENT_SIZE : (n + 1) * CLIENT_SIZE], out)
    ea = entities(args.a / f"{stem}entities.bin")
    eb = entities(args.b / f"{stem}entities.bin")
    for index in sorted(set(ea) | set(eb)):
        if index not in ea or index not in eb:
            out.append(f"entity {index}: only in {'A' if index in ea else 'B'}")
            continue
        if ea[index][0] != eb[index][0]:
            out.append(f"entity {index}: address {ea[index][0]:08x} / {eb[index][0]:08x}")
        diff_block(f"entity {index}", ea[index][1], eb[index][1], out)
    print(f"{len(out)} differences" + (f", the first {args.limit}:" if len(out) > args.limit else ":"))
    for line in out[: args.limit]:
        print(" ", line)


if __name__ == "__main__":
    main()
