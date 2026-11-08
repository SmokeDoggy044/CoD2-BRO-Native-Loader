#include "bro_scr_stuka_bomb.h"

#include "bro_scr_ai_common.h"
#include "bro_scr_common.h"
#include "bro_scr_drones.h"
#include "g_script.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "g_scr_runtime.h"
#include "scr_access.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "script_string.h"
#include "title_data.h"
#include "levels_templates.h"
#include "levels_twins.h"
#include "bro_scr_death_watch.h"
#include "bro_scr_level_threads.h"

namespace game {

using namespace bro_ai;

namespace {

using GetVec = uint32_t* XB_CDECL(uint32_t*, uint32_t);

inline uint8_t* level() { return title_global<uint8_t*>(0x00922ee0); }
inline ScrVec3 origin_of(uint32_t ent) {
    uint32_t buffer[3];
    const uint32_t* v = slot<GetVec>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
    ScrVec3 out;
    out.v[0] = v[0];
    out.v[1] = v[1];
    out.v[2] = v[2];
    return out;
}
// The level float at `offset`, `fallback` (a title constant) when undefined.
inline uint32_t level_float_or(uint32_t offset, uint32_t fallback) {
    const uint32_t value = *reinterpret_cast<uint32_t*>(level() + offset);
    return as_float(value) == as_float(undefined_float()) ? title_global<uint32_t>(fallback) : value;
}

// The vector field `key` (a Scr_Alloc'd vec3 of undefined lanes added when missing).
inline uint32_t* vector_field(uint32_t ent, uint32_t key) {
    ScriptKeyTable* const table = ent_script_object(ent);
    uint32_t* value = ScriptKeyTable_Find(table, key);
    if (!value) {
        const uint32_t lane = undefined_lane();
        const uint32_t init[3] = {lane, lane, lane};
        uint32_t* const v = Scr_Alloc(0xc, 4);
        uint32_t* stored = nullptr;
        if (v) {
            v[0] = init[0];
            v[1] = init[1];
            v[2] = init[2];
            stored = v;
        }
        value = ScriptKeyTable_Set(table, key, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(stored)));
    }
    return pointer<uint32_t>(*value);
}
inline uint32_t hash_of(const ScriptString* s) {
    uint32_t hash;
    return *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(s));
}

}  // namespace

void XB_CDECL BroScr_StukaBombImpact(uint32_t bomb, uint32_t plane, uint32_t delay, uint32_t range, uint32_t max_damage,
                                     uint32_t min_damage, uint32_t height) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(bomb, 0x06730ae8);  // Scr_EndOn(death)
    const float undefined = as_float(undefined_float());
    Scr_Wait(as_float(delay) == undefined ? 0.5f : as_float(delay));
    const float min_fraction =
        as_float(height) == undefined ? title_global<float>(0x0082545c) : as_float(height) * title_global<float>(0x0083ed50);
    const uint32_t lane = undefined_lane();
    uint32_t start[3] = {lane, lane, lane};
    uint32_t end[3] = {lane, lane, lane};
    struct Trace {
        uint32_t fraction;
        uint32_t position[3];
        uint32_t hit;
        uint32_t normal[3];
        ScriptString surface;
    } trace;
    trace.position[0] = lane;
    trace.position[1] = lane;
    trace.position[2] = lane;
    trace.hit = 0;
    trace.normal[0] = lane;
    trace.normal[1] = lane;
    trace.normal[2] = lane;
    ScriptString_FromRep(&trace.surface, nullptr);
    float last = title_global<float>(0x0083eba8);  // 2
    ScriptString last_surface;
    ScriptString_FromRep(&last_surface, nullptr);
    {
        ScriptString targetname;
        scr_string_from(&targetname, 0x00825450);  // "_stuka_bomb"
        ScriptString copy;
        ScriptString_Copy(&copy, &targetname);
        slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922b54)(bomb, copy);  // ScrEnt_SetTargetname
        scr_release(&targetname);
    }
    ScriptString name;
    {
        ScriptString number;
        ScriptString_ConcatCString(&name,
                                   ScriptString_FromCStringPlusInt(&number, pointer<const char>(0x008079d4),
                                                                   static_cast<int32_t>(*int_slot(bomb, 0x380a75c4))),
                                   pointer<const char>(0x00825444));  // "bomb" n "_explosion"
        scr_release(&number);
    }
    for (;;) {
        const ScrVec3 o = origin_of(bomb);
        start[0] = o.v[0];
        start[1] = o.v[1];
        start[2] = o.v[2];
        end[0] = o.v[0];
        end[1] = o.v[1];
        end[2] = bits(as_float(o.v[2]) - title_global<float>(0x0083f010));  // 10000 down
        slot<void XB_CDECL(Trace*, uint32_t*, uint32_t*, uint32_t, uint32_t)>(0x00922414)(&trace, start, end, 0,
                                                                                        bomb);  // GScr_LocationalTraceSurface
        {
            const uint32_t p[3] = {trace.position[0], trace.position[1], trace.position[2]};
            uint32_t* const field = vector_field(bomb, 0xb9766e80);
            field[0] = p[0];
            field[1] = p[1];
            field[2] = p[2];
        }
        if (entity_handle_is_valid(plane) && trace.hit == plane) {
            Scr_Wait(as_float(0x3d4ccccd));  // 0.05
            continue;
        }
        bool named = false;
        if (entity_handle_is_valid(trace.hit)) {
            ScriptString temp;
            named = ScrCall_ScrEnt_GetTargetname(&trace.hit, &temp)->rep != nullptr;
            scr_release(&temp);
        }
        if (named) {
            ScriptString temp;
            const bool skip = hash_of(ScrCall_ScrEnt_GetTargetname(&trace.hit, &temp)) == 0x06b2a670;
            scr_release(&temp);
            if (skip) {
                Scr_Wait(as_float(0x3d4ccccd));
                continue;
            }
        }
        const float fraction = as_float(trace.fraction);
        const float one = title_global<float>(0x0083eb20);
        bool explode = false;
        if (min_fraction > fraction) {
            explode = true;
        } else if (!(fraction > last)) {
            if (fraction == one) {
                Scr_Wait(as_float(0x3e800000));  // 0.25
                continue;
            }
        } else if (fraction > last + title_global<float>(0x0083eb24) || fraction == one) {  // + 0.5
            explode = true;
        } else if (ScriptString_Equals(&trace.surface, pointer<const char>(0x00831f70)) &&  // "default"
                   !ScriptString_EqualsString(&trace.surface, &last_surface)) {
            explode = true;
        }
        if (explode) break;
        ScriptString_Assign(&last_surface, &trace.surface);
        last = fraction;
        Scr_Wait(as_float(0x3d4ccccd));
    }
    queue_notify(plane, hash_of(&name));
    const uint32_t object = title_global<uint32_t>(0x00922ee0);
    const uint32_t* const handle = pointer<uint32_t>(object ? object + 4 : 0);
    BroScr_StukaBombExplode(*handle, bomb, static_cast<int32_t>(range), static_cast<int32_t>(max_damage),
                            static_cast<int32_t>(min_damage));
    scr_release(&name);
    scr_release(&last_surface);
    scr_release(&trace.surface);
}

