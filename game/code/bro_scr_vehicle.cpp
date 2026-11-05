#include "bro_scr_vehicle.h"

#include "bro_scr_common.h"
#include "bro_scr_util.h"
#include "g_script.h"
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

namespace {

// Scr_StartThreadFunctor1 (0x0011a910): four zero words, the function, the entity.
void start_thread1(uint32_t function, uint32_t ent) {
    using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    title_function<StartThread>(0x0011a910)(0, 0, 0, 0, function, ent);
}

}  // namespace

void XB_CDECL BroScr_PanzerIVStartThreads(uint32_t ent) {
    BroScr_PanzerIVSetDifficultyHealth(ent);
    start_thread1(0x0013b200, ent);  // BroScr_VehicleDeath_PanzerIV
    start_thread1(0x00121a00, ent);  // BroScr_VehicleTurretFireLoop
}

void XB_CDECL BroScr_StartFollowEnt(uint32_t ent, uint32_t a1, uint32_t target, uint32_t count) {
    *BroScr_FieldHandleSlot(ent, 0xd952b5cf) = target;
    *BroScr_FieldSlot(ent, 0x5cd3d8b8) = count;
    title_function<void XB_CDECL(uint32_t, uint32_t)>(0x00151370)(ent, a1);  // FollowEnt_Worker
}

void XB_CDECL BroScr_Sdkfz222Init(uint32_t ent, uint32_t health) {
    scr_slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922b94)(
        ent, health == title_global<uint32_t>(0x00922ec0) ? 600 : health);  // ScrEnt_SetHealth
    start_thread1(0x0013d2e0, ent);  // BroScr_VehicleDeath_Sdkfz222
    start_thread1(0x001655f0, ent);  // BroScr_SpawnWheelOrigins
    start_thread1(0x0013d270, ent);  // BroScr_SmokeWhenHalfHealth
}

void XB_CDECL BroScr_SetWheelSmokeFx(uint32_t vehicle, ScriptString fx) {
    (void)vehicle;
    if (fx.rep) title_function<uint32_t XB_CDECL(const ScriptString*, uint32_t)>(0x0011d7c0)(&fx, 0x007ff364);  // ScriptString_Equals(fx, "day")
    static const uint32_t kKeys[6] = {0xe2162fb5, 0x438c482f, 0xe21d8c25, 0xe216961a, 0xe2161386, 0x8f10d935};
    for (uint32_t k : kKeys) {
        uint32_t key = k;
        using Index = uint32_t XB_THISCALL(uint32_t, const uint32_t*);
        const uint32_t value = title_function<Index>(0x00461120)(title_global<uint32_t>(0x00922ee0) + 0x57c, &key);  // HashMap_Index
        title_function<void XB_THISCALL(uint32_t, uint32_t)>(0x0011dd20)(value, 0x00824f9c);  // ScriptString_AssignCString("generic_wheelsmoke")
    }
    scr_release(&fx);
}

void XB_CDECL BroScr_FlakvierlingInit(uint32_t ent, uint32_t health, uint32_t a2) {
    *BroScr_FieldSlot(ent, 0xb330b301) = 0;
    scr_slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922b94)(
        ent, health == title_global<uint32_t>(0x00922ec0) ? 0x989680 : health);  // ScrEnt_SetHealth
    const uint32_t value = a2 != title_global<uint32_t>(0x00922ec0) ? a2 : 0x2000;
    *BroScr_FieldSlot(ent, 0x93c3cf6c) = value;
    start_thread1(0x0014ffd0, ent);  // BroScr_VehicleDeath_Flakvierling
    start_thread1(0x00148760, ent);  // BroScr_FlakvierlingThink
}

namespace {

void use_anim_tree(uint32_t vehicle, uint32_t tree) {
    ScriptString name;
    scr_string_from(&name, tree);
    scr_slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009225f0)(vehicle, &name);  // GScr_UseAnimTree
    scr_release(&name);
}

// Script thread with one extra argument: functor {0x007f4a1c, function, vehicle, value}
// (ScrIface_Alloc(0x10), null when it fails), then Scr_CreateThread(0, 0, 0, vehicle, functor, 0).
void start_vehicle_thread2(uint32_t vehicle, uint32_t function, uint32_t value) {
    uint32_t* functor = static_cast<uint32_t*>(scr_slot<void* XB_CDECL(uint32_t)>(0x00922928)(0x10));
    if (functor) {
        functor[0] = 0x007f4a1c;
        functor[1] = function;
        functor[2] = vehicle;
        functor[3] = value;
    }
    using CreateThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);
    scr_slot<CreateThread>(0x0092216c)(0, 0, 0, vehicle, functor, 0);
}

}  // namespace

void XB_CDECL BroScr_HalftrackSetup(uint32_t vehicle) {
    use_anim_tree(vehicle, 0x00803cc4);  // "halftrack"
    const uint32_t health = scr_slot<uint32_t XB_CDECL(uint32_t)>(0x0092222c)(1000) + 500;  // Scr_RandomInt
    scr_slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922b94)(vehicle, health);         // ScrEnt_SetHealth
    StartVehicleThread(vehicle, 0x0014d0a0);  // BroScr_VehicleHealthThink
    StartVehicleThread(vehicle, 0x00142ee0);  // BroScr_VehicleInit_Halftrack
    StartVehicleThread(vehicle, 0x00127e30);  // BroScr_VehicleTurretFireLoop_2
    start_vehicle_thread2(vehicle, 0x00134a90, 1);  // BroScr_TankStunOnDamage
}

void XB_CDECL BroScr_PanzerIVSetup_4(uint32_t vehicle) {
    use_anim_tree(vehicle, 0x007f499c);  // "panzerIV"
    title_function<void XB_CDECL(uint32_t)>(0x00152550)(vehicle);  // BroScr_TankDefaultHealthThink_2
    StartVehicleThread(vehicle, 0x00145080);  // BroScr_VehicleInit_Sherman
    StartVehicleThread(vehicle, 0x00128720);  // BroScr_VehicleTurretFireLoop_5
    start_vehicle_thread2(vehicle, 0x00134a90, 1);  // BroScr_TankStunOnDamage
}

}  // namespace game
