#include "bro_scr_vehicle_health.h"

#include <cstring>

#include "bro_scr_util.h"
#include "g_scr_runtime.h"
#include "g_script.h"
#include "qcommon_core.h"
#include "scr_access.h"
#include "scr_field.h"
#include "title_data.h"

namespace game {

namespace {

template <typename T>
T* pointer(uint32_t address) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}

// A script-interface slot (pScr_*: a word in the import block at 0x00922xxx) called as F.
template <typename F>
F* slot(uint32_t address) {
    return title_function<F>(title_global<uint32_t>(address));
}

float as_float(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
uint32_t bits_of(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

void set_health(uint32_t ent, uint32_t health) { slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922b94)(ent, health); }  // ScrEnt_SetHealth

// Scr_StartThreadFunctor1's argument block: four zero words, the thread function, the entity.
uint32_t start_thread(uint32_t thread, uint32_t ent) {
    const uint32_t args[6] = {0, 0, 0, 0, thread, ent};
    return Scr_StartThreadFunctor1(args);
}

// A functor {0x007f5ba0, thread, ent} from ScrIface_Alloc (null when it fails) for Scr_CreateThread.
uint32_t* thread_functor(uint32_t thread, uint32_t ent) {
    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0xc);  // ScrIface_Alloc
    if (!functor) return nullptr;
    functor[0] = 0x007f5ba0;
    functor[1] = thread;
    functor[2] = ent;
    return functor;
}
void create_thread(uint32_t ent, uint32_t* functor) {
    slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(0, 0, 0, ent, functor,
                                                                                                    0);  // Scr_CreateThread
}

// Health by difficulty: easy, medium, hard, "fu", "gimp", other. The last two go through the
// ScrEnt_SetHealth call wrapper (pointer arguments).
void set_health_by_difficulty(uint32_t ent, const uint32_t (&health)[6]) {
    ScriptString difficulty;
    ScriptString_FromRep(&difficulty, nullptr);
    slot<void XB_CDECL(ScriptString*)>(0x009223cc)(&difficulty);  // GScr_GetDifficulty
    if (ScriptString_Equals(&difficulty, pointer<const char>(0x0082b244))) {  // "easy"
        set_health(ent, health[0]);
    } else if (ScriptString_Equals(&difficulty, pointer<const char>(0x0082b23c))) {  // "medium"
        set_health(ent, health[1]);
    } else if (ScriptString_Equals(&difficulty, pointer<const char>(0x0082b234))) {  // "hard"
        set_health(ent, health[2]);
    } else if (ScriptString_Equals(&difficulty, pointer<const char>(0x00832594))) {  // "fu"
        set_health(ent, health[3]);
    } else {
        const uint32_t value = ScriptString_Equals(&difficulty, pointer<const char>(0x008007a0)) ? health[4] : health[5];  // "gimp"
        ScrCall_ScrEnt_SetHealth(&ent, &value);
    }
    create_thread(ent, thread_functor(0x0014d0a0, ent));  // BroScr_VehicleHealthThink
    ScriptString_Release(&difficulty);
}

constexpr uint32_t kVehicleHealth[6] = {0x1964, 0x157c, 0x1388, 0xdac, 0x1d4c, 0x1388};
constexpr uint32_t kVehicleHealth5[6] = {0x4a38, 0x4268, 0x32c8, 0x2af8, 0x55f0, 0x4268};

uint32_t random_health_init(uint32_t ent) {
    const int32_t health = slot<int32_t XB_CDECL(int32_t)>(0x0092222c)(1000) + 500;  // Scr_RandomInt
    set_health(ent, static_cast<uint32_t>(health));
    return start_thread(0x0014d0a0, ent);  // BroScr_VehicleHealthThink
}

uint32_t default_health_think(uint32_t ent) {
    if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922b90)(ent) == title_global<uint32_t>(0x00922ec0)) {  // ScrEnt_GetHealth
        const int32_t health = slot<int32_t XB_CDECL(int32_t)>(0x0092222c)(1000) + 500;
        set_health(ent, static_cast<uint32_t>(health));
    }
    return start_thread(0x0014d0a0, ent);
}

uint32_t wing_turret(uint32_t ent, uint32_t watch) {
    set_health(ent, 300);
    return start_thread(watch, ent);
}

using GetVec = uint32_t* XB_CDECL(uint32_t*, uint32_t);
using GetString = ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t);

ScriptString* model_field(uint32_t ent) {
    return title_function<GetString>(0x00458da0)(BroScr_ScriptObjectOrEmpty(ent), 0x05d2529a);  // ScriptKeyTable_GetString
}

}  // namespace

void XB_CDECL BroScr_SetHealthByDifficulty(uint32_t ent) { set_health_by_difficulty(ent, kVehicleHealth); }
void XB_CDECL BroScr_SetHealthByDifficulty_2(uint32_t ent) { set_health_by_difficulty(ent, kVehicleHealth); }
void XB_CDECL BroScr_SetHealthByDifficulty_3(uint32_t ent) { set_health_by_difficulty(ent, kVehicleHealth); }
void XB_CDECL BroScr_SetHealthByDifficulty_4(uint32_t ent) { set_health_by_difficulty(ent, kVehicleHealth); }
void XB_CDECL BroScr_SetHealthByDifficulty_5(uint32_t ent) { set_health_by_difficulty(ent, kVehicleHealth5); }

uint32_t XB_CDECL BroScr_VehicleRandomHealthInit(uint32_t ent) { return random_health_init(ent); }
uint32_t XB_CDECL BroScr_VehicleRandomHealthInit_2(uint32_t ent) { return random_health_init(ent); }
uint32_t XB_CDECL BroScr_VehicleRandomHealthInit_3(uint32_t ent) { return random_health_init(ent); }

