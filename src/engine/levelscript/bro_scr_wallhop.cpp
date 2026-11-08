#include "bro_scr_wallhop.h"

#include <cstring>

#include "bro_scr_array.h"
#include "g_scr_runtime.h"
#include "g_script.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "scr_field.h"
#include "title_data.h"
#include "bro_scr_level_threads.h"

namespace game {

namespace {

template <typename T>
T* pointer(uint32_t address) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}
uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

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

uint32_t player() { return title_global<uint32_t>(0x00922eec); }

// ScrEnt_GetOrigin into `buffer`; returns the vector it hands back.
const uint32_t* origin_of(uint32_t* buffer, uint32_t ent) {
    return slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer, ent);
}

// The entity's 12-byte point array (ScriptKeyTable_GetOrAddArray_5, field 0x4f7c0a56).
DynArray* points(uint32_t ent) {
    using GetArray = DynArray* XB_THISCALL(ScriptKeyTable*, uint32_t);
    return title_function<GetArray>(0x00602a40)(BroScr_ScriptObjectOrEmpty(ent), 0x4f7c0a56);
}

// DynArray_Reserve12 (thiscall: size, capacity).
void reserve12(DynArray* array, uint32_t size, uint32_t capacity) {
    title_function<void XB_THISCALL(DynArray*, uint32_t, uint32_t)>(0x0045cde0)(array, size, capacity);
}

// Point `i` of `ent`'s array, grown to i + 1 (capacity i + 4) first when it is too short.
const uint32_t* point(uint32_t ent, uint32_t i) {
    DynArray* const array = points(ent);
    if (i >= array->size) reserve12(array, i + 1, i + 4);
    return &array->data[i * 3];
}

float distance_to(const uint32_t* from, const uint32_t* to) {
    return slot<float XB_CDECL(const uint32_t*, const uint32_t*)>(0x00922268)(from, to);  // Scr_Distance
}

uint32_t* closest_point_to_player(uint32_t* out, uint32_t ent) {
    uint32_t best[3];
    {
        const uint32_t* const first = point(ent, 0);
        best[0] = first[0];
        best[1] = first[1];
        best[2] = first[2];
    }
    uint32_t buffer[3];
    uint32_t eye[3];
    const uint32_t* first = point(ent, 0);
    const uint32_t* o = origin_of(buffer, player());
    eye[0] = o[0];
    eye[1] = o[1];
    eye[2] = o[2];
    float best_distance = distance_to(eye, first);
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(points(ent)->size); ++i) {
        const uint32_t* const p = point(ent, i);
        o = origin_of(buffer, player());
        eye[0] = o[0];
        eye[1] = o[1];
        eye[2] = o[2];
        const float d = distance_to(eye, p);
        if (best_distance > d) {
            const uint32_t* const nearer = point(ent, i);
            best[0] = nearer[0];
            best[1] = nearer[1];
            best[2] = nearer[2];
            best_distance = d;
        }
    }
    out[0] = best[0];
    out[1] = best[1];
    out[2] = best[2];
    return out;
}

// Element `i` of a 12-byte / 4-byte local array, grown to i + 1 (capacity i + 4) first when too short.
inline uint32_t* at12(DynArray* array, uint32_t i) {
    if (i >= array->size) reserve12(array, i + 1, i + 4);
    return &array->data[i * 3];
}
inline void reserve4(DynArray* array, uint32_t size, uint32_t capacity) {
    title_function<void XB_THISCALL(DynArray*, uint32_t, uint32_t)>(0x0052d530)(array, size, capacity);  // DynArray_Reserve_KASSPASS
}
inline float* at4(DynArray* array, uint32_t i) {
    if (i >= array->size) reserve4(array, i + 1, i + 4);
    return reinterpret_cast<float*>(&array->data[i]);
}

}  // namespace

