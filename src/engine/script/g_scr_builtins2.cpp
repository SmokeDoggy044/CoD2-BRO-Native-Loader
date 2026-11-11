#include "g_scr_builtins2.h"

#include <cstring>

#include "actor.h"
#include "bg_misc.h"
#include "cm.h"
#include "dobj.h"
#include "g_mover.h"
#include "g_script.h"
#include "g_scr_runtime.h"
#include "g_spawn.h"
#include "g_utils.h"
#include "g_vehicle.h"
#include "ngl_debug.h"
#include "qcommon.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "script_string.h"
#include "stream.h"
#include "sv.h"
#include "title_data.h"
#include "tr_main.h"
#include "xanim.h"
#include "g_scr_builtins.h"
#include "fx_cmd.h"
#include "g_pathnode.h"
#include "pmove.h"
#include "title_crt.h"

// title_call_at_regs for callees with an ST0 result (left on the x87 stack; asm entry at the end of the file).
extern "C" PLATFORM_HIDDEN float XB_CDECL gscr_call_at_st0(uint32_t top, uint32_t function, const game::TitleRegs* regs,
                                                       uint32_t count, const uint32_t* args);

namespace game {

namespace {

template <typename T>
T& field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

uint32_t addr(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }
const char* title_text(uint32_t address) { return reinterpret_cast<const char*>(static_cast<uintptr_t>(address)); }

// The entity a script handle names: index in the low 12 bits (below 0x540), generation above, checked
// against the slot table at 0x0097a968 {entity, generation}; null when stale.
uint8_t* handle_entity(uint32_t handle) {
    const uint32_t index = handle & 0xfff;
    if (index >= 0x540 || (handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return nullptr;
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x0097a968 + index * 8)));
}

// The actor (+0x240) of the handle's entity, or null.
uint8_t* handle_actor(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    return ent ? field<uint8_t*>(ent, 0x240) : nullptr;
}

// A script string's text (+6 of its rep), null for the empty string.
const char* script_text(const ScriptString* s) {
    const uint32_t rep = *reinterpret_cast<const uint32_t*>(s);
    return rep ? reinterpret_cast<const char*>(static_cast<uintptr_t>(rep + 6)) : nullptr;
}

uint32_t assign_cstring(void* out, const char* text) {
    return own_function<uint32_t XB_THISCALL(void*, const char*)>(&ScriptString_AssignCString)(out, text);  // ScriptString_AssignCString
}

// The anim record of an anim value's tree (high 16 bits; tree 0 when out of range).
const uint8_t* anim_tree(uint32_t anim) {
    uint8_t* const trees = field<uint8_t*>(title_global<uint8_t*>(0x0095f524), 4);  // rm_animTrees
    uint32_t index = anim >> 16;
    if (index >= field<uint32_t>(trees, 0)) index = 0;
    return field<uint8_t*>(trees, 4) + index * 12;
}

float title_ci_sqrt_float(float x) {
    float result;
    __asm__ volatile("flds %1\n\tcall *%2\n\tfstps %0"
                     : "=m"(result)
                     : "m"(x), "r"(own_address(&title_CIsqrt))  // __CIsqrt
                     : "eax", "ecx", "edx", "st", "memory");
    return result;
}

float mul(float a, float b) { return platform_mulss(a, b); }
float add(float a, float b) { return platform_addss(a, b); }

// GScr_RotateAxis / GScr_MoveAxis: ECX = the handle, the axis and the four words on the stack (EAX/EDX hold
// the first two words, as the wrappers leave them).
using AxisBuiltin = void XB_REGS3 XB_CDECL(uint32_t eax, uint32_t edx, uint32_t ecx, uint32_t axis, uint32_t a,
                                           uint32_t b, uint32_t c, uint32_t d);

// The bad places' duration: ceil(seconds * 1000) with the product on the x87 (FSTP QWORD).
double ceil_ms(float seconds) {
    return own_function<double XB_CDECL(double)>(&title_ceil)(  // __ceil_default
        platform_x87_mul_double(seconds, title_global<float>(0x0083eb9c)));
}

// A yaw wrapped into [0, 360) by whole turns.
float wrap_yaw(float yaw, float turn) {
    if (0.0f > yaw) {
        do yaw += turn;
        while (0.0f > yaw);
    } else if (yaw >= turn) {
        do yaw -= turn;
        while (yaw >= turn);
    }
    return yaw;
}

// Both bad place builtins: the temporary name string around the team lookup, then BadPlace_Create.
void create_bad_place(int32_t duration, const uint32_t* arc, const ScriptString* name, const ScriptString* team,
                      uint32_t temp_text) {
    uint32_t temp = 0;
    ScriptString_FromCString(reinterpret_cast<ScriptString*>(&temp), title_text(temp_text));
    const uint32_t mask = Scr_TeamFromString(team);
    ScriptString_Release(reinterpret_cast<ScriptString*>(&temp));
    BadPlace_Create(duration, arc, name, mask);
}

}  // namespace

uint32_t XB_CDECL GScr_TraceSurfaceType(uint8_t* out, const float* start, const float* end) {
    alignas(16) float from[4] = {start[0], start[1], start[2], start[0]};
    alignas(16) float to[4] = {end[0], end[1], end[2], end[0]};
    alignas(16) float mins[4] = {};
    alignas(16) float maxs[4] = {};
    alignas(16) uint8_t trace[0x40] = {};
    CM_BoxTrace(nullptr, 0, maxs, trace, from, to, mins, nullptr, 0x2820011, 0);
    std::memcpy(out + 4, trace, 12);
    const float fraction = field<float>(trace, 0x20);
    field<float>(out, 0) = fraction;
    const uint32_t hit = field<uint32_t>(trace, 0x30);
    if (hit == 0 || hit == field<uint32_t>(field<uint8_t*>(title_global<uint8_t*>(0x0095f534), 8), 0x224)) {
        field<uint32_t>(out, 0x10) = 0;
    } else {
        field<uint32_t>(out, 0x10) = field<uint32_t>(handle_entity(hit), 0x224);
    }
    if (title_global<float>(0x0083eb20) > fraction) {  // 1 > fraction: a hit
        std::memcpy(out + 0x14, trace + 0x10, 12);
        const int32_t surface = (field<int32_t>(trace, 0x24) >> 20) & 0x1f;
        if (surface > 0 && surface < 0x17)
            return assign_cstring(out + 0x20, title_text(title_global<uint32_t>(0x008d4d08 + surface * 16)));
        return assign_cstring(out + 0x20, title_text(0x00831f70));  // "default"
    }
    float d[3];
    d[2] = end[2] - start[2];
    d[1] = end[1] - start[1];
    d[0] = end[0] - start[0];
    const float length = title_ci_sqrt_float(add(add(mul(d[2], d[2]), mul(d[1], d[1])), mul(d[0], d[0])));
    if (length != title_global<float>(0x0083eafc)) {  // fucomip: NaN normalizes too
        const float inv = title_global<float>(0x0083eb20) / length;
        d[0] = mul(inv, d[0]);
        d[1] = mul(inv, d[1]);
        d[2] = mul(inv, d[2]);
    }
    std::memcpy(out + 0x14, d, 12);
    return assign_cstring(out + 0x20, title_text(0x00831f70));  // "default"
}

void XB_CDECL GScr_BadPlace_Delete(const ScriptString* name) {
    const char* const text = script_text(name);
    if (text && *text) BadPlace_Delete(name);
}

uint32_t XB_CDECL GScr_SpawnBoxEntity(float* mins, float* maxs, uint32_t solid, const uint8_t* owner) {
    for (int i = 0; i < 3; ++i) {
        const float low = mins[i];
        if (low > maxs[i]) {
            reinterpret_cast<uint32_t*>(mins)[i] = reinterpret_cast<const uint32_t*>(maxs)[i];
            maxs[i] = low;
        }
    }
    const int32_t context = owner ? *reinterpret_cast<const int32_t*>(owner + 0x28) : -1;
    uint8_t* const ent = G_Spawn(context);
    ScriptString* const classname = reinterpret_cast<ScriptString*>(ent + 0x25c);
    ScriptString_Assign(classname, &title_global<ScriptString>(0x00984c64));
    field<uint32_t>(ent, 0x260) = Com_HashKeyNoCase(0, script_text(classname));
    std::memcpy(ent + 0xe0, mins, 12);
    std::memcpy(ent + 0xf0, maxs, 12);
    BoxEntity_SetSolidity(ent, 0, solid & 0xff);
    G_UpdateEntityStringHashes(ent);
    return field<uint32_t>(ent, 0x224);
}

void XB_CDECL GScr_RotateRoll(uint32_t handle, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    own_function<AxisBuiltin>(&GScr_RotateAxis)(a, b, handle, 2, a, b, c, d);  // GScr_RotateAxis
}
void XB_CDECL GScr_RotateYaw(uint32_t handle, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    own_function<AxisBuiltin>(&GScr_RotateAxis)(a, b, handle, 1, a, b, c, d);  // GScr_RotateAxis
}
void XB_CDECL GScr_RotatePitch(uint32_t handle, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    own_function<AxisBuiltin>(&GScr_RotateAxis)(a, b, handle, 0, a, b, c, d);  // GScr_RotateAxis
}
void XB_CDECL GScr_MoveZ(uint32_t handle, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    own_function<AxisBuiltin>(&GScr_MoveAxis)(a, b, handle, 2, a, b, c, d);  // GScr_MoveAxis
}
void XB_CDECL GScr_MoveY(uint32_t handle, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    own_function<AxisBuiltin>(&GScr_MoveAxis)(a, b, handle, 1, a, b, c, d);  // GScr_MoveAxis
}
void XB_CDECL GScr_MoveX(uint32_t handle, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    own_function<AxisBuiltin>(&GScr_MoveAxis)(a, b, handle, 0, a, b, c, d);  // GScr_MoveAxis
}

void XB_CDECL GScr_ActorLeaveTurret(uint32_t handle) {
    if (uint8_t* const actor = handle_actor(handle)) Actor_LeaveTurret(actor);
}

void XB_CDECL GScr_GetTagAngles(uint32_t handle, float* out, const ScriptString* tag) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    const uint8_t* const matrix = G_GetCachedTagMatrix(field<uint32_t>(ent, 0x224), tag);
    if (matrix) MatrixToAngles(out, 0, reinterpret_cast<const float*>(matrix));
}

void XB_CDECL GScr_GetTagOrigin(uint32_t handle, uint32_t* out, const ScriptString* tag) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    const uint8_t* const matrix = G_GetCachedTagMatrix(field<uint32_t>(ent, 0x224), tag);
    if (!matrix) return;
    const uint32_t* const origin = reinterpret_cast<const uint32_t*>(matrix + 0x30);
    out[0] = origin[0];
    out[1] = origin[1];
    out[2] = origin[2];
}

void XB_CDECL GScr_BadPlace_Arc(const ScriptString* name, float seconds, const float* origin, float radius,
                                float height, const float* direction, float left, float right, const ScriptString* team) {
    const float ms = static_cast<float>(ceil_ms(seconds));  // FSTP DWORD
    const int32_t duration = static_cast<int32_t>(static_cast<int64_t>(ms));
    float arc[7];
    arc[0] = origin[2];
    arc[1] = origin[1];
    arc[2] = origin[2];
    arc[3] = radius;
    arc[4] = height * title_global<float>(0x0083eb24);  // 0.5
    const float yaw = vectoyaw(direction);
    const float turn = title_global<float>(0x0083ebcc);  // 360
    const float end = add(yaw, right);
    const float start = yaw - left;
    if (end - start >= turn) {
        arc[5] = 0.0f;
        arc[6] = turn;
    } else {
        arc[5] = wrap_yaw(start, turn);
        arc[6] = wrap_yaw(end, turn);
    }
    create_bad_place(duration, reinterpret_cast<const uint32_t*>(arc), name, team, 0x0083240c);  // "badplace_arc"
}

void XB_CDECL GScr_BadPlace_Cylinder(const ScriptString* name, float seconds, const float* origin, float radius,
                                     float height) {
    const int32_t duration = static_cast<int32_t>(static_cast<int64_t>(ceil_ms(seconds)));
    float arc[7];
    arc[0] = origin[2];
    arc[1] = origin[1];
    arc[2] = origin[2];
    arc[3] = radius;
    arc[4] = height * title_global<float>(0x0083eb24);  // 0.5
    arc[5] = 0.0f;
    arc[6] = title_global<float>(0x0083ebcc);  // 360
    create_bad_place(duration, reinterpret_cast<const uint32_t*>(arc), name, name,
                     0x0083241c);  // "badplace_cylinder"
}


namespace {

// A word or float of the original's frame (ESP entries: our C frame lies below a gap, so the original's locals
// are free to use at their addresses).
template <typename T>
T& at(uint32_t address) {
    return *reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}

// A title call at the original's ESP (`top`: ESP before the argument pushes), its return address written back
// into the dead-stack slot as the original's call leaves it.
uint32_t call_at(uint32_t top, uint32_t ret, uint32_t function, const TitleRegs& regs, uint32_t count,
                 const uint32_t* args) {
    const uint32_t result = title_call_at_regs(top, function, &regs, count, args);
    at<uint32_t>(top - count * 4 - 4) = ret;
    return result;
}

// The anim record's list base and index as the original holds them at its AnimTree_GetRootDelta* calls (EDX =
// the list, ECX = the record).
TitleRegs anim_regs(uint32_t anim, uint32_t direction, uint32_t position) {
    TitleRegs regs{};
    regs.eax = anim & 0xffff;
    regs.ecx = addr(anim_tree(anim));
    regs.edx = field<uint32_t>(field<uint8_t*>(title_global<uint8_t*>(0x0095f524), 4), 4);
    regs.edi = direction;
    regs.esi = position;
    return regs;
}

// AnglesToAxis (EDI right, ESI up; angles, forward) at the original's ESP.
void angles_to_axis_at(uint32_t top, uint32_t ret, uint32_t angles, uint32_t forward, uint32_t right, uint32_t up) {
    TitleRegs regs{};
    regs.edi = right;
    regs.esi = up;
    const uint32_t args[2] = {angles, forward};
    call_at(top, ret, own_address(&AnglesToAxis_entry), regs, 2, args);  // AnglesToAxis
}

}  // namespace

float XB_CDECL GScr_GetAngleDelta_c(uint32_t entry_esp) {
    const uint32_t F = entry_esp;
    const uint32_t anim = at<uint32_t>(F + 4);
    const uint32_t dir = F - 0x14, pos = F - 0xc;
    const uint32_t args[2] = {at<uint32_t>(F + 8), at<uint32_t>(F + 0xc)};
    call_at(F - 0x1c, 0x00303f71, own_address(&AnimTree_GetRootDeltaBetween_entry), anim_regs(anim, dir, pos), 2, args);  // AnimTree_GetRootDeltaBetween
    at<float>(F + 4) = vectoyaw_2(&at<float>(dir));  // kept in the first argument slot, then FLD
    return at<float>(F + 4);
}

void XB_CDECL GScr_GetMoveDelta_c(uint32_t entry_esp) {
    const uint32_t F = entry_esp;
    const uint32_t anim = at<uint32_t>(F + 8);
    const uint32_t args[2] = {at<uint32_t>(F + 0xc), at<uint32_t>(F + 0x10)};
    call_at(F - 0x10, 0x00303fd1, own_address(&AnimTree_GetRootDeltaBetween_entry), anim_regs(anim, F - 8, at<uint32_t>(F + 4)), 2,
            args);  // AnimTree_GetRootDeltaBetween
}

void XB_CDECL GScr_GetAnimWorldMoveDelta_c(uint32_t entry_esp) {
    const uint32_t F = entry_esp;
    float* const out = at<float*>(F + 4);
    const uint32_t anim = at<uint32_t>(F + 0xc);
    // Frame: right / direction F-0x54, at 1 F-0x48, at 0 F-0x3c, axis F-0x30 (forward, -right, up).
    float* const right = &at<float>(F - 0x54);
    float* const axis = &at<float>(F - 0x30);
    angles_to_axis_at(F - 0x64, 0x00303ffe, at<uint32_t>(F + 8), F - 0x30, F - 0x54, F - 0x18);
    axis[3] = 0.0f - right[0];
    axis[4] = 0.0f - right[1];
    axis[5] = 0.0f - right[2];
    const uint32_t time0[1] = {0};
    call_at(F - 0x64, 0x00304065, own_address(&AnimTree_GetRootDelta_entry), anim_regs(anim, F - 0x54, F - 0x3c), 1, time0);  // AnimTree_GetRootDelta
    const uint32_t time1[1] = {0x3f800000};  // 1.0
    call_at(F - 0x64, 0x00304098, own_address(&AnimTree_GetRootDelta_entry), anim_regs(anim, F - 0x54, F - 0x48), 1, time1);  // AnimTree_GetRootDelta
    const float* const at0 = &at<float>(F - 0x3c);
    const float* const at1 = &at<float>(F - 0x48);
    const float dx = at1[0] - at0[0];
    const float dz = at1[2] - at0[2];
    const float dy = at1[1] - at0[1];
    for (int i = 0; i < 3; ++i) out[i] = add(add(mul(axis[6 + i], dz), mul(axis[i], dx)), mul(axis[3 + i], dy));
}

void XB_CDECL GScr_GetAnimStartAngles_c(uint32_t entry_esp) {
    const uint32_t F = entry_esp;
    float* const out = at<float*>(F + 4);
    const uint32_t anim = at<uint32_t>(F + 0x10);
    // Frame: forward F-0x8c, -right F-0x80, up F-0x74, direction F-0x5c, position / right F-0x54, the turned
    // axis F-0x48.
    const uint32_t time0[1] = {0};
    call_at(F - 0x98, 0x0030418f, own_address(&AnimTree_GetRootDelta_entry), anim_regs(anim, F - 0x5c, F - 0x54), 1, time0);  // AnimTree_GetRootDelta
    angles_to_axis_at(F - 0x9c, 0x003041a9, at<uint32_t>(F + 0xc), F - 0x8c, F - 0x54, F - 0x74);
    const float zero = 0.0f;
    float* const f = &at<float>(F - 0x8c);
    float* const n = &at<float>(F - 0x80);
    float* const u = &at<float>(F - 0x74);
    const float* const r = &at<float>(F - 0x54);
    n[0] = zero - r[0];
    n[1] = zero - r[1];
    n[2] = zero - r[2];
    const float yaw = mul(vectoyaw_2(&at<float>(F - 0x5c)), title_global<float>(0x0083edd8));  // degrees to radians
    float& s = at<float>(F - 0x5c);
    float& c = at<float>(F - 0x90);
    FastSinCos(&s, 0, &c, yaw);
    const float ns = zero - s;
    const float nnc = zero - (zero - c);
    const float zz = zero - zero;
    // The axis turned by the yaw's rotation {c, s, 0}, {-s, c, 0}, {0, 0, 1} (written out with its zero terms).
    float* const m = &at<float>(F - 0x48);
    m[0] = add(add(mul(c, f[0]), mul(s, n[0])), mul(u[0], zero));
    m[1] = add(add(mul(f[1], c), mul(n[1], s)), mul(u[1], zero));
    m[2] = add(add(mul(f[2], c), mul(n[2], s)), mul(u[2], zero));
    m[3] = add(add(mul(u[0], zz), mul(ns, f[0])), mul(nnc, n[0]));
    m[4] = add(add(mul(u[1], zz), mul(f[1], ns)), mul(nnc, n[1]));
    m[5] = add(add(mul(u[2], zz), mul(f[2], ns)), mul(nnc, n[2]));
    m[6] = add(mul(add(f[0], n[0]), zero), u[0]);
    m[7] = add(mul(add(f[1], n[1]), zero), u[1]);
    m[8] = add(mul(add(f[2], n[2]), zero), u[2]);
    AxisToAngles(out, 0, m);
}

