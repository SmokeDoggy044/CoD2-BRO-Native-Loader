#include "bro_scr_level_threads.h"

#include <cstring>

#include "bro_scr_ai_anim.h"
#include "bro_scr_actor.h"
#include "bro_scr_array.h"
#include "bro_scr_arrays.h"
#include "bro_scr_ai_common.h"
#include "bro_scr_cover_turret.h"
#include "bro_scr_common.h"
#include "bro_scr_death_watch.h"
#include "bro_scr_entity_threads.h"
#include "bro_scr_flak.h"
#include "bro_scr_grenade.h"
#include "bro_scr_hud_fade.h"
#include "bro_scr_level.h"
#include "bro_scr_level_info.h"
#include "bro_scr_nodes_spawner.h"
#include "bro_scr_squad.h"
#include "bro_scr_util.h"
#include "bro_scr_vehicle.h"
#include "bro_scr_vehicle_path.h"
#include "bro_scr_wallhop.h"
#include "bro_scr_notify_threads.h"
#include "bro_scr_vehicle_health.h"
#include "bro_scr_wait_misc.h"
#include "g_scr_runtime.h"
#include "g_script.h"
#include "qcommon_core.h"
#include "scr_access.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "title_data.h"
#include "bro_scr_actor_threads.h"
#include "bro_scr_stubs.h"
#include "levels_twins.h"
#include "qcommon_fs.h"
#include "script_string.h"
#include "bro_scr_gibs.h"
#include "bro_scr_level_threads_2.h"
#include "bro_scr_thread_misc.h"
#include "bro_scr_wait_notify.h"
#include "scr_class.h"
#include "g_scr_builtins.h"

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
            title_function<StartThread>(0x0045b670)(0, 0, 0, 0, own_address(&BroScr_TankDeadGuyPlayAnim), guy, &pos, &angles,
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
    title_function<StartThread>(0x0045a6d0)(0, 0, 0, 0, own_address(&BroScr_NotifyLevelOnDeath), self, &ent);  // Scr_StartThread_ent_ent(BroScr_NotifyLevelOnDeath)
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
    title_function<StartThread>(0x004b0950)(0, 0, 0, 0, own_address(&BroScr_FlakWaitDeath_2), blend, &name,
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

namespace {

// BroScr_VehicleDeath_M13 / _Stuart (the same body with another dead model).
inline void vehicle_death_with_knob(uint32_t ent, uint32_t dead_model) {
    ScriptString_AssignCString(string_key(ent, 0xd6eafad9), pointer<const char>(dead_model));
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

}  // namespace

void XB_CDECL BroScr_VehicleDeath_M13(uint32_t ent) { vehicle_death_with_knob(ent, 0x007fda48); }     // "v_m13_d1"
void XB_CDECL BroScr_VehicleDeath_Stuart(uint32_t ent) { vehicle_death_with_knob(ent, 0x007fddd4); }  // "v_stuart_d1"

namespace {

// BroScr_VehicleRiderUnload / _2 / _3 (identical bodies).
inline void rider_unload(uint32_t vehicle, uint32_t rider) {
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

}  // namespace

void XB_CDECL BroScr_VehicleRiderUnload(uint32_t vehicle, uint32_t rider) { rider_unload(vehicle, rider); }
void XB_CDECL BroScr_VehicleRiderUnload_2(uint32_t vehicle, uint32_t rider) { rider_unload(vehicle, rider); }
void XB_CDECL BroScr_VehicleRiderUnload_3(uint32_t vehicle, uint32_t rider) { rider_unload(vehicle, rider); }

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
    own_function<StartThread>(&Scr_StartThreadFunctor1_entry)(0, 0, 0, 0, explode_sounds_thread, ent);  // Scr_StartThreadFunctor1
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

void XB_CDECL BroScr_StukaPlaneCrashThink(uint32_t ent) { plane_crash_think(ent, own_address(&BroScr_PlayExplodeSounds)); }  // BroScr_PlayExplodeSounds
void XB_CDECL BroScr_P47PlaneCrashThink(uint32_t ent) { plane_crash_think(ent, own_address(&BroScr_PlayExplodeSounds_2)); }    // BroScr_PlayExplodeSounds_2
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
        own_function<void XB_THISCALL(ScriptString*, const ScriptString*)>(&ScriptString_Assign_entry)(string_key(ent, 0xd6eafad9),
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
            own_function<StartThread>(&Scr_StartThreadFunctor1_entry)(0, 0, 0, 0, own_address(&BroScr_EngineSmokeTrail), ent);  // Scr_StartThreadFunctor1(BroScr_EngineSmokeTrail)
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

namespace {

using Reserve = void XB_THISCALL(DynArray*, uint32_t, uint32_t);

// &array[i] (4-byte elements), grown first with `reserve` (i + 1, i + 4) when i is past the count.
inline uint32_t* element_at(DynArray* array, uint32_t i, uint32_t reserve) {
    if (i >= array->size) title_function<Reserve>(reserve)(array, i + 1, i + 4);
    return &array->data[i];
}

constexpr uint32_t kReserveString = 0x0045c2e0;  // DynArray_ReserveString_PROLOGUE
constexpr uint32_t kReserveInt0 = 0x0045d480;    // DynArray_ReserveInt0_PROLOGUE

// A script string array's strings released, then its block freed.
inline void free_strings(DynArray* array) {
    uint32_t* const data = array->data;
    if (!data) return;
    for (uint32_t* p = data; p != data + array->size; ++p) ScriptString_Release(reinterpret_cast<ScriptString*>(p));
    Scr_Free(data);
}

}  // namespace

void XB_CDECL BroScr_SetupPlayerWeapons() {
    using SlotCall = uint32_t XB_CDECL(uint32_t, const uint32_t*);
    using SlotSet = void XB_CDECL(uint32_t, const uint32_t*, uint32_t);
    int32_t grenades = 0;
    {
        ScriptString name;
        ScriptString_FromCString(&name, pointer<const char>(0x0082466c));  // "stielhandgranate"
        slot<void XB_CDECL(uint32_t, ScriptString*, int32_t*)>(0x009227dc)(player(), &name,
                                                                           &grenades);  // GScr_PlayerGetWeaponTotalAmmo
        ScriptString_Release(&name);
    }
    DynArray slots = {nullptr, 0, 0};
    title_function<Reserve>(kReserveString)(&slots, 4, 7);
    ScriptString_AssignCString(reinterpret_cast<ScriptString*>(&slots.data[3]), pointer<const char>(0x0082b690));  // "grenade"
    ScriptString_AssignCString(reinterpret_cast<ScriptString*>(element_at(&slots, 2, kReserveString)),
                               pointer<const char>(0x0082b698));  // "pistol"
    ScriptString_AssignCString(reinterpret_cast<ScriptString*>(element_at(&slots, 1, kReserveString)),
                               pointer<const char>(0x00836824));  // "primaryb"
    ScriptString_AssignCString(reinterpret_cast<ScriptString*>(element_at(&slots, 0, kReserveString)),
                               pointer<const char>(0x00836830));  // "primary"
    DynArray weapons = {nullptr, 0, 0};
    DynArray ammo = {nullptr, 0, 0};
    DynArray clip = {nullptr, 0, 0};
    for (uint32_t i = 0; static_cast<int32_t>(i) < 4; ++i) {
        uint32_t* const weapon = element_at(&weapons, i, kReserveString);
        const uint32_t* s = element_at(&slots, i, kReserveString);
        slot<void XB_CDECL(uint32_t, const uint32_t*, uint32_t*)>(0x00922860)(player(), s,
                                                                             weapon);  // GScr_PlayerGetWeaponSlotWeapon
        s = element_at(&slots, i, kReserveString);
        uint32_t* const a = element_at(&ammo, i, own_address(&DynArray_Resize4));
        *a = slot<SlotCall>(0x00922868)(player(), s);  // GScr_PlayerGetWeaponSlotAmmo
        s = element_at(&slots, i, kReserveString);
        uint32_t* const c = element_at(&clip, i, own_address(&DynArray_Resize4));
        *c = slot<SlotCall>(0x00922870)(player(), s);  // GScr_PlayerGetWeaponSlotClipAmmo
    }
    slot<Ent>(0x009227d0)(player());  // GScr_PlayerTakeAllWeapons
    Scr_WaitTill(level_handle(), 0x54965009);
    for (uint32_t i = 0; static_cast<int32_t>(i) < 4; ++i) {
        uint32_t* const weapon = element_at(&weapons, i, kReserveString);
        const uint32_t* s = element_at(&slots, i, kReserveString);
        slot<void XB_CDECL(uint32_t, const uint32_t*, uint32_t*)>(0x00922864)(player(), s, weapon);  // GScr_CheckWeaponSlot
        const uint32_t* const a = element_at(&ammo, i, own_address(&DynArray_Resize4));
        s = element_at(&slots, i, kReserveString);
        slot<SlotSet>(0x0092286c)(player(), s, *a);  // GScr_PlayerSetWeaponSlotAmmo
        const uint32_t* const c = element_at(&clip, i, own_address(&DynArray_Resize4));
        s = element_at(&slots, i, kReserveString);
        slot<SlotSet>(0x00922874)(player(), s, *c);  // GScr_PlayerSetWeaponSlotClipAmmo
    }
    if (grenades > 0) {
        ScriptString name;
        ScriptString_FromCString(&name, pointer<const char>(0x0082466c));
        slot<void XB_CDECL(uint32_t, ScriptString*, int32_t)>(0x009227c8)(player(), &name,
                                                                          grenades);  // GScr_PlayerGiveWeaponWithAmmo
        ScriptString_Release(&name);
    }
    slot<EntHash>(0x009221a4)(level_handle(), 0x88c7e941);  // Scr_QueueNotify
    if (clip.data) Scr_Free(clip.data);
    if (ammo.data) Scr_Free(ammo.data);
    free_strings(&weapons);
    free_strings(&slots);
}

void XB_CDECL BroScr_InitFriendlyChainNodes() {
    DynArray nodes = {nullptr, 0, 0};
    Scr_GetNodeArrayNoKey(reinterpret_cast<uint32_t*>(&nodes));
    DynArray chain = {nullptr, 0, 0};
    DynArray by_target = {nullptr, 0, 0};
    DynArray by_targetname = {nullptr, 0, 0};
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(nodes.size); ++i) {
        using Spawnflags = uint32_t XB_CDECL(uint32_t);
        if (slot<Spawnflags>(0x00922ad0)(*element_at(&nodes, i, kReserveInt0)) & 2) continue;  // ScrPathNode_GetSpawnflags
        ScriptString text;
        const bool has_target = ScrCall_ScrPathNode_GetTarget(element_at(&nodes, i, kReserveInt0), &text)->rep != nullptr;
        ScriptString_Release(&text);
        if (!has_target) continue;
        {
            ScriptString key, target;
            ScriptString_FromCString(&key, pointer<const char>(0x0082dda8));  // "targetname"
            ScriptString* const t = ScrCall_ScrPathNode_GetTarget(element_at(&nodes, i, kReserveInt0), &target);
            Scr_GetNodeArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(t)),
                             static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&key)),
                             reinterpret_cast<uint32_t*>(&by_target));
            ScriptString_Release(&target);
            ScriptString_Release(&key);
        }
        {
            ScriptString key, name;
            ScriptString_FromCString(&key, pointer<const char>(0x00833ad0));  // "target"
            ScriptString* const n = ScrCall_ScrPathNode_GetTargetname(element_at(&nodes, i, kReserveInt0), &name);
            Scr_GetNodeArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(n)),
                             static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&key)),
                             reinterpret_cast<uint32_t*>(&by_targetname));
            ScriptString_Release(&name);
            ScriptString_Release(&key);
        }
        if (static_cast<int32_t>(by_target.size) > 0 || static_cast<int32_t>(by_targetname.size) > 0)
            ScrPtrArray_Append(reinterpret_cast<uint32_t*>(&chain), element_at(&nodes, i, kReserveInt0));
    }
    DynArray allies = {nullptr, 0, 0};
    for (;;) {
        ScriptString value;
        const bool debug = ScriptString_Equals(ScrCall_GScr_GetCvar(&value, 0x0082355c), pointer<const char>(0x0082e058));  // "chain" == "1"
        ScriptString_Release(&value);
        if (debug) {
            for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(chain.size); ++i) {
                uint32_t buffer[3];
                const uint32_t* const o = slot<GetVec>(0x00922ab8)(buffer, *element_at(&chain, i, kReserveInt0));  // ScrPathNode_GetOrigin
                ScrVec3 node;
                node.v[0] = o[0];
                node.v[1] = o[1];
                node.v[2] = o[2];
                const uint32_t lane = undefined_lane();
                ScrVec3 at;
                at.v[0] = lane;
                at.v[1] = lane;
                at.v[2] = lane;
                slot<void XB_CDECL(uint32_t, ScrVec3*)>(0x00922524)(player(), &at);  // GScr_GetOrigin
                const ScrVec3 from = at;
                distance(&from, &node);
            }
            ScriptString team;
            ScriptString_FromCString(&team, pointer<const char>(0x00832084));  // "allies"
            slot<void XB_CDECL(DynArray*, ScriptString*)>(0x009223d0)(&allies, &team);  // GScr_GetAI
            ScriptString_Release(&team);
            for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(allies.size); ++i) {
                if (i >= allies.size) DynArray_Resize(reinterpret_cast<uint8_t*>(&allies), i + 1, i + 4);
                uint32_t ref;
                slot<void XB_CDECL(uint32_t*, uint32_t)>(0x00922de0)(&ref, allies.data[i]);  // ScrSentient_GetRef_48
            }
        }
        BroScr_WaitFrame();
    }
}

void XB_CDECL BroScr_FriendlyChatTriggers(uint32_t unused, uint32_t ent) {
    (void)unused;
    DynArray triggers = {nullptr, 0, 0};
    ScriptString name, type;
    ScrCall_ScrEnt_GetTargetname(&ent, &name);
    ScriptString_FromRep(&type, nullptr);
    Scr_WaitTillString(ent, pointer<const char>(0x00831f60));  // "death"
    for (int32_t k = 0; k < 5; ++k) {
        switch (k) {  // the classnames in turn
        case 0: ScriptString_AssignCString(&type, pointer<const char>(0x00832270)); break;  // "trigger_multiple"
        case 1: ScriptString_AssignCString(&type, pointer<const char>(0x00839e64)); break;  // "trigger_once"
        case 2: ScriptString_AssignCString(&type, pointer<const char>(0x0083aa4c)); break;  // "trigger_use"
        case 3: ScriptString_AssignCString(&type, pointer<const char>(0x0083aa3c)); break;  // "trigger_damage"
        case 4: ScriptString_AssignCString(&type, pointer<const char>(0x0083aa2c)); break;  // "trigger_lookat"
        }
        using GetEntArray = uint32_t XB_CDECL(const ScriptString*, uint32_t, DynArray*, uint32_t);
        own_function<GetEntArray>(&Scr_GetEntArray)(&type, 0xa106e9bd, &triggers, 0);  // Scr_GetEntArray (classname)
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(triggers.size); ++i) {
            ScriptString text;
            bool skip = ScrCall_ScrEnt_GetTargetname(element_at(&triggers, i, own_address(&DynArray_Resize)), &text)->rep == nullptr;
            ScriptString_Release(&text);
            if (skip) continue;
            skip = !ScriptString_Equals(ScrCall_ScrEnt_GetTargetname(element_at(&triggers, i, own_address(&DynArray_Resize)), &text),
                                        pointer<const char>(0x00824724));  // "friendly_chat"
            ScriptString_Release(&text);
            if (skip) continue;
            skip = ScrCall_ScrEnt_GetTarget(element_at(&triggers, i, own_address(&DynArray_Resize)), &text)->rep == nullptr;
            ScriptString_Release(&text);
            if (skip) continue;
            const bool ours = ScriptString_EqualsString(ScrCall_ScrEnt_GetTarget(element_at(&triggers, i, own_address(&DynArray_Resize)), &text),
                                                        &name);
            ScriptString_Release(&text);
            if (ours) slot<Ent>(0x00922540)(*element_at(&triggers, i, own_address(&DynArray_Resize)));  // GScr_Delete
        }
    }
    ScriptString_Release(&type);
    ScriptString_Release(&name);
    if (triggers.data) Scr_Free(triggers.data);
}

namespace {

// The level's flak flag byte `flaknum` (bytes at level + 0x684) cleared.
inline void clear_flak_flag(uint32_t flaknum) {
    DynArrayBytes* const flags = pointer<DynArrayBytes>(title_global<uint32_t>(0x00922ee0) + 0x684);
    if (flaknum >= flags->size) DynArray_ResizeBytes(flags, flaknum + 1, flaknum + 4);
    flags->data[flaknum] = 0;
}

// *text = "flak died with flaknum <n>", queued on the level.
inline void notify_flak_died(ScriptString* text, uint32_t flaknum) {
    ScriptString_FromCString(text, pointer<const char>(0x00824344));  // "flak died with flaknum "
    ScriptString_AppendInt(text, static_cast<int32_t>(flaknum));
    const uint32_t level = title_global<uint32_t>(0x00922ee0);
    const uint32_t* const self = pointer<uint32_t>(level ? level + 4 : 0);
    uint32_t hash;
    const uint32_t h = *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(text));
    slot<EntHash>(0x009221a4)(*self, h);  // Scr_QueueNotify
}

}  // namespace

void XB_CDECL BroScr_FlakStopMovingOnDeath(uint32_t flak, uint32_t ai, uint32_t flaknum) {
    const uint32_t victim = ai;
    Scr_WaitTillString(victim, pointer<const char>(0x00831f60));  // "death"
    clear_flak_flag(flaknum);
    ScriptString text;
    notify_flak_died(&text, flaknum);
    // The IsDefined byte goes to the ai argument slot, as the original.
    if (*ScrField_h1ccaaaf3_IsDefined(reinterpret_cast<bool*>(&ai), flak) && *int_slot(flak, 0x1ccaaaf3) != 0) {
        uint32_t hash;
        slot<EntHash>(0x009221a4)(flak, *Scr_HashName(&hash, pointer<const char>(0x00824848)));  // "stop_moving"
    }
    ScrVec3 origin;
    origin_of(&origin, victim);
    float o[3];
    std::memcpy(o, origin.v, 12);
    const float step = title_global<float>(0x0083eba8);  // 2
    float to[3];
    to[2] = o[2] + title_global<float>(0x0083ece8);  // 5
    to[1] = o[1] + step;
    to[0] = o[0] + step;
    ScrVec3 angles;
    angles_of(&angles, victim);
    slot<void XB_CDECL(uint32_t, const float*, const ScrVec3*, uint32_t)>(0x009226f8)(victim, to, &angles,
                                                                                    1);  // GScr_ActorTryTeleport
    ScriptString_Release(&text);
}

void XB_CDECL BroScr_FlakDeathTrigger(uint32_t flak, uint32_t trigger, uint32_t flaknum) {
    Scr_EndOnString(trigger, pointer<const char>(0x00831f60));  // "death"
    for (;;) {
        uint32_t attacker = 0;
        title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t*)>(0x004b1590)(trigger, 0xe2a210cf,
                                                                                &attacker);  // Scr_WaitTillOutputEnt_ORAN
        bool hit = false;
        {
            ScriptString classname;
            if (ScriptString_Equals(ScrCall_ScrEnt_GetClassname(&trigger, &classname),
                                    pointer<const char>(0x0083aa3c))) {  // "trigger_damage"
                ScriptString noteworthy;
                if (ScrCall_ScrEnt_GetScriptNoteworthy(&trigger, &noteworthy)->rep != nullptr) {
                    ScriptString again;
                    hit = ScriptString_Equals(ScrCall_ScrEnt_GetScriptNoteworthy(&trigger, &again),
                                              pointer<const char>(0x00824854));  // "deathtrig"
                    ScriptString_Release(&again);
                }
                ScriptString_Release(&noteworthy);
            }
            ScriptString_Release(&classname);
        }
        if (!hit) break;
        bool by_tank = false;
        if (entity_handle_is_valid(attacker)) {
            ScriptString name;
            if (ScrCall_ScrEnt_GetTargetname(&attacker, &name)->rep != nullptr) {
                ScriptString again;
                by_tank = ScriptString_Equals(ScrCall_ScrEnt_GetTargetname(&attacker, &again),
                                              pointer<const char>(0x0082dd9c));  // "player_tank"
                ScriptString_Release(&again);
            }
            ScriptString_Release(&name);
        }
        if (by_tank) {
            uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0xc);  // ScrIface_Alloc
            if (functor) {
                functor[0] = 0x007f5ba0;
                functor[1] = own_address(&BroScr_NotifyExplode);  // BroScr_NotifyExplode
                functor[2] = flak;
            }
            slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(0, 0, 0, flak, functor,
                                                                                                       0);  // Scr_CreateThread
            break;
        }
    }
    slot<Ent>(0x00922540)(flak);  // GScr_Delete
    const uint32_t self = flak;
    // The IsDefined bytes go to the trigger argument slot, as the original.
    const int32_t offtime =
        *ScrField_script_offtime_IsDefined(reinterpret_cast<bool*>(&trigger), self) ? static_cast<int32_t>(*ScrField_script_offtime(self)) : 20000;
    const int32_t offradius =
        *ScrField_script_offradius_IsDefined(reinterpret_cast<bool*>(&trigger), self) ? static_cast<int32_t>(*ScrField_script_offradius(self)) : 350;
    const int32_t end = offtime + get_time();
    while (end > get_time()) {
        const float radius = static_cast<float>(offradius);
        ScrVec3 origin;
        origin_of(&origin, self);
        const uint32_t lane = undefined_lane();
        ScrVec3 at;
        at.v[0] = lane;
        at.v[1] = lane;
        at.v[2] = lane;
        slot<void XB_CDECL(uint32_t, ScrVec3*)>(0x00922524)(player(), &at);  // GScr_GetOrigin
        const ScrVec3 from = at;
        if (!(static_cast<long double>(radius) > distance(&from, &origin))) break;
        Scr_Wait(float_bits(0x3f800000));  // 1
    }
    clear_flak_flag(flaknum);
    ScriptString text;
    notify_flak_died(&text, flaknum);
    ScriptString_Release(&text);
}

void XB_CDECL BroScr_FlakDeathThink(uint32_t ent, uint32_t flaknum, uint8_t on_death) {
    if (on_death != title_global<uint8_t>(0x00922ed0) && on_death != 0)  // g_scrUndefinedBool
        Scr_WaitTillString(ent, pointer<const char>(0x00831f60));  // "death"
    else
        Scr_WaitTillString(ent, pointer<const char>(0x00824310));  // "explode"
    {
        const uint32_t level = title_global<uint32_t>(0x00922ee0);
        const uint32_t* const self = pointer<uint32_t>(level ? level + 4 : 0);
        uint32_t hash;
        const uint32_t h = *Scr_HashName(&hash, pointer<const char>(0x0082435c));  // "broken"
        slot<EntHash>(0x009221a4)(*self, h);                                      // Scr_QueueNotify
    }
    ScriptString sound;
    ScriptString_FromCString(&sound, pointer<const char>(0x008007a8));  // "explo_metal_rand"
    slot<PlaySound>(0x009221dc)(ent, &sound, 0, 0, 0);                  // GScr_PlaySound
    ScriptString_Release(&sound);
    ScrVec3 origin;
    origin_of(&origin, ent);
    slot<void XB_CDECL(uint32_t, uint32_t, const ScrVec3*, uint32_t)>(0x00922444)(0x3f400000, 0x40400000, &origin,
                                                                                  0x448fc000);  // GScr_Earthquake(0.75, 3, 1150)
    clear_flak_flag(flaknum);
    ScriptString text;
    notify_flak_died(&text, flaknum);
    const uint32_t self_ent = ent;
    // The IsDefined byte goes to the ent argument slot, as the original.
    if (*ScrField_script_objective_IsDefined(reinterpret_cast<bool*>(&ent), self_ent)) {
        const uint32_t level = title_global<uint32_t>(0x00922ee0);
        const uint32_t* const self = pointer<uint32_t>(level ? level + 4 : 0);
        uint32_t hash;
        const uint32_t h = *Scr_HashScriptString(
            &hash, reinterpret_cast<const uint8_t* const*>(ScrField_script_objective(self_ent)));
        slot<EntHash>(0x009221a4)(*self, h);
    }
    Scr_Wait(float_bits(0x3e6147ae));  // 0.22
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(self_ent, string_key(self_ent, 0xb8eafadd),
                                                                       0);  // GScr_SetModel
    ScriptString_Release(&text);
}

void XB_CDECL BroScr_FlakLoaderAmmoAnim(uint32_t flak, DynArray crew) {
    using IsAlive = uint32_t XB_CDECL(uint32_t);
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(crew.size); ++i) {
        const uint32_t* const member = element_at(&crew, i, own_address(&DynArray_Resize));  // DynArray_Resize
        uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
        if (functor) {
            functor[0] = 0x007f4a28;  // a two-entity functor
            functor[1] = own_address(&BroScr_EndSequenceOnDeath);  // BroScr_EndSequenceOnDeath
            functor[2] = flak;
            functor[3] = *member;
        }
        slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(0, 0, 0, flak, functor,
                                                                                                   0);  // Scr_CreateThread
    }
    Scr_WaitTillString(flak, pointer<const char>(0x00824364));  // "end_sequence"
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(crew.size); ++i) {
        if (!slot<IsAlive>(0x00922210)(*element_at(&crew, i, own_address(&DynArray_Resize)))) continue;  // GScr_IsAlive
        const uint32_t* const member = element_at(&crew, i, own_address(&DynArray_Resize));
        uint32_t hash;
        const uint32_t h = *Scr_HashName(&hash, pointer<const char>(0x00824364));
        slot<EntHash>(0x009221a4)(*member, h);  // Scr_QueueNotify
        slot<void XB_CDECL(uint32_t, int32_t)>(0x00922b94)(*element_at(&crew, i, own_address(&DynArray_Resize)), 250);  // ScrEnt_SetHealth
        if (!*bool_slot(*element_at(&crew, i, own_address(&DynArray_Resize)), 0x1ea97196)) continue;
        *bool_slot(*element_at(&crew, i, own_address(&DynArray_Resize)), 0x1ea97196) = 0;
        ScriptString tag, model;
        ScriptString_FromCString(&tag, pointer<const char>(0x00831e28));    // "tag_weapon_left"
        ScriptString_FromCString(&model, pointer<const char>(0x00824860));  // "xmodel/panzerflak_ammo"
        slot<void XB_CDECL(uint32_t, ScriptString*, ScriptString*)>(0x009224d8)(*element_at(&crew, i, own_address(&DynArray_Resize)), &model,
                                                                                &tag);  // GScr_Attach
        ScriptString_Release(&model);
        ScriptString_Release(&tag);
    }
    if (crew.data) Scr_Free(crew.data);
}

void XB_CDECL BroScr_VehicleDeath_Sdkfz222(uint32_t ent) {
    uint32_t attacker = 0;
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t*)>(0x004b1590)(ent, kDeath, &attacker);  // Scr_WaitTillOutputEnt_ORAN
    bool defined;
    if (*ScrField_mgturret_IsDefined(&defined, ent)) slot<Ent>(0x00922540)(*key_slot(ent, 0x2f68c7a5, 0));  // GScr_Delete
    {
        ScrVec3 origin;
        origin_of(&origin, ent);
        float at[3];
        std::memcpy(at, origin.v, 12);
        at[2] = at[2] + title_global<float>(0x0083ebe8);  // 128
        slot<void XB_CDECL(const float*, uint32_t, uint32_t, uint32_t)>(0x00922428)(at, 0x43000000, 0x44480000,
                                                                                    0x43c80000);  // GScr_RadiusDamage(128, 800, 400)
    }
    if (level_is(0x0082bcb8)) {  // mons1
        ScriptString_AssignCString(string_key(ent, 0xd6eafad9), pointer<const char>(0x00824880));  // "v_sdkfz222_mons_d1"
    } else {
        ScriptString model, dead;
        ScriptString_ConcatCString(&dead, ScrCall_ScrEnt_GetModel(&ent, &model), pointer<const char>(0x00800180));  // + "_d1"
        own_function<void XB_THISCALL(ScriptString*, const ScriptString*)>(&ScriptString_Assign_entry)(string_key(ent, 0xd6eafad9),
                                                                                        &dead);  // ScriptString_Assign
        ScriptString_Release(&dead);
        ScriptString_Release(&model);
    }
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(ent, string_key(ent, 0xd6eafad9), 0);  // GScr_SetModel
    ScriptString sound;
    ScriptString_FromCString(&sound, pointer<const char>(0x007f4e40));  // "vehicle_explode"
    slot<PlaySound>(0x009221dc)(ent, &sound, 0, 0, 0);                  // GScr_PlaySound
    ScriptString_Release(&sound);
    ScrVec3 origin;
    origin_of(&origin, ent);
    slot<void XB_CDECL(uint32_t, uint32_t, const ScrVec3*, uint32_t)>(0x00922444)(0x3e800000, 0x40400000, &origin,
                                                                                  0x44834000);  // GScr_Earthquake(0.25, 3, 1050)
    if (title_global<float>(0x0083eb20) > slot<long double XB_CDECL(uint32_t)>(0x009228a4)(ent)) {  // 1 > GScr_Vehicle_GetSpeedMPH
        uint32_t hash;
        slot<EntHash>(0x009221a4)(ent, *Scr_HashName(&hash, pointer<const char>(0x00824878)));  // "unload"
    }
}

void XB_CDECL BroScr_TankSpawnerThink(uint32_t self, uint32_t tank) {
    using RandomInt = int32_t XB_CDECL(int32_t);
    using RandomFloat = long double XB_CDECL(float);
    *int_slot(tank, 0x086550be) = 1;
    struct {
        uint32_t count;
        TankDeadGuyTable table;
    } guys;
    BroScr_TankSpawnerSlots_Init(&guys.count);
    DynArray ents = {nullptr, 0, 0};
    {
        ScriptString name;
        ScriptString_FromCString(&name, pointer<const char>(0x008007d0));  // "tankspawner"
        using GetEntArray = uint32_t XB_CDECL(const ScriptString*, uint32_t, DynArray*, uint32_t);
        own_function<GetEntArray>(&Scr_GetEntArray)(&name, 0x0d786b4c, &ents, 0);  // Scr_GetEntArray (by target)
        ScriptString_Release(&name);
    }
    TankDeadGuyRecord* const r = guys.table.record;
    r[0].spawner = *element_at(&ents, 0, own_address(&DynArray_Resize));  // DynArray_Resize
    r[1].spawner = *element_at(&ents, 1, own_address(&DynArray_Resize));
    r[2].spawner = *element_at(&ents, 2, own_address(&DynArray_Resize));
    r[0].anim = static_cast<int32_t>(title_global<uint32_t>(0x008cd3e0));  // g_anim_tankgetout_right
    r[1].anim = static_cast<int32_t>(title_global<uint32_t>(0x008cd3c8));  // g_anim_tankgetout_left
    r[2].anim = static_cast<int32_t>(title_global<uint32_t>(0x008cd3b0));  // g_anim_tankgetout_back
    r[0].model_anim = title_global<uint32_t>(0x008cd3ec);  // g_anim_tankgetout_rightdeath
    r[1].model_anim = title_global<uint32_t>(0x008cd3d4);  // g_anim_tankgetout_leftdeath
    r[2].model_anim = title_global<uint32_t>(0x008cd3bc);  // g_anim_tankgetout_backdeath
    r[0].model_only = static_cast<uint32_t>(slot<RandomInt>(0x0092222c)(2) - 1);  // Scr_RandomInt
    r[1].model_only = static_cast<uint32_t>(slot<RandomInt>(0x0092222c)(2) - 1);
    r[2].model_only = static_cast<uint32_t>(slot<RandomInt>(0x0092222c)(2) - 1);
    r[0].delay = 0.0f;
    r[1].delay = static_cast<float>(slot<RandomFloat>(0x00922230)(1.0f) + title_global<float>(0x0083eba8));  // + 2
    const float more = static_cast<float>(slot<RandomFloat>(0x00922230)(1.0f));
    r[2].delay = r[1].delay + more + title_global<float>(0x0083eba8);
    Scr_WaitTill(tank, 0x84785ea9);
    bool defined;
    if (*ScrField_hc54b0523_IsDefined(&defined, tank)) {
        *int_slot(tank, 0x086550be) = 0;
        if (ents.data) Scr_Free(ents.data);
        return;
    }
    ScrVec3 origin, angles;
    {
        ScriptString tag;
        ScriptString_FromCString(&tag, pointer<const char>(0x008007c4));  // "tag_hatch"
        tag_origin(&origin, tank, &tag);
        ScriptString_Release(&tag);
        ScriptString_FromCString(&tag, pointer<const char>(0x008007c4));
        const uint32_t lane = undefined_lane();
        angles.v[0] = lane;
        angles.v[1] = lane;
        angles.v[2] = lane;
        slot<void XB_CDECL(uint32_t, ScrVec3*, ScriptString*)>(0x00922620)(tank, &angles, &tag);  // GScr_GetTagAngles
        ScriptString_Release(&tag);
    }
    DynArray axis = {nullptr, 0, 0};
    const int32_t count = static_cast<int32_t>(guys.count);
    for (int32_t i = 0; i < count; ++i) {
        ScriptString team;
        ScriptString_FromCString(&team, pointer<const char>(0x0083208c));  // "axis"
        slot<void XB_CDECL(DynArray*, ScriptString*)>(0x009223d0)(&axis, &team);  // GScr_GetAI
        ScriptString_Release(&team);
        if (static_cast<int32_t>(axis.size) > 10)
            r[0].model_only = 1;
        else
            r[0].model_only = static_cast<uint32_t>(slot<RandomInt>(0x0092222c)(2) - 1);
    }
    if (!entity_handle_is_valid(r[0].spawner)) {
        r[0].model_only = 1;
        r[1].model_only = 1;
        r[2].model_only = 1;
    }
    for (uint32_t i = 0; count > 0 && static_cast<int32_t>(i) < static_cast<int32_t>(guys.count); ++i) {
        // Each use bounds-checks the index again (the last record past the end).
        if (entity_handle_is_valid(dead_guy(&guys.table, i).spawner)) {
            slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922b8c)(dead_guy(&guys.table, i).spawner, 1);  // the spawner's count
            ScrVec3 at;
            origin_of(&at, tank);
            slot<SetVec>(0x00922b24)(dead_guy(&guys.table, i).spawner, at);  // ScrEnt_SetOrigin
        }
        uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x80);  // ScrIface_Alloc
        if (functor) {
            functor[0] = 0x008245b4;
            functor[1] = own_address(&BroScr_SpawnTankDeadGuys);  // BroScr_SpawnTankDeadGuys
            functor[2] = self;
            own_function<void XB_THISCALL(uint32_t*, const void*)>(&Record54_Copy)(&functor[3], &guys);  // copy of the table
            functor[0x18] = i;
            functor[0x19] = tank;
            functor[0x1a] = origin.v[0];
            functor[0x1b] = origin.v[1];
            functor[0x1c] = origin.v[2];
            functor[0x1d] = angles.v[0];
            functor[0x1e] = angles.v[1];
            functor[0x1f] = angles.v[2];
        }
        slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(0, 0, 0, self, functor,
                                                                                                   0);  // Scr_CreateThread
    }
    Scr_Wait(float_bits(0x41200000));  // 10
    slot<EntHash>(0x009221a4)(tank, 0x9d03d3cf);  // Scr_QueueNotify
    if (axis.data) Scr_Free(axis.data);
    if (ents.data) Scr_Free(ents.data);
}

