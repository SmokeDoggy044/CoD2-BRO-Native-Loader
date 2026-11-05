#include "bro_scr_level_threads.h"

#include <cstring>

#include "bro_scr_ai_common.h"
#include "bro_scr_common.h"
#include "bro_scr_death_watch.h"
#include "bro_scr_notify_threads.h"
#include "bro_scr_wait_misc.h"
#include "g_scr_runtime.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "title_data.h"

namespace game {

using namespace bro_ai;

namespace {

constexpr uint32_t kDeath = 0x06730ae8;

using GetVec = const uint32_t* XB_CDECL(uint32_t*, uint32_t);
using GetString = ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t);
using PlaySoundAt = void XB_CDECL(uint32_t, ScriptString*, const uint32_t*, uint32_t, uint32_t);
using PlaySound = void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t);
using SetVec = void XB_CDECL(uint32_t, ScrVec3);
using Spawn = uint32_t XB_CDECL(ScriptString*, const uint32_t*, uint32_t);
using Ent = void XB_CDECL(uint32_t);
using EntHash = void XB_CDECL(uint32_t, uint32_t);

inline float float_bits(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

inline uint32_t player() { return title_global<uint32_t>(0x00922eec); }

// The level entity's handle: the word at +4 of the level object (0x00922ee0), read through null.
inline uint32_t level_handle() {
    const uint32_t object = title_global<uint32_t>(0x00922ee0);
    const uint32_t field = object ? object + 4 : 0;
    return *reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(field));
}

// *out = the entity's origin / angles (copied out of the call's buffer).
inline void origin_of(ScrVec3* out, uint32_t ent) {
    uint32_t buffer[3];
    const uint32_t* const o = slot<GetVec>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
    out->v[0] = o[0];
    out->v[1] = o[1];
    out->v[2] = o[2];
}

inline void angles_of(ScrVec3* out, uint32_t ent) {
    uint32_t buffer[3];
    const uint32_t* const a = slot<GetVec>(0x00922ba0)(buffer, ent);  // ScrEnt_GetAngles
    out->v[0] = a[0];
    out->v[1] = a[1];
    out->v[2] = a[2];
}

// Scr_AnglesToForward into a vector of undefined lanes (the result is not used).
inline void angles_to_forward(const ScrVec3* angles) {
    const uint32_t lane = undefined_lane();
    uint32_t forward[3] = {lane, lane, lane};
    slot<void XB_CDECL(uint32_t*, const ScrVec3*)>(0x00922290)(forward, angles);  // Scr_AnglesToForward
}

inline long double distance(const ScrVec3* a, const ScrVec3* b) {
    return slot<long double XB_CDECL(const ScrVec3*, const ScrVec3*)>(0x00922268)(a, b);  // Scr_Distance (ST0)
}

inline long double vehicle_speed(uint32_t ent) { return slot<long double XB_CDECL(uint32_t)>(0x00922b38)(ent); }

inline int32_t health(uint32_t ent) { return slot<int32_t XB_CDECL(uint32_t)>(0x00922b90)(ent); }  // ScrEnt_GetHealth

// The entity's string key (ScriptKeyTable_GetString: added empty when missing).
inline ScriptString* string_key(uint32_t ent, uint32_t key) {
    return title_function<GetString>(0x00458da0)(ent_script_object(ent), key);
}

// GScr_PlaySoundAtPosition(ent, string key, origin, 0, 0).
inline void play_key_sound_at_origin(uint32_t ent, uint32_t key) {
    ScrVec3 o;
    origin_of(&o, ent);
    ScriptString* const sound = string_key(ent, key);
    const uint32_t at[3] = {o.v[0], o.v[1], o.v[2]};
    slot<PlaySoundAt>(0x009221e0)(ent, sound, at, 0, 0);
}

// The entity's bool key, added as the undefined bool (zero-extended: the title pushes a word with
// stale upper bytes).
inline uint8_t* bool_slot(uint32_t ent, uint32_t key) {
    return reinterpret_cast<uint8_t*>(key_slot(ent, key, title_global<uint8_t>(0x00922ed0)));
}