void XB_CDECL GScr_TransformLocalOffset_c(uint32_t entry_esp) {
    const uint32_t F = entry_esp;
    float* const out = at<float*>(F + 4);
    const float* const origin = at<const float*>(F + 8);
    const uint32_t anim = at<uint32_t>(F + 0x10);
    // Frame: right / direction F-0x48, position F-0x3c, forward F-0x30, up F-0x18, the origin's copy F-0xc.
    const uint32_t time0[1] = {0};
    call_at(F - 0x50, 0x003043f9, own_address(&AnimTree_GetRootDelta_entry), anim_regs(anim, F - 0x48, F - 0x3c), 1, time0);  // AnimTree_GetRootDelta
    float* const o = &at<float>(F - 0xc);
    o[0] = origin[0];
    o[1] = origin[1];
    o[2] = origin[2];
    angles_to_axis_at(F - 0x54, 0x00304434, at<uint32_t>(F + 0xc), F - 0x30, F - 0x48, F - 0x18);
    const float* const p = &at<float>(F - 0x3c);
    const float* const f = &at<float>(F - 0x30);
    const float* const r = &at<float>(F - 0x48);
    const float* const u = &at<float>(F - 0x18);
    const float zero = 0.0f;
    out[0] = add(add(add(mul(u[0], p[2]), mul(f[0], p[0])), mul(p[1], zero - r[0])), o[0]);
    out[1] = add(add(add(mul(u[1], p[2]), mul(f[1], p[0])), mul(p[1], zero - r[1])), o[1]);
    out[2] = add(add(add(mul(u[2], p[2]), mul(f[2], p[0])), mul(p[1], zero - r[2])), o[2]);
}

void XB_CDECL GScr_DisconnectPaths_c(uint32_t entry_esp) {
    const uint32_t handle = at<uint32_t>(entry_esp + 4);
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    if (!(field<uint32_t>(ent, 0x2a4) & 0x1000)) {
        own_function<const char* XB_CDECL(const char*, const char*)>(&va_entry)(  // va
            title_text(0x008320e4), script_text(reinterpret_cast<const ScriptString*>(ent + 0x25c)));
        return;
    }
    // No frame: the call runs at the entry ESP (Path_DisconnectPathsForEntity keeps a frame address in 0x00929b40).
    TitleRegs regs{};
    regs.eax = title_global<uint32_t>(0x0095f4f4);
    regs.ecx = field<uint32_t>(ent, 0x2a4);
    regs.edx = handle >> 12;
    const uint32_t args[2] = {title_global<uint32_t>(0x0095f4f4), addr(ent)};
    call_at(entry_esp, 0x0030456f, own_address(&Path_DisconnectPathsForEntity_entry), regs, 2, args);  // Path_DisconnectPathsForEntity (callee pops 8)
}

namespace {

// The memory context for a model: the owner's +0x28, else the entity's +0x220, or the level's (0x0095f638 +0x34)
// for -1.
int32_t model_context(uint8_t* ent, const uint8_t* owner) {
    if (owner) return *reinterpret_cast<const int32_t*>(owner + 0x28);
    const int32_t context = field<int32_t>(ent, 0x220);
    if (context != -1) return context;
    return field<int32_t>(title_global<uint8_t*>(0x0095f638), 0x34);
}

// The cannot-change-the-animtree message with the entity's classname.
void animtree_message(uint8_t* ent) {
    own_function<const char* XB_CDECL(const char*, const char*)>(&va_entry)(  // va
        title_text(0x00831f78), script_text(reinterpret_cast<const ScriptString*>(ent + 0x25c)));
}

// The turret spawners' common part (G_Spawn by the owner's context, classname, origin; Turret_ParseInfo).
uint8_t* spawn_turret(const ScriptString* classname, const uint32_t* origin, const uint8_t* owner) {
    const int32_t context = owner ? *reinterpret_cast<const int32_t*>(owner + 0x28) : -1;
    uint8_t* const ent = G_Spawn(context);
    ScriptString* const name = reinterpret_cast<ScriptString*>(ent + 0x25c);
    ScriptString_Assign(name, classname);
    field<uint32_t>(ent, 0x260) = Com_HashKeyNoCase(0, script_text(name));
    std::memcpy(ent + 0x140, origin, 12);
    return ent;
}

}  // namespace

void XB_CDECL GScr_SetModel(uint32_t handle, const ScriptString* name, const uint8_t* owner) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    const char* const text = script_text(name);
    if (!text) return;
    G_SetModel(model_context(ent, owner), ent, text);
    G_DObjUpdate(ent);
    SV_LinkEntity(ent);
}

uint8_t XB_CDECL GScr_IsTouching_c(uint32_t entry_esp) {
    const uint32_t handle = at<uint32_t>(entry_esp + 4), other = at<uint32_t>(entry_esp + 8);
    uint8_t* const first = handle_entity(handle);
    if (!first) return 0;
    uint8_t* a;  // the box owner (ECX)
    uint8_t* b;  // ESI
    if (field<uint32_t>(first, 0xdc)) {
        a = handle_entity(other);
        b = first;
    } else {
        a = first;
        b = handle_entity(other);
        if (!b) return 0;
    }
    if (!a) return 0;
    const float* const origin = &field<float>(a, 0x140);
    const float* const mins = &field<float>(a, 0xe0);
    const float* const maxs = &field<float>(a, 0xf0);
    const float max_z = add(maxs[2], origin[2]);
    const float min_x = add(mins[0], origin[0]);
    const float min_y = add(mins[1], origin[1]);
    const float min_z = add(mins[2], origin[2]);
    const float max_x = add(maxs[0], origin[0]);
    const float max_y = add(maxs[1], origin[1]);
    if (field<uint32_t>(a, 0xdc) && field<uint32_t>(b, 0xdc)) {
        alignas(16) float amin[4] = {min_x, min_y, min_z, min_x};
        alignas(16) float amax[4] = {max_x, max_y, max_z, max_x};
        const float* const borigin = &field<float>(b, 0x140);
        const float bx = add(borigin[0], field<float>(b, 0xf0));
        const float by = add(borigin[1], field<float>(b, 0xf4));
        const float bz = add(borigin[2], field<float>(b, 0xf8));
        alignas(16) float bmax[4] = {bx, by, bz, bx};
        float low[4], high[4], worst[4];
        platform_subps(low, amin, bmax);
        platform_subps(high, bmax, amax);
        platform_maxps(worst, high, low);
        return (platform_cmpltps_mask(worst, &title_global<float>(0x0082e3c0)) & 7) == 7;
    }
    // SV_ClipToEntity at the original's ESP (EBP frame: A = its aligned ESP), the boxes at its frame addresses:
    // mins A-0x10, maxs A-0x20.
    const uint32_t A = (entry_esp - 4) & ~15u;
    float* const amin = &at<float>(A - 0x10);
    float* const amax = &at<float>(A - 0x20);
    amax[0] = max_x, amax[1] = max_y, amax[2] = max_z, amax[3] = max_x;
    amin[0] = min_x, amin[1] = min_y, amin[2] = min_z, amin[3] = min_x;
    const uint32_t flags = field<uint32_t>(a, 0xd4);
    TitleRegs regs{};
    regs.eax = flags;
    regs.ecx = addr(amax);
    regs.edx = addr(amin);
    regs.esi = addr(b);
    const uint32_t args[3] = {addr(amin), addr(amax), (flags & 0x200) ? 1u : 0u};
    return call_at(A - 0x30, 0x00304871, own_address(&SV_ClipToEntity_entry), regs, 3, args) != 0;  // SV_ClipToEntity
}

void XB_CDECL GScr_SetModelNoFlag(uint32_t handle, const ScriptString* name) {
    if (handle_entity(handle)) GScr_SetModel(handle, name, nullptr);
}

void XB_CDECL GScr_MagicGrenadeManual(uint32_t handle, const float* origin, const float* velocity, float seconds) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const actor = field<uint8_t*>(ent, 0x240);
    if (!actor) return;
    const int32_t fuse = static_cast<int32_t>(seconds * title_global<float>(0x0083eb9c));  // 1000
    fire_grenade(field<uint32_t>(actor, 0x131c), ent, velocity, origin, fuse);
}

void XB_CDECL GScr_ClearAnimTree(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint32_t tree;
    if (ent[0] == 0xb) {
        uint8_t* const actor = field<uint8_t*>(ent, 0x240);
        uint8_t* const info = field<uint8_t*>(actor, 0);
        if (!field<uint32_t>(info, 0x254))
            field<uint32_t>(field<uint8_t*>(actor, 0), 0x254) =
                addr(AnimTree_Create(info, title_global<uint8_t*>(0x00987a90)));
        tree = field<uint32_t>(field<uint8_t*>(actor, 0), 0x254);
    } else {
        tree = field<uint32_t>(ent, 0x254);
    }
    if (tree != field<uint32_t>(ent, 0x254)) animtree_message(ent);
    const uint32_t own = field<uint32_t>(ent, 0x254);
    if (!own) return;
    field<uint32_t>(ent, 0x254) = 0;
    G_DObjUpdate(ent);
    title_global<uint32_t>(0x00987074 + title_global<uint32_t>(0x00987070) * 4) = own;
    title_global<uint32_t>(0x00987070) = title_global<uint32_t>(0x00987070) + 1;
    AnimTree_FlushClearLists();
}

void XB_CDECL GScr_UseAnimTree(uint32_t handle, const ScriptString* name) {
    const uint32_t rep = *reinterpret_cast<const uint32_t*>(name);
    if (!rep || !title_global<uint16_t>(rep + 2)) return;
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint8_t* const record = G_FindAnimTreeByName(title_text(rep + 6));
    if (!record || !field<uint32_t>(record, 4)) return;
    if (G_GetEntityAnimTree(ent) != field<uint32_t>(ent, 0x254)) animtree_message(ent);
    G_SetEntityAnimTree(record, ent);
}

void XB_CDECL GScr_DoorUnlock(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    const uint32_t classname = field<uint32_t>(ent, 0x260);
    if (classname != title_global<uint32_t>(0x0097a6e0) && classname != title_global<uint32_t>(0x0097a6dc)) return;
    field<uint32_t>(ent, 0x3b4) = 0;
    Door_UpdatePaths(ent);
}

void XB_CDECL GScr_DoorLock(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    const uint32_t classname = field<uint32_t>(ent, 0x260);
    if (classname != title_global<uint32_t>(0x0097a6e0) && classname != title_global<uint32_t>(0x0097a6dc)) return;
    field<uint32_t>(ent, 0x3b4) = 1;
    Door_UpdatePaths(ent);
}

void XB_CDECL GScr_GetEye(uint32_t handle, uint32_t* out) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    const uint8_t* const sentient = field<uint8_t*>(ent, 0x244);
    if (!sentient) return;
    uint32_t eye[3];
    Sentient_EyeOrigin(sentient, 0, reinterpret_cast<uint8_t*>(eye));
    out[0] = eye[0];
    out[1] = eye[1];
    out[2] = eye[2];
}

void XB_CDECL GScr_DetachAll(uint32_t handle) {
    if (uint8_t* const ent = handle_entity(handle)) G_EntDetachAll(ent);
}

void XB_CDECL GScr_Detach(uint32_t handle, const ScriptString* model, const ScriptString* tag) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    const char* const tag_text = script_text(tag);
    G_EntDetach(ent, script_text(model), tag_text);
}

void XB_CDECL GScr_Attach(uint32_t handle, const ScriptString* model, const ScriptString* tag, uint32_t ignore,
                          const uint8_t* owner) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    const int32_t context = model_context(ent, owner);
    if (!script_text(model) || !script_text(tag)) return;
    G_EntDetach(ent, script_text(model), script_text(tag));
    const char* const tag_text = script_text(tag);
    G_EntAttach(ent, context, script_text(model), tag_text, ignore & 0xff);
}

uint32_t XB_CDECL GScr_SpawnTurretWithAngles(const ScriptString* classname, const uint32_t* origin,
                                             const ScriptString* weapon, const uint32_t* angles, const uint8_t* owner) {
    uint8_t* const ent = spawn_turret(classname, origin, owner);
    std::memcpy(ent + 0x150, angles, 12);
    Turret_ParseInfo(ent, script_text(weapon));
    G_UpdateEntityStringHashes(ent);
    return field<uint32_t>(ent, 0x224);
}

uint32_t XB_CDECL GScr_SpawnTurret(const ScriptString* classname, const uint32_t* origin, const ScriptString* weapon,
                                   const uint8_t* owner) {
    uint8_t* const ent = spawn_turret(classname, origin, owner);
    Turret_ParseInfo(ent, script_text(weapon));
    G_UpdateEntityStringHashes(ent);
    return field<uint32_t>(ent, 0x224);
}

void XB_CDECL GScr_Vehicle_EjectDriver(uint32_t handle) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    const uint32_t owner = field<uint32_t>(ent, 0x1a0);
    if (!handle_entity(owner)) {
        own_function<const char* XB_CDECL(const char*)>(&va_entry)(title_text(0x00831968));  // va "No driver to eject"
        return;
    }
    uint8_t* const driver = handle_entity(owner);
    if (field<uint8_t*>(driver, 0x23c)) Vehicle_ExitPlayer(driver);
}

uint8_t XB_CDECL GScr_ActorMayMoveToPoint_c(uint32_t entry_esp) {
    const uint32_t F = entry_esp;
    const uint32_t handle = at<uint32_t>(F + 4);
    if (!handle_actor(handle)) return 0;
    // Frame (EBP = F - 4, A = its 16-byte aligned ESP): origin {x, y, z, x} at A-0x20, bounds at A-0x10.
    const uint32_t A = (F - 4) & ~15u;
    const float* const point = at<const float*>(F + 8);
    float* const bounds = &at<float>(A - 0x10);
    for (uint32_t i = 0; i < 4; ++i) at<uint32_t>(A - 0x10 + i * 4) = title_global<uint32_t>(0x0082e110 + i * 4);
    float* const origin = &at<float>(A - 0x20);
    origin[2] = point[2];
    origin[1] = point[1];
    origin[3] = point[0];
    origin[0] = point[0];
    TitleRegs regs{};
    regs.eax = addr(origin);
    regs.ecx = at<uint8_t>(F + 0x10);
    regs.edx = handle;
    regs.esi = title_global<uint32_t>(0x0097a96c + (handle & 0xfff) * 8);
    const uint32_t args[14] = {handle, addr(origin), title_global<uint32_t>(0x0098a550), 0x41c00000, at<uint32_t>(F + 0xc),
                               0, 0, 0, 1, addr(bounds), own_address(&G_TraceCapsule), own_address(&G_Trace), 1, 0x42700000};
    return call_at(A - 0x30, 0x0030554c, own_address(&BG_CheckProne_entry), regs, 14, args) != 0;  // BG_CheckProne (G_TraceCapsule, G_Trace)
}

void XB_CDECL GScr_ActorGrabGrenade(uint32_t handle) {
    uint8_t* const actor = handle_actor(handle);
    if (actor && field<uint32_t>(actor, 0x1310)) Actor_Grenade_Grab(actor);
}

void XB_CDECL GScr_ActorEnterProne(uint32_t handle, float seconds) {
    uint8_t* const actor = handle_actor(handle);
    if (!actor) return;
    Actor_EnterProne(actor, static_cast<int32_t>(seconds * title_global<float>(0x0083eb9c)));  // 1000, CVTTSS2SI
}

void XB_CDECL GScr_MagicGrenadeManualDefaultFuse(uint32_t handle, const float* origin, const float* velocity) {
    GScr_MagicGrenadeManual(handle, origin, velocity, 5.0f);
}


namespace {

// A script string's text when it is not empty (the sound builtins' check), else null.
const char* nonempty_text(const ScriptString* s) {
    const char* const text = script_text(s);
    return text && *text ? text : nullptr;
}

uint32_t handle_generation(uint32_t handle) { return title_global<uint32_t>(0x0097a96c + (handle & 0xfff) * 8); }

// GScr_PlayLoopSound / GScr_PlaySound: FXCmd_PlaySoundAlias (ECX entity; alias, looped, -1, flag) after the
// optional sound-done notify.
int32_t play_sound_alias(uint32_t F, bool looped, bool check_pending, uint32_t ret) {
    const ScriptString* const alias = at<const ScriptString*>(F + 8);
    if (!nonempty_text(alias)) return -1;
    const uint32_t handle = at<uint32_t>(F + 4);
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return -1;
    if (check_pending && field<uint32_t>(ent, 0x3c4)) return -1;
    if (const uint32_t notify = at<uint32_t>(F + 0xc)) G_SetSoundDoneNotify(ent, notify);
    const char* const text = script_text(alias);
    TitleRegs regs{};
    regs.eax = addr(text);
    regs.ecx = addr(ent);
    regs.edx = handle_generation(handle);
    regs.esi = addr(ent);
    regs.edi = addr(alias);
    const uint32_t args[4] = {addr(text), looped ? 1u : 0u, 0xffffffffu, at<uint32_t>(F + 0x14)};
    return static_cast<int32_t>(call_at(F - 8, ret, own_address(&FXCmd_PlaySoundAlias_entry), regs, 4, args));  // FXCmd_PlaySoundAlias
}

uint64_t tail_jump(uint32_t target, uint32_t value) { return (static_cast<uint64_t>(value) << 32) | target; }

// GScr_Spawn / GScr_SpawnWithFlags: G_CallSpawn of a new entity with the classname, origin and spawn flags.
uint8_t* spawn_by_classname(const ScriptString* classname, const uint32_t* origin, uint32_t flags, const uint8_t* owner) {
    const int32_t context = owner ? *reinterpret_cast<const int32_t*>(owner + 0x28) : -1;
    uint8_t* const ent = G_Spawn(context);
    ScriptString* const name = reinterpret_cast<ScriptString*>(ent + 0x25c);
    ScriptString_Assign(name, classname);
    field<uint32_t>(ent, 0x260) = Com_HashKeyNoCase(0, script_text(name));
    std::memcpy(ent + 0x140, origin, 12);
    field<uint32_t>(ent, 0x2a0) = flags;
    if (G_CallSpawn(ent)) return ent;
    own_function<const char* XB_CDECL(const char*, const char*)>(&va_entry)(  // va
        title_text(0x00832598), script_text(classname));  // "unable to spawn \"%s\" entity"
    return nullptr;
}

}  // namespace

