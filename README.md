# Call of Duty 2: Big Red One - native loader for PC

A **native loader** that runs the original Xbox version of *Call of Duty 2: Big Red
One* on PC, on Linux and Windows. It needs your own copy of the game; this
repository and its releases contain none of the game's code or data.

The Xbox has an x86 CPU, so the game's own code needs no translation: the loader
maps the original `default.xbe` into memory and the PC's CPU runs it directly. What
we wrote replaces everything around it: the Xbox kernel (files, memory, threads),
the SDK libraries the game uses (Direct3D 8 on Direct3D 9, DirectSound on OpenAL
Soft, controllers and keyboard/mouse on SDL3), and a few small patches to the
game's code (timing, mouse look, an aim-assist switch).

What it is **not**:

- **Not a recompilation.** No machine code is translated ahead of time (as static
  recompilers such as N64Recomp do for consoles with a different CPU); the
  original code runs unmodified apart from the patches above.
- **Not a decompilation (yet).** There is no source code of the game here, and no
  game function has been decompiled so far.
- **Not a CPU emulator.** Only the Xbox's system software is reimplemented, the way
  Wine reimplements Windows; Cxbx-Reloaded works on the same model.

This native loader is the **proof of concept for a full native build**. The plan:
decompile the game's functions one at a time and hook them into the running game,
until none of the original code is left. Then the loader goes away and `cod_bro`
builds from source for Linux and Windows, needing only the game's assets. The
docs call the two stages route B (the native loader, now) and route C (the full
native build).

## Status

Prototype 0.1.0. The single-player campaign is playable from the menus through
the missions: 1 to 5 tested through, 6 started. It runs at the game's 30 fps,
with the game's own widescreen mode, higher render resolutions, borderless
fullscreen, controllers, and keyboard and mouse. Multiplayer and Xbox Live are not
supported. The Windows version is tested on Windows and under Wine. Details and
next steps: [docs/status.md](docs/status.md).

## Playing

1. Download the archive for your system from the
   [releases](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases):
   `cod_bro-<version>-linux.tar.gz` or `cod_bro-<version>-windows.zip`.
2. Unpack it anywhere and copy your game dump into its `assets/` folder.
3. Start `cod_bro_launcher`: it checks the game files, sets the options and starts
   the game.

| | Linux | Windows |
|---|---|---|
| System | 64-bit or 32-bit x86, glibc 2.30 or newer | 64-bit Windows 10 or 11 |
| Graphics | Vulkan 1.3 with 32-bit drivers (Mesa or NVIDIA), through the bundled DXVK | Vulkan 1.3 through the bundled DXVK, or else any GPU through the Direct3D 9 that is part of Windows (picked by itself; nothing to install) |
| Other | 32-bit X11/Wayland and PulseAudio/PipeWire client libraries (as 32-bit Steam games need) | - |

Controls and options are in the `README.txt` inside the archive.

## How the native loader works

- **Memory:** the XBE goes to its own base address (0x10000), the Xbox's
  contiguous memory to 0x80000000, as the game expects. On Windows,
  `cod_bro_loader.exe` is a small stub linked at 0x10000 that keeps those ranges
  free and loads the loader proper from `cod_bro_loader_core.dll`.
- **Kernel:** the game's kernel import table is filled with our implementations
  (`xbox/`, `loader/kernel_thunks.cpp`).
- **Per-thread state:** Xbox code finds its thread and processor data through the
  FS segment. On Linux FS points at a fake Xbox KPCR for each thread; on Windows FS
  stays the thread's own TEB, and the few game instructions that read Xbox-only
  fields through it are patched to read TEB slots holding the same values.
- **SDK libraries:** the game's statically linked copies of Direct3D, DirectSound,
  the input library and Xbox Live are redirected to our code by name, using a symbol
  file exported from Ghidra (`render/`, `audio/`, `input/`, `online/`).
- **Rendering:** the game's Direct3D 8 calls and NV2A shaders are translated to
  Direct3D 9, implemented by DXVK on Vulkan. On Windows the bundled DXVK
  (`dxvk/d3d9.dll`) is tried first and the `d3d9.dll` that comes with Windows is
  the fallback (`--d3d9 auto|dxvk|system`, Graphics in the launcher); no DirectX
  runtime or D3DX is needed (shaders are emitted as bytecode). See
  [docs/rendering.md](docs/rendering.md).

## License

The port's own code is licensed under the GNU General Public License, version 2
or (at your option) any later version: see [LICENSE](LICENSE) and
[LICENSES/README.md](LICENSES/README.md), which also covers the third-party code.
Call of Duty 2: Big Red One belongs to its owners; none of its code or data is
part of this project.