uint32_t XB_CDECL BroScr_TankDefaultHealthThink(uint32_t ent) { return default_health_think(ent); }
uint32_t XB_CDECL BroScr_TankDefaultHealthThink_2(uint32_t ent) { return default_health_think(ent); }
uint32_t XB_CDECL BroScr_TankInitHealth(uint32_t ent) { return default_health_think(ent); }

uint32_t XB_CDECL BroScr_VehicleHealth2500Init(uint32_t ent) {
    set_health(ent, 2500);
    return start_thread(0x0014d0a0, ent);
}

uint32_t XB_CDECL BroScr_InitWingTurret(uint32_t ent) { return wing_turret(ent, 0x0013a830); }
uint32_t XB_CDECL BroScr_InitWingTurret_2(uint32_t ent) { return wing_turret(ent, 0x0013aaa0); }

uint32_t XB_CDECL BroScr_InitWingTurret_3(uint32_t ent, uint32_t health) {
    if (health != title_global<uint32_t>(0x00922ec0)) {
        set_health(ent, health);
        return 0;
    }
    return wing_turret(ent, 0x0013eb00);
}

uint32_t XB_CDECL BroScr_StartVehicleTurretFireLoop_8(uint32_t ent) { return start_thread(0x00128f90, ent); }

uint32_t XB_CDECL BroScr_StartPathPosThreads(uint32_t ent) {
    start_thread(0x0014a910, ent);
    return start_thread(0x0014a9c0, ent);
}

void XB_CDECL BroScr_VehicleSetHealth_Jeep(uint32_t ent) {
    ScriptString tree;
    ScriptString_FromCString(&tree, pointer<const char>(0x007f4948));
    slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009225f0)(ent, &tree);  // GScr_UseAnimTree
    ScriptString_Release(&tree);
    BroScr_SetHealthByDifficulty(ent);
    create_thread(ent, thread_functor(0x0013f570, ent));  // BroScr_VehicleDeath_Jeep
    create_thread(ent, thread_functor(0x00128f90, ent));  // BroScr_VehicleTurretFireLoop_8
}

uint32_t* XB_CDECL BroScr_SpawnVehicleFromEnt(uint32_t* out, uint32_t ent, ScriptString name, uint32_t extra) {
    uint32_t buffer1[3], buffer2[3];
    const uint32_t* a = slot<GetVec>(0x00922ba0)(buffer1, ent);  // ScrEnt_GetAngles
    const uint32_t angles[3] = {a[0], a[1], a[2]};
    const uint32_t* o = slot<GetVec>(0x00922b20)(buffer2, ent);  // ScrEnt_GetOrigin
    const uint32_t origin[3] = {o[0], o[1], o[2]};
    ScriptString* const type = ScrField_vehicletype(ent);
    ScriptString* const model = model_field(ent);
    using Spawn = uint32_t XB_CDECL(ScriptString*, ScriptString*, ScriptString*, const uint32_t*, const uint32_t*, uint32_t);
    const uint32_t vehicle = slot<Spawn>(0x009222fc)(model, &name, type, origin, angles, extra);  // GScr_SpawnVehicle
    ScriptString* const source = ScrField_vehicletype(ent);
    ScriptString_Assign(ScrField_vehicletype(vehicle), source);
    *out = vehicle;
    ScriptString_Release(&name);
    return out;
}

uint32_t* XB_CDECL BroScr_SpawnScriptModelAt(uint32_t* out, uint32_t ent, ScriptString name, uint32_t extra) {
    ScriptString classname;
    ScriptString_FromCString(&classname, pointer<const char>(0x0083ab98));  // "script_model"
    uint32_t buffer[3];
    const uint32_t* o = slot<GetVec>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
    const uint32_t origin[3] = {o[0], o[1], o[2]};
    const uint32_t model_ent =
        slot<uint32_t XB_CDECL(ScriptString*, const uint32_t*, uint32_t)>(0x009222f0)(&classname, origin, extra);  // GScr_Spawn
    ScriptString_Release(&classname);
    ScriptString copy;
    ScriptString_Copy(&copy, &name);
    slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922b54)(model_ent, copy);  // ScrEnt_SetTargetname (releases its copy)
    ScriptString* const model = model_field(ent);
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(model_ent, model, extra);  // GScr_SetModel
    *out = model_ent;
    ScriptString_Release(&name);
    return out;
}

uint32_t* XB_CDECL BroScr_ReplaceWithVehicle(uint32_t* out, uint32_t ent, uint32_t extra) {
    ScriptString name;
    ScrCall_ScrEnt_GetTargetname(&ent, &name);
    uint32_t vehicle;
    BroScr_SpawnVehicleFromEnt(&vehicle, ent, name, extra);
    slot<void XB_CDECL(uint32_t)>(0x00922540)(ent);  // GScr_Delete
    *out = vehicle;
    return out;
}

uint32_t* XB_CDECL BroScr_ReplaceWithScriptModel(uint32_t* out, uint32_t ent, uint32_t extra) {
    ScriptString name;
    ScrCall_ScrEnt_GetTargetname(&ent, &name);
    uint32_t model_ent;
    BroScr_SpawnScriptModelAt(&model_ent, ent, name, extra);
    slot<void XB_CDECL(uint32_t)>(0x00922540)(ent);  // GScr_Delete
    *out = model_ent;
    return out;
}

uint32_t XB_CDECL BroScr_SetField7ab1fdf4_IsValid(uint32_t ent, uint32_t, uint32_t, uint32_t other) {
    *BroScr_FieldSlot(ent, 0x7ab1fdf4) = 1;
    return slot<uint32_t XB_CDECL(uint32_t)>(0x00922214)(other);  // Scr_EntityHandleIsValid
}

