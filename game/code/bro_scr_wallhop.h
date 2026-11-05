#pragma once

// Compiled level-script helpers of the player's wall hop (levelscript module, .text): the script
// origins beyond a wall, the hop's midpoint and landing spot. Names are ours (from the bodies).

#include <cstdint>

#include "bro_scr_util.h"
#include "compiler.h"
#include "qcommon_fs.h"
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

// 0x00135870: resamples the hop path (12-byte points) into `out`: queues "stop_spline_math_drawing"
// on the level entity (+4), then walks each segment in `steps` steps of (next - point) / steps;
// each step moves a cursor (starting at the first point) toward the stepped position by its
// direction scaled by (cursor-to-position length / first-segment length) * step length. A cursor
// position is appended when `min_gap` is undefined or not above its distance from the last kept
// one; the path's last point is appended as is.
void XB_CDECL BroScr_PlayerHopSplineMath(DynArray* path, float steps, float min_gap, DynArray* out);

// Not registered (thread_switch, docs/levelscript_classes.csv): waits and starts a thread; the C is
// kept (reviewed against the disassembly, not compared) until script threads are coroutines.
// 0x001400c0 (0x001400c0-0x00140bcf; Ghidra's body also holds four more functions up to 0x0014183c):
// the player's hop over a wall. `points` (by value, freed at the end) gives the landing side's point
// 1; BroScr_WallHopMove finds the landing spot, BroScr_WallHopMidpoint the top (+60), and
// BroScr_PlayerHopSplineMath(start (player origin + 60), top, landing; 4, 4) the path. The player is
// linked to a "script_origin" linked to a second one (the anchor, at the player + 60, yawed toward the
// landing); BroScr_WallHopRotate runs as a thread on the level entity with the per-point times
// (distance * 0.0025, points of time <= 0.05 dropped); the anchor moves along the path (waits of
// time - 0.05 when above 0.1), then to the landing at distance * 0.00571429 s, waits for "movedone",
// unlinks the player and deletes both origins. `to` is only passed on to BroScr_WallHopMove (unread).
void XB_CDECL BroScr_PlayerHopOverWall(uint32_t level_ent, DynArray points, ScrVec3 to);

}  // namespace game
