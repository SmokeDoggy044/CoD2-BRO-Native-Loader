#include "levels_prologue_events.h"

#include "bro_scr_ai_common.h"
#include "g_scr_runtime.h"
#include "g_script.h"
#include "levels_twins.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "scr_access.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "script_string.h"
#include "title_data.h"
#include "bro_scr_tank_init.h"
#include "levels_prologue_bodies_a.h"
#include "levels_templates.h"
#include "g_scr_builtins.h"
#include "gap_methods.h"

namespace game {

using namespace bro_ai;

namespace {

uint32_t level() { return title_global<uint32_t>(0x00921cbc); }
uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
// The level's own entity: the word at +4 of the level block (a null block reads address 4).
const uint32_t* level_ent() { return level() ? pointer<uint32_t>(level() + 4) : nullptr; }

using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, ...);
using CreateThread = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);
using Reserve = void XB_THISCALL(DynArray*, uint32_t, uint32_t);
using IndexString = ScriptString* XB_THISCALL(uint8_t*, const uint32_t*);
using Find = void XB_THISCALL(HashMap*, uint32_t*, const uint32_t*);
using GetVec = const uint32_t* XB_CDECL(uint32_t*, uint32_t);
using Spawn = uint32_t XB_CDECL(ScriptString*, const uint32_t*, uint32_t);
using PlaySound = void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t);
using GetNode = uint32_t XB_CDECL(ScriptString*, ScriptString*, uint32_t, uint32_t);

constexpr uint32_t kStartEntF = 0x0045a920;      // Scr_StartThread_ent_f_PROLOGUE

void create_thread(uint32_t ent, uint32_t* functor) {
    slot<CreateThread>(0x0092216c)(0, 0, 0, ent, functor, 0);  // Scr_CreateThread
}
uint32_t* alloc(uint32_t bytes) { return slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(bytes); }  // ScrIface_Alloc

// level+0x124 [speaker][line] = text (HashMap_Index_PROLOGUE_7, then HashMap_Index_PROLOGUE_11).
void set_dialogue(uint32_t speaker, uint32_t line, uint32_t text) {
    uint32_t keys[2] = {line, speaker};
    uint8_t* const lines = HashMap_Index_PROLOGUE_7(pointer<HashMap>(level() + 0x124), &keys[1]);
    ScriptString_AssignCString(title_function<IndexString>(0x00461120)(lines, &keys[0]), pointer<const char>(text));
}

// The level's anim lists (+0x744): list[outer][inner][i] = the anim handle at `anim` (the list resized to
// i + 1, i + 4 first when it is shorter: DynArray_ReserveInt0_PROLOGUE).
void set_anim(uint32_t outer, uint32_t inner, uint32_t i, uint32_t anim) {
    uint32_t keys[2] = {outer, inner};
    uint8_t* const lists = HashMap_Index_PROLOGUE_10(pointer<HashMap>(level() + 0x744), &keys[0]);
    DynArray* const list = reinterpret_cast<DynArray*>(HashMap_Index_PROLOGUE(reinterpret_cast<HashMap*>(lists), &keys[1]));
    if (list->size <= i) title_function<Reserve>(0x0045d480)(list, i + 1, i + 4);
    list->data[i] = title_global<uint32_t>(anim);
}

// map[key] = value for an int-keyed map node (HashMap_Find, inserted as {key, missing} when absent).
void map_set(HashMap* map, uint32_t key, uint32_t missing, uint32_t anim) {
    uint32_t it[2];
    uint32_t pair[2] = {key, missing};
    own_function<Find>(&HashMap_Find)(map, &it[0], &pair[0]);  // HashMap_Find_PROLOGUE
    uint32_t node = it[0];
    if (node == *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(map) + 8))  // end
        node = *reinterpret_cast<uint32_t*>(HashMap_Insert_PROLOGUE_2(map, reinterpret_cast<uint8_t*>(it), &pair[0]));
    pointer<uint32_t>(node)[3] = title_global<uint32_t>(anim);
}

// level+offset [outer][inner] = text (HashMap_Index_PROLOGUE_9, then HashMap_Index_PROLOGUE_12).
void set_name(uint32_t offset, uint32_t outer, uint32_t inner, uint32_t text) {
    using Index = ScriptString* XB_THISCALL(uint8_t*, const uint32_t*);
    uint32_t keys[2] = {outer, inner};
    uint8_t* const names = HashMap_Index_PROLOGUE_9(pointer<HashMap>(level() + offset), &keys[0]);
    ScriptString_AssignCString(title_function<Index>(0x00461230)(names, &keys[1]), pointer<const char>(text));
}

// The drone list 0x0fc1193c (BroLvl_prologue_SetupDrones, BroLvl_prologue_SetDroneRunAnims).
void drone_run_anims() {
    set_anim(0x658f814a, 0x0fc1193c, 0, 0x008c5890);
    set_anim(0x658f814a, 0x0fc1193c, 1, 0x008c5860);
    set_anim(0x658f814a, 0x0fc1193c, 2, 0x008c586c);
    set_anim(0x658f814a, 0x0fc1193c, 3, 0x008c5878);
    set_anim(0x658f814a, 0x0fc1193c, 4, 0x008c5884);
}

// The FUN_0043d4f0 thread on `tank` with (a, b): functor {0x007f4a64, body, tank, a, b}.
void target_thread(uint32_t tank, uint32_t a, uint32_t b) {
    uint32_t* const functor = alloc(0x14);
    if (functor) {
        functor[0] = 0x007f4a64;
        functor[1] = own_address(&BroLvl_prologue_TankFireAtTargets);
        functor[2] = tank;
        functor[3] = a;
        functor[4] = b;
    }
    create_thread(tank, functor);
}

void tank_targets(uint32_t tank, const DynArray* targets) {
    DynArray copy = {nullptr, 0, 0};
    DynArray_Assign4_PROLOGUE(&copy, targets);
    BroLvl_prologue_AddTankTargets(tank, copy);
}

