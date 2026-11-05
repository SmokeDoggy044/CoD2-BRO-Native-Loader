#include "bro_scr_drone_threads.h"

#include "bro_scr_ai_common.h"
#include "bro_scr_common.h"
#include "bro_scr_death_watch.h"
#include "bro_scr_drones.h"
#include "bro_scr_util.h"
#include "g_scr_runtime.h"
#include "g_script.h"
#include "qcommon_core.h"
#include "scr_access.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "script_string.h"
#include "title_data.h"

namespace game {

using namespace bro_ai;

namespace {

constexpr uint32_t kDeath = 0x06730ae8;

using Index = uint32_t XB_THISCALL(uint32_t, const uint32_t*);
using ArrayIndex = DynArray* XB_THISCALL(uint32_t, const uint32_t*);
using Reserve = void XB_THISCALL(DynArray*, uint32_t, uint32_t);
using GetVec = uint32_t* XB_CDECL(uint32_t*, uint32_t);
using GetString = ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t);

inline uint32_t level() { return title_global<uint32_t>(0x00922ee0); }
inline uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
inline void end_on(uint32_t ent, uint32_t hash) { slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, hash); }  // Scr_EndOn
inline long double random_float(uint32_t limit) { return slot<long double XB_CDECL(uint32_t)>(0x00922230)(limit); }  // Scr_RandomFloat (ST0)
inline uint32_t word_ea4() { return title_global<uint32_t>(0x00922ea4); }

// The animname's hash (the temporary released at once).
inline uint32_t animname_hash(uint32_t* ent) {
    ScriptString animname;
    uint32_t hash;
    Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(ScrCall_ScrEnt_GetAnimname(ent, &animname)));
    scr_release(&animname);
    return hash;
}
// The anim array of set `key` for the animname `hash` (level + 0x744, HashMap_Index_10 then HashMap_Index).
inline DynArray* anim_set(const uint32_t* hash, const uint32_t* key) {
    return title_function<ArrayIndex>(0x004612f0)(title_function<Index>(0x00464380)(level() + 0x744, hash), key);
}
// The rate array of set `key` (level + 0x2a8, the KASSPASS-section maps).
inline DynArray* rate_set(const uint32_t* hash, const uint32_t* key) {
    return title_function<ArrayIndex>(0x0052f830)(title_function<Index>(0x00530090)(level() + 0x2a8, hash), key);
}
inline uint32_t* int_at(DynArray* array, uint32_t i) {
    if (i >= array->size) title_function<Reserve>(0x0045d480)(array, i + 1, i + 4);  // DynArray_ReserveInt0
    return array->data + i;
}
inline uint32_t* float_at(DynArray* array, uint32_t i) {
    if (i >= array->size) title_function<Reserve>(0x0052d530)(array, i + 1, i + 4);  // DynArray_Reserve_KASSPASS
    return array->data + i;
}
inline uint32_t* path_at(DynArray* paths, uint32_t i) {
    if (i >= paths->size) DynArray_Resize(reinterpret_cast<uint8_t*>(paths), i + 1, i + 4);
    return paths->data + i;
}
// A copy of ScrEnt_GetOrigin / ScrEnt_GetAngles (`address`).
inline ScrVec3 get_vector(uint32_t address, uint32_t ent) {
    uint32_t buffer[3];
    const uint32_t* v = slot<GetVec>(address)(buffer, ent);
    return {{v[0], v[1], v[2]}};
}
inline bool defined_float(uint32_t value) { return as_float(value) != as_float(undefined_float()); }
inline ScriptString* drone_gun(uint32_t drone) {
    return title_function<GetString>(0x00458da0)(ent_script_object(drone), 0x1a7e7545);  // ScriptKeyTable_GetString
}
// GScr_Attach(drone, gun, "tag_weapon_right"); `gun` is read after the tag string is built.
inline void attach_gun_inline(uint32_t drone) {
    ScriptString tag;
    scr_string_from(&tag, 0x00831e14);  // "tag_weapon_right"
    slot<void XB_CDECL(uint32_t, ScriptString*, ScriptString*)>(0x009224d8)(drone, drone_gun(drone), &tag);
    scr_release(&tag);
}
// A BroScr_AttachDroneGun thread (level, drone, gun, direction): Scr_StartThread_ent_ent_str_vec.
inline void fling_gun(uint32_t* drone, ScriptString* gun, const uint32_t* direction, uint32_t level_handle) {
    using Start = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, ScriptString*,
                                    const uint32_t*);
    title_function<Start>(0x0017ae80)(0, 0, 0, 0, 0x0012f6c0, level_handle, drone, gun, direction);
}
inline const uint32_t* level_handle_ptr() {
    const uint32_t object = level();
    return pointer<uint32_t>(object ? object + 4 : 0);
}
// direction * (RandomFloat(range) + base), x on the x87 as the original.
inline void scale_direction(uint32_t* v, uint32_t range, float base) {
    const float distance = static_cast<float>(random_float(range) + base);
    const float y = as_float(v[1]) * distance;
    const float z = as_float(v[2]) * distance;
    v[0] = bits(static_cast<float>(static_cast<long double>(distance) * as_float(v[0])));
    v[1] = bits(y);
    v[2] = bits(z);
}

