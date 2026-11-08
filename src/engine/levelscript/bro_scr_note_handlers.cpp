#include "bro_scr_note_handlers.h"

#include "bro_scr_ai_anim.h"
#include "bro_scr_ai_common.h"
#include "bro_scr_ai_look.h"
#include "bro_scr_ai_weapons.h"
#include "bro_scr_common.h"
#include "g_scr_runtime.h"
#include "scr_access.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "script_string.h"
#include "title_data.h"
#include "bro_scr_actor_threads.h"

namespace game {

using namespace bro_ai;

namespace {

constexpr uint32_t kStand = 0x070b89a4;
constexpr uint32_t kCrouch = 0xc1165808;
constexpr uint32_t kProne = 0x0738b1a5;
constexpr uint32_t kBack = 0x0030880b;

using FlaggedKnobAll = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, float, float, float, uint32_t);
using KnobAll = void XB_CDECL(uint32_t, uint32_t, uint32_t, float, float, float, uint32_t);
using Knob = void XB_CDECL(uint32_t, uint32_t, float, float, float, uint32_t);
using CallEnt6Anims = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                        const uint32_t*, const uint32_t*, const uint32_t*, const uint32_t*,
                                        const uint32_t*);
using CallVt00825c50 = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                         const uint32_t*, const uint32_t*, const ScriptString*);
using BeginThreadFunctor1 = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using WaitTillOutputStr = void XB_CDECL(uint32_t, uint32_t, ScriptString*);
using PlaySound = void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t);
using AnimMode = void XB_CDECL(uint32_t, ScriptString*);
using EntFloat = void XB_CDECL(uint32_t, float);
using AimWeights = void XB_CDECL(uint32_t, uint32_t, uint32_t, float, float, float);

uint32_t body() { return title_global<uint32_t>(0x008cd7dc); }  // g_anim_Body
uint32_t is_ai(uint32_t ent) { return slot<uint32_t XB_CDECL(uint32_t)>(0x0092221c)(ent); }  // GScr_IsAI
uint32_t get_pose(uint32_t ent) { return slot<uint32_t XB_CDECL(uint32_t)>(0x00922dc0)(ent); }  // ScrActor_GetInt_184
void set_pose(uint32_t ent, uint32_t pose) { slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922dc4)(ent, pose); }  // ScrActor_SetInt_184
void end_on(uint32_t ent, uint32_t hash) { slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, hash); }  // Scr_EndOn
float random_range(float low, float high) {
    return static_cast<float>(slot<long double XB_CDECL(float, float)>(0x00922238)(low, high));  // Scr_RandomFloatRange
}
uint32_t name_hash(uint32_t text) {
    uint32_t hash;
    return *Scr_HashName(&hash, pointer<const char>(text));
}
// The current time as a script float (the undefined float for the undefined int).
float time_float() {
    const int32_t time = get_time();
    if (static_cast<uint32_t>(time) == undefined_int()) return title_global<float>(0x00922eac);
    return static_cast<float>(time);
}

// BroScr_DirectionalMoveAnimsLoop as a thread with the six move anims (pointers to the globals).
void directional_move_thread(uint32_t ent, const uint32_t* forward, const uint32_t* back, const uint32_t* left,
                             const uint32_t* right, const uint32_t* forward_right, const uint32_t* forward_left) {
    title_function<CallEnt6Anims>(0x001e53e0)(0, 0, 0, 0, 0x0019ff70, ent, forward, back, left, right, forward_right,
                                              forward_left);
}
void combat_run_thread(uint32_t ent, const uint32_t* forward) {
    directional_move_thread(ent, forward, pointer<const uint32_t>(0x008cda88), pointer<const uint32_t>(0x008cdac4),
                            pointer<const uint32_t>(0x008cdad0), pointer<const uint32_t>(0x008cdab8),
                            pointer<const uint32_t>(0x008cdaac));  // g_anim_combatrun_back, left, right, forwardright, forwardleft
}
void clear_combat_run(uint32_t ent) {
    slot<void XB_CDECL(uint32_t, uint32_t, float)>(0x009225d4)(ent, title_global<uint32_t>(0x008cda7c),
                                                               1.0f);  // GScr_ClearAnim(g_anim_combatrun)
}

