#include "bro_scr_vehicle.h"

#include <cstring>

#include "bro_scr_common.h"
#include "bro_scr_util.h"
#include "g_scr_runtime.h"
#include "g_script.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "scr_access.h"
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

void XB_CDECL BroScr_PanzerIVSetup_2(uint32_t vehicle) {
    ScriptString team;
    scr_string_from(&team, 0x0083208c);  // "axis"
    BroScr_NormalizeTeamName(vehicle, team);
    use_anim_tree(vehicle, 0x007f499c);  // "panzerIV"
    const uint32_t health = scr_slot<uint32_t XB_CDECL(uint32_t)>(0x0092222c)(1000) + 500;  // Scr_RandomInt
    scr_slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922b94)(vehicle, health);         // ScrEnt_SetHealth
    StartVehicleThread(vehicle, 0x0014d0a0);  // BroScr_VehicleHealthThink
    StartVehicleThread(vehicle, 0x00143870);  // BroScr_VehicleInit_PanzerIV
    StartVehicleThread(vehicle, 0x001283d0);  // BroScr_VehicleTurretFireLoop_4
    start_vehicle_thread2(vehicle, 0x00134a90, 1);  // BroScr_TankStunOnDamage
}

ScriptString* XB_CDECL BroScr_TreadFxSurfaceThink(ScriptString* out, uint32_t vehicle, uint32_t wheel) {
    ScriptString surface;
    title_function<ScriptString* XB_CDECL(ScriptString*, const uint32_t*, uint32_t)>(0x0011f8d0)(&surface, &vehicle,
                                                                                               wheel);  // ScrCall_GScr_Vehicle_GetWheelSurface
    ScriptString fx;
    title_function<void XB_THISCALL(ScriptString*, uint32_t)>(0x0011d5d0)(&fx, 0);  // ScriptString_FromRep(null)
    const auto equals = [&surface](uint32_t text) {
        return title_function<uint8_t XB_CDECL(const ScriptString*, uint32_t)>(0x0011d7c0)(&surface, text) != 0;  // ScriptString_Equals
    };
    uint32_t key = 0;
    if (equals(0x008331b4)) {         // "grass"
        key = 0x438c482f;
    } else if (equals(0x0083317c)) {  // "sand"
        key = 0xe2162fb5;
    } else if (equals(0x008331d4)) {  // "dirt"
        key = 0xe21d8c25;
    } else if (equals(0x00833184)) {  // "rock"
        key = 0xe216961a;
    } else if (equals(0x00833174)) {  // "snow"
        key = 0xe2161386;
    } else {
        if (equals(0x008331a8)) scr_slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(vehicle, 0x9b4136a0);  // "ice": Scr_QueueNotify
        title_function<void XB_THISCALL(ScriptString*, uint32_t)>(0x0011dd20)(&fx, 0x00824fb0);  // ScriptString_AssignCString("nofx")
    }
    if (key) {
        using Index = const ScriptString* XB_THISCALL(uint32_t, const uint32_t*);
        const ScriptString* value = title_function<Index>(0x00461120)(title_global<uint32_t>(0x00922ee0) + 0x57c, &key);  // HashMap_Index
        title_function<void XB_THISCALL(ScriptString*, const ScriptString*)>(0x0011d600)(&fx, value);  // ScriptString_Assign
    }
    if (fx.rep == nullptr) title_function<void XB_THISCALL(ScriptString*, uint32_t)>(0x0011dd20)(&fx, 0x00824fb0);  // "nofx"
    title_function<void XB_THISCALL(ScriptString*, const ScriptString*)>(0x0011d5e0)(out, &fx);  // ScriptString_Copy
    scr_release(&fx);
    scr_release(&surface);
    return out;
}

