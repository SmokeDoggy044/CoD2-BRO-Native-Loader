#include "bro_scr_mortar_loops.h"

#include "bro_scr_ai_common.h"
#include "bro_scr_common.h"
#include "bro_scr_level_threads.h"
#include "bro_scr_mortar.h"
#include "bro_scr_util.h"
#include "g_scr_runtime.h"
#include "qcommon_core.h"
#include "scr_access.h"
#include "script_string.h"
#include "title_data.h"
#include "bro_scr_level_threads_2.h"
#include "levels_twins.h"
#include "qcommon_fs.h"

namespace game {

using namespace bro_ai;

namespace {

using Find = uint32_t* XB_THISCALL(uint32_t, uint32_t*, const uint32_t*);
using CreateThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);
using GetVec = uint32_t* XB_CDECL(uint32_t*, uint32_t);

inline uint32_t level() { return title_global<uint32_t>(0x00922ee0); }
inline uint32_t level_handle() {
    const uint32_t object = level();
    return *pointer<uint32_t>(object ? object + 4 : 0);
}
// The level map at `offset`: the int value for `key` (inserted as the undefined int when missing).
inline int32_t map_int(uint32_t offset, const uint32_t* key) {
    const uint32_t map = level() + offset;
    uint32_t it;
    own_function<Find>(&HashMap_Find)(map, &it, key);  // HashMap_Find
    if (it == pointer<uint32_t>(map)[2]) {
        const uint32_t pair[2] = {*key, undefined_int()};
        uint32_t inserted;
        it = *own_function<Find>(&HashMap_Insert_PROLOGUE_2)(map, &inserted, pair);  // HashMap_Insert
    }
    return static_cast<int32_t>(pointer<uint32_t>(it)[3]);
}
inline long double random_range(float low, float high) {
    return slot<long double XB_CDECL(float, float)>(0x00922238)(low, high);  // Scr_RandomFloatRange
}
inline long double random_float(uint32_t limit) { return slot<long double XB_CDECL(uint32_t)>(0x00922230)(limit); }
inline uint32_t* element_at(DynArray* array, uint32_t i) {
    if (i >= array->size) DynArray_Resize(reinterpret_cast<uint8_t*>(array), i + 1, i + 4);
    return &array->data[i];
}
inline ScrVec3 get_vector(uint32_t address, uint32_t ent) {
    uint32_t buffer[3];
    const uint32_t* v = slot<GetVec>(address)(buffer, ent);
    ScrVec3 out;
    out.v[0] = v[0];
    out.v[1] = v[1];
    out.v[2] = v[2];
    return out;
}
// A BroScr_ShellIncomingThenImpact thread (functor {0x007f4b18, .., ent, six undefined floats, type}).
inline void shell_then_impact(uint32_t ent, uint32_t type) {
    const uint32_t u = undefined_float();
    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x28);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f4b18;
        functor[1] = 0x001705a0;  // BroScr_ShellIncomingThenImpact
        functor[2] = ent;
        for (int k = 3; k < 9; ++k) functor[k] = u;
        functor[9] = type;
    }
    slot<CreateThread>(0x0092216c)(0, 0, 0, ent, functor, 0);  // Scr_CreateThread
}
// The truncated type delay (a RandomFloatRange of the level's max / min maps).
inline float type_delay(const uint32_t* type) {
    const int32_t low = map_int(0x208, type);
    const int32_t high = map_int(0x2e8, type);
    return static_cast<float>(static_cast<int32_t>(random_range(static_cast<float>(high), static_cast<float>(low))));
}
inline void shell_with_defaults(uint32_t ent, uint32_t type) {
    own_function<void XB_CDECL(uint32_t, uint32_t)>(&BroScr_PlayShellIncoming)(ent, type);  // BroScr_PlayShellIncoming
    if (entity_handle_is_valid(ent)) {
        const uint32_t u = undefined_float();
        BroScr_MortarThinkWithDefaults(ent, u, u, u, u, u, u, type);
    }
}

}  // namespace