// One hilltop Sherman (BroLvl_prologue_Event3HilltopShermans).
void hilltop_sherman(uint32_t path, uint32_t name, uint32_t health, uint32_t targets_name, DynArray* targets) {
    uint32_t node;
    {
        ScriptString key;
        ScriptString_FromCString(&key, pointer<const char>(0x0082dda8));  // "targetname"
        ScriptString value;
        ScriptString_FromCString(&value, pointer<const char>(path));
        node = slot<GetNode>(0x009221d0)(&value, &key, 0, 0);  // GScr_GetVehicleNode
        ScriptString_Release(&value);
        ScriptString_Release(&key);
    }
    uint32_t tank;
    {
        ScriptString type;
        ScriptString_FromCString(&type, pointer<const char>(0x007f7930));  // "ShermanTank"
        ScriptString targetname;
        ScriptString_FromCString(&targetname, pointer<const char>(name));
        ScriptString model;
        ScriptString_FromCString(&model, pointer<const char>(0x007f790c));  // "v_sherman"
        uint32_t origin[3] = {0, 0, 0};
        uint32_t angles[3] = {0, 0, 0};
        using SpawnVehicle = uint32_t XB_CDECL(ScriptString*, ScriptString*, ScriptString*, uint32_t*, uint32_t*, uint32_t);
        tank = slot<SpawnVehicle>(0x009222fc)(&model, &targetname, &type, origin, angles, 0);  // GScr_SpawnVehicle
        ScriptString_Release(&model);
        ScriptString_Release(&targetname);
        ScriptString_Release(&type);
    }
    ScriptString_AssignCString(ScrField_vehicletype(tank), pointer<const char>(0x007f7930));  // "ShermanTank"
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922b94)(tank, health);  // ScrEnt_SetHealth
    {
        ScriptString arg;
        ScriptString_FromCString(&arg, pointer<const char>(0x007f7820));  // "no_turret"
        own_function<StartThread>(&Scr_StartThread_ent_str_PROLOGUE_entry)(0, 0, 0, 0, own_address(&BroScr_TankInit_2), tank, &arg);  // BroScr_TankInit_2
        ScriptString_Release(&arg);
    }
    slot<void XB_CDECL(uint32_t, uint32_t*)>(0x0092287c)(tank, &node);  // GScr_Vehicle_AttachPath
    {
        ScriptString text;
        ScriptString_FromCString(&text, pointer<const char>(targets_name));
        Scr_GetEntArray(address_of(&text), 0x0d786b4c, reinterpret_cast<uint8_t*>(targets), 0);  // targetname
        ScriptString_Release(&text);
    }
    tank_targets(tank, targets);
    target_thread(tank, title_global<uint32_t>(0x0083eef8), title_global<uint32_t>(0x0083ef10));  // 9.0, 15.0
}

// Scr_RandomFloat(spread) added to base on the x87, the sum stored as the wait.
void wait_random(float base, float spread) {
    const long double r = slot<long double XB_CDECL(float)>(0x00922230)(spread);  // Scr_RandomFloat
    Scr_Wait(static_cast<float>(static_cast<long double>(base) + r));
}

uint32_t spawn_origin() {
    ScriptString classname;
    ScriptString_FromCString(&classname, pointer<const char>(0x0083ab88));  // "script_origin"
    const uint32_t at[3] = {0, 0, 0};
    const uint32_t ent = slot<Spawn>(0x009222f0)(&classname, at, 0);  // GScr_Spawn
    ScriptString_Release(&classname);
    return ent;
}

void play_level_sound(uint32_t ent, const uint32_t* sound) {
    ScriptString* const name = title_function<IndexString>(0x00461120)(pointer<uint8_t>(level() + 0x57c), sound);
    slot<PlaySound>(0x009221dc)(ent, name, 0, 0, 0);  // GScr_PlaySound
}

int32_t random_range(float min, float max) {
    return slot<int32_t XB_CDECL(int32_t, int32_t)>(0x00922234)(static_cast<int32_t>(min),
                                                                static_cast<int32_t>(max));  // Scr_RandomIntRange
}

void set_origin(uint32_t ent, const uint32_t* v) {
    const ScrVec3 at = {{v[0], v[1], v[2]}};
    slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922b24)(ent, at);  // ScrEnt_SetOrigin
}

uint32_t* spawn_vehicle_drone(uint32_t* out, uint32_t a2, uint32_t a3, ScriptString* name, uint32_t a5, uint32_t a6) {
    const uint32_t flag = title_global<uint8_t>(0x00922ed0);  // g_scrUndefinedBool
    ScriptString empty;
    ScriptString_Copy(&empty, &title_global<ScriptString>(0x00922e90));
    ScriptString copy;
    ScriptString_Copy(&copy, name);
    using SpawnDrone = void XB_CDECL(uint32_t*, uint32_t, uint32_t, ScriptString, uint32_t, uint32_t, ScriptString,
                                     uint32_t, uint32_t, uint32_t);
    uint32_t result;  // the original uses an argument slot
    title_function<SpawnDrone>(0x0044e120)(&result, a2, a3, copy, a5, a6, empty, title_global<uint32_t>(0x00922ea4),
                                           flag, flag);  // BroLvl_prologue_SpawnVehicleDrone
    *out = result;
    ScriptString_Release(name);
    return out;
}

// The value-type lookup's four out words, each written when its pointer is given.
void types(uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d, uint32_t va, uint32_t vb, uint32_t vc, uint32_t vd) {
    if (a) *a = va;
    if (b) *b = vb;
    if (c) *c = vc;
    if (d) *d = vd;
}

// ScrField_tank_firing_IsDefined_PROLOGUE (0x00422fd0, out in ESI in the title): the int field
// 0x0b74e4e8 holds a value other than the undefined int; false for an invalid handle / no script object.
bool tank_firing_defined(uint32_t tank) {
    if (!entity_handle_is_valid(tank)) return false;
    ScriptKeyTable* const table = own_function<ScriptKeyTable* XB_CDECL(uint32_t)>(&ScrCall_GScr_GetEntScriptObject)(tank);
    if (!table) return false;
    const uint32_t undefined = undefined_int();
    const uint32_t* const value = ScriptKeyTable_Find(table, 0x0b74e4e8);
    return (value ? *value : undefined) != undefined;
}

// ScrField_tank_targets_IsDefined_PROLOGUE (0x00432d30): a copy of the array field 0xfe9bb316
// (ScriptKeyTable_GetArrayCopy) has storage; the copy is freed.
bool tank_targets_defined(uint32_t tank) {
    if (!entity_handle_is_valid(tank)) return false;
    ScriptKeyTable* const table = own_function<ScriptKeyTable* XB_CDECL(uint32_t)>(&ScrCall_GScr_GetEntScriptObject)(tank);
    if (!table) return false;
    uint32_t copy[3];
    const uint32_t* const array = ScriptKeyTable_GetArrayCopy(table, copy, 0xfe9bb316);
    const bool defined = array[1] != 0;
    if (copy[0]) Scr_Free(pointer<void>(copy[0]));
    return defined;
}

}  // namespace