namespace {

uint32_t script_object(uint32_t ent) {
    const uint32_t table = title_function<uint32_t XB_CDECL(uint32_t)>(0x0011db20)(ent);  // ScrCall_GScr_GetEntScriptObject
    return table ? table : 0x00922e94;  // g_scrEmptyKeyTable
}

ScriptString* field_string(uint32_t ent, uint32_t hash) {
    return title_function<ScriptString* XB_THISCALL(uint32_t, uint32_t)>(0x00458da0)(script_object(ent), hash);  // ScriptKeyTable_GetString
}

uint8_t* field_array(uint32_t ent, uint32_t hash) {
    return title_function<uint8_t* XB_THISCALL(uint32_t, uint32_t)>(0x0045cd50)(script_object(ent), hash);  // ScriptKeyTable_GetArray
}

ScriptString* string_plus_int(ScriptString* out, uint32_t text, uint32_t value) {
    return title_function<ScriptString* XB_CDECL(ScriptString*, uint32_t, uint32_t)>(0x0011a7d0)(out, text, value);  // ScriptString_FromCStringPlusInt
}

}  // namespace

void XB_CDECL BroScr_VehicleGetPassengers(uint32_t vehicle, uint32_t rider, ScriptString tag, uint32_t index) {
    using Assign = void XB_THISCALL(ScriptString*, const ScriptString*);
    const auto assign = title_function<Assign>(0x0011d600);  // ScriptString_Assign
    ScriptString temp;
    const bool named = ScrCall_ScrEnt_GetAnimname(&rider, &temp)->rep != nullptr;
    scr_release(&temp);
    if (named) {
        const ScriptString* animname = ScrCall_ScrEnt_GetAnimname(&rider, &temp);
        assign(field_string(rider, 0x55576ed6), animname);
        scr_release(&temp);
    }
    const ScriptString* name = string_plus_int(&temp, 0x007fb8b4, index);  // "passenger" + index
    ScriptString copy;
    title_function<Assign>(0x0011d5e0)(&copy, name);  // ScriptString_Copy
    scr_slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922c44)(rider, copy);  // ScrEnt_SetAnimname
    scr_release(&temp);
    const ScriptString* tag_name = string_plus_int(&temp, 0x007f5c18, index);  // "tag_passenger" + index
    assign(field_string(rider, 0xd91e5c5b), tag_name);
    scr_release(&temp);
    DynArray_PushBack(field_array(vehicle, 0xe2a39a33), &rider);
    const uint32_t zero[6] = {0, 0, 0, 0, 0, 0};  // 0.0f: offset (+0), angles (+3)
    using Link = void XB_CDECL(uint32_t, uint32_t, const ScriptString*, const uint32_t*, const uint32_t*, uint32_t);
    scr_slot<Link>(0x009224f4)(rider, vehicle, &tag, &zero[3], &zero[0], 0);  // GScr_LinkToTagWithOffset
    DynArray_PushBack(field_array(vehicle, 0xe104fd5c), &rider);
    using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                          const ScriptString*);
    title_function<StartThread>(0x0045b3b0)(0, 0, 0, 0, 0x0016ace0, vehicle, &rider, &tag);  // Scr_StartThread_ent_ent_str(BroScr_VehicleRidersThink)
    scr_release(&tag);
}

namespace {

// The vehicle's rider slots (key 0x2cd69a2a, ScriptKeyTable_GetOrAddArray): 8-byte entries
// {handle, flag}, grown (DynArray_Resize8(seat + 1, seat + 4)) when `seat` is past the count.
uint32_t* rider_slot(uint32_t vehicle, uint32_t seat) {
    uint32_t* array = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(
        ScriptKeyTable_GetOrAddArray(BroScr_ScriptObjectOrEmpty(vehicle), 0x2cd69a2a)));
    if (seat >= array[2]) title_function<void XB_THISCALL(uint32_t*, uint32_t, uint32_t)>(0x0017bfb0)(array, seat + 1, seat + 4);
    return reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(array[0])) + seat * 2;
}

uint32_t handle_valid(uint32_t handle) { return scr_slot<uint32_t XB_CDECL(uint32_t)>(0x00922214)(handle); }  // Scr_EntityHandleIsValid
uint32_t is_alive(uint32_t ent) { return scr_slot<uint32_t XB_CDECL(uint32_t)>(0x00922210)(ent); }            // GScr_IsAlive

