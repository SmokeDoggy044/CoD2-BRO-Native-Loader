#include "bro_scr_death_watch.h"

#include <cstring>

#include "bro_scr_cover_turret.h"
#include "g_scr_runtime.h"
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

void queue_notify(uint32_t ent, uint32_t notify) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(ent, notify);  // Scr_QueueNotify
}
int32_t health(uint32_t ent) { return slot<int32_t XB_CDECL(uint32_t)>(0x00922b90)(ent); }  // ScrEnt_GetHealth
void set_health(uint32_t ent, int32_t value) {
    slot<void XB_CDECL(uint32_t, int32_t)>(0x00922b94)(ent, value);  // ScrEnt_SetHealth
}
uint32_t hash_name(uint32_t text) {
    uint32_t hash;
    return *Scr_HashName(&hash, pointer<const char>(text));
}
uint32_t hash_string(const ScriptString* string) {
    uint32_t hash;
    return *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(string));
}

// The level entity's handle word (+4 of the level object 0x00922ee0, read through null).
const uint32_t* level_handle_word() {
    const uint32_t object = title_global<uint32_t>(0x00922ee0);
    return pointer<const uint32_t>(object ? object + 4 : 0);
}

// "flak died with flaknum <flaknum>" (the caller releases it).
void flak_died_name(ScriptString* out, int32_t flaknum) {
    ScriptString_FromCString(out, pointer<const char>(0x00824344));
    ScriptString_AppendInt(out, flaknum);
}

void set_goal_radius(uint32_t ent, uint32_t radius) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922df4)(ent, radius);  // ScrSentient_SetFloat_18
}

using Abs = long double XB_CDECL(float);  // Scr_Abs: ST0, compared at the x87's precision

// BroScr_LerpPlayerAngles(_2): pitch brought into [-90, 90] by 180s (sign through a float truth
// value), the yaw delta into [-180, 180] by 360s (sign from the first delta, kept), then the
// player's angles moved over `time` (linear, or eased by (cos(f*pi - pi) + 1) / 2).
inline void lerp_player_angles(uint32_t player, float pitch, float yaw, float roll, float time, uint32_t release,
                               bool eased) {
    float elapsed = 0.0f;
    while (slot<Abs>(0x00922260)(pitch) > as_float(title_global<uint32_t>(0x0083eb64))) {  // > 90
        const float sign = static_cast<float>(pitch > 0.0f ? 1 : 0) != 0.0f ? as_float(title_global<uint32_t>(0x0083eb20))
                                                                           : as_float(title_global<uint32_t>(0x0083eb34));
        pitch = static_cast<float>(static_cast<long double>(pitch) -
                                   static_cast<long double>(sign) * as_float(title_global<uint32_t>(0x0083eb60)));  // 180
    }
    uint32_t buffer[3];
    const uint32_t* a = slot<GetVec>(0x00922800)(buffer, player);  // GScr_PlayerGetAngles
    const uint32_t start[3] = {a[0], a[1], a[2]};
    const float dx = pitch - as_float(start[0]);
    const float dy = yaw - as_float(start[1]);
    const float dz = roll - as_float(start[2]);
    float dy_wrapped = dy;  // the original keeps it in the yaw argument's slot
    if (slot<Abs>(0x00922260)(dy) > as_float(title_global<uint32_t>(0x0083eb60))) {  // > 180
        do {
            const float sign = dy > as_float(title_global<uint32_t>(0x0083eafc)) ? as_float(title_global<uint32_t>(0x0083eb20))
                                                                                : as_float(title_global<uint32_t>(0x0083eb34));
            dy_wrapped = static_cast<float>(static_cast<long double>(dy_wrapped) -
                                            static_cast<long double>(sign) * as_float(title_global<uint32_t>(0x0083ebcc)));  // 360
        } while (slot<Abs>(0x00922260)(dy_wrapped) > as_float(title_global<uint32_t>(0x0083eb60)));
    }
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922844)(player, 1);  // GScr_PlayerSetInt_58c
    const float rate = as_float(title_global<uint32_t>(0x0083eb20)) / time;
    using FrameTime = long double XB_CDECL();
    while (static_cast<float>(time > elapsed ? 1 : 0) != 0.0f) {
        Scr_WaitFrames(0);
        elapsed = static_cast<float>(slot<FrameTime>(0x009223c8)() + elapsed);  // GScr_GetFrameTime (ST0)
        float f = rate * elapsed;
        if (eased) {
            const float pi = as_float(title_global<uint32_t>(0x00824410));
            f = static_cast<float>((slot<long double XB_CDECL(float)>(0x00922244)(f * pi - pi) +
                                    as_float(title_global<uint32_t>(0x0083eb20))) *
                                   as_float(title_global<uint32_t>(0x0083eb24)));  // (Scr_Cos + 1) * 0.5
        }
        float angles[3];
        angles[0] = f * dx + as_float(start[0]);
        angles[1] = f * dy_wrapped + as_float(start[1]);
        angles[2] = dz * f + as_float(start[2]);
        slot<void XB_CDECL(uint32_t, const float*)>(0x009227fc)(player, angles);  // GScr_PlayerSetAngles
    }
    if (static_cast<uint8_t>(release)) slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922844)(player, 0);
}

}  // namespace

