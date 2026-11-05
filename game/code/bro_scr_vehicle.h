#pragma once

// Compiled level-script vehicle helpers: the default-argument call wrappers of the vehicle
// scripts and the tank/panzer start threads.

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "scr_access.h"
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

// 0x00156970: BroScr_PanzerIVSetDifficultyHealth(ent), then threads (Scr_StartThreadFunctor1) on
// BroScr_VehicleDeath_PanzerIV and BroScr_VehicleTurretFireLoop.
void XB_CDECL BroScr_PanzerIVStartThreads(uint32_t ent);
// 0x001569c0: handle field 0xd952b5cf = `target`, field 0x5cd3d8b8 = `count`, then
// FollowEnt_Worker(ent, a1).
void XB_CDECL BroScr_StartFollowEnt(uint32_t ent, uint32_t a1, uint32_t target, uint32_t count);
// 0x00167340: ScrEnt_SetHealth(ent, health; 600 when undefined), then threads on
// BroScr_VehicleDeath_Sdkfz222, BroScr_SpawnWheelOrigins and BroScr_SmokeWhenHalfHealth.
void XB_CDECL BroScr_Sdkfz222Init(uint32_t ent, uint32_t health);
// 0x0015d170: compares `fx` with "day" when set (result unused), then sets the level's wheel
// smoke map (0x00922ee0 +0x57c) entries 0xe2162fb5, 0x438c482f, 0xe21d8c25, 0xe216961a,
// 0xe2161386, 0x8f10d935 to "generic_wheelsmoke". `vehicle` is unused. Releases `fx`.
void XB_CDECL BroScr_SetWheelSmokeFx(uint32_t vehicle, ScriptString fx);
// 0x00154190: field 0xb330b301 = 0, health = `health` (10000000 when undefined), field
// 0x93c3cf6c = `a2` (0x2000 when undefined), then threads on BroScr_VehicleDeath_Flakvierling and
// BroScr_FlakvierlingThink.
void XB_CDECL BroScr_FlakvierlingInit(uint32_t ent, uint32_t health, uint32_t a2);
// 0x001553b0 (dead): GScr_UseAnimTree(vehicle, "halftrack"), health 500 + RandomInt(1000), threads
// on BroScr_VehicleHealthThink, BroScr_VehicleInit_Halftrack, BroScr_VehicleTurretFireLoop_2 and
// BroScr_TankStunOnDamage (with 1).
void XB_CDECL BroScr_HalftrackSetup(uint32_t vehicle);
// 0x001577d0: GScr_UseAnimTree(vehicle, "panzerIV"), BroScr_TankDefaultHealthThink_2(vehicle),
// threads on BroScr_VehicleInit_Sherman, BroScr_VehicleTurretFireLoop_5 and
// BroScr_TankStunOnDamage (with 1).
void XB_CDECL BroScr_PanzerIVSetup_4(uint32_t vehicle);

// 0x001567f0: team "axis" (BroScr_NormalizeTeamName), GScr_UseAnimTree(vehicle,
// "panzerIV"), health 500 + RandomInt(1000), threads on BroScr_VehicleHealthThink,
// BroScr_VehicleInit_PanzerIV, BroScr_VehicleTurretFireLoop_4 and BroScr_TankStunOnDamage (1).
void XB_CDECL BroScr_PanzerIVSetup_2(uint32_t vehicle);
// 0x0015d2c0: *out = the level effect map's (+0x57c) tread effect for the wheel's surface
// (grass/sand/dirt/rock/snow keys), else "nofx" ("ice" notifies 0x9b4136a0 on the vehicle first);
// "nofx" also when the map's value is empty. Returns out.
ScriptString* XB_CDECL BroScr_TreadFxSurfaceThink(ScriptString* out, uint32_t vehicle, uint32_t wheel);
// 0x0016c7c0: seats `rider` in `vehicle`: keeps the rider's animname in field 0x55576ed6 when set,
// animname = "passenger<index>", field 0xd91e5c5b = "tag_passenger<index>", rider appended to the
// vehicle's arrays 0xe2a39a33 and 0xe104fd5c around GScr_LinkToTagWithOffset(rider, vehicle, tag,
// zero offsets), then a BroScr_VehicleRidersThink thread (vehicle, rider, tag). Releases `tag`.
void XB_CDECL BroScr_VehicleGetPassengers(uint32_t vehicle, uint32_t rider, ScriptString tag, uint32_t index);

