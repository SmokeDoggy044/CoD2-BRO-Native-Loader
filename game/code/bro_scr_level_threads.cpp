#include "bro_scr_level_threads.h"

#include <cstring>

#include "bro_scr_ai_anim.h"
#include "bro_scr_ai_common.h"
#include "bro_scr_common.h"
#include "bro_scr_death_watch.h"
#include "bro_scr_entity_threads.h"
#include "bro_scr_flak.h"
#include "bro_scr_grenade.h"
#include "bro_scr_hud_fade.h"
#include "bro_scr_level.h"
#include "bro_scr_notify_threads.h"
#include "bro_scr_vehicle_health.h"
#include "bro_scr_wait_misc.h"
#include "g_scr_runtime.h"
#include "qcommon_core.h"
#include "scr_access.h"
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

// BroScr_StukaPlaneCrashMove / BroScr_P47PlaneCrashMove (identical bodies; the crash thinkers with
// another step and sound key): every `step` (a title float) units moved while faster than 1.
inline void plane_crash_move(uint32_t ent, uint32_t step_address, uint32_t sound_key) {
    float moved = title_global<float>(0x0083eb4c);  // 0.001
    get_time();
    const uint32_t lane = undefined_lane();
    ScrVec3 previous;
    previous.v[0] = lane;
    previous.v[1] = lane;
    previous.v[2] = lane;
    const float step = title_global<float>(step_address);
    for (;;) {
        origin_of(&previous, ent);
        BroScr_WaitFrame();
        ScrVec3 current;
        origin_of(&current, ent);
        moved = static_cast<float>(distance(&previous, &current) + moved);
        if (!(vehicle_speed(ent) > title_global<float>(0x0083eb20))) continue;  // 1
        if (!(moved > step)) continue;
        vehicle_speed(ent);
        play_key_sound_at_origin(ent, sound_key);
        moved = moved - title_global<float>(step_address);
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

void XB_CDECL BroScr_StukaPlaneCrashMove(uint32_t ent) { plane_crash_move(ent, 0x0083ebe8, 0x1cc418a4); }  // 128
void XB_CDECL BroScr_P47PlaneCrashMove(uint32_t ent) { plane_crash_move(ent, 0x0083ebe8, 0x1cc418a4); }
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

namespace {

inline void set_word(uint32_t setter, uint32_t ent, uint32_t value) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(setter)(ent, value);
}
inline void set_float(uint32_t setter, uint32_t ent, float value) {
    uint32_t value_bits;
    std::memcpy(&value_bits, &value, 4);
    slot<void XB_CDECL(uint32_t, uint32_t)>(setter)(ent, value_bits);
}
inline void set_vec(uint32_t setter, uint32_t ent, ScrVec3 value) { slot<SetVec>(setter)(ent, value); }
// The setter takes a copy (released by it).
inline void set_string(uint32_t setter, uint32_t ent, const ScriptString* value) {
    ScriptString copy;
    ScriptString_Copy(&copy, value);
    slot<void XB_CDECL(uint32_t, ScriptString)>(setter)(ent, copy);
}
// Any lane not the undefined lane (NaN counts as defined).
inline bool vec_defined(const ScrVec3* v) {
    const float lane = title_global<float>(0x00922eb8);  // g_scrUndefinedVecLane
    float x, y, z;
    std::memcpy(&x, &v->v[0], 4);
    std::memcpy(&y, &v->v[1], 4);
    std::memcpy(&z, &v->v[2], 4);
    return x != lane || y != lane || z != lane;
}

}  // namespace

void XB_CDECL BroScr_RespawnerApplyAttribs(uint32_t ent, RespawnerAttributes a) {
    Scr_WaitFrames(0);
    if (a.health != undefined_int()) set_word(0x00922b94, ent, a.health);  // ScrEnt_SetHealth
    if (a.actor_d4 != title_global<float>(0x00922eac)) set_float(0x00922c5c, ent, a.actor_d4);  // ScrActor_SetFloat_d4
    if (a.actor_d8 != title_global<float>(0x00922eac)) set_float(0x00922c64, ent, a.actor_d8);  // ScrActor_SetFloat_d8
    if (vec_defined(&a.actor_108)) set_vec(0x00922c6c, ent, a.actor_108);  // ScrActor_SetVec_108
    if (vec_defined(&a.actor_114)) set_vec(0x00922c74, ent, a.actor_114);  // ScrActor_SetVec_114
    if (vec_defined(&a.actor_120)) set_vec(0x00922c7c, ent, a.actor_120);  // ScrActor_SetVec_120
    if (a.actor_828 != title_global<float>(0x00922eac)) set_float(0x00922c84, ent, a.actor_828);  // ScrActor_SetFloat_828
    if (a.actor_82c != title_global<float>(0x00922eac)) set_float(0x00922c8c, ent, a.actor_82c);  // ScrActor_SetFloat_82c
    if (a.actor_824 != title_global<float>(0x00922eac)) set_float(0x00922c94, ent, a.actor_824);  // ScrActor_SetFloat_824
    if (a.actor_830 != undefined_int()) set_word(0x00922c9c, ent, a.actor_830);  // ScrActor_SetInt_830
    if (a.actor_7b0 != undefined_int()) set_word(0x00922ca4, ent, a.actor_7b0);  // ScrActor_SetInt_7b0
    if (a.actor_7b4 != undefined_int()) set_word(0x00922cac, ent, a.actor_7b4);  // ScrActor_SetInt_7b4
    if (a.actor_7dc != undefined_int()) set_word(0x00922cb4, ent, a.actor_7dc);  // ScrActor_SetShort_7dc
    if (a.actor_7b8 != title_global<float>(0x00922eac)) set_float(0x00922cbc, ent, a.actor_7b8);  // ScrActor_SetFloat_7b8
    if (a.actor_7bc != title_global<float>(0x00922eac)) set_float(0x00922cc4, ent, a.actor_7bc);  // ScrActor_SetFloat_7bc
    if (a.actor_228 != undefined_int()) set_word(0x00922ccc, ent, a.actor_228);  // ScrActor_SetInt_228
    if (a.actor_22c != undefined_int()) set_word(0x00922cd4, ent, a.actor_22c);  // ScrActor_SetInt_22c
    if (vec_defined(&a.actor_234)) set_vec(0x00922cdc, ent, a.actor_234);  // ScrActor_SetVec_234
    if (a.actor_230 != undefined_int()) set_word(0x00922ce4, ent, a.actor_230);  // ScrActor_SetInt_230
    if (a.actor_240 != undefined_handle()) set_word(0x00922cec, ent, a.actor_240);  // ScrActor_SetInt_240
    if (a.actor_194 != undefined_int()) set_word(0x00922cf4, ent, a.actor_194);  // ScrActor_SetInt_194
    if (a.actor_7a4 != title_global<float>(0x00922eac)) set_float(0x00922cfc, ent, a.actor_7a4);  // ScrActor_SetFloat_7a4
    if (a.actor_154 != title_global<float>(0x00922eac)) set_float(0x00922d04, ent, a.actor_154);  // ScrActor_SetFloat_154
    if (a.actor_7e4 != title_global<float>(0x00922eac)) set_float(0x00922d0c, ent, a.actor_7e4);  // ScrActor_SetFloat_7e4
    if (a.actor_7e8 != undefined_int()) set_word(0x00922d14, ent, a.actor_7e8);  // ScrActor_SetInt_7e8
    if (a.actor_7ec != undefined_int()) set_word(0x00922d1c, ent, a.actor_7ec);  // ScrActor_SetInt_7ec
    if (a.actor_1308 != undefined_int()) set_word(0x00922d24, ent, a.actor_1308);  // ScrActor_SetInt_1308
    if (a.actor_ec.rep) set_string(0x00922d2c, ent, &a.actor_ec);  // ScrActor_SetString_ec
    if (a.actor_f0 != undefined_handle()) set_word(0x00922d34, ent, a.actor_f0);  // ScrActor_SetInt_f0
    if (a.actor_7d8 != undefined_int()) set_word(0x00922d4c, ent, a.actor_7d8);  // ScrActor_SetInt_7d8
    if (a.actor_130c != title_global<float>(0x00922eac)) set_float(0x00922d54, ent, a.actor_130c);  // ScrActor_SetFloat_130c
    if (entity_handle_is_valid(a.actor_1310)) set_word(0x00922d5c, ent, a.actor_1310);  // ScrActor_SetRef_1310
    if (a.actor_131c != undefined_int()) set_word(0x00922d64, ent, a.actor_131c);  // ScrActor_SetInt_131c
    if (a.actor_1328 != undefined_int()) set_word(0x00922d6c, ent, a.actor_1328);  // ScrActor_SetInt_1328
    if (entity_handle_is_valid(a.actor_12a0)) set_word(0x00922d74, ent, a.actor_12a0);  // ScrActor_SetRef_12a0
    if (a.actor_cc != undefined_int()) set_word(0x00922d7c, ent, a.actor_cc);  // ScrActor_SetInt_cc
    if (a.actor_1374 != undefined_int()) set_word(0x00922d84, ent, a.actor_1374);  // ScrActor_SetByte_1374
    if (a.actor_7de != undefined_int()) set_word(0x00922d8c, ent, a.actor_7de);  // ScrActor_SetByte_7de
    if (static_cast<uint32_t>(a.sentient_18) != undefined_int()) set_float(0x00922df4, ent, static_cast<float>(a.sentient_18));  // ScrSentient_SetFloat_18
    if (a.actor_1378 != undefined_int()) set_word(0x00922d94, ent, a.actor_1378);  // ScrActor_SetInt_1378
    if (a.actor_137c != undefined_int()) set_word(0x00922d9c, ent, a.actor_137c);  // ScrActor_SetInt_137c
    if (a.actor_1384.rep) set_string(0x00922da4, ent, &a.actor_1384);  // ScrActor_SetString_1384
    if (a.actor_1388.rep) set_string(0x00922dac, ent, &a.actor_1388);  // ScrActor_SetString_1388
    if (a.actor_138c.rep) set_string(0x00922db4, ent, &a.actor_138c);  // ScrActor_SetString_138c
    if (a.actor_2c0.rep) set_string(0x00922dbc, ent, &a.actor_2c0);  // ScrActor_SetString_2c0
    if (a.actor_184 != undefined_handle()) set_word(0x00922dc4, ent, a.actor_184);  // ScrActor_SetInt_184
    if (a.sentient_30 != undefined_int()) set_word(0x00922e0c, ent, a.sentient_30);  // ScrSentient_SetInt_30
    if (a.sentient_28 != undefined_int()) set_word(0x00922dd4, ent, a.sentient_28);  // ScrSentient_SetInt_28
    ScriptString value;
    const bool debug = ScriptString_EqualsInt(ScrCall_GScr_GetCvar(&value, 0x008245f4), 1) != 0;  // "debug_attrib"
    ScriptString_Release(&value);
    if (debug) {
        uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0xc);  // ScrIface_Alloc
        if (functor) {
            functor[0] = 0x007f5ba0;
            functor[1] = 0x00122230;  // BroScr_EmptyFunc_31
            functor[2] = ent;
        }
        slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(0, 0, 0, ent, functor,
                                                                                                   0);  // Scr_CreateThread
    }
    title_function<void XB_THISCALL(RespawnerAttributes*)>(0x0052b970)(&a);  // RespawnerAttributes_Dtor
}

