#include "bro_scr_ai_look.h"

#include "bro_scr_ai_common.h"
#include "g_scr_runtime.h"
#include "scr_access.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "script_string.h"
#include "title_data.h"
#include "bro_scr_actor.h"
#include "bro_scr_ai.h"

namespace game {

using namespace bro_ai;

namespace {

// The look timer (0x644536d0) still running: interrupts other than `allowed` (or undefined) wait.
bool look_blocked(uint32_t ent, uint32_t interrupt, uint32_t allowed) {
    if (interrupt == undefined_handle() || interrupt == allowed) return false;
    const uint32_t* const timer = int_slot(ent, 0x644536d0);
    return get_time() <= static_cast<int32_t>(*timer);
}

// Look rate 0x5e1a2a0c by speed, then the look notify by type.
void look_rate_and_notify(uint32_t ent, uint32_t speed, uint32_t notify_type) {
    *int_slot(ent, 0x5e1a2a0c) = speed == 0xc028584c ? 0x320 : 0x640;
    queue_notify(ent, notify_type != undefined_handle() && notify_type == 0xa3c65328 ? 0x758409ea : 0x189e36ec);
}

void store_vector(uint32_t* dst, uint32_t x, uint32_t y, uint32_t z) {
    dst[0] = x;
    dst[1] = y;
    dst[2] = z;
}

// Built and dropped (the assert using it is compiled out).
void drop_string(uint32_t text) {
    ScriptString s;
    ScriptString_FromCString(&s, pointer<const char>(text));
    ScriptString_Release(&s);
}

int32_t delay_until() {
    const int32_t spread = at<int32_t>(anim_object(), 0x8);
    const int32_t base = at<int32_t>(anim_object(), 0x18c) + get_time();
    return random_int(spread) + base;
}

bool in_list(const uint32_t* list, size_t count, uint32_t value) {
    for (size_t i = 0; i < count; ++i)
        if (list[i] == value) return true;
    return false;
}

const uint32_t kDelayScripts[] = {0x0001894d, 0xc0c70454, 0x0705b194, 0x3124fe8f, 0xa1cb941d,
                                  0x38b92491, 0x38948440, 0x388a1c90, 0x2aa2fdb3, 0xd8651488,
                                  0x812b5b15, 0xd8562c89, 0x9c8414fc, 0x91649867, 0x689c5825};
const uint32_t kKeepScripts[] = {0x0001c6ce, 0x00385d90, 0x003a09eb, 0x0035a8e5, 0x0039894e,
                                 0x06730ae8, 0x22a9df44, 0x00353554, 0xef68c4d4, 0xfe429dc3};

}  // namespace

void XB_CDECL BroScr_LookAtPosition(uint32_t ent, uint32_t x, uint32_t y, uint32_t z, float time, uint32_t speed,
                                    uint32_t notify_type, uint32_t interrupt) {
    ScriptString text;
    ScriptString_FromCString(&text, pointer<const char>(0x00825e60));  // "Can only call this function on an AI character"
    slot<uint32_t XB_CDECL(uint32_t)>(0x0092221c)(ent);                 // GScr_IsAI
    ScriptString_Release(&text);
    ScriptString_FromCString(&text, pointer<const char>(0x00825e20));  // "LookAtPosition called on AI that lookThread ..."
    int_slot(ent, 0xfa9a8384);
    ScriptString_Release(&text);
    drop_string(0x00825dfc);  // "lookSpeed must be casual or alert"
    if (look_blocked(ent, interrupt, 0x00825de8)) return;
    *handle_slot(ent, 0x42f377f3) = 0xd915114e;
    store_vector(reinterpret_cast<uint32_t*>(ScrField_hb217897f(ent)), x, y, z);
    const float ms = time * title_global<float>(0x0083eb9c);  // 1000
    const int32_t now = get_time();
    const float until = static_cast<float>(now) + ms;
    uint32_t* const timer = int_slot(ent, 0x644536d0);
    *timer = float_to_script_int(until);
    look_rate_and_notify(ent, speed, notify_type);
}

void XB_CDECL BroScr_CachePerTeamValues(uint32_t ent) {
    title_global<uint32_t>(0x00922fa0) = *int_slot(ent, 0xad8f34a0);
    ScriptString team;
    uint32_t hash_out;
    const uint32_t hash = *Scr_HashScriptString(
        &hash_out, reinterpret_cast<const uint8_t* const*>(ScrCall_ScrSentient_GetTeam(&ent, &team)));
    ScriptString_Release(&team);
    uint32_t offset;
    switch (hash) {
        case 0x00316d53: offset = 0x1c; break;
        case 0x000189ec: offset = 0xf8; break;
        case 0x4d6bc457: offset = 0xd0; break;
        case 0xc4f7a8d3: offset = 0x74; break;
        default: return;
    }
    const uint8_t* const obj = anim_object();
    title_global<uint32_t>(0x00922fa4) = at<uint32_t>(obj, offset);
    title_global<uint32_t>(0x00922fa8) = at<uint32_t>(obj, offset + 4);
    title_global<uint32_t>(0x00922fb0) = at<uint32_t>(obj, offset + 0xc);
    title_global<uint32_t>(0x00922fac) = at<uint32_t>(obj, offset + 8);
    title_global<uint32_t>(0x00922fb4) = at<uint32_t>(obj, offset);
    title_global<uint32_t>(0x00922fbc) = at<uint32_t>(obj, offset + 4);
    title_global<uint32_t>(0x00922fb8) = at<uint32_t>(obj, offset + 0xc);
}

void XB_CDECL BroScr_UpdateLookRates(uint32_t ent, uint32_t move, uint32_t pose, uint32_t speed, uint32_t stance,
                                     uint32_t arg5, uint32_t notify_type) {
    const uint32_t ninety = title_global<uint32_t>(0x0083eb64);  // 90.0f
    uint32_t rate;
    uint32_t second = 0;  // 0.0f
    if (*handle_slot(ent, 0x2181000b) == 0x0039894e || *handle_slot(ent, 0x2181000b) == 0x06730ae8) {
        rate = *key_slot(ent, 0x9a44df1e, undefined_float());
    } else {
        bool defined;
        if (*ScrField_anim_script(ent) == 0x22a9df44 && *ScrField_h9cad0b77_IsDefined(&defined, ent) &&
            *ScrField_h9cad0b77(ent) == 1) {
            rate = *ScrField_h9a44df1e(ent);
        } else if (*ScrField_anim_script(ent) == 0x22a9df44 && *ScrField_h81f1fdcb_IsDefined(&defined, ent)) {
            rate = second = ninety;
        } else if (notify_type != undefined_handle() && notify_type == 0xa3c65328) {
            rate = *ScrField_h9a44df1e(ent);
        } else if ((move == 0x070b89a4 || move == 0xc1165808) &&
                   ((stance == 0x0036b1a5 && pose == 0x00385d90) ||
                    ((stance == 0xa1cb941d || stance == 0x38948440) && speed == 0xc028584c))) {
            rate = second = ninety;
        } else {
            rate = *ScrField_h9a44df1e(ent);
        }
    }
    *key_slot(ent, 0x9a44df1e, undefined_float()) = rate;
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t)>(0x009226c8)(ent, rate, second,
                                                                           arg5);  // GScr_ActorSetLookRates
}