void XB_CDECL BroScr_StukaDropBombsThread(uint32_t a, uint32_t b) {
    ScriptString none;
    ScriptString_FromRep(&none, nullptr);
    // The fourth word is a stack temporary in the original (BroScr_StukaDropBombs reads three).
    title_function<void XB_CDECL(uint32_t, uint32_t, ScriptString, uint32_t)>(0x0014aaa0)(a, b, none, 0);  // BroScr_StukaDropBombs
}

namespace {

// The entity's field slot for `key`, added as 0 when missing (ScriptKeyTable_Set).
uint32_t* zero_slot(uint32_t ent, uint32_t key) {
    ScriptKeyTable* const table = BroScr_ScriptObjectOrEmpty(ent);
    if (uint32_t* value = ScriptKeyTable_Find(table, key)) return value;
    return ScriptKeyTable_Set(table, key, 0);
}

void use_anim_tree(uint32_t ent, uint32_t name) {
    ScriptString tree;
    ScriptString_FromCString(&tree, pointer<const char>(name));
    slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009225f0)(ent, &tree);  // GScr_UseAnimTree
    ScriptString_Release(&tree);
}

// The flakvierling anim tree and its three anim handles (fields 0x0038b194, 0xe0b632e1, 0xe0b632e2).
void flakvierling_anim_tree(uint32_t ent, uint32_t tree, uint32_t root, uint32_t firing, uint32_t third) {
    use_anim_tree(ent, tree);
    *zero_slot(ent, 0x0038b194) = title_global<uint32_t>(root);
    *zero_slot(ent, 0xe0b632e1) = title_global<uint32_t>(firing);
    *zero_slot(ent, 0xe0b632e2) = title_global<uint32_t>(third);
}

void willy_jeep_setup(uint32_t ent, bool health_thread) {
    use_anim_tree(ent, 0x007f4948);
    const int32_t health = slot<int32_t XB_CDECL(int32_t)>(0x0092222c)(1000) + 500;  // Scr_RandomInt
    set_health(ent, static_cast<uint32_t>(health));
    if (health_thread) create_thread(ent, thread_functor(0x0014d0a0, ent));  // BroScr_VehicleHealthThink
    create_thread(ent, thread_functor(0x0013f6f0, ent));                     // BroScr_VehicleInit_Jeep
    create_thread(ent, thread_functor(0x00128f30, ent));                     // BroScr_VehicleTurretFireLoop_7
    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f4a1c;
        functor[1] = 0x00134a90;  // BroScr_TankStunOnDamage
        functor[2] = ent;
        functor[3] = 1;
    }
    create_thread(ent, functor);
}

}  // namespace

void XB_CDECL BroScr_FlakvierlingAnimTree(uint32_t ent) {
    flakvierling_anim_tree(ent, 0x007f49c4, 0x008cd1b8, 0x008cd1c4, 0x008cd1d0);
}
void XB_CDECL BroScr_FlakvierlingBaseAnimTree(uint32_t ent) {
    flakvierling_anim_tree(ent, 0x007f49b0, 0x008cd4e8, 0x008cd4f4, 0x008cd500);
}

void XB_CDECL BroScr_TankAttachHullMg(uint32_t ent, ScriptString tag) {
    ScriptString weapon;
    ScriptString_FromCString(&weapon, pointer<const char>(0x007f77f4));
    ScriptString copy;
    ScriptString_Copy(&copy, &tag);
    title_function<void XB_CDECL(uint32_t, ScriptString, ScriptString)>(0x0013f190)(ent, copy, weapon);  // BroScr_TankAttachMgTurret
    ScriptString_Release(&tag);
}

void XB_CDECL BroScr_RocketExplosionThread(uint32_t ent, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6,
                                           ScriptString a7) {
    using Start = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                    const uint32_t*, const uint32_t*, const uint32_t*, const uint32_t*, const ScriptString*);
    title_function<Start>(0x0052cc30)(0, 0, 0, 0, 0x0013d650, ent, &a2, &a3, &a4, &a5, &a6, &a7);  // Scr_StartThread_ent_i_i_i_i_i_str
    ScriptString_Release(&a7);
}

void XB_CDECL BroScr_WillyJeepSetup(uint32_t ent) { willy_jeep_setup(ent, false); }
void XB_CDECL BroScr_WillyJeepSetup_2(uint32_t ent) { willy_jeep_setup(ent, true); }

uint32_t* XB_CDECL BroScr_CountAliveGunners(uint32_t* out, uint32_t ent) {
    using GetArray = uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t);
    using Resize = void XB_THISCALL(uint32_t*, uint32_t, uint32_t);
    // The gunner array field 0x2cd69a2a (8-byte elements; ScriptKeyTable_GetOrAddArray, DynArray_Resize8).
    auto element = [&](uint32_t i) {
        uint32_t* const array = title_function<GetArray>(0x0017c660)(BroScr_ScriptObjectOrEmpty(ent), 0x2cd69a2a);
        if (i >= array[2]) title_function<Resize>(0x0017bfb0)(array, i + 1, i + 4);
        return pointer<uint32_t>(array[0])[i * 2];
    };
    uint32_t count = 0;
    for (uint32_t i = 0; i < 3; ++i) {
        if (!slot<int32_t XB_CDECL(uint32_t)>(0x00922214)(element(i))) continue;  // Scr_EntityHandleIsValid
        if (slot<int32_t XB_CDECL(uint32_t)>(0x00922210)(element(i))) ++count;    // GScr_IsAlive
    }
    *out = count;
    return out;
}

