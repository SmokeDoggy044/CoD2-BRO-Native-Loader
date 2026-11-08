#include "bro_scr_drones.h"

#include <cstring>

#include "bro_scr_common.h"
#include "bro_scr_util.h"
#include "g_scr_runtime.h"
#include "g_script.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "scr_access.h"
#include "title_data.h"
#include "bro_scr_drone.h"
#include "levels_twins.h"
#include "scr_field.h"
#include "script_string.h"
#include "bro_scr_battle_threads.h"
#include "bro_scr_drone_threads.h"

namespace game {

namespace {

// A BroScr_DroneMain thread (Scr_StartThread_ent_b_b with pointers to two bytes: 0 and `flag`) on
// each valid drone of the level's list (+0x770) within sqrt(*radius_sq) of `ent`.
void start_drones_near(uint32_t ent, uint32_t radius_sq, uint8_t flag) {
    const auto drones = []() { return reinterpret_cast<DynArray*>(title_global<uint32_t>(0x00922ee0) + 0x770); };
    if (static_cast<int32_t>(drones()->size) < 1) return;
    for (uint32_t n = 0; static_cast<int32_t>(n) < static_cast<int32_t>(drones()->size); ++n) {
        DynArray* list = drones();
        if (n >= list->size) DynArray_Resize(reinterpret_cast<uint8_t*>(list), n + 1, n + 4);
        if (!scr_slot<uint32_t XB_CDECL(uint32_t)>(0x00922214)(list->data[n])) continue;  // Scr_EntityHandleIsValid
        using GetOrigin = const uint32_t* XB_CDECL(uint32_t*, uint32_t);
        uint32_t buffer[3];
        const uint32_t* o = scr_slot<GetOrigin>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
        uint32_t from[3] = {o[0], o[1], o[2]};
        list = drones();
        if (n >= list->size) DynArray_Resize(reinterpret_cast<uint8_t*>(list), n + 1, n + 4);
        uint32_t buffer2[3];
        o = scr_slot<GetOrigin>(0x00922b20)(buffer2, list->data[n]);
        uint32_t to[3] = {o[0], o[1], o[2]};
        const long double d = scr_slot<long double XB_CDECL(const uint32_t*, const uint32_t*)>(0x0092226c)(to, from);  // Scr_DistanceSquared
        if (!(static_cast<long double>(title_global<float>(radius_sq)) >= d)) continue;
        list = drones();
        uint8_t flags[2] = {flag, 0};
        if (n >= list->size) DynArray_Resize(reinterpret_cast<uint8_t*>(list), n + 1, n + 4);
        using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint8_t*,
                                              const uint8_t*);
        title_function<StartThread>(0x0045a970)(0, 0, 0, 0, own_address(&BroScr_DroneMain), list->data[n], &flags[1], &flags[0]);  // Scr_StartThread_ent_b_b(BroScr_DroneMain)
    }
}

}  // namespace

void XB_CDECL BroScr_StartDronesNearEnt(uint32_t ent) { start_drones_near(ent, 0x0083ed4c, 0); }       // 4096 (64 units)
void XB_CDECL BroScr_StukaStartNearbyDrones(uint32_t ent) { start_drones_near(ent, 0x0083f224, 1); }  // 147456 (384 units)

void XB_CDECL BroScr_StartDronesByTargetname(uint32_t self, ScriptString name) {
    (void)self;
    DynArray drones{nullptr, 0, 0};
    Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&name)), 0x0d786b4c,
                    reinterpret_cast<uint8_t*>(&drones), 0);
    for (uint32_t n = 0; static_cast<int32_t>(n) < static_cast<int32_t>(drones.size); ++n) {
        if (n >= drones.size) DynArray_Resize(reinterpret_cast<uint8_t*>(&drones), n + 1, n + 4);
        const uint32_t drone = drones.data[n];
        uint8_t* functor = static_cast<uint8_t*>(scr_slot<void* XB_CDECL(uint32_t)>(0x00922928)(0x10));  // ScrIface_Alloc
        if (functor) {
            uint32_t* words = reinterpret_cast<uint32_t*>(functor);
            words[0] = 0x007f4a7c;
            words[1] = own_address(&BroScr_DroneMain);  // BroScr_DroneMain
            words[2] = drone;
            functor[0xc] = 0;
            functor[0xd] = 0;
        }
        using CreateThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint8_t*, uint32_t);
        scr_slot<CreateThread>(0x0092216c)(0, 0, 0, drone, functor, 0);  // Scr_CreateThread
    }
    if (drones.data) own_function<void XB_CDECL(void*)>(&Scr_Free)(drones.data);  // Scr_Free
    scr_release(&name);
}