void XB_CDECL BroScr_DeathPhysicsLaunch(uint32_t ent, uint32_t attacker, uint32_t launch_arg, ScrVec3 force) {
    uint32_t buffer1[3], buffer2[3];
    const uint32_t* o = slot<GetVec>(0x00922b20)(buffer1, ent);  // ScrEnt_GetOrigin
    const uint32_t origin[3] = {o[0], o[1], o[2]};
    o = slot<GetVec>(0x00922b20)(buffer2, attacker);
    const uint32_t from[3] = {o[0], o[1], o[2]};
    float dir[3];
    dir[0] = as_float(from[0]) - as_float(origin[0]);
    dir[1] = as_float(from[1]) - as_float(origin[1]);
    dir[2] = as_float(from[2]) - as_float(origin[2]);
    const uint32_t undefined = title_global<uint32_t>(0x00922eb8);  // g_scrUndefinedVecLane
    uint32_t normal[3] = {undefined, undefined, undefined};
    slot<void XB_CDECL(uint32_t*, const float*)>(0x00922280)(normal, dir);  // Scr_VectorNormalize
    float launch[3];
    std::memcpy(launch, normal, sizeof(launch));
    launch[2] = slot<float XB_CDECL(float)>(0x00922260)(launch[2]);  // Scr_Abs
    using GetEntVar = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t);
    slot<GetEntVar>(0x009221c0)(ent, 0x06730ae8, hash_name(0x007f51b0));  // GScr_GetEntVar "_spawner_gmi::Axis_Death"
    slot<GetEntVar>(0x009221c0)(ent, 0x06730ae8, hash_name(0x007f5194));  // "_spawner_gmi::Allies_Death"
    const float u = as_float(title_global<uint32_t>(0x00922eb8));
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    const void* at = &force;
    if (!(as_float(force.v[0]) != u || as_float(force.v[1]) != u || as_float(force.v[2]) != u)) at = zero;
    slot<void XB_CDECL(uint32_t, const void*, const float*, uint32_t, uint32_t)>(0x009228dc)(ent, at, launch, launch_arg,
                                                                                           0);  // GScr_PhysicsLaunch
    Scr_Wait(as_float(0x3dcccccd));  // 0.1
    uint32_t buffer3[3];
    o = slot<GetVec>(0x00922b20)(buffer3, ent);
    const uint32_t now[3] = {o[0], o[1], o[2]};
    const float amount = static_cast<float>(health(ent) + 50);
    slot<void XB_CDECL(uint32_t, float, const uint32_t*)>(0x00922558)(ent, amount, now);  // GScr_DoDamage
}

void XB_CDECL BroScr_DeleteAfter5s(uint32_t ent) {
    Scr_Wait(5.0f);
    slot<void XB_CDECL(uint32_t)>(0x00922540)(ent);  // GScr_Delete
}

