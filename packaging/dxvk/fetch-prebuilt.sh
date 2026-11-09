#!/usr/bin/env bash
# Stages DXVK's official 32-bit v2.7.1 release binaries for a build/release.
# The binaries are deliberately not committed: this script downloads them from
# GitHub and lays them out in the shape cmake/dxvk_prebuilt.cmake expects.
#
#   packaging/dxvk/fetch-prebuilt.sh [DEST]
#
# DEST defaults to dxvk/prebuilt/ (gitignored). GitHub Actions stages into
# build/deps/dxvk and passes that path as COD_BRO_DXVK_PREBUILT_DIR.
set -euo pipefail

repo="$(cd "$(dirname "$0")/../.." && pwd)"
dest="${1:-$repo/dxvk/prebuilt}"
version_file="$repo/dxvk/prebuilt/VERSION"
license_dir="$repo/dxvk/prebuilt/LICENSES"

[ -f "$version_file" ] || { echo "missing $version_file" >&2; exit 1; }
[ -f "$license_dir/DXVK.txt" ] || { echo "missing $license_dir/DXVK.txt" >&2; exit 1; }
[ -f "$license_dir/libdisplay-info.txt" ] || { echo "missing $license_dir/libdisplay-info.txt" >&2; exit 1; }

version="${DXVK_VERSION:-$(tr -d '\r\n' < "$version_file")}"
base="https://github.com/doitsujin/dxvk/releases/download/v$version"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

fetch() {
    echo "Fetching $2"
    curl --fail --location --silent --show-error --retry 3 --output "$1" "$2"
}

find_first() {
    # -type l keeps the linker-name symlinks in DXVK's official archives.
    find "$1" \( -type f -o -type l \) \( -name "$2" -o -name "$3" \) -print -quit
}

fetch "$work/dxvk-windows.tar.gz" "$base/dxvk-$version.tar.gz"
fetch "$work/dxvk-native.tar.gz" "$base/dxvk-native-$version-steamrt-sniper.tar.gz"
mkdir -p "$work/windows" "$work/native"
tar -xzf "$work/dxvk-windows.tar.gz" -C "$work/windows"
tar -xzf "$work/dxvk-native.tar.gz" -C "$work/native"

windows_dll="$(find "$work/windows" -type f -path '*/x32/d3d9.dll' -print -quit)"
[ -n "$windows_dll" ] || { echo "DXVK Windows archive has no x32/d3d9.dll" >&2; exit 1; }

runtime="$(find_first "$work/native" 'libdxvk_d3d9.so.0' 'libdxvk_d3d9.so.0.*')"
[ -n "$runtime" ] || { echo "DXVK native archive has no libdxvk_d3d9.so.0" >&2; exit 1; }
linker="$(find_first "$work/native" 'libdxvk_d3d9.so' '__never__')"
d3d9_header="$(find "$work/native" \( -type f -o -type l \) -path '*/include/dxvk/d3d9.h' -print -quit)"
[ -n "$d3d9_header" ] || { echo "DXVK native archive has no include/dxvk/d3d9.h" >&2; exit 1; }
header_dir="$(dirname "$d3d9_header")"

mkdir -p "$dest/windows" "$dest/linux/lib/pkgconfig" "$dest/linux/include/dxvk" "$dest/LICENSES"
cp -L "$windows_dll" "$dest/windows/d3d9.dll"
cp -L "$runtime" "$dest/linux/lib/libdxvk_d3d9.so.0"
if [ -n "$linker" ]; then
    cp -L "$linker" "$dest/linux/lib/libdxvk_d3d9.so"
else
    cp -L "$runtime" "$dest/linux/lib/libdxvk_d3d9.so"
fi
cp -a "$header_dir"/. "$dest/linux/include/dxvk/"
# With the default DEST the license files are already in place: cp would refuse
# to copy a file onto itself.
if [ "$(cd "$license_dir" && pwd -P)" != "$(cd "$dest/LICENSES" && pwd -P)" ]; then
    cp -f "$license_dir/DXVK.txt" "$license_dir/libdisplay-info.txt" "$dest/LICENSES/"
fi
printf '%s\n' "$version" > "$dest/VERSION"

cat > "$dest/linux/lib/pkgconfig/dxvk-d3d9.pc" <<EOF
prefix=$dest
includedir=\${prefix}/linux/include/dxvk
libdir=\${prefix}/linux/lib
Name: dxvk-d3d9
Description: DXVK $version native D3D9 (official prebuilt)
Version: $version
Libs: -L\${libdir} -ldxvk_d3d9
Cflags: -I\${includedir}
EOF

echo "Staged DXVK $version at $dest"
