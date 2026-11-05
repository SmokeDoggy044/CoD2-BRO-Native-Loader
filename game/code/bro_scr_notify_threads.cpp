#include "bro_scr_notify_threads.h"

#include "bro_scr_actor.h"
#include "bro_scr_ai_common.h"
#include "bro_scr_common.h"
#include "g_scr_runtime.h"
#include "scr_field.h"
#include "title_data.h"

namespace game {

using namespace bro_ai;

namespace {

constexpr uint32_t kDeath = 0x06730ae8;  // "death"

void end_on(uint32_t ent, uint32_t hash) { slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, hash); }  // Scr_EndOn
void wait_till(uint32_t ent, uint32_t hash) { Scr_WaitTill(ent, hash); }

// The level script object (0x00922ef8) and its handle (word +4; read through null without one).
uint8_t* level_object() { return title_global<uint8_t*>(0x00922ef8); }
uint32_t level_handle() {
    const uint32_t object = title_global<uint32_t>(0x00922ef8);
    return *pointer<uint32_t>(object ? object + 4 : 0);
}
uint32_t player() { return title_global<uint32_t>(0x009236dc); }

uint32_t hash_name(const char* name) {
    uint32_t hash;
    return *Scr_HashName(&hash, name);
}

inline void delete_if_valid(uint32_t ent) {
    if (entity_handle_is_valid(ent)) slot<void XB_CDECL(uint32_t)>(0x00922540)(ent);  // GScr_Delete
}

void turret_fire(uint32_t turret) { slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922580)(turret, 0); }  // GScr_TurretCall272
void delete_ent(uint32_t ent) { slot<void XB_CDECL(uint32_t)>(0x00922540)(ent); }                      // GScr_Delete

using SetAnim7 = void XB_CDECL(uint32_t, uint32_t, uint32_t, float, float, float, uint32_t);
using SetAnim8 = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, float, float, float, uint32_t);

// The turret idle knob (0x008cf684): weight 1, time 0.2, rate 1, restart.
void turret_idle_knob(uint32_t turret) {
    slot<void XB_CDECL(uint32_t, uint32_t, float, float, float, uint32_t)>(0x009225d8)(
        turret, title_global<uint32_t>(0x008cf684), 1.0f, 0.2f, 1.0f, 1);  // GScr_SetAnimKnob
}

inline void cover_crouch_idle(uint32_t actor, uint32_t anim) {
    slot<SetAnim8>(0x009225e4)(actor, 0xde61361c, anim, title_global<uint32_t>(0x008cd7dc), 1.0f, 0.1f, 0.0f,
                               1);  // GScr_SetFlaggedAnimKnobAll(.., g_anim_Body, ..)
    Scr_Wait(1.0f);
}

// Forever: fire, wait 0.06 s, the "mg42_shoot" sound, wait 0.06 s.
[[noreturn]] inline void mg42_shoot_loop(uint32_t turret) {
    for (;;) {
        turret_fire(turret);
        Scr_Wait(0.06f);
        ScriptString sound;
        scr_string_from(&sound, 0x00813a54);  // "mg42_shoot"
        slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t)>(0x009221dc)(turret, &sound, 0, 0,
                                                                                               0);  // GScr_PlaySound
        scr_release(&sound);
        Scr_Wait(0.06f);
    }
}

inline void notify_on_notify(uint32_t ent, uint32_t wait, uint32_t notify, uint32_t end) {
    end_on(ent, 0x07b4e7f7);
    end_on(ent, kDeath);
    end_on(ent, end);
    wait_till(ent, wait);
    queue_notify(ent, notify);
}
inline void notify_after_time(uint32_t ent, float time, uint32_t notify, uint32_t end) {
    end_on(ent, 0x07b4e7f7);
    end_on(ent, kDeath);
    end_on(ent, end);
    Scr_Wait(time);
    queue_notify(ent, notify);
}

void stand_anim_for_time(uint32_t actor, float time, uint32_t stand, uint32_t crouch) {
    const bool standing = static_cast<uint8_t>(
        slot<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00922714)(actor, 0x070b89a4));  // GScr_ActorIsStanceAllowed
    slot<SetAnim7>(0x009225c8)(actor, title_global<uint32_t>(standing ? stand : crouch),
                               title_global<uint32_t>(0x008ce9ac), 1.0f, 0.1f, 1.0f, 0);  // GScr_SetAnimKnobAll(.., look_straight)
    Scr_Wait(time);
}

}  // namespace

void XB_CDECL BroScr_SetGoalRadius64UntilNotify(uint32_t ai) {
    end_on(ai, kDeath);
    const float radius = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922df0)(ai));  // ScrSentient_GetFloat_18
    using SetFloat = void XB_CDECL(uint32_t, float);
    slot<SetFloat>(0x00922df4)(ai, 64.0f);  // ScrSentient_SetFloat_18
    wait_till(level_handle(), 0x011066c4);
    slot<SetFloat>(0x00922df4)(ai, radius);
}

void XB_CDECL BroScr_NotifyWhenOtherDies(uint32_t ent, uint32_t other) {
    end_on(ent, 0x455955a2);
    wait_till(other, kDeath);
    queue_notify(ent, 0x455955a2);
}

void XB_CDECL BroScr_TrackTriggerTouch(uint32_t trigger, ScriptString name) {
    using IsTouching = uint8_t XB_CDECL(uint32_t, uint32_t);
    for (;;) {
        *reinterpret_cast<uint32_t*>(level_object() + 0x190) = 0;
        wait_till(trigger, 0xe2a210cf);  // "trigger"
        ScriptString_Assign(reinterpret_cast<ScriptString*>(level_object() + 0x1c8), &name);
        if (!slot<IsTouching>(0x00922530)(player(), trigger)) continue;  // GScr_IsTouching
        do {
            *reinterpret_cast<uint32_t*>(level_object() + 0x190) = trigger;
            Scr_Wait(1.0f);
        } while (slot<IsTouching>(0x00922530)(player(), trigger));
    }
}

void XB_CDECL BroScr_TurretAutoAiOnNotify(uint32_t ent, uint32_t turret) {
    uint32_t origin[3];
    slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(origin, ent);  // ScrEnt_GetOrigin (unused)
    wait_till(ent, 0xad76f051);
    queue_notify(turret, 0xcaa6bec3);
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922584)(turret, 0xad76f051);  // GScr_Turret_SetMode
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922590)(turret, player());  // GScr_Turret_SetTargetEntity
}