void XB_CDECL BroScr_WaitForever() {
    for (;;) Scr_Wait(float_bits(0x3d99999a));  // 0.075
}

void XB_CDECL BroScr_TurretStutter(uint32_t ent, float on_min, float on_max, float off_min, float off_max) {
    Scr_EndOnString(ent, pointer<const char>(0x00831f60));  // "death"
    Scr_EndOnString(ent, pointer<const char>(0x0082461c));  // "stop_stutter"
    const float off_range = off_max - off_min;
    const float on_range = on_max - on_min;
    using RandomFloat = long double XB_CDECL(float);
    for (;;) {
        Scr_Wait(static_cast<float>(slot<RandomFloat>(0x00922230)(off_range) + off_min));  // Scr_RandomFloat
        BroScr_TurretSetAlliesTeam(ent);
        Scr_Wait(static_cast<float>(slot<RandomFloat>(0x00922230)(on_range) + on_min));
        slot<EntHash>(0x00922584)(*ent_slot(ent, 0x2f68c7a5), 0xdc26d84c);  // GScr_Turret_SetMode
    }
}

void XB_CDECL BroScr_WaitTillFinished(uint32_t ent, uint32_t notify, uint32_t done) {
    ScriptString text;
    ScriptString_FromRep(&text, nullptr);
    for (;;) {
        title_function<void XB_CDECL(uint32_t, uint32_t, ScriptString*)>(0x004b14f0)(ent, notify,
                                                                                    &text);  // Scr_WaitTillOutputStr_ORAN
        if (!text.rep) ScriptString_AssignCString(&text, pointer<const char>(0x007f9e88));  // "undefined"
        uint32_t hash;
        const uint32_t h = *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&text));
        if (h == 0x000199a4 || h == 0x9aa82fee || h == 0xcc5542ad || h == done) break;
    }
    ScriptString_Release(&text);
}