void XB_CDECL BroScr_SetDeadQuote() {
    Scr_EndOnString(*level_handle_word(), pointer<const char>(0x008242e4));  // "mine death"
    Scr_WaitTillString(pointer<uint32_t>(title_global<uint32_t>(0x00922ee0))[0x7f8 / 4],
                       pointer<const char>(0x00831f60));  // the player's "death"
    if (*pointer<uint8_t>(title_global<uint32_t>(0x00922ee0) + 0xf0)) return;
    // "@DEADQUOTE_..." strings.
    const uint32_t quotes[42] = {
        0x008242c4, 0x008242a0, 0x00824280, 0x0082425c, 0x00824234, 0x00824214, 0x008241f0, 0x008241cc, 0x008241ac,
        0x0082418c, 0x00824168, 0x00824148, 0x00824124, 0x00824100, 0x008240e0, 0x008240b8, 0x00824094, 0x00824074,
        0x00824050, 0x00824030, 0x00824010, 0x00823fec, 0x00823fc8, 0x00823fa4, 0x00823f80, 0x00823f5c, 0x00823f3c,
        0x00823f1c, 0x00823ef8, 0x00823ed4, 0x00823eb4, 0x00823e8c, 0x00823e68, 0x00823e44, 0x00823e20, 0x00823e00,
        0x00823ddc, 0x00823db8, 0x00823d94, 0x00823d70, 0x00823d50, 0x00823d28};
    const int32_t i = slot<int32_t XB_CDECL(int32_t)>(0x0092222c)(42);  // Scr_RandomInt
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009222e4)(0x00823d18, quotes[i]);  // GScr_SetCvar "ui_deadquote"
}

void XB_CDECL BroScr_ObjectiveTrackerManager(ScriptString name, int32_t index, ScriptString text, int32_t count,
                                             ScriptString endon) {
    if (endon.rep) Scr_EndOnString(*level_handle_word(), endon.rep->text);
    ScriptString temp;
    using GetEnt = uint32_t XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t);
    const uint32_t manager = slot<GetEnt>(0x009221c4)(
        ScriptString_ConcatCString(&temp, &name, pointer<const char>(0x00824304)), 0x2d36ed4e, 0, 0, 0);  // "_manager"
    ScriptString_Release(&temp);
    ScriptString state;
    ScriptString_FromCString(&state, pointer<const char>(0x0083aecc));  // "active"
    slot<void XB_CDECL(int32_t, ScriptString*, ScriptString*)>(0x009222a4)(index, &state, &text);  // GScr_ObjectiveAdd
    ScriptString_Release(&state);
    using SetString = void XB_CDECL(int32_t, ScriptString*, int32_t);
    int32_t left = count;
    slot<SetString>(0x009222c0)(index, &text, left);             // GScr_ObjectiveStringLocalized
    slot<void XB_CDECL(int32_t)>(0x009222d0)(index);            // GScr_ObjectiveCurrent
    *pointer<uint8_t>(title_global<uint32_t>(0x00922ee0) + 0x2d0) = 0;
    while (left > 0) {
        Scr_WaitTill(manager, 0x67979261);
        --left;
        slot<SetString>(0x009222c0)(index, &text, left);
    }
    *pointer<uint8_t>(title_global<uint32_t>(0x00922ee0) + 0x2d0) = 1;
    // The original builds "objective<index>" in its second argument's slot.
    ScriptString objective;
    ScriptString done;
    ScriptString_ConcatCString(&done,
                               ScriptString_FromCStringPlusInt(&objective, pointer<const char>(0x0082dc78), index),
                               pointer<const char>(0x008242f0));  // "objective" .. "_tracker_complete"
    ScriptString_Release(&objective);
    const uint32_t* level = level_handle_word();
    const uint32_t notify = hash_string(&done);
    queue_notify(*level, notify);
    ScriptString_Release(&done);
    ScriptString_Release(&name);
    ScriptString_Release(&text);
    ScriptString_Release(&endon);
}

