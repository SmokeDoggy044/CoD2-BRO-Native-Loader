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

// 0x001c0460: the low-wall cover idle for the weapon (anim_special 0x175567a8 0x3124fe8f = in
// cover): faces the node or actor yaw ("face angle"); in cover, the idle set 0x62 rises
// (special 0x36b1a5), waits 0.75 s and settles back (idle set 0x61 / 0x63); else from the crouch
// pose movement into cover (pistol a, panzerfaust, or a / c by the idle set kept within anim
// object +0x11c, re-picked at random 30 %).
void XB_CDECL BroScr_StopCoverCrouchIdle(uint32_t ent);
// 0x001c0c10: the cover-crouch idle (the assert reads only build and drop their strings): pistol /
// no weapon play idle 1 / 2 or twitch 2 (idle set 0x63) by the idle set; a panzerfaust the low-wall
// idle (70 %) or twitch at rate 0.9 + random 0.2; others twitch / random idle / twitch 2.
void XB_CDECL Bro_cover_crouch__idle(uint32_t ent);
// 0x001c0f30: the cover-crouch reload: a panzerfaust stops the cover idle; not in cover goes in
// (crouch aim -> low wall b); then idle set 0x61 rises and settles, others c -> b, idle set 0x62.
// The 1384 string is read and dropped; `unused` is released.
void XB_CDECL BroScr_CoverCrouchReloadAnim(uint32_t ent, ScriptString unused);
// 0x001c1230: out of the low-wall cover to crouch aim by weapon / idle set (special 0x36b1a5).
void XB_CDECL BroScr_PlayCrouchIdleAnim(uint32_t ent);
// 0x001c1470: into the cover stand idle: from stand a stand2crouch_hide transition (idle set 0x62,
// in cover); else crouched (pose / movement set when not), in cover a / c settle to b, out of cover
// a crouch aim -> low wall b transition.
void XB_CDECL BroScr_CoverStandIdleTransition(uint32_t ent);
// 0x001c1770: "face enemy", crouch hide -> stand, special 0x36b1a5.
void XB_CDECL BroScr_FaceEnemyAnim(uint32_t ent);
// 0x001c25f0 / 0x001c2610 / 0x001c2640: begun threads (Scr_BeginThreadFunctor1, a1 = 1):
// BroScr_AnimScript_GrenadeCower; the 30cal stand / prone scripts set the actor int +0x1370 (fire
// anim) first and run BroScr_SetDesiredPoseStandStop / BroScr_SetDesiredPoseProne.
void XB_CDECL Bro_anim_main_grenade_cower(uint32_t ent);
void XB_CDECL Bro_b30cal_stand(uint32_t ent);
void XB_CDECL Bro_b30cal_prone(uint32_t ent);
// 0x001c2670 (inside Ghidra's Bro_b30cal_prone, which ends at 0x001c2670; pushed by
// Bro_anim_lmg_gmi; proposed BroScr_SetDesiredPoseProneMg42) / 0x001c27a0 / 0x001c28d0: the turret
// pose scripts as BroScr_SetDesiredPoseProne with BroScr_Mg42AnimTree(_2/_3) and
// BroScr_AnimScript_TurretCall_3/_4/_5: crouch then prone; prone; stand.
void XB_CDECL BroScr_SetDesiredPoseProneMg42(uint32_t ent);
void XB_CDECL BroScr_TurretPoseProne(uint32_t ent);
void XB_CDECL BroScr_TurretPoseStand(uint32_t ent);
// 0x001c29c0: a BroScr_PainAnimByHitLocation call thread (true), then for special 0x371ab2 while
// standing the mg42 gunner pain (true); else false.
uint8_t* XB_CDECL BroScr_TryTurretGunnerPain(uint8_t* out, uint32_t ent, uint32_t special);
// 0x001c2d10 (FUN_001c2d10; proposed BroScr_AnimMoveUntilEndNote): each frame a begun
// BroScr_ForwardNotifyString(ent, forward) thread, the anim time advanced, `notify`, a note
// (0x71445fee) waited: "end" gives true; unless the anim time ended, the anim move is applied and
// a blocked move (byte +0x3b1) gives false, else FUN_001c2aa0(ent, 4, origin, angles).
uint8_t* XB_CDECL BroScr_AnimMoveUntilEndNote(uint8_t* out, uint32_t ent, uint32_t forward, uint32_t notify);
// 0x001c2eb0: appends {a2, a4, a5, a3, a6, a7, a8, six undefined lanes} to the anim command array
// key 0x1791f035 and applies it (BroScr_ApplyAnimCommand).
void XB_CDECL BroScr_RecordAnimCommand(uint32_t ent, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6,
                                       uint32_t a7, uint32_t a8);
