#!/usr/bin/env python3
"""Compares the pictures of two runs (--replay-shots folders, or any two folders of same-named BMPs).

usage: tools/shot_compare.py DIR_A DIR_B [--threshold N]
One line per picture both folders have: identical, or the share of pixels whose largest channel difference is
above the threshold (default 8 of 255) and the largest difference. Exit code 1 if any picture differs above it.
No dependencies: reads uncompressed 24/32-bit BMPs (what the loader writes).
"""
import os
import struct
import sys


def read_bmp(path):
    d = open(path, "rb").read()
    if d[:2] != b"BM":
        raise ValueError(f"{path}: not a BMP")
    offset = struct.unpack_from("<I", d, 10)[0]
    width, height = struct.unpack_from("<ii", d, 18)
    bpp = struct.unpack_from("<H", d, 28)[0]
    if bpp not in (24, 32):
        raise ValueError(f"{path}: {bpp}-bit BMP")
    step = bpp // 8
    stride = (width * step + 3) & ~3
    rows = [d[offset + y * stride: offset + y * stride + width * step] for y in range(abs(height))]
    return width, abs(height), step, rows


def compare(a, b, threshold):
    wa, ha, sa, ra = read_bmp(a)
    wb, hb, sb, rb = read_bmp(b)
    if (wa, ha) != (wb, hb):
        return f"sizes differ ({wa}x{ha} / {wb}x{hb})", True
    if sa == sb and ra == rb:
        return "identical", False
    over = largest = 0
    for row_a, row_b in zip(ra, rb):
        if row_a == row_b and sa == sb:
            continue
        for x in range(wa):
            pa, pb = x * sa, x * sb
            diff = max(abs(row_a[pa + c] - row_b[pb + c]) for c in range(3))
            if diff > largest:
                largest = diff
            if diff > threshold:
                over += 1
    share = 100.0 * over / (wa * ha)
    return f"{share:.2f}% of pixels differ by more than {threshold} (largest {largest})", over > 0


def main():
    args = sys.argv[1:]
    threshold = 8
    if "--threshold" in args:
        i = args.index("--threshold")
        threshold = int(args[i + 1])
        del args[i:i + 2]
    if len(args) != 2:
        sys.exit(__doc__)
    a_dir, b_dir = args
    names = sorted(set(os.listdir(a_dir)) & set(os.listdir(b_dir)), key=lambda n: (len(n), n))
    names = [n for n in names if n.lower().endswith(".bmp")]
    if not names:
        sys.exit(f"no pictures in both {a_dir} and {b_dir}")
    failed = False
    for n in names:
        text, bad = compare(os.path.join(a_dir, n), os.path.join(b_dir, n), threshold)
        failed |= bad
        print(f"  {n}: {text}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