void XB_CDECL GScr_DetachNoTag(uint32_t handle, const ScriptString* model) {
    uint32_t tag = 0;
    ScriptString_FromCString(reinterpret_cast<ScriptString*>(&tag), title_text(0x0082dfeb));  // ""
    GScr_Detach(handle, model, reinterpret_cast<const ScriptString*>(&tag));
    ScriptString_Release(reinterpret_cast<ScriptString*>(&tag));
}

void XB_CDECL GScr_AttachNoTag(uint32_t handle, const ScriptString* model) {
    uint32_t tag = 0;
    ScriptString_FromCString(reinterpret_cast<ScriptString*>(&tag), title_text(0x0082dfeb));  // ""
    GScr_Attach(handle, model, reinterpret_cast<const ScriptString*>(&tag), 0, nullptr);
    ScriptString_Release(reinterpret_cast<ScriptString*>(&tag));
}

void XB_CDECL GScr_AttachWithOwner(uint32_t handle, const ScriptString* model, const ScriptString* tag, const uint8_t* owner) {
    GScr_Attach(handle, model, tag, 0, owner);
}

int32_t XB_CDECL GScr_PlaySoundMode7_c(uint32_t entry_esp) {
    const uint32_t F = entry_esp;
    const char* const text = nonempty_text(at<const ScriptString*>(F + 8));
    if (!text) return -1;
    uint8_t* const ent = handle_entity(at<uint32_t>(F + 4));
    if (!ent) return -1;
    TitleRegs regs{};
    regs.eax = addr(ent);
    regs.ecx = at<uint32_t>(F + 0xc);
    regs.edx = addr(text);
    const uint32_t args[3] = {addr(ent), addr(text), at<uint32_t>(F + 0xc)};
    return static_cast<int32_t>(call_at(F, 0x00305dd5, own_address(&FXCmd_PlaySoundAliasMode7_entry), regs, 3, args));  // FXCmd_PlaySoundAliasMode7
}

int32_t XB_CDECL GScr_PlayLoopSound_c(uint32_t entry_esp) { return play_sound_alias(entry_esp, true, false, 0x00305e55); }

int32_t XB_CDECL GScr_PlaySoundAtPosition_c(uint32_t entry_esp) {
    const uint32_t F = entry_esp;
    const ScriptString* const alias = at<const ScriptString*>(F + 8);
    if (!nonempty_text(alias)) return -1;
    const uint32_t handle = at<uint32_t>(F + 4);
    uint8_t* const ent = handle_entity(handle);
    if (!ent || field<uint32_t>(ent, 0x3c4)) return -1;
    if (const uint32_t notify = at<uint32_t>(F + 0x10)) G_SetSoundDoneNotify(ent, notify);
    const char* const text = script_text(alias);
    TitleRegs regs{};
    regs.eax = addr(text);
    regs.ecx = at<uint32_t>(F + 0xc);
    regs.edx = handle_generation(handle);
    regs.esi = addr(ent);
    regs.edi = addr(alias);
    const uint32_t args[4] = {addr(ent), addr(text), at<uint32_t>(F + 0xc), 0};
    return static_cast<int32_t>(call_at(F - 8, 0x00305eec, own_address(&FXCmd_PlaySoundAliasAtPoint_entry), regs, 4, args));  // FXCmd_PlaySoundAliasAtPoint
}

int32_t XB_CDECL GScr_PlaySound_c(uint32_t entry_esp) { return play_sound_alias(entry_esp, false, true, 0x00305f7f); }

uint64_t XB_CDECL GScr_PlayerWeaponSound_c(uint32_t entry_esp) {
    const uint32_t F = entry_esp;
    uint8_t* const ent = handle_entity(at<uint32_t>(F + 4));
    if (!ent) return tail_jump(0, 0xffffffffu);
    uint8_t* const client = field<uint8_t*>(ent, 0x23c);
    if (!client) return tail_jump(0, 0xffffffffu);
    const uint32_t record = 0x0096bd10 + field<uint32_t>(client, 0x80) * 0x34;
    const uint32_t kind = at<uint32_t>(F + 8);
    if (kind > 2) return tail_jump(0, 0xffffffffu);
    at<uint32_t>(F + 8) = 0x17 + kind;
    at<uint32_t>(F + 4) = title_global<uint32_t>(record + 4);
    return tail_jump(own_address(&FXCmd_PlayWeaponSound_entry), addr(ent));  // FXCmd_PlayWeaponSound (EAX = the entity)
}

uint32_t XB_CDECL GScr_SpawnVehicle(const ScriptString* model, const ScriptString* targetname, const ScriptString* def,
                                    const uint32_t* origin, const uint32_t* angles, const uint8_t* owner) {
    const int32_t context = owner ? *reinterpret_cast<const int32_t*>(owner + 0x28) : -1;
    uint8_t* const ent = G_Spawn(context);
    ScriptString* const classname = reinterpret_cast<ScriptString*>(ent + 0x25c);
    ScriptString_Assign(classname, &title_global<ScriptString>(0x00984c00));
    field<uint32_t>(ent, 0x260) = Com_HashKeyNoCase(0, script_text(classname));
    G_SetModel(-1, ent, script_text(model));
    if (!field<uint32_t>(ent, 0x258)) {
        G_FreeEntity(ent);
        return 0;
    }
    ScriptString_Assign(reinterpret_cast<ScriptString*>(ent + 0x264), targetname);
    std::memcpy(ent + 0x140, origin, 12);
    std::memcpy(ent + 0x150, angles, 12);
    Vehicle_Init(ent, script_text(def), 0);
    G_UpdateEntityStringHashes(ent);
    return field<uint32_t>(ent, 0x224);
}

uint32_t XB_CDECL GScr_SpawnWithFlags(const ScriptString* classname, const uint32_t* origin, uint32_t flags,
                                      const uint8_t* owner) {
    uint8_t* const ent = spawn_by_classname(classname, origin, flags, owner);
    if (!ent) return 0;
    G_UpdateEntityStringHashes(ent);
    return field<uint32_t>(ent, 0x224);
}

uint32_t XB_CDECL GScr_Spawn(const ScriptString* classname, const uint32_t* origin, const uint8_t* owner) {
    uint8_t* const ent = spawn_by_classname(classname, origin, 0, owner);
    return ent ? field<uint32_t>(ent, 0x224) : 0;
}

uint32_t XB_CDECL GScr_ActorUpdateNearestNode_c(uint32_t entry_esp) {
    const uint32_t F = entry_esp;
    const uint32_t handle = at<uint32_t>(F + 4);
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return 0;
    uint8_t* const actor = field<uint8_t*>(ent, 0x240);
    if (!actor) return 0;
    TitleRegs regs{};
    regs.eax = addr(ent);
    regs.ecx = handle >> 12;
    regs.edx = handle_generation(handle);
    regs.esi = addr(actor);
    regs.edi = field<uint32_t>(actor, 4);
    const uint32_t args[6] = {0, 0, 0, 0, 0x43400000, 0};  // radius 192
    const uint32_t node = call_at(F - 8, 0x003062ee, own_address(&Sentient_CachedNearestNode_entry), regs, 6, args);  // Sentient_CachedNearestNode
    field<uint32_t>(actor, 0x7e0) = node;
    return node ? title_global<uint16_t>(node) : 0;
}

uint32_t XB_CDECL GScr_ActorNodeCanSeeEnemyNode_c(uint32_t entry_esp) {
    const uint32_t F = entry_esp;
    const uint32_t handle = at<uint32_t>(F + 4);
    uint8_t* const actor = handle_actor(handle);
    if (!actor) return 0;
    uint8_t* const sentient = field<uint8_t*>(actor, 4);
    const uint32_t enemy = field<uint32_t>(sentient, 0x3c);
    if (!enemy) return 0;
    const uint32_t args[6] = {0, 0, 0, 1, 0x43400000, 0};  // radius 192
    TitleRegs regs{};
    regs.eax = enemy;
    regs.ebx = addr(actor);
    regs.ecx = handle >> 12;
    regs.edx = handle_generation(handle);
    regs.edi = addr(sentient);
    const uint32_t own = call_at(F - 0xc, 0x0030636f, own_address(&Sentient_CachedNearestNode_entry), regs, 6, args);  // Sentient_CachedNearestNode
    regs.eax = field<uint32_t>(actor, 4);
    regs.esi = own;
    regs.edi = field<uint32_t>(field<uint8_t*>(actor, 4), 0x3c);
    const uint32_t theirs = call_at(F - 0xc, 0x0030638b, own_address(&Sentient_CachedNearestNode_entry), regs, 6, args);  // Sentient_CachedNearestNode
    if (!own || !theirs) return 0;
    if (own == theirs) return 1;
    regs.eax = title_global<uint32_t>(0x0095f4f4);  // g_pathNodeBlocks
    regs.ecx = own;
    const uint32_t other[1] = {theirs};
    return (call_at(F - 0xc, 0x003063a4, own_address(&PathNode_CanSee_entry), regs, 1, other) & 0xff) ? 1 : 0;  // PathNode_CanSee
}

uint32_t XB_CDECL GScr_DropWeapon(uint32_t handle, const ScriptString* weapon, uint32_t side, float speed) {
    const uint32_t rep = *reinterpret_cast<const uint32_t*>(weapon);
    if (rep && Q_stricmpn(title_text(rep + 6), title_text(0x00832430), 99999) == 0) return 0;  // "none"
    uint8_t* const actor = handle_actor(handle);
    if (!actor) return 0;
    const char* const text = script_text(weapon);
    uint32_t index = 0;
    if (text && *text) index = BG_FindWeaponIndexForHash(Com_HashKeyNoCase(0, text)) & 0xff;
    if (!index)
        own_function<const char* XB_CDECL(const char*, const char*)>(&va_entry)(  // va
            title_text(0x00831e38), text);  // "unknown weapon '%s' in dropWeapon"
    const char* tag = nullptr;
    if (side == title_global<uint32_t>(0x0097a714))  // scr_const_left
        tag = title_text(0x00831e28);                 // "tag_weapon_left"
    else if (side == title_global<uint32_t>(0x0097a774))  // scr_const_right
        tag = title_text(0x00831e14);                      // "tag_weapon_right"
    uint8_t* const item = reinterpret_cast<uint8_t*>(
        static_cast<uintptr_t>(Drop_Weapon(field<uint8_t*>(actor, 0), index, tag)));
    if (speed != 0.0f) {  // ucomiss: NaN throws too
        uint8_t* const ent = field<uint8_t*>(actor, 0);
        const float centre_z = add(mul(field<float>(ent, 0xf8) - field<float>(ent, 0xe8), title_global<float>(0x0083eb24)),
                                   field<float>(ent, 0x148));
        float dir[3];
        dir[0] = field<float>(item, 0x140) - field<float>(ent, 0x140);
        dir[1] = field<float>(item, 0x144) - field<float>(ent, 0x144);
        dir[2] = add(field<float>(item, 0x148) - centre_z, title_global<float>(0x0083eba8));
        VectorNormalize(dir);
        field<float>(item, 0x2c) = mul(dir[0], speed);
        field<float>(item, 0x30) = mul(dir[1], speed);
        field<float>(item, 0x34) = mul(dir[2], speed);
    } else {
        field<float>(item, 0x34) = 0.0f;
        field<float>(item, 0x30) = 0.0f;
        field<float>(item, 0x2c) = 0.0f;
    }
    field<uint32_t>(item, 0x18) = title_global<uint32_t>(0x0098540c);
    return field<uint32_t>(item, 0x224);
}

uint32_t XB_CDECL GScr_ActorGetNearestNode_c(uint32_t entry_esp) {
    const uint32_t F = entry_esp;
    uint8_t* const actor = handle_actor(at<uint32_t>(F + 4));
    if (!actor) return 0;
    const uint32_t sentient = field<uint32_t>(actor, 4);
    TitleRegs regs{};
    regs.eax = sentient;
    regs.ecx = at<uint32_t>(F + 0xc);
    const uint32_t args[5] = {sentient, 0, 0, at<uint32_t>(F + 8), at<uint32_t>(F + 0xc)};
    const uint32_t node = call_at(F - 4, 0x00306da1, own_address(&Actor_FindNearestNode_entry), regs, 5, args);  // Actor_FindNearestNode
    return node ? title_global<uint16_t>(node) : 0;
}

uint32_t XB_CDECL GScr_ActorCanShootWithOffset_c(uint32_t entry_esp) {
    const uint32_t F = entry_esp;
    uint8_t* const actor = handle_actor(at<uint32_t>(F + 4));
    if (!actor) return 0;
    float* const muzzle = &at<float>(F - 0x18);
    const float unset = title_global<float>(0x00922eb8);
    muzzle[0] = unset;
    muzzle[1] = unset;
    muzzle[2] = unset;
    TitleRegs regs{};
    regs.eax = addr(muzzle);
    regs.edi = addr(actor);
    const uint32_t muzzle_args[2] = {addr(muzzle), 0};
    if (!call_at(F - 0x1c, 0x00306e0f, own_address(&Actor_GetMuzzleInfo_entry), regs, 2, muzzle_args)) return 0;  // Actor_GetMuzzleInfo
    const float* const offset = at<const float*>(F + 0xc);
    float* const start = &at<float>(F - 0xc);
    start[0] = add(offset[0], muzzle[0]);
    start[1] = add(offset[1], muzzle[1]);
    start[2] = add(offset[2], muzzle[2]);
    muzzle[0] = start[0];
    muzzle[2] = start[2];
    muzzle[1] = start[1];
    title_global<uint32_t>(0x0095f704) = field<uint32_t>(actor, 0);
    regs.eax = addr(muzzle);
    regs.ecx = at<uint32_t>(F + 8);
    regs.edx = at<uint32_t>(F - 8);
    const uint32_t shot_args[1] = {addr(actor)};
    const uint32_t clear = call_at(F - 0x1c, 0x00306e7e, own_address(&Actor_IsShotClear_entry), regs, 1, shot_args);  // Actor_IsShotClear
    title_global<uint32_t>(0x0095f704) = 0;
    return clear;
}

uint64_t XB_CDECL GScr_ActorSetAimAnimWeights_c(uint32_t entry_esp) {
    const uint32_t F = entry_esp;
    uint8_t* const actor = handle_actor(at<uint32_t>(F + 4));
    if (!actor) return 0;
    const int32_t start = field<int32_t>(actor, 0x19c);
    if (!start || field<uint32_t>(actor, 0x198)) return 0;
    const int32_t blend = field<int32_t>(actor, 0x1a0);
    const int32_t now = title_global<int32_t>(0x0098540c);
    if (blend < 0) {
        if (start - blend < now) {
            field<uint32_t>(actor, 0x19c) = 0;
            return 0;
        }
    } else if (blend > 0 && blend + start < now) {
        field<uint32_t>(actor, 0x1a0) = 0;
    }
    uint8_t* const tree = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(Actor_GetAnimTree(actor)));
    const uint32_t goal = at<uint32_t>(F + 0x10), blend_time = at<uint32_t>(F + 0x14), rate = at<uint32_t>(F + 0x18);
    AnimTree_SetGoalWeight(at<uint32_t>(F + 8) & 0xffff, tree, goal, blend_time, rate, 0, 0, 0);
    AnimTree_SetGoalWeight(at<uint32_t>(F + 0xc) & 0xffff, tree, goal, blend_time, rate, 0, 0, 0);
    return tail_jump(own_address(&Actor_UpdateAimBlend_entry), addr(actor));  // Actor_UpdateAimBlend (ECX = actor, EAX = 0)
}

void XB_CDECL GScr_RadiusDamageFromEntity_c(uint32_t entry_esp) {
    const uint32_t F = entry_esp;
    const uint32_t ent = addr(handle_entity(at<uint32_t>(F + 4)));
    title_global<uint32_t>(0x0098600c) = title_global<uint32_t>(0x00986010);
    TitleRegs regs{};
    regs.eax = at<uint32_t>(F + 0x14);
    regs.ecx = at<uint32_t>(F + 0x10);
    regs.edx = at<uint32_t>(F + 8);
    const uint32_t args[8] = {at<uint32_t>(F + 8), ent, ent, at<uint32_t>(F + 0x10), at<uint32_t>(F + 0x14),
                              at<uint32_t>(F + 0xc), 0, 0x17};
    call_at(F, 0x00306fb4, own_address(&G_RadiusDamage_entry), regs, 8, args);  // G_RadiusDamage
    title_global<uint32_t>(0x0098600c) = 0;
}

void XB_CDECL GScr_RadiusDamage_c(uint32_t entry_esp) {
    const uint32_t F = entry_esp;
    title_global<uint32_t>(0x0098600c) = title_global<uint32_t>(0x00986010);
    const uint32_t world = field<uint32_t>(title_global<uint8_t*>(0x0095f534), 8);
    TitleRegs regs{};
    regs.eax = at<uint32_t>(F + 4);
    regs.ecx = at<uint32_t>(F + 0x10);
    regs.edx = at<uint32_t>(F + 0xc);
    const uint32_t args[8] = {at<uint32_t>(F + 4), world, world, at<uint32_t>(F + 0xc), at<uint32_t>(F + 0x10),
                              at<uint32_t>(F + 8), 0, 0x17};
    call_at(F, 0x00307002, own_address(&G_RadiusDamage_entry), regs, 8, args);  // G_RadiusDamage
    title_global<uint32_t>(0x0098600c) = 0;
}


namespace {

// GScr_ActorBlendAimToPoint's inlined goal-weight setter: node `node` of the tree towards `goal` (0 under 0.001)
// over 0.1 (an info is allocated unless the goal is 0; the root goes to 1 at once), then its parents, time and
// notetrack.
void blend_aim_node(uint8_t* tree, uint32_t node, float goal) {
    const float zero = 0.0f;
    const float small = title_global<float>(0x0083eb4c);  // 0.001
    if (small > goal) goal = zero;
    title_global<uint32_t>(0x00928588) = addr(tree);
    const uint16_t index = *reinterpret_cast<const uint16_t*>(tree + 0x18 + node * 2);
    float time = title_global<float>(0x0083ebf8);  // 0.1
    uint8_t* info = nullptr;
    if (index) {
        info = &title_global<uint8_t>(0x00965cf0 + index * 0x30u);
    } else if (goal != zero) {
        info = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(AnimTree_AllocInfo(node, tree)));
    }
    if (info) {
        if (node == 0) {
            goal = title_global<float>(0x0083eb20);  // 1
            time = zero;
        }
        float distance = goal - field<float>(info, 0x24);
        uint32_t bits;
        std::memcpy(&bits, &distance, 4);
        bits &= 0x7fffffff;
        std::memcpy(&distance, &bits, 4);
        const float rate = mul(distance, time);
        field<float>(info, 0x20) = goal;
        field<float>(info, 0x1c) = rate;
        if (small > rate) {
            field<float>(info, 0x1c) = zero;
            field<float>(info, 0x24) = goal;
        } else if (field<float>(info, 0x24) == zero) {
            field<float>(info, 0x24) = mul(goal, small);
        }
        field<float>(info, 0x28) = title_global<float>(0x0083eb20);  // 1
        field<uint16_t>(info, 2) = 0xffff;
        field<uint16_t>(info, 0) = 0;
    }
    AnimTree_ActivateParents(node, 0x3dcccccd);  // 0.1
    AnimTree_ResetTime(node, 0);
    AnimTree_UpdateNoteIndex(0, node);
}

}  // namespace

