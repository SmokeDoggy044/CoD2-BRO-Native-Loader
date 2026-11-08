#include "bro_scr_tank_init.h"

#include "bro_scr_common.h"
#include "bro_scr_util.h"
#include "scr_access.h"
#include "title_data.h"
#include "bro_scr_vehicle.h"
#include "levels_twins.h"
#include "script_string.h"
#include "bro_scr_level_threads.h"
#include "bro_scr_vehicle_health.h"
#include "bro_scr_wait_notify.h"

namespace game {

namespace {

// Script thread on the vehicle: functor {0x007f5ba0, function, vehicle} from ScrIface_Alloc (null
// when it fails), then Scr_CreateThread(0, 0, 0, vehicle, functor, 0).
void start_thread(uint32_t vehicle, uint32_t function) {
    uint32_t* functor = static_cast<uint32_t*>(scr_slot<void* XB_CDECL(uint32_t)>(0x00922928)(0xc));
    if (functor) {
        functor[0] = 0x007f5ba0;
        functor[1] = function;
        functor[2] = vehicle;
    }
    using CreateThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);
    scr_slot<CreateThread>(0x0092216c)(0, 0, 0, vehicle, functor, 0);
}

// BroScr_TankStunOnDamage(vehicle, 1): functor {0x007f4a1c, function, vehicle, 1}.
void start_stun_thread(uint32_t vehicle) {
    uint32_t* functor = static_cast<uint32_t*>(scr_slot<void* XB_CDECL(uint32_t)>(0x00922928)(0x10));
    if (functor) {
        functor[0] = 0x007f4a1c;
        functor[1] = own_address(&BroScr_TankStunOnDamage);  // BroScr_TankStunOnDamage
        functor[2] = vehicle;
        functor[3] = 1;
    }
    using CreateThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);
    scr_slot<CreateThread>(0x0092216c)(0, 0, 0, vehicle, functor, 0);
}

void normalize_team(uint32_t vehicle, uint32_t text) {
    ScriptString team;
    scr_string_from(&team, text);
    own_function<void XB_CDECL(uint32_t, ScriptString)>(&BroScr_NormalizeTeamName)(vehicle, team);  // BroScr_NormalizeTeamName
}

void use_anim_tree(uint32_t vehicle, uint32_t tree) {
    ScriptString name;
    scr_string_from(&name, tree);
    scr_slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009225f0)(vehicle, &name);  // GScr_UseAnimTree
    scr_release(&name);
}

// Health 500 + Scr_RandomInt(1000) and the BroScr_VehicleHealthThink thread.
void random_health(uint32_t vehicle) {
    const uint32_t health = scr_slot<uint32_t XB_CDECL(uint32_t)>(0x0092222c)(1000) + 500;  // Scr_RandomInt
    scr_slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922b94)(vehicle, health);         // ScrEnt_SetHealth
    start_thread(vehicle, own_address(&BroScr_VehicleHealthThink));  // BroScr_VehicleHealthThink
}

struct Spec {
    uint32_t team;    // text, 0 = none
    uint32_t tree;    // anim tree text
    uint32_t health;  // health function, 0 = random_health
    uint32_t init;
    uint32_t turret;
    bool dead_stop;
};

// Team, anim tree, health and the init / turret / stun / [dead stop] threads.
void init_start(uint32_t vehicle, const Spec& spec) {
    if (spec.team) normalize_team(vehicle, spec.team);
    use_anim_tree(vehicle, spec.tree);
    if (spec.health)
        title_function<void XB_CDECL(uint32_t)>(spec.health)(vehicle);
    else
        random_health(vehicle);
    start_thread(vehicle, spec.init);
    start_thread(vehicle, spec.turret);
    start_stun_thread(vehicle);
    if (spec.dead_stop) start_thread(vehicle, own_address(&BroScr_VehicleDeadStop));  // BroScr_VehicleDeadStop
}