// A BroScr_DronePathSpawnLoop thread on (spawner, path, type, p6..p11): Scr_StartThread_ent_ent_str_i_f_f_f_f_b.
inline void start_spawn_loop(uint32_t spawner, uint32_t* path, ScriptString* type, uint32_t* p6, uint32_t* p7,
                             uint32_t* p8, uint32_t* p9, uint32_t* p10, uint32_t* p11) {
    using Start = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, ScriptString*,
                                    uint32_t*, uint32_t*, uint32_t*, uint32_t*, uint32_t*, uint32_t*);
    title_function<Start>(0x0052c7f0)(0, 0, 0, 0, 0x001706b0, spawner, path, type, p6, p7, p8, p9, p10,
                                      p11);  // (BroScr_DronePathSpawnLoop)
}
// The path's start field 0x6bd99abb = 0 (added as 0) and its bool 0x19738af8 = false.
inline void reset_path(DynArray* paths, uint32_t i) {
    *key_slot(*path_at(paths, i), 0x6bd99abb, 0) = 0;
    *reinterpret_cast<uint8_t*>(key_slot(*path_at(paths, i), 0x19738af8, title_global<uint8_t>(0x00922ed0))) = 0;
}
// p_min + RandomFloat(p_max) when both are defined, else `fallback` seconds.
inline void random_delay(uint32_t p_min, uint32_t p_max, uint32_t fallback) {
    if (defined_float(p_min) && defined_float(p_max)) {
        const long double random = random_float(p_max);
        Scr_WaitExact(static_cast<float>(as_float(p_min) + random));
    } else {
        Scr_Wait(as_float(fallback));
    }
}

}  // namespace