void XB_CDECL BroScr_FlakPanzerStopLoopSounds(uint32_t ent, int32_t flaknum) {
    ScriptString died;
    flak_died_name(&died, flaknum);
    const uint32_t* level = level_handle_word();
    const uint32_t notify = hash_string(&died);
    Scr_WaitTill(*level, notify);
    queue_notify(ent, hash_name(0x00824330));  // "flakpanzer_loopoff"
    queue_notify(ent, hash_name(0x00824318));  // "flakpanzer_loop_fastoff"
    ScriptString_Release(&died);
}

void XB_CDECL BroScr_EarthquakeUntilBroken(uint32_t ent) {
    Scr_EndOnString(ent, pointer<const char>(0x0082435c));  // "broken"
    for (;;) {
        uint32_t buffer[3];
        const uint32_t* o = slot<GetVec>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
        const uint32_t origin[3] = {o[0], o[1], o[2]};
        slot<void XB_CDECL(uint32_t, uint32_t, const uint32_t*, uint32_t)>(0x00922444)(0x3d4ccccd, 0x40000000, origin,
                                                                                      0x451c4000);  // GScr_Earthquake
        Scr_Wait(1.0f);
    }
}

void XB_CDECL BroScr_EndSequenceOnDeath(uint32_t self, uint32_t ent) {
    set_health(ent, 1);
    Scr_WaitTillString(ent, pointer<const char>(0x00831f60));  // "death"
    queue_notify(self, hash_name(0x00824364));                // "end_sequence"
}

void XB_CDECL BroScr_FlakWaitDeath(uint32_t ent, ScrVec3 pos, int32_t flaknum) {
    Scr_EndOnString(ent, pointer<const char>(0x00831f60));  // "death"
    slot<void XB_CDECL(uint32_t, ScrVec3*, uint32_t, uint32_t)>(0x009226f8)(ent, &pos, 0, 0);  // GScr_ActorTryTeleport
    Scr_Wait(1.0f);
    slot<void XB_CDECL(uint32_t, ScrVec3*)>(0x00922684)(ent, &pos);  // GScr_SetGoalPos
    set_goal_radius(ent, 0x43480000);                                // 200
    ScriptString target;
    const bool none = ScrCall_ScrEnt_GetTarget(&ent, &target)->rep == nullptr;
    ScriptString_Release(&target);
    if (none) return;
    ScriptString died;
    flak_died_name(&died, flaknum);
    const uint32_t* level = level_handle_word();
    const uint32_t notify = hash_string(&died);
    Scr_WaitTill(*level, notify);
    ScriptString key;
    ScriptString_FromCString(&key, pointer<const char>(0x0082dda8));  // "targetname"
    ScriptString value;
    using GetNode = uint32_t XB_CDECL(ScriptString*, ScriptString*, uint32_t, uint32_t);
    uint32_t node = slot<GetNode>(0x009221cc)(ScrCall_ScrEnt_GetTarget(&ent, &value), &key, 0, 0);  // GScr_GetNode
    if (node == 0xffffffff) node = 0;
    ScriptString_Release(&value);
    ScriptString_Release(&key);
    slot<void XB_CDECL(uint32_t, uint32_t*)>(0x00922688)(ent, &node);  // GScr_SetGoalPosFromNode
    uint8_t valid;
    if (*BroScr_IsValidHandle(&valid, node)) {
        float radius;
        uint32_t bits;
        std::memcpy(&bits, BroScr_GetNodeRadius(&radius, node), 4);
        set_goal_radius(ent, bits);
    } else {
        set_goal_radius(ent, 0x43000000);  // 128
    }
    ScriptString_Release(&died);
}

void XB_CDECL BroScr_FlakWaitDeath_2(uint32_t ent, ScriptString name, int32_t flaknum) {
    ScriptString died;
    flak_died_name(&died, flaknum);
    const uint32_t* level = level_handle_word();
    const uint32_t notify = hash_string(&died);
    Scr_WaitTill(*level, notify);
    ScriptString null_name;
    ScriptString_ConcatCString(&null_name, &name, pointer<const char>(0x00824374));  // "_null"
    float t = as_float(title_global<uint32_t>(0x0083eb20));  // 1.0
    // The script's `t > 0` goes through a float truth value.
    while (static_cast<float>(t > 0.0f ? 1 : 0) != 0.0f) {
        Scr_Wait(as_float(0x3d4ccccd));                         // 0.05
        t = t - as_float(title_global<uint32_t>(0x0083eb24));  // 0.5
    }
    slot<void XB_CDECL(uint32_t)>(0x00922540)(ent);  // GScr_Delete
    ScriptString_Release(&null_name);
    ScriptString_Release(&died);
    ScriptString_Release(&name);
}