void XB_CDECL BroScr_Mg42ShootSoundLoop(uint32_t turret) {
    end_on(turret, 0xeb04625e);
    mg42_shoot_loop(turret);
}

void XB_CDECL BroScr_TurretStateChangeWait(uint32_t turret, float time) {
    if (0.0f >= time) return;  // comiss/jae: a NaN time goes on
    Scr_EndOnString(turret, pointer<const char>(0x0083a948));  // "turretstatechange"
    Scr_Wait(time);
    queue_notify(turret, hash_name(pointer<const char>(0x0083a948)));
}

void XB_CDECL BroScr_SetAIFlagsUntilNotify(uint32_t ai) {
    using SetFlags = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t);
    end_on(ai, kDeath);
    slot<SetFlags>(0x009223a0)(ai, 1, 1, 0);  // GScr_SetAIFlags
    wait_till(ai, 0x0032304c);
    slot<SetFlags>(0x009223a0)(ai, 0, 0, 0);
}

void XB_CDECL BroScr_NotifyFallbackReachedGoal(uint32_t, uint32_t ent, int32_t index) {
    wait_till(ent, kDeath);
    ScriptString name;
    scr_string_from(&name, 0x00825ac4);  // "fallback_reached_goal"
    ScriptString_AppendInt(&name, index);
    const uint32_t level = level_handle();
    uint32_t hash;
    queue_notify(level, *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&name)));
    scr_release(&name);
}

void XB_CDECL BroScr_WaitFrame() { Scr_Wait(0.05f); }

void XB_CDECL Bro__spawner_gmi__friendly_wave_callback(uint32_t trigger) {
    const char* const self = pointer<const char>(0x00825adc);  // "_spawner_gmi::friendly_wave_callback"
    slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x009221c0)(trigger, 0xe2a210cf, hash_name(self));  // GScr_GetEntVar
    queue_notify(level_handle(), 0xaf6b65f9);
    ScriptString name;
    const bool wave = ScriptString_Equals(ScrCall_ScrEnt_GetTargetname(&trigger, &name), pointer<const char>(0x0081351c));  // "friendly_wave"
    scr_release(&name);
    *reinterpret_cast<uint32_t*>(level_object() + 0x1d0) = wave ? trigger : title_global<uint32_t>(0x00922e84);
    Scr_Wait(1.0f);
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x009221bc)(trigger, 0xe2a210cf, hash_name(self));  // GScr_SetEntVar
}

void XB_CDECL BroScr_NotifyMg42FinishedOnDeath(uint32_t, uint32_t ai, uint32_t turret) {
    end_on(turret, 0x7b91738c);
    wait_till(ai, kDeath);
    queue_notify(turret, 0x7b91738c);
}

void XB_CDECL BroScr_UseAiOnMg42Hint(uint32_t user, uint32_t turret) {
    using SetByte = void XB_CDECL(uint32_t, uint32_t);
    using Hint = void XB_CDECL(uint32_t, ScriptString*);
    end_on(turret, 0x7b91738c);
    slot<SetByte>(0x00922d84)(user, 1);  // ScrActor_SetByte_1374
    ScriptString hint;
    scr_string_from(&hint, 0x00825b04);  // "CGAME_USEAIONMG42"
    slot<Hint>(0x009225b8)(user, &hint);  // GScr_ResetHintCache
    scr_release(&hint);
    Scr_WaitTillString(user, pointer<const char>(0x0083aa58));  // "trigger"
    slot<SetByte>(0x00922d84)(user, 0);
    ScriptString none;
    scr_string_from(&none, 0x0082dfeb);  // ""
    slot<Hint>(0x009225b8)(user, &none);
    scr_release(&none);
    slot<void XB_CDECL(uint32_t)>(0x00922730)(user);  // GScr_ActorLeaveTurret
    queue_notify(turret, 0x7b91738c);
}

void XB_CDECL BroScr_NotifyFinishedUsingMg42(uint32_t trigger, uint32_t ent) {
    const char* const name = pointer<const char>(0x00825b18);  // "friendly_finished_using_mg42"
    Scr_EndOnString(ent, name);
    wait_till(trigger, 0xe2a210cf);  // "trigger"
    queue_notify(ent, hash_name(name));
}

void XB_CDECL BroScr_DeleteOnNotify(uint32_t ent, ScriptString notify) {
    Scr_EndOnString(ent, pointer<const char>(0x00831f60));  // "death"
    Scr_WaitTillString(ent, notify.rep ? notify.rep->text : nullptr);
    delete_ent(ent);
    scr_release(&notify);
}

void XB_CDECL BroScr_ShellShockUntilDeath(uint32_t ent) {
    Scr_WaitTillString(ent, pointer<const char>(0x00831f60));  // "death"
    ScriptString cvar;
    const bool low = ScriptString_Equals(ScrCall_GScr_GetCvar(&cvar, 0x0083c3c8), pointer<const char>(0x00825b58));
    scr_release(&cvar);  // r_texturebits == "16"
    if (low) return;
    ScriptString type;
    scr_string_from(&type, 0x00831f60);  // "death"
    slot<void XB_CDECL(uint32_t, ScriptString*, float)>(0x00922624)(ent, &type, 3.0f);  // GScr_ShellShock
    scr_release(&type);
}

void XB_CDECL BroScr_PlaySoundOnTempOrigin_4(uint32_t, ScriptString sound, ScrVec3 origin) {
    ScriptString classname;
    scr_string_from(&classname, 0x0083ab88);  // "script_origin"
    const uint32_t ent =
        slot<uint32_t XB_CDECL(ScriptString*, const ScrVec3*, uint32_t)>(0x009222f0)(&classname, &origin, 0);  // GScr_Spawn
    scr_release(&classname);
    slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922b24)(ent, origin);  // ScrEnt_SetOrigin
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t)>(0x009221dc)(ent, &sound, 0x9a73c1c0, 0,
                                                                                           0);  // GScr_PlaySound
    wait_till(ent, 0x9a73c1c0);  // "sounddone"
    Scr_Wait(0.5f);
    delete_ent(ent);
    scr_release(&sound);
}

void XB_CDECL BroScr_PlayerFeelsImportant(uint32_t player_ent, uint32_t health) {
    Scr_EndOnString(player_ent, pointer<const char>(0x00831f60));       // "death"
    Scr_WaitTillString(player_ent, pointer<const char>(0x00825b5c));    // "stop player feels important"
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922b94)(player_ent, health);  // ScrEnt_SetHealth
}

