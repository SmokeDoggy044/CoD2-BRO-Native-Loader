#include "bro_scr_wait_misc.h"

#include <cstring>

#include "bro_scr_death_watch.h"
#include "g_scr_runtime.h"
#include "g_script.h"
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

using GetVec = uint32_t* XB_CDECL(uint32_t*, uint32_t);

constexpr uint32_t kDeath = 0x06730ae8;

void queue_notify(uint32_t ent, uint32_t notify) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(ent, notify);  // Scr_QueueNotify
}
void stop_anim_scripted(uint32_t ent) { slot<void XB_CDECL(uint32_t)>(0x009224c8)(ent); }  // GScr_StopAnimScripted
uint32_t hash_name(uint32_t text) {
    uint32_t hash;
    return *Scr_HashName(&hash, pointer<const char>(text));
}

// The level entity's handle word (+4 of the level object 0x00922ee0, read through null).
const uint32_t* level_handle_word() {
    const uint32_t object = title_global<uint32_t>(0x00922ee0);
    return pointer<const uint32_t>(object ? object + 4 : 0);
}

ScriptKeyTable* script_object_or_empty(uint32_t ent) {
    const uint32_t object = title_function<uint32_t XB_CDECL(uint32_t)>(0x0011db20)(ent);  // GScr_GetEntScriptObject
    return object ? pointer<ScriptKeyTable>(object) : &title_global<ScriptKeyTable>(0x00922e94);
}

// The entity's field `key`, added as `missing` when absent.
uint32_t* field(uint32_t ent, uint32_t key, uint32_t missing) {
    ScriptKeyTable* const table = script_object_or_empty(ent);
    if (uint32_t* value = ScriptKeyTable_Find(table, key)) return value;
    return ScriptKeyTable_Set(table, key, missing);
}

}  // namespace

void XB_CDECL BroScr_DeathPhysicsLaunch_2(uint32_t ent, ScrVec3 angles, uint32_t launch_arg, ScrVec3 force, float delay) {
    float turned[3];  // the original rewrites the angle arguments in place
    turned[0] = as_float(angles.v[0]) * as_float(title_global<uint32_t>(0x0083eb34));  // * -1
    std::memcpy(&turned[1], &angles.v[1], 8);
    const uint32_t undefined = title_global<uint32_t>(0x00922eb8);  // g_scrUndefinedVecLane
    uint32_t forward[3] = {undefined, undefined, undefined};
    slot<void XB_CDECL(uint32_t*, const float*)>(0x00922290)(forward, turned);  // Scr_AnglesToForward
    const uint32_t launch[3] = {forward[0], forward[1], forward[2]};
    using GetEntVar = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t);
    slot<GetEntVar>(0x009221c0)(ent, kDeath, hash_name(0x007f51b0));  // GScr_GetEntVar "_spawner_gmi::Axis_Death"
    slot<GetEntVar>(0x009221c0)(ent, kDeath, hash_name(0x007f5194));  // "_spawner_gmi::Allies_Death"
    const float u = as_float(title_global<uint32_t>(0x00922eb8));
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    const void* at = &force;
    if (!(as_float(force.v[0]) != u || as_float(force.v[1]) != u || as_float(force.v[2]) != u)) at = zero;
    slot<void XB_CDECL(uint32_t, const void*, const uint32_t*, uint32_t, uint32_t)>(0x009228dc)(ent, at, launch,
                                                                                              launch_arg, 0);  // GScr_PhysicsLaunch
    if (delay == as_float(title_global<uint32_t>(0x00922eac)))  // g_scrUndefinedFloat
        Scr_Wait(as_float(0x3dcccccd));                          // 0.1
    else
        Scr_Wait(delay);
    uint32_t buffer[3];
    const uint32_t* o = slot<GetVec>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
    const uint32_t now[3] = {o[0], o[1], o[2]};
    const float amount = static_cast<float>(slot<int32_t XB_CDECL(uint32_t)>(0x00922b90)(ent) + 50);  // ScrEnt_GetHealth
    slot<void XB_CDECL(uint32_t, float, const uint32_t*)>(0x00922558)(ent, amount, now);  // GScr_DoDamage
}

void XB_CDECL BroScr_DeathPhysicsLaunchNoForce(uint32_t ent, uint32_t attacker, uint32_t launch_arg) {
    ScrVec3 force;
    force.v[0] = title_global<uint32_t>(0x00922eb8);  // g_scrUndefinedVecLane
    force.v[1] = title_global<uint32_t>(0x00922eb8);
    force.v[2] = title_global<uint32_t>(0x00922eb8);
    BroScr_DeathPhysicsLaunch(ent, attacker, launch_arg, force);
}

void XB_CDECL BroScr_DeathPhysicsLaunchFromTag(uint32_t ent, uint32_t attacker, uint32_t launch_arg, ScriptString tag) {
    const uint32_t undefined = title_global<uint32_t>(0x00922eb8);
    ScrVec3 at;
    at.v[0] = undefined;
    at.v[1] = undefined;
    at.v[2] = undefined;
    slot<void XB_CDECL(uint32_t, ScrVec3*, ScriptString*)>(0x0092261c)(ent, &at, &tag);  // GScr_GetTagOrigin
    BroScr_DeathPhysicsLaunch(ent, attacker, launch_arg, at);
    ScriptString_Release(&tag);
}

