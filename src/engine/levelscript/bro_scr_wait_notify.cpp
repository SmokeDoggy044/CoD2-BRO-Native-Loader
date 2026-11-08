#include "bro_scr_wait_notify.h"

#include <cstring>

#include "bro_scr_arrays.h"
#include "bro_scr_exploder.h"
#include "g_scr_runtime.h"
#include "scr_field.h"
#include "title_data.h"
#include "bro_scr_note_handlers.h"

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

constexpr uint32_t kDeath = 0x06730ae8;
constexpr uint32_t kSoundDone = 0x75c3b1bc;

void end_on(uint32_t ent, uint32_t notify) { slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, notify); }  // Scr_EndOn
void queue_notify(uint32_t ent, uint32_t notify) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(ent, notify);  // Scr_QueueNotify
}
int32_t health(uint32_t ent) { return slot<int32_t XB_CDECL(uint32_t)>(0x00922b90)(ent); }  // ScrEnt_GetHealth
void fire_weapon(uint32_t vehicle) { slot<void XB_CDECL(uint32_t)>(0x009228d4)(vehicle); }  // GScr_Vehicle_FireWeapon
void delete_ent(uint32_t ent) { slot<void XB_CDECL(uint32_t)>(0x00922540)(ent); }            // GScr_Delete

using GetVec = uint32_t* XB_CDECL(uint32_t*, uint32_t);
using Spawn = uint32_t XB_CDECL(ScriptString*, const uint32_t*, uint32_t);
using PlaySound = void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t);
using AnimScripted = void XB_CDECL(uint32_t, uint32_t, const ScrVec3*, const ScrVec3*, uint32_t);

// A new script_origin entity at `origin` (GScr_Spawn); the class name is a released temporary.
uint32_t spawn_script_origin(const uint32_t* origin) {
    ScriptString classname;
    ScriptString_FromCString(&classname, pointer<const char>(0x0083ab88));  // "script_origin"
    const uint32_t ent = slot<Spawn>(0x009222f0)(&classname, origin, 0);
    ScriptString_Release(&classname);
    return ent;
}

// The level entity's handle: the word at +4 of the level object (0x00922ee0), read through null.
uint32_t level_handle() {
    const uint32_t object = title_global<uint32_t>(0x00922ee0);
    const uint32_t field = object ? object + 4 : 0;
    return *reinterpret_cast<volatile uint32_t*>(static_cast<uintptr_t>(field));
}

uint32_t level_object() { return title_global<uint32_t>(0x00922ee0); }
uint32_t& level_word(uint32_t offset) { return *pointer<uint32_t>(level_object() + offset); }

void set_model(uint32_t ent, uint32_t model) {
    ScriptString name;
    ScriptString_FromCString(&name, pointer<const char>(model));
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(ent, &name, 0);  // GScr_SetModel
    ScriptString_Release(&name);
}

// The bodies shared by identical copies (each copy differs only in its EH handler).
inline void fire_if_alive(uint32_t vehicle) {
    if (health(vehicle) > 0) fire_weapon(vehicle);
}

inline void fire_anim_if_alive(uint32_t vehicle) {
    if (health(vehicle) <= 0) return;
    fire_weapon(vehicle);
    using SetAnimKnob = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    slot<SetAnimKnob>(0x009225d8)(vehicle, title_global<uint32_t>(0x008cd4b8), 0x3f800000, 0x3e4ccccd, 0x3f800000,
                                  1);  // GScr_SetAnimKnob (1.0, 0.2, 1.0)
}

inline void anim_scripted_then_notify(uint32_t ent, ScrVec3* origin, ScrVec3* angles, uint32_t anim) {
    slot<AnimScripted>(0x009224c0)(ent, 0xaec5813d, origin, angles, anim);  // GScr_AnimScriptedNoNotify
    Scr_WaitTillArgs(ent, 0xaec5813d, 0x199a4, 0, 0);
    queue_notify(ent, 0x93b8e727);
}