// 0x001580a0: seats a valid, living rider on the vehicle: the asked seat when its rider slot
// (array field 0x2cd69a2a, 8-byte {handle, flag}) is empty or its gunner dead, else the first seat
// 0-2 without a live gunner; for the undefined int the first slot 0-2 without a live rider; none
// free: nothing. The slot = {rider, false} (targetname "gunner"), notify 0x387a52e5 on the
// vehicle, tag "tag_gunner"/"tag_passenger1"/"tag_passenger2" by seat; unless skip_mount, the
// BroScr_VehicleMountRiders thread (vehicle, &slot, &seat, &tag, &skip_mount).
void XB_CDECL BroScr_VehicleSpawnRider(uint32_t vehicle, uint32_t rider, uint32_t seat, bool skip_mount);
// 0x0015ab50: BroScr_VehicleSpawnRider(the level vehicle named `name`, rider, seat, false) when
// one is found (BroScr_FindLevelVehicleByTargetname on a copy). Releases `name`.
void XB_CDECL BroScr_VehicleSpawnRiderByName(ScriptString name, uint32_t rider, uint32_t seat);

// 0x001655f0: field 0x8473dd9d = 0x5c and float field 0xdf09fd77 = 1000 when undefined; a
// script_origin at tag_wheel_back_right and one at tag_wheel_back_left (GScr_Spawn), each given
// the vehicle's angles + 32 on the first component and linked to it; a BroScr_TreadFx thread for
// each (hash 0x79193376 right, 0x13c7f9a4 left).
void XB_CDECL BroScr_SpawnWheelOrigins(uint32_t vehicle);

// 0x0016dae0: seats `rider` on the vehicle by role ("driver" / "gunner" / "passenger" + index;
// any other: nothing): script models get "drone_<role>" and the level's anim tree for it; other
// riders (not the level's player, +0x7f8) keep their animname in oldanimname and get the role;
// the rider's tag field 0xd91e5c5b = "tag_driver" / "tag_gunner" / "tag_passenger" + index. Then
// rider arrays 0xe2a39a33 and 0xe104fd5c, GScr_LinkToTagWithOffset(offset, angles: zeroed when
// all undefined, in the argument slots) and the BroScr_VehicleRidersThink thread. Releases role.
void XB_CDECL BroScr_DroneVehicleRiders(uint32_t vehicle, uint32_t rider, ScriptString role, uint32_t index, ScrVec3 offset,
                                        ScrVec3 angles);

// 0x00157b40: every script_vehicle with the model v_flakvierling or v_flakvierling_base that is
// not in the level's flak list (+0x784) yet: added, BroScr_FlakvierlingInit(it, health, a2) and
// the base / gun anim tree.
void XB_CDECL BroScr_FindFlakvierlings(uint32_t health, uint32_t a2);

// 0x00157e60: a v_flakvierling vehicle (GScr_SpawnVehicle, type "FlakVierling", targetname
// "flakvierling" when null, origin (0, 0, 0) when undefined, zero angles), added to the level's
// flak list, BroScr_FlakvierlingInit(it, health, a2) and its anim tree; field 0x6dcb7c56 = node
// (unless 0 / -1), 0x05903940 = GScr_GetTime(). *out = it; releases targetname; returns out.
uint32_t* XB_CDECL BroScr_SpawnFlakvierling(uint32_t* out, ScriptString targetname, uint32_t spawnflags, ScrVec3 origin,
                                            uint32_t node, uint32_t health, uint32_t a2);

}  // namespace game