void XB_CDECL BroScr_LerpPlayerAnglesCall(uint32_t player, float pitch, float yaw, float roll, float time) {
    BroScr_LerpPlayerAngles(player, pitch, yaw, roll, time, 1);
}

void XB_CDECL BroScr_LerpPlayerAnglesCall_2(uint32_t player, float pitch, float yaw, float roll, float time) {
    BroScr_LerpPlayerAngles_2(player, pitch, yaw, roll, time, 1);
}

void XB_CDECL BroScr_WaitForNotifyOrTimeout(uint32_t, ScrVec3, ScriptString unused_string, ScrVec3, ScriptString notify,
                                            ScriptString endon, float timeout) {
    if (endon.rep) Scr_EndOnString(*level_handle_word(), endon.rep->text);
    if (notify.rep) {
        const uint32_t* level = level_handle_word();
        uint32_t hash;
        const uint32_t value = *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&notify));
        queue_notify(*level, value);
        const uint32_t handle = *level_handle_word();
        Scr_EndOnString(handle, notify.rep ? notify.rep->text : nullptr);
    }
    if (timeout == as_float(title_global<uint32_t>(0x00922eac))) {  // g_scrUndefinedFloat: wait forever
        for (;;) Scr_WaitFrames(0);
    }
    using GetTime = int32_t XB_CDECL();
    const float ms = timeout * as_float(title_global<uint32_t>(0x0083eb9c));  // * 1000
    const float end = static_cast<float>(slot<GetTime>(0x009223c4)()) + ms;   // GScr_GetTime
    // The script's comparison goes through a float truth value.
    while (static_cast<float>(end > static_cast<float>(slot<GetTime>(0x009223c4)()) ? 1 : 0) != 0.0f) Scr_WaitFrames(0);
    ScriptString_Release(&unused_string);
    ScriptString_Release(&notify);
    ScriptString_Release(&endon);
}

void XB_CDECL BroScr_AnimReachGuyThread(uint32_t ent, uint32_t other) {
    using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                          const uint32_t*);
    title_function<StartThread>(0x0045a620)(0, 0, 0, 0, 0x0011fc20, *level_handle_word(), &ent,
                                            &other);  // Scr_StartThread_ent_ent_ent (BroScr_AnimReachNotifyOnDeath)
    const float radius = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922df0)(ent));  // ScrSentient_GetFloat_18
    uint32_t* const saved = field(ent, 0x25bf26c6, title_global<uint32_t>(0x00922eac));
    std::memcpy(saved, &radius, 4);
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922df4)(ent, 0);  // ScrSentient_SetFloat_18
    Scr_WaitTill(ent, 0x0032304c);
    queue_notify(other, 0x2253e8ee);
}

void XB_CDECL BroScr_LimitSameAnimation(uint32_t ent) {
    Scr_Wait(as_float(0x3d4ccccd));  // 0.05
    uint32_t* count = field(ent, 0x06f63273, title_global<uint32_t>(0x00922ec0));  // g_scrUndefinedInt
    *count = *count + 1;
    count = field(ent, 0x06f63273, title_global<uint32_t>(0x00922ec0));
    if (static_cast<int32_t>(*count) > 1) {
        ScriptString text;
        ScriptString_FromCString(&text, pointer<const char>(0x008245bc));  // "Too many of same animation playing"
        ScrLocal_ReleaseString_2(text);
    }
}

void XB_CDECL BroScr_DecrementCounterOnNotify(uint32_t object, uint32_t ent, uint32_t notify) {
    Scr_EndOnString(ent, pointer<const char>(0x00831f60));  // "death"
    Scr_WaitTill(object, notify);
    uint32_t* const count = field(ent, 0x06f63273, title_global<uint32_t>(0x00922ec0));
    *count = *count - 1;
}

void XB_CDECL BroScr_VehicleRiderDeathWatch(uint32_t vehicle, uint32_t rider) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(rider, 0x9966dcdf);  // Scr_EndOn
    Scr_WaitTill(rider, kDeath);
    uint32_t* const riders = field(vehicle, 0xb330b301, title_global<uint32_t>(0x00922ec0));
    *riders = *riders - 1;
    stop_anim_scripted(rider);
}

void XB_CDECL BroScr_VehicleRiderUnlinkOnDeath(uint32_t vehicle, uint32_t rider) {
    const uint32_t args[6] = {0, 0, 0, 0, 0x001205b0, rider};  // BroScr_KillWhenHealthLow
    Scr_StartThreadFunctor1(args);
    Scr_WaitTill(rider, kDeath);
    queue_notify(vehicle, 0xa05165ad);
    slot<void XB_CDECL(uint32_t)>(0x0092250c)(rider);  // GScr_Unlink
}

void XB_CDECL BroScr_VehicleRiderDeathWatch_2(uint32_t vehicle, uint32_t rider) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(vehicle, 0x996a3647);  // Scr_EndOn
    Scr_WaitTill(rider, kDeath);
    uint32_t* const riders = field(vehicle, 0xb330b301, title_global<uint32_t>(0x00922ec0));
    *riders = *riders - 1;
    stop_anim_scripted(rider);
    queue_notify(vehicle, 0x996a3647);
}

}  // namespace game