// The entity's float key, added as the undefined float.
inline const float* float_slot(uint32_t ent, uint32_t key) {
    return reinterpret_cast<const float*>(key_slot(ent, key, undefined_float()));
}

}  // namespace

bool* XB_CDECL BroScr_PlayerInsideEntZone(bool* out, uint32_t ent, float near_dist, float far_dist) {
    const uint32_t self = ent;
    float buffer[3];
    using GetOrigin = const float* XB_CDECL(float*, uint32_t);

    const float* o = slot<GetOrigin>(0x00922b20)(buffer, player());
    float p0 = o[0], p1 = o[1], p2 = o[2];
    o = slot<GetOrigin>(0x00922b20)(buffer, self);
    float d0 = o[0] - p0, d1 = o[1] - p1, d2 = o[2] - p2;
    const float* v = ScrField_h6ff72745(self);
    const float dot1 = (v[2] * d2 + v[1] * d1) + d0 * v[0];
    std::memcpy(&ent, &dot1, 4);

    v = ScrField_he2e8d8a5(self);
    float s0 = v[0] * near_dist, s1 = v[1] * near_dist, s2 = v[2] * near_dist;
    o = slot<GetOrigin>(0x00922b20)(buffer, self);
    float q0 = o[0] + s0, q1 = o[1] + s1, q2 = o[2] + s2;
    o = slot<GetOrigin>(0x00922b20)(buffer, player());
    d0 = q0 - o[0];
    d1 = q1 - o[1];
    d2 = q2 - o[2];
    v = ScrField_he2e8d8a5(self);
    const float dot2 = (v[2] * d2 + v[1] * d1) + v[0] * d0;
    near_dist = dot2;

    v = ScrField_he2e8d8a5(self);
    s0 = v[0] * far_dist;
    s1 = v[1] * far_dist;
    s2 = v[2] * far_dist;
    o = slot<GetOrigin>(0x00922b20)(buffer, self);
    q0 = o[0] + s0;
    q1 = o[1] + s1;
    q2 = o[2] + s2;
    o = slot<GetOrigin>(0x00922b20)(buffer, player());
    d0 = q0 - o[0];
    d1 = q1 - o[1];
    d2 = q2 - o[2];
    v = ScrField_he2e8d8a5(self);
    const float dot3 = (v[2] * d2 + v[1] * d1) + v[0] * d0;
    far_dist = dot3;

    *out = dot1 > 0.0f && dot2 > 0.0f && 0.0f > dot3;
    return out;
}

void XB_CDECL BroScr_DebugForwardStub(uint32_t ent) {
    for (;;) {
        ScrField_he2e8d8a5(ent);
        ScrVec3 origin, angles;
        origin_of(&origin, ent);
        ScrField_he2e8d8a5(ent);
        origin_of(&origin, ent);
        angles_of(&angles, ent);
        angles_to_forward(&angles);
        Scr_WaitFrames(0);
    }
}

