# Call of Duty 2: Big Red One — native loader for PC (modded)

Play the original Xbox version of *Call of Duty 2: Big Red One* on PC, on Linux
and Windows — with **PNG texture extraction/replacement**, **WAV sound
extraction/replacement**, **player-hands / weapon model / animation export** and
**Discord Rich Presence** built in.

You need your own copy of the game: this repository and its releases contain
none of the game's code or data.

The loader runs the game's original code directly on your PC's CPU (the Xbox has
an x86 CPU too) and replaces the Xbox around it. Parts of the game, such as the
renderer and the sound system, already run as rewritten source code. It is not an
emulator or a recompilation; see [how it works](docs/how-it-works.md). This fork
was developed with AI assistance.

## What's new in this fork

- **F4 → `Modding` window** with `Textures`, `Audio` and `Animations` tabs. Each
  tab has exactly two buttons — `Extract` and `Replace` — plus live extraction
  and replacement progress bars.
- **Textures are PNG.** `Extract` writes the game's DDS pictures (including DXT1,
  DXT3, DXT5, uncompressed, paletted and luminance formats) as editable PNGs;
  `Replace` loads those PNGs back into the running game, immediately, at any
  resolution. A `textures.csv` / `textures_manifest.csv` index keeps every
  picture paired with its in-game content hash.
- **Sounds are WAV.** `Extract` writes every wave bank and loose sound as
  16-bit PCM WAV with an `audio.csv` manifest (hash, source file, name, codec
  kind, channels, sample rate, frame count, exact byte length). `Replace`
  converts edited WAVs back to the game's original PCM8 / PCM16 / Xbox ADPCM
  encoding at the exact original byte length and writes patched copies under
  `mods/overrides/`; the game's files are never modified.
- **Animations export.** `Animations → Extract` dumps **all player view-hands,
  all weapon models and all animations** from the `.cod` packs and loose assets:
  `.xbmesh` / `.xbcell` / `.xbmorph` meshes, `.xmb` model descriptors, `.xbanim`
  / `.xbsanim` NAL clips and `.xbskel` skeletons. Files are written byte-for-byte
  into `mods/animations/viewhands/`, `mods/animations/weapons/<weapon>/`,
  `animations/`, `models/` and `skeletons/`, with an `animations.csv` manifest
  (pack, entry, byte offset, size, hash, group). `Replace` patches edited
  same-length assets into `mods/overrides/`. No conversion to glTF/FBX is
  claimed — these are the game's own Xbox formats, ready for CoD2 model tools.
- **Discord Rich Presence** with app ID `1567972853180010607`: "In the launcher"
  while configuring, then "Playing Call of Duty 2: Big Red One" with the current
  map name (no mission number) or "Multiplayer — System Link". It talks only to
  Discord's local IPC socket/pipe — no token, no network requests, no library.
- **X11 *and* Wayland** are linked and selected automatically on Linux: SDL3 gets
  `wayland,x11` on a Wayland session and `x11,wayland` on X11, so either display
  server works with the other as fallback. `SDL_VIDEO_DRIVER` or
  `COD_BRO_VIDEO_DRIVER` overrides it.
- **Prebuilt DXVK 2.7.1**: `packaging/dxvk/fetch-prebuilt.sh` downloads the
  official 32-bit Linux dxvk-native library/headers and the Windows `d3d9.dll`;
  releases bundle them. No DXVK source build is needed.
- **Application icon**: the supplied CoD2: BRO disc art is embedded in the
  Windows executables and shown in SDL windows on both platforms. Linux
  releases also ship a `.desktop` entry and an icon installer.
- **Automated releases**: a GitHub Actions workflow builds the Linux and
  Windows archives and publishes them, with `SHA256SUMS.txt`, on version tags.

## Playing

