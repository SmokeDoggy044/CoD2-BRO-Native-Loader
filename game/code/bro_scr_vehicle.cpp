#include "bro_scr_vehicle.h"

#include "bro_scr_common.h"
#include "title_data.h"

namespace game {

namespace {

// The bool arguments are pushed as a word whose low byte the original sets (the rest is stale
// ECX); the callees read the byte.
void PlaneSpawnWingTurrets(uint32_t plane, uint32_t a2, uint32_t a3, bool flag) {
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t, bool)>(0x001529c0)(plane, a2, a3, flag);
}

void VehicleSpawnRider(uint32_t vehicle, uint32_t a2, uint32_t a3, bool flag) {
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t, bool)>(0x001580a0)(vehicle, a2, a3, flag);
}

// The callee takes both strings by value and releases them; tag is made first, then the copy.
void TankGunHull(uint32_t function, uint32_t tank, ScriptString* name) {
    ScriptString tag;
    scr_string_from(&tag, 0x007f77f4);  // "tag_gunhull"
    ScriptString copy;
    title_function<void XB_THISCALL(ScriptString*, const ScriptString*)>(0x0011d5e0)(&copy, name);  // ScriptString_Copy
    title_function<void XB_CDECL(uint32_t, ScriptString, ScriptString)>(function)(tank, copy, tag);
    scr_release(name);
}

void PanzerIVStart(uint32_t tank, uint32_t thread) {
    ScriptString tree;
    scr_string_from(&tree, 0x007f499c);  // "panzerIV"
    scr_slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009225f0)(tank, &tree);  // GScr_UseAnimTree
    scr_release(&tree);
    using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    title_function<StartThread>(0x0045a7c0)(0, 0, 0, 0, thread, tank, 0x00922e90);  // Scr_StartThread_ent_str
}

// Script thread on the vehicle: a functor {0x007f5ba0, function, vehicle} from ScrIface_Alloc
// (null when it fails), then Scr_CreateThread(0, 0, 0, vehicle, functor, 0).
void StartVehicleThread(uint32_t vehicle, uint32_t function) {
    uint32_t* functor = static_cast<uint32_t*>(scr_slot<void* XB_CDECL(uint32_t)>(0x00922928)(0xc));
    if (functor) {
        functor[0] = 0x007f5ba0;
        functor[1] = function;
        functor[2] = vehicle;
    }
    using CreateThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);
    scr_slot<CreateThread>(0x0092216c)(0, 0, 0, vehicle, functor, 0);
}

// GScr_UseAnimTree(vehicle, tree), the health-by-difficulty function, then the death and turret
// threads.
void VehicleSetHealth(uint32_t vehicle, uint32_t tree, uint32_t set_health, uint32_t death, uint32_t turret) {
    ScriptString name;
    scr_string_from(&name, tree);
    scr_slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009225f0)(vehicle, &name);  // GScr_UseAnimTree
    scr_release(&name);
    title_function<void XB_CDECL(uint32_t)>(set_health)(vehicle);
    StartVehicleThread(vehicle, death);
    StartVehicleThread(vehicle, turret);
}

}  // namespace

void XB_CDECL BroScr_HalftrackAttachAllPassengers(uint32_t vehicle) {
    for (uint32_t group = 1; group <= 5; ++group)
        title_function<void XB_CDECL(uint32_t, uint32_t)>(0x001508d0)(vehicle, group);  // BroScr_HalftrackAttachPassengers
}

void XB_CDECL BroScr_PlaneSpawnWingTurretsCall(uint32_t plane, uint32_t a2, uint32_t a3) {
    PlaneSpawnWingTurrets(plane, a2, a3, true);
}

void XB_CDECL BroScr_PlaneSpawnWingTurretsCall_2(uint32_t plane, uint32_t a2) {
    PlaneSpawnWingTurrets(plane, a2, title_global<uint32_t>(0x00922ec0), true);  // g_scrUndefinedInt
}