void XB_CDECL BroScr_DroneAnimLoop(uint32_t drone) {
    end_on(drone, 0xf9ed2681);
    end_on(drone, kDeath);
    if (!entity_handle_is_valid(drone)) return;
    const uint32_t hash = animname_hash(&drone);
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009225f0)(
        drone, title_function<Index>(0x00461120)(level() + 0x238, &hash));  // GScr_UseAnimTree(HashMap_Index_11)
    for (;;) {
        const uint32_t key = 0x6e28adc5;
        const uint32_t i = slot<uint32_t XB_CDECL(uint32_t)>(0x0092222c)(anim_set(&hash, &key)->size);  // Scr_RandomInt
        const uint32_t rate = *float_at(rate_set(&hash, &key), i);
        if (as_float(rate) != as_float(undefined_float())) {
            const uint32_t* const value = float_at(rate_set(&hash, &key), i);
            *key_slot(drone, 0x2297b461, undefined_float()) = *value;
        } else {
            *key_slot(drone, 0x2297b461, undefined_float()) = word_ea4();
        }
        uint32_t root;
        {
            using Find = uint32_t* XB_THISCALL(uint32_t, uint32_t*, const uint32_t*);
            const uint32_t map = level() + 0x7c8;
            uint32_t it;
            title_function<Find>(0x0045e6d0)(map, &it, &hash);  // HashMap_Find
            if (it == pointer<uint32_t>(map)[2]) {
                const uint32_t pair[2] = {hash, undefined_int()};
                uint32_t inserted;
                it = *title_function<Find>(0x004608a0)(map, &inserted, pair);  // HashMap_Insert
            }
            root = pointer<uint32_t>(it)[3];
        }
        const uint32_t anim = *int_at(anim_set(&hash, &key), i);
        slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009225e4)(
            drone, 0x4fcdc42e, anim, root, 0x3f800000, 0x3e4ccccd, 0x3f800000, 1);  // GScr_SetFlaggedAnimKnobAll
        Scr_WaitTillArgs(drone, 0x4fcdc42e, 0x000199a4, 0, 0);
    }
}

