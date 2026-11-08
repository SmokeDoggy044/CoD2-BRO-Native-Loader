#pragma once

// Compiled level-script threads of the shared scripts (levelscript module, .text 0x00131180 -
// 0x0013d650): plane crash moves, explosion sounds, vehicle / plane mg42 fire loops, tank dead
// guys, death-physics launches, flak sound blends, engine smoke trails, shell shock, tank stun,
// debug loops, respawner attributes, turret stutter, wall-hop rotation, intro screen, weapon
// sounds, vehicle deaths, drone fire, plane crash thinkers, flak deaths, random rotation. Every
// one but BroScr_PlayerInsideEntZone waits (Scr_Wait*, Scr_WaitTill*, BroScr_WaitFrame): class thread_switch, kept out of kFunctions until script threads run as
// coroutines. Names are ours (from the bodies); "death" is hash 0x06730ae8, the player handle the
// word at 0x00922eec.

#include <cstdint>

#include "compiler.h"
#include "qcommon_fs.h"
#include "scr_access.h"
#include "script_string.h"

namespace game {

// 0x00131180 (FUN_00131180; proposed BroScr_PlayerInsideEntZone; registered): whether the player
// is behind `ent` along its vector key 0x6ff72745 (dot(v, ent - player) > 0) and between the planes
// at `near_dist` and `far_dist` along its vector key 0xe2e8d8a5 (in front of the first, behind the
// second). The original writes the three dot products into the ent / near / far argument slots.
bool* XB_CDECL BroScr_PlayerInsideEntZone(bool* out, uint32_t ent, float near_dist, float far_dist);

// 0x001315b0: each frame reads the vector key 0xe2e8d8a5, the origin twice, the angles and their
// forward vector (results unused: a debug draw compiled out).
void XB_CDECL BroScr_DebugForwardStub(uint32_t ent);
// 0x00131720 / 0x00131d70: each frame adds the distance moved to a counter (from 0.001); while the
// vehicle speed (slot 0x00922b38) is above 1, every 128 units plays the string key 0x1cc418a4 at
// the origin. Never returns.
void XB_CDECL BroScr_StukaPlaneCrashMove(uint32_t ent);
void XB_CDECL BroScr_P47PlaneCrashMove(uint32_t ent);
// 0x001318b0 / 0x00131f00: the string key 0xe6f61fa7 at the origin three times, 0.2 s and 0.4 s
// apart.
void XB_CDECL BroScr_PlayExplodeSounds(uint32_t ent);
void XB_CDECL BroScr_PlayExplodeSounds_2(uint32_t ent);
// 0x00131c30: notifies and ends on "stop mg42s", ends on death; while alive: unless `keep_target`
// both turrets target the player; then every 0.05 s turret call 272 on the left (0xef6522f9) and
// right (0xeead8089) turret.
void XB_CDECL BroScr_VehicleMg42Fire(uint32_t ent, bool keep_target);
// 0x00132280: ends on "stop firing"; ends the thread in key 0x65637ee2 and stores its own there;
// then as BroScr_VehicleMg42Fire (targets when `keep_target` is the undefined bool).
void XB_CDECL BroScr_PlaneTurretsFire(uint32_t ent, uint8_t keep_target);
// 0x00132490: one record (clamped to the last of four) of a dead-guy table: after its delay, a
// real spawn (BroScr_TankDeadGuyPlayAnim thread) when the player is at least 3500 away and the
// record is not model-only, else a "script_origin" and a "script_model" 128 below `pos` playing
// the record's anim (anim tree "tankdeadguys"), deleted after 20 s.
struct TankDeadGuyRecord {
    uint32_t spawner;  // +0x0
    int32_t anim;      // +0x4: the spawned guy's anim (as a float to the thread)
    uint32_t model_anim;  // +0x8: set from 0x008cd1ac / 0x008cd1a0 / 0x008cd194 for records 0-2
    uint32_t model_only;  // +0xc
    float delay;          // +0x10
};
struct TankDeadGuyTable {
    TankDeadGuyRecord record[4];
};
void XB_CDECL BroScr_SpawnTankDeadGuys(uint32_t self, uint32_t unused, TankDeadGuyTable table, uint32_t index,
                                       uint32_t unused2, ScrVec3 pos, ScrVec3 angles);
// 0x00132ab0: BroScr_NotifyLevelOnDeath(self, ent) thread; ends on `ent`'s death; waits for
// 0x0032304c on `ent`; then 0x92b8c65f on the level, `self` and `ent`.
void XB_CDECL BroScr_PauseUnpauseWait(uint32_t self, uint32_t ent);
// 0x00132b40 / 0x00132bb0 / 0x00132c80 / 0x00132d50: BroScr_DeathPhysicsLaunch_2 with an undefined
// force, the tag's origin as force, or the given force; delay the word at 0x00922ea4 (the second
// from-tag form takes it as an argument).
void XB_CDECL BroScr_DeathPhysicsLaunchThread(uint32_t ent, ScrVec3 angles, uint32_t launch_arg);
void XB_CDECL BroScr_DeathPhysicsLaunch_2FromTag(uint32_t ent, ScrVec3 angles, uint32_t launch_arg, ScriptString tag);
void XB_CDECL BroScr_DeathPhysicsLaunch_2FromTag_2(uint32_t ent, ScrVec3 angles, uint32_t launch_arg,
                                                   ScriptString tag, ScrVec3 unused, float delay);
void XB_CDECL BroScr_DeathPhysicsLaunch_2Call(uint32_t ent, ScrVec3 angles, uint32_t launch_arg, ScrVec3 force);
// 0x001336e0: a "sound_blend" at the origin, ends on the level's "flak died with flaknum <n>",
// BroScr_FlakWaitDeath_2 thread on it; then forever: waits for "<name>on", blends 0 -> 1 in 0.1
// steps every 0.05 s, waits for "<name>off", blends back (the blend calls themselves are gone).
void XB_CDECL BroScr_FlakSoundBlend(uint32_t ent, ScriptString name, int32_t flaknum);
// 0x00133950: ends on death; a "script_origin" at "tag_engine" (key 0x4c9e9556) linked to the
// vehicle; for up to 10 s while faster than 100: every 48 units moved plays
// "smk_trail_thick_lg_black_a"; then until the 10 s are up every 0.1-0.4 s.
void XB_CDECL BroScr_EngineSmokeTrail(uint32_t ent);
// 0x00133db0: the player's bool key 0x2eecefb8 and sentient int +0x30 set for `seconds` - 1
// (ends on the level's "player is shell shocked"), then cleared.
void XB_CDECL BroScr_ShellShockWait(uint32_t unused, int32_t seconds);
// 0x00134150: as BroScr_StukaPlaneCrashMove, but ends when the speed drops to 1 or below.
void XB_CDECL BroScr_MoveSoundEvery128Units(uint32_t ent);
// 0x00134540: ends the thread in key 0xaee44119 when defined and stores its own; then as
// BroScr_PlaneTurretsFire on "stop mg42s" / death.
void XB_CDECL BroScr_VehicleMg42Fire_2(uint32_t ent, uint8_t keep_target);
// 0x001349d0 / 0x00134a30: ends on death; int key 0x8c05ef86 / 0xcd84c908 = 0; waits forever.
void XB_CDECL BroScr_DebugStub_h8c05ef86(uint32_t unused, uint32_t ent);
void XB_CDECL BroScr_DebugStub_hcd84c908(uint32_t unused, uint32_t ent);
// 0x00134a90: while alive: int key 0x2f66b4d8 = 0, waits for "damage", key = 1, waits
// `seconds` (0.1 s steps), then `seconds` more, notifies 0xecd3b220.
void XB_CDECL BroScr_TankStunOnDamage(uint32_t ent, int32_t seconds);
// 0x00134c40: ends on "novistarget"; waits for "turret_on_vistarget"; int key 0x4d917ba7 = 1.
void XB_CDECL BroScr_TurretVisTargetWait(uint32_t ent);
// 0x00134cb0: each frame the forward vectors of the angles turned by +/- the float key 0xbe149b8c
// (results unused: a debug draw compiled out).
void XB_CDECL BroScr_DebugForwardStub_2(uint32_t ent);

// 0x00132f40: after a frame, each attribute of the record that is not undefined is set on the
// actor (undefined int / handle / float / vector lanes, empty string, invalid entity skip it), then
// with the "debug_attrib" cvar 1 a BroScr_EmptyFunc_31 thread; frees the record's strings
// (RespawnerAttributes_Dtor 0x0052b970). Field names: the setter (interface slot) each one feeds.
struct RespawnerAttributes {
    float actor_d4;  // +0x00 (argument +0x08): ScrActor_SetFloat_d4
    float actor_d8;  // +0x04 (argument +0x0c): ScrActor_SetFloat_d8
    ScrVec3 actor_108;  // +0x08 (argument +0x10): ScrActor_SetVec_108
    ScrVec3 actor_114;  // +0x14 (argument +0x1c): ScrActor_SetVec_114
    ScrVec3 actor_120;  // +0x20 (argument +0x28): ScrActor_SetVec_120
    float actor_828;  // +0x2c (argument +0x34): ScrActor_SetFloat_828
    float actor_82c;  // +0x30 (argument +0x38): ScrActor_SetFloat_82c
    float actor_824;  // +0x34 (argument +0x3c): ScrActor_SetFloat_824
    uint32_t actor_830;  // +0x38 (argument +0x40): ScrActor_SetInt_830
    uint32_t actor_7b0;  // +0x3c (argument +0x44): ScrActor_SetInt_7b0
    uint32_t actor_7b4;  // +0x40 (argument +0x48): ScrActor_SetInt_7b4
    uint32_t actor_7dc;  // +0x44 (argument +0x4c): ScrActor_SetShort_7dc
    float actor_7b8;  // +0x48 (argument +0x50): ScrActor_SetFloat_7b8
    float actor_7bc;  // +0x4c (argument +0x54): ScrActor_SetFloat_7bc
    uint32_t actor_22c;  // +0x50 (argument +0x58): ScrActor_SetInt_22c
    ScrVec3 actor_234;  // +0x54 (argument +0x5c): ScrActor_SetVec_234
    uint32_t actor_230;  // +0x60 (argument +0x68): ScrActor_SetInt_230
    uint32_t actor_240;  // +0x64 (argument +0x6c): ScrActor_SetInt_240
    uint32_t actor_194;  // +0x68 (argument +0x70): ScrActor_SetInt_194
    float actor_7a4;  // +0x6c (argument +0x74): ScrActor_SetFloat_7a4
    float actor_154;  // +0x70 (argument +0x78): ScrActor_SetFloat_154
    float actor_7e4;  // +0x74 (argument +0x7c): ScrActor_SetFloat_7e4
    uint32_t actor_7e8;  // +0x78 (argument +0x80): ScrActor_SetInt_7e8
    uint32_t actor_7ec;  // +0x7c (argument +0x84): ScrActor_SetInt_7ec
    uint32_t actor_1308;  // +0x80 (argument +0x88): ScrActor_SetInt_1308
    ScriptString actor_ec;  // +0x84 (argument +0x8c): ScrActor_SetString_ec
    uint32_t actor_f0;  // +0x88 (argument +0x90): ScrActor_SetInt_f0
    uint32_t actor_7d8;  // +0x8c (argument +0x94): ScrActor_SetInt_7d8
    float actor_130c;  // +0x90 (argument +0x98): ScrActor_SetFloat_130c
    uint32_t actor_1310;  // +0x94 (argument +0x9c): ScrActor_SetRef_1310
    uint32_t actor_131c;  // +0x98 (argument +0xa0): ScrActor_SetInt_131c
    uint32_t actor_1328;  // +0x9c (argument +0xa4): ScrActor_SetInt_1328
    uint32_t actor_12a0;  // +0xa0 (argument +0xa8): ScrActor_SetRef_12a0
    uint32_t actor_cc;  // +0xa4 (argument +0xac): ScrActor_SetInt_cc
    uint32_t actor_1374;  // +0xa8 (argument +0xb0): ScrActor_SetByte_1374
    uint32_t actor_7de;  // +0xac (argument +0xb4): ScrActor_SetByte_7de
    int32_t sentient_18;  // +0xb0 (argument +0xb8): ScrSentient_SetFloat_18
    uint32_t actor_1378;  // +0xb4 (argument +0xbc): ScrActor_SetInt_1378
    uint32_t actor_137c;  // +0xb8 (argument +0xc0): ScrActor_SetInt_137c
    ScriptString actor_1384;  // +0xbc (argument +0xc4): ScrActor_SetString_1384
    ScriptString actor_1388;  // +0xc0 (argument +0xc8): ScrActor_SetString_1388
    ScriptString actor_138c;  // +0xc4 (argument +0xcc): ScrActor_SetString_138c
    ScriptString actor_2c0;  // +0xc8 (argument +0xd0): ScrActor_SetString_2c0
    uint32_t actor_184;  // +0xcc (argument +0xd4): ScrActor_SetInt_184
    uint32_t health;  // +0xd0 (argument +0xd8): ScrEnt_SetHealth
    uint32_t actor_228;  // +0xd4 (argument +0xdc): ScrActor_SetInt_228
    uint32_t sentient_30;  // +0xd8 (argument +0xe0): ScrSentient_SetInt_30
    uint32_t sentient_28;  // +0xdc (argument +0xe4): ScrSentient_SetInt_28
};
void XB_CDECL BroScr_RespawnerApplyAttribs(uint32_t ent, RespawnerAttributes a);
// 0x00135020: waits forever (0.075 s steps).
void XB_CDECL BroScr_WaitForever();
// 0x00135030: ends on death / "stop_stutter"; forever: waits off_min + random(off_max - off_min),
// BroScr_TurretSetAlliesTeam, waits on_min + random(on_max - on_min), turret mode 0xdc26d84c on the
// entity key 0x2f68c7a5.
void XB_CDECL BroScr_TurretStutter(uint32_t ent, float on_min, float on_max, float off_min, float off_max);
// 0x00135630: waits for `notify` (with a string, "undefined" when none) until the string's hash is
// 0x000199a4, 0x9aa82fee, 0xcc5542ad or `done`.
void XB_CDECL BroScr_WaitTillFinished(uint32_t ent, uint32_t notify, uint32_t done);
// 0x001356f0: pitch +30 over the sum of the first `split` times, waits "rotatedone", pitch -30 over
// the sum of the rest, waits again; frees `times`.
void XB_CDECL BroScr_WallHopRotate(uint32_t unused, uint32_t ent, DynArray times, int32_t split);
// 0x00135e10: on the level: ends on `end_on` (when not empty); notifies and ends on `notify` (when
// not empty); then waits forever (0.1 s steps). The first seven words are not read.
void XB_CDECL BroScr_WaitForNotifyForever(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                                          uint32_t a6, uint32_t a7, ScriptString notify, ScriptString end_on);
// 0x00136120: the flak88's dead model "xmodel/turret_flak88_d" and sound "explo_metal_rand" (string
// keys 0xd6eafad9 / 0xd70abfb1); after death: int key 0xa027aabf = 0, the model, the sound, turret
// target cleared, radius damage 320 (750 - 650) and an earthquake (0.25, 3 s, 1050) at the origin.
void XB_CDECL BroScr_VehicleDeath_Flak88(uint32_t ent);
// 0x00137230: waits for 0x08d272e5 on the level vehicle named `name` (when there is one).
void XB_CDECL BroScr_WaitTillVehicleByName(ScriptString name);
// 0x001372b0: BroScr_ThrowGrenadeRightHand with the stand (pose 0x070b89a4, z 0x0083ecdc) or crouch
// (0xc1165808, z 0x007f5cfc) throw anim; nothing for another pose.
void XB_CDECL BroScr_TryThrowGrenadeRightHand(uint32_t ent, ScrVec3 target, float time, float fuse);
// 0x00137370: BroScr_ThrowGrenadeByPose with an undefined fuse.
void XB_CDECL BroScr_ThrowGrenadeByPoseCall(uint32_t ent, ScrVec3 target);
// 0x001373b0: "introScreen" 1; from a checkpoint (cvar "checkpoint" not "0") an empty intro line,
// else the level's four intro string keys; then "introScreen" 0.
void XB_CDECL BroScr_SetIntroScreenCvars();
// 0x00137c50: forever: waits for 0xa7062d94 with a string; "weapsound0".."weapsound4" play the
// player weapon sound 0..4.
void XB_CDECL BroScr_PlayerWeaponSounds(uint32_t ent);
// 0x00138650: waits for 0x262a34d8 with the spawned guy; unless it finished spawning, waits for its
// death; then the group's count key 0xb31e91b0 - 1 and "spawn guy died" on the group.
void XB_CDECL BroScr_NotifySpawnGuyDied(uint32_t unused, uint32_t spawner, uint32_t group);

// 0x00139da0: the M13's dead model "v_m13_d1", sounds "vehicle_explode" (key 0xcc2baca1) and
// "explo_metal_rand" (key 0xd70abfb1); after death: the model, the metal sound, death anim knob
// 0x008cd4ac (1, 0.2, 1, 1), the explosion sound, an earthquake (0.25, 3 s, 1050).
void XB_CDECL BroScr_VehicleDeath_M13(uint32_t ent);
// 0x0013e770: the same with the dead model "v_stuart_d1".
void XB_CDECL BroScr_VehicleDeath_Stuart(uint32_t ent);
// 0x00139f30: ends on the rider's death; after the vehicle's 0xecd7b044 (unload): weapon back in
// hand, the rider's delay, the unload anim (key 0x2d7db3f6) at the vehicle's tag (string key
// 0xb6abdc5e); then unlinked with all stances, notifies 0xa663db67 and restores its animname (key
// 0xf203df5e) when set. Stops when the rider dies.
void XB_CDECL BroScr_VehicleRiderUnload(uint32_t vehicle, uint32_t rider);
// 0x0013dc30 / 0x0013e900: the same body.
void XB_CDECL BroScr_VehicleRiderUnload_2(uint32_t vehicle, uint32_t rider);
void XB_CDECL BroScr_VehicleRiderUnload_3(uint32_t vehicle, uint32_t rider);
// 0x0013a0f0: ends on "nokill"; anim tree "halftrack", dead model
// "v_M3A1_Halftrack_troop_d1_desert", sound "explo_metal_rand"; after death: the model and an
// earthquake (0.25, 3 s, 1050).
void XB_CDECL BroScr_VehicleDeath_Halftrack(uint32_t ent);
// 0x0013a250: until `notify` brings "end", "finished" or none: each "fire" plays
// "drone_muzzleflash" at "tag_flash" and a random tracer 2000 along the tag's forward. Returns the
// last string (copied to *out).
ScriptString* XB_CDECL BroScr_DroneShoot(ScriptString* out, uint32_t ent, uint32_t notify);
// 0x0013a680 / 0x0013a8f0: a BroScr_PlayExplodeSounds / _2 thread, then as
// BroScr_StukaPlaneCrashMove with the string key 0xe6f61fa4 every 64 units.
void XB_CDECL BroScr_StukaPlaneCrashThink(uint32_t ent);
void XB_CDECL BroScr_P47PlaneCrashThink(uint32_t ent);
// 0x0013a830 / 0x0013aaa0: ends on death; health + 1000; damage below 300 is given back; the first
// at or above it ends in radius damage 2 (10000 - 9000) at the origin.
void XB_CDECL BroScr_WingTurretDamageWatch(uint32_t ent);
void XB_CDECL BroScr_WingTurretDamageWatch_2(uint32_t ent);
void XB_CDECL BroScr_WingTurretDamageWatch_3(uint32_t ent);  // 0x0013eb00, the same body
// 0x0013b200: anim tree "panzerIV", dead model = model + "_d1", sound "vehicle_explode"; after
// death: the model, the sound, an earthquake (0.25, 3 s, 1050) and damage twice the player's
// health to the player.
void XB_CDECL BroScr_VehicleDeath_PanzerIV(uint32_t ent);
// 0x0013b400: ends on death; forever: yaw a = 10 + random(20), pitch b = 4 + random(3), time
// t = 0.5 + random(0.5): BroScr_RandomRotateStep, rotate to (key 0x07355c08, base - 2b, -2a) in t
// (accel / decel t/2), "rotatedone", the step again, rotate to (key, base + 2b, 2a).
void XB_CDECL BroScr_RandomRotateLoop(uint32_t ent);
// 0x0013c710: waits for the level's "flak died with flaknum <n>"; then the death anim knob (key
// 0x9af3e4c4, weight 1) when the current anim is defined.
void XB_CDECL BroScr_FlakDied(uint32_t ent, int32_t flaknum);
// 0x0013cc40: waits for "trigger"; clears the level's flag byte `index` (bytes at level + 0x548).
void XB_CDECL BroScr_WaitTriggerClearFlag(uint32_t trigger, uint32_t index);
// 0x0013ceb0: forever, every 0.05 s: the vehicle unusable, usable again while any rider lives.
void XB_CDECL BroScr_FlakPanzerKeepUnusable(uint32_t vehicle, DynArray riders);
// 0x0013d270: ends on 0xc4e110dd; every 0.5 s: below half the starting health a
// BroScr_EngineSmokeTrail thread and 0xc4e110dd.
void XB_CDECL BroScr_SmokeWhenHalfHealth(uint32_t ent);
// 0x0013d580: ends on death; while alive, 88% of each "damage" below 900 is given back.
void XB_CDECL BroScr_ReduceDamage(uint32_t ent);

// 0x00137780: the player's slot weapons ("primary", "primaryb", "pistol", "grenade"), their ammo
// and clip ammo and the "stielhandgranate" count saved, all weapons taken; after the level's
// 0x54965009 each slot is checked and its ammo restored, the grenades given back (when any); then
// 0x88c7e941 on the level.
void XB_CDECL BroScr_SetupPlayerWeapons();
// 0x00138230: the chain nodes: every node without spawnflag 2 that has a target and whose target or
// targetname nodes (accumulated over all nodes) are not empty. Then forever, each frame, with the
// cvar "chain" "1": the distance from the player to each chain node and each ally's sentient ref
// 0x48 (results unused: a debug draw compiled out).
void XB_CDECL BroScr_InitFriendlyChainNodes();
// 0x00138760: after `ent`'s death, every trigger (multiple, once, use, damage, lookat) named
// "friendly_chat" whose target is `ent`'s targetname is deleted.
void XB_CDECL BroScr_FriendlyChatTriggers(uint32_t unused, uint32_t ent);

// 0x0013c530: after `ai`'s death: the level's flak flag `flaknum` cleared and "flak died with
// flaknum <n>" on the level; "stop_moving" on `flak` when its int key 0x1ccaaaf3 is set; `ai`
// teleported 2 / 2 / 5 off its origin.
void XB_CDECL BroScr_FlakStopMovingOnDeath(uint32_t flak, uint32_t ai, uint32_t flaknum);
// 0x0013c810: ends on `trigger`'s death; when `trigger` is the "trigger_damage" with noteworthy
// "deathtrig": waits until the "player_tank" damages it, then a BroScr_NotifyExplode thread on
// `flak`. Then `flak` deleted, and while the player is within its script_offradius (350) for up
// to script_offtime (20000 ms), waits a second; the flak flag cleared and "flak died with
// flaknum <n>" on the level.
void XB_CDECL BroScr_FlakDeathTrigger(uint32_t flak, uint32_t trigger, uint32_t flaknum);
// 0x0013cc90: waits for death (with `on_death` set) or "explode"; "broken" on the level,
// "explo_metal_rand", an earthquake (0.75, 3 s, 1150), the flak flag cleared, "flak died with
// flaknum <n>" and the objective (when set) on the level; after 0.22 s the model in key 0xb8eafadd.
void XB_CDECL BroScr_FlakDeathThink(uint32_t ent, uint32_t flaknum, uint8_t on_death);
// 0x0013cf40: a BroScr_EndSequenceOnDeath thread per crew member; after "end_sequence" on the flak,
// each live member gets it too, health 250, and with bool key 0x1ea97196 (cleared) an
// "xmodel/panzerflak_ammo" on its "tag_weapon_left". Frees `crew`.
void XB_CDECL BroScr_FlakLoaderAmmoAnim(uint32_t flak, DynArray crew);
// 0x0013d2e0: after death: its mg turret deleted, radius damage 128 (800 - 400) 128 above, the dead
// model ("v_sdkfz222_mons_d1" on mons1, else model + "_d1"), "vehicle_explode", an earthquake
// (0.25, 3 s, 1050); "unload" when slower than 1 mph.
void XB_CDECL BroScr_VehicleDeath_Sdkfz222(uint32_t ent);

// 0x0013aca0: int key 0x086550be = 1; the "tankspawner" guys (by target) go into a dead-guy table
// (BroScr_TankSpawnerSlots_Init: getout right / left / back with their death anims, model-only
// random, delays 0, 2 + random(1), that + random(1) + 2); after 0x84785ea9: with bool key
// 0xc54b0523 the key is cleared and nothing spawns; else the hatch tag's origin and angles, and per
// table entry (the table's count word) the spawner moved to the tank and a BroScr_SpawnTankDeadGuys
// thread; after 10 s 0x9d03d3cf. Every guy is model-only when the first spawner is invalid; with
// more than 10 axis AI the first is too.
void XB_CDECL BroScr_TankSpawnerThink(uint32_t self, uint32_t tank);

// 0x0013b880: ends on death; walks a node chain: per node the goal radius from the node (valid
// handle), a "misc_mg42" / "misc_turret" it targets gets a BroScr_MgWaitNewUser thread (radius 4),
// goal at the node, waits for 0x0032304c, uses that turret, the node's script_delay; next is a random
// node it targets. At the end: the level's panzer thread (word +0x318) for script_panzer 1; unless
// the last node is "no_last_goal": `mode` "sometimes" (half the time) or "player" sets the goal on
// the player (radius at least 192 / 128); then waits for 0x0032304c, radius 512. Releases `mode`.
void XB_CDECL BroScr_FollowNodeChain(uint32_t ai, uint32_t node, ScriptString mode);

// 0x00138ba0: the infinite panzerfaust box: of the trigger's targets the panzerfaust objective
// model and the ammo-box objective model get their plain models, the others are replaced by
// "script_origin"s (yaw + 270); hint "DAWNVILLE_PANZER"; a BroScr_PanzerfaustObjectiveModels
// thread on the level. Then forever on "trigger": "picked up infinite panzerfaust" on the level,
// objective off on the trigger; a panzerfaust in the primary / primaryb slot is refilled (taken,
// 0.1 s, given, switched to); else the weapon in the current slot is dropped onto the first free
// origin (key 0xda24dbe7) as its classname's model and replaced by a panzerfaust.
void XB_CDECL BroScr_PanzerfaustAmmoBox(uint32_t trigger);

// 0x0013d650: a rocket hitting near the player: "weapons_rocket_explosion", after 0.25 s radius
// damage 320 (base + random(range) - min) 8 / 2 off the player and an earthquake (0.75, 2 s, 2250),
// max health 100; while alive: player pm flags 0x80000 / 0x100000 off and 0x200000 on, after
// 0.15 s a view kick from `ent`, shell shock
// `shock` ("default") for `shock_time` (12; a BroScr_ShellShockWait thread unless `no_wait_thread`
// is set), after 1.5 s the first two flags back on. Defaults 150 / 100 / 100. Releases `shock`.
void XB_CDECL BroScr_RocketExplosion(uint32_t ent, int32_t shock_time, int32_t base_damage, int32_t damage_range,
                                     int32_t min_damage, uint32_t no_wait_thread, ScriptString shock);

// 0x0013d9a0: anim tree "panzerIV"; dead model: "v_sherman" stays, "v_sherman_snow" ->
// "v_sherman_snow_d1", else the model; sounds "vehicle_explode" / "explo_metal_rand"; after death:
// the model, both sounds, an earthquake (0.25, 3 s, 1050).
void XB_CDECL BroScr_VehicleDeath_Tank(uint32_t ent);
// 0x0013eac0: ends on death; on each "shoot_guns" a BroScr_VehicleMg42Fire_2 (targets kept).
void XB_CDECL BroScr_VehicleShootGunsThink(uint32_t ent);
// 0x0013ebc0: ends on "nokill"; after death: the tank-spawner key cleared (bool key 0xc54b0523),
// the mg turret deleted, 0x10a95eae, turret target cleared, an earthquake; a rolling death keeps
// at least 8 until 0x6b6fe7fa; stopped, after 0.1 s the dead model, after 0.3 s full stop,
// 0x84785ea9, paths disconnected; waits for the spawner's 0x9d03d3cf (when spawning); bool key
// 0x00339844 set.
void XB_CDECL BroScr_VehicleDeathThink(uint32_t ent);
// 0x0013f550: BroScr_TurretStutter(ent, on_min, on_max, on_min, on_max).
void XB_CDECL BroScr_TurretStutterCall(uint32_t ent, float min, float max);
// 0x0013f570 / 0x0013f6f0: ends on "nokill" (death only); anim tree "willyjeep", dead model
// "v_willysjeep_d1" ("_mons" on mons1), sound "explo_metal_rand". The death: after death the
// model and an earthquake (0.25, 3 s, 1050). The init: a BroScr_VehicleDeathThink thread, then
// waits for 0x10a95eae.
void XB_CDECL BroScr_VehicleDeath_Jeep(uint32_t ent);
void XB_CDECL BroScr_VehicleInit_Jeep(uint32_t ent);
// 0x0013f820 (FUN_0013f820; proposed BroScr_WaitHeavyHitOrDamage): waits for damage (FUN_006609c0
// with the attacker) and gives *dir = ent - attacker each time: true for a single hit above 95,
// false once the hits add up past 150.
uint8_t* XB_CDECL BroScr_WaitHeavyHitOrDamage(uint8_t* out, uint32_t ent, float* dir);

// 0x00141840 / 0x00148c90: BroScr_TryThrowGrenadeRightHand(ent, target, time / undefined, undefined).
void XB_CDECL BroScr_TryThrowGrenadeRightHandCall(uint32_t ent, ScrVec3 target, float time);
void XB_CDECL BroScr_TryThrowGrenadeRightHandCall_2(uint32_t ent, ScrVec3 target);
// 0x00141890: letterbox bars. With `setup_weapons` and `closing` (low bytes) BroScr_SetupPlayerWeapons
// runs as a thread on the level entity. Not closing: BroScr_FadeHudBlack(level entity, duration, closing,
// 70) (the bars shrink and go), notify 0x54965009 on the level entity, letterbox_enabled 0, cg_drawgun
// "1", hud visible, Scr_WaitFrames(0). Closing: letterbox_enabled 1, cg_drawgun "0", hud hidden, two
// new "black" hud elements (level +0x79c at y 0 and +0x270 at y 480, 640 wide, 0 / 70 high), the
// fade, +0x79c 70 high, +0x270 to y 410, notify 0x71291ea8. The first argument is not read.
void XB_CDECL BroScr_LetterboxFade(uint32_t self, float duration, uint32_t closing, uint32_t setup_weapons);
// 0x00148ce0: BroScr_LetterboxFade(self, duration, closing, true).
void XB_CDECL BroScr_LetterboxFadeOn(uint32_t self, float duration, uint32_t closing);

// 0x00141c00: the halftrack named by `trigger`'s target gets a BroScr_FriendlyChatTriggers thread (once:
// bool field 0x0718b2c3) and ends this thread on its "death"; then, on each trigger (Scr_WaitTillOutputEnt
// 0xe2a210cf): with spawnflags 8 the halftrack (or, without one, an allied AI, which becomes it) must be
// the one touching; without, the player (the halftrack then defaults to the allied AI nearest the player;
// none: return). When an axis AI touches the trigger: script_noteworthy "grenades" waits for that notify
// on the halftrack, 1.2 s, the "weaponslot grenade" key binding and the (empty) hint
// "SCRIPT_HINT_SWITCHTOGRENADE"; "get halftrack gunner" waits for that notify and 1 s. Then the trigger
// is deleted. The first argument is not read.
void XB_CDECL BroScr_HalftrackGunnerGrenadeHint(uint32_t self, uint32_t trigger);

// 0x00142d50 / 0x00142ee0 / 0x00143870 / 0x00145080: vehicle inits. Dead model key 0xd6eafad9
// ("v_m13_d1"; "v_M3A1_Halftrack_troop_d1_desert"; PanzerIV: model + "_d1" unless defined; Sherman:
// "v_sherman_d1", the snow / buchholz ones "v_sherman_snow_d1", the bulldozer none, else the model
// unless defined), sound 0xd70abfb1 "explo_metal_rand", a BroScr_VehicleDeathThink thread (Sherman: unless
// custom_kill is defined), then waits for 0x10a95eae; M13 then sets the dead model, and M13 / PanzerIV /
// Sherman play the "vehicle_explode" key (0xcc2baca1). The halftrack and PanzerIV set their anim tree.
void XB_CDECL BroScr_VehicleInit_M13(uint32_t ent);
void XB_CDECL BroScr_VehicleInit_Halftrack(uint32_t ent);
void XB_CDECL BroScr_VehicleInit_PanzerIV(uint32_t ent);
void XB_CDECL BroScr_VehicleInit_Sherman(uint32_t ent);

// 0x00142070: the friendly halftrack from `trigger`'s target and the wait for the trigger as in
// BroScr_HalftrackGunnerGrenadeHint (with Scr_EndOn death); then the axis AI and the axis spawners with
// a count touching the trigger: entity 0's int key 0xb31e91b0 = the spawner count, a
// BroScr_NotifySpawnGuyDied thread per spawner; waits until every touching AI is dead (dead ones
// removed after each death) and the key is down to 0 ("spawn guy died"), 2 s, notifies the level
// "<halftrack animname> <trigger script_noteworthy>" and deletes the trigger. The first argument is
// not read.
void XB_CDECL BroScr_SpawnGuyDiedThink(uint32_t self, uint32_t trigger);
// 0x00142800: a point per panzerfaust of BroScr_GetPanzerfaustRespawns (its target, else a spawned
// script_origin; the panzerfaust's angles); every 3 s, once fewer are left than points: notifies
// "picked up infinite panzerfaust", spawns a "weapon_panzerfaust" (targetname "respawn") at each point,
// and after 0.3 s deletes the old ones. Never returns (no respawns: returns at once).
void XB_CDECL BroScr_InfinitePanzerfaust();

// 0x00143020 / 0x00143460: plane crashes (Stuka / P47). Effect keys, dead model ("..._stuka_shot" /
// "v_p47_d1"); after death: turrets deleted (BroScr_DeleteVehicleTurrets_2 / _3), "explo_metal_rand",
// a BroScr_*PlaneCrashThink thread, an earthquake (0.25, 3 s, 1050), a script_origin (entity key
// 0x5553515d) linked to "tag_engine_left", a BroScr_*PlaneCrashMove thread; model "..._stuka_d" /
// "v_p47_d1" (script_vehiclegroup undefined, 4 or 5) or the dead model; 0.7 s.
void XB_CDECL BroScr_StukaPlaneCrash(uint32_t ent);
void XB_CDECL BroScr_P47PlaneCrash(uint32_t ent);

// 0x00143a40 (FUN_00143a40; proposed BroScr_FlipLevelFlagOnDeath): after `ent`'s "death", the level's
// byte array key 0xe337f297 [index] becomes (its old value != 1). The first argument is not read.
void XB_CDECL BroScr_FlipLevelFlagOnDeath(uint32_t self, uint32_t ent, uint32_t index);
// 0x00143b40: waits for the trigger's "trigger"; a BroScr_FlakDeathTrigger(flak, each trigger the
// start trigger targets, flaknum) thread; notifies the flak 0x17349563 and deletes the trigger.
void XB_CDECL BroScr_FlakWaitStartTrigger(uint32_t flak, uint32_t trigger, uint32_t flaknum);

// 0x00143cb0: `ent` plays the anim the name's hash picks from its anim-set field 0xc4813666 (words
// 0, 1, 2, 5, 6; none: the "bad animname passed in" message) scripted at `flak` (notify 0xfd3cf694),
// counted in the level's per-flak array (+0x50c); its note tracks until "end" take the
// "xmodel/panzerflak_ammo" clip off "tag_weapon_left" ("clip on") and back ("clip off"), bool key
// 0x1ea97196. The last one done notifies the level "flak_anim_done<flaknum>". Releases `animname`.
void XB_CDECL BroScr_FlakCrewAnim(uint32_t ent, uint32_t flak, uint32_t flaknum, ScriptString animname);

// 0x001441c0: as BroScr_FlakCrewAnim with the anim-set words 0-6, as the flagged anim knob 0x54e3b2b6
// (int key 0x9af3e4c4 = the anim); with cvar "old flak" not "on" a "panzer" starts its loop sound
// thread ("fireA": BroScr_FlakPanzerLoopSounds, else BroScr_FlakPanzerLoopSound). While the level's
// byte flag +0x684 [flaknum] is set, the note tracks until "end": the four clips ("clip_<n> on" / "off",
// "tag_clip_<n>", bool keys 0xc0f54bd1..4) and, except for a "panzer", the loop / cooldown sounds of
// "fire_1" / "fire_2" (600 ms; keys 0x872c50bf / 0x34314719). Releases both strings.
void XB_CDECL BroScr_FlakCrewAnims(uint32_t ent, ScriptString animname, uint32_t flaknum, ScriptString type);

// 0x00145310: ends on "death"; with a node (key 0x05374cc6) that has a target: the wait range fields
// 0x7212d405 / 0x7212d513 default to 5 / 15; after "goal" a random wait in that range,
// BroScr_SetRandomGoalNode, then the same thread again.
void XB_CDECL BroScr_SquadMemberThink(uint32_t ent);
// 0x001454a0 (FUN_001454a0; proposed BroScr_SquadMemberThinkOnNotify): as BroScr_SquadMemberThink with
// `notify` for "goal" and no wait; restarts itself (Scr_StartThread_ent_str) while the node is defined.
// Releases `notify`.
void XB_CDECL BroScr_SquadMemberThinkOnNotify(uint32_t ent, ScriptString notify);
// 0x00145ac0: as BroScr_VehicleInit_PanzerIV without the anim tree, then the dead model is set
// before the "vehicle_explode" sound.
void XB_CDECL BroScr_VehicleExplodeSound(uint32_t ent);

// 0x0014a2f0: waits for 0x92b8c65f on `ent` `count` times (at most its follower count, array key
// 0xdeca7f50). 0x00151c90: the same for all of them.
void XB_CDECL BroScr_WaitForFollowers(uint32_t ent, int32_t count);
void XB_CDECL BroScr_WaitForFollowersCall(uint32_t ent);
// 0x0014a380: waits for 0x92b8c65f on the level once per follower of every leader.
void XB_CDECL BroScr_WaitForAllFollowersNotify(DynArray* leaders);
// 0x0014a670: ends on the leader's 0xcc07a20a; on the follower's death it leaves the leader's
// follower array.
void XB_CDECL BroScr_RemoveFollowerOnDeath(uint32_t leader, uint32_t follower);
// 0x0014a910 / 0x0014a9c0: end on 0x71017853 of the entity and the level; every 0.05 s they walk the
// path-position array 0xaeae4e0b (all positions / the one at int key 0x9aec9c1a when not negative):
// the debug drawing is compiled out, only the key lookups are left. Never return.
void XB_CDECL BroScr_PathPosDebugStub(uint32_t ent);
void XB_CDECL BroScr_CurrentPathPosDebugStub(uint32_t ent);

// 0x00147cf0: ends on the rider's death; after its 0xa0e65a8e: the vehicle's int key 0xb330b301 counts
// it, a BroScr_VehicleRiderDeathWatch_2(vehicle, rider) thread; "tag_passenger1" / "tag_passenger2"
// play the flakvierling loader dismount L / R at the tag (waits for its end); the rider leaves its
// slot (array key 0x2cd69a2a), is unlinked, the count drops, 0x996a3647 on the vehicle, AI flags
// cleared and the gun back in the hand (0x07151174). Releases `tag`.
void XB_CDECL BroScr_VehicleRiderAttach(uint32_t vehicle, uint32_t rider, ScriptString tag);

// 0x00148080: as BroScr_VehicleRiderAttach with the flakvierling gun mount L / R anims; the rider's
// slot is cleared, then it becomes the gunner (slot 0, ScrCall_ScrEnt_SetTargetname_2, bool set),
// is linked to "tag_gunner" a frame later with that tag's rider anim, gets
// BroScr_VehicleRiderUnlinkOnDeath and BroScr_VehicleRiderWait(vehicle, rider, "tag_gunner") threads.
// Releases `tag`.
void XB_CDECL BroScr_VehicleRiderMount(uint32_t vehicle, uint32_t rider, ScriptString tag);

// 0x00148760: ends on death; anim tree "flakvierling" ("flakvierling_base" unless the model is
// "v_flakvierling"); on each 0xac1c7bac: fires (an earthquake 0.2 / 0.1 s / 2250) with a
// BroScr_StrafeImpacts thread; when manned (rider slot 0's bool, or bool key 0xd8e13e71) every shot
// past the count at 0x008cd168 damages the target (entity key 0x0d60fb3d, bool 0xe29137e5) by the int at
// 0x008cd16c and starts over, the shot time in int key 0x05903940; then the two barrel fire anims
// (keys 0xe0b632e2 / 0xe0b632e1) in turn, waited for. Never returns.
void XB_CDECL BroScr_FlakvierlingThink(uint32_t ent);

// 0x00148d10: the shared level main (each level script calls it): level cvars, the player
// (level +0x7f8, its bool key 0x2eecefb8 cleared, a BroScr_PlayerWeaponSounds thread), level +0x354 set,
// the difficulty hash (+0x84), the map name (+0x1c4) once, level info, dead / victory quotes, the intro
// screen cvars (Scr_BeginThread), a frame when in a thread, normal health 1 on g_gameskill "3",
// BroScr_InfinitePanzerfaust, every AI's damage var "_spawner_gmi::tanksquish", the friendly chain
// triggers (unused), +0x194 = 0, the level vars, BroScr_InitFriendlyChainNodes (cvar "chain" "1") and
// BroScr_ReattachAlliesGuns threads.
void XB_CDECL Bro__load_gmi__main();

// 0x00149200: a friendly chat trigger: the halftrack its target names (BroScr_FriendlyChatTriggers
// thread once, ends on its death); waits for the level's script_waittill notify; then for the
// halftrack (or an AI with the trigger's script_animname) with spawnflags 8, else for the player (then
// the allied AI with that animname nearest the player). With script_min_friendlies / 0xa7ad4a18, until
// the time limit (0x8177124f) the allies touching (BroScr_WaitForAliveTouchingCount) and the
// halftrack touching (0xa7ad4a18 / spawnflags 8; else again after 1 s). Deletes the trigger.
void XB_CDECL BroScr_FriendlyChatTriggerThink(uint32_t trigger);

// 0x00160d40: a flak's think (flakair / flakairlow / flaktank; flakpanzer for its model or type field
// 0xd1c52a4f), else nothing: a BroScr_FlakDeathThink thread; its targets (start triggers, bomb models,
// the crew spawners spawned); with the whole crew alive and at least 3 (flakpanzer: exactly 3, clips
// off) the flak model, the level flak arrays reset, the setup function (key 0xc511e266) and per guy the
// crew think (key 0x2f31323b), stop-moving / wait-death threads, the loader, loop sound and died
// threads; then while manned (level bytes +0x684 [flaknum]) the crew anims or, with the level flag
// array 0xe337f297 set, the guns put away. Frees its arrays and releases the type on the way out.
void XB_CDECL BroScr_FlakPanzerThink(uint32_t ent, uint32_t flaknum);

// 0x0014bac0 (FUN_0014bac0; proposed BroScr_FlakClaimTriggerThink): a trigger's target trigger gets a
// BroScr_WaitTriggerClearFlag thread (level bytes +0x548 [flaknum] set until touched); then while the
// flak is manned (level bytes +0x684) each toucher whose own flag array 0xe337f297 [flaknum] is
// undefined (or that has none) takes the flak: the level's and its own flag set, a
// BroScr_FlipLevelFlagOnDeath thread; 1 s between touches.
void XB_CDECL BroScr_FlakClaimTriggerThink(uint32_t trigger, uint32_t flaknum);

// 0x0014bdf0: ends on "fire_turret"; while manned, with the level flag clear a BroScr_FlakCrewAnims
// thread ("twitch" at 4 %, else "idle"; the empty type), then waits for "flak_anim_done<n>".
void XB_CDECL BroScr_FlakGunnerFireAnim(uint32_t ent, uint32_t flaknum);

// 0x0014c5a0: ends on `endon`; the squad's nodes (targetname `group`) and spawners (group field); with
// either missing an error print and return. Else keeps spawning (forced with int key 0x02d62a20) while
// the squad's AI are fewer than `max_alive` and `max_total`, each to the next node (handle key
// 0x05374cc6) with its think thread (`think` or BroScr_SquadMemberThink), `interval` (0.05 when
// undefined) apart, 0.05 s between checks. Never returns otherwise.
void XB_CDECL BroScr_SquadManagerSpawnSquad(uint32_t ent, ScriptString group, int32_t max_alive, int32_t max_total,
                                            ScriptString endon, float interval, uint32_t think);

// 0x0014ca20: a flying stuka's death model (the same on piano_lupo, else "stuka_flying_d1"); its
// undropped bombs deleted; after death: turrets deleted, "explo_metal_rand", an earthquake (0.25, 3 s,
// 1050), the death model when defined, notify 0x07cd8ff6.
void XB_CDECL BroScr_VehicleDeath_Stuka(uint32_t ent);

// 0x0014ce80: copies of the vehicle's arrays 0x9ecce58a and 0xb0996bdc; once the vehicle is gone
// (checked every 5 s) their valid entities are deleted. The first argument is not read.
void XB_CDECL BroScr_DeleteArraysOnVehicleRemoved(uint32_t self, uint32_t vehicle);

// 0x0014ea50: waits for "stundone" while stunned (int key 0x2f66b4d8 == 1); *out = whether the queue
// 0x4ced49f6 rotates to a valid living target and (no attack_range, or the player within it); out of
// range it waits 1 s first. Returns out.
bool* XB_CDECL BroScr_StunnedWait(bool* out, uint32_t ent);

// 0x0014f000: a stack piece: takes damage; unless it is a "stacker" (script_noteworthy) the entities
// its target names are flagged (0x400); after script_health (1) damage from the level turret's user or
// the world (anyone while that turret is gone) they are unflagged and a BroScr_SpawnWoodGibs thread runs
// at the last attacker (at the piece + 10 up when the attacker is at the origin). Level +0x314 = 0 when
// undefined.
void XB_CDECL BroScr_SetupStackers_2(uint32_t ent);

// 0x0014f560: a stack trigger (deleted at once without targets): its targets' targets flagged (0x400)
// unless the trigger is a "stacker" (then a gib flag per target); once touched: unflagged, a
// BroScr_SpawnWoodGibs thread per valid target at the toucher, the trigger deleted.
void XB_CDECL BroScr_SetupStackers(uint32_t ent);

// 0x0014fb70: a wall-hop trigger (ends on death; with at least two script origins): while the player
// touches it and faces the wall (within 45 degrees), the "Press Activate" hint once per level and on use
// BroScr_PlayerHopOverWall; 0.05 s steps.
void XB_CDECL BroScr_WallHopTrigger(uint32_t ent);

// 0x0014ffd0: the flakvierling's death model ("v_flakvierling" / "v_flakvierling_base_d1"); on death
// "artillery_generic", the model, turret target cleared, gunners released, an earthquake (0.25, 3 s,
// 1050) and radius damage (250, 1000 to 100).
void XB_CDECL BroScr_VehicleDeath_Flakvierling(uint32_t ent);

// 0x00150180: ends on the rider's death / 0xa0e65a8e; mounts it (Scr_CallThread BroScr_VehicleRiderMount),
// then loops its seat's flakpanzer fire anim at the tag (gunner / passenger1 / passenger2; other tags:
// return). Releases `tag` on return.
void XB_CDECL BroScr_VehicleRiderWait(uint32_t vehicle, uint32_t rider, ScriptString tag);

// 0x00150ca0 / 0x00150f50: at the path end: turrets deleted (BroScr_DeleteVehicleTurrets_2 / _3) unless
// script_noteworthy "noturrets", undropped bombs deleted, array 0x0030b1c2 emptied; dead with int key
// 0x4af51227: crash sound (BroScr_PlaneCrashSound / _2) and delete; alive: delete; else "loop" reattaches
// and restarts the path, otherwise the vehicle is killed.
void XB_CDECL BroScr_VehicleFollowPathToEnd(uint32_t ent);
void XB_CDECL BroScr_VehicleFollowPathToEnd_2(uint32_t ent);

// 0x00152310: ends on 0x9767cf68; after `guy`'s death the respawner's dead count (int key 0x846e35f4)
// goes up with a BroScr_UpdateAliveDeadCount thread and notify 0x67979261, or without a valid respawner
// `ent`'s engine count (0x00922b88 / 0x00922b8c) does.
void XB_CDECL BroScr_RespawnerCountDeath(uint32_t ent, uint32_t respawner, uint32_t guy);

// 0x00151cd0: ends on 0xcc07a20a; bool key 0xd57e151d set, lag_distance 0 when undefined; every 0.25 s
// while it and its followed path (0xd952b5cf) are valid: BroScr_FollowUpdateCurrentPos into int key
// 0x9aec9c1a with notify 0x14559f8c on a change.
void XB_CDECL BroScr_FollowLagThink(uint32_t ent);

// 0x00151e70: the follower's goal along the leader's path positions on each leader position change
// (ends on the follower's death / 0x49c98989, the leader's 0xb93f38bf / 0xcc07a20a); at the last entry
// the leader notifies and a BroScr_RemoveAllFollowers thread starts.
void XB_CDECL BroScr_FollowEntInit(uint32_t leader, uint32_t follower);

// 0x00153210: ends on "deadstop" (and "reached_end_node" unless int key 0xc904bb71 is set); waits while
// int key 0x147beb00 is defined and 0, stops for blockers in the level's vehicle list (kind 1 / 2) and
// restarts at speed mode 5; 0.2 s steps.
void XB_CDECL BroScr_VehicleDeadStop(uint32_t ent);

// 0x00153420: the hull gunner of a tank (BroScr_TankAttachMgTurret thread with `weapon` / `tag`, ends on
// death): axis tanks target the level's +0x6ec entity first; bursts at the target queue's first entry
// (spread from script_accuracy and distance), two per target, then the next one.
void XB_CDECL BroScr_TankHullGunnerThink(uint32_t ent, ScriptString weapon, ScriptString tag);

// 0x001542b0: a rider boards the vehicle's seat (`tag`): AI flags, to the vehicle's node (unless
// `skip_goal`) and the seat's entry tag, the flakvierling mount anim (unless "no_mount_anim"), seat
// slot taken (notify 0x9966dcdf), linked to the tag with its rider anim; BroScr_VehicleRiderUnlinkOnDeath
// and BroScr_VehicleRiderWait threads. Releases `tag`.
void XB_CDECL BroScr_VehicleMountRiders(uint32_t vehicle, uint32_t rider, uint32_t seat, ScriptString tag, bool skip_goal);

// 0x001566c0: the panzer IV ("panzerIV" anim tree, health 500 + RandomInt(1000)): a
// BroScr_VehicleHealthThink thread, BroScr_VehicleInit_PanzerIV called, BroScr_VehicleTurretFireLoop_4
// and BroScr_TankStunOnDamage(1) threads.
void XB_CDECL BroScr_PanzerIVSetup_3(uint32_t ent);

// 0x00156ac0: the level notifies "<targetname>_pause" / "_unpause" pause the leader (bool key
// 0x262b2a4b, notify 0xb93f38bf) and restart its followers' threads.
void XB_CDECL BroScr_PauseUnpauseThink(uint32_t ent);

// 0x00156e70: the respawner loop of spawner `ent` (ends on 0x0c34930d of it and of `respawner`): while
// its count lasts and respawning (bool key 0x788065eb) holds, delayed spawns that get `attribs`, a
// targetname, a BroScr_FollowNodeChain thread (`mode`) on a random target node and a
// BroScr_RespawnerCountDeath thread. Frees `attribs` and `mode` on return.
void XB_CDECL BroScr_RespawnerThink(uint32_t ent, uint32_t respawner, RespawnerAttributes attribs, uint32_t limit,
                                    ScriptString mode);

// 0x00157ae0: BroScr_TankHullGunnerThink(ent, a copy of `weapon`, "tag_gunhull") (does not return).
void XB_CDECL BroScr_TankHullGunnerAtGunhull(uint32_t ent, ScriptString weapon);

// 0x00158aa0: with a leader path: `follower` joins the leader's follower array once (its place in int
// key 0x185cac5b); the first follower starts the leader's pause / lag threads (then 0.05 s);
// BroScr_RemoveFollowerOnDeath and, unless paused, BroScr_FollowEntInit threads.
void XB_CDECL BroScr_AddFollowers(uint32_t leader, uint32_t follower);

// 0x00158420: the flak vehicle's crew respawn loop (ends on death / 0xca4175b9 / 0xbe7ade77): when a
// rider slot empties and the gunner is gone, a loader moves up or is respawned as the gunner.
void XB_CDECL BroScr_FlakGunnerRespawnLoop(uint32_t vehicle);

// 0x00158d40: the spawners whose groupname is `group` (after the level notify `start_notify`, when
// set) each get a BroScr_RespawnerThink thread (`attribs`, `limit`, `mode`) with a manager script_origin
// when `name` is set (else 0). Frees its arguments.
void XB_CDECL BroScr_SpawnerGroupManager(uint32_t unused, ScriptString group, ScriptString name, RespawnerAttributes attribs,
                                         uint32_t limit, ScriptString start_notify, ScriptString mode);
// 0x001592d0 / 0x00159400 / 0x00159530 / 0x001596a0: BroScr_SpawnerGroupManager with the default
// attributes (BroScr_SpawnerGroupParams_Init; _4: `attribs`), empty strings and the word at 0x00922ebc
// for what is not passed. Release their arguments.
void XB_CDECL BroScr_SpawnerGroupManagerCall(uint32_t ent, ScriptString group);
void XB_CDECL BroScr_SpawnerGroupManagerCall_2(uint32_t ent, ScriptString group, uint32_t limit);
void XB_CDECL BroScr_SpawnerGroupManagerCall_3(uint32_t ent, ScriptString group, ScriptString name, uint32_t limit,
                                               ScriptString notify, ScriptString mode);
void XB_CDECL BroScr_SpawnerGroupManagerCall_4(uint32_t ent, ScriptString group, RespawnerAttributes attribs);

// 0x001597a0: a BroScr_SpawnerGroupManager (on the level, default strings) per name in `names` that a
// spawner's groupname matches, each followed by `delay` or the level notify `notify`. Frees its arguments.
void XB_CDECL BroScr_RunSpawnerGroupsByGroupname(DynArray names, float delay, ScriptString notify, RespawnerAttributes attribs,
                                                 uint32_t limit);

// 0x00159cc0 (dead): BroScr_PlaneSpawnWingTurrets(plane, the undefined int, the undefined int, true);
// siblings BroScr_PlaneSpawnWingTurretsCall / _2 in bro_scr_vehicle.h.
void XB_CDECL BroScr_PlaneSpawnWingTurretsCall_3(uint32_t plane);
// 0x0015aa50: BroScr_AddFollowers(leader, follower).
void XB_CDECL BroScr_AddFollowersDefault(uint32_t leader, uint32_t follower);
// 0x00159e40: the flak's automated think (ends on death / 0xca4175b9 / 0xbe7ade77; started by
// BroScr_SetFlakManned / SetFlakAutomated): a BroScr_FlakGunnerRespawnLoop thread; every 0.2 s, without
// a gunner (rider slot 0) or `automated`, the turret target cleared; else with targets_groupname a new
// target (BroScr_FlakFindTargets) when the target is dead / missing or beyond flak_dist, otherwise the
// level's target ("convoy3": 64 higher); the lead aimed at; in reach and range, notify 0xac1c7bac
// (fire) when the "tag_flash" trace runs past the target or with can_shoot_low. Not registered: waits.
void XB_CDECL BroScr_FlakAutomatedThink(uint32_t flak);
// 0x0015aa80: notify 0xca4175b9; manned: after a frame a BroScr_FlakAutomatedThink thread, else
// BroScr_ReleaseGunners.
void XB_CDECL BroScr_SetFlakManned(uint32_t flak, bool manned);
// 0x0015aae0: bool field `automated` set: notify 0xca4175b9, after a frame a BroScr_FlakAutomatedThink
// thread; cleared: notify 0xbe7ade77.
void XB_CDECL BroScr_SetFlakAutomated(uint32_t flak, bool automated);
// 0x0015abe0 / 0x0015ac70: the same on the level vehicle named `name` when found. Release `name`.
void XB_CDECL BroScr_SetFlakMannedByName(ScriptString name, bool manned);
void XB_CDECL BroScr_SetFlakAutomatedByName(ScriptString name, bool automated);

}  // namespace game
