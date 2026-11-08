#include "bro_scr_fx_threads.h"

#include "bro_scr_ai_common.h"
#include "bro_scr_array.h"
#include "bro_scr_common.h"
#include "bro_scr_math.h"
#include "bro_scr_vehicle.h"
#include "g_scr_runtime.h"
#include "g_script.h"
#include "qcommon_core.h"
#include "scr_access.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "title_data.h"

namespace game {

using namespace bro_ai;

namespace {

uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// BroScr_FxCreationSound (0x0015f380; agent-a list): (ent, sound, origin, to, delay).
void fx_creation_sound(uint32_t ent, uint32_t sound, ScrVec3 origin, ScrVec3 to, uint32_t delay) {
    title_function<void XB_CDECL(uint32_t, uint32_t, ScrVec3, ScrVec3, uint32_t)>(0x0015f380)(ent, sound, origin, to,
                                                                                             delay);
}
// BroScr_FxCreationSoundThink (0x0015f720; agent-a list): (ent, sound, to, a, b, c, d).
void fx_creation_sound_think(uint32_t ent, uint32_t sound, ScrVec3 to, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    title_function<void XB_CDECL(uint32_t, uint32_t, ScrVec3, uint32_t, uint32_t, uint32_t, uint32_t)>(0x0015f720)(
        ent, sound, to, a, b, c, d);
}

ScrVec3 undefined_vec() {
    const uint32_t lane = undefined_lane();
    ScrVec3 v = {{lane, lane, lane}};
    return v;
}

// GScr_Spawn of a "script_origin" at `origin`, named "_fx_creation" (ScrEnt_SetTargetname takes a copy).
uint32_t spawn_fx_creation_origin(ScrVec3* origin) {
    ScriptString classname;
    scr_string_from(&classname, 0x0083ab88);  // "script_origin"
    const uint32_t ent =
        slot<uint32_t XB_CDECL(ScriptString*, ScrVec3*, uint32_t)>(0x009222f0)(&classname, origin, 0);  // GScr_Spawn
    scr_release(&classname);
    ScriptString name;
    scr_string_from(&name, 0x00825234);  // "_fx_creation"
    ScriptString copy;
    ScriptString_Copy(&copy, &name);
    slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922b54)(ent, copy);  // ScrEnt_SetTargetname
    scr_release(&name);
    return ent;
}

uint32_t* origin_of(uint32_t* buffer, uint32_t ent) {
    return slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
}
long double distance(const uint32_t* a, const uint32_t* b) {
    return slot<long double XB_CDECL(const uint32_t*, const uint32_t*)>(0x00922268)(a, b);  // Scr_Distance
}
long double vehicle_speed(uint32_t ent) { return slot<long double XB_CDECL(uint32_t)>(0x00922b38)(ent); }

// The tree-burst play (0x0015def0) on one entity.
void play_burst(uint32_t ent) { title_function<void XB_CDECL(uint32_t)>(0x0015def0)(ent); }  // Bro__treeburst_gmi__tree_burst_playBurst

void create_thread(uint32_t ent, uint32_t* functor) {
    slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(0, 0, 0, ent, functor,
                                                                                                    0);  // Scr_CreateThread
}

uint32_t hash_of(uint32_t* out, const ScriptString* string) {
    return *Scr_HashScriptString(out, reinterpret_cast<const uint8_t* const*>(string));
}

// The entity element i of `array` targets (GScr_GetEntArray by target).
uint32_t target_ent(uint32_t* array, uint32_t i) {
    using GetEntArray = uint32_t XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t);
    ScriptString target;
    const uint32_t found =
        slot<GetEntArray>(0x009221c4)(ScrCall_ScrEnt_GetTarget(BroScr_ArrayElement(array, i), &target), 0x0d786b4c, 0, 0, 0);
    scr_release(&target);
    return found;
}
// Whether element i of `array` has a target.
bool has_target(uint32_t* array, uint32_t i) {
    ScriptString target;
    const bool set = ScrCall_ScrEnt_GetTarget(BroScr_ArrayElement(array, i), &target)->rep != 0;
    scr_release(&target);
    return set;
}

}  // namespace