void XB_CDECL BroScr_DroneMain(uint32_t drone, uint32_t hide, uint32_t explode) {
    // The float literals of the title's constant pool.
    const float zero = title_global<float>(0x0083eafc);
    const float one = title_global<float>(0x0083eb20);
    queue_notify(drone, 0xf9ed2681);
    slot<void XB_CDECL(uint32_t)>(0x0092250c)(drone);  // GScr_Unlink
    PtrArray_RemoveValue(pointer<DynArray>(level() + 0x770), drone);
    const uint8_t gone = slot<uint32_t XB_CDECL(uint32_t)>(0x00922358)(drone) ? static_cast<uint8_t>(hide) : 1;  // GScr_HasVisibleModel
    if (gone != title_global<uint8_t>(0x00922ed0) && gone) {
        attach_gun_inline(drone);
        if (entity_handle_is_valid(drone)) {
            slot<void XB_CDECL(uint32_t)>(0x00922540)(drone);  // GScr_Delete
            --*pointer<int32_t>(level() + 0x508);
        }
        return;
    }
    const uint32_t hash = animname_hash(&drone);
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009225f0)(
        drone, title_function<Index>(0x00461120)(level() + 0x238, &hash));  // GScr_UseAnimTree(HashMap_Index_11)
    bool near = true;
    {
        const float limit = title_global<float>(0x007f51cc);  // 512 * 512
        const ScrVec3 at = get_vector(0x00922b20, drone);
        const ScrVec3 player = get_vector(0x00922b20, title_global<uint32_t>(0x00922eec));
        const float d2 = static_cast<float>(
            slot<long double XB_CDECL(const ScrVec3*, const ScrVec3*)>(0x0092226c)(&player, &at));  // Scr_DistanceSquared
        if (static_cast<float>(d2 > limit ? 1 : 0) != zero) near = false;
    }
    const uint32_t lane = undefined_lane();
    uint32_t direction[3] = {lane, lane, lane};
    uint32_t key;
    if (static_cast<uint8_t>(explode) != title_global<uint8_t>(0x00922ed0) && static_cast<uint8_t>(explode)) {
        if (near) {
            const int32_t pitch = slot<int32_t XB_CDECL(uint32_t)>(0x0092222c)(60);  // Scr_RandomInt
            const int32_t yaw = slot<int32_t XB_CDECL(uint32_t)>(0x0092222c)(360);
            direction[0] = bits(static_cast<float>(-30 - pitch));
            direction[1] = bits(static_cast<float>(yaw));
            direction[2] = bits(0.0f);
            uint32_t buffer[3];
            const uint32_t* f = ScrCall_Scr_AnglesToForward(buffer, address_of(direction));
            direction[0] = f[0];
            direction[1] = f[1];
            direction[2] = f[2];
            scale_direction(direction, 0x443b8000, title_global<float>(0x0083ef08));  // RandomFloat(750) + 200
            const uint32_t* const handle = level_handle_ptr();
            fling_gun(&drone, ScrField_drone_gun(drone), direction, *handle);
        } else {
            ScriptString tag;
            scr_string_from(&tag, 0x00831e14);  // "tag_weapon_right"
            slot<void XB_CDECL(uint32_t, ScriptString*, ScriptString*)>(0x009224d8)(drone, ScrField_drone_gun(drone),
                                                                                  &tag);  // GScr_Attach
            scr_release(&tag);
        }
        key = 0x0fc1193c;
    } else {
        if (near) {
            const int32_t pitch = slot<int32_t XB_CDECL(uint32_t)>(0x0092222c)(60);  // Scr_RandomInt
            const int32_t yaw = slot<int32_t XB_CDECL(uint32_t)>(0x0092222c)(30);
            const float yaw_offset = static_cast<float>(yaw - 15);
            const float pitch_offset = static_cast<float>(-30 - pitch);
            const ScrVec3 angles = get_vector(0x00922ba0, drone);  // ScrEnt_GetAngles
            uint32_t turned[3] = {bits(as_float(angles.v[0]) + pitch_offset), bits(as_float(angles.v[1]) + yaw_offset),
                                  angles.v[2]};
            uint32_t forward[3] = {lane, lane, lane};
            slot<void XB_CDECL(uint32_t*, const uint32_t*)>(0x00922290)(forward, turned);  // Scr_AnglesToForward
            direction[0] = forward[0];
            direction[1] = forward[1];
            direction[2] = forward[2];
            scale_direction(direction, 0x43960000, title_global<float>(0x0083ec30));  // RandomFloat(300) + 50
            const uint32_t* const handle = level_handle_ptr();
            ScriptString* const gun = drone_gun(drone);
            fling_gun(&drone, gun, direction, *handle);
        } else {
            attach_gun_inline(drone);
        }
        key = 0xd5cdd358;
    }
    {
        const uint32_t i = slot<uint32_t XB_CDECL(uint32_t)>(0x0092222c)(anim_set(&hash, &key)->size);  // Scr_RandomInt
        const uint32_t anim = *int_at(anim_set(&hash, &key), i);
        ScrVec3 angles = get_vector(0x00922ba0, drone);  // ScrEnt_GetAngles
        ScrVec3 origin = get_vector(0x00922b20, drone);  // ScrEnt_GetOrigin
        slot<void XB_CDECL(uint32_t, uint32_t, ScrVec3*, ScrVec3*, uint32_t)>(0x009224c0)(drone, 0x0632ef00, &origin,
                                                                                         &angles, anim);  // GScr_AnimScriptedNoNotify
    }
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x00135630)(drone, 0x0632ef00,
                                                                           0x000199a4);  // BroScr_WaitTillFinished
    // The trace result: fraction, position, an int, normal, surface name.
    struct Trace {
        uint32_t fraction;
        uint32_t position[3];
        uint32_t hit;
        uint32_t normal[3];
        ScriptString surface;
    } trace;
    const uint32_t u = undefined_lane();
    trace.position[0] = u;
    trace.position[1] = u;
    trace.position[2] = u;
    trace.hit = 0;
    trace.normal[0] = u;
    trace.normal[1] = u;
    trace.normal[2] = u;
    ScriptString_FromRep(&trace.surface, nullptr);
    {
        const ScrVec3 o = get_vector(0x00922b20, drone);
        uint32_t end[3] = {o.v[0], o.v[1], bits(as_float(o.v[2]) - title_global<float>(0x0083ebec))};
        const ScrVec3 o2 = get_vector(0x00922b20, drone);
        uint32_t start[3] = {o2.v[0], o2.v[1], bits(as_float(o2.v[2]) + title_global<float>(0x0083ed78))};
        slot<void XB_CDECL(Trace*, uint32_t*, uint32_t*, uint32_t, uint32_t)>(0x00922414)(
            &trace, start, end, 0, title_global<uint32_t>(0x00922e84));  // GScr_LocationalTraceSurface
    }
    const float fraction = as_float(trace.fraction);
    if (one > fraction && fraction > zero) {
        const ScrVec3 o = get_vector(0x00922b20, drone);
        const float drop = as_float(o.v[2]) - as_float(trace.position[2]);
        if (static_cast<float>(drop > title_global<float>(0x0083ed78) ? 1 : 0) != zero) {
            const float time = drop * title_global<float>(0x0083f0e4);
            const uint32_t target[3] = {trace.position[0], trace.position[1], trace.position[2]};
            uint32_t from[3];
            ScrCall_ScrEnt_GetOrigin(&drone, from);
            const float dz = as_float(target[2]) - as_float(from[2]);
            const float dy = as_float(target[1]) - as_float(from[1]);
            const float dx = as_float(target[0]) - as_float(from[0]);
            float elapsed = 0.0f;
            const float inverse = one / time;
            while (static_cast<float>(time > elapsed ? 1 : 0) != zero) {
                Scr_WaitFrames(0);
                elapsed = static_cast<float>(slot<long double XB_CDECL()>(0x009223c8)() + elapsed);  // GScr_GetFrameTime
                const float t = inverse * elapsed;
                const ScrVec3 at = {{bits(t * dx + as_float(from[0])), bits(t * dy + as_float(from[1])),
                                     bits(t * dz + as_float(from[2]))}};
                slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922b24)(drone, at);  // ScrEnt_SetOrigin
            }
        }
    }
    BroScr_DroneDeleteLater(drone);
    scr_release(&trace.surface);
}