namespace {

// BroScr_StukaPlaneCrashMove / BroScr_P47PlaneCrashMove (identical bodies).
inline void plane_crash_move(uint32_t ent) {
    float moved = title_global<float>(0x0083eb4c);  // 0.001
    get_time();
    const uint32_t lane = undefined_lane();
    ScrVec3 previous;
    previous.v[0] = lane;
    previous.v[1] = lane;
    previous.v[2] = lane;
    const float step = title_global<float>(0x0083ebe8);  // 128
    for (;;) {
        origin_of(&previous, ent);
        BroScr_WaitFrame();
        ScrVec3 current;
        origin_of(&current, ent);
        moved = static_cast<float>(distance(&previous, &current) + moved);
        if (!(vehicle_speed(ent) > title_global<float>(0x0083eb20))) continue;  // 1
        if (!(moved > step)) continue;
        vehicle_speed(ent);
        play_key_sound_at_origin(ent, 0x1cc418a4);
        moved = moved - title_global<float>(0x0083ebe8);
    }
}

// BroScr_PlayExplodeSounds / _2 (identical bodies).
inline void explode_sounds(uint32_t ent) {
    play_key_sound_at_origin(ent, 0xe6f61fa7);
    Scr_Wait(float_bits(0x3e4ccccd));  // 0.2
    play_key_sound_at_origin(ent, 0xe6f61fa7);
    Scr_Wait(float_bits(0x3ecccccd));  // 0.4
    play_key_sound_at_origin(ent, 0xe6f61fa7);
}

// Turret call 272 (slot 0x00922580) on the left and right turret keys every 0.05 s, forever.
inline void turret_fire_loop(uint32_t ent) {
    using TurretCall = void XB_CDECL(uint32_t, uint32_t);
    for (;;) {
        slot<TurretCall>(0x00922580)(*ent_slot(ent, 0xef6522f9), 0);  // left turret
        slot<TurretCall>(0x00922580)(*ent_slot(ent, 0xeead8089), 0);  // right turret
        Scr_Wait(float_bits(0x3d4ccccd));                            // 0.05
    }
}

// Both turrets target the player (GScr_Turret_SetTargetEntity).
inline void turrets_target_player(uint32_t ent) {
    using SetTarget = void XB_CDECL(uint32_t, uint32_t);
    slot<SetTarget>(0x00922590)(*ScrField_rightturret(ent), player());
    slot<SetTarget>(0x00922590)(*ScrField_leftturret(ent), player());
}

// The key's thread handle (added as 0) ended, then replaced by the running thread's.
inline void replace_key_thread(uint32_t ent, uint32_t key) {
    slot<Ent>(0x009221a0)(*key_slot(ent, key, 0));  // Scr_TerminateThread
    uint32_t* const thread = key_slot(ent, key, 0);
    *thread = slot<uint32_t XB_CDECL()>(0x0092219c)();  // Scr_GetCurrentThreadHandle
}

}  // namespace

void XB_CDECL BroScr_StukaPlaneCrashMove(uint32_t ent) { plane_crash_move(ent); }
void XB_CDECL BroScr_P47PlaneCrashMove(uint32_t ent) { plane_crash_move(ent); }
void XB_CDECL BroScr_PlayExplodeSounds(uint32_t ent) { explode_sounds(ent); }
void XB_CDECL BroScr_PlayExplodeSounds_2(uint32_t ent) { explode_sounds(ent); }

void XB_CDECL BroScr_VehicleMg42Fire(uint32_t ent, bool keep_target) {
    uint32_t hash;
    slot<EntHash>(0x009221a4)(ent, *Scr_HashName(&hash, pointer<const char>(0x0080f744)));  // Scr_QueueNotify "stop mg42s"
    Scr_EndOnString(ent, pointer<const char>(0x0080f744));
    Scr_EndOnString(ent, pointer<const char>(0x00831f60));  // "death"
    if (health(ent) <= 0) return;
    if (!keep_target) turrets_target_player(ent);
    turret_fire_loop(ent);
}

void XB_CDECL BroScr_PlaneTurretsFire(uint32_t ent, uint8_t keep_target) {
    Scr_EndOnString(ent, pointer<const char>(0x00819ae4));  // "stop firing"
    replace_key_thread(ent, 0x65637ee2);
    if (health(ent) <= 0) return;
    if (keep_target == title_global<uint8_t>(0x00922ed0)) turrets_target_player(ent);  // g_scrUndefinedBool
    turret_fire_loop(ent);
}

namespace {

// A record of the table; each use bounds-checks the index again (the last record past the end).
inline TankDeadGuyRecord& dead_guy(TankDeadGuyTable* table, uint32_t index) { return table->record[index < 4 ? index : 3]; }

}  // namespace

