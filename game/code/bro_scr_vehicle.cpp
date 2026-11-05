#include "bro_scr_vehicle.h"

#include "bro_scr_util.h"
#include "g_scr_runtime.h"
#include "g_script.h"
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

}  // namespace game