// ScrField_gunners(vehicle)[seat] (DynArray_At).
uint32_t gunner(uint32_t vehicle, uint32_t seat) {
    void* gunners = title_function<void* XB_CDECL(uint32_t)>(0x00146ec0)(vehicle);
    return *title_function<uint32_t* XB_THISCALL(void*, uint32_t)>(0x0017c050)(gunners, seat);
}

}  // namespace

void XB_CDECL BroScr_VehicleSpawnRider(uint32_t vehicle, uint32_t rider, uint32_t seat, bool skip_mount) {
    if (!handle_valid(rider) || !is_alive(rider)) return;
    uint32_t free_seat;
    if (seat != title_global<uint32_t>(0x00922ec0)) {  // g_scrUndefinedInt
        free_seat = seat;
        if (handle_valid(rider_slot(vehicle, seat)[0]) && is_alive(gunner(vehicle, seat))) {
            // The asked seat is taken: the first gunner seat (0-2) without a live gunner.
            for (free_seat = 0; free_seat < 3; ++free_seat) {
                if (!handle_valid(gunner(vehicle, free_seat)) || !is_alive(gunner(vehicle, free_seat))) break;
            }
            if (free_seat == 3) return;
        }
    } else {
        // The first rider slot (0-2) without a live rider.
        for (free_seat = 0; free_seat < 3; ++free_seat) {
            if (!handle_valid(rider_slot(vehicle, free_seat)[0]) || !is_alive(rider_slot(vehicle, free_seat)[0])) break;
        }
        if (free_seat == 3) return;
    }
    uint32_t entry[2];
    const uint32_t* value = ScrCall_ScrEnt_SetTargetname_2(entry, rider);  // {rider, false}, targetname "gunner"
    uint32_t* slot = rider_slot(vehicle, free_seat);
    slot[0] = value[0];
    slot[1] = value[1];
    scr_slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(vehicle, 0x387a52e5);  // Scr_QueueNotify
    ScriptString tag;
    title_function<void XB_THISCALL(ScriptString*, uint32_t)>(0x0011d5d0)(&tag, 0);  // ScriptString_FromRep(null)
    uint32_t text = 0;
    switch (free_seat) {
        case 0: text = 0x007fa47c; break;  // "tag_gunner"
        case 1: text = 0x007fc0bc; break;  // "tag_passenger1"
        case 2: text = 0x007f5c08; break;  // "tag_passenger2"
    }
    if (text) title_function<void XB_THISCALL(ScriptString*, uint32_t)>(0x0011dd20)(&tag, text);  // ScriptString_AssignCString
    if (!skip_mount) {
        // Scr_StartThread_ent_ent_i_str_b_2(0, 0, 0, 0, BroScr_VehicleMountRiders, vehicle, &slot, &seat, &tag, &skip_mount)
        uint32_t* const mount = rider_slot(vehicle, free_seat);
        using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*,
                                              uint32_t*, ScriptString*, bool*);
        title_function<StartThread>(0x0017af10)(0, 0, 0, 0, 0x001542b0, vehicle, mount, &free_seat, &tag, &skip_mount);
    }
    scr_release(&tag);
}

void XB_CDECL BroScr_VehicleSpawnRiderByName(ScriptString name, uint32_t rider, uint32_t seat) {
    ScriptString copy;
    ScriptString_Copy(&copy, &name);
    uint32_t found;
    const uint32_t vehicle = *title_function<uint32_t* XB_CDECL(uint32_t*, ScriptString)>(0x001366b0)(&found, copy);  // BroScr_FindLevelVehicleByTargetname
    if (handle_valid(vehicle)) VehicleSpawnRider(vehicle, rider, seat, false);
    scr_release(&name);
}

