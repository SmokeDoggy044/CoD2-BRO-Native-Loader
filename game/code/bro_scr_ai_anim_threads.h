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

struct DynArray;

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
// BroScr_ForwardNotifyString(ent, notify) thread, the anim time advanced, `notify`, a note
// (0x71445fee) waited: "end" gives true; unless the anim time ended, the anim move is applied and
// a blocked move (byte +0x3b1) gives false, else FUN_001c2aa0(ent, 4, origin, angles).
uint8_t* XB_CDECL BroScr_AnimMoveUntilEndNote(uint8_t* out, uint32_t ent, uint32_t notify);
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

// --- push 3 c (thread switch, not registered) ---
// 0x001c6040: ends on 0x5e12d3d2 / 0x07b4e7f7; plays the rechamber anim for the pose (stand / prone /
// crouch, and the corner / hide-low-wall variants by anim_special and anim_idleset) at a random rate
// 0.9-1.1, clears the pending rechamber, plays the bolt sound, waits on the note tracks, notifies
// 0x5e12d3d2.
void XB_CDECL Bro_combat__rechamber(uint32_t ent);

// 0x001c7be0: the pain anim script: chatter "pain", unless pain is off (0xad6d6fd5 = 0) or a turret
// gunner's pain took it: a running actor stumbles (or falls into a crawl when hurt), else the pose's
// pain anim (standing: by hit location; 0.2 s movement stop after), then note tracks, the new pose,
// movement and wounded anim.
void XB_CDECL BroScr_AnimScript_Pain(uint32_t ent);
// 0x001c8620 (cut off the end of BroScr_AnimScript_Pain in the symbol file): BroScr_RecordAnimCommand(ent,
// 1, 0, a2, the whole-body anim, a3..a5).
void XB_CDECL BroScr_RecordSetAnimKnob(uint32_t ent, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5);

// 0x001c9550: in dawnville, past the level's next chatter time (+0xe0): the first living actor of
// actors[first..end) gets a dawnville_defensive_german_1..5 face-anim line (picked by chance and the
// array size), the next time 2.5-4.5 s on; frees the array.
void XB_CDECL BroScr_FallbackSelectActors(DynArray actors, int32_t first, int32_t end);

// 0x001c99f0: a launcher reloads through BroScr_ReloadLauncher; else when BroScr_NeedsReload(threshold):
// the clip refilled, the rechamber cleared, then `anim` (when set) or the weapon / pose / movement's
// reload anim (flag 0x047f1336) with its note tracks; an unknown pose waits 2 s.
void XB_CDECL BroScr_TryReload(uint32_t ent, uint32_t threshold, uint32_t anim);
// 0x001cee30 / 0x001cefa0 / 0x001cf120 (stand), 0x001cf290 / 0x001cf400 (crouch): anim_idleset 0x77 /
// 'a' / 'b' / 'a' / 'b', the pose, a reload below half a clip, BroScr_AimToAlert, then an idle or
// twitch anim by chance (flag 0x54e3b2b6, rate key 0x6937c3b9) with its note tracks.
void XB_CDECL BroScr_StandIdleTwitch_1(uint32_t ent);
void XB_CDECL BroScr_StandIdleTwitch_2(uint32_t ent);
void XB_CDECL BroScr_StandIdleTwitch_3(uint32_t ent);
void XB_CDECL BroScr_CrouchIdleTwitch_1(uint32_t ent);
void XB_CDECL BroScr_CrouchIdleTwitch_2(uint32_t ent);

// 0x001d5a50 (FUN_001d5a50): the actor's team chatter block of the anim object started once (flag 1,
// seven times = now); without a next chatter time (0xad8f34a0): now - 5 s + up to 15 s, chance
// 0xb3845cc4 = random 0-1, 0xbe944753 = false, and BroScr_GiveUpPositionChatter /
// BroScr_GrenadeDangerThink as begun threads.
void XB_CDECL BroScr_BattleChatterInit(uint32_t ent);
// 0x001cbae0: forever: waits for 0x1d12d724, starts the team chatter (BroScr_BattleChatterInit) when
// needed, past the team's grenade-chatter time (dc) chatters and sets the next time, waits 5 s.
void XB_CDECL BroScr_GrenadeDangerThink(uint32_t ent);
// 0x001cb7b0: the enemy-sighted chatter when allowed, past the team (+20 s) and own times and the
// enemy is seen: the team time set, a new chance; true. Else an allied actor with an enemy resets the
// team time; false.
uint8_t* XB_CDECL BroScr_ChatterEnemySighted(uint8_t* out, uint32_t ent);