namespace {

// RadiusDamage(300, 100, 100) and an Earthquake(0.45, 2, 1000) at the drone; `sound` then plays with
// ScrCall_GScr_PlaySound (`call` true) or the slot.
inline void blow_up(uint32_t* drone, bool call) {
    uint32_t buffer[3];
    slot<void XB_CDECL(uint32_t*, uint32_t, uint32_t, uint32_t)>(0x00922428)(ScrCall_ScrEnt_GetOrigin(drone, buffer),
                                                                           0x43960000, 0x42c80000,
                                                                           0x42c80000);  // GScr_RadiusDamage
    uint32_t buffer2[3];
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t*, uint32_t)>(0x00922444)(
        0x3ee66666, 0x40000000, ScrCall_ScrEnt_GetOrigin(drone, buffer2), 0x447a0000);  // GScr_Earthquake
    if (call)
        ScrCall_GScr_PlaySound(drone, level() + 0x6e8);
    else
        slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009221dc)(*drone, level() + 0x6e8, 0, 0,
                                                                                         0);  // GScr_PlaySound
    title_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x0011a910)(
        0, 0, 0, 0, 0x0016c9d0, *drone);  // Scr_StartThreadFunctor1(BroScr_StukaStartNearbyDrones)
}
// A BroScr_DroneMain(drone, *hide, *explode) thread (Scr_StartThread_ent_b_b).
inline void start_drone_main(uint32_t drone, uint8_t* hide, uint8_t* explode) {
    using Start = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint8_t*, uint8_t*);
    title_function<Start>(0x0045a970)(0, 0, 0, 0, 0x00169e50, drone, hide, explode);
}

}  // namespace