// The shared tail; `slot` is the vehicle's argument slot (the original keeps the key there).
void init_finish(uint32_t vehicle, uint32_t* slot, uint32_t smoke) {
    *BroScr_FieldSlot(vehicle, 0x8473dd9d) = 0x10;
    *slot = 0x438c482f;
    using Index = const ScriptString* XB_THISCALL(uint32_t, const uint32_t*);
    if (title_function<Index>(0x00461120)(title_global<uint32_t>(0x00922ee0) + 0x57c, slot)->rep == nullptr) {  // HashMap_Index
        ScriptString fx;
        if (smoke)
            scr_string_from(&fx, smoke);
        else
            own_function<void XB_THISCALL(ScriptString*, uint32_t)>(&ScriptString_Copy)(&fx, 0x00922e90);  // ScriptString_Copy(empty)
        own_function<void XB_CDECL(uint32_t, ScriptString)>(&BroScr_SetWheelSmokeFx)(vehicle, fx);  // BroScr_SetWheelSmokeFx
    }
    start_thread(vehicle, own_address(&BroScr_SpawnWheelOrigins));  // BroScr_SpawnWheelOrigins
}

// Scr_StartThread_ent_str(0, 0, 0, 0, BroScr_TankHullGunnerAtGunhull, vehicle, weapon or
// `fallback` when null).
void gunner_at_gunhull(uint32_t vehicle, ScriptString* weapon, uint32_t fallback) {
    using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, ScriptString*);
    const auto start = own_function<StartThread>(&Scr_StartThread_ent_str_PROLOGUE_entry);
    if (weapon->rep) {
        start(0, 0, 0, 0, own_address(&BroScr_TankHullGunnerAtGunhull), vehicle, weapon);
    } else {
        ScriptString name;
        scr_string_from(&name, fallback);
        start(0, 0, 0, 0, own_address(&BroScr_TankHullGunnerAtGunhull), vehicle, &name);
        scr_release(&name);
    }
}

// Scr_StartThread_ent_str_str(0, 0, 0, 0, BroScr_TankHullGunnerThink, vehicle, weapon or
// "mg30cal_sherman_tank", tag).
void gunner_think(uint32_t vehicle, ScriptString* weapon, ScriptString* tag) {
    using StartThread =
        uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, ScriptString*, ScriptString*);
    const auto start = own_function<StartThread>(&Scr_StartThread_ent_str_str_PROLOGUE_entry);
    if (weapon->rep) {
        start(0, 0, 0, 0, own_address(&BroScr_TankHullGunnerThink), vehicle, weapon, tag);
    } else {
        ScriptString name;
        scr_string_from(&name, 0x00808b50);  // "mg30cal_sherman_tank"
        start(0, 0, 0, 0, own_address(&BroScr_TankHullGunnerThink), vehicle, &name, tag);
        scr_release(&name);
    }
}

constexpr uint32_t kAllies = 0x00832084;
constexpr uint32_t kAxis = 0x0083208c;
constexpr uint32_t kPanzerIV = 0x007f499c;
constexpr uint32_t kDay = 0x007ff364;

// TankInitHealth, VehicleExplodeSound, TankTurretFireLoopRecoil_3.
constexpr Spec kAlliesRecoil3 = {kAllies, kPanzerIV, 0x00152970, 0x00145ac0, 0x001287e0, true};
// TankDefaultHealthThink, VehicleInit_M13, TankTurretFireLoopRecoil.
constexpr Spec kAxisM13 = {kAxis, kPanzerIV, 0x001506d0, 0x00142d50, 0x00127d30, true};

Spec without_dead_stop(Spec spec) {
    spec.dead_stop = false;
    return spec;
}

void tank_init_gunner(uint32_t* slot, ScriptString* weapon, ScriptString* tag, const Spec& spec) {
    const uint32_t tank = *slot;
    init_start(tank, spec);
    gunner_think(tank, weapon, tag);
    init_finish(tank, slot, 0);
    scr_release(weapon);
    scr_release(tag);
}

}  // namespace

void XB_CDECL BroScr_TankInit(uint32_t tank, ScriptString weapon) {
    const uint32_t vehicle = tank;
    ScriptString model;
    const bool sherman = ScriptString_Equals(ScrCall_ScrEnt_GetModel(&tank, &model),
                                             reinterpret_cast<const char*>(static_cast<uintptr_t>(0x00821020)));  // "xmodel/v_us_lnd_sherman"
    scr_release(&model);
    normalize_team(vehicle, sherman ? kAllies : kAxis);
    use_anim_tree(vehicle, kPanzerIV);
    const uint32_t level = title_global<uint32_t>(0x00922ee0);
    if (scr_slot<uint32_t XB_CDECL(uint32_t)>(0x00922214)(title_global<uint32_t>(level + 0x6ec))) {  // Scr_EntityHandleIsValid
        ScriptKeyTable* const table = BroScr_ScriptObjectOrEmpty(vehicle);
        uint32_t* value = ScriptKeyTable_Find(table, 0x6e688ecd);
        if (!value) value = ScriptKeyTable_Set(table, 0x6e688ecd, 0);
        *value = title_global<uint32_t>(title_global<uint32_t>(0x00922ee0) + 0x6ec);
    }
    random_health(vehicle);
    start_thread(vehicle, own_address(&BroScr_VehicleInit_PanzerIV));  // BroScr_VehicleInit_PanzerIV
    start_thread(vehicle, own_address(&BroScr_VehicleTurretFireLoop_4));  // BroScr_VehicleTurretFireLoop_4
    start_stun_thread(vehicle);
    gunner_at_gunhull(vehicle, &weapon, 0x00801670);  // "mg34_german_tank"
    init_finish(vehicle, &tank, kDay);
    scr_release(&weapon);
}

