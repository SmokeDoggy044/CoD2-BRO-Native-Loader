#!/usr/bin/env bash
# DXVK is NOT compiled here. The Windows release ships DXVK's official prebuilt
# 32-bit d3d9.dll (from the v2.7.1 release, staged by
# packaging/dxvk/fetch-prebuilt.sh). This script only copies it to the old
# build/windows-dxvk/out path for anyone still using that layout.
set -euo pipefail

port_dir="$(cd "$(dirname "$0")/../.." && pwd)"
prebuilt="${COD_BRO_DXVK_PREBUILT_DIR:-$port_dir/dxvk/prebuilt}"
out="$port_dir/build/windows-dxvk/out"

test -f "$prebuilt/windows/d3d9.dll" || { echo "missing $prebuilt/windows/d3d9.dll" >&2; exit 1; }
test -f "$prebuilt/LICENSES/DXVK.txt" || { echo "missing DXVK license" >&2; exit 1; }
test -f "$prebuilt/VERSION" || { echo "missing VERSION" >&2; exit 1; }

mkdir -p "$out/licenses"
cp "$prebuilt/windows/d3d9.dll" "$out/d3d9.dll"
cp "$prebuilt/LICENSES/DXVK.txt" "$out/licenses/DXVK.txt"
cp "$prebuilt/LICENSES/libdisplay-info.txt" "$out/licenses/libdisplay-info.txt"
cp "$prebuilt/VERSION" "$out/VERSION"
echo "staged prebuilt DXVK $(cat "$prebuilt/VERSION") at $out (no compilation)"