void XB_CDECL BroScr_StartDroneDeathThreads(uint32_t self, ScriptString name) {
    (void)self;
    DynArray drones{nullptr, 0, 0};
    Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&name)), 0x0d786b4c,
                    reinterpret_cast<uint8_t*>(&drones), 0);
    for (uint32_t n = 0; static_cast<int32_t>(n) < static_cast<int32_t>(drones.size); ++n) {
        const uint32_t object = title_global<uint32_t>(0x00922ee0);
        const uint32_t* level = reinterpret_cast<const uint32_t*>(object ? object + 4 : 0);
        if (n >= drones.size) DynArray_Resize(reinterpret_cast<uint8_t*>(&drones), n + 1, n + 4);
        ScriptString noteworthy;
        ScriptString* note = ScrCall_ScrEnt_GetScriptNoteworthy(&drones.data[n], &noteworthy);
        const uint32_t level_word = *level;
        if (n >= drones.size) DynArray_Resize(reinterpret_cast<uint8_t*>(&drones), n + 1, n + 4);
        using Begin = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                        ScriptString*);
        own_function<Begin>(&Scr_BeginThread_ent_str_entry)(0, 0, 0, drones.data[n], 0xe2a210cf, own_address(&BroScr_DroneDeathThink), level_word,
                                          note);  // Scr_BeginThread_ent_str(BroScr_DroneDeathThink)
        scr_release(&noteworthy);
    }
    if (drones.data) own_function<void XB_CDECL(void*)>(&Scr_Free)(drones.data);  // Scr_Free
    scr_release(&name);
}

namespace {

uint32_t level_block() { return title_global<uint32_t>(0x00922ee0); }

// The level's per-type tables (+0x814 models, +0x274 guns): HashMap_Index (0x004642b0) by the
// type's hash; an entry is an array (count at +0xc) indexed through 0x00461230.
uint32_t* type_entry(uint32_t offset, const uint32_t* hash) {
    return own_function<uint32_t* XB_THISCALL(uint32_t, const uint32_t*)>(&HashMap_Index_PROLOGUE_9)(level_block() + offset, hash);
}

uint32_t* entry_at(uint32_t* entry, const uint32_t* index) {
    return title_function<uint32_t* XB_THISCALL(uint32_t*, const uint32_t*)>(0x00461230)(entry, index);
}

uint32_t* type_hash(uint32_t* out, const ScriptString* type) {
    return Scr_HashScriptString(out, reinterpret_cast<const uint8_t* const*>(type));
}

uint32_t random_int(uint32_t n) { return scr_slot<uint32_t XB_CDECL(uint32_t)>(0x0092222c)(n); }  // Scr_RandomInt

uint32_t* handle_field(uint32_t ent, uint32_t hash) {
    ScriptKeyTable* const table = BroScr_ScriptObjectOrEmpty(ent);
    uint32_t* value = ScriptKeyTable_Find(table, hash);
    if (!value) value = ScriptKeyTable_Set(table, hash, 0);
    return value;
}

}  // namespace