void XB_CDECL BroScr_FollowNodeChain(uint32_t ai, uint32_t node, ScriptString mode) {
    using SetFloat = void XB_CDECL(uint32_t, uint32_t);
    using GetFloat = long double XB_CDECL(uint32_t);
    using RandomInt = int32_t XB_CDECL(int32_t);
    const uint32_t self = ai;
    slot<EntHash>(0x00922188)(self, kDeath);  // Scr_EndOn
    bool used_turret = false;
    uint32_t turret = 0;
    while (node != 0 && node != 0xffffffffu) {
        // The valid byte goes to the ai argument slot, as the original.
        if (*BroScr_IsValidHandle(reinterpret_cast<uint8_t*>(&ai), node)) {
            float radius;
            const float r = *BroScr_GetNodeRadius(&radius, node);
            uint32_t r_bits;
            std::memcpy(&r_bits, &r, 4);
            slot<SetFloat>(0x00922df4)(self, r_bits);  // ScrSentient_SetFloat_18 (goal radius)
        }
        ScriptString text;
        bool has_target = ScrCall_ScrPathNode_GetTarget(&node, &text)->rep != nullptr;
        ScriptString_Release(&text);
        if (has_target) {
            using GetEnt = uint32_t XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t);
            turret = slot<GetEnt>(0x009221c4)(ScrCall_ScrPathNode_GetTarget(&node, &text), 0x0d786b4c, 0, 0, 1);  // GScr_GetEntArray
            ScriptString_Release(&text);
            bool is_turret = false;
            if (entity_handle_is_valid(turret)) {
                ScriptString classname;
                if (ScriptString_Equals(ScrCall_ScrEnt_GetClassname(&turret, &classname), pointer<const char>(0x00839e80))) {  // "misc_mg42"
                    is_turret = true;
                } else {
                    ScriptString again;
                    is_turret = ScriptString_Equals(ScrCall_ScrEnt_GetClassname(&turret, &again),
                                                    pointer<const char>(0x00839e8c));  // "misc_turret"
                    ScriptString_Release(&again);
                }
                ScriptString_Release(&classname);
            }
            if (is_turret) {
                uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
                if (functor) {
                    functor[0] = 0x007f4a28;  // a two-entity functor
                    functor[1] = own_address(&BroScr_MgWaitNewUser);  // BroScr_MgWaitNewUser
                    functor[2] = turret;
                    functor[3] = self;
                }
                slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(
                    0, 0, 0, turret, functor, 0);  // Scr_CreateThread
                slot<SetFloat>(0x00922df4)(self, 0x40800000);  // radius 4
                used_turret = true;
            }
        }
        slot<void XB_CDECL(uint32_t, const uint32_t*)>(0x00922688)(self, &node);  // GScr_SetGoalPosFromNode
        Scr_WaitTill(self, 0x0032304c);
        if (used_turret && entity_handle_is_valid(turret))
            BroScr_UseNodeTurret(self, turret, title_global<uint32_t>(0x00922ea8));
        uint8_t has_delay;
        if (*BroScr_PathNodeHasScriptDelay(&has_delay, node)) {
            float delay;
            Scr_Wait(*own_function<float* XB_CDECL(float*, uint32_t)>(ScrPathNodeField_script_delay)(&delay, node));  // ScrPathNodeField_script_delay
        }
        has_target = ScrCall_ScrPathNode_GetTarget(&node, &text)->rep != nullptr;
        ScriptString_Release(&text);
        if (!has_target) break;
        DynArray next = {nullptr, 0, 0};
        {
            ScriptString key;
            ScriptString_FromCString(&key, pointer<const char>(0x0082dda8));  // "targetname"
            ScriptString* const t = ScrCall_ScrPathNode_GetTarget(&node, &text);
            Scr_GetNodeArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(t)),
                             static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&key)), reinterpret_cast<uint32_t*>(&next));
            ScriptString_Release(&text);
            ScriptString_Release(&key);
        }
        if (static_cast<int32_t>(next.size) > 1) {
            const uint32_t pick = static_cast<uint32_t>(slot<RandomInt>(0x0092222c)(static_cast<int32_t>(next.size)));
            node = *element_at(&next, pick, kReserveInt0);
        } else {
            node = *element_at(&next, 0, kReserveInt0);
        }
        if (next.data) Scr_Free(next.data);
    }
    // The IsDefined byte goes to the ai argument slot, as the original.
    if (*ScrField_script_panzer_IsDefined(reinterpret_cast<bool*>(&ai), self) && *ScrField_script_panzer(self) == 1) {
        const uint32_t panzer_thread = title_global<uint32_t>(title_global<uint32_t>(0x00922ee0) + 0x318);
        if (panzer_thread) {
            using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
            own_function<StartThread>(&Scr_StartThreadFunctor1_entry)(0, 0, 0, 0, panzer_thread, self);  // Scr_StartThreadFunctor1
        }
    }
    bool no_last_goal = false;
    if (node != 0 && node != 0xffffffffu) {
        ScriptString noteworthy;
        if (ScrCall_ScrPathNode_GetScriptNoteworthy(&node, &noteworthy)->rep != nullptr) {
            ScriptString_Release(&noteworthy);
            no_last_goal = ScriptString_Equals(ScrCall_ScrPathNode_GetScriptNoteworthy(&node, &noteworthy),
                                               pointer<const char>(0x00824838));  // "no_last_goal"
        }
        ScriptString_Release(&noteworthy);
    }
    if (!no_last_goal) {
        bool done = false;
        if (mode.rep) {
            if (ScriptString_Equals(&mode, pointer<const char>(0x007f5cd0))) {  // "sometimes"
                if (slot<RandomInt>(0x0092222c)(100) < 50) {
                    done = true;
                } else {
                    if (title_global<float>(0x007e5c14) > slot<GetFloat>(0x00922df0)(self))  // 192 > ScrSentient_GetFloat_18
                        slot<SetFloat>(0x00922df4)(self, 0x43400000);
                    slot<EntHash>(0x0092268c)(self, player());  // GScr_SetGoalEntity
                }
            } else if (ScriptString_Equals(&mode, pointer<const char>(0x0083ac1c))) {  // "player"
                if (title_global<float>(0x0083ebe8) > slot<GetFloat>(0x00922df0)(self))  // 128
                    slot<SetFloat>(0x00922df4)(self, 0x43000000);
                slot<EntHash>(0x0092268c)(self, player());
            }
        }
        if (!done) {
            Scr_WaitTill(self, 0x0032304c);
            slot<SetFloat>(0x00922df4)(self, 0x44000000);  // radius 512
        }
    }
    ScriptString_Release(&mode);
}

namespace {

// GScr_PlayerTakeWeapon(player, weapon).
inline void take_weapon(const ScriptString* weapon) {
    slot<void XB_CDECL(uint32_t, const ScriptString*)>(0x009227cc)(player(), weapon);  // GScr_PlayerTakeWeapon
}
// A fresh panzerfaust given and switched to.
inline void give_and_switch_panzerfaust() {
    ScriptString name;
    ScriptString_FromCString(&name, pointer<const char>(0x00824750));  // "panzerfaust"
    slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009227c4)(player(), &name);  // GScr_PlayerGiveWeapon
    ScriptString_Release(&name);
    ScriptString_FromCString(&name, pointer<const char>(0x00824750));
    slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009227e0)(player(), &name);  // GScr_PlayerSwitchToWeapon
    ScriptString_Release(&name);
}

// The player's weapon in slot `slot_name` into held[index] (GScr_PlayerGetWeaponSlotWeapon); the
// array is reserved (index + 1, index + 4) after the slot name is made: when `fresh`, always.
inline void slot_weapon(uint32_t slot_name, DynArray* held, uint32_t index, bool fresh) {
    ScriptString name;
    ScriptString_FromCString(&name, pointer<const char>(slot_name));
    ScriptString* out;
    if (fresh) {
        title_function<Reserve>(kReserveString)(held, index + 1, index + 4);
        out = reinterpret_cast<ScriptString*>(&held->data[index]);
    } else {
        out = reinterpret_cast<ScriptString*>(element_at(held, index, kReserveString));
    }
    slot<void XB_CDECL(uint32_t, ScriptString*, ScriptString*)>(0x00922860)(player(), &name, out);
    ScriptString_Release(&name);
}

}  // namespace

void XB_CDECL BroScr_PanzerfaustAmmoBox(uint32_t trigger) {
    DynArray targets = {nullptr, 0, 0};
    DynArray others = {nullptr, 0, 0};
    {
        ScriptString target;
        using GetEntArray = uint32_t XB_CDECL(const ScriptString*, uint32_t, DynArray*, uint32_t);
        own_function<GetEntArray>(&Scr_GetEntArray)(ScrCall_ScrEnt_GetTarget(&trigger, &target), 0x0d786b4c, &targets,
                                                0);  // Scr_GetEntArray (by targetname)
        ScriptString_Release(&target);
    }
    uint32_t weapon = 0, box = 0;
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(targets.size); ++i) {
        ScriptString model;
        bool match = ScriptString_Equals(ScrCall_ScrEnt_GetModel(element_at(&targets, i, own_address(&DynArray_Resize)), &model),
                                         pointer<const char>(0x00823670));  // "xmodel/weapon_panzerfaust_objective"
        ScriptString_Release(&model);
        if (match) {
            weapon = *element_at(&targets, i, own_address(&DynArray_Resize));
            ScriptString name;
            ScriptString_FromCString(&name, pointer<const char>(0x00823608));  // "xmodel/weapon_panzerfaust"
            slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(weapon, &name, 0);  // GScr_SetModel
            ScriptString_Release(&name);
            continue;
        }
        match = ScriptString_Equals(ScrCall_ScrEnt_GetModel(element_at(&targets, i, own_address(&DynArray_Resize)), &model),
                                    pointer<const char>(0x00823648));  // "xmodel/ammo_panzerfaust_box2_objective"
        ScriptString_Release(&model);
        if (match) {
            box = *element_at(&targets, i, own_address(&DynArray_Resize));
            ScriptString name;
            ScriptString_FromCString(&name, pointer<const char>(0x008235e8));  // "xmodel/ammo_panzerfaust_box2"
            slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(box, &name, 0);
            ScriptString_Release(&name);
            continue;
        }
        DynArray_PushBack(reinterpret_cast<uint8_t*>(&others), element_at(&targets, i, own_address(&DynArray_Resize)));
    }
    entity_handle_is_valid(weapon);
    entity_handle_is_valid(box);
    DynArray spots = {nullptr, 0, 0};
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(others.size); ++i) {
        ScriptString classname;
        ScriptString_FromCString(&classname, pointer<const char>(0x0083ab88));  // "script_origin"
        const uint32_t zero[3] = {0, 0, 0};
        const uint32_t spot = slot<Spawn>(0x009222f0)(&classname, zero, 0);  // GScr_Spawn
        ScriptString_Release(&classname);
        ScrVec3 v;
        origin_of(&v, *element_at(&others, i, own_address(&DynArray_Resize)));
        slot<SetVec>(0x00922b24)(spot, v);  // ScrEnt_SetOrigin
        angles_of(&v, *element_at(&others, i, own_address(&DynArray_Resize)));
        slot<SetVec>(0x00922ba4)(spot, v);  // ScrEnt_SetAngles
        angles_of(&v, spot);
        float roll;
        std::memcpy(&roll, &v.v[2], 4);
        roll = roll + title_global<float>(0x0083efcc);  // 270
        std::memcpy(&v.v[2], &roll, 4);
        slot<SetVec>(0x00922ba4)(spot, v);
        slot<Ent>(0x00922540)(*element_at(&others, i, own_address(&DynArray_Resize)));  // GScr_Delete
        *element_at(&spots, i, own_address(&DynArray_Resize)) = spot;
    }
    {
        ScriptString hint;
        ScriptString_FromCString(&hint, pointer<const char>(0x0082477c));  // "DAWNVILLE_PANZER"
        slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009225b8)(trigger, &hint);  // GScr_ResetHintCache
        ScriptString_Release(&hint);
    }
    const uint32_t level = level_handle();
    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x18);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007ffb6c;
        functor[1] = own_address(&BroScr_PanzerfaustObjectiveModels);  // BroScr_PanzerfaustObjectiveModels
        functor[2] = level;
        functor[3] = trigger;
        functor[4] = weapon;
        functor[5] = box;
    }
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(0, 0, 0, level, functor,
                                                                                               0);  // Scr_CreateThread
    for (;;) {
        Scr_WaitTillString(trigger, pointer<const char>(0x0083aa58));  // "trigger"
        uint32_t hash;
        {
            const uint32_t object = title_global<uint32_t>(0x00922ee0);
            const uint32_t* const self = pointer<uint32_t>(object ? object + 4 : 0);
            const uint32_t h = *Scr_HashName(&hash, pointer<const char>(0x0082475c));  // "picked up infinite panzerfaust"
            slot<EntHash>(0x009221a4)(*self, h);                                      // Scr_QueueNotify
        }
        slot<EntHash>(0x009221a4)(trigger, *Scr_HashName(&hash, pointer<const char>(0x00823624)));  // "infinite panzerfaust objective off"
        DynArray held = {nullptr, 0, 0};
        slot_weapon(0x00836830, &held, 0, true);   // "primary"
        slot_weapon(0x00836824, &held, 1, false);  // "primaryb"
        ScriptString* w0 = reinterpret_cast<ScriptString*>(element_at(&held, 0, kReserveString));
        if (ScriptString_Equals(w0, pointer<const char>(0x00824750))) {  // "panzerfaust"
            take_weapon(reinterpret_cast<ScriptString*>(element_at(&held, 0, kReserveString)));
            Scr_Wait(float_bits(0x3dcccccd));  // 0.1
            give_and_switch_panzerfaust();
            free_strings(&held);
            continue;
        }
        if (ScriptString_Equals(reinterpret_cast<ScriptString*>(element_at(&held, 1, kReserveString)),
                                pointer<const char>(0x00824750))) {
            take_weapon(reinterpret_cast<ScriptString*>(element_at(&held, 1, kReserveString)));
            Scr_Wait(float_bits(0x3dcccccd));
            give_and_switch_panzerfaust();
            free_strings(&held);
            continue;
        }
        ScriptString slot_name, current;
        ScriptString_FromRep(&slot_name, nullptr);
        ScriptString_FromRep(&current, nullptr);
        slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009227d4)(player(), &current);  // GScr_PlayerGetCurrentWeapon
        uint32_t which;
        if (ScriptString_Equals(reinterpret_cast<ScriptString*>(element_at(&held, 0, kReserveString)),
                                pointer<const char>(0x00832430))) {  // "none"
            which = 0x00836830;  // "primary"
        } else if (ScriptString_Equals(reinterpret_cast<ScriptString*>(element_at(&held, 1, kReserveString)),
                                       pointer<const char>(0x00832430))) {
            which = 0x00836824;  // "primaryb"
        } else if (ScriptString_EqualsString(&current, reinterpret_cast<ScriptString*>(element_at(&held, 0, kReserveString)))) {
            which = 0x00836830;
        } else {
            which = 0x00836824;
        }
        ScriptString_AssignCString(&slot_name, pointer<const char>(which));
        ScriptString dropped;
        ScriptString_FromRep(&dropped, nullptr);
        slot<void XB_CDECL(uint32_t, ScriptString*, ScriptString*)>(0x00922860)(player(), &slot_name,
                                                                               &dropped);  // GScr_PlayerGetWeaponSlotWeapon
        if (!ScriptString_Equals(&dropped, pointer<const char>(0x00832430))) {
            ScriptString classname;
            ScriptString_FromRep(&classname, nullptr);
            for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(spots.size); ++i) {
                if (i >= spots.size) DynArray_Resize(reinterpret_cast<uint8_t*>(&spots), i + 1, i + 4);
                const uint32_t spot = spots.data[i];
                bool taken = false;
                if (entity_handle_is_valid(spot)) {
                    ScriptKeyTable* const table = own_function<ScriptKeyTable* XB_CDECL(uint32_t)>(&ScrCall_GScr_GetEntScriptObject)(spot);
                    if (table) {
                        const uint32_t* const value = ScriptKeyTable_Find(table, 0xda24dbe7);
                        taken = entity_handle_is_valid(value ? *value : 0) != 0;
                    }
                }
                if (taken) continue;
                ScriptString model;
                const uint32_t id = *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&dropped));
                own_function<void XB_THISCALL(ScriptString*, const ScriptString*)>(&ScriptString_Assign_entry)(
                    &classname, ScrCall_GScr_GetWeaponClassname(&model, id));  // ScriptString_Assign
                ScriptString_Release(&model);
                const uint32_t zero[3] = {0, 0, 0};
                const uint32_t item = slot<Spawn>(0x009222f0)(&classname, zero, 0);  // GScr_Spawn
                ScrVec3 v;
                origin_of(&v, *element_at(&spots, i, own_address(&DynArray_Resize)));
                slot<SetVec>(0x00922b24)(item, v);
                angles_of(&v, *element_at(&spots, i, own_address(&DynArray_Resize)));
                slot<SetVec>(0x00922ba4)(item, v);
                slot<EntHash>(0x009224fc)(item, *element_at(&spots, i, own_address(&DynArray_Resize)));  // GScr_LinkTo
                *key_slot(*element_at(&spots, i, own_address(&DynArray_Resize)), 0xda24dbe7, 0) = item;
                break;
            }
            ScriptString_Release(&classname);
        }
        take_weapon(&dropped);
        give_and_switch_panzerfaust();
        ScriptString_Release(&dropped);
        ScriptString_Release(&current);
        ScriptString_Release(&slot_name);
        free_strings(&held);
    }
}

namespace {

// BroScr_RocketExplosion's part while the player lives.
inline void rocket_shock(uint32_t ent, int32_t shock_time, uint32_t no_wait_thread, ScriptString* name_ptr) {
    ScriptString& name = *name_ptr;
    using Allow = void XB_CDECL(uint32_t, uint32_t);
    slot<Allow>(0x00922820)(player(), 0);  // GScr_PlayerAllowPmFlag80000
    slot<Allow>(0x00922824)(player(), 0);  // GScr_PlayerAllowPmFlag100000
    slot<Allow>(0x00922828)(player(), 1);  // GScr_PlayerAllowPmFlag200000
    Scr_Wait(float_bits(0x3e19999a));      // 0.15
    ScrVec3 origin;
    origin_of(&origin, ent);
    slot<void XB_CDECL(uint32_t, uint32_t, const ScrVec3*)>(0x0092262c)(player(), 0x7f, &origin);  // GScr_ViewKick(127)
    slot<void XB_CDECL(uint32_t, ScriptString*, float)>(0x00922624)(player(), &name,
                                                                   static_cast<float>(shock_time));  // GScr_ShellShock
    if (no_wait_thread == undefined_int()) {
        const uint32_t level = level_handle();
        uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
        if (functor) {
            functor[0] = 0x007f4a1c;
            functor[1] = own_address(&BroScr_ShellShockWait);  // BroScr_ShellShockWait
            functor[2] = level;
            functor[3] = static_cast<uint32_t>(shock_time);
        }
        slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(0, 0, 0, level, functor,
                                                                                                   0);  // Scr_CreateThread
    }
    Scr_Wait(float_bits(0x3fc00000));  // 1.5
    slot<Allow>(0x00922820)(player(), 1);
    slot<Allow>(0x00922824)(player(), 1);
}

}  // namespace

void XB_CDECL BroScr_RocketExplosion(uint32_t ent, int32_t shock_time, int32_t base_damage, int32_t damage_range,
                                     int32_t min_damage, uint32_t no_wait_thread, ScriptString shock) {
    ScriptString name;
    ScriptString_FromRep(&name, nullptr);
    const uint32_t undefined = undefined_int();
    if (static_cast<uint32_t>(shock_time) == undefined) shock_time = 12;
    if (static_cast<uint32_t>(base_damage) == undefined) base_damage = 150;
    if (static_cast<uint32_t>(damage_range) == undefined) damage_range = 100;
    if (static_cast<uint32_t>(min_damage) == undefined) min_damage = 100;
    if (!shock.rep)
        ScriptString_AssignCString(&name, pointer<const char>(0x00831f70));  // "default"
    else
        own_function<void XB_THISCALL(ScriptString*, const ScriptString*)>(&ScriptString_Assign_entry)(&name, &shock);  // ScriptString_Assign
    const uint32_t lane = undefined_lane();
    ScrVec3 at;
    at.v[0] = lane;
    at.v[1] = lane;
    at.v[2] = lane;
    slot<void XB_CDECL(uint32_t, ScrVec3*)>(0x00922524)(player(), &at);  // GScr_GetOrigin
    float p[3];
    std::memcpy(p, at.v, 12);
    float pos[3];
    pos[0] = p[0];
    pos[1] = p[1] + title_global<float>(0x0083ed78);  // 8
    pos[2] = p[2] + title_global<float>(0x0083eba8);  // 2
    ScrVec3 again;
    again.v[0] = lane;
    again.v[1] = lane;
    again.v[2] = lane;
    slot<void XB_CDECL(uint32_t, ScrVec3*)>(0x00922524)(player(), &again);
    const float max_damage = static_cast<float>(slot<int32_t XB_CDECL(int32_t)>(0x0092222c)(damage_range) + base_damage);  // Scr_RandomInt
    const float min_f = static_cast<float>(min_damage);
    {
        ScriptString sound;
        ScriptString_FromCString(&sound, pointer<const char>(0x007fff50));  // "weapons_rocket_explosion"
        slot<PlaySound>(0x009221dc)(player(), &sound, 0, 0, 0);            // GScr_PlaySound
        ScriptString_Release(&sound);
    }
    Scr_Wait(float_bits(0x3e800000));  // 0.25
    slot<void XB_CDECL(const float*, uint32_t, float, float)>(0x00922428)(pos, 0x43a00000, max_damage,
                                                                          min_f);  // GScr_RadiusDamage(320)
    slot<void XB_CDECL(uint32_t, uint32_t, const float*, uint32_t)>(0x00922444)(0x3f400000, 0x40000000, pos,
                                                                                0x450ca000);  // GScr_Earthquake(0.75, 2, 2250)
    slot<void XB_CDECL(uint32_t, int32_t)>(0x00922c34)(player(), 100);  // ScrEnt_SetMaxhealth
    if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922210)(player())) {  // GScr_IsAlive
        rocket_shock(ent, shock_time, no_wait_thread, &name);
    }
    ScriptString_Release(&name);
    ScriptString_Release(&shock);
}

void XB_CDECL BroScr_WingTurretDamageWatch_3(uint32_t ent) { wing_turret_damage_watch(ent); }

void XB_CDECL BroScr_VehicleDeath_Tank(uint32_t ent) {
    {
        ScriptString tree;
        ScriptString_FromCString(&tree, pointer<const char>(0x007f499c));  // "panzerIV"
        slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009225f0)(ent, &tree);  // GScr_UseAnimTree
        ScriptString_Release(&tree);
    }
    ScriptString model;
    uint32_t dead = 0;
    if (ScriptString_Equals(ScrCall_ScrEnt_GetModel(&ent, &model), pointer<const char>(0x007f790c))) dead = 0x007f790c;  // "v_sherman"
    ScriptString_Release(&model);
    if (!dead) {
        if (ScriptString_Equals(ScrCall_ScrEnt_GetModel(&ent, &model), pointer<const char>(0x00821090)))  // "v_sherman_snow"
            dead = 0x00821f50;                                                                          // "v_sherman_snow_d1"
        ScriptString_Release(&model);
    }
    if (dead) {
        ScriptString_AssignCString(string_key(ent, 0xd6eafad9), pointer<const char>(dead));
    } else {
        ScriptString* const m = ScrCall_ScrEnt_GetModel(&ent, &model);
        own_function<void XB_THISCALL(ScriptString*, const ScriptString*)>(&ScriptString_Assign_entry)(string_key(ent, 0xd6eafad9),
                                                                                        m);  // ScriptString_Assign
        ScriptString_Release(&model);
    }
    ScriptString_AssignCString(string_key(ent, 0xcc2baca1), pointer<const char>(0x007f4e40));  // "vehicle_explode"
    ScriptString_AssignCString(string_key(ent, 0xd70abfb1), pointer<const char>(0x008007a8));  // "explo_metal_rand"
    const uint32_t self = ent;
    // The attacker goes to the ent argument slot, as the original.
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t*)>(0x004b1590)(self, kDeath, &ent);  // Scr_WaitTillOutputEnt_ORAN
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(self, string_key(self, 0xd6eafad9), 0);  // GScr_SetModel
    slot<PlaySound>(0x009221dc)(self, string_key(self, 0xd70abfb1), 0, 0, 0);                                    // GScr_PlaySound
    slot<PlaySound>(0x009221dc)(self, string_key(self, 0xcc2baca1), 0, 0, 0);
    ScrVec3 origin;
    origin_of(&origin, self);
    slot<void XB_CDECL(uint32_t, uint32_t, const ScrVec3*, uint32_t)>(0x00922444)(0x3e800000, 0x40400000, &origin,
                                                                                  0x44834000);  // GScr_Earthquake(0.25, 3, 1050)
}

void XB_CDECL BroScr_VehicleShootGunsThink(uint32_t ent) {
    Scr_EndOnString(ent, pointer<const char>(0x00831f60));  // "death"
    for (;;) {
        Scr_WaitTillString(ent, pointer<const char>(0x008248c4));  // "shoot_guns"
        BroScr_VehicleMg42Fire_2(ent, 0);
    }
}

void XB_CDECL BroScr_VehicleDeathThink(uint32_t ent) {
    using SetSpeed = void XB_CDECL(uint32_t, uint32_t, uint32_t);
    const uint32_t self = ent;
    Scr_EndOnString(self, pointer<const char>(0x008007bc));  // "nokill"
    ent = 0;
    // The attacker and the IsDefined bytes go to the ent argument slot, as the original.
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t*)>(0x004b1590)(self, kDeath, &ent);  // Scr_WaitTillOutputEnt_ORAN
    if (*ScrField_hc54b0523_IsDefined(reinterpret_cast<bool*>(&ent), self)) *int_slot(self, 0x086550be) = 0;
    if (*ScrField_mgturret_IsDefined(reinterpret_cast<bool*>(&ent), self)) slot<Ent>(0x00922540)(*key_slot(self, 0x2f68c7a5, 0));  // GScr_Delete
    slot<EntHash>(0x009221a4)(self, 0x10a95eae);  // Scr_QueueNotify
    slot<Ent>(0x009228d0)(self);                  // GScr_Vehicle_ClearTurretTarget
    ScrVec3 origin;
    origin_of(&origin, self);
    slot<void XB_CDECL(uint32_t, uint32_t, const ScrVec3*, uint32_t)>(0x00922444)(0x3e800000, 0x40400000, &origin,
                                                                                  0x44834000);  // GScr_Earthquake(0.25, 3, 1050)
    if (*ScrField_rollingdeath_IsDefined(reinterpret_cast<bool*>(&ent), self)) {
        if (title_global<float>(0x0083ed78) > vehicle_speed(self))  // 8
            slot<SetSpeed>(0x00922890)(self, 0x41000000, 0x41c80000);  // GScr_Vehicle_SetSpeed(8, 25)
        Scr_WaitTill(self, 0x6b6fe7fa);
        slot<SetSpeed>(0x00922890)(self, 0, 0x41c80000);
    } else {
        slot<SetSpeed>(0x00922890)(self, 0, 0x41c80000);  // (0, 25)
    }
    Scr_Wait(float_bits(0x3dcccccd));  // 0.1
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(self, string_key(self, 0xd6eafad9), 0);  // GScr_SetModel
    Scr_Wait(float_bits(0x3e99999a));                       // 0.3
    slot<SetSpeed>(0x00922890)(self, 0, 0x461c4000);        // (0, 10000)
    slot<EntHash>(0x009221a4)(self, 0x84785ea9);            // Scr_QueueNotify
    slot<Ent>(0x00922570)(self);                            // GScr_DisconnectPaths
    slot<Ent>(0x009228b0)(self);                            // GScr_Vehicle_Clear200
    if (*ScrField_h086550be_IsDefined(reinterpret_cast<bool*>(&ent), self)) Scr_WaitTill(self, 0x9d03d3cf);
    *bool_slot(self, 0x00339844) = 1;
}

void XB_CDECL BroScr_TurretStutterCall(uint32_t ent, float min, float max) { BroScr_TurretStutter(ent, min, max, min, max); }

namespace {

// The jeep's anim tree, dead model and sound (BroScr_VehicleDeath_Jeep / BroScr_VehicleInit_Jeep).
inline void jeep_setup(uint32_t ent) {
    ScriptString tree;
    ScriptString_FromCString(&tree, pointer<const char>(0x007f4948));  // "willyjeep"
    slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009225f0)(ent, &tree);  // GScr_UseAnimTree
    ScriptString_Release(&tree);
    ScriptString_AssignCString(string_key(ent, 0xd6eafad9),
                               pointer<const char>(level_is(0x0082bcb8) ? 0x008248f4 : 0x008248e4));  // mons1: "v_willysjeep_d1_mons"
    ScriptString_AssignCString(string_key(ent, 0xd70abfb1), pointer<const char>(0x008007a8));  // "explo_metal_rand"
}

}  // namespace

void XB_CDECL BroScr_VehicleDeath_Jeep(uint32_t ent) {
    Scr_EndOnString(ent, pointer<const char>(0x008007bc));  // "nokill"
    jeep_setup(ent);
    uint32_t attacker;
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t*)>(0x004b1590)(ent, kDeath, &attacker);  // Scr_WaitTillOutputEnt_ORAN
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(ent, string_key(ent, 0xd6eafad9), 0);  // GScr_SetModel
    ScrVec3 origin;
    origin_of(&origin, ent);
    slot<void XB_CDECL(uint32_t, uint32_t, const ScrVec3*, uint32_t)>(0x00922444)(0x3e800000, 0x40400000, &origin,
                                                                                  0x44834000);  // GScr_Earthquake(0.25, 3, 1050)
}

void XB_CDECL BroScr_VehicleInit_Jeep(uint32_t ent) {
    jeep_setup(ent);
    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0xc);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f5ba0;
        functor[1] = own_address(&BroScr_VehicleDeathThink);  // BroScr_VehicleDeathThink
        functor[2] = ent;
    }
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(0, 0, 0, ent, functor,
                                                                                               0);  // Scr_CreateThread
    Scr_WaitTill(ent, 0x10a95eae);
}

uint8_t* XB_CDECL BroScr_WaitHeavyHitOrDamage(uint8_t* out, uint32_t ent, float* dir) {
    float total = 0.0f;
    float damage = 0.0f;
    uint32_t attacker = 0;
    for (;;) {
        title_function<void XB_CDECL(uint32_t, uint32_t, float*, uint32_t*)>(0x006609c0)(ent, 0xce270885, &damage,
                                                                                         &attacker);  // waits for "damage"
        ScrVec3 from, to;
        origin_of(&from, attacker);
        origin_of(&to, ent);
        float a[3], b[3];
        std::memcpy(a, from.v, 12);
        std::memcpy(b, to.v, 12);
        dir[0] = b[0] - a[0];
        dir[1] = b[1] - a[1];
        dir[2] = b[2] - a[2];
        if (damage > title_global<float>(0x0083ee28)) {  // 95
            *out = 1;
            return out;
        }
        total = damage + total;
        if (total > title_global<float>(0x0083ebf0)) break;  // 150
    }
    *out = 0;
    return out;
}

void XB_CDECL BroScr_TryThrowGrenadeRightHandCall(uint32_t ent, ScrVec3 target, float time) {
    BroScr_TryThrowGrenadeRightHand(ent, target, time, title_global<float>(0x00922eac));  // fuse: g_scrUndefinedFloat
}

void XB_CDECL BroScr_TryThrowGrenadeRightHandCall_2(uint32_t ent, ScrVec3 target) {
    const float undefined = title_global<float>(0x00922eac);  // g_scrUndefinedFloat
    BroScr_TryThrowGrenadeRightHand(ent, target, undefined, undefined);
}

