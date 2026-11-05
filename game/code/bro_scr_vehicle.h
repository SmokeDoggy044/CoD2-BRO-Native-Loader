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
// 0x00145cd0 (dead): BroScr_TankAttachMgTurret(ent, tag, 0x007f77f4). Releases `tag`.
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

}  // namespace game
