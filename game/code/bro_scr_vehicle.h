#pragma once

// Compiled level-script vehicle helpers: the default-argument call wrappers of the vehicle
// scripts and the tank/panzer start threads.

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "script_string.h"

namespace game {

// 0x001555e0: BroScr_HalftrackAttachPassengers(vehicle, seat group) for groups 1-5.
void XB_CDECL BroScr_HalftrackAttachAllPassengers(uint32_t vehicle);

// 0x00157ab0 / 0x00159cf0: BroScr_PlaneSpawnWingTurrets(plane, a2, a3 / the undefined int, true).
void XB_CDECL BroScr_PlaneSpawnWingTurretsCall(uint32_t plane, uint32_t a2, uint32_t a3);
void XB_CDECL BroScr_PlaneSpawnWingTurretsCall_2(uint32_t plane, uint32_t a2);

// 0x00159d20: BroScr_FindFlakvierlings(undefined, undefined) with the word at 0x00922ebc. The
// copy with an argument right after it (0x00159d30) is a separate function Ghidra lacks.
void XB_CDECL BroScr_FindAllFlakvierlings();

// 0x00159de0 / 0x00159e10: BroScr_VehicleSpawnRider(vehicle, a2, 0x00922ebc's word / a3, false).
void XB_CDECL BroScr_VehicleSpawnRiderCall(uint32_t vehicle, uint32_t a2);
void XB_CDECL BroScr_VehicleSpawnRiderCall_2(uint32_t vehicle, uint32_t a2, uint32_t a3);

// 0x00168350 / 0x00168470: BroScr_TankInit_5 / BroScr_TankInit_3 (tank, a copy of name,
// "tag_gunhull"); name is released.
void XB_CDECL BroScr_TankGunHull_5(uint32_t tank, ScriptString name);
void XB_CDECL BroScr_TankGunHull_3(uint32_t tank, ScriptString name);

// 0x00167660 / 0x001683e0 / 0x00168500: GScr_UseAnimTree(tank, "panzerIV"), then a thread
// (Scr_StartThread_ent_str with the string global 0x00922e90) on BroScr_TankInit_2 /
// BroScr_TankGunHull_5 / BroScr_TankGunHull_3.
void XB_CDECL BroScr_PanzerIVTankInitStart(uint32_t tank);
void XB_CDECL BroScr_PanzerIVGunHullStart(uint32_t tank);
void XB_CDECL BroScr_PanzerIVGunHullStart_2(uint32_t tank);

// 0x001552d0 / 0x00155500 / 0x001578f0 / 0x001579d0: GScr_UseAnimTree(vehicle, "panzerIV" /
// "halftrack"), a health-by-difficulty function, then threads on a death and a turret function
// (see the .cpp for which).
void XB_CDECL BroScr_VehicleSetHealth_PanzerIV(uint32_t vehicle);
void XB_CDECL BroScr_VehicleSetHealth_Halftrack(uint32_t vehicle);
void XB_CDECL BroScr_VehicleSetHealth_PanzerIV_2(uint32_t vehicle);
void XB_CDECL BroScr_TunisiaPanzerIVInit(uint32_t vehicle);

// 0x0017ac70 (thiscall): the tank spawner's slot block (BroScr_TankSpawnerThink's local): word 0
// = 0, then four 0x14-byte entries {0, undefined int x3, undefined float}. Returns self.
uint32_t* XB_THISCALL BroScr_TankSpawnerSlots_Init(uint32_t* self);

}  // namespace game