namespace {

// The level object's word at `offset` (0x00922ee0 + offset).
inline uint32_t& level_field(uint32_t offset) {
    return *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x00922ee0) + offset));
}

// GScr_HudSetShader(&level hud at `offset`, "black", 640, height).
inline void black_shader(uint32_t offset, uint32_t height) {
    ScriptString shader;
    ScriptString_FromCString(&shader, reinterpret_cast<const char*>(static_cast<uintptr_t>(0x007f51f8)));  // "black"
    using SetShader = void XB_CDECL(uint32_t*, ScriptString*, uint32_t, uint32_t);
    slot<SetShader>(0x009228ec)(&level_field(offset), &shader, 0x280, height);
    ScriptString_Release(&shader);
}

// BroScr_FadeHudBlack(level entity (its first word), duration, closing, 70).
inline void fade_bars(float duration, uint32_t closing) {
    BroScr_FadeHudBlack(float_bits(level_handle()), duration, closing, title_global<float>(0x0083eda0));  // 70
}

}  // namespace

void XB_CDECL BroScr_LetterboxFade(uint32_t, float duration, uint32_t closing, uint32_t setup_weapons) {
    using Set = void XB_CDECL(uint32_t, uint32_t);
    using SetCvar = void XB_CDECL(uint32_t, uint32_t);
    if (static_cast<uint8_t>(setup_weapons) && static_cast<uint8_t>(closing)) {
        const uint32_t owner = level_handle();
        uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0xc);  // ScrIface_Alloc
        if (functor) {
            functor[0] = 0x007f5ba0;
            functor[1] = own_address(&BroScr_SetupPlayerWeapons);  // BroScr_SetupPlayerWeapons
            functor[2] = owner;
        }
        slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(
            0, 0, 0, owner, functor, 0);  // Scr_CreateThread
    }
    if (!static_cast<uint8_t>(closing)) {
        fade_bars(duration, closing);
        slot<EntHash>(0x009221a4)(level_handle(), 0x54965009);  // Scr_QueueNotify
        slot<SetCvar>(0x009222e8)(0x00835390, 0);                // GScr_SetCvarInt("letterbox_enabled", 0)
        slot<SetCvar>(0x009222e4)(0x008235b0, 0x0082e058);       // GScr_SetCvar("cg_drawgun", "1")
        slot<Ent>(0x009228e4)(1);                                // GScr_SetHudVisible
        Scr_WaitFrames(0);
        return;
    }
    slot<SetCvar>(0x009222e8)(0x00835390, 1);           // GScr_SetCvarInt("letterbox_enabled", 1)
    slot<SetCvar>(0x009222e4)(0x008235b0, 0x0082e098);  // GScr_SetCvar("cg_drawgun", "0")
    slot<Ent>(0x009228e4)(0);                           // GScr_SetHudVisible
    uint32_t handle;
    level_field(0x79c) = *slot<uint32_t* XB_CDECL(uint32_t*)>(0x0092248c)(&handle);  // GScr_NewHudElem (a new hud elem)
    slot<Set>(0x00922a48)(level_field(0x79c), 0);                                   // ScrHudElem_Set_04
    slot<Set>(0x00922a50)(level_field(0x79c), 0);                                   // ScrHudElem_Set_08
    slot<Set>(0x00922a68)(level_field(0x79c), 0);                                   // ScrHudElem_SetFloat_6c
    if (level_field(0x79c) != 0xffffffffu) black_shader(0x79c, 0);
    level_field(0x270) = *slot<uint32_t* XB_CDECL(uint32_t*)>(0x0092248c)(&handle);
    slot<Set>(0x00922a48)(level_field(0x270), 0);
    slot<Set>(0x00922a50)(level_field(0x270), 0x1e0);  // y 480
    slot<Set>(0x00922a68)(level_field(0x270), 0);
    if (level_field(0x270) != 0xffffffffu) black_shader(0x270, 0x46);
    fade_bars(duration, closing);
    if (level_field(0x79c) != 0xffffffffu) black_shader(0x79c, 0x46);
    slot<Set>(0x00922a50)(level_field(0x270), 0x19a);         // y 410
    slot<EntHash>(0x009221a4)(level_handle(), 0x71291ea8);  // Scr_QueueNotify
}

void XB_CDECL BroScr_LetterboxFadeOn(uint32_t self, float duration, uint32_t closing) {
    BroScr_LetterboxFade(self, duration, closing, 1);  // the title sets only the low byte of the last word
}

void XB_CDECL BroScr_HalftrackGunnerGrenadeHint(uint32_t, uint32_t trigger) {
    using GetEntArray = uint32_t XB_CDECL(const ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t);
    uint32_t halftrack;
    {
        ScriptString target;
        const ScriptString* const name = ScrCall_ScrEnt_GetTarget(&trigger, &target);
        halftrack = slot<GetEntArray>(0x009221c4)(name, 0x0d786b4c, 0, 0, 0);  // GScr_GetEntArray (by targetname)
        ScriptString_Release(&target);
    }
    if (entity_handle_is_valid(halftrack)) {
        bool defined;
        if (!*ScrField_h0718b2c3_IsDefined(&defined, halftrack)) {
            const uint32_t owner = level_handle();
            uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
            if (functor) {
                functor[0] = 0x007f4a28;
                functor[1] = own_address(&BroScr_FriendlyChatTriggers);  // BroScr_FriendlyChatTriggers
                functor[2] = owner;
                functor[3] = halftrack;
            }
            slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(
                0, 0, 0, owner, functor, 0);  // Scr_CreateThread
            // The added word's upper bytes are the ECX the thread call leaves (stale): 0 here.
            *reinterpret_cast<uint8_t*>(ScrField_h0718b2c3(halftrack, 0)) = 1;
        }
        Scr_EndOnString(halftrack, pointer<const char>(0x00831f60));  // "death"
    }
    // Wait for the halftrack (when it is valid), else an allied AI, to touch a spawnflags-8 trigger, or
    // for the player when the flag is clear (then the halftrack is the allied AI nearest the player).
    uint32_t who = 0;
    for (;;) {
        title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t*)>(0x004b1590)(trigger, 0xe2a210cf, &who);  // Scr_WaitTillOutputEnt_ORAN
        if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922b30)(trigger) & 8) {  // ScrEnt_GetSpawnflags
            if (entity_handle_is_valid(halftrack)) {
                if (who == halftrack) break;
                continue;
            }
            ScriptString team;
            const bool allies = ScriptString_Equals(ScrCall_ScrSentient_GetTeam(&who, &team), pointer<const char>(0x00832084));
            ScriptString_Release(&team);
            if (!allies) continue;
            halftrack = who;
            break;
        }
        if (who != player()) continue;
        if (entity_handle_is_valid(halftrack)) break;
        DynArray exclude = {};
        ScriptString team;
        ScriptString_FromCString(&team, pointer<const char>(0x00832084));  // "allies"
        uint32_t buffer[3];
        const uint32_t* const o = ScrCall_GScr_GetOrigin(buffer, pointer<uint32_t>(0x00922eec));
        ScrVec3 origin;
        origin.v[0] = o[0];
        origin.v[1] = o[1];
        origin.v[2] = o[2];
        uint32_t closest;
        halftrack = *BroScr_GetClosestAI(&closest, origin, team, &exclude);
        if (!entity_handle_is_valid(halftrack)) {
            if (exclude.data) Scr_Free(exclude.data);
            return;
        }
        if (exclude.data) Scr_Free(exclude.data);
        break;
    }
    DynArray keys = {};  // script strings
    DynArray axis = {};
    {
        ScriptString team;
        ScriptString_FromCString(&team, pointer<const char>(0x0083208c));  // "axis"
        slot<void XB_CDECL(DynArray*, ScriptString*)>(0x009223d0)(&axis, &team);  // GScr_GetAI
        ScriptString_Release(&team);
    }
    for (uint32_t i = 0; i < axis.size; ++i) {
        if (i >= axis.size) DynArray_Resize(reinterpret_cast<uint8_t*>(&axis), i + 1, i + 4);
        if (!slot<uint8_t XB_CDECL(uint32_t, uint32_t)>(0x00922530)(trigger, axis.data[i])) continue;  // GScr_IsTouching
        ScriptString noteworthy;
        bool match = ScriptString_Equals(ScrCall_ScrEnt_GetScriptNoteworthy(&trigger, &noteworthy), pointer<const char>(0x008249b0));
        ScriptString_Release(&noteworthy);
        if (match) {  // "grenades"
            Scr_WaitTillString(halftrack, pointer<const char>(0x008249b0));
            Scr_Wait(float_bits(0x3f99999a));  // 1.2
            ScriptString command;
            ScriptString_FromCString(&command, pointer<const char>(0x0082499c));  // "weaponslot grenade"
            slot<void XB_CDECL(DynArray*, ScriptString*)>(0x009223f0)(&keys, &command);  // GScr_GetKeyBinding
            ScriptString_Release(&command);
            ScriptString hint;
            ScriptString_FromCString(&hint, pointer<const char>(0x00824980));  // "SCRIPT_HINT_SWITCHTOGRENADE"
            // BroScr_EmptyFuncStrArg_8 (0x00182480): releases the string; the key array is passed unread.
            own_function<void XB_CDECL(ScriptString, DynArray*)>(&BroScr_Stub_ReleaseArg1)(hint, &keys);
            break;
        }
        ScriptString noteworthy_2;
        match = ScriptString_Equals(ScrCall_ScrEnt_GetScriptNoteworthy(&trigger, &noteworthy_2), pointer<const char>(0x00824968));
        ScriptString_Release(&noteworthy_2);
        if (match) {  // "get halftrack gunner"
            Scr_WaitTillString(halftrack, pointer<const char>(0x00824968));
            Scr_Wait(1.0f);
        }
        break;
    }
    slot<Ent>(0x00922540)(trigger);  // GScr_Delete
    if (axis.data) Scr_Free(axis.data);
    if (keys.data) {
        ScriptString* const strings = reinterpret_cast<ScriptString*>(keys.data);
        for (uint32_t i = 0; i < keys.size; ++i) ScriptString_Release(&strings[i]);
        Scr_Free(keys.data);
    }
}


namespace {

using CreateThread = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);

// Scr_CreateThread(owner, functor {vtable, body, arg}) (ScrIface_Alloc 0xc; a null functor when the
// allocation fails).
inline void thread_1(uint32_t owner, uint32_t vtable, uint32_t body, uint32_t arg) {
    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0xc);  // ScrIface_Alloc
    if (functor) {
        functor[0] = vtable;
        functor[1] = body;
        functor[2] = arg;
    }
    slot<CreateThread>(0x0092216c)(0, 0, 0, owner, functor, 0);  // Scr_CreateThread
}

// GScr_PlaySound(ent, the "vehicle_explode" sound key 0xcc2baca1, 0x2c5b1495, 0, 0).
inline void play_explode_key(uint32_t ent) {
    slot<PlaySound>(0x009221dc)(ent, string_key(ent, 0xcc2baca1), 0x2c5b1495, 0, 0);
}

}  // namespace

void XB_CDECL BroScr_VehicleInit_M13(uint32_t ent) {
    {
        ScriptString model;
        // The script compares the model with "v_m13" but sets the same dead model either way.
        const bool m13 = ScriptString_Equals(ScrCall_ScrEnt_GetModel(&ent, &model), pointer<const char>(0x007fecc0));
        ScriptString_Release(&model);
        (void)m13;
    }
    ScriptString_AssignCString(string_key(ent, 0xd6eafad9), pointer<const char>(0x007fda48));  // "v_m13_d1"
    ScriptString_AssignCString(string_key(ent, 0xd70abfb1), pointer<const char>(0x008007a8));  // "explo_metal_rand"
    thread_1(ent, 0x007f5ba0, own_address(&BroScr_VehicleDeathThink), ent);                                               // BroScr_VehicleDeathThink
    Scr_WaitTill(ent, 0x10a95eae);
    ScriptString_AssignCString(string_key(ent, 0xcc2baca1), pointer<const char>(0x007f4e40));  // "vehicle_explode"
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(ent, string_key(ent, 0xd6eafad9), 0);  // GScr_SetModel
    play_explode_key(ent);
}

void XB_CDECL BroScr_VehicleInit_Halftrack(uint32_t ent) {
    {
        ScriptString tree;
        ScriptString_FromCString(&tree, pointer<const char>(0x00803cc4));  // "halftrack"
        slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009225f0)(ent, &tree);  // GScr_UseAnimTree
        ScriptString_Release(&tree);
    }
    ScriptString_AssignCString(string_key(ent, 0xd6eafad9), pointer<const char>(0x007fa2a0));  // "v_M3A1_Halftrack_troop_d1_desert"
    ScriptString_AssignCString(string_key(ent, 0xd70abfb1), pointer<const char>(0x008007a8));  // "explo_metal_rand"
    thread_1(ent, 0x007f5ba0, own_address(&BroScr_VehicleDeathThink), ent);                                               // BroScr_VehicleDeathThink
    Scr_WaitTill(ent, 0x10a95eae);
}

void XB_CDECL BroScr_VehicleInit_PanzerIV(uint32_t ent) {
    {
        ScriptString tree;
        ScriptString_FromCString(&tree, pointer<const char>(0x007f499c));  // "panzerIV"
        slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009225f0)(ent, &tree);  // GScr_UseAnimTree
        ScriptString_Release(&tree);
    }
    bool defined;
    if (!*ScrField_deathmodel_IsDefined(&defined, ent)) {
        ScriptString model;
        ScriptString dead;
        ScriptString_ConcatCString(&dead, ScrCall_ScrEnt_GetModel(&ent, &model), pointer<const char>(0x00800180));  // "_d1"
        ScriptString_Assign(string_key(ent, 0xd6eafad9), &dead);
        ScriptString_Release(&dead);
        ScriptString_Release(&model);
    }
    ScriptString_AssignCString(string_key(ent, 0xd70abfb1), pointer<const char>(0x008007a8));  // "explo_metal_rand"
    thread_1(ent, 0x007f5ba0, own_address(&BroScr_VehicleDeathThink), ent);                                               // BroScr_VehicleDeathThink
    Scr_WaitTill(ent, 0x10a95eae);
    ScriptString_AssignCString(string_key(ent, 0xcc2baca1), pointer<const char>(0x007f4e40));  // "vehicle_explode"
    play_explode_key(ent);
}

void XB_CDECL BroScr_VehicleInit_Sherman(uint32_t ent) {
    bool match;
    {
        ScriptString model;
        match = ScriptString_Equals(ScrCall_ScrEnt_GetModel(&ent, &model), pointer<const char>(0x007f790c));  // "v_sherman"
        ScriptString_Release(&model);
    }
    if (match) {
        ScriptString_AssignCString(string_key(ent, 0xd6eafad9), pointer<const char>(0x007f4e50));  // "v_sherman_d1"
    } else {
        {
            ScriptString model;
            match = ScriptString_Equals(ScrCall_ScrEnt_GetModel(&ent, &model), pointer<const char>(0x00821090));  // "v_sherman_snow"
            ScriptString_Release(&model);
        }
        if (!match) {
            ScriptString model;
            match = ScriptString_Equals(ScrCall_ScrEnt_GetModel(&ent, &model), pointer<const char>(0x0082106c));  // "v_sherman_buchholz"
            ScriptString_Release(&model);
        }
        if (match) {
            ScriptString_AssignCString(string_key(ent, 0xd6eafad9), pointer<const char>(0x00821f50));  // "v_sherman_snow_d1"
        } else {
            {
                ScriptString model;
                match = ScriptString_Equals(ScrCall_ScrEnt_GetModel(&ent, &model), pointer<const char>(0x00816acc));  // "v_sherman_bulldozer"
                ScriptString_Release(&model);
            }
            bool defined;
            if (!match && !*ScrField_deathmodel_IsDefined(&defined, ent)) {
                ScriptString model;
                const ScriptString* const current = ScrCall_ScrEnt_GetModel(&ent, &model);
                ScriptString_Assign(ScrField_deathmodel(ent), current);
                ScriptString_Release(&model);
            }
        }
    }
    ScriptString_AssignCString(string_key(ent, 0xcc2baca1), pointer<const char>(0x007f4e40));  // "vehicle_explode"
    ScriptString_AssignCString(string_key(ent, 0xd70abfb1), pointer<const char>(0x008007a8));  // "explo_metal_rand"
    bool custom;
    if (!*ScrField_custom_kill_IsDefined(&custom, ent)) thread_1(ent, 0x007f5ba0, own_address(&BroScr_VehicleDeathThink), ent);  // BroScr_VehicleDeathThink
    Scr_WaitTill(ent, 0x10a95eae);
    play_explode_key(ent);
}


namespace {

// The DynArray element at i, grown first when i is past the end (the scripts' checked index).
inline uint32_t& element(DynArray* array, uint32_t i) {
    if (i >= array->size) DynArray_Resize(reinterpret_cast<uint8_t*>(array), i + 1, i + 4);
    return array->data[i];
}

// The friendly halftrack named by `trigger`'s target: a BroScr_FriendlyChatTriggers thread on it
// (once: bool field 0x0718b2c3), this thread ending on its death (Scr_EndOn).
inline uint32_t chat_halftrack(uint32_t* trigger) {
    using GetEntArray = uint32_t XB_CDECL(const ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t);
    uint32_t halftrack;
    {
        ScriptString target;
        const ScriptString* const name = ScrCall_ScrEnt_GetTarget(trigger, &target);
        halftrack = slot<GetEntArray>(0x009221c4)(name, 0x0d786b4c, 0, 0, 0);  // GScr_GetEntArray (by targetname)
        ScriptString_Release(&target);
    }
    if (entity_handle_is_valid(halftrack)) {
        bool defined;
        if (!*ScrField_h0718b2c3_IsDefined(&defined, halftrack)) {
            const uint32_t owner = level_handle();
            uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
            if (functor) {
                functor[0] = 0x007f4a28;
                functor[1] = own_address(&BroScr_FriendlyChatTriggers);  // BroScr_FriendlyChatTriggers
                functor[2] = owner;
                functor[3] = halftrack;
            }
            slot<CreateThread>(0x0092216c)(0, 0, 0, owner, functor, 0);  // Scr_CreateThread
            *reinterpret_cast<uint8_t*>(ScrField_h0718b2c3(halftrack, 0)) = 1;
        }
        slot<EntHash>(0x00922188)(halftrack, kDeath);  // Scr_EndOn
    }
    return halftrack;
}

// Waits (Scr_WaitTillOutputEnt 0xe2a210cf on the trigger) as BroScr_HalftrackGunnerGrenadeHint does
// for the halftrack (or an allied AI, which becomes it) with spawnflags 8, else for the player (the
// halftrack then defaults to the allied AI nearest the player). False: none found (return).
inline bool wait_chat_trigger(uint32_t trigger, uint32_t* halftrack) {
    uint32_t who = 0;
    for (;;) {
        title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t*)>(0x004b1590)(trigger, 0xe2a210cf, &who);  // Scr_WaitTillOutputEnt_ORAN
        if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922b30)(trigger) & 8) {  // ScrEnt_GetSpawnflags
            if (entity_handle_is_valid(*halftrack)) {
                if (who == *halftrack) return true;
                continue;
            }
            ScriptString team;
            const bool allies = ScriptString_Equals(ScrCall_ScrSentient_GetTeam(&who, &team), pointer<const char>(0x00832084));
            ScriptString_Release(&team);
            if (!allies) continue;
            *halftrack = who;
            return true;
        }
        if (who != player()) continue;
        if (entity_handle_is_valid(*halftrack)) return true;
        DynArray exclude = {};
        ScriptString team;
        ScriptString_FromCString(&team, pointer<const char>(0x00832084));  // "allies"
        uint32_t buffer[3];
        const uint32_t* const o = ScrCall_GScr_GetOrigin(buffer, pointer<uint32_t>(0x00922eec));
        ScrVec3 origin;
        origin.v[0] = o[0];
        origin.v[1] = o[1];
        origin.v[2] = o[2];
        uint32_t closest;
        *halftrack = *BroScr_GetClosestAI(&closest, origin, team, &exclude);
        if (!entity_handle_is_valid(*halftrack)) {
            if (exclude.data) Scr_Free(exclude.data);
            return false;
        }
        if (exclude.data) Scr_Free(exclude.data);
        return true;
    }
}

}  // namespace

void XB_CDECL BroScr_SpawnGuyDiedThink(uint32_t, uint32_t trigger) {
    uint32_t halftrack = chat_halftrack(&trigger);
    if (!wait_chat_trigger(trigger, &halftrack)) return;
    DynArray guys = {};  // the axis AI touching the trigger
    DynArray ai = {};
    {
        ScriptString team;
        ScriptString_FromCString(&team, pointer<const char>(0x0083208c));  // "axis"
        slot<void XB_CDECL(DynArray*, ScriptString*)>(0x009223d0)(&ai, &team);  // GScr_GetAI
        ScriptString_Release(&team);
    }
    for (uint32_t i = 0; i < ai.size; ++i) {
        if (slot<uint8_t XB_CDECL(uint32_t, uint32_t)>(0x00922530)(element(&ai, i), trigger))  // GScr_IsTouching
            DynArray_PushBack(reinterpret_cast<uint8_t*>(&guys), &element(&ai, i));
    }
    {
        ScriptString team;
        ScriptString_FromCString(&team, pointer<const char>(0x0083208c));  // "axis"
        slot<void XB_CDECL(DynArray*, ScriptString*)>(0x009223dc)(&ai, &team);  // GScr_GetSpawnerTeamArray
        ScriptString_Release(&team);
    }
    DynArray spawners = {};  // the axis spawners with a count left touching the trigger
    for (uint32_t i = 0; i < ai.size; ++i) {
        if (slot<int32_t XB_CDECL(uint32_t)>(0x00922b88)(element(&ai, i)) > 0 &&  // ScrEnt_GetCount
            slot<uint8_t XB_CDECL(uint32_t, uint32_t)>(0x00922530)(element(&ai, i), trigger))  // GScr_IsTouching
            DynArray_PushBack(reinterpret_cast<uint8_t*>(&spawners), &element(&ai, i));
    }
    // Entity 0's int key 0xb31e91b0: the spawners left, each counted down by a BroScr_NotifySpawnGuyDied.
    *int_slot(0, 0xb31e91b0) = spawners.size;
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(spawners.size); ++i) {
        const uint32_t owner = level_handle();
        if (i >= spawners.size) DynArray_Resize(reinterpret_cast<uint8_t*>(&spawners), i + 1, i + 4);
        uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x14);  // ScrIface_Alloc
        if (functor) {
            functor[0] = 0x007f4a10;
            functor[1] = own_address(&BroScr_NotifySpawnGuyDied);  // BroScr_NotifySpawnGuyDied
            functor[2] = owner;
            functor[3] = spawners.data[i];
            functor[4] = 0;
        }
        slot<CreateThread>(0x0092216c)(0, 0, 0, owner, functor, 0);  // Scr_CreateThread
    }
    // Until every touching guy is dead and the spawners are used up.
    for (;;) {
        while (static_cast<int32_t>(guys.size) > 0) {
            Scr_WaitTill(element(&guys, 0), kDeath);
            for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(guys.size); ++i) {
                if (!slot<uint32_t XB_CDECL(uint32_t)>(0x00922210)(element(&guys, i)))  // GScr_IsAlive
                    PtrArray_RemoveValue(&guys, element(&guys, i));
            }
        }
        if (static_cast<int32_t>(*int_slot(0, 0xb31e91b0)) <= 0) break;
        Scr_WaitTillString(0, pointer<const char>(0x00824714));  // "spawn guy died"
    }
    Scr_Wait(2.0f);
    ScriptString animname;
    ScrCall_ScrEnt_GetAnimname(&halftrack, &animname);
    {
        // Notify the level "<halftrack animname> <trigger script_noteworthy>".
        uint32_t* const level = title_global<uint32_t>(0x00922ee0) ? reinterpret_cast<uint32_t*>(title_global<uint32_t>(0x00922ee0) + 4) : nullptr;
        ScriptString noteworthy;
        const ScriptString* const note = ScrCall_ScrEnt_GetScriptNoteworthy(&trigger, &noteworthy);
        ScriptString spaced;
        ScriptString_ConcatCString(&spaced, &animname, pointer<const char>(0x0082d6e4));  // " "
        ScriptString text;
        ScriptString_Concat(&text, &spaced, note);
        uint32_t hash;
        slot<EntHash>(0x009221a4)(*level, *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&text)));  // Scr_QueueNotify
        ScriptString_Release(&text);
        ScriptString_Release(&spaced);
        ScriptString_Release(&noteworthy);
    }
    slot<Ent>(0x00922540)(trigger);  // GScr_Delete
    ScriptString_Release(&animname);
    if (spawners.data) Scr_Free(spawners.data);
    if (ai.data) Scr_Free(ai.data);
    if (guys.data) Scr_Free(guys.data);
}


void XB_CDECL BroScr_InfinitePanzerfaust() {
    DynArray respawns = {};  // the panzerfausts lying at the respawn points
    BroScr_GetPanzerfaustRespawns(&respawns);
    if (respawns.size == 0) {
        if (respawns.data) Scr_Free(respawns.data);
        return;
    }
    // A point per panzerfaust: its target (by targetname), else a script_origin spawned at it; with
    // the panzerfaust's angles.
    DynArray points = {};
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(respawns.size); ++i) {
        bool targeted;
        {
            ScriptString target;
            targeted = ScrCall_ScrEnt_GetTarget(&element(&respawns, i), &target)->rep != nullptr;
            ScriptString_Release(&target);
        }
        if (targeted) {
            ScriptString target;
            const ScriptString* const name = ScrCall_ScrEnt_GetTarget(&element(&respawns, i), &target);
            const uint32_t point = slot<uint32_t XB_CDECL(const ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t)>(
                0x009221c4)(name, 0x0d786b4c, 0, 0, 0);  // GScr_GetEntArray (by targetname)
            element(&points, i) = point;
            ScriptString_Release(&target);
        } else {
            ScriptString classname;
            ScriptString_FromCString(&classname, pointer<const char>(0x0083ab88));  // "script_origin"
            ScrVec3 o;
            origin_of(&o, element(&respawns, i));
            const uint32_t at[3] = {o.v[0], o.v[1], o.v[2]};
            const uint32_t point = slot<Spawn>(0x009222f0)(&classname, at, 0);  // GScr_Spawn
            element(&points, i) = point;
            ScriptString_Release(&classname);
        }
        ScrVec3 a;
        angles_of(&a, element(&respawns, i));
        slot<SetVec>(0x00922ba4)(element(&points, i), a);  // ScrEnt_SetAngles
    }
    // Once a panzerfaust is picked up: a new one at every point (targetname "respawn"); the old
    // ones are deleted 0.3 s later.
    for (;;) {
        do {
            Scr_Wait(3.0f);
            BroScr_GetPanzerfaustRespawns(&respawns);
        } while (static_cast<int32_t>(respawns.size) >= static_cast<int32_t>(points.size));
        uint32_t hash;
        slot<EntHash>(0x009221a4)(level_handle(), *Scr_HashName(&hash, pointer<const char>(0x0082475c)));  // Scr_QueueNotify("picked up infinite panzerfaust")
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(points.size); ++i) {
            ScriptString weapon;
            ScriptString_FromCString(&weapon, pointer<const char>(0x0082473c));  // "weapon_panzerfaust"
            ScrVec3 o;
            origin_of(&o, element(&points, i));
            const uint32_t at[3] = {o.v[0], o.v[1], o.v[2]};
            const uint32_t fresh = slot<Spawn>(0x009222f0)(&weapon, at, 0);  // GScr_Spawn
            ScriptString_Release(&weapon);
            ScrVec3 a;
            angles_of(&a, element(&points, i));
            slot<SetVec>(0x00922ba4)(fresh, a);  // ScrEnt_SetAngles
            ScriptString name;
            ScriptString_FromCString(&name, pointer<const char>(0x00824734));  // "respawn"
            ScriptString copy;
            ScriptString_Copy(&copy, &name);
            slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922b54)(fresh, copy);  // ScrEnt_SetTargetname (releases its copy)
            ScriptString_Release(&name);
        }
        Scr_Wait(float_bits(0x3e99999a));  // 0.3
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(respawns.size); ++i)
            slot<Ent>(0x00922540)(element(&respawns, i));  // GScr_Delete
    }
}


namespace {

// The plane crash scripts (Stuka / P47): effect and model keys, then after death the turrets
// deleted, "explo_metal_rand", the crash think thread, an earthquake (0.25, 3 s, 1050), a
// script_origin (entity key 0x5553515d) linked to "tag_engine_left", the crash move thread, the
// shot-down model (script_vehiclegroup 4 / 5 or none: `shot`, else the dead model key) and 0.7 s.
inline void plane_crash(uint32_t ent, uint32_t dead, void XB_CDECL delete_turrets(uint32_t), uint32_t think,
                        uint32_t move, uint32_t shot) {
    ScriptString_AssignCString(string_key(ent, 0x22282ca0), pointer<const char>(0x007f5df4));  // "plane_explode_ground_a"
    ScriptString_AssignCString(string_key(ent, 0x1cc418a4), pointer<const char>(0x007fcf04));  // "smk_trail_thick_lg_black_a"
    ScriptString_AssignCString(string_key(ent, 0xe6f61fa7), pointer<const char>(0x00824a28));  // "plane_damaged_a"
    ScriptString_AssignCString(string_key(ent, 0xe6f61fa4), pointer<const char>(0x00824a0c));  // "fire_trail_thick_lg_black_a"
    uint32_t attacker = 0;
    ScriptString_AssignCString(string_key(ent, 0xd6eafad9), pointer<const char>(dead));
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t*)>(0x004b1590)(ent, kDeath, &attacker);  // Scr_WaitTillOutputEnt_ORAN
    delete_turrets(ent);
    {
        ScriptString sound;
        ScriptString_FromCString(&sound, pointer<const char>(0x008007a8));  // "explo_metal_rand"
        slot<PlaySound>(0x009221dc)(ent, &sound, 0, 0, 0);  // GScr_PlaySound
        ScriptString_Release(&sound);
    }
    thread_1(ent, 0x007f5ba0, think, ent);
    ScrVec3 origin;
    origin_of(&origin, ent);
    slot<void XB_CDECL(uint32_t, uint32_t, const ScrVec3*, uint32_t)>(0x00922444)(0x3e800000, 0x40400000, &origin,
                                                                                  0x44834000);  // GScr_Earthquake(0.25, 3, 1050)
    {
        ScriptString classname;
        ScriptString_FromCString(&classname, pointer<const char>(0x0083ab88));  // "script_origin"
        const uint32_t at[3] = {bits(0.0f), bits(0.0f), bits(0.0f)};
        const uint32_t engine = slot<Spawn>(0x009222f0)(&classname, at, 0);  // GScr_Spawn
        *key_slot(ent, 0x5553515d, 0) = engine;
        ScriptString_Release(&classname);
    }
    {
        ScriptString tag;
        ScriptString_FromCString(&tag, pointer<const char>(0x008249dc));  // "tag_engine_left"
        const uint32_t angles_offset[3] = {bits(0.0f), bits(0.0f), bits(0.0f)};
        const uint32_t origin_offset[3] = {bits(0.0f), bits(0.0f), bits(0.0f)};
        using Link = void XB_CDECL(uint32_t, uint32_t, const ScriptString*, const uint32_t*, const uint32_t*, uint32_t);
        slot<Link>(0x009224f4)(*key_slot(ent, 0x5553515d, 0), ent, &tag, origin_offset, angles_offset, 0);  // GScr_LinkToTagWithOffset
        ScriptString_Release(&tag);
    }
    thread_1(ent, 0x007f5ba0, move, ent);
    using SetModel = void XB_CDECL(uint32_t, ScriptString*, uint32_t);
    bool defined;
    if (!*ScrField_script_vehiclegroup_IsDefined(&defined, ent) || *ScrField_script_vehiclegroup(ent) == 4 ||
        *ScrField_script_vehiclegroup(ent) == 5) {
        ScriptString model;
        ScriptString_FromCString(&model, pointer<const char>(shot));
        slot<SetModel>(0x00922548)(ent, &model, 0);  // GScr_SetModel
        ScriptString_Release(&model);
    } else {
        slot<SetModel>(0x00922548)(ent, string_key(ent, 0xd6eafad9), 0);  // GScr_SetModel
    }
    Scr_Wait(float_bits(0x3f333333));  // 0.7
}

}  // namespace

void XB_CDECL BroScr_StukaPlaneCrash(uint32_t ent) {
    plane_crash(ent, 0x008249ec, BroScr_DeleteVehicleTurrets_2, own_address(&BroScr_StukaPlaneCrashThink), own_address(&BroScr_StukaPlaneCrashMove),
                0x008249bc);  // "xmodel/vehicle_plane_stuka_shot", Think / Move, "xmodel/vehicle_plane_stuka_d"
}

void XB_CDECL BroScr_P47PlaneCrash(uint32_t ent) {
    plane_crash(ent, 0x00824a38, BroScr_DeleteVehicleTurrets_3, own_address(&BroScr_P47PlaneCrashThink), own_address(&BroScr_P47PlaneCrashMove),
                0x00824a38);  // "v_p47_d1" both, Think / Move
}