void XB_CDECL BroScr_SoundBlendDebug(uint32_t ent, int32_t seconds) {
    ScriptString classname;
    scr_string_from(&classname, 0x0083abbc);  // "sound_blend"
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    const uint32_t blender =
        slot<uint32_t XB_CDECL(ScriptString*, const float*, uint32_t)>(0x009222f0)(&classname, zero, 0);  // GScr_Spawn
    scr_release(&classname);
    uint32_t buffer[3];
    const uint32_t* o = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
    const ScrVec3 origin = {{o[0], o[1], o[2]}};
    slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922b24)(blender, origin);  // ScrEnt_SetOrigin
    const uint32_t* a = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922ba0)(buffer, ent);  // ScrEnt_GetAngles
    const uint32_t angles[3] = {a[0], a[1], a[2]};
    uint32_t forward[3] = {undefined_lane(), undefined_lane(), undefined_lane()};
    slot<void XB_CDECL(uint32_t*, const uint32_t*)>(0x00922290)(forward, angles);  // Scr_AnglesToForward
    const int32_t end_ms = seconds * 1000;
    static_cast<void>(get_time() + end_ms);  // the end time, for the compiled-out debug draw
    for (;;) {
        ScriptString cvar;
        ScriptString_Equals(ScrCall_GScr_GetCvar(&cvar, 0x00825b78), pointer<const char>(0x0082e058));  // scr_debug_blender == "1"
        scr_release(&cvar);
        static_cast<void>(get_time());  // the time, likewise unused
        Scr_Wait(0.05f);
    }
}

void XB_CDECL BroScr_WaitForever_2() {
    for (;;) Scr_Wait(0.0f);
}

void XB_CDECL BroScr_MusicStopThenPlay(const ScriptString* track) {
    slot<void XB_CDECL(float)>(0x0092230c)(0.0f);  // GScr_MusicStop
    Scr_Wait(0.2f);
    slot<void XB_CDECL(const ScriptString*)>(0x00922308)(track);  // GScr_MusicPlay (tail jump)
}

void XB_CDECL BroScr_PlayLevelMusic(ScriptString track) {
    slot<void XB_CDECL(float)>(0x0092230c)(0.0f);  // GScr_MusicStop
    slot<void XB_CDECL(float)>(0x00922314)(1.0f);  // GScr_SetMusicTrack
    Scr_Wait(0.1f);
    slot<void XB_CDECL(const ScriptString*)>(0x00922308)(&track);  // GScr_MusicPlay
    *reinterpret_cast<uint32_t*>(level_object() + 0x150) = 0x2d7828fe;
    level_object()[0xcc] = 0;
    level_object()[0x68] = 1;
    scr_release(&track);
}

void XB_CDECL BroScr_TurretStateChangeAfterTime(uint32_t ai, float time, uint32_t turret) {
    if (0.0f >= time) return;
    end_on(ai, 0x07b4e7f7);
    end_on(turret, 0xeb04625e);
    Scr_Wait(time);
    queue_notify(turret, 0xeb04625e);
}

void XB_CDECL BroScr_TurretIdleAnimLoop(uint32_t ai, uint32_t turret) {
    end_on(ai, kDeath);
    end_on(ai, 0x07b4e7f7);
    end_on(turret, 0xeb04625e);
    end_on(turret, kDeath);
    turret_idle_knob(turret);
    for (;;) {
        turret_fire(turret);
        Scr_Wait(0.1f);
    }
}

void XB_CDECL BroScr_TurretIdleAnimLoop_2(uint32_t ai, uint32_t turret) {
    end_on(ai, kDeath);
    end_on(ai, 0x07b4e7f7);
    end_on(turret, 0xeb04625e);
    turret_idle_knob(turret);
    for (;;) {
        turret_fire(turret);
        Scr_Wait(0.1f);
    }
}

void XB_CDECL BroScr_RelayNotifyWhileBothAlive(uint32_t ent, uint32_t other) {
    end_on(ent, kDeath);
    end_on(other, kDeath);
    end_on(ent, 0x1e4aecac);
    wait_till(ent, 0x259b8110);
    queue_notify(ent, 0x1e4aecac);
}

void XB_CDECL BroScr_CornerPlayAnimForTime(uint32_t actor, float time, uint32_t anim, ScriptString name) {
    ScriptString unused;
    ScrCall_ScrActor_GetString_1384(&actor, &unused);
    slot<SetAnim7>(0x009225c8)(actor, anim, title_global<uint32_t>(0x008cd7dc), 1.0f, 0.1f, 1.0f,
                               1);  // GScr_SetAnimKnobAll(.., g_anim_Body, ..)
    queue_notify(actor, 0x281d9462);
    Scr_Wait(time);
    scr_release(&unused);
    scr_release(&name);
}

void XB_CDECL BroScr_CoverCrouchPlayIdle_1(uint32_t actor) { cover_crouch_idle(actor, title_global<uint32_t>(0x008ceec8)); }
void XB_CDECL BroScr_CoverCrouchPlayIdle_2(uint32_t actor) { cover_crouch_idle(actor, title_global<uint32_t>(0x008ceed4)); }

void XB_CDECL BroScr_StandAnimForTime(uint32_t actor, float time) {
    stand_anim_for_time(actor, time, 0x008ce424, 0x008ce3a0);  // corner stand / crouch pose, right
}
void XB_CDECL BroScr_StandAnimForTime_2(uint32_t actor, float time) {
    stand_anim_for_time(actor, time, 0x008ce418, 0x008ce394);  // left
}

void XB_CDECL BroScr_RandomTumble(uint32_t ent, ScrVec3 dir) {
    using RandomFloat = long double XB_CDECL(float);
    const float speed = static_cast<float>(slot<RandomFloat>(0x00922230)(200.0f) + title_global<float>(0x0083ebf0));  // + 150
    const float vx = speed * as_float(dir.v[0]);
    const float vy = speed * as_float(dir.v[1]);
    const float vz = static_cast<float>(slot<RandomFloat>(0x00922230)(200.0f) + title_global<float>(0x0083eb90));  // + 100
    float roll = static_cast<float>(slot<RandomFloat>(0x00922230)(2500.0f) * title_global<float>(0x0083ec9c) +
                                    title_global<float>(0x0080e5ec));  // * 3 + 3000
    if (vx > title_global<float>(0x0083eafc)) roll = static_cast<float>(roll * title_global<float>(0x0083eb34));  // 0, -1
    slot<void XB_CDECL(uint32_t, float, float, uint32_t, uint32_t)>(0x009227b4)(ent, roll, 5.0f, 0, 0);  // GScr_RotateRoll
    const float velocity[3] = {vx, vy, vz};
    slot<void XB_CDECL(uint32_t, const float*)>(0x00922638)(ent, velocity);  // GScr_MoveGravity
    Scr_Wait(10.0f);
    delete_ent(ent);
}