1. Download the archive for your system from the
   [releases](https://github.com/monsef64/CoD2-Bro-Modded/releases):
   `cod_bro-<version>-linux.tar.gz` or `cod_bro-<version>-windows.zip`.
2. Unpack it anywhere and copy your game dump into its `assets/` folder.
3. Start `cod_bro_launcher`. It sets the options and starts the game, and tells you
   when a new release is out (*Update now* installs it; nothing is installed unless
   you click it).

| | Linux | Windows |
|---|---|---|
| System | x86 (64- or 32-bit), glibc 2.30 or newer | 64-bit Windows 10 or 11 |
| Graphics | Vulkan 1.3 with 32-bit drivers (prebuilt DXVK included) | Vulkan 1.3 through the included prebuilt DXVK, or else the Direct3D 9 that comes with Windows |
| Other | 32-bit X11/Wayland and PulseAudio/PipeWire libraries (as 32-bit Steam games need) | - |

Controls and options are in the `README.txt` inside the archive. Saves and settings
are kept in your user data folder (`~/.local/share/cod_bro` or `%APPDATA%\cod_bro`).

### If the launcher window will not open

On some Wayland compositors SDL3 has no software framebuffer and no usable EGL,
so a 2D `SDL_Renderer` cannot be created (`Couldn't find matching render driver`).
The launcher now prefers X11 whenever `DISPLAY` is available (X11 works both on a
native X11 session and through XWayland), falls back through every available
render driver, and retries with the X11 backend if all of them fail. You can also
force one yourself:

```sh
SDL_VIDEO_DRIVER=x11 ./cod_bro_launcher          # X11 / XWayland
COD_BRO_VIDEO_DRIVER=x11 ./cod_bro_launcher     # same, our own override
SDL_VIDEO_DRIVER=wayland ./cod_bro_launcher     # native Wayland
SDL_RENDER_DRIVER=software ./cod_bro_launcher   # force the 2D software renderer
```

## Modding (F4)

Press **F4** in a level to open the `Modding` window. Escape or F4 closes it; the
same mouse/keyboard capture is shared with the older `` ` `` (grave) PC menu.

Where files live — `<user data>` is `~/.local/share/cod_bro` on Linux (or
`$XDG_DATA_HOME/cod_bro`) and `%APPDATA%\cod_bro` on Windows:

```
<user data>/mods/
├── textures/          PNGs written by Textures → Extract
├── textures.csv       PNG ↔ in-game texture hash index
├── audio/             WAVs written by Audio → Extract
├── audio.csv          WAV ↔ wave-bank entry manifest
├── animations/        hands, weapon models, animations (Animations → Extract)
│   ├── viewhands/     first-person hands models + their skeletons
│   ├── weapons/<name>/ one folder per weapon: models + its animations
│   ├── animations/    every other NAL clip
│   ├── models/        every other mesh / model descriptor
│   ├── skeletons/     every other bone hierarchy
│   └── animations.csv asset ↔ pack/offset/size/hash manifest
└── overrides/         patched assets written by Audio / Animations → Replace
```

### Textures tab

| Button | What it does |
|---|---|
| `Extract` | Scans `assets/*.cod` KAPF packs and loose `.dds` files, decodes every DDS mip 0 to RGBA, and writes `<hash>.png` plus `textures.csv` into `mods/textures/`. A progress bar shows files done / total and the current file. |
| `Replace` | Reads `textures.csv` and every PNG in `mods/textures/`, scales it to the original dimensions, converts it to A8R8G8B8 and registers it by content hash with the renderer. Existing textures are re-uploaded on the next draw, so changes appear **live**. Deleting a PNG and pressing Replace restores the original. |

Both operations also draw a second progress bar for the queued job, so long pack
scans never look frozen. Extraction and replacement run on a background thread;
the game keeps rendering while they work.

### Audio tab

| Button | What it does |
|---|---|
| `Extract` | Reads loose WAVs, `.wbk` wave banks and packed `.wbk` entries inside `.cod` files, decodes PCM8 / PCM16 / Xbox ADPCM (and float/PCM WAV), and writes `<hash>.wav` (16-bit PCM RIFF) into `mods/audio/` with `audio.csv`. |
| `Replace` | For every WAV in `mods/audio/`, resamples to the original rate/channels, re-encodes to the original codec and **exact byte length**, then writes the patched bank/WAV to `mods/overrides/<source>`. For loose WAV sounds (`kind=0`) it copies the WAV directly. Restart the level (or the game) to hear the new sound; remove the file from `overrides/` to restore the original. |

### Animations tab

Press `Extract` to export the whole model/animation family from the game's
`.cod` (KAPF) packs and loose files. This includes **every player view-hands
model** (the animated first-person arms), **every weapon's view and world
models**, and **every animation clip in the game** — not a hand-picked subset.
`Replace` puts edited files back without ever touching `assets/`.

| Button | What it does |
|---|---|
| `Extract` | Scans all `.cod` packs and loose assets for `.xbmesh`, `.xbcell`, `.xbmorph` (NGL meshes), `.xmb` (XModel descriptors), `.xbanim` / `.xbsanim` (NAL animation clips) and `.xbskel` (skeletons). Writes each distinct asset once, byte-for-byte, into `mods/animations/` — hands under `viewhands/`, weapon models and their animations under `weapons/<weapon>/`, the remaining clips under `animations/`, models under `models/`, skeletons under `skeletons/`. `animations.csv` records the source pack, original asset path, exact byte offset, exact size and content hash for every occurrence; hands/weapon grouping is in its `group` column. The progress bar shows assets done / total. |
| `Replace` | Reads `animations.csv`, compares every exported file with the bytes in the game file, and for files that changed **and kept the exact original byte length** writes a patched copy of the source `.cod` / loose file into `mods/overrides/<same path>`. Restart or reload the level to see the change; delete the override to restore the original. A changed-size file is reported as skipped, never truncated. |

These are the game's **original Xbox formats**, not glTF/FBX/OBJ: the port
exports them losslessly so CoD2 Xbox model/animation tools can edit them. The
KAPF pack directory stores each entry's size, so replacements must keep that
length; rebuilding a pack with new sizes is out of scope for the in-game tool.
Hands, weapons and shared clips are classified from their asset names
(`viewmodel_hands_*`, `viewmodel_*`, weapon names, …); the `animations.csv`
`category` and `group` columns make the classification explicit, and **all**
model/animation assets are exported regardless of category.

The override layer is read-only redirection in `xbox/filesystem.cpp`: the game
keeps reading its original paths, while `.wbk`, `.wav`, `.cod`, `.dds` and `.ff`
reads transparently prefer a same-named file under `mods/overrides/`. No file in
`assets/` is ever written to.

### Why the manifest matters

The game identifies textures by a hash of their bytes, sounds by bank entry
offsets and streamed assets by their KAPF pack entry, not by filename. The CSV
files record those identities (hash, source pack, data offset, data size, codec
kind, channels, rate, frames; `animations.csv` adds category, group, kind and
extension), which is what lets edited PNG/WAV/model/animation files go back to
exactly the right place without renaming anything in the game dump.

## Discord Rich Presence

- App ID: `1567972853180010607`.
- Launcher: **"Call of Duty 2: Big Red One" / "In the launcher"**, with the state
  *"Configuring the game"*.
- In game: **"Playing Call of Duty 2: Big Red One"**, with only the current map
  name read from the game's own `cl.mapname` — no mission number. When no map is
  loaded it falls back to *"Multiplayer — System Link"*, *"Single-player campaign"*
  or *"In the menus"* as appropriate.
- Enable/disable: the launcher's **General → Discord Rich Presence** checkbox
  (`discord-rpc = on|off` in `settings.ini`), `--discord-rpc on|off` on
  `cod_bro_loader`, or `COD_BRO_DISCORD_RPC=0` in the environment. Optional
  `COD_BRO_DISCORD_ASSET` sets the large-image key.
- If Discord isn't running, the game silently skips RPC and keeps playing. The
  implementation (`discord/rich_presence.cpp`) uses Discord's documented local
  IPC frames (handshake + `SET_ACTIVITY`) over a Unix socket or Windows named
  pipe — no SDK, no account data, no network traffic.

## Features (base port)

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

### Controls (additions)

| Key | Action |
|---|---|
| `F4` | Toggle the `Modding` window |
| `` ` `` | Toggle the original PC menu |
| `Escape` | Close whichever menu is open / pause |
| `F10` | Free the mouse |

## Building

The project is a 32-bit C++20 CMake build. DXVK and the SM3 shader bytecode are
prebuilt, so no meson/DXVK source build or HLSL compiler is needed. Download
DXVK's official 2.7.1 binaries once:

```sh
./packaging/dxvk/fetch-prebuilt.sh
```

The script stages into `dxvk/prebuilt/` (the binary directories are gitignored).
To stage somewhere else, pass a destination and configure with
`-DCOD_BRO_DXVK_PREBUILT_DIR=<dir>`; GitHub Actions uses `build/deps/dxvk`.

Linux (Ubuntu 25.04 or newer, with i386 development packages):

```sh
sudo dpkg --add-architecture i386 && sudo apt update
sudo apt install -y cmake ninja-build g++-multilib pkg-config \
    libsdl3-dev:i386 libopenal-dev:i386 libvulkan-dev:i386 \
    libwayland-dev:i386 libxkbcommon-dev:i386 libx11-dev:i386
cmake --preset linux-loader-release
cmake --build --preset linux-loader-release -j$(nproc)
cpack --preset linux-loader-release          # cod_bro-<version>-linux.tar.gz
```

Windows cross-build from Linux (MinGW-w64 i686; Wine is optional for tests):

```sh
sudo apt install -y mingw-w64
cmake --preset windows-mingw-release
cmake --build --preset windows-mingw-release -j$(nproc)
cpack --preset windows-mingw-release        # cod_bro-<version>-windows.zip
```

`cmake/dxvk_prebuilt.cmake` creates the `cod_bro_dxvk` interface target that
points `render/` and ImGui's D3D9 back end at the staged headers/library; it
falls back to an installed `dxvk-d3d9.pc` when no staged copy is present.
`platform/CMakeLists.txt` can link X11, Wayland and xkbcommon explicitly on
Linux (turn it off with `-DCOD_BRO_LINK_X11_WAYLAND=OFF` to let SDL3 load them
at run time). The bytecode in `render/*/prebuilt/` avoids an HLSL compiler
([docs/shader-prebuilt.md](docs/shader-prebuilt.md)).

The supplied icon is embedded in the Windows `.exe`/`.dll` resources and baked
into `platform/window_icon.cpp` for both platforms' SDL windows. Linux releases
also contain `cod_bro.png`, `cod_bro.desktop` and `install-linux-icon.sh`; run
the latter once to add the game to a desktop's applications menu.

Source layout additions: `modding/` (window, DDS/PNG, KAPF packs, WAV codecs,
wave banks, CSV and Xbox model/animation export), `discord/` (local IPC),
`platform/display_backends.*` and `platform/window_icon.*`,
`packaging/icons/`, `dxvk/prebuilt/` (version/license texts; binaries are
fetched), `packaging/dxvk/fetch-prebuilt.sh` and `.github/workflows/` (CI).
`tests/animation_test.cpp` exercises the Animations scan/Extract/Replace
pipeline on a synthetic KAPF dump. See
[docs/development.md](docs/development.md) for the full layout and options.

## Automated releases

`.github/workflows/release.yml` builds both archives on pushes and pull
requests. On a `v<major>.<minor>.<patch>` tag — or a manual dispatch with
`publish` checked — it publishes `cod_bro-<version>-linux.tar.gz`,
`cod_bro-<version>-windows.zip` and `SHA256SUMS.txt` to GitHub Releases. The
tag version must match `project(VERSION)` in `CMakeLists.txt`; the launcher's
updater expects those exact archive names.

## Antivirus warnings (Windows)

Some antivirus programs flag `cod_bro_launcher.exe` and `cod_bro_loader.exe` as
`Gen:Variant.Babar.740485`. We believe these are false positives, and we would
rather tell you why than have you find out by surprise:

- **"Gen:Variant" is a generic match** on behaviour, not a known piece of malware.
- **The launcher downloads and installs updates.** *Update now* fetches the new
  release from this repository, checks it against SHA256SUMS.txt, unpacks it with
  Windows' own `tar` and replaces the program files in its folder.
- **The loader runs another program's code.** `cod_bro_loader.exe` is a tiny
  program that reserves memory at fixed addresses, loads the original game's code
  from your dump into it and changes parts of it while it runs.
- **The files are not code-signed.** A signing certificate costs money every year,
  and this is a free hobby project.

All source code is in this repository and you can build it yourself
([docs/development.md](docs/development.md)); checksums are in SHA256SUMS.txt.
The launcher is optional: `cod_bro_loader.exe` runs the game on its own (with
`settings.ini`).

## Made with AI

This fork and the upstream port are developed with AI assistance. The reverse
engineering of the game, the rewritten game code, the modding pipeline and its
documentation were produced with AI tools under the maintainer's direction;
every rewritten function in the upstream project is checked against the original
by the comparison harness. Mistakes are the project's own: please report them as
issues.

## License

The native loader's own code (everything outside `third_party/`), including the
rewritten game code in `game/code/`, the new `modding/` and `discord/` modules,
is licensed under the **GNU General Public License, version 2 or (at your
option) any later version** ([LICENSE](LICENSE)).

The releases bundle these libraries, each under its own license (texts and
versions in `LICENSES/` inside every archive):

| Component | Used for | License |
|---|---|---|
| [SDL3](https://github.com/libsdl-org/SDL) | Window, controllers, keyboard and mouse | zlib |
| [OpenAL Soft](https://github.com/kcat/openal-soft) | Sound | LGPL-2.0-or-later (a separate library you can replace) |
| [DXVK 2.7.1](https://github.com/doitsujin/dxvk) (prebuilt) | Direct3D 9 on Vulkan; includes libdisplay-info | zlib / MIT (see `dxvk/prebuilt/LICENSES/`) |
| [Dear ImGui](https://github.com/ocornut/imgui) | Launcher and F4 Modding windows | MIT |
| [stb](https://github.com/nothings/stb) | `stb_truetype`, plus `stb_image` / `stb_image_write` for PNG modding | MIT / public domain (`modding/stb/LICENSE`) |
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
| [docs/shader-prebuilt.md](docs/shader-prebuilt.md) | Why the SM3 shader bytecode is prebuilt and how to regenerate it |
| [packaging/mods/README.txt](packaging/mods/README.txt) | Short modding guide shipped in every release |
| [assets/README.md](assets/README.md) | For players: where the game files go |
| [LICENSES/README.md](LICENSES/README.md) | Licenses of everything in the repository |
