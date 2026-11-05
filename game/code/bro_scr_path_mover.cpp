#include "bro_scr_path_mover.h"

#include "bro_scr_ai_common.h"
#include "bro_scr_common.h"
#include "g_scr_runtime.h"
#include "qcommon_fs.h"
#include "scr_access.h"
#include "script_string.h"
#include "title_data.h"

namespace game {

using namespace bro_ai;

namespace {

constexpr uint32_t kPoints = 0x85644aaa;  // the mover's point list (vec3 array)

using GetArray = DynArray* XB_THISCALL(ScriptKeyTable*, uint32_t);
using Reserve = void XB_THISCALL(DynArray*, uint32_t, uint32_t);
using VecOp = void XB_CDECL(uint32_t*, const uint32_t*);

inline DynArray* points(uint32_t ent) {
    return title_function<GetArray>(0x00602a40)(ent_script_object(ent), kPoints);  // ScriptKeyTable_GetOrAddArray_5
}
// Element `i` of a vec3 array (DynArray_Reserve12 to i + 1, i + 4 first when past the count).
inline uint32_t* point_at(DynArray* array, uint32_t i) {
    if (i >= array->size) title_function<Reserve>(0x0045cde0)(array, i + 1, i + 4);
    return array->data + i * 3;
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
inline void lane_fill(uint32_t* v) {
    const uint32_t lane = undefined_lane();
    v[0] = lane;
    v[1] = lane;
    v[2] = lane;
}

// BroScr_PathMoverFromTargetChain (direct call).
inline void from_chain(uint32_t unused, uint32_t ent, uint32_t type, uint32_t speed, uint32_t mortar_speed, uint32_t count,
                       uint32_t min, uint32_t max) {
    BroScr_PathMoverFromTargetChain(unused, ent, type, speed, mortar_speed, count, min, max);
}

}  // namespace

void XB_CDECL BroScr_PathMoverRun(uint32_t, uint32_t ent, uint32_t type, int32_t speed, int32_t mortar_speed,
                                  uint32_t count, uint32_t min, uint32_t max) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, 0x8ff01d8d);  // Scr_EndOn
    float time = static_cast<float>(mortar_speed) * title_global<float>(0x0083eb24);  // * 0.5
    uint32_t direction[3];
    lane_fill(direction);
    ScrVec3 zero;
    zero.v[0] = bits(0.0f);
    zero.v[1] = bits(0.0f);
    zero.v[2] = bits(0.0f);
    slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922ba4)(ent, zero);  // ScrEnt_SetAngles
    *reinterpret_cast<uint8_t*>(key_slot(ent, 0xac835695, title_global<uint8_t>(0x00922ed0))) = 0;
    using Start = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, float*,
                                    uint32_t*, uint32_t*, uint32_t*);
    title_function<Start>(0x0017b270)(0, 0, 0, 0, 0x00172a10, ent, &type, &time, &count, &min,
                                      &max);  // Scr_StartThread_ent_hash_f_i_f_f_2(BroScr_RandomMortarsAroundEnt)
    for (uint32_t n = 1; static_cast<int32_t>(n) < static_cast<int32_t>(points(ent)->size); ++n) {
        const uint32_t* const from = point_at(points(ent), n - 1);
        const uint32_t* const to = point_at(points(ent), n);
        uint32_t a[3] = {from[0], from[1], from[2]};
        uint32_t b[3] = {to[0], to[1], to[2]};
        direction[0] = bits(as_float(b[0]) - as_float(a[0]));
        direction[1] = bits(as_float(b[1]) - as_float(a[1]));
        direction[2] = bits(as_float(b[2]) - as_float(a[2]));
        uint32_t angles_out[3];
        lane_fill(angles_out);
        slot<VecOp>(0x00922284)(angles_out, direction);  // Scr_VectorToAngles
        uint32_t angles[3] = {angles_out[0], angles_out[1], angles_out[2]};
        direction[0] = angles[0];
        direction[1] = angles[1];
        direction[2] = angles[2];
        uint32_t forward[3];
        lane_fill(forward);
        slot<VecOp>(0x00922290)(forward, angles);  // Scr_AnglesToForward
        {
            const uint32_t f[3] = {forward[0], forward[1], forward[2]};
            uint32_t* const field = vector_field(ent, 0x6ff72745);
            field[0] = f[0];
            field[1] = f[1];
            field[2] = f[2];
        }
        uint32_t angles2[3] = {direction[0], direction[1], direction[2]};
        uint32_t right[3];
        lane_fill(right);
        slot<VecOp>(0x0092228c)(right, angles2);  // Scr_AnglesToRight
        {
            const uint32_t r[3] = {right[0], right[1], right[2]};
            uint32_t* const field = vector_field(ent, 0xe2e8d8a5);
            field[0] = r[0];
            field[1] = r[1];
            field[2] = r[2];
        }
        uint32_t yaw[3] = {bits(0.0f), direction[1], bits(0.0f)};
        slot<void XB_CDECL(uint32_t, uint32_t*, uint32_t, uint32_t, uint32_t)>(0x009227a8)(ent, yaw, 0x3e800000, 0,
                                                                                         0);  // GScr_RotateTo(0.25 s)
        const uint32_t* const next = point_at(points(ent), n);
        const uint32_t* const prev = point_at(points(ent), n - 1);
        const long double distance =
            slot<long double XB_CDECL(const uint32_t*, const uint32_t*)>(0x00922268)(prev, next);  // Scr_Distance
        time = static_cast<float>(distance / static_cast<long double>(speed));
        slot<void XB_CDECL(uint32_t, const uint32_t*, float, uint32_t, uint32_t)>(0x00922794)(
            ent, point_at(points(ent), n), time, 0, 0);  // GScr_MoveTo
        Scr_WaitTill(ent, 0x2a63b2d6);
    }
    *reinterpret_cast<uint8_t*>(key_slot(ent, 0xac835695, title_global<uint8_t>(0x00922ed0))) = 1;
    queue_notify(ent, 0x596979d8);
}