// 0x001cca00 (FUN_001cca00): anim mode 0x83ad10; when the point back along `step_out`'s move delta is
// a safe move point: BroScr_LookAtEnemyLoop begun, anim_special none while `step_out`, BroScr_RamboFire
// and `step_back` play (flag 0x54e3b2b6, note tracks), then restored.
void XB_CDECL BroScr_CornerRamboFire(uint32_t ent, uint32_t step_out, uint32_t step_back);
// 0x001cf570: prone, a reload below 0.x of the clip, then forever: the prone aim idle; past +-x
// degrees off the desired yaw a turn anim (BroScr_ProneAimWeightsLoop thread, note tracks, notify
// 0x6660fd4f), else the idle with aim weights for 1 s.
void XB_CDECL BroScr_StopProneIdle(uint32_t ent);

// 0x001cccd0: the balcony fall, tried with two fall anims: the forward stumble until an obstacle, then
// the fall to its end note; true when one finished, else the anim commands cleared and false.
uint8_t* XB_CDECL BroScr_BalconyStumbleDeath(uint8_t* out, uint32_t ent);
// 0x001cd080: an axis (not allied, not 0xe5aa1ee7 weapon) standing actor tries a shooting side step
// (jump / walk / run left / right, element anim object +0x38): a safe point it can shoot from plays
// it (flag 0xf519d3c8) with a delayed rechamber; +0x38 = the failed tries. True when it stepped.
uint8_t* XB_CDECL BroScr_StandShootSideStep(uint8_t* out, uint32_t ent);

// 0x001cf8b0 .. 0x001d3210: the corner run-to-alert scripts (corner A / B, stand / crouch, right /
// left): face the corner yaw, the run-to-alert anim when moving, the corner idleset / special / pose,
// a reload; then casual idles, or with the combat delay passed the alert pose and an aim sequence (or
// a look), else back to casual.
void XB_CDECL BroScr_CornerRun2AlertRight(uint32_t ent, uint32_t yaw);
void XB_CDECL BroScr_CornerBRun2AlertRight(uint32_t ent, uint32_t yaw);
void XB_CDECL BroScr_CornerCrouchRun2AlertRight(uint32_t ent, uint32_t yaw);
void XB_CDECL BroScr_CornerBCrouchRun2AlertRight(uint32_t ent, uint32_t yaw);
void XB_CDECL BroScr_CornerRun2AlertLeft(uint32_t ent, uint32_t yaw);
void XB_CDECL BroScr_CornerBRun2AlertLeft(uint32_t ent, uint32_t yaw);
void XB_CDECL BroScr_CornerCrouchRun2AlertLeft(uint32_t ent, uint32_t yaw);
void XB_CDECL BroScr_CornerBCrouchRun2AlertLeft(uint32_t ent, uint32_t yaw);

// 0x001d4970: the aim anim script: pose movement stop, the aim blend times, the idle anim, then
// aiming at the target (0x009230ec / 0x0092379c).
void XB_CDECL BroScr_AnimScript_Aim(uint32_t ent);
// 0x001d5e30: the team chatter started; before the combat delay passes the next chatter time is now
// (0xbe944753 false); after it, unless the enemy-sighted chatter played, a chatter past the chance-
// scaled time.
void XB_CDECL BroScr_MiscCombatTimer(uint32_t ent);
// 0x001cb310: ends on death / 0x39894e / 0x2cf20de4; marks the force-shoot (0x88c31fb5, the thread in
// 0x60d7df22), looks / turns at the target entity (death watch) or at the point, stores both, aims
// (BroScr_AimIdleAtTarget) until 0xf378e0c7, waits `delay`, plays the pose's shoot anim and shoots at
// the target's eye / origin or the point. Releases `name`.
void XB_CDECL BroScr_ForceShootAtPoint(uint32_t ent, ScrVec3 point, uint32_t delay, ScriptString name, uint32_t unused7,
                                       uint32_t target, uint32_t unused9);
