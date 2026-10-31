# Call of Duty 2: Big Red One - native loader for PC

Play the original Xbox version of *Call of Duty 2: Big Red One* on PC, on Linux and
Windows. You need your own copy of the game: this repository and its releases contain
none of the game's code or data.

The loader runs the game's original code directly on your PC's CPU (the Xbox has an x86
CPU too) and replaces the Xbox around it. Parts of the game, such as the renderer and
the sound system, already run as rewritten source code. It is not an emulator or a
recompilation; see [how it works](docs/how-it-works.md). It is developed with AI; see
[Made with AI](#made-with-ai).

## Playing

1. Download the archive for your system from the
   [releases](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases):
   `cod_bro-<version>-linux.tar.gz` or `cod_bro-<version>-windows.zip`.
2. Unpack it anywhere and copy your game dump into its `assets/` folder.
3. Start `cod_bro_launcher`. It sets the options and starts the game, and tells you
   when a new release is out (*Update now* installs it; nothing is installed unless
   you click it).

| | Linux | Windows |
|---|---|---|
| System | x86 (64- or 32-bit), glibc 2.30 or newer | 64-bit Windows 10 or 11 |
| Graphics | Vulkan 1.3 with 32-bit drivers (DXVK included) | Vulkan 1.3 through the included DXVK, or else the Direct3D 9 that comes with Windows |
| Other | 32-bit X11/Wayland and PulseAudio/PipeWire libraries (as 32-bit Steam games need) | - |

Controls and options are in the `README.txt` inside the archive. Saves and settings
are kept in your user data folder (`~/.local/share/cod_bro` or `%APPDATA%\cod_bro`).

## Features

- The whole single-player campaign.
- 60 fps (or the game's 30), higher render resolutions, the game's widescreen mode
  (wider ultrawide experimental), borderless fullscreen, a field of view setting.
- Controllers, and keyboard and mouse with your own key bindings.
- **Enhanced graphics** and **Enhanced sound**: optional improvements, each on its own
  switch, for those who want them; switched off, you get the picture and sound as on
  the Xbox.
  - Graphics: anti-aliasing, anisotropic filtering, sharpening, colour vibrance,
    texture detail.
  - Sound: a higher-quality resampler, more voices, reverb, occlusion behind walls,
    louder close gunfire.
- Multiplayer over System Link on the local network
  ([docs/multiplayer.md](docs/multiplayer.md)); Xbox Live is not available.
- Other releases of the game (the Collector's Edition, the European
  French/Spanish/Italian disc) are recognised.

## Antivirus warnings (Windows)

Some antivirus programs flag `cod_bro_launcher.exe` and `cod_bro_loader.exe` as
`Gen:Variant.Babar.740485`. On VirusTotal, for 0.2.0, 8 of 71 engines report this for the
launcher and 7 of 71 for the loader.
We believe these are false positives, and we would rather tell you why than have you
find out by surprise:

- **"Gen:Variant" is a generic match** on behaviour, not a known piece of malware.
- **The launcher downloads and installs updates.** *Update now* fetches the new release
  from this repository, checks it against SHA256SUMS.txt, unpacks it with Windows' own
  `tar` and replaces the program files in its folder: what malware "downloaders" do too.
- **The loader runs another program's code.** `cod_bro_loader.exe` is a tiny program
  that reserves memory at fixed addresses, loads the original game's code from your dump
  into it and changes parts of it while it runs. That is how the native loader works,
  and also what some malware does.
- **The files are not code-signed.** A signing certificate costs money every year, and
  this is a free hobby project.

To check for yourself: every release's notes link to the VirusTotal results of its
files; all source code is in this repository and you can build it yourself
([docs/development.md](docs/development.md)); the checksums are in SHA256SUMS.txt. The
launcher is optional: `cod_bro_loader.exe` runs the game on its own (with
`settings.ini`), and you can update by downloading releases yourself.

## Status

Version 0.2.0 is the last feature release of the native loader: from here it gets bug
fixes only. Work continues in the
[decompilation repository](https://github.com/SmokeDoggy044/CoD2-BRO-Decomp): rewriting
the rest of the game, then a native build of our own that needs only the game's assets.
Details and next steps: [docs/status.md](docs/status.md). Problems and questions:
[issues](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues) (attach
`log.txt` from your user data folder).

## Made with AI

This project is developed with AI. Most of its code, the reverse engineering of the
game (the Ghidra analysis behind every patch and replaced library), the function
names, the decompiled game code and its documentation were written by Claude,
Anthropic's AI model, working in Claude Code. The evidence for each function name is
recorded in `tools/data/manual_names.csv`, and every rewritten function is checked
against the original by the comparison harness before it replaces it. The maintainer
directs the work, decides what gets built, playtests the builds, and reviews and
publishes the results. Mistakes are the project's own: please report them as issues.

## License

The native loader's own code (everything outside `third_party/`), including the
rewritten game code in `game/code/`, is licensed under the **GNU General Public
License, version 2 or (at your option) any later version** ([LICENSE](LICENSE)).

The releases bundle these libraries, each under its own license (texts and
versions in `LICENSES/` inside every archive):

| Component | Used for | License |
|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) | Window, controllers, keyboard and mouse | zlib |
| [OpenAL Soft](https://github.com/kcat/openal-soft) | Sound | LGPL-2.0-or-later (a separate library you can replace) |
| [DXVK](https://github.com/doitsujin/dxvk) | Direct3D 9 on Vulkan | zlib |
| [Dear ImGui](https://github.com/ocornut/imgui) | The launcher's interface | MIT |
| [stb_truetype](https://github.com/nothings/stb) | Drawing fonts for sharper text | MIT / public domain |
| EB Garamond, Jost, Liberation Serif, Anton (`fonts/`) | Sharper text: stand-ins for the game's bitmap fonts | SIL Open Font License 1.1 ([fonts/README.md](fonts/README.md)) |
| GCC runtime, mingw-w64 (Windows only) | C/C++ runtime | GPL-3.0 with the GCC runtime library exception; mingw-w64 terms |

The `third_party/xbox-includes` submodule (Xbox kernel and SDK headers; GPL-2.0,
CC0 and LGPL-2.1 files) is not compiled into any build. File-by-file details are
in [LICENSES/README.md](LICENSES/README.md).

**The game is not included.** *Call of Duty 2: Big Red One* and all of its code and
data belong to Activision Publishing, Inc., a subsidiary of Microsoft Corporation,
and their respective copyright holders. You need your own legally obtained copy of
the game; piracy is illegal. The symbol files, function names and test results in
`tools/data/` come from our own analysis. This project is not affiliated with or
endorsed by Microsoft, Activision or Treyarch. Call of Duty is a trademark of
Activision Publishing, Inc.; Xbox is a trademark of Microsoft Corporation.

## Documentation

| File | Contents |
|---|---|
| [docs/how-it-works.md](docs/how-it-works.md) | How the native loader works; the rewritten renderer |
| [docs/development.md](docs/development.md) | Building, running, testing, tools, the repository's layout, releases |
| [docs/status.md](docs/status.md) | What works, playtest results, session notes, next steps |
| [docs/own-renderer.md](docs/own-renderer.md) | Our own renderer and Enhanced graphics |
| [docs/rendering.md](docs/rendering.md) | The Direct3D replacement: render scale, widescreen, frame dumps |
| [docs/comparison-harness.md](docs/comparison-harness.md) | Checking rewritten functions against the original |
| [docs/multiplayer.md](docs/multiplayer.md) | System Link multiplayer |
| [docs/playtesting.md](docs/playtesting.md) | `--diag` sessions, scripted and unattended runs |
| [docs/naming.md](docs/naming.md), [docs/nal_animation.md](docs/nal_animation.md) | How functions get their names; the NAL animation codec |
| [assets/README.md](assets/README.md) | For players: where the game files go |
| [LICENSES/README.md](LICENSES/README.md) | Licenses of everything in the repository |
