# Call of Duty 2: Big Red One - decompilation

A work-in-progress **decompilation** of the original Xbox version of *Call of Duty
2: Big Red One* into portable C++. The goal is a full native build: `cod_bro`
built from source for Linux and Windows, needing only the game's assets. It needs
your own copy of the game; this repository contains none of the game's code or
data.

This repository is a fork of the
[native loader](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader), which
runs the original `default.xbe` on PC by replacing the Xbox kernel and SDK
libraries around it. **Players should use the native loader's releases**; this fork
is for the decompilation work only. The loader is what makes that work testable:
each decompiled function is hooked into the running game and checked against the
original while the game plays, until none of the original code is left and the
loader goes away.

## Progress

| Step | Done | What |
|---|---:|---|
| 1. Naming | **640/14488** | Functions named in Ghidra after their Quake 3 / RTCW counterparts, CoD-specific ones from their bodies (plus 385 globals) - [docs/naming.md](docs/naming.md) |
| 2. Decompiling | **3/14488** (engine 3/9511, levels 0/4977) | Functions decompiled into `game/code/` and matched against the originals - [docs/comparison-harness.md](docs/comparison-harness.md) |

The total covers the code the full native build needs from the XBE:

- **`.text`** (9,511 functions): the engine and game code, including the C runtime
  and SDK helpers linked into it.
- **Level sections** (PROLOGUE, ORAN, ... DRAGEETH, one per mission: 4,977
  functions): the missions' compiled level scripts. They are to be converted by a
  translator rather than one function at a time, and count as done once the
  translated code matches the originals like any other function.

**Not counted: the Microsoft library sections** (XGRPH, XONLINE, DSOUND, XNET,
D3DX, D3D, XPP: 2,954 functions), for two reasons:

- They drive the Xbox hardware and services (the NV2A GPU, the audio processor,
  the Xbox network stack and Xbox Live), so their code is of no use on PC. The
  native build replaces them with its own platform code (`render/`, `audio/`,
  `input/`, `online/`), which the native loader already uses.
- They are Microsoft's Xbox SDK, not the game's code; publishing a decompilation
  of them would mean republishing Microsoft's code.

Current state and next steps: [docs/status.md](docs/status.md) (top entry) and
[docs/naming.md](docs/naming.md) ("Next").

## How it works

1. **Name** a subsystem's functions and globals in Ghidra (`tools/data/manual_names.csv`
   with the evidence for each name, applied by `tools/ghidra/ApplyNames.java`, then
   exported to `tools/data/default_xbe_symbols.txt`), so decompiled code reads like
   the original. Module by module: the game module first (its AI last), then the
   cgame, then client/UI.
2. **Decompile** in batches by subsystem, leaf functions first, into `game/code/`
   (built as `libcod_bro_game.so` in Linux loader builds) and list each function in
   `game/code/functions.cpp`.
3. **Compare:** run the game with `--compare`. Sampled calls run both the original
   and the decompiled function in forked processes from the same memory and compare
   registers and every written byte (`tools/data/functions.csv`).
4. **Replace:** a function with many matched tests and no mismatches becomes
   `Status::Replaced` and runs instead of the original from then on.

Every batch is smoke-tested in gameplay (`tools/smoke_test.sh OUT`: mission 4,
past the movie and briefing into the level).

## Documentation

| File | Contents |
|---|---|
| [docs/status.md](docs/status.md) | Decompilation status and next steps; native loader status per subsystem |
| [docs/naming.md](docs/naming.md) | Naming in Ghidra: workflow, findings, state, next |
| [docs/comparison-harness.md](docs/comparison-harness.md) | Checking decompiled functions against the originals while the game runs (`--compare`) |
| [tools/q3match/README.md](tools/q3match/README.md) | Automatic name proposals by matching against Quake 3 |
| [docs/rendering.md](docs/rendering.md) | The loader's Direct3D replacement |
| [docs/multiplayer.md](docs/multiplayer.md) | codmp_xboxf.xbe and System Link |
| [docs/playtesting.md](docs/playtesting.md) | `--diag` sessions, scripted and unattended runs |
| [LICENSES/README.md](LICENSES/README.md) | Licenses of everything in the repository |

## Layout

| Folder | Contents |
|---|---|
| `game/code/` | **The decompiled game code** and the function table for `--compare` |
| `game/` | The full native build's `cod_bro` executable |
| `tools/data/` | Symbol file, name CSVs, comparison results (`functions.csv`) |
| `tools/ghidra/` | Ghidra scripts: apply names, export symbols and match data, cvar globals |
| `tools/q3match/` | Quake 3 matcher for name proposals |
| `tools/smoke_test.sh` | Unattended gameplay check for each batch |
| `loader/` | The native loader, including the comparison harness (`compare*.cpp`) |
| `xbox/`, `platform/` | Xbox kernel/XAPI functions on top of portable platform code |
| `render/`, `audio/`, `input/`, `online/`, `text/` | Replacements for the Xbox SDK libraries |
| `launcher/`, `packaging/`, `assets/`, `fonts/` | From the native loader (player builds) |
| `tests/` | Kernel-layer, audio and comparison-harness tests |
| `third_party/xbox-includes/` | Xbox kernel/SDK headers (git submodule) |

Rules for portable code: no compiler extensions outside `platform/compiler.h`,
fixed-width types in structs that mirror Xbox memory (with `static_assert`s), and
32-bit Xbox pointers stored as `xbox::XbPtr`.

## Building

Clone with `git clone --recursive` (or `git submodule update --init`). Fedora
packages for the 32-bit loader build:

    sudo dnf install glibc-devel.i686 libstdc++-devel.i686 openal-soft-devel.i686

Then:

    cmake --preset linux-loader && cmake --build --preset linux-loader
    ctest --preset linux-loader
    ./build/linux-loader/bin/cod_bro_loader --compare --level 1   # --help lists the options

The comparison harness runs on Linux only. Builds expect the game files at
`../unpacked` (CMake setting `COD_BRO_GAME_FILES`) or `--assets DIR`. Other presets
(Windows, release packaging) and the loader's options are described in the
[native loader's README](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader#readme).

## Keeping up with the native loader

Loader fixes are made in the loader repository and merged here:

    git pull upstream main

When this README conflicts, keep this fork's version.

## License

The project's own code (everything outside `third_party/`) is licensed under the
**GNU General Public License, version 2 or (at your option) any later version**
([LICENSE](LICENSE)). Bundled and third-party components: [LICENSES/README.md](LICENSES/README.md).

**The game is not included.** *Call of Duty 2: Big Red One* and all of its code and
data belong to their owners; you need your own copy of the game. The symbol file
and CSVs in `tools/data/` hold only function addresses, names and test results from
our own analysis. This project is not affiliated with or endorsed by Activision,
Treyarch or Microsoft. Call of Duty is a trademark of Activision Publishing, Inc.;
Xbox is a trademark of Microsoft Corporation.