void XB_CDECL BroScr_FlipLevelFlagOnDeath(uint32_t, uint32_t ent, uint32_t index) {
    Scr_WaitTillString(ent, pointer<const char>(0x00831f60));  // "death"
    // The level's byte array key 0xe337f297 (read through the level object without a null check).
    DynArrayBytes copy;
    ScriptKeyTable* const table = ent_script_object(*pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) + 4));
    DynArrayBytes* const old = static_cast<DynArrayBytes*>(
        ScrField_he337f297_GetArrayCopy(reinterpret_cast<uint8_t*>(table) + 0x10, &copy));
    if (index >= old->size) DynArray_ResizeBytesNoFill(old, index + 1, index + 4);
    const uint8_t* const was = old->data + index;
    DynArrayBytes* const flags = reinterpret_cast<DynArrayBytes*>(static_cast<uintptr_t>(ScriptKeyTable_GetOrAddArray_4(
        ent_script_object(*pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) + 4)), 0xe337f297)));
    if (index >= flags->size) DynArray_ResizeBytesNoFill(flags, index + 1, index + 4);
    flags->data[index] = *was != 1;
    if (copy.data) Scr_Free(copy.data);
}

void XB_CDECL BroScr_FlakWaitStartTrigger(uint32_t flak, uint32_t trigger, uint32_t flaknum) {
    Scr_WaitTillString(trigger, pointer<const char>(0x0083aa58));  // "trigger"
    DynArray deaths = {};
    bool targeted;
    {
        ScriptString target;
        targeted = ScrCall_ScrEnt_GetTarget(&trigger, &target)->rep != nullptr;
        ScriptString_Release(&target);
    }
    if (targeted) {
        // A BroScr_FlakDeathTrigger thread per trigger the start trigger targets.
        ScriptString target;
        const ScriptString* const name = ScrCall_ScrEnt_GetTarget(&trigger, &target);
        Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(name)), 0x0d786b4c,
                        reinterpret_cast<uint8_t*>(&deaths), 0);  // by targetname
        ScriptString_Release(&target);
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(deaths.size); ++i) {
            const uint32_t* const death = &element(&deaths, i);
            uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x14);  // ScrIface_Alloc
            if (functor) {
                functor[0] = 0x007f4af4;
                functor[1] = own_address(&BroScr_FlakDeathTrigger);  // BroScr_FlakDeathTrigger
                functor[2] = flak;
                functor[3] = *death;
                functor[4] = flaknum;
            }
            slot<CreateThread>(0x0092216c)(0, 0, 0, flak, functor, 0);  // Scr_CreateThread
        }
    }
    slot<EntHash>(0x009221a4)(flak, 0x17349563);  // Scr_QueueNotify
    slot<Ent>(0x00922540)(trigger);              // GScr_Delete
    if (deaths.data) Scr_Free(deaths.data);
}


namespace {

// The anim-set struct of field 0xc4813666 (ScriptKeyTable_GetStruct1c).
inline const uint32_t* flak_anims(uint32_t ent) {
    return pointer<uint32_t>(ScriptKeyTable_GetStruct1c(ent_script_object(ent), 0xc4813666));
}

// The level's int array at +0x50c (a running-anim count per flak number), read without a null check.
inline uint32_t& flak_anim_count(uint32_t flaknum) {
    DynArray* const counts = pointer<DynArray>(title_global<uint32_t>(0x00922ee0) + 0x50c);
    if (flaknum >= counts->size) DynArray_ResizeInt(counts, flaknum + 1, flaknum + 4);
    return counts->data[flaknum];
}

// The "xmodel/panzerflak_ammo" clip on "tag_weapon_left": GScr_Detach or GScr_AttachWithOwner.
inline void flak_clip(uint32_t ent, bool attach) {
    ScriptString tag;
    ScriptString_FromCString(&tag, pointer<const char>(0x00831e28));  // "tag_weapon_left"
    ScriptString model;
    ScriptString_FromCString(&model, pointer<const char>(0x00824860));  // "xmodel/panzerflak_ammo"
    if (attach)
        slot<void XB_CDECL(uint32_t, ScriptString*, ScriptString*)>(0x009224d8)(ent, &model, &tag);  // GScr_Detach
    else
        slot<void XB_CDECL(uint32_t, ScriptString*, ScriptString*, uint32_t)>(0x009224d0)(ent, &model, &tag, 0);  // GScr_AttachWithOwner
    ScriptString_Release(&model);
    ScriptString_Release(&tag);
}

}  // namespace

void XB_CDECL BroScr_FlakCrewAnim(uint32_t ent, uint32_t flak, uint32_t flaknum, ScriptString animname) {
    uint32_t anim = 0;
    ScriptString note;
    ScriptString_FromRep(&note, nullptr);
    uint32_t hash;
    switch (*Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&animname))) {
        case 0xe2651df9: anim = flak_anims(ent)[0]; break;
        case 0x003bda2e: anim = flak_anims(ent)[1]; break;
        case 0x0032aa25: anim = flak_anims(ent)[2]; break;
        case 0x00351de5: anim = static_cast<const uint32_t*>(ScrField_hc4813666(ent))[5]; break;
        case 0xef455c08: anim = flak_anims(ent)[6]; break;
        default: break;
    }
    if (!anim) {
        ScriptString message;
        ScriptString_FromCString(&message, pointer<const char>(0x00824a68));  // "bad animname passed in"
        ScrLocal_ReleaseString(ent, 0, message);
    }
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922d7c)(ent, 1);  // ScrActor_SetInt_cc
    ++flak_anim_count(flaknum);
    ScrVec3 angles, origin;
    angles_of(&angles, flak);
    origin_of(&origin, flak);
    slot<void XB_CDECL(uint32_t, uint32_t, ScrVec3*, ScrVec3*, uint32_t)>(0x009224c0)(ent, 0xfd3cf694, &origin, &angles,
                                                                                     anim);  // GScr_AnimScriptedNoNotify
    // The note tracks until "end": "clip on" takes the clip off the hand, "clip off" puts it back
    // (bool key 0x1ea97196: the clip is off).
    for (;;) {
        title_function<void XB_CDECL(uint32_t, uint32_t, ScriptString*)>(0x004b14f0)(ent, 0xfd3cf694, &note);  // Scr_WaitTillOutputStr_ORAN
        if (ScriptString_Equals(&note, pointer<const char>(0x00832b28))) break;  // "end"
        if (ScriptString_Equals(&note, pointer<const char>(0x00824a60)) && !*bool_slot(ent, 0x1ea97196)) {  // "clip on"
            *bool_slot(ent, 0x1ea97196) = 1;
            flak_clip(ent, false);
        }
        if (ScriptString_Equals(&note, pointer<const char>(0x00824a54)) && *bool_slot(ent, 0x1ea97196)) {  // "clip off"
            *bool_slot(ent, 0x1ea97196) = 0;
            flak_clip(ent, true);
        }
    }
    --flak_anim_count(flaknum);
    if (flak_anim_count(flaknum) == 0) {
        ScriptString done;
        ScriptString_FromCString(&done, pointer<const char>(0x00824a44));  // "flak_anim_done"
        ScriptString_AppendInt(&done, static_cast<int32_t>(flaknum));
        slot<EntHash>(0x009221a4)(level_handle(), *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&done)));  // Scr_QueueNotify
        ScriptString_Release(&done);
    }
    ScriptString_Release(&note);
    ScriptString_Release(&animname);
}


namespace {

// The clip models of the flak panzer crew (the four "tag_clip_<n>" tags, bool keys 0xc0f54bd1 +
// n - 1: the clip is off): "clip_<n> on" takes it off once, "clip_<n> off" puts it back.
inline void flak_clip_tag(uint32_t ent, uint32_t tag_text, bool attach) {
    ScriptString tag;
    ScriptString_FromCString(&tag, pointer<const char>(tag_text));
    ScriptString model;
    ScriptString_FromCString(&model, pointer<const char>(0x00824860));  // "xmodel/panzerflak_ammo"
    if (attach)
        slot<void XB_CDECL(uint32_t, ScriptString*, ScriptString*)>(0x009224d8)(ent, &model, &tag);  // GScr_Detach
    else
        slot<void XB_CDECL(uint32_t, ScriptString*, ScriptString*, uint32_t)>(0x009224d0)(ent, &model, &tag, 0);  // GScr_AttachWithOwner
    ScriptString_Release(&model);
    ScriptString_Release(&tag);
}

inline void flak_clip_note(uint32_t ent, const ScriptString* note, uint32_t text, uint32_t key, uint32_t tag, bool off) {
    if (!ScriptString_Equals(note, pointer<const char>(text))) return;
    if (off) {
        if (*bool_slot(ent, key)) flak_clip_tag(ent, tag, true);
        *bool_slot(ent, key) = 0;
    } else {
        if (!*bool_slot(ent, key)) flak_clip_tag(ent, tag, false);
        *bool_slot(ent, key) = 1;
    }
}

inline void flak_sound(uint32_t ent, uint32_t text) {
    ScriptString sound;
    ScriptString_FromCString(&sound, pointer<const char>(text));
    slot<PlaySound>(0x009221dc)(ent, &sound, 0, 0, 0);  // GScr_PlaySound
    ScriptString_Release(&sound);
}

}  // namespace

void XB_CDECL BroScr_FlakCrewAnims(uint32_t ent, ScriptString animname, uint32_t flaknum, ScriptString type) {
    using Knob = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    uint32_t anim = 0;
    uint32_t hash;
    switch (*Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&animname))) {
        case 0xe2651df9: anim = flak_anims(ent)[0]; break;
        case 0x003bda2e: anim = static_cast<const uint32_t*>(ScrField_hc4813666(ent))[1]; break;
        case 0x0032aa25: anim = flak_anims(ent)[2]; break;
        case 0x065544c1: anim = flak_anims(ent)[3]; break;
        case 0x065544c2: anim = flak_anims(ent)[4]; break;
        case 0x00351de5: anim = static_cast<const uint32_t*>(ScrField_hc4813666(ent))[5]; break;
        case 0xef455c08: anim = static_cast<const uint32_t*>(ScrField_hc4813666(ent))[6]; break;
        default: break;
    }
    if (!anim) {
        ScriptString message;
        ScriptString_FromCString(&message, pointer<const char>(0x00824a68));  // "bad animname passed in"
        ScrLocal_ReleaseString(ent, 0, message);
    }
    slot<int32_t XB_CDECL()>(0x009223c4)();  // GScr_GetTime (result unused)
    *key_slot(ent, 0x9af3e4c4, 0) = anim;
    slot<Knob>(0x009225e0)(ent, 0x54e3b2b6, *key_slot(ent, 0x9af3e4c4, 0), 0x3f800000, 0x3e4ccccd, 0x3f800000,
                           1);  // GScr_SetFlaggedAnimKnob(1, 0.2, 1)
    // With cvar "old flak" not "on": the panzer's loop sound thread (BroScr_FlakPanzerLoopSounds for
    // "fireA", else BroScr_FlakPanzerLoopSound).
    bool new_flak;
    {
        ScriptString cvar;
        new_flak = !ScriptString_Equals(ScrCall_GScr_GetCvar(&cvar, 0x00824b54), pointer<const char>(0x008243a8));  // "old flak", "on"
        ScriptString_Release(&cvar);
    }
    if (new_flak && type.rep && ScriptString_Equals(&type, pointer<const char>(0x00824b4c))) {  // "panzer"
        using StartEntI = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*);
        if (ScriptString_Equals(&animname, pointer<const char>(0x00824b44)))  // "fireA"
            title_function<StartEntI>(0x0045a680)(0, 0, 0, 0, own_address(&BroScr_FlakPanzerLoopSounds), ent, &flaknum);  // Scr_StartThread_ent_i_PROLOGUE(BroScr_FlakPanzerLoopSounds)
        else
            title_function<StartEntI>(0x0045a680)(0, 0, 0, 0, own_address(&BroScr_FlakPanzerLoopSound), ent, &flaknum);  // (BroScr_FlakPanzerLoopSound)
    }
    // The note tracks while the level's byte flag +0x684 [flaknum] (the flak is manned) stays set,
    // until "end".
    ScriptString note;
    ScriptString_FromRep(&note, nullptr);
    for (;;) {
        DynArrayBytes* const manned = pointer<DynArrayBytes>(title_global<uint32_t>(0x00922ee0) + 0x684);
        if (flaknum >= manned->size) DynArray_ResizeBytes(manned, flaknum + 1, flaknum + 4);
        if (!manned->data[flaknum]) break;
        title_function<void XB_CDECL(uint32_t, uint32_t, ScriptString*)>(0x004b14f0)(ent, 0x54e3b2b6, &note);  // Scr_WaitTillOutputStr_ORAN
        if (ScriptString_Equals(&note, pointer<const char>(0x00832b28))) break;  // "end"
        flak_clip_note(ent, &note, 0x00824b38, 0xc0f54bd1, 0x00824b2c, false);  // "clip_1 on", "tag_clip_1"
        flak_clip_note(ent, &note, 0x00824b20, 0xc0f54bd2, 0x00824b14, false);  // "clip_2 on"
        flak_clip_note(ent, &note, 0x00824b08, 0xc0f54bd3, 0x00824afc, false);  // "clip_3 on"
        flak_clip_note(ent, &note, 0x00824af0, 0xc0f54bd4, 0x00824ae4, false);  // "clip_4 on"
        flak_clip_note(ent, &note, 0x00824ad8, 0xc0f54bd1, 0x00824b2c, true);   // "clip_1 off"
        flak_clip_note(ent, &note, 0x00824acc, 0xc0f54bd2, 0x00824b14, true);   // "clip_2 off"
        flak_clip_note(ent, &note, 0x00824ac0, 0xc0f54bd3, 0x00824afc, true);   // "clip_3 off"
        flak_clip_note(ent, &note, 0x00824ab4, 0xc0f54bd4, 0x00824ae4, true);   // "clip_4 off"
        bool fire = false;
        if (ScriptString_Equals(&note, pointer<const char>(0x00824aac))) fire = true;  // "fire_1"
        if (ScriptString_Equals(&note, pointer<const char>(0x00824aa4))) fire = true;  // "fire_2"
        if (type.rep && ScriptString_Equals(&type, pointer<const char>(0x00824b4c))) continue;  // "panzer"
        // Others: firing keeps the loop sound going (int key 0x872c50bf: until 600 ms after the last
        // shot; bool key 0x34314719: playing); past that the cooldown sound.
        if (fire) {
            const int32_t until = slot<int32_t XB_CDECL()>(0x009223c4)() + 600;  // GScr_GetTime
            *int_slot(ent, 0x872c50bf) = static_cast<uint32_t>(until);
            if (!*bool_slot(ent, 0x34314719)) {
                *bool_slot(ent, 0x34314719) = 1;
                flak_sound(ent, 0x00824a94);  // "flakpanzer_loop"
            }
            continue;
        }
        const uint32_t* const until = int_slot(ent, 0x872c50bf);
        if (slot<int32_t XB_CDECL()>(0x009223c4)() > static_cast<int32_t>(*until)) {  // GScr_GetTime
            flak_sound(ent, 0x00824a80);  // "flakpanzer_cooldown"
            *bool_slot(ent, 0x34314719) = 0;
        }
    }
    ScriptString_Release(&note);
    ScriptString_Release(&animname);
    ScriptString_Release(&type);
}


void XB_CDECL BroScr_SquadMemberThink(uint32_t ent) {
    Scr_EndOnString(ent, pointer<const char>(0x00831f60));  // "death"
    bool defined;
    if (!*ScrField_h05374cc6_IsDefined(&defined, ent)) return;
    bool none;
    {
        ScriptString target;
        none = ScrCall_ScrPathNode_GetTarget(key_slot(ent, 0x05374cc6, 0), &target)->rep == nullptr;  // the node
        ScriptString_Release(&target);
    }
    if (none) return;
    if (!*ScrField_h7212d405_IsDefined(&defined, ent)) *ScrField_h7212d405(ent) = 5;
    if (!*ScrField_h7212d513_IsDefined(&defined, ent)) *ScrField_h7212d513(ent) = 15;
    Scr_WaitTillString(ent, pointer<const char>(0x0083ad18));  // "goal"
    const int32_t max = static_cast<int32_t>(*ScrField_h7212d513(ent));
    const int32_t min = static_cast<int32_t>(*ScrField_h7212d405(ent));
    Scr_Wait(static_cast<float>(slot<int32_t XB_CDECL(int32_t, int32_t)>(0x00922234)(min, max)));  // Scr_RandomIntRange
    BroScr_SetRandomGoalNode(ent);
    thread_1(ent, 0x007f5ba0, own_address(&BroScr_SquadMemberThink), ent);  // BroScr_SquadMemberThink
}

void XB_CDECL BroScr_SquadMemberThinkOnNotify(uint32_t ent, ScriptString notify) {
    Scr_EndOnString(ent, pointer<const char>(0x00831f60));  // "death"
    bool defined;
    if (*ScrField_h05374cc6_IsDefined(&defined, ent)) {
        bool none;
        {
            ScriptString target;
            none = ScrCall_ScrPathNode_GetTarget(key_slot(ent, 0x05374cc6, 0), &target)->rep == nullptr;  // the node
            ScriptString_Release(&target);
        }
        if (!none) {
            Scr_WaitTillString(ent, notify.rep ? notify.rep->text : nullptr);
            BroScr_SetRandomGoalNode(ent);
            if (*ScrField_h05374cc6_IsDefined(&defined, ent)) {
                using Start = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, ScriptString*);
                own_function<Start>(&Scr_StartThread_ent_str_PROLOGUE_entry)(0, 0, 0, 0, own_address(&BroScr_SquadMemberThinkOnNotify), ent, &notify);  // Scr_StartThread_ent_str (this body again)
            }
        }
    }
    ScriptString_Release(&notify);
}

void XB_CDECL BroScr_VehicleExplodeSound(uint32_t ent) {
    bool defined;
    if (!*ScrField_deathmodel_IsDefined(&defined, ent)) {
        ScriptString model;
        ScriptString dead;
        ScriptString_ConcatCString(&dead, ScrCall_ScrEnt_GetModel(&ent, &model), pointer<const char>(0x00800180));  // "_d1"
        ScriptString_Assign(string_key(ent, 0xd6eafad9), &dead);
        ScriptString_Release(&dead);
        ScriptString_Release(&model);
    }
    ScriptString_AssignCString(string_key(ent, 0xd70abfb1), pointer<const char>(0x008007a8));  // "explo_metal_rand"
    thread_1(ent, 0x007f5ba0, own_address(&BroScr_VehicleDeathThink), ent);                                               // BroScr_VehicleDeathThink
    Scr_WaitTill(ent, 0x10a95eae);
    ScriptString_AssignCString(string_key(ent, 0xcc2baca1), pointer<const char>(0x007f4e40));  // "vehicle_explode"
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(ent, string_key(ent, 0xd6eafad9), 0);  // GScr_SetModel
    play_explode_key(ent);
}


namespace {

// The entity's follower array (key 0xdeca7f50, ScriptKeyTable_GetArray).
inline DynArray* followers(uint32_t ent) {
    return own_function<DynArray* XB_THISCALL(ScriptKeyTable*, uint32_t)>(&ScriptKeyTable_GetArray)(ent_script_object(ent), 0xdeca7f50);
}

// The entity's path-position debug array (key 0xaeae4e0b; the debug drawing is compiled out).
inline uint32_t path_positions(uint32_t ent) { return ScriptKeyTable_GetOrAddArray_3(ent_script_object(ent), 0xaeae4e0b); }

}  // namespace

void XB_CDECL BroScr_WaitForFollowers(uint32_t ent, int32_t count) {
    if (count > static_cast<int32_t>(followers(ent)->size)) count = static_cast<int32_t>(followers(ent)->size);
    for (int32_t left = count; left > 0; --left) Scr_WaitTill(ent, 0x92b8c65f);
}

void XB_CDECL BroScr_WaitForAllFollowersNotify(DynArray* leaders) {
    int32_t total = 0;
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(leaders->size); ++i)
        total += static_cast<int32_t>(followers(element(leaders, i))->size);
    for (int32_t left = total; left > 0; --left) Scr_WaitTill(level_handle(), 0x92b8c65f);
}

void XB_CDECL BroScr_RemoveFollowerOnDeath(uint32_t leader, uint32_t follower) {
    slot<EntHash>(0x00922188)(leader, 0xcc07a20a);  // Scr_EndOn
    Scr_WaitTill(follower, kDeath);
    PtrArray_RemoveValue(followers(leader), follower);
}

void XB_CDECL BroScr_PathPosDebugStub(uint32_t ent) {
    slot<EntHash>(0x00922188)(ent, 0x71017853);             // Scr_EndOn
    slot<EntHash>(0x00922188)(level_handle(), 0x71017853);  // Scr_EndOn
    for (;;) {
        for (int32_t i = 0; i < static_cast<int32_t>(pointer<DynArray>(path_positions(ent))->size); ++i) {
            path_positions(ent);
            path_positions(ent);
        }
        Scr_Wait(float_bits(0x3d4ccccd));  // 0.05
    }
}

void XB_CDECL BroScr_CurrentPathPosDebugStub(uint32_t ent) {
    slot<EntHash>(0x00922188)(ent, 0x71017853);             // Scr_EndOn
    slot<EntHash>(0x00922188)(level_handle(), 0x71017853);  // Scr_EndOn
    for (;;) {
        if (static_cast<int32_t>(*int_slot(ent, 0x9aec9c1a)) >= 0) {  // the current index
            path_positions(ent);
            int_slot(ent, 0x9aec9c1a);
        }
        Scr_Wait(float_bits(0x3d4ccccd));  // 0.05
    }
}

void XB_CDECL BroScr_WaitForFollowersCall(uint32_t ent) {
    BroScr_WaitForFollowers(ent, static_cast<int32_t>(followers(ent)->size));
}


namespace {

using Resize8 = void XB_THISCALL(uint32_t*, uint32_t, uint32_t);

// The vehicle's rider slots (array key 0x2cd69a2a, 8-byte entries {rider, bool}) found inline (an
// empty array set when missing), slot i grown in.
inline uint32_t* rider_slot_inline(uint32_t vehicle, uint32_t i) {
    ScriptKeyTable* const table = ent_script_object(vehicle);
    uint32_t* value = ScriptKeyTable_Find(table, 0x2cd69a2a);
    if (!value) {
        const uint32_t empty[3] = {0, 0, 0};
        value = ScriptKeyTable_SetNewArray(table, 0x2cd69a2a, empty);
    }
    uint32_t* const array = pointer<uint32_t>(*value);
    if (i >= array[2]) own_function<Resize8>(&DynArray_Resize8)(array, i + 1, i + 4);  // DynArray_Resize8
    return pointer<uint32_t>(array[0]) + i * 2;
}

// The same slot through ScriptKeyTable_GetOrAddArray.
inline uint32_t* rider_slot(uint32_t vehicle, uint32_t i) {
    uint32_t* const array = pointer<uint32_t>(ScriptKeyTable_GetOrAddArray(ent_script_object(vehicle), 0x2cd69a2a));
    if (i >= array[2]) own_function<Resize8>(&DynArray_Resize8)(array, i + 1, i + 4);  // DynArray_Resize8
    return pointer<uint32_t>(array[0]) + i * 2;
}

// The flakvierling loaders' anim (passenger1: `left`, passenger2: `right`, anim globals) played at
// their tag on the vehicle, waited for; nothing for "tag_gunner" or other tags.
inline void rider_tag_anim(uint32_t vehicle, uint32_t rider, ScriptString* tag, uint32_t left, uint32_t right) {
    using Anim = void XB_CDECL(uint32_t, uint32_t, const uint32_t*, const uint32_t*, uint32_t);
    using TagVec = void XB_CDECL(uint32_t, uint32_t*, ScriptString*);
    if (ScriptString_Equals(tag, pointer<const char>(0x007fa47c))) return;  // "tag_gunner"
    if (ScriptString_Equals(tag, pointer<const char>(0x007fc0bc))) {  // "tag_passenger1"
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922748)(rider, title_global<uint32_t>(left));  // ScrActor_SetInt_1370
        const uint32_t lane = undefined_lane();
        uint32_t a[3] = {lane, lane, lane};
        slot<TagVec>(0x00922620)(vehicle, a, tag);  // GScr_GetTagAngles
        const uint32_t angles[3] = {a[0], a[1], a[2]};
        const uint32_t lane_2 = undefined_lane();
        uint32_t o[3] = {lane_2, lane_2, lane_2};
        slot<TagVec>(0x0092261c)(vehicle, o, tag);  // GScr_GetTagOrigin
        const uint32_t origin[3] = {o[0], o[1], o[2]};
        slot<Anim>(0x009224c0)(rider, 0x54e3b2b6, origin, angles, title_global<uint32_t>(left));  // GScr_AnimScriptedNoNotify
    } else if (ScriptString_Equals(tag, pointer<const char>(0x007f5c08))) {  // "tag_passenger2"
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922748)(rider, title_global<uint32_t>(right));  // ScrActor_SetInt_1370
        const uint32_t anim = title_global<uint32_t>(right);
        const uint32_t tag_word = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tag));
        uint32_t a[3], o[3];
        const uint32_t* const angles = ScrCall_GScr_GetTagAngles(a, &vehicle, tag_word);
        const uint32_t* const origin = ScrCall_GScr_GetTagOrigin(o, &vehicle, tag_word);
        slot<Anim>(0x009224c0)(rider, 0x54e3b2b6, origin, angles, anim);  // GScr_AnimScriptedNoNotify
    } else {
        return;
    }
    Scr_WaitTill(rider, 0x54e3b2b6);
}

}  // namespace

void XB_CDECL BroScr_VehicleRiderAttach(uint32_t vehicle, uint32_t rider, ScriptString tag) {
    slot<EntHash>(0x00922188)(rider, kDeath);  // Scr_EndOn
    Scr_WaitTill(rider, 0xa0e65a8e);
    ++*int_slot(vehicle, 0xb330b301);  // riders getting off
    title_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*)>(0x004b09e0)(
        0, 0, 0, 0, own_address(&BroScr_VehicleRiderDeathWatch_2), vehicle, &rider);  // Scr_CallThread_ent_ent_ORAN(BroScr_VehicleRiderDeathWatch_2)
    // The flakvierling loaders' dismount anims at their tag ("tag_gunner" none).
    rider_tag_anim(vehicle, rider, &tag, 0x008cd200, 0x008cd20c);  // LoaderDismountL / R
    // The rider leaves its slot: {0, the undefined bool} (the title stores a word whose upper bytes
    // are stale stack: 0 here).
    for (uint32_t i = 0; i < 3; ++i) {
        if (rider_slot_inline(vehicle, i)[0] != rider) continue;
        const uint32_t flag = title_global<uint8_t>(0x00922ed0);
        uint32_t* const entry = rider_slot(vehicle, i);
        entry[0] = 0;
        entry[1] = flag;
    }
    slot<Ent>(0x0092250c)(rider);  // GScr_Unlink
    --*int_slot(vehicle, 0xb330b301);
    slot<EntHash>(0x009221a4)(vehicle, 0x996a3647);  // Scr_QueueNotify
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t)>(0x009223a0)(rider, 0, 0, 0);  // GScr_SetAIFlags
    BroScr_PutGunInHand(rider, 0x07151174);
    ScriptString_Release(&tag);
}


void XB_CDECL BroScr_VehicleRiderMount(uint32_t vehicle, uint32_t rider, ScriptString tag) {
    slot<EntHash>(0x00922188)(rider, kDeath);  // Scr_EndOn
    Scr_WaitTill(rider, 0xa0e65a8e);
    ++*int_slot(vehicle, 0xb330b301);
    title_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*)>(0x004b09e0)(
        0, 0, 0, 0, own_address(&BroScr_VehicleRiderDeathWatch_2), vehicle, &rider);  // Scr_CallThread_ent_ent_ORAN(BroScr_VehicleRiderDeathWatch_2)
    rider_tag_anim(vehicle, rider, &tag, 0x008cd1dc, 0x008cd1e8);  // GunMountL / R
    // The rider leaves its slot ({0, the undefined bool word: stale upper bytes, 0 here}, then the
    // bool cleared) and becomes the gunner in slot 0.
    for (uint32_t i = 0; i < 3; ++i) {
        if (rider_slot_inline(vehicle, i)[0] != rider) continue;
        const uint32_t flag = title_global<uint8_t>(0x00922ed0);
        uint32_t* const entry = rider_slot(vehicle, i);
        entry[0] = 0;
        entry[1] = flag;
        *reinterpret_cast<uint8_t*>(rider_slot(vehicle, i) + 1) = 0;
    }
    slot<Ent>(0x0092250c)(rider);  // GScr_Unlink
    --*int_slot(vehicle, 0xb330b301);
    slot<EntHash>(0x009221a4)(vehicle, 0x996a3647);  // Scr_QueueNotify
    uint32_t pair[2];
    const uint32_t* const gunner = ScrCall_ScrEnt_SetTargetname_2(pair, rider);  // {rider, false}, targetname "gunner"
    uint32_t* const first = rider_slot_inline(vehicle, 0);
    first[0] = gunner[0];
    first[1] = gunner[1];
    *reinterpret_cast<uint8_t*>(rider_slot_inline(vehicle, 0) + 1) = 1;
    ScriptString gun_tag;
    ScriptString_FromCString(&gun_tag, pointer<const char>(0x007fa47c));  // "tag_gunner"
    Scr_WaitFrames(0);
    {
        const uint32_t angles_offset[3] = {bits(0.0f), bits(0.0f), bits(0.0f)};
        const uint32_t origin_offset[3] = {bits(0.0f), bits(0.0f), bits(0.0f)};
        using Link = void XB_CDECL(uint32_t, uint32_t, const ScriptString*, const uint32_t*, const uint32_t*, uint32_t);
        slot<Link>(0x009224f4)(rider, vehicle, &gun_tag, origin_offset, angles_offset, 0);  // GScr_LinkToTagWithOffset
    }
    {
        ScriptString copy;
        ScriptString_Copy(&copy, &gun_tag);
        BroScr_SetRiderAnimForTag(rider, copy);  // releases its copy
    }
    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f4a28;
        functor[1] = own_address(&BroScr_VehicleRiderUnlinkOnDeath);  // BroScr_VehicleRiderUnlinkOnDeath
        functor[2] = vehicle;
        functor[3] = rider;
    }
    slot<CreateThread>(0x0092216c)(0, 0, 0, vehicle, functor, 0);  // Scr_CreateThread
    using Start = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                    const ScriptString*);
    own_function<Start>(&Scr_StartThread_ent_ent_str_PROLOGUE_entry)(0, 0, 0, 0, own_address(&BroScr_VehicleRiderWait), vehicle, &rider, &gun_tag);  // Scr_StartThread_ent_ent_str(BroScr_VehicleRiderWait)
    ScriptString_Release(&gun_tag);
    ScriptString_Release(&tag);
}


namespace {

// The flakvierling's shot: GScr_Vehicle_FireWeapon, an earthquake (0.2, 0.1 s, 2250) at it.
inline void flakvierling_fire(uint32_t ent) {
    slot<Ent>(0x009228d4)(ent);  // GScr_Vehicle_FireWeapon
    ScrVec3 origin;
    origin_of(&origin, ent);
    slot<void XB_CDECL(uint32_t, uint32_t, const ScrVec3*, uint32_t)>(0x00922444)(0x3e4ccccd, 0x3dcccccd, &origin,
                                                                                  0x450ca000);  // GScr_Earthquake(0.2, 0.1, 2250)
}

// The fire anim (key `anim`) on the anim root (key 0x0038b194) as the flagged knob 0x54e3b2b6 of all,
// waited for.
inline void flakvierling_fire_anim(uint32_t ent, uint32_t anim) {
    using KnobAll = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    const uint32_t root = *key_slot(ent, 0x0038b194, 0);
    slot<KnobAll>(0x009225e4)(ent, 0x54e3b2b6, *key_slot(ent, anim, 0), root, 0x3f800000, 0x3e4ccccd, 0x3f800000,
                              1);  // GScr_SetFlaggedAnimKnobAll(1, 0.2, 1)
    Scr_WaitTill(ent, 0x54e3b2b6);
}

}  // namespace

void XB_CDECL BroScr_FlakvierlingThink(uint32_t ent) {
    slot<EntHash>(0x00922188)(ent, kDeath);  // Scr_EndOn
    uint32_t right = 0;  // which barrel pair's anim plays next
    int32_t shots = 0;
    bool full;
    {
        ScriptString model;
        full = ScriptString_Equals(ScrCall_ScrEnt_GetModel(&ent, &model), pointer<const char>(0x00824c44));  // "v_flakvierling"
        ScriptString_Release(&model);
    }
    {
        ScriptString tree;
        if (full)
            ScriptString_FromCString(&tree, pointer<const char>(0x007f49c4));  // "flakvierling"
        else
            ScriptString_FromCString(&tree, pointer<const char>(0x007f49b0));  // "flakvierling_base"
        slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009225f0)(ent, &tree);  // GScr_UseAnimTree
        ScriptString_Release(&tree);
    }
    for (;;) {
        Scr_WaitTill(ent, 0xac1c7bac);  // a shot
        if (!*reinterpret_cast<const uint8_t*>(rider_slot_inline(ent, 0) + 1) && !*bool_slot(ent, 0xd8e13e71)) {
            // No gunner: impacts only.
            flakvierling_fire(ent);
            thread_1(ent, 0x007f5ba0, own_address(&BroScr_StrafeImpacts), ent);  // BroScr_StrafeImpacts
        } else {
            // Manned: past the shot count at 0x008cd168 the target (entity key 0x0d60fb3d, when its bool
            // key 0xe29137e5 is set) takes the damage at 0x008cd16c; the time of the shot in int key
            // 0x05903940.
            flakvierling_fire(ent);
            if (shots > title_global<int32_t>(0x008cd168) && *bool_slot(*key_slot(ent, 0x0d60fb3d, 0), 0xe29137e5)) {
                const uint32_t zero[3] = {bits(0.0f), bits(0.0f), bits(0.0f)};
                const uint32_t target = *key_slot(ent, 0x0d60fb3d, 0);
                const float damage = static_cast<float>(title_global<int32_t>(0x008cd16c));
                slot<void XB_CDECL(uint32_t, float, const uint32_t*)>(0x00922558)(target, damage, zero);  // GScr_DoDamage
                shots = 0;
            }
            thread_1(ent, 0x007f5ba0, own_address(&BroScr_StrafeImpacts), ent);  // BroScr_StrafeImpacts
            ++shots;
            const int32_t now = slot<int32_t XB_CDECL()>(0x009223c4)();  // GScr_GetTime
            *int_slot(ent, 0x05903940) = static_cast<uint32_t>(now);
        }
        if (right == 1) {
            flakvierling_fire_anim(ent, 0xe0b632e1);
            right = 0;
        } else {
            flakvierling_fire_anim(ent, 0xe0b632e2);
            right = 1;
        }
    }
}


