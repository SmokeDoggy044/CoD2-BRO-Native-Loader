#pragma once

// Compiled level-script setup helpers (levelscript module, .text): exploder entities. Names are
// ours (from the bodies).

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00137d60: sets up one exploder piece. With an exploder number (GScr_GetIntMapValue of
// script_exploder; not undefined or 0): health items are deleted; "exploder" pieces get flag 0x400
// cleared, become solid (not with script_noteworthy "notsolid") and connect paths (spawnflag 1);
// "exploderchunk"/"exploderchunk visible" pieces are deleted with their target; others without
// script_fxid are deleted (connecting paths first). Without one: fx models (not chunks) and
// "exploder"/"exploderchunk" pieces get flag 0x400 (the latter also non-solid, paths connected),
// and the entity goes into the first free slot of the level's list at +0x78 (or is appended).
void XB_CDECL BroScr_SetupExploders(uint32_t ent);

// 0x00126a30: per-level start-up (from the level's load main): cg_victoryscreen_menu "off", level
// byte +0x7f4 cleared, campaign string +0x64 defaulted to "american"; then by level name (+0x1c4):
// levelshot cvars, American screens, campaign "american", normal health 1.0, the level's weapons and
// the one to switch to; +0x7f4 set for the campaign levels (not testmap/james1/others). "credits"
// stops after the name checks. The argument is only passed on to BroScr_SetAmericanScreens.
void XB_CDECL BroScr_SetupLevelInfo(uint32_t passed_on);

// 0x001398e0 (dead): fills a local array with the 24 campaign weapon names, gives the player max
// ammo (GScr_PlayerGiveMaxAmmoByAmmoType) for each one it has, then frees the array.
void XB_CDECL BroScr_InitWeaponNames();

}  // namespace game