void XB_CDECL BroScr_TurretSetAlliesTeam(uint32_t ent) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922584)(*zero_slot(ent, 0x2f68c7a5), 0x78da62f2);  // GScr_Turret_SetMode
    bool defined;
    ScrField_script_team_IsDefined(&defined, ent);
    using SetTeam = void XB_CDECL(uint32_t, ScriptString*);
    if (defined) {
        ScriptString* const team = ScrField_script_team(ent);
        slot<SetTeam>(0x00922598)(*zero_slot(ent, 0x2f68c7a5), team);  // GScr_Turret_SetTeam
        return;
    }
    ScriptString allies;
    ScriptString_FromCString(&allies, pointer<const char>(0x00832084));  // "allies"
    slot<SetTeam>(0x00922598)(*zero_slot(ent, 0x2f68c7a5), &allies);
    ScriptString_Release(&allies);
}

void XB_CDECL BroScr_SpawnWheelOrigins_2(uint32_t ent) {
    // (the original keeps the second tag name and both classnames in the argument slot)
    const uint32_t lane = title_global<uint32_t>(0x00922eb8);  // g_scrUndefinedVecLane
    using GetTagOrigin = void XB_CDECL(uint32_t, uint32_t*, ScriptString*);
    uint32_t right[3] = {lane, lane, lane};
    {
        ScriptString tag;
        ScriptString_FromCString(&tag, pointer<const char>(0x0083a400));  // "tag_wheel_back_right"
        slot<GetTagOrigin>(0x0092261c)(ent, right, &tag);  // GScr_GetTagOrigin
        ScriptString_Release(&tag);
    }
    uint32_t buffer[3];
    const uint32_t* a = slot<GetVec>(0x00922ba0)(buffer, ent);  // ScrEnt_GetAngles
    const uint32_t right_angles[3] = {a[0], a[1], a[2]};
    uint32_t left[3] = {lane, lane, lane};
    {
        ScriptString tag;
        ScriptString_FromCString(&tag, pointer<const char>(0x0083a418));  // "tag_wheel_back_left"
        slot<GetTagOrigin>(0x0092261c)(ent, left, &tag);
        ScriptString_Release(&tag);
    }
    a = slot<GetVec>(0x00922ba0)(buffer, ent);
    const uint32_t left_angles[3] = {a[0], a[1], a[2]};
    using Spawn = uint32_t XB_CDECL(ScriptString*, const uint32_t*, uint32_t);
    uint32_t spawned[2];
    const uint32_t* const origins[2] = {right, left};
    for (int i = 0; i < 2; ++i) {
        ScriptString classname;
        ScriptString_FromCString(&classname, pointer<const char>(0x0083ab88));  // "script_origin"
        spawned[i] = slot<Spawn>(0x009222f0)(&classname, origins[i], 0);  // GScr_Spawn
        ScriptString_Release(&classname);
    }
    const uint32_t* const angles[2] = {right_angles, left_angles};
    for (int i = 0; i < 2; ++i) {
        const ScrVec3 turned = {{bits_of(platform_addss(as_float(angles[i][0]), title_global<float>(0x0083eeac))),  // + 32
                                 angles[i][1], angles[i][2]}};
        slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922ba4)(spawned[i], turned);  // ScrEnt_SetAngles
    }
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009224fc)(spawned[0], ent);  // GScr_LinkTo
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009224fc)(spawned[1], ent);
    // BroScr_TrackWheelOrigin threads (functors {0x007f4a28, 0x00123250, ent, origin}): left, then right
    const uint32_t wheel_origins[2] = {spawned[1], spawned[0]};  // plain array: VC7.1 has no <initializer_list>
    for (const uint32_t origin : wheel_origins) {
        uint32_t* functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
        if (functor) {
            functor[0] = 0x007f4a28;
            functor[1] = 0x00123250;
            functor[2] = ent;
            functor[3] = origin;
        }
        create_thread(ent, functor);
    }
}

namespace {

// A handle field of `ent` (ScriptKeyTable_Find, else ScriptKeyTable_Set to 0).
uint32_t* handle_slot(uint32_t ent, uint32_t hash) {
    ScriptKeyTable* const table = BroScr_ScriptObjectOrEmpty(ent);
    if (uint32_t* value = ScriptKeyTable_Find(table, hash)) return value;
    return ScriptKeyTable_Set(table, hash, 0);
}
uint32_t* turret_slot(uint32_t ent) { return handle_slot(ent, 0x2f68c7a5); }

using SpawnTurret = uint32_t XB_CDECL(ScriptString*, const uint32_t*, ScriptString*, uint32_t);
using LinkToTag = void XB_CDECL(uint32_t, uint32_t, ScriptString*, const uint32_t*, const uint32_t*, uint32_t);

}  // namespace