void XB_CDECL BroLvl_prologue_StartEntArrayDefault(uint32_t self, ScriptString name) {
    ScriptString empty;
    ScriptString_Copy(&empty, &title_global<ScriptString>(0x00922e90));
    ScriptString copy;
    ScriptString_Copy(&copy, &name);
    title_function<void XB_CDECL(uint32_t, ScriptString, ScriptString)>(0x00437bb0)(self, copy, empty);  // FUN_00437bb0
    ScriptString_Release(&name);
}

void XB_CDECL BroLvl_prologue_TankFireAtTargets(uint32_t tank, float a, float b) {
    using EndOn = void XB_CDECL(uint32_t, uint32_t);
    slot<EndOn>(0x00922188)(tank, 0x06730ae8);  // Scr_EndOn death
    slot<EndOn>(0x00922188)(tank, 0x318562c7);
    if (!tank_firing_defined(tank)) *int_slot(tank, 0x0b74e4e8) = 1;  // (the result byte goes over the `tank` argument slot)
    using GetArray = DynArray* XB_THISCALL(ScriptKeyTable*, uint32_t);
    for (;;) {
        while (*int_slot(tank, 0x0b74e4e8) == 0) Scr_Wait(as_float(0x3e800000));  // 0.25
        if (!tank_targets_defined(tank)) return;
        if (static_cast<int32_t>(own_function<GetArray>(&ScriptKeyTable_GetArray)(ent_script_object(tank), 0xfe9bb316)->size) < 1) return;
        own_function<void XB_CDECL(DynArray*)>(&DynArray_Shuffle)(  // DynArray_Shuffle
            own_function<GetArray>(&ScriptKeyTable_GetArray)(ent_script_object(tank), 0xfe9bb316));
        for (uint32_t i = 0;
             static_cast<int32_t>(i) <
             static_cast<int32_t>(own_function<GetArray>(&ScriptKeyTable_GetArray)(ent_script_object(tank), 0xfe9bb316)->size);
             ++i) {
            DynArray* const targets = own_function<GetArray>(&ScriptKeyTable_GetArray)(ent_script_object(tank), 0xfe9bb316);
            if (i >= targets->size) DynArray_Resize(reinterpret_cast<uint8_t*>(targets), i + 1, i + 4);
            title_function<StartThread>(0x0045a850)(0, 0, 0, 0, 0x00432c90, tank, targets->data + i, &a,
                                                    &b);  // Scr_StartThread_ent_ent_f_f_PROLOGUE
            Scr_WaitTill(tank, 0xf931a702);
            while (*int_slot(tank, 0x0b74e4e8) == 0) Scr_Wait(as_float(0x3e800000));  // 0.25
        }
    }
}

void XB_CDECL BroLvl_prologue_AddTankTargets(uint32_t tank, DynArray targets) {
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(targets.size); ++i) {
        if (i >= targets.size) DynArray_Resize(reinterpret_cast<uint8_t*>(&targets), i + 1, i + 4);
        uint32_t target = targets.data[i];
        ScriptKeyTable* const table = ent_script_object(tank);
        uint32_t* value = ScriptKeyTable_Find(table, 0xfe9bb316);
        if (!value) {
            const uint32_t empty[3] = {0, 0, 0};
            value = ScriptKeyTable_SetArray(table, 0xfe9bb316, empty);
        }
        DynArray_PushBack(pointer<uint8_t>(*value), &target);
    }
    if (targets.data) Scr_Free(targets.data);
}