// 0x001c2f70: anim mode "nophysics", every recorded command applied again, the array emptied,
// anim mode "none".
void XB_CDECL BroScr_ReplayAnimCommands(uint32_t ent);
// 0x001c3200: start walking: the combat walk when a combat anim takes over, else (alert: alert2aim)
// the walk start into one of three combat walk loops (50 / 30 / 20 %).
void XB_CDECL BroScr_StandStartWalk(uint32_t ent);
// 0x001c3390 / 0x001c35b0: walk to stop (random idle set 0x61 / 0x62): rff before half the walk
// cycle, else lff; just the move 0x385d90 when a combat anim takes over (_2: no standanim check).
void XB_CDECL BroScr_StandWalkToStop(uint32_t ent);
void XB_CDECL BroScr_StandWalkToStop_2(uint32_t ent);
// 0x001c37a0: to stop standing (pistol / none: their anim; else random idle set and crouch2stand),
// Shoot cleared.
void XB_CDECL BroScr_StandMoveToStop(uint32_t ent);
// 0x001c3880: crouch to crouch walk (move 0x3a09eb).
void XB_CDECL BroScr_CrouchToCrouchWalk(uint32_t ent);
// 0x001c38c0 / 0x001c3a20 (identical): random idle set; h66973bee set or no weapon: just the move;
// else the crouch walk stop into crouch aim.
void XB_CDECL BroScr_CrouchWalkToCrouchStop(uint32_t ent);
void XB_CDECL BroScr_CrouchWalkToCrouchStop_2(uint32_t ent);
// 0x001c3b80: BroScr_CrouchRun(ent).
void XB_CDECL BroScr_CrouchRun_2(uint32_t ent);
// 0x001c3b90: "face current", prone legs straight and aim weights, prone -> crouch run.
void XB_CDECL BroScr_ProneFaceCurrent(uint32_t ent);
// 0x001c3ca0: random idle set, stand -> crouch attack, Shoot cleared.
void XB_CDECL BroScr_StandToCrouchStop(uint32_t ent);