void XB_CDECL BroScr_TankAttachMgTurret(uint32_t ent, ScriptString weapon, ScriptString tag) {
    const uint32_t zero[3] = {0, 0, 0};  // 0.0f
    if (!weapon.rep) {
        ScriptString default_weapon;
        ScriptString_FromCString(&default_weapon, pointer<const char>(0x008248d0));  // "mg42_panzerIV_tank"
        ScriptString classname;
        ScriptString_FromCString(&classname, pointer<const char>(0x00839e8c));  // "misc_turret"
        const uint32_t turret = slot<SpawnTurret>(0x00922300)(&classname, zero, &default_weapon, 0);  // GScr_SpawnTurret
        *turret_slot(ent) = turret;
        ScriptString_Release(&classname);
        ScriptString_Release(&default_weapon);
    } else {
        if (ScriptString_Equals(&weapon, pointer<const char>(0x007f7820))) {  // "no_turret"
            ScriptString_Release(&weapon);
            ScriptString_Release(&tag);
            return;
        }
        ScriptString classname;
        ScriptString_FromCString(&classname, pointer<const char>(0x00839e8c));  // "misc_turret"
        const uint32_t turret = slot<SpawnTurret>(0x00922300)(&classname, zero, &weapon, 0);
        *ScrField_mgturret(ent) = turret;
        ScriptString_Release(&classname);
    }
    bool defined;
    if (*ScrField_h629afc27_IsDefined(&defined, ent)) {
        const int32_t value = static_cast<int32_t>(*ScrField_h629afc27(ent));
        const uint32_t turret = *ScrField_mgturret(ent);
        slot<void XB_CDECL(uint32_t, float)>(0x009225a4)(turret, static_cast<float>(value));  // GScr_Turret_Set54
    }
    {
        ScriptString model;
        ScriptString_FromCString(&model, pointer<const char>(0x007f77e8));  // "v_gunturret"
        slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(*turret_slot(ent), &model, 0);  // GScr_SetModel
        ScriptString_Release(&model);
    }
    const uint32_t angles_offset[3] = {0, 0, 0}, origin_offset[3] = {0, 0, 0};
    slot<LinkToTag>(0x009224f4)(*turret_slot(ent), ent, &tag, origin_offset, angles_offset, 0);  // GScr_LinkToTagWithOffset
    uint32_t buffer[3];
    const uint32_t* const a = slot<GetVec>(0x00922ba0)(buffer, ent);  // ScrEnt_GetAngles
    const ScrVec3 angles = {{a[0], a[1], a[2]}};
    slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922ba4)(*turret_slot(ent), angles);  // ScrEnt_SetAngles
    const uint32_t turret = *turret_slot(ent);
    create_thread(turret, thread_functor(0x001992e0, turret));  // BroScr_MgBurstFire
    slot<void XB_CDECL(uint32_t)>(0x009225a0)(*turret_slot(ent));  // GScr_Turret_MakeUnusable
    BroScr_TurretSetAlliesTeam(ent);
    ScriptString_Release(&weapon);
    ScriptString_Release(&tag);
}

void XB_CDECL BroScr_PlaneSpawnWingTurrets(uint32_t ent, uint32_t bombs, uint32_t health, bool follow_path) {
    const uint32_t undefined = title_global<uint32_t>(0x00922ec0);
    if (health == undefined) {
        set_health(ent, 300);
        create_thread(ent, thread_functor(0x0013eb00, ent));  // BroScr_WingTurretDamageWatch_3
    } else {
        set_health(ent, health);
    }
    create_thread(ent, thread_functor(0x0014ca20, ent));  // BroScr_VehicleDeath_Stuka
    if (follow_path) {
        uint32_t* const functor = thread_functor(0x0014cbf0, ent);  // BroScr_VehicleFollowPath
        using BeginThreadEndOn = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*);
        slot<BeginThreadEndOn>(0x00922170)(0, 0, 0, ent, ent, 0x485de552, functor);  // Scr_BeginThreadEndOn
    }
    {
        ScriptKeyTable* const table = BroScr_ScriptObjectOrEmpty(ent);
        uint32_t* value = ScriptKeyTable_Find(table, 0x4af51227);
        if (!value) value = ScriptKeyTable_Set(table, 0x4af51227, undefined);
        *value = 0;
    }
    if (bombs != title_global<uint32_t>(0x00922ec0)) {
        uint32_t* functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
        if (functor) {
            functor[0] = 0x007f4a1c;
            functor[1] = 0x001522e0;  // BroScr_StukaDropBombsThread
            functor[2] = ent;
            functor[3] = bombs;
        }
        create_thread(ent, functor);
    }
    // (the original keeps its string temporaries in its argument slots)
    bool no_turrets = false;
    {
        ScriptString noteworthy;
        if (ScrCall_ScrEnt_GetScriptNoteworthy(&ent, &noteworthy)->rep) {
            ScriptString again;
            no_turrets = ScriptString_Equals(ScrCall_ScrEnt_GetScriptNoteworthy(&ent, &again),
                                             pointer<const char>(0x007f60b4));  // "noturrets"
            ScriptString_Release(&again);
        }
        ScriptString_Release(&noteworthy);
    }
    if (no_turrets) return;
    const uint32_t kLeft = 0xef6522f9, kRight = 0xeead8089;  // leftturret, rightturret
    const uint32_t kWeapons[2] = {0x00824d84, 0x008057f0};    // "stuka_wing_turret", "me109_wing_turret_nosound"
    const uint32_t kFields[2] = {kLeft, kRight};
    for (int i = 0; i < 2; ++i) {
        ScriptString weapon;
        ScriptString_FromCString(&weapon, pointer<const char>(kWeapons[i]));
        ScriptString classname;
        ScriptString_FromCString(&classname, pointer<const char>(0x00839e8c));  // "misc_turret"
        const uint32_t zero[3] = {0, 0, 0};  // 0.0f
        const uint32_t turret = slot<SpawnTurret>(0x00922300)(&classname, zero, &weapon, 0);  // GScr_SpawnTurret
        *handle_slot(ent, kFields[i]) = turret;
        ScriptString_Release(&classname);
        ScriptString_Release(&weapon);
    }
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922584)(*handle_slot(ent, kLeft), 0xdc26d84c);  // GScr_Turret_SetMode
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922584)(*handle_slot(ent, kRight), 0xdc26d84c);
    uint8_t* const zone_name = pointer<uint8_t>(title_global<uint32_t>(0x00922ee0) + 0x4b0);
    const uint32_t zone = *reinterpret_cast<uint32_t*>(zone_name)
                              ? slot<uint32_t XB_CDECL(uint8_t*)>(0x00922494)(zone_name)  // GScr_FindZone
                              : 0;
    // (both branches of the original's second zone-name test make these calls; zone is 0 without one)
    decltype(ScrField_leftturret) const fields[2] = {ScrField_leftturret, ScrField_rightturret};
    for (auto* field : fields) {
        ScriptString model;
        ScriptString_FromCString(&model, pointer<const char>(0x00824d78));  // "v_p47_guns"
        const uint32_t turret = *field(ent);
        slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(turret, &model, zone);  // GScr_SetModel
        ScriptString_Release(&model);
    }
    const uint32_t kTags[2] = {0x008057d4, 0x008057c4};  // "tag_gunLeft", "tag_gunRight"
    const uint32_t lane = title_global<uint32_t>(0x00922eb8);  // g_scrUndefinedVecLane
    for (int i = 0; i < 2; ++i) {
        ScriptString tag;
        ScriptString_FromCString(&tag, pointer<const char>(kTags[i]));
        uint32_t origin[3] = {lane, lane, lane};
        slot<void XB_CDECL(uint32_t, uint32_t*, ScriptString*)>(0x0092261c)(ent, origin, &tag);  // GScr_GetTagOrigin
        const ScrVec3 position = {{origin[0], origin[1], origin[2]}};
        slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922b24)(*handle_slot(ent, kFields[i]), position);  // ScrEnt_SetOrigin
        ScriptString_Release(&tag);
    }
    for (int i = 0; i < 2; ++i) {
        uint32_t buffer[3];
        const uint32_t* const a = slot<GetVec>(0x00922ba0)(buffer, ent);  // ScrEnt_GetAngles
        const ScrVec3 angles = {{a[0], a[1], a[2]}};
        slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922ba4)(*handle_slot(ent, kFields[i]), angles);  // ScrEnt_SetAngles
    }
    for (int i = 0; i < 2; ++i) {
        ScriptString tag;
        ScriptString_FromCString(&tag, pointer<const char>(kTags[i]));
        const uint32_t origin_offset[3] = {0, 0, 0}, angles_offset[3] = {0, 0, 0};
        slot<LinkToTag>(0x009224f4)(*handle_slot(ent, kFields[i]), ent, &tag, origin_offset, angles_offset, 0);  // GScr_LinkToTagWithOffset
        ScriptString_Release(&tag);
    }
}