uint32_t* XB_CDECL BroScr_WallHopMidpoint(uint32_t* out, uint32_t, ScrVec3 to, ScrVec3 offset) {
    uint32_t buffer[3];
    const uint32_t* const o = origin_of(buffer, player());
    const float px = as_float(o[0]), py = as_float(o[1]), pz = as_float(o[2]);
    const float half = title_global<float>(0x0083eb24);  // 0.5
    const float mx = platform_mulss(as_float(to.v[0]) - px, half);
    const float my = platform_mulss(as_float(to.v[1]) - py, half);
    const float mz = platform_mulss(as_float(to.v[2]) - pz, half);
    float x = platform_addss(mx, px);
    float z = platform_addss(platform_addss(mz, pz), title_global<float>(0x0083eb3c));  // + 60
    float y = platform_addss(my, py);
    const float undefined = title_global<float>(0x00922eb8);
    const float ox = as_float(offset.v[0]), oy = as_float(offset.v[1]), oz = as_float(offset.v[2]);
    if (ox != undefined || oy != undefined || oz != undefined) {
        y = platform_addss(oy, y);
        z = platform_addss(oz, z);
        x = platform_addss(x, ox);
    }
    out[0] = bits_of(x);
    out[1] = bits_of(y);
    out[2] = bits_of(z);
    return out;
}

uint32_t* XB_CDECL BroScr_WallHopMove(uint32_t* out, uint32_t skip, ScrVec3 to) {
    uint32_t buffer[3];
    uint32_t eye[3];
    const uint32_t* o = origin_of(buffer, player());
    eye[0] = o[0];
    eye[1] = o[1];
    eye[2] = o[2];
    const float distance = distance_to(eye, to.v);
    uint32_t angles_buffer[3];
    const uint32_t* const a = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922ba0)(angles_buffer, player());  // ScrEnt_GetAngles
    const uint32_t angles[3] = {a[0], a[1], a[2]};
    const uint32_t undefined = title_global<uint32_t>(0x00922eb8);
    uint32_t forward[3] = {undefined, undefined, undefined};
    slot<void XB_CDECL(uint32_t*, const uint32_t*)>(0x00922290)(forward, angles);  // Scr_AnglesToForward
    o = origin_of(buffer, player());
    const float ox = as_float(o[0]), oy = as_float(o[1]), oz = as_float(o[2]);
    const float fz = platform_mulss(as_float(forward[2]), distance);
    const float fy = platform_mulss(as_float(forward[1]), distance);
    const float fx = platform_mulss(as_float(forward[0]), distance);
    const ScrVec3 start = {{bits_of(platform_addss(fx, ox)), bits_of(platform_addss(fy, oy)), bits_of(platform_addss(fz, oz))}};
    uint32_t ground_buffer[3];
    const uint32_t* const ground = BroScr_TraceGroundPos_3(ground_buffer, skip, start);
    const uint32_t gx = ground[0], gy = ground[1];
    const float gz = platform_addss(as_float(ground[2]), title_global<float>(0x0083ecdc));  // + 72
    out[0] = gx;
    out[1] = gy;
    out[2] = bits_of(gz);
    return out;
}

void XB_CDECL BroScr_WallHopGetScriptOrigins(uint32_t ent) {
    DynArray targets = {};
    ScriptString target;
    Scr_GetEntArray(address_of(ScrCall_ScrEnt_GetTarget(&ent, &target)), 0x0d786b4c, reinterpret_cast<uint8_t*>(&targets), 0);
    ScriptString_Release(&target);
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(targets.size); ++i) {
        ScriptString classname;
        const uint32_t* const element = BroScr_ArrayElement(reinterpret_cast<uint32_t*>(&targets), i);
        const bool origin = ScriptString_Equals(ScrCall_ScrEnt_GetClassname(element, &classname),
                                                pointer<const char>(0x0083ab88));  // "script_origin"
        ScriptString_Release(&classname);
        if (!origin) continue;
        uint32_t buffer[3];
        const uint32_t* const o = origin_of(buffer, *BroScr_ArrayElement(reinterpret_cast<uint32_t*>(&targets), i));
        const uint32_t x = o[0], y = o[1], z = o[2];
        const uint32_t n = points(ent)->size;
        DynArray* const array = points(ent);
        if (n >= array->size) reserve12(array, n + 1, n + 4);
        uint32_t* const dst = &array->data[n * 3];
        dst[0] = x;
        dst[1] = y;
        dst[2] = z;
    }
    if (targets.data) Scr_Free(targets.data);
}