// 0x001d5890: ends on death; unless unarmed or prone (a moving actor only when `wait_stop`, after it
// stops) BroScr_ForceShootAtPoint with `name` (default 0x7f63f0) and a7 (undefined: 0).
void XB_CDECL BroScr_ForceShootThink(uint32_t ent, ScrVec3 point, uint32_t delay, ScriptString name, uint32_t a7,
                                     uint32_t target, uint32_t wait_stop);

// 0x001d4a40: one cover shooting burst when the enemy is far enough (or `mode` is 0x7f63f0), with
// rounds, a weapon and a muzzle line to `target`: the flamethrower / automatic bursts as
// BroScr_ShootAtTarget threads, the semi-automatic 2-3 single shots, else a launcher / bolt shot;
// then the shoot anim off and 0x807e5a80. Releases `mode`; true when it shot.
uint8_t* XB_CDECL BroScr_CoverShootBehavior(uint8_t* out, uint32_t ent, uint32_t wait_after, ScriptString mode,
                                            uint32_t target);
// 0x001d46c0: a fallback trigger: unless its level fallback entry exists, a BroScr_FallbackThink thread
// on the level (script_fallback 0x8bbe4822, 0x1aa720d8); on the trigger the level is notified
// "<0x826458><fallback>" and the trigger's flood spawner deleted.
void XB_CDECL BroScr_FallbackerTriggerThink(uint32_t trigger);
// 0x001d6420: the concealment stand anim script: forever standing aim idles, a cover shooting burst
// when it can shoot, a reload.
void XB_CDECL BroScr_AnimScript_ConcealmentStand(uint32_t ent);

// 0x001dd3c0: prone at the cover node (teleport), the misc combat timer, a launcher reload, then
// BroScr_AnimScript_Combat as a call thread.
void XB_CDECL BroScr_CoverProneBehavior(uint32_t ent);
// 0x001de000 / 0x001de040: the concealment / cover prone anim scripts (BroScr_CoverProneBehavior).
void XB_CDECL BroScr_AnimScript_ConcealmentProne(uint32_t ent);
void XB_CDECL BroScr_AnimScript_CoverProne(uint32_t ent);
// 0x001dbb90 / 0x001dba80 / 0x001dd750: the cover crouch / concealment crouch / cover stand anim
// scripts: the pose, then Bro_cover_crouch__cover_crouch / Bro_cover_stand__cover_stand as a call thread.
void XB_CDECL BroScr_AnimScript_CoverCrouch(uint32_t ent);
void XB_CDECL BroScr_AnimScript_ConcealmentCrouch(uint32_t ent);
void XB_CDECL BroScr_AnimScript_CoverStand(uint32_t ent);
// 0x001dcd30: turns to the enemy (BroScr_TurnToEnemy_2 thread, then BroScr_CoverProne_2) until
// 0x6e5fd3a8, then Bro_combat__RangedCombat. Releases `unused`.
void XB_CDECL BroScr_AnimScript_Combat(uint32_t ent, ScriptString unused);

// 0x001db300 / 0x001db4a0 / 0x001db6c0 / 0x001db860: stop at a right / left corner standing /
// crouching: corner A or B (the current idleset when already there, else by the yaw), then that
// corner's run-to-alert script; crouching unarmed or with a pistol plays the corner idle instead.
// Release `unused`.
void XB_CDECL BroScr_StopAtCornerStandRight(uint32_t ent, uint32_t yaw, ScriptString unused);
void XB_CDECL BroScr_StopAtCornerCrouchRight(uint32_t ent, uint32_t yaw, ScriptString unused);
void XB_CDECL BroScr_StopAtCornerStandLeft(uint32_t ent, uint32_t yaw, ScriptString unused);
void XB_CDECL BroScr_StopAtCornerCrouchLeft(uint32_t ent, uint32_t yaw, ScriptString unused);

