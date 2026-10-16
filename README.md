# Call of Duty 2: Big Red One - PC port

A private project to run the original Xbox game natively on PC, from the owner's
own disc.

**Route B (now):** `cod_bro_loader` maps the original `default.xbe` into a 32-bit
Linux process and runs it directly on the CPU. Our code replaces the Xbox kernel
and, step by step, the SDK libraries (Direct3D, DirectSound, input) and then the
game's own functions, which are decompiled one at a time and hooked in.

**Route C (goal):** once every function is decompiled, the loader is deleted and
`cod_bro` is a standalone game for Linux and Windows that needs only the game's
assets.

The single-player campaign is playable from the menus through the missions tested
so far, at 30 fps, with widescreen, higher render resolutions and keyboard/mouse.
Details and next steps: [docs/status.md](docs/status.md).

## Documentation

| File | Contents |
|---|---|
| [docs/status.md](docs/status.md) | What works per subsystem, playtest results, next steps |
| [docs/rendering.md](docs/rendering.md) | Direct3D replacement: design, draw path, render scale, widescreen, frame dumps |
| [docs/playtesting.md](docs/playtesting.md) | `--diag` sessions, scripted and unattended runs, the feature sweep |
| [docs/comparison-harness.md](docs/comparison-harness.md) | Planned: checking decompiled functions against the original |
| [assets/README.md](assets/README.md) | Where the game files go |
| [LICENSES/README.md](LICENSES/README.md) | Licenses of everything in the repository |

## Layout

| Folder | Contents | Fate |
|---|---|---|
| `loader/` | XBE mapping, fake Xbox `fs`/KPCR, kernel import table, title patches and hooks, crash reports, `--diag` | Route B only, Linux 32-bit |
| `xbox/` | Xbox kernel/XAPI functions on top of `platform/` | Shrinks into the game's platform code |
| `platform/` | Logging, paths, files, virtual memory, threads | Kept, portable |
| `render/` | Xbox Direct3D 8 replacement (D3D9/DXVK first, Vulkan later) | Kept, portable |
| `audio/` | DirectSound replacement on OpenAL Soft | Kept, portable |
| `input/` | Controllers, keyboard and mouse on SDL3; scripted presses | Kept, portable |
| `online/` | Xbox Live stubs (always unavailable) | Kept, portable |
| `game/` | Decompiled game code and the `cod_bro` executable | Kept, portable |
| `launcher/` | `cod_bro_launcher`: settings and game-file setup (Dear ImGui, fetched by CMake) | Kept, portable |
| `tests/` | Settings, kernel-layer and audio tests (no game files needed) | Kept |
| `assets/` | Game files in the final version (empty during development) | Kept, not in git |
| `tools/` | Kernel export generator, Ghidra scripts, feature sweep, data files | Kept |
| `third_party/xbox-includes/` | Xbox kernel/SDK headers (git submodule of github.com/mborgerson/xbox-includes) | Reference |
| `LICENSES/` | License texts and which applies where | - |

Rules for portable code: no compiler extensions outside `platform/compiler.h`,
fixed-width types in structs that mirror Xbox memory (with `static_assert`s), and
32-bit Xbox pointers stored as `xbox::XbPtr`.

## Building

Clone with `git clone --recursive`, or run `git submodule update --init` in an
existing clone, to get `third_party/xbox-includes` (needed only to regenerate the
kernel exports).

Fedora packages for the 32-bit loader build:

    sudo dnf install glibc-devel.i686 libstdc++-devel.i686 openal-soft-devel.i686

Then:

    cmake --preset linux-loader && cmake --build --preset linux-loader
    ./build/linux-loader/bin/cod_bro_loader               # --help lists the options

The `linux` and `windows` presets build only the portable code (`cod_bro`), to keep
both platforms compiling from the start. Linux needs `openal-soft-devel`; on Windows
OpenAL Soft is built from source (CMake FetchContent).

`windows-mingw` cross-builds the Windows version on Linux with MinGW-w64 (32-bit,
loader included) and runs its tests under Wine:

    sudo dnf install mingw32-gcc-c++ mingw32-winpthreads-static wine
    cmake --preset windows-mingw && cmake --build --preset windows-mingw
    ctest --preset windows-mingw
    wine build/windows-mingw/bin/cod_bro_loader.exe