namespace {

// A thread on the level entity (functor {0x007f5ba0, body, level}) through `starter`
// (Scr_CreateThread / Scr_BeginThread).
inline void level_thread(uint32_t starter, uint32_t body) {
    const uint32_t level = level_handle();
    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0xc);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f5ba0;
        functor[1] = body;
        functor[2] = level;
    }
    slot<CreateThread>(starter)(0, 0, 0, level, functor, 0);
}

// Whether cvar `name` equals `text`.
inline bool cvar_is(uint32_t name, uint32_t text) {
    ScriptString value;
    const bool equal = ScriptString_Equals(ScrCall_GScr_GetCvar(&value, name), pointer<const char>(text));
    ScriptString_Release(&value);
    return equal;
}

}  // namespace

void XB_CDECL Bro__load_gmi__main() {
    BroScr_InitLevelCvars();
    level_field(0x7f8) = player();
    *bool_slot(level_field(0x7f8), 0x2eecefb8) = 0;
    thread_1(player(), 0x007f5ba0, own_address(&BroScr_PlayerWeaponSounds), player());  // BroScr_PlayerWeaponSounds
    *reinterpret_cast<uint8_t*>(&level_field(0x354)) = 1;
    ScriptString difficulty;
    ScriptString_FromRep(&difficulty, nullptr);
    slot<void XB_CDECL(ScriptString*)>(0x009223cc)(&difficulty);  // GScr_GetDifficulty
    uint32_t hash;
    level_field(0x84) = *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&difficulty));
    if (level_field(0x1c4) == 0) {  // the level's map name string
        ScriptString mapname;
        const ScriptString* const value = ScrCall_GScr_GetCvar(&mapname, 0x00835230);  // "mapname"
        ScriptString_Assign(reinterpret_cast<ScriptString*>(&level_field(0x1c4)), value);
        ScriptString_Release(&mapname);
    }
    BroScr_SetupLevelInfo(level_handle());
    level_thread(0x0092216c, own_address(&BroScr_SetDeadQuote));  // Scr_CreateThread(BroScr_SetDeadQuote)
    BroScr_SetRandomVictoryQuote();  // the title passes the level entity, not read
    level_thread(0x00922168, own_address(&BroScr_SetIntroScreenCvars));  // Scr_BeginThread(BroScr_SetIntroScreenCvars)
    if (slot<uint8_t XB_CDECL()>(0x0092218c)()) Scr_WaitFrames(0);  // Scr_InThread
    if (cvar_is(0x00832fec, 0x008398e0)) slot<void XB_CDECL(uint32_t, float)>(0x00922554)(player(), 1.0f);  // g_gameskill "3": GScr_SetNormalHealth
    level_thread(0x0092216c, own_address(&BroScr_InfinitePanzerfaust));  // BroScr_InfinitePanzerfaust
    // Every AI's damage var 0xce270885 = "_spawner_gmi::tanksquish".
    DynArray ai = {};
    slot<void XB_CDECL(DynArray*)>(0x009223d4)(&ai);  // GScr_GetAIAll
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ai.size); ++i) {
        const uint32_t* const guy = &element(&ai, i);
        slot<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x009221bc)(
            *guy, 0xce270885, *Scr_HashName(&hash, pointer<const char>(0x007ff4e0)));  // GScr_SetEntVar
    }
    DynArray chains = {};
    {
        ScriptString classname;
        ScriptString_FromCString(&classname, pointer<const char>(0x00839e3c));  // "trigger_friendlychain"
        Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&classname)), 0xa106e9bd,
                        reinterpret_cast<uint8_t*>(&chains), 0);  // by classname
        ScriptString_Release(&classname);
    }
    level_field(0x194) = 0;
    BroScr_LoadGmiInitLevelVars();  // the title passes the level entity, not read
    if (cvar_is(0x0082355c, 0x0082e058)) level_thread(0x0092216c, own_address(&BroScr_InitFriendlyChainNodes));  // "chain" "1": BroScr_InitFriendlyChainNodes
    level_thread(0x0092216c, own_address(&BroScr_ReattachAlliesGuns));  // BroScr_ReattachAlliesGuns
    if (chains.data) Scr_Free(chains.data);
    if (ai.data) Scr_Free(ai.data);
    ScriptString_Release(&difficulty);
}


namespace {

// Whether `ent` has an animname equal to the trigger's script_animname (the empty animname: false).
inline bool animname_matches(uint32_t* ent, uint32_t trigger) {
    bool empty;
    {
        ScriptString animname;
        empty = ScrCall_ScrEnt_GetAnimname(ent, &animname)->rep == nullptr;
        ScriptString_Release(&animname);
    }
    if (empty) return false;
    ScriptString animname;
    const ScriptString* const name = ScrCall_ScrEnt_GetAnimname(ent, &animname);
    const bool equal = ScriptString_EqualsString(name, ScrField_script_animname(trigger));
    ScriptString_Release(&animname);
    return equal;
}

// The same for the AI array's element i (the element looked up again for each use).
inline bool ai_animname_matches(DynArray* ai, uint32_t i, uint32_t trigger) {
    bool empty;
    {
        ScriptString animname;
        empty = ScrCall_ScrEnt_GetAnimname(&element(ai, i), &animname)->rep == nullptr;
        ScriptString_Release(&animname);
    }
    if (empty) return false;
    ScriptString animname;
    const ScriptString* const name = ScrCall_ScrEnt_GetAnimname(&element(ai, i), &animname);
    const bool equal = ScriptString_EqualsString(name, ScrField_script_animname(trigger));
    ScriptString_Release(&animname);
    return equal;
}

}  // namespace

void XB_CDECL BroScr_FriendlyChatTriggerThink(uint32_t trigger) {
    uint32_t halftrack = 0;
    bool targeted;
    {
        ScriptString target;
        targeted = ScrCall_ScrEnt_GetTarget(&trigger, &target)->rep != nullptr;
        ScriptString_Release(&target);
    }
    bool defined;
    if (targeted) {
        // The halftrack named by the target: a BroScr_FriendlyChatTriggers thread (once: bool field
        // 0x0718b2c3), this thread ends on its death. The validity and "allies" checks' results are
        // not used.
        {
            ScriptString target;
            const ScriptString* const name = ScrCall_ScrEnt_GetTarget(&trigger, &target);
            halftrack = slot<uint32_t XB_CDECL(const ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009221c4)(
                name, 0x0d786b4c, 0, 0, 0);  // GScr_GetEntArray (by targetname)
            ScriptString_Release(&target);
        }
        entity_handle_is_valid(halftrack);
        {
            ScriptString team;
            ScriptString_Equals(ScrCall_ScrSentient_GetTeam(&halftrack, &team), pointer<const char>(0x00832084));  // "allies"
            ScriptString_Release(&team);
        }
        if (!*ScrField_h0718b2c3_IsDefined(&defined, halftrack)) {
            const uint32_t owner = level_handle();
            uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
            if (functor) {
                functor[0] = 0x007f4a28;
                functor[1] = own_address(&BroScr_FriendlyChatTriggers);  // BroScr_FriendlyChatTriggers
                functor[2] = owner;
                functor[3] = halftrack;
            }
            slot<CreateThread>(0x0092216c)(0, 0, 0, owner, functor, 0);  // Scr_CreateThread
            *reinterpret_cast<uint8_t*>(ScrField_h0718b2c3(halftrack, 0)) = 1;
        }
        slot<EntHash>(0x00922188)(halftrack, kDeath);  // Scr_EndOn
    } else {
        ScrField_script_animname_IsDefined(&defined, trigger);  // result not used
    }
    if (*ScrField_script_waittill_IsDefined(&defined, trigger)) {
        uint32_t* const level = title_global<uint32_t>(0x00922ee0) ? pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) + 4) : nullptr;
        uint32_t hash;
        const uint32_t notify = *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(ScrField_script_waittill(trigger)));
        Scr_WaitTill(*level, notify);
    }
    // Waits for the halftrack (or, without one, an AI with the trigger's script_animname, which
    // becomes it) with spawnflags 8, else for the player (then, without a halftrack, the allied AI
    // with that animname nearest the player).
    uint32_t who = 0;
    for (;;) {
        title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t*)>(0x004b1590)(trigger, 0xe2a210cf, &who);  // Scr_WaitTillOutputEnt_ORAN
        if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922b30)(trigger) & 8) {  // ScrEnt_GetSpawnflags
            if (entity_handle_is_valid(halftrack)) {
                if (who == halftrack) break;
                continue;
            }
            if (!animname_matches(&who, trigger)) continue;
            halftrack = who;
            break;
        }
        if (who != player()) continue;
        if (entity_handle_is_valid(halftrack)) break;
        DynArray matches = {};
        DynArray ai = {};
        {
            ScriptString team;
            ScriptString_FromCString(&team, pointer<const char>(0x00832084));  // "allies"
            slot<void XB_CDECL(DynArray*, ScriptString*)>(0x009223d0)(&ai, &team);  // GScr_GetAI
            ScriptString_Release(&team);
        }
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ai.size); ++i) {
            if (!ai_animname_matches(&ai, i, trigger)) continue;
            const uint32_t* const guy = &element(&ai, i);
            const uint32_t n = matches.size;
            DynArray_Resize(reinterpret_cast<uint8_t*>(&matches), n + 1, n + 4);
            matches.data[n] = *guy;
        }
        if (matches.size == 0) {
            if (ai.data) Scr_Free(ai.data);
            if (matches.data) Scr_Free(matches.data);
            continue;
        }
        DynArray_Resize(reinterpret_cast<uint8_t*>(&ai), 0, 0);
        DynArray exclude = {};
        const uint32_t lane = undefined_lane();
        ScrVec3 origin;
        origin.v[0] = lane;
        origin.v[1] = lane;
        origin.v[2] = lane;
        slot<void XB_CDECL(uint32_t, ScrVec3*)>(0x00922524)(player(), &origin);  // GScr_GetOrigin
        uint32_t closest;
        halftrack = *BroScr_GetClosestFromArray(&closest, origin, &matches, &exclude);
        DynArray_Resize(reinterpret_cast<uint8_t*>(&matches), 0, 0);
        const bool found = entity_handle_is_valid(halftrack) != 0;
        if (exclude.data) Scr_Free(exclude.data);
        if (ai.data) Scr_Free(ai.data);
        if (matches.data) Scr_Free(matches.data);
        if (found) break;
    }
    // With script_min_friendlies / field 0xa7ad4a18: until the time limit (field 0x8177124f, ms; none
    // without), wait for the friendlies touching the trigger; the halftrack must touch it too (0xa7ad4a18
    // or spawnflags 8; else 1 s and again).
    ScriptString reason;
    ScriptString_FromCString(&reason, pointer<const char>(0x00824c6c));  // "reason 0"
    uint8_t ok = title_global<uint8_t>(0x00922ed0);  // g_scrUndefinedBool
    (void)ok;
    if (*ScrField_script_min_friendlies_IsDefined(&defined, trigger) || *ScrField_ha7ad4a18_IsDefined(&defined, trigger)) {
        int32_t until = 0;
        if (*ScrField_h8177124f_IsDefined(&defined, trigger)) {
            const uint32_t* const limit = ScrField_h8177124f(trigger);
            const int32_t now = slot<int32_t XB_CDECL()>(0x009223c4)();  // GScr_GetTime
            until = static_cast<int32_t>(*limit) + now;
        }
        for (;;) {
            if (!(slot<int32_t XB_CDECL()>(0x009223c4)() < until) && until != 0) break;  // GScr_GetTime
            ok = 1;
            if (*ScrField_script_min_friendlies_IsDefined(&defined, trigger)) {
                const uint32_t undefined = undefined_float();
                const uint32_t count = *ScrField_script_min_friendlies(trigger);
                ScriptString team;
                ScriptString_FromCString(&team, pointer<const char>(0x00832084));  // "allies"
                own_function<void XB_CDECL(uint32_t, ScriptString, uint32_t, uint32_t)>(&BroScr_WaitForAliveTouchingCount)(
                    trigger, team, count, undefined);  // BroScr_WaitForAliveTouchingCount
            }
            using Touching = uint8_t XB_CDECL(uint32_t, uint32_t);
            if (*ScrField_ha7ad4a18_IsDefined(&defined, trigger) && !slot<Touching>(0x00922530)(halftrack, trigger)) {  // GScr_IsTouching
                ScriptString_AssignCString(&reason, pointer<const char>(0x00824c60));  // "reason 1"
                ok = 0;
            }
            if ((slot<uint32_t XB_CDECL(uint32_t)>(0x00922b30)(trigger) & 8) && !slot<Touching>(0x00922530)(halftrack, trigger)) {
                ScriptString_AssignCString(&reason, pointer<const char>(0x00824c54));  // "reason 2"
                ok = 0;
                Scr_Wait(1.0f);
                continue;
            }
            if (ok) break;
            Scr_Wait(1.0f);
        }
    }
    slot<Ent>(0x00922540)(trigger);  // GScr_Delete
    ScriptString_Release(&reason);
}

namespace {

inline uint32_t* new_functor(uint32_t size) {
    return slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(size);  // ScrIface_Alloc
}

inline void create_thread(uint32_t owner, uint32_t* functor) {
    slot<CreateThread>(0x0092216c)(0, 0, 0, owner, functor, 0);  // Scr_CreateThread
}

// Scr_CreateThread(owner, {0x007f4a1c, body, a, b}).
inline void thread_2(uint32_t owner, uint32_t body, uint32_t a, uint32_t b) {
    uint32_t* const functor = new_functor(0x10);
    if (functor) {
        functor[0] = 0x007f4a1c;
        functor[1] = body;
        functor[2] = a;
        functor[3] = b;
    }
    create_thread(owner, functor);
}

// Scr_CreateThread(owner, {0x007f4af4, body, a, b, c}).
inline void thread_3(uint32_t owner, uint32_t body, uint32_t a, uint32_t b, uint32_t c) {
    uint32_t* const functor = new_functor(0x14);
    if (functor) {
        functor[0] = 0x007f4af4;
        functor[1] = body;
        functor[2] = a;
        functor[3] = b;
        functor[4] = c;
    }
    create_thread(owner, functor);
}

// Scr_CreateThread(owner, {0x007f9a78, body, a, a copy of *array (DynArray_Assign4)}).
inline void thread_array(uint32_t owner, uint32_t body, uint32_t a, const DynArray* array) {
    uint32_t* const functor = new_functor(0x18);
    if (functor) {
        functor[0] = 0x007f9a78;
        functor[1] = body;
        functor[2] = a;
        functor[3] = 0;
        functor[4] = 0;
        functor[5] = 0;
        own_function<void XB_THISCALL(uint32_t*, const DynArray*)>(&DynArray_Assign4_PROLOGUE)(&functor[3], array);  // DynArray_Assign4_PROLOGUE
    }
    create_thread(owner, functor);
}

// The level's byte / int array at `offset` [i], grown first (no null check on the level object).
inline uint8_t& level_byte(uint32_t offset, uint32_t i) {
    DynArrayBytes* const array = pointer<DynArrayBytes>(title_global<uint32_t>(0x00922ee0) + offset);
    if (i >= array->size) DynArray_ResizeBytes(array, i + 1, i + 4);
    return array->data[i];
}

inline uint32_t& level_int(uint32_t offset, uint32_t i) {
    DynArray* const array = pointer<DynArray>(title_global<uint32_t>(0x00922ee0) + offset);
    if (i >= array->size) DynArray_ResizeInt(array, i + 1, i + 4);
    return array->data[i];
}

// The level entity's script key table (the level object read without a null check).
inline ScriptKeyTable* level_table() {
    return ent_script_object(*pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) + 4));
}

// Whether the level's byte array key 0xe337f297 [flaknum] is 0 (read from a copy of the array).
inline bool flak_flag_clear(uint32_t flaknum) {
    DynArrayBytes copy;
    DynArrayBytes* const flags = static_cast<DynArrayBytes*>(
        ScrField_he337f297_GetArrayCopy(reinterpret_cast<uint8_t*>(level_table()) + 0x10, &copy));
    if (flaknum >= flags->size) DynArray_ResizeBytesNoFill(flags, flaknum + 1, flaknum + 4);
    const bool clear = flags->data[flaknum] == 0;
    if (copy.data) Scr_Free(copy.data);
    return clear;
}

// BroScr_PutGunInHand(guy, hand) for each guy.
inline void guns_in_hand(DynArray* guys, uint32_t hand) {
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(guys->size); ++i)
        BroScr_PutGunInHand(element(guys, i), hand);
}

// A BroScr_FlakCrewAnims thread {0x008244d0: flak, anim, flaknum, type} on the flak.
inline void crew_anims_thread(uint32_t flak, const ScriptString* anim, uint32_t flaknum, const ScriptString* type) {
    uint32_t* const functor = new_functor(0x18);
    if (functor) {
        functor[0] = 0x008244d0;
        functor[1] = own_address(&BroScr_FlakCrewAnims);  // BroScr_FlakCrewAnims
        functor[2] = flak;
        ScriptString_Copy(reinterpret_cast<ScriptString*>(&functor[3]), anim);
        functor[4] = flaknum;
        ScriptString_Copy(reinterpret_cast<ScriptString*>(&functor[5]), type);
    }
    create_thread(flak, functor);
}

// A BroScr_FlakCrewAnim thread {0x007ffb0c: guy, flak, flaknum, anim} on each guy.
inline void crew_anim_threads(DynArray* guys, uint32_t flak, uint32_t flaknum, const ScriptString* anim) {
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(guys->size); ++i) {
        const uint32_t guy = element(guys, i);
        uint32_t* const functor = new_functor(0x18);
        if (functor) {
            functor[0] = 0x007ffb0c;
            functor[1] = own_address(&BroScr_FlakCrewAnim);  // BroScr_FlakCrewAnim
            functor[2] = guy;
            functor[3] = flak;
            functor[4] = flaknum;
            ScriptString_Copy(reinterpret_cast<ScriptString*>(&functor[5]), anim);
        }
        create_thread(guy, functor);
    }
}

// Scr_WaitTill(level, "flak_anim_done" + flaknum) (the text left in *done).
inline void wait_flak_anim_done(ScriptString* done, uint32_t flaknum) {
    ScriptString_AssignCString(done, pointer<const char>(0x00824a44));  // "flak_anim_done"
    ScriptString_AppendInt(done, static_cast<int32_t>(flaknum));
    const uint32_t object = title_global<uint32_t>(0x00922ee0);
    const uint32_t* const level = pointer<uint32_t>(object ? object + 4 : 0);
    uint32_t hash;
    const uint32_t h = *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(done));
    Scr_WaitTill(*level, h);
}

// A BroScr_FlakSoundBlend thread {0x007f8f7c: flak, sound, flaknum} on the flak.
inline void flak_sound_blend(uint32_t flak, uint32_t text, uint32_t flaknum) {
    ScriptString sound;
    ScriptString_FromCString(&sound, pointer<const char>(text));
    uint32_t* const functor = new_functor(0x14);
    if (functor) {
        functor[0] = 0x007f8f7c;
        functor[1] = own_address(&BroScr_FlakSoundBlend);  // BroScr_FlakSoundBlend
        functor[2] = flak;
        ScriptString_Copy(reinterpret_cast<ScriptString*>(&functor[3]), &sound);
        functor[4] = flaknum;
    }
    create_thread(flak, functor);
    ScriptString_Release(&sound);
}

inline bool string_is(const ScriptString* s, uint32_t text) { return ScriptString_Equals(s, pointer<const char>(text)); }

}  // namespace

void XB_CDECL BroScr_FlakPanzerThink(uint32_t ent, uint32_t flaknum) {
    using IsAlive = uint32_t XB_CDECL(uint32_t);
    using SetModel = void XB_CDECL(uint32_t, ScriptString*, uint32_t);
    // The flak type: "flakpanzer" for the flakpanzer model, else string field 0xd1c52a4f.
    ScriptString type;
    ScriptString_FromRep(&type, nullptr);
    bool panzer_model;
    {
        ScriptString model;
        panzer_model = string_is(ScrCall_ScrEnt_GetModel(&ent, &model), 0x0082534c);  // "xmodel/vehicle_tank_flakpanzer"
        ScriptString_Release(&model);
    }
    if (panzer_model) ScriptString_AssignCString(&type, pointer<const char>(0x00825340));  // "flakpanzer"
    bool defined;
    if (*ScrField_hd1c52a4f_IsDefined(&defined, ent)) ScriptString_Assign(&type, ScrField_hd1c52a4f(ent));
    if (string_is(&type, 0x008251f8) || string_is(&type, 0x00823538) || string_is(&type, 0x00825340) ||
        string_is(&type, 0x00823544)) {  // "flakair", "flakairlow", "flakpanzer", "flaktank"
        {
            const uint8_t undefined = title_global<uint8_t>(0x00922ed0);
            uint32_t* const functor = new_functor(0x14);
            if (functor) {
                functor[0] = 0x008023c8;
                functor[1] = own_address(&BroScr_FlakDeathThink);  // BroScr_FlakDeathThink
                functor[2] = ent;
                functor[3] = flaknum;
                *reinterpret_cast<uint8_t*>(&functor[4]) = undefined;
            }
            create_thread(ent, functor);
        }
        // The flak's targets: start triggers (BroScr_FlakWaitStartTrigger threads, then wait for
        // "start_turret"), script models (BroScr_PlantBombObjective threads), the rest the crew spawners.
        DynArray targets = {nullptr, 0, 0};
        {
            ScriptString target;
            Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ScrCall_ScrEnt_GetTarget(&ent, &target))),
                            0x0d786b4c, reinterpret_cast<uint8_t*>(&targets), 0);  // by targetname
            ScriptString_Release(&target);
        }
        bool start_trigger = false;
        DynArray spawners = {nullptr, 0, 0};
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(targets.size); ++i) {
            bool is;
            {
                ScriptString classname;
                is = string_is(ScrCall_ScrEnt_GetClassname(&element(&targets, i), &classname), 0x00832270);  // "trigger_multiple"
                ScriptString_Release(&classname);
            }
            if (is) {
                thread_3(ent, own_address(&BroScr_FlakWaitStartTrigger), ent, element(&targets, i), flaknum);  // BroScr_FlakWaitStartTrigger
                start_trigger = true;
                continue;
            }
            {
                ScriptString classname;
                is = string_is(ScrCall_ScrEnt_GetClassname(&element(&targets, i), &classname), 0x0083ab98);  // "script_model"
                ScriptString_Release(&classname);
            }
            if (is)
                thread_3(ent, own_address(&BroScr_PlantBombObjective), ent, element(&targets, i), flaknum);  // BroScr_PlantBombObjective
            else
                DynArray_PushBack(reinterpret_cast<uint8_t*>(&spawners), &element(&targets, i));
        }
        if (start_trigger) Scr_WaitTillString(ent, pointer<const char>(0x00825330));  // "start_turret"
        DynArray guys = {nullptr, 0, 0};
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(spawners.size); ++i) {
            const uint32_t guy = slot<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00922518)(element(&spawners, i), 0);  // GScr_DoSpawn
            element(&guys, i) = guy;
        }
        bool alive = true;
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(guys.size); ++i) {
            if (!slot<IsAlive>(0x00922210)(element(&guys, i))) {  // GScr_IsAlive
                alive = false;
                break;
            }
        }
        if (alive && static_cast<int32_t>(guys.size) >= static_cast<int32_t>(spawners.size)) {
            thread_array(ent, own_address(&BroScr_FlakPanzerKeepUnusable), ent, &guys);  // BroScr_FlakPanzerKeepUnusable
            // The crew size: 3 or more (flakair, flakairlow, flaktank), exactly 3 for the flakpanzer
            // (its four clips taken off first: bool keys 0xc0f54bd1.. set).
            bool crewed = true;
            if (string_is(&type, 0x008251f8) || string_is(&type, 0x00823538) || string_is(&type, 0x00823544)) {
                crewed = guys.capacity && static_cast<int32_t>(guys.size) >= 3;
            } else if (string_is(&type, 0x00825340)) {
                *bool_slot(ent, 0xc0f54bd1) = 1;
                flak_clip_tag(ent, 0x00824b2c, false);  // "tag_clip_1"
                *bool_slot(ent, 0xc0f54bd2) = 1;
                flak_clip_tag(ent, 0x00824b14, false);  // "tag_clip_2"
                *bool_slot(ent, 0xc0f54bd3) = 1;
                flak_clip_tag(ent, 0x00824afc, false);  // "tag_clip_3"
                *bool_slot(ent, 0xc0f54bd4) = 1;
                flak_clip_tag(ent, 0x00824ae4, false);  // "tag_clip_4"
                crewed = guys.capacity && guys.size == 3;
            }
            if (crewed) {
                if (string_is(&type, 0x008251f8) || string_is(&type, 0x00823538) || string_is(&type, 0x00823544)) {
                    ScriptString model;
                    ScriptString_FromCString(&model, pointer<const char>(0x0082521c));  // "xmodel/turret_flak88"
                    slot<SetModel>(0x00922548)(ent, &model, 0);  // GScr_SetModel
                    ScriptString_Release(&model);
                } else if (string_is(&type, 0x00825340)) {
                    ScriptString model;
                    ScriptString_FromCString(&model, pointer<const char>(0x0082534c));  // "xmodel/vehicle_tank_flakpanzer"
                    slot<SetModel>(0x00922548)(ent, &model, 0);  // GScr_SetModel
                    ScriptString_Release(&model);
                }
                // Manned (level bytes +0x684), the level flag array key 0xe337f297 and bytes +0x548 cleared.
                level_byte(0x684, flaknum) = 1;
                {
                    ScriptKeyTable* const table = level_table();
                    uint32_t* value = ScriptKeyTable_Find(table, 0xe337f297);
                    if (!value) {
                        const uint32_t empty[3] = {0, 0, 0};
                        value = ScriptKeyTable_SetArray_4(table, 0xe337f297, empty);
                    }
                    DynArrayBytes* const flags = pointer<DynArrayBytes>(*value);
                    if (flaknum >= flags->size) DynArray_ResizeBytesNoFill(flags, flaknum + 1, flaknum + 4);
                    flags->data[flaknum] = 0;
                }
                level_byte(0x548, flaknum) = 0;
                // The flak's setup function (key 0xc511e266: flak, type, the anim set 0xc4813666).
                {
                    using Setup = void XB_CDECL(uint32_t, ScriptString, const uint32_t*);
                    const uint32_t* const anims = flak_anims(ent);
                    ScriptString copy;
                    ScriptString_Copy(&copy, &type);
                    title_function<Setup>(*key_slot(ent, 0xc511e266, 0))(ent, copy, anims);
                }
                // Per guy: gun in hand, the crew think (function key 0x2f31323b: guy, index), a
                // BroScr_FlakStopMovingOnDeath thread, the clip-off bool 0x1ea97196 cleared.
                for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(guys.size); ++i) {
                    BroScr_PutGunInHand(element(&guys, i), 0x0036b1a5);
                    const uint32_t think = *key_slot(ent, 0x2f31323b, 0);
                    ScriptString message;
                    ScriptString_FromCString(&message, pointer<const char>(0x0082531c));  // "No function ptr!"
                    ScrLocal_ReleaseString(ent, think != 0, message);
                    const uint32_t guy = element(&guys, i);
                    thread_2(guy, think, guy, i);
                    thread_3(ent, own_address(&BroScr_FlakStopMovingOnDeath), ent, element(&guys, i), flaknum);  // BroScr_FlakStopMovingOnDeath
                    *bool_slot(element(&guys, i), 0x1ea97196) = 0;
                }
                // A BroScr_FlakWaitDeath thread {0x007f4b84: guy, the flak's origin, flaknum} per guy.
                for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(guys.size); ++i) {
                    ScrVec3 origin;
                    origin_of(&origin, ent);
                    const uint32_t guy = element(&guys, i);
                    uint32_t* const functor = new_functor(0x1c);
                    if (functor) {
                        functor[0] = 0x007f4b84;
                        functor[1] = own_address(&BroScr_FlakWaitDeath);  // BroScr_FlakWaitDeath
                        functor[2] = guy;
                        functor[3] = origin.v[0];
                        functor[4] = origin.v[1];
                        functor[5] = origin.v[2];
                        functor[6] = flaknum;
                    }
                    create_thread(guy, functor);
                }
                level_int(0x50c, flaknum) = 0;
                level_int(0x2d4, flaknum) = 0;
                *bool_slot(ent, 0x34314719) = 0;
                *int_slot(ent, 0x872c50bf) = 0;
                thread_array(ent, own_address(&BroScr_FlakLoaderAmmoAnim), ent, &guys);  // BroScr_FlakLoaderAmmoAnim
                thread_2(ent, own_address(&BroScr_FlakPanzerStopLoopSounds), ent, flaknum);    // BroScr_FlakPanzerStopLoopSounds
                thread_2(ent, own_address(&BroScr_FlakDied), ent, flaknum);    // BroScr_FlakDied
                if (string_is(&type, 0x00825340)) {
                    flak_sound_blend(ent, 0x00824a94, flaknum);  // "flakpanzer_loop"
                    flak_sound_blend(ent, 0x00825304, flaknum);  // "flakpanzer_loop_fast"
                }
                // While manned: with the level flag clear, the crew anims (guns in hand) and a wait for
                // them ("flak_anim_done<n>" on the level; 2 s for flakairlow); else guns on the back, 1 s.
                ScriptString anim, done;
                ScriptString_FromRep(&anim, nullptr);
                ScriptString_FromRep(&done, nullptr);
                const ScriptString* const empty = pointer<const ScriptString>(0x00922e90);
                while (level_byte(0x684, flaknum)) {
                    if (string_is(&type, 0x008251f8) || string_is(&type, 0x00823544)) {  // "flakair", "flaktank"
                        if (!flak_flag_clear(flaknum)) {
                            guns_in_hand(&guys, 0x07151174);
                            Scr_Wait(1.0f);
                            continue;
                        }
                        guns_in_hand(&guys, 0x0036b1a5);
                        if (level_byte(0x548, flaknum)) {
                            ScriptString_AssignCString(&anim, pointer<const char>(0x008252e8));  // "preidle"
                        } else if (!level_int(0x2d4, flaknum)) {
                            level_int(0x2d4, flaknum) = 1;
                            ScriptString_AssignCString(&anim, pointer<const char>(0x008252e0));  // "turn"
                        } else if (random_int(100) > 95) {
                            ScriptString_AssignCString(&anim, pointer<const char>(0x00824c88));  // "twitch"
                        } else if (random_int(100) > 60) {
                            ScriptString_AssignCString(&anim, pointer<const char>(0x0082c09c));  // "fire"
                        } else {
                            ScriptString_AssignCString(&anim, pointer<const char>(0x00804310));  // "idle"
                        }
                        crew_anims_thread(ent, &anim, flaknum, empty);
                        if (string_is(&anim, 0x0082c09c)) slot<Ent>(0x009228d4)(ent);  // GScr_Vehicle_FireWeapon
                        crew_anim_threads(&guys, ent, flaknum, &anim);
                        wait_flak_anim_done(&done, flaknum);
                    } else if (string_is(&type, 0x00823538)) {  // "flakairlow"
                        if (!flak_flag_clear(flaknum)) {
                            guns_in_hand(&guys, 0x07151174);
                            Scr_Wait(1.0f);
                            continue;
                        }
                        guns_in_hand(&guys, 0x0036b1a5);
                        if (level_byte(0x548, flaknum)) {
                            ScriptString_AssignCString(&anim, pointer<const char>(0x00804310));  // "idle"
                        } else if (!level_int(0x2d4, flaknum)) {
                            level_int(0x2d4, flaknum) = 1;
                            ScriptString_AssignCString(&anim, pointer<const char>(0x00804310));  // "idle"
                        } else if (random_int(100) > 70) {
                            ScriptString_AssignCString(&anim, pointer<const char>(0x0082c09c));  // "fire"
                        } else {
                            ScriptString_AssignCString(&anim, pointer<const char>(0x00804310));  // "idle"
                        }
                        crew_anims_thread(ent, &anim, flaknum, empty);
                        if (string_is(&anim, 0x0082c09c)) flak_sound(ent, 0x008252f8);  // "Flak88_fire"
                        crew_anim_threads(&guys, ent, flaknum, &anim);
                        Scr_Wait(2.0f);
                    } else if (string_is(&type, 0x00825340)) {  // "flakpanzer"
                        if (!flak_flag_clear(flaknum)) {
                            guns_in_hand(&guys, 0x07151174);
                            Scr_Wait(1.0f);
                            continue;
                        }
                        guns_in_hand(&guys, 0x0036b1a5);
                        if (random_int(100) > 50)
                            ScriptString_AssignCString(&anim, pointer<const char>(0x00824b44));  // "fireA"
                        else
                            ScriptString_AssignCString(&anim, pointer<const char>(0x008252f0));  // "fireB"
                        {
                            ScriptString panzer;
                            ScriptString_FromCString(&panzer, pointer<const char>(0x00824b4c));  // "panzer"
                            crew_anims_thread(ent, &anim, flaknum, &panzer);
                            ScriptString_Release(&panzer);
                        }
                        crew_anim_threads(&guys, ent, flaknum, &anim);
                        wait_flak_anim_done(&done, flaknum);
                    }
                }
                ScriptString_Release(&done);
                ScriptString_Release(&anim);
            }
        }
        if (guys.data) Scr_Free(guys.data);
        if (spawners.data) Scr_Free(spawners.data);
        if (targets.data) Scr_Free(targets.data);
    }
    ScriptString_Release(&type);
}