void XB_CDECL BroScr_SpawnTankDeadGuys(uint32_t self, uint32_t unused, TankDeadGuyTable table, uint32_t index,
                                       uint32_t unused2, ScrVec3 pos, ScrVec3 angles) {
    (void)self;
    (void)unused;
    (void)unused2;
    float delay = dead_guy(&table, index).delay;
    if (delay != title_global<float>(0x00922eac)) Scr_Wait(dead_guy(&table, index).delay);  // g_scrUndefinedFloat
    if (dead_guy(&table, index).model_only == 0) {
        ScrVec3 player_origin;
        origin_of(&player_origin, player());
        if (!(static_cast<long double>(title_global<float>(0x0083f154)) > distance(&player_origin, &pos))) {  // 3500
            const uint32_t guy = slot<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00922520)(dead_guy(&table, index).spawner, 0);  // GScr_ForceSpawn
            delay = static_cast<float>(dead_guy(&table, index).anim);
            using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, ScrVec3*,
                                                  ScrVec3*, float*);
            title_function<StartThread>(0x0045b670)(0, 0, 0, 0, 0x00121960, guy, &pos, &angles,
                                                    &delay);  // Scr_StartThread_ent_vec_vec_f(BroScr_TankDeadGuyPlayAnim)
            return;
        }
    }
    ScriptString classname;
    ScriptString_FromCString(&classname, pointer<const char>(0x0083ab88));  // "script_origin"
    uint32_t model = slot<Spawn>(0x009222f0)(&classname, pos.v, 0);    // GScr_Spawn
    ScriptString_Release(&classname);
    slot<SetVec>(0x00922ba4)(model, angles);  // ScrEnt_SetAngles
    ScriptString_FromCString(&classname, pointer<const char>(0x0083ab98));  // "script_model"
    float below[3];
    std::memcpy(below, pos.v, 12);
    below[2] = below[2] - title_global<float>(0x0083ebe8);  // 128
    model = slot<uint32_t XB_CDECL(ScriptString*, const float*, uint32_t)>(0x009222f0)(&classname, below, 0);
    ScriptString_Release(&classname);
    slot<SetVec>(0x00922ba4)(model, angles);
    ScriptString tree;
    ScriptString_FromCString(&tree, pointer<const char>(0x007f4960));  // "tankdeadguys"
    slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009225f0)(model, &tree);  // GScr_UseAnimTree
    ScriptString_Release(&tree);
    table.record[0].model_anim = title_global<uint32_t>(0x008cd1ac);
    table.record[1].model_anim = title_global<uint32_t>(0x008cd1a0);
    table.record[2].model_anim = title_global<uint32_t>(0x008cd194);
    using AnimScripted = void XB_CDECL(uint32_t, uint32_t, const ScrVec3*, const ScrVec3*, uint32_t);
    slot<AnimScripted>(0x009224c0)(model, 0xe2ea7dce, &pos, &angles, dead_guy(&table, index).model_anim);  // GScr_AnimScriptedNoNotify
    Scr_Wait(float_bits(0x41a00000));  // 20
    if (entity_handle_is_valid(model)) slot<Ent>(0x00922540)(model);  // GScr_Delete
}

void XB_CDECL BroScr_PauseUnpauseWait(uint32_t self, uint32_t ent) {
    using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*);
    title_function<StartThread>(0x0045a6d0)(0, 0, 0, 0, 0x00121a60, self, &ent);  // Scr_StartThread_ent_ent(BroScr_NotifyLevelOnDeath)
    slot<EntHash>(0x00922188)(ent, kDeath);  // Scr_EndOn
    Scr_WaitTill(ent, 0x0032304c);
    slot<EntHash>(0x009221a4)(level_handle(), 0x92b8c65f);  // Scr_QueueNotify
    slot<EntHash>(0x009221a4)(self, 0x92b8c65f);
    slot<EntHash>(0x009221a4)(ent, 0x92b8c65f);
}

void XB_CDECL BroScr_DeathPhysicsLaunchThread(uint32_t ent, ScrVec3 angles, uint32_t launch_arg) {
    const uint32_t lane = undefined_lane();
    ScrVec3 force;
    force.v[0] = lane;
    force.v[1] = lane;
    force.v[2] = lane;
    BroScr_DeathPhysicsLaunch_2(ent, angles, launch_arg, force, title_global<float>(0x00922ea4));
}