On Windows the loader is two files: `cod_bro_loader.exe`, a small stub linked at the
XBE's base address (0x10000) so that range and the kernel's are free, and
`cod_bro_loader_core.dll` with everything else (`loader/win32_stub.c`).

## Running

Builds use the release layout: executables in `build/<preset>/bin/`, next to an
`assets` link to the game files (`../unpacked` by default; CMake setting
`COD_BRO_GAME_FILES`). The game only reads there. What it writes (saves, the cache
drive) goes to the per-user data folder: `~/.local/share/cod_bro` (Linux),
`%APPDATA%\cod_bro` (Windows), or `--data DIR`. Override assets with `--assets DIR`
or `COD_BRO_ASSETS`.

Players use `cod_bro_launcher`: it shows where the game files go, sets the options
below and starts the game. It saves them as `settings.ini` in the user data folder
(always there, not under `--data`): `key = value` lines named after the options
(`aspect = 4:3`, `silent = true`), which the game reads as options placed before its
command line, so options given there win. Without game files and without
`--assets`/`COD_BRO_ASSETS`, the game starts the launcher and exits.

Common options: `--aspect 16:9|4:3`, `--fullscreen`/`--windowed` (Alt+Enter switches
while playing), `--scale N` (window size), `--render-scale X`,
`--anisotropy N`, `--mouse-sensitivity DEG`, `--aim-assist on|off`, `--level N` (start a mission directly),
`--diag DIR` (playtesting, see [docs/playtesting.md](docs/playtesting.md)),
`--debug`/`--trace`. The keyboard layout is in `input/devices.h`: WASD, mouse look,
Tab shows the objectives while held, Escape pauses. Click the window to capture the
mouse; F10 or Alt+Tab frees it.

## Tools

- **Symbols:** run `tools/ghidra/ExportSymbols.java` in Ghidra on `default.xbe` and
  save to `tools/data/default_xbe_symbols.txt`; the loader uses it to name functions
  in backtraces and to find SDK functions to replace.
- **Kernel exports:** `xbox/kernel_exports.inc` is generated from
  `tools/data/xboxkrnl_ordinals.txt` and `third_party/xbox-includes/include/xboxkrnl.h`
  (`--target gen_kernel_exports`). Each export's calling convention and argument size
  come from the header.
- **Vertex programs:** `tools/nv2a_vsdis.py` disassembles NV2A microcode from a frame
  dump.

## Tests

`ctest --preset linux-loader` runs the kernel-layer tests in `tests/` (critical
sections, dispatcher, memory, file system, crypto, time, frame pacing, audio; the file
tests build a fake disc in a temp folder and the audio test mixes on an OpenAL
loopback device, so neither needs game files or a sound card). They need a 32-bit
build because `xbox/` stores the title's pointers as 32-bit `XbPtr`; making `xbox/`
64-bit-clean is part of route C.

## Releasing

    packaging/sniper/build.sh   # build/sniper/release/package/cod_bro-<version>-linux.tar.gz

This builds the release in the Steam Runtime 3 "sniper" SDK container (podman;
Debian 11, glibc 2.31), so it runs on distributions since about 2020 (glibc 2.30+,
checked on a bare 32-bit Debian 11). SDL3, OpenAL Soft and DXVK are built from
source there (`packaging/sniper/inside.sh`, versions at its top), with
`-mstackrealign` like our code: title threads call into them with 4-byte stack
alignment. Everything is cached in `build/sniper/`; changing the script or
`cmake/toolchains/sniper-i686.cmake` rebuilds the libraries.

The archive holds `cod_bro_launcher`, `cod_bro_loader`, the symbol file, an empty
`assets/` (with its README), the player README (`packaging/README.txt.in`), the
three libraries in `lib/` (found through an `$ORIGIN/lib` RPATH) and `LICENSES/`
with every bundled library's license, version and source (`cmake/release.cmake`).
The version is `project(VERSION)` in CMakeLists.txt.

`cpack --preset linux-loader-release` packages the same layout from this
machine's own libraries instead; it only runs on systems as new as this one.