namespace {

// The level's byte array key 0xe337f297 (added empty when missing) [i] (grown without the fill).
inline uint8_t& level_flag(uint32_t i) {
    ScriptKeyTable* const table = level_table();
    uint32_t* value = ScriptKeyTable_Find(table, 0xe337f297);
    if (!value) {
        const uint32_t empty[3] = {0, 0, 0};
        value = ScriptKeyTable_SetArray_4(table, 0xe337f297, empty);
    }
    DynArrayBytes* const flags = pointer<DynArrayBytes>(*value);
    if (i >= flags->size) DynArray_ResizeBytesNoFill(flags, i + 1, i + 4);
    return flags->data[i];
}

// The entity's own byte array key 0xe337f297 (added empty when missing, ScriptKeyTable_SetArrayBytes).
inline DynArrayBytes* ent_flags(uint32_t ent) {
    ScriptKeyTable* const table = ent_script_object(ent);
    uint32_t* value = ScriptKeyTable_Find(table, 0xe337f297);
    if (!value) {
        const uint32_t empty[3] = {0, 0, 0};
        value = ScriptKeyTable_SetArrayBytes(table, 0xe337f297, empty);
    }
    return pointer<DynArrayBytes>(*value);
}

inline DynArray* key_array(uint32_t ent, uint32_t key) {
    return own_function<DynArray* XB_THISCALL(ScriptKeyTable*, uint32_t)>(&ScriptKeyTable_GetArray)(ent_script_object(ent), key);  // ScriptKeyTable_GetArray
}

}  // namespace

void XB_CDECL BroScr_FlakClaimTriggerThink(uint32_t trigger, uint32_t flaknum) {
    using GetEntArray = uint32_t XB_CDECL(const ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t);
    bool targeted;
    {
        ScriptString target;
        targeted = ScrCall_ScrEnt_GetTarget(&trigger, &target)->rep != nullptr;
        ScriptString_Release(&target);
    }
    if (targeted) {
        // The target trigger clears the level's byte +0x548 [flaknum] (set now) when touched.
        uint32_t other;
        {
            ScriptString target;
            other = slot<GetEntArray>(0x009221c4)(ScrCall_ScrEnt_GetTarget(&trigger, &target), 0x0d786b4c, 0, 0, 0);  // GScr_GetEntArray
            ScriptString_Release(&target);
        }
        thread_2(other, own_address(&BroScr_WaitTriggerClearFlag), other, flaknum);  // BroScr_WaitTriggerClearFlag
        level_byte(0x548, flaknum) = 1;
    }
    // While manned: whoever touches the trigger (unless its own flag 0xe337f297 [flaknum] is set and not
    // the undefined bool) takes the flak: the level's and its own flag set, a BroScr_FlipLevelFlagOnDeath
    // thread on the level; then 1 s.
    uint32_t who = 0;
    while (level_byte(0x684, flaknum)) {
        title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t*)>(0x004b1590)(trigger, 0xe2a210cf, &who);  // Scr_WaitTillOutputEnt_ORAN
        const uint32_t user = who;
        bool defined;
        bool taken = false;
        if (*ScrField_he337f297_IsDefined(&defined, user)) {
            DynArrayBytes* const own = ent_flags(user);
            if (flaknum >= own->size) DynArray_ResizeBytes(own, flaknum + 1, flaknum + 4);
            taken = own->data[flaknum] != title_global<uint8_t>(0x00922ed0);
        }
        if (!taken) {
            level_flag(flaknum) = 1;
            DynArrayBytes* const own = ent_flags(user);
            if (flaknum >= own->size) DynArray_ResizeBytes(own, flaknum + 1, flaknum + 4);
            own->data[flaknum] = 1;
            const uint32_t level = level_handle();
            thread_3(level, own_address(&BroScr_FlipLevelFlagOnDeath), level, user, flaknum);  // BroScr_FlipLevelFlagOnDeath
        }
        Scr_Wait(1.0f);
    }
}

void XB_CDECL BroScr_FlakGunnerFireAnim(uint32_t ent, uint32_t flaknum) {
    using Start = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, ScriptString*, uint32_t*,
                                    const ScriptString*);
    Scr_EndOnString(ent, pointer<const char>(0x00824c90));  // "fire_turret"
    ScriptString anim, done;
    ScriptString_FromRep(&anim, nullptr);
    ScriptString_FromRep(&done, nullptr);
    while (level_byte(0x684, flaknum)) {
        if (flak_flag_clear(flaknum)) {
            if (random_int(100) > 95)
                ScriptString_AssignCString(&anim, pointer<const char>(0x00824c88));  // "twitch"
            else
                ScriptString_AssignCString(&anim, pointer<const char>(0x00804310));  // "idle"
            own_function<Start>(&Scr_StartThread_ent_str_i_str_2_entry)(0, 0, 0, 0, own_address(&BroScr_FlakCrewAnims), ent, &anim, &flaknum,
                                              pointer<const ScriptString>(0x00922e90));  // Scr_StartThread_ent_str_i_str_2(BroScr_FlakCrewAnims)
        }
        wait_flak_anim_done(&done, flaknum);
    }
    ScriptString_Release(&done);
    ScriptString_Release(&anim);
}

void XB_CDECL BroScr_SquadManagerSpawnSquad(uint32_t ent, ScriptString group, int32_t max_alive, int32_t max_total,
                                            ScriptString endon, float interval, uint32_t think) {
    using Spawn = uint32_t XB_CDECL(uint32_t, uint32_t);
    using SetGoal = void XB_CDECL(uint32_t, const uint32_t*);
    Scr_EndOnString(ent, endon.rep ? endon.rep->text : nullptr);
    uint32_t node = 0;
    uint32_t spawner = 0;
    DynArray nodes = {nullptr, 0, 0};
    DynArray spawners = {nullptr, 0, 0};
    DynArray ai = {nullptr, 0, 0};
    {
        ScriptString key;
        ScriptString_FromCString(&key, pointer<const char>(0x0082dda8));  // "targetname"
        Scr_GetNodeArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&group)),
                         static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&key)), reinterpret_cast<uint32_t*>(&nodes));
        ScriptString_Release(&key);
    }
    {
        ScriptString copy;
        ScriptString_Copy(&copy, &group);
        BroScr_GetSpawnersByGroupField(copy, &spawners);
    }
    if (!spawners.size || !nodes.size) {
        ScriptString text;
        if (!spawners.size)
            ScriptString_FromCString(&text, pointer<const char>(0x00824cd4));  // "SQUAD MANAGER:  Could not find spawners for squad "
        else
            ScriptString_FromCString(&text, pointer<const char>(0x00824c9c));  // "...target nodes for squad "
        ScriptString message;
        ScriptString_Concat(&message, &text, &group);
        ScrLocal_ReleaseString_2(message);
        ScriptString_Release(&text);
        if (spawners.data) Scr_Free(spawners.data);
        if (nodes.data) Scr_Free(nodes.data);
        ScriptString_Release(&group);
        ScriptString_Release(&endon);
        return;
    }
    if (interval == title_global<float>(0x00922eac)) interval = title_global<float>(0x0083edc8);  // 0.05
    // Keeps the squad (string field 0x2dae9972 = group) at the alive limit within the total: spawners in
    // turn (forced with int key 0x02d62a20 set), each spawn sent to the next node (handle key 0x05374cc6),
    // after 0.01 s added with its think thread (`think`, else BroScr_SquadMemberThink).
    for (;;) {
        {
            ScriptString copy;
            ScriptString_Copy(&copy, &group);
            BroScr_GetAIByGroupField(copy, &ai);
        }
        while (static_cast<int32_t>(ai.size) < max_alive && static_cast<int32_t>(ai.size) < max_total) {
            uint32_t guy;
            const uint32_t* const force = ScriptKeyTable_Find(ent_script_object(element(&spawners, spawner)), 0x02d62a20);
            if (force && *force != undefined_int() && *int_slot(element(&spawners, spawner), 0x02d62a20))
                guy = slot<Spawn>(0x00922520)(element(&spawners, spawner), 0);  // GScr_ForceSpawn
            else
                guy = slot<Spawn>(0x00922518)(element(&spawners, spawner), 0);  // GScr_DoSpawn
            if (entity_handle_is_valid(guy)) {
                slot<SetGoal>(0x00922688)(guy, element_at(&nodes, node, kReserveInt0));  // GScr_SetGoalPosFromNode
                const uint32_t* const at = element_at(&nodes, node, kReserveInt0);
                *key_slot(guy, 0x05374cc6, 0) = *at;
                if (static_cast<int32_t>(++node) >= static_cast<int32_t>(nodes.size)) node = 0;
                Scr_Wait(float_bits(0x3c23d70a));  // 0.01
                const uint32_t n = ai.size;
                DynArray_Resize(reinterpret_cast<uint8_t*>(&ai), n + 1, n + 4);
                ai.data[n] = guy;
                uint32_t* const functor = new_functor(0xc);
                if (think) {
                    if (functor) {
                        functor[0] = 0x007f5ba0;
                        functor[1] = think;
                        functor[2] = guy;
                    }
                } else if (functor) {
                    functor[0] = 0x007f5ba0;
                    functor[1] = own_address(&BroScr_SquadMemberThink);  // BroScr_SquadMemberThink
                    functor[2] = guy;
                }
                create_thread(guy, functor);
            }
            if (static_cast<int32_t>(++spawner) >= static_cast<int32_t>(spawners.size)) spawner = 0;
            Scr_Wait(interval);
        }
        Scr_Wait(float_bits(0x3d4ccccd));  // 0.05
    }
}

void XB_CDECL BroScr_VehicleDeath_Stuka(uint32_t ent) {
    bool flying;
    {
        ScriptString model;
        flying = string_is(ScrCall_ScrEnt_GetModel(&ent, &model), 0x007f8db8);  // "stuka_flying"
        ScriptString_Release(&model);
    }
    if (flying) {
        // The death model: the same model on piano_lupo (level +0x1c4 the map name), else "_d1".
        uint32_t text;
        if (string_is(pointer<ScriptString>(title_global<uint32_t>(0x00922ee0) + 0x1c4), 0x0082bc28))  // "piano_lupo"
            text = 0x007f8db8;
        else
            text = 0x00824d08;  // "stuka_flying_d1"
        ScriptString_AssignCString(string_key(ent, 0xd6eafad9), pointer<const char>(text));
    }
    BroScr_DeleteUndroppedBombs(ent);
    uint32_t attacker = 0;
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t*)>(0x004b1590)(ent, kDeath, &attacker);  // Scr_WaitTillOutputEnt_ORAN
    BroScr_DeleteVehicleTurrets(ent);
    flak_sound(ent, 0x008007a8);  // "explo_metal_rand"
    ScrVec3 origin;
    origin_of(&origin, ent);
    slot<void XB_CDECL(uint32_t, uint32_t, const ScrVec3*, uint32_t)>(0x00922444)(0x3e800000, 0x40400000, &origin,
                                                                                  0x44834000);  // GScr_Earthquake(0.25, 3, 1050)
    bool defined;
    if (*ScrField_deathmodel_IsDefined(&defined, ent))
        slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(ent, string_key(ent, 0xd6eafad9), 0);  // GScr_SetModel
    slot<EntHash>(0x009221a4)(ent, 0x07cd8ff6);  // Scr_QueueNotify
}

void XB_CDECL BroScr_DeleteArraysOnVehicleRemoved(uint32_t, uint32_t vehicle) {
    DynArray first = {nullptr, 0, 0};
    own_function<void XB_THISCALL(DynArray*, const DynArray*)>(&DynArray_Assign4_PROLOGUE)(&first, key_array(vehicle, 0x9ecce58a));  // DynArray_Assign4_PROLOGUE
    DynArray second = {nullptr, 0, 0};
    own_function<void XB_THISCALL(DynArray*, const DynArray*)>(&DynArray_Assign4_PROLOGUE)(&second, key_array(vehicle, 0xb0996bdc));
    // Every 5 s until the vehicle is gone, then the still valid entities of both copies deleted.
    bool removed;
    do {
        removed = !entity_handle_is_valid(vehicle);
        Scr_Wait(5.0f);
    } while (!removed);
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(second.size); ++i)
        if (entity_handle_is_valid(element(&second, i))) slot<Ent>(0x00922540)(element(&second, i));  // GScr_Delete
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(first.size); ++i)
        if (entity_handle_is_valid(element(&first, i))) slot<Ent>(0x00922540)(element(&first, i));  // GScr_Delete
    if (second.data) Scr_Free(second.data);
    if (first.data) Scr_Free(first.data);
}

bool* XB_CDECL BroScr_StunnedWait(bool* out, uint32_t ent) {
    bool defined;
    if (*ScrField_stunned_IsDefined(&defined, ent) && *int_slot(ent, 0x2f66b4d8) == 1)
        Scr_WaitTillString(ent, pointer<const char>(0x00820d2c));  // "stundone"
    // False without a valid, living first entry in the queue 0x4ced49f6; true without attack_range or
    // with the player inside it; else after 1 s false.
    if (!*BroScr_RotateQueue_h4ced49f6(reinterpret_cast<uint8_t*>(&defined), ent)) {
        *out = false;
        return out;
    }
    DynArray* const queue = key_array(ent, 0x4ced49f6);
    if (!queue->size) DynArray_Resize(reinterpret_cast<uint8_t*>(queue), 1, 4);
    const uint32_t target = queue->data[0];
    if (!entity_handle_is_valid(target) || health(target) <= 0) {
        *out = false;
        return out;
    }
    if (!*ScrField_attack_range_IsDefined(&defined, ent)) {
        *out = true;
        return out;
    }
    uint32_t at[3], player_at[3];
    const uint32_t* const o = ScrCall_ScrEnt_GetOrigin(&ent, at);
    const uint32_t* const p = ScrCall_GScr_GetOrigin(player_at, pointer<const uint32_t>(0x00922eec));
    const int32_t range = static_cast<int32_t>(*ScrField_attack_range(ent));
    const long double d = slot<long double XB_CDECL(const uint32_t*, const uint32_t*)>(0x00922268)(p, o);  // Scr_Distance
    if (d < static_cast<long double>(range)) {
        *out = true;
        return out;
    }
    Scr_Wait(1.0f);
    *out = false;
    return out;
}

namespace {

// Whether the entity's script_noteworthy is set and "stacker".
inline bool is_stacker(uint32_t* ent) {
    ScriptString first;
    bool stacker = false;
    if (ScrCall_ScrEnt_GetScriptNoteworthy(ent, &first)->rep) {
        ScriptString again;
        stacker = string_is(ScrCall_ScrEnt_GetScriptNoteworthy(ent, &again), 0x00824d34);  // "stacker"
        ScriptString_Release(&again);
    }
    ScriptString_Release(&first);
    return stacker;
}

// The valid entities `ent`'s target names, flagged (GScr_SetEntFlag400) and appended to `stacked`
// (`targets` refilled).
inline void flag_stacked(uint32_t* ent, DynArray* targets, DynArray* stacked) {
    {
        ScriptString target;
        Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ScrCall_ScrEnt_GetTarget(ent, &target))),
                        0x0d786b4c, reinterpret_cast<uint8_t*>(targets), 0);  // by targetname
        ScriptString_Release(&target);
    }
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(targets->size); ++i) {
        if (!entity_handle_is_valid(element(targets, i))) continue;
        slot<Ent>(0x00922568)(element(targets, i));  // GScr_SetEntFlag400
        DynArray_PushBack(reinterpret_cast<uint8_t*>(stacked), &element(targets, i));
    }
}

// A BroScr_SpawnWoodGibs thread {0x008244dc: ent, at, flag} on `ent`.
inline void wood_gibs_thread(uint32_t ent, const ScrVec3* at, uint8_t flag) {
    uint32_t* const functor = new_functor(0x1c);
    if (functor) {
        functor[0] = 0x008244dc;
        functor[1] = own_address(&BroScr_SpawnWoodGibs);  // BroScr_SpawnWoodGibs
        functor[2] = ent;
        functor[3] = at->v[0];
        functor[4] = at->v[1];
        functor[5] = at->v[2];
        *reinterpret_cast<uint8_t*>(&functor[6]) = flag;
    }
    create_thread(ent, functor);
}

}  // namespace

void XB_CDECL BroScr_SetupStackers_2(uint32_t ent) {
    uint8_t stacker = 0;
    uint32_t& count = *pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) + 0x314);
    if (count == undefined_int()) count = 0;
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x0092255c)(ent, 1);  // GScr_SetTakeDamage
    DynArray targets = {nullptr, 0, 0};
    DynArray stacked = {nullptr, 0, 0};
    if (is_stacker(&ent)) {
        stacker = 1;
    } else {
        bool targeted;
        {
            ScriptString target;
            targeted = ScrCall_ScrEnt_GetTarget(&ent, &target)->rep != nullptr;
            ScriptString_Release(&target);
        }
        if (targeted) flag_stacked(&ent, &targets, &stacked);
    }
    // script_health (1 without) taken down by the damage (0xce270885) the level turret's user, the
    // world, or anyone while the turret is gone deals; then the stacked entities unflagged and wood gibs
    // at the last attacker (at the stack, 10 units up, when the attacker sits at the origin).
    int32_t left = 1;
    bool defined;
    if (*ScrField_script_health_IsDefined(&defined, ent)) left = static_cast<int32_t>(*ScrField_script_health(ent));
    uint32_t damage = undefined_int();
    uint32_t attacker = 0;
    while (left > 0) {
        title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t*, uint32_t*)>(0x0045cf40)(ent, 0xce270885, &damage,
                                                                                         &attacker);  // Scr_WaitTillOutput2_PROLOGUE
        bool counts = true;
        if (entity_handle_is_valid(*pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) + 0x7f0)) &&
            slot<uint32_t XB_CDECL(uint32_t)>(0x00922588)(*pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) + 0x7f0)) !=
                attacker) {  // GScr_Turret_GetUser
            ScriptString classname;
            counts = string_is(ScrCall_ScrEnt_GetClassname(&attacker, &classname), 0x0083a984);  // "worldspawn"
            ScriptString_Release(&classname);
        }
        if (counts) left -= static_cast<int32_t>(damage);
    }
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(stacked.size); ++i)
        slot<Ent>(0x00922564)(element(&stacked, i));  // GScr_ClearEntFlag400
    ScrVec3 at;
    origin_of(&at, attacker);
    if (as_float(at.v[0]) == 0.0f && as_float(at.v[1]) == 0.0f && as_float(at.v[2]) == 0.0f) {
        ScrVec3 o;
        origin_of(&o, ent);
        const float z = as_float(o.v[2]) - title_global<float>(0x0083ed10);  // -10
        o.v[2] = bits(z);
        wood_gibs_thread(ent, &o, stacker);
    } else {
        ScrVec3 a;
        origin_of(&a, attacker);
        wood_gibs_thread(ent, &a, stacker);
    }
    if (stacked.data) Scr_Free(stacked.data);
    if (targets.data) Scr_Free(targets.data);
}

void XB_CDECL BroScr_SetupStackers(uint32_t ent) {
    bool untargeted;
    {
        ScriptString target;
        untargeted = ScrCall_ScrEnt_GetTarget(&ent, &target)->rep == nullptr;
        ScriptString_Release(&target);
    }
    if (untargeted) {
        slot<Ent>(0x00922540)(ent);  // GScr_Delete
        return;
    }
    uint32_t& count = *pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) + 0x314);
    if (count == undefined_int()) count = 0;
    DynArray targets = {nullptr, 0, 0};
    {
        ScriptString target;
        Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ScrCall_ScrEnt_GetTarget(&ent, &target))),
                        0x0d786b4c, reinterpret_cast<uint8_t*>(&targets), 0);  // by targetname
        ScriptString_Release(&target);
    }
    if (!targets.size) {
        slot<Ent>(0x00922540)(ent);  // GScr_Delete
        if (targets.data) Scr_Free(targets.data);
        return;
    }
    // Per valid target with a target: flag byte = the trigger is a "stacker" (then nothing else), else
    // the entities it targets flagged and collected. After the trigger is touched they are unflagged, wood
    // gibs (with the flag) at whoever touched it on each valid target, and the trigger deleted.
    DynArray sub = {nullptr, 0, 0};
    DynArray stacked = {nullptr, 0, 0};
    DynArrayBytes flags = {nullptr, 0, 0};
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(targets.size); ++i) {
        bool skip = true;
        ScriptString target;
        bool got = false;
        if (entity_handle_is_valid(element(&targets, i))) {
            got = true;
            skip = ScrCall_ScrEnt_GetTarget(&element(&targets, i), &target)->rep == nullptr;
        }
        if (got) ScriptString_Release(&target);
        if (skip) continue;
        if (is_stacker(&ent)) {
            if (i >= flags.size) DynArray_ResizeBytes(&flags, i + 1, i + 4);
            flags.data[i] = 1;
            continue;
        }
        if (i >= flags.size) DynArray_ResizeBytes(&flags, i + 1, i + 4);
        flags.data[i] = 0;
        flag_stacked(&element(&targets, i), &sub, &stacked);
    }
    uint32_t who = 0;
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t*)>(0x004b1590)(ent, 0xe2a210cf, &who);  // Scr_WaitTillOutputEnt_ORAN
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(stacked.size); ++i)
        slot<Ent>(0x00922564)(element(&stacked, i));  // GScr_ClearEntFlag400
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(targets.size); ++i) {
        if (!entity_handle_is_valid(element(&targets, i))) continue;
        if (i >= flags.size) DynArray_ResizeBytes(&flags, i + 1, i + 4);
        ScrVec3 at;
        origin_of(&at, who);
        const uint32_t target = element(&targets, i);
        wood_gibs_thread(target, &at, flags.data[i]);
    }
    slot<Ent>(0x00922540)(ent);  // GScr_Delete
    if (flags.data) Scr_Free(flags.data);
    if (stacked.data) Scr_Free(stacked.data);
    if (sub.data) Scr_Free(sub.data);
    if (targets.data) Scr_Free(targets.data);
}

void XB_CDECL BroScr_WallHopTrigger(uint32_t ent) {
    using HopOverWall = void XB_CDECL(uint32_t, DynArray, ScrVec3);
    using Touching = uint8_t XB_CDECL(uint32_t, uint32_t);
    Scr_EndOnString(ent, pointer<const char>(0x00831f60));  // "death"
    BroScr_WallHopGetScriptOrigins(ent);
    bool defined;
    if (!*ScrField_h4f7c0a56_IsDefined(&defined, ent)) return;
    if (static_cast<int32_t>(title_function<DynArray* XB_THISCALL(ScriptKeyTable*, uint32_t)>(0x00602a40)(
                                 ent_script_object(ent), 0x4f7c0a56)->size) < 2)  // ScriptKeyTable_GetOrAddArray_5
        return;
    for (;;) {
        do {
            Scr_WaitTillString(ent, pointer<const char>(0x0083aa58));  // "trigger"
        } while (!slot<Touching>(0x00922530)(player(), ent));  // GScr_IsTouching
        // While the player touches it: facing the wall (yaw within 45 degrees of the direction from the
        // first point to the second), the hint once per level (bool key 0xddfa2c42 unset) and on the use
        // button the hop (BroScr_PlayerHopOverWall, then 1 s). The points come from a by-value copy that
        // the callee fills and frees, so this local stays empty.
        do {
            DynArray points = {nullptr, 0, 0};
            {
                DynArray copy = {nullptr, 0, 0};
                title_function<void XB_THISCALL(DynArray*, const DynArray*)>(0x0045c410)(&copy, &points);  // DynArray_Assign12_PROLOGUE
                own_function<void XB_CDECL(uint32_t, DynArray)>(&BroScr_WallHopGetPointsNearFirst_entry)(ent, copy);  // BroScr_WallHopGetPointsNearFirst
            }
            using Reserve12 = void XB_THISCALL(DynArray*, uint32_t, uint32_t);
            if (points.size == 0) title_function<Reserve12>(0x0045cde0)(&points, 1, 4);  // DynArray_Reserve12_PROLOGUE
            const float* const first = reinterpret_cast<const float*>(points.data);
            if (points.size <= 1) title_function<Reserve12>(0x0045cde0)(&points, 2, 5);
            const float* const second = reinterpret_cast<const float*>(points.data) + 3;
            const float dz = second[2] - first[2];
            const float dy = second[1] - first[1];
            const float dx = second[0] - first[0];
            const uint32_t dir[3] = {bits(dx), bits(dy), bits(dz)};
            const uint32_t lane = undefined_lane();
            uint32_t angles[3] = {lane, lane, lane};
            slot<void XB_CDECL(uint32_t*, const uint32_t*)>(0x00922284)(angles, dir);  // Scr_VectorToAngles
            const ScrVec3 to = {{angles[0], angles[1], angles[2]}};
            ScrVec3 view;
            angles_of(&view, player());
            float yaw = as_float(view.v[1]);
            if (0.0f > yaw) yaw += 360.0f;
            float high = as_float(to.v[1]) + 45.0f;
            if (high > 360.0f) high -= 360.0f;
            float low = as_float(to.v[1]) - 45.0f;
            if (0.0f > low) low += 360.0f;
            if (high > yaw && yaw > low) {
                const uint32_t* const shown = ScriptKeyTable_Find(level_table(), 0xddfa2c42);
                if ((shown ? *reinterpret_cast<const uint8_t*>(shown) : title_global<uint8_t>(0x00922ed0)) == 0) {
                    title_function<void XB_CDECL(const char*)>(0x005b45b0)(pointer<const char>(0x00824d3c));  // ScrCall_SendGameMessageBoldCStr_LIBEATOR("Press Activate to Hop over wall.")
                    thread_1(ent, 0x007f5ba0, own_address(&BroScr_ShowContextTextBriefly), ent);  // BroScr_ShowContextTextBriefly
                }
                if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922804)(player())) {  // GScr_PlayerGetFlag40_594 (use)
                    const uint32_t object = title_global<uint32_t>(0x00922ee0);
                    const uint32_t* const level = pointer<uint32_t>(object ? object + 4 : 0);
                    DynArray copy = {nullptr, 0, 0};
                    title_function<void XB_THISCALL(DynArray*, const DynArray*)>(0x0045c410)(&copy, &points);  // DynArray_Assign12_PROLOGUE
                    own_function<HopOverWall>(&BroScr_PlayerHopOverWall)(*level, copy, to);  // BroScr_PlayerHopOverWall
                    Scr_Wait(1.0f);
                }
            }
            Scr_Wait(float_bits(0x3d4ccccd));  // 0.05
            if (points.data) Scr_Free(points.data);
        } while (slot<Touching>(0x00922530)(player(), ent));
    }
}

void XB_CDECL BroScr_VehicleDeath_Flakvierling(uint32_t ent) {
    bool full;
    {
        ScriptString model;
        full = string_is(ScrCall_ScrEnt_GetModel(&ent, &model), 0x00824c44);  // "v_flakvierling"
        ScriptString_Release(&model);
    }
    uint32_t text;
    if (full)
        text = 0x00824c44;
    else
        text = 0x00824d60;  // "v_flakvierling_base_d1"
    ScriptString_AssignCString(string_key(ent, 0xd6eafad9), pointer<const char>(text));
    Scr_WaitTill(ent, kDeath);
    flak_sound(ent, 0x007f50f0);  // "artillery_generic"
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(ent, string_key(ent, 0xd6eafad9), 0);  // GScr_SetModel
    slot<Ent>(0x009228d0)(ent);  // GScr_Vehicle_ClearTurretTarget
    BroScr_ReleaseGunners(ent);
    ScrVec3 origin;
    origin_of(&origin, ent);
    slot<void XB_CDECL(uint32_t, uint32_t, const ScrVec3*, uint32_t)>(0x00922444)(0x3e800000, 0x40400000, &origin,
                                                                                  0x44834000);  // GScr_Earthquake(0.25, 3, 1050)
    ScrVec3 at;
    origin_of(&at, ent);
    slot<void XB_CDECL(const ScrVec3*, uint32_t, uint32_t, uint32_t)>(0x00922428)(&at, 0x437a0000, 0x447a0000,
                                                                                  0x42c80000);  // GScr_RadiusDamage(250, 1000, 100)
}

namespace {

// The rider's fire anim (the word at `anim`) played at the tag over and over.
inline void rider_fire_loop(uint32_t vehicle, uint32_t rider, ScriptString* tag, uint32_t anim) {
    using Anim = void XB_CDECL(uint32_t, uint32_t, const uint32_t*, const uint32_t*, uint32_t);
    using TagVec = void XB_CDECL(uint32_t, uint32_t*, ScriptString*);
    for (;;) {
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922748)(rider, title_global<uint32_t>(anim));  // ScrActor_SetInt_1370
        const uint32_t lane = undefined_lane();
        uint32_t a[3] = {lane, lane, lane};
        slot<TagVec>(0x00922620)(vehicle, a, tag);  // GScr_GetTagAngles
        const uint32_t angles[3] = {a[0], a[1], a[2]};
        const uint32_t lane_2 = undefined_lane();
        uint32_t o[3] = {lane_2, lane_2, lane_2};
        slot<TagVec>(0x0092261c)(vehicle, o, tag);  // GScr_GetTagOrigin
        const uint32_t origin[3] = {o[0], o[1], o[2]};
        slot<Anim>(0x009224c0)(rider, 0x54e3b2b6, origin, angles, title_global<uint32_t>(anim));  // GScr_AnimScriptedNoNotify
        Scr_WaitTill(rider, 0x54e3b2b6);
    }
}

}  // namespace

void XB_CDECL BroScr_VehicleRiderWait(uint32_t vehicle, uint32_t rider, ScriptString tag) {
    using Call = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, ScriptString*);
    slot<EntHash>(0x00922188)(rider, kDeath);     // Scr_EndOn
    slot<EntHash>(0x00922188)(rider, 0xa0e65a8e);  // Scr_EndOn
    own_function<Call>(&Scr_CallThread_ent_ent_str_entry)(0, 0, 0, 0, own_address(&BroScr_VehicleRiderMount), vehicle, &rider, &tag);  // Scr_CallThread_ent_ent_str(BroScr_VehicleRiderMount)
    if (string_is(&tag, 0x007fa47c)) rider_fire_loop(vehicle, rider, &tag, 0x008cd38c);  // "tag_gunner", g_anim_flakpanzer_gunner_fire_a
    if (string_is(&tag, 0x007fc0bc)) rider_fire_loop(vehicle, rider, &tag, 0x008cd398);  // "tag_passenger1", g_anim_flakpanzer_leftloader_fire_b
    if (string_is(&tag, 0x007f5c08)) rider_fire_loop(vehicle, rider, &tag, 0x008cd3a4);  // "tag_passenger2", g_anim_flakpanzer_rightloader_fire_a
    ScriptString_Release(&tag);
}

namespace {

// Whether the entity's script_noteworthy is set and equals `text`.
inline bool noteworthy_is(uint32_t* ent, uint32_t text) {
    ScriptString first;
    bool equal = false;
    if (ScrCall_ScrEnt_GetScriptNoteworthy(ent, &first)->rep) {
        ScriptString again;
        equal = string_is(ScrCall_ScrEnt_GetScriptNoteworthy(ent, &again), text);
        ScriptString_Release(&again);
    }
    ScriptString_Release(&first);
    return equal;
}

// The plane path end shared by the two BroScr_VehicleFollowPathToEnd copies (`second`: the _2 turret
// and crash-sound helpers).
template <bool second>
inline void follow_path_to_end(uint32_t ent) {
    Scr_WaitTillString(ent, pointer<const char>(0x0083abfc));  // "reached_end_node"
    if (!noteworthy_is(&ent, 0x007f60b4)) {  // "noturrets"
        if (second)
            BroScr_DeleteVehicleTurrets_3(ent);
        else
            BroScr_DeleteVehicleTurrets_2(ent);
    }
    BroScr_DeleteUndroppedBombs(ent);
    DynArray_Resize(reinterpret_cast<uint8_t*>(key_array(ent, 0x0030b1c2)), 0, 0);
    if (health(ent) <= 0 && *int_slot(ent, 0x4af51227)) {
        if (second)
            BroScr_PlaneCrashSound_2(ent);
        else
            BroScr_PlaneCrashSound(ent);
        slot<Ent>(0x00922540)(ent);  // GScr_Delete
        return;
    }
    if (health(ent) > 0) {
        slot<Ent>(0x00922540)(ent);  // GScr_Delete
        return;
    }
    if (noteworthy_is(&ent, 0x00824d24)) {  // "loop"
        slot<void XB_CDECL(uint32_t, uint32_t*)>(0x0092287c)(ent, ScrField_path(ent));  // GScr_Vehicle_AttachPath
        slot<Ent>(0x00922880)(ent);  // GScr_Vehicle_StartPath
        return;
    }
    slot<Ent>(0x0092289c)(ent);  // GScr_Vehicle_Kill
}

}  // namespace

void XB_CDECL BroScr_VehicleFollowPathToEnd(uint32_t ent) { follow_path_to_end<false>(ent); }
void XB_CDECL BroScr_VehicleFollowPathToEnd_2(uint32_t ent) { follow_path_to_end<true>(ent); }

