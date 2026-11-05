#pragma once

// Compiled level-script world effects: tree bursts, window and stone breaks, minefields, loop
// sound origins.

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "script_string.h"

namespace game {

// 0x0015d510: *out = the level's vector for `name` (map at 0x00922ee0 +0x3b4, by
// Scr_HashScriptString; HashMap_Index inserts an undefined one when missing), or (0, 0, 0) when
// `name` is empty or all three lanes are the undefined lane. Releases `name`; returns out.
uint32_t* XB_CDECL BroScr_GetLevelVecOrZero(uint32_t* out, ScriptString name);

}  // namespace game