// The actor's string +0x2c0 (ScrCall_ScrActor_GetString_2c0) is set; nothing for a non-AI.
bool has_string_2c0(uint32_t* ent) {
    if (!is_ai(*ent)) return false;
    ScriptString value;
    const bool has = ScrCall_ScrActor_GetString_2c0(ent, &value)->rep != nullptr;
    scr_release(&value);
    return has;
}
void play_sound(uint32_t ent, ScriptString* sound) { slot<PlaySound>(0x009221dc)(ent, sound, 0, 0, 0); }  // GScr_PlaySound
void play_sound_text(uint32_t ent, uint32_t text) {
    ScriptString sound;
    scr_string_from(&sound, text);
    play_sound(ent, &sound);
    scr_release(&sound);
}
// ScrCall_GScr_PlaySound(&ent, a temporary of `text`).
void play_sound_call(uint32_t* ent, uint32_t text) {
    ScriptString sound;
    scr_string_from(&sound, text);
    ScrCall_GScr_PlaySound(ent, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&sound)));
    scr_release(&sound);
}
// "bodyfall_" + the actor's string +0x2c0 + `suffix`, or `dirt` without one.
void bodyfall_sound(uint32_t* ent, uint32_t suffix, uint32_t dirt) {
    if (has_string_2c0(ent)) {
        ScriptString prefix;
        scr_string_from(&prefix, 0x00826258);  // "bodyfall_"
        ScriptString kind;
        ScriptString joined;
        ScriptString_Concat(&joined, &prefix, ScrCall_ScrActor_GetString_2c0(ent, &kind));
        ScriptString sound;
        ScrCall_GScr_PlaySound(ent, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(
                                        ScriptString_ConcatCString(&sound, &joined, pointer<const char>(suffix)))));
        scr_release(&sound);
        scr_release(&joined);
        scr_release(&kind);
        scr_release(&prefix);
    } else {
        play_sound_call(ent, dirt);
    }
}
void anim_mode(uint32_t ent, uint32_t text) {
    ScriptString mode;
    scr_string_from(&mode, text);
    slot<AnimMode>(0x0092273c)(ent, &mode);  // GScr_ActorSetAnimMode
    scr_release(&mode);
}
// The prone aim weights back (slot 0x009226b4 (ent, 1), then the shootfeet 45 up / down aim anims).
void prone_aim_off(uint32_t ent) {
    slot<EntFloat>(0x009226b4)(ent, 1.0f);
    slot<AimWeights>(0x009226bc)(ent, title_global<uint32_t>(0x008cf1d4), title_global<uint32_t>(0x008cf1c8), 1.0f,
                                 0.1f, 1.0f);  // GScr_ActorSetAimAnimWeights(straight45up, straight45down)
}
// Leaving prone for `pose`: "face default", the aim weights back, then the pose.
void pose_from_prone(uint32_t ent, uint32_t pose) {
    if (get_pose(ent) == kProne) {
        ScriptString mode;
        scr_string_from(&mode, 0x0083addc);  // "face default"
        slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t)>(0x00922740)(ent, &mode, 0,
                                                                                       0);  // GScr_ActorSetOrientMode
        scr_release(&mode);
        prone_aim_off(ent);
    }
    set_pose(ent, pose);
}
// ScrCall_ScrActor_SetInt_184(&ent, &pose).
void set_pose_call(uint32_t* ent, uint32_t pose) {
    const uint32_t value = pose;
    ScrCall_ScrActor_SetInt_184(ent, &value);
}
// Removes `key` from the entity's table (ScrCall_GScr_GetEntScriptObject_2).
void remove_key(const uint32_t* ent, uint32_t key) {
    title_function<void XB_THISCALL(const uint32_t*, uint32_t)>(0x0011eb30)(ent, key);
}
// GScr_Attach(ent, model, a temporary of `tag`).
void attach_with(uint32_t ent, ScriptString* model, ScriptString* tag) {
    slot<void XB_CDECL(uint32_t, ScriptString*, ScriptString*)>(0x009224d8)(ent, model, tag);  // GScr_Detach
    scr_release(tag);
}