void XB_CDECL BroScr_FlakPanzerLoopSounds(uint32_t ent, int32_t flaknum) {
    ScriptString died;
    flak_died_name(&died, flaknum);
    const uint32_t level = *level_handle_word();
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(level, hash_string(&died));  // Scr_EndOn
    queue_notify(ent, hash_name(0x00824330));  // "flakpanzer_loopoff"
    queue_notify(ent, hash_name(0x00824390));  // "flakpanzer_loop_faston"
    Scr_Wait(as_float(0x40054fdf));            // 2.08
    queue_notify(ent, hash_name(0x00824318));  // "flakpanzer_loop_fastoff"
    queue_notify(ent, hash_name(0x0082437c));  // "flakpanzer_loopon"
    ScriptString_Release(&died);
}

void XB_CDECL BroScr_RestoreHealthOnDamage(uint32_t ent) {
    const int32_t full = health(ent);
    while (health(ent) > 0) {
        Scr_WaitTillString(ent, pointer<const char>(0x00836254));  // "damage"
        if (health(ent) > 0) set_health(ent, full);
    }
}

void XB_CDECL BroScr_StoneGibLaunch(uint32_t ent, ScrVec3 from) {
    uint32_t buffer[3];
    const uint32_t* o = slot<GetVec>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
    const uint32_t origin[3] = {o[0], o[1], o[2]};
    float dir[3];
    dir[0] = as_float(origin[0]) - as_float(from.v[0]);
    dir[1] = as_float(origin[1]) - as_float(from.v[1]);
    dir[2] = as_float(origin[2]) - as_float(from.v[2]);
    const uint32_t undefined = title_global<uint32_t>(0x00922eb8);  // g_scrUndefinedVecLane
    uint32_t normal[3] = {undefined, undefined, undefined};    // the original uses `from`'s slots
    slot<void XB_CDECL(uint32_t*, const float*)>(0x00922280)(normal, dir);  // Scr_VectorNormalize
    float velocity[3];
    std::memcpy(velocity, normal, sizeof(velocity));
    const float speed = static_cast<float>(slot<int32_t XB_CDECL(int32_t)>(0x0092222c)(200) + 150);  // Scr_RandomInt
    velocity[0] = speed * velocity[0];
    velocity[1] = velocity[1] * speed;
    velocity[2] = speed * velocity[2];
    using RandomFloat = long double XB_CDECL(uint32_t);  // ST0, used at the x87's precision
    velocity[2] = static_cast<float>(slot<RandomFloat>(0x00922230)(0x43480000) +
                                     as_float(title_global<uint32_t>(0x0083ef08)));  // rand(200) + 200
    // The script's `x > 0` goes through a float truth value.
    float roll;
    if (static_cast<float>(velocity[0] > 0.0f ? 1 : 0) != 0.0f)
        roll = static_cast<float>(-(slot<RandomFloat>(0x00922230)(0x451c4000) + as_float(title_global<uint32_t>(0x0083edb0))));
    else
        roll = static_cast<float>(slot<RandomFloat>(0x00922230)(0x451c4000) + as_float(title_global<uint32_t>(0x0083edb0)));
    slot<void XB_CDECL(uint32_t, float, uint32_t, uint32_t, uint32_t)>(0x009227b4)(ent, roll, 0x40a00000, 0,
                                                                                  0);  // GScr_RotateRoll (5 s)
    slot<void XB_CDECL(uint32_t, const float*, uint32_t)>(0x009227a4)(ent, velocity, 0x41400000);  // GScr_MoverMoveGravity 12
    Scr_Wait(6.0f);
    slot<void XB_CDECL(uint32_t)>(0x00922540)(ent);  // GScr_Delete
}