BRO_SCR_ARGS_ENTRY(BroScr_WallHopGetPointsNearFirst)
uint32_t XB_CDECL BroScr_WallHopGetPointsNearFirst(uint32_t* args) {
    const uint32_t ent = args[0];
    DynArray* const result = reinterpret_cast<DynArray*>(&args[1]);
    DynArray* array = points(ent);
    if (array->size <= 1) reserve12(array, 2, 5);
    const uint32_t* const second = &array->data[3];
    array = points(ent);
    if (array->size == 0) reserve12(array, 1, 4);
    const uint32_t* const first = array->data;
    uint32_t buffer[3];
    const uint32_t* const o = origin_of(buffer, player());
    const uint32_t eye[3] = {o[0], o[1], o[2]};
    using Closer = int32_t XB_CDECL(const uint32_t*, const uint32_t*, const uint32_t*);
    const bool first_nearer = slot<Closer>(0x00922278)(eye, first, second) == 1;  // Scr_Closer
    const uint32_t near_index = first_nearer ? 0 : 1;
    // Each copy: the source point (grown as in the original: index 1 -> (2, 5), 0 -> (1, 4)), then
    // the result grown, then the three words.
    for (uint32_t k = 0; k < 2; ++k) {
        const uint32_t index = k == 0 ? near_index : 1 - near_index;
        array = points(ent);
        if (array->size <= index) reserve12(array, index + 1, index + 4);
        const uint32_t* const src = &array->data[index * 3];
        if (result->size <= k) reserve12(result, k + 1, k + 4);
        uint32_t* const dst = &result->data[k * 3];
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
    }
    if (result->data) Scr_Free(result->data);
    return 0;
}

uint32_t* XB_CDECL BroScr_GetClosestArrayPointToPlayer(uint32_t* out, uint32_t ent) {
    return closest_point_to_player(out, ent);
}

uint32_t* XB_CDECL BroScr_GetClosestArrayPointToPlayer_2(uint32_t* out, uint32_t ent) {
    return closest_point_to_player(out, ent);
}

