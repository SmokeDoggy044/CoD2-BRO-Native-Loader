# Game files and disc dumps

The launcher recognises the player's game files, checks them, and extracts them from a disc dump
(`src/launcher/game_files.h`, `tests/game_files_test.cpp`; UI: Options, General, Game files).

## Editions

`src/launcher/game_files.txt` (installed beside the launcher) lists every file of each known edition: SHA-1, size
and path - no game data. Made by `tools/game_file_tables.py` from a disc dump or a folder:

```
tools/game_file_tables.py src/launcher/game_files.txt \
    "standard:Standard edition=Call of Duty 2 - Big Red One (World).xiso.iso" \
    "collectors:Collector's Edition=Collectors editions/COD: Collectors Editions"
```

| Edition | default.xbe SHA-1 | Files | Size |
|---|---|---|---|
| Standard (World) | bb33fd9d2fd2d667df9e669dcb6b47a347bdccbb | 644 | 2.97 GiB |
| Collector's Edition | 5a84a08720483e93add552cfdbf7fbf924658553 | 839 | 3.82 GiB |

The Collector's Edition has every path of the standard release plus `bonus_ce/` and six more movies; 330 of the
shared files differ (mostly the repacked `.cod` level packs), so each edition has its own rows.

## The check

The edition comes from `default.xbe`'s SHA-1. A known edition: every row must be present with its size and SHA-1
(paths compared without case); extra files are ignored. An unknown `default.xbe` (another region or print): only
the standard release's paths are checked, and the page says the edition is untested. The result names the
missing and damaged files. A folder chosen on the page is checked right away; Check re-runs it.

## Disc dumps

`DiscImage` reads the Xbox file system (XDVDFS: the volume descriptor at sector 32 with `MICROSOFT*XBOX*MEDIA`,
directory tables as binary trees of entries) from an XISO or from a full dump, whose game partition starts at
0x18300000 (XGD1), 0xFD90000 (XGD2) or 0x2080000 (XGD3). Dumps are 3-4 GB: reads go through `platform::File`
(64-bit positioned reads; 32-bit builds included). A dump whose file system points past its end is refused as an
incomplete copy.

Extraction checks the dump first, then writes each file as `<name>.part`, hashing it on the way, and renames it
only when its SHA-1 matches the edition's row; files of other names in the target folder are left alone. A
damaged file stops the extraction and names it.

Developer tool: `build/<preset>/bin/game_files_tool check SOURCE | extract DUMP TARGET`; the launcher page can be
opened on a dump without the dialog (`COD_BRO_LAUNCHER_PAGE=options COD_BRO_LAUNCHER_IMAGE=FILE`).
Checked: the standard XISO (22 s), the extracted standard and Collector's Edition folders, and an extraction of the
XISO - all 644 files identical to the reference dump; Windows build under Wine on the 3 GB dump.
