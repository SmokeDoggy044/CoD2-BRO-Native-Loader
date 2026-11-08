#!/usr/bin/env python3
"""Compares the per-frame state records of two runs: recordings (--record) or replay hash files (--replay-hashes).

usage: tools/replay_compare.py A B [--frames N]

Prints the first frame whose state differs (level.time, entity count, level/client/entity hashes), the entity
indices that differ there and for the next N frames (default 5), and how many frames differ in all.
"""
import argparse
import struct


def frames(path):
    """{frame: (level_time, entity_count, level_hash, entity_hash, client_hash, [entity hashes])}"""
    data = open(path, "rb").read()
    if data[:8] != b"XBREPLAY":
        raise SystemExit(f"{path}: not a recording")
    result = {}
    entities = []
    i = 12
    while i + 3 <= len(data):
        tag = data[i]
        size = struct.unpack_from("<H", data, i + 1)[0]
        body = data[i + 3 : i + 3 + size]
        if len(body) < size:
            break
        if tag == ord("E"):
            entities = list(struct.unpack(f"<{size // 4}I", body))
        elif tag == ord("F") and size == 24:
            frame, time, count, level, entity, client = struct.unpack("<IiIIII", body)
            result[frame] = (time, count, level, entity, client, entities)
            entities = []
        i += 3 + size
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("a")
    parser.add_argument("b")
    parser.add_argument("--frames", type=int, default=5, help="differing frames to detail (default 5)")
    args = parser.parse_args()
    fa, fb = frames(args.a), frames(args.b)
    common = sorted(set(fa) & set(fb))
    print(f"frames: {len(fa)} / {len(fb)}, {len(common)} in both")
    differing = [f for f in common if fa[f][:5] != fb[f][:5]]
    if not differing:
        print("all frames in both match")
        return
    print(f"{len(differing)} frames differ, the first at frame {differing[0]}")
    for f in differing[: args.frames]:
        a, b = fa[f], fb[f]
        parts = []
        if a[0] != b[0]:
            parts.append(f"level.time {a[0]}/{b[0]}")
        if a[1] != b[1]:
            parts.append(f"entities {a[1]}/{b[1]}")
        if a[2] != b[2]:
            parts.append("level locals")
        if a[4] != b[4]:
            parts.append("clients")
        ea, eb = a[5], b[5]
        diff = [k for k in range(max(len(ea), len(eb))) if (ea[k] if k < len(ea) else 0) != (eb[k] if k < len(eb) else 0)]
        if diff:
            parts.append(f"entities {' '.join(map(str, diff[:16]))}{' ...' if len(diff) > 16 else ''}")
        print(f"  frame {f} (level.time {a[0]}): " + ", ".join(parts))


if __name__ == "__main__":
    main()