void XB_CDECL BroScr_WaitFrameStrArg(uint32_t, uint32_t, ScriptString unused) {
    Scr_WaitFrames(0);
    scr_release(&unused);
}

void XB_CDECL BroScr_NotifyOnNotify(uint32_t ent, uint32_t wait, uint32_t notify, uint32_t end) {
    notify_on_notify(ent, wait, notify, end);
}
void XB_CDECL BroScr_NotifyAfterTime(uint32_t ent, float time, uint32_t notify, uint32_t end) {
    notify_after_time(ent, time, notify, end);
}

// --- 0x00183930-0x0018c700 ---

void XB_CDECL BroScr_TurretStateChangeAfterTime_2(uint32_t ai, float time, uint32_t turret) {
    // The compiled test is inverted: the body runs when time is not above 0 (or NaN).
    if (time > 0.0f) return;
    end_on(ai, 0x07b4e7f7);
    end_on(turret, 0xeb04625e);
    Scr_WaitFrames(0);
    Scr_Wait(time);
    queue_notify(turret, 0xeb04625e);
}

void XB_CDECL BroScr_Mg42ShootLoop(uint32_t ai, uint32_t turret) {
    end_on(ai, 0x07b4e7f7);
    end_on(turret, 0xeb04625e);
    slot<void XB_CDECL(uint32_t, uint32_t, float, float, float, uint32_t)>(0x009225d8)(
        turret, title_global<uint32_t>(0x008cf7b0), 1.0f, 0.2f, 1.0f, 1);  // GScr_SetAnimKnob
    mg42_shoot_loop(turret);
}

void XB_CDECL BroScr_TurretStateChangeAfterTime_3(uint32_t ai, float time, uint32_t turret) {
    if (0.0f >= time) return;
    end_on(ai, 0x07b4e7f7);
    end_on(turret, 0xeb04625e);
    end_on(turret, kDeath);
    Scr_Wait(time);
    queue_notify(turret, 0xeb04625e);
}

void XB_CDECL BroScr_Mg42ShootLoop_2(uint32_t ai, uint32_t turret) {
    end_on(ai, kDeath);
    end_on(ai, 0x07b4e7f7);
    end_on(turret, 0xeb04625e);
    end_on(turret, kDeath);
    mg42_shoot_loop(turret);
}

void XB_CDECL BroScr_Mg42ShootLoop_3(uint32_t ai, uint32_t turret) {
    end_on(ai, kDeath);
    end_on(ai, 0x07b4e7f7);
    end_on(turret, 0xeb04625e);
    mg42_shoot_loop(turret);
}

void XB_CDECL BroScr_ActorAdvanceAnimAndTeleport(uint32_t actor, AnimCommand command) {
    slot<void XB_CDECL(uint32_t)>(0x00922760)(actor);  // GScr_ActorAdvanceAnimTime
    slot<void XB_CDECL(uint32_t, const uint32_t*, const uint32_t*)>(0x00922758)(actor, &command.w[7],
                                                                               &command.w[10]);  // GScr_ActorTeleport
    Scr_Wait(0.05f);
}

void XB_CDECL BroScr_ApplyAnimCommand(uint32_t actor, AnimCommand command) {
    using Handler = void XB_CDECL(uint32_t, AnimCommand);
    switch (command.w[0]) {
    case 1:
        slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009225dc)(
            actor, command.w[1], command.w[4], command.w[5], command.w[6], 0);  // GScr_SetAnim
        break;
    case 2:
        title_function<Handler>(0x00183d10)(actor, command);  // BroScr_AnimCmd_SetAnimKnobAll
        break;
    case 3:
        title_function<Handler>(0x00183d40)(actor, command);  // BroScr_AnimCmd_SetFlaggedAnimKnobAll
        break;
    case 4:
        BroScr_ActorAdvanceAnimAndTeleport(actor, command);
        break;
    default:
        break;
    }
}

void XB_CDECL BroScr_NotifyOnNotify_2(uint32_t ent, uint32_t wait, uint32_t notify, uint32_t end) {
    notify_on_notify(ent, wait, notify, end);
}
void XB_CDECL BroScr_NotifyAfterTime_2(uint32_t ent, float time, uint32_t notify, uint32_t end) {
    notify_after_time(ent, time, notify, end);
}

void XB_CDECL BroScr_MiscStepSounds(uint32_t ent, uint32_t end) {
    using PlaySound = void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t);
    end_on(ent, 0x07b4e7f7);
    end_on(ent, end);
    for (;;) {
        Scr_Wait(0.5f);
        ScriptString step1;
        scr_string_from(&step1, 0x00825b98);  // "misc_step1"
        slot<PlaySound>(0x009221dc)(ent, &step1, 0, 0, 0);  // GScr_PlaySound
        scr_release(&step1);
        Scr_Wait(0.5f);
        ScriptString step2;
        scr_string_from(&step2, 0x00825b8c);  // "misc_step2"
        slot<PlaySound>(0x009221dc)(ent, &step2, 0, 0, 0);
        scr_release(&step2);
    }
}

void XB_CDECL BroScr_WaitKillAnimScript(uint32_t ent) {
    end_on(ent, 0x9657e884);
    wait_till(ent, 0x07b4e7f7);
}

void XB_CDECL BroScr_DeleteAfter300(uint32_t ent) {
    Scr_Wait(300.0f);
    delete_ent(ent);  // tail jump
}

void XB_CDECL BroScr_ProneAimWeightsLoop(uint32_t actor) {
    end_on(actor, 0x07b4e7f7);
    end_on(actor, kDeath);
    end_on(actor, 0x6660fd4f);
    for (;;) {
        slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, float, float, float)>(0x009226bc)(
            actor, title_global<uint32_t>(0x008cf1d4), title_global<uint32_t>(0x008cf1c8), 1.0f, 0.05f,
            1.0f);  // GScr_ActorSetAimAnimWeights(prone shootfeet 45 up, 45 down)
        Scr_Wait(0.1f);
    }
}