void XB_CDECL LevelScript_prologue_ValueTypeLookup(uint32_t key, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d) {
    switch (key) {
    case 0x0001b227: case 0x19157d61: case 0x6ff72745: case 0x7aeb941a: case 0x846e4eea: case 0x8b05303f:
    case 0xb217897f: case 0xb9766e80: case 0xcc045c84: case 0xd91b375d: case 0xe2e8d8a5:
        types(a, b, c, d, 0, 0x00458e40, 0, 0x00458f20);
        return;
    case 0x0030b1c2: case 0x00325b53: case 0x05c4ceb7: case 0x0605894e: case 0x4ced49f6: case 0x8969d6d6:
    case 0x9ecce58a: case 0xb0996bdc: case 0xc5121aee: case 0xc548bcb7: case 0xdeca7f50: case 0xe104fd5c:
    case 0xe2a39a33: case 0xef260d98: case 0xfe9bb316:
        types(a, b, c, d, 0, own_address(&DynArray_NewCopy_PROLOGUE), 0, 0x0045e330);  // FUN_0045cfd0, FUN_0045e330
        return;
    case 0x00339844: case 0x0718b2c3: case 0x0aa8c328: case 0x0aff1238: case 0x0fc14a52: case 0x14b95ada:
    case 0x185dadca: case 0x18b0887b: case 0x19738af8: case 0x19a3d654: case 0x1e46f957: case 0x1ea97196:
    case 0x217d7784: case 0x262b2a4b: case 0x2cdbaad4: case 0x2eecefb8: case 0x34314719: case 0x552cfcab:
    case 0x602384d4: case 0x75c85f71: case 0x788065eb: case 0x80bdb846: case 0x83ccaf2a: case 0x871acd84:
    case 0x88260b0d: case 0x8903ef3c: case 0x98d8d171: case 0xa287dc52: case 0xaa3cddde: case 0xac835695:
    case 0xb2f8556f: case 0xbe944753: case 0xc0bb2676: case 0xc0f54bd1: case 0xc0f54bd2: case 0xc0f54bd3:
    case 0xc0f54bd4: case 0xc54b0523: case 0xd57e151d: case 0xd8e13e71: case 0xdbc2aa3c: case 0xdbcf23c0:
    case 0xddfa2c42: case 0xdedbf624: case 0xe29137e5: case 0xec299128: case 0xed6f4ccc: case 0xf1fea246:
        types(a, b, c, d, 0, 0x00458e20, 0, 0x00458ec0);
        return;
    case 0x00382b25: case 0x026e8e55: case 0x02d62a20: case 0x02f1dfc6: case 0x03375c37: case 0x05903940:
    case 0x05a82fed: case 0x06f63273: case 0x070b8a34: case 0x086550be: case 0x0881aad6: case 0x0b6525df:
    case 0x0b74e4e8: case 0x0bc3efc4: case 0x0bfd0e20: case 0x0e22c955: case 0x0e297820: case 0x0e625b35:
    case 0x0f865995: case 0x0feddbea: case 0x147beb00: case 0x16246d58: case 0x166cb1df: case 0x1751689c:
    case 0x185cac5b: case 0x18cd6af8: case 0x1aa720d8: case 0x1ccaaaf3: case 0x1ee3f374: case 0x20b11efc:
    case 0x2263ce3d: case 0x2266f963: case 0x227464a0: case 0x257acba4: case 0x2663b762: case 0x27ed5a85:
    case 0x27f6bbd8: case 0x2861269a: case 0x2df5ba35: case 0x2ebdbc24: case 0x2f2d9764: case 0x2f66b4d8:
    case 0x32222d6e: case 0x32292cf8: case 0x33a4ea6c: case 0x380a75c4: case 0x408e5187: case 0x460bf109:
    case 0x484fd687: case 0x4a3b5a6c: case 0x4af51227: case 0x4b1fafe5: case 0x4c5a47b9: case 0x4c7e3571:
    case 0x4d917ba7: case 0x521b4bf0: case 0x54e90118: case 0x56aed3da: case 0x599fdcf6: case 0x5cd3d8b8:
    case 0x5d927ca7: case 0x5e1a2a0c: case 0x5f01d656: case 0x60046515: case 0x6067ece3: case 0x60a41c54:
    case 0x60d7df22: case 0x62271501: case 0x629afc27: case 0x644536d0: case 0x70c2e5bb: case 0x7212d405:
    case 0x7212d513: case 0x73623f0d: case 0x747cde6c: case 0x7599373d: case 0x77123c0f: case 0x77123d19:
    case 0x77749be9: case 0x7790b9a1: case 0x77f1e6fa: case 0x78063481: case 0x7954a99e: case 0x7ab1fdf4:
    case 0x7b1e85ba: case 0x7b74ea2f: case 0x7f3c9f90: case 0x7fa1fc35: case 0x80658b23: case 0x80cab64f:
    case 0x8109a0a9: case 0x810bb259: case 0x8135a338: case 0x8177124f: case 0x846e35f4: case 0x8473dd9d:
    case 0x872c50bf: case 0x88c31fb5: case 0x8bbe4822: case 0x8bbf13fe: case 0x8c05ef86: case 0x8ef2ad7c:
    case 0x900ed884: case 0x916aa46c: case 0x93c3cf6c: case 0x94ce2dcb: case 0x95b9edc3: case 0x95ec6a8d:
    case 0x96f9883f: case 0x97c82b3d: case 0x99cb8902: case 0x99ccf0cc: case 0x9a652fcd: case 0x9aec9c1a:
    case 0x9c0e3c3d: case 0x9cad0b77: case 0xa027aabf: case 0xa0a79061: case 0xa1930943: case 0xa31c116e:
    case 0xa58909fb: case 0xa7ad4a18: case 0xa8cd6cb7: case 0xa8ce4655: case 0xaa35a0b5: case 0xaa7e54b7:
    case 0xabc1df0a: case 0xac91e5fb: case 0xad6d6fd5: case 0xad8f34a0: case 0xb31e91b0: case 0xb330b301:
    case 0xb42c3b83: case 0xb5b3a1f8: case 0xb5ea7cdf: case 0xb5ef42c3: case 0xb61a7388: case 0xb66101f1:
    case 0xb8826c68: case 0xbb86cab1: case 0xbe5f9a9f: case 0xbf9dc6e4: case 0xc07c63aa: case 0xc6c08c3f:
    case 0xc786fa48: case 0xc904bb71: case 0xcbfba6d8: case 0xcd84c908: case 0xce29c482: case 0xd4133af2:
    case 0xd553f35a: case 0xd66dcf8c: case 0xdd7e1820: case 0xde065107: case 0xe223aad3: case 0xe2c9ccdd:
    case 0xe5f891d2: case 0xea4a313a: case 0xec070be6: case 0xec5d0f65: case 0xee367afa: case 0xee885a52:
    case 0xf3b8dce5: case 0xf56955a1: case 0xfa9a8384: case 0xfbac8485: case 0xfd6181bd:
        types(a, b, c, d, own_address(&Scr_AtolThunk), 0x00458f30, 0x0011da70, 0x00458ec0);
        return;
    case 0x0038b194: case 0x00398ae8: case 0x05374cc6: case 0x0726affb: case 0x07c43e15: case 0x095a5675:
    case 0x0ca29c7f: case 0x0ccee5c1: case 0x0d60d0ae: case 0x0d60fb3d: case 0x175567a8: case 0x1de0c645:
    case 0x2181000b: case 0x230139de: case 0x236fc839: case 0x27cc2178: case 0x28667a5b: case 0x2afa5406:
    case 0x2d7db3f6: case 0x2f31323b: case 0x2f68c7a5: case 0x30248704: case 0x31aaf853: case 0x358ad77b:
    case 0x384ad510: case 0x3d130364: case 0x3fd9be84: case 0x4225d5c5: case 0x42f377f3: case 0x434381bd:
    case 0x474f5086: case 0x4c9e9556: case 0x4de32b74: case 0x5553515d: case 0x59cb9dd1: case 0x65637ee2:
    case 0x6671d055: case 0x66973bee: case 0x66a44409: case 0x682ea6e9: case 0x6bd99abb: case 0x6dcb7c56:
    case 0x6e688ecd: case 0x6f631b0e: case 0x714cea67: case 0x72e59ffd: case 0x764918ae: case 0x7bc62f6e:
    case 0x7d9f2c49: case 0x80044f23: case 0x81f1fdcb: case 0x8cb5047a: case 0x90e6cdc7: case 0x93348a05:
    case 0x97c730fd: case 0x9a7145f5: case 0x9af3e4c4: case 0x9dc74345: case 0xa33a251d: case 0xa587f48a:
    case 0xa78b2c24: case 0xaeb1527d: case 0xaee44119: case 0xaf6fd78f: case 0xb1812439: case 0xb7f3f874:
    case 0xb8e86881: case 0xbad1ef6d: case 0xbb50e708: case 0xbfc60f36: case 0xc3f4bdfc: case 0xc511e266:
    case 0xc51219a9: case 0xcaeba6a6: case 0xcbffac0d: case 0xcc043125: case 0xcf1e52a5: case 0xd82f7174:
    case 0xd850f89e: case 0xd952b5cf: case 0xda24dbe7: case 0xdce273fb: case 0xe057503b: case 0xe0b632e1:
    case 0xe0b632e2: case 0xedc6ddd3: case 0xeead8089: case 0xeeadf3f2: case 0xef6522f9: case 0xf895a04c:
    case 0xf8b8e073: case 0xfa79d674: case 0xfd2768ea:
        types(a, b, c, d, 0, 0x00458f30, 0, 0x00458ec0);
        return;
    case 0x01757a0e: case 0x070009e5: case 0x07355c08: case 0x14264808: case 0x1666e7cf: case 0x166fe566:
    case 0x2297b461: case 0x25bf26c6: case 0x2d4a70b5: case 0x35ee66af: case 0x3873520a: case 0x3dba7dea:
    case 0x41a569d7: case 0x515816fa: case 0x66db43f0: case 0x6937c3b9: case 0x6a80ec39: case 0x6c663e5f:
    case 0x6c663f49: case 0x6e62eedd: case 0x766fe577: case 0x807116c4: case 0x8166d37c: case 0x838d6a89:
    case 0x93655872: case 0x98b54d9f: case 0x9a44df1e: case 0xa171373f: case 0xa22fa6ba: case 0xa3b8e90e:
    case 0xac9b3dfb: case 0xaff9c3b8: case 0xb3845cc4: case 0xb873524f: case 0xbe149b8c: case 0xc8b638d8:
    case 0xc8b639ce: case 0xce8a66f4: case 0xdf09fd77: case 0xe223c6aa: case 0xec0a1800:
        types(a, b, c, d, own_address(&ScrCall_ScrIface_atof), 0x00458ed0, 0x0011da90, 0x00458ec0);  // ScrCall_ScrIface_atof
        return;
    case 0x053fe992:
        types(a, b, c, d, 0, own_address(&DynArray_NewCopy_PROLOGUE_8), 0, 0x0045e660);  // FUN_0045e600, FUN_0045e660
        return;
    case 0x05d2529a: case 0x062bea65: case 0x0aedcb08: case 0x0c4d3fa0: case 0x142a3d9e: case 0x16e6cf7e:
    case 0x17138232: case 0x1a7e7545: case 0x1cc418a4: case 0x1cc76848: case 0x1e0d2e88: case 0x1fbf2135:
    case 0x210b183c: case 0x22282ca0: case 0x22950d34: case 0x22cb4cdd: case 0x28ce30a6: case 0x2a11980e:
    case 0x2dae9972: case 0x33197128: case 0x35f3abc6: case 0x3bfcf651: case 0x41a849d7: case 0x46861906:
    case 0x555635d6: case 0x55576ed6: case 0x58489939: case 0x64d387a6: case 0x6cfad87a: case 0x6d482943:
    case 0x75545f54: case 0x75f8caea: case 0x7a6bf1dd: case 0x7c52ac8e: case 0x8007a3d3: case 0x8035694d:
    case 0x8064b64f: case 0x81047339: case 0x81ea0dd3: case 0x83048760: case 0x93572ac3: case 0x976440c4:
    case 0x99ca82d7: case 0x9c1519ac: case 0x9e8b61c4: case 0x9f19950c: case 0xa066a2f6: case 0xa1763cc1:
    case 0xa264fbf5: case 0xa3d681e3: case 0xaee61f94: case 0xaff32d79: case 0xb13f0c5e: case 0xb6abdc5e:
    case 0xb8eafadd: case 0xbf1ecaaa: case 0xc25f482c: case 0xc271a9cf: case 0xcc2baca1: case 0xd1c52a4f:
    case 0xd6eafad9: case 0xd70abfb1: case 0xd8dbb2bb: case 0xd91e5c5b: case 0xdfe2186c: case 0xe0e46b1d:
    case 0xe33cb884: case 0xe5d3774f: case 0xe5d3a68d: case 0xe6f61fa4: case 0xe6f61fa7: case 0xe86f599a:
    case 0xec02fc10: case 0xec05b8e0: case 0xec099bbc: case 0xec0b8919: case 0xec0bd591: case 0xec2bcfdd:
    case 0xf203df5e: case 0xf8cb7452: case 0xff7c2a57:
        types(a, b, c, d, own_address(&ScriptString_NewRepFromCString), 0x00458e70, own_address(&ScriptString_EqualsValue), 0x0045bc10);  // ScriptString_NewRepFromCString, FUN_00458e70
        return;
    case 0x12993a44:
        types(a, b, c, d, 0, 0x0045bc20, 0, 0x0045bc80);  // FUN_0045bc20
        return;
    case 0x1791f035:
        types(a, b, c, d, 0, own_address(&DynArray_NewCopy_PROLOGUE_3), 0, 0x0045e330);  // FUN_0045e150, FUN_0045e330
        return;
    case 0x2cd69a2a:
        types(a, b, c, d, 0, own_address(&DynArray_NewCopy_PROLOGUE_6), 0, 0x0045e330);  // FUN_0045e270, FUN_0045e330
        return;
    case 0x4f7c0a56: case 0x85644aaa: case 0xb89aec99:
        types(a, b, c, d, 0, own_address(&DynArray_NewCopy_PROLOGUE_2), 0, 0x0045e330);  // FUN_0045d030, FUN_0045e330
        return;
    case 0x512d9463: case 0x677e856b: case 0x78250ed9: case 0x9ef08b23:
        types(a, b, c, d, 0, own_address(&DynArray_NewCopy_PROLOGUE_4), 0, 0x0045e330);  // FUN_0045e1b0, FUN_0045e330
        return;
    case 0xaeae4e0b:
        types(a, b, c, d, 0, own_address(&DynArray_NewCopy_PROLOGUE_7), 0, 0x0045e330);  // FUN_0045e2d0, FUN_0045e330
        return;
    case 0xc4813666:
        types(a, b, c, d, 0, 0x00458ef0, 0, 0x00458f20);
        return;
    case 0xe337f297:
        types(a, b, c, d, 0, own_address(&DynArray_NewCopy_PROLOGUE_5), 0, 0x0045e330);  // FUN_0045e210, FUN_0045e330
        return;

    default:
        return;
    }
}