// The move plays' BroScr_PlayStanceTransitionAnim calls.
void stand_run(uint32_t ent, uint32_t anim, float time) { BroScr_PlayStanceTransitionAnim(ent, anim, time, kStand, 0x0001c6ce, 0); }
void stand_walk(uint32_t ent, uint32_t anim, float time) { BroScr_PlayStanceTransitionAnim(ent, anim, time, kStand, 0x003a09eb, 1); }

void handle_note_tracks_forever_pass(ScriptString* note, uint32_t ent, uint32_t notify, uint32_t callback,
                                     const ScriptString* text) {
    ScriptString copy;
    ScriptString_Copy(&copy, text);
    ScriptString result;
    ScriptString_Assign(note, BroScr_HandleNoteTracks(&result, ent, notify, callback, copy));
    scr_release(&result);
}

}  // namespace

void XB_CDECL BroScr_PlayStanceTransitionAnim(uint32_t ent, uint32_t anim, float time, uint32_t pose,
                                              uint32_t movement, uint32_t aiming) {
    const float ms = time * title_global<float>(0x0083eb9c);  // 1000
    const int32_t end = static_cast<int32_t>(static_cast<float>(get_time()) + ms);
    slot<FlaggedKnobAll>(0x009225e4)(ent, 0x54e3b2b6, anim, body(), 1.0f, time, 1.0f, 0);  // GScr_SetFlaggedAnimKnobAll
    Scr_Wait(time * title_global<float>(0x0083eb24));  // time / 2
    set_pose(ent, pose);
    *handle_slot(ent, 0x0ccee5c1) = movement;  // anim_movement
    if (aiming)
        *handle_slot(ent, 0x764918ae) = 0xc4a729a7;  // anim_alertness
    else
        *handle_slot(ent, 0x764918ae) = 0xc028584c;
    Scr_Wait(static_cast<float>(end - get_time()) * title_global<float>(0x0083eb4c));  // ms -> s
}

void XB_CDECL BroScr_HeadLookMainLoop_Thunk(uint32_t ent) {
    own_function<void XB_CDECL(uint32_t)>(&BroScr_HeadLookMainLoop)(ent);  // BroScr_HeadLookMainLoop
}

void XB_CDECL FUN_001ac9f0(uint32_t ent) {
    end_on(ent, 0x06730ae8);  // death
    end_on(ent, 0x247afc7f);
    const float r = random_range(8.0f, 10.0f);
    const float half = static_cast<float>(
        slot<long double XB_CDECL(float)>(0x00922230)(r * title_global<float>(0x0083eb24)));  // Scr_RandomFloat(r / 2)
    get_time();
    Scr_Wait(r * title_global<float>(0x0083eb24) + half);
    const float low = r * title_global<float>(0x0083ed28);  // 0.7
    for (;;) {
        uint8_t looked;
        BroScr_TryLookAtPlayer(&looked, ent);
        Scr_Wait(random_range(low, r));
    }
}

void XB_CDECL BroScr_ForwardNotifyString(uint32_t ent, uint32_t notify) {
    ScriptString note;
    ScriptString_FromRep(&note, nullptr);
    title_function<WaitTillOutputStr>(0x004b14f0)(ent, notify, &note);  // Scr_WaitTillOutputStr_ORAN
    slot<void XB_CDECL(uint32_t, uint32_t, ScriptString*)>(0x009221ac)(ent, 0x71445fee,
                                                                      &note);  // Scr_QueueNotifyWithString
    scr_release(&note);
}

void XB_CDECL BroScr_StandCombatRun(uint32_t ent) {
    const float time = title_global<float>(0x0083eb20);  // 1
    uint8_t passed;
    bool defined;
    if (*BroScr_UpdateCombatDelay(&passed, ent)) {
        if (*ScrField_run_combatanim_IsDefined(&defined, ent)) {
            stand_run(ent, *ScrField_run_combatanim(ent), time);
            return;
        }
        clear_combat_run(ent);
        combat_run_thread(ent, ScrField_anim_combatrunanim(ent));
        stand_run(ent, title_global<uint32_t>(0x008cda7c), time);  // g_anim_combatrun
        return;
    }
    if (*ScrField_h93348a05_IsDefined(&defined, ent)) {
        stand_run(ent, *ScrField_h93348a05(ent), time);
        return;
    }
    clear_combat_run(ent);
    slot<Knob>(0x009225d8)(ent, title_global<uint32_t>(0x008cf0a8), 1.0f, 0.1f, 1.0f, 0);  // GScr_SetAnimKnob(precombatrun1)
    combat_run_thread(ent, pointer<const uint32_t>(0x008cda94));
    stand_run(ent, title_global<uint32_t>(0x008cda7c), time);  // g_anim_combatrun
}