void XB_CDECL BroScr_MgBurstSettings(uint32_t, uint32_t ent, uint32_t* delay_min, uint32_t* delay_range,
                                     uint32_t* burst_min, uint32_t* burst_range) {
    using Factor = uint32_t* XB_CDECL(uint32_t*, ScriptString);
    const auto factor = [](uint32_t text) {
        ScriptString mode;
        ScriptString_FromCString(&mode, pointer<const char>(text));
        uint32_t out;
        return *own_function<Factor>(&BroScr_MgBurstModeFactor)(&out, mode);  // BroScr_MgBurstModeFactor (releases mode)
    };
    bool defined;
    if (*ScrField_script_delay_min_IsDefined(&defined, ent)) *delay_min = *ScrField_script_delay_min(ent);
    else *delay_min = factor(0x00839ef0);  // "delay"
    if (*ScrField_script_delay_max_IsDefined(&defined, ent)) {
        const float max = as_float(*ScrField_script_delay_max(ent));
        *reinterpret_cast<float*>(delay_range) = max - as_float(*ScrField_script_delay_min(ent));
    } else {
        *delay_range = factor(0x00825d00);  // "delay_range"
    }
    if (*ScrField_script_burst_min_IsDefined(&defined, ent)) *burst_min = *ScrField_script_burst_min(ent);
    else *burst_min = factor(0x00825cf8);  // "burst"
    if (*ScrField_script_burst_max_IsDefined(&defined, ent)) {
        const float max = as_float(*ScrField_script_burst_max(ent));
        *reinterpret_cast<float*>(burst_range) = max - as_float(*ScrField_script_burst_min(ent));
    } else {
        *burst_range = factor(0x00825cec);  // "burst_range"
    }
}