inline void play_sound_on_temp_origin(ScriptString* sound, const ScrVec3& origin) {
    const float start[3] = {0.0f, 0.0f, as_float(title_global<uint32_t>(0x0083eb20))};  // (0, 0, 1)
    uint32_t start_bits[3];
    std::memcpy(start_bits, start, sizeof(start_bits));
    const uint32_t ent = spawn_script_origin(start_bits);
    slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922b24)(ent, origin);  // ScrEnt_SetOrigin
    slot<PlaySound>(0x009221dc)(ent, sound, 0x9a73c1c0, 0, 0);        // GScr_PlaySound
    Scr_WaitTill(ent, 0x9a73c1c0);
    delete_ent(ent);
    ScriptString_Release(sound);
}

inline void stuka_siren(uint32_t ent) {
    Scr_EndOnString(ent, pointer<const char>(0x0082372c));  // "stop_mg42s"
    for (;;) {
        ScriptString sound;
        ScriptString_FromCString(&sound, pointer<const char>(0x00823720));  // "stuka_siren"
        slot<PlaySound>(0x009221dc)(ent, &sound, 0x9a73c1c0, 0, 0);        // GScr_PlaySound
        ScriptString_Release(&sound);
        Scr_WaitTill(ent, 0x9a73c1c0);
    }
}

inline void turret_fire_loop(uint32_t vehicle) {
    while (health(vehicle) > 0) {
        Scr_WaitTill(vehicle, 0xac1c7bac);
        if (health(vehicle) > 0) fire_weapon(vehicle);
    }
}

// The same on "turret_fire" (a string wait); with `recoil` also the fire anim knob.
inline void turret_fire_loop_string(uint32_t vehicle, bool recoil) {
    while (health(vehicle) > 0) {
        Scr_WaitTillString(vehicle, pointer<const char>(0x0083aa04));  // "turret_fire"
        if (recoil)
            fire_anim_if_alive(vehicle);
        else if (health(vehicle) > 0)
            fire_weapon(vehicle);
    }
}

// Each "trigger" on `trigger` queues "<text><number>" (number: an int field of the trigger) on the level.
inline void notify_level_on_trigger(uint32_t trigger, const uint32_t* number_field, uint32_t text) {
    const int32_t number = static_cast<int32_t>(*number_field);
    for (;;) {
        Scr_WaitTillString(trigger, pointer<const char>(0x0083aa58));  // "trigger"
        const uint32_t level = title_global<uint32_t>(0x00922ee0);
        const uint32_t* handle = pointer<uint32_t>(level ? level + 4 : 0);
        ScriptString name;
        uint32_t hash;
        const uint32_t notify = *Scr_HashScriptString(
            &hash, reinterpret_cast<const uint8_t* const*>(ScriptString_FromCStringPlusInt(&name, pointer<const char>(text), number)));
        queue_notify(*handle, notify);
        ScriptString_Release(&name);
    }
}

}  // namespace

void XB_CDECL BroScr_NotifyAfterWaitTill_56caf2eb(uint32_t self, uint32_t object, uint32_t notify) {
    end_on(self, notify);
    Scr_WaitTill(object, 0x56caf2eb);
    queue_notify(self, notify);
}

void XB_CDECL BroScr_NotifyAfterSingleAnim(uint32_t self, uint32_t object, uint32_t notify) {
    end_on(self, notify);
    Scr_WaitTillArgs(object, 0xce00786e, 0x199a4, 0, 0);
    queue_notify(self, notify);
}

void XB_CDECL BroScr_AnimNoteTracksThread(uint32_t ent, uint32_t anim) {
    end_on(ent, 0x3621f548);
    end_on(ent, kDeath);
    ScriptString none;
    ScriptString_Copy(&none, &title_global<ScriptString>(0x00922e90));
    // The original writes the result into its first argument's slot.
    ScriptString result;
    using HandleNoteTracks = ScriptString* XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, ScriptString);
    own_function<HandleNoteTracks>(&BroScr_HandleNoteTracks)(&result, ent, anim, 0, none);  // BroScr_HandleNoteTracks
    ScriptString_Release(&result);
}

void XB_CDECL BroScr_AnimReachNotifyOnDeath(uint32_t, uint32_t ent, uint32_t other) {
    end_on(ent, 0x0032304c);
    Scr_WaitTill(ent, kDeath);
    queue_notify(other, 0x2253e8ee);
}