void XB_CDECL BroScr_StandCombatWalk(uint32_t ent) {
    const float time = title_global<float>(0x0083f124);  // 1.6
    bool defined;
    uint8_t passed;
    if (*ScrField_walk_combatanim_IsDefined(&defined, ent) && *BroScr_UpdateCombatDelay(&passed, ent)) {
        const bool twitch = random_int(100) < 20;
        if (twitch && *ScrField_h30248704_IsDefined(&defined, ent)) {
            stand_walk(ent, *ScrField_h30248704(ent), time);
            return;
        }
        stand_walk(ent, *ScrField_walk_combatanim(ent), time);
        return;
    }
    if (*ScrField_h9dc74345_IsDefined(&defined, ent)) {
        const bool twitch = random_int(100) < 20;
        if (twitch && *ScrField_hb8e86881_IsDefined(&defined, ent)) {
            stand_walk(ent, *ScrField_hb8e86881(ent), time);
            return;
        }
        stand_walk(ent, *key_slot(ent, 0x9dc74345, 0), time);
        return;
    }
    const int32_t pick = random_int(100);
    if (pick < 50)
        stand_walk(ent, title_global<uint32_t>(0x008cf5e8), time);
    else if (pick < 80)
        stand_walk(ent, title_global<uint32_t>(0x008cf5f4), time);  // g_anim_stand_walk_combat_loop_02
    else
        stand_walk(ent, title_global<uint32_t>(0x008cf600), time);
}

void XB_CDECL BroScr_CrouchRun(uint32_t ent) {
    bool defined;
    if (*ScrField_h66973bee_IsDefined(&defined, ent)) {
        slot<KnobAll>(0x009225c8)(ent, *key_slot(ent, 0x66973bee, 0), body(), 1.0f, 0.4f, 1.0f, 0);  // GScr_SetAnimKnobAll
        return;
    }
    slot<Knob>(0x009225d8)(ent, *key_slot(ent, 0x682ea6e9, 0), 1.0f, 0.4f, 1.0f, 0);  // GScr_SetAnimKnob
    directional_move_thread(ent, pointer<const uint32_t>(0x008ce70c), pointer<const uint32_t>(0x008ce700),
                            pointer<const uint32_t>(0x008ce73c), pointer<const uint32_t>(0x008ce748),
                            pointer<const uint32_t>(0x008ce730),
                            pointer<const uint32_t>(0x008ce724));  // g_anim_crouchrun_loop_*
    BroScr_PlayStanceTransitionAnim(ent, title_global<uint32_t>(0x008ce6b8), title_global<float>(0x0083eba4), kCrouch,
                                    0x0001c6ce, 0);  // g_anim_crouchrun, 0.6 s
}

void XB_CDECL BroScr_CrouchWalkLoop(uint32_t ent) {
    bool defined;
    if (*ScrField_h66973bee_IsDefined(&defined, ent)) {
        slot<KnobAll>(0x009225c8)(ent, *key_slot(ent, 0x66973bee, 0), body(), 1.0f, 0.4f, 1.0f, 0);  // GScr_SetAnimKnobAll
        return;
    }
    BroScr_PlayStanceTransitionAnim(ent, title_global<uint32_t>(0x008ce760), title_global<float>(0x0083ebc0), kCrouch,
                                    0x003a09eb, 1);  // g_anim_crouchwalk_loop, 0.8 s
}

