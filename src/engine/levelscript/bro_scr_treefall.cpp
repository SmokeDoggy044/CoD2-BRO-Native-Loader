#include "bro_scr_treefall.h"

#include <cstring>

#include "g_scr_runtime.h"
#include "g_script.h"
#include "scr_access.h"
#include "scr_field.h"
#include "script_string.h"
#include "title_data.h"

namespace game {

namespace {

template <typename T>
T* pointer(uint32_t address) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}

// A script-interface slot (pScr_*: a word in the import block at 0x00922xxx) called as F.
template <typename F>
F* slot(uint32_t address) {
    return title_function<F>(title_global<uint32_t>(address));
}

float as_float(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

using IsValid = uint32_t XB_CDECL(uint32_t);
using GetVec = uint32_t* XB_CDECL(uint32_t*, uint32_t);
using Spawn = uint32_t XB_CDECL(ScriptString*, const uint32_t*, uint32_t);
using GetEnt = uint32_t XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t);
using PlaySound = void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t);
using RotatePitch = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using Ent = void XB_CDECL(uint32_t);

constexpr uint32_t kTargetname = 0x0d786b4c;
constexpr uint32_t kRotateDone = 0xae6d9d1d;

}  // namespace

void XB_CDECL Bro__treefall_callbacked__treefall(uint32_t trigger, uint32_t other) {
    if (!slot<IsValid>(0x00922214)(trigger)) return;  // Scr_EntityHandleIsValid
    uint32_t facing = other;
    uint32_t buffer[3];
    if (!slot<IsValid>(0x00922214)(other)) {
        ScriptString classname;
        ScriptString_FromCString(&classname, pointer<const char>(0x0083ab88));  // "script_origin"
        const uint32_t* p = slot<GetVec>(0x00922b20)(buffer, title_global<uint32_t>(0x00922eec));  // ScrEnt_GetOrigin (player)
        const uint32_t at[3] = {p[0], p[1], p[2]};
        facing = slot<Spawn>(0x009222f0)(&classname, at, 0);  // GScr_Spawn
        ScriptString_Release(&classname);
        const uint32_t* a = slot<GetVec>(0x00922ba0)(buffer, title_global<uint32_t>(0x00922eec));  // ScrEnt_GetAngles
        ScrVec3 angles;
        angles.v[0] = a[0];
        angles.v[1] = a[1];
        angles.v[2] = a[2];
        slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922ba4)(facing, angles);  // ScrEnt_SetAngles
    }
    {
        uint32_t hash;
        const uint32_t name = *Scr_HashName(&hash, pointer<const char>(0x00824570));  // "_treefall_callbacked::treefall"
        slot<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x009221c0)(trigger, 0xe2a210cf, name);  // GScr_GetEntVar
    }
    ScriptString temp;
    {
        const bool none = ScrCall_ScrEnt_GetTarget(&trigger, &temp)->rep == nullptr;
        ScriptString_Release(&temp);
        if (none) return;
    }
    uint32_t tree = slot<GetEnt>(0x009221c4)(ScrCall_ScrEnt_GetTarget(&trigger, &temp), kTargetname, 0, 0, 0);  // GScr_GetEntArray
    ScriptString_Release(&temp);
    if (!slot<IsValid>(0x00922214)(tree)) return;
    {
        ScriptString target;
        const bool any = ScrCall_ScrEnt_GetTarget(&tree, &target)->rep != nullptr;
        ScriptString_Release(&target);
        if (any) {
            const uint32_t below = slot<GetEnt>(0x009221c4)(ScrCall_ScrEnt_GetTarget(&tree, &temp), kTargetname, 0, 0, 0);
            ScriptString_Release(&temp);
            if (slot<IsValid>(0x00922214)(below)) slot<Ent>(0x00922540)(below);  // GScr_Delete
        }
    }
    {
        ScriptString classname;
        const bool vehicle = ScriptString_Equals(ScrCall_ScrEnt_GetClassname(&tree, &classname),
                                                 pointer<const char>(0x0083ab78));  // "script_vehicle"
        ScriptString_Release(&classname);
        if (vehicle) {
            const float* o = reinterpret_cast<const float*>(ScrCall_ScrEnt_GetOrigin(&tree, buffer));
            const float point[3] = {o[0], o[1], o[2] + as_float(title_global<uint32_t>(0x007e5bf0))};  // + 64
            slot<void XB_CDECL(uint32_t, const float*, uint32_t, uint32_t, uint32_t)>(0x00922898)(
                tree, point, 0x3e99999a, 0x3f2b851f, 0x41300000);  // GScr_Vehicle_PushFromPoint (0.3, 0.67, 11)
        }
    }
    {
        uint32_t buffer2[3];
        const uint32_t* tree_origin = ScrCall_ScrEnt_GetOrigin(&tree, buffer);
        const uint32_t player = title_global<uint32_t>(0x00922eec);
        const uint32_t* player_origin = ScrCall_ScrEnt_GetOrigin(&player, buffer2);
        const long double distance =
            slot<long double XB_CDECL(const uint32_t*, const uint32_t*)>(0x00922268)(player_origin, tree_origin);  // Scr_Distance
        if (platform_x87_ld(as_float(title_global<uint32_t>(0x0083ebe8))) > distance) {  // 128 (FCOMIP)
            ScriptString effect, rumble;
            ScriptString_FromCString(&effect, pointer<const char>(0x00824568));  // "zmaaaga"
            ScriptString_FromCString(&rumble, pointer<const char>(0x007f9190));  // "aaaa"
            ScriptString copy;
            ScriptString_Copy(&copy, &rumble);
            ScrCall_GScr_ShakeRumbleNamed(address_of(&effect), 0x3f99999a, copy, 0);  // 1.2
            ScriptString_Release(&rumble);
            ScriptString_Release(&effect);
        }
    }
    uint32_t pivot;
    {
        ScriptString classname;
        ScriptString_FromCString(&classname, pointer<const char>(0x0083ab88));  // "script_origin"
        pivot = slot<Spawn>(0x009222f0)(&classname, ScrCall_ScrEnt_GetOrigin(&tree, buffer), 0);
        ScriptString_Release(&classname);
    }
    uint32_t vec[3];
    ScrCall_ScrEnt_SetOrigin(&pivot, reinterpret_cast<const ScrVec3*>(ScrCall_ScrEnt_GetOrigin(&tree, vec)));
    uint32_t angles[3];
    ScrCall_ScrEnt_SetAngles(&pivot, reinterpret_cast<const ScrVec3*>(ScrCall_ScrEnt_GetAngles(&tree, angles)));
    bool defined;
    if (*ScrField_script_fxid_IsDefined(&defined, trigger)) {
        slot<PlaySound>(0x009221dc)(pivot, ScrField_script_fxid(trigger), 0, 0, 0);  // GScr_PlaySound
    } else {
        ScriptString sound;
        ScriptString_FromCString(&sound, pointer<const char>(0x0082455c));  // "treefall"
        slot<PlaySound>(0x009221dc)(pivot, &sound, 0, 0, 0);
        ScriptString_Release(&sound);
    }
    ScrCall_ScrEnt_GetAngles(&tree, angles);  // read and dropped
    ScrCall_ScrEnt_GetAngles(&pivot, angles);
    {
        uint32_t far_buffer[3];
        const float* f = reinterpret_cast<const float*>(ScrCall_ScrEnt_GetOrigin(&facing, vec));
        const float to[2] = {f[0], f[1]};
        const float* t = reinterpret_cast<const float*>(ScrCall_ScrEnt_GetOrigin(&tree, far_buffer));
        const float delta[3] = {to[0] - t[0], to[1] - t[1], 0.0f};
        uint32_t out[3];
        ScrCall_ScrEnt_SetAngles(&pivot, reinterpret_cast<const ScrVec3*>(ScrCall_Scr_VectorToAngles(out, address_of(delta))));
    }
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009224fc)(tree, pivot);                      // GScr_LinkTo
    slot<RotatePitch>(0x009227ac)(pivot, 0xc2ad0000, 0x3f8ccccd, 0x3d4ccccd, 0x3e4ccccd);  // GScr_RotatePitch -86.5, 1.1, 0.05, 0.2
    Scr_WaitTill(pivot, kRotateDone);
    slot<RotatePitch>(0x009227ac)(pivot, 0x40a00000, 0x3e570a3d, 0x3d4ccccd, 0x3e19999a);  // 5, 0.21, 0.05, 0.15
    Scr_WaitTill(pivot, kRotateDone);
    slot<RotatePitch>(0x009227ac)(pivot, 0xc0a00000, 0x3e851eb8, 0x3e19999a, 0x3dcccccd);  // -5, 0.26, 0.15, 0.1
    Scr_WaitTill(pivot, kRotateDone);
    slot<Ent>(0x0092250c)(tree);   // GScr_Unlink
    slot<Ent>(0x00922540)(pivot);  // GScr_Delete
    slot<Ent>(0x00922540)(trigger);
}

}  // namespace game