namespace {

// times[i], reserving the float array first when i is past its count (DynArray_Reserve_KASSPASS).
inline float time_at(DynArray* times, uint32_t i) {
    if (i >= times->size)
        title_function<void XB_THISCALL(DynArray*, uint32_t, uint32_t)>(0x0052d530)(times, i + 1, i + 4);
    float f;
    std::memcpy(&f, &times->data[i], 4);
    return f;
}

}  // namespace

void XB_CDECL BroScr_WallHopRotate(uint32_t unused, uint32_t ent, DynArray times, int32_t split) {
    (void)unused;
    float up = 0.0f;
    uint32_t i = 0;
    for (; static_cast<int32_t>(i) < split; ++i) up = time_at(&times, i) + up;
    float down = 0.0f;
    for (i = static_cast<uint32_t>(split); static_cast<int32_t>(i) < static_cast<int32_t>(times.size); ++i) down = time_at(&times, i) + down;
    using Rotate = void XB_CDECL(uint32_t, uint32_t, float, uint32_t, uint32_t);
    slot<Rotate>(0x009227ac)(ent, 0x41f00000, up, 0, 0);  // GScr_RotatePitch(30)
    Scr_WaitTillString(ent, pointer<const char>(0x0083abc8));  // "rotatedone"
    slot<Rotate>(0x009227ac)(ent, 0xc1f00000, down, 0, 0);  // GScr_RotatePitch(-30)
    Scr_WaitTillString(ent, pointer<const char>(0x0083abc8));
    if (times.data) Scr_Free(times.data);
}

void XB_CDECL BroScr_WaitForNotifyForever(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                          ScriptString notify, ScriptString end_on) {
    if (end_on.rep) Scr_EndOnString(level_handle(), end_on.rep->text);
    if (notify.rep) {
        const uint32_t level = title_global<uint32_t>(0x00922ee0);
        const uint32_t* const self = pointer<uint32_t>(level ? level + 4 : 0);
        uint32_t hash;
        const uint32_t h = *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&notify));
        slot<EntHash>(0x009221a4)(*self, h);  // Scr_QueueNotify
        Scr_EndOnString(level_handle(), notify.rep ? notify.rep->text : nullptr);
    }
    for (;;) Scr_Wait(float_bits(0x3dcccccd));  // 0.1
}