void XB_CDECL BroScr_TreadFx(uint32_t vehicle, uint32_t tread, uint32_t wheel) {
    using EndOn = void XB_CDECL(uint32_t, uint32_t);
    slot<EndOn>(0x00922188)(tread, 0x06730ae8);    // Scr_EndOn "death"
    slot<EndOn>(0x00922188)(vehicle, 0x06730ae8);
    ScriptString fast;
    const bool skip = !ScriptString_Equals(ScrCall_GScr_GetCvar(&fast, 0x008253e4), pointer<const char>(0x0082e098));  // "scr_gmi_fast", "0"
    scr_release(&fast);
    if (skip) return;
    alignas(4) uint8_t map[0x28];
    title_function<void XB_THISCALL(uint8_t*, uint32_t)>(0x00460d40)(map, title_global<uint32_t>(0x00922ee0) + 0x57c);  // HashMap_CopyCtor
    title_function<void XB_THISCALL(uint8_t*)>(0x0045ebe0)(map);  // HashMap_Dtor
    uint32_t key = 0x438c482f;
    using Index = uint32_t* XB_THISCALL(uint32_t, uint32_t*);
    if (*title_function<Index>(0x00461120)(title_global<uint32_t>(0x00922ee0) + 0x57c, &key) == 0) {  // HashMap_Index
        ScriptString message;
        scr_string_from(&message, 0x008253a0);  // "Tread effects are undefined. Add _treadfx::main(); to this level"
        ScrLocal_ReleaseString_2(message);
    }
    ScriptString surface;
    BroScr_TreadFxSurfaceThink(&surface, vehicle, wheel);
    float travelled = title_global<float>(0x0083eb4c);  // 0.001
    bool defined;
    const int32_t step =
        *ScrField_hb42c3b83_IsDefined(&defined, vehicle) ? static_cast<int32_t>(*ScrField_hb42c3b83(vehicle)) : 1;
    while (slot<uint32_t XB_CDECL(uint32_t)>(0x00922214)(tread)) {  // Scr_EntityHandleIsValid
        uint32_t buffer[3];
        const uint32_t* o = origin_of(buffer, tread);
        const uint32_t last[3] = {o[0], o[1], o[2]};
        Scr_Wait(slot<float XB_CDECL(uint32_t)>(0x00922230)(0x3e4ccccd));  // Scr_RandomFloat(0.2)
        o = origin_of(buffer, tread);
        const uint32_t now[3] = {o[0], o[1], o[2]};
        travelled = static_cast<float>(distance(last, now) + travelled);
        if (!(vehicle_speed(vehicle) > title_global<float>(0x0083eb20))) continue;  // 1
        o = origin_of(buffer, vehicle);
        const uint32_t vehicle_origin[3] = {o[0], o[1], o[2]};
        o = origin_of(buffer, title_global<uint32_t>(0x00922eec));  // the player
        const uint32_t player_origin[3] = {o[0], o[1], o[2]};
        if (!(title_global<float>(0x008016ec) > distance(player_origin, vehicle_origin))) continue;  // 6000
        if (!(travelled > static_cast<float>(static_cast<int32_t>(*int_slot(vehicle, 0x8473dd9d))))) continue;
        const uint32_t* a = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922ba0)(buffer, tread);  // ScrEnt_GetAngles
        const uint32_t angles[3] = {a[0], a[1], a[2]};
        uint32_t forward[3] = {undefined_lane(), undefined_lane(), undefined_lane()};
        slot<void XB_CDECL(uint32_t*, const uint32_t*)>(0x00922290)(forward, angles);  // Scr_AnglesToForward
        const float speed = static_cast<float>(vehicle_speed(vehicle));
        const float scale = speed / as_float(*key_slot(vehicle, 0xdf09fd77, undefined_float())) *
                            static_cast<float>(step) * title_global<float>(0x0083eb34);  // -1
        float scaled_buffer[3];
        const float* s = BroScr_VectorScale(scaled_buffer, as_float(forward[0]), as_float(forward[1]),
                                            as_float(forward[2]), scale);
        float scaled[3] = {s[0], s[1], s[2]};
        ScriptString last_surface;
        ScriptString_Copy(&last_surface, &surface);
        ScriptString next;
        ScriptString_Assign(&surface, BroScr_TreadFxSurfaceThink(&next, vehicle, wheel));
        scr_release(&next);
        if (!ScriptString_EqualsString(&surface, &last_surface))
            slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(vehicle, 0xd33dc7a9);  // Scr_QueueNotify
        if (!ScriptString_Equals(&surface, pointer<const char>(0x00824fb0)))  // "nofx"
            slot<void XB_CDECL(uint32_t, ScriptString*, float*, uint32_t, uint32_t)>(0x009221e0)(tread, &surface, scaled, 0,
                                                                                               0);  // GScr_PlaySoundAtPosition
        travelled -= static_cast<float>(static_cast<int32_t>(*int_slot(vehicle, 0x8473dd9d)));
        scr_release(&last_surface);
    }
    scr_release(&surface);
}

