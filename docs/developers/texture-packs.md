# Texture pack releases

The optional texture packs (World 4x, UI, Surface maps) are released as an index and data parts, installed per
pack by the launcher, and unpacked by the game. The stored files are encrypted with keys taken from the player's
own copy of the game: without the original textures they cannot be unpacked.

## Release files

| File | What |
|---|---|
| `cod_bro_textures.index` | the table of contents (text, below) |
| `cod_bro_textures.data.001`, `.002`, ... | the stored files back to back, each part under 1.9 GiB; a file never crosses a part |
| `SHA256SUMS.txt` | the index's and the parts' SHA-256 |

Index grammar (`src/platform/texture_index.h`; an installed `pack.idx` uses the same):

```
cod_bro_textures 1
release <version>
part <file name> <size> <sha256>            numbered from 1 in this order
pack <id>                                   world | ui | surface_maps, then that pack's lines
ini <mod.ini line>
file <id: 16 hex> <t|n|s> <part> <offset> <stored size> <size> <sha256 of the stored bytes>
```

`id` = the runtime content hash of the title texture (`render::hash_bytes`, `src/render/texture_hash.h`), the
same name the loose packs use. `t` = colour texture (`textures/<id>.dds`), `n` / `s` = surface maps
(`maps/<id>_normal.dds`, `maps/<id>_surface.dds`). Unknown words are ignored (later minor versions).

## Stored files

Each file = the DDS compressed with zstd (level 19), then ChaCha20 (RFC 8439, block counter 0):

- key = SHA-256("cod_bro texture key v1" || the original texture's bytes as the title holds them): the bytes the
  id hashes (DXT data as in the dump's DDS; uncompressed power-of-two textures Morton-swizzled, as
  `tools/texture_bridge.py`). Surface maps use their colour texture's key.
- nonce = the id (8 bytes, little-endian), then the kind (32-bit little-endian: 0 `t`, 1 `n`, 2 `s`).

Without the original texture there is no key; the file id (a 64-bit non-cryptographic hash) does not give it. A
wrong key fails at once in zstd's frame check. The scheme is public and a player with the game can capture
textures while it runs; it only keeps the packs unusable without the game.

## Builder: `tools/texture_pack/publish.py`

```
publish.py OUT --pack world=WORK/mod --pack ui=UI/mod --pack surface_maps=WORK/maps_mod            # publish
publish.py OUT --pack ... --stage check            # parts' checksums, every file unpacked + compared byte for byte
publish.py OUT --stage install --install-to MODS   # the installed form, to cross-check the launcher's
```

The keys come from the dump (`--unpacked`, `--manifest`; cached in `OUT/keys.csv`, never published). Release 0.1:
5,185 files, 3.5 GB stored in 2 parts (the three zips: 3.9 GB); publish takes under 3 minutes on 8 cores.

## Installed form and the engine

`<data>/mods/<pack>/mod.ini` + `pack.idx` (one part, `pack.dat`) + `pack.dat` (the pack's stored files, still
encrypted). `render::texture_pack::open` reads `pack.idx` when a mod folder has one (loose `textures/` and `maps/`
otherwise; both kinds mix in one mods folder, load order as before).

Keys: `BackendConfig::replace_texture` and `GfxDevice::replacement_texture` carry the title's texture memory
(`original`) next to the content hash. The first ask for a content with packed files copies those bytes; the
loading thread hashes them into the key, then reads, decrypts and decompresses the file there (no work on the
drawing thread beyond the copy). Surface map jobs that come before their key wait for it and are queued again
when it is known.

## Launcher

`src/launcher/texture_packs.h` (no UI; `tests/texture_packs_test.cpp`) and the Data Files page's Texture Packs
tab (`main.cpp`):

- Source: the newest release of the pack repository (GitHub API, pre-releases included) or a folder of
  downloaded files ("Install from a folder..."). The index is checked against `SHA256SUMS.txt` (required for the
  web). `COD_BRO_TEXTURE_SOURCE=<address>` replaces the GitHub lookup (test servers; plain http is accepted only
  on 127.0.0.1).
- Install / Update: works in `mods/<pack>.download/`; files an installed older version holds (same id, kind and
  checksum) are copied from it, the rest are read as runs of back-to-back files, one range request per run
  (`platform::https_download`; a server without ranges has the bytes before the offset skipped). Every file is
  checked against its SHA-256 before it is written. Network errors: up to 4 attempts per run, each from the first
  file not yet written. An interrupted or cancelled install resumes: the download folder's files are checked
  again and kept up to the first that fails. The finished folder then replaces `mods/<pack>/`.
- Remove deletes the folder (after asking). Packs need the game files set up first.

Developer tool: `build/<preset>/bin/texture_pack_install MODS SOURCE PACK...` runs the same install without the
window (`COD_BRO_CANCEL_AFTER=BYTES` for resume checks). Checked on release 0.1: from a folder, over http with
ranges, without ranges, with dropped connections, and cancelled + resumed - every installed file identical to
`publish.py --stage install`.