float XB_CDECL GScr_ActorBlendAimToPoint(uint32_t handle, const float* point) {
    uint8_t* const actor = handle_actor(handle);
    if (!actor) return title_global<float>(0x0083eafc);  // 0
    const float zero = 0.0f;
    const float one = title_global<float>(0x0083eb20);
    float down = zero, straight, up = zero;
    float eye[3];
    Sentient_EyeOrigin(field<uint8_t*>(actor, 4), 0, reinterpret_cast<uint8_t*>(eye));
    const float dz = point[2] - eye[2];
    const float dy = point[1] - eye[1];
    const float dx = point[0] - eye[0];
    const float length = title_ci_sqrt_float(add(add(mul(dz, dz), mul(dy, dy)), mul(dx, dx)));
    float s = dz;
    if (length != title_global<float>(0x0083eafc)) s = mul(one / length, dz);  // fucomip: NaN divides too
    if (s > zero) {
        up = s;
        straight = one - s;
    } else {
        down = zero - s;
        straight = one - down;
    }
    uint8_t* const info = field<uint8_t*>(actor, 0);
    if (!field<uint32_t>(info, 0x254))
        field<uint32_t>(field<uint8_t*>(actor, 0), 0x254) = addr(AnimTree_Create(info, title_global<uint8_t*>(0x00987a90)));
    uint8_t* const tree = field<uint8_t*>(field<uint8_t*>(actor, 0), 0x254);
    const auto bits = [](float v) {
        uint32_t b;
        std::memcpy(&b, &v, 4);
        return b;
    };
    AnimTree_SetGoalWeight(field<uint16_t>(actor, 0x160), tree, bits(down), 0x3dcccccd, 0x3f800000, 0, 0, 0);
    AnimTree_SetGoalWeight(field<uint16_t>(actor, 0x164), tree, bits(straight), 0x3dcccccd, 0x3f800000, 0, 0, 0);
    AnimTree_SetGoalWeight(field<uint16_t>(actor, 0x168), tree, bits(up), 0x3dcccccd, 0x3f800000, 0, 0, 0);
    blend_aim_node(tree, field<uint16_t>(actor, 0x16c), down);
    blend_aim_node(tree, field<uint16_t>(actor, 0x170), straight);
    blend_aim_node(tree, field<uint16_t>(actor, 0x174), up);
    return title_global<float>(0x0083ebf8);  // 0.1
}

// ---- 0x00307010 on (push 2) ----

namespace {

// A word copy of a vector (movss pairs: the bits as they are).
void copy3(void* dst, const void* src) {
    const uint32_t* const s = static_cast<const uint32_t*>(src);
    uint32_t* const d = static_cast<uint32_t*>(dst);
    d[0] = s[0];
    d[1] = s[1];
    d[2] = s[2];
}

// call_at for callees with an XMM0 result: its bits.
uint32_t call_at_xmm0(uint32_t top, uint32_t ret, uint32_t function, const TitleRegs& regs, uint32_t count,
                      const uint32_t* args) {
    const uint32_t bits = actor_call_at_xmm0_raw(top, function, &regs, count, args);
    at<uint32_t>(top - count * 4 - 4) = ret;
    return bits;
}

const char* title_va(uint32_t format) {
    return own_function<const char* XB_CDECL(const char*)>(&va_entry)(title_text(format));  // va
}

}  // namespace

void XB_CDECL GScr_Vehicle_Kill_c(uint32_t entry_esp) {
    const uint32_t F = entry_esp;
    uint8_t* const ent = handle_entity(at<uint32_t>(F + 4));
    if (!ent || !field<uint32_t>(ent, 0x248)) return;
    TitleRegs regs{};
    regs.eax = field<uint32_t>(ent, 0x248);
    regs.esi = addr(ent);
    call_at(F - 8, 0x0030704c, own_address(&Vehicle_FreePhysics_entry), regs, 0, nullptr);  // Vehicle_FreePhysics (ESI = entity)
    ent[0] = 0xf;
    ScriptString* const classname = reinterpret_cast<ScriptString*>(ent + 0x25c);
    ScriptString_Assign(classname, &title_global<ScriptString>(0x00984c04));
    field<uint32_t>(ent, 0x260) = Com_HashKeyNoCase(0, script_text(classname));
    G_UpdateEntityStringHashes(ent);
    Scr_Notify(ent, title_global<uint32_t>(0x0097a684));  // scr_const_death
}

void XB_CDECL GScr_Vehicle_AttachPath_c(uint32_t entry_esp) {
    const uint32_t F = entry_esp;
    uint8_t* const ent = handle_entity(at<uint32_t>(F + 4));
    if (!ent) return;
    const int32_t node = *at<const int32_t*>(F + 8);
    if (node == -1) return;
    uint8_t* const vehicle = field<uint8_t*>(ent, 0x248);
    if (!vehicle) return;
    uint8_t* const def = title_global<uint8_t*>(0x00950a28 + field<int16_t>(vehicle, 0x178) * 4);  // g_vehicleDefs
    uint8_t* const state = vehicle + 0xc0;
    if (ent[0x297] == 2) title_va(0x008319f4);  // "Vehicle is invalid on path after it's been used"
    VehPath_InitAtNode(static_cast<uint32_t>(node), vehicle);
    const auto place = [&] {
        field<uint32_t>(state, 0) = field<uint32_t>(vehicle, 0x14);
        field<uint32_t>(state, 4) = field<uint32_t>(vehicle, 0x18);
        field<uint32_t>(state, 8) = field<uint32_t>(vehicle, 0x1c);
        field<uint32_t>(state, 0x20) = field<uint32_t>(vehicle, 0x20);
        field<uint32_t>(state, 0x24) = field<uint32_t>(vehicle, 0x24);
        field<uint32_t>(state, 0x28) = field<uint32_t>(vehicle, 0x28);
    };
    // Vehicle_SetOriginAndAngles, then position +0x10 and angles +0x30 copied from the new origin/angles.
    const auto set_origin = [&] {
        Vehicle_SetOriginAndAngles(ent, &field<float>(state, 0), &field<float>(state, 0x20));
        copy3(state + 0x10, state);
        copy3(state + 0x30, state + 0x20);
    };
    place();
    set_origin();
    // The wheel tags' (0x009886f8, 6 names) heights: z of the world tag matrix into +0x78 + 4 * i.
    alignas(16) float matrix[16];
    for (uint32_t i = 0; i < 6; ++i) {
        const DObj* const obj = field<const DObj*>(ent, 0x22c);
        if (!obj) continue;
        const int32_t bone = DObj_GetTagIndex(obj, title_global<uint32_t>(0x009886f8 + i * 4));
        if (bone < 0) continue;
        G_DObjGetWorldTagMatrix(ent, static_cast<uint32_t>(bone), matrix);
        field<uint32_t>(state, 0x78 + i * 4) = reinterpret_cast<const uint32_t*>(matrix)[14];
    }
    const int16_t type = field<int16_t>(def, 0x20);
    if (type == 1 || type == 2) {
        // Vehicle_UpdatePhysics (fall 0, the frame time 0x008dda9c) at the original's ESP: EBP frame, A = its
        // aligned ESP, the body at A-0x60.
        const uint32_t A = (F - 4) & ~15u;
        TitleRegs regs{};
        regs.eax = addr(ent);
        regs.ecx = addr(def);
        regs.edx = title_global<uint32_t>(0x008dda9c);
        regs.ebx = addr(state);
        regs.esi = field<uint32_t>(ent, 0x22c) ? title_global<uint32_t>(0x0098870c) : addr(vehicle);  // the last tag name
        regs.edi = 0x00988710;
        const uint32_t args[3] = {addr(ent), 0, title_global<uint32_t>(0x008dda9c)};
        call_at(A - 0x60, 0x00307207, own_address(&Vehicle_UpdatePhysics_entry), regs, 3, args);  // Vehicle_UpdatePhysics
    }
    set_origin();
}

uint8_t XB_CDECL GScr_ActorCanUseTurret_c(uint32_t entry_esp) {
    const uint32_t F = entry_esp;
    uint8_t* const actor = handle_actor(at<uint32_t>(F + 4));
    if (!actor) return 0;
    const uint32_t handle = at<uint32_t>(F + 8);
    uint8_t* const turret = handle_entity(handle);
    if (!turret) return 0;
    TitleRegs regs{};
    regs.eax = addr(turret);
    regs.ecx = addr(actor);
    regs.edx = handle >> 12;
    regs.edi = addr(turret);
    return call_at(F - 4, 0x003072bf, own_address(&Actor_CanUseTurret_entry), regs, 0, nullptr) != 0;  // Actor_CanUseTurret (ECX, EDI)
}

uint32_t XB_CDECL GScr_ActorCanShoot_c(uint32_t entry_esp) {
    // GScr_ActorCanShootWithOffset with a zero offset at F-0xc.
    const uint32_t F = entry_esp;
    const uint32_t handle = at<uint32_t>(F + 4), end = at<uint32_t>(F + 8);
    at<uint32_t>(F - 0xc) = 0;
    at<uint32_t>(F - 8) = 0;
    at<uint32_t>(F - 4) = 0;
    TitleRegs regs{};
    regs.eax = F - 0xc;
    regs.ecx = end;
    regs.edx = handle;
    const uint32_t args[3] = {handle, end, F - 0xc};
    return call_at(F - 0xc, 0x003072fb, own_address(&GScr_ActorCanShootWithOffset_entry), regs, 3, args);  // GScr_ActorCanShootWithOffset
}

void XB_CDECL GScr_ActorMelee_c(uint32_t entry_esp) {
    const uint32_t F = entry_esp;
    const uint32_t handle = at<uint32_t>(F + 4);
    uint8_t* const actor = handle_actor(handle);
    if (!actor) return;
    title_global<uint32_t>(0x0095f704) = field<uint32_t>(actor, 0);
    TitleRegs regs{};
    regs.eax = addr(actor);
    regs.ecx = field<uint32_t>(actor, 0);
    regs.edx = handle_generation(handle);
    call_at(F, 0x00307342, own_address(&Actor_MeleeAttack_entry), regs, 0, nullptr);  // Actor_MeleeAttack (EAX = actor)
    title_global<uint32_t>(0x0095f704) = 0;
}

void XB_CDECL GScr_MagicGrenade_c(uint32_t entry_esp) {
    // (handle, start, end, fuse seconds): the throw velocity (F-0xc) from the min-energy arc, then the low and
    // high arcs by time, then the fallback; fire_grenade of the actor's grenade weapon from start. The entity
    // is kept in the handle's slot (F+4); aim point F-0x18. Body ESP F-0x28.
    const uint32_t F = entry_esp;
    uint8_t* const ent = handle_entity(at<uint32_t>(F + 4));
    if (!ent) return;
    at<uint32_t>(F + 4) = addr(ent);
    uint8_t* const actor = field<uint8_t*>(ent, 0x240);
    if (!actor) return;
    const uint32_t start = at<uint32_t>(F + 8), end = at<uint32_t>(F + 0xc);
    const uint32_t aim = F - 0x18, velocity = F - 0xc;
    at<uint32_t>(F - 4) = 0;
    at<uint32_t>(F - 8) = 0;
    at<uint32_t>(F - 0xc) = 0;
    TitleRegs regs{};
    regs.eax = aim;
    regs.ebx = velocity;  // the zero offset
    regs.esi = start;
    regs.edi = end;
    {
        const uint32_t args[2] = {start, aim};
        call_at(F - 0x28, 0x003073cc, own_address(&G_GrenadeThrowPoints_entry), regs, 2, args);  // G_GrenadeThrowPoints (EDI, EBX, ESI; ret 8)
    }
    const uint32_t actor_arg[1] = {addr(actor)};
    regs.eax = start;
    regs.ebx = start;
    regs.ecx = velocity;
    regs.edx = aim;
    uint32_t ok = call_at(F - 0x28, 0x003073de, own_address(&Actor_GrenadeThrow_MinEnergy_entry), regs, 1, actor_arg);  // Actor_GrenadeThrow_MinEnergy
    if (!ok) {
        regs.eax = start;
        regs.ecx = aim;
        regs.edx = velocity;
        regs.esi = 0;
        ok = call_at(F - 0x28, 0x003073f4, own_address(&Actor_GrenadeThrow_ByTime_entry), regs, 1, actor_arg);  // Actor_GrenadeThrow_ByTime (low)
    }
    if (!ok) {
        regs.eax = start;
        regs.ecx = aim;
        regs.edx = velocity;
        regs.esi = 1;
        ok = call_at(F - 0x28, 0x0030740d, own_address(&Actor_GrenadeThrow_ByTime_entry), regs, 1, actor_arg);  // Actor_GrenadeThrow_ByTime (high)
    }
    if (!ok) {
        regs.eax = 0;
        regs.ecx = velocity;
        regs.edx = 0;
        regs.esi = start;
        regs.edi = aim;
        const uint32_t args[2] = {addr(actor), velocity};
        ok = call_at(F - 0x28, 0x00307422, own_address(&Actor_GrenadeThrow_Fallback_entry), regs, 2, args);  // Actor_GrenadeThrow_Fallback
        if (!ok) return;
    }
    const int32_t fuse = static_cast<int32_t>(mul(at<float>(F + 0x10), title_global<float>(0x0083eb9c)));  // 1000
    fire_grenade(field<uint32_t>(actor, 0x131c), at<uint8_t*>(F + 4), &at<const float>(velocity),
                 &at<const float>(start), fuse);
}

void XB_CDECL GScr_MagicGrenadeDefaultFuse_c(uint32_t entry_esp) {
    const uint32_t F = entry_esp;
    TitleRegs regs{};
    regs.eax = at<uint32_t>(F + 0xc);
    regs.ecx = at<uint32_t>(F + 8);
    regs.edx = at<uint32_t>(F + 4);
    const uint32_t args[4] = {at<uint32_t>(F + 4), at<uint32_t>(F + 8), at<uint32_t>(F + 0xc), 0x40a00000};  // 5 s
    call_at(F, 0x00307cb9, own_address(&GScr_MagicGrenade_entry), regs, 4, args);  // GScr_MagicGrenade
}

void XB_CDECL GScr_AnimScripted_c(uint32_t entry_esp) {
    // (handle, rate, origin, angles, anim, notify, root anim[, unused]): an actor aborts its anim script and
    // pushes state 10; the root anim (kept in the handle's slot F+4) is the given one when the notify string is
    // not empty, else 0; with no tree index in it (high word 0) the entity tree's own root (0x0024b860). Then
    // G_AnimScriptedStart with mode = (notify == 0x00984af8). Frame: tree F-4, body ESP F-0x14.
    const uint32_t F = entry_esp;
    uint8_t* const ent = handle_entity(at<uint32_t>(F + 4));
    if (!ent || !at<uint32_t>(F + 0x14)) return;
    const ScriptString* const notify = at<const ScriptString*>(F + 0x18);
    const uint32_t rep = *reinterpret_cast<const uint32_t*>(notify);
    at<uint16_t>(F + 6) = 0;
    at<uint16_t>(F + 4) = 0;
    uint32_t anim;
    if (rep && title_global<uint16_t>(rep + 2)) {
        anim = at<uint32_t>(F + 0x1c);
        at<uint32_t>(F + 4) = anim;
    } else {
        anim = at<uint32_t>(F + 4);
    }
    uint8_t* const actor = field<uint8_t*>(ent, 0x240);
    const uint32_t tree = G_GetEntAnimTree(ent);
    at<uint32_t>(F - 4) = tree;
    TitleRegs regs{};
    if (actor) {
        regs.eax = tree;
        regs.ebx = addr(ent);
        regs.esi = addr(actor);
        call_at(F - 0x14, 0x003074ec, own_address(&Actor_AbortAnimScript_entry), regs, 0, nullptr);  // Actor_AbortAnimScript (ESI)
        regs.edi = 0xa;
        call_at(F - 0x14, 0x003074f6, own_address(&Actor_PushState_entry), regs, 0, nullptr);  // Actor_PushState (ESI, EDI = 10)
    }
    if (at<uint16_t>(F + 6) == 0)
        anim = own_function<uint32_t XB_REGS1 XB_CDECL(uint32_t)>(&AnimTree_RootHandle)(tree);  // the tree's root anim
    const uint32_t mode = ScriptString_EqualsString(notify, &title_global<ScriptString>(0x00984af8)) ? 1 : 0;
    at<uint32_t>(F - 0x14) = 0x00984af8;  // EqualsString's arguments, still on the stack
    at<uint32_t>(F - 0x18) = addr(notify);
    regs.eax = at<uint32_t>(F + 0x14);
    regs.ecx = at<uint32_t>(F + 0x10);
    regs.edx = at<uint32_t>(F + 0xc);
    regs.ebx = addr(ent);
    regs.esi = 0;
    regs.edi = actor ? 0xa : 0;
    const uint32_t args[7] = {addr(ent), at<uint32_t>(F + 0xc), at<uint32_t>(F + 0x10), at<uint32_t>(F + 0x14), anim,
                              at<uint32_t>(F + 8), mode};
    call_at(F - 0x18, 0x0030753b, own_address(&G_AnimScriptedStart_entry), regs, 7, args);  // G_AnimScriptedStart
}

void XB_CDECL GScr_AnimScriptedFlag0_c(uint32_t entry_esp) {
    const uint32_t F = entry_esp;
    TitleRegs regs{};
    regs.eax = at<uint32_t>(F + 4);
    regs.ecx = at<uint32_t>(F + 0xc);
    regs.edx = at<uint32_t>(F + 8);
    uint32_t args[8];
    for (uint32_t i = 0; i < 7; ++i) args[i] = at<uint32_t>(F + 4 + i * 4);
    args[7] = 0;  // an eighth word GScr_AnimScripted never reads
    call_at(F, 0x00307cea, own_address(&GScr_AnimScripted_entry), regs, 8, args);  // GScr_AnimScripted
}