void XB_CDECL BroScr_TreeBurstTriggerThink(uint32_t ent, uint8_t wait_trigger) {
    bool targets = false;
    if (wait_trigger) {
        Scr_WaitTill(ent, 0xe2a210cf);
        ScriptString target;
        targets = ScrCall_ScrEnt_GetTarget(&ent, &target)->rep != nullptr;
        scr_release(&target);
    } else {
        bool defined;
        if (!*ScrField_burst_notify_IsDefined(&defined, ent)) return;
        const uint32_t* level = pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) ? title_global<uint32_t>(0x00922ee0) + 4 : 0);
        ScriptKeyTable* const table = ent_script_object(ent);
        ScriptString* const notify =
            title_function<ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t)>(0x00458da0)(table, 0x2a11980e);  // ScriptKeyTable_GetString
        uint32_t hash;
        Scr_WaitTill(*level, hash_of(&hash, notify));
    }
    if (targets) {
        DynArray ents = {nullptr, 0, 0};
        ScriptString target;  // built in the wait_trigger argument slot
        Scr_GetEntArray(address_of(ScrCall_ScrEnt_GetTarget(&ent, &target)), 0x0d786b4c, reinterpret_cast<uint8_t*>(&ents), 0);
        scr_release(&target);
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ents.size); ++i)
            play_burst(*BroScr_ArrayElement(reinterpret_cast<uint32_t*>(&ents), i));
        if (ents.data) Scr_Free(ents.data);
        return;
    }
    if (wait_trigger) return;
    bool defined;
    if (*ScrField_burst_notify_IsDefined(&defined, ent)) play_burst(ent);
}