## Documentation

| File | Contents |
|---|---|
| [docs/status.md](docs/status.md) | What works per subsystem, playtest results, next steps |
| [docs/rendering.md](docs/rendering.md) | Direct3D replacement: design, draw path, render scale, widescreen, frame dumps |
| [docs/playtesting.md](docs/playtesting.md) | `--diag` sessions, scripted and unattended runs, the feature sweep |
| [docs/comparison-harness.md](docs/comparison-harness.md) | Planned: checking decompiled functions against the original |
| [assets/README.md](assets/README.md) | For players: where the game files go |
| [LICENSES/README.md](LICENSES/README.md) | Licenses of everything in the repository |

## Layout

| Folder | Contents | Fate |
|---|---|---|
| `loader/` | The native loader: XBE mapping, per-thread FS state, kernel import table, title patches and hooks, crash reports, `--diag` | Native loader only (32-bit Linux and Windows) |
| `xbox/` | Xbox kernel/XAPI functions on top of `platform/` | Shrinks into the game's platform code |
| `platform/` | Logging, paths, files, virtual memory, threads | Kept, portable |
| `render/` | Xbox Direct3D 8 replacement (D3D9/DXVK first, Vulkan later) | Kept, portable |
| `audio/` | DirectSound replacement on OpenAL Soft | Kept, portable |
| `input/` | Controllers, keyboard and mouse on SDL3; scripted presses | Kept, portable |
| `online/` | Xbox Live stubs (always unavailable) | Kept, portable |
| `game/` | The full native build's `cod_bro` executable and, later, the decompiled game code (none yet) | Kept, portable |
| `launcher/` | `cod_bro_launcher`: settings and game-file setup (Dear ImGui, fetched by CMake) | Kept, portable |
| `tests/` | Settings, kernel-layer and audio tests (no game files needed) | Kept |
| `assets/` | Where players put the game files (only its README is in git) | Kept |
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

Fedora packages for the 32-bit native loader build:

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
`COD_BRO_GAME_FILES`), so development needs no copy of the 3 GB. In git, `assets/`
holds only the players' README, which releases ship. The game only reads there.
What it writes (saves, the cache drive) goes to the per-user data folder:
`~/.local/share/cod_bro` (Linux), `%APPDATA%\cod_bro` (Windows), or `--data DIR`.
Each run's log is `log.txt` in the user data folder (with `--diag`, in its folder);
on Windows a message box points to it when the game cannot start or crashes.
Override assets with `--assets DIR` or `COD_BRO_ASSETS`.

Players use `cod_bro_launcher`: it shows where the game files go, sets the options
below and starts the game. It saves them as `settings.ini` in the user data folder
(always there, not under `--data`): `key = value` lines named after the options
(`aspect = 4:3`, `silent = true`), which the game reads as options placed before its
command line, so options given there win. Without game files and without
`--assets`/`COD_BRO_ASSETS`, the game starts the launcher and exits.

Common options: `--aspect 16:9|4:3`, `--fullscreen`/`--windowed` (Alt+Enter switches
while playing), `--scale N` (window size), `--render-scale X`,
`--anisotropy N`, `--brightness X`, `--mouse-sensitivity DEG`, `--aim-assist on|off`, `--level N` (start a mission directly),
`--diag DIR` (playtesting, see [docs/playtesting.md](docs/playtesting.md)),
`--debug`/`--trace`. Playtests and performance checks use the optimized
`linux-loader-release` build (`linux-loader` is a Debug build, several times slower
in heavy scenes). Profiling (Linux): `XB_PROFILE=auto[,SECONDS]` samples every thread
from the first second under 24 fps (or `XB_PROFILE=START,SECONDS`) and logs where
the time went; `XB_PROFILE_OUT=FILE` adds every sampled address for `addr2line`. The keyboard layout is in `input/devices.h`: WASD, mouse look,
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

Windows (cross-built here with MinGW-w64, `cmake/release_windows.cmake`):

    packaging/windows-dxvk/build.sh        # once: DXVK's d3d9.dll (podman, Fedora container)
    cmake --preset windows-mingw-release && cmake --build --preset windows-mingw-release
    cpack --preset windows-mingw-release   # build/windows-mingw-release/package/cod_bro-<version>-windows.zip

The zip holds the launcher, the loader stub and `cod_bro_loader_core.dll`,
`OpenAL32.dll` (the only DLL; SDL3 and the GCC runtime are linked in), the symbol
file, `dxvk/d3d9.dll`, `assets/`, the README and `LICENSES/`. DXVK sits in its own
folder so that only the loader picks it, and the system's `d3d9.dll` stays
reachable as the fallback.
