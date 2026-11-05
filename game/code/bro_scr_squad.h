#pragma once

// Compiled level-script helpers for AI groups, spawners, respawners and goal nodes (levelscript
// module, .text). Names are ours (from the bodies).

#include <cstdint>

#include "compiler.h"
#include "qcommon_fs.h"
#include "script_string.h"

namespace game {

// 0x0014c260 / 0x0014c400: appends to `out` every AI (GScr_GetAIAll) / spawner (GScr_GetSpawnerArray)
// whose string field 0x2dae9972 is set and equals `value` (`out` emptied first). Releases `value`.
void XB_CDECL BroScr_GetAIByGroupField(ScriptString value, DynArray* out);
void XB_CDECL BroScr_GetSpawnersByGroupField(ScriptString value, DynArray* out);
// 0x00152730 (dead): BroScr_GetAIByGroupField of a copy of `value` into a local array that is
// walked without use and freed. Releases `value`.
void XB_CDECL BroScr_GetAIByGroupFieldCall(ScriptString value);
// 0x001485f0: for each of the three gunner slots (8-byte array field 0x2cd69a2a): a valid, alive
// gunner gets notify 0xa0e65a8e, and the slot is cleared to {0, undefined bool}.
void XB_CDECL BroScr_ReleaseGunners(uint32_t ent);
// 0x0014b940: when `ent`'s string field 0x22cb4cdd is "special", counts `counted` up (slot 0x00922b88
// getter, 0x00922b8c setter). Else, once the alive count (0xaa7e54b7) equals the dead count
// (0x846e35f4), both are reset and every member of the array field 0xc548bcb7 is counted up.
void XB_CDECL BroScr_UpdateAliveDeadCount(uint32_t ent, uint32_t counted);
// 0x00138a40: appends every "weapon_panzerfaust" entity targetnamed "respawn" to `out`
// (BroScr_AppendIfValidEnt).
void XB_CDECL BroScr_GetPanzerfaustRespawns(DynArray* out);
// 0x0013c3f0 (dead): *out = the path node closest to `ent`'s origin among the nodes targetnamed
// `name` (all nodes when `name` is empty), 0x00922ea0 when there are none. Releases `name`.
uint32_t* XB_CDECL BroScr_GetClosestNodeToEnt(uint32_t* out, uint32_t ent, ScriptString name);
// 0x0013ab60 (dead): the axis spawner at the level's index +0x324: its count (slot 0x00922b88) into
// field 0x56aed3da, its origin into the vector field 0xcc045c84, count set to 1; *out = the spawner.
uint32_t* XB_CDECL BroScr_TakeSpawnerSaveState(uint32_t* out);
// 0x0013dfe0: a random node among those targetnamed like the target of `ent`'s node (handle field
// 0x05374cc6) becomes its goal (GScr_SetGoalPosFromNode) and its new node. With none, the field
// is removed (0x0011eb30).
void XB_CDECL BroScr_SetRandomGoalNode(uint32_t ent);
// 0x00149f60: a mortar entity: bool field 0xb2f8556f cleared; with a target, the targeted entities
// go into the array field 0x05c4ceb7 (flag set meanwhile; cleared again when the array is not
// defined), and the entity named by string field 0x16e6cf7e into handle field 0x28667a5b
// (GScr_SetEntFlag400 on it when set).
void XB_CDECL BroScr_MortarEntInit(uint32_t ent);
// 0x0013b5f0: `spawner` stops respawning for `owner` (once: bool field 0x18b0887b). With a valid
// `owner` whose respawn count (0x5f01d656) is set: the count goes down; below zero, notify
// 0x0c34930d on `owner`, on every entity of its group (string field 0xdfe2186c) and on `spawner`,
// field 0x788065eb of `spawner` cleared, and level notify "<group>_stopped_respawning". Without
// one, the same count on `spawner` itself (only 0x788065eb cleared below zero).
void XB_CDECL BroScr_StopRespawning(uint32_t spawner, uint32_t owner);
// 0x0013c150 (dead): the non-sentient entities of group `name` share `count` increments of their
// int field 0x78063481 (round robin; an undefined field starts at 0). Releases `name`.
void XB_CDECL BroScr_GetNonSentientsByGroupname(ScriptString name, int32_t count);

}  // namespace game