void XB_CDECL BroScr_StartLoopingFx() {
    DynArray ents = {nullptr, 0, 0};
    uint32_t* const array = reinterpret_cast<uint32_t*>(&ents);
    Scr_WaitFrames(0);
    ScriptString name;
    scr_string_from(&name, 0x008253f4);  // "_fx_looping"
    Scr_GetEntArray(address_of(&name), 0x0d786b4c, reinterpret_cast<uint8_t*>(&ents), 0);
    scr_release(&name);
    using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, const uint32_t*,
                                          const uint32_t*, const uint32_t*, const uint32_t*, const uint32_t*);
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ents.size); ++i) {
        bool defined;
        if (*ScrField_script_fxid_IsDefined(&defined, *BroScr_ArrayElement(array, i))) {
            // A BroScr_FxCreationSoundThink functor {0x00824458, thread, ent, sound, to, h, f, h, f}.
            uint32_t to[3];
            uint32_t h1, h2, f1, f2;
            uint32_t ent;
            uint32_t sound;
            if (has_target(array, i)) {
                const uint32_t target = target_ent(array, i);
                uint32_t buffer[3];
                const uint32_t* o = origin_of(buffer, target);
                to[0] = o[0];
                h1 = h2 = undefined_handle();
                to[1] = o[1];
                to[2] = o[2];
                f1 = f2 = undefined_float();
                const ScriptString* const fxid = ScrField_script_fxid(*BroScr_ArrayElement(array, i));
                ent = *BroScr_ArrayElement(array, i);
                hash_of(&sound, fxid);
            } else {
                h1 = h2 = undefined_handle();
                f1 = f2 = undefined_float();
                to[0] = to[1] = to[2] = undefined_lane();
                const ScriptString* const fxid = ScrField_script_fxid(*BroScr_ArrayElement(array, i));
                ent = *BroScr_ArrayElement(array, i);
                hash_of(&sound, fxid);
            }
            uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x2c);  // ScrIface_Alloc
            if (functor) {
                functor[0] = 0x00824458;
                functor[1] = 0x0015f720;  // BroScr_FxCreationSoundThink
                functor[2] = ent;
                functor[3] = sound;
                functor[4] = to[0];
                functor[5] = to[1];
                functor[6] = to[2];
                functor[7] = h2;
                functor[8] = f1;
                functor[9] = h1;
                functor[10] = f2;
            }
            create_thread(ent, functor);
            continue;
        }
        if (!*ScrField_he5d3774f_IsDefined(&defined, *BroScr_ArrayElement(array, i))) continue;
        uint32_t to[3];
        uint32_t h1, h2, f1, f2;
        if (has_target(array, i)) {
            const uint32_t target = target_ent(array, i);
            uint32_t buffer[3];
            const uint32_t* o = origin_of(buffer, target);
            to[0] = o[0];
            to[1] = o[1];
            to[2] = o[2];
            slot<void XB_CDECL(uint32_t)>(0x00922540)(target);  // GScr_Delete
        } else {
            to[0] = to[1] = to[2] = undefined_lane();
        }
        h1 = h2 = undefined_handle();
        f1 = f2 = undefined_float();
        const ScriptString* const sound_name = ScrField_he5d3774f(*BroScr_ArrayElement(array, i));
        uint32_t* const element = BroScr_ArrayElement(array, i);
        uint32_t sound;
        hash_of(&sound, sound_name);
        title_function<StartThread>(0x0017b050)(0, 0, 0, 0, 0x0015f720, *element, &sound, to, &h2, &f1, &h1,
                                                &f2);  // Scr_StartThread_ent_hash_vec_hash_f_hash_f(BroScr_FxCreationSoundThink)
    }
    if (ents.data) Scr_Free(ents.data);
}

void XB_CDECL BroScr_SpawnFxCreationOrigin(uint32_t, uint32_t sound, ScrVec3 origin, ScrVec3 to, uint32_t delay) {
    fx_creation_sound(spawn_fx_creation_origin(&origin), sound, origin, to, delay);
}

void XB_CDECL BroScr_SpawnFxCreationOrigin_2(uint32_t, uint32_t sound, ScrVec3 origin, ScrVec3 to, uint32_t a, uint32_t b,
                                             uint32_t c, uint32_t d) {
    fx_creation_sound_think(spawn_fx_creation_origin(&origin), sound, to, a, b, c, d);
}

void XB_CDECL BroScr_FxCreationSoundCall(uint32_t ent, uint32_t sound) {
    const uint32_t delay = undefined_float();
    fx_creation_sound(ent, sound, undefined_vec(), undefined_vec(), delay);
}

void XB_CDECL BroScr_FxCreationSoundCall_2(uint32_t ent, uint32_t sound, ScrVec3 to) {
    fx_creation_sound(ent, sound, undefined_vec(), to, undefined_float());
}

void XB_CDECL BroScr_FxCreationSoundCall_3(uint32_t ent, uint32_t sound, ScrVec3 to, uint32_t delay) {
    fx_creation_sound(ent, sound, undefined_vec(), to, delay);
}