namespace {

DynArray* table_array(uint32_t ent, uint32_t key) {
    using GetArray = DynArray* XB_THISCALL(ScriptKeyTable*, uint32_t);
    return title_function<GetArray>(0x0045cd50)(BroScr_ScriptObjectOrEmpty(ent), key);  // ScriptKeyTable_GetArray
}

// The array under `key` through ScriptKeyTable_Find, else ScriptKeyTable_SetArray with an empty one.
DynArray* table_array_or_add(uint32_t ent, uint32_t key) {
    ScriptKeyTable* const table = BroScr_ScriptObjectOrEmpty(ent);
    uint32_t* value = ScriptKeyTable_Find(table, key);
    if (!value) {
        DynArray empty = {};
        using SetArray = uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t, DynArray*);
        value = title_function<SetArray>(0x0045cc80)(table, key, &empty);
    }
    return pointer<DynArray>(*value);
}

// Element `i`, the array grown to i + 1 (capacity i + 4) first when it is too short.
uint32_t* grow_at(DynArray* array, uint32_t i) {
    if (i >= array->size) DynArray_Resize(reinterpret_cast<uint8_t*>(array), i + 1, i + 4);
    return &array->data[i];
}

uint32_t* int_slot(uint32_t ent, uint32_t hash) {
    ScriptKeyTable* const table = BroScr_ScriptObjectOrEmpty(ent);
    if (uint32_t* value = ScriptKeyTable_Find(table, hash)) return value;
    return ScriptKeyTable_Set(table, hash, title_global<uint32_t>(0x00922ec0));
}

}  // namespace