void XB_CDECL BroScr_TrackWheelOrigin(uint32_t vehicle, uint32_t ent) {
    float distance = as_float(title_global<uint32_t>(0x0083eb4c));         // 0.001
    const float step = as_float(title_global<uint32_t>(0x0083ef04));       // 24
    using Speed = long double XB_CDECL(uint32_t);                          // ST0
    for (;;) {
        uint32_t buffer1[3], buffer2[3];
        const uint32_t* o = slot<GetVec>(0x00922b20)(buffer1, ent);  // ScrEnt_GetOrigin
        const uint32_t before[3] = {o[0], o[1], o[2]};
        Scr_Wait(as_float(0x3e19999a));  // 0.15
        o = slot<GetVec>(0x00922b20)(buffer2, ent);
        const uint32_t after[3] = {o[0], o[1], o[2]};
        using Distance = long double XB_CDECL(const uint32_t*, const uint32_t*);
        distance = static_cast<float>(slot<Distance>(0x00922268)(before, after) + distance);  // Scr_Distance (ST0)
        const long double speed = slot<Speed>(0x00922b38)(vehicle);
        // The script's comparisons go through float truth values.
        if (static_cast<float>(speed > as_float(title_global<uint32_t>(0x0083eb20)) ? 1 : 0) == 0.0f) continue;  // > 1
        if (static_cast<float>(distance > step ? 1 : 0) == 0.0f) continue;
        uint32_t buffer3[3];
        const uint32_t* a = slot<GetVec>(0x00922ba0)(buffer3, ent);  // ScrEnt_GetAngles
        const uint32_t angles[3] = {a[0], a[1], a[2]};
        const uint32_t undefined = title_global<uint32_t>(0x00922eb8);
        uint32_t forward[3] = {undefined, undefined, undefined};
        slot<void XB_CDECL(uint32_t*, const uint32_t*)>(0x00922290)(forward, angles);  // Scr_AnglesToForward (unused)
        slot<Speed>(0x00922b38)(vehicle);                                            // result dropped
        distance = distance - as_float(title_global<uint32_t>(0x0083ef04));
    }
}

void XB_CDECL BroScr_WaitPoint15() { Scr_Wait(as_float(0x3e19999a)); }

void XB_CDECL BroScr_WaitZero() { Scr_Wait(0.0f); }

void XB_CDECL BroScr_IdleUntilEnd(uint32_t ent, ScrVec3, ScriptString endon) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, 0x06730ae8);  // Scr_EndOn death
    if (endon.rep) Scr_EndOnString(*level_handle_word(), endon.rep->text);
    for (;;) Scr_Wait(as_float(0x3d4ccccd));  // 0.05
}

void XB_CDECL BroScr_LerpPlayerAngles(uint32_t player, float pitch, float yaw, float roll, float time, uint32_t release) {
    lerp_player_angles(player, pitch, yaw, roll, time, release, false);
}

void XB_CDECL BroScr_LerpPlayerAngles_2(uint32_t player, float pitch, float yaw, float roll, float time, uint32_t release) {
    lerp_player_angles(player, pitch, yaw, roll, time, release, true);
}

void XB_CDECL Bro__window_gmi__new_window_break(uint32_t ent) {
    using GetEnt = uint32_t XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t);
    ScriptString temp;
    uint32_t window = slot<GetEnt>(0x009221c4)(ScrCall_ScrEnt_GetTarget(&ent, &temp), 0x0d786b4c, 0, 0,
                                               0);  // GScr_GetEntArray by targetname
    ScriptString_Release(&temp);
    const uint32_t broken = slot<GetEnt>(0x009221c4)(ScrCall_ScrEnt_GetTarget(&window, &temp), 0x0d786b4c, 0, 0, 0);
    ScriptString_Release(&temp);
    ScriptString noteworthy;
    const bool none = ScrCall_ScrEnt_GetScriptNoteworthy(&ent, &noteworthy)->rep == nullptr;
    ScriptString_Release(&noteworthy);
    using PlaySound = void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t);
    if (none) {
        ScriptString sound;
        ScriptString_FromCString(&sound, pointer<const char>(0x00824414));  // "window_break"
        slot<PlaySound>(0x009221dc)(window, &sound, 0, 0, 0);              // GScr_PlaySound
        ScriptString_Release(&sound);
    } else {
        ScriptString sound;
        slot<PlaySound>(0x009221dc)(window, ScrCall_ScrEnt_GetScriptNoteworthy(&ent, &sound), 0, 0, 0);
        ScriptString_Release(&sound);
    }
    slot<void XB_CDECL(uint32_t)>(0x00922568)(window);  // GScr_SetEntFlag400
    slot<void XB_CDECL(uint32_t)>(0x00922564)(broken);  // GScr_ClearEntFlag400
    Scr_Wait(1.0f);
    slot<void XB_CDECL(uint32_t)>(0x00922540)(window);  // GScr_Delete
    slot<void XB_CDECL(uint32_t)>(0x00922540)(ent);
}