void XB_CDECL BroLvl_prologue_Event3HilltopShermans() {
    DynArray targets = {nullptr, 0, 0};
    hilltop_sherman(0x007f793c, 0x007f7918, 3000, 0x007f78f0, &targets);  // "event3_sheman2_top_path1", "event3_hilltop_sherman1", "event3_sheman1_top_targets1"
    hilltop_sherman(0x007f78d4, 0x007f78bc, 3000, 0x007f78a0, &targets);  // "event3_sheman2_top_path2", "event3_hilltop_sherman2", "event3_sheman2_top_targets1"
    hilltop_sherman(0x007f7884, 0x007f786c, 10000, 0x007f7850, &targets);  // "event3_sheman3_top_path1", "event3_hilltop_sherman3", "event3_sheman3_top_targets1"
    if (targets.data) Scr_Free(targets.data);
}

void XB_CDECL BroLvl_prologue_TankStartTargets(uint32_t tank, ScriptString name) {
    DynArray targets = {nullptr, 0, 0};
    ScriptString key;
    ScriptString_ConcatCString(&key, &name, pointer<const char>(0x007f7a7c));  // "_targets"
    Scr_GetEntArray(address_of(&key), 0x0d786b4c, reinterpret_cast<uint8_t*>(&targets), 0);  // targetname
    tank_targets(tank, &targets);
    target_thread(tank, title_global<uint32_t>(0x0083eba8), title_global<uint32_t>(0x0083ece8));  // 2.0, 5.0
    ScriptString_Release(&key);
    if (targets.data) Scr_Free(targets.data);
    ScriptString_Release(&name);
}

