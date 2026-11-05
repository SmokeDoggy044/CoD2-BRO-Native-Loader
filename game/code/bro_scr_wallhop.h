#pragma once

// Compiled level-script helpers of the player's wall hop (levelscript module, .text): the script
// origins beyond a wall, the hop's midpoint and landing spot. Names are ours (from the bodies).

#include <cstdint>

#include "bro_scr_util.h"
#include "compiler.h"
#include "scr_access.h"

namespace game {

// 0x00124db0: out = player origin + (to - player origin) * 0.5, z raised by 60, plus `offset` when
// any of its lanes is not the undefined lane. The second argument is unused. Returns out.
uint32_t* XB_CDECL BroScr_WallHopMidpoint(uint32_t* out, uint32_t unused, ScrVec3 to, ScrVec3 offset);
// 0x0012f4d0: the ground below the player origin + forward(player angles) * distance(player, to)
// (BroScr_TraceGroundPos_3 skipping `skip`), raised by 72. Returns out.
uint32_t* XB_CDECL BroScr_WallHopMove(uint32_t* out, uint32_t skip, ScrVec3 to);
// 0x00147000: appends the origin of each "script_origin" `ent` targets to its 12-byte array field
// 0x4f7c0a56.
void XB_CDECL BroScr_WallHopGetScriptOrigins(uint32_t ent);
// 0x001471f0 (args: ent, then a by-value array of 12-byte points that is filled and freed): the two
// first points of `ent`'s field 0x4f7c0a56 copied into it, the one nearer the player first
// (Scr_Closer). Nothing is kept.
BRO_SCR_ARGS_DECL(BroScr_WallHopGetPointsNearFirst)
// 0x00147470 / 0x001476b0 (dead): *out = the point of `ent`'s field 0x4f7c0a56 nearest the player.
uint32_t* XB_CDECL BroScr_GetClosestArrayPointToPlayer(uint32_t* out, uint32_t ent);
uint32_t* XB_CDECL BroScr_GetClosestArrayPointToPlayer_2(uint32_t* out, uint32_t ent);

}  // namespace game
