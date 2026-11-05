#include "bro_scr_grenade.h"

#include <cstring>

#include "bro_scr_common.h"
#include "g_scr_runtime.h"
#include "scr_field.h"
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

uint32_t as_bits(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

using GetVec = uint32_t* XB_CDECL(uint32_t*, uint32_t);
using AnimScripted = void XB_CDECL(uint32_t, uint32_t, const uint32_t*, const uint32_t*, uint32_t);
using GetTagOrigin = void XB_CDECL(uint32_t, uint32_t*, ScriptString*);
using MagicGrenade = void XB_CDECL(uint32_t, const uint32_t*, const uint32_t*, float);
using MagicGrenadeDefault = void XB_CDECL(uint32_t, const uint32_t*, const uint32_t*);
using HandleNoteTracks = ScriptString* XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, ScriptString);

constexpr uint32_t kDeath = 0x06730ae8;
constexpr uint32_t kThrowNotify = 0x59414211;  // the throw anim's notify

float undefined_lane() { return as_float(title_global<uint32_t>(0x00922eb8)); }    // g_scrUndefinedVecLane
float undefined_float() { return as_float(title_global<uint32_t>(0x00922eac)); }   // g_scrUndefinedFloat

void end_on(uint32_t ent, uint32_t notify) { slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, notify); }
void queue_notify(uint32_t ent, uint32_t notify) { slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(ent, notify); }

// An undefined offset (all three lanes) becomes 0.
void zero_if_undefined(ScrVec3* offset) {
    const float u = undefined_lane();
    if (as_float(offset->v[0]) == u && as_float(offset->v[1]) == u && as_float(offset->v[2]) == u) {
        offset->v[0] = 0;
        offset->v[1] = 0;
        offset->v[2] = 0;
    }
}

// BroScr_BattleChatter (0x001c70a0) with a title string; releases it.
void battle_chatter(uint32_t ent, uint32_t text) {
    ScriptString line;
    scr_string_from(&line, text);
    title_function<void XB_CDECL(uint32_t, ScriptString)>(0x001c70a0)(ent, line);
}

uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

}  // namespace