void XB_CDECL BroScr_UpdateWoundedTime(uint32_t ent, uint32_t anim, int32_t time) {
    const float ftime = static_cast<float>(time);
    const int32_t health = slot<int32_t XB_CDECL(uint32_t)>(0x00922b90)(ent);  // ScrEnt_GetHealth
    float ratio = ftime / static_cast<float>(health) * title_global<float>(0x0083ece8);  // 5
    const int32_t over = ratio > title_global<float>(0x0083eb20) ? 1 : 0;              // 1
    if (!(static_cast<float>(over) == title_global<float>(0x0083eafc))) ratio = title_global<float>(0x0083eb20);
    const int32_t now = get_time();
    *int_slot(ent, 0x32292cf8) = static_cast<uint32_t>(now + 5000);
    uint32_t* const wounded = int_slot(ent, 0x8ef2ad7c);
    const long double normal = slot<long double XB_CDECL(uint32_t)>(0x00922550)(ent);  // GScr_GetNormalHealth (ST0)
    *wounded = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int64_t>(2.0L - (normal + normal))));
    const int32_t value = static_cast<int32_t>(*int_slot(ent, 0x8ef2ad7c));
    const int32_t high = static_cast<float>(value) > title_global<float>(0x0083ed08) ? 1 : 0;  // 0.99
    if (!(static_cast<float>(high) == title_global<float>(0x0083eafc))) *int_slot(ent, 0x8ef2ad7c) = 0;

    float result;
    uint32_t* target;
    if (anim == 0x3d2dc3b7 || anim == 0x3cb25fb7 || anim == 0xedcc9bb6) {
        *handle_slot(ent, 0xfa79d674) = 0x07151174;
        const uint32_t* const h = int_slot(ent, 0x32222d6e);
        const float a = platform_mulss(title_global<float>(0x0083eb20) - ratio, static_cast<float>(static_cast<int32_t>(*h)));
        result = platform_addss(a, platform_mulss(ratio, title_global<float>(0x0083eb34)));  // -1
        target = int_slot(ent, 0x32222d6e);
    } else if (anim == 0x1a2ddf3d || anim == 0x1bb2433d || anim == 0x6dc2dea5) {
        *handle_slot(ent, 0xfa79d674) = 0x003798b4;
        const uint32_t* const h = int_slot(ent, 0x32222d6e);
        const float a = platform_mulss(title_global<float>(0x0083eb20) - ratio, static_cast<float>(static_cast<int32_t>(*h)));
        result = platform_addss(a, ratio);
        target = int_slot(ent, 0x32222d6e);
    } else {
        *ScrField_hfa79d674(ent) = 0x0769ccd2;
        const float hf = static_cast<float>(static_cast<int32_t>(*ScrField_h32222d6e(ent)));
        const float a = platform_mulss(hf, title_global<float>(0x0083eb20) - ratio);
        result = platform_addss(a, platform_mulss(ratio, title_global<float>(0x0083eafc)));  // x 0
        target = ScrField_h32222d6e(ent);
    }
    *target = float_to_script_int(result);
}

