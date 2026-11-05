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
// 0x00139f30: ends on the rider's death; after the vehicle's 0xecd7b044 (unload): weapon back in
// hand, the rider's delay, the unload anim (key 0x2d7db3f6) at the vehicle's tag (string key
// 0xb6abdc5e); then unlinked with all stances, notifies 0xa663db67 and restores its animname (key
// 0xf203df5e) when set. Stops when the rider dies.
void XB_CDECL BroScr_VehicleRiderUnload(uint32_t vehicle, uint32_t rider);
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

}  // namespace game