// 0x001e0c90: a spawned actor's setup from its spawner fields: threads for the idle-pose / 0x95b9edc3
// fields, the tank-squish var, a 0.05 s wait, the "auto*" spawner's targetname, the special spawner
// names (fixed radius / goal), team grenade awareness by g_gameskill, the field defaults (awareness,
// suppression, accuracy, sight range, radius, health, grenades, pacifist, ...), the Stavro rank name,
// drop-health and battle threads, then the goal (the player, shrinking radius) or the balcony node.
// Releases `name`.
void XB_CDECL BroScr_SpawnerSetupAI(uint32_t spawner, uint32_t ent, ScriptString name);
// 0x001e1f60: BroScr_SpawnerSetupAI(spawner, ent, name); a living actor gets finished_spawning and
// notifies 0x7f6000. Releases `name`.
void XB_CDECL BroScr_SpawnerFinishSpawn(uint32_t spawner, uint32_t ent, ScriptString name);
// 0x001de980: on the trigger the level is notified the trigger's targetname, its level flag set, the
// trigger deleted.
void XB_CDECL BroScr_TriggerNotifyLevelTargetname(uint32_t trigger);
// 0x001e3c80: the reacquire anim script: forever (wounded: the face anim first) aims at the target
// while alert (0xc4a729a7) and stopped, else Bro_stop__main.
void XB_CDECL BroScr_AnimScript_Reacquire(uint32_t ent);

// 0x001dbc90 / 0x001dbeb0: ends on 0x8325dc; forever picks a cover pose at the node: by chance
// and the allowed stances the left / right cover pose scripts (_1 / _2, flag 0 or 1).
void XB_CDECL BroScr_ChoosePoseRandom(uint32_t ent);
void XB_CDECL BroScr_ChoosePoseRandom_2(uint32_t ent);