void XB_CDECL BroScr_PlayerHopSplineMath(DynArray* path, float steps, float min_gap, DynArray* out) {
    const uint32_t level = title_global<uint32_t>(0x00922ee0);
    uint32_t hash;
    const uint32_t notify = *Scr_HashName(&hash, pointer<const char>(0x00824638));  // "stop_spline_math_drawing"
    // the level's entity at +4 (the original reads address 0 without a level; never seen)
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(*pointer<uint32_t>(level ? level + 4 : 0), notify);  // Scr_QueueNotify
    if (path->size <= 1) reserve12(path, 2, 5);
    const uint32_t* second = &path->data[3];  // taken before the (never needed) growth below
    if (path->size == 0) reserve12(path, 1, 4);
    const float total = distance_to(path->data, second);
    if (path->size == 0) reserve12(path, 1, 4);
    uint32_t last_kept[3] = {path->data[0], path->data[1], path->data[2]};
    if (path->size == 0) reserve12(path, 1, 4);
    float cur[3] = {as_float(path->data[0]), as_float(path->data[1]), as_float(path->data[2])};
    uint32_t size = path->size;
    if (static_cast<int32_t>(size) <= 0) return;
    for (uint32_t i = 0;;) {
        if (i == size - 1) {
            // the last point is kept as it is
            if (i >= size) reserve12(path, i + 1, i + 4);
            const uint32_t* const p = &path->data[i * 3];
            const uint32_t n = out->size;
            reserve12(out, n + 1, n + 4);
            uint32_t* const dst = &out->data[n * 3];
            dst[0] = p[0];
            dst[1] = p[1];
            dst[2] = p[2];
        } else {
            if (i >= size) reserve12(path, i + 1, i + 4);
            const uint32_t* const p = &path->data[i * 3];
            const uint32_t* next;
            if (i + 1 < path->size) {
                next = &path->data[i * 3 + 3];
            } else {
                reserve12(path, i + 2, i + 5);
                next = &path->data[i * 3 + 3];
            }
            const float dx = as_float(next[0]) - as_float(p[0]);
            const float dy = as_float(next[1]) - as_float(p[1]);
            const float dz = as_float(next[2]) - as_float(p[2]);
            const float inverse = 1.0f / steps;
            const uint32_t step[3] = {bits_of(platform_mulss(inverse, dx)), bits_of(platform_mulss(inverse, dy)),
                                      bits_of(platform_mulss(inverse, dz))};
            const float segment = static_cast<float>(slot<long double XB_CDECL(const uint32_t*)>(0x00922270)(step));  // Scr_Length
            if (i >= path->size) reserve12(path, i + 1, i + 4);
            const uint32_t* const from = &path->data[i * 3];
            float pos[3] = {as_float(from[0]), as_float(from[1]), as_float(from[2])};
            for (int32_t k = 0; platform_greater(steps, static_cast<float>(k)); ++k) {
                pos[0] = platform_addss(pos[0], as_float(step[0]));
                pos[1] = platform_addss(pos[1], as_float(step[1]));
                pos[2] = platform_addss(pos[2], as_float(step[2]));
                const uint32_t to_pos[3] = {bits_of(pos[0] - cur[0]), bits_of(pos[1] - cur[1]), bits_of(pos[2] - cur[2])};
                const uint32_t lane = title_global<uint32_t>(0x00922eb8);  // g_scrUndefinedVecLane
                uint32_t direction[3] = {lane, lane, lane};
                slot<void XB_CDECL(uint32_t*, const uint32_t*)>(0x00922280)(direction, to_pos);  // Scr_VectorNormalize
                const uint32_t to_pos_2[3] = {bits_of(pos[0] - cur[0]), bits_of(pos[1] - cur[1]), bits_of(pos[2] - cur[2])};
                const long double length = slot<long double XB_CDECL(const uint32_t*)>(0x00922270)(to_pos_2);  // Scr_Length
                const float scale =
                    static_cast<float>(length / platform_x87_ld(total) * platform_x87_ld(segment));
                cur[0] = platform_addss(platform_mulss(as_float(direction[0]), scale), cur[0]);
                cur[1] = platform_addss(platform_mulss(as_float(direction[1]), scale), cur[1]);
                cur[2] = platform_addss(platform_mulss(as_float(direction[2]), scale), cur[2]);
                const uint32_t cur_bits[3] = {bits_of(cur[0]), bits_of(cur[1]), bits_of(cur[2])};
                const float gap = distance_to(last_kept, cur_bits);
                if (min_gap == as_float(title_global<uint32_t>(0x00922eac)) || !platform_greater(min_gap, gap)) {
                    last_kept[0] = cur_bits[0];
                    last_kept[1] = cur_bits[1];
                    last_kept[2] = cur_bits[2];
                    const uint32_t n = out->size;
                    reserve12(out, n + 1, n + 4);
                    uint32_t* const dst = &out->data[n * 3];
                    dst[0] = cur_bits[0];
                    dst[1] = cur_bits[1];
                    dst[2] = cur_bits[2];
                }
            }
        }
        size = path->size;
        ++i;
        if (static_cast<int32_t>(i) >= static_cast<int32_t>(size)) break;
    }
}

