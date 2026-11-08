#pragma once

// Compiled level-script helpers for vehicles (levelscript module): health set-ups (by difficulty,
// random, fixed) that start the vehicle health thread, wing turrets, vehicle/script-model spawns
// that copy an entity's place and name. Names are ours (from the bodies).

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "script_string.h"

namespace game {

// 0x0014ee50 / 0x00150560 / 0x00150720 / 0x001523e0: health by GScr_GetDifficulty ("easy" 6500,
// "medium" 5500, "hard" 5000, "fu" 3500, "gimp" 7500, else 5000), then the vehicle health thread
// (0x0014d0a0) on `ent` (Scr_CreateThread). 0x00152800: the same with 19000, 17000, 13000, 11000,
// 22000, else 17000.
void XB_CDECL BroScr_SetHealthByDifficulty(uint32_t ent);
void XB_CDECL BroScr_SetHealthByDifficulty_2(uint32_t ent);
void XB_CDECL BroScr_SetHealthByDifficulty_3(uint32_t ent);
void XB_CDECL BroScr_SetHealthByDifficulty_4(uint32_t ent);
void XB_CDECL BroScr_SetHealthByDifficulty_5(uint32_t ent);
// 0x0014efc0 / 0x00150890 / 0x00151230: health 500 + Scr_RandomInt(1000), then the vehicle health
// thread (Scr_StartThreadFunctor1). Returns the thread.
uint32_t XB_CDECL BroScr_VehicleRandomHealthInit(uint32_t ent);
uint32_t XB_CDECL BroScr_VehicleRandomHealthInit_2(uint32_t ent);
uint32_t XB_CDECL BroScr_VehicleRandomHealthInit_3(uint32_t ent);
// 0x001506d0 / 0x00152550 / 0x00152970: the same, the health set only when it is undefined.
uint32_t XB_CDECL BroScr_TankDefaultHealthThink(uint32_t ent);
uint32_t XB_CDECL BroScr_TankDefaultHealthThink_2(uint32_t ent);
uint32_t XB_CDECL BroScr_TankInitHealth(uint32_t ent);
// 0x00151200 (dead): health 2500, then the vehicle health thread.
uint32_t XB_CDECL BroScr_VehicleHealth2500Init(uint32_t ent);
// 0x00142ff0 / 0x00143430 (dead): health 300, then the damage watch thread 0x0013a830 / 0x0013aaa0.
uint32_t XB_CDECL BroScr_InitWingTurret(uint32_t ent);
uint32_t XB_CDECL BroScr_InitWingTurret_2(uint32_t ent);
// 0x00145c80 (dead): health `health` (tail jump); when undefined, 300 and the damage watch thread
// 0x0013eb00.
uint32_t XB_CDECL BroScr_InitWingTurret_3(uint32_t ent, uint32_t health);
// 0x00135130: starts the turret fire loop thread 0x00128f90 on `ent`.
uint32_t XB_CDECL BroScr_StartVehicleTurretFireLoop_8(uint32_t ent);
// 0x001522a0 (dead): starts the threads 0x0014a910 and 0x0014a9c0 on `ent`.
uint32_t XB_CDECL BroScr_StartPathPosThreads(uint32_t ent);
// 0x00154020: the jeep anim tree (0x007f4948), BroScr_SetHealthByDifficulty, then the threads
// 0x0013f570 (death) and 0x00128f90 (turret fire loop) on `ent` (Scr_CreateThread).
void XB_CDECL BroScr_VehicleSetHealth_Jeep(uint32_t ent);
// Not registered (thread_switch): the spawn calls run the new entity's script, which switches script
// threads and keeps a block of the caller's stack in host memory (compare: mismatches only there).
// 0x00133eb0: *out = GScr_SpawnVehicle(model (string field 0x05d2529a), name, ent's vehicletype,
// ent's origin, ent's angles, extra); the new vehicle's vehicletype = ent's. Releases `name`.
uint32_t* XB_CDECL BroScr_SpawnVehicleFromEnt(uint32_t* out, uint32_t ent, ScriptString name, uint32_t extra);
// 0x00134010: *out = a "script_model" spawned at ent's origin (GScr_Spawn), named `name`, with ent's
// model (string field 0x05d2529a; GScr_SetModel). Releases `name`.
uint32_t* XB_CDECL BroScr_SpawnScriptModelAt(uint32_t* out, uint32_t ent, ScriptString name, uint32_t extra);
// 0x00133fc0 / 0x00134100: *out = BroScr_SpawnVehicleFromEnt / BroScr_SpawnScriptModelAt with ent's
// targetname, then deletes `ent`.
uint32_t* XB_CDECL BroScr_ReplaceWithVehicle(uint32_t* out, uint32_t ent, uint32_t extra);
uint32_t* XB_CDECL BroScr_ReplaceWithScriptModel(uint32_t* out, uint32_t ent, uint32_t extra);
// 0x00134be0 (dead): field 0x7ab1fdf4 of `ent` = 1; returns Scr_EntityHandleIsValid(other).
uint32_t XB_CDECL BroScr_SetField7ab1fdf4_IsValid(uint32_t ent, uint32_t unused1, uint32_t unused2, uint32_t other);
// 0x001522e0 (not registered, thread_switch: BroScr_StukaDropBombs spawns, see above):
// BroScr_StukaDropBombs(a, b, a null string).
void XB_CDECL BroScr_StukaDropBombsThread(uint32_t a, uint32_t b);
// 0x00136450 / 0x00136580: the flakvierling (0x007f49c4) / flakvierling base (0x007f49b0) anim tree,
// then the anim handle fields 0x0038b194, 0xe0b632e1, 0xe0b632e2 (added as 0 when missing) = the
// words at 0x008cd1b8/c4/d0 / 0x008cd4e8/f4, 0x008cd500.
void XB_CDECL BroScr_FlakvierlingAnimTree(uint32_t ent);
void XB_CDECL BroScr_FlakvierlingBaseAnimTree(uint32_t ent);
// 0x00145cd0 (dead): BroScr_TankAttachMgTurret(ent, a copy of `weapon`, "tag_gunhull"). Releases `weapon`.
void XB_CDECL BroScr_TankAttachHullMg(uint32_t ent, ScriptString tag);
// 0x00145000: starts the thread 0x0013d650 on `ent` with five words and a string (the KASSPASS
// section's starter 0x0052cc30, handed pointers to the arguments). Releases `a7`.
void XB_CDECL BroScr_RocketExplosionThread(uint32_t ent, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6,
                                           ScriptString a7);
// Not registered (thread_switch): a started thread runs at once and leaves the saved stack pointer
// in host memory (0x00153ed0 mismatched only there; 0x00145d60 starts the same threads).
// 0x00145d60 / 0x00153ed0: the jeep anim tree, health 500 + Scr_RandomInt(1000), (0x00153ed0 only: the
// vehicle health thread), the threads 0x0013f6f0 and 0x00128f30, and 0x00134a90 with a functor
// {0x007f4a1c, ent, 1}.
void XB_CDECL BroScr_WillyJeepSetup(uint32_t ent);
void XB_CDECL BroScr_WillyJeepSetup_2(uint32_t ent);
// 0x00147c00: *out = how many of the first three gunners (array field 0x2cd69a2a, 8-byte elements)
// are valid and alive; returns out.
uint32_t* XB_CDECL BroScr_CountAliveGunners(uint32_t* out, uint32_t ent);
// 0x00134ec0: the turret (handle field 0x2f68c7a5) gets mode 0x78da62f2 and the entity's script_team,
// "allies" when that is not set.
void XB_CDECL BroScr_TurretSetAlliesTeam(uint32_t ent);

// 0x001346f0 (dead): a "script_origin" at each back wheel tag (right, left; GScr_GetTagOrigin), turned
// to `ent`'s angles with pitch + 32, linked to `ent`, each tracked by a BroScr_TrackWheelOrigin
// thread (Scr_CreateThread: left first).
void XB_CDECL BroScr_SpawnWheelOrigins_2(uint32_t ent);

// Not registered (thread_switch, docs/research/levelscript_classes.csv): GScr_SpawnTurret runs the new entity's
// script and Scr_CreateThread starts a thread; both switch script threads and keep a block of the
// caller's stack in host memory. Kept (reviewed against the disassembly, not compared) until script
// threads are coroutines.
// 0x0013f190: unless `weapon` is "no_turret": a "misc_turret" with `weapon` ("mg42_panzerIV_tank" when
// empty) as the turret handle field 0x2f68c7a5 of `ent`; its field 0x629afc27 (when defined) goes to
// GScr_Turret_Set54 as a float; model "v_gunturret", linked to `ent`'s `tag` (zero offsets), `ent`'s
// angles, a BroScr_MgBurstFire thread, unusable; then BroScr_TurretSetAlliesTeam(ent). Releases both
// strings.
void XB_CDECL BroScr_TankAttachMgTurret(uint32_t ent, ScriptString weapon, ScriptString tag);

// Not registered (thread_switch, docs/research/levelscript_classes.csv): GScr_SpawnTurret runs the new
// entities' scripts and Scr_CreateThread / Scr_BeginThreadEndOn start threads. Kept (reviewed against
// the disassembly, not compared) until script threads are coroutines.
// 0x001529c0: health `health` (undefined: 300 and a BroScr_WingTurretDamageWatch_3 thread), a
// BroScr_VehicleDeath_Stuka thread, with `follow_path` BroScr_VehicleFollowPath (Scr_BeginThreadEndOn,
// end notify 0x485de552), field 0x4af51227 = 0, with `bombs` defined a BroScr_StukaDropBombsThread
// (functor {0x007f4a1c, thread, ent, bombs}). Unless script_noteworthy is "noturrets": two "misc_turret"s
// ("stuka_wing_turret" left, "me109_wing_turret_nosound" right; leftturret / rightturret fields), mode
// 0xdc26d84c, model "v_p47_guns" in the level zone (GScr_FindZone of the name at level +0x4b0, if set),
// placed at tag_gunLeft / tag_gunRight with `ent`'s angles and linked there.
void XB_CDECL BroScr_PlaneSpawnWingTurrets(uint32_t ent, uint32_t bombs, uint32_t health, bool follow_path);

// Not registered (thread_switch, docs/research/levelscript_classes.csv): GScr_Spawn runs each bomb's script at
// once (as BroScr_SpawnScriptModelAt). Kept (reviewed against the disassembly, not compared) until
// script threads are coroutines.
// 0x0014aaa0: field 0x380a75c4 = `count`, the bomb array 0x0030b1c2 emptied; then while the index is
// below that field: a "script_model" bomb at `ent`'s origin appended to the array, model `model`
// ("stuka_bomb" when empty; in the level zone when the zone name at level +0x358 is set),
// bomb field 0xe2c9ccdd = 0. On a "stuka" / "stuka_flying" / "stuka_prologue" model the bomb takes the
// origin and angles of tag_bombleft / tag_bombright / tag_bombmid in turn (starting left, wrapping
// after mid), else `ent`'s origin and angles; then it is linked to `ent`. Releases `model`.
void XB_CDECL BroScr_StukaDropBombs(uint32_t ent, uint32_t count, ScriptString model);

// Not registered (thread_switch, docs/research/levelscript_classes.csv): GScr_Spawn of the seven
// "script_origin"s runs their scripts at once and Scr_CreateThread starts
// BroScr_DeleteArraysOnVehicleRemoved. Kept (reviewed against the disassembly, not compared) until
// script threads are coroutines.
// 0x0014d0a0 (started by the vehicle health setups): field defaults (0x515816fa -0.927, 0xa22fa6ba 400,
// 0xbe149b8c 22, 0x8166d37c 240, 0x35ee66af 1 = scale s), `ent` appended to the level list at +0x4e0;
// seven "script_origin"s placed around it from its right (R = right * 64s) and forward (A = forward *
// 96s, C = forward * 84s) vectors: p0 = origin + R + A, p1 = origin - R + A, p2 = p0 - 2A, p3 = p1 - 2A,
// p4 = origin + A, p5 = origin + C, p6 = origin - C; p0-p4 go to array 0x9ecce58a, p5-p6 to 0xb0996bdc,
// p4 to handle field 0xcc043125; all are linked to `ent`, and BroScr_DeleteArraysOnVehicleRemoved runs
// on the level entity.
void XB_CDECL BroScr_VehicleHealthThink(uint32_t ent);

}  // namespace game