namespace {

// *at = the tag's origin (GScr_GetTagOrigin into undefined lanes).
inline void tag_origin(ScrVec3* at, uint32_t ent, ScriptString* tag) {
    const uint32_t lane = undefined_lane();
    at->v[0] = lane;
    at->v[1] = lane;
    at->v[2] = lane;
    slot<void XB_CDECL(uint32_t, ScrVec3*, ScriptString*)>(0x0092261c)(ent, at, tag);  // GScr_GetTagOrigin
}

}  // namespace

void XB_CDECL BroScr_DeathPhysicsLaunch_2FromTag(uint32_t ent, ScrVec3 angles, uint32_t launch_arg, ScriptString tag) {
    ScrVec3 force;
    tag_origin(&force, ent, &tag);
    BroScr_DeathPhysicsLaunch_2(ent, angles, launch_arg, force, title_global<float>(0x00922ea4));
    ScriptString_Release(&tag);
}

void XB_CDECL BroScr_DeathPhysicsLaunch_2FromTag_2(uint32_t ent, ScrVec3 angles, uint32_t launch_arg,
                                                   ScriptString tag, ScrVec3 unused, float delay) {
    (void)unused;
    ScrVec3 force;
    tag_origin(&force, ent, &tag);
    BroScr_DeathPhysicsLaunch_2(ent, angles, launch_arg, force, delay);
    ScriptString_Release(&tag);
}

void XB_CDECL BroScr_DeathPhysicsLaunch_2Call(uint32_t ent, ScrVec3 angles, uint32_t launch_arg, ScrVec3 force) {
    BroScr_DeathPhysicsLaunch_2(ent, angles, launch_arg, force, title_global<float>(0x00922ea4));
}

void XB_CDECL BroScr_FlakSoundBlend(uint32_t ent, ScriptString name, int32_t flaknum) {
    ScriptString text;
    ScriptString_FromCString(&text, pointer<const char>(0x0083abbc));  // "sound_blend"
    const uint32_t zero[3] = {0, 0, 0};
    const uint32_t blend = slot<Spawn>(0x009222f0)(&text, zero, 0);  // GScr_Spawn
    ScriptString_Release(&text);
    ScrVec3 origin;
    origin_of(&origin, ent);
    slot<SetVec>(0x00922b24)(blend, origin);  // ScrEnt_SetOrigin
    ScriptString_FromCString(&text, pointer<const char>(0x00824344));  // "flak died with flaknum "
    ScriptString_AppendInt(&text, flaknum);
    const uint32_t level = level_handle();
    uint32_t hash;
    slot<EntHash>(0x00922188)(level, *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&text)));  // Scr_EndOn
    using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, ScriptString*,
                                          int32_t*);
    title_function<StartThread>(0x004b0950)(0, 0, 0, 0, 0x001227d0, blend, &name,
                                            &flaknum);  // Scr_StartThread_ent_str_i(BroScr_FlakWaitDeath_2)
    ScriptString on, null_name, off;
    ScriptString_ConcatCString(&on, &name, pointer<const char>(0x008243a8));         // "on"
    ScriptString_ConcatCString(&null_name, &name, pointer<const char>(0x00824374));  // "_null"
    ScriptString_ConcatCString(&off, &name, pointer<const char>(0x0080da58));        // "off"
    for (;;) {
        Scr_WaitTill(ent, *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&on)));
        for (float f = 0.0f; title_global<float>(0x0083eb20) > f; f = f + title_global<float>(0x0083ebf8))  // 1, 0.1
            Scr_Wait(float_bits(0x3d4ccccd));                                                                 // 0.05
        Scr_WaitTill(ent, *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&off)));
        for (float f = title_global<float>(0x0083eb20); f > 0.0f; f = f - title_global<float>(0x0083ebf8))
            Scr_Wait(float_bits(0x3d4ccccd));
    }
}