void XB_CDECL BroScr_DropWeapon(uint32_t ent) {
    using GetInt = uint32_t XB_CDECL(uint32_t);
    if (slot<GetInt>(0x00922d90)(ent) && slot<GetInt>(0x00922d30)(ent)) {  // ScrActor_GetInt_1378, ScrActor_GetInt_f0
        const int32_t ammo = random_int(50) + 75;
        const uint32_t name = slot<uint32_t XB_CDECL(uint32_t)>(0x00922a28)(slot<GetInt>(0x00922d30)(ent));  // the weapon's name
        ScriptString weapon_name;
        scr_string_from(&weapon_name, name);
        const uint32_t hand = *ScrField_anim_gunhand(ent);
        const uint32_t weapon = slot<uint32_t XB_CDECL(uint32_t, ScriptString*, uint32_t, float)>(0x009226f0)(
            ent, &weapon_name, hand, static_cast<float>(ammo));  // GScr_DropWeapon
        scr_release(&weapon_name);
        if (entity_handle_is_valid(weapon))
            title_function<BeginThreadFunctor1>(0x0011b4d0)(0, 0, 0, 0, 0x00184090, weapon);  // BroScr_DeleteAfter300
    }
    own_function<void XB_CDECL(uint32_t, uint32_t)>(&BroScr_PutGunInHand)(ent, 0x0036b1a5);  // BroScr_PutGunInHand
}

