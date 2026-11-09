CoD2: BRO mods
==============

Press F4 while playing to open the Modding window. It has three tabs:

  Textures   -> Extract   writes the game's DDS pictures as PNG into
                          <user data>/mods/textures/
               Replace   loads edited PNGs into the running game (live)

  Audio      -> Extract   writes the game's sounds as 16-bit PCM WAV into
                          <user data>/mods/audio/
               Replace   converts edited WAVs back and writes patched game-file
                          copies into <user data>/mods/overrides/
                          (restart the game or reload the level to hear them)

  Animations -> Extract   exports ALL player view-hands, EVERY weapon model and
                          EVERY animation (.xbmesh/.xbcell/.xbmorph/.xmb/
                          .xbanim/.xbsanim/.xbskel) from the .cod packs into
                          <user data>/mods/animations/:
                            viewhands/          first-person hands models
                            weapons/<weapon>/   weapon models + their anims
                            animations/         every other NAL clip
                            models/ skeletons/  everything else
                          plus animations.csv (pack, offset, size, hash, group).
               Replace   patches edited, SAME-LENGTH assets into copies of the
                          source game files under <user data>/mods/overrides/
                          (restart or reload the level to see them).

The exported model/animation files are the game's original Xbox formats, not
glTF/FBX: they are written byte-for-byte so CoD2 model tools can edit them.
Changed-size files are skipped by Replace (a KAPF pack stores entry sizes).

<user data> is:
  Linux    ~/.local/share/cod_bro   (or $XDG_DATA_HOME/cod_bro)
  Windows  %APPDATA%\cod_bro

The original files in assets/ are never modified. Delete a file from
mods/overrides/ to restore the original asset. See README.txt in the source
tree ("Modding" and "Discord Rich Presence") for the full workflow.