// 0x001c22b0: grenade cower anim script (0xfe429dc3; ends on 0x07b4e7f7): after a random 0.25 s,
// the prone cower or a random left / right grenade hide (side string key 0x75f8caea) from the pose
// (crouching otherwise), its note tracks, the loop, start callback BroScr_GrenadeCowerHide; then
// waits forever.
void XB_CDECL BroScr_AnimScript_GrenadeCower(uint32_t ent);
// 0x001c3d80: random idle set, "face current", prone body, prone -> crouch.
void XB_CDECL BroScr_IdleFaceCurrent(uint32_t ent);
// 0x001c3f10 / 0x001c3fa0: prone aim -> crawl (move `move`, into the crawl) / crawl -> aim.
void XB_CDECL BroScr_ProneAimToCrawl(uint32_t ent, uint32_t move);
void XB_CDECL BroScr_ProneCrawlToAim(uint32_t ent);
// 0x001c4030 / 0x001c40f0 / 0x001c41b0: into prone (pitch blends, legs 0.3 / 0.2 s, aim weights):
// crouch2prone gunsupport / stand2prone onehand / by the motion direction a crouch-run dive when
// its end point is a safe move point, else gunsupport.
void XB_CDECL BroScr_CrouchToProne(uint32_t ent);
void XB_CDECL BroScr_StandToProne(uint32_t ent);
void XB_CDECL BroScr_CrouchRunToProne(uint32_t ent);
// 0x001c5f20: blend times (first, second), then Bro_aim__keep_aiming at the 0x7aeb941a point (when
// 0x33a4ea6c is set), the 0x27cc2178 entity's origin, or the enemy's eye; min / max 0x009230ec /
// 0x0092379c.
void XB_CDECL BroScr_KeepAimingAtTarget(uint32_t ent, uint32_t first, uint32_t second);
// 0x001c6ba0: ends on death / 0x39894e / 0x2cf20de4 / 0x3c77a48b; on the target's death
// BroScr_StopForceShoot.
void XB_CDECL BroScr_ForceShootTargetDeathWatch(uint32_t ent, uint32_t target);
// 0x001c7600: a BroScr_AnimFaceWait call thread (anim, a3..a7, true); a3 and a6 are released.
void XB_CDECL BroScr_StartAnimFaceWait(uint32_t ent, uint32_t anim, ScriptString a3, uint32_t a4, uint32_t a5,
                                       ScriptString a6, uint32_t a7);
// 0x001c79e0 / 0x001c7a00: begun threads (a1 = 1): BroScr_ChatterGrenadeAttack / BroScr_AnimScript_Walk.
void XB_CDECL Bro_anim_main_grenade_return_throw(uint32_t ent);
void XB_CDECL Bro_anim_main_walk(uint32_t ent);

// 0x001c5c60: unless 0x764918ae is 0xc4a729a7: the alert-to-aim anim of the pose and idle set
// (standing: 'w' wounded, 'a', else set 'b'; crouching with anim_special 0x3124fe8f:
// BroScr_PlayCrouchIdleAnim; crouching / prone: 'a', else set 'b'), then 0x764918ae = 0xc4a729a7.
void XB_CDECL BroScr_PlayIdleAnim(uint32_t ent);

// 0x001c4510 (begun thread): ends on 0x07b4e7f7; anim script 0x3a09eb, desired pose applied
// (wounded: BroScr_WoundedWalk). Then forever by the allowed move pose: crouch walk / prone crawl,
// standing the walk combat anim (walk_combatanim with a combat delay passed, 0x9dc74345 without;
// 20 %: the alternates 0x30248704 / 0xb8e86881), the wounded walk (until 0x32292cf8 or 0x8ef2ad7c
// above 0.5), the stand shuffle (FUN_001b2ed0, below the anim object's +0xc0 limit, no combat, move
// 0x385d90) or a random walk loop; played flagged 0x9e83b205 (blend 1.2) and waited for.
void XB_CDECL BroScr_AnimScript_Walk(uint32_t ent);

// 0x001c1860 (call thread): BroScr_AnimFaceResetKeys, then with an anim or a sound (ends on death):
// a "wait" kind at or below the level playing (0x03375c37) queues a record {anim, sound, notify,
// kind, time, id, level} (array 0x053fe992, id = 0x257acba4 + 1), starts the stop-notify and (with a
// time) stop-time threads, waits for 0xc77f1e4d, then replays the record ("notify" in 0x1cc76848) or
// reports "failed", and drops it. A level at or above the one playing interrupts it (0xad9b8a7b,
// "interrupted"), plays the anim (flagged 0x1c859d65, note-track thread) and sound (after note
// 0x84f611ff; with `flag` a notifying sound and the set-flag thread), waits until 0xaa35a0b5 is set
// and reports "finished". Else "failed". Releases `sound` and `kind`.
void XB_CDECL BroScr_AnimFaceWait(uint32_t ent, uint32_t anim, ScriptString sound, uint32_t level, uint32_t notify,
                                  ScriptString kind, uint32_t time, uint32_t flag);

