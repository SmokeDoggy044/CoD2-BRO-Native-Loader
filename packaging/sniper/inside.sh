#!/usr/bin/env bash
# Runs inside the sniper SDK container (see build.sh): builds the libraries the
# runtime lacks, then the release and its archive.
#
#   SDL3         from source: the runtime's package links X11, Wayland, PulseAudio,
#                D-Bus... directly; SDL's own build loads them at run time (the
#                player's system copies), so only SDL itself is bundled
#   OpenAL Soft  from source (sniper has 1.19; the port needs 1.22+)
#   DXVK         prebuilt (dxvk-native d3d9 from DXVK's official sniper build)
#
# C++ is linked with a static libstdc++ (GCC 14's is newer than the system's).
# Everything is built with -mstackrealign: title threads call into these libraries
# with the Xbox's 4-byte stack alignment (DXVK's SSE code crashed without it).
# Dependency versions: change them here. Any change to this script or the
# toolchain file rebuilds the dependencies on the next run.
set -euo pipefail

SDL_VERSION=3.4.18
OPENAL_VERSION=1.24.2
DXVK_VERSION=2.7.1

root=/port/build/sniper
src=$root/src
deps=$root/deps
licenses=$deps/licenses
jobs=$(nproc)
toolchain=/port/cmake/toolchains/sniper-i686.cmake
recipe=$(cat "$0" "$toolchain" | md5sum | cut -c1-8)  # in the stamps: rebuild on changes
export PKG_CONFIG_PATH=$deps/lib/pkgconfig
mkdir -p "$src" "$licenses"

fetch() {  # fetch <dir> <git url> <tag>
    if [ ! -d "$src/$1" ]; then
        git clone --quiet --depth 1 --branch "$3" --recurse-submodules --shallow-submodules "$2" "$src/$1"
    fi
}

# --- SDL3 ---------------------------------------------------------------------------
if [ ! -f "$deps/.sdl-$SDL_VERSION-$recipe" ]; then
    fetch sdl-$SDL_VERSION https://github.com/libsdl-org/SDL.git release-$SDL_VERSION
    rm -rf "$root/sdl-build"  # cached flags would survive otherwise
    cmake -S "$src/sdl-$SDL_VERSION" -B "$root/sdl-build" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=$toolchain -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$deps" -DCMAKE_INSTALL_LIBDIR=lib \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
        -DSDL_DEPS_SHARED=ON -DSDL_X11=ON -DSDL_WAYLAND=ON -DSDL_PULSEAUDIO=ON -DSDL_ALSA=ON \
        -DSDL_PIPEWIRE=ON -DSDL_VULKAN=ON
    cmake --build "$root/sdl-build" -j "$jobs"
    cmake --install "$root/sdl-build"
    cp "$src/sdl-$SDL_VERSION/LICENSE.txt" "$licenses/SDL3.txt"
    touch "$deps/.sdl-$SDL_VERSION-$recipe"
fi

# --- OpenAL Soft ---------------------------------------------------------------
if [ ! -f "$deps/.openal-$OPENAL_VERSION-$recipe" ]; then
    fetch openal-soft-$OPENAL_VERSION https://github.com/kcat/openal-soft.git $OPENAL_VERSION
    rm -rf "$root/openal-build"  # cached flags would survive otherwise
    cmake -S "$src/openal-soft-$OPENAL_VERSION" -B "$root/openal-build" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=$toolchain -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$deps" -DCMAKE_INSTALL_LIBDIR=lib \
        -DALSOFT_UTILS=OFF -DALSOFT_EXAMPLES=OFF -DALSOFT_TESTS=OFF \
        -DALSOFT_REQUIRE_PULSEAUDIO=ON -DALSOFT_REQUIRE_ALSA=ON
    cmake --build "$root/openal-build" -j "$jobs"
    cmake --install "$root/openal-build"
    cp "$src/openal-soft-$OPENAL_VERSION/COPYING" "$licenses/OpenAL-Soft.txt"
    cp "$src/openal-soft-$OPENAL_VERSION/BSD-3Clause" "$licenses/OpenAL-Soft-BSD-3-Clause.txt"
    cp "$src/openal-soft-$OPENAL_VERSION/LICENSE-pffft" "$licenses/OpenAL-Soft-pffft.txt"
    touch "$deps/.openal-$OPENAL_VERSION-$recipe"
fi
# OpenAL Soft builds its own copy of fmt in.
cp "$src/openal-soft-$OPENAL_VERSION"/fmt-*/LICENSE "$licenses/fmt.txt"

# --- DXVK (native, d3d9) ---------------------------------------------------------
# Prebuilt: DXVK's official Steam Runtime sniper build of the 32-bit dxvk-native
# d3d9 library (staged by packaging/dxvk/fetch-prebuilt.sh). Nothing is
# compiled from source here; the official binaries are already built with
# -mstackrealign for the title's 4-byte stacks.
prebuilt=${COD_BRO_DXVK_DIR:-/port/dxvk/prebuilt}
test -f "$prebuilt/linux/lib/libdxvk_d3d9.so.0" || { echo "missing prebuilt DXVK" >&2; exit 1; }
mkdir -p "$deps/lib" "$deps/include" "$deps/lib/pkgconfig"
cp -f "$prebuilt/linux/lib/libdxvk_d3d9.so" "$prebuilt/linux/lib/libdxvk_d3d9.so.0" "$deps/lib/"
cp -rf "$prebuilt/linux/include/dxvk" "$deps/include/"
cat > "$deps/lib/pkgconfig/dxvk-d3d9.pc" <<EOF
prefix=$deps
includedir=\${prefix}/include/dxvk
libdir=\${prefix}/lib
Name: dxvk-d3d9
Description: DXVK 2.7.1 native D3D9 (prebuilt)
Version: $DXVK_VERSION
Libs: -L\${libdir} -ldxvk_d3d9
Cflags: -I\${includedir}
EOF
cp "$prebuilt/LICENSES/DXVK.txt" "$licenses/DXVK.txt"
cp "$prebuilt/LICENSES/libdisplay-info.txt" "$licenses/libdisplay-info.txt"

cat > "$licenses/BUNDLED.txt" <<EOF
Libraries bundled in lib/ (and Dear ImGui, built into cod_bro_launcher). Built in
the Steam Runtime 3 "sniper" SDK; C++ code links the GCC runtime statically
(GCC Runtime Library Exception).
OpenAL Soft is LGPL: lib/libopenal.so.1 can be replaced with any compatible build.

SDL3 $SDL_VERSION
  license: LICENSES/bundled/SDL3.txt
  source:  https://github.com/libsdl-org/SDL (tag release-$SDL_VERSION)
DXVK $DXVK_VERSION (dxvk-native, d3d9; libdisplay-info built in)
  license: LICENSES/bundled/DXVK.txt, LICENSES/bundled/libdisplay-info.txt
  source:  https://github.com/doitsujin/dxvk (tag v$DXVK_VERSION)
OpenAL Soft $OPENAL_VERSION (fmt built in)
  license: LICENSES/bundled/OpenAL-Soft.txt, OpenAL-Soft-BSD-3-Clause.txt,
           OpenAL-Soft-pffft.txt, fmt.txt
  source:  https://github.com/kcat/openal-soft (tag $OPENAL_VERSION)
EOF

# --- The release -----------------------------------------------------------------------
cmake --preset sniper-release
cmake --build --preset sniper-release
# compare_test needs the host's process sandbox (seccomp filters, soft-dirty page tracking) that
# the container does not give; it runs on the development machine (a tool there, not for players).
ctest --preset sniper-release -E '^compare_test$'
cpack --preset sniper-release
