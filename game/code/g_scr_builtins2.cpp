#include "g_scr_builtins2.h"

#include <cstring>

#include "actor.h"
#include "bg_misc.h"
#include "cm.h"
#include "g_mover.h"
#include "g_script.h"
#include "g_scr_runtime.h"
#include "g_spawn.h"
#include "g_utils.h"
#include "g_vehicle.h"
#include "qcommon.h"
#include "qcommon_fs.h"
#include "script_string.h"
#include "sv.h"
#include "title_data.h"
#include "xanim.h"

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
    return title_function<uint32_t XB_THISCALL(void*, const char*)>(0x0011dd20)(out, text);  // ScriptString_AssignCString
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
                     : "m"(x), "r"(uintptr_t{0x0033acf4})  // __CIsqrt
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
    return title_function<double XB_CDECL(double)>(0x001ef3fd)(  // __ceil_default
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
    title_function<AxisBuiltin>(0x00302fb0)(a, b, handle, 2, a, b, c, d);  // GScr_RotateAxis
}
void XB_CDECL GScr_RotateYaw(uint32_t handle, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    title_function<AxisBuiltin>(0x00302fb0)(a, b, handle, 1, a, b, c, d);  // GScr_RotateAxis
}
void XB_CDECL GScr_RotatePitch(uint32_t handle, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    title_function<AxisBuiltin>(0x00302fb0)(a, b, handle, 0, a, b, c, d);  // GScr_RotateAxis
}
void XB_CDECL GScr_MoveZ(uint32_t handle, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    title_function<AxisBuiltin>(0x00303080)(a, b, handle, 2, a, b, c, d);  // GScr_MoveAxis
}
void XB_CDECL GScr_MoveY(uint32_t handle, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    title_function<AxisBuiltin>(0x00303080)(a, b, handle, 1, a, b, c, d);  // GScr_MoveAxis
}
void XB_CDECL GScr_MoveX(uint32_t handle, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    title_function<AxisBuiltin>(0x00303080)(a, b, handle, 0, a, b, c, d);  // GScr_MoveAxis
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
    call_at(top, ret, 0x002a2af0, regs, 2, args);  // AnglesToAxis
}

}  // namespace

float XB_CDECL GScr_GetAngleDelta_c(uint32_t entry_esp) {
    const uint32_t F = entry_esp;
    const uint32_t anim = at<uint32_t>(F + 4);
    const uint32_t dir = F - 0x14, pos = F - 0xc;
    const uint32_t args[2] = {at<uint32_t>(F + 8), at<uint32_t>(F + 0xc)};
    call_at(F - 0x1c, 0x00303f71, 0x002e6710, anim_regs(anim, dir, pos), 2, args);  // AnimTree_GetRootDeltaBetween
    at<float>(F + 4) = vectoyaw_2(&at<float>(dir));  // kept in the first argument slot, then FLD
    return at<float>(F + 4);
}

void XB_CDECL GScr_GetMoveDelta_c(uint32_t entry_esp) {
    const uint32_t F = entry_esp;
    const uint32_t anim = at<uint32_t>(F + 8);
    const uint32_t args[2] = {at<uint32_t>(F + 0xc), at<uint32_t>(F + 0x10)};
    call_at(F - 0x10, 0x00303fd1, 0x002e6710, anim_regs(anim, F - 8, at<uint32_t>(F + 4)), 2,
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
    call_at(F - 0x64, 0x00304065, 0x002e6650, anim_regs(anim, F - 0x54, F - 0x3c), 1, time0);  // AnimTree_GetRootDelta
    const uint32_t time1[1] = {0x3f800000};  // 1.0
    call_at(F - 0x64, 0x00304098, 0x002e6650, anim_regs(anim, F - 0x54, F - 0x48), 1, time1);  // AnimTree_GetRootDelta
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
    call_at(F - 0x98, 0x0030418f, 0x002e6650, anim_regs(anim, F - 0x5c, F - 0x54), 1, time0);  // AnimTree_GetRootDelta
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
    call_at(F - 0x50, 0x003043f9, 0x002e6650, anim_regs(anim, F - 0x48, F - 0x3c), 1, time0);  // AnimTree_GetRootDelta
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
        title_function<const char* XB_CDECL(const char*, const char*)>(0x002a93d0)(  // va
            title_text(0x008320e4), script_text(reinterpret_cast<const ScriptString*>(ent + 0x25c)));
        return;
    }
    // No frame: the call runs at the entry ESP (Path_DisconnectPathsForEntity keeps a frame address in 0x00929b40).
    TitleRegs regs{};
    regs.eax = title_global<uint32_t>(0x0095f4f4);
    regs.ecx = field<uint32_t>(ent, 0x2a4);
    regs.edx = handle >> 12;
    const uint32_t args[2] = {title_global<uint32_t>(0x0095f4f4), addr(ent)};
    call_at(entry_esp, 0x0030456f, 0x002e8ef0, regs, 2, args);  // Path_DisconnectPathsForEntity (callee pops 8)
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
    title_function<const char* XB_CDECL(const char*, const char*)>(0x002a93d0)(  // va
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
    return call_at(A - 0x30, 0x00304871, 0x002e8270, regs, 3, args) != 0;  // SV_ClipToEntity
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
        title_function<const char* XB_CDECL(const char*)>(0x002a93d0)(title_text(0x00831968));  // va "No driver to eject"
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
                               0, 0, 0, 1, addr(bounds), 0x002601a0, 0x002601e0, 1, 0x42700000};
    return call_at(A - 0x30, 0x0030554c, 0x002dcc60, regs, 14, args) != 0;  // BG_CheckProne (G_TraceCapsule, G_Trace)
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
    return static_cast<int32_t>(call_at(F - 8, ret, 0x002a6990, regs, 4, args));  // FXCmd_PlaySoundAlias
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
    title_function<const char* XB_CDECL(const char*, const char*)>(0x002a93d0)(  // va
        title_text(0x00832598), script_text(classname));  // "unable to spawn \"%s\" entity"
    return nullptr;
}

}  // namespace

