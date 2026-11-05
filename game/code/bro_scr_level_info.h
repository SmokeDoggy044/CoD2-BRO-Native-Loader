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

}  // namespace game