void XB_CDECL BroScr_TankInit_2(uint32_t tank, ScriptString weapon) {
    const uint32_t vehicle = tank;
    // TankDefaultHealthThink_2, VehicleInit_Sherman, VehicleTurretFireLoop_5
    init_start(vehicle, {kAllies, kPanzerIV, own_address(&BroScr_TankDefaultHealthThink_2), own_address(&BroScr_VehicleInit_Sherman), own_address(&BroScr_VehicleTurretFireLoop_5), true});
    gunner_at_gunhull(vehicle, &weapon, 0x00808b50);  // "mg30cal_sherman_tank"
    init_finish(vehicle, &tank, 0);
    scr_release(&weapon);
}

void XB_CDECL BroScr_TankInit_3(uint32_t tank, ScriptString weapon, ScriptString tag) {
    tank_init_gunner(&tank, &weapon, &tag, kAlliesRecoil3);
}

void XB_CDECL BroScr_TankInit_4(uint32_t tank, ScriptString weapon, ScriptString tag) {
    tank_init_gunner(&tank, &weapon, &tag, without_dead_stop(kAlliesRecoil3));
}

void XB_CDECL BroScr_TankInit_5(uint32_t tank, ScriptString weapon, ScriptString tag) {
    tank_init_gunner(&tank, &weapon, &tag, kAxisM13);
}

void XB_CDECL BroScr_TankInit_6(uint32_t tank, ScriptString weapon, ScriptString tag) {
    tank_init_gunner(&tank, &weapon, &tag, without_dead_stop(kAxisM13));
}

void XB_CDECL BroScr_TankInit_7(uint32_t tank) {
    const uint32_t vehicle = tank;
    init_start(vehicle, kAlliesRecoil3);
    init_finish(vehicle, &tank, 0);
}

void XB_CDECL BroScr_TankInit_8(uint32_t tank) {
    const uint32_t vehicle = tank;
    init_start(vehicle, without_dead_stop(kAlliesRecoil3));
    init_finish(vehicle, &tank, 0);
}

void XB_CDECL BroScr_VehicleInit_JeepAllies(uint32_t vehicle) {
    const uint32_t ent = vehicle;
    // "willyjeep", VehicleInit_Jeep, VehicleTurretFireLoop_7
    init_start(ent, {kAllies, 0x007f4948, 0, own_address(&BroScr_VehicleInit_Jeep), own_address(&BroScr_VehicleTurretFireLoop_7), true});
    init_finish(ent, &vehicle, kDay);
}

void XB_CDECL BroScr_VehicleInit_HalftrackAllies(uint32_t vehicle) {
    const uint32_t ent = vehicle;
    // "halftrack", VehicleInit_Halftrack, VehicleTurretFireLoop_2
    init_start(ent, {kAllies, 0x00803cc4, 0, own_address(&BroScr_VehicleInit_Halftrack), own_address(&BroScr_VehicleTurretFireLoop_2), true});
    init_finish(ent, &vehicle, kDay);
}

void XB_CDECL BroScr_VehicleInit_PanzerIV_2(uint32_t tank) {
    const uint32_t vehicle = tank;
    init_start(vehicle, kAxisM13);
    init_finish(vehicle, &tank, 0);
}

void XB_CDECL BroScr_PanzerIVSetup(uint32_t tank) {
    const uint32_t vehicle = tank;
    Spec spec = without_dead_stop(kAlliesRecoil3);
    spec.team = 0;
    init_start(vehicle, spec);
    init_finish(vehicle, &tank, 0);
}

}  // namespace game