void XB_CDECL BroScr_ShowContextTextBriefly() {
    using GetObject = uint32_t XB_CDECL(uint32_t);
    uint32_t object = title_function<GetObject>(0x0011db20)(pointer<uint32_t>(title_global<uint32_t>(0x00922ee0))[1]);  // GScr_GetEntScriptObject
    // The original stores a byte into a stack word whose upper bytes are stale (ECX at entry).
    ScriptKeyTable_Set(object ? pointer<ScriptKeyTable>(object) : &title_global<ScriptKeyTable>(0x00922e94), 0xddfa2c42, 1);
    Scr_Wait(8.0f);
    object = title_function<GetObject>(0x0011db20)(pointer<uint32_t>(title_global<uint32_t>(0x00922ee0))[1]);
    ScriptKeyTable_Set(object ? pointer<ScriptKeyTable>(object) : &title_global<ScriptKeyTable>(0x00922e94), 0xddfa2c42, 0);
}

void XB_CDECL BroScr_DroneDeleteLater(uint32_t drone) {
    const uint32_t level_delay = *pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) + 0x544);
    const float far = as_float(title_global<uint32_t>(0x007f51cc));  // 512 squared
    float delay = as_float(level_delay) == as_float(title_global<uint32_t>(0x00922eac))  // g_scrUndefinedFloat
                      ? as_float(title_global<uint32_t>(0x0083ece8))                   // 5
                      : as_float(level_delay);
    uint32_t buffer1[3], buffer2[3];
    const uint32_t* o = slot<GetVec>(0x00922b20)(buffer1, drone);  // ScrEnt_GetOrigin
    const uint32_t at[3] = {o[0], o[1], o[2]};
    o = slot<GetVec>(0x00922b20)(buffer2, title_global<uint32_t>(0x00922eec));  // the player
    const uint32_t player[3] = {o[0], o[1], o[2]};
    using DistanceSquared = long double XB_CDECL(const uint32_t*, const uint32_t*);
    const float d2 = static_cast<float>(slot<DistanceSquared>(0x0092226c)(player, at));  // Scr_DistanceSquared (ST0)
    // The script's comparisons go through float truth values.
    float extra;
    if (static_cast<float>(d2 > far ? 1 : 0) != 0.0f) {
        const float scale = as_float(title_global<uint32_t>(0x007f51cc)) / d2;
        delay = delay * scale;
        const float one = as_float(title_global<uint32_t>(0x0083eb20));
        if (static_cast<float>(one > delay ? 1 : 0) != 0.0f) delay = one;
        extra = scale * as_float(title_global<uint32_t>(0x0083eb98));  // 10
        if (static_cast<float>(one > extra ? 1 : 0) != 0.0f) extra = one;
    } else {
        extra = as_float(title_global<uint32_t>(0x0083eb98));
    }
    Scr_Wait(delay);
    Scr_WaitExact(extra);
    if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922214)(drone)) {  // Scr_EntityHandleIsValid
        slot<void XB_CDECL(uint32_t)>(0x00922540)(drone);        // GScr_Delete
        *pointer<int32_t>(title_global<uint32_t>(0x00922ee0) + 0x508) -= 1;  // the level's drone count
    }
}

}  // namespace game