void XB_CDECL BroScr_RespawnerCountDeath(uint32_t ent, uint32_t respawner, uint32_t guy) {
    using Start = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*);
    slot<EntHash>(0x00922188)(ent, 0x9767cf68);  // Scr_EndOn
    uint32_t attacker = 0;
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t*)>(0x004b1590)(guy, kDeath, &attacker);  // Scr_WaitTillOutputEnt_ORAN
    if (entity_handle_is_valid(respawner)) {
        ++*int_slot(respawner, 0x846e35f4);
        title_function<Start>(0x0045a6d0)(0, 0, 0, 0, own_address(&BroScr_UpdateAliveDeadCount), respawner, &ent);  // Scr_StartThread_ent_ent_PROLOGUE(BroScr_UpdateAliveDeadCount)
        slot<EntHash>(0x009221a4)(respawner, 0x67979261);  // Scr_QueueNotify
    } else {
        const uint32_t n = slot<uint32_t XB_CDECL(uint32_t)>(0x00922b88)(ent);  // ScrEnt count
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922b8c)(ent, n + 1);
    }
}

void XB_CDECL BroScr_FollowLagThink(uint32_t ent) {
    slot<EntHash>(0x00922188)(ent, 0xcc07a20a);  // Scr_EndOn
    *bool_slot(ent, 0xd57e151d) = 1;
    bool defined;
    if (!*ScrField_lag_distance_IsDefined(&defined, ent)) *int_slot(ent, 0x96f9883f) = 0;  // lag_distance
    // While it and its path (handle key 0xd952b5cf) are valid: the current path position (int key
    // 0x9aec9c1a) updated, notify 0x14559f8c on a change; 0.25 s steps.
    for (;;) {
        if (entity_handle_is_valid(ent)) {
            ScriptKeyTable* const table = own_function<ScriptKeyTable* XB_CDECL(uint32_t)>(&ScrCall_GScr_GetEntScriptObject)(ent);
            if (table) {
                const uint32_t* const path = ScriptKeyTable_Find(table, 0xd952b5cf);
                if (entity_handle_is_valid(path ? *path : 0)) {
                    uint32_t buffer;
                    const uint32_t current = *BroScr_FollowUpdateCurrentPos(&buffer, ent);
                    if (current != *int_slot(ent, 0x9aec9c1a)) {
                        *int_slot(ent, 0x9aec9c1a) = current;
                        queue_notify(ent, 0x14559f8c);
                    }
                }
            }
        }
        Scr_Wait(0.25f);
    }
}

void XB_CDECL BroScr_FollowEntInit(uint32_t leader, uint32_t follower) {
    using SetGoalPos = void XB_CDECL(uint32_t, const uint32_t*);
    slot<EntHash>(0x00922188)(follower, kDeath);     // Scr_EndOn
    slot<EntHash>(0x00922188)(follower, 0x49c98989);  // Scr_EndOn
    slot<EntHash>(0x00922188)(leader, 0xb93f38bf);    // Scr_EndOn
    slot<EntHash>(0x00922188)(leader, 0xcc07a20a);    // Scr_EndOn
    slot<EntHash>(0x00922df4)(follower, 0);           // ScrSentient_SetFloat_18
    // On each leader position change (0x14559f8c): the follower's point (BroScr_FollowPathOffsetPoint of
    // the last index) and its goal: the leader's path position (int key 0x9aec9c1a) plus key 0x5cd3d8b8
    // minus the follower's key 0x185cac5b, plus twice key 0xea4a313a / lag_distance while that is below
    // 0x5cd3d8b8; goal the second (bool key 0x217d7784) or first vector of the leader's path entry. At the
    // path's last entry: notifies 0xcc07a20a and 0xa3122ec6, a BroScr_RemoveAllFollowers thread.
    int32_t index = static_cast<int32_t>(undefined_int());
    for (;;) {
        Scr_WaitTill(leader, 0x14559f8c);
        if (static_cast<uint32_t>(index) != undefined_int()) BroScr_FollowPathOffsetPoint(leader, index);
        const int32_t spacing = static_cast<int32_t>(*int_slot(leader, 0xea4a313a));
        const int32_t lag = spacing / static_cast<int32_t>(*int_slot(leader, 0x96f9883f)) * 2;
        const uint32_t* const offset = int_slot(leader, 0x5cd3d8b8);
        if (static_cast<int32_t>(*int_slot(follower, 0x185cac5b)) < static_cast<int32_t>(*offset)) {
            const uint32_t* const n = int_slot(leader, 0x5cd3d8b8);
            const int32_t current = static_cast<int32_t>(*int_slot(leader, 0x9aec9c1a));
            index = static_cast<int32_t>(*n) + (current + lag) - static_cast<int32_t>(*int_slot(follower, 0x185cac5b));
        } else {
            const int32_t current = static_cast<int32_t>(*int_slot(leader, 0x9aec9c1a));
            const int32_t own = static_cast<int32_t>(*int_slot(follower, 0x185cac5b));
            index = static_cast<int32_t>(*int_slot(leader, 0x5cd3d8b8)) - own + current;
        }
        if (index >= 0) {
            const uint32_t i = static_cast<uint32_t>(index);
            if (*bool_slot(leader, 0x217d7784)) {
                DynArray* const path = pointer<DynArray>(path_positions(leader));
                if (i >= path->size) DynArray_Resize24(path, i + 1, i + 4);
                slot<SetGoalPos>(0x00922684)(follower, path->data + i * 6 + 3);  // GScr_SetGoalPos
            } else {
                DynArray* const path = pointer<DynArray>(path_positions(leader));
                if (i >= path->size) DynArray_Resize24(path, i + 1, i + 4);
                slot<SetGoalPos>(0x00922684)(follower, path->data + i * 6);  // GScr_SetGoalPos
            }
        }
        if (index == static_cast<int32_t>(pointer<DynArray>(path_positions(leader))->size - 1)) {
            queue_notify(leader, 0xcc07a20a);
            queue_notify(leader, 0xa3122ec6);
            own_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(&Scr_StartThreadFunctor1_entry)(
                0, 0, 0, 0, own_address(&BroScr_RemoveAllFollowers), leader);  // Scr_StartThreadFunctor1(BroScr_RemoveAllFollowers)
        }
    }
}

void XB_CDECL BroScr_VehicleDeadStop(uint32_t ent) {
    using SetSpeed = void XB_CDECL(uint32_t, uint32_t, uint32_t);
    using Find = uint32_t* XB_CDECL(uint32_t*, uint32_t, DynArray*);
    Scr_EndOnString(ent, pointer<const char>(0x00824d98));  // "deadstop"
    bool defined;
    if (!*ScrField_hc904bb71_IsDefined(&defined, ent) || *int_slot(ent, 0xc904bb71) == 0)
        Scr_EndOnString(ent, pointer<const char>(0x0083abfc));  // "reached_end_node"
    // Paused while int key 0x147beb00 is defined and 0 (1 s steps); a blocker from the level's vehicle
    // list (+0x4e0) stops it (accel 25 for kind 1, 10000 for kind 2, 0.1 s steps) until it is gone, then
    // GScr_Vehicle_SetSpeedMode2(5); 0.2 s steps.
    for (;;) {
        for (;;) {
            if (!entity_handle_is_valid(ent)) break;
            ScriptKeyTable* const table = own_function<ScriptKeyTable* XB_CDECL(uint32_t)>(&ScrCall_GScr_GetEntScriptObject)(ent);
            if (!table) break;
            const uint32_t* const value = ScriptKeyTable_Find(table, 0x147beb00);
            const uint32_t undefined = undefined_int();
            if ((value ? *value : undefined) == undefined) break;
            if (*int_slot(ent, 0x147beb00) != 0) break;
            Scr_Wait(1.0f);
        }
        uint32_t result;
        const uint32_t blocker =
            *own_function<Find>(&BroScr_VehicleFindBlocker)(&result, ent, pointer<DynArray>(title_global<uint32_t>(0x00922ee0) + 0x4e0));  // BroScr_VehicleFindBlocker
        if (blocker) {
            if (blocker == 1) {
                slot<SetSpeed>(0x00922890)(ent, 0, 0x41c80000);  // GScr_Vehicle_SetSpeed(0, 25)
                do {
                    slot<SetSpeed>(0x00922890)(ent, 0, 0x41c80000);  // GScr_Vehicle_SetSpeed(0, 25)
                    Scr_Wait(float_bits(0x3dcccccd));                // 0.1
                } while (*own_function<Find>(&BroScr_VehicleFindBlocker)(&result, ent, pointer<DynArray>(title_global<uint32_t>(0x00922ee0) + 0x4e0)) == 1);
                slot<void XB_CDECL(uint32_t, float)>(0x00922894)(ent, 5.0f);  // GScr_Vehicle_SetSpeedMode2
            } else if (blocker == 2) {
                slot<SetSpeed>(0x00922890)(ent, 0, 0x461c4000);  // GScr_Vehicle_SetSpeed(0, 10000)
                do {
                    slot<SetSpeed>(0x00922890)(ent, 0, 0x461c4000);  // GScr_Vehicle_SetSpeed(0, 10000)
                    Scr_Wait(float_bits(0x3dcccccd));                // 0.1
                } while (*own_function<Find>(&BroScr_VehicleFindBlocker)(&result, ent, pointer<DynArray>(title_global<uint32_t>(0x00922ee0) + 0x4e0)) == 2);
                slot<void XB_CDECL(uint32_t, float)>(0x00922894)(ent, 5.0f);  // GScr_Vehicle_SetSpeedMode2
            }
        }
        Scr_Wait(float_bits(0x3e4ccccd));  // 0.2
    }
}

namespace {

// BroScr_StunnedWait(ent) (the title passes the undefined entity 0x00922e84 as a third, unread word).
inline bool stunned_wait(uint32_t ent) {
    bool result;
    return *own_function<bool* XB_CDECL(bool*, uint32_t, uint32_t)>(&BroScr_StunnedWait)(&result, ent, title_global<uint32_t>(0x00922e84));
}

// The first entry of the target queue (array key 0x4ced49f6, set empty when missing), grown to one.
inline uint32_t first_target(uint32_t ent) {
    using SetArray = uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t, DynArray*);
    ScriptKeyTable* const table = ent_script_object(ent);
    uint32_t* value = ScriptKeyTable_Find(table, 0x4ced49f6);
    if (!value) {
        DynArray empty = {nullptr, 0, 0};
        value = own_function<SetArray>(&ScriptKeyTable_SetArray)(table, 0x4ced49f6, &empty);  // ScriptKeyTable_SetArray
    }
    DynArray* const queue = pointer<DynArray>(*value);
    if (!queue->size) DynArray_Resize(reinterpret_cast<uint8_t*>(queue), 1, 4);
    return queue->data[0];
}

// GScr_Vehicle_SetTurretTargetEnt(ent, the first target, offset).
inline void target_first(uint32_t ent, const float* offset) {
    const uint32_t target = first_target(ent);
    slot<void XB_CDECL(uint32_t, uint32_t, const float*)>(0x009228cc)(ent, target, offset);
}

// Waits `step` while GScr_GetTime is before `end` and BroScr_StunnedWait holds.
inline void wait_while_stunned(float end, float step, uint32_t ent) {
    while (end > static_cast<float>(get_time()) && stunned_wait(ent)) Scr_Wait(step);
}

}  // namespace

void XB_CDECL BroScr_TankHullGunnerThink(uint32_t ent, ScriptString weapon, ScriptString tag) {
    using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, ScriptString*, ScriptString*);
    using RandomFloat = long double XB_CDECL(float);
    own_function<StartThread>(&Scr_StartThread_ent_str_str_PROLOGUE_entry)(0, 0, 0, 0, own_address(&BroScr_TankAttachMgTurret), ent, &weapon, &tag);  // Scr_StartThread_ent_str_str(BroScr_TankAttachMgTurret)
    slot<EntHash>(0x00922188)(ent, kDeath);  // Scr_EndOn
    bool defined;
    if (*ScrField_script_team_IsDefined(&defined, ent) &&
        ScriptString_Equals(ScrField_script_team(ent), pointer<const char>(0x0083208c))) {  // "axis"
        DynArray* const queue = key_array(ent, 0x4ced49f6);
        if (!queue->size) DynArray_Resize(reinterpret_cast<uint8_t*>(queue), 1, 4);
        queue->data[0] = *pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) + 0x6ec);
    }
    if (*ScrField_triggeredthink_IsDefined(&defined, ent)) Scr_WaitTill(ent, 0x9a652fcd);
    // Bursts at the queue's first target: aimed 64 up with a random spread scaled by distance and
    // script_accuracy, a BroScr_TurretVisTargetWait thread until it sees the target (int key 0x4d917ba7),
    // then fire (0x2f9d0580 / 0xac1c7bac notifies) for the level's burst time (+0x77c s) and 1 s, then
    // 1.5-3.5 s aimed without spread; two bursts, then the target is dropped (when not stunned) or
    // 0x4d7beb69 notified (no queue, or no key 0xde065107); 0.3 s steps.
    int32_t bursts = 0;
    for (;;) {
        bool next = false;
        if (*BroScr_RotateQueue_h4ced49f6(reinterpret_cast<uint8_t*>(&defined), ent) && stunned_wait(ent)) {
            float offset[3] = {0.0f, 0.0f, 0.0f};
            if (*ScrField_script_accuracy_IsDefined(&defined, ent)) {
                uint32_t buffer[3];
                const uint32_t* o = slot<GetVec>(0x00922b20)(buffer, first_target(ent));  // ScrEnt_GetOrigin
                const uint32_t to[3] = {o[0], o[1], o[2]};
                o = slot<GetVec>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
                const uint32_t from[3] = {o[0], o[1], o[2]};
                const float d = static_cast<float>(
                    slot<long double XB_CDECL(const uint32_t*, const uint32_t*)>(0x00922268)(from, to));  // Scr_Distance
                const float spread = *reinterpret_cast<const float*>(ScrField_script_accuracy(ent)) * (d * float_bits(0x3951b717));  // 0.0002
                const float size = static_cast<float>(slot<long double XB_CDECL(float)>(0x00922260)(spread));  // Scr_Abs
                float sign = title_global<float>(0x00922eac);
                if (spread != 0.0f) sign = spread / size;
                const float c0 = (size * float_bits(0x3e19999a) +
                                  static_cast<float>(slot<RandomFloat>(0x00922230)(size * float_bits(0x3f59999a)))) * sign;  // 0.15, 0.85
                const float c1 = (size * float_bits(0x3e19999a) +
                                  static_cast<float>(slot<RandomFloat>(0x00922230)(size * float_bits(0x3f59999a)))) * sign;
                const float c2 = (size * float_bits(0x3e19999a) +
                                  static_cast<float>(slot<RandomFloat>(0x00922230)(size * float_bits(0x3f59999a)))) * sign;
                offset[0] = c0;
                offset[1] = c1;
                offset[2] = c2;
            }
            const float aim[3] = {offset[0], offset[1], offset[2] + 64.0f};
            target_first(ent, aim);
            *int_slot(ent, 0x4d917ba7) = 0;
            uint32_t* const functor = new_functor(0xc);
            if (functor) {
                functor[0] = 0x007f5ba0;
                functor[1] = own_address(&BroScr_TurretVisTargetWait);  // BroScr_TurretVisTargetWait
                functor[2] = ent;
            }
            create_thread(ent, functor);
            while (stunned_wait(ent) && *int_slot(ent, 0x4d917ba7) == 0) Scr_Wait(float_bits(0x3e4ccccd));  // 0.2
            queue_notify(ent, 0x2f9d0580);
            *int_slot(ent, 0x4d917ba7) = 0;
            if (stunned_wait(ent)) {
                slot<Ent>(0x009228d0)(ent);  // GScr_Vehicle_ClearTurretTarget
                const uint32_t burst = *pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) + 0x77c);
                if (burst != undefined_int()) {
                    const int32_t ms = static_cast<int32_t>(burst) * 1000;
                    wait_while_stunned(static_cast<float>(ms + get_time()), float_bits(0x3e4ccccd), ent);  // 0.2
                }
                if (stunned_wait(ent)) {
                    queue_notify(ent, 0xac1c7bac);
                    wait_while_stunned(static_cast<float>(get_time() + 1000), 0.0f, ent);
                    if (stunned_wait(ent)) {
                        const float plain[3] = {0.0f, 0.0f, 64.0f};
                        target_first(ent, plain);
                        const int32_t pause = random_int(2000) + 1500;
                        const int32_t until = get_time() + pause;
                        const float end = static_cast<uint32_t>(until) == undefined_int() ? title_global<float>(0x00922eac)
                                                                                         : static_cast<float>(until);
                        wait_while_stunned(end, float_bits(0x3e99999a), ent);  // 0.3
                        next = ++bursts <= 1;
                    }
                }
            }
        }
        if (next) continue;
        bursts = 0;
        if (*ScrField_h4ced49f6_IsDefined(&defined, ent)) {
            if (!stunned_wait(ent)) {
                own_function<uint32_t* XB_CDECL(uint32_t, uint32_t)>(&ScrField_h4ced49f6_PopFront)(ent, first_target(ent));  // ScrField_h4ced49f6_PopFront
                Scr_Wait(float_bits(0x3e99999a));  // 0.3
                continue;
            }
            if (entity_handle_is_valid(ent)) {
                ScriptKeyTable* const table = own_function<ScriptKeyTable* XB_CDECL(uint32_t)>(&ScrCall_GScr_GetEntScriptObject)(ent);
                if (table) {
                    const uint32_t* const value = ScriptKeyTable_Find(table, 0xde065107);
                    const uint32_t undefined = undefined_int();
                    if ((value ? *value : undefined) != undefined) {
                        Scr_Wait(float_bits(0x3e99999a));  // 0.3
                        continue;
                    }
                }
            }
        }
        queue_notify(ent, 0x4d7beb69);
        Scr_Wait(float_bits(0x3e99999a));  // 0.3
    }
}

namespace {

// The rider's entry point: the vehicle's tag `text` origin (GScr_GetTagOrigin into undefined lanes).
inline void entry_tag_origin(uint32_t vehicle, uint32_t text, uint32_t* goal) {
    ScriptString tag;
    ScriptString_FromCString(&tag, pointer<const char>(text));
    const uint32_t lane = undefined_lane();
    uint32_t o[3] = {lane, lane, lane};
    slot<void XB_CDECL(uint32_t, uint32_t*, ScriptString*)>(0x0092261c)(vehicle, o, &tag);  // GScr_GetTagOrigin
    goal[0] = o[0];
    goal[1] = o[1];
    goal[2] = o[2];
    ScriptString_Release(&tag);
}

// The flakvierling loader's mount anim (anim global `anim`) from the rider's origin, angled as the
// vehicle's tag `text` (ScrCall_GScr_GetTagAngles).
inline void loader_mount_anim(uint32_t* vehicle, uint32_t* rider, uint32_t anim, uint32_t text) {
    using Anim = void XB_CDECL(uint32_t, uint32_t, const uint32_t*, const uint32_t*, uint32_t);
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922748)(*rider, title_global<uint32_t>(anim));  // ScrActor_SetInt_1370
    ScriptString tag;
    ScriptString_FromCString(&tag, pointer<const char>(text));
    const uint32_t mount = title_global<uint32_t>(anim);
    uint32_t a[3], o[3];
    const uint32_t* const angles =
        ScrCall_GScr_GetTagAngles(a, vehicle, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&tag)));
    const uint32_t* const origin = ScrCall_ScrEnt_GetOrigin(rider, o);
    slot<Anim>(0x009224c0)(*rider, 0x54e3b2b6, origin, angles, mount);  // GScr_AnimScriptedNoNotify
    ScriptString_Release(&tag);
}

}  // namespace

void XB_CDECL BroScr_VehicleMountRiders(uint32_t vehicle, uint32_t rider, uint32_t seat, ScriptString tag, bool skip_goal) {
    using SetGoal = void XB_CDECL(uint32_t, const uint32_t*);
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t)>(0x009223a0)(rider, 1, 1, 1);  // GScr_SetAIFlags
    slot<EntHash>(0x00922df4)(rider, 0x40800000);  // ScrSentient_SetFloat_18(4)
    slot<EntHash>(0x00922cfc)(rider, 0x3f800000);  // ScrActor_SetFloat_7a4(1)
    slot<EntHash>(0x00922188)(rider, kDeath);      // Scr_EndOn
    slot<EntHash>(0x00922188)(rider, 0x548b3b43);  // Scr_EndOn
    if (!skip_goal) {
        slot<SetGoal>(0x00922688)(rider, key_slot(vehicle, 0x6dcb7c56, 0));  // GScr_SetGoalPosFromNode
        Scr_WaitTill(rider, 0x0032304c);
    }
    ++*int_slot(vehicle, 0xb330b301);  // riders getting on / off
    *bool_slot(rider, 0x2cdbaad4) = 1;
    title_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*)>(0x004b09e0)(
        0, 0, 0, 0, own_address(&BroScr_VehicleRiderDeathWatch), vehicle, &rider);  // Scr_CallThread_ent_ent_ORAN(BroScr_VehicleRiderDeathWatch)
    // To the seat's entry tag (none for other tags: the undefined goal), the gun put away.
    const uint32_t lane = undefined_lane();
    uint32_t goal[3] = {lane, lane, lane};
    if (ScriptString_Equals(&tag, pointer<const char>(0x007fa47c))) {  // "tag_gunner"
        entry_tag_origin(vehicle, 0x00824dd0, goal);                    // "tag_entry_gunner"
    } else if (ScriptString_Equals(&tag, pointer<const char>(0x007fc0bc))) {  // "tag_passenger1"
        entry_tag_origin(vehicle, 0x00824dbc, goal);                           // "tag_entry_loaderL"
    } else if (ScriptString_Equals(&tag, pointer<const char>(0x007f5c08))) {  // "tag_passenger2"
        ScriptString entry;
        ScriptString_FromCString(&entry, pointer<const char>(0x00824da8));  // "Tag_entry_loaderR"
        uint32_t o[3];
        const uint32_t* const at =
            ScrCall_GScr_GetTagOrigin(o, &vehicle, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&entry)));
        goal[0] = at[0];
        goal[1] = at[1];
        goal[2] = at[2];
        ScriptString_Release(&entry);
    }
    slot<SetGoal>(0x00922684)(rider, goal);  // GScr_SetGoalPos
    Scr_WaitTill(rider, 0x0032304c);
    BroScr_PutGunInHand(rider, 0x0036b1a5);
    // The mount anim (unless script_noteworthy "no_mount_anim"), waited for.
    if (!noteworthy_is(&rider, 0x007f69b8)) {
        if (ScriptString_Equals(&tag, pointer<const char>(0x007fa47c))) {  // "tag_gunner"
            using Anim = void XB_CDECL(uint32_t, uint32_t, const uint32_t*, const uint32_t*, uint32_t);
            slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922748)(rider, title_global<uint32_t>(0x008cd1f4));  // ScrActor_SetInt_1370(GunnerMount)
            ScriptString entry;
            ScriptString_FromCString(&entry, pointer<const char>(0x00824dd0));  // "tag_entry_gunner"
            const uint32_t lane_2 = undefined_lane();
            uint32_t a[3] = {lane_2, lane_2, lane_2};
            slot<void XB_CDECL(uint32_t, uint32_t*, ScriptString*)>(0x00922620)(vehicle, a, &entry);  // GScr_GetTagAngles
            const uint32_t angles[3] = {a[0], a[1], a[2]};
            uint32_t buffer[3];
            const uint32_t* const o = slot<GetVec>(0x00922b20)(buffer, rider);  // ScrEnt_GetOrigin
            const uint32_t origin[3] = {o[0], o[1], o[2]};
            slot<Anim>(0x009224c0)(rider, 0x54e3b2b6, origin, angles, title_global<uint32_t>(0x008cd1f4));  // GScr_AnimScriptedNoNotify
            ScriptString_Release(&entry);
        } else if (ScriptString_Equals(&tag, pointer<const char>(0x007fc0bc))) {  // "tag_passenger1"
            loader_mount_anim(&vehicle, &rider, 0x008cd218, 0x00824dbc);         // LoaderMountL, "tag_entry_loaderL"
        } else if (ScriptString_Equals(&tag, pointer<const char>(0x007f5c08))) {  // "tag_passenger2"
            loader_mount_anim(&vehicle, &rider, 0x008cd224, 0x00824da8);         // LoaderMountR, "Tag_entry_loaderR"
        }
        Scr_WaitTill(rider, 0x54e3b2b6);
    }
    --*int_slot(vehicle, 0xb330b301);
    *bool_slot(rider, 0x2cdbaad4) = 0;
    *reinterpret_cast<uint8_t*>(rider_slot(vehicle, seat) + 1) = 1;
    queue_notify(rider, 0x9966dcdf);
    {
        const uint32_t origin_offset[3] = {bits(0.0f), bits(0.0f), bits(0.0f)};
        const uint32_t angles_offset[3] = {bits(0.0f), bits(0.0f), bits(0.0f)};
        using Link = void XB_CDECL(uint32_t, uint32_t, const ScriptString*, const uint32_t*, const uint32_t*, uint32_t);
        slot<Link>(0x009224f4)(rider, vehicle, &tag, origin_offset, angles_offset, 0);  // GScr_LinkToTagWithOffset
    }
    {
        ScriptString copy;
        ScriptString_Copy(&copy, &tag);
        BroScr_SetRiderAnimForTag(rider, copy);  // releases its copy
    }
    BroScr_PutGunInHand(rider, 0x0036b1a5);
    uint32_t* const functor = new_functor(0x10);
    if (functor) {
        functor[0] = 0x007f4a28;
        functor[1] = own_address(&BroScr_VehicleRiderUnlinkOnDeath);  // BroScr_VehicleRiderUnlinkOnDeath
        functor[2] = vehicle;
        functor[3] = rider;
    }
    create_thread(vehicle, functor);
    using Start = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*, const ScriptString*);
    own_function<Start>(&Scr_StartThread_ent_ent_str_PROLOGUE_entry)(0, 0, 0, 0, own_address(&BroScr_VehicleRiderWait), vehicle, &rider, &tag);  // Scr_StartThread_ent_ent_str(BroScr_VehicleRiderWait)
    ScriptString_Release(&tag);
}

void XB_CDECL BroScr_PanzerIVSetup_3(uint32_t ent) {
    {
        ScriptString tree;
        ScriptString_FromCString(&tree, pointer<const char>(0x007f499c));  // "panzerIV"
        slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009225f0)(ent, &tree);  // GScr_UseAnimTree
        ScriptString_Release(&tree);
    }
    slot<EntHash>(0x00922b94)(ent, static_cast<uint32_t>(random_int(1000) + 500));  // ScrEnt_SetHealth
    uint32_t* functor = new_functor(0xc);
    if (functor) {
        functor[0] = 0x007f5ba0;
        functor[1] = own_address(&BroScr_VehicleHealthThink);  // BroScr_VehicleHealthThink
        functor[2] = ent;
    }
    create_thread(ent, functor);
    BroScr_VehicleInit_PanzerIV(ent);
    functor = new_functor(0xc);
    if (functor) {
        functor[0] = 0x007f5ba0;
        functor[1] = own_address(&BroScr_VehicleTurretFireLoop_4);  // BroScr_VehicleTurretFireLoop_4
        functor[2] = ent;
    }
    create_thread(ent, functor);
    thread_2(ent, own_address(&BroScr_TankStunOnDamage), ent, 1);  // BroScr_TankStunOnDamage
}

namespace {

// Scr_WaitTill(level, hash of the entity's targetname + `suffix`).
inline void wait_targetname_notify(uint32_t* ent, uint32_t suffix) {
    const uint32_t object = title_global<uint32_t>(0x00922ee0);
    const uint32_t* const level = pointer<uint32_t>(object ? object + 4 : 0);
    ScriptString name;
    ScriptString text;
    ScriptString_ConcatCString(&text, ScrCall_ScrEnt_GetTargetname(ent, &name), pointer<const char>(suffix));
    uint32_t hash;
    const uint32_t h = *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&text));
    Scr_WaitTill(*level, h);
    ScriptString_Release(&text);
    ScriptString_Release(&name);
}

// The follower array (key 0xdeca7f50, set empty when missing).
inline DynArray* follower_array(uint32_t ent) {
    using SetArray = uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t, DynArray*);
    ScriptKeyTable* const table = ent_script_object(ent);
    uint32_t* value = ScriptKeyTable_Find(table, 0xdeca7f50);
    if (!value) {
        DynArray empty = {nullptr, 0, 0};
        value = own_function<SetArray>(&ScriptKeyTable_SetArray)(table, 0xdeca7f50, &empty);  // ScriptKeyTable_SetArray
    }
    return pointer<DynArray>(*value);
}

// Follower i (the array grown to i + 1).
inline const uint32_t* follower_at(uint32_t ent, uint32_t i) {
    DynArray* const array = follower_array(ent);
    if (i >= array->size) DynArray_Resize(reinterpret_cast<uint8_t*>(array), i + 1, i + 4);
    return array->data + i;
}

}  // namespace

void XB_CDECL BroScr_PauseUnpauseThink(uint32_t ent) {
    slot<EntHash>(0x00922188)(ent, 0xcc07a20a);  // Scr_EndOn
    // On the level's "<targetname>_pause": paused (bool key 0x262b2a4b), notify 0xb93f38bf (ends the
    // followers' threads); on "<targetname>_unpause": unpaused, BroScr_PauseUnpauseWait and
    // BroScr_FollowEntInit threads again for each follower, notify 0x14559f8c.
    for (;;) {
        wait_targetname_notify(&ent, 0x00824e9c);  // "_pause"
        *bool_slot(ent, 0x262b2a4b) = 1;
        queue_notify(ent, 0xb93f38bf);
        wait_targetname_notify(&ent, 0x00824e90);  // "_unpause"
        *bool_slot(ent, 0x262b2a4b) = 0;
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(follower_array(ent)->size); ++i) {
            const uint32_t* follower = follower_at(ent, i);
            uint32_t* functor = new_functor(0x10);
            if (functor) {
                functor[0] = 0x007f4a28;
                functor[1] = own_address(&BroScr_PauseUnpauseWait);  // BroScr_PauseUnpauseWait
                functor[2] = ent;
                functor[3] = *follower;
            }
            create_thread(ent, functor);
            follower = follower_at(ent, i);
            functor = new_functor(0x10);
            if (functor) {
                functor[0] = 0x007f4a28;
                functor[1] = own_address(&BroScr_FollowEntInit);  // BroScr_FollowEntInit
                functor[2] = ent;
                functor[3] = *follower;
            }
            create_thread(ent, functor);
        }
        queue_notify(ent, 0x14559f8c);
    }
}