void XB_CDECL GScr_AnimScriptedNoNotify_c(uint32_t entry_esp) {
    // (handle, rate, origin, angles, anim): GScr_AnimScripted with an empty notify (F-4) and the anim's tree
    // (high word) as root, unless the anim's node is 0xffff.
    const uint32_t F = entry_esp;
    ScriptString* const empty = &at<ScriptString>(F - 4);
    ScriptString_FromRep(empty, nullptr);
    const uint32_t anim = at<uint32_t>(F + 0x14);
    if ((anim & 0xffff) != 0xffff) {
        TitleRegs regs{};
        regs.eax = at<uint32_t>(F + 4);
        regs.ecx = at<uint32_t>(F + 0xc);
        regs.edx = at<uint32_t>(F + 8);
        const uint32_t args[8] = {at<uint32_t>(F + 4), at<uint32_t>(F + 8), at<uint32_t>(F + 0xc), at<uint32_t>(F + 0x10),
                                  anim, F - 4, anim & 0xffff0000u, 1};
        call_at(F - 4, 0x00307d30, own_address(&GScr_AnimScripted_entry), regs, 8, args);  // GScr_AnimScripted
    }
    ScriptString_Release(empty);
}

uint8_t XB_CDECL GScr_ActorUseTurret_c(uint32_t entry_esp) {
    // 1 and the turret in +0x136c when Actor_CanUseTurret, else +0x136c = 0 (a stale turret handle leaves it).
    const uint32_t F = entry_esp;
    uint8_t* const actor = handle_actor(at<uint32_t>(F + 4));
    if (!actor) return 0;
    const uint32_t handle = at<uint32_t>(F + 8);
    uint8_t* const turret = handle_entity(handle);
    if (!turret) return 0;
    TitleRegs regs{};
    regs.eax = handle & 0xfff;
    regs.ecx = addr(actor);
    regs.edx = handle >> 12;
    regs.esi = addr(actor);
    regs.edi = addr(turret);
    const uint32_t ok = call_at(F - 8, 0x003076fd, own_address(&Actor_CanUseTurret_entry), regs, 0, nullptr);  // Actor_CanUseTurret
    if (!ok) {
        field<uint32_t>(actor, 0x136c) = 0;
        return 0;
    }
    field<uint32_t>(actor, 0x136c) = addr(turret);
    return 1;
}

void XB_CDECL GScr_ActorThrowGrenade_c(uint32_t entry_esp) {
    // A living actor's throw: method +0x1320 ("" -> 0x00984b94), Actor_CheckGrenadeThrow from the entity's
    // origin with the hand offset +0x1338 into the release point F-0x18 and velocity F-0xc (failing: the last
    // ones, +0x132c/+0x1344). A held grenade (+0x1310) is attached to the hand and launched from there, else
    // fire_grenade (fuse 5 s) and one grenade less (+0x1328). +0x1324 = 0 and the method cleared either way.
    // Body ESP F-0x24; the method copy (by value) at F-0x34.
    const uint32_t F = entry_esp;
    uint8_t* const actor = handle_actor(at<uint32_t>(F + 4));
    if (!actor) return;
    ScriptString* const method = reinterpret_cast<ScriptString*>(actor + 0x1320);
    if (field<int32_t>(field<uint8_t*>(actor, 0), 0x350) > 0) {
        const uint32_t rep = field<uint32_t>(actor, 0x1320);
        if (!rep || rep + 6 == 0 || !title_global<char>(rep + 6))
            ScriptString_Assign(method, &title_global<ScriptString>(0x00984b94));
        uint8_t* const ent = field<uint8_t*>(actor, 0);
        const uint32_t pos = F - 0x18, vel = F - 0xc;
        ScriptString_Copy(&at<ScriptString>(F - 0x34), method);
        TitleRegs regs{};
        regs.eax = addr(actor + 0x1338);
        regs.ecx = F - 0x34;
        regs.edx = pos;
        regs.esi = addr(actor);
        regs.edi = addr(ent + 0x140);
        const uint32_t args[7] = {addr(actor), addr(ent + 0x140), addr(actor + 0x1338), at<uint32_t>(F - 0x34), pos, vel, 1};
        if (!call_at(F - 0x24, 0x003077d3, own_address(&Actor_CheckGrenadeThrow_entry), regs, 7, args)) {  // Actor_CheckGrenadeThrow (ret 0x1c)
            copy3(&at<uint32_t>(pos), actor + 0x132c);
            copy3(&at<uint32_t>(vel), actor + 0x1344);
        }
        if (field<uint32_t>(actor, 0x1310)) {
            Actor_Grenade_AttachToTag(actor);
            const uint32_t x = at<uint32_t>(pos), y = at<uint32_t>(pos + 4), z = at<uint32_t>(pos + 8);
            const auto grenade = [actor] { return field<uint8_t*>(actor, 0x1310); };
            field<uint32_t>(grenade(), 0x20) = x;
            field<uint32_t>(grenade(), 0x24) = y;
            field<uint32_t>(grenade(), 0x28) = z;
            field<uint32_t>(grenade(), 0x140) = x;
            field<uint32_t>(grenade(), 0x144) = y;
            field<uint32_t>(grenade(), 0x148) = z;
            field<uint32_t>(grenade(), 0x2c) = at<uint32_t>(vel);
            field<uint32_t>(grenade(), 0x30) = at<uint32_t>(vel + 4);
            field<uint32_t>(grenade(), 0x34) = at<uint32_t>(vel + 8);
            field<uint32_t>(grenade(), 0x2b0) = field<uint32_t>(field<uint8_t*>(actor, 0), 0x224);
            field<uint32_t>(grenade(), 0x1a0) = field<uint32_t>(field<uint8_t*>(actor, 0), 0x224);
            SV_LinkEntity(grenade());
        } else {
            fire_grenade(field<uint32_t>(actor, 0x131c), field<uint8_t*>(actor, 0), &at<const float>(vel),
                         &at<const float>(pos), 5000);
            const int32_t count = field<int32_t>(actor, 0x1328);
            if (count > 0) field<int32_t>(actor, 0x1328) = count - 1;
        }
    }
    field<uint32_t>(actor, 0x1324) = 0;
    ScriptString_Clear(method);
}

void XB_CDECL GScr_CheckGrenadeThrow_c(uint32_t entry_esp) {
    // (handle, out velocity, hand offset, method): an actor with an enemy and grenades left, the global grenade
    // gate (0x0094d450 > 0, NaN passes) and no held grenade aims at the enemy's origin plus its velocity
    // (Actor_GrenadeTargetClear); the method ("min energy" 0x00984b90, "min time" 0x00984b94, "max time"
    // 0x00984b88, else a message) picks the arc, the fallback when it fails. +0x1324 = the result; a throw also
    // keeps the method (+0x1320), the offset (+0x1338) and gives out the velocity (+0x1344). Frame: own origin
    // F-0xc, velocity / aim F-0x18, target F-0x24, the method copy in the handle's slot F+4.
    const uint32_t F = entry_esp;
    uint8_t* const actor = handle_actor(at<uint32_t>(F + 4));
    if (!actor) return;
    if (!field<uint32_t>(field<uint8_t*>(actor, 4), 0x3c) || field<int32_t>(actor, 0x1328) <= 0) return;
    copy3(&at<uint32_t>(F - 0xc), field<uint8_t*>(actor, 0) + 0x140);
    ScriptString* const method = &at<ScriptString>(F + 4);
    ScriptString_Copy(method, at<const ScriptString*>(F + 0x10));
    uint8_t* const out = actor + 0x1344;
    at<uint32_t>(F - 0x28) = addr(out);
    const uint32_t hand = at<uint32_t>(F + 0xc);
    uint32_t result = 0;
    uint8_t* const enemy = field<uint8_t*>(field<uint8_t*>(actor, 4), 0x3c);
    if (!enemy || 0.0f >= title_global<float>(0x0094d450) || field<uint32_t>(actor, 0x1310)) {
        ScriptString_Release(method);
    } else {
        uint8_t* const enemy_ent = field<uint8_t*>(enemy, 0);
        const float ox = field<float>(enemy_ent, 0x140), oy = field<float>(enemy_ent, 0x144),
                    oz = field<float>(enemy_ent, 0x148);
        float* const velocity = &at<float>(F - 0x18);
        Sentient_GetVelocity(velocity, 0, enemy);
        float* const target = &at<float>(F - 0x24);
        const float tx = add(velocity[0], ox);
        target[1] = add(velocity[1], oy);
        const float tz = add(velocity[2], oz);
        target[0] = tx;
        target[2] = tz;
        if (!Actor_GrenadeTargetClear(field<uint32_t>(actor, 0x131c), target, actor)) {
            ScriptString_Release(method);
        } else {
            uint8_t* const start = actor + 0x132c;
            G_GrenadeThrowPoints(target, reinterpret_cast<const float*>(static_cast<uintptr_t>(hand)),
                                 reinterpret_cast<float*>(start), &at<float>(F - 0xc), velocity);
            const uint32_t actor_arg[1] = {addr(actor)};
            TitleRegs regs{};
            regs.eax = addr(start);
            regs.ebx = hand;
            regs.esi = addr(start);
            regs.edi = F - 0x24;
            if (ScriptString_EqualsString(method, &title_global<ScriptString>(0x00984b90))) {
                regs.ecx = addr(out);
                regs.edx = F - 0x18;
                result = call_at(F - 0x38, 0x00307b0f, own_address(&Actor_GrenadeThrow_MinEnergy_entry), regs, 1, actor_arg);  // Actor_GrenadeThrow_MinEnergy
            } else {
                int32_t high = -1;
                if (ScriptString_EqualsString(method, &title_global<ScriptString>(0x00984b94)))
                    high = 0;
                else if (ScriptString_EqualsString(method, &title_global<ScriptString>(0x00984b88)))
                    high = 1;
                if (high >= 0) {
                    regs.ecx = F - 0x18;
                    regs.edx = addr(out);
                    regs.esi = static_cast<uint32_t>(high);
                    result = call_at(F - 0x38, 0x00307bb9, own_address(&Actor_GrenadeThrow_ByTime_entry), regs, 1, actor_arg);  // Actor_GrenadeThrow_ByTime
                } else {
                    // "checkGrenadeThrow: method must be 'min energy', 'min time', or 'max time' - value %s"
                    own_function<const char* XB_CDECL(const char*, const char*)>(&va_entry)(title_text(0x0083d068),
                                                                                          script_text(method));
                }
            }
            if (!result) {
                regs.eax = 0;
                regs.ecx = addr(out);
                regs.esi = addr(start);
                regs.edi = F - 0x18;
                const uint32_t args[2] = {addr(actor), addr(out)};
                result = call_at(F - 0x38, 0x00307b2a, own_address(&Actor_GrenadeThrow_Fallback_entry), regs, 2, args);  // Actor_GrenadeThrow_Fallback
            }
            ScriptString_Release(method);
        }
    }
    field<uint32_t>(actor, 0x1324) = result;
    if (!result) return;
    ScriptString_Assign(reinterpret_cast<ScriptString*>(actor + 0x1320), at<const ScriptString*>(F + 0x10));
    const uint32_t* const offset = at<const uint32_t*>(F + 0xc);
    field<uint32_t>(actor, 0x1338) = offset[0];
    field<uint32_t>(actor, 0x133c) = offset[1];
    field<uint32_t>(actor, 0x1340) = offset[2];
    copy3(at<uint32_t*>(F + 8), out);
}

void XB_CDECL GScr_SetGoalEntity_c(uint32_t entry_esp) {
    const uint32_t F = entry_esp;
    uint8_t* const ent = handle_entity(at<uint32_t>(F + 4));
    if (!ent) return;
    uint8_t* const sentient = field<uint8_t*>(ent, 0x244);
    if (!sentient) return;
    const uint32_t handle = at<uint32_t>(F + 8);
    uint8_t* const goal = handle_entity(handle);
    if (!goal) return;
    title_global<uint32_t>(0x0095f704) = addr(ent);
    TitleRegs regs{};
    regs.eax = addr(goal);
    regs.ecx = handle >> 12;
    regs.edx = addr(ent);
    regs.esi = addr(sentient);
    const uint32_t args[2] = {addr(sentient), addr(goal)};
    call_at(F - 4, 0x00307c80, own_address(&Sentient_SetGoalEntity_entry), regs, 2, args);  // Sentient_SetGoalEntity (ret 8)
    field<uint32_t>(sentient, 0x20) = title_global<uint32_t>(0x0083eb34);  // -1
    title_global<uint32_t>(0x0095f704) = 0;
}

uint32_t XB_CDECL GScr_GetEntArray(const ScriptString* name, uint32_t key, uint32_t* out, uint32_t unused) {
    (void)unused;  // the original passes it on as an unread second argument of G_FindEntsBy*
    const uint32_t hash = Com_HashKeyNoCase(0, script_text(name));
    switch (key) {
        case 0x0d786b4c: return G_FindEntsByTargetname(out, hash);  // inlined in the original
        case 0xee2890d4: return G_FindEntsByTarget(out, hash);
        case 0xa106e9bd: return G_FindEntsByClassname(out, hash);
        case 0x2d36ed4e: return G_FindEntsByGroupname(out, hash);
        case 0x93c97595: return G_FindEntsByScriptNoteworthy(out, hash);
        default: break;
    }
    // Any other key: every used entity (the complement of the free list 0x0097a8c0) whose spawn fields (+0x428)
    // match through the import table's lookup (0x0095f49c -> +0x80c); no out = the first one (its vehicle's
    // +0x200 = 1).
    uint32_t bits[42];
    EntBits_Invert(bits, &title_global<uint32_t>(0x0097a8c0));
    uint32_t iterator[4];
    EntBits_IterBegin(bits, iterator);
    uint32_t count = 0;
    for (;;) {
        const int32_t index = static_cast<int32_t>(iterator[3]);
        if (index == -1 && static_cast<int32_t>(iterator[2]) == -1) return count;  // index -1 alone goes on
        uint8_t* const ent = title_global<uint8_t*>(0x0097a968 + static_cast<uint32_t>(index) * 8);
        if (const uint32_t fields = field<uint32_t>(ent, 0x428)) {
            using Match = uint8_t XB_CDECL(uint32_t, uint32_t, const char*);
            Match* const match = title_function<Match>(field<uint32_t>(title_global<uint8_t*>(0x0095f49c), 0x80c));
            if (match(fields, key, script_text(name))) {
                if (!out) {
                    if (uint8_t* const vehicle = field<uint8_t*>(ent, 0x248)) field<uint32_t>(vehicle, 0x200) = 1;
                    return field<uint32_t>(ent, 0x224);
                }
                out[count++] = field<uint32_t>(ent, 0x224);
            }
        }
        EntBits_IterNext(iterator);
    }
}

float XB_CDECL GScr_ActorCanSee_c(uint32_t entry_esp) {
    // (handle, other, max age seconds): the visibility of the other entity - a sentient through
    // Actor_UpdateVisibilityIfStale (age in ms), else Actor_UpdateVisibility with the actor's +0x828/+0x82c;
    // 0 for stale handles. The result goes through the handle's slot (F+4) onto the x87.
    const uint32_t F = entry_esp;
    uint8_t* const actor = handle_actor(at<uint32_t>(F + 4));
    if (!actor) return title_global<float>(0x0083eafc);  // 0
    const uint32_t handle = at<uint32_t>(F + 8);
    uint8_t* const other = handle_entity(handle);
    if (!other) return title_global<float>(0x0083eafc);
    TitleRegs regs{};
    regs.ebx = addr(other);
    regs.esi = addr(actor);
    uint32_t bits;
    if (uint8_t* const sentient = field<uint8_t*>(other, 0x244)) {
        const int32_t ms = static_cast<int32_t>(mul(at<float>(F + 0xc), title_global<float>(0x0083eb9c)));  // 1000
        regs.eax = addr(sentient);
        regs.ecx = static_cast<uint32_t>(ms);
        regs.edx = handle >> 12;
        const uint32_t args[1] = {static_cast<uint32_t>(ms)};
        bits = call_at_xmm0(F - 8, 0x00307f65, own_address(&Actor_UpdateVisibilityIfStale_entry), regs, 1, args);  // Actor_UpdateVisibilityIfStale
    } else {
        regs.eax = addr(actor);
        regs.ecx = handle >> 12;
        regs.edx = field<uint32_t>(actor, 0x82c);
        const uint32_t args[2] = {field<uint32_t>(actor, 0x828), field<uint32_t>(actor, 0x82c)};
        bits = call_at_xmm0(F - 8, 0x00307f87, own_address(&Actor_UpdateVisibility_entry), regs, 2, args);  // Actor_UpdateVisibility
    }
    at<uint32_t>(F + 4) = bits;
    return at<float>(F + 4);
}

void XB_CDECL GScr_LoadZoneSync(uint8_t* zone) {
    // Loads a zone in place: the frame counter 0x008d9d38 + 1, the zone's progress (+0x34) 0, then Stream_Update
    // until its request (+0x28 in the stream system's list +0x38) reaches state 0 (+0xdc); passes while the
    // system works on that zone (+0x20) with nothing pending (+0x10) are not counted, else at most 10001 more.
    if (!zone) return;
    title_global<uint32_t>(0x008d9d38) = title_global<uint32_t>(0x008d9d38) + 1;
    uint8_t* system = title_global<uint8_t*>(0x0095f638);
    field<uint32_t>(zone, 0x34) = 0;
    int32_t index = field<int32_t>(zone, 0x28);
    int32_t passes = 0;
    for (;;) {
        if (index != -1) {
            uint8_t* const request = field<uint8_t*>(system, 0x38 + index * 4);
            if (request && !field<uint32_t>(request, 0xdc)) return;
        }
        Stream_Update(system);
        system = title_global<uint8_t*>(0x0095f638);
        index = field<int32_t>(zone, 0x28);
        if (field<int32_t>(system, 0x20) == index && !field<uint32_t>(system, 0x10)) continue;
        if (passes++ > 10000) return;
    }
}

