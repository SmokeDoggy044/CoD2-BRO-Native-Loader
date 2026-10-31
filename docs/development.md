# Developing the native loader

Building, running and testing the native loader, the repository's layout, its tools and
how releases are made. For what the project is and how to play, see the
[README](../README.md); for how the loader works, [how-it-works.md](how-it-works.md).

## Layout

| Folder | Contents | Fate |
|---|---|---|
| `game/code/` | The rewritten game code (C++) and its registry (`functions.cpp`) | Kept, portable: grows into the full game |
| `game/` | The full native build's `cod_bro` executable | Kept, portable |
| `loader/` | The native loader: XBE mapping, per-thread FS state, kernel import table, title patches and hooks, the release map, the comparison harness, crash reports, `--diag` | Native loader only (32-bit Linux and Windows) |
| `xbox/` | Xbox kernel/XAPI functions on top of `platform/` | Shrinks into the game's platform code |
| `platform/` | Logging, paths, files, virtual memory, threads | Kept, portable |
| `render/` | Xbox Direct3D 8 replacement (D3D9/DXVK first, Vulkan later) | Kept, portable |
| `audio/` | DirectSound replacement on OpenAL Soft | Kept, portable |
| `input/` | Controllers, keyboard and mouse on SDL3, key bindings; scripted presses | Kept, portable |
| `online/` | System Link on a virtual LAN; Xbox Live stubs (always unavailable) | Kept, portable |
| `text/` | Sharper text and keyboard button prompts: the game's packs and font tables, font atlases and button icons drawn again with stb_truetype | Kept, portable |
| `fonts/` | Free stand-in fonts for sharper text (SIL OFL) | Kept |
| `launcher/` | `cod_bro_launcher`: settings, key bindings, game-file setup and updates (Dear ImGui, fetched by CMake) | Kept, portable |
| `tests/` | Kernel-layer, audio, settings, release map, key binding and shader tests; the comparison harness's own test; offline tests of rewritten functions | Kept |
| `assets/` | Where players put the game files (only its README is in git) | Kept |
| `tools/` | Ghidra scripts, the progress tracker, comparison and naming helpers, kernel export generator, feature sweep | Kept |
| `tools/data/` | Symbol files, release-map signatures, function names with their evidence, comparison results, per-function progress | Kept |
| `third_party/xbox-includes/` | Xbox kernel/SDK headers (git submodule of github.com/mborgerson/xbox-includes) | Reference |
| `LICENSES/` | License texts and which applies where | - |

Rules for portable code: no compiler extensions outside `platform/compiler.h`,
fixed-width types in structs that mirror Xbox memory (with `static_assert`s), and
32-bit Xbox pointers stored as `xbox::XbPtr`. Rewritten game code reaches the game's
globals through `title_global<T>(address)` (`game/code/title_data.h`) and does its
float math in SSE in the original's order, so it rounds like the game (details in
[docs/comparison-harness.md](comparison-harness.md)).

## Building

Clone with `git clone --recursive`, or run `git submodule update --init` in an
existing clone, to get `third_party/xbox-includes` (needed only to regenerate the
kernel exports).

Fedora packages for the 32-bit native loader build:

    sudo dnf install glibc-devel.i686 libstdc++-devel.i686 openal-soft-devel.i686 vkd3d-compiler

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
command line, so options given there win. `check-updates = false` is the launcher's
own (the game skips keys set to false). Without game files and without
`--assets`/`COD_BRO_ASSETS`, the game starts the launcher and exits.