void XB_CDECL BroLvl_prologue_LoopSoundInBox(uint32_t self, uint32_t sound, ScrVec3 min, ScrVec3 max, uint32_t loop,
                                             float base, float spread) {
    (void)self;
    const uint32_t ent = spawn_origin();
    if (loop) {
        for (;;) {
            uint32_t at[3];
            const int32_t x = random_range(as_float(min.v[0]), as_float(max.v[0]));
            const int32_t y = random_range(as_float(min.v[1]), as_float(max.v[1]));
            at[0] = bits(static_cast<float>(x));
            at[1] = bits(static_cast<float>(y));
            at[2] = bits(static_cast<float>(random_range(as_float(min.v[2]), as_float(max.v[2]))));
            set_origin(ent, at);
            wait_random(base, spread);
            play_level_sound(ent, &sound);
        }
    }
    slot<void XB_CDECL(uint32_t)>(0x00922540)(ent);  // GScr_Delete
}

void XB_CDECL BroLvl_prologue_LoopSoundInBoxXZ(uint32_t self, uint32_t sound, ScrVec3 min, ScrVec3 max, int32_t y,
                                               uint32_t loop, float base, float spread) {
    (void)self;
    const uint32_t ent = spawn_origin();
    if (static_cast<uint8_t>(loop)) {
        uint32_t at[3];
        at[1] = bits(static_cast<float>(y));
        for (;;) {
            at[0] = bits(static_cast<float>(random_range(as_float(min.v[0]), as_float(max.v[0]))));
            at[2] = bits(static_cast<float>(random_range(as_float(min.v[2]), as_float(max.v[2]))));
            set_origin(ent, at);
            wait_random(base, spread);
            play_level_sound(ent, &sound);
        }
    }
    slot<void XB_CDECL(uint32_t)>(0x00922540)(ent);  // GScr_Delete
}

void XB_CDECL BroLvl_prologue_LoopSoundAtPoints(uint32_t self, uint32_t sound, DynArray* points, uint32_t loop,
                                                float base, float spread) {
    (void)self;
    const uint32_t ent = spawn_origin();
    if (static_cast<uint8_t>(loop)) {
        for (;;) {
            own_function<void XB_CDECL(DynArray*)>(&DynArray_Shuffle12)(points);  // DynArray_Shuffle12_PROLOGUE
            for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(points->size); ++i) {
                if (i >= points->size) title_function<Reserve>(0x0045cde0)(points, i + 1, i + 4);  // DynArray_Reserve12_PROLOGUE
                set_origin(ent, points->data + i * 3);
                wait_random(base, spread);
                play_level_sound(ent, &sound);
            }
        }
    }
    slot<void XB_CDECL(uint32_t)>(0x00922540)(ent);  // GScr_Delete
}