void XB_CDECL BroScr_VehicleDeath_Flak88(uint32_t ent) {
    ScriptString_AssignCString(string_key(ent, 0xd6eafad9), pointer<const char>(0x00824654));  // "xmodel/turret_flak88_d"
    ScriptString_AssignCString(string_key(ent, 0xd70abfb1), pointer<const char>(0x008007a8));  // "explo_metal_rand"
    const uint32_t self = ent;
    // The attacker goes to the ent argument slot, as the original.
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t*)>(0x004b1590)(self, kDeath, &ent);  // Scr_WaitTillOutputEnt_ORAN
    *int_slot(self, 0xa027aabf) = 0;
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(self, string_key(self, 0xd6eafad9), 0);  // GScr_SetModel
    slot<PlaySound>(0x009221dc)(self, string_key(self, 0xd70abfb1), 0, 0, 0);                                    // GScr_PlaySound
    slot<Ent>(0x009228d0)(self);  // GScr_Vehicle_ClearTurretTarget
    ScrVec3 origin;
    origin_of(&origin, self);
    slot<void XB_CDECL(const ScrVec3*, uint32_t, uint32_t, uint32_t)>(0x00922428)(&origin, 0x43a00000, 0x443b8000,
                                                                                  0x44228000);  // GScr_RadiusDamage(320, 750, 650)
    origin_of(&origin, self);
    slot<void XB_CDECL(uint32_t, uint32_t, const ScrVec3*, uint32_t)>(0x00922444)(0x3e800000, 0x40400000, &origin,
                                                                                  0x44834000);  // GScr_Earthquake(0.25, 3, 1050)
}

void XB_CDECL BroScr_WaitTillVehicleByName(ScriptString name) {
    ScriptString copy;
    ScriptString_Copy(&copy, &name);
    uint32_t vehicle;
    BroScr_FindLevelVehicleByTargetname(&vehicle, copy);
    if (entity_handle_is_valid(vehicle)) Scr_WaitTill(vehicle, 0x08d272e5);
    ScriptString_Release(&name);
}

void XB_CDECL BroScr_TryThrowGrenadeRightHand(uint32_t ent, ScrVec3 target, float time, float fuse) {
    using GetInt = uint32_t XB_CDECL(uint32_t);
    uint32_t anim;
    float z;
    if (slot<GetInt>(0x00922dc0)(ent) == 0x070b89a4) {  // ScrActor_GetInt_184: stand
        anim = title_global<uint32_t>(0x008cf4b0);       // g_anim_stand_grenade_throw
        z = title_global<float>(0x0083ecdc);
    } else if (slot<GetInt>(0x00922dc0)(ent) == 0xc1165808) {  // crouch
        anim = title_global<uint32_t>(0x008ce5c8);              // g_anim_crouch_grenade_throw
        z = title_global<float>(0x007f5cfc);
    } else {
        return;
    }
    ScrVec3 offset;
    offset.v[0] = 0;
    offset.v[1] = 0;
    std::memcpy(&offset.v[2], &z, 4);
    BroScr_ThrowGrenadeRightHand(ent, target, time, anim, offset, fuse);
}

void XB_CDECL BroScr_ThrowGrenadeByPoseCall(uint32_t ent, ScrVec3 target) {
    BroScr_ThrowGrenadeByPose(ent, target, title_global<float>(0x00922eac));  // g_scrUndefinedFloat
}

namespace {

// The level's name (the ScriptString at +0x1c4 of the level object) equals `name`.
inline bool level_is(uint32_t name) {
    return ScriptString_Equals(pointer<const ScriptString>(title_global<uint32_t>(0x00922ee0) + 0x1c4),
                               pointer<const char>(name));
}

}  // namespace

void XB_CDECL BroScr_SetIntroScreenCvars() {
    using SetCvarInt = void XB_CDECL(const char*, uint32_t);
    const uint32_t none = undefined_handle();
    slot<SetCvarInt>(0x009222e8)(pointer<const char>(0x008344f4), 1);  // GScr_SetCvarInt "introScreen"
    ScriptString value;
    const bool from_checkpoint =
        !ScriptString_Equals(ScrCall_GScr_GetCvar(&value, 0x008353b8), pointer<const char>(0x0082e098));  // "checkpoint" != "0"
    ScriptString_Release(&value);
    if (from_checkpoint) {
        BroScr_SetIntroScreenLine(none, none, none, none);
    } else {
        if (level_is(0x0082bcb8)) {  // mons1
            BroScr_SetIntroScreenLine(0x47705ceb, 0x7231b442, 0x72399762, 0x462a0acf);
        } else if (level_is(0x0082cc0c)) {  // liberator
            BroScr_SetIntroScreenLine(0x73cf2ca2, 0x3b944fc0, 0x3b9c6ce0, 0x72957a86);
        } else if (level_is(0x0082bc28)) {  // piano_lupo
            BroScr_SetIntroScreenLine(0x92a2a489, 0x649f2381, 0x649700a1, 0x93f8f2ad);
        } else if (level_is(0x008325d0)) {  // prologue
            BroScr_SetIntroScreenLine(0xc2915e9b, 0xf61ebc51, 0xf6169f71, 0xc3cb08bf);
        } else if (level_is(0x0082bc3c)) {  // kasserine
            BroScr_SetIntroScreenLine(0x4518b395, 0x8222f339, 0x822ad019, 0x4442e5b1);
        } else if (level_is(0x0082bc48)) {  // kasserine_pass
            BroScr_SetIntroScreenLine(0x5166aa37, 0x928103f4, 0x928920d4, 0x503cfc13);
        } else if (level_is(0x0082bca0)) {  // buchholz
            BroScr_SetIntroScreenLine(0x2f1564a1, 0x21729d80, 0x217abea0, 0x2e4f3285);
        } else if (level_is(0x0082bcc0)) {  // omaha_beach
            BroScr_SetIntroScreenLine(0x5884132b, 0x72ce163c, 0x72c6351c, 0x59de450f);
        } else if (level_is(0x0082bc60)) {  // oran
            BroScr_SetIntroScreenLine(0xb71d5bca, 0x7db2dc7b, 0x7dbaff5b, 0xb6470dee);
        } else if (level_is(0x0082bc90)) {  // dragons_teeth
            BroScr_SetIntroScreenLine(0x770186e6, 0x1bb23a92, 0x1bba19b2, 0x765bd0c2);
        } else if (level_is(0x0082bc20)) {  // troina
            BroScr_SetIntroScreenLine(0x49ef5d14, 0x8a454c4d, 0x8a4d6f6d, 0x48b50b30);
        } else if (level_is(0x0082bcac)) {  // eilendorf
            BroScr_SetIntroScreenLine(0x0bd302c9, 0x6054aeb3, 0x605c8d93, 0x0a8954ed);
        } else if (level_is(0x0082bc58)) {  // tunisia
            BroScr_SetIntroScreenLine(0x99d36b9b, 0xf4c4adf9, 0xf4cc8ed9, 0x98893dbf);
        } else if (level_is(0x0082bc34)) {  // gela
            BroScr_SetIntroScreenLine(0x471d1e10, 0xaa32de55, 0xaa3afd75, 0x46474834);
        }
    }
    slot<SetCvarInt>(0x009222e8)(pointer<const char>(0x008344f4), 0);
}

