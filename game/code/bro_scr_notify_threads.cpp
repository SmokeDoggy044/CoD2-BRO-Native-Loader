#include "bro_scr_notify_threads.h"

#include "bro_scr_ai_common.h"
#include "bro_scr_common.h"
#include "g_scr_runtime.h"
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

void turret_fire(uint32_t turret) { slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922580)(turret, 0); }  // GScr_TurretCall272
void delete_ent(uint32_t ent) { slot<void XB_CDECL(uint32_t)>(0x00922540)(ent); }                      // GScr_Delete

using SetAnim7 = void XB_CDECL(uint32_t, uint32_t, uint32_t, float, float, float, uint32_t);
using SetAnim8 = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, float, float, float, uint32_t);

// The turret idle knob (0x008cf684): weight 1, time 0.2, rate 1, restart.
void turret_idle_knob(uint32_t turret) {
    slot<void XB_CDECL(uint32_t, uint32_t, float, float, float, uint32_t)>(0x009225d8)(
        turret, title_global<uint32_t>(0x008cf684), 1.0f, 0.2f, 1.0f, 1);  // GScr_SetAnimKnob
}

void cover_crouch_idle(uint32_t actor, uint32_t anim) {
    slot<SetAnim8>(0x009225e4)(actor, 0xde61361c, anim, title_global<uint32_t>(0x008cd7dc), 1.0f, 0.1f, 0.0f,
                               1);  // GScr_SetFlaggedAnimKnobAll(.., g_anim_Body, ..)
    Scr_Wait(1.0f);
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
    end_on(ent, 0x07b4e7f7);
    end_on(ent, kDeath);
    end_on(ent, end);
    wait_till(ent, wait);
    queue_notify(ent, notify);
}

void XB_CDECL BroScr_NotifyAfterTime(uint32_t ent, float time, uint32_t notify, uint32_t end) {
    end_on(ent, 0x07b4e7f7);
    end_on(ent, kDeath);
    end_on(ent, end);
    Scr_Wait(time);
    queue_notify(ent, notify);
}

}  // namespace game