void XB_CDECL BroScr_FlagWhilePlayerLooksAway(uint32_t ent) {
    bool cleared = true;
    for (;;) {
        uint32_t buffer1[3], buffer2[3];
        const uint32_t* p = slot<GetVec>(0x00922b20)(buffer1, title_global<uint32_t>(0x00922eec));  // ScrEnt_GetOrigin
        const uint32_t player[3] = {p[0], p[1], p[2]};
        const uint32_t* e = slot<GetVec>(0x00922b20)(buffer2, ent);
        const uint32_t origin[3] = {e[0], e[1], e[2]};
        float dir[3];
        dir[0] = as_float(origin[0]) - as_float(player[0]);
        dir[1] = as_float(origin[1]) - as_float(player[1]);
        dir[2] = as_float(origin[2]) - as_float(player[2]);
        const uint32_t undefined = title_global<uint32_t>(0x00922eb8);  // g_scrUndefinedVecLane
        uint32_t to[3] = {undefined, undefined, undefined};
        slot<void XB_CDECL(uint32_t*, const float*)>(0x00922284)(to, dir);  // Scr_VectorToAngles
        const uint32_t angles_to[3] = {to[0], to[1], to[2]};
        uint32_t buffer3[3];
        const uint32_t* a = slot<GetVec>(0x00922ba0)(buffer3, title_global<uint32_t>(0x00922eec));  // ScrEnt_GetAngles
        const uint32_t angles[3] = {a[0], a[1], a[2]};
        float yaw = as_float(angles[1]);
        // The script's `yaw < 0` goes through a float truth value.
        if (static_cast<float>(0.0f > yaw ? 1 : 0) != 0.0f) yaw += as_float(title_global<uint32_t>(0x0083ebcc));  // 360
        const float diff = yaw - as_float(angles_to[1]);
        if (diff > as_float(title_global<uint32_t>(0x0083ef1c)) || as_float(title_global<uint32_t>(0x0083ef18)) > diff) {
            if (cleared) {
                cleared = false;
                slot<void XB_CDECL(uint32_t)>(0x00922568)(ent);  // GScr_SetEntFlag400
            }
        } else if (!cleared) {
            cleared = true;
            slot<void XB_CDECL(uint32_t)>(0x00922564)(ent);  // GScr_ClearEntFlag400
        }
        Scr_WaitFrames(0);
    }
}

void XB_CDECL BroScr_KillWhenHealthLow(uint32_t ent) {
    while (health(ent) >= 5) Scr_Wait(as_float(0x3d4ccccd));  // 0.05
    slot<void XB_CDECL(uint32_t)>(0x009224c8)(ent);  // GScr_StopAnimScripted
    const float from[3] = {0.0f, 0.0f, 0.0f};
    const float amount = static_cast<float>(health(ent) + 100);
    slot<void XB_CDECL(uint32_t, float, const float*)>(0x00922558)(ent, amount, from);  // GScr_DoDamage
}

void XB_CDECL BroScr_WaitTill_h08d272e5(uint32_t object) { Scr_WaitTill(object, 0x08d272e5); }

void XB_CDECL BroScr_PlayFlakSound(uint32_t, ScrVec3 origin) {
    const uint32_t ent = spawn_script_origin(origin.v);
    slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922b24)(ent, origin);  // ScrEnt_SetOrigin
    ScriptString sound;
    ScriptString_FromCString(&sound, pointer<const char>(0x0080a530));  // "flak_generic"
    slot<PlaySound>(0x009221dc)(ent, &sound, kSoundDone, 0, 0);       // GScr_PlaySound
    ScriptString_Release(&sound);
    Scr_WaitTill(ent, kSoundDone);
    Scr_Wait(1.0f);
    delete_ent(ent);
}

void XB_CDECL BroScr_PlaySoundAtTempOrigin(ScrVec3 origin, uint32_t, ScriptString sound, ScrVec3 position) {
    const uint32_t ent = spawn_script_origin(origin.v);
    const uint32_t at[3] = {position.v[0], position.v[1], position.v[2]};
    using PlayAt = void XB_CDECL(uint32_t, ScriptString*, const uint32_t*, uint32_t, uint32_t);
    slot<PlayAt>(0x009221e0)(ent, &sound, at, kSoundDone, 0);  // GScr_PlaySoundAtPosition
    Scr_WaitTill(ent, kSoundDone);
    Scr_Wait(1.0f);
    delete_ent(ent);
    ScriptString_Release(&sound);
}