void XB_CDECL BroLvl_prologue_TruckDrones(uint32_t self, uint32_t truck) {
    (void)self;
    ScriptString tag;
    ScriptString_FromRep(&tag, nullptr);
    uint32_t n = 2;
    for (int32_t count = 0; count < 6; ++count, ++n) {
        if (n == 3 || n == 8) ++n;
        uint32_t drone;
        {
            ScriptString classname;
            ScriptString_FromCString(&classname, pointer<const char>(0x0083ab98));  // "script_model"
            uint32_t buffer[3];
            const uint32_t* const o = slot<GetVec>(0x00922b20)(buffer, truck);  // ScrEnt_GetOrigin
            const uint32_t origin[3] = {o[0], o[1], o[2]};
            drone = slot<Spawn>(0x009222f0)(&classname, origin, 0);  // GScr_Spawn
            ScriptString_Release(&classname);
        }
        {
            ScriptString anim;
            ScriptString_FromCString(&anim, pointer<const char>(0x007f6644));  // "event1_drones"
            ScriptString copy;
            ScriptString_Copy(&copy, &anim);
            slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922c44)(drone, copy);  // ScrEnt_SetAnimname
            ScriptString_Release(&anim);
        }
        uint32_t pick;
        {
            ScriptString name;
            uint32_t hash;
            const uint32_t* const key = Scr_HashScriptString(
                &hash, reinterpret_cast<const uint8_t* const*>(ScrCall_ScrEnt_GetAnimname(&drone, &name)));
            uint8_t* const models = HashMap_Index_PROLOGUE_9(pointer<HashMap>(level() + 0x814), key);
            pick = random_int(pointer<int32_t>(reinterpret_cast<uintptr_t>(models))[3]);
            ScriptString_Release(&name);
        }
        {
            ScriptString name;
            uint32_t hash;
            const uint32_t* const key = Scr_HashScriptString(
                &hash, reinterpret_cast<const uint8_t* const*>(ScrCall_ScrEnt_GetAnimname(&drone, &name)));
            uint8_t* const models = HashMap_Index_PROLOGUE_9(pointer<HashMap>(level() + 0x814), key);
            ScriptString* const model = title_function<IndexString>(0x00461230)(models, &pick);  // HashMap_Index_PROLOGUE_12
            slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(drone, model, 0);  // GScr_SetModel
            ScriptString_Release(&name);
        }
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922348)(drone, 4);  // GScr_SetForcedLod
        uint32_t& drones = *pointer<uint32_t>(level() + 0x508);
        if (drones == undefined_int()) drones = 0;
        ++*pointer<uint32_t>(level() + 0x508);
        {
            ScriptString name;
            uint32_t hash;
            const uint32_t* const key = Scr_HashScriptString(
                &hash, reinterpret_cast<const uint8_t* const*>(ScrCall_ScrEnt_GetAnimname(&drone, &name)));
            uint8_t* const guns = HashMap_Index_PROLOGUE_9(pointer<HashMap>(level() + 0x274), key);
            pick = random_int(pointer<int32_t>(reinterpret_cast<uintptr_t>(guns))[3]);
            ScriptString_Release(&name);
        }
        {
            ScriptString name;
            uint32_t hash;
            const uint32_t* const key = Scr_HashScriptString(
                &hash, reinterpret_cast<const uint8_t* const*>(ScrCall_ScrEnt_GetAnimname(&drone, &name)));
            uint8_t* const guns = HashMap_Index_PROLOGUE_9(pointer<HashMap>(level() + 0x274), key);
            const ScriptString* const gun = title_function<IndexString>(0x00461230)(guns, &pick);  // HashMap_Index_PROLOGUE_12
            ScriptString_Assign(ScrField_drone_gun(drone), gun);
            ScriptString_Release(&name);
        }
        {
            ScriptString where;
            ScriptString_FromCString(&where, pointer<const char>(0x00831e14));  // "tag_weapon_right"
            using Attach = void XB_CDECL(uint32_t, ScriptString*, ScriptString*, uint32_t);
            slot<Attach>(0x009224d0)(drone, ScrField_drone_gun(drone), &where, 0);  // GScr_AttachWithOwner
            ScriptString_Release(&where);
        }
        DynArray_PushBack(pointer<uint8_t>(level() + 0x770), &drone);
        {
            ScriptString name;
            ScriptString_Assign(&tag, ScriptString_FromCStringPlusInt(&name, pointer<const char>(0x007f5c18), n));  // "tag_passenger"
            ScriptString_Release(&name);
        }
        uint32_t offset[3] = {0, 0, 0};
        uint32_t angles[3] = {0, 0, 0};
        using Link = void XB_CDECL(uint32_t, uint32_t, ScriptString*, uint32_t*, uint32_t*, uint32_t);
        slot<Link>(0x009224f4)(drone, truck, &tag, angles, offset, 1);  // GScr_LinkToTagWithOffset
        own_function<StartThread>(&Scr_StartThread_ent_str_PROLOGUE_entry)(0, 0, 0, 0, 0x004230e0, drone, &tag);  // BroLvl_prologue_AttachPassengerDrones
    }
    ScriptString_Release(&tag);
}

void XB_CDECL BroLvl_prologue_InitDialogueBloomfield() {
    set_dialogue(0x1c593687, 0x8662e417, 0x007f8414);  // "PRO_E02L03_BLO_NowsYourCh"
    set_dialogue(0x1c593687, 0x839aab5b, 0x007f83f8);  // "PRO_E02L04_BLO_WellImSure"
    set_dialogue(0x1c593687, 0x48177856, 0x007f7b2c);  // "PRO_E03L01_BLO_AcrossTheR"
    set_dialogue(0x1c593687, 0xd8f372ca, 0x007f83e0);  // "PRO_E03L04_BLO_Hold up!"
    set_dialogue(0x1c593687, 0xd8f372ca, 0x007f5ee8);  // "PRO_E03L05_BLO_WaitForThe"
    set_dialogue(0x1c593687, 0x0d13f2cd, 0x007f83c4);  // "PRO_E03L08_BLO_IGuessWere"
    set_dialogue(0x1c593687, 0x077510d2, 0x007f83ac);  // "PRO_E04L02_BLO_TIGER"
    set_dialogue(0x1c593687, 0x97475490, 0x007f696c);  // "PRO_E04L03_BLO_GetOuttaTh"
    set_dialogue(0x1c593687, 0xddc0ed18, 0x007f6384);  // "PRO_E04L07_BLO_AwChristFA"
    set_dialogue(0x1c593687, 0x29718b2e, 0x007f8390);  // "PRO_E04L08_BLO_Maybe if w"
    set_dialogue(0x1c593687, 0x210c8695, 0x007f8374);  // "PRO_E04L10_BLO_SargeLookI"
    set_dialogue(0x1c593687, 0xc8c2df73, 0x007f8358);  // "PRO_E04L11_BLO_TheBridgeI"
    set_dialogue(0x1c593687, 0x2873ace1, 0x007f833c);  // "PRO_E05L01_BLO_GoodNowWeC"
    set_dialogue(0x1c593687, 0xf808b693, 0x007f8320);  // "PRO_E05L02_BLO_TakeCover"
    set_dialogue(0x1c593687, 0xf9827ce3, 0x007f8304);  // "PRO_E06L03_BLO_YoureGonna"
    set_dialogue(0x1c593687, 0x06e31d43, 0x007f82ec);  // "PRO_E06L04_BLO_MEDIC!"
    set_dialogue(0x1c593687, 0xecdcfb44, 0x007f82d0);  // "PRO_E06L05_BLO_KeepLookin"
}

void XB_CDECL BroLvl_prologue_InitJeepPassIntroScene() {
    const uint32_t speaker = 0x8d39895a;
    map_set(reinterpret_cast<HashMap*>(HashMap_Index_PROLOGUE_8(pointer<HashMap>(level() + 0x19c), &speaker)),
            0x06a6de2f, 0, 0x008c59ec);  // g_anim_prologue_c_pro_jeeppass_intro
    map_set(reinterpret_cast<HashMap*>(HashMap_Index_PROLOGUE_8(pointer<HashMap>(level() + 0x1d8), &speaker)),
            0x06a6de2f, 0, 0x008c5aa0);  // g_anim_prologue_f_PRO_E01L09b_JEE_ItsHellOu_a
    set_dialogue(0x8d39895a, 0x452b8e59, 0x007f8558);  // "PRO_E01L09b_JEE_ItsHellOu"
}