void XB_CDECL BroScr_CoverCrouchPlayIdle_3(uint32_t actor) { cover_crouch_idle(actor, title_global<uint32_t>(0x008ceec8)); }
void XB_CDECL BroScr_CoverCrouchPlayIdle_4(uint32_t actor) { cover_crouch_idle(actor, title_global<uint32_t>(0x008ceed4)); }
void XB_CDECL BroScr_CoverCrouchPlayIdle_5(uint32_t actor) { cover_crouch_idle(actor, title_global<uint32_t>(0x008ceec8)); }
void XB_CDECL BroScr_CoverCrouchPlayIdle_6(uint32_t actor) { cover_crouch_idle(actor, title_global<uint32_t>(0x008ceed4)); }

void XB_CDECL BroScr_NotifyAfterTime_3(uint32_t ent, uint32_t notify, uint32_t end, float time) {
    end_on(ent, kDeath);
    end_on(ent, end);
    Scr_Wait(time);
    queue_notify(ent, notify);
}

void XB_CDECL BroScr_GetEyeLoop(uint32_t ent) {
    end_on(ent, 0x07b4e7f7);
    end_on(ent, 0x47e458b4);
    for (;;) {
        Scr_Wait(0.05f);
        uint32_t eye[3] = {undefined_lane(), undefined_lane(), undefined_lane()};
        slot<void XB_CDECL(uint32_t, uint32_t*)>(0x00922528)(ent, eye);  // GScr_GetEye (unused)
    }
}

void XB_CDECL BroScr_DeleteEntsOnNotify(uint32_t, uint32_t ent, ScriptString notify, uint32_t first, uint32_t second) {
    Scr_WaitTillString(ent, notify.rep ? notify.rep->text : nullptr);
    delete_if_valid(first);
    delete_if_valid(second);
    scr_release(&notify);
}

void XB_CDECL BroScr_DebugCvarWait(uint32_t, ScriptString unused) {
    ScriptString cvar;
    const bool debug = ScriptString_Equals(ScrCall_GScr_GetCvar(&cvar, 0x00823564), pointer<const char>(0x0082e058));
    scr_release(&cvar);  // debug == "1"
    if (debug) {
        const int32_t end = get_time() + 3000;
        while (end > get_time()) Scr_Wait(0.05f);
    }
    scr_release(&unused);
}

void XB_CDECL BroScr_FallbackDebugLoop(uint32_t ent) {
    end_on(ent, 0xed4ff1b2);
    end_on(ent, 0x6a48b933);
    for (;;) Scr_Wait(0.05f);
}

void XB_CDECL BroScr_SetGoalRadiusFromNodeOnNotify(uint32_t ai, uint32_t node) {
    using SetFloat = void XB_CDECL(uint32_t, float);
    using GetRadius = long double XB_CDECL(uint32_t);
    wait_till(ai, 0x0032304c);
    if (node != 0 && node != 0xffffffffu && slot<uint32_t XB_CDECL(uint32_t)>(0x00922224)(node) &&  // GScr_IsValidHandle
        slot<GetRadius>(0x00922ac8)(node) > title_global<float>(0x0083eafc)) {  // ScrPathNode_GetRadius > 0
        slot<SetFloat>(0x00922df4)(ai, static_cast<float>(slot<GetRadius>(0x00922ac8)(node)));  // ScrSentient_SetFloat_18
        return;
    }
    slot<SetFloat>(0x00922df4)(ai, 512.0f);
}

void XB_CDECL BroScr_DeleteAfterFrame(uint32_t ent) {
    Scr_Wait(0.05f);
    delete_ent(ent);  // tail jump
}

void XB_CDECL BroScr_DeleteOnOtherDeath(uint32_t other, uint32_t ent) {
    Scr_EndOnString(ent, pointer<const char>(0x00831f60));     // "death"
    Scr_WaitTillString(other, pointer<const char>(0x00831f60));
    delete_if_valid(ent);
}

void XB_CDECL Bro__util__Switch_Music(uint32_t trigger) {
    if (level_object()[0xcc] || !level_object()[0x68]) return;
    level_object()[0xcc] = 1;
    const char* const self = pointer<const char>(0x00825c7c);  // "_util::Switch_Music"
    slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x009221c0)(trigger, 0xe2a210cf, hash_name(self));  // GScr_GetEntVar
    ScriptString noteworthy;
    const bool named = ScrCall_ScrEnt_GetScriptNoteworthy(&trigger, &noteworthy)->rep != nullptr;
    scr_release(&noteworthy);
    if (named) {
        uint32_t hash;
        ScriptString name;
        Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(ScrCall_ScrEnt_GetScriptNoteworthy(&trigger, &name)));
        scr_release(&name);
        if (*reinterpret_cast<uint32_t*>(level_object() + 0x150) != hash) {
            ScriptString track;
            slot<void XB_CDECL(ScriptString*, float)>(0x00922310)(ScrCall_ScrEnt_GetScriptNoteworthy(&trigger, &track),
                                                                  1.0f);  // GScr_AmbientPlay
            scr_release(&track);
            *reinterpret_cast<uint32_t*>(level_object() + 0x150) = hash;
        }
    } else if (*reinterpret_cast<uint32_t*>(level_object() + 0x150) != 0x2d7828fe) {
        slot<void XB_CDECL(float)>(0x00922314)(1.0f);  // GScr_SetMusicTrack
        *reinterpret_cast<uint32_t*>(level_object() + 0x150) = 0x2d7828fe;
    }
    Scr_Wait(0.25f);
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x009221bc)(trigger, 0xe2a210cf, hash_name(self));  // GScr_SetEntVar
    level_object()[0xcc] = 0;
}

void XB_CDECL BroScr_AmbientPlayOutside(ScriptString name) {
    if (!level_object()[0xcc]) {
        level_object()[0xcc] = 1;
        if (name.rep && !ScriptString_Equals(&name, pointer<const char>(0x0081dee0))) {  // "outside"
            uint32_t hash;
            Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&name));
            if (*reinterpret_cast<uint32_t*>(level_object() + 0x150) != hash) {
                slot<void XB_CDECL(ScriptString*, float)>(0x00922310)(&name, 1.0f);  // GScr_AmbientPlay
                *reinterpret_cast<uint32_t*>(level_object() + 0x150) = hash;
            }
        } else if (*reinterpret_cast<uint32_t*>(level_object() + 0x150) != 0x2d7828fe) {
            slot<void XB_CDECL(float)>(0x00922314)(1.0f);  // GScr_SetMusicTrack
            *reinterpret_cast<uint32_t*>(level_object() + 0x150) = 0x2d7828fe;
        }
        Scr_Wait(0.25f);
        level_object()[0xcc] = 0;
    }
    scr_release(&name);
}