void XB_CDECL BroScr_StukaDropBombs(uint32_t ent, uint32_t count, ScriptString model) {
    const uint32_t kBombs = 0x0030b1c2, kCount = 0x380a75c4;
    *int_slot(ent, kCount) = count;
    DynArray_Resize(reinterpret_cast<uint8_t*>(table_array(ent, kBombs)), 0, 0);
    ScriptString unused;
    ScriptString_FromRep(&unused, nullptr);
    int32_t mount = static_cast<int32_t>(title_global<uint32_t>(0x00922ec0));  // bomb tag rotation, undefined
    uint8_t* const zone_name = pointer<uint8_t>(title_global<uint32_t>(0x00922ee0) + 0x358);
    const uint32_t zone =
        *reinterpret_cast<uint32_t*>(zone_name) ? slot<uint32_t XB_CDECL(uint8_t*)>(0x00922494)(zone_name) : 0;  // GScr_FindZone
    const auto has_zone_name = []() { return *pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) + 0x358) != 0; };
    const uint32_t lane = title_global<uint32_t>(0x00922eb8);  // g_scrUndefinedVecLane
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(*int_slot(ent, kCount)); ++i) {
        {
            ScriptString classname;
            ScriptString_FromCString(&classname, pointer<const char>(0x0083ab98));  // "script_model"
            uint32_t buffer[3];
            const uint32_t* const o = slot<GetVec>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
            const uint32_t origin[3] = {o[0], o[1], o[2]};
            const uint32_t bomb =
                slot<uint32_t XB_CDECL(ScriptString*, const uint32_t*, uint32_t)>(0x009222f0)(&classname, origin, 0);  // GScr_Spawn
            *grow_at(table_array_or_add(ent, kBombs), i) = bomb;
            ScriptString_Release(&classname);
        }
        using SetModel = void XB_CDECL(uint32_t, ScriptString*, uint32_t);
        if (!model.rep) {
            ScriptString name;
            ScriptString_FromCString(&name, pointer<const char>(0x007f766c));  // "stuka_bomb"
            const uint32_t bomb_zone = has_zone_name() ? zone : 0;
            slot<SetModel>(0x00922548)(*grow_at(table_array(ent, kBombs), i), &name, bomb_zone);  // GScr_SetModel
            ScriptString_Release(&name);
        } else {
            const uint32_t bomb_zone = has_zone_name() ? zone : 0;
            slot<SetModel>(0x00922548)(*grow_at(table_array(ent, kBombs), i), &model, bomb_zone);
        }
        *int_slot(*grow_at(table_array_or_add(ent, kBombs), i), 0xe2c9ccdd) = 0;
        // "stuka", "stuka_flying", "stuka_prologue": the model strings stay alive until all checks are done,
        // released last to first (the string pool's reuse order)
        bool stuka;
        {
            ScriptString first, second, third;
            uint32_t held = 1;
            stuka = ScriptString_Equals(ScrCall_ScrEnt_GetModel(&ent, &first), pointer<const char>(0x007f4970));
            if (!stuka) {
                held |= 2;
                stuka = ScriptString_Equals(ScrCall_ScrEnt_GetModel(&ent, &second), pointer<const char>(0x007f8db8));
                if (!stuka) {
                    held |= 4;
                    stuka = ScriptString_Equals(ScrCall_ScrEnt_GetModel(&ent, &third), pointer<const char>(0x007f60c0));
                }
            }
            if (held & 4) ScriptString_Release(&third);
            if (held & 2) ScriptString_Release(&second);
            ScriptString_Release(&first);
        }
        if (!stuka) {
            uint32_t buffer[3];
            const uint32_t* v = slot<GetVec>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
            const ScrVec3 origin = {{v[0], v[1], v[2]}};
            slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922b24)(*grow_at(table_array(ent, kBombs), i), origin);  // ScrEnt_SetOrigin
            v = slot<GetVec>(0x00922ba0)(buffer, ent);  // ScrEnt_GetAngles
            const ScrVec3 angles = {{v[0], v[1], v[2]}};
            slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922ba4)(*grow_at(table_array(ent, kBombs), i), angles);  // ScrEnt_SetAngles
            slot<void XB_CDECL(uint32_t, uint32_t)>(0x009224fc)(*grow_at(table_array(ent, kBombs), i), ent);  // GScr_LinkTo
            continue;
        }
        if (static_cast<uint32_t>(mount) == title_global<uint32_t>(0x00922ec0) || mount > 3) mount = 1;
        // "tag_bombleft", "tag_bombright", "tag_bombmid"
        const uint32_t tag_name = mount == 1 ? 0x007f87e4 : mount == 2 ? 0x007f87f4 : 0x00824c7c;
        using GetTagVec = void XB_CDECL(uint32_t, uint32_t*, ScriptString*);
        {
            ScriptString tag;
            ScriptString_FromCString(&tag, pointer<const char>(tag_name));
            uint32_t v[3] = {lane, lane, lane};
            slot<GetTagVec>(0x0092261c)(ent, v, &tag);  // GScr_GetTagOrigin
            const ScrVec3 origin = {{v[0], v[1], v[2]}};
            slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922b24)(*grow_at(table_array(ent, kBombs), i), origin);
            ScriptString_Release(&tag);
        }
        {
            ScriptString tag;
            ScriptString_FromCString(&tag, pointer<const char>(tag_name));
            uint32_t v[3] = {lane, lane, lane};
            slot<GetTagVec>(0x00922620)(ent, v, &tag);  // GScr_GetTagAngles
            const ScrVec3 angles = {{v[0], v[1], v[2]}};
            slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922ba4)(*grow_at(table_array(ent, kBombs), i), angles);
            ScriptString_Release(&tag);
        }
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x009224fc)(*grow_at(table_array(ent, kBombs), i), ent);  // GScr_LinkTo
        ++mount;
    }
    ScriptString_Release(&unused);
    ScriptString_Release(&model);
}

namespace {

// The entity's float field 0x35ee66af (added as undefined), scaled by the float at `factor`.
float scale_field(uint32_t ent, uint32_t factor) {
    ScriptKeyTable* const table = BroScr_ScriptObjectOrEmpty(ent);
    uint32_t* value = ScriptKeyTable_Find(table, 0x35ee66af);
    if (!value) value = ScriptKeyTable_Set(table, 0x35ee66af, title_global<uint32_t>(0x00922eac));
    return platform_mulss(as_float(*value), title_global<float>(factor));
}

struct Vec {
    float x, y, z;
};

// BroScr_VectorScale (0x00182270, the other list's helper; called at its title address).
Vec vector_scale(const Vec& v, float scale) {
    float out[3];
    const float* const r = title_function<float* XB_CDECL(float*, float, float, float, float)>(0x00182270)(out, v.x, v.y, v.z,
                                                                                                         scale);
    return {r[0], r[1], r[2]};
}

Vec origin_of(uint32_t ent) {
    uint32_t buffer[3];
    const uint32_t* const o = slot<GetVec>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
    return {as_float(o[0]), as_float(o[1]), as_float(o[2])};
}

void set_origin(uint32_t ent, const Vec& v) {
    const ScrVec3 position = {{bits_of(v.x), bits_of(v.y), bits_of(v.z)}};
    slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922b24)(ent, position);  // ScrEnt_SetOrigin
}

