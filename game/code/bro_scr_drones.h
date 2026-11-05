#pragma once

// Compiled level-script drones (scripted soldiers on spline paths).

#include <cstddef>
#include <cstdint>

#include "compiler.h"
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

}  // namespace game