void XB_CDECL BroScr_TurretIdleAnimStart(uint32_t ai, uint32_t turret) {
    end_on(ai, 0x07b4e7f7);
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922748)(ai, title_global<uint32_t>(0x008cf0d8));  // ScrActor_SetInt_1370
    slot<void XB_CDECL(uint32_t, uint32_t, float, float, float, uint32_t)>(0x009225d8)(
        ai, title_global<uint32_t>(0x008cf0d8), 1.0f, 0.2f, 1.0f, 1);  // GScr_SetAnimKnob
    BroScr_TurretIdleAnimLoop(ai, turret);  // never returns
}

void XB_CDECL BroScr_Turret30calFire(uint32_t ai, uint32_t turret) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922748)(ai, title_global<uint32_t>(0x008cf348));  // ScrActor_SetInt_1370
    slot<void XB_CDECL(uint32_t, uint32_t, float, float, float, uint32_t)>(0x009225d8)(
        ai, title_global<uint32_t>(0x008cf348), 1.0f, 0.05f, 1.0f, 1);  // GScr_SetAnimKnob(stand30calgunner_fire)
    BroScr_TurretIdleAnimLoop_2(ai, turret);  // never returns
}

void XB_CDECL BroScr_NotifyAfterTimeChecked(uint32_t ent, uint32_t notify, float time) {
    ScriptString message;
    scr_string_from(&message, 0x00825c90);  // "time must be > 0" (the check was compiled out)
    scr_release(&message);
    end_on(ent, notify);
    end_on(ent, 0x07b4e7f7);
    end_on(ent, kDeath);
    Scr_Wait(time);
    queue_notify(ent, notify);
}

// --- 0x0018da20-0x0019a99a ---

namespace {

// The gunner fire knob (0x008cf210 prone mg42 fire) on the AI: SetInt_1370, then SetAnimKnob
// (1, 0.2, 1, restart).
void gunner_fire_knob(uint32_t ai) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922748)(ai, title_global<uint32_t>(0x008cf210));  // ScrActor_SetInt_1370
    slot<void XB_CDECL(uint32_t, uint32_t, float, float, float, uint32_t)>(0x009225d8)(
        ai, title_global<uint32_t>(0x008cf210), 1.0f, 0.2f, 1.0f, 1);  // GScr_SetAnimKnob
}

// Shared body of BroScr_ActorRunToEntIgnoringAll(_2); `flag` is the third argument of slot
// 0x009226ec (1 / 0).
void run_to_ent_ignoring_all(uint32_t ai, uint32_t target, uint32_t flag) {
    end_on(ai, kDeath);
    using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*);
    title_function<StartThread>(0x0045a6d0)(0, 0, 0, 0, 0x00181670, target,
                                            &ai);  // Scr_StartThread_ent_ent(BroScr_NotifyWhenOtherDies)
    using SetFlags = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t);
    slot<SetFlags>(0x009223a0)(ai, 1, 1, 1);  // GScr_SetAIFlags
    uint32_t buffer[3];
    const uint32_t* o = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer, target);  // ScrEnt_GetOrigin
    const uint32_t goal[3] = {o[0], o[1], o[2]};
    slot<void XB_CDECL(uint32_t, const uint32_t*)>(0x00922684)(ai, goal);  // GScr_SetGoalPos
    wait_till(ai, 0x0032304c);
    slot<SetFlags>(0x009223a0)(ai, 0, 0, 0);
    uint32_t node = slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x009226ec)(ai, title_global<uint32_t>(0x008cd76c),
                                                                                      flag);
    if (node == 0xffffffffu) node = 0;
    slot<void XB_CDECL(uint32_t, const uint32_t*)>(0x00922688)(ai, &node);  // GScr_SetGoalPosFromNode
    queue_notify(target, 0x455955a2);
}

bool stance_allowed(uint32_t actor, uint32_t pose) {
    return static_cast<uint8_t>(
               slot<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00922714)(actor, pose)) != 0;  // GScr_ActorIsStanceAllowed
}

void fallback_clear_goal_loop(uint32_t ai) {
    Scr_EndOnString(ai, pointer<const char>(0x00825d54));  // "fallback_clear_goal"
    Scr_EndOnString(ai, pointer<const char>(0x00825d3c));  // "fallback_clear_death"
    for (;;) {
        if (entity_handle_is_valid(ai)) {
            if (ScriptKeyTable* table = title_function<ScriptKeyTable* XB_CDECL(uint32_t)>(0x0011db20)(ai))
                ScriptKeyTable_Find(table, 0xa33a251d);  // read, unused (the debug draw was compiled out)
        }
        Scr_Wait(0.05f);
    }
}

}  // namespace

void XB_CDECL BroScr_Mg42AnimThenShootLoop(uint32_t ai, uint32_t turret) {
    gunner_fire_knob(ai);
    BroScr_Mg42ShootLoop(ai, turret);  // never returns
}

void XB_CDECL BroScr_Mg42AnimThenShootLoop_2(uint32_t ai, uint32_t turret) {
    end_on(ai, 0x07b4e7f7);
    gunner_fire_knob(ai);
    BroScr_Mg42ShootLoop_2(ai, turret);  // never returns
}