void XB_CDECL BroScr_WaitFiveSeconds() {
    using GetTime = int32_t XB_CDECL();
    const int32_t end = slot<GetTime>(0x009223c4)() + 5000;  // GScr_GetTime
    while (end > slot<GetTime>(0x009223c4)()) Scr_WaitFrames(0);
}

void XB_CDECL BroScr_FadeFromBlack(ScriptString text) {
    using Set = void XB_CDECL(uint32_t, uint32_t);
    using HudOp = void XB_CDECL(uint32_t, uint32_t);
    // The new hud element's handle is written to a stack word first (the slot returns a pointer to it).
    uint32_t handle;
    const uint32_t black = *slot<uint32_t* XB_CDECL(uint32_t*)>(0x0092248c)(&handle);  // GScr_NewHudElem (a new hud elem)
    level_word(0x2e0) = black;
    slot<Set>(0x00922a48)(level_word(0x2e0), 0);                                    // ScrHudElem_Set_04
    slot<Set>(0x00922a50)(level_word(0x2e0), 0);                                    // ScrHudElem_Set_08
    slot<Set>(0x00922a68)(level_word(0x2e0), 0);                                    // ScrHudElem_SetFloat_6c
    ScriptString shader;
    ScriptString_FromCString(&shader, pointer<const char>(0x007f51f8));  // "black"
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t)>(0x009228ec)(level_object() + 0x2e0, &shader, 0x280,
                                                                                 0x1e0);  // GScr_HudSetShader 640x480
    ScriptString_Release(&shader);
    Scr_Wait(as_float(0x3dcccccd));  // 0.1
    if (text.rep) {
        const uint32_t elem = *slot<uint32_t* XB_CDECL(uint32_t*)>(0x0092248c)(&handle);
        level_word(0x62c) = elem;
        slot<Set>(0x00922a48)(level_word(0x62c), 0x140);       // x 320
        slot<Set>(0x00922a50)(level_word(0x62c), 0xdc);        // y 220
        slot<Set>(0x00922a58)(level_word(0x62c), 1);           // ScrHudElem_Set_14
        slot<Set>(0x00922a60)(level_word(0x62c), 1);           // ScrHudElem_Set_18
        slot<Set>(0x00922a68)(level_word(0x62c), 0x3f800000);  // SetFloat_6c 1.0
        slot<Set>(0x00922a70)(level_word(0x62c), 0x3fd9999a);  // SetFloat_0c 1.7
        slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009228e8)(level_object() + 0x62c, &text);  // GScr_HudSetText
        slot<Set>(0x00922a78)(level_word(0x62c), 0);                                            // ScrHudElem_SetAlpha
        slot<HudOp>(0x0092290c)(level_object() + 0x62c, 0x3f99999a);                              // GScr_HudFadeOverTime 1.2
        slot<Set>(0x00922a78)(level_word(0x62c), 0xc8);
    }
    Scr_Wait(as_float(0x400ccccd));  // 2.2
    if (text.rep) {
        slot<HudOp>(0x0092290c)(level_object() + 0x62c, 0x3f99999a);
        slot<Set>(0x00922a78)(level_word(0x62c), 0);
    }
    Scr_Wait(as_float(0x3fcccccd));  // 1.6
    slot<void XB_CDECL(uint32_t)>(0x0092291c)(level_object() + 0x2e0);  // GScr_HudDestroy
    if (text.rep) slot<void XB_CDECL(uint32_t)>(0x0092291c)(level_object() + 0x62c);
    ScriptString_Release(&text);
}

void XB_CDECL BroScr_StartFxOnTrigger(uint32_t, uint32_t, uint32_t trigger) {
    notify_level_on_trigger(trigger, ScrField_h7fa1fc35(trigger), 0x008235bc);  // "start fx"
}

void XB_CDECL BroScr_StopFxOnTrigger(uint32_t, uint32_t, uint32_t trigger) {
    notify_level_on_trigger(trigger, ScrField_h0bfd0e20(trigger), 0x008235c8);  // "stop fx"
}

