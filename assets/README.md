# Game assets

In the finished version, copy the game's files here (the contents of the disc:
`media/`, `sp/`, `mp/`, `textures/`, ...). The game looks for this folder next to
its executable, so a release is just the executables plus `assets/`. The launcher
(`cod_bro_launcher`) can point the game at another folder instead; it only tells
players to copy their game dump here, deliberately without dumping instructions.

Development builds use the same layout without copying 3 GB: CMake links
`build/<preset>/bin/assets` to the game files (`cod_bro/unpacked/` by default, CMake
setting `COD_BRO_GAME_FILES`). `--assets DIR` or the `COD_BRO_ASSETS` environment
variable override it.

The game never writes here; saves and the cache drive go to the per-user data
folder (see the main README).

Nothing in this folder except this README is tracked by git.