void XB_CDECL BroScr_EngineSmokeTrail(uint32_t ent) {
    constexpr uint32_t kTrail = 0x4c9e9556;
    slot<EntHash>(0x00922188)(ent, kDeath);  // Scr_EndOn
    const float step = title_global<float>(0x0083ecd8);  // 48
    float moved = title_global<float>(0x0083eb4c);       // 0.001
    const int32_t end = get_time() + 10000;
    ScriptString tag, classname;
    ScriptString_FromCString(&tag, pointer<const char>(0x007fcf20));        // "tag_engine"
    ScriptString_FromCString(&classname, pointer<const char>(0x0083ab88));  // "script_origin"
    ScrVec3 at;
    tag_origin(&at, ent, &tag);
    const uint32_t trail = slot<Spawn>(0x009222f0)(&classname, at.v, 0);  // GScr_Spawn
    *key_slot(ent, kTrail, 0) = trail;
    ScriptString_Release(&classname);
    ScriptString_Release(&tag);
    slot<EntHash>(0x009224fc)(*key_slot(ent, kTrail, 0), ent);  // GScr_LinkTo
    while (vehicle_speed(ent) > title_global<float>(0x0083eb90) && end > get_time()) {  // 100
        ScrVec3 previous;
        origin_of(&previous, *key_slot(ent, kTrail, 0));
        Scr_Wait(float_bits(0x3d4ccccd));  // 0.05
        ScrVec3 current;
        origin_of(&current, *key_slot(ent, kTrail, 0));
        moved = static_cast<float>(distance(&previous, &current) + moved);
        if (!(vehicle_speed(ent) > title_global<float>(0x0083eb20))) continue;  // 1
        if (!(moved > step)) continue;
        vehicle_speed(ent);
        ScriptString smoke;
        ScriptString_FromCString(&smoke, pointer<const char>(0x007fcf04));  // "smk_trail_thick_lg_black_a"
        slot<PlaySound>(0x009221dc)(*key_slot(ent, kTrail, 0), &smoke, 0, 0, 0);  // GScr_PlaySound
        ScriptString_Release(&smoke);
        moved = moved - title_global<float>(0x0083ecd8);
    }
    if (!(end > get_time())) return;
    do {
        ScriptString smoke;
        ScriptString_FromCString(&smoke, pointer<const char>(0x007fcf04));
        slot<PlaySound>(0x009221dc)(*key_slot(ent, kTrail, 0), &smoke, 0, 0, 0);
        ScriptString_Release(&smoke);
        const long double r = slot<long double XB_CDECL(float)>(0x00922230)(float_bits(0x3e99999a));  // Scr_RandomFloat(0.3)
        Scr_Wait(static_cast<float>(r + title_global<float>(0x0083ebf8)));                            // + 0.1
    } while (end > get_time());
}

void XB_CDECL BroScr_ShellShockWait(uint32_t unused, int32_t seconds) {
    (void)unused;
    using SetInt = void XB_CDECL(uint32_t, uint32_t);
    *bool_slot(player(), 0x2eecefb8) = 1;
    slot<SetInt>(0x00922e0c)(player(), 1);  // ScrSentient_SetInt_30
    const uint32_t level = level_handle();
    uint32_t hash;
    slot<EntHash>(0x00922188)(level, *Scr_HashName(&hash, pointer<const char>(0x00824604)));  // Scr_EndOn "player is shell shocked"
    Scr_Wait(static_cast<float>(seconds) - title_global<float>(0x0083eb20));                   // - 1
    *bool_slot(player(), 0x2eecefb8) = 0;
    slot<SetInt>(0x00922e0c)(player(), 0);
}

void XB_CDECL BroScr_MoveSoundEvery128Units(uint32_t ent) {
    float moved = title_global<float>(0x0083eb4c);       // 0.001
    const float step = title_global<float>(0x0083ebe8);  // 128
    do {
        ScrVec3 previous;
        origin_of(&previous, ent);
        BroScr_WaitFrame();
        ScrVec3 current;
        origin_of(&current, ent);
        moved = static_cast<float>(distance(&previous, &current) + moved);
        if (vehicle_speed(ent) > title_global<float>(0x0083eb20) && moved > step) {  // 1
            vehicle_speed(ent);
            play_key_sound_at_origin(ent, 0x1cc418a4);
            moved = moved - title_global<float>(0x0083ebe8);
        }
    } while (vehicle_speed(ent) > title_global<float>(0x0083eb20));
}

