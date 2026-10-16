# Licenses

This folder holds the license texts of everything in the repository and in the
releases.

## The port's own code

Everything outside `third_party/` is original work of this project:

    Copyright (C) 2026 the CoD2-BRO-Native-Loader contributors

    This program is free software; you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free Software
    Foundation; either version 2 of the License, or (at your option) any later
    version.

    This program is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
    PARTICULAR PURPOSE. See the GNU General Public License for more details.

The license text is `LICENSE` at the repository root (also
`GPL-2.0-or-later.txt` here).

None of the headers below are compiled into any build; the port has its own
(`audio/xbox_dsound.h`, `online/xonline.h`, `render/xbox_d3d.h`, ...). Only
`xboxkrnl.h` (CC0) is read, by `tools/gen_kernel_exports.py`, to generate
`xbox/kernel_exports.inc`. GPL-2.0-or-later code from them (or from Cxbx-Reloaded
or xemu) could be used directly, since the port has the same license.

## Third-party code

`third_party/xbox-includes/` (a git submodule of github.com/mborgerson/xbox-includes,
pinned at commit 0f8e252 of 2024-07-30; its own `COPYING` is the GPL-2.0 text; its
`Makefile` and `xbox.cpp` are not built):

| Files | License | Text |
|---|---|---|
| `d3d8.h`, `d3d8types.h`, `dsound.h`, `dsoundtypes.h`, `xacteng.h`, `xapi.h`, `xgraphic.h`, `xonline.h` (from Cxbx) | GPL-2.0-or-later | `GPL-2.0-or-later.txt` |
| `xboxkrnl.h` | CC0-1.0 | `CC0-1.0.txt` |
| `d3d8types_wine.h` (from Wine) | LGPL-2.1-or-later | `LGPL-2.1-or-later.txt` |
| `xboxdef.h` | no notice in the file; the package's `COPYING` (GPL-2.0) | `GPL-2.0-or-later.txt` |

Fetched by CMake at configure time, not stored here: Dear ImGui (MIT,
github.com/ocornut/imgui, tag v1.91.9) for the launcher; on Windows also SDL3
(zlib) and OpenAL Soft (LGPL-2.0-or-later). Releases include their license texts
and those of every other bundled library (`LICENSES/BUNDLED.txt` in an archive).

## Not in this repository

Call of Duty 2: Big Red One and its files (XBE, assets) belong to their owners and
are never committed: the port loads them from the user's own copy of the game.
`tools/data/default_xbe_symbols.txt` lists function addresses and analysis names
from a Ghidra export, with no game code or data.