void XB_CDECL BroScr_PlayerWeaponSounds(uint32_t ent) {
    ScriptString note;
    ScriptString_FromRep(&note, nullptr);
    using WeaponSound = void XB_CDECL(uint32_t, uint32_t);
    for (;;) {
        title_function<void XB_CDECL(uint32_t, uint32_t, ScriptString*)>(0x004b14f0)(ent, 0xa7062d94,
                                                                                    &note);  // Scr_WaitTillOutputStr_ORAN
        if (ScriptString_Equals(&note, pointer<const char>(0x008246b0)))  // "weapsound0"
            slot<WeaponSound>(0x009221fc)(ent, 0);                        // GScr_PlayerWeaponSound
        else if (ScriptString_Equals(&note, pointer<const char>(0x008246a4)))  // "weapsound1"
            slot<WeaponSound>(0x009221fc)(ent, 1);
        else if (ScriptString_Equals(&note, pointer<const char>(0x00824698)))  // "weapsound2"
            slot<WeaponSound>(0x009221fc)(ent, 2);
        else if (ScriptString_Equals(&note, pointer<const char>(0x0082468c)))  // "weapsound3"
            slot<WeaponSound>(0x009221fc)(ent, 3);
        else if (ScriptString_Equals(&note, pointer<const char>(0x00824680)))  // "weapsound4"
            slot<WeaponSound>(0x009221fc)(ent, 4);
    }
}

void XB_CDECL BroScr_NotifySpawnGuyDied(uint32_t unused, uint32_t spawner, uint32_t group) {
    uint32_t guy;
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t*)>(0x004b1590)(spawner, 0x262a34d8,
                                                                            &guy);  // Scr_WaitTillOutputEnt_ORAN
    // The result byte goes to the unused argument slot, as the original.
    if (!*BroScr_WaitFinishedSpawning(reinterpret_cast<uint8_t*>(&unused), guy)) Scr_WaitTill(guy, kDeath);
    --*int_slot(group, 0xb31e91b0);
    uint32_t hash;
    slot<EntHash>(0x009221a4)(group, *Scr_HashName(&hash, pointer<const char>(0x00824714)));  // Scr_QueueNotify "spawn guy died"
}

void XB_CDECL BroScr_VehicleDeath_M13(uint32_t ent) {
    ScriptString_AssignCString(string_key(ent, 0xd6eafad9), pointer<const char>(0x007fda48));  // "v_m13_d1"
    ScriptString_AssignCString(string_key(ent, 0xcc2baca1), pointer<const char>(0x007f4e40));  // "vehicle_explode"
    ScriptString_AssignCString(string_key(ent, 0xd70abfb1), pointer<const char>(0x008007a8));  // "explo_metal_rand"
    const uint32_t self = ent;
    // The attacker goes to the ent argument slot, as the original.
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t*)>(0x004b1590)(self, kDeath, &ent);  // Scr_WaitTillOutputEnt_ORAN
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(self, string_key(self, 0xd6eafad9), 0);  // GScr_SetModel
    slot<PlaySound>(0x009221dc)(self, string_key(self, 0xd70abfb1), 0, 0, 0);                                    // GScr_PlaySound
    using SetAnimKnob = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    slot<SetAnimKnob>(0x009225d8)(self, title_global<uint32_t>(0x008cd4ac), 0x3f800000, 0x3e4ccccd, 0x3f800000,
                                  1);  // GScr_SetAnimKnob(1, 0.2, 1)
    slot<PlaySound>(0x009221dc)(self, string_key(self, 0xcc2baca1), 0, 0, 0);
    ScrVec3 origin;
    origin_of(&origin, self);
    slot<void XB_CDECL(uint32_t, uint32_t, const ScrVec3*, uint32_t)>(0x00922444)(0x3e800000, 0x40400000, &origin,
                                                                                  0x44834000);  // GScr_Earthquake(0.25, 3, 1050)
}