// 0x001c9fc0: a grenade throw with `anim` (none: a rifle / no-weapon actor uses the stand / crouch
// throw at 80 / 65 high, offset x y 0; others none): the target by "min energy", else "min time",
// else "max time"; with one the throw anim, the release note, chatter, the throw, the note tracks.
// *out = 0x19c8ae44 when thrown, else 0 (`offset` is changed in place as in the original).
uint32_t* XB_CDECL BroScr_TryGrenadeThrow(uint32_t* out, uint32_t ent, uint32_t anim, ScrVec3 offset);
// 0x001d5f70: battle chatter of `type` when due: no combat delay resets the chatter time; enemy-sighted
// chatter wins; flankLeft / flankRight after the chance * 30000 + 30000 ms team wait (d4), coverme
// after the actor's wait with a valid +0x3c ref or on axis, the team time (d8, + 3 s) and the player
// within 128; others after the actor's wait (chance * 15000 + 20000). Releases `type`.
void XB_CDECL BroScr_ChooseFlankOrCoverMe(uint32_t ent, ScriptString type);
// 0x001cbc70: the cover attack from a corner (`set`: the corner anim set): the cover blend, the aim
// blend thread, step-out knobs, LookAtEnemyLoop, then by fire type an automatic pair of bursts, a
// semi-automatic series or one bolt-action shot (aiming until it can shoot); out of rounds notifies
// 0x807e5a80 at once, else the aim idle, the step-back knobs, alertness 0x06279a34, the 0x281d9462 /
// 0x807e5a80 notifies. Releases `text`.
void XB_CDECL BroScr_CoverAttack(uint32_t ent, uint32_t yaw, const uint32_t* set, ScriptString text);
// 0x001d7a60 / 0x001d7d70: the wide cover left / right anim scripts: facing the node yaw + 0x83eb60
// and teleported to the node, the corner stand (crouch when standing is not allowed) pose for random(1) s
// and while a sentient slot is pending, forever.
void XB_CDECL BroScr_AnimScript_CoverWideLeft(uint32_t ent);
void XB_CDECL BroScr_AnimScript_CoverWideRight(uint32_t ent);
// 0x001dd180 / 0x001dd510: the cover left / right anim scripts: anim sets, the cover type (keeping the
// idleset), chatter, then BroScr_ChoosePoseRandom / _2 as a call thread.
void XB_CDECL BroScr_AnimScript_CoverLeft(uint32_t ent);
void XB_CDECL BroScr_AnimScript_CoverRight(uint32_t ent);
// 0x001d93b0 / 0x001d9750: prone cover: ends on 0x07b4e7f7, 0x06730ae8 and `end`; while prone is allowed
// and it can shoot prone (_1: and may move there), the prone shoot loop (_2: BroScr_ProneShootLoop_2), a
// reload, until the enemy is near off a "Cover Prone" node; notifies 0xc3cc3e6a (_2: 0xf3e6fbcb,
// 0x6e5fd3a8). _1 releases `end`.
void XB_CDECL BroScr_CoverProne(uint32_t ent, ScriptString end);
void XB_CDECL BroScr_CoverProne_2(uint32_t ent, uint32_t end);
// 0x001e3430: BroScr_BipodGunner as a call thread until 0xc3cc3e6a, then Bro_combat__RangedCombat as a
// call thread. Releases `unused`.
void XB_CDECL BroScr_BipodGunnerStateMain(uint32_t ent, ScriptString unused);
// 0x001d78a0: the cover stand idle: misc timer, facing `direction`, the idle transition, teleport to
// `node`, a grenade try, reloads, crouch idles (with grenade tries) while a sentient slot is pending,
// then the face-enemy anim. Releases `text`.
void XB_CDECL BroScr_CoverStandIdle(uint32_t ent, ScrVec3 node, ScrVec3 direction, ScriptString text);
// 0x001db090: the crouched stop idle: unarmed idle; pistol aim idle; in combat the crouch aim pose with a
// reload; else a crouch twitch (corner A: _1, corner B: _2, otherwise by chance).
void XB_CDECL BroScr_StopCrouchIdle(uint32_t ent);
// 0x001e1cf0: ends on "death" / 0x2b740dd3; forever alternates the turret's auto AI (BroScr_MgTurretThink
// thread until "auto_ai") and manual use (goal at the turret's spot, mode 0xad76f051, until "manual_ai";
// started in auto for `mode` 0x6c271e5a, else manual aimed at the player).
void XB_CDECL BroScr_TurretSetAIMode(uint32_t ent, uint32_t turret, uint32_t flag, uint32_t mode);
// 0x001dce30: the combat anim script: wounded face anim, the misc timer; melee charge (MeleeCombat), an
// LMG carrier (BipodGunnerStateMain), prone range (AnimScript_Combat) or reloads and RangedCombat, each
// as a call thread; the nearest enemy distance to 0xa3b8e90e. No direct callers (anim script table).
void XB_CDECL BroScr_CombatMain(uint32_t ent);
// 0x001d6610: teleports to the node, the corner idle, a grenade try (BroScr_TryGrenadeThrow), a
// reload; casual: the idle for 1 s; else waits out pending sentient slots (flank / cover-me chatter,
// grenade tries), then by chance the peek anim, a rambo fire (not allied) or BroScr_CoverAttack.
// `set` is the corner anim set (BroScr_GetCoverLeft/RightAnimSet).
void XB_CDECL BroScr_CornerBehavior(uint32_t ent, ScrVec3 node, uint32_t yaw, const uint32_t* set);
// 0x001d70f0 / 0x001d7280 / 0x001d74b0 / 0x001d7680: the left / right cover pose scripts (_1 stand,
// _2 crouch): the cover idle, movement stop, with `flag` (low byte) the corner A / B stance change,
// then BroScr_CornerBehavior. _2 releases `text`.
void XB_CDECL BroScr_CoverLeftPose_1(uint32_t ent, const ScriptString* text, uint32_t yaw, const uint32_t* node,
                                     uint32_t flag);
void XB_CDECL BroScr_CoverLeftPose_2(uint32_t ent, ScriptString text, uint32_t yaw, const uint32_t* node, uint32_t flag);
void XB_CDECL BroScr_CoverRightPose_1(uint32_t ent, const ScriptString* text, uint32_t yaw, const uint32_t* node,
                                      uint32_t flag);
void XB_CDECL BroScr_CoverRightPose_2(uint32_t ent, ScriptString text, uint32_t yaw, const uint32_t* node,
                                      uint32_t flag);