Vec add(const Vec& a, const Vec& b) {
    return {platform_addss(a.x, b.x), platform_addss(a.y, b.y), platform_addss(a.z, b.z)};
}
Vec sub(const Vec& a, const Vec& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }

// Scr_AnglesToRight / Scr_AnglesToForward of `ent`'s angles (the result starts as undefined lanes).
Vec angles_to(uint32_t interface_slot, uint32_t ent) {
    uint32_t buffer[3];
    const uint32_t* const a = slot<GetVec>(0x00922ba0)(buffer, ent);  // ScrEnt_GetAngles
    const uint32_t angles[3] = {a[0], a[1], a[2]};
    const uint32_t lane = title_global<uint32_t>(0x00922eb8);  // g_scrUndefinedVecLane
    uint32_t out[3] = {lane, lane, lane};
    slot<void XB_CDECL(uint32_t*, const uint32_t*)>(interface_slot)(out, angles);
    return {as_float(out[0]), as_float(out[1]), as_float(out[2])};
}

// Element `i` of the array under `key` (ScriptKeyTable_GetArray) = `ent`, grown to i + 1 first.
void store_at(uint32_t owner, uint32_t key, uint32_t i, uint32_t ent) { *grow_at(table_array(owner, key), i) = ent; }

// GScr_LinkTo(each entity of `owner`'s array `key`, owner); the array is looked up through
// ScriptKeyTable_Find / ScriptKeyTable_SetArray for the count and again for each element.
void link_all(uint32_t owner, uint32_t key) {
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(table_array_or_add(owner, key)->size); ++i)
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x009224fc)(*grow_at(table_array_or_add(owner, key), i), owner);  // GScr_LinkTo
}

}  // namespace

void XB_CDECL BroScr_VehicleHealthThink(uint32_t ent) {
    bool defined;
    // defaults: field 0x515816fa -0.927, 0xa22fa6ba 400, 0xbe149b8c 22, 0x8166d37c 240, 0x35ee66af 1
    if (!*ScrField_h515816fa_IsDefined(&defined, ent)) *ScrField_h515816fa(ent) = title_global<uint32_t>(0x00824d30);
    if (!*ScrField_ha22fa6ba_IsDefined(&defined, ent)) *ScrField_ha22fa6ba(ent) = title_global<uint32_t>(0x0083f11c);
    if (!*ScrField_hbe149b8c_IsDefined(&defined, ent)) *ScrField_hbe149b8c(ent) = title_global<uint32_t>(0x0083edd4);
    if (!*ScrField_h8166d37c_IsDefined(&defined, ent)) *ScrField_h8166d37c(ent) = title_global<uint32_t>(0x0083eed4);
    if (!*ScrField_h35ee66af_IsDefined(&defined, ent)) *ScrField_h35ee66af(ent) = title_global<uint32_t>(0x0083eb20);
    {
        // appended to the level's vehicle list at +0x4e0
        DynArray* const vehicles = pointer<DynArray>(title_global<uint32_t>(0x00922ee0) + 0x4e0);
        *grow_at(vehicles, vehicles->size) = ent;
    }
    const Vec right = angles_to(0x0092228c, ent);  // Scr_AnglesToRight
    const Vec side = vector_scale(right, scale_field(ent, 0x007e5bf0));  // * 64
    uint32_t points[7];
    for (uint32_t& point : points) {
        ScriptString classname;
        ScriptString_FromCString(&classname, pointer<const char>(0x0083ab88));  // "script_origin"
        const float one = title_global<float>(0x0083eb20);
        const uint32_t start[3] = {bits_of(one), bits_of(one), bits_of(one)};
        point = slot<uint32_t XB_CDECL(ScriptString*, const uint32_t*, uint32_t)>(0x009222f0)(&classname, start, 0);  // GScr_Spawn
        ScriptString_Release(&classname);
    }
    set_origin(points[0], add(origin_of(ent), side));
    set_origin(points[1], sub(origin_of(ent), side));
    const Vec forward = angles_to(0x00922290, ent);  // Scr_AnglesToForward
    const Vec ahead = vector_scale(forward, scale_field(ent, 0x007e5bf4));  // * 96
    const Vec reach1 = vector_scale(forward, scale_field(ent, 0x00824d2c));  // * 84
    const Vec reach2 = vector_scale(forward, scale_field(ent, 0x00824d2c));
    set_origin(points[5], add(origin_of(ent), reach1));
    set_origin(points[6], sub(origin_of(ent), reach2));
    set_origin(points[0], add(origin_of(points[0]), ahead));
    set_origin(points[1], add(origin_of(points[1]), ahead));
    const Vec back = vector_scale(ahead, title_global<float>(0x0083eba8));  // * 2
    set_origin(points[2], sub(origin_of(points[0]), back));
    set_origin(points[3], sub(origin_of(points[1]), back));
    set_origin(points[4], add(origin_of(ent), ahead));
    const uint32_t kCorners = 0x9ecce58a, kEnds = 0xb0996bdc;
    for (uint32_t i = 0; i < 5; ++i) store_at(ent, kCorners, i, points[i]);
    store_at(ent, kEnds, 0, points[5]);
    store_at(ent, kEnds, 1, points[6]);
    *handle_slot(ent, 0xcc043125) = points[4];
    link_all(ent, kEnds);
    link_all(ent, kCorners);
    const uint32_t level = title_global<uint32_t>(0x00922ee0);
    const uint32_t level_ent = *pointer<uint32_t>(level ? level + 4 : 0);
    uint32_t* functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f4a28;
        functor[1] = 0x0014ce80;  // BroScr_DeleteArraysOnVehicleRemoved
        functor[2] = level_ent;
        functor[3] = ent;
    }
    create_thread(level_ent, functor);
}

}  // namespace game
