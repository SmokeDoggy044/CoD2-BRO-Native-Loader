#!/usr/bin/env python3
"""Compares sound captures of the same replay (--audio-capture FILE; engine improvements phase 3.0 tools).

    tools/audio_compare.py [--out DIR] [--window FRAMES] NAME=FILE.wav [NAME=FILE.wav ...]

Each capture comes with FILE.wav.frames.csv (presented frame -> sample position) and FILE.wav.voices.csv (every voice
10 times a second). Windows are cut by replay frame (default 60 = 1 s of game time), so captures made at different
speeds or with different timing line up. Per window and capture: loudness (RMS dBFS of the mid signal), five bands
(dB), the left/right balance; the first capture is the reference the others are compared with. Per voice category:
out level (voice level x gain x direct gain, dB), occlusion, direct HF gain, reverb send.

Writes DIR/windows.csv and DIR/summary.txt (also printed). Numbers only - no listening needed to see what changed.
"""
import argparse
import csv
import math
import os
import sys
import wave
from collections import defaultdict

import numpy as np

BANDS = [(0, 200), (200, 800), (800, 3000), (3000, 8000), (8000, 24000)]
BAND_NAMES = ["low", "lowmid", "mid", "high", "air"]
FLOOR = -120.0


def db(power):
    return 10.0 * math.log10(power) if power > 1e-12 else FLOOR


def load(path):
    with wave.open(path, "rb") as w:
        rate, channels = w.getframerate(), w.getnchannels()
        data = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype(np.float64) / 32768.0
    data = data.reshape(-1, channels)
    frames = {}
    with open(path + ".frames.csv") as f:
        for row in csv.DictReader(f):
            if row.get("sample"):  # a cut last row: left out
                frames[int(row["frame"])] = int(row["sample"])
    voices = []
    if os.path.exists(path + ".voices.csv"):
        with open(path + ".voices.csv") as f:
            voices = [v for v in csv.DictReader(f) if None not in v.values()]  # a cut last row: left out
    return rate, data, frames, voices


