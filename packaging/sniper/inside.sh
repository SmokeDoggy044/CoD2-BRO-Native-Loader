#!/usr/bin/env bash
# Runs inside the sniper SDK container (see build.sh): builds the libraries the
# runtime lacks, then the release and its archive.
#
#   SDL3         from source: the runtime's package links X11, Wayland, PulseAudio,
#                D-Bus... directly; SDL's own build loads them at run time (the
#                player's system copies), so only SDL itself is bundled
#   OpenAL Soft  from source (sniper has 1.19; the port needs 1.22+)
#   DXVK         from source (dxvk-native, d3d9 only; libdisplay-info built in)
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
if [ ! -f "$deps/.dxvk-$DXVK_VERSION-$recipe" ]; then
    fetch dxvk-$DXVK_VERSION https://github.com/doitsujin/dxvk.git v$DXVK_VERSION
    cat > "$root/dxvk-cross.txt" <<'EOF'
[binaries]
c = 'gcc-14'
cpp = 'g++-14'
ar = 'ar'
strip = 'strip'
pkgconfig = 'i686-linux-gnu-pkg-config'

[built-in options]
c_args = ['-m32', '-mstackrealign']
cpp_args = ['-m32', '-mstackrealign']
c_link_args = ['-m32', '-static-libgcc']
cpp_link_args = ['-m32', '-static-libstdc++', '-static-libgcc']

[host_machine]
system = 'linux'
cpu_family = 'x86'
cpu = 'i686'
endian = 'little'
EOF
    rm -rf "$root/dxvk-build"
    meson setup "$root/dxvk-build" "$src/dxvk-$DXVK_VERSION" \
        --cross-file "$root/dxvk-cross.txt" --buildtype release --strip \
        --prefix "$deps" --libdir lib \
        -Denable_d3d9=true -Denable_d3d8=false -Denable_d3d10=false -Denable_d3d11=false -Denable_dxgi=false
    ninja -C "$root/dxvk-build" -j "$jobs"
    ninja -C "$root/dxvk-build" install
    cp "$src/dxvk-$DXVK_VERSION/LICENSE" "$licenses/DXVK.txt"
    cp "$src/dxvk-$DXVK_VERSION/subprojects/libdisplay-info/LICENSE" "$licenses/libdisplay-info.txt"
    touch "$deps/.dxvk-$DXVK_VERSION-$recipe"
fi

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
ctest --preset sniper-release
cpack --preset sniper-release