namespace {

inline DynArray* plane_bombs(uint32_t plane) {
    return own_function<DynArray* XB_THISCALL(ScriptKeyTable*, uint32_t)>(&ScriptKeyTable_GetArray)(ent_script_object(plane),
                                                                                       0x0030b1c2);  // ScriptKeyTable_GetArray
}
inline uint32_t bomb_at(uint32_t plane, uint32_t i) {
    DynArray* const bombs = plane_bombs(plane);
    if (i >= bombs->size) DynArray_Resize(reinterpret_cast<uint8_t*>(bombs), i + 1, i + 4);
    return bombs->data[i];
}
inline void start_functor1(uint32_t function, uint32_t ent) {
    title_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x0011a910)(
        0, 0, 0, 0, function, ent);  // Scr_StartThreadFunctor1
}

}  // namespace

void XB_CDECL BroScr_PlaneDropBomb(uint32_t plane, uint32_t count, uint32_t interval, uint32_t delay, uint32_t range,
                                   uint32_t max_damage, uint32_t min_damage, uint32_t height, uint32_t whistle) {
    if (*int_slot(plane, 0x380a75c4) == 0) return;  // bomb_count
    uint32_t wait = undefined_float();
    if (as_float(interval) != as_float(undefined_float())) wait = interval;
    if (count == undefined_int()) {
        count = *ScrField_bomb_count(plane);
    } else if (static_cast<int32_t>(count) > static_cast<int32_t>(*ScrField_bomb_count(plane))) {
        count = *ScrField_bomb_count(plane);
    } else if (static_cast<int32_t>(count) <= 0) {
        return;
    }
    for (uint32_t i = 0;; ++i) {
        if (!(static_cast<int32_t>(i) < static_cast<int32_t>(plane_bombs(plane)->size))) return;
        if (static_cast<int32_t>(count) <= 0) return;
        if (!entity_handle_is_valid(bomb_at(plane, i))) continue;
        if (*ScrField_dropped(bomb_at(plane, i)) != 0) continue;
        const uint32_t lane = undefined_lane();
        uint32_t velocity[3] = {lane, lane, lane};
        uint32_t angles[3];
        uint32_t forward_buffer[3];
        const uint32_t* const f = ScrCall_Scr_AnglesToForward(
            forward_buffer, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ScrCall_ScrEnt_GetAngles(&plane, angles))));
        const uint32_t forward[3] = {f[0], f[1], f[2]};
        const float speed = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922b38)(plane));  // vehicle speed
        const float x = static_cast<float>(static_cast<long double>(as_float(forward[0])) * speed);
        const float z = as_float(forward[2]) * speed;
        const float y = as_float(forward[1]) * speed;
        velocity[0] = bits(x);
        velocity[1] = bits(y);
        velocity[2] = bits(z);
        *ScrField_bomb_count(bomb_at(plane, i)) = i + 1;
        slot<void XB_CDECL(uint32_t)>(0x0092250c)(bomb_at(plane, i));  // GScr_Unlink
        if (static_cast<uint8_t>(whistle)) {
            ScriptString sound;
            scr_string_from(&sound, 0x007f765c);  // "Bomb_Whistle"
            const uint32_t bomb = *own_function<uint32_t* XB_THISCALL(DynArray*, uint32_t)>(&DynArray_At_PROLOGUE)(
                static_cast<DynArray*>(ScrField_bomb(plane)), i);  // DynArray_At
            slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t)>(0x009221dc)(bomb, &sound, 0, 0,
                                                                                                  0);  // GScr_PlaySound
            scr_release(&sound);
        }
        slot<void XB_CDECL(uint32_t, uint32_t*, uint32_t)>(0x009227a4)(bomb_at(plane, i), velocity,
                                                                     0x40a00000);  // GScr_MoverMoveGravity(5 s)
        start_functor1(own_address(&BroScr_DeleteAfter5s), bomb_at(plane, i));  // BroScr_DeleteAfter5s
        start_functor1(own_address(&BroScr_RandomRotateLoop), bomb_at(plane, i));  // BroScr_RandomRotateLoop
        {
            using Start = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*,
                                            uint32_t*, uint32_t*, uint32_t*, uint32_t*, uint32_t*);
            const uint32_t bomb = bomb_at(plane, i);
            title_function<Start>(0x0017b2f0)(0, 0, 0, 0, own_address(&BroScr_StukaBombImpact), bomb, &plane, &delay, &range, &max_damage,
                                              &min_damage, &height);  // Scr_StartThread_ent_ent_f_i_i_i_f(StukaBombImpact)
        }
        uint32_t pause = interval;
        if (as_float(wait) == as_float(undefined_float())) {
            pause = bits(static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922230)(0x3f000000) +
                                            title_global<float>(0x0083ec24)));  // RandomFloat(0.5) + 0.25
            interval = pause;
        } else {
            pause = wait;
        }
        --count;
        --*ScrField_bomb_count(plane);
        *ScrField_dropped(bomb_at(plane, i)) = 1;
        Scr_Wait(as_float(pause));
    }
}