void XB_CDECL BroScr_FxCreationSoundCall_4(uint32_t ent, uint32_t sound, uint32_t at, uint32_t delay) {
    uint32_t buffer[3];
    const uint32_t* o = origin_of(buffer, at);
    const ScrVec3 to = {{o[0], o[1], o[2]}};
    fx_creation_sound(ent, sound, undefined_vec(), to, delay);
}

void XB_CDECL BroScr_FxCreationSoundThinkCall(uint32_t ent, uint32_t sound) {
    const uint32_t f = undefined_float();
    const uint32_t h = undefined_handle();
    fx_creation_sound_think(ent, sound, undefined_vec(), title_global<uint32_t>(0x00922e88), f, h, f);
}

void XB_CDECL BroScr_FxCreationSoundThinkCall_2(uint32_t ent, uint32_t sound, ScrVec3 to) {
    const uint32_t f = undefined_float();
    const uint32_t h = undefined_handle();
    fx_creation_sound_think(ent, sound, to, h, f, h, f);
}

void XB_CDECL BroScr_FxCreationSoundThinkCall_3(uint32_t ent, uint32_t sound, ScrVec3 to, uint32_t a) {
    const uint32_t f = undefined_float();
    fx_creation_sound_think(ent, sound, to, a, f, undefined_handle(), f);
}

void XB_CDECL BroScr_FxCreationSoundThinkCall_4(uint32_t ent, uint32_t sound, ScrVec3 to, uint32_t a, uint32_t b) {
    fx_creation_sound_think(ent, sound, to, a, b, undefined_handle(), undefined_float());
}

void XB_CDECL BroScr_SpawnFxCreationOriginCall(uint32_t, uint32_t sound, ScrVec3 origin) {
    const uint32_t level = title_global<uint32_t>(0x00922ee0);
    const uint32_t first = *pointer<uint32_t>(level ? level + 4 : 0);
    BroScr_SpawnFxCreationOrigin(first, sound, origin, undefined_vec(), undefined_float());
}

void XB_CDECL BroScr_SpawnFxCreationOriginCall_2(uint32_t unused, uint32_t sound, ScrVec3 origin, ScrVec3 to) {
    BroScr_SpawnFxCreationOrigin(unused, sound, origin, to, undefined_float());
}

void XB_CDECL BroScr_SpawnFxCreationOrigin_2Call(uint32_t unused, uint32_t sound, ScrVec3 origin) {
    const uint32_t f = undefined_float();
    const uint32_t h = undefined_handle();
    BroScr_SpawnFxCreationOrigin_2(unused, sound, origin, undefined_vec(), h, f, h, f);
}

void XB_CDECL BroScr_SpawnFxCreationOrigin_2Call_2(uint32_t unused, uint32_t sound, ScrVec3 origin, ScrVec3 to) {
    const uint32_t f = undefined_float();
    const uint32_t h = undefined_handle();
    BroScr_SpawnFxCreationOrigin_2(unused, sound, origin, to, h, f, h, f);
}

void XB_CDECL BroScr_SpawnFxCreationOrigin_2Call_3(uint32_t unused, uint32_t sound, ScrVec3 origin, ScrVec3 to, uint32_t a) {
    const uint32_t f = undefined_float();
    BroScr_SpawnFxCreationOrigin_2(unused, sound, origin, to, a, f, undefined_handle(), f);
}

void XB_CDECL BroScr_SpawnFxCreationOrigin_2Call_4(uint32_t unused, uint32_t sound, ScrVec3 origin, uint32_t a) {
    const uint32_t f = undefined_float();
    BroScr_SpawnFxCreationOrigin_2(unused, sound, origin, undefined_vec(), a, f, undefined_handle(), f);
}

void XB_CDECL BroScr_SpawnFxCreationOrigin_2Call_5(uint32_t unused, uint32_t sound, ScrVec3 origin, ScrVec3 to, uint32_t a,
                                                  uint32_t b) {
    BroScr_SpawnFxCreationOrigin_2(unused, sound, origin, to, a, b, undefined_handle(), undefined_float());
}

}  // namespace game