void XB_CDECL BroScr_PlayerHopOverWall(uint32_t level_ent, DynArray points, ScrVec3 to) {
    // The title also passes `to` to BroScr_WallHopMove as a fourth argument it never reads.
    (void)to;
    uint32_t move[3];
    {
        const uint32_t* const p = at12(&points, 1);
        ScrVec3 second;
        second.v[0] = p[0];
        second.v[1] = p[1];
        second.v[2] = p[2];
        BroScr_WallHopMove(move, level_ent, second);
    }
    uint32_t mid[3];
    {
        ScrVec3 offset;
        offset.v[0] = bits_of(0.0f);
        offset.v[1] = bits_of(0.0f);
        offset.v[2] = bits_of(title_global<float>(0x0083eb3c));  // 60
        ScrVec3 target;
        target.v[0] = move[0];
        target.v[1] = move[1];
        target.v[2] = move[2];
        BroScr_WallHopMidpoint(mid, level_ent, target, offset);
    }
    DynArray path = {};
    {
        uint32_t buffer[3];
        const uint32_t* const o = origin_of(buffer, player());
        const uint32_t start[3] = {o[0], o[1], bits_of(as_float(o[2]) + title_global<float>(0x0083eb3c))};  // + 60
        uint32_t* d = at12(&path, 0);
        d[0] = start[0];
        d[1] = start[1];
        d[2] = start[2];
        d = at12(&path, 1);
        d[0] = mid[0];
        d[1] = mid[1];
        d[2] = mid[2];
        d = at12(&path, 2);
        d[0] = move[0];
        d[1] = move[1];
        d[2] = move[2];
    }
    DynArray spline = {};
    BroScr_PlayerHopSplineMath(&path, title_global<float>(0x007bd0c8), title_global<float>(0x007bd0c8), &spline);  // 4, 4

    using Spawn = uint32_t XB_CDECL(ScriptString*, const uint32_t*, uint32_t);
    uint32_t mover;
    {
        ScriptString classname;
        ScriptString_FromCString(&classname, pointer<const char>(0x0083ab88));  // "script_origin"
        uint32_t buffer[3];
        const uint32_t* const o = origin_of(buffer, player());
        const uint32_t at[3] = {o[0], o[1], o[2]};
        mover = slot<Spawn>(0x009222f0)(&classname, at, 0);  // GScr_Spawn
        ScriptString_Release(&classname);
    }
    uint32_t anchor;
    {
        ScriptString classname;
        ScriptString_FromCString(&classname, pointer<const char>(0x0083ab88));  // "script_origin"
        uint32_t buffer[3];
        const uint32_t* const o = origin_of(buffer, player());
        const uint32_t at[3] = {o[0], o[1], bits_of(as_float(o[2]) + title_global<float>(0x0083eb3c))};  // + 60
        anchor = slot<Spawn>(0x009222f0)(&classname, at, 0);
        ScriptString_Release(&classname);
    }
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009224fc)(mover, anchor);  // GScr_LinkTo
    {
        ScriptString tag;
        ScriptString_FromCString(&tag, pointer<const char>(0x0082dfeb));  // ""
        const float one = title_global<float>(0x0083eb20);
        const float limits[3] = {one, one, one};
        using PlayerLinkTo = void XB_CDECL(uint32_t, uint32_t, ScriptString*, const float*);
        slot<PlayerLinkTo>(0x00922500)(player(), mover, &tag, limits);  // GScr_PlayerLinkTo
        ScriptString_Release(&tag);
    }
    {
        uint32_t buffer[3];
        const uint32_t* const o = origin_of(buffer, mover);
        float dir[3];
        dir[0] = as_float(move[0]) - as_float(o[0]);
        dir[1] = as_float(move[1]) - as_float(o[1]);
        dir[2] = as_float(move[2]) - as_float(o[2]);
        const uint32_t lane = title_global<uint32_t>(0x00922eb8);  // g_scrUndefinedVecLane
        uint32_t angles[3] = {lane, lane, lane};
        slot<void XB_CDECL(uint32_t*, const float*)>(0x00922290)(angles, dir);  // slot 0x00922290 (direction in, angles out)
        ScrVec3 yaw;
        yaw.v[0] = bits_of(0.0f);
        yaw.v[1] = angles[1];
        yaw.v[2] = bits_of(0.0f);
        slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922ba4)(anchor, yaw);  // ScrEnt_SetAngles
    }

    // Per spline point: the anchor's travel time at 400 units/s (distance * 0.0025); points with a
    // time above 0.05 are kept, and the kept points / times are copied back over the first ones.
    DynArray times = {};
    DynArray kept_times = {};
    DynArray kept_points = {};
    for (uint32_t i = 0; i < spline.size; ++i) {
        uint32_t buffer[3];
        const uint32_t* const o = origin_of(buffer, anchor);
        const uint32_t from[3] = {o[0], o[1], o[2]};
        const uint32_t* const p = at12(&spline, i);
        const uint32_t point[3] = {p[0], p[1], p[2]};
        using Distance = long double XB_CDECL(const uint32_t*, const uint32_t*);
        const float distance = static_cast<float>(slot<Distance>(0x00922268)(from, point));  // Scr_Distance
        *at4(&times, i) = distance * title_global<float>(0x0083f0e4);                       // * 0.0025
        if (static_cast<float>(*at4(&times, i) > title_global<float>(0x0083edc8) ? 1 : 0) != title_global<float>(0x0083eafc)) {
            const uint32_t* const src = at12(&spline, i);
            const uint32_t n = kept_points.size;
            reserve12(&kept_points, n + 1, n + 4);
            uint32_t* const dst = &kept_points.data[n * 3];
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            const float* const t = at4(&times, i);
            const uint32_t m = kept_times.size;
            reserve4(&kept_times, m + 1, m + 4);
            reinterpret_cast<float*>(kept_times.data)[m] = *t;
        }
    }
    for (uint32_t i = 0; i < spline.size; ++i) {
        const uint32_t* const src = at12(&kept_points, i);
        uint32_t* const dst = at12(&spline, i);
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
    }
    const uint32_t count = spline.size;
    for (uint32_t i = 0; i < count; ++i) {
        const float* const src = at4(&kept_times, i);
        *at4(&times, i) = *src;
    }

    // BroScr_WallHopRotate as a thread on the level entity: functor {0x00824630, 0x001356f0, level
    // entity, anchor, copy of the times, (count + 1) / 2 rounded up}.
    int32_t half = static_cast<int32_t>(static_cast<float>(static_cast<int32_t>(count) + 1) * title_global<float>(0x0083eb24));  // * 0.5
    if (static_cast<int32_t>(count) % 2 > 0) ++half;
    const uint32_t level = title_global<uint32_t>(0x00922ee0);
    const uint32_t owner = *pointer<uint32_t>(level ? level + 4 : 0);
    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x20);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x00824630;
        functor[1] = own_address(&BroScr_WallHopRotate);  // BroScr_WallHopRotate
        functor[2] = owner;
        functor[3] = anchor;
        DynArray* const copy = reinterpret_cast<DynArray*>(&functor[4]);
        copy->data = nullptr;
        copy->capacity = 0;
        copy->size = 0;
        title_function<void XB_THISCALL(DynArray*, const DynArray*)>(0x0052d600)(copy, &times);  // DynArray_Assign4_KASSPASS
        functor[7] = static_cast<uint32_t>(half);
    }
    slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(0, 0, 0, owner, functor,
                                                                                                    0);  // Scr_CreateThread

    // The anchor (carrying the linked player) moves point by point, waiting each time (less 0.05 when
    // above 0.1).
    if (count > 0) {
        for (uint32_t i = 0; i < spline.size; ++i) {
            const float t = *at4(&times, i);
            const uint32_t* const p = at12(&spline, i);
            slot<void XB_CDECL(uint32_t, const uint32_t*, float, uint32_t, uint32_t)>(0x00922794)(anchor, p, t, 0, 0);  // GScr_MoveTo
            const float v = *at4(&times, i);
            if (static_cast<float>(v > title_global<float>(0x0083ebf8) ? 1 : 0) != title_global<float>(0x0083eafc))  // 0.1
                Scr_Wait(*at4(&times, i) - title_global<float>(0x0083edc8));                                          // - 0.05
            else
                Scr_Wait(*at4(&times, i));
        }
    }
    {
        uint32_t buffer[3];
        const uint32_t* const o = origin_of(buffer, anchor);
        const uint32_t from[3] = {o[0], o[1], o[2]};
        using Distance = long double XB_CDECL(const uint32_t*, const uint32_t*);
        const float t = static_cast<float>(slot<Distance>(0x00922268)(from, move) *
                                           title_global<float>(0x0081c378));  // * 0.00571429 (175 units/s)
        slot<void XB_CDECL(uint32_t, const uint32_t*, float, uint32_t, uint32_t)>(0x00922794)(anchor, move, t, 0, 0);
    }
    Scr_WaitTillString(anchor, pointer<const char>(0x0083ac7c));  // "movedone"
    slot<void XB_CDECL(uint32_t)>(0x0092250c)(player());          // GScr_Unlink
    slot<void XB_CDECL(uint32_t)>(0x00922540)(mover);             // GScr_Delete
    slot<void XB_CDECL(uint32_t)>(0x00922540)(anchor);
    if (kept_points.data) Scr_Free(kept_points.data);
    if (kept_times.data) Scr_Free(kept_times.data);
    if (times.data) Scr_Free(times.data);
    if (spline.data) Scr_Free(spline.data);
    if (path.data) Scr_Free(path.data);
    if (points.data) Scr_Free(points.data);
}

}  // namespace game