// 0x001c70a0: unless dead (type other than "death") or generic_dialogue is off, or a german giving up
// position: the voice (nationality + "_" + entity number % the nationality's voice count + 1; "v" +
// the level's voice for 0x27711210 axis), a chance and (death and two others) a default anim by the
// type's hash, then a BroScr_AnimFaceWait call thread with "generic_" + type + "_" + voice.
// Releases `type`.
void XB_CDECL BroScr_BattleChatter(uint32_t ent, ScriptString type);
// 0x001c76a0 (begun thread): grenade return throw (stand / crouch), "face current", 0.2 s,
// "face default", "grenadeattack" chatter, the throw on note 0x32aa25, then the end note.
void XB_CDECL BroScr_ChatterGrenadeAttack(uint32_t ent);
// 0x001c7880 (begun thread): crouch / stop; aiming at a target (0x764918ae is 0xc4a729a7):
// BroScr_KeepAimingAtTarget(0.1, 2); else the low-wall hide loops forever.
void XB_CDECL Bro_hide__main(uint32_t ent);
// 0x001c7a20: Bro_hide__main as a begun thread.
void XB_CDECL BroScr_StartHideThread(uint32_t ent);
// 0x001c7a40 / 0x001c7a70 / 0x001c7aa0: the turret anim (stand / prone aim, prone fire) to
// ScrActor_SetInt_1370, then a begun BroScr_TurretPoseStand / BroScr_TurretPoseProne /
// BroScr_SetDesiredPoseProneMg42 thread.
void XB_CDECL Bro_mg42_stand(uint32_t ent);
void XB_CDECL Bro_mg42_prone(uint32_t ent);
void XB_CDECL Bro_anim_lmg_gmi(uint32_t ent);
// 0x001c7ad0: "meleeattack" chatter, melee_charge2right with note tracks (BroScr_MeleeNoteTrackHandler),
// then 0x2afa5406 = 0x07151174 and movement stop.
void XB_CDECL BroScr_MeleeAttackAnim(uint32_t ent);
// 0x001c8660 / 0x001c86a0: BroScr_RecordAnimCommand(ent, 2, 0, a2..a6) / (ent, 3, a2..a7).
void XB_CDECL BroScr_RecordSetAnimKnobAll(uint32_t ent, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6);
void XB_CDECL BroScr_RecordAnimCommand3(uint32_t ent, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6,
                                        uint32_t a7);
// 0x001c86e0: the 0x12993a44 struct's +0x24 = g_anim_Body, 0x2181000b = 0x22a9df44, the struct reset;
// with scripted dialogue (or 0x3d130364): a face-anim wait (0x3d130364, the dialogue, 0.9, 0x0cbd9c3d)
// and both keys removed; 0x210b183c copied to 0xe0e46b1d when set.
void XB_CDECL BroScr_ScriptedAnimInit(uint32_t ent);

// 0x001c6840: with rounds left (0x0feddbea): a pending rechamber (0xc786fa48) plays the upper-body
// bolt anim, sound and note tracks and clears it; else a look-at-enemy thread and a burst (auto 3-8,
// semi 2-3, else 1 and a rechamber pending) while rounds last and the target is within 25 degrees,
// each shot counting a round down; then notify 0x807e5a80.
void XB_CDECL BroScr_RechamberRifle(uint32_t ent);
// 0x001ca3c0: ends on 0x07b4e7f7 (and `end` when defined), waits `delay` when defined, then
// BroScr_RechamberRifle.
void XB_CDECL BroScr_RechamberRifleDelayed(uint32_t ent, uint32_t end, uint32_t delay);
// 0x001c6c00: the rambo anticipation, then automatic: one burst of 5-9 shots on note 0x32aa25 when
// the muzzle is clear; else 2-4 single semi-auto shots (muzzle clear), each after its interval.
void XB_CDECL BroScr_RamboFire(uint32_t ent);
// 0x001c6ea0: until the actor can shoot, `time` runs out or a sentient slot is pending: the three
// aim anims in steps of 0.25 s (four at a time). `name` ("FIXME" when empty) is released.
void XB_CDECL BroScr_AimUntilCanShoot(uint32_t ent, uint32_t anim_a, uint32_t anim_b, uint32_t anim_c, uint32_t time,
                                      ScriptString name);