void XB_CDECL BroScr_StukaBombExplode(uint32_t, uint32_t bomb, int32_t range, int32_t max_damage, int32_t min_damage) {
    if (!*reinterpret_cast<uint32_t*>(level() + 0x264)) return;
    const uint32_t scale = level_float_or(0x630, 0x0083eb24);     // 0.5
    const uint32_t duration = level_float_or(0x268, 0x0083eba8);  // 2
    const uint32_t radius = level_float_or(0x2a0, 0x0083eff8);    // 1024
    const int32_t undefined = static_cast<int32_t>(undefined_int());
    if (range == undefined) range = 0x300;
    if (max_damage == undefined) max_damage = 0x190;
    if (min_damage == undefined) min_damage = 0x19;
    uint32_t origin_ent;
    {
        ScriptString classname;
        scr_string_from(&classname, 0x0083ab88);  // "script_origin"
        ScrVec3 at = origin_of(bomb);
        origin_ent = slot<uint32_t XB_CDECL(ScriptString*, ScrVec3*, uint32_t)>(0x009222f0)(&classname, &at, 0);  // GScr_Spawn
        scr_release(&classname);
    }
    bool play = true;
    if (ScriptString_Equals(reinterpret_cast<ScriptString*>(level() + 0x1c4), pointer<const char>(0x0082bc28))) {  // "piano_lupo"
        const ScrVec3 player = origin_of(title_global<uint32_t>(0x00922eec));
        const ScrVec3 at = origin_of(bomb);
        const long double distance =
            slot<long double XB_CDECL(const ScrVec3*, const ScrVec3*)>(0x00922268)(&at, &player);  // Scr_Distance
        play = distance > static_cast<long double>(title_global<float>(0x007f6180));  // 250
    }
    if (play)
        slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009221dc)(
            origin_ent, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(level() + 0x264)), 0x45727144, 0,
            0);  // GScr_PlaySound
    slot<void XB_CDECL(uint32_t)>(0x00922540)(bomb);  // GScr_Delete
    {
        ScrVec3 at = origin_of(origin_ent);
        slot<void XB_CDECL(uint32_t, uint32_t, ScrVec3*, uint32_t)>(0x00922444)(scale, duration, &at, radius);  // GScr_Earthquake
    }
    {
        ScrVec3 at = origin_of(origin_ent);
        const float min_f = static_cast<float>(min_damage);
        const float max_f = static_cast<float>(max_damage);
        const float range_f = static_cast<float>(range);
        slot<void XB_CDECL(ScrVec3*, float, float, float)>(0x00922428)(&at, range_f, max_f, min_f);  // GScr_RadiusDamage
    }
    const uint32_t object = title_global<uint32_t>(0x00922ee0);
    queue_notify(*pointer<uint32_t>(object ? object + 4 : 0), 0xa581b0e9);
    BroScr_StukaStartNearbyDrones(origin_ent);
    Scr_WaitTill(origin_ent, 0x45727144);
}

}  // namespace game