namespace {

// A float widened to double on the x87 (FLD DWORD / FSTP QWORD), as the title passes %g arguments.
double x87_widen_at(const float* value) {
    double result;
    __asm__ volatile("flds %1\n\tfstpl %0" : "=m"(result) : "m"(*value));
    return result;
}

// call_at for callees with an ST0 result.
float call_at_st0(uint32_t top, uint32_t ret, uint32_t function, const TitleRegs& regs, uint32_t count,
                  const uint32_t* args) {
    const float result = gscr_call_at_st0(top, function, &regs, count, args);
    at<uint32_t>(top - count * 4 - 4) = ret;
    return result;
}

uint32_t float_bits(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    return bits;
}

// GScr_ActorCanSeeEnemy*: Actor_UpdateVisibilityIfStale on the enemy (sentient +0x3c) with `age` ms, the result
// through the handle's slot (F+4); 0 without an actor or enemy.
float enemy_visibility(uint32_t F, bool default_age, uint32_t ret) {
    uint8_t* const actor = handle_actor(at<uint32_t>(F + 4));
    if (!actor) return title_global<float>(0x0083eafc);  // 0
    const uint32_t enemy = field<uint32_t>(field<uint8_t*>(actor, 4), 0x3c);
    if (!enemy) return title_global<float>(0x0083eafc);
    const uint32_t age = default_age ? field<uint32_t>(actor, 0x830)
                                     : static_cast<uint32_t>(static_cast<int32_t>(
                                           mul(at<float>(F + 8), title_global<float>(0x0083eb9c))));  // 1000
    TitleRegs regs{};
    regs.eax = enemy;
    regs.ecx = age;
    regs.esi = addr(actor);
    const uint32_t args[1] = {age};
    at<uint32_t>(F + 4) = call_at_xmm0(F - 4, ret, own_address(&Actor_UpdateVisibilityIfStale_entry), regs, 1, args);  // Actor_UpdateVisibilityIfStale
    return at<float>(F + 4);
}

// GScr_ActorShoot / GScr_ActorShootAt: Actor_ShootWeapon (actor, scale, point, 1) with 0x0095f704 = the actor's
// entity meanwhile.
void actor_shoot(uint32_t F, uint32_t scale, uint32_t point, uint32_t edx, uint32_t ret) {
    uint8_t* const actor = handle_actor(at<uint32_t>(F + 4));
    if (!actor) return;
    title_global<uint32_t>(0x0095f704) = field<uint32_t>(actor, 0);
    TitleRegs regs{};
    regs.eax = addr(actor);
    regs.ecx = field<uint32_t>(actor, 0);
    regs.edx = edx;
    const uint32_t args[4] = {addr(actor), scale, point, 1};
    call_at(F, ret, own_address(&Actor_ShootWeapon_entry), regs, 4, args);  // Actor_ShootWeapon
    title_global<uint32_t>(0x0095f704) = 0;
}

// GScr_ForceSpawnNamed / GScr_DoSpawnNamed: an actor from a spawner (type 12) with the given targetname (the
// handle's slot F+4 holds it; force: an empty name becomes "0"), at most once per level time (+0x2f0); its
// handle, else 0. Other entities: the message with targetname ("<unnamed>"), classname and origin.
uint32_t spawn_named(uint32_t F, bool force, uint32_t ret) {
    uint8_t* const ent = handle_entity(at<uint32_t>(F + 4));
    if (!ent) return 0;
    ScriptString* const name = &at<ScriptString>(F + 4);
    ScriptString_FromRep(name, nullptr);
    const ScriptString* const given = at<const ScriptString*>(F + 8);
    if (*reinterpret_cast<const uint32_t*>(given))
        ScriptString_Assign(name, given);
    else if (force)
        ScrCall_sprintf(name, 0);
    if (ent[0] != 0xc) {
        const uint32_t rep = field<uint32_t>(ent, 0x264);
        const char* targetname = title_text(0x0083223c);  // "<unnamed>"
        if (rep && rep + 6 != 0) targetname = script_text(reinterpret_cast<const ScriptString*>(ent + 0x264));
        const char* const classname = script_text(reinterpret_cast<const ScriptString*>(ent + 0x25c));
        // "dospawn can only be called on actor spawners\nattempted to call dospawn on entity ..."
        own_function<const char* XB_CDECL(const char*, const char*, const char*, double, double, double)>(&va_entry)(title_text(0x008321b8), targetname, classname, x87_widen_at(&field<float>(ent, 0x140)),
                        x87_widen_at(&field<float>(ent, 0x144)), x87_widen_at(&field<float>(ent, 0x148)));  // va
        ScriptString_Release(name);
        return 0;
    }
    if (field<int32_t>(ent, 0x2f0) >= title_global<int32_t>(0x0098540c)) {  // level_time
        ScriptString_Release(name);
        return 0;
    }
    const uint8_t* const owner = at<const uint8_t*>(F + 0xc);
    const int32_t context = owner ? *reinterpret_cast<const int32_t*>(owner + 0x28) : -1;
    TitleRegs regs{};
    regs.eax = static_cast<uint32_t>(context);
    regs.ecx = F + 4;
    regs.esi = addr(ent);
    const uint32_t args[4] = {addr(ent), F + 4, force ? 1u : 0u, static_cast<uint32_t>(context)};
    const uint32_t actor = call_at(F - 4, ret, own_address(&Actor_SpawnFromSpawner_entry), regs, 4, args);  // Actor_SpawnFromSpawner
    if (!actor) {
        ScriptString_Release(name);
        return 0;
    }
    field<int32_t>(ent, 0x2f0) = title_global<int32_t>(0x0098540c);
    const uint32_t handle = title_global<uint32_t>(actor + 0x224);
    ScriptString_Release(name);
    return handle;
}

// GScr_ForceSpawn / GScr_DoSpawn: the *Named version with an empty name (F-4).
uint32_t spawn_unnamed(uint32_t F, uint32_t function, uint32_t ret) {
    ScriptString* const empty = &at<ScriptString>(F - 4);
    ScriptString_FromRep(empty, nullptr);
    TitleRegs regs{};
    regs.eax = at<uint32_t>(F + 8);
    regs.ecx = F - 4;
    regs.edx = at<uint32_t>(F + 4);
    const uint32_t args[3] = {at<uint32_t>(F + 4), F - 4, at<uint32_t>(F + 8)};
    const uint32_t handle = call_at(F - 8, ret, function, regs, 3, args);
    ScriptString_Release(empty);
    return handle;
}

}  // namespace

float XB_CDECL GScr_ActorCanSeeEnemyDefaultAge_c(uint32_t entry_esp) {
    return enemy_visibility(entry_esp, true, 0x00308064);
}
float XB_CDECL GScr_ActorCanSeeEnemy_c(uint32_t entry_esp) { return enemy_visibility(entry_esp, false, 0x003080d0); }

float XB_CDECL GScr_ActorCanSeeDefaultAge_c(uint32_t entry_esp) {
    // GScr_ActorCanSee with the actor's default age (+0x830 ms, CVTSI2SS times 0.001).
    const uint32_t F = entry_esp;
    const uint32_t handle = at<uint32_t>(F + 4);
    uint8_t* const actor = handle_actor(handle);
    if (!actor) return title_global<float>(0x0083eafc);  // 0
    const float age = mul(static_cast<float>(field<int32_t>(actor, 0x830)), title_global<float>(0x0083eb4c));
    TitleRegs regs{};
    regs.eax = at<uint32_t>(F + 8);
    regs.ecx = handle;
    regs.edx = handle >> 12;
    const uint32_t args[3] = {handle, at<uint32_t>(F + 8), float_bits(age)};
    return call_at_st0(F, 0x00308141, own_address(&GScr_ActorCanSee_entry), regs, 3, args);  // GScr_ActorCanSee
}

void XB_CDECL GScr_Vehicle_FireWeapon_c(uint32_t entry_esp) {
    // The vehicle's turret fires its weapon (entity byte +3; bullets or projectiles) from each flash tag
    // (0x009886e8: one, or two alternating by +0x188 when the def's +0x2c is set; a missing tag gives a message
    // and the last matrix): with a driver (+0x1a0), one tag and a target (+0x1d0, no +0x1ac) the direction
    // turns towards it by at most 10 degrees of pitch and yaw, else the tag's forward; +0x1ac moves the start
    // back along it by +0x1a8. Then the flash event (0xa3, 0xae/0xaf), a projectile weapon's push against the
    // barrel (tag_barrel +0x2c4), the fire time (+0x180 = weapon def +0x5dc) and +0x188 flipped. EBP frame:
    // A = its aligned ESP, body S = A - 0x120 (barrel matrix +0xe0, tag matrix +0x90, fire record +0x48).
    const uint32_t F = entry_esp;
    uint8_t* const ent = handle_entity(at<uint32_t>(F + 4));
    if (!ent) return;
    uint8_t* const vehicle = field<uint8_t*>(ent, 0x248);
    if (!vehicle) return;
    const uint32_t A = (F - 4) & ~15u, S = A - 0x120;
    at<uint32_t>(S + 0x8c) = addr(vehicle);
    uint8_t* const def = title_global<uint8_t*>(0x00950a28 + field<int16_t>(vehicle, 0x178) * 4);  // g_vehicleDefs
    const auto targetname = [ent] { return script_text(reinterpret_cast<const ScriptString*>(ent + 0x264)); };
    using Va2 = const char* XB_CDECL(const char*, const char*);
    if (!(0.0f < static_cast<float>(field<int32_t>(ent, 0x350))))
        title_va(0x00831938);  // "Vehicle must have health to control the turret"
    const uint8_t weapon = ent[3];
    if (!weapon) {
        own_function<Va2>(&va_entry)(title_text(0x00831918), targetname());  // "No weapon specified for [%s]"
        return;
    }
    uint8_t* const wdef = title_global<uint8_t**>(0x0095f4d8)[weapon];  // bg_weaponDefs
    at<uint32_t>(S + 0x84) = addr(wdef);
    const int32_t type = field<int32_t>(wdef, 0x94);
    if (type != 0 && type != 2) title_va(0x008318e0);  // "Vehicles only support bullet and projectile weapons"
    const int32_t barrel = field<int32_t>(vehicle, 0x2c4);
    if (barrel < 0) {
        own_function<Va2>(&va_entry)(title_text(0x008318c8), targetname());  // "No tag_barrel for [%s]"
        return;
    }
    G_DObjGetWorldTagMatrix(ent, static_cast<uint32_t>(barrel), &at<float>(S + 0xe0));
    uint32_t mode, first, event;
    uint32_t esi = addr(def);
    if (field<uint32_t>(def, 0x2c)) {
        esi = field<uint32_t>(vehicle, 0x188);
        first = esi ? 2 : 0;
        mode = 2;
        event = 0xae + (esi ? 1 : 0);
    } else {
        mode = 1;
        first = 0;
        event = 0xa3;
    }
    at<uint32_t>(S + 0x14) = mode;
    at<uint32_t>(S + 0x1c) = event;
    at<uint32_t>(S + 0x10) = 0x009886e8 + first * 4;
    at<uint32_t>(S + 0x18) = mode;
    float* const tag = &at<float>(S + 0x90);  // forward +0, right +0x10, up +0x20, origin +0x30
    float* const dir = &at<float>(S + 0x78);
    do {
        const uint32_t name = title_global<uint32_t>(at<uint32_t>(S + 0x10));
        const DObj* const obj = field<const DObj*>(ent, 0x22c);
        int32_t bone = -1;
        if (obj) {
            esi = name;
            bone = DObj_GetTagIndex(obj, name);
        }
        if (bone >= 0) {
            G_DObjGetWorldTagMatrix(ent, static_cast<uint32_t>(bone), tag);
        } else {
            const char* const text = targetname();
            own_function<const char* XB_CDECL(const char*, uint32_t, const char*)>(&va_entry)(
                title_text(0x008318b8), name, text);  // "No %s for [%s]"
        }
        bool aimed = false;
        if (field<uint32_t>(ent, 0x1a0) && at<uint32_t>(S + 0x14) == 1 && !field<uint32_t>(vehicle, 0x1ac)) {
            const float* const target = &field<float>(vehicle, 0x1d0);
            if (target[0] != 0.0f || target[1] != 0.0f || target[2] != 0.0f) {  // ucomiss: NaN aims
                dir[0] = target[0] - tag[12];
                dir[1] = target[1] - tag[13];
                dir[2] = target[2] - tag[14];
                VectorNormalize(dir);
                vectoangles_ToOut(tag, &at<float>(S + 0x30));
                vectoangles_ToOut(dir, &at<float>(S + 0xd0));
                AnglesNormalize180(&at<float>(S + 0xd0), &at<float>(S + 0x30), &at<float>(S + 0x20));
                for (uint32_t i = 0; i < 2; ++i) {
                    const float v = at<float>(S + 0x20 + i * 4);
                    if (title_global<float>(0x0083ed10) > v)  // -10
                        at<uint32_t>(S + 0x20 + i * 4) = title_global<uint32_t>(0x0083ed10);
                    else if (v > title_global<float>(0x0083eb98))  // 10
                        at<uint32_t>(S + 0x20 + i * 4) = title_global<uint32_t>(0x0083eb98);
                }
                at<uint32_t>(S + 0x28) = 0;
                AnglesNormalize180(&at<float>(S + 0x20), &at<float>(S + 0x30), &at<float>(S + 0x30));
                AnglesToForward(&at<float>(S + 0x30), dir);
                esi = S + 0x78;
                aimed = true;
            }
        }
        if (!aimed) copy3(dir, tag);
        // The fire record: direction +0, right +0xc, up +0x18, start +0x24 (+0x30 the direction, +0x3c the def).
        uint8_t* const record = &at<uint8_t>(S + 0x48);
        copy3(record + 0xc, tag + 4);
        copy3(record + 0x18, tag + 8);
        copy3(record, dir);
        if (field<uint32_t>(vehicle, 0x1ac)) {
            float* const start = reinterpret_cast<float*>(record + 0x24);
            start[0] = add(mul(0.0f - field<float>(vehicle, 0x1a8), dir[0]), tag[12]);
            start[1] = add(mul(0.0f - field<float>(vehicle, 0x1a8), dir[1]), tag[13]);
            start[2] = add(mul(0.0f - field<float>(vehicle, 0x1a8), dir[2]), tag[14]);
        } else {
            copy3(record + 0x24, tag + 12);
        }
        uint8_t* const fire_def = at<uint8_t*>(S + 0x84);
        TitleRegs regs{};
        regs.ebx = addr(ent);
        regs.edi = addr(vehicle);
        regs.ecx = field<uint32_t>(fire_def, 0x94);
        if (!field<uint32_t>(fire_def, 0x94)) {
            regs.eax = S + 0x48;
            regs.esi = esi;
            const uint32_t args[5] = {addr(ent), 0x3f800000, field<uint32_t>(fire_def, 0x5b8), addr(ent), 0};
            call_at(S, 0x0030862a, own_address(&Weapon_FireBullet_entry), regs, 5, args);  // Weapon_FireBullet
        } else {
            esi = S + 0x48;
            regs.eax = addr(ent);
            regs.esi = esi;
            if (ent[0x297] != 2) {
                const uint32_t args[3] = {0, 0x41200000, 1};
                call_at(S, 0x0030864c, own_address(&Weapon_FireProjectile_entry), regs, 3, args);  // Weapon_FireProjectile (10 degrees)
            } else {
                const uint32_t args[2] = {0, 0x41200000};
                call_at(S, 0x0030865d, own_address(&Vehicle_FireSpreadProjectile_entry), regs, 2, args);  // Vehicle_FireSpreadProjectile
            }
        }
        at<uint32_t>(S + 0x10) += 4;
    } while (--at<uint32_t>(S + 0x18));
    G_AddEvent(ent, at<uint32_t>(S + 0x1c), 0);
    if (field<int32_t>(at<uint8_t*>(S + 0x84), 0x94) == 2) {
        // A projectile pushes the vehicle back along the barrel (S+0x30 = -forward).
        at<float>(S + 0x30) = 0.0f - at<float>(S + 0xe0);
        at<float>(S + 0x34) = 0.0f - at<float>(S + 0xe4);
        at<float>(S + 0x38) = 0.0f - at<float>(S + 0xe8);
        Vehicle_ApplyPush(ent, &at<float>(S + 0x30), 1.0f, 0.0f, 0);
    }
    field<uint32_t>(vehicle, 0x180) = field<uint32_t>(at<uint8_t*>(S + 0x84), 0x5dc);
    field<uint32_t>(vehicle, 0x188) = field<uint32_t>(vehicle, 0x188) == 0 ? 1 : 0;
}

void XB_CDECL GScr_MagicBullet_c(uint32_t entry_esp) {
    // (weapon name, start, end): a bullet of a bullet weapon (type +0x94 0) from start towards end (normalized
    // unless the length is 0) fired by the world entity (Weapon_FireBullet), and a temp entity (event 0xa3, +3 =
    // the weapon) at start. An unknown name gives the message - with the name's rep pointer as its %s (title
    // quirk). Body S = F - 0x58: fire record +0x18 (direction +0, start +0x24, the def +0x3c).
    const uint32_t F = entry_esp, S = F - 0x58;
    const ScriptString* const name = at<const ScriptString*>(F + 4);
    const uint32_t rep = *reinterpret_cast<const uint32_t*>(name);
    uint32_t index = 0;
    if (rep && rep + 6 != 0 && title_global<char>(rep + 6))
        index = BG_FindWeaponIndexForHash(Com_HashKeyNoCase(rep, title_text(rep + 6))) & 0xff;
    if (!index) {
        own_function<const char* XB_CDECL(const char*, uint32_t)>(&va_entry)(
            title_text(0x00832454), *reinterpret_cast<const uint32_t*>(name));  // va
        return;
    }
    const float* const start = at<const float*>(F + 8);
    const float* const end = at<const float*>(F + 0xc);
    uint8_t* const wdef = title_global<uint8_t**>(0x0095f4d8)[index];  // bg_weaponDefs
    at<float>(S + 0x3c) = start[0];
    const float dz = end[2] - start[2];
    const float dy = end[1] - start[1];
    at<float>(S + 0x40) = start[1];
    at<float>(S + 0x44) = start[2];
    const float dx = end[0] - start[0];
    const float length = title_ci_sqrt_float(add(add(mul(dz, dz), mul(dy, dy)), mul(dx, dx)));
    at<uint32_t>(S + 0x54) = addr(wdef);
    at<float>(F + 4) = length;  // the name's slot keeps the length
    float x = dx, y = dy, z = dz;
    if (length != title_global<float>(0x0083eafc)) {  // fucomip: NaN normalizes too
        const float inv = title_global<float>(0x0083eb20) / length;
        x = mul(inv, dx);
        y = mul(inv, dy);
        z = mul(inv, dz);
    }
    at<float>(S + 0xc) = dx;
    at<float>(S + 0x10) = dy;
    at<float>(S + 0x14) = dz;
    at<float>(S + 0x18) = x;
    at<float>(S + 0x1c) = y;
    at<float>(S + 0x20) = z;
    if (field<uint32_t>(wdef, 0x94)) return;
    TitleRegs regs{};
    regs.eax = S + 0x18;
    regs.ecx = field<uint32_t>(title_global<uint8_t*>(0x0095f534), 8);
    regs.edx = field<uint32_t>(wdef, 0x5b8);
    regs.ebx = index;
    regs.esi = addr(start);
    regs.edi = addr(wdef);
    const uint32_t args[5] = {regs.ecx, 0, regs.edx, 0, 0};
    call_at(S, 0x0030888e, own_address(&Weapon_FireBullet_entry), regs, 5, args);  // Weapon_FireBullet (world entity)
    G_TempEntity(reinterpret_cast<const uint32_t*>(start), 0xa3)[3] = static_cast<uint8_t>(index);
}

void XB_CDECL GScr_ActorShoot_c(uint32_t entry_esp) {
    const uint32_t scale = at<uint32_t>(entry_esp + 8);
    actor_shoot(entry_esp, scale, 0, scale, 0x00308a7c);
}
void XB_CDECL GScr_ActorShootAt_c(uint32_t entry_esp) {
    const uint32_t point = at<uint32_t>(entry_esp + 0xc);  // the third word
    actor_shoot(entry_esp, 0x3f800000, point, point, 0x00308adf);  // scale 1.0
}