ScriptString* XB_CDECL BroScr_HandleNoteTracks(ScriptString* out, uint32_t ent, uint32_t notify, uint32_t callback,
                                              ScriptString text) {
    ScriptString note;
    ScriptString_FromRep(&note, nullptr);
    for (;;) {
        ScriptString_AssignCString(&note, pointer<const char>(0x007f9e88));  // "undefined"
        title_function<WaitTillOutputStr>(0x004b14f0)(ent, notify, &note);  // Scr_WaitTillOutputStr_ORAN
        uint32_t hash_out;
        const uint32_t h = *Scr_HashScriptString(&hash_out, reinterpret_cast<const uint8_t* const*>(&note));
        if (h == 0x000199a4 || h == 0x9aa82fee || h == 0xcc5542ad) break;
        if (h == 0x19785ffb || h == 0x00385cd0) {
            ScriptString sound;
            scr_string_from(&sound, 0x00826144);  // "step_run_"
            bool defined_9e8b;
            if (has_string_2c0(&ent)) {
                ScriptString kind;
                ScriptString_AppendString(&sound, ScrCall_ScrActor_GetString_2c0(&ent, &kind));
                scr_release(&kind);
            } else if (*ScrField_h9e8b61c4_IsDefined(&defined_9e8b, ent)) {
                ScriptString_AppendString(&sound, title_function<ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t)>(
                                                      0x00458da0)(ent_script_object(ent), 0x9e8b61c4));  // ScriptKeyTable_GetString
            } else {
                ScriptString_AppendCString(&sound, pointer<const char>(0x008331d4));  // "dirt"
            }
            play_sound(ent, &sound);
            scr_release(&sound);
        } else if (h == 0xe0042600) {
            if (has_string_2c0(&ent)) {
                ScriptString prefix;
                scr_string_from(&prefix, 0x008263a8);  // "step_scrape_"
                ScriptString kind;
                ScriptString sound;
                play_sound(ent, ScriptString_Concat(&sound, &prefix, ScrCall_ScrActor_GetString_2c0(&ent, &kind)));
                scr_release(&sound);
                scr_release(&kind);
                scr_release(&prefix);
            } else {
                play_sound_text(ent, 0x00826394);  // "step_scrape_dirt"
            }
        } else if (h == 0x162775e2) {
            *ScrField_hc786fa48(ent) = 0;
        } else if (h == name_hash(0x00826388)) {  // "reload done"
            BroScr_RefillClip(ent);
            *ScrField_hc786fa48(ent) = 0;
        } else if (h == name_hash(0x00826374)) {  // "anim_pose = stand"
            pose_from_prone(ent, kStand);
        } else if (h == name_hash(0x00826360)) {  // "anim_pose = crouch"
            pose_from_prone(ent, kCrouch);
        } else if (h == name_hash(0x0082634c)) {  // "anim_pose = prone"
            slot<void XB_CDECL(uint32_t, float, float, uint32_t, uint32_t, uint32_t)>(0x009226b8)(
                ent, -45.0f, 45.0f, title_global<uint32_t>(0x008cf12c), title_global<uint32_t>(0x008cf138),
                title_global<uint32_t>(0x008cf144));  // GScr_ActorSetPitchBlendAnims(prone_legsdown, legsstraight, legsup)
            slot<EntFloat>(0x009226b0)(ent, 1.0f);
            set_pose_call(&ent, kProne);
        } else if (h == name_hash(0x00826338)) {  // "anim_pose = back"
            if (get_pose(ent) == kProne) prone_aim_off(ent);
            set_pose_call(&ent, kBack);
        } else if (h == name_hash(0x00826320)) {  // "anim_movement = stop"
            *ScrField_anim_movement(ent) = 0x00385d90;
        } else if (h == name_hash(0x00826308)) {  // "anim_movement = walk"
            *ScrField_anim_movement(ent) = 0x003a09eb;
        } else if (h == name_hash(0x008262f4)) {  // "anim_movement = run"
            *ScrField_anim_movement(ent) = 0x0001c6ce;
        } else if (h == name_hash(0x008262e4)) {  // "anim_aiming = 1"
            *ScrField_anim_alertness(ent) = 0xc4a729a7;
        } else if (h == name_hash(0x008262d4)) {  // "anim_aiming = 0"
            *ScrField_anim_alertness(ent) = 0x06279a34;
        } else if (h == name_hash(0x008262bc)) {  // "anim_alertness = casual"
            *ScrField_anim_alertness(ent) = 0xc028584c;
        } else if (h == name_hash(0x008262a4)) {  // "anim_alertness = alert"
            *ScrField_anim_alertness(ent) = 0x06279a34;
        } else if (h == name_hash(0x0082628c)) {  // "anim_alertness = aiming"
            *ScrField_anim_alertness(ent) = 0xc4a729a7;
        } else if (h == 0xe2c992d7) {
            BroScr_DropWeapon(ent);
        } else if (h == name_hash(0x00826280)) {  // "gravity on"
            anim_mode(ent, 0x0083ad10);         // "gravity"
        } else if (h == name_hash(0x00826274)) {  // "gravity off"
            anim_mode(ent, 0x0083ac50);         // "nogravity"
        } else if (h == name_hash(0x00826264)) {  // "bodyfall large"
            bodyfall_sound(&ent, 0x00826250, 0x0082623c);  // "_large", "bodyfall_dirt_large"
        } else if (h == name_hash(0x0082622c)) {  // "bodyfall small"
            bodyfall_sound(&ent, 0x00826224, 0x00826210);  // "_small", "bodyfall_dirt_small"
        } else if (h == name_hash(0x008254ac)) {  // "anim_gunhand = left"
            is_ai(ent);
        } else if (h == name_hash(0x00825494)) {  // "anim_gunhand = right"
            is_ai(ent);
        } else if (h == name_hash(0x008261fc)) {  // "anim_gunhand = none"
            is_ai(ent);
        } else if (h == name_hash(0x008261e8)) {  // "attach clip right"
        } else if (h == name_hash(0x008261d4)) {  // "detach clip right"
        } else if (h == name_hash(0x008261c0)) {  // "anim_melee = right"
            *ScrField_anim_meleestate(ent) = 0x07151174;
        } else if (h == name_hash(0x008261ac)) {  // "anim_melee = left"
            *ScrField_anim_meleestate(ent) = 0x003798b4;
        } else if (h == name_hash(0x008261a0)) {  // "swish small"
            play_sound_call(&ent, 0x0082618c);  // "melee_swing_large"
        } else if (h == name_hash(0x00826180)) {  // "swish large"
            play_sound_call(&ent, 0x0082616c);  // "melee_swing_small"
        } else if (h == name_hash(0x00826150)) {  // "swap taghelmet to tagleft"
            bool defined;
            if (!*ScrField_haee61f94_IsDefined(&defined, ent)) continue;
            if (*ScrField_h3bfcf651_IsDefined(&defined, ent)) {
                ScriptString helmet_tag;
                scr_string_from(&helmet_tag, 0x00825e98);  // "TAG_HELMETSIDE"
                attach_with(ent, ScrField_h3bfcf651(ent), &helmet_tag);
                remove_key(&ent, 0x3bfcf651);
            }
            ScriptString no_tag;
            scr_string_from(&no_tag, 0x0082dfeb);  // ""
            attach_with(ent, ScrField_haee61f94(ent), &no_tag);
            ScriptString tag;
            scr_string_from(&tag, 0x007f644c);  // "TAG_WEAPON_LEFT"
            slot<void XB_CDECL(uint32_t, ScriptString*, ScriptString*, uint32_t)>(0x009224d0)(ent, ScrField_haee61f94(ent),
                                                                                             &tag, 0);  // GScr_AttachWithOwner
            scr_release(&tag);
            remove_key(&ent, 0xaee61f94);
        } else if (callback) {
            title_function<void XB_CDECL(uint32_t, uint32_t)>(callback)(ent, h);
        }
    }
    if (is_ai(ent) && get_pose(ent) == kBack) set_pose(ent, kCrouch);
    ScriptString_Copy(out, &note);
    scr_release(&note);
    scr_release(&text);
    return out;
}