namespace {

float as_float(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

uint32_t as_bits(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

// GScr_GetTagOrigin(vehicle, &out, tag) with out preset to the undefined vector lane.
ScrVec3 tag_origin(uint32_t vehicle, ScriptString* tag) {
    const uint32_t lane = title_global<uint32_t>(0x00922eb8);  // g_scrUndefinedVecLane
    uint32_t out[3] = {lane, lane, lane};
    scr_slot<void XB_CDECL(uint32_t, uint32_t*, ScriptString*)>(0x0092261c)(vehicle, out, tag);
    return {{out[0], out[1], out[2]}};
}

// The vehicle's angles with 32 added to the first (ScrEnt_GetAngles), set on `ent`.
void copy_angles_plus_32(uint32_t ent, uint32_t vehicle) {
    uint32_t buffer[3];
    const uint32_t* a = scr_slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922ba0)(buffer, vehicle);
    const ScrVec3 angles = {{as_bits(as_float(a[0]) + as_float(title_global<uint32_t>(0x0083eeac))), a[1], a[2]}};
    scr_slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922ba4)(ent, angles);  // ScrEnt_SetAngles
}

// Script thread on the vehicle with an entity and a hash: functor {0x007f4a88, BroScr_TreadFx,
// vehicle, ent, hash}.
void start_tread_fx(uint32_t vehicle, uint32_t ent, uint32_t hash) {
    uint32_t* functor = static_cast<uint32_t*>(scr_slot<void* XB_CDECL(uint32_t)>(0x00922928)(0x14));
    if (functor) {
        functor[0] = 0x007f4a88;
        functor[1] = 0x00163b40;
        functor[2] = vehicle;
        functor[3] = ent;
        functor[4] = hash;
    }
    using CreateThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);
    scr_slot<CreateThread>(0x0092216c)(0, 0, 0, vehicle, functor, 0);
}

}  // namespace

void XB_CDECL BroScr_SpawnWheelOrigins(uint32_t vehicle) {
    const uint32_t ent = vehicle;
    // The original keeps its temporaries in the argument slot: the two IsDefined bytes, then the
    // four strings (the last one's released rep stays there).
    ScriptString* const temp = reinterpret_cast<ScriptString*>(&vehicle);
    using IsDefined = bool* XB_CDECL(bool*, uint32_t);
    if (!*title_function<IsDefined>(0x0012b870)(reinterpret_cast<bool*>(&vehicle), ent))  // ScrField_h8473dd9d_IsDefined
        *BroScr_FieldSlot(ent, 0x8473dd9d) = 0x5c;
    if (!*title_function<IsDefined>(0x0012b450)(reinterpret_cast<bool*>(&vehicle), ent))  // ScrField_hdf09fd77_IsDefined
        *title_function<uint32_t* XB_CDECL(uint32_t)>(0x0012b400)(ent) = title_global<uint32_t>(0x0083eb9c);  // 1000.0
    scr_string_from(temp, 0x0083a400);  // "tag_wheel_back_right"
    const ScrVec3 right = tag_origin(ent, temp);
    scr_release(temp);
    scr_string_from(temp, 0x0083a418);  // "tag_wheel_back_left"
    const ScrVec3 left = tag_origin(ent, temp);
    scr_release(temp);
    using Spawn = uint32_t XB_CDECL(ScriptString*, const ScrVec3*, uint32_t);
    scr_string_from(temp, 0x0083ab88);  // "script_origin"
    const uint32_t right_origin = scr_slot<Spawn>(0x009222f0)(temp, &right, 0);  // GScr_Spawn
    scr_release(temp);
    scr_string_from(temp, 0x0083ab88);
    const uint32_t left_origin = scr_slot<Spawn>(0x009222f0)(temp, &left, 0);
    scr_release(temp);
    copy_angles_plus_32(right_origin, ent);
    copy_angles_plus_32(left_origin, ent);
    scr_slot<void XB_CDECL(uint32_t, uint32_t)>(0x009224fc)(right_origin, ent);  // GScr_LinkTo
    scr_slot<void XB_CDECL(uint32_t, uint32_t)>(0x009224fc)(left_origin, ent);
    start_tread_fx(ent, right_origin, 0x79193376);
    start_tread_fx(ent, left_origin, 0x13c7f9a4);
}