Common options: `--aspect 16:9|4:3` (or wider, e.g. `21:9`, `32:9`, `3440:1440`:
experimental, campaign only), `--fullscreen`/`--windowed` (Alt+Enter switches while
playing), `--scale N` (window size), `--render-scale X`, `--anisotropy N`,
`--brightness X`, `--sharp-text on|off`, `--button-prompts auto|keyboard|controller`,
`--language L`, `--mouse-sensitivity DEG`, `--keys LIST` (key bindings, as the
launcher writes them), `--aim-assist on|off`, `--fps 60|30`, `--late-frames
show|wait`, `--fov DEG` (also the in-game PC menu: the `` ` `` key), `--level N`
(start a mission directly), `--multiplayer` (start multiplayer), `--net-port N`
(System Link UDP ports N..N+3), `--diag DIR` (playtesting, see
[docs/playtesting.md](playtesting.md)), `--debug`/`--trace`. Playtests and
performance checks use the optimized `linux-loader-release` build (`linux-loader` is
a Debug build, several times slower in heavy scenes).

The default keyboard layout is in `input/devices.h`, the bindings in
`input/bindings.h`: WASD, mouse look, Tab shows the objectives while held, Escape
pauses. The menus take the mouse (point, click, right click = back, wheel = up/down;
`install_menu_pointer` in `loader/patches.cpp`). In a level, click the window to
capture the mouse; F10 or Alt+Tab frees it.

Profiling (Linux and Windows): `--profile auto[,SECONDS]` (or `XB_PROFILE`; the
launcher's "Measure performance") samples every thread for SECONDS each time a second
runs under 80% of the frame rate aimed at (at most once a minute, 20 times a run,
naming the mission), or once with `START,SECONDS`, and logs where the time went;
`XB_PROFILE_OUT=FILE` adds every sampled address. Release packages are stripped:
`tools/resolve_profile.py LOG BINARY` names our own offsets from the unstripped
build.

### Comparing rewritten code with the original (Linux)

    ./build/linux-loader/bin/cod_bro_loader --compare --level 1

`--compare` tests the `Compare` functions against the originals while the game runs
and adds the counts to `tools/data/functions.csv`; `--compare-all` tests the
settled ones too; `--force` calls functions the campaign never reaches. These runs
leave out the PC changes and `settings.ini` (`--pc-patches off`: 30 fps, 4:3, the
game's own fonts), so the game's code runs as on the Xbox. `tools/compare_batch.py`
builds and runs several missions at once and prints one line per function. Details:
[docs/comparison-harness.md](comparison-harness.md).

## Tools

- **Symbols:** run `tools/ghidra/ExportSymbols.java` in Ghidra on `default.xbe` and
  save to `tools/data/default_xbe_symbols.txt`; the loader uses it to name functions
  in backtraces and to find SDK functions to replace.
- **Other releases:** the port's addresses are those of one build of the game. On
  another release (the Collector's Edition, the European French/Spanish/Italian disc)
  the loader finds them by matching that build's functions and data references,
  hashed in `tools/data/<xbe>_signatures.txt` (`loader/release_map.h`). Regenerate
  these with `cod_bro_loader [--multiplayer] --write-signatures FILE` on the supported
  dump whenever the symbol files gain functions. A multiplayer XBE in which some
  addresses are not found is refused unless `--any-release` is given; the log lists
  them.
- **Progress:** `tools/progress.py` tracks every function of the game (named,
  decompiled, tested, rewritten, set aside) by part of the game
  (`tools/data/progress.csv`).
- **Decompiling and naming:** `tools/fnpack.py` (one compact text per function:
  callers, callees, globals, strings, the disassembly named inline),
  `tools/namepack.py` (naming packets from a Ghidra export), `tools/scaffold.py`
  (declarations, stubs and registrations for a batch), `tools/q3match/` (matches the
  game's functions to the Quake III Arena and Return to Castle Wolfenstein sources,
  the engine's ancestors), `tools/vc71match/` (compiles `game/code/` with the
  game's own compiler and scores it against the original instructions).
- **Kernel exports:** `xbox/kernel_exports.inc` is generated from
  `tools/data/xboxkrnl_ordinals.txt` and `third_party/xbox-includes/include/xboxkrnl.h`
  (`--target gen_kernel_exports`). Each export's calling convention and argument size
  come from the header.
- **Vertex programs:** `tools/nv2a_vsdis.py` disassembles NV2A microcode from a frame
  dump.

## Tests

`ctest --preset linux-loader` runs the tests in `tests/`:

- The kernel layer (critical sections, dispatcher, memory, file system, crypto, time,
  frame pacing) and audio: the file tests build a fake disc in a temp folder and the
  audio test mixes on an OpenAL loopback device, so neither needs game files or a
  sound card.
- Settings, the update check, key bindings, the release map, the D3D9 shader
  translator and the virtual LAN.
- `compare_test`: the comparison harness itself, against a stand-in library.
- `nal_entropy_test` and `leaf_test`: rewritten functions against the original
  code from `default.xbe`, on generated inputs (skipped without the game files;
  `leaf_test` takes about half an hour).

They need a 32-bit build because `xbox/` stores the title's pointers as 32-bit
`XbPtr`; making `xbox/` 64-bit-clean is part of the full native build.

## Releasing

    packaging/sniper/build.sh   # build/sniper/release/package/cod_bro-<version>-linux.tar.gz

This builds the release in the Steam Runtime 3 "sniper" SDK container (podman;
Debian 11, glibc 2.31), so it runs on distributions since about 2020 (glibc 2.30+,
checked on a bare 32-bit Debian 11). SDL3, OpenAL Soft and DXVK are built from
source there (`packaging/sniper/inside.sh`, versions at its top), with
`-mstackrealign` like our code: title threads call into them with 4-byte stack
alignment. Everything is cached in `build/sniper/`; changing the script or
`cmake/toolchains/sniper-i686.cmake` rebuilds the libraries.

The archive holds `cod_bro_launcher`, `cod_bro_loader`, the symbol and signature
files, the rewritten game code (`lib/libcod_bro_game.so`), an empty `assets/` (with
its README), the player README (`packaging/README.txt.in`), the three libraries in
`lib/` (found through an `$ORIGIN/lib` RPATH) and `LICENSES/` with every bundled
library's license, version and source (`cmake/release.cmake`). The version is
`project(VERSION)` in CMakeLists.txt.

`cpack --preset linux-loader-release` packages the same layout from this
machine's own libraries instead; it only runs on systems as new as this one.

Windows (cross-built here with MinGW-w64, `cmake/release_windows.cmake`):

    packaging/windows-dxvk/build.sh        # once: DXVK's d3d9.dll (podman, Fedora container)
    cmake --preset windows-mingw-release && cmake --build --preset windows-mingw-release
    cpack --preset windows-mingw-release   # build/windows-mingw-release/package/cod_bro-<version>-windows.zip

The zip holds the launcher, the loader stub and `cod_bro_loader_core.dll` (with the
rewritten game code linked in), `OpenAL32.dll` (the only DLL; SDL3 and the GCC
runtime are linked in), the symbol and signature files, `dxvk/d3d9.dll`, `assets/`,
the README and `LICENSES/`. DXVK sits in its own folder so that only the loader
picks it, and the system's `d3d9.dll` stays reachable as the fallback.