void XB_CDECL BroScr_DroneFollowSpline(uint32_t drone, uint32_t path, uint32_t flag) {
    end_on(drone, 0xf9ed2681);
    end_on(path, kDeath);
    *reinterpret_cast<uint8_t*>(key_slot(drone, 0x88260b0d, title_global<uint8_t>(0x00922ed0))) = 1;
    const uint32_t lane = undefined_lane();
    ScrVec3 at = {{lane, lane, lane}};
    const uint32_t* point = pointer<uint32_t>(*key_slot(path, 0x6bd99abb, 0));
    int32_t index = 0;
    if (*reinterpret_cast<uint8_t*>(key_slot(path, 0x19738af8, title_global<uint8_t>(0x00922ed0))) != 0 && point) {
        uint32_t event = 0;  // byte offset into the event / type lists
        for (;;) {
            if (point[0] == 0xffffffff && point[1] == 0xffffffff && point[2] == 0xffffffff) break;
            at = get_vector(0x00922b20, drone);  // ScrEnt_GetOrigin
            const float distance = static_cast<float>(
                slot<long double XB_CDECL(const ScrVec3*, const uint32_t*)>(0x00922268)(&at, point));  // Scr_Distance
            if (*key_slot(path, 0x384ad510, 0) != 0 && *pointer<int32_t>(*key_slot(path, 0x384ad510, 0)) != -1 &&
                index == *pointer<int32_t>(*key_slot(path, 0x384ad510, 0) + event)) {
                const uint32_t types = *key_slot(path, 0x095a5675, 0);
                uint8_t hide;
                uint8_t explode;
                bool start = true;
                if (*pointer<uint32_t>(types + event) == 0xdcc8dc52) {
                    blow_up(&drone, false);
                    start = false;
                } else if (*pointer<uint32_t>(*ScrField_h095a5675(path) + event) == 0xa07fa5bd) {
                    blow_up(&drone, false);
                    ScrField_h095a5675(path);
                    start = false;
                } else if (*pointer<uint32_t>(*ScrField_h095a5675(path) + event) == 0x18147c6e) {
                    if (slot<int32_t XB_CDECL(uint32_t)>(0x0092222c)(100) < 50) blow_up(&drone, true);  // Scr_RandomInt
                    start = false;
                } else if (*pointer<uint32_t>(*ScrField_h095a5675(path) + event) == 0x00019d45) {
                    explode = 0;
                    hide = 0;
                } else if (*pointer<uint32_t>(*ScrField_h095a5675(path) + event) == 0xcea3fa90) {
                    start = slot<int32_t XB_CDECL(uint32_t)>(0x0092222c)(100) < 50;
                    hide = 0;
                    explode = 0;
                } else if (*pointer<uint32_t>(*ScrField_h095a5675(path) + event) == 0xcea3fa75) {
                    start = slot<int32_t XB_CDECL(uint32_t)>(0x0092222c)(100) < 50;
                    hide = 0;
                    explode = 0;
                } else if (*pointer<uint32_t>(*ScrField_h095a5675(path) + event) == 0x0fc14a52) {
                    hide = 1;
                    explode = 0;
                } else {
                    start = false;
                }
                if (start) start_drone_main(drone, &hide, &explode);
                event += 4;
            }
            float time;
            bool defined;
            if (*ScrField_h2297b461_IsDefined(&defined, drone)) {
                time = distance / as_float(*key_slot(drone, 0x2297b461, undefined_float()));
            } else if (*ScrField_script_speed_IsDefined(&defined, path)) {
                time = distance / static_cast<float>(*reinterpret_cast<int32_t*>(ScrField_script_speed(path)));
            } else {
                time = distance * title_global<float>(0x0081c378);  // 1 / 175
            }
            if (0.0f >= time) {
                point += 3;
                continue;
            }
            ScrVec3 target = {{point[0], point[1], point[2]}};
            end_on(drone, 0xf9ed2681);
            slot<void XB_CDECL(uint32_t, ScrVec3*, float, uint32_t, uint32_t)>(0x00922794)(drone, &target, time, 0,
                                                                                         0);  // GScr_MoveTo
            const ScrVec3 to = {{point[0], point[1], point[2]}};
            BroScr_RotateToFaceDirection(drone, at, to, bits(time));
            ++index;
            point += 3;
            const float step = title_global<float>(0x0083eca0);  // 0.2
            if (static_cast<float>(time > step ? 1 : 0) != 0.0f)
                Scr_WaitExact(time - step);
            else if (static_cast<float>(time > title_global<float>(0x0083eb4c) ? 1 : 0) != 0.0f)  // 0.001
                Scr_WaitExact(time);
            else
                Scr_WaitExact(as_float(0x3a83126f));  // 0.001
        }
    }
    using Start = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*);
    title_function<Start>(0x0045b810)(0, 0, 0, 0, 0x0016d390, drone, &flag);  // Scr_StartThread_ent_b(BroScr_DroneMainCall)
}

