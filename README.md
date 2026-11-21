<div align="center">

# Call of Duty 2: Big Red One — Custom Engine

**Our own engine implementation of *Call of Duty 2: Big Red One*, built from source for Linux and Windows.**

[![License: GPL-2.0-or-later](https://img.shields.io/badge/license-GPL--2.0--or--later-blue)](LICENSE)
![Platforms: Linux | Windows](https://img.shields.io/badge/platforms-Linux%20%7C%20Windows-lightgrey)
[![Release](https://img.shields.io/github/v/release/SmokeDoggy044/CoD2-BRO-Native-Loader?label=release)](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases)
![Made with AI](https://img.shields.io/badge/made%20with-AI-8A2BE2)

[Play](#play) · [Comparison](#original-and-custom-engine) · [Features](#features) · [Status](#status) · [Build](#build-from-source) · [Documentation](docs/README.md) · [Made with AI](#made-with-ai)

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

The whole single-player campaign is playable on PC.

1. Download the latest release for Linux or Windows from the
   [releases page](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases).
2. Unpack it and start `cod_bro_launcher`.
3. In **Options**, **General**, choose **Extract from a disc dump...** and pick the dump
   of your game disc (or copy already extracted game files into the `assets/` folder).
4. Pick your options and play.

<p align="center"><img src="docs/images/launcher.png" alt="The launcher" width="700"></p>

The launcher checks your game files before using them: it recognises the edition (the
standard release or the Collector's Edition) and compares every file with it, so a
damaged or incomplete copy is caught before you play, naming the files. It also tells
you when a new release is out and installs it with one click, and its **Data Files**
page installs the optional [texture packs](#texture-packs).

| | Linux | Windows |
|---|---|---|
| System | x86 (64- or 32-bit), glibc 2.30 or newer | 64-bit Windows 10 or 11 |
| Graphics | Vulkan 1.3 (Direct3D 9 through the bundled DXVK as a fallback) | Vulkan 1.3 (Direct3D 9 as a fallback) |

## Antivirus warnings (Windows)

Some antivirus programs flag the launcher and the game's starter programs (`cod_bro.exe`,
`cod_bro_loader.exe`); the current numbers are in the table below. The Linux files show no
detections. We believe these are false positives, and we would rather tell you why than have
you find out by surprise:

- **"Gen:Variant" is a generic match** on behaviour, not a known piece of malware.
- **The launcher downloads and installs updates and texture packs.** *Update now* fetches
  the new release from this repository, checks it against SHA256SUMS.txt, unpacks it with
  Windows' own `tar` and replaces the program files in its folder: what malware
  "downloaders" do too.
- **The starter programs reserve memory at fixed addresses** and load the original game's
  data and mission scripts from your copy into it, which some malware also does. To do that
  they are tiny (5 KB, no C runtime), sit at a fixed address without address randomisation
  and carry a large empty memory area: a shape some scanners associate with packed programs
  (Sophos' `Mal/EncPk`), although the files are neither packed nor encrypted.
- **The files are not code-signed.** A signing certificate costs money every year, and
  this is a free hobby project.

### Transparency: VirusTotal results

We check every release's Windows programs on [VirusTotal](https://www.virustotal.com) and
publish the results here, with what we have done about them. Each file name links to its
public VirusTotal page, where you can see every engine's verdict yourself.

Release 1.0.10, checked on 21 November 2026 (1.0.9's numbers for comparison):

| File | Engines that flag it | 1.0.9 | What they report |
|---|---|---|---|
| [`cod_bro_launcher.exe`](https://www.virustotal.com/gui/file/ef6d540cb48120e60b71f7bbdb21142a8acb4dc711767b5532a4ecf76d1c25db) | 9 of 70 | 8 | `Gen:Variant.Babar.740485` from Bitdefender and six engines that use Bitdefender's (ALYac, Arcabit, Emsisoft, GData, eScan, VIPRE); CTX; a machine-learning score from Trapmine |
| [`cod_bro.exe`](https://www.virustotal.com/gui/file/b4576aefdfeaac686f0bf35d7113ffb2b9def89fda073d91413880392b59b08f) | 6 of 71 | 6 | `Mal/EncPk-OJ` from Sophos and ZoneAlarm (which uses Sophos'); Malwarebytes' machine-learning `Anomalous`; unnamed verdicts from APEX and Google; `Trojan/Win32.Agent` from Antiy |
| [`cod_bro_loader.exe`](https://www.virustotal.com/gui/file/752aad63531502c7ecfee83500b00a5ab624a575e10b1166654aa6bcfba792f6) | 6 of 71 | 6 | the same as `cod_bro.exe` (1.0.9's was also flagged by Microsoft's machine-learning `Trojan:Win32/Wacatac.B!ml`; 1.0.10's is not) |

What we have done:

- **Bitdefender** confirmed `Gen:Variant.Babar.740485` as a false positive (21 November 2026);
  until their correction is released, the same signature still matches 1.0.10's launcher, and
  it then reaches the engines that use theirs over the following days.
- **Microsoft:** a false-positive report for 1.0.9's starters is in progress; Microsoft's engine
  does not flag 1.0.10's.
- **The starters stay small and plain on purpose.** Adding an icon to them in a test build
  raised their detections from 6 to about 20, so they have none; the launcher has one.

Machine-learning detections ("ML", "MachineLearning", "Anomalous", `!ml`) are guesses
from a file's shape, not matches with known malware, and they often change from one
release to the next.

**Every update can change these results.** Each release builds new program files, and
many vendors clear a false positive only for the exact file they were sent. So a new
release may be flagged again, sometimes by engines that passed the previous one, until we
report it and the vendor reviews it. That review is done by the vendors and takes time,
usually days, during which a fresh release can show more detections than the one before.
We check each release when it comes out, report what is flagged, and keep this table up
to date.

To check for yourself: all source code is in this repository and you can build it
yourself ([Development](docs/developers/development.md)); the checksums are in
SHA256SUMS.txt. The launcher is optional: `cod_bro.exe` runs the game on its own (with
`settings.ini`), and you can update by downloading releases yourself.

The project is developed on Linux, so Windows antivirus warnings like these can slip past
us; please report them as an issue when you see one. Thanks to the reporter of
[issue #15](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/15) and to
Reddit user GroundbreakingMap719 for bringing this to our attention.

## Original and custom engine

The same moments of the campaign, on the left as the Xbox shows them (the engine's
*Original* graphics), on the right with Enhanced Graphics and the texture packs.

<!-- comparison:start -->
<table>
<tr><th width="50%">Original</th><th width="50%">Custom engine</th></tr>
<tr><td colspan="2" align="center"><b>Prologue</b></td></tr>
<tr><td><img src="docs/images/comparison/m0_2578_original.jpg" alt="Prologue, original"></td><td><img src="docs/images/comparison/m0_2578_engine.jpg" alt="Prologue, custom engine"></td></tr>
<tr><td><img src="docs/images/comparison/m0_18402_original.jpg" alt="Prologue, original"></td><td><img src="docs/images/comparison/m0_18402_engine.jpg" alt="Prologue, custom engine"></td></tr>
<tr><td colspan="2" align="center"><b>Oran</b></td></tr>
<tr><td><img src="docs/images/comparison/m1_37032_original.jpg" alt="Oran, original"></td><td><img src="docs/images/comparison/m1_37032_engine.jpg" alt="Oran, custom engine"></td></tr>
<tr><td colspan="2" align="center"><b>Tunisia</b></td></tr>
<tr><td><img src="docs/images/comparison/m2_13228_original.jpg" alt="Tunisia, original"></td><td><img src="docs/images/comparison/m2_13228_engine.jpg" alt="Tunisia, custom engine"></td></tr>
<tr><td colspan="2" align="center"><b>Kasserine Pass</b></td></tr>
<tr><td><img src="docs/images/comparison/m3_44872_original.jpg" alt="Kasserine Pass, original"></td><td><img src="docs/images/comparison/m3_44872_engine.jpg" alt="Kasserine Pass, custom engine"></td></tr>
<tr><td colspan="2" align="center"><b>Kasserine</b></td></tr>
<tr><td><img src="docs/images/comparison/m4_3802_original.jpg" alt="Kasserine, original"></td><td><img src="docs/images/comparison/m4_3802_engine.jpg" alt="Kasserine, custom engine"></td></tr>
<tr><td><img src="docs/images/comparison/m4_35829_original.jpg" alt="Kasserine, original"></td><td><img src="docs/images/comparison/m4_35829_engine.jpg" alt="Kasserine, custom engine"></td></tr>
<tr><td colspan="2" align="center"><b>Liberator</b></td></tr>
<tr><td><img src="docs/images/comparison/m5_27443_original.jpg" alt="Liberator, original"></td><td><img src="docs/images/comparison/m5_27443_engine.jpg" alt="Liberator, custom engine"></td></tr>
<tr><td colspan="2" align="center"><b>Gela</b></td></tr>
<tr><td><img src="docs/images/comparison/m6_22726_original.jpg" alt="Gela, original"></td><td><img src="docs/images/comparison/m6_22726_engine.jpg" alt="Gela, custom engine"></td></tr>
<tr><td><img src="docs/images/comparison/m6_40427_original.jpg" alt="Gela, original"></td><td><img src="docs/images/comparison/m6_40427_engine.jpg" alt="Gela, custom engine"></td></tr>
<tr><td colspan="2" align="center"><b>Piano Lupo</b></td></tr>
<tr><td><img src="docs/images/comparison/m7_28773_original.jpg" alt="Piano Lupo, original"></td><td><img src="docs/images/comparison/m7_28773_engine.jpg" alt="Piano Lupo, custom engine"></td></tr>
<tr><td><img src="docs/images/comparison/m7_51311_original.jpg" alt="Piano Lupo, original"></td><td><img src="docs/images/comparison/m7_51311_engine.jpg" alt="Piano Lupo, custom engine"></td></tr>
<tr><td><img src="docs/images/comparison/m7_73850_original.jpg" alt="Piano Lupo, original"></td><td><img src="docs/images/comparison/m7_73850_engine.jpg" alt="Piano Lupo, custom engine"></td></tr>
<tr><td colspan="2" align="center"><b>Omaha Beach</b></td></tr>
<tr><td><img src="docs/images/comparison/m9_28439_original.jpg" alt="Omaha Beach, original"></td><td><img src="docs/images/comparison/m9_28439_engine.jpg" alt="Omaha Beach, custom engine"></td></tr>
<tr><td><img src="docs/images/comparison/m9_50711_original.jpg" alt="Omaha Beach, original"></td><td><img src="docs/images/comparison/m9_50711_engine.jpg" alt="Omaha Beach, custom engine"></td></tr>
<tr><td><img src="docs/images/comparison/m9_61847_original.jpg" alt="Omaha Beach, original"></td><td><img src="docs/images/comparison/m9_61847_engine.jpg" alt="Omaha Beach, custom engine"></td></tr>
<tr><td><img src="docs/images/comparison/m9_72983_original.jpg" alt="Omaha Beach, original"></td><td><img src="docs/images/comparison/m9_72983_engine.jpg" alt="Omaha Beach, custom engine"></td></tr>
<tr><td colspan="2" align="center"><b>Eilendorf</b></td></tr>
<tr><td><img src="docs/images/comparison/m11_5409_original.jpg" alt="Eilendorf, original"></td><td><img src="docs/images/comparison/m11_5409_engine.jpg" alt="Eilendorf, custom engine"></td></tr>
<tr><td><img src="docs/images/comparison/m11_43886_original.jpg" alt="Eilendorf, original"></td><td><img src="docs/images/comparison/m11_43886_engine.jpg" alt="Eilendorf, custom engine"></td></tr>
<tr><td><img src="docs/images/comparison/m11_63124_original.jpg" alt="Eilendorf, original"></td><td><img src="docs/images/comparison/m11_63124_engine.jpg" alt="Eilendorf, custom engine"></td></tr>
</table>
<!-- comparison:end -->

Every enhancement has its own switch, and *Original* turns them all off at once: the
picture and sound are then the Xbox's.

## Features

**Playing on PC**

- The whole single-player campaign, in English, German, French, Spanish or Italian
  (as your disc has them).
- Smooth frame pacing at your display's refresh rate (60 Hz and up), any render
  resolution, borderless fullscreen.
- Proper widescreen: a wider view instead of a cropped one, the HUD at the screen's
  edges, a field of view setting; the menus stay 4:3 with soft blurred sides.
- Keyboard and mouse with your own key bindings, mouse control in every menu, and
  key pictures in the button prompts. Controllers work as on the Xbox.
- All settings in the game's own Options menus (Controls, Graphics, Sound) and in the
  pause menu, and every option in the launcher too.
- Sharper text: the game's fonts redrawn at your resolution.
- Multiplayer over System Link on your local network.

**Enhanced Graphics** (Vulkan renderer)

- Sun shadows from characters and vehicles, on top of the game's baked shadows.
- Lighting per pixel, normal maps and shine on surfaces, and the levels' muzzle flashes
  and explosions lighting the world per pixel.
- Ambient occlusion in corners and creases.
- Rounder characters, weapons and props (model smoothing).
- Anti-aliasing, anisotropic filtering, sharper distant textures, sharpening and
  vibrance.

**Enhanced Sound**

- Sound through walls and doors depending on the material, quieter behind partial cover,
  and bending through openings and round corners.
- Reverb and echoes from the room you are in, measured from the level itself, and the
  high tones of distant sounds fading in the air.
- More voices at once, close gunfire and passing bullets that carry weight, and
  cutscene sound in stereo.

### Texture packs

Optional, installed from the launcher's **Data Files** page:

| Pack | What | Size |
|---|---|---|
| PBRify 4x textures | the levels, weapons, vehicles and characters upscaled 4x | 605 MB |
| UI textures | menus, HUD and fonts upscaled | 36 MB |
| Surface maps | normal, height and surface maps for Enhanced Graphics' lighting | 2.9 GB |

The packs only work with your own copy of the game: every texture in them is locked with
a key that only the matching original texture gives.

## Status

The single-player engine is rewritten: renderer, sound, script engine, game logic,
animation, collision, menus and the engine core run as our own code. Every rewritten
function was checked against the original while the game ran, and replaced it only once
both behaved exactly the same. Recorded play-throughs of the missions check every build
(Linux and Windows) against each other. The mission scripts still run as the original code from your disc, and our
engine gives them everything they expect from it. The multiplayer still runs its original
code.

## How it works

The Xbox has an ordinary x86 processor, much like a PC, so the original game code can run
on a PC directly while its surroundings are replaced. That is what made the rewrite
testable inside the real game:

1. **Name.** The game contains no names, only addresses. Its engine descends from
   Quake III Arena, whose source code is public, so many functions can be matched to
   their Quake III counterparts; the rest are named by working out what they do.
2. **Rewrite.** Each function becomes C++ that behaves exactly like the original, down to
   the order of its floating-point operations.
3. **Compare.** While the game runs, the original and the rewrite run side by side on
   the same input, and every register and every byte they write is compared.
4. **Replace.** Once every function of a part has passed, that part runs our code.

The mission scripts were compiled to machine code by the game's own script compiler.
They keep running as the original code from your disc, behind a narrow boundary to our
engine ([the level boundary](docs/formats/level_boundary.md)).

More in [How it works](docs/developers/how-it-works.md),
[The comparison harness](docs/developers/comparison-harness.md) and
[The engine host](docs/developers/own-engine-host.md).

## Build from source

Linux is needed for development: the in-game comparison only works there for now.

```sh
git clone --recursive https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader.git
cd CoD2-BRO-Native-Loader

# Fedora: 32-bit libraries and the shader compilers
sudo dnf install glibc-devel.i686 libstdc++-devel.i686 openal-soft-devel.i686 vkd3d-compiler glslang

cmake --preset linux-loader-release -DCOD_BRO_GAME_FILES=/path/to/your/game/dump
cmake --build --preset linux-loader-release
ctest --preset linux-loader-release
./build/linux-loader-release/bin/cod_bro_launcher
```

Windows builds (MinGW), release packaging, the recording and replay tools and all options
are described in [Development](docs/developers/development.md).

## Project layout

```
src/
  engine/      the rewritten game code, one folder per part of the game
  runtime/     our executable, and the system layer the original mission scripts run on
  render/      graphics: Vulkan and Direct3D 9 back ends behind our graphics interface
  audio/       sound on OpenAL Soft, including the sound propagation
  input/       controllers, keyboard and mouse
  platform/    files, memory, threads, settings, downloads
  launcher/    the launcher
  text/        sharper text and button prompts
  online/      System Link
tests/         unit tests and the comparison tests
tools/         analysis, decompiling, testing and texture pack tools
docs/          documentation for developers, file formats and research notes
third_party/   bundled sources (zstd's decoder, Xbox headers)
```

## Contributing

Bug reports and questions are welcome in the
[issues](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues); for problems while
playing, attach `log.txt` from your user data folder (`~/.local/share/cod_bro` or
`%APPDATA%\cod_bro`). To work on the code, start with [Development](docs/developers/development.md)
and the [tools we use](docs/developers/tools.md).

## Thanks

Thanks to everyone who reported a problem or suggested something, many of them with logs,
screenshots and patient retesting. Where each report stands:

| | Report | State |
|---|---|---|
| [cibere11](https://github.com/cibere11) | [#1](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/1) Language setting | Added in [0.1.5](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v0.1.5) |
| [jbond2003](https://github.com/jbond2003) | [#2](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/2) Input needing the window clicked | Could not be reproduced yet |
| [jbond2003](https://github.com/jbond2003) | [#3](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/3) Menus without a keyboard | Fixed in [0.1.5](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v0.1.5) |
| [jbond2003](https://github.com/jbond2003) | [#4](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/4) Controller prompts with keyboard and mouse | Fixed in [0.1.5](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v0.1.5) |
| [armitom](https://github.com/armitom) | [#5](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/5) Field of view setting | Added in [0.1.9](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v0.1.9) |
| [Skiner111](https://github.com/Skiner111) | [#6](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/6) Black screen on Intel HD 3000 | Fixed |
| [cibere11](https://github.com/cibere11) | [#7](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/7) Automatic updates | Added in [0.1.7](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v0.1.7) |
| [Skiner111](https://github.com/Skiner111) | [#8](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/8) Low GPU use with Direct3D 9 | Renderer changed since; not confirmed |
| [ElTioRata](https://github.com/ElTioRata) | [#9](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/9) Multiplayer start-up crash | Fixed in [0.1.13](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v0.1.13) |
| [ZehuaZhang](https://github.com/ZehuaZhang) | [#10](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/10) Ultrawide support | Added in [0.1.9](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v0.1.9) (experimental) |
| [ZehuaZhang](https://github.com/ZehuaZhang) | [#11](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/11) Thompson firing on its own | Not fixed yet; needs a log |
| [YaroslavValerySablin](https://github.com/YaroslavValerySablin) | [#12](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/12) Russian and other languages | Russian and Ukrainian added in [1.0.8](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.8) (AI drafts, corrections welcome) |
| [IIFLIPII](https://github.com/IIFLIPII) | [#13](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/13) Sprint | Added in [1.0.3](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.3) |
| [SneakiestDuke68](https://github.com/SneakiestDuke68) | [#14](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/14) Binding the arrow keys, Enter and Backspace | Added in [0.2.1](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v0.2.1) |
| [dosndr2017-sys](https://github.com/dosndr2017-sys) | [#16](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/16) "Cannot set up the Xbox memory" on Windows | Fixed in [0.2.2](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v0.2.2) |
| [Hutchyy9](https://github.com/Hutchyy9) | [#17](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/17) Hang before the second mission | Traced to damaged game files, not the game |
| [ZIRBY112](https://github.com/ZIRBY112) | [#19](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/19) Cutscene bars, sniper and tank aiming | Fixed in [1.0.0](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.0) |
| [kratosac57-cyber](https://github.com/kratosac57-cyber) | [#20](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/20) The European disc with Spanish | Works since [1.0.3](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.3) |
| [roioros](https://github.com/roioros) | [#22](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/22) Cheats (god mode, infinite ammo) | God mode and infinite ammo added in [1.0.10](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.10) (Cheats page) |
| [Robin613507](https://github.com/Robin613507) | [#23](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/23) Cutscene bars, more bodies, hide HUD, skip intro, PC wording | Added in [1.0.5](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.5) and [1.0.7](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.7); "PC" in the save messages and a fix for mouse freezes in the menus in [1.0.9](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.9); more than 16 bodies and draw distance on hold |
| [dosndr2017-sys](https://github.com/dosndr2017-sys) | [#24](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/24) Crash in The Great Crusade | Fixed in [1.0.4](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.4) |
| [KraxxBR1](https://github.com/KraxxBR1) | [#25](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/25) Game closing at start-up (AMD) | Crash now logged ([1.0.7](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.7)); a change to the shaders for AMD's driver in [1.0.10](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.10), not confirmed yet; Portuguese (Brazil) text added in [1.0.10](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.10) |
| [kratosac57-cyber](https://github.com/kratosac57-cyber) | [#26](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/26) Closing after the prologue; quitting to Windows; pause menu delay | Fixed in [1.0.6](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.6); Quit Game in [1.0.7](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.7); pause menu delay: likely cause fixed in [1.0.9](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.9), not confirmed yet |
| [Jmon86](https://github.com/Jmon86) | [#27](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/27) Controller crash, black screen, missing sounds, not starting again, hit markers | Fixed in [1.0.6](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.6) and [1.0.8](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.8); hit markers now switch on at once in [1.0.9](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.9) |
| [invictus39](https://github.com/invictus39) | [#28](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/28) Vulkan crash on AMD | Crash traced to the first shader compile with [1.0.9](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.9)'s crash report; a change to the shaders in [1.0.10](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.10), not confirmed yet; the pause-menu prompts fixed in [1.0.10](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.10) |
| [SneakiestDuke68](https://github.com/SneakiestDuke68) | [#29](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/29) Ambient occlusion lines; Polish | Fixed in [1.0.7](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.7) (confirmed); Polish added in [1.0.7](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.7) |
| [ninjapro2828](https://github.com/ninjapro2828) | [#30](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/30) Missing gunfire, silent missions, not starting again | Fixed in [1.0.8](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.8) |
| [Sol110405](https://github.com/Sol110405) | [#32](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/32) "Broc Pool empty" | Fixed in [1.0.8](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.8) |
| [Tuko-Mas](https://github.com/Tuko-Mas) | [#33](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/33) Closing at start-up (AMD) | A change to the shaders for AMD's driver in [1.0.10](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.10), not confirmed yet |
| [Lucifer4ikSX](https://github.com/Lucifer4ikSX) | [#28](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/28) Logs of the AMD Vulkan crash | A change to the shaders for AMD's driver in [1.0.10](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.10), not confirmed yet |
| [MaximeClonen](https://github.com/MaximeClonen) | [#35](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/35) Achievements and an Insane difficulty | Suggestion, not planned yet |
| [Military-Man](https://github.com/Military-Man) | [#38](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/38) Game not opening | Waiting for a log |
| [ertup0](https://github.com/ertup0) | [#39](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/issues/39) Free camera and a UI toggle | Free camera added in [1.0.10](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.10) (Cheats page); the HUD toggle (H) since [1.0.7](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases/tag/v1.0.7) |

Thanks also to everyone who reported an antivirus warning (see [Antivirus warnings](#antivirus-warnings-windows)),
and to everyone who plays the game this way and tells others about it.

## Made with AI

This project is developed with AI. The function names, the rewritten code, the tools
and the documentation are written by Claude, Anthropic's AI model, working in
[Claude Code](https://claude.com/claude-code). The evidence for every function name is
recorded in `tools/data/manual_names.csv`, and every rewritten function is checked against
the original before it replaces it. The maintainer directs the work, decides what gets
built, playtests the builds, and reviews and publishes the results. Mistakes are the
project's own: please report them as issues.

## History

Up to version 0.2.2 this repository held the native loader: the original game code ran
on the PC with our replacements for the Xbox around it, while the engine was rewritten in
[CoD2-BRO-Decomp](https://github.com/SmokeDoggy044/CoD2-BRO-Decomp). Version 1.0.0
replaces it with the custom engine, and this repository's history with the engine's.
Forks made before keep the loader's history; its releases stay on the
[releases page](https://github.com/SmokeDoggy044/CoD2-BRO-Native-Loader/releases).

## License

Our code is licensed under the [GNU General Public License, version 2 or later](LICENSE).
Bundled libraries and fonts come under their own licenses, listed in
[LICENSES/README.md](LICENSES/README.md).

*Call of Duty 2: Big Red One* and all of its code and data belong to their owners. This
project is not affiliated with or endorsed by Activision, Treyarch or Microsoft. Call of
Duty is a trademark of Activision Publishing, Inc.; Xbox is a trademark of Microsoft
Corporation.
