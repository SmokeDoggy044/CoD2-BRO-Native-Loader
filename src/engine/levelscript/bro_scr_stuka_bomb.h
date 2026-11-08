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

// 0x0016f040: ends on death; waits `delay` (0.5 when undefined); targetname "_stuka_bomb"; name =
// "bomb<int 0x380a75c4>_explosion". Then each round a trace 10000 down from the bomb (its end into
// the vector field 0xb9766e80): hits on `plane` or on an entity named 0x06b2a670 retry in 0.05 s;
// below the fraction `height` * 0.0001 (0.0064 when undefined), on a jump of the fraction by more than
// 0.5 over the last, at fraction 1 after a rise, or on a new non-"default" surface: notify `plane`
// with the name's hash and BroScr_StukaBombExplode(level, bomb, range, max, min). A fraction that
// did not rise waits 0.25 s at 1, else (as the rest) keeps the surface / fraction and waits 0.05 s.
void XB_CDECL BroScr_StukaBombImpact(uint32_t bomb, uint32_t plane, uint32_t delay, uint32_t range, uint32_t max_damage,
                                     uint32_t min_damage, uint32_t height);

// 0x0016fc30: with bomb_count set: up to `count` (bomb_count when undefined or larger; none at 0
// or less) of the plane's bombs (array field "bomb" 0x0030b1c2) not yet dropped: bomb_count of
// the bomb = its number, unlinked, the "Bomb_Whistle" sound (`whistle`), MoverMoveGravity along
// the plane's forward * speed for 5 s, BroScr_DeleteAfter5s / BroScr_RandomRotateLoop threads, a
// BroScr_StukaBombImpact thread (bomb, plane, delay..height); the plane's bomb_count - 1, the bomb
// dropped = 1, a wait of `interval` (RandomFloat(0.5) + 0.25 when undefined).
void XB_CDECL BroScr_PlaneDropBomb(uint32_t plane, uint32_t count, uint32_t interval, uint32_t delay, uint32_t range,
                                   uint32_t max_damage, uint32_t min_damage, uint32_t height, uint32_t whistle);

}  // namespace game