void XB_CDECL BroScr_RespawnerThink(uint32_t ent, uint32_t respawner, RespawnerAttributes attribs, uint32_t limit,
                                    ScriptString mode) {
    using Count = uint32_t XB_CDECL(uint32_t);
    using SetCount = void XB_CDECL(uint32_t, uint32_t);
    using Spawn = uint32_t XB_CDECL(uint32_t, uint32_t);
    using RandomFloat = long double XB_CDECL(float);
    *bool_slot(ent, 0xaa3cddde) = 1;  // the first spawn (no delay)
    *bool_slot(ent, 0x18b0887b) = 0;
    // The spawn zone: the spawner's zone when the level uses zones (+0x188 defined and set), else 0.
    uint32_t zone;
    const uint32_t zones = *pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) + 0x188);
    if (zones != undefined_int() && zones) {
        uint32_t buffer[3];
        const uint32_t* const o = slot<GetVec>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
        zone = slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x0092249c)(o[0], o[1], o[2]);  // GScr_GetZoneAtPoint
    } else {
        zone = 0;
    }
    if (entity_handle_is_valid(respawner)) {
        slot<EntHash>(0x00922188)(respawner, 0x0c34930d);  // Scr_EndOn
        *int_slot(respawner, 0x846e35f4) = 0;  // dead count
        *int_slot(respawner, 0xaa7e54b7) = 0;  // spawned count
    }
    slot<EntHash>(0x00922188)(ent, 0x0c34930d);  // Scr_EndOn
    *bool_slot(ent, 0x788065eb) = 1;  // respawning
    bool defined;
    if (entity_handle_is_valid(respawner)) {
        if (limit != undefined_int() && !*ScrField_h5f01d656_IsDefined(&defined, respawner)) *ScrField_h5f01d656(respawner) = limit;
    } else if (limit != undefined_int()) {
        *int_slot(ent, 0x5f01d656) = limit;
    }
    if (slot<Count>(0x00922b88)(ent) == undefined_int()) slot<SetCount>(0x00922b8c)(ent, 1);
    // While respawning: a count reset (int key 0x78063481, notify 0x9767cf68) taken over; with a count
    // left, after the delay (first delay 0x6e62eedd once, else script_delay / delay_min..max except for
    // the first spawn) BroScr_StopRespawning, then a spawn (forced with key 0x02d62a20) that gets the
    // attributes, targetname (key 0x0c4d3fa0 or "<groupname>_ai"), a random target node chain and a
    // BroScr_RespawnerCountDeath thread; 0.5 s steps.
    for (;;) {
        if (entity_handle_is_valid(ent)) {
            ScriptKeyTable* const table = own_function<ScriptKeyTable* XB_CDECL(uint32_t)>(&ScrCall_GScr_GetEntScriptObject)(ent);
            if (table) {
                const uint32_t* const value = ScriptKeyTable_Find(table, 0x78063481);
                const uint32_t undefined = undefined_int();
                if ((value ? *value : undefined) != undefined) {
                    slot<SetCount>(0x00922b8c)(ent, *int_slot(ent, 0x78063481));
                    queue_notify(ent, 0x9767cf68);
                    *int_slot(ent, 0x78063481) = title_global<uint32_t>(0x00922ebc);
                }
            }
        }
        if (static_cast<int32_t>(slot<Count>(0x00922b88)(ent)) < 1) {
            Scr_Wait(1.0f);
            continue;
        }
        if (*ScrField_h6e62eedd_IsDefined(&defined, ent)) {
            Scr_Wait(*reinterpret_cast<const float*>(ScrField_h6e62eedd(ent)));
            const uint32_t undefined = undefined_float();
            *ScrField_h6e62eedd(ent) = undefined;
        } else if (!*bool_slot(ent, 0xaa3cddde)) {
            if (*ScrField_script_delay_IsDefined(&defined, ent)) {
                const float delay = *reinterpret_cast<const float*>(ScrField_script_delay(ent));
                const float* const base = reinterpret_cast<const float*>(ScrField_script_delay(ent));
                const long double r = slot<RandomFloat>(0x00922230)(delay);  // Scr_RandomFloat
                Scr_Wait(static_cast<float>(*base + r));
            } else if (*ScrField_script_delay_min_IsDefined(&defined, ent) &&
                       *ScrField_script_delay_max_IsDefined(&defined, ent)) {
                const float high = *reinterpret_cast<const float*>(ScrField_script_delay_max(ent));
                const float low = *reinterpret_cast<const float*>(ScrField_script_delay_min(ent));
                const float* const base = reinterpret_cast<const float*>(ScrField_script_delay_min(ent));
                const long double r = slot<RandomFloat>(0x00922230)(high - low);  // Scr_RandomFloat
                Scr_Wait(static_cast<float>(*base + r));
            }
        }
        BroScr_StopRespawning(ent, respawner);
        if (!*bool_slot(ent, 0x788065eb)) break;
        uint32_t guy;
        if (*ScrField_h02d62a20_IsDefined(&defined, ent) && *ScrField_h02d62a20(ent))
            guy = slot<Spawn>(0x00922520)(ent, zone);  // GScr_ForceSpawn
        else
            guy = slot<Spawn>(0x00922518)(ent, zone);  // GScr_DoSpawn
        if (entity_handle_is_valid(guy)) {
            *reinterpret_cast<uint8_t*>(ScrField_using_respawner(guy, 0)) = 1;
            *bool_slot(ent, 0xaa3cddde) = 0;
            Scr_WaitTillString(guy, pointer<const char>(0x007f6000));  // "finished spawning"
            if (entity_handle_is_valid(respawner)) ++*int_slot(respawner, 0xaa7e54b7);
            if (!*ScrField_h0c4d3fa0_IsDefined(&defined, guy)) {
                ScriptString group;
                ScriptString name;
                ScriptString_ConcatCString(&name, ScrCall_ScrEnt_GetGroupname(&ent, &group), pointer<const char>(0x00824ea4));  // "_ai"
                ScriptString copy;
                ScriptString_Copy(&copy, &name);
                slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922b54)(guy, copy);  // ScrEnt_SetTargetname (releases its copy)
                ScriptString_Release(&name);
                ScriptString_Release(&group);
            } else {
                ScriptString copy;
                ScriptString_Copy(&copy, ScrField_h0c4d3fa0(guy));
                slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922b54)(guy, copy);  // ScrEnt_SetTargetname (releases its copy)
            }
            own_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, RespawnerAttributes*)>(&Scr_StartThread_ent_RespawnerAttributes_entry)(
                0, 0, 0, 0, own_address(&BroScr_RespawnerApplyAttribs), guy, &attribs);  // Scr_StartThread_ent_RespawnerAttributes(BroScr_RespawnerApplyAttribs)
            bool targeted;
            {
                ScriptString target;
                targeted = ScrCall_ScrEnt_GetTarget(&ent, &target)->rep != nullptr;
                ScriptString_Release(&target);
            }
            if (targeted) {
                DynArray nodes = {nullptr, 0, 0};
                {
                    ScriptString key;
                    ScriptString_FromCString(&key, pointer<const char>(0x0082dda8));  // "targetname"
                    ScriptString target;
                    Scr_GetNodeArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ScrCall_ScrEnt_GetTarget(&ent, &target))),
                                     static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&key)), reinterpret_cast<uint32_t*>(&nodes));
                    ScriptString_Release(&target);
                    ScriptString_Release(&key);
                }
                if (static_cast<int32_t>(nodes.size) > 0) {
                    const uint32_t i = static_cast<uint32_t>(random_int(static_cast<int32_t>(nodes.size)));
                    title_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                                     const ScriptString*)>(0x004b0ae0)(
                        0, 0, 0, 0, own_address(&BroScr_FollowNodeChain), guy, element_at(&nodes, i, kReserveInt0), &mode);  // Scr_StartThread_ent_pathnode_str_ORAN(BroScr_FollowNodeChain)
                }
                if (nodes.data) Scr_Free(nodes.data);
            }
            uint32_t* const functor = new_functor(0x14);
            if (functor) {
                functor[0] = 0x007f4a10;
                functor[1] = own_address(&BroScr_RespawnerCountDeath);  // BroScr_RespawnerCountDeath
                functor[2] = ent;
                functor[3] = respawner;
                functor[4] = guy;
            }
            create_thread(ent, functor);
            *bool_slot(ent, 0x18b0887b) = 0;
        }
        Scr_Wait(0.5f);
    }
    title_function<void XB_THISCALL(RespawnerAttributes*)>(0x0052b970)(&attribs);  // RespawnerAttributes_Dtor
    ScriptString_Release(&mode);
}

void XB_CDECL BroScr_TankHullGunnerAtGunhull(uint32_t ent, ScriptString weapon) {
    ScriptString tag;
    ScriptString_FromCString(&tag, pointer<const char>(0x007f77f4));  // "tag_gunhull"
    ScriptString copy;
    ScriptString_Copy(&copy, &weapon);
    BroScr_TankHullGunnerThink(ent, copy, tag);  // does not return
}

void XB_CDECL BroScr_AddFollowers(uint32_t leader, uint32_t follower) {
    if (!pointer<DynArray>(path_positions(leader))->size) return;
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(followers(leader)->size); ++i) {
        DynArray* const list = followers(leader);
        if (i >= list->size) DynArray_Resize(reinterpret_cast<uint8_t*>(list), i + 1, i + 4);
        if (list->data[i] == follower) return;
    }
    const uint32_t count = followers(leader)->size;
    *int_slot(follower, 0x185cac5b) = count;  // its place in the line
    DynArray_PushBack(reinterpret_cast<uint8_t*>(followers(leader)), &follower);
    bool defined;
    if (!*ScrField_hd57e151d_IsDefined(&defined, leader)) {
        // The leader's first follower: its pause and lag threads.
        uint32_t* functor = new_functor(0xc);
        if (functor) {
            functor[0] = 0x007f5ba0;
            functor[1] = own_address(&BroScr_PauseUnpauseThink);  // BroScr_PauseUnpauseThink
            functor[2] = leader;
        }
        create_thread(leader, functor);
        functor = new_functor(0xc);
        if (functor) {
            functor[0] = 0x007f5ba0;
            functor[1] = own_address(&BroScr_FollowLagThink);  // BroScr_FollowLagThink
            functor[2] = leader;
        }
        create_thread(leader, functor);
        Scr_Wait(float_bits(0x3d4ccccd));  // 0.05
    }
    uint32_t* functor = new_functor(0x10);
    if (functor) {
        functor[0] = 0x007f4a28;
        functor[1] = own_address(&BroScr_RemoveFollowerOnDeath);  // BroScr_RemoveFollowerOnDeath
        functor[2] = leader;
        functor[3] = follower;
    }
    create_thread(leader, functor);
    if (*ScrField_is_paused_IsDefined(&defined, leader) && *bool_slot(leader, 0x262b2a4b)) return;
    functor = new_functor(0x10);
    if (functor) {
        functor[0] = 0x007f4a28;
        functor[1] = own_address(&BroScr_FollowEntInit);  // BroScr_FollowEntInit
        functor[2] = leader;
        functor[3] = follower;
    }
    create_thread(leader, functor);
}

namespace {

inline bool is_alive(uint32_t ent) { return slot<uint32_t XB_CDECL(uint32_t)>(0x00922210)(ent) != 0; }  // GScr_IsAlive

// Whether rider slot i holds a valid, living rider.
inline bool rider_alive(uint32_t vehicle, uint32_t i) {
    return entity_handle_is_valid(rider_slot(vehicle, i)[0]) && is_alive(rider_slot(vehicle, i)[0]);
}

// Rider slot i emptied: {0, the undefined bool word (stale upper bytes: 0 here)}.
inline void rider_slot_clear(uint32_t vehicle, uint32_t i, uint32_t flag) {
    uint32_t* const entry = rider_slot(vehicle, i);
    entry[0] = 0;
    entry[1] = flag;
}

}  // namespace

void XB_CDECL BroScr_FlakGunnerRespawnLoop(uint32_t vehicle) {
    slot<EntHash>(0x00922188)(vehicle, kDeath);     // Scr_EndOn
    slot<EntHash>(0x00922188)(vehicle, 0xca4175b9);  // Scr_EndOn
    slot<EntHash>(0x00922188)(vehicle, 0xbe7ade77);  // Scr_EndOn
    // Every frame while all three rider slots hold living riders (int 0x7e8 set on them); then, without
    // a gunner (slot 0), a seated loader that is not boarding is sent up (0xa0e65a8e, waits 0x996a3647),
    // else a living loader is taken out of its slot (0x548b3b43) and respawned as the gunner
    // (BroScr_VehicleSpawnRider seat 0); notify 0x08d272e5 without one; 1 s steps.
    for (;;) {
        Scr_Wait(1.0f);
        bool all = true;
        do {
            Scr_WaitFrames(0);
            for (uint32_t i = 0; static_cast<int32_t>(i) < 3; ++i) {
                if (rider_alive(vehicle, i)) {
                    slot<EntHash>(0x00922d14)(rider_slot(vehicle, i)[0], 1);  // ScrActor_SetInt_7e8
                } else {
                    rider_slot_clear(vehicle, i, title_global<uint8_t>(0x00922ed0));
                    all = false;
                }
            }
        } while (all);
        if (rider_alive(vehicle, 0)) {
            queue_notify(vehicle, 0x08d272e5);
            continue;
        }
        bool next = false;
        for (uint32_t i = 1; static_cast<int32_t>(i) <= 2; ++i) {
            if (!rider_alive(vehicle, i) || !*reinterpret_cast<const uint8_t*>(rider_slot(vehicle, i) + 1) ||
                *bool_slot(rider_slot(vehicle, i)[0], 0x2cdbaad4))
                continue;
            const uint32_t rider = rider_slot(vehicle, i)[0];
            queue_notify(rider, 0xa0e65a8e);
            Scr_WaitTill(vehicle, 0x996a3647);
            if (is_alive(rider)) {
                next = true;
                break;
            }
        }
        if (next) continue;
        for (uint32_t i = 1; static_cast<int32_t>(i) <= 2; ++i) {
            if (!rider_alive(vehicle, i) || *bool_slot(rider_slot(vehicle, i)[0], 0x2cdbaad4)) continue;
            const uint32_t rider = rider_slot(vehicle, i)[0];
            queue_notify(rider, 0x548b3b43);
            Scr_WaitFrames(0);
            rider_slot_clear(vehicle, i, title_global<uint8_t>(0x00922ed0));
            if (is_alive(rider)) {
                BroScr_VehicleSpawnRider(vehicle, rider, 0, false);
                next = true;
                break;
            }
        }
        if (next) continue;
        queue_notify(vehicle, 0x08d272e5);
    }
}

void XB_CDECL BroScr_SpawnerGroupManager(uint32_t, ScriptString group, ScriptString name, RespawnerAttributes attribs,
                                         uint32_t limit, ScriptString start_notify, ScriptString mode) {
    using StartRespawner = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                             const RespawnerAttributes*, const uint32_t*, const ScriptString*);
    // The group's spawners (groupname == `group`).
    DynArray spawners = {nullptr, 0, 0};
    slot<void XB_CDECL(DynArray*)>(0x009223d8)(&spawners);  // GScr_GetSpawnerArray
    DynArray list = {nullptr, 0, 0};
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(spawners.size); ++i) {
        bool match = false;
        ScriptString first;
        if (ScrCall_ScrEnt_GetGroupname(element_at(&spawners, i, own_address(&DynArray_Resize)), &first)->rep) {
            ScriptString again;
            match = ScriptString_EqualsString(ScrCall_ScrEnt_GetGroupname(element_at(&spawners, i, own_address(&DynArray_Resize)), &again), &group);
            ScriptString_Release(&again);
        }
        ScriptString_Release(&first);
        if (match) DynArray_PushBack(reinterpret_cast<uint8_t*>(&list), element_at(&spawners, i, own_address(&DynArray_Resize)));
    }
    if (start_notify.rep) {
        const uint32_t object = title_global<uint32_t>(0x00922ee0);
        const uint32_t* const level = pointer<uint32_t>(object ? object + 4 : 0);
        uint32_t hash;
        const uint32_t h = *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&start_notify));
        Scr_WaitTill(*level, h);
    }
    // A named group gets a manager script_origin (groupname "<group>_manager", string key 0xdfe2186c =
    // group, int key 0xa8ce4655 = spawner count, array 0xc548bcb7 = the spawners, string key 0x22cb4cdd
    // = name); every spawner a BroScr_RespawnerThink thread with it.
    uint32_t manager = 0;
    if (name.rep) {
        {
            ScriptString kind;
            ScriptString_FromCString(&kind, pointer<const char>(0x0083ab88));  // "script_origin"
            const uint32_t origin[3] = {bits(0.0f), bits(0.0f), bits(0.0f)};
            manager = slot<Spawn>(0x009222f0)(&kind, origin, 0);  // GScr_Spawn
            ScriptString_Release(&kind);
        }
        {
            ScriptString text;
            ScriptString_ConcatCString(&text, &group, pointer<const char>(0x00824304));  // "_manager"
            ScriptString copy;
            ScriptString_Copy(&copy, &text);
            slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922c1c)(manager, copy);  // ScrEnt_SetGroupname (releases its copy)
            ScriptString_Release(&text);
        }
        ScriptString_Assign(title_function<GetString>(0x00458da0)(ent_script_object(manager), 0xdfe2186c), &group);  // ScriptKeyTable_GetString
        *bool_slot(manager, 0x18b0887b) = 0;
        slot<EntHash>(0x00922188)(manager, 0x0c34930d);  // Scr_EndOn
        *int_slot(manager, 0xa8ce4655) = list.size;
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(*int_slot(manager, 0xa8ce4655)); ++i) {
            const uint32_t* const spawner = element_at(&list, i, own_address(&DynArray_Resize));
            using SetArray = uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t, DynArray*);
            ScriptKeyTable* const table = ent_script_object(manager);
            uint32_t* value = ScriptKeyTable_Find(table, 0xc548bcb7);
            if (!value) {
                DynArray empty = {nullptr, 0, 0};
                value = own_function<SetArray>(&ScriptKeyTable_SetArray)(table, 0xc548bcb7, &empty);  // ScriptKeyTable_SetArray
            }
            DynArray_PushBack(pointer<uint8_t>(*value), spawner);
        }
        {
            ScriptKeyTable* const table = ent_script_object(manager);
            ScriptString* text = reinterpret_cast<ScriptString*>(ScriptKeyTable_Find(table, 0x22cb4cdd));
            if (!text) {
                ScriptString empty;
                ScriptString_FromRep(&empty, nullptr);
                ScriptString copy;
                ScriptString_Copy(&copy, &empty);
                text = reinterpret_cast<ScriptString*>(
                    ScriptKeyTable_Set(table, 0x22cb4cdd, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(copy.rep))));
                ScriptString_Release(&empty);
            }
            ScriptString_Assign(text, &name);
        }
    }
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(list.size); ++i)
        own_function<StartRespawner>(&Scr_StartThread_ent_ent_RespawnerAttributes_i_str_entry)(0, 0, 0, 0, own_address(&BroScr_RespawnerThink), *element_at(&list, i, own_address(&DynArray_Resize)), &manager, &attribs,
                                                   &limit, &mode);  // Scr_StartThread_ent_ent_RespawnerAttributes_i_str(BroScr_RespawnerThink)
    if (list.data) Scr_Free(list.data);
    if (spawners.data) Scr_Free(spawners.data);
    ScriptString_Release(&group);
    ScriptString_Release(&name);
    title_function<void XB_THISCALL(RespawnerAttributes*)>(0x0052b970)(&attribs);  // RespawnerAttributes_Dtor
    ScriptString_Release(&start_notify);
    ScriptString_Release(&mode);
}

namespace {

// The default spawner-group attributes (BroScr_SpawnerGroupParams_Init) / a copy (RespawnerAttributes_CopyCtor).
inline void attribs_default(RespawnerAttributes* a) { BroScr_SpawnerGroupParams_Init(reinterpret_cast<uint8_t*>(a)); }
inline void attribs_copy(RespawnerAttributes* out, const RespawnerAttributes* a) {
    title_function<void XB_THISCALL(RespawnerAttributes*, const RespawnerAttributes*)>(0x0052ba50)(out, a);
}
inline void attribs_free(RespawnerAttributes* a) {
    title_function<void XB_THISCALL(RespawnerAttributes*)>(0x0052b970)(a);  // RespawnerAttributes_Dtor
}

// BroScr_SpawnerGroupManager(0, copies of group / name, a copy of *a, limit, copies of notify / mode),
// the arguments built last to first as the title pushes them.
inline void group_manager(uint32_t ent, const ScriptString* group, const ScriptString* name, const RespawnerAttributes* a,
                          uint32_t limit, const ScriptString* notify, const ScriptString* mode) {
    ScriptString mode_copy, notify_copy, name_copy, group_copy;
    ScriptString_Copy(&mode_copy, mode);
    ScriptString_Copy(&notify_copy, notify);
    RespawnerAttributes copy;
    attribs_copy(&copy, a);
    ScriptString_Copy(&name_copy, name);
    ScriptString_Copy(&group_copy, group);
    BroScr_SpawnerGroupManager(ent, group_copy, name_copy, copy, limit, notify_copy, mode_copy);
}

inline const ScriptString* empty_string() { return pointer<const ScriptString>(0x00922e90); }

}  // namespace

void XB_CDECL BroScr_SpawnerGroupManagerCall(uint32_t ent, ScriptString group) {
    RespawnerAttributes a;
    attribs_default(&a);
    group_manager(ent, &group, empty_string(), &a, title_global<uint32_t>(0x00922ebc), empty_string(), empty_string());
    attribs_free(&a);
    ScriptString_Release(&group);
}

void XB_CDECL BroScr_SpawnerGroupManagerCall_2(uint32_t ent, ScriptString group, uint32_t limit) {
    RespawnerAttributes a;
    attribs_default(&a);
    group_manager(ent, &group, empty_string(), &a, limit, empty_string(), empty_string());
    attribs_free(&a);
    ScriptString_Release(&group);
}

void XB_CDECL BroScr_SpawnerGroupManagerCall_3(uint32_t ent, ScriptString group, ScriptString name, uint32_t limit,
                                               ScriptString notify, ScriptString mode) {
    RespawnerAttributes a;
    attribs_default(&a);
    group_manager(ent, &group, &name, &a, limit, &notify, &mode);
    attribs_free(&a);
    ScriptString_Release(&group);
    ScriptString_Release(&name);
    ScriptString_Release(&notify);
    ScriptString_Release(&mode);
}

void XB_CDECL BroScr_SpawnerGroupManagerCall_4(uint32_t ent, ScriptString group, RespawnerAttributes attribs) {
    group_manager(ent, &group, empty_string(), &attribs, title_global<uint32_t>(0x00922ebc), empty_string(), empty_string());
    ScriptString_Release(&group);
    attribs_free(&attribs);
}

void XB_CDECL BroScr_RunSpawnerGroupsByGroupname(DynArray names, float delay, ScriptString notify, RespawnerAttributes attribs,
                                                 uint32_t limit) {
    // Without names: nothing. Without a delay or notify: the names freed. Else a BroScr_SpawnerGroupManager
    // per name that matches a spawner's groupname (the spawner test compares spawner j with name j),
    // each followed by `delay` or the level notify `notify`; empty names wait 0.05 s.
    if (!names.capacity) {
        free_strings(&names);
    } else if (delay == title_global<float>(0x00922eac) && !notify.rep) {
        title_function<void XB_THISCALL(DynArray*)>(0x0045c9c0)(&names);  // ScrStringArray_Free
    } else {
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(names.size); ++i) {
            if (!*element_at(&names, i, kReserveString)) {
                Scr_Wait(float_bits(0x3d4ccccd));  // 0.05
                continue;
            }
            DynArray spawners = {nullptr, 0, 0};
            slot<void XB_CDECL(DynArray*)>(0x009223d8)(&spawners);  // GScr_GetSpawnerArray
            DynArray list = {nullptr, 0, 0};
            for (uint32_t j = 0; static_cast<int32_t>(j) < static_cast<int32_t>(spawners.size); ++j) {
                if (!*element_at(&names, j, kReserveString)) continue;
                bool match = false;
                ScriptString first;
                if (ScrCall_ScrEnt_GetGroupname(element_at(&spawners, j, own_address(&DynArray_Resize)), &first)->rep) {
                    ScriptString again;
                    const ScriptString* const group = ScrCall_ScrEnt_GetGroupname(element_at(&spawners, j, own_address(&DynArray_Resize)), &again);
                    match = ScriptString_EqualsString(group, reinterpret_cast<const ScriptString*>(element_at(&names, j, kReserveString)));
                    ScriptString_Release(&again);
                }
                ScriptString_Release(&first);
                if (match) DynArray_PushBack(reinterpret_cast<uint8_t*>(&list), element_at(&spawners, j, own_address(&DynArray_Resize)));
            }
            if (static_cast<int32_t>(list.size) < 1) {
                if (list.data) Scr_Free(list.data);
                if (spawners.data) Scr_Free(spawners.data);
                continue;
            }
            const uint32_t object = title_global<uint32_t>(0x00922ee0);
            const uint32_t* const level = pointer<uint32_t>(object ? object + 4 : 0);
            {
                ScriptString mode_copy, notify_copy, name_copy, group_copy;
                ScriptString_Copy(&mode_copy, empty_string());
                ScriptString_Copy(&notify_copy, empty_string());
                RespawnerAttributes copy;
                attribs_copy(&copy, &attribs);
                ScriptString_Copy(&name_copy, empty_string());
                ScriptString_Copy(&group_copy, reinterpret_cast<const ScriptString*>(element_at(&names, i, kReserveString)));
                BroScr_SpawnerGroupManager(*level, group_copy, name_copy, copy, limit, notify_copy, mode_copy);
            }
            if (delay != title_global<float>(0x00922eac)) {
                Scr_Wait(delay);
            } else if (notify.rep) {
                const uint32_t object_2 = title_global<uint32_t>(0x00922ee0);
                Scr_WaitTillScriptString(*pointer<uint32_t>(object_2 ? object_2 + 4 : 0), &notify);
            }
            if (list.data) Scr_Free(list.data);
            if (spawners.data) Scr_Free(spawners.data);
        }
        free_strings(&names);
    }
    ScriptString_Release(&notify);
    attribs_free(&attribs);
}

void XB_CDECL BroScr_PlaneSpawnWingTurretsCall_3(uint32_t plane) {
    // As BroScr_PlaneSpawnWingTurretsCall / _2 (bro_scr_vehicle.cpp, which vc71match cannot compile).
    const uint32_t undefined = title_global<uint32_t>(0x00922ec0);  // g_scrUndefinedInt
    BroScr_PlaneSpawnWingTurrets(plane, undefined, undefined, true);
}

void XB_CDECL BroScr_AddFollowersDefault(uint32_t leader, uint32_t follower) {
    // BroScr_AddFollowers(leader, follower) (the title passes the undefined entity 0x00922e84 as a third, unread word).
    own_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(&BroScr_AddFollowers)(leader, follower, title_global<uint32_t>(0x00922e84));
}

namespace {

// A script condition stored as a float and tested against the 0.0 at 0x0083eafc.
inline bool script_true(bool condition) {
    return static_cast<float>(condition ? 1 : 0) != as_float(title_global<uint32_t>(0x0083eafc));
}

// The flak's target (entity key 0x0d60fb3d) is defined: the flak valid, its own key table (no shared
// empty one) and the key's handle (0 when missing) valid.
inline bool flak_target_defined(uint32_t flak) {
    if (!entity_handle_is_valid(flak)) return false;
    ScriptKeyTable* const table = own_function<ScriptKeyTable* XB_CDECL(uint32_t)>(&ScrCall_GScr_GetEntScriptObject)(flak);
    if (!table) return false;
    const uint32_t* const target = ScriptKeyTable_Find(table, 0x0d60fb3d);
    return entity_handle_is_valid(target ? *target : 0) != 0;
}

inline uint32_t* flak_target(uint32_t flak) { return key_slot(flak, 0x0d60fb3d, 0); }

// Scr_Distance(flak origin, target origin) (the target's origin read first).
inline float flak_target_distance(uint32_t flak) {
    ScrVec3 to, from;
    origin_of(&to, *flak_target(flak));
    origin_of(&from, flak);
    return static_cast<float>(distance(&from, &to));
}

// GScr_Vehicle_SetTurretTargetEnt(flak, target, BroScr_FlakPickLeadPos(flak, target)) (the target read
// again for the call).
inline void flak_aim_lead(uint32_t flak) {
    const uint32_t target = *flak_target(flak);
    float lead[3];
    const float* const offset = BroScr_FlakPickLeadPos(lead, flak, target);
    slot<void XB_CDECL(uint32_t, uint32_t, const float*)>(0x009228cc)(flak, *flak_target(flak), offset);
}

}  // namespace

void XB_CDECL BroScr_FlakAutomatedThink(uint32_t flak) {
    using ClearTarget = void XB_CDECL(uint32_t);
    slot<EntHash>(0x00922188)(flak, kDeath);     // Scr_EndOn
    slot<EntHash>(0x00922188)(flak, 0xca4175b9);  // Scr_EndOn
    slot<EntHash>(0x00922188)(flak, 0xbe7ade77);  // Scr_EndOn
    thread_1(flak, 0x007f5ba0, own_address(&BroScr_FlakGunnerRespawnLoop), flak);  // BroScr_FlakGunnerRespawnLoop
    float dist = as_float(undefined_float());     // the last target distance, kept across passes
    for (;;) {
        // Neither a gunner in slot 0 (with int key 0xb330b301 clear) nor `automated`: no target.
        const bool gunner = *reinterpret_cast<const uint8_t*>(rider_slot_inline(flak, 0) + 1) != 0;
        if ((!gunner || *int_slot(flak, 0xb330b301) != 0) && !*bool_slot(flak, 0xd8e13e71)) {
            slot<ClearTarget>(0x009228d0)(flak);  // GScr_Vehicle_ClearTurretTarget
            Scr_Wait(0.2f);
            continue;
        }
        bool group;
        if (*ScrField_targets_groupname_IsDefined(&group, flak)) {
            // A living target beyond flak_dist, or none: BroScr_FlakFindTargets picks a new one.
            if (flak_target_defined(flak) && is_alive(*flak_target(flak))) {
                dist = flak_target_distance(flak);
                if (script_true(dist > static_cast<float>(static_cast<int32_t>(*int_slot(flak, 0x93c3cf6c))))) {
                    uint32_t found;
                    const uint32_t* const next = BroScr_FlakFindTargets(&found, flak);
                    *ScrField_target_ent(flak) = *next;
                }
            } else {
                uint32_t found;
                const uint32_t* const next = BroScr_FlakFindTargets(&found, flak);
                *flak_target(flak) = *next;
            }
            if (flak_target_defined(flak)) {
                dist = flak_target_distance(flak);
                flak_aim_lead(flak);
            } else {
                slot<ClearTarget>(0x009228d0)(flak);  // GScr_Vehicle_ClearTurretTarget
            }
        } else if (flak_target_defined(flak)) {
            // A target given by the level: the lead aimed at, 64 higher for "convoy3".
            dist = flak_target_distance(flak);
            bool convoy;
            {
                ScriptString name;
                convoy = ScriptString_Equals(ScrCall_ScrEnt_GetTargetname(flak_target(flak), &name),
                                             pointer<const char>(0x007fa63c));  // "convoy3"
                ScriptString_Release(&name);
            }
            if (convoy) {
                const uint32_t target = *flak_target(flak);
                float lead[3];
                const float* const l = BroScr_FlakPickLeadPos(lead, flak, target);
                float offset[3];
                offset[2] = l[2] + as_float(title_global<uint32_t>(0x007e5bf0));  // + 64
                offset[1] = l[1];
                offset[0] = l[0];
                slot<void XB_CDECL(uint32_t, uint32_t, const float*)>(0x009228cc)(flak, *flak_target(flak),
                                                                                  offset);  // GScr_Vehicle_SetTurretTargetEnt
            } else {
                flak_aim_lead(flak);
            }
        }
        // In reach (BroScr_FlakCanShootLow; the title passes the target as a third, unread word) and
        // nearer than flak_dist: fire (notify 0xac1c7bac) when the "tag_flash" trace over flak_dist
        // runs past the target, or with can_shoot_low.
        if (flak_target_defined(flak)) {
            const uint32_t target = *flak_target(flak);
            uint8_t low;
            if (*own_function<uint8_t* XB_CDECL(uint8_t*, uint32_t, uint32_t)>(&BroScr_FlakCanShootLow)(&low, flak, target) &&
                script_true(static_cast<float>(static_cast<int32_t>(*int_slot(flak, 0x93c3cf6c))) > dist)) {
                ScrVec3 origin;
                {
                    ScriptString tag;
                    ScriptString_FromCString(&tag, pointer<const char>(0x008389c0));  // "tag_flash"
                    ScrVec3 at;
                    tag_origin(&at, flak, &tag);
                    origin = at;
                    ScriptString_Release(&tag);
                }
                ScrVec3 angles;
                {
                    ScriptString tag;
                    ScriptString_FromCString(&tag, pointer<const char>(0x008389c0));  // "tag_flash"
                    const uint32_t lane = undefined_lane();
                    ScrVec3 at;
                    at.v[0] = lane;
                    at.v[1] = lane;
                    at.v[2] = lane;
                    slot<void XB_CDECL(uint32_t, ScrVec3*, ScriptString*)>(0x00922620)(flak, &at, &tag);  // GScr_GetTagAngles
                    angles = at;
                    ScriptString_Release(&tag);
                }
                float forward[3];
                {
                    const uint32_t lane = undefined_lane();
                    uint32_t out[3] = {lane, lane, lane};
                    slot<void XB_CDECL(uint32_t*, const ScrVec3*)>(0x00922290)(out, &angles);  // Scr_AnglesToForward
                    std::memcpy(forward, out, 12);
                }
                const float range = static_cast<float>(static_cast<int32_t>(*int_slot(flak, 0x93c3cf6c)));
                float o[3];
                std::memcpy(o, origin.v, 12);
                const float end[3] = {o[0] + range * forward[0], o[1] + range * forward[1], o[2] + range * forward[2]};
                struct Trace {
                    float fraction;  // written by the trace
                    uint32_t position[3];
                    uint32_t zero;
                    uint32_t normal[3];
                    ScriptString surface;
                } trace;
                const uint32_t lane = undefined_lane();
                trace.position[0] = lane;
                trace.position[1] = lane;
                trace.position[2] = lane;
                trace.zero = 0;
                trace.normal[0] = lane;
                trace.normal[1] = lane;
                trace.normal[2] = lane;
                ScriptString_FromRep(&trace.surface, nullptr);
                slot<void XB_CDECL(Trace*, const float*, const float*, uint32_t, uint32_t)>(0x00922414)(
                    &trace, o, end, 0, flak);  // GScr_LocationalTraceSurface
                const long double reach =
                    slot<long double XB_CDECL(const uint32_t*, const float*)>(0x00922268)(trace.position, o);  // Scr_Distance
                if (script_true(reach > dist) || *bool_slot(flak, 0x185dadca)) queue_notify(flak, 0xac1c7bac);
                ScriptString_Release(&trace.surface);
            }
        }
        Scr_Wait(0.2f);
    }
}

void XB_CDECL BroScr_SetFlakManned(uint32_t flak, bool manned) {
    queue_notify(flak, 0xca4175b9);
    if (manned) {
        Scr_WaitFrames(0);
        own_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(&Scr_StartThreadFunctor1_entry)(
            0, 0, 0, 0, own_address(&BroScr_FlakAutomatedThink), flak);  // Scr_StartThreadFunctor1(BroScr_FlakAutomatedThink)
    } else {
        BroScr_ReleaseGunners(flak);
    }
}

void XB_CDECL BroScr_SetFlakAutomated(uint32_t flak, bool automated) {
    if (automated) {
        *reinterpret_cast<uint8_t*>(ScrField_automated(flak, 0)) = 1;
        queue_notify(flak, 0xca4175b9);
        Scr_WaitFrames(0);
        own_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(&Scr_StartThreadFunctor1_entry)(
            0, 0, 0, 0, own_address(&BroScr_FlakAutomatedThink), flak);  // Scr_StartThreadFunctor1(BroScr_FlakAutomatedThink)
    } else {
        *reinterpret_cast<uint8_t*>(ScrField_automated(flak, 0)) = 0;
        queue_notify(flak, 0xbe7ade77);
    }
}

void XB_CDECL BroScr_SetFlakMannedByName(ScriptString name, bool manned) {
    uint32_t flak;
    {
        ScriptString copy;
        ScriptString_Copy(&copy, &name);
        BroScr_FindLevelVehicleByTargetname(&flak, copy);
    }
    if (entity_handle_is_valid(flak)) BroScr_SetFlakManned(flak, manned);
    ScriptString_Release(&name);
}

void XB_CDECL BroScr_SetFlakAutomatedByName(ScriptString name, bool automated) {
    uint32_t flak;
    {
        ScriptString copy;
        ScriptString_Copy(&copy, &name);
        BroScr_FindLevelVehicleByTargetname(&flak, copy);
    }
    if (entity_handle_is_valid(flak)) BroScr_SetFlakAutomated(flak, automated);
    ScriptString_Release(&name);
}

}  // namespace game