void XB_CDECL BroScr_FindAllFlakvierlings() {
    const uint32_t undefined = title_global<uint32_t>(0x00922ebc);
    title_function<void XB_CDECL(uint32_t, uint32_t)>(0x00157b40)(undefined, undefined);  // BroScr_FindFlakvierlings
}

void XB_CDECL BroScr_VehicleSpawnRiderCall(uint32_t vehicle, uint32_t a2) {
    VehicleSpawnRider(vehicle, a2, title_global<uint32_t>(0x00922ebc), false);
}

void XB_CDECL BroScr_VehicleSpawnRiderCall_2(uint32_t vehicle, uint32_t a2, uint32_t a3) {
    VehicleSpawnRider(vehicle, a2, a3, false);
}

void XB_CDECL BroScr_TankGunHull_5(uint32_t tank, ScriptString name) {
    TankGunHull(0x00166620, tank, &name);  // BroScr_TankInit_5
}

void XB_CDECL BroScr_TankGunHull_3(uint32_t tank, ScriptString name) {
    TankGunHull(0x001676f0, tank, &name);  // BroScr_TankInit_3
}

void XB_CDECL BroScr_PanzerIVTankInitStart(uint32_t tank) {
    PanzerIVStart(tank, 0x001673c0);  // BroScr_TankInit_2
}

void XB_CDECL BroScr_PanzerIVGunHullStart(uint32_t tank) {
    PanzerIVStart(tank, 0x00168350);  // BroScr_TankGunHull_5
}

void XB_CDECL BroScr_PanzerIVGunHullStart_2(uint32_t tank) {
    PanzerIVStart(tank, 0x00168470);  // BroScr_TankGunHull_3
}

void XB_CDECL BroScr_VehicleSetHealth_PanzerIV(uint32_t vehicle) {
    // "panzerIV", BroScr_SetHealthByDifficulty_2, BroScr_VehicleDeath_M13, BroScr_TankTurretFireLoopRecoil_2
    VehicleSetHealth(vehicle, 0x007f499c, 0x00150560, 0x00139da0, 0x00127db0);
}

void XB_CDECL BroScr_VehicleSetHealth_Halftrack(uint32_t vehicle) {
    // "halftrack", BroScr_SetHealthByDifficulty_3, BroScr_VehicleDeath_Halftrack, BroScr_VehicleTurretFireLoop_3
    VehicleSetHealth(vehicle, 0x00803cc4, 0x00150720, 0x0013a0f0, 0x00127e90);
}

void XB_CDECL BroScr_VehicleSetHealth_PanzerIV_2(uint32_t vehicle) {
    // "panzerIV", BroScr_SetHealthByDifficulty_4, BroScr_VehicleDeath_Tank, BroScr_VehicleTurretFireLoop_6
    VehicleSetHealth(vehicle, 0x007f499c, 0x001523e0, 0x0013d9a0, 0x00128780);
}

void XB_CDECL BroScr_TunisiaPanzerIVInit(uint32_t vehicle) {
    // "panzerIV", BroScr_SetHealthByDifficulty_5, BroScr_VehicleDeath_Stuart, BroScr_TankTurretFireLoopRecoil_4
    VehicleSetHealth(vehicle, 0x007f499c, 0x00152800, 0x0013e770, 0x00128860);
}

uint32_t* XB_THISCALL BroScr_TankSpawnerSlots_Init(uint32_t* self) {
    self[0] = 0;
    for (int slot = 0; slot < 4; ++slot) {
        uint32_t* entry = self + 1 + slot * 5;
        entry[0] = 0;
        entry[1] = title_global<uint32_t>(0x00922ec0);  // g_scrUndefinedInt, re-read for each word
        entry[2] = title_global<uint32_t>(0x00922ec0);
        entry[3] = title_global<uint32_t>(0x00922ec0);
        entry[4] = title_global<uint32_t>(0x00922eac);  // g_scrUndefinedFloat (movss: a word copy)
    }
    return self;
}

}  // namespace game