void XB_CDECL BroScr_VehicleRiderUnload(uint32_t vehicle, uint32_t rider) {
    using IsAlive = uint32_t XB_CDECL(uint32_t);
    slot<EntHash>(0x00922188)(rider, kDeath);  // Scr_EndOn
    Scr_WaitTill(vehicle, 0xecd7b044);
    if (!slot<IsAlive>(0x00922210)(rider)) return;  // GScr_IsAlive
    *ScrField_hasweapon(rider) = 1;
    BroScr_PutGunInHand(rider, 0x07151174);
    Scr_Wait(static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922be0)(rider)));  // ScrEnt_GetDelay
    if (!slot<IsAlive>(0x00922210)(rider)) return;
    ScrVec3 origin, angles;
    ScrCall_GScr_GetTagOrigin(origin.v, &vehicle,
                              static_cast<uint32_t>(reinterpret_cast<uintptr_t>(string_key(rider, 0xb6abdc5e))));
    ScrCall_GScr_GetTagAngles(angles.v, &vehicle,
                              static_cast<uint32_t>(reinterpret_cast<uintptr_t>(string_key(rider, 0xb6abdc5e))));
    using AnimScripted = void XB_CDECL(uint32_t, uint32_t, const ScrVec3*, const ScrVec3*, uint32_t);
    slot<AnimScripted>(0x009224c0)(rider, 0x6ce9b2ce, &origin, &angles, *ScrField_h2d7db3f6(rider));  // GScr_AnimScriptedNoNotify
    Scr_WaitTillArgs(rider, 0x6ce9b2ce, 0x000199a4, 0, 0);
    if (slot<IsAlive>(0x00922210)(rider)) {
        slot<Ent>(0x0092250c)(rider);  // GScr_Unlink
        slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t)>(0x00922710)(rider, 0x070b89a4, 0xc1165808,
                                                                                0x0738b1a5);  // GScr_ActorAllowedStances
    }
    slot<EntHash>(0x009221a4)(rider, 0xa663db67);  // Scr_QueueNotify
    // The IsDefined byte goes to the vehicle argument slot, as the original.
    if (*ScrField_hf203df5e_IsDefined(reinterpret_cast<bool*>(&vehicle), rider))
        ScrCall_ScrEnt_SetAnimname(&rider, string_key(rider, 0xf203df5e));
}

void XB_CDECL BroScr_VehicleDeath_Halftrack(uint32_t ent) {
    Scr_EndOnString(ent, pointer<const char>(0x008007bc));  // "nokill"
    ScriptString tree;
    ScriptString_FromCString(&tree, pointer<const char>(0x00803cc4));  // "halftrack"
    slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009225f0)(ent, &tree);  // GScr_UseAnimTree
    ScriptString_Release(&tree);
    ScriptString_AssignCString(string_key(ent, 0xd6eafad9),
                               pointer<const char>(0x007fa2a0));  // "v_M3A1_Halftrack_troop_d1_desert"
    ScriptString_AssignCString(string_key(ent, 0xd70abfb1), pointer<const char>(0x008007a8));  // "explo_metal_rand"
    uint32_t attacker;
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t*)>(0x004b1590)(ent, kDeath, &attacker);  // Scr_WaitTillOutputEnt_ORAN
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(ent, string_key(ent, 0xd6eafad9), 0);  // GScr_SetModel
    ScrVec3 origin;
    origin_of(&origin, ent);
    slot<void XB_CDECL(uint32_t, uint32_t, const ScrVec3*, uint32_t)>(0x00922444)(0x3e800000, 0x40400000, &origin,
                                                                                  0x44834000);  // GScr_Earthquake(0.25, 3, 1050)
}

ScriptString* XB_CDECL BroScr_DroneShoot(ScriptString* out, uint32_t ent, uint32_t notify) {
    ScriptString note;
    ScriptString_FromRep(&note, nullptr);
    for (;;) {
        title_function<void XB_CDECL(uint32_t, uint32_t, ScriptString*)>(0x004b14f0)(ent, notify,
                                                                                    &note);  // Scr_WaitTillOutputStr_ORAN
        if (!note.rep) ScriptString_AssignCString(&note, pointer<const char>(0x007f9e88));  // "undefined"
        if (ScriptString_Equals(&note, pointer<const char>(0x00832b28)) ||  // "end"
            ScriptString_Equals(&note, pointer<const char>(0x007f9e7c)) ||  // "finished"
            ScriptString_Equals(&note, pointer<const char>(0x007f9e88)))    // "undefined"
            break;
        if (!ScriptString_Equals(&note, pointer<const char>(0x0082c09c))) continue;  // "fire"
        ScriptString tag;
        ScriptString_FromCString(&tag, pointer<const char>(0x008389c0));  // "tag_flash"
        ScrVec3 origin;
        tag_origin(&origin, ent, &tag);
        ScriptString_Release(&tag);
        ScriptString_FromCString(&tag, pointer<const char>(0x008389c0));
        const uint32_t lane = undefined_lane();
        ScrVec3 angles;
        angles.v[0] = lane;
        angles.v[1] = lane;
        angles.v[2] = lane;
        slot<void XB_CDECL(uint32_t, ScrVec3*, ScriptString*)>(0x00922620)(ent, &angles, &tag);  // GScr_GetTagAngles
        float forward[3] = {title_global<float>(0x00922eb8), title_global<float>(0x00922eb8),
                            title_global<float>(0x00922eb8)};
        slot<void XB_CDECL(float*, const ScrVec3*)>(0x00922290)(forward, &angles);  // Scr_AnglesToForward
        ScriptString_Release(&tag);
        ScriptString sound;
        ScriptString_FromCString(&sound, pointer<const char>(0x007fa2e4));  // "drone_muzzleflash"
        const uint32_t at[3] = {origin.v[0], origin.v[1], origin.v[2]};
        slot<PlaySoundAt>(0x009221e0)(ent, &sound, at, 0, 0);  // GScr_PlaySoundAtPosition
        ScriptString_Release(&sound);
        const float range = title_global<float>(0x0083ecc8);  // 2000
        float o[3];
        std::memcpy(o, origin.v, 12);
        const float end[3] = {forward[0] * range + o[0], o[1] + forward[1] * range, o[2] + forward[2] * range};
        slot<void XB_CDECL(const ScrVec3*, const float*)>(0x00922454)(&origin, end);  // GScr_RandomTracer
    }
    ScriptString_Copy(out, &note);
    ScriptString_Release(&note);
    return out;
}