namespace {

bool all_undefined(const ScrVec3& v) {
    const float lane = title_global<float>(0x00922eb8);  // g_scrUndefinedVecLane
    float f[3];
    std::memcpy(f, v.v, 12);
    return f[0] == lane && f[1] == lane && f[2] == lane;
}

bool is_script_model(uint32_t* rider) {
    ScriptString temp;
    const bool model = ScriptString_Equals(ScrCall_ScrEnt_GetClassname(rider, &temp),
                                           reinterpret_cast<const char*>(static_cast<uintptr_t>(0x0083ab98)));  // "script_model"
    scr_release(&temp);
    return model;
}

// ScrEnt_SetAnimname through the slot with a copy of `text`.
void set_animname_copy(uint32_t rider, uint32_t text) {
    ScriptString name;
    scr_string_from(&name, text);
    ScriptString copy;
    ScriptString_Copy(&copy, &name);
    scr_slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922c44)(rider, copy);
    scr_release(&name);
}

// GScr_UseAnimTree(rider, the level's tree for its animname: level +0x238 by hash).
void use_animname_tree(uint32_t* rider) {
    ScriptString temp;
    const ScriptString* name = ScrCall_ScrEnt_GetAnimname(rider, &temp);
    uint32_t hash;
    Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(name));
    using Index = ScriptString* XB_THISCALL(uint32_t, const uint32_t*);
    ScriptString* tree = title_function<Index>(0x00461120)(title_global<uint32_t>(0x00922ee0) + 0x238, &hash);  // HashMap_Index
    scr_slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009225f0)(*rider, tree);  // GScr_UseAnimTree
    scr_release(&temp);
}

// A rider that already has an animname keeps it in oldanimname.
void keep_old_animname(uint32_t* rider) {
    ScriptString temp;
    const bool named = ScrCall_ScrEnt_GetAnimname(rider, &temp)->rep != nullptr;
    scr_release(&temp);
    if (!named) return;
    const ScriptString* name = ScrCall_ScrEnt_GetAnimname(rider, &temp);
    ScriptString* old = title_function<ScriptString* XB_CDECL(uint32_t)>(0x00129610)(*rider);  // ScrField_oldanimname
    title_function<void XB_THISCALL(ScriptString*, const ScriptString*)>(0x0011d600)(old, name);  // ScriptString_Assign
    scr_release(&temp);
}

bool is_level_player(uint32_t rider) { return rider == title_global<uint32_t>(title_global<uint32_t>(0x00922ee0) + 0x7f8); }

}  // namespace