void XB_CDECL BroScr_DronePathSpawnLoop(uint32_t spawner, uint32_t path, ScriptString type, uint32_t count,
                                        uint32_t delay_min, uint32_t delay_max, uint32_t p7, uint32_t p8, uint32_t p9) {
    end_on(path, 0x78b19de2);
    if (*pointer<uint32_t>(level() + 0x4dc) == undefined_int()) {
        *pointer<uint32_t>(level() + 0x4dc) = 0;
        *pointer<uint32_t>(level() + 0x2a4) = 0;
    }
    {
        ScriptString copy;
        ScriptString_Copy(&copy, &type);
        slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922c44)(path, copy);  // ScrEnt_SetAnimname
    }
    *key_slot(path, 0x3873520a, undefined_float()) = delay_min;
    *key_slot(path, 0xb873524f, undefined_float()) = delay_max;
    *key_slot(path, 0x766fe577, undefined_float()) = p7;
    *key_slot(path, 0x166fe566, undefined_float()) = p8;
    *reinterpret_cast<uint8_t*>(key_slot(path, 0x0fc14a52, title_global<uint8_t>(0x00922ed0))) = static_cast<uint8_t>(p9);
    *key_slot(path, 0x7f3c9f90, undefined_int()) = 0;
    uint32_t n = count;
    if (n == undefined_int()) n = 1;
    using SpawnOne = void XB_CDECL(uint32_t, uint32_t, ScriptString, uint32_t, uint32_t, uint32_t);
    if (static_cast<int32_t>(n) > 0) {
        for (int32_t i = 0; i < static_cast<int32_t>(n); ++i) {
            ScriptString copy;
            ScriptString_Copy(&copy, &type);
            title_function<SpawnOne>(0x00170200)(spawner, path, copy, p7, p8, p9);  // BroScr_DronePathSpawnOne
            random_delay(delay_min, delay_max, 0x40400000);  // else 3 s
        }
    } else {
        for (;;) {
            ScriptString copy;
            ScriptString_Copy(&copy, &type);
            title_function<SpawnOne>(0x00170200)(spawner, path, copy, p7, p8, p9);  // BroScr_DronePathSpawnOne
            random_delay(delay_min, delay_max, 0x40400000);
        }
    }
    scr_release(&type);
}

