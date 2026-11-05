#pragma once

// Compiled level-script path movers (levelscript module, .text 0x00173c50-0x001754ae): an entity
// moved along the origins of its target chain (array field 0x85644aaa of vec3s), with a
// BroScr_RandomMortarsAroundEnt thread on it. Every one waits (Scr_WaitTill): class thread_switch,
// kept out of kFunctions until script threads run as coroutines. Names are ours (from the bodies).

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00173c50: ends on 0x8ff01d8d; angles 0, bool 0xac835695 = false; a BroScr_RandomMortarsAroundEnt
// thread (ent, `type`, speed * 0.5, count, min, max); then per leg of the point list: forward /
// right of the leg's direction into the vector fields 0x6ff72745 / 0xe2e8d8a5, a 0.25 s yaw turn,
// MoveTo the next point in distance / `speed` s and a wait for 0x2a63b2d6. At the end bool
// 0xac835695 = true and notify 0x596979d8.
void XB_CDECL BroScr_PathMoverRun(uint32_t level, uint32_t ent, uint32_t type, int32_t speed, int32_t mortar_speed,
                                  uint32_t count, uint32_t min, uint32_t max);
// 0x00174d40: defaults speed 100, mortar speed 1000, count 500, min 0.5, max 2.0 for undefined
// ones; appends the origin of `ent` and of each entity of its target chain (by targetname) to its
// point list, then BroScr_PathMoverRun(level, ent, ...). The first argument is unused.
void XB_CDECL BroScr_PathMoverFromTargetChain(uint32_t unused, uint32_t ent, uint32_t type, uint32_t speed,
                                              uint32_t mortar_speed, uint32_t count, uint32_t min, uint32_t max);
// 0x00175320 / 0x00175370 / 0x001753c0 / 0x00175410 / 0x00175460 (dead: no calls, no pointers):
// BroScr_PathMoverFromTargetChain with the missing arguments undefined (handle for `type`, ints,
// floats). 0x00175460 is a function of its own (Ghidra counts it into 0x00175410).
void XB_CDECL BroScr_PathMoverFromTargetChainCall(uint32_t unused, uint32_t ent);
void XB_CDECL BroScr_PathMoverFromTargetChainCall_2(uint32_t unused, uint32_t ent, uint32_t type);
void XB_CDECL BroScr_PathMoverFromTargetChainCall_3(uint32_t unused, uint32_t ent, uint32_t type, uint32_t speed);
void XB_CDECL BroScr_PathMoverFromTargetChainCall_4(uint32_t unused, uint32_t ent, uint32_t type, uint32_t speed,
                                                    uint32_t mortar_speed);
void XB_CDECL BroScr_PathMoverFromTargetChainCall_5(uint32_t unused, uint32_t ent, uint32_t type, uint32_t speed,
                                                    uint32_t mortar_speed, uint32_t count);

}  // namespace game
