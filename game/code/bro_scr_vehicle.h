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
// 0x001522e0: BroScr_StukaDropBombs(a, b, a null string) (cdecl a, b; the entry hands over its ESP and
// EBX/ESI/EDI).
extern "C" PLATFORM_HIDDEN void XB_CDECL BroScr_StukaDropBombsThread(uint32_t entry_esp, uint32_t ebx, uint32_t esi, uint32_t edi);
extern "C" void BroScr_StukaDropBombsThread_entry(void);

}  // namespace game