void XB_CDECL BroScr_DroneDeathThink(uint32_t, ScriptString name) {
    DynArray drones = {nullptr, 0, 0};
    Scr_GetEntArray(address_of(&name), 0x0d786b4c, reinterpret_cast<uint8_t*>(&drones), 0);  // by targetname
    if (drones.data && static_cast<int32_t>(drones.size) >= 1) {
        uint32_t sound = undefined_int();
        bool skip = false;
        if (slot<int32_t XB_CDECL(uint32_t)>(0x009222dc)(0x007f4ca4) > 0)  // GScr_GetCvarInt("no_shrimps")
            skip = *ScrField_using_shrimps(*path_at(&drones, 0)) == 0;
        if (!skip) {
            if (*ScrField_using_shrimps(*path_at(&drones, 0)) != 0) {
                slot<void XB_CDECL(uint32_t)>(0x009221f4)(*ScrField_shrimp_id(*path_at(&drones, 0)));  // GScr_FreeSound
            } else {
                const uint32_t* const handle = level_handle_ptr();
                ScriptString targetname;
                ScriptString* const name_of = ScrCall_ScrEnt_GetTargetname(path_at(&drones, 0), &targetname);
                sound = slot<uint32_t XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t)>(0x009221dc)(
                    *handle, name_of, 0, 0, 0);  // GScr_PlaySound
                scr_release(&targetname);
            }
            for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(drones.size); ++i) {
                if (*int_slot(*path_at(&drones, i), 0x7f3c9f90) != 0) {  // using_shrimps
                    const uint32_t* const handle = level_handle_ptr();
                    ScriptString animname;
                    ScriptString* const type = ScrCall_ScrEnt_GetAnimname(path_at(&drones, i), &animname);
                    uint32_t* const just_delete = ScrField_just_delete(*path_at(&drones, i), 0);
                    uint32_t* const max_death = ScrField_max_death_delay(*path_at(&drones, i));
                    uint32_t* const min_death = ScrField_min_death_delay(*path_at(&drones, i));
                    uint32_t* const max_delay = ScrField_max_delay(*path_at(&drones, i));
                    uint32_t* const min_delay = ScrField_min_delay(*path_at(&drones, i));
                    uint32_t count = 0;
                    using Start = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*,
                                                    ScriptString*, uint32_t*, uint32_t*, uint32_t*, uint32_t*, uint32_t*,
                                                    uint32_t*);
                    title_function<Start>(0x0052c7f0)(0, 0, 0, 0, 0x001706b0, *handle, path_at(&drones, i), type, &count,
                                                      min_delay, max_delay, min_death, max_death,
                                                      just_delete);  // (BroScr_DronePathSpawnLoop)
                    scr_release(&animname);
                    const float a = as_float(*ScrField_min_delay(*path_at(&drones, i))) /
                                    static_cast<float>(static_cast<int32_t>(drones.size));
                    const float b = as_float(*ScrField_max_delay(*path_at(&drones, i))) /
                                    static_cast<float>(static_cast<int32_t>(drones.size));
                    Scr_Wait(b + a);
                } else {
                    *ScrField_using_shrimps(*path_at(&drones, i)) = 1;
                    queue_notify(*path_at(&drones, i), 0x78b19de2);
                    *ScrField_shrimp_id(*path_at(&drones, i)) = sound;
                }
            }
        }
    }
    if (drones.data) Scr_Free(drones.data);
    scr_release(&name);
}

void XB_CDECL BroScr_DronePathsStartAll(uint32_t spawner, DynArray paths, ScriptString type, uint32_t p6, uint32_t p7,
                                        uint32_t p8, uint32_t p9, uint32_t p10, uint32_t p11) {
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(paths.size); ++i) {
        reset_path(&paths, i);
        start_spawn_loop(spawner, path_at(&paths, i), &type, &p6, &p7, &p8, &p9, &p10, &p11);
        Scr_WaitFrames(0);
    }
    if (paths.data) Scr_Free(paths.data);
    scr_release(&type);
}

void XB_CDECL BroScr_DronePathsStartAllStaggered(uint32_t spawner, DynArray paths, ScriptString type, uint32_t p6,
                                                 uint32_t p7, uint32_t p8, uint32_t p9, uint32_t p10, uint32_t p11) {
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(paths.size); ++i) {
        if (!entity_handle_is_valid(*path_at(&paths, i))) continue;
        reset_path(&paths, i);
        start_spawn_loop(spawner, path_at(&paths, i), &type, &p6, &p7, &p8, &p9, &p10, &p11);
        random_delay(p7, p8, 0x3f000000);  // else 0.5 s
    }
    if (paths.data) Scr_Free(paths.data);
    scr_release(&type);
}

}  // namespace game