// 0x001deab0: the turret AI thread (started by BroScr_TurretSetAIMode; ends on "death" / "auto_ai"): to the
// turret, the level flag of its target waited for when `flag` is defined, a script_origin aim target and
// a script_model follower (deleted on death / auto_ai). With an mg42_target path: auto mode aimed at the
// origin, which walks the path, sweeps toward the player's eye, follows the player, bursts around the
// level +0x190 entity's targets. Without: manual aimed at the player, then auto bursts, forever.
void XB_CDECL BroScr_MgTurretThink(uint32_t ent, uint32_t turret, uint8_t flag);

// 0x001cd420 / 0x001ce710: the prone shoot loop (busy 0xb66101f1 meanwhile): prone aim knobs, a random wait
// up to 1 s, then an automatic burst of 6-13 shots on the "fire" note, 2-4 semi-automatic shots, or a
// launcher reload and one shot (field 0xc786fa48 set). _2 draws the wait before the knobs.
void XB_CDECL BroScr_ProneShootLoop(uint32_t ent);
void XB_CDECL BroScr_ProneShootLoop_2(uint32_t ent);

// 0x001cdb60: whether a standing actor's melee charge reaches its living enemy (*out): in melee range
// already, or within range 0x118 with a safe spot 32 short of the enemy: the charge anim (with
// "meleecharge" chatter and the melee pair linked once) until in range, then the melee attack anim.
// The distance (squared) goes to 0xa3b8e90e; notifies 0x807e5a80 at the end.
uint8_t* XB_CDECL BroScr_MeleeCharge(uint8_t* out, uint32_t ent);

// 0x001dabf0: the standing stop idle: in combat facing the target, stand pose, a reload, the pistol
// aim idle or one of four aim loops; else the standanim field, the pistol idle, or a stand twitch
// (_1 while wounded or field 0x8ef2ad7c over the limit; corner A: _2, corner B: _3, else by chance),
// a 0.2 s wait when no time passed.
void XB_CDECL BroScr_StopStandIdle(uint32_t ent);

// 0x001d5410: ends on death / 0xcd9e558b; the end time (now + time * 1000) to 0x18cd6af8; unless a
// 0x0036b1a5 weapon, moving (or waiting to stop with `wait_stop`, until the end) or prone: repeated
// BroScr_ForceShootThread call threads (name default "forceShoot", a7 undefined: 0) while time is
// left, then 0x2cf20de4 and BroScr_StopForceShoot; *out whether it shot. Releases `name`.
uint8_t* XB_CDECL BroScr_ForceShoot(uint8_t* out, uint32_t ent, ScrVec3 point, float time, ScriptString name, uint32_t a7,
                                    uint32_t target, uint32_t wait_stop);

// 0x001e3520: the stop anim script (wounded: the face anim first), forever by the node at the actor's
// position: cover left / right: the corner stops (StopAtCorner*Right / *Left; the idle set re-picked every
// 10 s off a corner); cover crouch: the cover-crouch idle forever; else the stand / crouch / prone stop
// idle by the allowed pose. An LMG carrier: the bipod gunner state call thread, then the stop idle.
void XB_CDECL Bro_stop__main(uint32_t ent);

// 0x001e25d0: a spawner's thread: the script_mg42 new-user thread, the script_delete key entry, then for
// each living sentient it spawns: the friendly-fire mission fail and debug threads, the script_delete
// others deleted, key 0xd66dcf8c cleared when it and 0xb5ea7cdf are set, BroScr_SpawnerFinishSpawn
// (with the level as spawner and the spawner's targetname). Ends on a non-sentient or dead spawn.
void XB_CDECL BroScr_SpawnedThink(uint32_t unused, uint32_t spawner);

// 0x001e2e10: the bipod gunner (ends on 0x07b4e7f7 / death / `end`): waits out field 0xf56955a1 and
// 0x0032304c, the "gunner" mg34 run / deploy / go-prone anims in the level map, plays deploy and go
// prone, then spawns a misc_turret ahead of the actor (an mg34 / 30cal bipod turret by the weapon), the
// owner-change call thread, the weapon back to int +0xf4, and uses the turret.
void XB_CDECL BroScr_BipodGunner(uint32_t ent, uint32_t end);