void XB_CDECL GScr_SetAIFlags(uint32_t handle, uint32_t flag28, uint32_t flag27, uint32_t clear_enemy) {
    uint8_t* const ent = handle_entity(handle);
    if (!ent) return;
    uint32_t flags = field<uint32_t>(ent, 0x2a4);
    field<uint32_t>(ent, 0x2a4) = (flag28 & 0xff) ? flags | 0x10000000 : flags & ~0x10000000u;
    flags = field<uint32_t>(ent, 0x2a4);
    field<uint32_t>(ent, 0x2a4) = (flag27 & 0xff) ? flags | 0x08000000 : flags & ~0x08000000u;
    if (!(clear_enemy & 0xff)) return;
    // The sentient forgets its enemy (the enemy's +0x44 count - 1); an actor (team +4 == 2) re-thinks 500 ms on.
    uint8_t* const sentient = field<uint8_t*>(ent, 0x244);
    uint8_t* const enemy = field<uint8_t*>(sentient, 0x3c);
    if (!enemy) return;
    --field<int16_t>(enemy, 0x44);
    const uint32_t team = field<uint32_t>(sentient, 4);
    field<uint32_t>(sentient, 0x40) = 0;
    field<uint32_t>(sentient, 0x3c) = 0;
    if (team != 2) return;
    uint8_t* const actor = field<uint8_t*>(field<uint8_t*>(sentient, 0), 0x240);
    if (!actor) return;
    const int32_t later = title_global<int32_t>(0x0098540c) + 500;  // level_time
    if (field<int32_t>(actor, 0x34) < later) field<int32_t>(actor, 0x34) = later;
}

uint32_t XB_CDECL GScr_TurretCall272_c(uint32_t entry_esp, uint32_t* regs) {
    // (turret handle, user handle): Turret_FireForUserOrDriver (EAX turret, ECX user or 0; tail jump) for a
    // turret (+0x24c), else the message (va tail jump with the classname written into the argument slots).
    const uint32_t F = entry_esp;
    const uint32_t handle = at<uint32_t>(F + 4);
    regs[0] = handle & 0xfff;
    regs[1] = handle >> 12;
    uint8_t* const turret = handle_entity(handle);
    if (!turret) return 0;
    regs[0] = addr(turret);
    regs[1] = addr(handle_entity(at<uint32_t>(F + 8)));
    if (field<uint32_t>(turret, 0x24c)) return own_address(&Turret_FireForUserOrDriver_entry);  // Turret_FireForUserOrDriver
    const uint32_t rep = field<uint32_t>(turret, 0x25c);
    at<uint32_t>(F + 8) = rep ? rep + 6 : 0;
    at<uint32_t>(F + 4) = 0x00832094;  // "entity type '%s' is not a turret"
    regs[0] = at<uint32_t>(F + 8);
    return own_address(&va_entry);  // va
}

uint32_t XB_CDECL GScr_ActorFindBetterCover_c(uint32_t entry_esp) {
    // Actor_Cover_FindBetterCover for an actor in state 1 (+8) or with +0xa4 == 1; 0x0095f704 = its entity.
    const uint32_t F = entry_esp;
    uint8_t* const actor = handle_actor(at<uint32_t>(F + 4));
    if (!actor) return 0;
    const uint32_t state = field<uint32_t>(actor, 8);
    if (state != 1 && field<uint32_t>(actor, 0xa4) != 1) return 0;
    title_global<uint32_t>(0x0095f704) = field<uint32_t>(actor, 0);
    TitleRegs regs{};
    regs.eax = addr(actor);
    regs.ecx = 1;
    regs.edx = state;
    const uint32_t result = call_at(F, 0x00308ca9, own_address(&Actor_Cover_FindBetterCover_entry), regs, 0, nullptr);  // Actor_Cover_FindBetterCover
    title_global<uint32_t>(0x0095f704) = 0;
    return result;
}

uint32_t XB_CDECL GScr_ForceSpawnNamed_c(uint32_t entry_esp) { return spawn_named(entry_esp, true, 0x00308dc4); }
uint32_t XB_CDECL GScr_DoSpawnNamed_c(uint32_t entry_esp) { return spawn_named(entry_esp, false, 0x00308eeb); }

void XB_CDECL GScr_ActorApplyAnimMove_c(uint32_t entry_esp) {
    // The actor moves by its anim this frame: move mode +0x278 = +0x280 (4 when 0), +0x284 = 1,
    // Actor_MoveByAnimMode and Actor_ApplyMove (frame time 0x008dda9c), then the model's tree weights blended
    // (frame time 0x008ddaa0).
    const uint32_t F = entry_esp;
    uint8_t* const actor = handle_actor(at<uint32_t>(F + 4));
    if (!actor) return;
    const uint32_t mode = field<uint32_t>(actor, 0x280);
    field<uint32_t>(actor, 0x278) = mode ? mode : 4;
    uint8_t* const ent = field<uint8_t*>(actor, 0);
    actor[0x284] = 1;
    TitleRegs regs{};
    regs.eax = title_global<uint32_t>(0x008dda9c);
    regs.esi = addr(actor);
    regs.edi = addr(ent);
    const uint32_t move_args[2] = {addr(actor), title_global<uint32_t>(0x008dda9c)};
    call_at(F - 8, 0x00308f88, own_address(&Actor_MoveByAnimMode_entry), regs, 2, move_args);  // Actor_MoveByAnimMode (ret 8)
    const uint32_t apply_args[1] = {addr(actor)};
    call_at(F - 8, 0x00308f8e, own_address(&Actor_ApplyMove_entry), regs, 1, apply_args);  // Actor_ApplyMove (ret 4)
    if (const uint8_t* const obj = field<uint8_t*>(ent, 0x22c))
        AnimTree_BlendWeightsForModel(obj, title_global<float>(0x008ddaa0));
}

uint32_t XB_CDECL GScr_ForceSpawn_c(uint32_t entry_esp) { return spawn_unnamed(entry_esp, own_address(&GScr_ForceSpawnNamed_entry), 0x00308fd1); }
uint32_t XB_CDECL GScr_DoSpawn_c(uint32_t entry_esp) { return spawn_unnamed(entry_esp, own_address(&GScr_DoSpawnNamed_entry), 0x00309011); }

uint32_t XB_CDECL GScr_LocationalTraceSurface_c(uint32_t entry_esp) {
    // (out, start, end, hit-box contents, pass handle): GScr_TraceSurfaceType's record from a locational trace
    // (contents 0x280e031 with the flag, else 0x802031). A near-vertical trace (horizontal length^2 < 0.01) of
    // an entity with cached candidate lists (+0x450 triangles, +0x454 brushes; box +0x430/+0x440 regathered
    // with margin 50 (0x0098f160, built once) when the trace leaves it) filters them into the frame's lists
    // and runs SV_LocationalTrace; other vertical traces from outside the world's cells, or from a cell with no
    // data (+0x3c), give a hit at start with the normal up and no surface; the rest G_LocationalTrace. EBP
    // frame: A = its aligned ESP, body S = A - 0x18a0: start/end {x, y, z, x} at +0x10 / +0x30 (the trace box
    // there while filtering), mask +0x2c, results +0x40 (endpos, normal +0x10, fraction +0x20, surface +0x24,
    // entity +0x30), brush list +0x80 (data, pointer +0x800, count +0x804), triangle list +0x890 (+0x1000).
    const uint32_t F = entry_esp;
    const uint32_t A = (F - 4) & ~15u, S = A - 0x18a0;
    const uint32_t* const start = at<const uint32_t*>(F + 8);
    const uint32_t* const end = at<const uint32_t*>(F + 0xc);
    const uint32_t mask = at<uint8_t>(F + 0x10) ? 0x0280e031 : 0x00802031;
    at<uint32_t>(S + 0x2c) = mask;
    at<uint32_t>(S + 0x70) = 0;
    const uint32_t start4[4] = {start[0], start[1], start[2], start[0]};
    const uint32_t end4[4] = {end[0], end[1], end[2], end[0]};
    const auto put = [](uint32_t address, const uint32_t* v) { std::memcpy(&at<uint32_t>(address), v, 16); };
    put(S + 0x10, end4);
    put(S + 0x30, start4);
    const float dx = at<float>(S + 0x10) - at<float>(S + 0x30);
    const float dy = at<float>(S + 0x14) - at<float>(S + 0x34);
    const bool vertical = title_global<float>(0x0083eb94) > add(mul(dx, dx), mul(dy, dy));  // 0.01
    uint8_t* const ent = handle_entity(at<uint32_t>(F + 0x14));
    enum { kLocational, kNoTrace, kCandidates } path = kLocational;
    if (vertical) {
        if (ent && field<uint32_t>(ent, 0x450) && field<uint32_t>(ent, 0x454)) {
            path = kCandidates;
        } else {
            put(S + 0x10, start4);
            put(S + 0x30, start4);
            int32_t cell = CM_CellForPoint(0, &at<float>(S + 0x30));
            path = kNoTrace;
            if (cell >= 0) {
                uint8_t* const cm = title_global<uint8_t*>(0x0095f6b8);
                if (static_cast<uint32_t>(cell) >= field<uint32_t>(cm, 0x18)) cell = 0;
                if (field<uint32_t>(field<uint8_t*>(cm, 0x1c), cell * 0x50 + 0x3c)) path = kLocational;
            }
        }
    }
    if (path == kCandidates) {
        float* const mins = &at<float>(S + 0x10);
        float* const maxs = &at<float>(S + 0x30);
        alignas(16) float lo[4], hi[4], over[4], under[4], worst[4];
        platform_maxps(hi, reinterpret_cast<const float*>(start4), reinterpret_cast<const float*>(end4));
        platform_minps(lo, reinterpret_cast<const float*>(start4), reinterpret_cast<const float*>(end4));
        platform_subps(over, hi, &field<float>(ent, 0x440));
        platform_subps(under, &field<float>(ent, 0x430), lo);
        platform_maxps(worst, under, over);
        const bool inside = (platform_cmpltps_mask(worst, &title_global<float>(0x0082e3c0)) & 7) == 7;
        std::memcpy(mins, lo, 16);
        std::memcpy(maxs, hi, 16);
        if (!inside) {
            float* const margin = &title_global<float>(0x0098f160);
            if (!(title_global<uint32_t>(0x0098f170) & 1)) {
                title_global<uint32_t>(0x0098f170) |= 1;
                for (uint32_t i = 0; i < 4; ++i)
                    title_global<uint32_t>(0x0098f160 + i * 4) = title_global<uint32_t>(0x0083ec30);  // 50
            }
            alignas(16) float box[4];
            // Lane 3 is never read; stored as 0 (host stack garbage otherwise, see Vehicle_GatherCollisionCandidates).
            platform_subps(box, lo, margin);
            box[3] = 0.0f;
            std::memcpy(&field<float>(ent, 0x430), box, 16);
            platform_addps(box, hi, margin);
            box[3] = 0.0f;
            std::memcpy(&field<float>(ent, 0x440), box, 16);
            CM_GatherBoxCandidates(&field<float>(ent, 0x430), field<uint8_t*>(ent, 0x450), field<uint8_t*>(ent, 0x454),
                                   &field<float>(ent, 0x440));
        }
        at<uint32_t>(S + 0x1890) = S + 0x890;
        at<uint32_t>(S + 0x1894) = 0;
        at<uint32_t>(S + 0x884) = 0;
        at<uint32_t>(S + 0x880) = S + 0x80;
        CM_FilterBoxCandidates(&at<uint8_t>(S + 0x80), 0, field<uint8_t*>(ent, 0x454), mins, maxs, mask,
                               field<uint8_t*>(ent, 0x450), &at<uint8_t>(S + 0x890));
        // Its arguments stay on the stack above SV_LocationalTrace's.
        const uint32_t filter_args[5] = {S + 0x10, S + 0x30, mask, field<uint32_t>(ent, 0x450), S + 0x890};
        std::memcpy(&at<uint32_t>(S - 0x14), filter_args, 20);
        put(S + 0x30, end4);
        put(S + 0x10, start4);
        TitleRegs regs{};
        regs.eax = S + 0x890;
        regs.ebx = field<uint32_t>(ent, 0x224);
        regs.ecx = S + 0x40;
        regs.edx = S + 0x80;
        regs.esi = addr(start);
        regs.edi = addr(end);
        const uint32_t args[7] = {S + 0x890, S + 0x80, S + 0x40, S + 0x10, S + 0x30, regs.ebx, mask};
        call_at(S - 0x14, 0x00305978, own_address(&SV_LocationalTrace_entry), regs, 7, args);  // SV_LocationalTrace
    } else if (path == kLocational) {
        put(S + 0x30, end4);
        put(S + 0x10, start4);
        TitleRegs regs{};
        regs.esi = addr(start);
        regs.edi = addr(end);
        uint32_t ret;
        if (ent) {
            regs.eax = S + 0x10;
            regs.ebx = field<uint32_t>(ent, 0x224);
            regs.ecx = S + 0x40;
            regs.edx = S + 0x30;
            ret = 0x00305a73;
        } else {
            regs.eax = S + 0x30;
            regs.ecx = S + 0x10;
            regs.edx = S + 0x40;
            ret = 0x00305ae9;
        }
        const uint32_t args[7] = {S + 0x40, S + 0x10, S + 0x30, regs.ebx, mask, 0x008da6f0, 0};
        call_at(S, ret, own_address(&G_LocationalTrace_entry), regs, 7, args);  // G_LocationalTrace
    } else {
        // No trace: a hit at start (fraction 0), normal up, no surface.
        put(S + 0x10, start4);
        put(S + 0x30, start4);
        at<uint32_t>(S + 0x40) = start[0];
        at<uint32_t>(S + 0x44) = start[1];
        at<uint32_t>(S + 0x48) = start[2];
        at<uint32_t>(S + 0x60) = 0;
        const uint32_t up[4] = {0, 0, title_global<uint32_t>(0x0083eb20), 0};  // 1
        put(S + 0x10, up);
        put(S + 0x50, up);
        at<uint32_t>(S + 0x64) = 0;
    }
    uint8_t* const out = at<uint8_t*>(F + 4);
    field<uint32_t>(out, 4) = at<uint32_t>(S + 0x40);
    field<uint32_t>(out, 8) = at<uint32_t>(S + 0x44);
    field<uint32_t>(out, 0xc) = at<uint32_t>(S + 0x48);
    const float fraction = at<float>(S + 0x60);
    field<float>(out, 0) = fraction;
    const uint32_t hit = at<uint32_t>(S + 0x70);
    if (hit == 0 || hit == field<uint32_t>(field<uint8_t*>(title_global<uint8_t*>(0x0095f534), 8), 0x224))
        field<uint32_t>(out, 0x10) = 0;
    else
        field<uint32_t>(out, 0x10) = field<uint32_t>(handle_entity(hit), 0x224);
    if (title_global<float>(0x0083eb20) > fraction) {  // 1 > fraction: a hit
        field<uint32_t>(out, 0x14) = at<uint32_t>(S + 0x50);
        field<uint32_t>(out, 0x18) = at<uint32_t>(S + 0x54);
        field<uint32_t>(out, 0x1c) = at<uint32_t>(S + 0x58);
        const int32_t surface = (at<int32_t>(S + 0x64) >> 20) & 0x1f;
        if (surface > 0 && surface < 0x17)
            return assign_cstring(out + 0x20, title_text(title_global<uint32_t>(0x008d4d08 + surface * 16)));
        return assign_cstring(out + 0x20, title_text(0x00831f70));  // "default"
    }
    const float* const s = reinterpret_cast<const float*>(start);
    const float* const e = reinterpret_cast<const float*>(end);
    const float dz = e[2] - s[2];
    const float dy2 = e[1] - s[1];
    const float dx2 = e[0] - s[0];
    at<float>(S + 0x18) = dz;
    at<float>(S + 0x14) = dy2;
    const float length = title_ci_sqrt_float(add(add(mul(dz, dz), mul(dy2, dy2)), mul(dx2, dx2)));
    at<float>(S + 0x2c) = length;
    at<float>(S + 0x10) = dx2;
    if (length != title_global<float>(0x0083eafc)) {  // fucomip: NaN normalizes too
        const float inv = title_global<float>(0x0083eb20) / length;
        at<float>(S + 0x10) = mul(inv, dx2);
        at<float>(S + 0x14) = mul(inv, dy2);
        at<float>(S + 0x18) = mul(inv, dz);
    }
    field<uint32_t>(out, 0x14) = at<uint32_t>(S + 0x10);
    field<uint32_t>(out, 0x18) = at<uint32_t>(S + 0x14);
    field<uint32_t>(out, 0x1c) = at<uint32_t>(S + 0x18);
    return assign_cstring(out + 0x20, title_text(0x00831f70));  // "default"
}