void XB_CDECL BroScr_LookAtEntity(uint32_t ent, uint32_t target, float time, uint32_t speed, uint32_t notify_type,
                                  uint32_t interrupt) {
    if (look_blocked(ent, interrupt, 0xcd18b019)) return;
    const bool sentient = slot<uint32_t XB_CDECL(uint32_t)>(0x00922220)(target) != 0;  // GScr_IsSentient
    *handle_slot(ent, 0x42f377f3) = sentient ? 0x9b751a35 : 0xccdbaaf9;
    *ent_slot(ent, 0xeeadf3f2) = target;
    const int32_t now = get_time();
    const int32_t until = static_cast<int32_t>(time * title_global<float>(0x0083eb9c)) + now;
    *int_slot(ent, 0x644536d0) = static_cast<uint32_t>(until);
    look_rate_and_notify(ent, speed, notify_type);
}

void XB_CDECL BroScr_LookAtPositionNoType(uint32_t ent, uint32_t x, uint32_t y, uint32_t z, float time,
                                          uint32_t speed, uint32_t notify_type, uint32_t interrupt) {
    if (look_blocked(ent, interrupt, 0x71b01aea)) return;
    store_vector(reinterpret_cast<uint32_t*>(ScrField_hb217897f(ent)), x, y, z);
    const int32_t now = get_time();
    const int32_t until = static_cast<int32_t>(time * title_global<float>(0x0083eb9c)) + now;
    *int_slot(ent, 0x644536d0) = static_cast<uint32_t>(until);
    look_rate_and_notify(ent, speed, notify_type);
}

void XB_CDECL BroScr_SetLookOverrideFields(uint32_t ent, uint32_t a, uint32_t b) {
    *handle_slot(ent, 0x81f1fdcb) = a;
    *handle_slot(ent, 0x3fd9be84) = b;
}

void XB_CDECL BroScr_DelayTimerOnScriptChange(uint32_t ent, uint32_t script) {
    if (in_list(kDelayScripts, sizeof(kDelayScripts) / 4, script)) {
        const int32_t until = delay_until();
        if (until > static_cast<int32_t>(*int_slot(ent, 0x900ed884))) *int_slot(ent, 0x900ed884) = static_cast<uint32_t>(until);
        return;
    }
    if (in_list(kKeepScripts, sizeof(kKeepScripts) / 4, script)) return;
    uint32_t ref;
    using GetRef = uint32_t* XB_THISCALL(const uint32_t*, uint32_t*);
    if (!static_cast<uint8_t>(ScrCall_Scr_EntityHandleIsValid(own_function<GetRef>(&BroScr_GetSentientRef3c)(&ent, &ref))))
        return;  // BroScr_GetSentientRef3c
    const int32_t until = delay_until();
    if (until > static_cast<int32_t>(*ScrField_h900ed884(ent))) *ScrField_h900ed884(ent) = static_cast<uint32_t>(until);
}

uint32_t* XB_CDECL BroScr_GetEnemyEyePos(uint32_t* out, uint32_t ent) {
    using GetRef = uint32_t* XB_CDECL(uint32_t*, uint32_t);
    using GetEye = void XB_CDECL(uint32_t, uint32_t*);
    uint32_t ref;
    bool stored;
    if (entity_handle_is_valid(*slot<GetRef>(0x00922e00)(&ref, ent))) {  // ScrSentient_GetRef_3c
        const uint32_t enemy = *slot<GetRef>(0x00922e00)(&ref, ent);
        uint32_t eye[3] = {undefined_lane(), undefined_lane(), undefined_lane()};
        slot<GetEye>(0x00922528)(enemy, eye);  // GScr_GetEye
        store_vector(reinterpret_cast<uint32_t*>(ScrField_h8b05303f(ent)), eye[0], eye[1], eye[2]);
        const int32_t now = get_time();
        *int_slot(ent, 0x60a41c54) = static_cast<uint32_t>(now);
        stored = true;
    } else {
        bool defined;
        stored = false;
        if (*ScrField_h60a41c54_IsDefined(&defined, ent)) {
            const int32_t now = get_time();
            stored = static_cast<int32_t>(*ScrField_h60a41c54(ent)) + 3000 < now;
        }
    }
    if (stored) {
        const uint32_t* const v = reinterpret_cast<const uint32_t*>(ScrField_h8b05303f(ent));
        store_vector(out, v[0], v[1], v[2]);
        return out;
    }
    uint32_t eye[3] = {undefined_lane(), undefined_lane(), undefined_lane()};
    slot<GetEye>(0x00922528)(ent, eye);
    using GetVec = const float* XB_CDECL(uint32_t*, uint32_t);
    const float k = title_global<float>(0x007f662c);  // 196
    uint32_t buffer[3];
    const float x = platform_addss(platform_mulss(slot<GetVec>(0x00922c68)(buffer, ent)[0], k), as_float(eye[0]));
    const float y = platform_addss(platform_mulss(slot<GetVec>(0x00922c68)(buffer, ent)[1], k), as_float(eye[1]));
    const float z = platform_addss(platform_mulss(slot<GetVec>(0x00922c68)(buffer, ent)[2], k), as_float(eye[2]));
    store_vector(out, bits(x), bits(y), bits(z));
    return out;
}

}  // namespace game