void XB_CDECL BroScr_ThrowGrenadeAtTarget_2(uint32_t ent, ScrVec3 target, float time, uint32_t anim, ScrVec3 offset,
                                            float fuse, float yaw_offset, uint32_t hand) {
    end_on(ent, kDeath);
    zero_if_undefined(&offset);
    float yaw_add = yaw_offset;
    if (yaw_add == undefined_float()) yaw_add = 0.0f;

    uint32_t buffer[3];
    const uint32_t* o = slot<GetVec>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
    float origin[3];
    std::memcpy(origin, o, 12);
    const float delta[3] = {as_float(target.v[0]) - origin[0], as_float(target.v[1]) - origin[1],
                            as_float(target.v[2]) - origin[2]};
    const uint32_t lane = title_global<uint32_t>(0x00922eb8);
    uint32_t angles[3] = {lane, lane, lane};
    slot<void XB_CDECL(uint32_t*, const float*)>(0x00922284)(angles, delta);  // Scr_VectorToAngles
    const int32_t down = -slot<int32_t XB_CDECL(const char*)>(0x009222dc)(pointer<const char>(0x0083365c));  // "g_gravity"

    o = slot<GetVec>(0x00922b20)(buffer, ent);
    std::memcpy(origin, o, 12);
    const float start[3] = {origin[0] + as_float(offset.v[0]), origin[1] + as_float(offset.v[1]),
                            origin[2] + as_float(offset.v[2])};
    float t = time;
    if (t == undefined_float()) {
        const long double distance =
            slot<long double XB_CDECL(const float*, const float*)>(0x00922268)(start, reinterpret_cast<const float*>(target.v));  // Scr_Distance
        t = static_cast<float>(distance * platform_x87_ld(as_float(title_global<uint32_t>(0x0083f07c))));  // / 600
    }
    const float dx = as_float(target.v[0]) - start[0];
    const float dz = as_float(target.v[2]) - start[2];
    const float dy = as_float(target.v[1]) - start[1];
    const float tt = t * t;
    const float half_g = static_cast<float>(down) * as_float(title_global<uint32_t>(0x0083eb24));  // * 0.5
    const float rate = as_float(title_global<uint32_t>(0x0083eb20)) / t;                            // 1 / t
    const float velocity[3] = {rate * dx, rate * dy, (dz - tt * half_g) * rate};

    battle_chatter(ent, 0x008244e4);  // "grenadeattack"
    const uint32_t turn[3] = {0, as_bits(as_float(angles[1]) + yaw_add), 0};
    {
        uint32_t buffer2[3];
        const uint32_t* p = slot<GetVec>(0x00922b20)(buffer2, ent);  // ScrEnt_GetOrigin
        const uint32_t at[3] = {p[0], p[1], p[2]};
        slot<AnimScripted>(0x009224c0)(ent, kThrowNotify, at, turn, anim);  // GScr_AnimScriptedNoNotify
    }
    *ScrField_he5f891d2(ent) = 1;
    Scr_WaitTillArgs(ent, kThrowNotify, 0x0032aa25, 0, 0);  // the release note

    const uint32_t* const v = reinterpret_cast<const uint32_t*>(velocity);
    if (hand == title_global<uint32_t>(0x00922eb4) || hand == 0x07151174) {  // g_scrUndefinedHandle / right
        ScriptString tag;
        uint32_t start_bits[3];
        if (!(fuse == undefined_float() || static_cast<float>(0.0f > fuse ? 1 : 0) != 0.0f ||
              static_cast<float>(fuse > as_float(title_global<uint32_t>(0x0083ece8)) ? 1 : 0) != 0.0f)) {  // defined, inside [0, 5]
            scr_string_from(&tag, 0x0083c66c);  // "Bip01 R Hand"
            const uint32_t* at = ScrCall_GScr_GetTagOrigin(start_bits, &ent, address_of(&tag));
            slot<MagicGrenade>(0x0092260c)(ent, at, v, fuse);  // GScr_MagicGrenadeManual
        } else {
            scr_string_from(&tag, 0x0083c66c);
            // This branch alone has the slot call inline (into an undefined vector).
            const uint32_t lane2 = title_global<uint32_t>(0x00922eb8);
            uint32_t at[3] = {lane2, lane2, lane2};
            slot<GetTagOrigin>(0x0092261c)(ent, at, &tag);  // GScr_GetTagOrigin
            const uint32_t start[3] = {at[0], at[1], at[2]};
            slot<MagicGrenadeDefault>(0x00922610)(ent, start, v);  // GScr_MagicGrenadeManualDefaultFuse
        }
        ScriptString_Release(&tag);
    } else if (hand == 0x003798b4) {  // left
        ScriptString tag;
        uint32_t start_bits[3];
        if (!(fuse == undefined_float() || static_cast<float>(0.0f > fuse ? 1 : 0) != 0.0f ||
              static_cast<float>(fuse > as_float(title_global<uint32_t>(0x0083ece8)) ? 1 : 0) != 0.0f)) {  // defined, inside [0, 5]
            scr_string_from(&tag, 0x00831e28);  // "tag_weapon_left"
            const uint32_t* at = ScrCall_GScr_GetTagOrigin(start_bits, &ent, address_of(&tag));
            slot<MagicGrenade>(0x0092260c)(ent, at, v, fuse);
        } else {
            scr_string_from(&tag, 0x00831e28);
            const uint32_t* at = ScrCall_GScr_GetTagOrigin(start_bits, &ent, address_of(&tag));
            slot<MagicGrenadeDefault>(0x00922610)(ent, at, v);
        }
        ScriptString_Release(&tag);
    }
    *ScrField_he5f891d2(ent) = 0;
    queue_notify(ent, 0x3d8cf578);
    ScriptString none;
    ScriptString_Copy(&none, &title_global<ScriptString>(0x00922e90));
    ScriptString result;
    title_function<HandleNoteTracks>(0x001ada20)(&result, ent, kThrowNotify, 0, none);  // BroScr_HandleNoteTracks
    ScriptString_Release(&result);
}