void XB_CDECL GScr_AttachNoTag(uint32_t handle, const ScriptString* model) {
    uint32_t tag = 0;
    ScriptString_FromCString(reinterpret_cast<ScriptString*>(&tag), title_text(0x0082dfeb));  // ""
    GScr_Detach(handle, model, reinterpret_cast<const ScriptString*>(&tag));
    ScriptString_Release(reinterpret_cast<ScriptString*>(&tag));
}

void XB_CDECL GScr_DetachNoTag(uint32_t handle, const ScriptString* model) {
    uint32_t tag = 0;
    ScriptString_FromCString(reinterpret_cast<ScriptString*>(&tag), title_text(0x0082dfeb));  // ""
    GScr_Attach(handle, model, reinterpret_cast<const ScriptString*>(&tag), 0, nullptr);
    ScriptString_Release(reinterpret_cast<ScriptString*>(&tag));
}

void XB_CDECL GScr_DetachEx(uint32_t handle, const ScriptString* model, const ScriptString* tag, const uint8_t* owner) {
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
    return static_cast<int32_t>(call_at(F, 0x00305dd5, 0x002a6880, regs, 3, args));  // FXCmd_PlaySoundAliasMode7
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
    return static_cast<int32_t>(call_at(F - 8, 0x00305eec, 0x002a6740, regs, 4, args));  // FXCmd_PlaySoundAliasAtPoint
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
    return tail_jump(0x002a62d0, addr(ent));  // FXCmd_PlayWeaponSound (EAX = the entity)
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
    const uint32_t node = call_at(F - 8, 0x003062ee, 0x002095c0, regs, 6, args);  // Sentient_CachedNearestNode
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
    const uint32_t own = call_at(F - 0xc, 0x0030636f, 0x002095c0, regs, 6, args);  // Sentient_CachedNearestNode
    regs.eax = field<uint32_t>(actor, 4);
    regs.esi = own;
    regs.edi = field<uint32_t>(field<uint8_t*>(actor, 4), 0x3c);
    const uint32_t theirs = call_at(F - 0xc, 0x0030638b, 0x002095c0, regs, 6, args);  // Sentient_CachedNearestNode
    if (!own || !theirs) return 0;
    if (own == theirs) return 1;
    regs.eax = title_global<uint32_t>(0x0095f4f4);  // g_pathNodeBlocks
    regs.ecx = own;
    const uint32_t other[1] = {theirs};
    return (call_at(F - 0xc, 0x003063a4, 0x002b0d10, regs, 1, other) & 0xff) ? 1 : 0;  // PathNode_CanSee
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
        title_function<const char* XB_CDECL(const char*, const char*)>(0x002a93d0)(  // va
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
    const uint32_t node = call_at(F - 4, 0x00306da1, 0x00209700, regs, 5, args);  // Actor_FindNearestNode
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
    if (!call_at(F - 0x1c, 0x00306e0f, 0x00206080, regs, 2, muzzle_args)) return 0;  // Actor_GetMuzzleInfo
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
    const uint32_t clear = call_at(F - 0x1c, 0x00306e7e, 0x0020a140, regs, 1, shot_args);  // Actor_IsShotClear
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
    return tail_jump(0x0020d430, addr(actor));  // Actor_UpdateAimBlend (ECX = actor, EAX = 0)
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
    call_at(F, 0x00306fb4, 0x00268e60, regs, 8, args);  // G_RadiusDamage
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
    call_at(F, 0x00307002, 0x00268e60, regs, 8, args);  // G_RadiusDamage
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