void XB_CDECL BroScr_FriendlyChainOnTrigger(uint32_t trigger) {
    Scr_WaitTillString(trigger, pointer<const char>(0x0083aa58));  // "trigger"
    ScriptString chain;
    ScriptString_Copy(&chain, ScrField_h83048760(trigger));
    BroScr_GetFriendlyChainTriggers(chain);
}

void XB_CDECL BroScr_WaitTillTrigger(uint32_t ent) {
    Scr_WaitTillString(ent, pointer<const char>(0x0083aa58));  // "trigger"
}

void XB_CDECL BroScr_ExploderTriggerThink(uint32_t trigger) {
    const int32_t number = static_cast<int32_t>(*ScrField_script_exploder(trigger));
    ScriptString name;
    uint32_t hash;  // the original keeps it in its argument's slot
    hash = *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(ScriptString_FromCStringPlusInt(
                                            &name, pointer<const char>(0x008235d0), number)));  // "killexplodertriggers"
    ScriptString_Release(&name);
    end_on(level_handle(), hash);
    slot<void XB_CDECL(int32_t, uint32_t)>(0x009223bc)(number, 0);  // GScr_SetIntMapValue
    slot<uint32_t XB_CDECL(int32_t)>(0x009223c0)(number);           // GScr_GetIntMapValue (unused)
    Scr_WaitTillString(trigger, pointer<const char>(0x0083aa58));   // "trigger"
    BroScr_Exploder(level_handle(), number);
    queue_notify(level_handle(), hash);
}

void XB_CDECL BroScr_PanzerfaustObjectiveModels(uint32_t, uint32_t level, uint32_t weapon, uint32_t box) {
    for (;;) {
        uint32_t hash;
        Scr_WaitTill(level, *Scr_HashName(&hash, pointer<const char>(0x00823694)));  // "infinite panzerfaust objective on"
        set_model(weapon, 0x00823670);  // "xmodel/weapon_panzerfaust_objective"
        set_model(box, 0x00823648);     // "xmodel/ammo_panzerfaust_box2_objective"
        Scr_WaitTill(level, *Scr_HashName(&hash, pointer<const char>(0x00823624)));  // "... objective off"
        set_model(weapon, 0x00823608);  // "xmodel/weapon_panzerfaust"
        set_model(box, 0x008235e8);     // "xmodel/ammo_panzerfaust_box2"
    }
}

void XB_CDECL BroScr_VehicleFireAnimIfAlive(uint32_t vehicle) { fire_anim_if_alive(vehicle); }
void XB_CDECL BroScr_VehicleFireAnimIfAlive_2(uint32_t vehicle) { fire_anim_if_alive(vehicle); }

void XB_CDECL BroScr_AnimScriptedThenNotify(uint32_t ent, ScrVec3 origin, ScrVec3 angles, uint32_t anim) {
    anim_scripted_then_notify(ent, &origin, &angles, anim);
}
void XB_CDECL BroScr_AnimScriptedThenNotify_2(uint32_t ent, ScrVec3 origin, ScrVec3 angles, uint32_t anim) {
    anim_scripted_then_notify(ent, &origin, &angles, anim);
}
void XB_CDECL BroScr_AnimScriptedThenNotify_3(uint32_t ent, ScrVec3 origin, ScrVec3 angles, uint32_t anim) {
    anim_scripted_then_notify(ent, &origin, &angles, anim);
}

void XB_CDECL BroScr_VehicleFireIfAlive(uint32_t vehicle) { fire_if_alive(vehicle); }
void XB_CDECL BroScr_VehicleFireIfAlive_2(uint32_t vehicle) { fire_if_alive(vehicle); }
void XB_CDECL BroScr_VehicleFireIfAlive_3(uint32_t vehicle) { fire_if_alive(vehicle); }
void XB_CDECL BroScr_VehicleFireIfAlive_4(uint32_t vehicle) { fire_if_alive(vehicle); }

void XB_CDECL BroScr_PlaySoundOnTempOrigin(uint32_t, ScriptString sound, ScrVec3 origin) {
    play_sound_on_temp_origin(&sound, origin);
}
void XB_CDECL BroScr_PlaySoundOnTempOrigin_2(uint32_t, ScriptString sound, ScrVec3 origin) {
    play_sound_on_temp_origin(&sound, origin);
}
void XB_CDECL BroScr_PlaySoundOnTempOrigin_3(uint32_t, ScriptString sound, ScrVec3 origin) {
    play_sound_on_temp_origin(&sound, origin);
}