namespace {

inline void plane_crash_think(uint32_t ent, uint32_t explode_sounds_thread) {
    using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    title_function<StartThread>(0x0011a910)(0, 0, 0, 0, explode_sounds_thread, ent);  // Scr_StartThreadFunctor1
    plane_crash_move(ent, 0x007e5bf0, 0xe6f61fa4);  // 64
}

// BroScr_WingTurretDamageWatch / _2 (identical bodies).
inline void wing_turret_damage_watch(uint32_t ent) {
    using SetHealth = void XB_CDECL(uint32_t, int32_t);
    Scr_EndOnString(ent, pointer<const char>(0x00831f60));  // "death"
    uint32_t damage = undefined_int();  // the original keeps it in the ent argument slot
    slot<SetHealth>(0x00922b94)(ent, health(ent) + 1000);  // ScrEnt_SetHealth
    for (;;) {
        title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t*)>(0x005b4da0)(ent, 0xce270885,
                                                                                &damage);  // Scr_WaitTillOutputEnt_LIBEATOR ("damage")
        if (static_cast<int32_t>(damage) >= 300) break;
        slot<SetHealth>(0x00922b94)(ent, health(ent) + static_cast<int32_t>(damage));
    }
    ScrVec3 origin;
    origin_of(&origin, ent);
    slot<void XB_CDECL(const ScrVec3*, uint32_t, uint32_t, uint32_t)>(0x00922428)(&origin, 0x40000000, 0x461c4000,
                                                                                  0x460ca000);  // GScr_RadiusDamage(2, 10000, 9000)
}

}  // namespace

void XB_CDECL BroScr_StukaPlaneCrashThink(uint32_t ent) { plane_crash_think(ent, 0x001318b0); }  // BroScr_PlayExplodeSounds
void XB_CDECL BroScr_P47PlaneCrashThink(uint32_t ent) { plane_crash_think(ent, 0x00131f00); }    // BroScr_PlayExplodeSounds_2
void XB_CDECL BroScr_WingTurretDamageWatch(uint32_t ent) { wing_turret_damage_watch(ent); }
void XB_CDECL BroScr_WingTurretDamageWatch_2(uint32_t ent) { wing_turret_damage_watch(ent); }

void XB_CDECL BroScr_VehicleDeath_PanzerIV(uint32_t ent) {
    ScriptString text;
    ScriptString_FromCString(&text, pointer<const char>(0x007f499c));  // "panzerIV"
    slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009225f0)(ent, &text);  // GScr_UseAnimTree
    ScriptString_Release(&text);
    {
        ScriptString model, dead;
        ScriptString_ConcatCString(&dead, ScrCall_ScrEnt_GetModel(&ent, &model), pointer<const char>(0x00800180));  // + "_d1"
        title_function<void XB_THISCALL(ScriptString*, const ScriptString*)>(0x0011d600)(string_key(ent, 0xd6eafad9),
                                                                                        &dead);  // ScriptString_Assign
        ScriptString_Release(&dead);
        ScriptString_Release(&model);
    }
    ScriptString_AssignCString(string_key(ent, 0xcc2baca1), pointer<const char>(0x007f4e40));  // "vehicle_explode"
    const uint32_t self = ent;
    // The attacker goes to the ent argument slot, as the original.
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t*)>(0x004b1590)(self, kDeath, &ent);  // Scr_WaitTillOutputEnt_ORAN
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(self, string_key(self, 0xd6eafad9), 0);  // GScr_SetModel
    slot<PlaySound>(0x009221dc)(self, string_key(self, 0xcc2baca1), 0, 0, 0);                                    // GScr_PlaySound
    ScrVec3 origin;
    origin_of(&origin, self);
    slot<void XB_CDECL(uint32_t, uint32_t, const ScrVec3*, uint32_t)>(0x00922444)(0x3e800000, 0x40400000, &origin,
                                                                                  0x44834000);  // GScr_Earthquake(0.25, 3, 1050)
    const uint32_t lane = undefined_lane();
    ScrVec3 none;
    none.v[0] = lane;
    none.v[1] = lane;
    none.v[2] = lane;
    const float damage = static_cast<float>(health(player()) * 2);
    slot<void XB_CDECL(uint32_t, float, const ScrVec3*)>(0x00922558)(player(), damage, &none);  // GScr_DoDamage
}

