#pragma once

// Compiled level-script stuka bombs (levelscript module, .text 0x0016cb40-0x0016ce40). Waits
// (Scr_WaitTill): class thread_switch, kept out of kFunctions until script threads run as
// coroutines. Names are ours (from the bodies).

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x0016cb40: only with the level's bomb sound (+0x264): a script_origin at the bomb, the sound
// on it (notify 0x45727144; on "piano_lupo" only beyond 250 of the player), the bomb deleted, an
// Earthquake(level +0x630 or 0.5, +0x268 or 2, +0x2a0 or 1024) and RadiusDamage(range 768, max
// 400, min 25 when undefined) there, level notify 0xa581b0e9, BroScr_StukaStartNearbyDrones, and
// a wait for the sound's notify. The first argument is unused.
void XB_CDECL BroScr_StukaBombExplode(uint32_t unused, uint32_t bomb, int32_t range, int32_t max_damage,
                                      int32_t min_damage);

}  // namespace game