// 0x001c98d0: with a reload pending (BroScr_GetFieldUnlessAutoOrSemi): launchers (three weapon types)
// reload in place (int f0 = f4, 0x552cfcab set, gun in the right hand, clip refilled, pending cleared);
// else a reload-timeout and a rechamber thread, then waits for 0x5e12d3d2.
void XB_CDECL BroScr_ReloadLauncher(uint32_t ent);
// 0x001c9890: blend times, the idle anim, then BroScr_KeepAimingAtTarget(0x009230ec, 0x0092379c).
void XB_CDECL BroScr_AimIdleAtTarget(uint32_t ent, uint32_t first, uint32_t second);
// 0x001cba70 (functor target): on each trigger: "giveupposition" chatter, a look at the player, 5 s.
void XB_CDECL BroScr_GiveUpPositionChatter(uint32_t ent);
// 0x001cd050: BroScr_BattleChatter(ent, "death").
void XB_CDECL BroScr_BattleChatterOnDeath(uint32_t ent);
// 0x001dca00: once (anim object +0x158 undefined): the "animscriptent" cvar (-1 when not positive),
// an empty level thread, the anim object's ranges / times / idle-set letters / voice counts, the pose
// movement table and the weapon class.
void XB_CDECL BroScr_AnimScriptEntInit(uint32_t ent);
// 0x001dcc70 / 0x001dcca0: for a valid entity BroScr_AnimScriptEntInit and FUN_001b7e70; the latter
// returns 0.
void XB_CDECL BroScr_AnimScriptEntInitIfValid(uint32_t ent);
uint32_t XB_CDECL Bro_anim_main_init(uint32_t ent);
// 0x001cdb30: BroScr_ScriptedAnimInit, then BroScr_WaitKillAnimScript as a begun thread.
void XB_CDECL Bro_anim_main_scripted(uint32_t ent);
// The anim-script main entries (begun threads, a1 = 1): 0x001cdb10 pain, 0x001d96d0 concealment
// stand, 0x001d96f0 / 0x001d9710 cover wide left / right, 0x001d9730 death, 0x001dccd0 concealment
// crouch, 0x001dccf0 cover crouch, 0x001dcd10 run, 0x001dd850 combat, 0x001dd870 / 0x001dd890 /
// 0x001dd8b0 cover left / right / stand, 0x001de940 concealment prone, 0x001de960 cover prone,
// 0x001e3c60 stop, 0x001e3da0 reacquire.
void XB_CDECL Bro_anim_main_pain(uint32_t ent);
void XB_CDECL Bro_anim_main_concealment_stand(uint32_t ent);
void XB_CDECL Bro_anim_main_cover_wide_left(uint32_t ent);
void XB_CDECL Bro_anim_main_cover_wide_right(uint32_t ent);
void XB_CDECL Bro_anim_main_death(uint32_t ent);
void XB_CDECL Bro_anim_main_concealment_crouch(uint32_t ent);
void XB_CDECL Bro_anim_main_cover_crouch(uint32_t ent);
void XB_CDECL Bro_anim_main_run(uint32_t ent);
void XB_CDECL Bro_anim_main_combat(uint32_t ent);
void XB_CDECL Bro_anim_main_cover_left(uint32_t ent);
void XB_CDECL Bro_anim_main_cover_right(uint32_t ent);
void XB_CDECL Bro_anim_main_cover_stand(uint32_t ent);
void XB_CDECL Bro_anim_main_concealment_prone(uint32_t ent);
void XB_CDECL Bro_anim_main_cover_prone(uint32_t ent);
void XB_CDECL Bro_anim_main_stop(uint32_t ent);
void XB_CDECL Bro_anim_main_reacquire(uint32_t ent);

}  // namespace game