// 0x001e2010: the MG42 cover spot spawner thread: ai_mode defaulted, the target node's turret moved to the node;
// a turret with a target: its level flag cleared and a trigger notify thread when undefined. While the
// spawner has a count: a guy spawned (retried each second), the AI mode / manual target threads on
// him, his death, then the spawner's script_delay (or a random delay_min..max, else 1 s).
void XB_CDECL BroScr_Mg42CoverSpotThink(uint32_t ent);

// 0x001d6a10: the cover-crouch thread (ends on 0x07b4e7f7), forever: while no sentient slot is pending
// and rounds are left (no rechamber): face enemy, aim blend times by team, idle, keep aiming, the
// crouch aim, aim until it can shoot, then the cover shoot behaviour. Allies / a pending slot / half
// the time: the crouch reload, teleport to the cover spot, grenade, reload, then flank / cover-me and
// idle while a slot is pending; otherwise grenade and reload.
void XB_CDECL Bro_cover_crouch__cover_crouch(uint32_t ent);

// 0x001dd8d0: the melee combat thread (ends on 0x07b4e7f7): while the melee charge reaches the living
// enemy: the melee pair relinked, the look-at-enemy thread, the attack anim (or stand pose), chatter,
// the MELEE knob, then the next swing by the melee state 0x2afa5406 (start / left / right / after the
// charge) and the enemy's stance (the player's by GScr_GetStance), its note tracks, the new state.
// Then the pair unlinked if linked, the combat main thread, notify 0x807e5a80. Releases `reason`.
void XB_CDECL Bro_combat__MeleeCombat(uint32_t ent, ScriptString reason);

// 0x001de080: the ranged combat thread (ends on 0x07b4e7f7), facing the enemy, each pass: no pose
// movement, a reload; within anim object +0x198 of the enemy (none: just past it for the pose): stand
// or crouch where it can shoot (close and seeing the enemy: stand), farther: crouch first. Standing may
// side step first (chance by the distance, not allies); then the cover shoot behaviour; no pose: a side
// step or the plain aim. Then the enemy distance to 0xa3b8e90e: prone range: the combat anim script;
// a melee charge: the melee combat thread (both end it); else a grenade try. Releases `reason`.
void XB_CDECL Bro_combat__RangedCombat(uint32_t ent, ScriptString reason);

// 0x001dc0c0: the cover-stand thread (ends on 0x07b4e7f7), forever: aim (blend 2); while no sentient
// slot is pending and rounds are left (no rechamber): face enemy, team blend times, the stand aim, aim
// until it can shoot, then a volley (none: the plain aim; every tenth another) and a grenade try; or a
// machine gunner seeing the enemy node suppresses (70%) while rounds are left. Then axis (no pending
// slot, half the time): a grenade and a reload; otherwise the cover-stand idle at the cover spot.
void XB_CDECL Bro_cover_stand__cover_stand(uint32_t ent);

// 0x001d3980: the fallback thread (on the level, from BroScr_FallbackerTriggerThink) for fallback n:
// its nodes (script_fallback n); with any: the level's +0x6c / +0xc / +0xe4 entries cleared, the spawner
// threads (counted at +0xc), the AI join threads; unless nobody is in it: the fallbacker setup thread,
// the "fallbacker_trigger<n>" wait, +0xe4 set, then 40% of its AI (n or `group`) selected, sent to random
// nodes and waited for ("fallback_reached_goal<n>"), then the living rest sent.
void XB_CDECL BroScr_FallbackThink(uint32_t level, int32_t n, uint32_t group);

// 0x001d8080: the death anim script (Ghidra's body also holds 0x001d91d0, an unreferenced combat aim/shoot
// helper). Weapons dropped, notify 0x247afc7f; a scripted deathanim (looped on note tracks with 0x27ed5a85) or
// a cover death, else the pain thread, death chatter, the face wait, g_anim_DEATH; a balcony launch; a helmet
// pop; then the death anim by explosion damage / running direction / pose, hit location, damage yaw and a
// roll, played on its note tracks (0x54e3b2b6).
void XB_CDECL BroScr_AnimScript_Death(uint32_t ent);

// 0x001d91d0 (inside Ghidra's BroScr_AnimScript_Death; unreferenced): the crouched combat aim and shoot step.
void XB_CDECL BroScr_CrouchAimAndShoot(uint32_t ent);

}  // namespace game