uint32_t* XB_CDECL BroScr_SpawnDrone(uint32_t* out, ScrVec3 origin, ScriptString type, uint32_t lod) {
    if (title_global<uint32_t>(level_block() + 0x508) == title_global<uint32_t>(0x00922ec0)) title_global<uint32_t>(level_block() + 0x508) = 0;
    if (title_global<uint32_t>(level_block() + 0x5a8) == title_global<uint32_t>(0x00922ec0)) title_global<uint32_t>(level_block() + 0x5a8) = 40;
    if (static_cast<int32_t>(title_global<uint32_t>(level_block() + 0x508)) >=
        static_cast<int32_t>(title_global<uint32_t>(level_block() + 0x5a8))) {
        *out = title_global<uint32_t>(0x00922e84);  // too many drones: the empty handle
        scr_release(&type);
        return out;
    }
    ScriptString classname;
    scr_string_from(&classname, 0x0083ab98);  // "script_model"
    const uint32_t zone = scr_slot<uint32_t XB_CDECL(ScrVec3)>(0x0092249c)(origin);  // GScr_GetZoneAtPoint
    const uint32_t drone =
        scr_slot<uint32_t XB_CDECL(ScriptString*, const ScrVec3*, uint32_t)>(0x009222f0)(&classname, &origin, zone);  // GScr_Spawn
    scr_release(&classname);
    scr_slot<void XB_CDECL(uint32_t)>(0x0092234c)(drone);  // GScr_SetEntFlag2000000
    ++title_global<uint32_t>(level_block() + 0x508);
    uint32_t hash;
    uint32_t index = random_int(type_entry(0x814, type_hash(&hash, &type))[3]);
    uint32_t hash2;
    uint32_t* model = entry_at(type_entry(0x814, type_hash(&hash2, &type)), &index);
    scr_slot<void XB_CDECL(uint32_t, uint32_t*, uint32_t)>(0x00922548)(drone, model, 0);  // GScr_SetModel
    ScriptString group;
    scr_string_from(&group, 0x00810340);  // "drone"
    ScriptString copy;
    ScriptString_Copy(&copy, &group);
    scr_slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922c1c)(drone, copy);  // ScrEnt_SetGroupname
    scr_release(&group);
    if (lod != title_global<uint32_t>(0x00922ec0))
        scr_slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922348)(drone, static_cast<int32_t>(lod) > 4 ? 4 : lod);  // GScr_SetForcedLod
    uint32_t gun_hash;
    type_hash(&gun_hash, &type);
    if (static_cast<int32_t>(type_entry(0x274, &gun_hash)[3]) > 0) {
        uint32_t gun = random_int(type_entry(0x274, &gun_hash)[3]);
        uint32_t* name = entry_at(type_entry(0x274, &gun_hash), &gun);
        using GetString = ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t);
        ScriptString* field = title_function<GetString>(0x00458da0)(BroScr_ScriptObjectOrEmpty(drone), 0x1a7e7545);  // ScriptKeyTable_GetString
        own_function<void XB_THISCALL(ScriptString*, const uint32_t*)>(&ScriptString_Assign_entry)(field, name);         // ScriptString_Assign
        bool defined;
        own_function<bool* XB_CDECL(bool*, uint32_t)>(ScrField_drone_gun_IsDefined)(&defined, drone);  // ScrField_drone_gun_IsDefined
        if (defined && scr_slot<uint32_t XB_CDECL(uint32_t)>(0x00922214)(drone)) {      // Scr_EntityHandleIsValid
            ScriptString tag;
            scr_string_from(&tag, 0x00831e14);  // "tag_weapon_right"
            uint32_t* gun_model = own_function<uint32_t* XB_CDECL(uint32_t)>(ScrField_drone_gun)(drone);  // ScrField_drone_gun
            scr_slot<void XB_CDECL(uint32_t, uint32_t*, ScriptString*, uint32_t)>(0x009224d0)(drone, gun_model, &tag, 0);  // GScr_AttachWithOwner
            scr_release(&tag);
        }
    }
    *out = drone;
    scr_release(&type);
    return out;
}

