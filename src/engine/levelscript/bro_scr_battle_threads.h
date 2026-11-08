#pragma once

// Compiled level-script battle threads (levelscript module, .text): the default-argument wrappers
// of the anim reaches, drone paths, plane bombs and mortar loops, and the shell-incoming impacts.
// Every one reaches a script wait (the wrapped thread bodies wait): class thread_switch, kept out
// of kFunctions until script threads run as coroutines. Names are ours (from the bodies).

#include <cstdint>

#include "compiler.h"
#include "qcommon_fs.h"
#include "scr_access.h"
#include "script_string.h"

namespace game {

// 0x0016d0c0 / 0x0016d1a0: BroScr_AnimReach / BroScr_AnimReachEnt(ent, a one-element array {guy}
// (copied), a3, a copy of `anime`, a5, a6); frees the array, releases `anime`.
void XB_CDECL BroScr_AnimReachCall(uint32_t ent, uint32_t guy, uint32_t a3, ScriptString anime, uint32_t a5, uint32_t a6);
void XB_CDECL BroScr_AnimReachEntCall(uint32_t ent, uint32_t guy, uint32_t a3, ScriptString anime, uint32_t a5, uint32_t a6);
// 0x0016d390: BroScr_DroneMain(drone, flag, 0).
void XB_CDECL BroScr_DroneMainCall(uint32_t drone, uint32_t flag);
// 0x0016f650: ends on 0xf9ed2681; waits exactly RandomFloatRange(min, max), then a
// BroScr_DroneMainCall thread on the drone with `flag` (Scr_StartThread_ent_b).
void XB_CDECL BroScr_DroneMainDelayed(uint32_t drone, uint32_t min, uint32_t max, uint32_t flag);
// 0x001705a0: BroScr_PlayShellIncoming(ent, type), then for a valid `ent`
// BroScr_MortarThinkWithDefaults(ent, a2..a7, type). 0x00171450: the same with a2..a7 the
// undefined float.
void XB_CDECL BroScr_ShellIncomingThenImpact(uint32_t ent, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6,
                                             uint32_t a7, uint32_t type);
void XB_CDECL BroScr_ShellIncomingThenImpact_2(uint32_t ent, uint32_t type);
// 0x001714e0: moves `ent` to `pos`, drops it to the ground (BroScr_DropToGround), then
// BroScr_ShellIncomingThenImpact_2(ent, type).
void XB_CDECL BroScr_ShellIncomingAtPos(uint32_t ent, uint32_t type, ScrVec3 pos);
// 0x00170670 / 0x00171920: BroScr_PlaneDropBomb(plane, a2..a8, true) / (plane, a2..a7, the undefined
// float, true).
void XB_CDECL BroScr_PlaneDropBombCall(uint32_t plane, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6,
                                       uint32_t a7, uint32_t a8);
void XB_CDECL BroScr_PlaneDropBombCall_2(uint32_t plane, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6,
                                         uint32_t a7);
// 0x00171f00: ends on "death"; BroScr_PlaneDropBomb(plane, the undefined int, it as a float, the
// undefined float, 0, 0, 0, the undefined float, true) forever.
void XB_CDECL BroScr_PlaneBombLoop(uint32_t plane);
// 0x001723b0..0x00172680: BroScr_DronePathSpawnLoop(a1, a2, a copy of `name`, p4..p9) with the
// missing arguments the word at 0x00922ebc (p4), the word at 0x00922ea4 (p5..p8) and the
// undefined bool (p9); releases `name`. _1: (none), _2: p9, _3: p4, _4: p4 p9, _5: p4..p6,
// _6: p4..p6 p9.
void XB_CDECL BroScr_DronePathSpawnLoopCall_1(uint32_t a1, uint32_t a2, ScriptString name);
void XB_CDECL BroScr_DronePathSpawnLoopCall_2(uint32_t a1, uint32_t a2, ScriptString name, uint32_t p9);
void XB_CDECL BroScr_DronePathSpawnLoopCall_3(uint32_t a1, uint32_t a2, ScriptString name, uint32_t p4);
void XB_CDECL BroScr_DronePathSpawnLoopCall_4(uint32_t a1, uint32_t a2, ScriptString name, uint32_t p4, uint32_t p9);
void XB_CDECL BroScr_DronePathSpawnLoopCall_5(uint32_t a1, uint32_t a2, ScriptString name, uint32_t p4, uint32_t p5,
                                              uint32_t p6);
void XB_CDECL BroScr_DronePathSpawnLoopCall_6(uint32_t a1, uint32_t a2, ScriptString name, uint32_t p4, uint32_t p5,
                                              uint32_t p6, uint32_t p9);
// 0x00172710..0x00172840: BroScr_MortarGroupShuffledLoop(ent, a copy of `group`, a, b), which never
// returns (the by-value arguments are never released): _1 (undefined handle x2), _2 (a, undefined
// handle), _3 (the hash of `name`, undefined handle), _4 (the hash of `name`, b).
void XB_CDECL BroScr_MortarGroupShuffledLoopCall_1(uint32_t ent, DynArray group);
void XB_CDECL BroScr_MortarGroupShuffledLoopCall_2(uint32_t ent, DynArray group, uint32_t a);
void XB_CDECL BroScr_MortarGroupShuffledLoopCall_3(uint32_t ent, DynArray group, ScriptString name);
void XB_CDECL BroScr_MortarGroupShuffledLoopCall_4(uint32_t ent, DynArray group, ScriptString name, uint32_t b);
// 0x00173320..0x00173710: BroScr_DronePathsStartAll(a1, a copy of `paths`, a copy of `name`,
// p6..p11) with the missing arguments as for the spawn-loop wrappers (0x00922ebc p6, 0x00922ea4
// p7..p10, the undefined bool p11); frees `paths`, releases `name`. 0x001737e0: the _6 shape for
// BroScr_DronePathsStartAllStaggered.
void XB_CDECL BroScr_DronePathsStartAllCall_1(uint32_t a1, DynArray paths, ScriptString name);
void XB_CDECL BroScr_DronePathsStartAllCall_2(uint32_t a1, DynArray paths, ScriptString name, uint32_t p11);
void XB_CDECL BroScr_DronePathsStartAllCall_3(uint32_t a1, DynArray paths, ScriptString name, uint32_t p6);
void XB_CDECL BroScr_DronePathsStartAllCall_4(uint32_t a1, DynArray paths, ScriptString name, uint32_t p6, uint32_t p11);
void XB_CDECL BroScr_DronePathsStartAllCall_5(uint32_t a1, DynArray paths, ScriptString name, uint32_t p6, uint32_t p7,
                                              uint32_t p8);
void XB_CDECL BroScr_DronePathsStartAllCall_6(uint32_t a1, DynArray paths, ScriptString name, uint32_t p6, uint32_t p7,
                                              uint32_t p8, uint32_t p11);
void XB_CDECL BroScr_DronePathsStartAllStaggeredCall(uint32_t a1, DynArray paths, ScriptString name, uint32_t p6,
                                                     uint32_t p7, uint32_t p8, uint32_t p11);

}  // namespace game
