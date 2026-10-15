# Licenses

The repository is private for now and will be made public later. This folder holds
the license texts of everything in it.

## The port's own code

Not chosen yet. Everything outside `third_party/` is original work of this project
and gets its license (file `LICENSE` at the repository root, plus the text here)
before the repository goes public.

Note for the choice: none of the headers below are compiled into any build; the
port has its own (`audio/dsound.h`, `online/xonline.h`, `render/xbox_d3d.h`, ...).
Only `xboxkrnl.h` (CC0) is read, by `tools/gen_kernel_exports.py`, to generate
`xbox/kernel_exports.inc`. So the GPL/LGPL headers put no condition on the port's
license; they are kept as reference only.

## Third-party code

`third_party/xbox-includes/` (github.com/mborgerson/xbox-includes, version in its
`VERSION` file; its own `COPYING` is the GPL-2.0 text):

| Files | License | Text |
|---|---|---|
| `d3d8.h`, `d3d8types.h`, `dsound.h`, `dsoundtypes.h`, `xacteng.h`, `xapi.h`, `xgraphic.h`, `xonline.h` (from Cxbx) | GPL-2.0-or-later | `GPL-2.0-or-later.txt` |
| `xboxkrnl.h` | CC0-1.0 | `CC0-1.0.txt` |
| `d3d8types_wine.h` (from Wine) | LGPL-2.1-or-later | `LGPL-2.1-or-later.txt` |
| `xboxdef.h` | no notice in the file; the package's `COPYING` (GPL-2.0) | `GPL-2.0-or-later.txt` |

## Not in this repository

Call of Duty 2: Big Red One and its files (XBE, assets) belong to their owners and
are never committed: the port loads them from the user's own copy of the game.
`tools/data/default_xbe_symbols.txt` lists function addresses and analysis names
from a Ghidra export, with no game code or data.