void XB_CDECL BroScr_HandleNoteTracksForever(uint32_t ent, uint32_t notify, uint32_t end, uint32_t callback,
                                             ScriptString text) {
    end_on(ent, end);
    end_on(ent, 0x07b4e7f7);
    ScriptString note;
    ScriptString_FromRep(&note, nullptr);
    const float min_time = title_global<float>(0x0083edc8);  // 0.05
    for (;;) {
        const float start = time_float();
        handle_note_tracks_forever_pass(&note, ent, notify, callback, &text);
        const int32_t quick = min_time > static_cast<float>(get_time()) - start ? 1 : 0;
        if (static_cast<float>(quick) == title_global<float>(0x0083eafc)) continue;
        const float start2 = time_float();
        handle_note_tracks_forever_pass(&note, ent, notify, callback, &text);
        const float elapsed = static_cast<float>(get_time()) - start2;
        const int32_t quick2 = min_time > elapsed ? 1 : 0;
        if (static_cast<float>(quick2) == title_global<float>(0x0083eafc)) continue;
        get_time();
        Scr_Wait(min_time - elapsed);
    }
}

void XB_CDECL BroScr_HandleNoteTracksForTime(uint32_t ent, float time, uint32_t notify, uint32_t callback,
                                             ScriptString text) {
    const uint32_t end = 0x6cd0168f;
    title_function<CallVt00825c50>(0x001e5480)(0, 0, 0, 0, 0x001aeac0, ent, &notify, &end, &callback,
                                               &text);  // BroScr_HandleNoteTracksForever
    Scr_Wait(time);
    queue_notify(ent, 0x6cd0168f);
    scr_release(&text);
}

void XB_CDECL BroScr_UnarmedCrouchIdle(uint32_t ent) {
    using FlaggedKnobAllRate = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, float, float, uint32_t, uint32_t);
    const uint32_t rate = *key_slot(ent, 0x6937c3b9, undefined_float());
    slot<FlaggedKnobAllRate>(0x009225e4)(ent, 0x80bd2428, title_global<uint32_t>(0x008cf690), body(), 1.0f, 0.1f, rate,
                                         0);  // GScr_SetFlaggedAnimKnobAll(unarmed_crouch_idle1)
    ScriptString empty;
    ScriptString_Copy(&empty, pointer<ScriptString>(0x00922e90));
    ScriptString result;
    BroScr_HandleNoteTracks(&result, ent, 0x80bd2428, 0, empty);
    scr_release(&result);
    if (random_int(100) >= 60) return;
    const uint32_t twitch_rate = *ScrField_h6937c3b9(ent);
    slot<FlaggedKnobAllRate>(0x009225e4)(ent, 0x80bd2428, title_global<uint32_t>(0x008cf69c), body(), 1.0f, 0.1f,
                                         twitch_rate, 0);  // unarmed_crouch_twitch1
    ScriptString empty2;
    ScriptString_Copy(&empty2, pointer<ScriptString>(0x00922e90));
    ScriptString result2;
    BroScr_HandleNoteTracks(&result2, ent, 0x80bd2428, 0, empty2);
    scr_release(&result2);
}

}  // namespace game
