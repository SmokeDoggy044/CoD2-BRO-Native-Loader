#pragma once

// Compiled level-script drones (scripted soldiers on spline paths).

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "scr_access.h"
#include "script_string.h"

namespace game {

// BroScr_DroneMain threads (Scr_StartThread_ent_b_b: bytes 0 and `flag`) on each valid drone of
// the level's list (0x00922ee0 +0x770) whose origin is within the radius of `ent`'s:
// 0x0016a920 64 units (flag 0), 0x0016c9d0 384 units (flag 1).
void XB_CDECL BroScr_StartDronesNearEnt(uint32_t ent);
void XB_CDECL BroScr_StukaStartNearbyDrones(uint32_t ent);
// 0x0016d280: a BroScr_DroneMain thread (functor {0x007f4a7c, function, drone, 0, 0}) on every
// entity with targetname `name`. Releases `name`.
void XB_CDECL BroScr_StartDronesByTargetname(uint32_t self, ScriptString name);
// 0x001731c0 (dead): per entity with targetname `name`: a BroScr_DroneDeathThink thread
// (Scr_BeginThread_ent_str, ends on 0xe2a210cf) with the level object word and the entity's
// script_noteworthy. Releases `name`.
void XB_CDECL BroScr_StartDroneDeathThreads(uint32_t self, ScriptString name);

// 0x00169810: a drone script_model at `origin` (GScr_Spawn in the zone at that point) unless the
// level's drone count (+0x508, 0 when undefined) has reached its limit (+0x5a8, 40 when
// undefined: then *out = the word at 0x00922e84). Counts it, entity flag 0x2000000, a random
// model of the type's list (level +0x814), group "drone", forced LOD min(lod, 4) unless
// undefined; when the type has guns (level +0x274) field 0x1a7e7545 = a random one and, with
// drone_gun defined, GScr_DetachEx of it from tag_weapon_right. *out = the drone; releases
// `type`; returns out. Not registered (thread_switch: GScr_Spawn runs the new entity's script).
uint32_t* XB_CDECL BroScr_SpawnDrone(uint32_t* out, ScrVec3 origin, ScriptString type, uint32_t lod);
// 0x00170200: builds the path's spline (BroScr_DroneSplineMath into array field 0xb89aec99) when
// empty and the start field 0x6bd99abb is 0; spawns a drone at the start (lod field 0xce29c482),
// targetname = the path's + "_drone", animname = type; adds it to the level's drone list unless
// the spawner's script_explode is 1 or more; threads BroScr_DroneAnimLoop and
// BroScr_DroneFollowSpline(path, flag byte); BroScr_DroneMainDelayed(min, max, flag) when both
// delays are defined. Releases `type`. The path's argument slot ends with a delay word. Not
// registered (thread_switch through BroScr_SpawnDrone).
void XB_CDECL BroScr_DronePathSpawnOne(uint32_t spawner, uint32_t path, ScriptString type, float delay_min, float delay_max,
                                       uint32_t flag);

}  // namespace game