void XB_CDECL BroScr_PainAnimByHitLocation(uint32_t actor, uint8_t force) {
    using GetInt = uint32_t XB_CDECL(uint32_t);
    if (!force && slot<GetInt>(0x00922dc0)(actor) != 0x070b89a4 &&  // ScrActor_GetInt_184 (pose)
        slot<GetInt>(0x00922dc0)(actor) != 0xc1165808)
        return;
    BroScr_SetDirectionBlendWeights(actor, static_cast<float>(static_cast<int32_t>(slot<GetInt>(0x00922ce0)(actor))));
    uint32_t front, back, left, right;
    switch (slot<GetInt>(0x00922ce8)(actor)) {  // ScrActor_GetInt_240: the hit location
    case 0x78b45d51:
    case 0x792bc151:  // chest
        front = title_global<uint32_t>(0x008cea6c);
        back = title_global<uint32_t>(0x008cea60);
        left = title_global<uint32_t>(0x008cea78);
        right = title_global<uint32_t>(0x008cea84);
        break;
    case 0x6dc5e695:
    case 0x9a2dd287:
    case 0x9bb24e87:  // left arm
        right = title_global<uint32_t>(0x008ceae4);
        front = title_global<uint32_t>(0x008ceacc);
        back = title_global<uint32_t>(0x008ceac0);
        left = title_global<uint32_t>(0x008cead8);
        break;
    case 0x0036980b:
    case 0x00359844:
    case 0xd667b8d4:  // head
        right = title_global<uint32_t>(0x008ceab4);
        front = title_global<uint32_t>(0x008cea9c);
        back = title_global<uint32_t>(0x008cea90);
        left = title_global<uint32_t>(0x008ceaa8);
        break;
    case 0x000192ce:
    case 0xbcb2520d:
    case 0xedcba386:
    case 0xbd2dce0d:  // right arm
        front = title_global<uint32_t>(0x008ceafc);
        back = title_global<uint32_t>(0x008ceaf0);
        left = title_global<uint32_t>(0x008ceb08);
        right = title_global<uint32_t>(0x008ceb14);
        break;
    case 0x1a2ddf3d:
    case 0x3d2dc3b7:
    case 0x1bb2433d:
    case 0x3cb25fb7:
    case 0x6dc2dea5:
    case 0x0036b1a5:
    case 0xedcc9bb6:  // no pain anim
        return;
    default: {
        ScriptString message;
        scr_string_from(&message, 0x00825cac);  // "Unexpected_hit_location + self.damagelocation" (assert compiled out)
        scr_release(&message);
        return;
    }
    }
    float weight;
    if (static_cast<int32_t>(slot<GetInt>(0x00922cd0)(actor)) > 200)  // ScrActor_GetInt_22c: the damage
        weight = title_global<float>(0x0083eb20);  // 1
    else
        weight = (static_cast<float>(static_cast<int32_t>(slot<GetInt>(0x00922cd0)(actor))) + title_global<float>(0x0083ec30)) *
                 title_global<float>(0x0083ef78);  // (damage + 50) * 0.004
    using ClearAnim = void XB_CDECL(uint32_t, uint32_t, float);
    using SetAnim = void XB_CDECL(uint32_t, uint32_t, uint32_t, float, float, uint32_t);
    slot<ClearAnim>(0x009225d4)(actor, title_global<uint32_t>(0x008cea54), 0.1f);  // GScr_ClearAnim(minor_pain)
    slot<SetAnim>(0x009225dc)(actor, front, at<uint32_t>(anim_object(), 0x140), 0.05f, 1.0f, 0);  // GScr_SetAnim
    slot<SetAnim>(0x009225dc)(actor, back, at<uint32_t>(anim_object(), 0x13c), 0.05f, 1.0f, 0);
    slot<SetAnim>(0x009225dc)(actor, left, at<uint32_t>(anim_object(), 0x144), 0.05f, 1.0f, 0);
    slot<SetAnim>(0x009225dc)(actor, right, at<uint32_t>(anim_object(), 0x148), 0.05f, 1.0f, 0);
    const float rate = title_global<float>(0x0083eb20) / weight;
    using SetAnimRate = void XB_CDECL(uint32_t, uint32_t, float, float, float, uint32_t);
    slot<SetAnimRate>(0x009225dc)(actor, title_global<uint32_t>(0x008cea54), weight, rate * title_global<float>(0x0083edc8),
                                  1.0f, 0);  // minor_pain, time 0.05 / weight
    Scr_Wait(0.05f);
    slot<ClearAnim>(0x009225d4)(actor, title_global<uint32_t>(0x008cea54), rate * title_global<float>(0x0083eca0));  // 0.2 / weight
    Scr_Wait(0.2f);
}

void XB_CDECL BroScr_ActorRunToEntIgnoringAll(uint32_t ai, uint32_t target) { run_to_ent_ignoring_all(ai, target, 1); }
void XB_CDECL BroScr_ActorRunToEntIgnoringAll_2(uint32_t ai, uint32_t target) { run_to_ent_ignoring_all(ai, target, 0); }

void XB_CDECL BroScr_MgFollowEntOrigin(uint32_t mg, uint32_t, uint32_t ent) {
    ScriptString cvar;
    const bool off = ScriptString_Equals(ScrCall_GScr_GetCvar(&cvar, 0x007f49a8), pointer<const char>(0x0080da58));
    scr_release(&cvar);  // mg42 == "off"
    if (off) return;
    *key_slot(mg, 0xc51219a9, 0) = mg;
    for (;;) {
        uint32_t buffer[3];
        const uint32_t* o = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
        const ScrVec3 origin = {{o[0], o[1], o[2]}};
        slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922b24)(mg, origin);  // ScrEnt_SetOrigin
        Scr_Wait(0.1f);
    }
}

void XB_CDECL BroScr_TrackStartFlagLoop(uint32_t ent) {
    for (;;) {
        wait_till(ent, 0xd50105fc);
        *int_slot(ent, 0x070b8a34) = 0;
        wait_till(ent, 0xccd105e5);
        *int_slot(ent, 0x070b8a34) = 1;
    }
}

void XB_CDECL BroScr_MgWaitNewUser(uint32_t mg, uint32_t ai) {
    if (*int_slot(mg, 0x7b1e85ba) != 0) return;
    // The original's ECX here is a stale table pointer; its upper bytes would land in a missing field.
    *reinterpret_cast<uint8_t*>(ScrField_used_an_mg42(ai, 0)) = 1;
    *int_slot(mg, 0x7b1e85ba) = 1;
    wait_till(ai, kDeath);
    *int_slot(mg, 0x7b1e85ba) = 0;
    queue_notify(mg, hash_name(pointer<const char>(0x007fff6c)));  // "get new user"
}

void XB_CDECL BroScr_FallbackOnGoalRestore(uint32_t ai) {
    wait_till(ai, 0x0032304c);
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922d24)(ai, *int_slot(ai, 0xac91e5fb));  // ScrActor_SetInt_1308
    slot<void XB_CDECL(uint32_t, float)>(0x00922d0c)(
        ai, static_cast<float>(static_cast<int32_t>(*int_slot(ai, 0x62271501))));  // ScrActor_SetFloat_7e4
    queue_notify(ai, 0xed4ff1b2);
    queue_notify(ai, 0x6a48b933);
}

void XB_CDECL BroScr_FallbackClearGoalWait(uint32_t ai) {
    Scr_EndOnString(ai, pointer<const char>(0x00823550));  // "fallback"
    fallback_clear_goal_loop(ai);
}
void XB_CDECL BroScr_FallbackClearGoalWait_2(uint32_t ai) { fallback_clear_goal_loop(ai); }

