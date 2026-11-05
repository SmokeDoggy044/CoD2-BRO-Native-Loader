#pragma once

// Compiled level-script AI anim threads (levelscript module, .text, thread-switch push c): anim
// plays that run the note-track handler, face/pose/cover anims, look and aim loops. Every one
// reaches a script wait (directly or through a Scr_CallThread_* start): class thread_switch, kept
// out of kFunctions until script threads run as coroutines. Names are ours (from the bodies).

#include <cstdint>

#include "compiler.h"
#include "scr_access.h"
#include "script_string.h"

namespace game {

// 0x001ba4e0: "Times through: " + n, then a BroScr_HandleNoteTracksForever call thread on ent
// (0x6cd13551, 0x6d4893a9, 0, the text); the text is released after.
void XB_CDECL BroScr_DebugPrintTimesThrough(uint32_t ent, uint32_t n);
// 0x001ba580: BroScr_StandCombatRun(ent).
void XB_CDECL BroScr_StandCombatRun_2(uint32_t ent);
// 0x001ba590: plays `anim` on the body (notify 0x8d403614) and runs BroScr_HandleNoteTracks on it;
// then the actor int +0x184 = a3, key 0xccee5c1 = a4, key 0x764918ae = 0xc4a729a7 / 0xc028584c by
// `flag`; `after` (when set) gets the whole body. With `critical` the actor is held in a critical
// section the call did not already have.
void XB_CDECL BroScr_PlayPoseTransitionAnim(uint32_t ent, uint32_t anim, uint32_t a3, uint32_t a4, uint32_t flag,
                                            uint32_t critical, uint32_t after);
// 0x001ba730: the same with notify 0x8d403617, a BroScr_ProneAimFaceDefault call thread first
// (half the anim length, 0x97d5fe54, a3, a4, flag) and notify 0x97d5fe54 after the note tracks.
void XB_CDECL BroScr_PlayAnimWithNoteTracks(uint32_t ent, uint32_t anim, uint32_t a3, uint32_t a4, uint32_t flag,
                                            uint32_t critical, uint32_t after);
// 0x001bac80: plays `anim` from the root (notify 0xc5a136d6, blend 0.1), starts
// BroScr_WaitThreeEndNotes (0xc5a136d6, 0xf8a618a1 twice) and BroScr_NotifyAfterTime_3
// (0xf8a618a1 after length x 3 + 1), waits for 0xf8a618a1, notifies 0x47e458b4. Nothing without
// an anim.
void XB_CDECL BroScr_PlayAnimWaitEndNotes(uint32_t ent, uint32_t anim);

// 0x001bad70 / 0x001bb230 / 0x001bb610: the wounded face-anim loops while the actor int +0x184 is
// 0x19669cd9 (wounded), by the wounded pose 0xb7f3f874 (0x6088a8c crawl, 0x68698d3 kneel,
// 0x70b89a4 stand: +0x184 set to it). Orient "face current" during, "face default" after; `unused`
// is released. _3 (ends on 0x07b4e7f7): crawl -> knees; kneeling above 0.2 health crouches
// (0xc1165808), else shoots straight (clip key 0x0feddbea - 1) while the enemy is within 25 degrees,
// else stands once health > 0.5 - 4 x the kneeling time (idle 0.2 s steps). PlayFaceAnim: crawl /
// kneel go up when health > 0.1 + random 0.4, else idle. _2: `pose` (undefined/0: stand) picks
// prone pitch blends or crouch instead; any other wounded pose leaves without "face default".
void XB_CDECL BroScr_PlayFaceAnim_3(uint32_t ent, ScriptString unused);
void XB_CDECL BroScr_PlayFaceAnim(uint32_t ent, ScriptString unused);
void XB_CDECL BroScr_PlayFaceAnim_2(uint32_t ent, ScriptString unused, uint32_t pose);
// 0x001bb930: the wounded walk loop: crawling with move 0x385d90 loops the crawl (0.05 s) and kneels
// up + stands when the motion yaw leaves -30..30, else crawls on (move 0x3a09eb); other crawls
// stumble up (move 0x1c6ce); kneeling stands; standing (from move 0x385d90 via stand2run) stumbles
// into BroScr_StandCombatRun. `unused` is released.
void XB_CDECL BroScr_WoundedWalk(uint32_t ent, ScriptString unused);
// 0x001bf600: aim -> alert anim when key 0x764918ae is 0xc4a729a7: standing (wounded time, h8ef2ad7c
// > 0.5 or idle set 0x77: the wounded one, idle set 0x77), idle set 0x61 (one of two at random) or
// the "b" one; crouched / prone: crouch a (idle set 0x61) or b.
void XB_CDECL BroScr_AimToAlert(uint32_t ent);
// 0x001bf8b0: keeps aiming at `point`: standing / crouched shoot + aim knobs, weapon aim anims, aim
// params (anim object +0x3c..+0x50) and the blend time; prone the three prone shoot knobs and a
// BroScr_TurnToEnemy_2 call thread (0xf378e0c7, 0) with time 0.25. Waits the time clamped to
// min..max when positive, notifies 0xf378e0c7 and clears the Shoot anim.
void XB_CDECL Bro_aim__keep_aiming(uint32_t ent, ScrVec3 point, float min_time, float max_time);
// 0x001bfc50 / 0x001bfd80: desired pose crouch then prone / stand, applied (BroScr_ApplyDesiredAnimPose),
// move 0x385d90; with a turret: BroScr_Mg30calAnimTree(_2) on it and BroScr_AnimScript_TurretCall_1/_2
// on the actor, as call threads.
void XB_CDECL BroScr_SetDesiredPoseProne(uint32_t ent);
void XB_CDECL BroScr_SetDesiredPoseStandStop(uint32_t ent);
// 0x001bfe70 / 0x001c0130 / 0x001c02d0: look loops: the counter 0x747cde6c + 1, a kill notify
// (0xa538f810 / 0x671c92d3 / 0x1651c9e7) ending the previous loop, end on death / kill, a
// BroScr_LookAtUntilKillAnimscript_1/_2/_3 call thread with the counter, then every 2 s (position:
// 0.2 s) a 2 s look at the enemy (when valid) / the position / the entity (whose death also ends it).
void XB_CDECL BroScr_LookAtEnemyLoop(uint32_t ent);
void XB_CDECL BroScr_LookAtPositionLoop(uint32_t ent, uint32_t x, uint32_t y, uint32_t z);
void XB_CDECL BroScr_LookAtEntityLoop(uint32_t ent, uint32_t target);
// 0x001c0030: key 0x88c31fb5 = 0, BroScr_AimToAlert as a begun thread (Scr_BeginThreadFunctor1:
// switches), notifies 0xd6604ba3 / 0x0fc3c1c9 / 0x259b8110, key 0x33a4ea6c = 0, keys 0x27cc2178 and
// 0x7aeb941a removed, stop looking.
void XB_CDECL BroScr_StopForceShoot(uint32_t ent);

}  // namespace game