def analyse(rate, block):
    """Loudness, band levels and balance of a stereo block."""
    mid = block.mean(axis=1)
    out = {"rms": db(float(np.mean(mid * mid))) if len(mid) else FLOOR}
    if len(block):
        left, right = float(np.mean(block[:, 0] ** 2)), float(np.mean(block[:, 1] ** 2))
        out["balance"] = db(right) - db(left) if left > 1e-12 and right > 1e-12 else 0.0
    else:
        out["balance"] = 0.0
    size = 4096
    if len(mid) < size:
        spectrum = np.zeros(size // 2 + 1)
    else:
        window = np.hanning(size)
        spectrum = np.zeros(size // 2 + 1)
        count = 0
        for start in range(0, len(mid) - size + 1, size // 2):
            spectrum += np.abs(np.fft.rfft(mid[start:start + size] * window)) ** 2
            count += 1
        spectrum /= count * np.sum(window ** 2) * size / 2
    freqs = np.fft.rfftfreq(size, 1.0 / rate)
    for name, (lo, hi) in zip(BAND_NAMES, BANDS):
        out[name] = db(float(spectrum[(freqs >= lo) & (freqs < hi)].sum()))
    return out


def voice_table(voices):
    """Per category: rows, mean out level (dB, rows that rendered), occlusion, direct HF, send."""
    acc = defaultdict(lambda: defaultdict(list))
    for v in voices:
        cat = v["category"]
        level = float(v["level"])
        a = acc[cat]
        a["rows"].append(1)
        if level > 0:
            a["out"].append(20 * math.log10(max(level * float(v["gain"]) * float(v["direct"]), 1e-6)))
        if v["spatial"] == "1":
            a["occlusion"].append(float(v["occlusion"]))
            a["hf"].append(float(v["direct_hf"]))
            a["send"].append(float(v["send"]))
    table = {}
    for cat, a in acc.items():
        mean = lambda xs: sum(xs) / len(xs) if xs else float("nan")
        table[cat] = {"rows": len(a["rows"]), "out_db": mean(a["out"]), "occlusion": mean(a["occlusion"]),
                      "direct_hf": mean(a["hf"]), "send": mean(a["send"])}
    return table


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("captures", nargs="+", help="NAME=FILE.wav (or FILE.wav: named after the file)")
    ap.add_argument("--out", help="folder for windows.csv and summary.txt")
    ap.add_argument("--window", type=int, default=60, help="frames per window (default 60 = 1 s)")
    ap.add_argument("--top", type=int, default=8, help="windows listed with the largest change (default 8)")
    args = ap.parse_args()

    caps = []
    for spec in args.captures:
        name, _, path = spec.partition("=") if "=" in spec else (os.path.basename(spec), "", spec)
        rate, data, frames, voices = load(path)
        caps.append({"name": name, "rate": rate, "data": data, "frames": frames, "voices": voices})
    common = set(caps[0]["frames"])
    for c in caps[1:]:
        common &= set(c["frames"])
    if not common:
        sys.exit("the captures share no frames (not the same replay?)")
    first, last = min(common), max(common)

    rows = []
    totals = [defaultdict(float) for _ in caps]
    for start in range(first, last - args.window + 1, args.window):
        end = start + args.window
        if start not in common or end not in common:
            continue
        row = {"frame": start}
        for i, c in enumerate(caps):
            a = analyse(c["rate"], c["data"][c["frames"][start]:c["frames"][end]])
            for k, v in a.items():
                row[f"{c['name']}_{k}"] = round(v, 2)
                if k != "balance":
                    totals[i][k] += 10 ** (v / 10)
            totals[i]["n"] += 1
        rows.append(row)
    if not rows:
        sys.exit("no complete window in the common frames")

    lines = [f"frames {first}..{last}, {len(rows)} windows of {args.window} frames; reference: {caps[0]['name']}", ""]
    keys = ["rms"] + BAND_NAMES
    lines.append("energy mean (dB)  " + "".join(f"{k:>9}" for k in keys))
    for i, c in enumerate(caps):
        n = totals[i]["n"]
        lines.append(f"  {c['name']:<16}" + "".join(f"{db(totals[i][k] / n):9.2f}" for k in keys))
    ref = caps[0]["name"]
    for c in caps[1:]:
        lines.append("")
        lines.append(f"{c['name']} - {ref} per window (dB):  mean / max |change|")
        for k in keys + ["balance"]:
            d = [r[f"{c['name']}_{k}"] - r[f"{ref}_{k}"] for r in rows
                 if r[f"{ref}_{k}"] > FLOOR and r[f"{c['name']}_{k}"] > FLOOR]
            if d:
                lines.append(f"  {k:<8} {sum(d) / len(d):+7.2f}  {max(abs(x) for x in d):6.2f}")
        worst = sorted(rows, key=lambda r: -abs(r[f"{c['name']}_rms"] - r[f"{ref}_rms"]))[:args.top]
        lines.append(f"  largest loudness changes: " + ", ".join(
            f"frame {r['frame']} {r[c['name'] + '_rms'] - r[ref + '_rms']:+.1f}" for r in worst))

    lines.append("")
    lines.append("voices by category: rows, out level dB, occlusion, direct HF, reverb send (spatial only)")
    tables = [voice_table(c["voices"]) for c in caps]
    cats = sorted(set().union(*tables))
    for cat in cats:
        for c, t in zip(caps, tables):
            v = t.get(cat)
            if v:
                lines.append(f"  {cat:<12} {c['name']:<16} {v['rows']:6d} {v['out_db']:8.2f} {v['occlusion']:6.3f}"
                             f" {v['direct_hf']:6.3f} {v['send']:6.3f}")
    text = "\n".join(lines)
    print(text)
    if args.out:
        os.makedirs(args.out, exist_ok=True)
        with open(os.path.join(args.out, "windows.csv"), "w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=list(rows[0]))
            w.writeheader()
            w.writerows(rows)
        with open(os.path.join(args.out, "summary.txt"), "w") as f:
            f.write(text + "\n")


if __name__ == "__main__":
    main()