void XB_CDECL BroScr_TurretRandomSpread(uint32_t turret, uint32_t target) {
    const char* const stop = pointer<const char>(0x00825d18);  // "stop random_spread"
    queue_notify(turret, hash_name(stop));
    Scr_EndOnString(turret, stop);
    Scr_EndOnString(turret, pointer<const char>(0x00825d0c));  // "StopFiring"
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922590)(turret, target);  // GScr_Turret_SetTargetEntity
    using GetOrigin = uint32_t* XB_CDECL(uint32_t*, uint32_t);
    using SetOrigin = void XB_CDECL(uint32_t, ScrVec3);
    using RandomFloat = long double XB_CDECL(float);
    for (;;) {
        // The target follows the turret's key 0xcf1e52a5 entity (the player's origin through
        // GScr_GetOrigin when the target is the player; 48 up when the entity is the player).
        if (target == player()) {
            ScrVec3 at = {{undefined_lane(), undefined_lane(), undefined_lane()}};
            slot<void XB_CDECL(uint32_t, ScrVec3*)>(0x00922524)(*ent_slot(turret, 0xcf1e52a5), &at);  // GScr_GetOrigin
            slot<SetOrigin>(0x00922b24)(target, at);
        } else if (*ent_slot(turret, 0xcf1e52a5) == player()) {
            uint32_t buffer[3];
            const uint32_t* o = slot<GetOrigin>(0x00922b20)(buffer, *ent_slot(turret, 0xcf1e52a5));  // ScrEnt_GetOrigin
            const float z = as_float(o[2]) + title_global<float>(0x0083ecd8);                       // + 48
            const ScrVec3 at = {{o[0], o[1], bits(z)}};
            slot<SetOrigin>(0x00922b24)(target, at);
        } else {
            uint32_t buffer[3];
            const uint32_t* o = slot<GetOrigin>(0x00922b20)(buffer, *ent_slot(turret, 0xcf1e52a5));
            const ScrVec3 at = {{o[0], o[1], o[2]}};
            slot<SetOrigin>(0x00922b24)(target, at);
        }
        // A random offset: x and y from the second and first RandomFloat(40), z from RandomFloat(60)
        // (subtracted on the x87).
        const float r1 = static_cast<float>(slot<RandomFloat>(0x00922230)(40.0f));
        const float r2 = static_cast<float>(slot<RandomFloat>(0x00922230)(40.0f));
        const float half = title_global<float>(0x0083eb5c);  // 20
        const float dx = half - r2;
        const float dy = half - r1;
        const float dz = static_cast<float>(half - slot<RandomFloat>(0x00922230)(60.0f));
        uint32_t buffer[3];
        const uint32_t* o = slot<GetOrigin>(0x00922b20)(buffer, target);
        const float x = as_float(o[0]) + dx;
        const float z = static_cast<float>(static_cast<long double>(as_float(o[2])) + dz);
        const float y = as_float(o[1]) + dy;
        const ScrVec3 at = {{bits(x), bits(y), bits(z)}};
        slot<SetOrigin>(0x00922b24)(target, at);
        Scr_Wait(0.2f);
    }
}

void XB_CDECL BroScr_SetIdlePoseByAllowedStance(uint32_t actor) {
    using SetAnim7 = void XB_CDECL(uint32_t, uint32_t, uint32_t, float, float, float, uint32_t);
    using SetPose = void XB_CDECL(uint32_t, uint32_t);
    Scr_Wait(0.1f);
    if (!slot<uint32_t XB_CDECL(uint32_t)>(0x00922210)(actor)) return;  // GScr_IsAlive
    const uint32_t root = 0x008cf300;  // g_anim_root
    // stand first; crouch, then prone where standing is not allowed (each tested once)
    uint32_t pose = 0x070b89a4;
    if (!stance_allowed(actor, 0x070b89a4)) {
        if (stance_allowed(actor, 0xc1165808))
            pose = 0xc1165808;
        else if (stance_allowed(actor, 0x0738b1a5))
            pose = 0x0738b1a5;
    }
    if (pose == 0xc1165808) {  // crouch
        slot<SetAnim7>(0x009225c8)(actor, title_global<uint32_t>(0x008ce544), title_global<uint32_t>(root), 1.0f, 0.2f, 1.0f,
                                   0);  // GScr_SetAnimKnobAll(crouch_alert_a_idle)
        *ScrField_anim_idleset(actor) = 0x61;
        slot<SetPose>(0x00922dc4)(actor, 0xc1165808);  // ScrActor_SetInt_184
    } else if (pose == 0x0738b1a5) {  // prone
        slot<SetAnim7>(0x009225c8)(actor, title_global<uint32_t>(0x008cf0f0), title_global<uint32_t>(root), 1.0f, 0.2f, 1.0f,
                                   0);  // prone_aim_idle
        slot<void XB_CDECL(uint32_t, float, float, uint32_t, uint32_t, uint32_t)>(0x009226b8)(
            actor, -45.0f, 45.0f, title_global<uint32_t>(0x008cf12c), title_global<uint32_t>(0x008cf138),
            title_global<uint32_t>(0x008cf144));  // GScr_ActorSetPitchBlendAnims(legs down, straight, up)
        slot<void XB_CDECL(uint32_t, float)>(0x009226b0)(actor, 1.0f);
        slot<SetPose>(0x00922dc4)(actor, 0x0738b1a5);
    } else {  // stand
        slot<SetAnim7>(0x009225c8)(actor, title_global<uint32_t>(0x008cf3e4), title_global<uint32_t>(root), 1.0f, 0.2f, 1.0f,
                                   0);  // stand_alert_1
        *ScrField_anim_idleset(actor) = 0x61;
        slot<SetPose>(0x00922dc4)(actor, 0x070b89a4);
    }
    Scr_Wait(0.2f);
    using NotifyWithString = void XB_CDECL(uint32_t, uint32_t, ScriptString*);
    ScriptString end;
    scr_string_from(&end, 0x00832b28);  // "end"
    slot<NotifyWithString>(0x009221ac)(actor, 0x2c6a01b1, &end);  // Scr_QueueNotifyWithString
    scr_release(&end);
    ScriptString end2;
    scr_string_from(&end2, 0x00832b28);
    slot<NotifyWithString>(0x009221ac)(actor, 0x8d403617, &end2);
    scr_release(&end2);
}

}  // namespace game