void XB_CDECL BroScr_PathMoverFromTargetChain(uint32_t, uint32_t ent, uint32_t type, uint32_t speed,
                                              uint32_t mortar_speed, uint32_t count, uint32_t min, uint32_t max) {
    if (speed == undefined_int()) speed = 100;
    if (mortar_speed == undefined_int()) mortar_speed = 1000;
    if (count == undefined_int()) count = 500;
    const float undefined = as_float(undefined_float());
    if (as_float(min) == undefined) min = title_global<uint32_t>(0x0083eb24);  // 0.5
    if (as_float(max) == undefined) max = title_global<uint32_t>(0x0083eba8);  // 2.0
    uint32_t current = ent;
    if (entity_handle_is_valid(ent)) {
        for (;;) {
            uint32_t buffer[3];
            const uint32_t* const o = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer, current);  // ScrEnt_GetOrigin
            const uint32_t origin[3] = {o[0], o[1], o[2]};
            ScriptKeyTable* const table = ent_script_object(ent);
            uint32_t* value = ScriptKeyTable_Find(table, kPoints);
            if (!value) {
                const uint32_t empty[3] = {0, 0, 0};
                value = title_function<uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t, const uint32_t*)>(0x006029c0)(
                    table, kPoints, empty);  // ScriptKeyTable_AddArray
            }
            title_function<void XB_THISCALL(DynArray*, const uint32_t*)>(0x005b4cd0)(pointer<DynArray>(*value),
                                                                                    origin);  // DynArray_PushBackVec3
            uint32_t self = current;
            ScriptString target;
            const bool none = ScrCall_ScrEnt_GetTarget(&self, &target)->rep == nullptr;
            scr_release(&target);
            if (none) break;
            ScriptString name;
            current = slot<uint32_t XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009221c4)(
                ScrCall_ScrEnt_GetTarget(&self, &name), 0x0d786b4c, 0, 0, 0);  // GScr_GetEntArray
            scr_release(&name);
            if (!entity_handle_is_valid(current)) break;
        }
    }
    const uint32_t object = title_global<uint32_t>(0x00922ee0);
    const uint32_t* const handle = pointer<uint32_t>(object ? object + 4 : 0);
    BroScr_PathMoverRun(*handle, ent, type, static_cast<int32_t>(speed), static_cast<int32_t>(mortar_speed), count, min,
                        max);
}

void XB_CDECL BroScr_PathMoverFromTargetChainCall(uint32_t unused, uint32_t ent) {
    const uint32_t f = undefined_float();
    const uint32_t i = undefined_int();
    from_chain(unused, ent, undefined_handle(), i, i, i, f, f);
}

void XB_CDECL BroScr_PathMoverFromTargetChainCall_2(uint32_t unused, uint32_t ent, uint32_t type) {
    const uint32_t f = undefined_float();
    const uint32_t i = undefined_int();
    from_chain(unused, ent, type, i, i, i, f, f);
}

void XB_CDECL BroScr_PathMoverFromTargetChainCall_3(uint32_t unused, uint32_t ent, uint32_t type, uint32_t speed) {
    const uint32_t f = undefined_float();
    const uint32_t i = undefined_int();
    from_chain(unused, ent, type, speed, i, i, f, f);
}

void XB_CDECL BroScr_PathMoverFromTargetChainCall_4(uint32_t unused, uint32_t ent, uint32_t type, uint32_t speed,
                                                    uint32_t mortar_speed) {
    const uint32_t f = undefined_float();
    from_chain(unused, ent, type, speed, mortar_speed, undefined_int(), f, f);
}

void XB_CDECL BroScr_PathMoverFromTargetChainCall_5(uint32_t unused, uint32_t ent, uint32_t type, uint32_t speed,
                                                    uint32_t mortar_speed, uint32_t count) {
    const uint32_t f = undefined_float();
    from_chain(unused, ent, type, speed, mortar_speed, count, f, f);
}

}  // namespace game