void XB_CDECL BroScr_DronePathSpawnOne(uint32_t spawner, uint32_t path, ScriptString type, float delay_min, float delay_max,
                                       uint32_t flag) {
    const uint32_t node = path;
    using GetArray = uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t);
    const auto spline = [node]() { return title_function<GetArray>(0x00602a40)(BroScr_ScriptObjectOrEmpty(node), 0xb89aec99); };  // ScriptKeyTable_GetOrAddArray_5
    if (spline()[2] == 0 && *handle_field(node, 0x6bd99abb) == 0) {
        const uint32_t level = level_block();
        uint32_t points[3];
        own_function<uint32_t* XB_CDECL(uint32_t*, uint32_t, uint32_t)>(&BroScr_DroneSplineMath)(points, title_global<uint32_t>(level ? level + 4 : 0),
                                                                                    node);  // BroScr_DroneSplineMath
        title_function<void XB_THISCALL(uint32_t*, const uint32_t*)>(0x0045c410)(spline(), points);  // DynArray_Assign12
        if (points[0]) own_function<void XB_CDECL(uint32_t)>(&Scr_Free)(points[0]);              // Scr_Free
    }
    uint32_t drone = 0;
    if (*handle_field(node, 0x6bd99abb) != 0) {
        const uint32_t lod = *own_function<uint32_t* XB_CDECL(uint32_t)>(ScrField_hce29c482)(node);  // ScrField_hce29c482
        ScriptString copy;
        ScriptString_Copy(&copy, &type);
        const uint32_t* start = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(*handle_field(node, 0x6bd99abb)));
        uint32_t result;
        drone = *own_function<uint32_t* XB_CDECL(uint32_t*, ScrVec3, ScriptString, uint32_t)>(&BroScr_SpawnDrone)(
            &result, {{start[0], start[1], start[2]}}, copy, lod);  // BroScr_SpawnDrone
    }
    if (!scr_slot<uint32_t XB_CDECL(uint32_t)>(0x00922214)(drone)) {  // Scr_EntityHandleIsValid
        scr_release(&type);
        return;
    }
    {
        ScriptString targetname;
        const ScriptString* name = ScrCall_ScrEnt_GetTargetname(&path, &targetname);
        ScriptString joined;
        ScriptString_ConcatCString(&joined, name, reinterpret_cast<const char*>(static_cast<uintptr_t>(0x00825470)));  // + "_drone"
        ScriptString copy;
        ScriptString_Copy(&copy, &joined);
        scr_slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922b54)(drone, copy);  // ScrEnt_SetTargetname
        scr_release(&joined);
        scr_release(&targetname);
    }
    {
        ScriptString copy;
        ScriptString_Copy(&copy, &type);
        scr_slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922c44)(drone, copy);  // ScrEnt_SetAnimname
    }
    // The IsDefined byte goes to the path's argument slot, as the original.
    own_function<bool* XB_CDECL(bool*, uint32_t)>(ScrField_script_explode_IsDefined)(reinterpret_cast<bool*>(&path), spawner);  // ScrField_script_explode_IsDefined
    if (!*reinterpret_cast<bool*>(&path) ||
        static_cast<int32_t>(*own_function<uint32_t* XB_CDECL(uint32_t)>(ScrField_script_explode)(spawner)) < 1)  // ScrField_script_explode
        DynArray_PushBack(reinterpret_cast<uint8_t*>(level_block() + 0x770), &drone);
    uint32_t* functor = static_cast<uint32_t*>(scr_slot<void* XB_CDECL(uint32_t)>(0x00922928)(0xc));  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f5ba0;
        functor[1] = own_address(&BroScr_DroneAnimLoop);  // BroScr_DroneAnimLoop
        functor[2] = drone;
    }
    using CreateThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);
    scr_slot<CreateThread>(0x0092216c)(0, 0, 0, drone, functor, 0);
    functor = static_cast<uint32_t*>(scr_slot<void* XB_CDECL(uint32_t)>(0x00922928)(0x14));
    if (functor) {
        functor[0] = 0x00802434;
        functor[1] = own_address(&BroScr_DroneFollowSpline);  // BroScr_DroneFollowSpline
        functor[2] = drone;
        functor[3] = node;
        *reinterpret_cast<uint8_t*>(&functor[4]) = static_cast<uint8_t>(flag);
    }
    scr_slot<CreateThread>(0x0092216c)(0, 0, 0, drone, functor, 0);
    // Both delays are checked through the path's argument slot.
    const float undefined = title_global<float>(0x00922eac);  // g_scrUndefinedFloat
    std::memcpy(&path, &delay_min, 4);
    if (delay_min != undefined) {
        std::memcpy(&path, &delay_max, 4);
        if (delay_max != undefined) {
            using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, float*, float*, uint32_t*);
            title_function<StartThread>(0x0017ae10)(0, 0, 0, 0, own_address(&BroScr_DroneMainDelayed), drone, &delay_min, &delay_max, &flag);  // Scr_StartThread_ent_f_f_b(BroScr_DroneMainDelayed)
        }
    }
    scr_release(&type);
}

}  // namespace game