void XB_CDECL BroLvl_prologue_InitReadMapAnims() {
    set_anim(0xa5c25861, 0x846e3bd9, 0, 0x008c5a04);  // g_anim_prologue_c_prologue_readmap_guy1
    set_anim(0xa5c25862, 0x846e3bd9, 0, 0x008c5a10);  // g_anim_prologue_c_prologue_readmap_guy2
}

void XB_CDECL BroLvl_prologue_SetupDrones() {
    {
        const uint32_t key = 0x658f814a;
        using Index = ScriptString* XB_THISCALL(uint32_t, const uint32_t*);
        ScriptString_AssignCString(title_function<Index>(0x00461120)(level() + 0x238, &key),
                                   pointer<const char>(0x007f498c));  // "prologue_drones"
    }
    set_name(0x814, 0x658f814a, 0, 0x007f7244);  // "c_1stInffall_Multi_3A"
    set_name(0x274, 0x658f814a, 0, 0x007f8584);  // "worldmodel_m1"
    set_name(0x274, 0x658f814a, 1, 0x007f8574);  // "G_TOMP_world"
    map_set(pointer<HashMap>(level() + 0x7c8), 0x658f814a, undefined_int(), 0x008c58b4);
    set_anim(0x658f814a, 0x6e28adc5, 0, 0x008c5848);
    set_anim(0x658f814a, 0x6e28adc5, 1, 0x008c5854);
    set_anim(0x658f814a, 0x6e28adc5, 2, 0x008c58a8);
    set_anim(0x658f814a, 0xd5cdd358, 0, 0x008c589c);
    drone_run_anims();
}

void XB_CDECL BroLvl_prologue_SetDroneRunAnims() { drone_run_anims(); }

uint32_t* XB_CDECL BroLvl_prologue_SpawnVehicleDrone3(uint32_t* out, uint32_t a2, uint32_t a3, ScriptString name) {
    return spawn_vehicle_drone(out, a2, a3, &name, title_global<uint32_t>(0x00922ea4), title_global<uint32_t>(0x00922ebc));
}

uint32_t* XB_CDECL BroLvl_prologue_SpawnVehicleDrone4(uint32_t* out, uint32_t a2, uint32_t a3, ScriptString name,
                                                      uint32_t a5) {
    return spawn_vehicle_drone(out, a2, a3, &name, a5, title_global<uint32_t>(0x00922ebc));
}

uint32_t* XB_CDECL BroLvl_prologue_SpawnVehicleDrone5(uint32_t* out, uint32_t a2, uint32_t a3, ScriptString name,
                                                      uint32_t a5, uint32_t a6) {
    return spawn_vehicle_drone(out, a2, a3, &name, a5, a6);
}

void XB_CDECL Bro_prologue__Event4_Plane_Crash_Distance(uint32_t self) {
    uint32_t scratch;
    slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x009221c0)(  // GScr_GetEntVar
        self, 0xe2a210cf, *Scr_HashName(&scratch, pointer<const char>(0x007f89e8)));  // "prologue::Event4_Plane_Crash_Distance"
    queue_notify(*level_ent(), 0xc9fe433d);
    const uint32_t* const owner = level_ent();
    scratch = title_global<uint32_t>(0x007f4d90);  // 11.1
    title_function<StartThread>(kStartEntF)(0, 0, 0, 0, own_address(&BroLvl_prologue_FriendlyChainThink), *owner, &scratch);  // BroLvl_prologue_FriendlyChainThink
    own_function<StartThread>(&Scr_StartThreadFunctor1_entry)(0, 0, 0, 0, 0x00424a00, *level_ent());  // FUN_00424a00
    own_function<StartThread>(&Scr_StartThreadFunctor1_entry)(0, 0, 0, 0, 0x00435c30, *level_ent());  // BroLvl_prologue_Event4Flak
    own_function<StartThread>(&Scr_StartThreadFunctor1_entry)(0, 0, 0, 0, 0x00452b90, *level_ent());
}

void XB_CDECL BroLvl_prologue_StartFromCheckpoint() {
    bool start;
    {
        ScriptString first;
        ScriptString second;
        uint32_t made = 1;
        if (ScriptString_Equals(ScrCall_GScr_GetCvar(&first, 0x008353b8), pointer<const char>(0x0082dfeb))) {  // "checkpoint", ""
            start = false;
        } else {
            made = 3;
            start = !ScriptString_Equals(ScrCall_GScr_GetCvar(&second, 0x008353b8), pointer<const char>(0x0082e098));  // "0"
        }
        if (made & 2) ScriptString_Release(&second);
        if (made & 1) ScriptString_Release(&first);
    }
    if (!start) return;
    uint32_t hash;
    {
        ScriptString value;
        Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(ScrCall_GScr_GetCvar(&value, 0x008353b8)));
        ScriptString_Release(&value);
    }
    *pointer<uint8_t>(level() + 0x260) = 1;
    uint32_t body = 0;
    if (hash == 0x392e691b) {
        *pointer<uint32_t>(level() + 0x844) = hash;
        const uint32_t ent = pointer<uint32_t>(level())[1];
        uint32_t* const functor = alloc(0xc);
        if (functor) {
            functor[0] = 0x007f5ba0;
            functor[1] = 0x004571b0;
            functor[2] = ent;
        }
        create_thread(ent, functor);
    } else if (hash == 0x3de9e603) {
        *pointer<uint32_t>(level() + 0x844) = hash;
        body = 0x00456050;  // BroLvl_prologue_Event2Start
    } else if (hash == 0x3de9e600) {
        *pointer<uint32_t>(level() + 0x844) = hash;
        body = 0x0044a8d0;  // BroLvl_prologue_ZoneTransitions
    } else if (hash == 0x4e43dfd6) {
        *pointer<uint32_t>(level() + 0x844) = 0x4e43dfd6;
        body = 0x0044fde0;
    }
    if (body) own_function<StartThread>(&Scr_StartThreadFunctor1_entry)(0, 0, 0, 0, body, pointer<uint32_t>(level())[1]);
    slot<void XB_CDECL()>(0x00922324)();  // GScr_RestorePlayerLoadout
}

}  // namespace game