void XB_CDECL BroScr_MortarGroupShuffledLoop(uint32_t, DynArray group, uint32_t end, uint32_t type) {
    DynArray copy = {nullptr, 0, 0};
    if (end != undefined_handle()) slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(level_handle(), end);  // Scr_EndOn
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(group.size); ++i) {
        const uint32_t ent = *element_at(&group, i);
        uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0xc);  // ScrIface_Alloc
        if (functor) {
            functor[0] = 0x007f5ba0;
            functor[1] = 0x00149f60;  // BroScr_MortarEntInit
            functor[2] = ent;
        }
        slot<CreateThread>(0x0092216c)(0, 0, 0, ent, functor, 0);  // Scr_CreateThread
    }
    for (;;) {
        Scr_Wait(as_float(0x3dcccccd));  // 0.1
        own_function<void XB_THISCALL(DynArray*, const DynArray*)>(&DynArray_Assign4_PROLOGUE)(&copy, &group);  // DynArray_Assign4
        if (map_int(0x71c, &type) > 0 &&
            slot<int32_t XB_CDECL(int32_t)>(0x0092222c)(map_int(0x71c, &type)) + 1 > 0) {  // Scr_RandomInt
            DynArray_Shuffle(&copy);
            for (int32_t j = 0; j < map_int(0x71c, &type); ++j) {
                shell_then_impact(*element_at(&copy, static_cast<uint32_t>(j)), type);
                Scr_Wait(static_cast<float>(random_range(as_float(0x3d4ccccd), as_float(0x3f000000))));  // 0.05-0.5
            }
            Scr_Wait(static_cast<float>(random_range(as_float(0x3dcccccd), as_float(0x3f000000))));  // 0.1-0.5
            continue;
        }
        DynArray_Shuffle(&copy);
        for (uint32_t k = 0; static_cast<int32_t>(k) < static_cast<int32_t>(copy.size); ++k) {
            if (!entity_handle_is_valid(*element_at(&copy, k))) continue;
            shell_then_impact(*element_at(&copy, k), type);
            const int32_t low = map_int(0x208, &type);
            const int32_t high = map_int(0x2e8, &type);
            Scr_Wait(static_cast<float>(random_range(static_cast<float>(high), static_cast<float>(low))));
        }
    }
}

void XB_CDECL BroScr_MortarLoopNearPlayer(uint32_t ent, float min_dist, float max_dist, uint32_t end, uint32_t type) {
    if (end != undefined_handle()) slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(level_handle(), end);  // Scr_EndOn
    const float undefined = as_float(undefined_float());
    if (min_dist == undefined) min_dist = title_global<float>(0x0083f11c);  // 400
    if (max_dist == undefined) max_dist = title_global<float>(0x007f7bf8);  // 1200
    Scr_Wait(type_delay(&type));
    const float min2 = min_dist * min_dist;
    const float max2 = max_dist * max_dist;
    for (;;) {
        ScrVec3 at = get_vector(0x00922b20, ent);
        ScrVec3 player = get_vector(0x00922b20, *pointer<uint32_t>(level() + 0x7f8));
        const float d2 = static_cast<float>(
            slot<long double XB_CDECL(const ScrVec3*, const ScrVec3*)>(0x0092226c)(&player, &at));  // Scr_DistanceSquared
        if (static_cast<float>(d2 > min2 ? 1 : 0) != 0.0f && static_cast<float>(max2 > d2 ? 1 : 0) != 0.0f)
            shell_with_defaults(ent, type);
        Scr_Wait(type_delay(&type));
    }
}

void XB_CDECL BroScr_MortarGroupNearEntLoop(uint32_t, uint32_t ent, DynArray group, float min_dist, float max_dist,
                                           uint32_t end, uint32_t type, int32_t count, DynArray extra) {
    if (group.size == 0 || type == undefined_handle() || !entity_handle_is_valid(ent)) {
        if (group.data) Scr_Free(group.data);
        if (extra.data) Scr_Free(extra.data);
        return;
    }
    if (end != undefined_handle()) slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(level_handle(), end);  // Scr_EndOn
    const float min2 = min_dist * min_dist;
    const float max2 = max_dist * max_dist;
    DynArray near = {nullptr, 0, 0};
    using Index = uint32_t* XB_THISCALL(uint32_t, const uint32_t*);
    for (;;) {
        const int32_t low = static_cast<int32_t>(*title_function<Index>(0x00460f80)(level() + 0x208, &type));  // HashMap_Index_14
        const int32_t high = static_cast<int32_t>(*title_function<Index>(0x00460f80)(level() + 0x2e8, &type));
        Scr_Wait(static_cast<float>(random_range(static_cast<float>(high), static_cast<float>(low))));
        DynArray_Resize(reinterpret_cast<uint8_t*>(&near), 0, 0);
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(group.size); ++i) {
            ScrVec3 at = get_vector(0x00922b20, ent);
            ScrVec3 other = get_vector(0x00922b20, *element_at(&group, i));
            const float d2 = static_cast<float>(
                slot<long double XB_CDECL(const ScrVec3*, const ScrVec3*)>(0x0092226c)(&other, &at));  // Scr_DistanceSquared
            if (static_cast<float>(d2 > min2 ? 1 : 0) != 0.0f && static_cast<float>(max2 > d2 ? 1 : 0) != 0.0f)
                DynArray_PushBack(reinterpret_cast<uint8_t*>(&near), element_at(&group, i));
        }
        int32_t n = count;
        if (static_cast<int32_t>(near.size) < n) n = static_cast<int32_t>(near.size);
        if (n <= 0) continue;
        for (; n; --n) {
            const uint32_t pick = slot<uint32_t XB_CDECL(uint32_t)>(0x0092222c)(near.size);  // Scr_RandomInt
            const uint32_t target = *element_at(&near, pick);
            uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
            if (functor) {
                functor[0] = 0x007f4b00;
                functor[1] = 0x00171450;  // BroScr_ShellIncomingThenImpact_2
                functor[2] = target;
                functor[3] = type;
            }
            slot<CreateThread>(0x0092216c)(0, 0, 0, target, functor, 0);  // Scr_CreateThread
            if (pick < near.size) {  // the inlined erase
                uint32_t* p = near.data;
                uint32_t* stop = p + near.size;
                uint32_t k = pick;
                bool found = p != stop;
                for (; found && k; --k)
                    if (++p == stop) found = false;
                if (found) {
                    --stop;
                    for (; p != stop; ++p) p[0] = p[1];
                    --near.size;
                }
            }
            Scr_Wait(static_cast<float>(random_range(as_float(0x3f000000), as_float(0x3f800000))));  // 0.5-1
        }
    }
}