uint8_t XB_CDECL GScr_ActorTryTeleport_c(uint32_t entry_esp) {
    // (handle, point, angles or null, ignore sight): outside level spawning, a move of more than 10 units
    // needs the player (the entity whose classname hash +0x260 is "player") to see neither the point nor the
    // actor's origin from its eye (Actor_CanSeePointFromBounds; the last argument ignores that) and above 128
    // units flips the teleport bit (+0x10 ^ 8); a short one fails while the actor's path has points (+0x740,
    // +0x798). Then the entity is placed (trajectory and origin, no velocity), optionally turned (G_SetAngle2,
    // the actor's 16-bit-rounded angles +0x130 and desired angles), its path trimmed (Path_AccumulateTrim), the
    // path cleared, move mode 1 -> 7, and the claimed node (sentient +0x58) dropped (+0x5a = 0) unless it lies
    // exactly at the point. AL 1, 0 for a stale handle or a refusal. EBP frame: A = its aligned ESP, body
    // S = A - 0x40: point {x, y, z, x} +0x10, eye +0x20, squared distance (all four lanes) +0x30.
    const uint32_t F = entry_esp;
    uint8_t* const actor = handle_actor(at<uint32_t>(F + 4));
    if (!actor) return 0;
    const uint32_t A = (F - 4) & ~15u, S = A - 0x40;
    at<uint32_t>(S + 0xc) = addr(actor);
    uint8_t* const ent = field<uint8_t*>(actor, 0);
    const uint32_t* const point = at<const uint32_t*>(F + 8);
    const auto point_vector = [&] {
        at<uint32_t>(S + 0x1c) = point[0];
        at<uint32_t>(S + 0x10) = point[0];
        at<uint32_t>(S + 0x18) = point[2];
        at<uint32_t>(S + 0x14) = point[1];
    };
    if (!title_global<uint32_t>(0x00985fe0)) {  // level_spawning
        point_vector();
        float* const delta = &at<float>(S + 0x30);
        platform_subps(delta, &at<float>(S + 0x10), &field<float>(ent, 0x140));
        platform_mulps(delta, delta, delta);
        delta[0] = add(add(delta[0], delta[1]), delta[2]);
        if (at<float>(S + 0x30) > title_global<float>(0x0083eb90)) {  // 100
            uint8_t* const player = G_FindEntityByIntField(&title_global<uint8_t>(0x0097a8c0), 0x260,
                                                           title_global<int32_t>(0x0097a75c));  // scr_const_player
            Sentient_EyeOrigin(field<uint8_t*>(player, 0x244), 0, &at<uint8_t>(S + 0x10));
            at<uint32_t>(S + 0x20) = at<uint32_t>(S + 0x10);
            at<uint32_t>(S + 0x24) = at<uint32_t>(S + 0x14);
            at<uint32_t>(S + 0x28) = at<uint32_t>(S + 0x18);
            point_vector();
            const uint32_t handle = at<uint32_t>(F + 4), player_handle = field<uint32_t>(player, 0x224);
            const bool ignore_sight = at<uint8_t>(F + 0x10) != 0;
            TitleRegs regs{};
            regs.eax = S + 0x20;
            regs.ecx = S + 0x10;
            regs.ebx = addr(ent);
            regs.esi = addr(player);
            regs.edi = handle;
            const uint32_t point_args[3] = {S + 0x10, player_handle, handle};
            if (call_at(S, 0x00306522, own_address(&Actor_CanSeePointFromBounds_entry), regs, 3, point_args) && !ignore_sight)  // Actor_CanSeePointFromBounds
                return 0;
            regs.ecx = 0;
            regs.esi = player_handle;
            const uint32_t origin_args[3] = {addr(ent + 0x140), player_handle, handle};
            if (call_at(S, 0x0030654c, own_address(&Actor_CanSeePointFromBounds_entry), regs, 3, origin_args) && !ignore_sight) return 0;
            if (at<float>(S + 0x30) > title_global<float>(0x0083ee50))  // 16384
                field<uint32_t>(ent, 0x10) ^= 8;
        } else if (field<int16_t>(actor, 0x740) > 0 && field<uint32_t>(actor, 0x798)) {
            return 0;
        }
    }
    field<uint32_t>(ent, 0x20) = point[0];
    field<uint32_t>(ent, 0x24) = point[1];
    field<uint32_t>(ent, 0x28) = point[2];
    field<uint32_t>(ent, 0x34) = 0;
    field<uint32_t>(ent, 0x30) = 0;
    field<uint32_t>(ent, 0x2c) = 0;
    field<uint32_t>(ent, 0x14) = 0;
    field<uint32_t>(ent, 0x18) = 0;
    field<uint32_t>(ent, 0x1c) = 0;
    field<uint32_t>(ent, 0x140) = point[0];
    field<uint32_t>(ent, 0x144) = point[1];
    field<uint32_t>(ent, 0x148) = point[2];
    field<uint32_t>(actor, 0x2a8) = 0;
    field<uint32_t>(actor, 0x2a4) = 0;
    field<uint32_t>(actor, 0x2a0) = 0;
    field<uint32_t>(actor, 0x2cc) = 0;
    field<uint32_t>(actor, 0x2c8) = 0;
    field<uint32_t>(actor, 0x2c4) = 0;
    if (const float* const angles = at<const float*>(F + 0xc)) {
        G_SetAngle2(ent, 0, angles);
        const float to_short = title_global<float>(0x0083ebd8), to_angle = title_global<float>(0x0083ebd4);
        const int32_t yaw = static_cast<int32_t>(mul(field<float>(ent, 0x154), to_short)) & 0xffff;
        const int32_t pitch = static_cast<int32_t>(mul(field<float>(ent, 0x150), to_short)) & 0xffff;
        const float yaw_angle = mul(static_cast<float>(yaw), to_angle);
        field<float>(actor, 0x130) = mul(static_cast<float>(pitch), to_angle);
        field<float>(actor, 0x134) = yaw_angle;
        field<float>(actor, 0x138) = yaw_angle;
        Actor_SetDesiredAngles(actor, field<float>(ent, 0x150), field<float>(ent, 0x154));
    }
    Path_AccumulateTrim(actor + 0x3c0, &field<float>(field<uint8_t*>(actor, 0), 0x140));
    field<uint16_t>(actor, 0x740) = 0;
    field<uint16_t>(actor, 0x742) = 0;
    field<uint32_t>(actor, 0x7c4) = 0;
    if (field<uint32_t>(actor, 0x278) == 1) field<uint32_t>(actor, 0x278) = 7;
    uint8_t* const sentient = field<uint8_t*>(actor, 4);
    const uint16_t* const node_handle = &field<uint16_t>(sentient, 0x58);
    if (PathNode_HandleIsValid(node_handle) & 0xff) {
        const uint8_t* const node = PathNode_FromHandle2(node_handle);
        if (!node) return 1;
        const float* const p = reinterpret_cast<const float*>(point);
        const float* const at_node = reinterpret_cast<const float*>(node + 0x44);
        if (p[0] == at_node[0] && p[1] == at_node[1] && p[2] == at_node[2]) return 1;  // ucomiss: NaN differs
    }
    sentient[0x5a] = 0;
    return 1;
}

uint8_t XB_CDECL GScr_ActorIsSafeMovePoint_c(uint32_t entry_esp) {
    // (handle, point): whether the actor may move to the point - within its sentient's goal (height +-80,
    // radius^2 +0x1c around +0xc), on no line between a sentient of its team and that one's enemy (the point
    // and the actor on opposite sides of the line, and the line crossing the actor-point segment), no solid
    // trace down the 256-unit column at the point (SV_Trace, mask +0x310 | 0x6000), and Path_TraceAhead from the
    // sentient's position (+6 up; mask | 0x4004, 0x0095f704 = the entity meanwhile). With debug lines on
    // (0x009506cc) each verdict draws its line (100 frames). EBP frame: A = its aligned ESP, body S = A - 0xb0:
    // the column's top / bottom +0x24 / +0x34, maxs +0x10 (= the top), end +0x40, start (EBX) +0x50, mins +0x60
    // (= the bottom), results +0x70 (never set before the call, as in the original).
    const uint32_t F = entry_esp;
    uint8_t* const actor = handle_actor(at<uint32_t>(F + 4));
    if (!actor) return 0;
    const uint32_t A = (F - 4) & ~15u, S = A - 0xb0;
    at<uint32_t>(S + 0xc) = addr(actor);
    const float* const point = at<const float*>(F + 8);
    uint8_t* const sentient = field<uint8_t*>(actor, 4);
    const auto debug_line = [](const float* end, const float* start, uint32_t color) {
        if (title_global<uint32_t>(0x009506cc))
            DebugDraw_AddLine(end, start, &title_global<float>(color), 1, 100, 1, 0);
    };
    const float* const origin = &field<float>(field<uint8_t*>(actor, 0), 0x140);
    const float dz = point[2] - field<float>(sentient, 0x14);
    const float dx = point[0] - field<float>(sentient, 0xc);
    const float dy = point[1] - field<float>(sentient, 0x10);
    bool outside = mul(dz, dz) > title_global<float>(0x0083eb6c);  // 80^2
    if (!outside) {
        at<uint32_t>(S + 0x30) = addr(sentient);
        outside = add(mul(dy, dy), mul(dx, dx)) > field<float>(sentient, 0x1c);
    }
    if (outside) {
        debug_line(point, origin, 0x0082eadc);
        return 0;
    }
    const uint32_t mask = 1u << (field<uint32_t>(sentient, 4) & 31);
    for (uint8_t* other = Sentient_FirstOfTeams(mask); other; other = Sentient_NextOfTeams(other, mask)) {
        if (other == at<uint8_t*>(S + 0x30)) continue;
        uint8_t* const enemy = field<uint8_t*>(other, 0x3c);
        if (!enemy) continue;
        uint8_t* const other_ent = field<uint8_t*>(other, 0);
        uint8_t* const enemy_ent = field<uint8_t*>(enemy, 0);
        const float ox = field<float>(other_ent, 0x140), oy = field<float>(other_ent, 0x144);
        const float ex = field<float>(enemy_ent, 0x140), ey = field<float>(enemy_ent, 0x144);
        const float* const me = &field<float>(field<uint8_t*>(actor, 0), 0x140);
        const float mx = me[0], my = me[1];
        // The actor and the point on opposite sides of the line other -> enemy ...
        const float side_me = add(mul(mx - ox, oy - ey), mul(my - oy, ex - ox));
        const float side_point = add(mul(point[0] - ox, oy - ey), mul(point[1] - oy, ex - ox));
        if (mul(side_me, side_point) >= 0.0f) continue;
        // ... and other and enemy on opposite sides of the actor -> point segment (NaN counts as crossing).
        const float c = add(mul(oy - me[1], point[0] - me[0]), mul(ox - me[0], me[1] - point[1]));
        const float d = add(mul(ex - me[0], me[1] - point[1]), mul(ey - me[1], point[0] - me[0]));
        if (!(mul(c, d) >= 0.0f)) {
            debug_line(point, origin, 0x0082eb1c);
            return 0;
        }
    }
    // The column through the point, 128 up and down.
    const float px = point[0], py = point[1];
    const float bottom = point[2] - title_global<float>(0x0083ebe8);  // 128
    const float top = point[2] + title_global<float>(0x0083ebe8);
    const float end4[4] = {px, py, bottom, px}, start4[4] = {px, py, top, px};
    at<float>(S + 0x3c) = bottom;
    at<float>(S + 0x2c) = top;
    at<float>(S + 0x24) = px;
    at<float>(S + 0x28) = py;
    at<float>(S + 0x34) = px;
    at<float>(S + 0x38) = py;
    std::memcpy(&at<float>(S + 0x40), end4, 16);
    std::memcpy(&at<float>(S + 0x10), start4, 16);
    std::memcpy(&at<float>(S + 0x60), end4, 16);
    std::memcpy(&at<float>(S + 0x50), start4, 16);
    const uint32_t handle = at<uint32_t>(F + 4);
    TitleRegs regs{};
    regs.eax = S + 0x70;
    regs.ebx = S + 0x50;
    regs.ecx = S + 0x10;
    regs.edx = S + 0x60;
    regs.esi = mask;
    regs.edi = addr(point);
    const uint32_t trace_args[11] = {S + 0x70, S + 0x60, S + 0x10, S + 0x40, handle,
                                     field<uint32_t>(actor, 0x310) | 0x6000, 0, 0, 0, 0, 0};
    call_at(S, 0x00306a3a, own_address(&SV_Trace_entry), regs, 11, trace_args);  // SV_Trace
    if (at<uint8_t>(S + 0xac)) {
        debug_line(&at<float>(S + 0x34), &at<float>(S + 0x24), 0x0082ebdc);  // bottom to top
        return 0;
    }
    // Path_TraceAhead back from the sentient's position.
    uint8_t* const sentient_ent = field<uint8_t*>(field<uint8_t*>(actor, 4), 0);
    at<uint32_t>(S + 0x24) = field<uint32_t>(sentient_ent, 0x140);
    at<uint32_t>(S + 0x28) = field<uint32_t>(sentient_ent, 0x144);
    at<float>(S + 0x2c) = add(field<float>(sentient_ent, 0x148), title_global<float>(0x0083ecd0));  // 6
    const uint32_t path_mask = field<uint32_t>(actor, 0x310) | 0x4004;
    title_global<uint32_t>(0x0095f704) = field<uint32_t>(actor, 0);
    regs.eax = S - 0x10;
    regs.ecx = S + 0x24;
    regs.edx = handle;
    regs.esi = addr(actor);
    const uint32_t path_args[5] = {addr(point), 0, handle, path_mask, S + 0x34};
    const uint32_t clear = call_at(S, 0x00306ae8, own_address(&Path_TraceAhead_entry), regs, 5, path_args);  // Path_TraceAhead (ret 0x14)
    title_global<uint32_t>(0x0095f704) = 0;
    if (!clear) {
        debug_line(point, &at<float>(S + 0x24), 0x0082ebec);
        return 0;
    }
    debug_line(point, &field<float>(field<uint8_t*>(actor, 0), 0x140), 0x0082eaec);
    return 1;
}

}  // namespace game

// ESP entries: the C version gets the entry ESP with its own frame moved below a gap.
#define GSCR_ESP_ENTRY(name)                                                                        \
    PLATFORM_ASM_ENTRY(name##_entry, "movl %esp, %edx\n" GAME_GAP_ESP("0x10000") "pushl %edx\n"    \
                                     PLATFORM_ASM_CALL(name##_c) "addl $4, %esp\n popl %esp\n ret")

GSCR_ESP_ENTRY(GScr_GetAngleDelta)
GSCR_ESP_ENTRY(GScr_GetMoveDelta)
GSCR_ESP_ENTRY(GScr_GetAnimWorldMoveDelta)
GSCR_ESP_ENTRY(GScr_GetAnimStartAngles)
GSCR_ESP_ENTRY(GScr_TransformLocalOffset)
GSCR_ESP_ENTRY(GScr_DisconnectPaths)
GSCR_ESP_ENTRY(GScr_ActorMayMoveToPoint)
GSCR_ESP_ENTRY(GScr_PlaySoundMode7)
GSCR_ESP_ENTRY(GScr_PlayLoopSound)
GSCR_ESP_ENTRY(GScr_PlaySoundAtPosition)
GSCR_ESP_ENTRY(GScr_PlaySound)
GSCR_ESP_ENTRY(GScr_ActorUpdateNearestNode)
GSCR_ESP_ENTRY(GScr_ActorNodeCanSeeEnemyNode)
GSCR_ESP_ENTRY(GScr_ActorGetNearestNode)
GSCR_ESP_ENTRY(GScr_ActorCanShootWithOffset)
GSCR_ESP_ENTRY(GScr_RadiusDamageFromEntity)
GSCR_ESP_ENTRY(GScr_RadiusDamage)

// Tail-jump entries: the C version returns {target, value} in EDX:EAX (target 0: return `value` in EAX); the
// jump runs at the entry ESP with the argument slots the C version wrote.
PLATFORM_ASM_ENTRY(GScr_PlayerWeaponSound_entry,
                   "movl %esp, %edx\n" GAME_GAP_ESP("0x10000") "pushl %edx\n"
                   PLATFORM_ASM_CALL(GScr_PlayerWeaponSound_c) "addl $4, %esp\n popl %esp\n"
                   "testl %eax, %eax\n jz 1f\n pushl %eax\n movl %edx, %eax\n ret\n"  // EAX = the entity
                   "1:\n movl %edx, %eax\n ret")
PLATFORM_ASM_ENTRY(GScr_ActorSetAimAnimWeights_entry,
                   "movl %esp, %edx\n" GAME_GAP_ESP("0x10000") "pushl %edx\n"
                   PLATFORM_ASM_CALL(GScr_ActorSetAimAnimWeights_c) "addl $4, %esp\n popl %esp\n"
                   "testl %eax, %eax\n jz 1f\n pushl %eax\n movl %edx, %ecx\n xorl %eax, %eax\n ret\n"  // ECX = actor
                   "1:\n ret")
GSCR_ESP_ENTRY(GScr_IsTouching)
GSCR_ESP_ENTRY(GScr_Vehicle_Kill)
GSCR_ESP_ENTRY(GScr_Vehicle_AttachPath)
GSCR_ESP_ENTRY(GScr_ActorCanUseTurret)
GSCR_ESP_ENTRY(GScr_ActorCanShoot)
GSCR_ESP_ENTRY(GScr_ActorMelee)
GSCR_ESP_ENTRY(GScr_MagicGrenade)
GSCR_ESP_ENTRY(GScr_MagicGrenadeDefaultFuse)
GSCR_ESP_ENTRY(GScr_AnimScripted)
GSCR_ESP_ENTRY(GScr_AnimScriptedFlag0)
GSCR_ESP_ENTRY(GScr_AnimScriptedNoNotify)
GSCR_ESP_ENTRY(GScr_ActorUseTurret)
GSCR_ESP_ENTRY(GScr_ActorThrowGrenade)
GSCR_ESP_ENTRY(GScr_CheckGrenadeThrow)
GSCR_ESP_ENTRY(GScr_SetGoalEntity)
GSCR_ESP_ENTRY(GScr_ActorCanSee)
GSCR_ESP_ENTRY(GScr_ActorCanSeeEnemyDefaultAge)
GSCR_ESP_ENTRY(GScr_ActorCanSeeEnemy)
GSCR_ESP_ENTRY(GScr_ActorCanSeeDefaultAge)
GSCR_ESP_ENTRY(GScr_Vehicle_FireWeapon)
GSCR_ESP_ENTRY(GScr_MagicBullet)
GSCR_ESP_ENTRY(GScr_ActorShoot)
GSCR_ESP_ENTRY(GScr_ActorShootAt)
GSCR_ESP_ENTRY(GScr_ActorFindBetterCover)
GSCR_ESP_ENTRY(GScr_ForceSpawnNamed)
GSCR_ESP_ENTRY(GScr_DoSpawnNamed)
GSCR_ESP_ENTRY(GScr_ActorApplyAnimMove)
GSCR_ESP_ENTRY(GScr_ForceSpawn)
GSCR_ESP_ENTRY(GScr_DoSpawn)

// Tail jumps with EAX and ECX: the C version returns the target (0: return regs[0] in EAX) and fills regs[0..1];
// the jump runs at the entry ESP with the argument slots the C version wrote.
PLATFORM_ASM_ENTRY(GScr_TurretCall272_entry,
                   "movl %esp, %edx\n" GAME_GAP_ESP("0x10000") "subl $8, %esp\n movl %esp, %eax\n pushl %eax\n pushl %edx\n"
                   PLATFORM_ASM_CALL(GScr_TurretCall272_c) "movl 8(%esp), %edx\n movl 12(%esp), %ecx\n addl $16, %esp\n"
                   "popl %esp\n testl %eax, %eax\n jz 1f\n pushl %eax\n movl %edx, %eax\n ret\n"
                   "1:\n movl %edx, %eax\n ret")

// title_call_at_regs with the callee's ST0 left in place (title_call_at_regs and the floor helpers it calls do
// no x87 work).
PLATFORM_ASM_ENTRY(gscr_call_at_st0,
                   "pushl 20(%esp)\n pushl 20(%esp)\n pushl 20(%esp)\n pushl 20(%esp)\n pushl 20(%esp)\n"
                   PLATFORM_ASM_CALL(title_call_at_regs) "addl $20, %esp\n ret")

// GScr_LocationalTraceSurface's 6 KB frame and trace callees below it: a 192 KB gap.
PLATFORM_ASM_ENTRY(GScr_LocationalTraceSurface_entry, "movl %esp, %edx\n" GAME_GAP_ESP("0x30000") "pushl %edx\n"
                   PLATFORM_ASM_CALL(GScr_LocationalTraceSurface_c) "addl $4, %esp\n popl %esp\n ret")
GSCR_ESP_ENTRY(GScr_ActorTryTeleport)
GSCR_ESP_ENTRY(GScr_ActorIsSafeMovePoint)
