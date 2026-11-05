#pragma once

// Compiled level-script friendly / fallback / chain threads (levelscript module): fallback node
// chains, friendly chains, balcony nodes, MG42 use, turn to enemy, the hangar chain. Every one waits
// (Scr_Wait*, Scr_WaitTill*, a note-track wait, Scr_CallThread*): class thread_switch, kept out of
// kFunctions until script threads run as coroutines. Names are ours.

#include <cstdint>

#include "compiler.h"
#include "script_string.h"

namespace game {

// 0x0019a9a0: ends on death; goal = the "targetname" node of the target (kept in 0xa33a251d), goal
// radius from field 0x99ccf0cc (a BroScr_SetGoalRadiusFromNodeOnNotify thread) or the node's radius
// (512 without); then per "fallback" notify: actor float +0x7b8 = 20, a BroScr_NotifyFallbackReachedGoal
// level thread, the debug thread with cvar fallback 1, and on to the node's target (goal radius
// 0x7e4 = 500, old one kept in 0x62271501, BroScr_FallbackOnGoalRestore); at the end of the chain
// notifies "fallback_arrived" + int 0x8bbe4822 on the level.
void XB_CDECL BroScr_FallbackNodeThink(uint32_t ent);
// 0x001a7010: ends on level 0xa264707f. Mode 1: waits for the ent's death (attacker out); an
// "allies" (default) ent killed by the player or (level "pavlov") a misc_turret starts
// BroScr_FriendlyFireMissionFail (on `self`) with field 0x7599373d 1 or level +0xdc below 4, else
// clears level +0xf4 / +0xdc; an "axis" one counts +0xdc up. Other modes: a living sentient's
// script_noteworthy / team picks its damage (0xce270885) / death ent vars (the _spawner_gmi
// KillFriends / Allies / Axis handlers). Releases `team`.
void XB_CDECL BroScr_SetupFriendlyFireMissionFail(uint32_t self, uint32_t ent, uint32_t mode, ScriptString team);
// 0x001b3450: health scaled by 0x83eaf4; ends on 0xb633d42e / death; counts the chain's living AI
// (array 0x8969d6d6) with int 0x94ce2dcb 0; below the 0x8cd778 limit (or no nodes) the goal is node
// 0 (0x94ce2dcb = 0), else a random later node of array 0x0605894e (kept in 0x94ce2dcb); waits for
// the goal, then the goal node from slot 0x009226ec(ent, 0x8cd76c, 1).
void XB_CDECL BroScr_FriendlyChainThink(uint32_t ent, uint32_t chain);
// 0x001b5280: unless bool field 0xa287dc52; ends on death / "stop_going_to_node". Without a node:
// one of the target's "targetname" nodes (random with script_random, else the level's round-robin
// +0x198). A misc_mg42 / misc_turret the node targets gets a BroScr_MgWaitNewUser thread; balcony
// node kept, goal = the node (BroScr_SetAIFlagsUntilNotify with field 0x73623f0d; radius 0 and a
// goal wait with 0x99ccf0cc), radius = the node's (512 without one or script_radius); then on to
// that turret (radius 4, "goal" wait, BroScr_UseNodeTurret).
void XB_CDECL BroScr_GoToBalconyNode(uint32_t ent, uint32_t node);
// 0x001b7e70 (FUN_001b7e70, name ours): the anim-script defaults after BroScr_AnimScriptEntInit
// (pose / hand / time keys, gun in hand, combat run anim), a BroScr_HeadLookMainLoop_Thunk thread
// begun (Scr_WaitFrames(0) in a thread), actor int +0xf0 0 -> 0x36b1a5, clip refilled, field
// 0x01757a0e by weapon fire type (scaled for "axis"), int 0x77749be9 = 0xfff55d77.
void XB_CDECL BroScr_AnimScriptEntSetDefaults(uint32_t ent);
// 0x001b84a0 / 0x001b99c0: end on 0x07b4e7f7 / death / `end`; (TurnToEnemy waits 0xa6f66bfd first)
// int 0xb66101f1 = 0, `callback`(ent, 0xfd17b5c8 / 0x1c417c3e) thread. Loop: not prone, "face
// default"; prone, "face current" and the yaw to the enemy against the angles: with 0xb66101f1 the
// prone straight / left45 / right blend, past 45 degrees the prone turn anim (TurnToEnemy also puts
// the gun in hand) and the callback again; then a BroScr_NotifyOnNotify(_2) / NotifyAfterTime(_2)
// pair and the wait for its notify. _2 works the angle in floats.
void XB_CDECL BroScr_TurnToEnemy(uint32_t ent, uint32_t callback, uint32_t end);
void XB_CDECL BroScr_TurnToEnemy_2(uint32_t ent, uint32_t callback, uint32_t end);
// 0x001b58e0: ends on "death" / the turret's "friendly_finished_using_mg42"; a
// BroScr_NotifyMg42FinishedOnDeath level thread; goal radius 28 to the node (old radius in
// 0xe223c6aa, BroScr_RestoreGoalRadiusOnGoal, actor +0x1308 / +0x7e4 kept and cleared), waits for
// the goal and restores them. With the player near the node: the finished notify at once; else
// the turret in 0xaf6fd78f, hint / finished-wait threads, the turret used (BroScr_NotifyFinishedUsingMg42
// on the hint it targets) each second while near the node and the goal node, then the notify.
void XB_CDECL BroScr_FriendlyUseMg42(uint32_t ent, uint32_t turret, uint32_t node);
// 0x001b3990: ends on level 0x011066c4; every 0.25 s over the level's chain lists (+0x8c, +0x98):
// invalid chains dropped, on each idle chain (bool 0x14b95ada clear) dead / invalid AI dropped (the
// +0x98 chains' AI get goal radius 128) and notify 0xce19d6b0; then level notify 0x1b179a21.
void XB_CDECL BroScr_HangarChainUpdateLoop(uint32_t self);

}  // namespace game