void XB_CDECL BroScr_RandomRotateLoop(uint32_t ent) {
    using RandomFloat = long double XB_CDECL(float);
    Scr_EndOnString(ent, pointer<const char>(0x00831f60));  // "death"
    ScrVec3 base;
    angles_of(&base, ent);
    float base_yaw;
    std::memcpy(&base_yaw, &base.v[1], 4);
    float target[3] = {title_global<float>(0x00922eb8), title_global<float>(0x00922eb8), title_global<float>(0x00922eb8)};
    using RotateTo = void XB_CDECL(uint32_t, const float*, float, float, float);
    for (;;) {
        const float a = static_cast<float>(slot<RandomFloat>(0x00922230)(20.0f) + title_global<float>(0x0083eb98));  // + 10
        const float b = static_cast<float>(slot<RandomFloat>(0x00922230)(3.0f) + title_global<float>(0x007bd0c8));   // + 4
        const float t = static_cast<float>(slot<RandomFloat>(0x00922230)(0.5f) + title_global<float>(0x0083eb24));   // + 0.5
        const float ease = t * title_global<float>(0x0083eb24);
        BroScr_RandomRotateStep(ent, t);
        target[0] = *float_slot(ent, 0x07355c08);
        target[1] = b * title_global<float>(0x0083ec5c) + base_yaw;  // -2
        target[2] = a * title_global<float>(0x0083ec5c);
        slot<RotateTo>(0x009227a8)(ent, target, t, ease, ease);  // GScr_RotateTo
        Scr_WaitTillString(ent, pointer<const char>(0x0083abc8));  // "rotatedone"
        BroScr_RandomRotateStep(ent, t);
        target[1] = b * title_global<float>(0x0083eba8) + base_yaw;  // 2
        target[2] = a * title_global<float>(0x0083eba8);
        slot<RotateTo>(0x009227a8)(ent, target, t, ease, ease);
        Scr_WaitTillString(ent, pointer<const char>(0x0083abc8));
    }
}

void XB_CDECL BroScr_FlakDied(uint32_t ent, int32_t flaknum) {
    ScriptString text;
    ScriptString_FromCString(&text, pointer<const char>(0x00824344));  // "flak died with flaknum "
    ScriptString_AppendInt(&text, flaknum);
    const uint32_t level = title_global<uint32_t>(0x00922ee0);
    const uint32_t* const self = pointer<uint32_t>(level ? level + 4 : 0);
    uint32_t hash;
    const uint32_t h = *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&text));
    Scr_WaitTill(*self, h);
    const uint32_t flak = ent;
    // The IsDefined byte goes to the ent argument slot, as the original.
    if (*ScrField_currentanim_IsDefined(reinterpret_cast<bool*>(&ent), flak)) {
        using SetAnimKnob = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
        slot<SetAnimKnob>(0x009225d8)(flak, *key_slot(flak, 0x9af3e4c4, 0), 0x3f800000, 0, 0, 0);  // GScr_SetAnimKnob(1)
    }
    ScriptString_Release(&text);
}

void XB_CDECL BroScr_WaitTriggerClearFlag(uint32_t trigger, uint32_t index) {
    Scr_WaitTillString(trigger, pointer<const char>(0x0083aa58));  // "trigger"
    DynArrayBytes* const flags = pointer<DynArrayBytes>(title_global<uint32_t>(0x00922ee0) + 0x548);
    if (index >= flags->size) DynArray_ResizeBytes(flags, index + 1, index + 4);
    flags->data[index] = 0;
}

void XB_CDECL BroScr_FlakPanzerKeepUnusable(uint32_t vehicle, DynArray riders) {
    using IsAlive = uint32_t XB_CDECL(uint32_t);
    for (;;) {
        slot<Ent>(0x009228b4)(vehicle);  // GScr_Vehicle_MakeUnusable
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(riders.size); ++i) {
            if (i >= riders.size) DynArray_Resize(reinterpret_cast<uint8_t*>(&riders), i + 1, i + 4);
            if (slot<IsAlive>(0x00922210)(riders.data[i])) slot<Ent>(0x009228b8)(vehicle);  // GScr_Vehicle_MakeUsable
        }
        Scr_Wait(float_bits(0x3d4ccccd));  // 0.05
    }
}

void XB_CDECL BroScr_SmokeWhenHalfHealth(uint32_t ent) {
    slot<EntHash>(0x00922188)(ent, 0xc4e110dd);  // Scr_EndOn
    const int32_t half = health(ent) / 2;
    for (;;) {
        if (health(ent) < half) {
            using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
            title_function<StartThread>(0x0011a910)(0, 0, 0, 0, 0x00133950, ent);  // Scr_StartThreadFunctor1(BroScr_EngineSmokeTrail)
            slot<EntHash>(0x009221a4)(ent, 0xc4e110dd);  // Scr_QueueNotify
        }
        Scr_Wait(float_bits(0x3f000000));  // 0.5
    }
}

void XB_CDECL BroScr_ReduceDamage(uint32_t ent) {
    using SetHealth = void XB_CDECL(uint32_t, int32_t);
    float damage = title_global<float>(0x0083eb20);  // 1
    slot<EntHash>(0x00922188)(ent, kDeath);           // Scr_EndOn
    for (;;) {
        title_function<void XB_CDECL(uint32_t, uint32_t, float*)>(0x0052d8c0)(ent, 0xce270885,
                                                                             &damage);  // Scr_WaitTillOutputEnt_KASSPASS ("damage")
        const float d = damage;
        const uint32_t back = float_to_script_int(d * title_global<float>(0x007e5be8));  // 0.88
        if (health(ent) <= 0) continue;
        if (!(title_global<float>(0x0083ee3c) > d)) continue;  // 900
        slot<SetHealth>(0x00922b94)(ent, health(ent) + static_cast<int32_t>(back));  // ScrEnt_SetHealth
    }
}

}  // namespace game