void XB_CDECL BroScr_StukaSiren(uint32_t ent) { stuka_siren(ent); }
void XB_CDECL BroScr_StukaSiren_2(uint32_t ent) { stuka_siren(ent); }
void XB_CDECL BroScr_StukaSiren_3(uint32_t ent) { stuka_siren(ent); }

void XB_CDECL BroScr_StukaTakeoff(uint32_t ent) {
    ScriptString tree;
    ScriptString_FromCString(&tree, pointer<const char>(0x007f4970));  // "stuka"
    slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009225f0)(ent, &tree);  // GScr_UseAnimTree
    ScriptString_Release(&tree);
    Scr_WaitTillString(ent, pointer<const char>(0x0081db48));  // "takeoff"
    using SetAnim = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    slot<SetAnim>(0x009225dc)(ent, title_global<uint32_t>(0x008cd4d0), 0x3f800000, 0x3e4ccccd, 0x3f800000,
                              0);  // GScr_SetAnim (1.0, 0.2, 1.0)
    slot<SetAnim>(0x009225dc)(ent, title_global<uint32_t>(0x008cd4c4), 0x3f800000, 0x3e4ccccd, 0x3f800000, 0);
}

void XB_CDECL BroScr_TankDeadGuyPlayAnim(uint32_t ent, ScrVec3 origin, ScrVec3 angles, float anim) {
    const int32_t handle = static_cast<int32_t>(anim);
    slot<AnimScripted>(0x009224c0)(ent, 0xe2ea7dce, &origin, &angles,
                                   static_cast<uint32_t>(handle));  // GScr_AnimScriptedNoNotify
    Scr_WaitTillArgs(ent, 0xe2ea7dce, 0x199a4, 0, 0);
}

void XB_CDECL BroScr_VehicleTurretFireLoop(uint32_t vehicle) { turret_fire_loop(vehicle); }
void XB_CDECL BroScr_VehicleTurretFireLoop_2(uint32_t vehicle) { turret_fire_loop(vehicle); }
void XB_CDECL BroScr_VehicleTurretFireLoop_3(uint32_t vehicle) { turret_fire_loop(vehicle); }
void XB_CDECL BroScr_VehicleTurretFireLoop_4(uint32_t vehicle) { turret_fire_loop(vehicle); }
void XB_CDECL BroScr_VehicleTurretFireLoop_7(uint32_t vehicle) { turret_fire_loop(vehicle); }
void XB_CDECL BroScr_VehicleTurretFireLoop_8(uint32_t vehicle) { turret_fire_loop(vehicle); }
void XB_CDECL BroScr_VehicleTurretFireLoop_5(uint32_t vehicle) { turret_fire_loop_string(vehicle, false); }
void XB_CDECL BroScr_VehicleTurretFireLoop_6(uint32_t vehicle) { turret_fire_loop_string(vehicle, false); }
void XB_CDECL BroScr_TankTurretFireLoopRecoil(uint32_t vehicle) { turret_fire_loop_string(vehicle, true); }
void XB_CDECL BroScr_TankTurretFireLoopRecoil_2(uint32_t vehicle) { turret_fire_loop_string(vehicle, true); }
void XB_CDECL BroScr_TankTurretFireLoopRecoil_3(uint32_t vehicle) { turret_fire_loop_string(vehicle, true); }
void XB_CDECL BroScr_TankTurretFireLoopRecoil_4(uint32_t vehicle) { turret_fire_loop_string(vehicle, true); }

void XB_CDECL BroScr_NotifyLevelOnDeath(uint32_t self, uint32_t ent) {
    end_on(ent, 0x92b8c65f);
    Scr_WaitTill(ent, kDeath);
    queue_notify(level_handle(), 0x92b8c65f);
    queue_notify(self, 0x92b8c65f);
}

void XB_CDECL BroScr_WaitTillNotifyCount(int32_t count) {
    for (int32_t i = count; i > 0; --i) Scr_WaitTill(level_handle(), 0x92b8c65f);
}

void XB_CDECL BroScr_WaitTill_hcc07a20a(uint32_t object) { Scr_WaitTill(object, 0xcc07a20a); }

}  // namespace game