namespace game {

using namespace bro_ai;

uint8_t* XB_CDECL BroScr_TryLookAtPlayer(uint8_t* out, uint32_t actor) {
    using GetEye = void XB_CDECL(uint32_t, uint32_t*);
    *out = 0;
    if (!slot<uint32_t XB_CDECL(uint32_t)>(0x00922210)(title_global<uint32_t>(0x009236dc))) return out;  // GScr_IsAlive(player)
    if (*handle_slot(actor, 0x764918ae) == 0xc4a729a7) return out;
    uint32_t player_eye[3] = {undefined_lane(), undefined_lane(), undefined_lane()};
    slot<GetEye>(0x00922528)(title_global<uint32_t>(0x009236dc), player_eye);  // GScr_GetEye
    const uint32_t from[3] = {player_eye[0], player_eye[1], player_eye[2]};
    uint32_t actor_eye[3] = {undefined_lane(), undefined_lane(), undefined_lane()};
    slot<GetEye>(0x00922528)(actor, actor_eye);
    const uint32_t eye[3] = {actor_eye[0], actor_eye[1], actor_eye[2]};
    const uint32_t delta[3] = {bits(as_float(from[0]) - as_float(eye[0])), bits(as_float(from[1]) - as_float(eye[1])),
                               bits(as_float(from[2]) - as_float(eye[2]))};
    const float length_squared =
        static_cast<float>(slot<long double XB_CDECL(const uint32_t*)>(0x00922274)(delta));  // Scr_LengthSquared (ST0)
    if (*ScrField_anim_script(actor) == 0x22a9df44) return out;
    if (*ScrField_anim_script(actor) == 0xef68c4d4) return out;
    const uint32_t* const next = ScrField_h644536d0(actor);
    if (!(get_time() > static_cast<int32_t>(*next))) return out;
    if (!(as_float(at<uint32_t>(anim_object(), 0x60)) > length_squared)) return out;
    const long double seen = slot<long double XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x009226d8)(
        actor, title_global<uint32_t>(0x009236dc), 0x41200000);  // GScr_ActorCanSee(actor, player, 10)
    if (seen == 0.0L) return out;
    uint32_t to_player[3] = {bits(as_float(eye[0]) - as_float(from[0])), bits(as_float(eye[1]) - as_float(from[1])),
                             bits(as_float(eye[2]) - as_float(from[2]))};
    uint32_t angles[3];
    ScrCall_ScrEnt_GetAngles(pointer<const uint32_t>(0x009236dc), angles);
    ScrCall_Scr_VectorToAngles(angles, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(to_player)));  // unused
    const float time = static_cast<float>(
        slot<long double XB_CDECL(uint32_t, uint32_t)>(0x00922238)(0x3f400000, 0x3fc00000));  // Scr_RandomFloatRange(0.75, 1.5)
    BroScr_LookAtEntity(actor, title_global<uint32_t>(0x009236dc), time, 0xc028584c, 0, 0x0bc350d7);
    *out = 1;
    return out;
}

}  // namespace game