void XB_CDECL BroScr_RandomMortarsAroundEnt(uint32_t ent, uint32_t type, float speed, int32_t count, uint32_t min,
                                           uint32_t max) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, 0x8ff01d8d);  // Scr_EndOn
    uint32_t origin_ent;
    {
        ScriptString classname;
        scr_string_from(&classname, 0x0083ab88);  // "script_origin"
        ScrVec3 zero;
        zero.v[0] = bits(0.0f);
        zero.v[1] = bits(0.0f);
        zero.v[2] = bits(0.0f);
        origin_ent = slot<uint32_t XB_CDECL(ScriptString*, ScrVec3*, uint32_t)>(0x009222f0)(&classname, &zero, 0);  // GScr_Spawn
        scr_release(&classname);
    }
    const float back = speed * title_global<float>(0x0083eb34);  // * -1
    for (;;) {
        ScrVec3 pos;
        bool inside;
        if (*BroScr_PlayerInsideEntZone(&inside, ent, speed, back)) {
            queue_notify(level_handle(), 0x42d71e20);
            pos = get_vector(0x00922b20, *pointer<uint32_t>(level() + 0x7f8));  // the player's origin
        } else {
            const ScrVec3 angles = get_vector(0x00922ba0, ent);  // ScrEnt_GetAngles
            const uint32_t lane = undefined_lane();
            uint32_t right[3] = {lane, lane, lane};
            slot<void XB_CDECL(uint32_t*, const ScrVec3*)>(0x0092228c)(right, &angles);  // Scr_AnglesToRight
            const uint32_t r[3] = {right[0], right[1], right[2]};
            const uint32_t limit = slot<int32_t XB_CDECL(uint32_t)>(0x0092222c)(100) < 50 ? bits(speed) : bits(back);  // Scr_RandomInt
            const float side = static_cast<float>(random_float(limit));
            const float z = as_float(r[2]) * side;
            const float y = as_float(r[1]) * side;
            const float x = side * as_float(r[0]);
            const ScrVec3 o = get_vector(0x00922b20, ent);
            pos.v[0] = bits(as_float(o.v[0]) + x);
            pos.v[1] = bits(as_float(o.v[1]) + y);
            pos.v[2] = bits(as_float(o.v[2]) + z);
            const ScrVec3 a = get_vector(0x00922ba0, ent);
            uint32_t turned[3] = {a.v[0], bits(as_float(a.v[1]) + title_global<float>(0x0083eb60)), a.v[2]};  // + 180
            uint32_t forward[3] = {lane, lane, lane};
            slot<void XB_CDECL(uint32_t*, const uint32_t*)>(0x00922290)(forward, turned);  // Scr_AnglesToForward
            const uint32_t f[3] = {forward[0], forward[1], forward[2]};
            const float depth = static_cast<float>(random_float(bits(static_cast<float>(count))));
            const float fz = as_float(f[2]) * depth;
            const float fy = as_float(f[1]) * depth;
            pos.v[0] = bits(depth * as_float(f[0]) + as_float(pos.v[0]));
            pos.v[2] = bits(as_float(pos.v[2]) + fz);
            pos.v[1] = bits(as_float(pos.v[1]) + fy);
        }
        slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922b24)(origin_ent, pos);  // ScrEnt_SetOrigin
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(origin_ent, 0x06730ae8);  // Scr_EndOn(death)
        {
            uint32_t ground[3];
            BroScr_TraceGroundPos(ground, origin_ent, get_vector(0x00922b20, origin_ent));
            ScrVec3 g;
            g.v[0] = ground[0];
            g.v[1] = ground[1];
            g.v[2] = ground[2];
            slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922b24)(origin_ent, g);
        }
        shell_with_defaults(origin_ent, type);
        const uint32_t base = *pointer<uint32_t>(level() + 0x808);
        const float undefined = as_float(undefined_float());
        if (as_float(base) != undefined && as_float(*pointer<uint32_t>(level() + 0x780)) != undefined) {
            const long double random = random_float(*pointer<uint32_t>(level() + 0x780));
            Scr_Wait(static_cast<float>(as_float(*pointer<uint32_t>(level() + 0x808)) + random));
        } else {
            const long double random = random_float(max);
            Scr_Wait(static_cast<float>(as_float(min) + random));
        }
    }
}

}  // namespace game
