#pragma once

// Compiled level-script actor threads (levelscript module, .text 0x0019f090-0x001b9a00): look-at
// ends, MG42 gunner recovery / pain, aim blending, cover idles, wounded walk, the anim-script turret
// entries. Every one waits (Scr_Wait*, Scr_WaitTill*, a note-track wait, Scr_CallThread*): class
// thread_switch, kept out of kFunctions until script threads run as coroutines. Names are ours.

#include <cstdint>

#include "compiler.h"
#include "qcommon_fs.h"
#include "script_string.h"

namespace game {

// 0x0019e5c0 (functor target): ends on death, 0xe099bb44 and 0x247afc7f. The look target by kind
// 0x42f377f3: 0x9b751a35 the eye of sentient 0xeeadf3f2, 0xccdbaaf9 the origin of entity 0xeeadf3f2
// (returns when not sentient / not valid), else the vector 0xb217897f; GScr_ActorLookAt with rate
// 0x5e1a2a0c, look start 0x7b74ea2f = now. Until the time 0x644536d0, every 0.05 s: the target followed
// (the eye while alive, the origin), a stripped debug line thread (eye, target - 2 z; called when
// `notify_type` is 0xa3c65328, else begun), the head yaw / pitch toward the target computed and
// dropped, BroScr_UpdateLookRates in the first 50 of every 500 ms, 0x7b74ea2f = now. Then
// BroScr_StopLookingAt.
void XB_CDECL BroScr_HeadLookThink(uint32_t ent, uint32_t notify_type);
// 0x0019f090: ends on death; waits for 0x247afc7f, then BroScr_StopLookingAt.
void XB_CDECL BroScr_StopLookingAtOnNotify(uint32_t ent);
// 0x0019f320 / 0x0019f6f0 (_2 ends on 0x07b4e7f7 first): a BroScr_TurretSetProneRecoverAnim /
// BroScr_EmptyFunc_60 call thread (ent, a2), actor int +0x1370 = the prone MG42 recover anim, that
// anim as flagged knob 0x54e3b2b6 (1, 0.2, 1, restart) and a wait for it.
void XB_CDECL BroScr_TurretProneMG42Recover(uint32_t ent, uint32_t a2);
void XB_CDECL BroScr_TurretProneMG42Recover_2(uint32_t ent, uint32_t a2);
// 0x0019f9d0: ends on 0x07b4e7f7 / death; waits `time`, then handle field 0x0ccee5c1 = 0x385d90.
void XB_CDECL BroScr_SetAnimMovementStopAfter(uint32_t ent, float time);
// 0x001a2450: with actor int +0x184 0x19669cd9: +0x184 = 0xc1165808 for field 0xb7f3f874 values
// 0x06088a8c / 0x068698d3, else 0x070b89a4; BroScr_SetPoseMovement(0x070b89a4, 0x3a09eb); the
// wounded walk loop anim knob (1, 0.1, 1); waits 10 s.
void XB_CDECL BroScr_WoundedWalkLoop(uint32_t ent);
// 0x001a6fe0: waits for "friendly_finished_using_mg42" on `ai`, then BroScr_LeaveTurretToNode.
void XB_CDECL BroScr_WaitFriendlyFinishedMG42(uint32_t unused, uint32_t ai);
// 0x001a95a0: BroScr_MagicBulletShield(ent, 100000000, 5, its health, 1) (never returns).
[[noreturn]] void XB_CDECL BroScr_MagicBulletShieldDefault(uint32_t ent);
// 0x001abc70 / 0x001abd00 / 0x001abd90 (_3 also ends on `other`'s death): ends on death and
// `end`; waits for `notify`, notifies `end`; BroScr_StopLookingAt when the look id 0x747cde6c is
// still `id`. (end, notify) = (0xa538f810, 0x807e5a80) / (0x671c92d3, 0xd6604ba3) /
// (0x1651c9e7, 0x0fc3c1c9).
void XB_CDECL BroScr_LookAtEndOnNotify_1(uint32_t ent, uint32_t id);
void XB_CDECL BroScr_LookAtEndOnNotify_2(uint32_t ent, uint32_t id);
void XB_CDECL BroScr_LookAtEndOnNotify_3(uint32_t ent, uint32_t other, uint32_t id);
// 0x001b31a0 (debug): forever BroScr_DebugDrawRadiusCircle of the first two chain nodes (array
// 0x0605894e), one frame each round.
[[noreturn]] void XB_CDECL BroScr_DebugDrawChainRadiiLoop(uint32_t ent, uint32_t a2);
// 0x001b6a50 / 0x001b6a80 / 0x001b9310 / 0x001b9710 / 0x001b9740: BroScr_AnimScript_Turret_2 /
// _Turret_2 / _Turret_3 / _Turret / _Turret (never return): (ent, fire, recover, aim callbacks, a2 =
// the turret).
[[noreturn]] void XB_CDECL BroScr_AnimScript_TurretCall_1(uint32_t ent, uint32_t a2);
[[noreturn]] void XB_CDECL BroScr_AnimScript_TurretCall_2(uint32_t ent, uint32_t a2);
[[noreturn]] void XB_CDECL BroScr_AnimScript_TurretCall_3(uint32_t ent, uint32_t a2);
[[noreturn]] void XB_CDECL BroScr_AnimScript_TurretCall_4(uint32_t ent, uint32_t a2);
[[noreturn]] void XB_CDECL BroScr_AnimScript_TurretCall_5(uint32_t ent, uint32_t a2);
// 0x001b6dc0 / 0x001b6e70 / 0x001b6f20: a BroScr_LookAtEndOnNotify_1 / _2 / _3 call thread, ends on
// death (and `other`'s) and that one's `end`; waits for 0x07b4e7f7, notifies `end`;
// BroScr_StopLookingAt when the look id is still `id`.
void XB_CDECL BroScr_LookAtUntilKillAnimscript_1(uint32_t ent, uint32_t id);
void XB_CDECL BroScr_LookAtUntilKillAnimscript_2(uint32_t ent, uint32_t id);
void XB_CDECL BroScr_LookAtUntilKillAnimscript_3(uint32_t ent, uint32_t other, uint32_t id);
// 0x001b6fe0: ends on 0x07b4e7f7 / `end`; forever BroScr_BlendAimAnimsByYaw(ent, a..base), 0.2 s.
[[noreturn]] void XB_CDECL BroScr_BlendAimAnimsByYawLoop(uint32_t ent, uint32_t end, uint32_t anim_a, uint32_t anim_b,
                                                       uint32_t anim_c, uint32_t a_bits, uint32_t b_bits,
                                                       uint32_t c_bits, uint32_t base_bits);
// 0x001b7180: hidelowwallb idle1 (75%) or twitch1 as flagged knob 0x54e3b2b6 on Body (1, 0.1,
// rate 0.9 + RandomFloat(0.2), restart), then its note tracks.
void XB_CDECL BroScr_CoverCrouchRandomIdleAnim(uint32_t ent);
// 0x001b7bc0: ends on death / 0xad9b8a7b; note tracks of 0x1c859d65; int 0xfd6181bd = 1; notify
// 0x1c87198d.
void XB_CDECL BroScr_AnimFaceWaitNotetracks(uint32_t ent);
// 0x001b9950: the stand MG42 gunner pain anim as flagged knob 0x94d136d5 (1, 0.1, 1), then its
// note tracks.
void XB_CDECL BroScr_PlayStandMG42GunnerPain(uint32_t ent);

// 0x001754b0 (was FUN_001754b0): the face anim of note track `note` in the animname's table
// (level +0x160 [animname][key][index][hash(note)]), with the anim from level +0x6f0 [animname]
// [key][index] value 0xca20284c (0 when unset): BroScr_StartAnimFaceWait(ent, anim, face, 1, the word
// at 0x00922ee8, "", the word at 0x00922ea4); the table entry then emptied. Releases `note`.
void XB_CDECL BroScr_NotetrackFaceAnim(uint32_t ent, uint32_t key, uint32_t index, ScriptString note);

// 0x0019f390 / 0x0019f770 (identical) / 0x001b9770: anim tree "mg42" on the turret; its users
// count 0xabc1df0a + 1 (0 when undefined); waits for 0x07b4e7f7 on the gunner; then the count - 1
// while above 0, else (_3: the gunner's gun back in hand, 0x07151174) the key removed and the anim
// tree cleared.
void XB_CDECL BroScr_Mg42AnimTree(uint32_t ent, uint32_t gunner);
void XB_CDECL BroScr_Mg42AnimTree_2(uint32_t ent, uint32_t gunner);
void XB_CDECL BroScr_Mg42AnimTree_3(uint32_t ent, uint32_t gunner);
// 0x0019f930 (new; Ghidra counts it into 0x0019f770): with a valid sentient ref +0x3c: the stand
// MG42 fire anim (actor int +0x1370, knob 1 / 0.2 / 1) and BroScr_Mg42ShootLoop_3(ent, turret);
// then the stand MG42 aim anim the same way. BroScr_AnimScript_TurretCall_5's first callback.
void XB_CDECL BroScr_Mg42AnimThenShootLoop_3(uint32_t ent, uint32_t turret);
// 0x0018df30 (GScr_EmptyStub220 is called at +4, inside the first 5 bytes: the harness cannot compare
// it; Replaced): the directional move weights for the actor's motion yaw
// (BroScr_ComputeMoveDirectionWeights), then GScr_SetAnim(ent, anim, weight, blend, 1.0, 0) for the
// forward, back, second side (index 4) and first side (index 3) anims and the two diagonals when set; between
// GScr_EmptyStub220/221(5). The last argument (the callers' visible-model flag) is unused.
void XB_CDECL BroScr_SetDirectionalMoveAnims(uint32_t ent, uint32_t forward, uint32_t back, uint32_t side_a,
                                             uint32_t side_b, uint32_t diagonal_a, uint32_t diagonal_b, uint32_t blend,
                                             uint32_t visible);
// 0x0019fe80: ends on 0x07b4e7f7; the thread kept in 0xa78b2c24 terminated and replaced by this one;
// forever: waits for 0x2faeed05, BroScr_SetDirectionalMoveAnims(ent, anims, 0.2, has a visible model).
[[noreturn]] void XB_CDECL BroScr_DirectionalMoveAnimsOnNotify(uint32_t ent, uint32_t a2, uint32_t a3, uint32_t a4,
                                                             uint32_t a5, uint32_t a6, uint32_t a7);
// 0x0019ff70: the same with key 0xf8b8e073, a BroScr_DirectionalMoveAnimsOnNotify call thread, one
// BroScr_SetDirectionalMoveAnims(.., 0.2, true), then every 0.2 s the visible-model form.
[[noreturn]] void XB_CDECL BroScr_DirectionalMoveAnimsLoop(uint32_t ent, uint32_t a2, uint32_t a3, uint32_t a4,
                                                         uint32_t a5, uint32_t a6, uint32_t a7);
// 0x001a05f0: ends on 0x07b4e7f7 / death / `end`; waits `time`; entering prone (0x0738b1a5) sets the
// prone feet aim weights (slot 0x009226b0 = 1); leaving it slot 0x009226b4 = 1 and orient "face
// default"; actor int +0x184 = `pose`, handle 0x0ccee5c1 = `movement`, 0x764918ae = 0xc4a729a7 /
// 0xc028584c by `flag`.
void XB_CDECL BroScr_ProneAimFaceDefault(uint32_t ent, float time, uint32_t end, uint32_t pose, uint32_t movement,
                                         uint32_t flag);
// 0x001a2510: pose movement 0x070b89a4 / 0x1c6ce (handle 0x0ccee5c1 and actor int +0x184 too), then
// "misc_step1" / "misc_step2" twice each RandomFloat(0.7) s apart, and 1 s.
void XB_CDECL BroScr_PlayStepSounds(uint32_t ent);
// 0x001a6a40: waits for `guy`'s death, notifies "fallbacker_died<n>" on the level object and
// decrements its count n (int array +0x6c).
void XB_CDECL BroScr_NotifyFallbackerDied(uint32_t unused, uint32_t guy, int32_t n);

// 0x001b3360: ends on 0xb633d42e / death; AI flags (1, 1, 0); goal = the origin of the owner's first
// chain node (array 0x0605894e); waits for 0x0032304c; AI flags 0; the goal node from slot 0x009226ec
// (ent, the word at 0x008cd76c, 1; -1 read as 0).
void XB_CDECL BroScr_GoToChainNode(uint32_t ent, uint32_t owner);
// 0x001b8b50: 0.1 s; waits for 0x2b740dd3; a `classname` entity spawned 1 above it with its angles;
// BroScr_PutGunInHand(owner, 0x07151174), notify 0x2b740dd3 on the owner, `ent` deleted. Releases
// `classname`.
void XB_CDECL BroScr_TurretOwnerChangeSwap(uint32_t ent, uint32_t owner, ScriptString classname);
// 0x001b7060 / 0x001b7230: orient "face angle" at BroScr_GetNodeOrActorYaw; hideLowWall_1 / _2 (50%)
// / hidelowwallc idle1 (50%) or 0x008ce91c / 0x008ce928 (25% each) as flagged knob 0x54e3b2b6 on Body
// (1, 0.1, rate 0.9 + RandomFloat(0.2), restart), then its note tracks.
void XB_CDECL BroScr_CoverCrouchIdleTwitch(uint32_t ent);
void XB_CDECL BroScr_CoverCrouchIdleTwitch_2(uint32_t ent);
// 0x001b6350: waits for 0x262a34d8 on `spawner` (the spawned entity out); unless
// BroScr_WaitFinishedSpawning, BroScr_ReattachGunInHand on it (and a copy of `names` made and
// dropped). Releases `names`. The first three arguments are unused.
void XB_CDECL BroScr_OnSpawnedReattachGun(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t spawner, DynArray names);
// 0x001b64a0: 0.1 s; a BroScr_TempEventTextThink thread (level, it, p4..p7) on the level object per
// "temp_event_text" entity. The first three arguments are unused.
void XB_CDECL BroScr_TempEventText(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t p4, uint32_t p5, uint32_t p6,
                                   uint32_t p7);
// 0x001abe30: notifies and ends on 0x1e4aecac (and both deaths); a BroScr_OrientToEntityKillWatch
// call thread; forever: orient "face point" at the target's eye (sentients) or origin, 0.2 s.
[[noreturn]] void XB_CDECL BroScr_UpdateOrientToEntity(uint32_t ent, uint32_t target);
// 0x001b2ed0 (was FUN_001b2ed0): pose movement 0x070b89a4 / 0x385d90; handle 0xbfc60f36 0x77 or
// 0x36b1a5 -> 0x61; stand alert a (0x61) or b shuffle forward as flagged knob 0x5fa04364 on Body
// (1, 0.05, 1), then its note tracks.
void XB_CDECL BroScr_ShuffleForwardAnim(uint32_t ent);
// 0x001a4f40: ends on death / 0x2b740dd3; nothing while is_firing is set; else is_firing = true, a
// script_origin target (with a manual_target), and forever: wait for "StartFiring" on the turret, a
// BroScr_MgBurstFireThink thread (gunner, turret, target) when the turret's manual_target is valid,
// turret flag 4 set until "StopFiring".
void XB_CDECL BroScr_TurretManualTargetThink(uint32_t gunner, uint32_t turret);
// 0x001a4d30: delay min / range and burst min / range from the turret's script_delay_* /
// script_burst_* (range = max - min) or BroScr_MgBurstModeFactor("delay" / "delay_range" / "burst" /
// "burst_range"); ends on 0xcaa6bec3; forever: turret flag 4, a BroScr_TurretRandomSpread thread at a
// valid target, burst_min + RandomFloat(burst range) s, flag 4 cleared, delay_min + RandomFloat(delay
// range) s.
[[noreturn]] void XB_CDECL BroScr_MgBurstFireThink(uint32_t gunner, uint32_t turret, uint32_t target);
// 0x001b7c70: unless the handle 0x2181000b is 0x39894e or death: the prone cower
// (prone) or grenadehide left / right (by string 0x75f8caea, crouch pose) loop knob on Body (1, 0.4,
// 1), RandomFloat(0.25) s, the stop / to-crouch anim as flagged knob 0x8e613617 and its note tracks.
// Field 0xe057503b = 0 either way.
void XB_CDECL BroScr_GrenadeCowerHide(uint32_t ent);
// 0x0019f0d0: ends on death / 0x247afc7f; anim object +0x60 = the float at 0x007f71fc, float
// 0x9a44df1e = the float at 0x0083eb64, int 0x644536d0 = the time, int 0xfa9a8384 = 1; begun threads
// BroScr_StopLookingAtOnNotify and BroScr_EndOnDeath (a frame each when in a thread); forever: waits
// for 0x189e36ec, notifies 0xe099bb44, a begun BroScr_HeadLookThink(ent, undefined) thread.
[[noreturn]] void XB_CDECL BroScr_HeadLookMainLoop(uint32_t ent);

// 0x001b7980: the corner death anim for cover side `side` (left 0x38948440: stand cornerstanddeath1_left
// on the slot, crouch corner_death_pushup / cornercrouchdeath1_left (50%); right 0xa1cb941d: stand
// cornerstanddeath1 / 2_right (50%), crouch cornercrouchdeath1_right) as flagged knob 0xaeb1527d on
// the root (1, 0.05, 1), then its note tracks; *out = played (false for other poses / sides).
bool* XB_CDECL BroScr_PlayCoverDeathAnim(bool* out, uint32_t ent, uint32_t side);
// 0x001b7360 / 0x001b7670: crouch unless standing / crouching; unless the cover side key 0x175567a8 is
// already right (0xa1cb941d) / left (0x38948440): idle set a / b by the clamped yaw - angles.y -/+ 15
// against 180, then after orient "face angle" (anim set +0 + yaw) the anim set's idle (+0x14) as
// flagged knob 0xe227eb87 on Body (1, 0.25, rate field 0x6937c3b9) and its note tracks; movement stop
// and the side key set. (The left idle writes the "right" side value and the other way round.)
void XB_CDECL BroScr_CoverLeftIdle(uint32_t ent, float yaw);
void XB_CDECL BroScr_CoverRightIdle(uint32_t ent, float yaw);
// 0x001a5f10: after 0xe2a210cf deletes every spawner of the trigger's flood group (int 0x460bf109),
// each with a BroScr_DebugCvarWait("DELETED", origin) thread on the level; then
// BroScr_DeleteFloodSpawner unless its targetname is "flood_spawner".
void XB_CDECL BroScr_FloodSpawnerDeleteWatch(uint32_t ent);
// 0x001b4e40: a BroScr_SetTargetnameFromOrigin thread per target; after 0xe2a210cf a
// BroScr_TriggeredSpawnerThink(spawner, trigger) thread per valid target; one frame; then
// BroScr_DeleteFloodSpawner unless int 0xa7ad4a18 is defined.
void XB_CDECL BroScr_FloodSpawnerTriggerThink(uint32_t ent);
// 0x001a7ec0: until `count` living AI of `team` touch `trigger` (checked each second; none counted
// while fewer AI exist), or `timeout` seconds (the undefined float: none) are over. Releases `team`.
void XB_CDECL BroScr_WaitForAliveTouchingCount(uint32_t trigger, ScriptString team, int32_t count, float timeout);

// 0x001b6ab0 (was FUN_001b6ab0): standing / crouching, not prone, with the nearest enemy distance
// (float 0xa3b8e90e) set: while the stand_shoot_run_back move end is a safe move point and the
// distance is within the anim object's +0x94 and past its +0x9c: the anim as flagged knob 0xd5f0267a
// on Body (1, 0.15, 1), stand, movement 0x1c6ce, its note tracks, notify 0xeac9abbd. *out = played.
bool* XB_CDECL BroScr_TryShootRunBack(bool* out, uint32_t ent);
// 0x001a81b0 (was FUN_001a81b0): a script_origin counter; while AI of `team` touch `trigger`: int
// 0x97c82b3d = their count, a BroScr_DecrementAiSizeOnDeath(counter, guy) thread each, wait for
// 0xddc0d6b6 on the counter, then (when `guys` has storage) each second until the last valid guy with
// a model sees the player's health above 85%. The counter deleted. Releases `team`.
void XB_CDECL BroScr_WaitTriggerAiCleared(uint32_t trigger, ScriptString team, DynArray* guys);
// 0x001aad20: with sv_cheats on, the noteworthy text (or "Missing Script_NoteWorthy") split at ';'
// into lines with a point each 32 apart above the origin, the entity deleted, then forever a wait of
// 0.05 s near the player (`range`, 0x400 when undefined) or 1 s (the 3D text drawing is compiled out).
// Without cheats the entity is deleted.
void XB_CDECL BroScr_TempEventTextThink(uint32_t level, uint32_t ent, int32_t range, uint32_t a4, uint32_t a5,
                                        uint32_t a6);
// 0x001992e0: delay / burst like BroScr_MgBurstFireThink; forever: with turret flag 1 and the next
// burst due "fire" (a BroScr_Mg42ShootSoundLoop thread on the change) and a BroScr_TurretStateChangeWait
// of burst_min + RandomFloat(burst range) s, the next burst (delay_min + RandomFloat(delay range)) s
// later; else "aim" until it. Each round waits for 0xeb04625e.
[[noreturn]] void XB_CDECL BroScr_MgBurstFire(uint32_t turret);
// 0x001a5940: count 1 when undefined; while the count is above 0: after 0xe2a210cf every allies /
// axis spawner of the trigger's group (int 0x8bbf13fe) gets count + 1, the trigger's count - 1, 0.05 s
// and its delay; then BroScr_DeleteFloodSpawner(trigger).
void XB_CDECL BroScr_IncrementTriggerThink(uint32_t trigger);
// 0x001a63a0: the misc_mg42 / misc_turret entities whose int 0xec070be6 is `owner`; with any, forever:
// after 0x262a34d8 on `ent`, notify "get new user" on the first one without a user (int 0x7b1e85ba 0).
void XB_CDECL BroScr_MgGetNewUser(uint32_t ent, uint32_t owner);
// 0x001a5100: ends on "death"; forever rounds of `count` picks: int 0x99cb8902 = RandomInt(count), then
// after "next_target" moved on (wrapping) past the indices used this round.
[[noreturn]] void XB_CDECL BroScr_CycleTargetsThread(uint32_t ent, uint32_t a2, uint32_t a3, int32_t count);
// 0x0019a5c0: notify 0x93f871b8, leaves its turret; actor int +0x1308 kept in 0xac91e5fb (then 0), float
// +0x7e4 in 0x62271501 (then 5000); goal = `node` (radius from it when valid); ends on death;
// BroScr_NotifyFallbackReachedGoal(level, ent, n) and BroScr_FallbackOnGoalRestore threads (and the
// debug loop with cvar fallback 1); after 0xed4ff1b2 notifies "fallback_reached_goal<n>" on the level.
void XB_CDECL BroScr_FallbackGoalThink(uint32_t ent, int32_t n, uint32_t node);

// 0x001b9340 / 0x001b8ec0 (_3) / 0x001b6600 (_2): the anim-script turret loops (never return): ends
// on 0x07b4e7f7, anim script 0xef68c4d4, gun 0x36b1a5 in hand, cover side 0x371ab2; burst settings
// (BroScr_MgBurstSettings, re-read each round / the inlined script_delay_* / script_burst_* once);
// with turret flag 1 and the next burst due (_2: the wait to it at most 0): "fire" (a `fire` call
// thread on the change) and a TurretStateChangeAfterTime_3 / _2 / plain call thread for burst_min +
// RandomFloat(burst range) s (the plain form only when that is above 0), the next burst delay_min +
// RandomFloat(delay range) s later, "recover" with `recover`(ent, turret) when given; else "aim" (an
// `aim` thread on the change) and a state change thread for the time to the next burst. Each round
// waits for 0xeb04625e on the turret.
[[noreturn]] void XB_CDECL BroScr_AnimScript_Turret(uint32_t ent, uint32_t fire, uint32_t recover, uint32_t aim,
                                                  uint32_t turret);
[[noreturn]] void XB_CDECL BroScr_AnimScript_Turret_3(uint32_t ent, uint32_t fire, uint32_t recover, uint32_t aim,
                                                    uint32_t turret);
[[noreturn]] void XB_CDECL BroScr_AnimScript_Turret_2(uint32_t ent, uint32_t fire, uint32_t recover, uint32_t aim,
                                                    uint32_t turret);

// 0x001a4910: after 0xe2a210cf: notify 0xad76f051 to the axis AI of the trigger's group (int
// 0xbe5f9a9f) and handle 0x97c730fd = 0xad76f051 on its spawners; then BroScr_DeleteFloodSpawner.
void XB_CDECL BroScr_Mg42AutoTriggerThink(uint32_t trigger);
// 0x001a53e0: ends on "death"; forever: the axis AI touching the trigger (the last one found kept),
// alive: a grenade (GScr_MagicGrenadeManual, `fuse`) from a random target toward a random target of
// it at twice their distance as speed, 1 s; each round waits for "throw".
[[noreturn]] void XB_CDECL BroScr_MagicGrenadeThrowThink(uint32_t trigger, uint32_t fuse);
// 0x0019db90: with a helmet model (string 0xaee61f94): a script_model of it 64 above at the helmet
// bone's tag origin / angles tumbling away (BroScr_RandomTumble with actor vec +0x234), the same for
// the side model (0x3bfcf651) at TAG_HELMETSIDE; after 0.05 s both taken off the actor (slot
// 0x009224d8) and their keys removed.
void XB_CDECL BroScr_HelmetPop(uint32_t ent);
// 0x001ab050 / 0x001ab530 (_2: a frame first, yaw 0 with int 0x33a4ea6c set, the shots paced by the
// notify 0x1973c1fe and counted off the ammo): ends on `end`, the anim script, death (and the
// target's); up to `numshots` shots at the target (eye / origin, + offset field 0x7aeb941a) while the
// enemy is near (or mode "forceShoot"), with ammo and within 20 degrees, 0.05 s apart, until
// `end_time`; then notify `end`. Releases `mode`.
void XB_CDECL BroScr_ShootAtTarget(uint32_t ent, uint32_t end, int32_t numshots, ScriptString mode, int32_t end_time,
                                   uint32_t target);
void XB_CDECL BroScr_ShootAtTarget_2(uint32_t ent, uint32_t end, int32_t numshots, ScriptString mode, int32_t end_time,
                                     uint32_t target);
// 0x001a6b10: friendly reinforcement waves: every 2 s while the level's wave trigger (+0x1d0) has a
// target: count the wave friendlies (bool 0xed6f4ccc) into level +0xf8 and spawn more (a random
// spawner of the target, zone at `ent` when level +0x188) up to level +0x158 (7 when undefined),
// each named <target>_spawned, marked, then either group "friendly_wave" with the player as goal or a
// level +0x18c thread (level, guy) (level +0x5c set), and a credits name; 2 s between spawns.
[[noreturn]] void XB_CDECL BroScr_FriendlyWaveSpawn(uint32_t ent);

// 0x00199bc0: once per spawner (int 0xfbac8485): while its count lasts: waits for 0xccd105e5 until int
// 0x070b8a34 is set (and for the player in the trigger with int 0xa7ad4a18), the delay 0x6e62eedd,
// a (force) spawn in the zone (level +0x188), then after its death a debug wait ("DIED" / "DELETED"),
// script_delay or delay_min + RandomFloat(range) or 5 + RandomFloat(4) s, and a growing extra wait
// (step float 0xce8a66f4, 1 when undefined); 2 s after a failed spawn.
void XB_CDECL BroScr_TriggeredSpawnerThink(uint32_t spawner, uint32_t trigger);

}  // namespace game
