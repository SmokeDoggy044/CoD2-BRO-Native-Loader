#pragma once

// Compiled level-script pose-move scripts (levelscript module, .text, thread-switch push c): the
// anim scripts' "pose_move" table entries that take an actor from its current pose (ScrActor_GetInt_184:
// stand 0x070b89a4 / crouch 0xc1165808 / prone 0x0738b1a5) and movement (anim_movement 0x0ccee5c1:
// stop 0x385d90 / walk 0x3a09eb / run 0x1c6ce) into the target one through the transition scripts.
// Every one reaches a script wait: class thread_switch, kept out of kFunctions until script threads
// run as coroutines. Names are ours (from the bodies).

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x001c8920: to stand / stop.
void XB_CDECL BroScr_PoseMove_StandStop(uint32_t ent);
// 0x001c8ad0: to stand / walk.
void XB_CDECL BroScr_PoseMove_StandWalk(uint32_t ent);
// 0x001c8c40: to stand / run.
void XB_CDECL BroScr_PoseMove_StandRun(uint32_t ent);
// 0x001c8db0: to crouch / stop.
void XB_CDECL BroScr_PoseMove_CrouchStop(uint32_t ent);
// 0x001c8f50: to crouch / walk.
void XB_CDECL BroScr_PoseMove_CrouchWalk(uint32_t ent);
// 0x001c90c0: to crouch / run.
void XB_CDECL BroScr_PoseMove_CrouchRun(uint32_t ent);
// 0x001c9210: to prone / stop.
void XB_CDECL BroScr_PoseMove_ProneStop(uint32_t ent);
// 0x001c9370: to a prone crawl with movement `move` (BroScr_ProneAimToCrawl; already prone and moving:
// just the movement set).
void XB_CDECL BroScr_PoseToProneCrawl(uint32_t ent, uint32_t move);
// 0x001cedf0 / 0x001cee10: BroScr_PoseToProneCrawl with walk / run.
void XB_CDECL BroScr_PoseMove_ProneWalk(uint32_t ent);
void XB_CDECL BroScr_PoseMove_ProneRun(uint32_t ent);

}  // namespace game