void XB_CDECL BroScr_DroneVehicleRiders(uint32_t vehicle, uint32_t rider, ScriptString role, uint32_t index, ScrVec3 offset,
                                        ScrVec3 angles) {
    if (all_undefined(offset)) offset = {{0, 0, 0}};
    if (all_undefined(angles)) angles = {{0, 0, 0}};
    using Assign = void XB_THISCALL(ScriptString*, const ScriptString*);
    using AssignText = void XB_THISCALL(ScriptString*, uint32_t);
    ScriptString tag;
    title_function<void XB_THISCALL(ScriptString*, uint32_t)>(0x0011d5d0)(&tag, 0);  // ScriptString_FromRep(null)
    const auto role_is = [&role](uint32_t text) {
        return ScriptString_Equals(&role, reinterpret_cast<const char*>(static_cast<uintptr_t>(text)));
    };
    if (role_is(0x007fc068)) {  // "driver"
        if (is_script_model(&rider)) {
            set_animname_copy(rider, 0x007fc058);  // "drone_driver"
            use_animname_tree(&rider);
        } else if (!is_level_player(rider)) {
            keep_old_animname(&rider);
            set_animname_copy(rider, 0x007fc068);
        }
        title_function<AssignText>(0x0011dd20)(field_string(rider, 0xd91e5c5b), 0x007f5c28);  // "tag_driver"
        title_function<AssignText>(0x0011dd20)(&tag, 0x007f5c28);
    } else if (role_is(0x007fc050)) {  // "gunner"
        if (is_script_model(&rider)) {
            set_animname_copy(rider, 0x007fc038);  // "drone_gunner"
            use_animname_tree(&rider);
        } else if (!is_level_player(rider)) {
            keep_old_animname(&rider);
            ScriptString name;
            scr_string_from(&name, 0x007fc050);
            ScrCall_ScrEnt_SetAnimname(&rider, &name);
            scr_release(&name);
        }
        title_function<AssignText>(0x0011dd20)(field_string(rider, 0xd91e5c5b), 0x007fa47c);  // "tag_gunner"
        title_function<AssignText>(0x0011dd20)(&tag, 0x007fa47c);
    } else if (role_is(0x007fb8b4)) {  // "passenger"
        // Two model checks on the vehicle whose results are unused.
        ScriptString temp;
        ScriptString_Equals(ScrCall_ScrEnt_GetModel(&vehicle, &temp), reinterpret_cast<const char*>(static_cast<uintptr_t>(0x0082542c)));
        scr_release(&temp);
        ScriptString_Equals(ScrCall_ScrEnt_GetModel(&vehicle, &temp),
                            reinterpret_cast<const char*>(static_cast<uintptr_t>(0x007fa288)));  // "v_m3a1_halftrack_75mm"
        scr_release(&temp);
        if (is_script_model(&rider)) {
            ScrCall_ScrEnt_SetAnimname(&rider, string_plus_int(&temp, 0x007fc028, index));  // "drone_passenger" + index
            scr_release(&temp);
            use_animname_tree(&rider);
        } else if (!is_level_player(rider)) {
            keep_old_animname(&rider);
            ScrCall_ScrEnt_SetAnimname(&rider, string_plus_int(&temp, 0x007fb8b4, index));  // "passenger" + index
            scr_release(&temp);
        }
        title_function<Assign>(0x0011d600)(field_string(rider, 0xd91e5c5b), string_plus_int(&temp, 0x007f5c18, index));  // "tag_passenger" + index
        scr_release(&temp);
        title_function<Assign>(0x0011d600)(&tag, string_plus_int(&temp, 0x007f5c18, index));
        scr_release(&temp);
    } else {
        scr_release(&tag);
        scr_release(&role);
        return;
    }
    DynArray_PushBack(field_array(vehicle, 0xe2a39a33), &rider);
    using Link = void XB_CDECL(uint32_t, uint32_t, const ScriptString*, const ScrVec3*, const ScrVec3*, uint32_t);
    scr_slot<Link>(0x009224f4)(rider, vehicle, &tag, &offset, &angles, 0);  // GScr_LinkToTagWithOffset
    const uint32_t copy = rider;
    DynArray_PushBack(field_array(vehicle, 0xe104fd5c), &copy);
    using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                          const ScriptString*);
    title_function<StartThread>(0x0045b3b0)(0, 0, 0, 0, 0x0016ace0, vehicle, &rider, &tag);  // Scr_StartThread_ent_ent_str(BroScr_VehicleRidersThink)
    scr_release(&tag);
    scr_release(&role);
}

namespace {

uint32_t* ent_at(DynArray* ents, uint32_t i) {
    if (i >= ents->size) DynArray_Resize(reinterpret_cast<uint8_t*>(ents), i + 1, i + 4);
    return ents->data + i;
}

bool model_is(uint32_t* ent, uint32_t text) {
    ScriptString model;
    const bool same = ScriptString_Equals(ScrCall_ScrEnt_GetModel(ent, &model), reinterpret_cast<const char*>(static_cast<uintptr_t>(text)));
    scr_release(&model);
    return same;
}

DynArray* level_flak_list() { return reinterpret_cast<DynArray*>(static_cast<uintptr_t>(title_global<uint32_t>(0x00922ee0) + 0x784)); }

}  // namespace

