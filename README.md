<div align="center">

# Call of Duty 2: Big Red One — Custom Engine

**Our own engine implementation of *Call of Duty 2: Big Red One*, built from source for Linux and Windows.**

[![License: GPL-2.0-or-later](https://img.shields.io/badge/license-GPL--2.0--or--later-blue)](LICENSE)
![Platforms: Linux | Windows](https://img.shields.io/badge/platforms-Linux%20%7C%20Windows-lightgrey)
[![Playable release](https://img.shields.io/github/v/release/SmokeDoggy044/CoD2-BRO-Native-Loader?label=playable%20release)](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases)
![Made with AI](https://img.shields.io/badge/made%20with-AI-8A2BE2)

[Play](#play) · [Features](#features) · [Status](#status) · [Build](#build-from-source) · [Documentation](docs/README.md) · [Made with AI](#made-with-ai)

</div>

---

*Call of Duty 2: Big Red One* (2005) came out on consoles only. This project brings it to
PC by rewriting its engine as source code of our own: the renderer, sound, script engine,
game logic and menus. The game's maps, models, sounds and mission scripts come from your
own copy of the Xbox game.

> [!NOTE]
> You need your own copy of the Xbox game. This repository contains none of the game's
> code or data.

## Play

The whole single-player campaign is playable on PC today.

1. Download the latest release for Linux or Windows from the
   [releases page](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases).
2. Unpack it and copy your game dump into its `assets/` folder.
3. Start `cod_bro_launcher`, pick your options and play.

The releases are built from the
[CoD2-BRO-Native-Loader](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader)
repository, which has the system requirements, controls and antivirus notes. This
repository is where the engine is rewritten; its first release will replace that one.

## Features

**Available now**

- The whole single-player campaign.
- 60 fps, higher render resolutions, widescreen, borderless fullscreen and a field of view setting.
- Controllers, and keyboard and mouse with your own key bindings.
- Optional enhanced graphics (anti-aliasing, anisotropic filtering, sharpening) and enhanced
  sound (reverb, occlusion behind walls, more voices), each with its own switch. Switched
  off, you get the picture and sound as on the Xbox.
- Multiplayer over System Link on your local network.

**Planned for the custom engine**

- A Vulkan renderer next to Direct3D 9.
- A proper in-game settings menu, and a launcher that has every option.
- Ambient occlusion, occlusion culling and dynamic lighting on top of the game's baked lighting.
- More realistic sound occlusion: materials, partial cover, sound bending around corners.

Every enhancement stays optional, so the original look and sound are always one switch away.

## Status

The engine is nearly fully rewritten, and most of it already runs as our own code in
development builds. Every rewritten function is checked against the original while the
game runs, and only replaces it once both behave exactly the same. Next comes the custom
engine itself: the rewritten code assembled into a game of its own, with the planned
features built in. Its first release will replace the current one.

## How it works

The Xbox has an ordinary x86 processor, much like a PC, so the original game code can run
on a PC directly while its surroundings are replaced. That is how the playable release
works, and it is what makes the rewrite testable inside the real game:

1. **Name.** The game contains no names, only addresses. Its engine descends from
   Quake III Arena, whose source code is public, so many functions can be matched to
   their Quake III counterparts; the rest are named by working out what they do.
2. **Rewrite.** Each function becomes C++ that behaves exactly like the original, down to
   the order of its floating-point operations.
3. **Compare.** While the game runs, the original and the rewrite run side by side on
   the same input, and every register and every byte they write is compared.
4. **Replace.** Once every function of a part has passed, that part runs our code.

The mission scripts were compiled to machine code by the game's own script compiler.
They keep running as the original code from your disc, and our engine gives them
everything they expect from it.

More in [How it works](docs/developers/how-it-works.md) and
[The comparison harness](docs/developers/comparison-harness.md).

## Build from source

Linux is needed for development: the in-game comparison only works there for now.

```sh
git clone --recursive https://github.com/SmokeDoggy044/CoD2-BRO-Decomp.git
cd CoD2-BRO-Decomp

# Fedora: 32-bit libraries and vkd3d's shader compiler
sudo dnf install glibc-devel.i686 libstdc++-devel.i686 openal-soft-devel.i686 SDL3-devel.i686 vkd3d-compiler

cmake --preset linux-loader -DCOD_BRO_GAME_FILES=/path/to/your/game/dump
cmake --build --preset linux-loader
ctest --preset linux-loader
./build/linux-loader/bin/cod_bro_loader --level 1
```

Add `--compare` to run the rewritten functions side by side with the original. Windows
builds, release packaging and all options are described in
[Development](docs/developers/development.md).

## Project layout

```
src/
  engine/      the rewritten game code, one folder per part of the game
  runtime/     runs the original code on a PC: the program loader and the Xbox system layer
  render/      graphics (Direct3D 9 through DXVK, our own graphics interface)
  audio/       sound on OpenAL Soft
  input/       controllers, keyboard and mouse
  platform/    files, memory, threads, settings
  launcher/    the launcher
  text/        sharper text and button prompts
  online/      System Link
tests/         unit tests and the comparison tests
tools/         analysis, decompiling and testing tools; tools/data holds the names and results
docs/          documentation for developers, file formats and research notes
```

## Contributing

Bug reports and questions are welcome in the
[issues](https://github.com/SmokeDoggy044/CoD2-BRO-Decomp/issues); for problems while
playing, attach `log.txt` from your user data folder (`~/.local/share/cod_bro` or
`%APPDATA%\cod_bro`). To work on the code, start with [Development](docs/developers/development.md)
and the [tools we use](docs/developers/tools.md).

## Made with AI

This project is developed with AI. The function names, the rewritten code, the tools
and the documentation are written by Claude, Anthropic's AI model, working in
[Claude Code](https://claude.com/claude-code). The evidence for every function name is
recorded in `tools/data/manual_names.csv`, and every rewritten function is checked against
the original before it replaces it. The maintainer directs the work, decides what gets
built, playtests the builds, and reviews and publishes the results. Mistakes are the
project's own: please report them as issues.

## License

Our code is licensed under the [GNU General Public License, version 2 or later](LICENSE).
Bundled libraries and fonts come under their own licenses, listed in
[LICENSES/README.md](LICENSES/README.md).

*Call of Duty 2: Big Red One* and all of its code and data belong to their owners. This
project is not affiliated with or endorsed by Activision, Treyarch or Microsoft. Call of
Duty is a trademark of Activision Publishing, Inc.; Xbox is a trademark of Microsoft
Corporation.
