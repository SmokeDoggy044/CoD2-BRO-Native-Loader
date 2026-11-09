# DXVK 2.7.1 prebuilt staging directory

This directory holds the version and license texts needed to bundle DXVK, but
the **official binaries are not committed**. Run:

```sh
packaging/dxvk/fetch-prebuilt.sh                 # stages here (default)
packaging/dxvk/fetch-prebuilt.sh build/deps/dxvk # or anywhere else
```

The script downloads DXVK's v2.7.1 release assets from GitHub and stages them
in this layout:

```
linux/include/dxvk/            d3d9.h and friends (MinGW-style headers)
linux/lib/libdxvk_d3d9.so      linker name
linux/lib/libdxvk_d3d9.so.0    SONAME / runtime library
linux/lib/pkgconfig/dxvk-d3d9.pc
windows/d3d9.dll               PE32 i386 MinGW DLL
LICENSES/DXVK.txt              zlib license (tracked)
LICENSES/libdisplay-info.txt   libdisplay-info license (tracked)
VERSION                        2.7.1 (tracked)
```

Sources:

- `dxvk-native-2.7.1-steamrt-sniper.tar.gz` → `linux/` (32-bit dxvk-native,
  built for the Steam Runtime sniper container with `-mstackrealign`).
- `dxvk-2.7.1.tar.gz` → `windows/d3d9.dll`.

`linux/` and `windows/` are gitignored. Pass another directory with
`-DCOD_BRO_DXVK_PREBUILT_DIR=<dir>` to CMake (the GitHub Actions workflow
stages into `build/deps/dxvk`). `cmake/dxvk_prebuilt.cmake` links the Linux
library directly and falls back to an installed `dxvk-d3d9.pc` when no staged
copy is present. `cmake/release.cmake` installs both `.so` files into `lib/`;
`cmake/release_windows.cmake` installs `d3d9.dll` into `dxvk/`.

Upstream: https://github.com/doitsujin/dxvk (release v2.7.1).