void XB_CDECL BroScr_FindFlakvierlings(uint32_t health, uint32_t a2) {
    DynArray ents = {nullptr, 0, 0};
    {
        ScriptString classname;
        scr_string_from(&classname, 0x0083ab78);  // "script_vehicle"
        title_function<uint32_t XB_CDECL(const ScriptString*, uint32_t, DynArray*, uint32_t)>(0x0011f5d0)(&classname, 0xa106e9bd, &ents,
                                                                                                         0);  // Scr_GetEntArray
        scr_release(&classname);
    }
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ents.size); ++i) {
        // The original keeps both model strings until after the test (flag bits 1 and 2).
        ScriptString model1;
        ScriptString model2;
        bool flak = ScriptString_Equals(ScrCall_ScrEnt_GetModel(ent_at(&ents, i), &model1),
                                        reinterpret_cast<const char*>(static_cast<uintptr_t>(0x00824c44)));  // "v_flakvierling"
        bool second = false;
        if (!flak) {
            second = true;
            flak = ScriptString_Equals(ScrCall_ScrEnt_GetModel(ent_at(&ents, i), &model2),
                                       reinterpret_cast<const char*>(static_cast<uintptr_t>(0x00824ea8)));  // "v_flakvierling_base"
        }
        if (second) scr_release(&model2);
        scr_release(&model1);
        if (!flak) continue;
        bool known = false;
        for (uint32_t j = 0; static_cast<int32_t>(j) < static_cast<int32_t>(level_flak_list()->size); ++j) {
            const uint32_t* ent = ent_at(&ents, i);
            if (*ent_at(level_flak_list(), j) == *ent) {
                known = true;
                break;
            }
        }
        if (known) continue;
        DynArray_PushBack(reinterpret_cast<uint8_t*>(level_flak_list()), ent_at(&ents, i));
        const auto last = []() { return level_flak_list()->data[level_flak_list()->size - 1]; };
        title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x00154190)(last(), health, a2);  // BroScr_FlakvierlingInit
        const bool base = model_is(ent_at(&ents, i), 0x00824ea8);
        if (base)
            title_function<void XB_CDECL(uint32_t)>(0x00136580)(last());  // BroScr_FlakvierlingBaseAnimTree
        else
            title_function<void XB_CDECL(uint32_t)>(0x00136450)(last());  // BroScr_FlakvierlingAnimTree
    }
    if (ents.data) title_function<void XB_CDECL(void*)>(0x002f5e10)(ents.data);  // Scr_Free
}

uint32_t* XB_CDECL BroScr_SpawnFlakvierling(uint32_t* out, ScriptString targetname, uint32_t spawnflags, ScrVec3 origin,
                                            uint32_t node, uint32_t health, uint32_t a2) {
    if (targetname.rep == nullptr)
        title_function<void XB_THISCALL(ScriptString*, uint32_t)>(0x0011dd20)(&targetname, 0x007f49c4);  // "flakvierling"
    if (all_undefined(origin)) origin = {{0, 0, 0}};
    ScriptString type;
    scr_string_from(&type, 0x007f5dcc);  // "FlakVierling"
    ScriptString model;
    scr_string_from(&model, 0x00824c44);  // "v_flakvierling"
    const ScrVec3 angles = {{0, 0, 0}};
    using SpawnVehicle = uint32_t XB_CDECL(ScriptString*, ScriptString*, ScriptString*, const ScrVec3*, const ScrVec3*, uint32_t);
    uint32_t vehicle = scr_slot<SpawnVehicle>(0x009222fc)(&model, &targetname, &type, &origin, &angles, spawnflags);  // GScr_SpawnVehicle
    scr_release(&model);
    scr_release(&type);
    DynArray_PushBack(reinterpret_cast<uint8_t*>(level_flak_list()), &vehicle);
    const auto last = []() { return level_flak_list()->data[level_flak_list()->size - 1]; };
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x00154190)(last(), health, a2);  // BroScr_FlakvierlingInit
    // The original keeps a stack address (its entry ESP - 0x34) in a2's argument slot.
    a2 = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&out)) - 4 - 0x34;
    title_function<void XB_CDECL(uint32_t)>(0x00136450)(last());  // BroScr_FlakvierlingAnimTree
    if (node != 0 && node != 0xffffffff) {
        ScriptKeyTable* const table = BroScr_ScriptObjectOrEmpty(vehicle);
        uint32_t* value = ScriptKeyTable_Find(table, 0x6dcb7c56);
        if (!value) value = ScriptKeyTable_Set(table, 0x6dcb7c56, 0);
        *value = node;
    }
    const uint32_t time = scr_slot<uint32_t XB_CDECL()>(0x009223c4)();  // GScr_GetTime
    *BroScr_FieldSlot(vehicle, 0x05903940) = time;
    *out = vehicle;
    scr_release(&targetname);
    return out;
}

}  // namespace game
