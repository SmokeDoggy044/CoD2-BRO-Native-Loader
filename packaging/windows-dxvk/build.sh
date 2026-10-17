#!/usr/bin/env bash
# Builds DXVK's d3d9.dll for the Windows release (32-bit, MinGW-w64) in a Fedora
# container, so the host needs neither meson nor glslang.
#
#   packaging/windows-dxvk/build.sh   -> build/windows-dxvk/out/{d3d9.dll,licenses/,VERSION}
#
# release_windows.cmake puts it in the zip as dxvk/d3d9.dll. Built with
# -mstackrealign like the Linux copy: title threads call into it with the Xbox's
# 4-byte stack alignment. The source and build stay in build/windows-dxvk/.
set -euo pipefail

DXVK_VERSION=2.7.1  # the same as the Linux release (packaging/sniper/inside.sh)

port_dir="$(cd "$(dirname "$0")/../.." && pwd)"
work="$port_dir/build/windows-dxvk"
mkdir -p "$work"

podman run --rm \
    --userns=keep-id --user 0:0 \
    --security-opt label=disable \
    -v "$work:/work" \
    -e DXVK_VERSION="$DXVK_VERSION" \
    registry.fedoraproject.org/fedora:44 \
    bash -euo pipefail -c '
        dnf install -y -q mingw32-gcc-c++ mingw32-winpthreads-static meson ninja-build glslang git >/dev/null
        src=/work/dxvk-$DXVK_VERSION
        [ -d "$src" ] || git clone --quiet --depth 1 --branch v$DXVK_VERSION \
            --recurse-submodules --shallow-submodules https://github.com/doitsujin/dxvk.git "$src"
        rm -rf /work/build
        meson setup /work/build "$src" --cross-file "$src/build-win32.txt" \
            --buildtype release --strip -Dbuild_id=false \
            -Dc_args=-mstackrealign -Dcpp_args=-mstackrealign
        ninja -C /work/build src/d3d9/d3d9.dll
        rm -rf /work/out && mkdir -p /work/out/licenses
        # meson strips only on install
        i686-w64-mingw32-strip -o /work/out/d3d9.dll /work/build/src/d3d9/d3d9.dll
        cp "$src/LICENSE" /work/out/licenses/DXVK.txt
        cp "$src/subprojects/libdisplay-info/LICENSE" /work/out/licenses/libdisplay-info.txt
        echo "$DXVK_VERSION" > /work/out/VERSION
        chown -R --reference=/work /work/out /work/build "$src"
    '
echo "built $work/out/d3d9.dll (DXVK $DXVK_VERSION)"