void XB_CDECL BroScr_ThrowGrenadeAtTarget(uint32_t ent, ScrVec3 target, uint32_t anim, ScrVec3 offset, float fuse) {
    end_on(ent, kDeath);
    zero_if_undefined(&offset);

    uint32_t buffer[3];
    const uint32_t* o = slot<GetVec>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
    float origin[3];
    std::memcpy(origin, o, 12);
    const float delta[3] = {as_float(target.v[0]) - origin[0], as_float(target.v[1]) - origin[1],
                            as_float(target.v[2]) - origin[2]};
    const uint32_t lane = title_global<uint32_t>(0x00922eb8);
    uint32_t angles[3] = {lane, lane, lane};
    slot<void XB_CDECL(uint32_t*, const float*)>(0x00922284)(angles, delta);  // Scr_VectorToAngles
    const int32_t gravity = slot<int32_t XB_CDECL(const char*)>(0x009222dc)(pointer<const char>(0x0083365c));  // "g_gravity"

    o = slot<GetVec>(0x00922b20)(buffer, ent);
    std::memcpy(origin, o, 12);
    const float dz = as_float(target.v[2]) - (origin[2] + as_float(offset.v[2]));
    const float dy = as_float(target.v[1]) - (origin[1] + as_float(offset.v[1]));
    const float dx = as_float(target.v[0]) - (origin[0] + as_float(offset.v[0]));
    const float rise = static_cast<float>(
        slot<long double XB_CDECL(float)>(0x00922264)(static_cast<float>(gravity + gravity) * dz));  // Scr_Sqrt
    const float rate = as_float(title_global<uint32_t>(0x0083eb20)) / (rise / static_cast<float>(gravity));  // 1 / t
    const float velocity[3] = {rate * dx, rate * dy, rise};

    battle_chatter(ent, 0x008244e4);  // "grenadeattack"
    const uint32_t turn[3] = {0, angles[1], 0};
    {
        uint32_t buffer2[3];
        const uint32_t* p = slot<GetVec>(0x00922b20)(buffer2, ent);  // ScrEnt_GetOrigin
        const uint32_t at[3] = {p[0], p[1], p[2]};
        slot<AnimScripted>(0x009224c0)(ent, kThrowNotify, at, turn, anim);  // GScr_AnimScriptedNoNotify
    }
    *ScrField_he5f891d2(ent) = 1;
    Scr_WaitTillArgs(ent, kThrowNotify, 0x0032aa25, 0, 0);  // the release note

    ScriptString tag;
    if (!(fuse == undefined_float() || static_cast<float>(0.0f > fuse ? 1 : 0) != 0.0f ||
              static_cast<float>(fuse > as_float(title_global<uint32_t>(0x0083ece8)) ? 1 : 0) != 0.0f)) {  // defined, inside [0, 5]
        scr_string_from(&tag, 0x0083c66c);  // "Bip01 R Hand"
        const uint32_t lane2 = title_global<uint32_t>(0x00922eb8);
        uint32_t at[3] = {lane2, lane2, lane2};
        slot<GetTagOrigin>(0x0092261c)(ent, at, &tag);  // GScr_GetTagOrigin
        const uint32_t start[3] = {at[0], at[1], at[2]};
        slot<MagicGrenade>(0x0092260c)(ent, start, reinterpret_cast<const uint32_t*>(velocity), fuse);  // GScr_MagicGrenadeManual
    } else {
        scr_string_from(&tag, 0x0083c66c);
        const uint32_t lane2 = title_global<uint32_t>(0x00922eb8);
        uint32_t at[3] = {lane2, lane2, lane2};
        slot<GetTagOrigin>(0x0092261c)(ent, at, &tag);
        const uint32_t start[3] = {at[0], at[1], at[2]};
        slot<MagicGrenadeDefault>(0x00922610)(ent, start, reinterpret_cast<const uint32_t*>(velocity));  // ..DefaultFuse
    }
    ScriptString_Release(&tag);
    *ScrField_he5f891d2(ent) = 0;
    queue_notify(ent, 0x3d8cf578);
    ScriptString none;
    ScriptString_Copy(&none, &title_global<ScriptString>(0x00922e90));
    ScriptString result;
    title_function<HandleNoteTracks>(0x001ada20)(&result, ent, kThrowNotify, 0, none);  // BroScr_HandleNoteTracks
    ScriptString_Release(&result);
}

void XB_CDECL BroScr_ThrowGrenadeRightHand(uint32_t ent, ScrVec3 target, float time, uint32_t anim, ScrVec3 offset,
                                           float fuse) {
    BroScr_ThrowGrenadeAtTarget_2(ent, target, time, anim, offset, fuse, 0.0f, 0x07151174);
}

void XB_CDECL BroScr_ThrowGrenadeByPose(uint32_t ent, ScrVec3 target, float fuse) {
    using GetInt = uint32_t XB_CDECL(uint32_t);
    uint32_t anim;
    float height;
    if (slot<GetInt>(0x00922dc0)(ent) == 0x070b89a4) {  // ScrActor_GetInt_184: standing
        anim = title_global<uint32_t>(0x008cf4b0);       // g_anim_stand_grenade_throw
        height = as_float(title_global<uint32_t>(0x0083ecdc));  // 72
    } else if (slot<GetInt>(0x00922dc0)(ent) == 0xc1165808) {  // crouching
        anim = title_global<uint32_t>(0x008ce5c8);              // g_anim_crouch_grenade_throw
        height = as_float(title_global<uint32_t>(0x007f5cfc));  // 65
    } else {
        return;
    }
    ScrVec3 offset;
    offset.v[0] = 0;
    offset.v[1] = 0;
    offset.v[2] = as_bits(height);
    BroScr_ThrowGrenadeAtTarget(ent, target, anim, offset, fuse);
}

}  // namespace game
