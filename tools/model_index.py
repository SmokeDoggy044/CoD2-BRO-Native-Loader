#!/usr/bin/env python3
"""Which missions hold which models: for the object viewer (--viewer, phase 1.0d).

Scans each mission's .cod packs (sp/<mission>/*.cod) for .xbmesh entries; a model's name for --viewer is the
entry's name without the extension. Writes tools/data/model_index.csv (model, level, mission, pack), level =
--level N (tools/progress.py MISSIONS order).

Usage: model_index.py UNPACKED [--out CSV]
       model_index.py UNPACKED --level N   (prints that mission's models as one --viewer list)
"""
import argparse
import csv
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from extract_textures import pack_entries  # noqa: E402
from progress import MISSIONS  # noqa: E402


def scan(unpacked):
    rows = []
    for level, (_, folder) in enumerate(MISSIONS):
        mission = folder.split("/")[-1]
        seen = set()
        for pack in sorted((unpacked / folder).glob("*.cod")):
            for name, _asset, _offset, _size in pack_entries(pack):
                if not name.lower().endswith(".xbmesh"):
                    continue
                model = name[: -len(".xbmesh")]
                if model.lower() in seen:
                    continue
                seen.add(model.lower())
                rows.append((model, level, mission, pack.name))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("unpacked", type=pathlib.Path)
    parser.add_argument("--out", type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parent / "data" / "model_index.csv")
    parser.add_argument("--level", type=int, help="print this mission's models as a --viewer list")
    args = parser.parse_args()
    rows = scan(args.unpacked)
    if args.level is not None:
        print(",".join(r[0] for r in rows if r[1] == args.level))
        return
    with open(args.out, "w", newline="") as f:
        out = csv.writer(f)
        out.writerow(["model", "level", "mission", "pack"])
        out.writerows(rows)
    print(f"{len(rows)} models over {len(MISSIONS)} missions -> {args.out}")


if __name__ == "__main__":
    main()