void XB_CDECL BroScr_VehicleMg42Fire_2(uint32_t ent, uint8_t keep_target) {
    const uint32_t self = ent;
    // The IsDefined byte goes to the ent argument slot, as the original.
    if (*ScrField_haee44119_IsDefined(reinterpret_cast<bool*>(&ent), self))
        slot<Ent>(0x009221a0)(*key_slot(self, 0xaee44119, 0));  // Scr_TerminateThread
    uint32_t* const thread = key_slot(self, 0xaee44119, 0);
    *thread = slot<uint32_t XB_CDECL()>(0x0092219c)();  // Scr_GetCurrentThreadHandle
    Scr_EndOnString(self, pointer<const char>(0x0080f744));  // "stop mg42s"
    Scr_EndOnString(self, pointer<const char>(0x00831f60));  // "death"
    if (health(self) <= 0) return;
    if (keep_target == title_global<uint8_t>(0x00922ed0)) turrets_target_player(self);  // g_scrUndefinedBool
    turret_fire_loop(self);
}

namespace {

inline void debug_stub(uint32_t ent, uint32_t key) {
    slot<EntHash>(0x00922188)(ent, kDeath);  // Scr_EndOn
    *int_slot(ent, key) = 0;
    for (;;) Scr_Wait(float_bits(0x3d4ccccd));  // 0.05
}

}  // namespace

void XB_CDECL BroScr_DebugStub_h8c05ef86(uint32_t, uint32_t ent) { debug_stub(ent, 0x8c05ef86); }
void XB_CDECL BroScr_DebugStub_hcd84c908(uint32_t, uint32_t ent) { debug_stub(ent, 0xcd84c908); }

void XB_CDECL BroScr_TankStunOnDamage(uint32_t ent, int32_t seconds) {
    if (!(health(ent) > 0)) return;
    const int32_t ms = seconds * 1000;
    const float wait = static_cast<float>(seconds);
    do {
        *int_slot(ent, 0x2f66b4d8) = 0;
        Scr_WaitTillString(ent, pointer<const char>(0x00836254));  // "damage"
        *int_slot(ent, 0x2f66b4d8) = 1;
        const float end = static_cast<float>(get_time() + ms);
        while (end > static_cast<float>(get_time())) Scr_Wait(float_bits(0x3dcccccd));  // 0.1
        Scr_Wait(wait);
        slot<EntHash>(0x009221a4)(ent, 0xecd3b220);  // Scr_QueueNotify
    } while (health(ent) > 0);
}

void XB_CDECL BroScr_TurretVisTargetWait(uint32_t ent) {
    Scr_EndOnString(ent, pointer<const char>(0x007fcec8));     // "novistarget"
    Scr_WaitTillString(ent, pointer<const char>(0x0083a9b4));  // "turret_on_vistarget"
    *int_slot(ent, 0x4d917ba7) = 1;
}

void XB_CDECL BroScr_DebugForwardStub_2(uint32_t ent) {
    for (;;) {
        ScrVec3 angles;
        angles_of(&angles, ent);
        ScrVec3 turned = angles;
        float yaw;
        std::memcpy(&yaw, &angles.v[1], 4);
        yaw = yaw + *float_slot(ent, 0xbe149b8c);
        std::memcpy(&turned.v[1], &yaw, 4);
        angles_to_forward(&turned);
        angles_of(&angles, ent);
        turned = angles;
        std::memcpy(&yaw, &angles.v[1], 4);
        yaw = yaw - *float_slot(ent, 0xbe149b8c);
        std::memcpy(&turned.v[1], &yaw, 4);
        angles_to_forward(&turned);
        Scr_Wait(0.0f);
    }
}

}  // namespace game
