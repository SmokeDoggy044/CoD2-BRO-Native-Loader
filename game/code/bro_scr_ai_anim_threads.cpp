#include "bro_scr_ai_anim_threads.h"

#include <cstring>

#include "bro_scr_actor.h"
#include "bro_scr_anim.h"
#include "bro_scr_ai.h"
#include "bro_scr_ai_anim.h"
#include "bro_scr_ai_common.h"
#include "bro_scr_ai_look.h"
#include "bro_scr_ai_weapons.h"
#include "bro_scr_nodes_spawner.h"
#include "bro_scr_note_handlers.h"
#include "bro_scr_notify_threads.h"
#include "bro_scr_common.h"
#include "bro_scr_cover_turret.h"
#include "bro_scr_friendly_threads.h"
#include "g_scr_runtime.h"
#include "g_script.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "bro_scr_stubs.h"
#include "scr_access.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "script_string.h"
#include "title_data.h"

namespace game {

using namespace bro_ai;

namespace {

using KnobAllNoFlag = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using KnobAll = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
// The Scr_CallThread_* starts: four zero words, the thread function, the entity, pointers to the values.
using CallVt00825c50 = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                         const uint32_t*, const uint32_t*, const ScriptString*);
using CallFHashHashHashI = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const float*,
                                             const uint32_t*, const uint32_t*, const uint32_t*, const uint32_t*);
using CallHashHashHash = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                           const uint32_t*, const uint32_t*);
using CallHashHashF = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                        const uint32_t*, const float*);
// A recorded anim command (AnimCommand, 13 words, the array under key 0x1791f035), passed by value.
using ApplyAnimCommand = void XB_CDECL(uint32_t, AnimCommand);
using StandCombatRun = void XB_CDECL(uint32_t);
using BeginThreadFunctor1 = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using Knob6 = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using CallEntHashI = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                       const uint32_t*);
using CallEntI = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*);
using CallEntEntI = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                      const uint32_t*);
using GetRef = uint32_t* XB_CDECL(uint32_t*, uint32_t);
using CallEntEnt = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*);
using FromCStringPlusInt = ScriptString* XB_CDECL(ScriptString*, uint32_t, uint32_t);
using HandleNoteTracks = ScriptString* XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, ScriptString);
using HandleNoteTracksForTime = void XB_CDECL(uint32_t, float, uint32_t, uint32_t, ScriptString);

// BroScr_HandleNoteTracks(out, ent, notify, callback, a copy of the empty string); the result is released.
inline void handle_note_tracks(uint32_t ent, uint32_t notify, uint32_t callback = 0) {
    ScriptString empty;
    ScriptString_Copy(&empty, pointer<ScriptString>(0x00922e90));
    ScriptString result;
    title_function<HandleNoteTracks>(0x001ada20)(&result, ent, notify, callback, empty);  // BroScr_HandleNoteTracks (agent-b list)
    scr_release(&result);
}

inline long double anim_length(uint32_t ent, uint32_t anim) {
    return slot<long double XB_CDECL(uint32_t, uint32_t)>(0x009225cc)(ent, anim);  // GScr_GetAnimLength
}

// Begins a critical section the actor is not already in; whether it did.
inline bool begin_critical(uint32_t ent, uint32_t critical) {
    if (!critical) return false;
    const uint32_t in = slot<uint32_t XB_CDECL(uint32_t)>(0x00922704)(ent);  // GScr_ActorInCriticalSection
    if (in) return false;
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009226fc)(ent, in);  // GScr_ActorBeginCriticalSection
    return true;
}

// GScr_ActorSetOrientMode(ent, text, 0, 0) with a temporary string.
inline void set_orient_mode(uint32_t ent, uint32_t text) {
    ScriptString mode;
    scr_string_from(&mode, text);
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t)>(0x00922740)(ent, &mode, 0, 0);
    scr_release(&mode);
}
inline uint32_t get_int_184(uint32_t ent) { return slot<uint32_t XB_CDECL(uint32_t)>(0x00922dc0)(ent); }  // ScrActor_GetInt_184
inline void set_int_184(uint32_t ent, uint32_t value) { slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922dc4)(ent, value); }
inline long double normal_health(uint32_t ent) { return slot<long double XB_CDECL(uint32_t)>(0x00922550)(ent); }  // GScr_GetNormalHealth
inline uint32_t anim_after() { return title_global<uint32_t>(0x00922ea8); }  // the whole-body anim the wounded plays pass

// The look-loop counter 0x747cde6c: 0 when undefined (`inlined`: the find/set inline, else through
// the ScrField accessor), then + 1; notifies `kill` (ending the previous loop) and ends on death / kill.
inline void bump_look_counter(uint32_t ent, uint32_t kill, bool inlined) {
    bool defined;
    if (!*ScrField_h747cde6c_IsDefined(&defined, ent)) {
        if (inlined)
            *int_slot(ent, 0x747cde6c) = 0;
        else
            *ScrField_h747cde6c(ent) = 0;
    }
    const uint32_t count = *int_slot(ent, 0x747cde6c) + 1;
    *int_slot(ent, 0x747cde6c) = count;
    queue_notify(ent, kill);
}

// GScr_ActorSetOrientMode(ent, "face angle", the node's or actor's yaw (0x00184310), 0).
inline void orient_face_angle(uint32_t ent) {
    uint32_t out;
    const uint32_t yaw = *title_function<GetRef>(0x00184310)(&out, ent);  // BroScr_GetNodeOrActorYaw
    ScriptString mode;
    scr_string_from(&mode, 0x0083adfc);  // "face angle"
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t)>(0x00922740)(ent, &mode, yaw, 0);
    scr_release(&mode);
}
inline uint32_t weapon_type(uint32_t ent) {
    uint32_t out;
    return *BroScr_GetWeaponType(&out, ent);
}
// BroScr_IsOneOf3(value, the three words at anim object + offset).
inline bool is_one_of3(uint32_t value, uint32_t offset) {
    const uint32_t* const set = reinterpret_cast<const uint32_t*>(anim_object() + offset);
    uint8_t out;
    return *BroScr_IsOneOf3(&out, value, set[0], set[1], set[2]) != 0;
}
// A face-wait record (28 bytes, the array field 0x053fe992): constructor 0x0045bcd0, destructor 0x00458fd0.
struct FaceWaitRecord {
    uint32_t anim;
    ScriptString sound;
    uint32_t notify;
    ScriptString kind;
    uint32_t time;   // a script int
    uint32_t id;
    uint32_t level;  // a float
};
static_assert(sizeof(FaceWaitRecord) == 0x1c);
using CallEntStrHashHash = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const ScriptString*,
                                            const uint32_t*, const uint32_t*);
using CallEntFHashHash = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                          const uint32_t*, const uint32_t*);
using SetAnim7 = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using GetString = ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t);
using RemoveKey = void XB_THISCALL(const uint32_t*, uint32_t);

// The face-wait records (ScriptKeyTable_GetOrAddArray_6).
inline DynArray* face_waits(uint32_t ent) {
    return pointer<DynArray>(ScriptKeyTable_GetOrAddArray_6(ent_script_object(ent), 0x053fe992));
}
// The same through ScriptKeyTable_Find, added empty (ScriptKeyTable_SetNewArray_2) when missing.
inline DynArray* face_waits_found(uint32_t ent) {
    ScriptKeyTable* const table = ent_script_object(ent);
    uint32_t* value = ScriptKeyTable_Find(table, 0x053fe992);
    if (!value) {
        const uint32_t empty[3] = {0, 0, 0};
        value = ScriptKeyTable_SetNewArray_2(table, 0x053fe992, empty);
    }
    return pointer<DynArray>(*value);
}
// Record i, the array grown to (i + 1, i + 4) first when i is past the count.
inline FaceWaitRecord* face_wait_at(DynArray* waits, uint32_t i) {
    if (i >= waits->size) DynArray_Reserve28(waits, i + 1, i + 4);
    return reinterpret_cast<FaceWaitRecord*>(waits->data) + i;
}
// The inlined int field test: a valid entity with a script object whose value is set (missing: 0).
inline bool int_set(uint32_t ent, uint32_t key) {
    if (!entity_handle_is_valid(ent)) return false;
    ScriptKeyTable* const table = title_function<ScriptKeyTable* XB_CDECL(uint32_t)>(0x0011db20)(ent);
    if (!table) return false;
    const uint32_t* const value = ScriptKeyTable_Find(table, key);
    return (value ? *value : 0) != 0;
}
using SetAnim6 = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
// Scr_BeginThreadFunctor1(1, 0, 0, 0, function, ent): an anim script as a begun thread.
inline void begin_anim_thread(uint32_t ent, uint32_t function) {
    title_function<BeginThreadFunctor1>(0x0011b4d0)(1, 0, 0, 0, function, ent);
}
// The level object's handle word (g_levelScriptObject + 4; a null object reads address 0).
inline uint32_t level_handle() {
    const uint32_t object = title_global<uint32_t>(0x00922ef8);
    return *pointer<uint32_t>(object ? object + 4 : 0);
}
// Whether the sentient's team is "axis".
inline bool team_is_axis(uint32_t ent) {
    ScriptString team;
    const bool axis = ScriptString_Equals(ScrCall_ScrSentient_GetTeam(&ent, &team), pointer<const char>(0x0083208c));
    scr_release(&team);
    return axis;
}
// The alert-to-aim plays: BroScr_PlayAnimWithNoteTracks(ent, anim, pose, move 0x385d90, 1, critical 1, after).
inline void play_alert_anim(uint32_t ent, uint32_t anim, uint32_t pose) {
    BroScr_PlayAnimWithNoteTracks(ent, anim, pose, 0x00385d90, 1, 1, anim_after());
}
// The cover plays: BroScr_PlayAnimWithNoteTracks(ent, anim, pose, move 0x385d90, 0, critical 1, after).
inline void play_cover_anim(uint32_t ent, uint32_t anim, uint32_t pose) {
    BroScr_PlayAnimWithNoteTracks(ent, anim, pose, 0x00385d90, 0, 1, anim_after());
}

// The turret pose scripts: desired pose `pose` (key 0x8cb5047a) applied (BroScr_ApplyDesiredAnimPose),
// then `then` (when not 0) as the desired pose, move 0x385d90 (key 0xccee5c1); with a turret:
// call threads `tree` on the turret (with the actor) and `call` on the actor (with the turret).
inline void set_turret_pose(uint32_t ent, uint32_t pose, uint32_t then, uint32_t tree, uint32_t call) {
    *handle_slot(ent, 0x8cb5047a) = pose;
    BroScr_ApplyDesiredAnimPose(ent);
    if (then) *handle_slot(ent, 0x8cb5047a) = then;
    *handle_slot(ent, 0x0ccee5c1) = 0x00385d90;
    uint32_t turret = slot<uint32_t XB_CDECL(uint32_t)>(0x0092274c)(ent);  // GScr_ActorGetTurret
    if (!entity_handle_is_valid(turret)) return;
    title_function<CallEntEnt>(0x004b09e0)(0, 0, 0, 0, tree, turret, &ent);  // Scr_CallThread_ent_ent_ORAN
    title_function<CallEntEnt>(0x004b09e0)(0, 0, 0, 0, call, ent, &turret);
}

// Whether a combat walk / stand anim takes over (walk_combatanim with a combat delay passed,
// h9dc74345 without one, standanim when `stand_anim`).
inline bool combat_anim_takes_over(uint32_t ent, bool stand_anim) {
    bool defined;
    uint8_t passed;
    if (*ScrField_walk_combatanim_IsDefined(&defined, ent) && *BroScr_UpdateCombatDelay(&passed, ent)) return true;
    if (*ScrField_h9dc74345_IsDefined(&defined, ent) && !*BroScr_UpdateCombatDelay(&passed, ent)) return true;
    return stand_anim && *ScrField_standanim_IsDefined(&defined, ent);
}
// The idle set (key 0xbfc60f36) 0x61 or 0x62 at random.
inline void random_idle_set(uint32_t ent) {
    if (random_int(100) < 50)
        *handle_slot(ent, 0xbfc60f36) = 0x61;
    else
        *handle_slot(ent, 0xbfc60f36) = 0x62;
}
// BroScr_StandWalkToStop(_2): the stop anim by the walk cycle's time (rff before half way, else lff),
// or just the move 0x385d90 when a combat anim takes over.
inline void stand_walk_to_stop(uint32_t ent, bool stand_anim) {
    random_idle_set(ent);
    const float time = static_cast<float>(slot<long double XB_CDECL(uint32_t, uint32_t)>(0x009225ec)(
        ent, title_global<uint32_t>(0x008cf6c0)));  // GScr_GetAnimTime(g_anim_walk_and_run_loops)
    if (combat_anim_takes_over(ent, stand_anim)) {
        *handle_slot(ent, 0x0ccee5c1) = 0x00385d90;
        return;
    }
    const int32_t early = title_global<float>(0x0083eb24) > time ? 1 : 0;  // 0.5
    if (static_cast<float>(early) != title_global<float>(0x0083eafc))
        BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008cf624), 0x070b89a4, 0x00385d90, 1, 1,
                                      anim_after());  // g_anim_stand_walk_stop_rff
    else
        BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008cf618), 0x070b89a4, 0x00385d90, 1, 1,
                                      anim_after());  // g_anim_stand_walk_stop_lff
}
// BroScr_CrouchWalkToCrouchStop(_2) (two identical copies).
inline void crouch_walk_to_stop(uint32_t ent) {
    random_idle_set(ent);
    bool defined;
    if (*ScrField_h66973bee_IsDefined(&defined, ent)) {
        *handle_slot(ent, 0x0ccee5c1) = 0x00385d90;
        return;
    }
    if (weapon_type(ent) == 0x0036b1a5) {
        *ScrField_anim_movement(ent) = 0x00385d90;
        return;
    }
    BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008ce76c), 0xc1165808, 0x00385d90, 1, 1,
                                  title_global<uint32_t>(0x008ce520));  // g_anim_crouchwalk_stop_rff, then crouch_aim_straight
}

// The prone body: legs straight (blend `time`) on the whole body, then the shoot-feet aim weights;
// `pitch`: first the prone pitch blends (-45..45 legs down/straight/up) and 0x009226b0(1).
inline void prone_legs(uint32_t ent, uint32_t time) {
    slot<KnobAllNoFlag>(0x009225c8)(ent, title_global<uint32_t>(0x008cf138), title_global<uint32_t>(0x008cd7dc),
                                    0x3f800000, time, 0x3f800000, 0);  // GScr_SetAnimKnobAll(g_anim_prone_legsstraight)
}
inline void prone_aim_weights(uint32_t ent) {
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009226bc)(
        ent, title_global<uint32_t>(0x008cf1d4), title_global<uint32_t>(0x008cf1c8), 0x3f800000, 0x3dcccccd,
        0x3f800000);  // GScr_ActorSetAimAnimWeights(prone shootfeet 45 up / down)
}
inline void prone_pitch(uint32_t ent) {
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009226b8)(
        ent, 0xc2340000, 0x42340000, title_global<uint32_t>(0x008cf12c), title_global<uint32_t>(0x008cf138),
        title_global<uint32_t>(0x008cf144));  // GScr_ActorSetPitchBlendAnims(-45, 45, prone legs down/straight/up)
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009226b0)(ent, 0x3f800000);
}
// Into prone: BroScr_PlayAnimWithNoteTracks(ent, anim, prone, move 0x385d90, 1, 1, after).
inline void play_to_prone(uint32_t ent, uint32_t anim) {
    BroScr_PlayAnimWithNoteTracks(ent, anim, 0x0738b1a5, 0x00385d90, 1, 1, anim_after());
}

// The wounded pose key 0xb7f3f874: 0x6088a8c crawling, 0x68698d3 kneeling, 0x70b89a4 standing.
inline uint32_t* wounded_pose(uint32_t ent) { return handle_slot(ent, 0xb7f3f874); }

}  // namespace

void XB_CDECL BroScr_DebugPrintTimesThrough(uint32_t ent, uint32_t n) {
    ScriptString text;
    title_function<FromCStringPlusInt>(0x0011a7d0)(&text, 0x00826434, n);  // "Times through: "
    const uint32_t a = 0x6cd13551;
    const uint32_t b = 0x6d4893a9;
    const uint32_t c = 0;
    title_function<CallVt00825c50>(0x001e5480)(0, 0, 0, 0, 0x001aeac0, ent, &a, &b, &c, &text);  // Scr_CallThread_vt00825c50(BroScr_HandleNoteTracksForever)
    scr_release(&text);
}

void XB_CDECL BroScr_StandCombatRun_2(uint32_t ent) {
    title_function<StandCombatRun>(0x001acd90)(ent);  // BroScr_StandCombatRun (agent-b list)
}

void XB_CDECL BroScr_PlayPoseTransitionAnim(uint32_t ent, uint32_t anim, uint32_t a3, uint32_t a4, uint32_t flag,
                                            uint32_t critical, uint32_t after) {
    const bool began = begin_critical(ent, critical);
    slot<KnobAll>(0x009225e4)(ent, 0x8d403614, anim, title_global<uint32_t>(0x008cd7dc), 0x3f800000, 0x3f800000,
                              0x3f800000, 0);  // GScr_SetFlaggedAnimKnobAll(g_anim_Body)
    handle_note_tracks(ent, 0x8d403614);
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922dc4)(ent, a3);  // ScrActor_SetInt_184
    *handle_slot(ent, 0x0ccee5c1) = a4;
    *handle_slot(ent, 0x764918ae) = flag ? 0xc4a729a7 : 0xc028584c;
    if (after)
        slot<KnobAllNoFlag>(0x009225c8)(ent, after, title_global<uint32_t>(0x008cd7dc), 0x3f800000, 0, 0x3f800000,
                                        0);  // GScr_SetAnimKnobAll(g_anim_Body)
    if (began) slot<void XB_CDECL(uint32_t)>(0x00922700)(ent);  // GScr_ActorEndCriticalSection
}

void XB_CDECL BroScr_PlayAnimWithNoteTracks(uint32_t ent, uint32_t anim, uint32_t a3, uint32_t a4, uint32_t flag,
                                            uint32_t critical, uint32_t after) {
    const bool began = begin_critical(ent, critical);
    const uint32_t notify = 0x97d5fe54;
    const float half = static_cast<float>(anim_length(ent, anim) * title_global<float>(0x0083eb24));  // 0.5
    title_function<CallFHashHashHashI>(0x001e5530)(0, 0, 0, 0, 0x001a05f0, ent, &half, &notify, &a3, &a4,
                                     &flag);  // Scr_CallThread_ent_f_hash_hash_hash_i_2(BroScr_ProneAimFaceDefault)
    slot<KnobAll>(0x009225e4)(ent, 0x8d403617, anim, title_global<uint32_t>(0x008cd7dc), 0x3f800000, 0x3f800000,
                              0x3f800000, 0);  // GScr_SetFlaggedAnimKnobAll(g_anim_Body)
    handle_note_tracks(ent, 0x8d403617);
    queue_notify(ent, 0x97d5fe54);
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922dc4)(ent, a3);  // ScrActor_SetInt_184
    *handle_slot(ent, 0x0ccee5c1) = a4;
    *handle_slot(ent, 0x764918ae) = flag ? 0xc4a729a7 : 0xc028584c;
    if (after)
        slot<KnobAllNoFlag>(0x009225c8)(ent, after, title_global<uint32_t>(0x008cd7dc), 0x3f800000, 0, 0x3f800000,
                                        0);  // GScr_SetAnimKnobAll(g_anim_Body)
    if (began) slot<void XB_CDECL(uint32_t)>(0x00922700)(ent);  // GScr_ActorEndCriticalSection
}

void XB_CDECL BroScr_PlayAnimWaitEndNotes(uint32_t ent, uint32_t anim) {
    if (!anim) return;
    slot<KnobAll>(0x009225e4)(ent, 0xc5a136d6, anim, title_global<uint32_t>(0x008cf300), 0x3f800000, 0x3dcccccd,
                              0x3f800000, 1);  // GScr_SetFlaggedAnimKnobAll(g_anim_root)
    const float length = static_cast<float>(anim_length(ent, anim));
    const uint32_t end = 0xf8a618a1;
    const float timeout = length * title_global<float>(0x0083ec9c) + title_global<float>(0x0083eb20);  // x 3 + 1
    const uint32_t notify = 0xc5a136d6;
    title_function<CallHashHashHash>(0x0052c8a0)(0, 0, 0, 0, 0x001b2e20, ent, &notify, &end,
                                      &end);  // Scr_CallThread_ent_hash_hash_hash_KASSPASS(BroScr_WaitThreeEndNotes)
    title_function<CallHashHashF>(0x001e55c0)(0, 0, 0, 0, 0x00184b30, ent, &end, &end,
                                      &timeout);  // Scr_CallThread_ent_hash_hash_f_5(BroScr_NotifyAfterTime_3)
    Scr_WaitTill(ent, end);
    queue_notify(ent, 0x47e458b4);
}
void XB_CDECL BroScr_PlayFaceAnim_3(uint32_t ent, ScriptString unused) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, 0x07b4e7f7);  // Scr_EndOn
    float kneel_time = 0.0f;
    while (get_int_184(ent) == 0x19669cd9) {
        if (*wounded_pose(ent) == 0x06088a8c) {
            set_orient_mode(ent, 0x0083adec);  // "face current"
            BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008cf6f0), 0x19669cd9, 0x00385d90, 0, 1,
                                          anim_after());  // g_anim_wounded_crawl2knees_forward
            *wounded_pose(ent) = 0x068698d3;
        } else if (*wounded_pose(ent) == 0x068698d3) {
            if (normal_health(ent) > title_global<float>(0x0083eca0)) {  // 0.2
                set_int_184(ent, 0xc1165808);
                continue;
            }
            set_orient_mode(ent, 0x0083adec);  // "face current"
            uint32_t yaw;
            const uint32_t yaw_bits = *BroScr_GetAbsYawToTarget(&yaw, ent);
            const int32_t facing = title_global<float>(0x0083ebc4) > as_float(yaw_bits) ? 1 : 0;  // 25
            if (static_cast<float>(facing) != title_global<float>(0x0083eafc)) {  // 0
                slot<KnobAll>(0x009225e4)(ent, 0xb5377cad, title_global<uint32_t>(0x008cf738),
                                          title_global<uint32_t>(0x008cd7dc), 0x3f800000, 0x3d4ccccd, 0x3f800000,
                                          1);  // g_anim_wounded_kneesshoot_straight
                slot<void XB_CDECL(uint32_t, uint32_t)>(0x0092269c)(ent, 0x3f800000);  // GScr_ActorShoot
                const uint32_t clip = *int_slot(ent, 0x0feddbea);
                *int_slot(ent, 0x0feddbea) = clip - 1;
                handle_note_tracks(ent, 0xb5377cad);
            } else {
                const float scaled = kneel_time * title_global<float>(0x007bd0c8);  // 4
                if (normal_health(ent) > title_global<float>(0x0083eb24) - scaled) {  // 0.5
                    BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008cf720), 0x070b89a4, 0x00385d90, 0,
                                                  1, anim_after());  // g_anim_wounded_knees2stand
                } else {
                    slot<KnobAll>(0x009225e4)(ent, 0xcda48b8c, title_global<uint32_t>(0x008cf72c),
                                              title_global<uint32_t>(0x008cd7dc), 0x3f800000, 0x3d4ccccd, 0x3f800000,
                                              0);  // g_anim_wounded_kneesidle
                    ScriptString empty;
                    ScriptString_Copy(&empty, pointer<ScriptString>(0x00922e90));
                    title_function<HandleNoteTracksForTime>(0x001aeca0)(ent, title_global<float>(0x0083eca0),
                                                                        0xcda48b8c, 0, empty);  // (agent-b list)
                    kneel_time = kneel_time + title_global<float>(0x0083eca0);  // 0.2
                }
            }
        } else if (*wounded_pose(ent) == 0x070b89a4) {
            set_int_184(ent, 0x070b89a4);
        }
    }
    set_orient_mode(ent, 0x0083addc);  // "face default"
    scr_release(&unused);
}

void XB_CDECL BroScr_PlayFaceAnim(uint32_t ent, ScriptString unused) {
    while (get_int_184(ent) == 0x19669cd9) {
        set_orient_mode(ent, 0x0083adec);  // "face current"
        if (*wounded_pose(ent) == 0x06088a8c) {
            const float threshold =
                static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922230)(0x3ecccccd) +
                                   title_global<float>(0x0083ebf8));  // Scr_RandomFloat(0.4) + 0.1
            const int32_t above = normal_health(ent) > threshold ? 1 : 0;
            if (static_cast<float>(above) != title_global<float>(0x0083eafc)) {  // 0
                BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008cf6f0), 0x19669cd9, 0x00385d90, 0, 1,
                                              anim_after());  // g_anim_wounded_crawl2knees_forward
                *wounded_pose(ent) = 0x068698d3;
            } else {
                slot<KnobAll>(0x009225e4)(ent, 0x54e3b2b6, title_global<uint32_t>(0x008cf714),
                                          title_global<uint32_t>(0x008cd7dc), 0x3f800000, 0x3dcccccd, 0x3f800000,
                                          1);  // g_anim_wounded_crawlidle
                handle_note_tracks(ent, 0x54e3b2b6);
            }
        } else if (*wounded_pose(ent) == 0x068698d3) {
            const float threshold =
                static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922230)(0x3ecccccd) +
                                   title_global<float>(0x0083ebf8));  // Scr_RandomFloat(0.4) + 0.1
            const int32_t above = normal_health(ent) > threshold ? 1 : 0;
            if (static_cast<float>(above) != title_global<float>(0x0083eafc)) {  // 0
                BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008cf720), 0x070b89a4, 0x00385d90, 0, 1,
                                              anim_after());  // g_anim_wounded_knees2stand
            } else {
                slot<KnobAll>(0x009225e4)(ent, 0x54e3b2b6, title_global<uint32_t>(0x008cf72c),
                                          title_global<uint32_t>(0x008cd7dc), 0x3f800000, 0x3dcccccd, 0x3f800000,
                                          1);  // g_anim_wounded_kneesidle
                handle_note_tracks(ent, 0x54e3b2b6);
            }
        } else if (*wounded_pose(ent) == 0x070b89a4) {
            set_int_184(ent, 0x070b89a4);
        }
    }
    set_orient_mode(ent, 0x0083addc);  // "face default"
    scr_release(&unused);
}

void XB_CDECL BroScr_PlayFaceAnim_2(uint32_t ent, ScriptString unused, uint32_t pose) {
    ScriptString name;
    ScrCall_ScrActor_GetString_1384(&ent, &name);  // read and dropped
    if (pose == undefined_handle() || !pose) pose = 0x070b89a4;
    while (get_int_184(ent) == 0x19669cd9) {
        set_orient_mode(ent, 0x0083adec);  // "face current"
        if (*wounded_pose(ent) == 0x06088a8c) {
            if (pose == 0x0738b1a5) {
                slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009226b8)(
                    ent, 0xc2340000, 0x42340000, title_global<uint32_t>(0x008cf12c), title_global<uint32_t>(0x008cf138),
                    title_global<uint32_t>(0x008cf144));  // GScr_ActorSetPitchBlendAnims(-45, 45, prone legs down/straight/up)
                slot<void XB_CDECL(uint32_t, uint32_t)>(0x009226b0)(ent, 0x3f000000);  // 0.5
                set_int_184(ent, pose);
            } else {
                BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008cf6f0), 0x19669cd9, 0x00385d90, 0, 1,
                                              anim_after());  // g_anim_wounded_crawl2knees_forward
                *wounded_pose(ent) = 0x068698d3;
            }
        } else if (*wounded_pose(ent) == 0x068698d3) {
            if (pose == 0xc1165808 || pose == 0x0738b1a5)
                set_int_184(ent, 0xc1165808);
            else
                BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008cf720), 0x070b89a4, 0x00385d90, 0, 1,
                                              anim_after());  // g_anim_wounded_knees2stand
        } else if (*wounded_pose(ent) == 0x070b89a4) {
            set_int_184(ent, 0x070b89a4);
        } else {
            scr_release(&name);  // leaves without "face default"
            scr_release(&unused);
            return;
        }
    }
    set_orient_mode(ent, 0x0083addc);  // "face default"
    scr_release(&name);
    scr_release(&unused);
}

void XB_CDECL BroScr_WoundedWalk(uint32_t ent, ScriptString unused) {
    {
        ScriptString name;
        ScriptString_Copy(&name, reinterpret_cast<const ScriptString*>(anim_object() + 0x98));
        scr_release(&name);
    }
    using MotionYaw = long double XB_CDECL(uint32_t);
    while (get_int_184(ent) == 0x19669cd9) {
        if (*wounded_pose(ent) == 0x06088a8c) {
            if (*handle_slot(ent, 0x0ccee5c1) == 0x00385d90) {
                slot<KnobAll>(0x009225e4)(ent, 0xa8f155c5, title_global<uint32_t>(0x008cf708),
                                          title_global<uint32_t>(0x008cd7dc), 0x3f800000, 0x3dcccccd, 0x3f800000,
                                          1);  // g_anim_wounded_crawl_forward_loop
                Scr_Wait(as_float(0x3d4ccccd));  // 0.05
                if (title_global<float>(0x0083ee38) > slot<MotionYaw>(0x00922744)(ent) &&  // 30, GScr_ActorGetMotionYaw
                    slot<MotionYaw>(0x00922744)(ent) > title_global<float>(0x007f6a08)) {   // -30
                    *handle_slot(ent, 0x0ccee5c1) = 0x003a09eb;
                    set_orient_mode(ent, 0x0083adec);  // "face current"
                    handle_note_tracks(ent, 0xa8f155c5);
                } else {
                    BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008cf6f0), 0x19669cd9, 0x00385d90, 0,
                                                  1, anim_after());  // g_anim_wounded_crawl2knees_forward
                    *wounded_pose(ent) = 0x068698d3;
                    BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008cf720), 0x070b89a4, 0x00385d90, 0,
                                                  1, anim_after());  // g_anim_wounded_knees2stand
                }
            } else {
                set_orient_mode(ent, 0x0083adec);  // "face current"
                slot<KnobAll>(0x009225e4)(ent, 0x54e3b2b6, title_global<uint32_t>(0x008cf6fc),
                                          title_global<uint32_t>(0x008cd7dc), 0x3f800000, 0x3dcccccd, 0x3f800000,
                                          1);  // g_anim_wounded_crawl2stumble
                handle_note_tracks(ent, 0x54e3b2b6, 0x0018e2b0);  // BroScr_SetOrientFaceMotionIf
                set_int_184(ent, 0x070b89a4);
                *handle_slot(ent, 0x0ccee5c1) = 0x0001c6ce;
                set_orient_mode(ent, 0x0083adec);  // "face current"
            }
        } else if (*wounded_pose(ent) == 0x068698d3) {
            BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008cf720), 0x070b89a4, 0x00385d90, 0, 1,
                                          anim_after());  // g_anim_wounded_knees2stand
        } else if (*wounded_pose(ent) == 0x070b89a4) {
            if (*handle_slot(ent, 0x0ccee5c1) == 0x00385d90)
                BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008cf774), 0x19669cd9, 0x0001c6ce, 0, 1,
                                              anim_after());  // g_anim_wounded_stand2run_forward
            slot<KnobAll>(0x009225e4)(ent, 0x5fa3c33d, title_global<uint32_t>(0x008cf66c),
                                      title_global<uint32_t>(0x008cd7dc), 0x3f800000, 0x3dcccccd, 0x3f800000,
                                      0);  // g_anim_stumble1
            set_orient_mode(ent, 0x0083adec);  // "face current"
            handle_note_tracks(ent, 0x5fa3c33d);
            *handle_slot(ent, 0x0ccee5c1) = 0x0001c6ce;
            slot<KnobAllNoFlag>(0x009225c8)(ent, title_global<uint32_t>(0x008cf678), title_global<uint32_t>(0x008cd7dc),
                                            0x3f800000, 0, 0x3f800000, 1);  // GScr_SetAnimKnobAll(g_anim_stumble2)
            title_function<StandCombatRun>(0x001acd90)(ent);  // BroScr_StandCombatRun (agent-b list)
        }
    }
    scr_release(&unused);
}

void XB_CDECL BroScr_AimToAlert(uint32_t ent) {
    if (*handle_slot(ent, 0x764918ae) != 0xc4a729a7) return;
    if (get_int_184(ent) == 0x070b89a4) {
        const uint32_t* const wounded_time = ScrField_anim_woundedtime(ent);
        bool wounded = get_time() < static_cast<int32_t>(*wounded_time);
        if (!wounded) {
            const int32_t hurt = static_cast<float>(static_cast<int32_t>(*ScrField_h8ef2ad7c(ent))) >
                                         title_global<float>(0x0083eb24)  // 0.5
                                     ? 1
                                     : 0;
            wounded = static_cast<float>(hurt) != title_global<float>(0x0083eafc) || *ScrField_anim_idleset(ent) == 0x77;
        }
        if (wounded) {
            *ScrField_anim_idleset(ent) = 0x77;
            BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008cf6d8), 0x070b89a4, 0x00385d90, 0, 0,
                                          anim_after());  // g_anim_wounded_aim2woundedalert
        } else if (*ScrField_anim_idleset(ent) == 0x61) {
            if (random_int(100) < 50)
                BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008cf360), 0x070b89a4, 0x00385d90, 0, 0,
                                              anim_after());  // g_anim_stand_aim2alert_1
            else
                BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008cf36c), 0x070b89a4, 0x00385d90, 0, 0,
                                              anim_after());  // g_anim_stand_aim2alert_2
        } else {
            BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008cf378), 0x070b89a4, 0x00385d90, 0, 0,
                                          anim_after());  // g_anim_stand_aim2alertb
        }
    } else if (get_int_184(ent) == 0xc1165808 || get_int_184(ent) == 0x0738b1a5) {
        if (*ScrField_anim_idleset(ent) == 0x61)
            BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008ce4e4), 0xc1165808, 0x00385d90, 0, 0,
                                          anim_after());  // g_anim_crouch_aim2alert_a
        else
            BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008ce4f0), 0xc1165808, 0x00385d90, 0, 0,
                                          anim_after());  // g_anim_crouch_aim2alert_b
    }
}

void XB_CDECL BroScr_SetDesiredPoseProne(uint32_t ent) {
    set_turret_pose(ent, 0xc1165808, 0x0738b1a5, 0x0019cfc0, 0x001b6a50);  // BroScr_Mg30calAnimTree, BroScr_AnimScript_TurretCall_1
}

void XB_CDECL BroScr_SetDesiredPoseStandStop(uint32_t ent) {
    set_turret_pose(ent, 0x070b89a4, 0, 0x0019d180, 0x001b6a80);  // BroScr_Mg30calAnimTree_2, BroScr_AnimScript_TurretCall_2
}

void XB_CDECL BroScr_LookAtEnemyLoop(uint32_t ent) {
    bump_look_counter(ent, 0xa538f810, true);
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, 0x06730ae8);  // Scr_EndOn "death"
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, 0xa538f810);
    title_function<CallEntI>(0x0052d0f0)(0, 0, 0, 0, 0x001b6dc0, ent,
                                         int_slot(ent, 0x747cde6c));  // Scr_CallThread_ent_i_KASSPASS(BroScr_LookAtUntilKillAnimscript_1)
    for (;;) {
        uint32_t ref;
        if (entity_handle_is_valid(*slot<GetRef>(0x00922e00)(&ref, ent))) {  // ScrSentient_GetRef_3c (the enemy)
            uint32_t enemy;
            BroScr_LookAtEntity(ent, *slot<GetRef>(0x00922e00)(&enemy, ent), title_global<float>(0x0083eba8), 0x06279a34,
                                0xa3c65328, 0x0bc350d7);  // 2 s
        }
        Scr_Wait(2.0f);
    }
}

void XB_CDECL BroScr_StopForceShoot(uint32_t ent) {
    *int_slot(ent, 0x88c31fb5) = 0;
    title_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x0011b4d0)(
        0, 0, 0, 0, 0x001bf600, ent);  // Scr_BeginThreadFunctor1(BroScr_AimToAlert)
    queue_notify(ent, 0xd6604ba3);
    queue_notify(ent, 0x0fc3c1c9);
    queue_notify(ent, 0x259b8110);
    *int_slot(ent, 0x33a4ea6c) = 0;
    title_function<RemoveKey>(0x0011eb30)(&ent, 0x27cc2178);  // ScrCall_GScr_GetEntScriptObject_2: removes the key
    title_function<RemoveKey>(0x0011eb30)(&ent, 0x7aeb941a);
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009226cc)(ent, 0);  // GScr_ActorStopLookAt
}

void XB_CDECL BroScr_LookAtPositionLoop(uint32_t ent, uint32_t x, uint32_t y, uint32_t z) {
    bump_look_counter(ent, 0x671c92d3, true);
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, 0x06730ae8);  // Scr_EndOn "death"
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, 0x671c92d3);
    title_function<CallEntI>(0x0052d0f0)(0, 0, 0, 0, 0x001b6e70, ent,
                                         int_slot(ent, 0x747cde6c));  // Scr_CallThread_ent_i_KASSPASS(BroScr_LookAtUntilKillAnimscript_2)
    for (;;) {
        BroScr_LookAtPosition(ent, x, y, z, title_global<float>(0x0083eba8), 0x06279a34, 0, 0x71b01aea);  // 2 s
        Scr_Wait(as_float(0x3e4ccccd));  // 0.2
    }
}

void XB_CDECL BroScr_LookAtEntityLoop(uint32_t ent, uint32_t target) {
    if (!entity_handle_is_valid(target)) return;
    bump_look_counter(ent, 0x1651c9e7, false);
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, 0x06730ae8);     // Scr_EndOn "death"
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(target, 0x06730ae8);  // the target's death
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, 0x1651c9e7);
    title_function<CallEntEntI>(0x0052ced0)(0, 0, 0, 0, 0x001b6f20, ent, &target,
                                            int_slot(ent, 0x747cde6c));  // Scr_CallThread_ent_ent_i_KASSPASS(BroScr_LookAtUntilKillAnimscript_3)
    for (;;) {
        BroScr_LookAtEntity(ent, target, title_global<float>(0x0083eba8), 0x06279a34, 0xa3c65328, 0x0bc350d7);  // 2 s
        Scr_Wait(2.0f);
    }
}

void XB_CDECL Bro_aim__keep_aiming(uint32_t ent, ScrVec3 point, float min_time, float max_time) {
    {
        ScriptString message;
        scr_string_from(&message, 0x0082649c);  // "aim::keep_aiming and anim_movement != stop"
        handle_slot(ent, 0x0ccee5c1);           // the assert's read
        scr_release(&message);
    }
    {
        ScriptString message;
        scr_string_from(&message, 0x0082646c);  // "aim::keep_aiming and anim_alertness != aiming"
        handle_slot(ent, 0x764918ae);
        scr_release(&message);
    }
    {
        ScriptString name;
        ScriptString_Copy(&name, reinterpret_cast<const ScriptString*>(anim_object() + 0x194));
        scr_release(&name);
    }
    float time = title_global<float>(0x0083ec24);  // 0.25
    using SetAimParams = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    using BlendAim = long double XB_CDECL(uint32_t, ScrVec3*);
    if (get_int_184(ent) == 0x070b89a4) {
        slot<Knob6>(0x009225d8)(ent, title_global<uint32_t>(0x008cf318), 0x3f800000, 0x3e4ccccd, 0x3f800000, 0);  // GScr_SetAnimKnob(g_anim_Shoot)
        slot<Knob6>(0x009225d8)(ent, title_global<uint32_t>(0x008cf354), 0x3f800000, 0x3e4ccccd, 0x3f800000, 0);  // g_anim_stand_aim
        BroScr_SetAimAnimsForWeapon(ent);
        const uint8_t* const o = anim_object();
        slot<SetAimParams>(0x009226a8)(ent, at<uint32_t>(o, 0x3c), at<uint32_t>(o, 0x40), at<uint32_t>(o, 0x44),
                                       at<uint32_t>(o, 0x48), at<uint32_t>(o, 0x4c), at<uint32_t>(o, 0x50));  // GScr_ActorSetAimParams
        time = static_cast<float>(slot<BlendAim>(0x009226ac)(ent, &point));  // GScr_ActorBlendAimToPoint
    } else if (get_int_184(ent) == 0xc1165808) {
        slot<Knob6>(0x009225d8)(ent, title_global<uint32_t>(0x008cf318), 0x3f800000, 0x3e4ccccd, 0x3f800000, 0);  // g_anim_Shoot
        slot<Knob6>(0x009225d8)(ent, title_global<uint32_t>(0x008ce4d8), 0x3f800000, 0x3e4ccccd, 0x3f800000, 0);  // g_anim_crouch_aim
        BroScr_SetAimAnimsForWeapon_2(ent);
        const uint8_t* const o = anim_object();
        slot<SetAimParams>(0x009226a8)(ent, at<uint32_t>(o, 0x3c), at<uint32_t>(o, 0x40), at<uint32_t>(o, 0x44),
                                       at<uint32_t>(o, 0x48), at<uint32_t>(o, 0x4c), at<uint32_t>(o, 0x50));
        time = static_cast<float>(slot<BlendAim>(0x009226ac)(ent, &point));
    } else {
        slot<Knob6>(0x009225d8)(ent, title_global<uint32_t>(0x008cf1a4), 0x3f800000, 0x3e4ccccd, 0, 0);  // g_anim_prone_shoot_left
        slot<Knob6>(0x009225d8)(ent, title_global<uint32_t>(0x008cf1b0), 0x3f800000, 0x3e4ccccd, 0, 0);  // g_anim_prone_shoot_right
        slot<Knob6>(0x009225d8)(ent, title_global<uint32_t>(0x008cf1bc), 0x3f800000, 0x3e4ccccd, 0, 0);  // g_anim_prone_shoot_straight
        const uint32_t notify = 0xf378e0c7;
        const uint32_t zero = 0;
        title_function<CallEntHashI>(0x001e4c00)(0, 0, 0, 0, 0x001b99c0, ent, &notify,
                                                 &zero);  // Scr_CallThread_vt00825be4_2(BroScr_TurnToEnemy_2)
    }
    // Clamped to min_time..max_time (each step as a script bool converted to float), then waited when positive.
    const int32_t below = min_time > time ? 1 : 0;
    if (static_cast<float>(below) != 0.0f) time = min_time;
    const int32_t above = time > max_time ? 1 : 0;
    if (static_cast<float>(above) != 0.0f) time = max_time;
    const int32_t positive = time > 0.0f ? 1 : 0;
    if (static_cast<float>(positive) != 0.0f) Scr_Wait(time);
    queue_notify(ent, 0xf378e0c7);
    slot<Knob6>(0x009225dc)(ent, title_global<uint32_t>(0x008cf318), 0, 0x3e4ccccd, 0x3f800000, 0);  // GScr_SetAnim(g_anim_Shoot)
}

void XB_CDECL BroScr_StopCoverCrouchIdle(uint32_t ent) {
    const uint32_t weapon = weapon_type(ent);
    if (*handle_slot(ent, 0x175567a8) == 0x3124fe8f) {  // anim_special: low wall cover already
        if (weapon == 0xa39d17dd) {
            orient_face_angle(ent);
            return;
        }
        if (weapon == 0xe6a85d8c || weapon == 0x0036b1a5) {  // pistol, none
            orient_face_angle(ent);
            const uint32_t idle = *ScrField_anim_idleset(ent);
            const uint32_t* const set = reinterpret_cast<const uint32_t*>(anim_object() + 0xa8);
            if (idle != set[0] && idle != set[1] && idle != set[2]) *ScrField_anim_idleset(ent) = 0x61;
            if (*ScrField_anim_idleset(ent) != 0x62) return;
            *ScrField_anim_special(ent) = 0x0036b1a5;
            play_cover_anim(ent, title_global<uint32_t>(0x008ceed4), 0xc1165808);
            Scr_Wait(0.75f);
            *ScrField_anim_idleset(ent) = 0x61;
            *ScrField_anim_special(ent) = 0x3124fe8f;
            play_cover_anim(ent, title_global<uint32_t>(0x008cece8), 0xc1165808);  // g_anim_pistol_crouch2lowwallcovera
            return;
        }
        orient_face_angle(ent);
        if (!is_one_of3(*ScrField_anim_idleset(ent), 0xb4)) *ScrField_anim_idleset(ent) = 0x61;
        if (*ScrField_anim_idleset(ent) != 0x62) return;
        *ScrField_anim_special(ent) = 0x0036b1a5;
        play_cover_anim(ent, title_global<uint32_t>(0x008ce8d4), 0xc1165808);  // g_anim_hidelowwallb2crouch_aim
        Scr_Wait(0.75f);
        if (random_int(100) > 50) {
            *ScrField_anim_idleset(ent) = 0x61;
            *ScrField_anim_special(ent) = 0x3124fe8f;
            play_cover_anim(ent, title_global<uint32_t>(0x008ce4b4), 0xc1165808);  // g_anim_crouch2hideLowWall
        } else {
            *ScrField_anim_idleset(ent) = 0x63;
            *ScrField_anim_special(ent) = 0x3124fe8f;
            play_cover_anim(ent, title_global<uint32_t>(0x008ce508), 0xc1165808);  // g_anim_crouch_aim2hidelowwallc
        }
        return;
    }
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, 0xc1165808, 0x00385d90);  // BroScr_SetPoseMovement
    if (weapon == 0xe6a85d8c || weapon == 0x0036b1a5) {
        orient_face_angle(ent);
        *handle_slot(ent, 0xbfc60f36) = 0x61;        // anim_idleset
        *handle_slot(ent, 0x175567a8) = 0x3124fe8f;  // anim_special
        play_cover_anim(ent, title_global<uint32_t>(0x008cece8), 0xc1165808);  // g_anim_pistol_crouch2lowwallcovera
    } else if (weapon == 0xa39d17dd) {
        orient_face_angle(ent);
        *ScrField_anim_special(ent) = 0x3124fe8f;
        play_cover_anim(ent, title_global<uint32_t>(0x008cec64), 0xc1165808);  // g_anim_panzerfaust_crouchaim2lowwallcover
    } else {
        orient_face_angle(ent);
        if (random_int(100) > 70 || !is_one_of3(*ScrField_anim_idleset(ent), 0x11c))
            *ScrField_anim_idleset(ent) = random_int(100) < 50 ? 0x61 : 0x63;
        const uint32_t idle = *ScrField_anim_idleset(ent);
        if (idle == 0x61) {
            *ScrField_anim_special(ent) = 0x3124fe8f;
            play_cover_anim(ent, title_global<uint32_t>(0x008ce4b4), 0xc1165808);  // g_anim_crouch2hideLowWall
        } else {
            if (idle != 0x63) *ScrField_anim_idleset(ent) = 0x63;
            *ScrField_anim_special(ent) = 0x3124fe8f;
            play_cover_anim(ent, title_global<uint32_t>(0x008ce508), 0xc1165808);  // g_anim_crouch_aim2hidelowwallc
        }
    }
}

void XB_CDECL Bro_cover_crouch__idle(uint32_t ent) {
    {
        ScriptString message;
        scr_string_from(&message, 0x008264f8);  // "cover_crouch::idle: Not in crouch pose."
        get_int_184(ent);                       // the assert's read
        scr_release(&message);
    }
    {
        ScriptString message;
        scr_string_from(&message, 0x008264c8);  // "cover_crouch::idle: Not in cover_crouch pose."
        handle_slot(ent, 0x175567a8);
        scr_release(&message);
    }
    using EntFn = void XB_CDECL(uint32_t);
    const uint32_t weapon = weapon_type(ent);
    if (weapon == 0xe6a85d8c || weapon == 0x0036b1a5) {  // pistol, none
        const uint32_t idle = *handle_slot(ent, 0xbfc60f36);  // anim_idleset
        if (idle == 0x61) {
            title_function<EntFn>(0x00183290)(ent);  // BroScr_CoverCrouchPlayIdle_1 (agent-b list)
        } else if (idle == 0x62) {
            title_function<EntFn>(0x001832d0)(ent);  // BroScr_CoverCrouchPlayIdle_2 (agent-b list)
        } else {
            *ScrField_anim_idleset(ent) = 0x63;
            title_function<EntFn>(0x001b7230)(ent);  // BroScr_CoverCrouchIdleTwitch_2 (agent-b list)
        }
    } else if (weapon == 0xa39d17dd) {
        orient_face_angle(ent);
        const int32_t pick = random_int(100);
        const float rate = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922230)(0x3e4ccccd) +
                                              title_global<float>(0x0083ec18));  // Scr_RandomFloat(0.2) + 0.9
        const uint32_t notify = 0x54e3b2b6;
        const uint32_t anim = pick < 70 ? title_global<uint32_t>(0x008cecb8)   // g_anim_panzerfaust_lowwall_idle
                                        : title_global<uint32_t>(0x008cecc4);  // g_anim_panzerfaust_lowwall_twitch
        ScrCall_GScr_SetFlaggedAnimKnobAll(&ent, &notify, anim, title_global<uint32_t>(0x008cd7dc), 0x3f800000,
                                           0x3dcccccd, bits(rate));
        handle_note_tracks(ent, 0x54e3b2b6);
    } else {
        const uint32_t idle = *ScrField_anim_idleset(ent);
        if (idle == 0x61) {
            title_function<EntFn>(0x001b7060)(ent);  // BroScr_CoverCrouchIdleTwitch (agent-b list)
        } else if (idle == 0x62) {
            title_function<EntFn>(0x001b7180)(ent);  // BroScr_CoverCrouchRandomIdleAnim (agent-b list)
        } else {
            if (idle != 0x63) *ScrField_anim_idleset(ent) = 0x63;
            title_function<EntFn>(0x001b7230)(ent);  // BroScr_CoverCrouchIdleTwitch_2 (agent-b list)
        }
    }
}

void XB_CDECL BroScr_CoverCrouchReloadAnim(uint32_t ent, ScriptString unused) {
    ScriptString name;
    ScrCall_ScrActor_GetString_1384(&ent, &name);  // read and dropped
    if (weapon_type(ent) == 0xa39d17dd) {
        BroScr_StopCoverCrouchIdle(ent);
    } else if (*handle_slot(ent, 0x175567a8) != 0x3124fe8f) {  // anim_special
        title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, 0xc1165808, 0x00385d90);  // BroScr_SetPoseMovement
        *ScrField_anim_idleset(ent) = 0x62;
        *ScrField_anim_special(ent) = 0x3124fe8f;
        BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008ce4fc), 0xc1165808, 0x00385d90, 1, 1,
                                      anim_after());  // g_anim_crouch_aim2hidelowwallb
    }
    if (*handle_slot(ent, 0xbfc60f36) != 0x62) {  // anim_idleset
        if (*handle_slot(ent, 0xbfc60f36) == 0x61) {
            *ScrField_anim_special(ent) = 0x0036b1a5;
            BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008ce8b0), 0xc1165808, 0x00385d90, 1, 1,
                                          anim_after());  // g_anim_hideLowWall2crouch
            *ScrField_anim_idleset(ent) = 0x62;
            *ScrField_anim_special(ent) = 0x3124fe8f;
            BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008ce4fc), 0xc1165808, 0x00385d90, 1, 1,
                                          anim_after());  // g_anim_crouch_aim2hidelowwallb
        } else {
            *ScrField_anim_idleset(ent) = 0x62;
            BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008ce904), 0xc1165808, 0x00385d90, 1, 1,
                                          anim_after());  // g_anim_hidelowwallc2hidelowwallb
        }
    }
    scr_release(&name);
    scr_release(&unused);
}

void XB_CDECL BroScr_PlayCrouchIdleAnim(uint32_t ent) {
    if (weapon_type(ent) == 0xa39d17dd) {
        *handle_slot(ent, 0x175567a8) = 0x0036b1a5;  // anim_special
        BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008cecd0), 0xc1165808, 0x00385d90, 1, 1,
                                      anim_after());  // g_anim_panzerfaust_lowwallcover2crouchaim
    } else if (*handle_slot(ent, 0xbfc60f36) == 0x61) {  // anim_idleset
        *ScrField_anim_special(ent) = 0x0036b1a5;
        BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008ce8b0), 0xc1165808, 0x00385d90, 1, 1,
                                      anim_after());  // g_anim_hideLowWall2crouch
    } else if (*ScrField_anim_idleset(ent) == 0x62) {
        *ScrField_anim_special(ent) = 0x0036b1a5;
        BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008ce8d4), 0xc1165808, 0x00385d90, 1, 1,
                                      anim_after());  // g_anim_hidelowwallb2crouch_aim
    } else if (*ScrField_anim_idleset(ent) == 0x63) {
        *ScrField_anim_idleset(ent) = 0x62;
        BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008ce904), 0xc1165808, 0x00385d90, 1, 1,
                                      anim_after());  // g_anim_hidelowwallc2hidelowwallb
        *ScrField_anim_special(ent) = 0x0036b1a5;
        BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008ce8d4), 0xc1165808, 0x00385d90, 1, 1,
                                      anim_after());  // g_anim_hidelowwallb2crouch_aim
    } else {
        *ScrField_anim_idleset(ent) = 0x62;
        *ScrField_anim_special(ent) = 0x0036b1a5;
        BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008ce8d4), 0xc1165808, 0x00385d90, 1, 1,
                                      anim_after());  // g_anim_hidelowwallb2crouch_aim
    }
}

void XB_CDECL BroScr_CoverStandIdleTransition(uint32_t ent) {
    using SetPoseMovement = void XB_CDECL(uint32_t, uint32_t, uint32_t);
    const uint32_t pose = get_int_184(ent);
    if (pose == 0x070b89a4) {
        title_function<SetPoseMovement>(0x001a00c0)(ent, 0x070b89a4, 0x00385d90);  // BroScr_SetPoseMovement
        BroScr_PlayPoseTransitionAnim(ent, title_global<uint32_t>(0x008cf330), 0xc1165808, 0x00385d90, 0, 0,
                                      anim_after());  // g_anim_stand2crouch_hide
        *handle_slot(ent, 0xbfc60f36) = 0x62;        // anim_idleset
        *handle_slot(ent, 0x175567a8) = 0x3124fe8f;  // anim_special
        return;
    }
    if (pose != 0xc1165808) {
        if (pose != 0x0738b1a5) set_int_184(ent, 0xc1165808);
        title_function<SetPoseMovement>(0x001a00c0)(ent, 0xc1165808, 0x00385d90);
    }
    if (*handle_slot(ent, 0x175567a8) == 0x3124fe8f) {
        if (*ScrField_anim_idleset(ent) == 0x61) {
            *ScrField_anim_special(ent) = 0x0036b1a5;
            BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008ce8b0), 0xc1165808, 0x00385d90, 1, 1,
                                          anim_after());  // g_anim_hideLowWall2crouch
            *ScrField_anim_idleset(ent) = 0x62;
            *ScrField_anim_special(ent) = 0x3124fe8f;
            BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008ce4fc), 0xc1165808, 0x00385d90, 1, 1,
                                          anim_after());  // g_anim_crouch_aim2hidelowwallb
        } else if (*ScrField_anim_idleset(ent) == 0x63) {
            *ScrField_anim_idleset(ent) = 0x62;
            BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008ce904), 0xc1165808, 0x00385d90, 1, 1,
                                          anim_after());  // g_anim_hidelowwallc2hidelowwallb
        } else if (*ScrField_anim_idleset(ent) != 0x62) {
            *ScrField_anim_idleset(ent) = 0x62;
        }
    } else {
        *ScrField_anim_special(ent) = 0x3124fe8f;
        *ScrField_anim_idleset(ent) = 0x62;
        BroScr_PlayPoseTransitionAnim(ent, title_global<uint32_t>(0x008ce4fc), 0xc1165808, 0x00385d90, 0, 1,
                                      anim_after());  // g_anim_crouch_aim2hidelowwallb
    }
}

void XB_CDECL BroScr_FaceEnemyAnim(uint32_t ent) {
    set_orient_mode(ent, 0x0083adc0);  // "face enemy"
    BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008ce6ac), 0x070b89a4, 0x00385d90, 1, 0,
                                  anim_after());  // g_anim_crouchhide2stand
    *handle_slot(ent, 0x175567a8) = 0x0036b1a5;  // anim_special
}

void XB_CDECL Bro_anim_main_grenade_cower(uint32_t ent) {
    title_function<BeginThreadFunctor1>(0x0011b4d0)(1, 0, 0, 0, 0x001c22b0, ent);  // Scr_BeginThreadFunctor1(BroScr_AnimScript_GrenadeCower)
}

void XB_CDECL Bro_b30cal_stand(uint32_t ent) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922748)(ent, title_global<uint32_t>(0x008cf348));  // ScrActor_SetInt_1370(g_anim_stand30calgunner_fire)
    title_function<BeginThreadFunctor1>(0x0011b4d0)(1, 0, 0, 0, 0x001bfd80, ent);  // (BroScr_SetDesiredPoseStandStop)
}

void XB_CDECL Bro_b30cal_prone(uint32_t ent) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922748)(ent, title_global<uint32_t>(0x008cf0d8));  // ScrActor_SetInt_1370
    title_function<BeginThreadFunctor1>(0x0011b4d0)(1, 0, 0, 0, 0x001bfc50, ent);  // (BroScr_SetDesiredPoseProne)
}

void XB_CDECL BroScr_SetDesiredPoseProneMg42(uint32_t ent) {
    set_turret_pose(ent, 0xc1165808, 0x0738b1a5, 0x0019f390, 0x001b9310);  // BroScr_Mg42AnimTree, BroScr_AnimScript_TurretCall_3
}

void XB_CDECL BroScr_TurretPoseProne(uint32_t ent) {
    set_turret_pose(ent, 0x0738b1a5, 0x0738b1a5, 0x0019f770, 0x001b9710);  // BroScr_Mg42AnimTree_2, BroScr_AnimScript_TurretCall_4
}

void XB_CDECL BroScr_TurretPoseStand(uint32_t ent) {
    set_turret_pose(ent, 0x070b89a4, 0, 0x001b9770, 0x001b9740);  // BroScr_Mg42AnimTree_3, BroScr_AnimScript_TurretCall_5
}

uint8_t* XB_CDECL BroScr_TryTurretGunnerPain(uint8_t* out, uint32_t ent, uint32_t special) {
    const uint8_t yes = 1;
    using CallEntB = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint8_t*);
    title_function<CallEntB>(0x0052cb60)(0, 0, 0, 0, 0x0018dbc0, ent, &yes);  // Scr_CallThread_ent_b_KASSPASS(BroScr_PainAnimByHitLocation)
    if (special == 0x00371ab2 && get_int_184(ent) == 0x070b89a4) {
        title_function<void XB_CDECL(uint32_t)>(0x001b9950)(ent);  // BroScr_PlayStandMG42GunnerPain (agent-b list)
        *out = 1;
    } else {
        *out = 0;
    }
    return out;
}

uint8_t* XB_CDECL BroScr_AnimMoveUntilEndNote(uint8_t* out, uint32_t ent, uint32_t notify) {
    ScriptString note;
    ScriptString_FromRep(&note, nullptr);
    for (;;) {
        uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
        if (functor) {
            functor[0] = 0x007f4b00;
            functor[1] = 0x001acae0;  // BroScr_ForwardNotifyString
            functor[2] = ent;
            functor[3] = notify;
        }
        slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x00922168)(
            0, 0, 0, ent, functor, 0);  // Scr_BeginThread
        if (static_cast<uint8_t>(slot<uint32_t XB_CDECL()>(0x0092218c)())) Scr_WaitFrames(0);  // Scr_InThread
        const uint8_t done = static_cast<uint8_t>(slot<uint32_t XB_CDECL(uint32_t)>(0x00922760)(ent));  // GScr_ActorAdvanceAnimTime
        queue_notify(ent, notify);
        title_function<void XB_CDECL(uint32_t, uint32_t, ScriptString*)>(0x004b14f0)(ent, 0x71445fee,
                                                                                      &note);  // Scr_WaitTillOutputStr_ORAN
        if (note.rep && ScriptString_Equals(&note, pointer<const char>(0x00832b28))) {  // "end"
            *out = 1;
            break;
        }
        if (done) continue;
        slot<void XB_CDECL(uint32_t)>(0x0092275c)(ent);  // GScr_ActorApplyAnimMove
        if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922770)(ent)) {  // ScrActor_GetByte_3b1
            *out = 0;
            break;
        }
        uint32_t buffer[3];
        const uint32_t* a = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922ba0)(buffer, ent);  // ScrEnt_GetAngles
        const ScrVec3 angles = {{a[0], a[1], a[2]}};
        uint32_t origin_buffer[3];
        const uint32_t* o = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(origin_buffer, ent);  // ScrEnt_GetOrigin
        const ScrVec3 origin = {{o[0], o[1], o[2]}};
        title_function<void XB_CDECL(uint32_t, uint32_t, ScrVec3, ScrVec3)>(0x001c2aa0)(ent, 4, origin, angles);  // FUN_001c2aa0
    }
    scr_release(&note);
    return out;
}

void XB_CDECL BroScr_RecordAnimCommand(uint32_t ent, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6,
                                       uint32_t a7, uint32_t a8) {
    const uint32_t u = undefined_lane();
    const AnimCommand command = {{a2, a4, a5, a3, a6, a7, a8, u, u, u, u, u, u}};
    ScriptKeyTable* const table = ent_script_object(ent);
    DynArray_Push52(reinterpret_cast<DynArray*>(ScriptKeyTable_FindOrAdd(table, 0x1791f035)), command.w);
    title_function<ApplyAnimCommand>(0x00183db0)(ent, command);  // BroScr_ApplyAnimCommand (agent-b list)
}

void XB_CDECL BroScr_ReplayAnimCommands(uint32_t ent) {
    using SetAnimMode = void XB_CDECL(uint32_t, ScriptString*);
    {
        ScriptString mode;
        scr_string_from(&mode, 0x0083a928);  // "nophysics"
        slot<SetAnimMode>(0x0092273c)(ent, &mode);  // GScr_ActorSetAnimMode
        scr_release(&mode);
    }
    const uint8_t* it = reinterpret_cast<const uint8_t*>(
        reinterpret_cast<DynArray*>(ScriptKeyTable_FindOrAdd(ent_script_object(ent), 0x1791f035))->data);
    for (;;) {
        ScriptKeyTable* const table = ent_script_object(ent);
        uint32_t* value = ScriptKeyTable_Find(table, 0x1791f035);
        if (!value) {
            const uint32_t empty[3] = {0, 0, 0};
            value = ScriptKeyTable_SetNewArray_3(table, 0x1791f035, empty);
        }
        const DynArray* const commands = pointer<DynArray>(*value);
        if (it == reinterpret_cast<const uint8_t*>(commands->data) + commands->size * 0x34) break;
        AnimCommand command;
        std::memcpy(&command, it, sizeof(command));
        title_function<ApplyAnimCommand>(0x00183db0)(ent, command);  // BroScr_ApplyAnimCommand (agent-b list)
        it += 0x34;
    }
    DynArray_Resize52(reinterpret_cast<DynArray*>(ScriptKeyTable_FindOrAdd(ent_script_object(ent), 0x1791f035)), 0, 0);
    {
        ScriptString mode;
        scr_string_from(&mode, 0x00832430);  // "none"
        slot<SetAnimMode>(0x0092273c)(ent, &mode);
        scr_release(&mode);
    }
}

void XB_CDECL BroScr_StandStartWalk(uint32_t ent) {
    if (combat_anim_takes_over(ent, true)) {
        title_function<void XB_CDECL(uint32_t)>(0x001acf80)(ent);  // BroScr_StandCombatWalk (agent-b list)
        return;
    }
    if (*ScrField_anim_alertness(ent) == 0xc028584c)
        BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008cf3d8), 0x070b89a4, 0x00385d90, 1, 1,
                                      anim_after());  // g_anim_stand_alert2aim
    const int32_t pick = random_int(100);
    uint32_t loop;
    if (pick < 50)
        loop = title_global<uint32_t>(0x008cf5e8);
    else if (pick < 80)
        loop = title_global<uint32_t>(0x008cf5f4);  // g_anim_stand_walk_combat_loop_02
    else
        loop = title_global<uint32_t>(0x008cf600);
    BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008cf60c), 0x070b89a4, 0x003a09eb, 1, 1,
                                  loop);  // g_anim_stand_walk_start_straight
}

void XB_CDECL BroScr_StandWalkToStop(uint32_t ent) { stand_walk_to_stop(ent, true); }

void XB_CDECL BroScr_StandWalkToStop_2(uint32_t ent) { stand_walk_to_stop(ent, false); }

void XB_CDECL BroScr_StandMoveToStop(uint32_t ent) {
    const uint32_t weapon = weapon_type(ent);
    if (weapon == 0xe6a85d8c || weapon == 0x0036b1a5) {  // pistol, none
        BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008ced0c), 0x070b89a4, 0x00385d90, 1, 1,
                                      anim_after());
    } else {
        if (random_int(100) < 50)
            *ScrField_anim_idleset(ent) = 0x61;
        else
            *ScrField_anim_idleset(ent) = 0x62;
        BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008ce4cc), 0x070b89a4, 0x00385d90, 1, 1,
                                      anim_after());  // g_anim_crouch2stand
    }
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x009225d4)(ent, title_global<uint32_t>(0x008cf318), 0);  // GScr_ClearAnim(g_anim_Shoot)
}

void XB_CDECL BroScr_CrouchToCrouchWalk(uint32_t ent) {
    BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008ce4a8), 0xc1165808, 0x003a09eb, 0, 1,
                                  title_global<uint32_t>(0x008ce760));  // g_anim_crouch2crouchwalk_straight, then crouchwalk_loop
}

void XB_CDECL BroScr_CrouchWalkToCrouchStop(uint32_t ent) { crouch_walk_to_stop(ent); }

void XB_CDECL BroScr_CrouchWalkToCrouchStop_2(uint32_t ent) { crouch_walk_to_stop(ent); }

void XB_CDECL BroScr_CrouchRun_2(uint32_t ent) {
    title_function<void XB_CDECL(uint32_t)>(0x001ad1f0)(ent);  // BroScr_CrouchRun (agent-b list)
}

void XB_CDECL BroScr_ProneFaceCurrent(uint32_t ent) {
    set_orient_mode(ent, 0x0083adec);  // "face current"
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009226b4)(ent, 0x3f800000);
    slot<KnobAllNoFlag>(0x009225c8)(ent, title_global<uint32_t>(0x008cf138), title_global<uint32_t>(0x008cd7dc),
                                    0x3f800000, 0x3e4ccccd, 0x3f800000, 0);  // GScr_SetAnimKnobAll(g_anim_prone_legsstraight)
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009226bc)(
        ent, title_global<uint32_t>(0x008cf1d4), title_global<uint32_t>(0x008cf1c8), 0x3f800000, 0x3dcccccd,
        0x3f800000);  // GScr_ActorSetAimAnimWeights(prone shootfeet 45 up / down)
    BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008cf0cc), 0xc1165808, 0x0001c6ce, 0, 1,
                                  title_global<uint32_t>(0x008ce70c));  // g_anim_prone2crouchrun_straight, then crouchrun_loop_forward
}

void XB_CDECL BroScr_StandToCrouchStop(uint32_t ent) {
    random_idle_set(ent);
    BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008cf324), 0xc1165808, 0x00385d90, 1, 1,
                                  anim_after());  // g_anim_stand2crouch_attack
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x009225d4)(ent, title_global<uint32_t>(0x008cf318), 0);  // GScr_ClearAnim(g_anim_Shoot)
}

void XB_CDECL BroScr_ProneAimToCrawl(uint32_t ent, uint32_t move) {
    prone_legs(ent, 0x3dcccccd);
    BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008cf0e4), 0x0738b1a5, move, 0, 1,
                                  title_global<uint32_t>(0x008cf0fc));  // g_anim_prone_aim2crawl, then prone_crawl
    prone_aim_weights(ent);
}

void XB_CDECL BroScr_ProneCrawlToAim(uint32_t ent) {
    prone_legs(ent, 0x3dcccccd);
    prone_aim_weights(ent);
    play_to_prone(ent, title_global<uint32_t>(0x008cf108));  // g_anim_prone_crawl2aim
}

void XB_CDECL BroScr_CrouchToProne(uint32_t ent) {
    prone_pitch(ent);
    prone_legs(ent, 0x3e99999a);  // 0.3
    prone_aim_weights(ent);
    play_to_prone(ent, title_global<uint32_t>(0x008ce4c0));  // g_anim_crouch2prone_gunsupport
}

void XB_CDECL BroScr_StandToProne(uint32_t ent) {
    prone_pitch(ent);
    prone_legs(ent, 0x3e4ccccd);  // 0.2
    prone_aim_weights(ent);
    play_to_prone(ent, title_global<uint32_t>(0x008cf33c));  // g_anim_stand2prone_onehand
}

void XB_CDECL BroScr_CrouchRunToProne(uint32_t ent) {
    prone_pitch(ent);
    prone_legs(ent, 0x3e4ccccd);  // 0.2
    prone_aim_weights(ent);
    uint32_t out;
    const uint32_t direction = *BroScr_YawToDirectionName(
        &out, static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922744)(ent)));  // GScr_ActorGetMotionYaw
    uint32_t anim;
    if (direction == 0x0658b1b4)
        anim = title_global<uint32_t>(0x008ce6dc);  // g_anim_crouchrun2prone_straight
    else if (direction == 0x003798b4)
        anim = title_global<uint32_t>(0x008ce6c4);  // g_anim_crouchrun2prone_left
    else if (direction == 0x07151174)
        anim = title_global<uint32_t>(0x008ce6d0);  // g_anim_crouchrun2prone_right
    else
        anim = title_global<uint32_t>(0x008ce4c0);  // g_anim_crouch2prone_gunsupport
    const uint32_t u = undefined_lane();
    uint32_t delta[3] = {u, u, u};
    uint32_t world[3] = {u, u, u};
    slot<void XB_CDECL(uint32_t*, uint32_t, uint32_t, uint32_t)>(0x00922408)(delta, anim, 0, 0x3f800000);  // GScr_GetMoveDelta
    slot<void XB_CDECL(uint32_t, uint32_t*, uint32_t*)>(0x0092263c)(ent, delta, world);  // GScr_LocalToWorldCoords
    if (static_cast<uint8_t>(slot<uint32_t XB_CDECL(uint32_t, uint32_t*)>(0x009226f4)(ent, world)))  // GScr_ActorIsSafeMovePoint
        play_to_prone(ent, anim);
    else
        play_to_prone(ent, title_global<uint32_t>(0x008ce4c0));  // g_anim_crouch2prone_gunsupport
}

void XB_CDECL BroScr_ForceShootTargetDeathWatch(uint32_t ent, uint32_t target) {
    using EndOn = void XB_CDECL(uint32_t, uint32_t);
    slot<EndOn>(0x00922188)(ent, 0x06730ae8);  // Scr_EndOn "death"
    slot<EndOn>(0x00922188)(ent, 0x0039894e);
    slot<EndOn>(0x00922188)(ent, 0x2cf20de4);
    slot<EndOn>(0x00922188)(ent, 0x3c77a48b);
    Scr_WaitTill(target, 0x06730ae8);  // the target's death
    BroScr_StopForceShoot(ent);
}

void XB_CDECL Bro_anim_main_grenade_return_throw(uint32_t ent) {
    title_function<BeginThreadFunctor1>(0x0011b4d0)(1, 0, 0, 0, 0x001c76a0, ent);  // Scr_BeginThreadFunctor1(BroScr_ChatterGrenadeAttack)
}

void XB_CDECL Bro_anim_main_walk(uint32_t ent) {
    title_function<BeginThreadFunctor1>(0x0011b4d0)(1, 0, 0, 0, 0x001c4510, ent);  // Scr_BeginThreadFunctor1(BroScr_AnimScript_Walk)
}

void XB_CDECL BroScr_AnimScript_GrenadeCower(uint32_t ent) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, 0x07b4e7f7);  // Scr_EndOn
    BroScr_InitAnimScript(ent, 0xfe429dc3);
    BroScr_ApplyDesiredAnimPose(ent);
    Scr_Wait(static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922230)(0x3e800000)));  // Scr_RandomFloat(0.25)
    uint32_t start;
    uint32_t loop;
    if (get_int_184(ent) == 0x0738b1a5) {
        loop = title_global<uint32_t>(0x008cd824);   // g_anim_c_prone_grenade_cower_loop
        start = title_global<uint32_t>(0x008cd830);  // g_anim_c_prone_grenade_cower_start
    } else {
        const char* side;
        if (random_int(100) < 50) {
            start = title_global<uint32_t>(0x008ce868);  // g_anim_grenadehide_crouch2left
            loop = title_global<uint32_t>(0x008ce880);   // g_anim_grenadehide_left
            side = pointer<const char>(0x0083b7c4);      // "left"
        } else {
            start = title_global<uint32_t>(0x008ce874);  // g_anim_grenadehide_crouch2right
            loop = title_global<uint32_t>(0x008ce898);   // g_anim_grenadehide_right
            side = pointer<const char>(0x0083b7b4);      // "right"
        }
        ScriptString_AssignCString(title_function<GetString>(0x00458da0)(ent_script_object(ent), 0x75f8caea), side);  // ScriptKeyTable_GetString
    }
    const uint32_t body = title_global<uint32_t>(0x008cd7dc);  // g_anim_Body
    if (get_int_184(ent) == 0xc1165808) {
        slot<KnobAll>(0x009225e4)(ent, 0x8e613617, start, body, 0x3f800000, 0x3dcccccd, 0x3f800000, 1);  // GScr_SetFlaggedAnimKnobAll
    } else if (get_int_184(ent) == 0x0738b1a5) {
        slot<KnobAll>(0x009225e4)(ent, 0x8e613617, start, body, 0x3f800000, 0x3ecccccd, 0x3f800000, 1);
    } else {
        slot<KnobAll>(0x009225e4)(ent, 0x8e613617, start, body, 0x3f800000, 0x3ecccccd, 0x3f800000, 1);
        set_int_184(ent, 0xc1165808);
    }
    *handle_slot(ent, 0x0ccee5c1) = 0x00385d90;
    handle_note_tracks(ent, 0x8e613617);
    slot<KnobAllNoFlag>(0x009225c8)(ent, loop, title_global<uint32_t>(0x008cd7dc), 0x3f800000, 0x3dcccccd, 0x3f800000,
                                    1);  // GScr_SetAnimKnobAll
    *key_slot(ent, 0xe057503b, 0) = 0x001b7c70;  // the start callback: BroScr_GrenadeCowerHide
    for (;;) Scr_Wait(10.0f);
}

void XB_CDECL BroScr_IdleFaceCurrent(uint32_t ent) {
    random_idle_set(ent);
    set_orient_mode(ent, 0x0083adec);  // "face current"
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009226b4)(ent, 0x3f800000);
    prone_legs(ent, 0x3dcccccd);
    prone_aim_weights(ent);
    BroScr_PlayAnimWithNoteTracks(ent, title_global<uint32_t>(0x008cf0c0), 0xc1165808, 0x00385d90, 1, 1,
                                  anim_after());  // g_anim_prone2crouch_straight
}

void XB_CDECL BroScr_KeepAimingAtTarget(uint32_t ent, uint32_t first, uint32_t second) {
    BroScr_SetAimBlendTimes(first, second);
    bool defined;
    ScrVec3 point;
    if (*ScrField_h33a4ea6c_IsDefined(&defined, ent) && *int_slot(ent, 0x33a4ea6c) != 0) {
        const float* const p = ScrField_h7aeb941a(ent);  // the aim point
        std::memcpy(point.v, p, 12);
    } else if (*ScrField_h27cc2178_IsDefined(&defined, ent)) {
        const uint32_t target = *ScrField_h27cc2178(ent);
        uint32_t origin[3] = {undefined_lane(), undefined_lane(), undefined_lane()};
        slot<void XB_CDECL(uint32_t, uint32_t*)>(0x00922524)(target, origin);  // GScr_GetOrigin
        std::memcpy(point.v, origin, 12);
    } else {
        uint32_t eye[3];
        const uint32_t* const p = BroScr_GetEnemyEyePos(eye, ent);
        std::memcpy(point.v, p, 12);
    }
    Bro_aim__keep_aiming(ent, point, title_global<float>(0x009230ec), title_global<float>(0x0092379c));
}

void XB_CDECL BroScr_StartAnimFaceWait(uint32_t ent, uint32_t anim, ScriptString a3, uint32_t a4, uint32_t a5,
                                       ScriptString a6, uint32_t a7) {
    const uint8_t yes = 1;
    using Call = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                   const ScriptString*, const uint32_t*, const uint32_t*, const ScriptString*,
                                   const uint32_t*, const uint8_t*);
    title_function<Call>(0x001e5100)(0, 0, 0, 0, 0x001c1860, ent, &anim, &a3, &a4, &a5, &a6, &a7,
                                     &yes);  // Scr_CallThread_ent_anim_str_f_hash_str_f_b(BroScr_AnimFaceWait)
    scr_release(&a3);
    scr_release(&a6);
}

void XB_CDECL BroScr_PlayIdleAnim(uint32_t ent) {
    {
        ScriptString unused;
        ScriptString_Copy(&unused, reinterpret_cast<const ScriptString*>(anim_object() + 0x194));
        scr_release(&unused);
    }
    if (*handle_slot(ent, 0x764918ae) == 0xc4a729a7) return;
    if (get_int_184(ent) == 0x070b89a4) {  // standing
        if (*ScrField_anim_idleset(ent) == 'w') {
            play_alert_anim(ent, title_global<uint32_t>(0x008cf6e4), 0x070b89a4);  // wounded_alert2aim
        } else if (*ScrField_anim_idleset(ent) == 'a') {
            play_alert_anim(ent, title_global<uint32_t>(0x008cf3d8), 0x070b89a4);  // stand_alert2aim
        } else {
            *ScrField_anim_idleset(ent) = 'b';
            play_alert_anim(ent, title_global<uint32_t>(0x008cf414), 0x070b89a4);  // stand_alertb2aim
        }
    } else if (get_int_184(ent) == 0xc1165808 && *ScrField_anim_special(ent) == 0x3124fe8f) {
        BroScr_PlayCrouchIdleAnim(ent);
    } else if (get_int_184(ent) == 0xc1165808 || get_int_184(ent) == 0x0738b1a5) {  // crouching / prone
        if (*ScrField_anim_idleset(ent) == 'a') {
            play_alert_anim(ent, title_global<uint32_t>(0x008ce538), 0xc1165808);  // crouch_alert2aim_a
        } else {
            *ScrField_anim_idleset(ent) = 'b';
            play_alert_anim(ent, title_global<uint32_t>(0x008ce574), 0xc1165808);  // crouch_alertb2aim
        }
    }
    *handle_slot(ent, 0x764918ae) = 0xc4a729a7;
}

void XB_CDECL BroScr_AnimScript_Walk(uint32_t ent) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, 0x07b4e7f7);  // Scr_EndOn
    BroScr_InitAnimScript(ent, 0x003a09eb);
    BroScr_ApplyDesiredAnimPose(ent);
    if (get_int_184(ent) == 0x19669cd9) {  // wounded
        ScriptString name;
        scr_string_from(&name, 0x0082654c);  // "walk::main"
        BroScr_WoundedWalk(ent, name);
    }
    {
        ScriptString unused;
        ScriptString_Copy(&unused, reinterpret_cast<const ScriptString*>(anim_object() + 0x98));
        scr_release(&unused);
    }
    ScriptString kept;  // never released: the loop does not end
    ScriptString_FromRep(&kept, nullptr);
    for (;;) {
        uint32_t pose;
        BroScr_GetAllowedMovePose(&pose, ent, 0);
        uint8_t combat;
        BroScr_UpdateCombatDelay(&combat, ent);
        uint32_t anim;
        if (pose == 0x070b89a4) {  // standing
            if (int_set(ent, 0xb1812439) && combat) {  // walk_combatanim
                bool defined;
                if (random_int(100) < 20 && *ScrField_h30248704_IsDefined(&defined, ent))
                    anim = *ScrField_h30248704(ent);
                else
                    anim = *key_slot(ent, 0xb1812439, 0);
            } else if (int_set(ent, 0x9dc74345) && !combat) {
                bool defined;
                if (random_int(100) < 20 && *ScrField_hb8e86881_IsDefined(&defined, ent))
                    anim = *ScrField_hb8e86881(ent);
                else
                    anim = *key_slot(ent, 0x9dc74345, 0);
            } else {
                const uint32_t* const until = int_slot(ent, 0x32292cf8);
                if (get_time() < static_cast<int32_t>(*until) ||
                    static_cast<float>(static_cast<float>(static_cast<int32_t>(*int_slot(ent, 0x8ef2ad7c))) > 0.5f) !=
                        0.0f) {
                    anim = title_global<uint32_t>(0x008cf780);  // wounded_walk_loop
                } else {
                    const float value =
                        static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922708)(ent));  // slot 0x00922708
                    const float limit = as_float(at<uint32_t>(anim_object(), 0xc0));
                    if (static_cast<float>(limit > value) != 0.0f && !combat &&
                        *ScrField_anim_movement(ent) == 0x00385d90) {
                        title_function<void XB_CDECL(uint32_t)>(0x001b2ed0)(ent);  // FUN_001b2ed0 (stand shuffle forward)
                        continue;
                    }
                    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, 0x070b89a4,
                                                                                            0x003a09eb);  // BroScr_SetPoseMovement
                    const int32_t pick = random_int(100);
                    if (pick < 40)
                        anim = title_global<uint32_t>(0x008cf5e8);
                    else if (pick < 70)
                        anim = title_global<uint32_t>(0x008cf5f4);  // stand_walk_combat_loop_02
                    else
                        anim = title_global<uint32_t>(0x008cf600);
                }
            }
        } else if (pose == 0x0738b1a5) {  // prone
            title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, 0x0738b1a5, 0x003a09eb);
            anim = title_global<uint32_t>(0x008cf0fc);  // prone_crawl
        } else if (pose == 0xc1165808) {  // crouching
            title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, 0xc1165808, 0x003a09eb);
            anim = title_global<uint32_t>(0x008ce760);  // crouchwalk_loop
        } else {
            continue;
        }
        slot<KnobAll>(0x009225e4)(ent, 0x9e83b205, anim, title_global<uint32_t>(0x008cd7dc), bits(1.0f), bits(1.2f),
                                  bits(1.0f), 0);  // GScr_SetFlaggedAnimKnobAll(g_anim_Body)
        Scr_WaitTill(ent, 0x9e83b205);
    }
}

void XB_CDECL BroScr_AnimFaceWait(uint32_t ent, uint32_t anim, ScriptString sound, uint32_t level, uint32_t notify,
                                  ScriptString kind, uint32_t time, uint32_t flag) {
    BroScr_AnimFaceResetKeys(ent);
    if (anim || sound.rep) {
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, 0x06730ae8);  // Scr_EndOn "death"
        if (static_cast<float>(static_cast<int32_t>(*int_slot(ent, 0x03375c37))) >= as_float(level) && kind.rep &&
            ScriptString_Equals(&kind, pointer<const char>(0x00833c88))) {  // "wait"
            // Queue behind the face anim playing now: a record, then wait for the stop notify.
            ScrField_h053fe992(ent);
            const uint32_t id = *ScrField_h257acba4(ent) + 1;
            FaceWaitRecord record;
            title_function<void XB_THISCALL(FaceWaitRecord*)>(0x0045bcd0)(&record);  // the record's constructor
            record.anim = anim;
            ScriptString_Assign(&record.sound, &sound);
            record.notify = notify;
            record.level = level;
            ScriptString_Assign(&record.kind, &kind);
            record.time = float_to_script_int(as_float(time));
            record.id = id;
            DynArray_PushBack28(static_cast<DynArray*>(ScrField_h053fe992(ent)), reinterpret_cast<const uint8_t*>(&record));
            ScriptString name;
            scr_string_from(&name, 0x0082652c);  // "animscript face stop waiting "
            ScriptString_AppendInt(&name, static_cast<int32_t>(id));
            const uint32_t stop_a = 0xc77f1e4d;
            const uint32_t stop_b = 0xc77f1e4d;
            title_function<CallEntStrHashHash>(0x001e51c0)(0, 0, 0, 0, 0x0019e1a0, ent, &name, &stop_a,
                                                          &stop_b);  // Scr_CallThread_ent_str_hash_hash_3(BroScr_AnimFaceWaitOnNotify)
            if (as_float(time) != title_global<float>(0x00922eac)) {
                const uint32_t stop_c = 0xc77f1e4d;
                const uint32_t stop_d = 0xc77f1e4d;
                title_function<CallEntFHashHash>(0x001e5270)(0, 0, 0, 0, 0x0019e260, ent, &time, &stop_c,
                                                            &stop_d);  // Scr_CallThread_ent_f_hash_hash_4(BroScr_AnimFaceWaitOnTime)
            }
            Scr_WaitTill(ent, 0xc77f1e4d);
            // This record's index.
            uint32_t found = undefined_int();
            for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(face_waits(ent)->size); ++i) {
                if (face_wait_at(face_waits(ent), i)->id == id) {
                    found = i;
                    break;
                }
            }
            if (ScriptString_Equals(title_function<GetString>(0x00458da0)(ent_script_object(ent), 0x1cc76848),
                                    pointer<const char>(0x00825ea8))) {  // "notify": play the record now
                ScriptString kind_copy;
                ScriptString_Copy(&kind_copy, pointer<ScriptString>(0x00922e90));
                const uint32_t record_notify = face_wait_at(face_waits(ent), found)->notify;
                const uint32_t record_level = face_wait_at(face_waits(ent), found)->level;
                ScriptString sound_copy;
                ScriptString_Copy(&sound_copy, &face_wait_at(face_waits(ent), found)->sound);
                const uint32_t record_anim = face_wait_at(face_waits(ent), found)->anim;
                BroScr_AnimFaceWait(ent, record_anim, sound_copy, record_level, record_notify, kind_copy,
                                    title_global<uint32_t>(0x00922ea4), flag);
            } else if (notify != undefined_handle()) {
                ScriptString_AssignCString(title_function<GetString>(0x00458da0)(ent_script_object(ent), 0xa264fbf5),
                                           pointer<const char>(0x0083ae08));  // "failed"
                queue_notify(ent, notify);
            }
            // The record removed.
            uint8_t* p = reinterpret_cast<uint8_t*>(face_waits(ent)->data);
            for (;;) {
                const DynArray* const waits = face_waits_found(ent);
                if (p == reinterpret_cast<uint8_t*>(waits->data) + waits->size * sizeof(FaceWaitRecord)) break;
                const uint32_t record_id = face_wait_at(face_waits_found(ent), found)->id;
                if (reinterpret_cast<const FaceWaitRecord*>(p)->id == record_id) {
                    DynArray_Erase28(face_waits(ent), p);
                    break;
                }
                p += sizeof(FaceWaitRecord);
            }
            scr_release(&name);
            title_function<void XB_THISCALL(FaceWaitRecord*)>(0x00458fd0)(&record);  // the record's destructor
        } else if (as_float(level) >= static_cast<float>(static_cast<int32_t>(*int_slot(ent, 0x03375c37)))) {
            // Interrupt the face anim playing now and play this one.
            queue_notify(ent, 0xad9b8a7b);
            Scr_WaitFrames(2);
            slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, 0xad9b8a7b);  // Scr_EndOn
            bool defined;
            if (notify != undefined_handle() && *ScrField_hbb50e708_IsDefined(&defined, ent)) {
                ScriptString_AssignCString(ScrField_ha264fbf5(ent), pointer<const char>(0x00826520));  // "interrupted"
                queue_notify(ent, *ScrField_hbb50e708(ent));
            }
            uint32_t* const playing_level = ScrField_h03375c37(ent);
            *playing_level = float_to_script_int(as_float(level));
            ScriptString_Assign(title_function<GetString>(0x00458da0)(ent_script_object(ent), 0x22950d34), &sound);
            *ScrField_hbb50e708(ent) = notify;
            *ScrField_hfd6181bd(ent) = 1;
            *ScrField_haa35a0b5(ent) = 1;
            if (anim) {
                slot<SetAnim7>(0x009225e0)(ent, 0x1c859d65, anim, bits(1.0f), bits(0.1f), bits(1.0f),
                                           1);  // GScr_SetFlaggedAnimKnob
                *ScrField_hfd6181bd(ent) = 0;
                title_function<BeginThreadFunctor1>(0x0011b4d0)(0, 0, 0, 0, 0x001b7bc0,
                                                                ent);  // Scr_BeginThreadFunctor1(BroScr_AnimFaceWaitNotetracks)
            }
            if (sound.rep) {
                uint32_t note = 0x84f611ff;
                if (anim && static_cast<uint8_t>(slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t*)>(0x009225d0)(
                                ent, anim, &note)))  // slot 0x009225d0: the anim has the note
                    Scr_WaitTillArgs(ent, 0x1c859d65, 0x84f611ff, 0, 0);
                if (static_cast<uint8_t>(flag)) {
                    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t)>(0x0092220c)(
                        ent, &sound, 0x91935ca7, 1);  // GScr_PlaySoundWithNotify
                    *ScrField_haa35a0b5(ent) = 0;
                    title_function<BeginThreadFunctor1>(0x0011b4d0)(0, 0, 0, 0, 0x0019e120,
                                                                    ent);  // Scr_BeginThreadFunctor1(BroScr_AnimFaceWaitSetFlag)
                } else {
                    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t)>(0x009221dc)(
                        ent, &sound, 0, 0, 0);  // GScr_PlaySound
                }
            }
            while (*int_slot(ent, 0xaa35a0b5) == 0) Scr_WaitTill(ent, 0x1c87198d);
            *ScrField_h03375c37(ent) = 0;
            title_function<RemoveKey>(0x0011eb30)(&ent, 0x22950d34);  // ScrCall_GScr_GetEntScriptObject_2: removes the key
            title_function<RemoveKey>(0x0011eb30)(&ent, 0xbb50e708);
            if (notify != undefined_handle()) {
                ScriptString_AssignCString(title_function<GetString>(0x00458da0)(ent_script_object(ent), 0xa264fbf5),
                                           pointer<const char>(0x007f9e7c));  // "finished"
                queue_notify(ent, notify);
            }
        } else if (notify != undefined_handle()) {
            ScriptString_AssignCString(title_function<GetString>(0x00458da0)(ent_script_object(ent), 0xa264fbf5),
                                       pointer<const char>(0x0083ae08));  // "failed"
            queue_notify(ent, notify);
        }
    }
    scr_release(&sound);
    scr_release(&kind);
}

void XB_CDECL BroScr_BattleChatter(uint32_t ent, ScriptString type) {
    uint32_t alias_anim = 0;
    if (!slot<uint32_t XB_CDECL(uint32_t)>(0x00922210)(ent) &&  // GScr_IsAlive
        !ScriptString_Equals(&type, pointer<const char>(0x00831f60))) {  // "death"
        scr_release(&type);
        return;
    }
    bool defined;
    if (*ScrField_generic_dialogue_IsDefined(&defined, ent) && *int_slot(ent, 0xec5d0f65) == 0) {
        scr_release(&type);
        return;
    }
    {
        ScriptString nationality;
        bool skip = ScriptString_Equals(ScrActor_GetString_f8_Copy(&ent, &nationality),
                                        pointer<const char>(0x00833508)) &&  // "german"
                    ScriptString_Equals(&type, pointer<const char>(0x008267a4));  // "giveupposition"
        scr_release(&nationality);
        if (skip) {
            scr_release(&type);
            return;
        }
    }
    // The voice: nationality + "_" + (entity number % the nationality's voice count + 1).
    ScriptString voice;
    ScrActor_GetString_f8_Copy(&ent, &voice);
    int32_t voices = 0;
    bool known = true;
    if (ScriptString_Equals(&voice, pointer<const char>(0x008236b8)))  // "american"
        voices = at<int32_t>(anim_object(), 0xec);
    else if (ScriptString_Equals(&voice, pointer<const char>(0x00833510)))  // "french"
        voices = at<int32_t>(anim_object(), 0x5c);
    else if (ScriptString_Equals(&voice, pointer<const char>(0x00833500)))  // "italian"
        voices = at<int32_t>(anim_object(), 0xc4);
    else if (ScriptString_Equals(&voice, pointer<const char>(0x00833508)))  // "german"
        voices = at<int32_t>(anim_object(), 0x12c);
    else
        known = false;
    if (known) {
        const int32_t number = static_cast<int32_t>(slot<uint32_t XB_CDECL(uint32_t)>(0x00922640)(ent)) % voices + 1;  // GScr_GetEntityNumber
        ScriptString_AppendCString(&voice, pointer<const char>(0x0082b1b8));  // "_"
        ScriptString_AppendInt(&voice, number);
    }
    // The chance and (some types) the anim, by the type's hash.
    float chance;
    uint32_t hash;
    switch (*Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&type))) {
    case 0x0039894e: {
        ScriptString name;
        ScriptString_Copy(&name, reinterpret_cast<const ScriptString*>(anim_object() + 0x64));
        uint32_t out;
        alias_anim = *BroScr_GetDefaultString(&out, ent, name);
        chance = as_float(0x3f666666);  // 0.9
        break;
    }
    case 0x06730ae8: {  // "death"
        ScriptString name;
        ScriptString_Copy(&name, reinterpret_cast<const ScriptString*>(anim_object() + 0xa4));
        uint32_t out;
        alias_anim = *BroScr_GetDefaultString(&out, ent, name);
        chance = 1.0f;
        break;
    }
    case 0x84c3473a:
    case 0x81098bb4: {
        ScriptString name;
        ScriptString_Copy(&name, reinterpret_cast<const ScriptString*>(anim_object() + 0x10));
        uint32_t out;
        alias_anim = *BroScr_GetDefaultString(&out, ent, name);
        chance = 0.5f;
        break;
    }
    case 0x25c9495c:
    case 0x195345dd:
    case 0x5119be67:
    case 0x9a87fdcc:
        chance = as_float(0x3e4ccccd);  // 0.2
        break;
    case 0x27711210:
        if (team_is_axis(ent)) {
            ScriptString* const level_voice = reinterpret_cast<ScriptString*>(title_global<uint8_t*>(0x00922ef8) + 0x64);
            if (!level_voice->rep) ScriptString_AssignCString(level_voice, pointer<const char>(0x008236b8));  // "american"
            ScriptString_AppendCString(&voice, pointer<const char>(0x0041853c));  // "v"
            ScriptString_AppendString(&voice, reinterpret_cast<ScriptString*>(title_global<uint8_t*>(0x00922ef8) + 0x64));
        }
        chance = as_float(0x3dcccccd);  // 0.1
        break;
    case 0x00caced4:
    case 0x913e1ab2:
    case 0x9f49c106:
        chance = as_float(0x3dcccccd);  // 0.1
        break;
    case 0x8a317add:
        chance = as_float(0x3ecccccd);  // 0.4
        break;
    case 0xcde22a34:
        chance = team_is_axis(ent) ? as_float(0x3ecccccd) : as_float(0x3e99999a);  // 0.4 / 0.3
        break;
    case 0xc6bf32ed:
    case 0xc8e5ee49:
    case 0xee49c4f0:
        chance = team_is_axis(ent) ? as_float(0x3e99999a) : as_float(0x3ecccccd);  // 0.3 / 0.4
        break;
    default:
        chance = as_float(0x3e99999a);  // 0.3
        break;
    }
    // "generic_" + type + "_" + voice, through BroScr_AnimFaceWait.
    ScriptString alias;
    scr_string_from(&alias, 0x00826798);  // "generic_"
    ScriptString_AppendString(&alias, &type);
    ScriptString_AppendCString(&alias, pointer<const char>(0x0082b1b8));  // "_"
    ScriptString_AppendString(&alias, &voice);
    const uint8_t no = 0;
    const uint32_t notify = undefined_handle();
    const uint32_t chance_word = bits(chance);
    using Call = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                   const ScriptString*, const uint32_t*, const uint32_t*, const ScriptString*,
                                   const uint32_t*, const uint8_t*);
    title_function<Call>(0x001e5100)(0, 0, 0, 0, 0x001c1860, ent, &alias_anim, &alias, &chance_word, &notify,
                                     pointer<ScriptString>(0x00922e90), pointer<uint32_t>(0x00922ea4),
                                     &no);  // Scr_CallThread_ent_anim_str_f_hash_str_f_b(BroScr_AnimFaceWait)
    scr_release(&alias);
    scr_release(&voice);
    scr_release(&type);
}

void XB_CDECL BroScr_ChatterGrenadeAttack(uint32_t ent) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, 0x07b4e7f7);  // Scr_EndOn
    const uint32_t anim = get_int_184(ent) == 0x070b89a4 ? title_global<uint32_t>(0x008cf4a4)   // stand_grenade_return_throw
                                                         : title_global<uint32_t>(0x008ce5bc);  // crouch_grenade_return_throw
    slot<KnobAll>(0x009225e4)(ent, 0x59414211, anim, title_global<uint32_t>(0x008cd7dc), bits(1.0f), bits(0.3f),
                              bits(1.0f), 1);  // GScr_SetFlaggedAnimKnobAll(g_anim_Body)
    Scr_WaitTillArgs(ent, 0x59414211, 0xe6a022d0, 0, 0);
    slot<void XB_CDECL(uint32_t)>(0x00922728)(ent);  // slot 0x00922728
    set_orient_mode(ent, 0x0083adec);  // "face current"
    Scr_Wait(as_float(0x3e4ccccd));    // 0.2
    set_orient_mode(ent, 0x0083addc);  // "face default"
    ScriptString type;
    scr_string_from(&type, 0x008244e4);  // "grenadeattack"
    BroScr_BattleChatter(ent, type);
    Scr_WaitTillArgs(ent, 0x59414211, 0x0032aa25, 0, 0);
    slot<void XB_CDECL(uint32_t)>(0x00922724)(ent);  // GScr_ActorThrowGrenade
    Scr_WaitTillArgs(ent, 0x59414211, 0xcaa6aa08, 0, 0);
}

void XB_CDECL Bro_hide__main(uint32_t ent) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, 0x07b4e7f7);  // Scr_EndOn
    if (get_int_184(ent) == 0x19669cd9) {  // wounded
        ScriptString name;
        scr_string_from(&name, 0x008267b4);  // "hide::main"
        BroScr_PlayFaceAnim(ent, name);
    }
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, 0xc1165808, 0x00385d90);  // BroScr_SetPoseMovement
    if (*handle_slot(ent, 0x764918ae) == 0xc4a729a7) {
        BroScr_KeepAimingAtTarget(ent, title_global<uint32_t>(0x0083ebf8), title_global<uint32_t>(0x0083eba8));  // 0.1, 2
        return;
    }
    for (;;) {
        slot<KnobAll>(0x009225e4)(ent, 0x8e63b2ff, title_global<uint32_t>(0x008ce8bc), title_global<uint32_t>(0x008cd7dc),
                                  bits(1.0f), bits(0.2f), bits(1.0f), 0);  // hideLowWall_1
        Scr_WaitTill(ent, 0x8e63b2ff);
        slot<KnobAll>(0x009225e4)(ent, 0x8e63b2ff, title_global<uint32_t>(0x008ce8c8), title_global<uint32_t>(0x008cd7dc),
                                  bits(1.0f), bits(0.2f), bits(1.0f), 0);  // hideLowWall_2
        Scr_WaitTill(ent, 0x8e63b2ff);
    }
}

void XB_CDECL BroScr_StartHideThread(uint32_t ent) {
    title_function<BeginThreadFunctor1>(0x0011b4d0)(1, 0, 0, 0, 0x001c7880, ent);  // Scr_BeginThreadFunctor1(Bro_hide__main)
}

void XB_CDECL Bro_mg42_stand(uint32_t ent) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922748)(ent, title_global<uint32_t>(0x008cf630));  // ScrActor_SetInt_1370(g_anim_standmg42gunner_aim)
    title_function<BeginThreadFunctor1>(0x0011b4d0)(1, 0, 0, 0, 0x001c28d0, ent);  // (BroScr_TurretPoseStand)
}

void XB_CDECL Bro_mg42_prone(uint32_t ent) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922748)(ent, title_global<uint32_t>(0x008cf204));  // ScrActor_SetInt_1370(g_anim_pronemg42gunner_aim)
    title_function<BeginThreadFunctor1>(0x0011b4d0)(1, 0, 0, 0, 0x001c27a0, ent);  // (BroScr_TurretPoseProne)
}

void XB_CDECL Bro_anim_lmg_gmi(uint32_t ent) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922748)(ent, title_global<uint32_t>(0x008cf210));  // ScrActor_SetInt_1370(g_anim_pronemg42gunner_fire)
    title_function<BeginThreadFunctor1>(0x0011b4d0)(1, 0, 0, 0, 0x001c2670, ent);  // (BroScr_SetDesiredPoseProneMg42)
}

void XB_CDECL BroScr_MeleeAttackAnim(uint32_t ent) {
    ScriptString type;
    scr_string_from(&type, 0x008267c0);  // "meleeattack"
    BroScr_BattleChatter(ent, type);
    slot<KnobAll>(0x009225e4)(ent, 0xcc615b7e, title_global<uint32_t>(0x008ce9d0), title_global<uint32_t>(0x008cd7dc),
                              bits(1.0f), bits(0.1f), bits(1.0f), 0);  // melee_charge2right
    handle_note_tracks(ent, 0xcc615b7e, 0x0018da00);  // BroScr_MeleeNoteTrackHandler
    *handle_slot(ent, 0x2afa5406) = 0x07151174;
    *handle_slot(ent, 0x0ccee5c1) = 0x00385d90;
}

void XB_CDECL BroScr_RecordSetAnimKnobAll(uint32_t ent, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6) {
    BroScr_RecordAnimCommand(ent, 2, 0, a2, a3, a4, a5, a6);
}

void XB_CDECL BroScr_RecordAnimCommand3(uint32_t ent, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6,
                                        uint32_t a7) {
    BroScr_RecordAnimCommand(ent, 3, a2, a3, a4, a5, a6, a7);
}

void XB_CDECL BroScr_ScriptedAnimInit(uint32_t ent) {
    const uint32_t body = title_global<uint32_t>(0x008cd7dc);  // g_anim_Body
    at<uint32_t>(pointer<uint8_t>(ScriptKeyTable_GetStruct28(ent_script_object(ent), 0x12993a44)), 0x24) = body;
    *handle_slot(ent, 0x2181000b) = 0x22a9df44;
    BroScr_ResetAnimStruct_12993a44(ent);
    bool defined;
    if (*ScrField_scripted_dialogue_IsDefined(&defined, ent) || *ScrField_h3d130364_IsDefined(&defined, ent)) {
        const uint32_t time = title_global<uint32_t>(0x00922ea4);
        ScriptString kind;
        ScriptString_Copy(&kind, pointer<ScriptString>(0x00922e90));
        ScriptString sound;
        ScriptString_Copy(&sound, title_function<GetString>(0x00458da0)(ent_script_object(ent), 0xbf1ecaaa));  // scripted_dialogue
        const uint32_t anim = *key_slot(ent, 0x3d130364, 0);
        BroScr_StartAnimFaceWait(ent, anim, sound, 0x3f666666, 0x0cbd9c3d, kind, time);  // level 0.9
        title_function<RemoveKey>(0x0011eb30)(&ent, 0x3d130364);  // ScrCall_GScr_GetEntScriptObject_2: removes the key
        title_function<RemoveKey>(0x0011eb30)(&ent, 0xbf1ecaaa);
    }
    if (*ScrField_h210b183c_IsDefined(&defined, ent)) {
        const ScriptString* const from = title_function<GetString>(0x00458da0)(ent_script_object(ent), 0x210b183c);
        ScriptString_Assign(title_function<GetString>(0x00458da0)(ent_script_object(ent), 0xe0e46b1d), from);
    }
}

void XB_CDECL BroScr_RechamberRifle(uint32_t ent) {
    if (static_cast<int32_t>(*int_slot(ent, 0x0feddbea)) <= 0) return;  // no rounds left
    if (*int_slot(ent, 0xc786fa48) != 0) {  // rechamber pending: the bolt anim and sound
        slot<SetAnim6>(0x009225dc)(ent, title_global<uint32_t>(0x008cf6a8), bits(1.0f), bits(0.1f), bits(1.0f),
                                   0);  // GScr_SetAnim(g_anim_upperbody)
        slot<SetAnim7>(0x009225e0)(ent, 0x5e10573a, title_global<uint32_t>(0x008cf6b4), bits(1.0f), bits(0.1f), bits(1.0f),
                                   0);  // GScr_SetFlaggedAnimKnob(g_anim_upperbody_rechamber)
        {
            ScriptString sound;
            scr_string_from(&sound, 0x00826578);  // "weap_kar98k_rechamber"
            slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t)>(0x009221dc)(ent, &sound, 0, 0,
                                                                                                0);  // GScr_PlaySound
            scr_release(&sound);
        }
        handle_note_tracks(ent, 0x5e10573a);
        *ScrField_hc786fa48(ent) = 0;
        slot<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x009225d4)(ent, title_global<uint32_t>(0x008cf6a8),
                                                                     bits(0.1f));  // GScr_ClearAnim
        return;
    }
    title_function<BeginThreadFunctor1>(0x0011b4d0)(0, 0, 0, 0, 0x001bfe70, ent);  // Scr_BeginThreadFunctor1(BroScr_LookAtEnemyLoop)
    int32_t shots;
    uint32_t fire;
    if (*BroScr_GetWeaponFireType(&fire, ent) == 0x00315aef) {  // automatic
        shots = random_int(6) + 3;
    } else if (*BroScr_GetWeaponFireType(&fire, ent) == 0x003819c9) {  // semi-automatic
        shots = random_int(2) + 2;
    } else {
        shots = 1;
        *ScrField_hc786fa48(ent) = 1;
    }
    uint32_t out;
    uint32_t yaw = *BroScr_GetAbsYawToTarget(&out, ent);
    for (int32_t i = 0; i < shots; ++i) {
        if (!(static_cast<int32_t>(*int_slot(ent, 0x0feddbea)) > 0)) break;
        if (static_cast<float>(25.0f > as_float(yaw)) == 0.0f) break;
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x0092269c)(ent, at<uint32_t>(anim_object(), 0x154));  // GScr_ActorShoot
        const uint32_t left = *ScrField_h0feddbea(ent);
        *ScrField_h0feddbea(ent) = left - 1;
        Scr_Wait(as_float(*BroScr_GetShotInterval(&out, ent)));
        yaw = *BroScr_GetAbsYawToTarget(&out, ent);
    }
    queue_notify(ent, 0x807e5a80);
}

void XB_CDECL BroScr_RechamberRifleDelayed(uint32_t ent, uint32_t end, uint32_t delay) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, 0x07b4e7f7);  // Scr_EndOn
    if (end != undefined_handle()) slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, end);
    if (as_float(delay) != title_global<float>(0x00922eac)) Scr_Wait(as_float(delay));
    BroScr_RechamberRifle(ent);
}

void XB_CDECL BroScr_RamboFire(uint32_t ent) {
    slot<KnobAll>(0x009225e4)(ent, 0x54e3b2b6, title_global<uint32_t>(0x008cf240), title_global<uint32_t>(0x008cd7dc),
                              bits(1.0f), bits(0.1f), bits(1.0f), 0);  // ramboanticipation
    handle_note_tracks(ent, 0x54e3b2b6);
    uint32_t out;
    if (*BroScr_GetWeaponFireType(&out, ent) == 0x00315aef) {  // automatic: a burst while the muzzle is clear
        slot<KnobAll>(0x009225e4)(ent, 0xa263b28f, title_global<uint32_t>(0x008cf24c), title_global<uint32_t>(0x008cd7dc),
                                  bits(1.0f), bits(0.1f), bits(1.0f), 0);  // ramboautomaticfire
        BroScr_CheckCanShoot(&out, ent, title_global<uint32_t>(0x00922ebc));
        uint8_t clear;
        if (!*BroScr_GetMuzzleOriginCall(&clear, ent, title_global<uint32_t>(0x00922e84), 0)) return;
        for (int32_t shots = random_int(5) + 5; shots > 0; --shots) {
            slot<void XB_CDECL(uint32_t, uint32_t)>(0x0092269c)(ent, at<uint32_t>(anim_object(), 0x58));  // GScr_ActorShoot
            Scr_WaitTillArgs(ent, 0xa263b28f, 0x0032aa25, 0, 0);
        }
        return;
    }
    BroScr_CheckCanShoot(&out, ent, title_global<uint32_t>(0x00922ebc));
    for (int32_t shots = random_int(3) + 2; shots > 0; --shots) {  // semi-automatic: single shots
        const ScrVec3 zero = {0, 0, 0};  // 0.0f words
        uint8_t clear;
        if (!*BroScr_GetMuzzleOrigin(&clear, ent, zero, title_global<uint32_t>(0x00922e84), 0)) continue;
        const uint32_t interval = *BroScr_GetShotInterval(&out, ent);
        slot<KnobAll>(0x009225e4)(ent, 0xa263b28f, title_global<uint32_t>(0x008cf258), title_global<uint32_t>(0x008cd7dc),
                                  bits(1.0f), bits(0.1f), bits(1.0f), 0);  // rambosemiautofire
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x0092269c)(ent, at<uint32_t>(anim_object(), 0x58));  // GScr_ActorShoot
        Scr_Wait(as_float(interval));
    }
}

void XB_CDECL BroScr_AimUntilCanShoot(uint32_t ent, uint32_t anim_a, uint32_t anim_b, uint32_t anim_c, uint32_t time,
                                      ScriptString name) {
    if (!name.rep) ScriptString_AssignCString(&name, pointer<const char>(0x00826790));  // "FIXME"
    ScriptString unused;
    ScrCall_ScrActor_GetString_1384(&ent, &unused);
    float elapsed = 0.0f;
    uint32_t out;
    for (;;) {
        if (*BroScr_CheckCanShoot(&out, ent, title_global<uint32_t>(0x00922ebc))) break;
        if (static_cast<float>(as_float(time) > elapsed) == 0.0f) break;
        if (slot<uint32_t XB_CDECL(uint32_t)>(0x0092271c)(ent)) break;  // GScr_ActorHasPendingSentientSlot
        for (int32_t i = 0; i < 4; ++i) {
            if (*BroScr_CheckCanShoot(&out, ent, 0)) break;
            if (static_cast<float>(as_float(time) > elapsed) == 0.0f) break;
            slot<SetAnim6>(0x009225d8)(ent, anim_a, bits(1.0f), bits(0.1f), bits(1.0f), 0);  // GScr_SetAnimKnob
            slot<SetAnim6>(0x009225d8)(ent, anim_b, bits(1.0f), bits(0.1f), bits(1.0f), 0);
            slot<SetAnim6>(0x009225d8)(ent, anim_c, bits(1.0f), bits(0.1f), bits(1.0f), 0);
            Scr_Wait(0.25f);
            elapsed = elapsed + 0.25f;
        }
    }
    scr_release(&unused);
    scr_release(&name);
}

void XB_CDECL BroScr_ReloadLauncher(uint32_t ent) {
    uint32_t type;
    BroScr_GetWeaponType(&type, ent);
    uint32_t pending;
    if (!*BroScr_GetFieldUnlessAutoOrSemi(&pending, ent)) return;
    if (type == 0xa39d17dd || type == 0x318fbe80 || type == 0x45963119) {  // reload in place
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922d34)(ent, slot<uint32_t XB_CDECL(uint32_t)>(0x00922d38)(ent));  // ScrActor_SetInt_f0(GetInt_f4)
        *reinterpret_cast<uint8_t*>(ScrField_h552cfcab(ent, 0)) = 1;
        BroScr_PutGunInHand(ent, 0x07151174);
        BroScr_RefillClip(ent);
        *int_slot(ent, 0xc786fa48) = 0;
        return;
    }
    title_function<BeginThreadFunctor1>(0x0011b4d0)(0, 0, 0, 0, 0x0019d460, ent);  // Scr_BeginThreadFunctor1(BroScr_ReloadLauncherTimeout)
    title_function<BeginThreadFunctor1>(0x0011b4d0)(0, 0, 0, 0, 0x001c6040, ent);  // (Bro_combat__rechamber)
    Scr_WaitTill(ent, 0x5e12d3d2);
}

void XB_CDECL BroScr_AimIdleAtTarget(uint32_t ent, uint32_t first, uint32_t second) {
    BroScr_SetAimBlendTimes(first, second);
    BroScr_PlayIdleAnim(ent);
    BroScr_KeepAimingAtTarget(ent, title_global<uint32_t>(0x009230ec), title_global<uint32_t>(0x0092379c));
}

void XB_CDECL BroScr_GiveUpPositionChatter(uint32_t ent) {
    for (;;) {
        Scr_WaitTill(ent, 0xe2a210cf);  // trigger
        ScriptString type;
        scr_string_from(&type, 0x008267a4);  // "giveupposition"
        BroScr_BattleChatter(ent, type);
        uint8_t looked;
        BroScr_TryLookAtPlayer(&looked, ent);
        Scr_Wait(5.0f);
    }
}

void XB_CDECL BroScr_BattleChatterOnDeath(uint32_t ent) {
    ScriptString type;
    scr_string_from(&type, 0x00831f60);  // "death"
    BroScr_BattleChatter(ent, type);
}

void XB_CDECL BroScr_AnimScriptEntInit(uint32_t ent) {
    if (at<uint32_t>(anim_object(), 0x158) != undefined_int()) return;  // once
    at<uint32_t>(anim_object(), 0x158) = 1;
    if (slot<int32_t XB_CDECL(uint32_t)>(0x009222dc)(0x00826c64) <= 0)  // GScr_GetCvarInt("animscriptent")
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x009222e4)(0x00826c64, 0x00832c8c);  // GScr_SetCvar(.., "-1")
    title_function<BeginThreadFunctor1>(0x0011b4d0)(0, 0, 0, 0, 0x001837f0, level_handle());  // (BroScr_EmptyFunc_54)
    at<uint32_t>(anim_object(), 0xa0) = 1;
    at<uint32_t>(anim_object(), 0x190) = 0;
    at<uint32_t>(anim_object(), 0x38) = 0;
    at<uint32_t>(anim_object(), 0x9c) = title_global<uint32_t>(0x0083ed4c);   // 4096
    at<uint32_t>(anim_object(), 0x198) = title_global<uint32_t>(0x00826c60);  // 253069.36
    at<uint32_t>(anim_object(), 0x118) = title_global<uint32_t>(0x007f5144);  // 40000
    at<uint32_t>(anim_object(), 0x94) = title_global<uint32_t>(0x007f5144);
    at<uint32_t>(anim_object(), 0x90) = title_global<uint32_t>(0x0083f150);   // 490000
    at<uint32_t>(anim_object(), 0x68) = title_global<uint32_t>(0x007f71fc);   // 90000
    at<uint32_t>(anim_object(), 0xc0) = title_global<uint32_t>(0x0083ec30);   // 50
    at<uint32_t>(anim_object(), 0x58) = title_global<uint32_t>(0x0083eb20);   // 1
    at<uint32_t>(anim_object(), 0x154) = title_global<uint32_t>(0x0083eb24);  // 0.5
    at<uint32_t>(anim_object(), 0x18c) = 10000;
    at<uint32_t>(anim_object(), 0x8) = 6000;
    at<uint32_t>(anim_object(), 0x15c) = title_global<uint32_t>(0x0083eb24);  // 0.5
    at<uint32_t>(anim_object(), 0x54) = title_global<uint32_t>(0x0083ebf8);   // 0.1
    BroScr_InitPoseMovementTable();
    title_function<void XB_CDECL(uint32_t)>(0x001a2e70)(ent);  // BroScr_WeaponClassFor (agent-b list)
    at<uint32_t>(anim_object(), 0xec) = 8;  // the nationalities' voice counts
    at<uint32_t>(anim_object(), 0x5c) = 1;
    at<uint32_t>(anim_object(), 0xc4) = 1;
    at<uint32_t>(anim_object(), 0x12c) = 1;
    at<uint32_t>(anim_object(), 0xa8) = 'a';
    at<uint32_t>(anim_object(), 0xac) = 'b';
    at<uint32_t>(anim_object(), 0xb0) = 0;
    at<uint32_t>(anim_object(), 0x11c) = 'a';
    at<uint32_t>(anim_object(), 0x120) = 0;
    at<uint32_t>(anim_object(), 0x124) = 'c';
    at<uint32_t>(anim_object(), 0xb4) = 'a';
    at<uint32_t>(anim_object(), 0xb8) = 'b';
    at<uint32_t>(anim_object(), 0xbc) = 'c';
    ScriptString_AssignCString(reinterpret_cast<ScriptString*>(anim_object() + 0x114),
                               pointer<const char>(0x00826c44));  // "abcdefghijklmnopqrstuvwxyz"
}

void XB_CDECL BroScr_AnimScriptEntInitIfValid(uint32_t ent) {
    if (!entity_handle_is_valid(ent)) return;
    BroScr_AnimScriptEntInit(ent);
    BroScr_AnimScriptEntSetDefaults(ent);
}

uint32_t XB_CDECL Bro_anim_main_init(uint32_t ent) {
    if (!entity_handle_is_valid(ent)) return 0;
    BroScr_AnimScriptEntInit(ent);
    BroScr_AnimScriptEntSetDefaults(ent);
    return 0;
}

void XB_CDECL Bro_anim_main_scripted(uint32_t ent) {
    BroScr_ScriptedAnimInit(ent);
    begin_anim_thread(ent, 0x00184020);  // BroScr_WaitKillAnimScript
}

// The anim-script main entries: the script as a begun thread.
void XB_CDECL Bro_anim_main_pain(uint32_t ent) { begin_anim_thread(ent, 0x001c7be0); }  // BroScr_AnimScript_Pain
void XB_CDECL Bro_anim_main_concealment_stand(uint32_t ent) { begin_anim_thread(ent, 0x001d6420); }  // BroScr_AnimScript_ConcealmentStand
void XB_CDECL Bro_anim_main_cover_wide_left(uint32_t ent) { begin_anim_thread(ent, 0x001d7a60); }  // BroScr_AnimScript_CoverWideLeft
void XB_CDECL Bro_anim_main_cover_wide_right(uint32_t ent) { begin_anim_thread(ent, 0x001d7d70); }  // BroScr_AnimScript_CoverWideRight
void XB_CDECL Bro_anim_main_death(uint32_t ent) { begin_anim_thread(ent, 0x001d8080); }  // BroScr_AnimScript_Death
void XB_CDECL Bro_anim_main_concealment_crouch(uint32_t ent) { begin_anim_thread(ent, 0x001dba80); }  // BroScr_AnimScript_ConcealmentCrouch
void XB_CDECL Bro_anim_main_cover_crouch(uint32_t ent) { begin_anim_thread(ent, 0x001dbb90); }  // BroScr_AnimScript_CoverCrouch
void XB_CDECL Bro_anim_main_run(uint32_t ent) { begin_anim_thread(ent, 0x001d99e0); }  // Bro_run__main
void XB_CDECL Bro_anim_main_combat(uint32_t ent) { begin_anim_thread(ent, 0x001dce30); }  // BroScr_CombatMain
void XB_CDECL Bro_anim_main_cover_left(uint32_t ent) { begin_anim_thread(ent, 0x001dd180); }  // BroScr_AnimScript_CoverLeft
void XB_CDECL Bro_anim_main_cover_right(uint32_t ent) { begin_anim_thread(ent, 0x001dd510); }  // BroScr_AnimScript_CoverRight
void XB_CDECL Bro_anim_main_cover_stand(uint32_t ent) { begin_anim_thread(ent, 0x001dd750); }  // BroScr_AnimScript_CoverStand
void XB_CDECL Bro_anim_main_concealment_prone(uint32_t ent) { begin_anim_thread(ent, 0x001de000); }  // BroScr_AnimScript_ConcealmentProne
void XB_CDECL Bro_anim_main_cover_prone(uint32_t ent) { begin_anim_thread(ent, 0x001de040); }  // BroScr_AnimScript_CoverProne
void XB_CDECL Bro_anim_main_stop(uint32_t ent) { begin_anim_thread(ent, 0x001e3520); }  // Bro_stop__main
void XB_CDECL Bro_anim_main_reacquire(uint32_t ent) { begin_anim_thread(ent, 0x001e3c80); }  // BroScr_AnimScript_Reacquire

// --- push 3 c: combat / cover / pain anim scripts (thread switch, not registered) ---

namespace {

using KnobAll2 = uint32_t XB_CDECL(const uint32_t*, const uint32_t*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

inline void end_on(uint32_t ent, uint32_t hash) { slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, hash); }  // Scr_EndOn
// The debug text (key 0xc25f482c) set from a title string: inline (ScriptKeyTable_GetString) or
// through the ScrField accessor.
inline void debug_text(uint32_t ent, uint32_t text) {
    ScriptString_AssignCString(title_function<GetString>(0x00458da0)(ent_script_object(ent), 0xc25f482c),
                               pointer<const char>(text));
}
inline void debug_text_field(uint32_t ent, uint32_t text) {
    ScriptString_AssignCString(ScrField_hc25f482c(ent), pointer<const char>(text));
}
// A debug print of a field: the text built and released around the field's read.
inline void debug_print_idleset(uint32_t ent, uint32_t text) {
    ScriptString message;
    scr_string_from(&message, text);
    ScrField_anim_idleset(ent);
    scr_release(&message);
}
inline uint32_t body_root() { return title_global<uint32_t>(0x008cd7dc); }  // g_anim_Body
// GScr_PlaySound(ent, title string, 0, 0, 0) with a temporary string.
inline void play_sound(uint32_t ent, uint32_t text) {
    ScriptString sound;
    scr_string_from(&sound, text);
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t)>(0x009221dc)(ent, &sound, 0, 0, 0);
    scr_release(&sound);
}

}  // namespace

void XB_CDECL Bro_combat__rechamber(uint32_t ent) {
    end_on(ent, 0x5e12d3d2);
    end_on(ent, 0x07b4e7f7);
    debug_text(ent, 0x0082c0e0);
    if (*handle_slot(ent, 0x2181000b) == 0xc0c70454) slot<void XB_CDECL(uint32_t)>(0x009226a4)(ent);  // Actor_CallStateSlot5
    handle_slot(ent, 0x1de0c645);  // read and dropped
    const float rate = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922230)(0x3e4ccccd) +
                                          title_global<float>(0x0083ec18));  // Scr_RandomFloat(0.2) + 0.9
    debug_text(ent, 0x0082e058);
    uint32_t notify;
    switch (get_int_184(ent)) {
    case 0x070b89a4:  // stand
        switch (*handle_slot(ent, 0x175567a8)) {  // anim_special
        case 0x38948440:  // cover left
            if (*ScrField_anim_idleset(ent) == 0x61) {
                slot<KnobAll>(0x009225e4)(ent, 0x5e10573a, title_global<uint32_t>(0x008ce430), body_root(), 0x3f800000,
                                          0x3e4ccccd, bits(rate), 0);  // g_anim_cornerstandrechamber_left
                debug_text(ent, 0x008266f4);
            } else {
                debug_print_idleset(ent, 0x00826590);
                slot<KnobAll>(0x009225e4)(ent, 0x5e10573a, title_global<uint32_t>(0x008ce2c8), body_root(), 0x3f800000,
                                          0x3e4ccccd, bits(rate), 0);  // g_anim_cornerb_stand_rechamber_left
                debug_text(ent, 0x00826698);
            }
            break;
        case 0xa1cb941d:  // cover right
            if (*ScrField_anim_idleset(ent) == 0x61) {
                notify = 0x5e10573a;
                ScrCall_GScr_SetFlaggedAnimKnobAll_2(&ent, &notify, title_global<uint32_t>(0x008ce43c), body_root(),
                                                     0x3f800000, 0x3e4ccccd, bits(rate));  // g_anim_cornerstandrechamber_right
                debug_text_field(ent, 0x00826628);
            } else {
                debug_print_idleset(ent, 0x008265e8);
                notify = 0x5e10573a;
                ScrCall_GScr_SetFlaggedAnimKnobAll_2(&ent, &notify, title_global<uint32_t>(0x008ce2d4), body_root(),
                                                     0x3f800000, 0x3e4ccccd, bits(rate));  // g_anim_cornerb_stand_rechamber_right
                debug_text_field(ent, 0x008265d0);
            }
            break;
        default:
            slot<KnobAll>(0x009225e4)(ent, 0x5e10573a, title_global<uint32_t>(0x008cf4f8), body_root(), 0x3f800000,
                                      0x3e4ccccd, bits(rate), 0);  // g_anim_stand_rechamber
            debug_text(ent, 0x00826640);
            break;
        }
        break;
    case 0x0738b1a5:  // prone
        slot<KnobAll>(0x009225e4)(ent, 0x5e10573a, title_global<uint32_t>(0x008cf168), body_root(), 0x3f800000,
                                  0x3e4ccccd, bits(rate), 0);  // g_anim_prone_rechamber
        slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009226bc)(
            ent, title_global<uint32_t>(0x008cf1d4), title_global<uint32_t>(0x008cf1c8), 0x3f800000, 0x3dcccccd,
            0x3f800000);  // GScr_ActorSetAimAnimWeights(straight45up, straight45down)
        debug_text(ent, 0x00826654);
        *ScrField_hc786fa48(ent) = 0;
        break;
    case 0xc1165808:  // crouch
        switch (*ScrField_anim_special(ent)) {
        case 0x3124fe8f:  // hide low wall
            debug_text_field(ent, 0x00826680);
            if (*ScrField_anim_idleset(ent) != 0x62) {
                ScriptString empty;
                ScriptString_Copy(&empty, pointer<ScriptString>(0x00922e90));
                BroScr_CoverCrouchReloadAnim(ent, empty);
            }
            notify = 0x5e10573a;
            ScrCall_GScr_SetFlaggedAnimKnobAll_2(&ent, &notify, title_global<uint32_t>(0x008ce8ec), body_root(), 0x3f800000,
                                                 0x3e4ccccd, bits(rate));  // g_anim_hidelowwallb_rechamber
            debug_text_field(ent, 0x00826668);
            break;
        case 0x38948440:  // cover left
            if (*ScrField_anim_idleset(ent) == 0x61) {
                notify = 0x5e10573a;
                ScrCall_GScr_SetFlaggedAnimKnobAll_2(&ent, &notify, title_global<uint32_t>(0x008ce3ac), body_root(),
                                                     0x3f800000, 0x3e4ccccd, bits(rate));  // g_anim_cornercrouchrechamber_left
                debug_text_field(ent, 0x008266f4);
            } else {
                debug_print_idleset(ent, 0x008266b0);
                notify = 0x5e10573a;
                ScrCall_GScr_SetFlaggedAnimKnobAll_2(&ent, &notify, title_global<uint32_t>(0x008ce070), body_root(),
                                                     0x3f800000, 0x3e4ccccd, bits(rate));  // g_anim_cornerb_crouch_rechamber_left
                debug_text_field(ent, 0x00826698);
            }
            break;
        case 0xa1cb941d:  // cover right
            if (*ScrField_anim_idleset(ent) == 0x61) {
                notify = 0x5e10573a;
                ScrCall_GScr_SetFlaggedAnimKnobAll_2(&ent, &notify, title_global<uint32_t>(0x008ce3b8), body_root(),
                                                     0x3f800000, 0x3e4ccccd, bits(rate));  // g_anim_cornercrouchrechamber_right
                debug_text_field(ent, 0x00826750);
            } else {
                debug_print_idleset(ent, 0x00826710);
                notify = 0x5e10573a;
                ScrCall_GScr_SetFlaggedAnimKnobAll_2(&ent, &notify, title_global<uint32_t>(0x008ce07c), body_root(),
                                                     0x3f800000, 0x3e4ccccd, bits(rate));  // g_anim_cornerb_crouch_rechamber_right
                debug_text_field(ent, 0x008265d0);  // the stand text (the title's tails share it)
            }
            break;
        default:
            notify = 0x5e10573a;
            ScrCall_GScr_SetFlaggedAnimKnobAll_2(&ent, &notify, title_global<uint32_t>(0x008ce5ec), body_root(), 0x3f800000,
                                                 0x3e4ccccd, bits(rate));  // g_anim_crouch_rechamber
            debug_text_field(ent, 0x00826768);
            *ScrField_hc786fa48(ent) = 0;
            break;
        }
        break;
    default:
        slot<KnobAll>(0x009225e4)(ent, 0x5e10573a, title_global<uint32_t>(0x008ce5ec), body_root(), 0x3f800000,
                                  0x3e4ccccd, bits(rate), 0);  // g_anim_crouch_rechamber
        debug_text(ent, 0x0082677c);
        break;
    }
    *int_slot(ent, 0xc786fa48) = 0;
    play_sound(ent, 0x00826578);  // "weap_kar98k_rechamber"
    handle_note_tracks(ent, 0x5e10573a);
    debug_text(ent, 0x00826564);  // "Rechambering done"
    queue_notify(ent, 0x5e12d3d2);
}

namespace {
// BroScr_GetFloatOrDefault500 beyond 4 x the anim object's +0x9c, as a script float != 0.
inline bool pain_target_far(uint32_t ent) {
    const float near = as_float(at<uint32_t>(anim_object(), 0x9c)) * title_global<float>(0x007bd0c8);
    uint32_t out;
    return static_cast<float>(as_float(*BroScr_GetFloatOrDefault500(&out, ent)) > near ? 1 : 0) !=
           title_global<float>(0x0083eafc);
}
// The standing pains: ScrCall_GScr_SetFlaggedAnimKnob(ent, notify, anim, 1, 0.1, 1).
inline void stand_pain(const uint32_t* ent, const uint32_t* notify, uint32_t anim) {
    ScrCall_GScr_SetFlaggedAnimKnob(ent, notify, title_global<uint32_t>(anim), 0x3f800000, 0x3dcccccd, 0x3f800000);
}
}  // namespace

void XB_CDECL BroScr_AnimScript_Pain(uint32_t ent) {
    end_on(ent, 0x07b4e7f7);
    BroScr_InitAnimScript(ent, 0x0039894e);
    BroScr_ApplyDesiredAnimPose(ent);
    {
        ScriptString type;
        scr_string_from(&type, 0x00831f68);  // "pain"
        BroScr_BattleChatter(ent, type);
    }
    bool defined;
    if (*ScrField_had6d6fd5_IsDefined(&defined, ent) && *int_slot(ent, 0xad6d6fd5) == 0) return;  // pain allowed
    const uint32_t time = slot<uint32_t XB_CDECL(uint32_t)>(0x00922cd0)(ent);  // ScrActor_GetInt_22c
    const uint32_t location = slot<uint32_t XB_CDECL(uint32_t)>(0x00922ce8)(ent);  // ScrActor_GetInt_240
    BroScr_UpdateWoundedTime(ent, location, static_cast<int32_t>(time));
    if (get_int_184(ent) != 0x0738b1a5)
        slot<SetAnim6>(0x009225d8)(ent, title_global<uint32_t>(0x008ceb20), 0x3f800000, 0x3dcccccd, 0x3f800000,
                                   0);  // GScr_SetAnimKnob(g_anim_pain)
    if (*handle_slot(ent, 0xd82f7174) != 0x0036b1a5) {  // a turret gunner
        uint8_t done;
        if (*BroScr_TryTurretGunnerPain(&done, ent, *handle_slot(ent, 0xd82f7174))) return;
    }
    {
        const uint8_t no = 0;
        using CallEntB = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint8_t*);
        title_function<CallEntB>(0x0052cb60)(0, 0, 0, 0, 0x0018dbc0, ent, &no);  // Scr_CallThread_ent_b_KASSPASS(BroScr_PainAnimByHitLocation)
    }
    uint32_t wounded = *handle_slot(ent, 0xb7f3f874);  // wounded_anim
    uint32_t movement = 0x00385d90;
    uint32_t pose = get_int_184(ent);
    const float health = static_cast<float>(static_cast<int32_t>(slot<uint32_t XB_CDECL(uint32_t)>(0x00922b90)(ent)));  // ScrEnt_GetHealth
    const float ratio = health / static_cast<float>(static_cast<int32_t>(slot<uint32_t XB_CDECL(uint32_t)>(0x00922cd0)(ent)));
    uint32_t notify = 0x94d3b23d;
    bool stop_after = false;  // the BroScr_SetAnimMovementStopAfter thread
    using Yaw = long double XB_CDECL(uint32_t);
    if (*handle_slot(ent, 0x0ccee5c1) == 0x0001c6ce && title_global<float>(0x0083eb3c) > slot<Yaw>(0x00922744)(ent) &&
        slot<Yaw>(0x00922744)(ent) > title_global<float>(0x0083ee2c) &&
        (get_int_184(ent) == 0x070b89a4 || get_int_184(ent) == 0xc1165808)) {  // running, motion within 60 degrees
        if (static_cast<float>(title_global<float>(0x0083eb20) > ratio ? 1 : 0) != 0.0f) {  // hurt
            if (get_int_184(ent) == 0xc1165808) {
                ScrCall_SetFlaggedAnimKnobByRef(&ent, &notify, title_global<uint32_t>(0x008ce754), 0x3f800000, 0x3dcccccd,
                                                0x3f800000);  // g_anim_crouchrun_painfall
                movement = 0x00385d90;
                pose = 0xc1165808;
            } else {
                ScrCall_SetFlaggedAnimKnobByRef(&ent, &notify, title_global<uint32_t>(0x008cf30c), 0x3f800000, 0x3dcccccd,
                                                0x3f800000);  // g_anim_run2crawl
                movement = 0x00385d90;
                wounded = 0x06088a8c;
                pose = 0x19669cd9;
            }
        } else if (static_cast<float>(title_global<float>(0x0083eba8) > ratio ? 1 : 0) != 0.0f) {  // 2
            ScrCall_SetFlaggedAnimKnobByRef(&ent, &notify, title_global<uint32_t>(0x008cf30c), 0x3f800000, 0x3dcccccd,
                                            0x3f800000);  // g_anim_run2crawl
            movement = 0x003a09eb;
            wounded = 0x06088a8c;
            pose = 0x19669cd9;
        } else {
            ScrCall_GScr_SetAnimKnobAll(&ent, title_global<uint32_t>(0x008cf66c), body_root(), 0x3f800000, 0x3dcccccd,
                                        0x3f800000);  // g_anim_stumble1
            return;
        }
    } else {
        switch (get_int_184(ent)) {
        case 0x0030880b:
            slot<SetAnim7>(0x009225e0)(ent, notify, title_global<uint32_t>(0x008cd7ac), 0x3f800000, 0x3dcccccd,
                                       0x3f800000, 0);  // GScr_SetFlaggedAnimKnob(g_anim_back_pain)
            pose = 0xc1165808;
            break;
        case 0x070b89a4:  // stand: by hit location
            switch (slot<uint32_t XB_CDECL(uint32_t)>(0x00922ce8)(ent)) {
            case 0x00359844:
            case 0x0036980b:
            case 0x78b45d51:
            case 0x792bc151:
            case 0xd667b8d4:
                if (static_cast<float>(title_global<float>(0x0083eb20) > ratio ? 1 : 0) != title_global<float>(0x0083eafc) &&
                    pain_target_far(ent)) {
                    if (random_int(100) < 50) {
                        stand_pain(&ent, &notify, 0x008cf4e0);  // g_anim_stand_pain_hunch
                    } else {
                        stand_pain(&ent, &notify, 0x008cf4bc);  // g_anim_stand_pain_falltohand
                    }
                    pose = 0xc1165808;
                } else {
                    if (random_int(100) > 50) {
                        stand_pain(&ent, &notify, 0x008cf4c8);  // g_anim_stand_pain_front_lshoulder
                    } else {
                        stand_pain(&ent, &notify, 0x008cf4d4);  // g_anim_stand_pain_front_rshoulder
                    }
                    pose = 0x070b89a4;
                }
                stop_after = true;
                break;
            case 0x1a2ddf3d:
            case 0x1bb2433d:
            case 0x3cb25fb7:
            case 0x3d2dc3b7:
            case 0x6dc2dea5:
            case 0xedcc9bb6:  // legs
                ScrCall_SetFlaggedAnimKnobByRef(&ent, &notify, title_global<uint32_t>(0x008cf4ec), 0x3f800000, 0x3dcccccd,
                                                0x3f800000);  // g_anim_stand_pain_leghit
                pose = 0xc1165808;
                break;
            case 0x9a2dd287:
            case 0x9bb24e87:
                stand_pain(&ent, &notify, 0x008cf4c8);  // g_anim_stand_pain_front_lshoulder
                pose = 0x070b89a4;
                stop_after = true;
                break;
            case 0xbcb2520d:
            case 0xbd2dce0d:
                stand_pain(&ent, &notify, 0x008cf4d4);  // g_anim_stand_pain_front_rshoulder
                pose = 0x070b89a4;
                stop_after = true;
                break;
            default: {
                const int32_t r = random_int(100);
                if (r < 25) {
                    stand_pain(&ent, &notify, 0x008cf4c8);  // g_anim_stand_pain_front_lshoulder
                    pose = 0x070b89a4;
                } else if (r < 50) {
                    stand_pain(&ent, &notify, 0x008cf4d4);  // g_anim_stand_pain_front_rshoulder
                    pose = 0x070b89a4;
                } else if (r < 75) {
                    stand_pain(&ent, &notify, 0x008cf4e0);  // g_anim_stand_pain_hunch
                    pose = 0xc1165808;
                } else {
                    stand_pain(&ent, &notify, 0x008cf4bc);  // g_anim_stand_pain_falltohand
                    pose = 0xc1165808;
                }
                stop_after = true;
                break;
            }
            }
            break;
        case 0x0738b1a5:  // prone
        {
            const uint32_t anim = random_int(2) ? title_global<uint32_t>(0x008cf15c)    // g_anim_prone_painb_holdhead
                                                : title_global<uint32_t>(0x008cf150);  // g_anim_prone_paina_holdchest
            slot<SetAnim7>(0x009225e0)(ent, notify, anim, 0x3f800000, 0x3dcccccd, 0x3f800000, 0);  // GScr_SetFlaggedAnimKnob
        }
            slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009226bc)(
                ent, title_global<uint32_t>(0x008cf1d4), title_global<uint32_t>(0x008cf1c8), 0x3f800000, 0x3dcccccd,
                0x3f800000);  // GScr_ActorSetAimAnimWeights(straight45up, straight45down)
            break;
        case 0x19669cd9:
        case 0xc1165808:
            random_int(2);  // drawn and dropped
            slot<SetAnim7>(0x009225e0)(ent, notify, title_global<uint32_t>(0x008ce5e0), 0x3f800000, 0x3dcccccd,
                                       0x3f800000, 0);  // g_anim_crouch_pain_holdStomach
            pose = 0xc1165808;
            stop_after = true;
            break;
        default: {
            ScriptString message;
            scr_string_from(&message, 0x008267cc);  // "Unexpected_anim_pose_value + self.anim_pose"
            scr_release(&message);
            break;
        }
        }
    }
    if (stop_after) {
        const float after = title_global<float>(0x0083eca0);  // 0.2
        using CallEntF = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const float*);
        title_function<CallEntF>(0x001e5370)(0, 0, 0, 0, 0x0019f9d0, ent, &after);  // Scr_CallThread_ent_f(BroScr_SetAnimMovementStopAfter)
        movement = 0x00385d90;
    }
    set_orient_mode(ent, 0x0083adec);  // "face current"
    handle_note_tracks(ent, notify);
    set_int_184(ent, pose);
    *ScrField_anim_movement(ent) = movement;
    *ScrField_wounded_anim(ent) = wounded;
    set_orient_mode(ent, 0x0083addc);  // "face default"
}

void XB_CDECL BroScr_RecordSetAnimKnob(uint32_t ent, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5) {
    BroScr_RecordAnimCommand(ent, 1, 0, a2, anim_after(), a3, a4, a5);
}

void XB_CDECL BroScr_FallbackSelectActors(DynArray actors, int32_t first, int32_t end) {
    uint8_t* const level = title_global<uint8_t*>(0x00922ef8);  // g_levelScriptObject
    if (ScriptString_Equals(reinterpret_cast<const ScriptString*>(level + 0x1c4), pointer<const char>(0x00826898)) &&
        get_time() > at<int32_t>(title_global<uint8_t*>(0x00922ef8), 0xe0)) {  // "dawnville", the next chatter time
        ScriptString line;
        ScriptString_FromRep(&line, nullptr);
        for (int32_t i = first; i < end; ++i) {
            if (static_cast<uint32_t>(i) >= actors.size)
                DynArray_Resize(reinterpret_cast<uint8_t*>(&actors), i + 1, i + 4);
            if (!slot<uint32_t XB_CDECL(uint32_t)>(0x00922210)(actors.data[i])) continue;  // GScr_IsAlive
            const int32_t now = get_time();
            at<int32_t>(title_global<uint8_t*>(0x00922ef8), 0xe0) = random_int(2000) + now + 2500;
            const float count = static_cast<float>(static_cast<int32_t>(actors.size));
            const int32_t part = static_cast<int32_t>(count * title_global<float>(0x0083ec44));  // 0.4
            const float some = part == static_cast<int32_t>(undefined_int()) ? title_global<float>(0x00922eac)
                                                                              : static_cast<float>(part);
            uint32_t text;
            if (random_int(100) > 50) {
                if (count - some > title_global<float>(0x0083eb20)) {  // 1
                    if (random_int(100) > 66) text = 0x00826878;       // "dawnville_defensive_german_1"
                    else if (random_int(100) > 66) text = 0x00826858;  // "..._2"
                    else text = 0x008267f8;                            // "..._3"
                } else {
                    if (random_int(100) > 66) text = 0x00826838;       // "..._4"
                    else if (random_int(100) > 66) text = 0x00826818;  // "..._5"
                    else text = 0x00826878;                            // "..._1"
                }
            } else if (static_cast<float>(some > title_global<float>(0x0083eb20) ? 1 : 0) != title_global<float>(0x0083eafc)) {
                if (random_int(100) > 66) text = 0x00826858;       // "..._2"
                else if (random_int(100) > 66) text = 0x008267f8;  // "..._3"
                else text = 0x00826838;                            // "..._4"
            } else {
                if (random_int(100) > 66) text = 0x00826818;       // "..._5"
                else if (random_int(100) > 66) text = 0x00826878;  // "..._1"
                else text = 0x00826858;                            // "..._2"
            }
            ScriptString_AssignCString(&line, pointer<const char>(text));
            ScriptString empty;
            ScriptString_Copy(&empty, pointer<ScriptString>(0x00922e90));
            ScriptString sound;
            ScriptString_Copy(&sound, &line);
            if (static_cast<uint32_t>(i) >= actors.size)
                DynArray_Resize(reinterpret_cast<uint8_t*>(&actors), i + 1, i + 4);
            BroScr_StartAnimFaceWait(actors.data[i], title_global<uint32_t>(0x00922ea8), sound, 0x3f800000,
                                     undefined_handle(), empty, title_global<uint32_t>(0x00922ea4));
            break;
        }
        scr_release(&line);
    }
    if (actors.data) Scr_Free(actors.data);
}

namespace {
// The reload plays: ScrCall_GScr_SetFlaggedAnimKnobAll_2(ent, 0x047f1336, anim, g_anim_Body, 1, 0.1, 1).
inline void reload_knob_all(uint32_t* ent, uint32_t anim) {
    uint32_t notify = 0x047f1336;
    ScrCall_GScr_SetFlaggedAnimKnobAll_2(ent, &notify, title_global<uint32_t>(anim), body_root(), 0x3f800000, 0x3dcccccd,
                                         0x3f800000);
}
// The upper-body reloads: g_anim_upperbody on (ScrCall_GScr_SetAnim), then `anim` flagged 0x047f1336.
inline void reload_upper_body(uint32_t* ent, uint32_t anim) {
    ScrCall_GScr_SetAnim(ent, title_global<uint32_t>(0x008cf6a8), 0x3f800000, 0x3dcccccd, 0x3f800000);
    uint32_t notify = 0x047f1336;
    ScrCall_SetFlaggedAnimKnobByRef(ent, &notify, title_global<uint32_t>(anim), 0x3f800000, 0x3dcccccd, 0x3f800000);
}
inline void clear_upper_body(uint32_t ent) {
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x009225d4)(ent, title_global<uint32_t>(0x008cf6a8),
                                                                 0x3dcccccd);  // GScr_ClearAnim(g_anim_upperbody, 0.1)
}
}  // namespace

void XB_CDECL BroScr_TryReload(uint32_t ent, uint32_t threshold, uint32_t anim) {
    uint32_t type;
    BroScr_GetWeaponType(&type, ent);
    if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922d30)(ent) != 0x0036b1a5) {  // ScrActor_GetInt_f0
        uint32_t clip;
        BroScr_GetWeaponClipSize(&clip, ent);  // read and dropped
    }
    if (type == 0xa39d17dd) {  // launcher
        BroScr_ReloadLauncher(ent);
        return;
    }
    uint8_t need;
    if (!*BroScr_NeedsReload(&need, ent, threshold)) return;
    if (*ScrField_anim_script(ent) == 0xc0c70454) slot<void XB_CDECL(uint32_t)>(0x009226a4)(ent);  // Actor_CallStateSlot5
    ScrField_anim_gunhand(ent);  // read and dropped
    BroScr_RefillClip(ent);
    *ScrField_hc786fa48(ent) = 0;
    if (anim) {
        slot<KnobAll>(0x009225e4)(ent, 0x047f1336, anim, body_root(), 0x3f800000, 0x3dcccccd, 0x3f800000, 0);
        handle_note_tracks(ent, 0x047f1336);
        return;
    }
    if (type == 0x45963119) {  // bazooka
        slot<KnobAll>(0x009225e4)(ent, 0x047f1336, title_global<uint32_t>(0x008cd818), body_root(), 0x3f800000, 0x3dcccccd,
                                  0x3f800000, 0);  // g_anim_c_prj_bazooka_reload
        handle_note_tracks(ent, 0x047f1336);
        clear_upper_body(ent);
        return;
    }
    uint32_t out;
    if (*BroScr_GetWeaponType(&out, ent) == 0xe6a85d8c) {  // pistol
        if (*ScrField_anim_movement(ent) == 0x00385d90) {
            if (*ScrField_anim_special(ent) != 0x0036b1a5) return;
            if (get_int_184(ent) == 0xc1165808) {
                reload_knob_all(&ent, 0x008cecf4);  // g_anim_pistol_crouch_reload
            } else if (get_int_184(ent) == 0x070b89a4) {
                reload_knob_all(&ent, 0x008cf060);  // g_anim_pistol_stand_reload
            } else {
                return;
            }
        } else {
            if (get_int_184(ent) != 0x070b89a4) return;
            reload_upper_body(&ent, 0x008cf06c);  // g_anim_pistol_stand_reload_upperbody
        }
        handle_note_tracks(ent, 0x047f1336);
        return;
    }
    if (get_int_184(ent) == 0x070b89a4) {
        if (*ScrField_anim_movement(ent) == 0x0001c6ce)
            reload_upper_body(&ent, 0x008cf2f4);  // g_anim_reload_stand_rifle_upperbody
        else
            reload_knob_all(&ent, 0x008cf2e8);  // g_anim_reload_stand_rifle
    } else if (get_int_184(ent) == 0xc1165808) {
        if (*ScrField_anim_movement(ent) == 0x0001c6ce) {
            reload_upper_body(&ent, 0x008cf2f4);  // g_anim_reload_stand_rifle_upperbody
        } else if (*ScrField_anim_special(ent) == 0x3124fe8f) {  // hide low wall
            if (*ScrField_anim_idleset(ent) != 0x62) {
                ScriptString empty;
                ScriptString_Copy(&empty, pointer<ScriptString>(0x00922e90));
                BroScr_CoverCrouchReloadAnim(ent, empty);
            }
            reload_knob_all(&ent, 0x008cf2c4);  // g_anim_reload_crouch_cover_rifle
        } else {
            reload_knob_all(&ent, 0x008cf2d0);  // g_anim_reload_crouch_rifle
        }
    } else if (get_int_184(ent) == 0x0738b1a5) {
        reload_knob_all(&ent, 0x008cf2dc);  // g_anim_reload_prone_rifle
        slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009226bc)(
            ent, title_global<uint32_t>(0x008cf1d4), title_global<uint32_t>(0x008cf1c8), 0x3f800000, 0x3dcccccd,
            0x3f800000);  // GScr_ActorSetAimAnimWeights(straight45up, straight45down)
    } else {
        Scr_Wait(2.0f);
        return;
    }
    handle_note_tracks(ent, 0x047f1336);
    clear_upper_body(ent);
}

namespace {
// The idle twitches: anim_idleset = `set`, the pose with movement 0x385d90 (BroScr_SetPoseMovement),
// a reload below 0.5 of the clip, BroScr_AimToAlert, `anim` at the rate key 0x6937c3b9, note tracks.
inline void idle_twitch_begin(uint32_t ent, uint32_t set, uint32_t pose) {
    *handle_slot(ent, 0xbfc60f36) = set;
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, pose, 0x00385d90);  // BroScr_SetPoseMovement
    BroScr_TryReload(ent, title_global<uint32_t>(0x0083eb24), title_global<uint32_t>(0x00922ea8));
    BroScr_AimToAlert(ent);
}
inline void idle_twitch_play(uint32_t ent, uint32_t anim) {
    const uint32_t rate = *key_slot(ent, 0x6937c3b9, undefined_float());
    slot<KnobAll>(0x009225e4)(ent, 0x54e3b2b6, anim, body_root(), 0x3f800000, 0x3dcccccd, rate, 1);
    handle_note_tracks(ent, 0x54e3b2b6);
}
// The two-anim twitches: `idle` below 70 of 100, else `twitch`.
inline void idle_twitch(uint32_t ent, uint32_t set, uint32_t pose, uint32_t idle, uint32_t twitch) {
    idle_twitch_begin(ent, set, pose);
    const uint32_t anim = random_int(100) < 70 ? title_global<uint32_t>(idle) : title_global<uint32_t>(twitch);
    idle_twitch_play(ent, anim);
}
}  // namespace

void XB_CDECL BroScr_StandIdleTwitch_1(uint32_t ent) {
    idle_twitch(ent, 0x77, 0x070b89a4, 0x008cf78c, 0x008cf798);  // g_anim_wounded_woundedalert_idle / _twitch
}

void XB_CDECL BroScr_StandIdleTwitch_2(uint32_t ent) {
    idle_twitch_begin(ent, 0x61, 0x070b89a4);
    const int32_t r = random_int(100);
    uint32_t anim;
    if (r < 30)
        anim = title_global<uint32_t>(0x008cf3e4);  // g_anim_stand_alert_1
    else if (anim = title_global<uint32_t>(0x008cf3f0), !(r < 65))  // g_anim_stand_alert_2
        anim = title_global<uint32_t>(0x008cf3fc);  // g_anim_stand_alert_3
    idle_twitch_play(ent, anim);
}

void XB_CDECL BroScr_StandIdleTwitch_3(uint32_t ent) {
    idle_twitch(ent, 0x62, 0x070b89a4, 0x008cf420, 0x008cf438);  // g_anim_stand_alertb_idle1 / _twitch1
}

void XB_CDECL BroScr_CrouchIdleTwitch_1(uint32_t ent) {
    idle_twitch(ent, 0x61, 0xc1165808, 0x008ce544, 0x008ce550);  // g_anim_crouch_alert_a_idle / _twitch
}

void XB_CDECL BroScr_CrouchIdleTwitch_2(uint32_t ent) {
    idle_twitch(ent, 0x62, 0xc1165808, 0x008ce55c, 0x008ce568);  // g_anim_crouch_alert_b_idle1 / _twitch1
}

namespace {
// The team hash of the entity's team (Scr_HashScriptString of ScrCall_ScrSentient_GetTeam).
inline uint32_t team_hash(uint32_t* ent) {
    ScriptString team;
    uint32_t hash;
    const uint32_t value = *Scr_HashScriptString(
        &hash, reinterpret_cast<const uint8_t* const*>(ScrCall_ScrSentient_GetTeam(ent, &team)));
    scr_release(&team);
    return value;
}
// A team's chatter block of the anim object: the started flag at `base` (1), the times at +8, +0xc,
// +0x14, +0x10, +0x18, +0x1c, +0x20 set to now, in that order.
inline void chatter_team_init(uint32_t base) {
    uint8_t* const object = anim_object();
    if (at<uint32_t>(object, base) != undefined_int()) return;
    at<uint32_t>(object, base) = 1;
    static const uint8_t kTimes[7] = {0x08, 0x0c, 0x14, 0x10, 0x18, 0x1c, 0x20};
    for (int i = 0; i < 7; ++i) {
        const int32_t now = get_time();
        at<int32_t>(anim_object(), base + kTimes[i]) = now;
    }
}
}  // namespace

void XB_CDECL BroScr_BattleChatterInit(uint32_t ent) {
    switch (team_hash(&ent)) {
    case 0x00316d53: chatter_team_init(0x14); break;  // "axis"
    case 0x000189ec: chatter_team_init(0xf0); break;  // "all"
    case 0x4d6bc457: chatter_team_init(0xc8); break;  // "neutral"
    case 0xc4f7a8d3: chatter_team_init(0x6c); break;  // "allies"
    }
    bool defined;
    if (*ScrField_had8f34a0_IsDefined(&defined, ent)) return;
    const int32_t now = get_time();
    const int32_t spread = random_int(15000);
    *int_slot(ent, 0xad8f34a0) = now + spread - 5000;  // the next chatter time
    float* const chance = reinterpret_cast<float*>(ScrField_hb3845cc4(ent));
    *chance = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922230)(0x3f800000));  // Scr_RandomFloat(1)
    // ScrField_hbe944753 (a bool field; a missing word's upper bytes are the caller's stale ECX).
    *reinterpret_cast<uint8_t*>(key_slot(ent, 0xbe944753, title_global<uint8_t>(0x00922ed0))) = 0;
    title_function<BeginThreadFunctor1>(0x0011b4d0)(0, 0, 0, 0, 0x001cba70, ent);  // BroScr_GiveUpPositionChatter
    title_function<BeginThreadFunctor1>(0x0011b4d0)(0, 0, 0, 0, 0x001cbae0, ent);  // BroScr_GrenadeDangerThink
}

void XB_CDECL BroScr_GrenadeDangerThink(uint32_t ent) {
    for (;;) {
        Scr_WaitTill(ent, 0x1d12d724);
        uint32_t started = undefined_int();
        switch (team_hash(&ent)) {
        case 0x00316d53: started = at<uint32_t>(anim_object(), 0x14); break;
        case 0x000189ec: started = at<uint32_t>(anim_object(), 0xf0); break;
        case 0x4d6bc457: started = at<uint32_t>(anim_object(), 0xc8); break;
        case 0xc4f7a8d3: started = at<uint32_t>(anim_object(), 0x6c); break;
        }
        if (started == undefined_int()) BroScr_BattleChatterInit(ent);
        uint32_t out;
        const uint32_t* const next = BroScr_GetPerTeamValue_dc(&out, ent);
        if (get_time() > static_cast<int32_t>(*next)) {
            ScriptString type;
            scr_string_from(&type, 0x008268b4);
            BroScr_BattleChatter(ent, type);
            const int32_t now = get_time();
            int32_t value;
            if (now > static_cast<int32_t>(*BroScr_GetPerTeamValue_dc(&out, ent)) + 2000)
                value = get_time();
            else
                value = get_time() + 5000;
            BroScr_SetPerTeamValue_dc(ent, static_cast<uint32_t>(value));
        }
        Scr_Wait(5.0f);
    }
}

uint8_t* XB_CDECL BroScr_ChatterEnemySighted(uint8_t* out, uint32_t ent) {
    const uint32_t chance = *key_slot(ent, 0xb3845cc4, undefined_float());
    const float scale = title_global<float>(0x0083f010);
    const int32_t delay = static_cast<int32_t>(as_float(chance) * scale + scale);
    // The missing bool word: the undefined bool byte under the stale upper bytes of `chance`.
    const uint32_t missing = (chance & 0xffffff00u) | title_global<uint8_t>(0x00922ed0);
    uint32_t team_out;
    using GetRef = uint32_t* XB_CDECL(uint32_t*, uint32_t);
    if (*reinterpret_cast<uint8_t*>(key_slot(ent, 0xbe944753, missing)) == 0 &&
        get_time() > static_cast<int32_t>(*BroScr_GetPerTeamValue_d0(&team_out, ent) + 20000) &&
        get_time() > static_cast<int32_t>(*int_slot(ent, 0xad8f34a0) + delay)) {
        uint32_t enemy;
        if (entity_handle_is_valid(*slot<GetRef>(0x00922e00)(&enemy, ent))) {  // ScrSentient_GetRef_3c
            const uint32_t target = *slot<GetRef>(0x00922e00)(&enemy, ent);
            if (slot<long double XB_CDECL(uint32_t, uint32_t)>(0x009226dc)(ent, target) !=
                title_global<float>(0x0083eafc)) {  // GScr_ActorCanSeeDefaultAge
                ScriptString type;
                scr_string_from(&type, 0x008268a4);
                BroScr_BattleChatter(ent, type);
                BroScr_SetPerTeamValue_d0(ent, static_cast<uint32_t>(get_time()));
                float* const next = reinterpret_cast<float*>(ScrField_hb3845cc4(ent));
                *next = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922230)(0x3f800000));  // Scr_RandomFloat(1)
                *out = 1;
                return out;
            }
        }
    }
    uint32_t enemy;
    bool reset = false;
    if (entity_handle_is_valid(*slot<GetRef>(0x00922e00)(&enemy, ent))) {
        ScriptString team;
        reset = ScriptString_Equals(ScrCall_ScrSentient_GetTeam(&ent, &team), pointer<const char>(0x00832084));
        scr_release(&team);
    }
    if (reset) BroScr_SetPerTeamValue_d0(ent, static_cast<uint32_t>(get_time()));
    *out = 0;
    return out;
}

namespace {
// Scr_BeginThread of a {0x007f5ba0, function, ent} functor on `ent`, then a frame wait inside a thread.
inline void begin_entity_thread(uint32_t function, uint32_t ent) {
    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0xc);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f5ba0;
        functor[1] = function;
        functor[2] = ent;
    }
    slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x00922168)(
        0, 0, 0, ent, functor, 0);  // Scr_BeginThread
    if (static_cast<uint8_t>(slot<uint32_t XB_CDECL()>(0x0092218c)())) Scr_WaitFrames(0);  // Scr_InThread
}
}  // namespace

void XB_CDECL BroScr_CornerRamboFire(uint32_t ent, uint32_t step_out, uint32_t step_back) {
    ScriptString name;
    ScrCall_ScrActor_GetString_1384(&ent, &name);  // held to the end
    {
        ScriptString mode;
        scr_string_from(&mode, 0x0083ad10);
        slot<void XB_CDECL(uint32_t, ScriptString*)>(0x0092273c)(ent, &mode);  // GScr_ActorSetAnimMode
        scr_release(&mode);
    }
    float delta[3] = {as_float(undefined_lane()), as_float(undefined_lane()), as_float(undefined_lane())};
    slot<void XB_CDECL(float*, uint32_t, uint32_t, uint32_t)>(0x00922408)(delta, step_out, 0,
                                                                        0x3f800000);  // GScr_GetMoveDelta
    // The third lane is never written in the original (stale stack); 0 here.
    float back[3] = {0.0f - delta[0], 0.0f - delta[1], 0.0f};
    float point[3] = {as_float(undefined_lane()), as_float(undefined_lane()), as_float(undefined_lane())};
    slot<void XB_CDECL(uint32_t, float*, float*)>(0x0092263c)(ent, back, point);  // GScr_LocalToWorldCoords
    if (static_cast<uint8_t>(slot<uint32_t XB_CDECL(uint32_t, float*)>(0x009226f4)(ent, point))) {  // GScr_ActorIsSafeMovePoint
        title_function<BeginThreadFunctor1>(0x0011b4d0)(0, 0, 0, 0, 0x001bfe70, ent);  // BroScr_LookAtEnemyLoop
        const uint32_t special = *handle_slot(ent, 0x175567a8);  // anim_special
        *handle_slot(ent, 0x175567a8) = 0x0036b1a5;
        slot<KnobAll>(0x009225e4)(ent, 0x54e3b2b6, step_out, body_root(), 0x3f800000, 0x3dcccccd, 0x3f800000, 0);
        handle_note_tracks(ent, 0x54e3b2b6);
        BroScr_RamboFire(ent);
        queue_notify(ent, 0x807e5a80);
        slot<KnobAll>(0x009225e4)(ent, 0x54e3b2b6, step_back, body_root(), 0x3f800000, 0x3dcccccd, 0x3f800000, 0);
        handle_note_tracks(ent, 0x54e3b2b6);
        *handle_slot(ent, 0x175567a8) = special;
    }
    scr_release(&name);
}

void XB_CDECL BroScr_StopProneIdle(uint32_t ent) {
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, 0x0738b1a5, 0x00385d90);  // BroScr_SetPoseMovement
    BroScr_TryReload(ent, title_global<uint32_t>(0x0083ec24), title_global<uint32_t>(0x00922ea8));
    for (;;) {
        slot<KnobAllNoFlag>(0x009225c8)(ent, title_global<uint32_t>(0x008cf0f0), body_root(), 0x3f800000, 0x3dcccccd,
                                        0x3f800000, 0);  // GScr_SetAnimKnobAll(g_anim_prone_aim_idle)
        uint32_t out[3];
        const uint32_t* const angles = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922ba0)(out, ent);  // ScrEnt_GetAngles
        const uint32_t copy[3] = {angles[0], angles[1], angles[2]};
        const float desired = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922d00)(ent));  // ScrActor_GetFloat_154
        float turn = static_cast<float>(static_cast<int32_t>(desired - as_float(copy[1])) % 360);
        if (static_cast<float>(turn > title_global<float>(0x0083eb60) ? 1 : 0) != 0.0f) turn -= title_global<float>(0x0083ebcc);
        if (static_cast<float>(turn > title_global<float>(0x0083ece8) ? 1 : 0) != 0.0f ||
            static_cast<float>(title_global<float>(0x0083f094) > turn ? 1 : 0) != 0.0f) {
            // Both directions play g_anim_prone_turn_right (two copies in the original).
            slot<SetAnim7>(0x009225e0)(ent, 0x59c142f9, title_global<uint32_t>(0x008cf1f8), 0x3f800000, 0x3dcccccd,
                                       0x3f800000, 1);  // GScr_SetFlaggedAnimKnob
            begin_entity_thread(0x001840b0, ent);  // BroScr_ProneAimWeightsLoop
            handle_note_tracks(ent, 0x59c142f9);
            queue_notify(ent, 0x6660fd4f);
        } else {
            slot<SetAnim6>(0x009225d8)(ent, title_global<uint32_t>(0x008cf0f0), 0x3f800000, 0x3dcccccd, 0x3f800000,
                                       0);  // GScr_SetAnimKnob(g_anim_prone_aim_idle)
            slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009226bc)(
                ent, title_global<uint32_t>(0x008cf1d4), title_global<uint32_t>(0x008cf1c8), 0x3f800000, 0x3d4ccccd,
                0x3f800000);  // GScr_ActorSetAimAnimWeights(straight45up, straight45down)
            Scr_Wait(1.0f);
        }
    }
}

namespace {
inline void clear_anim_commands(uint32_t ent) {
    DynArray_Resize52(reinterpret_cast<DynArray*>(ScriptKeyTable_FindOrAdd(ent_script_object(ent), 0x1791f035)), 0, 0);
}
inline void set_anim_mode(uint32_t ent, uint32_t text) {
    ScriptString mode;
    scr_string_from(&mode, text);
    slot<void XB_CDECL(uint32_t, ScriptString*)>(0x0092273c)(ent, &mode);  // GScr_ActorSetAnimMode
    scr_release(&mode);
}
inline void anim_scripted_begin(uint32_t ent) { slot<void XB_CDECL(uint32_t)>(0x00922750)(ent); }  // pScr slot 0x388
inline void anim_scripted_end(uint32_t ent) { slot<void XB_CDECL(uint32_t)>(0x00922754)(ent); }    // pScr slot 0x389
// One balcony fall try: the stumble recorded and moved until an obstacle, then `fall` (flag 0xaeb1527d)
// until its end note.
inline bool balcony_fall(uint32_t ent, uint32_t fall) {
    const uint32_t rate = title_global<uint32_t>(0x0083edc8);
    BroScr_RecordAnimCommand(ent, 1, 0, title_global<uint32_t>(0x008cd7b8), title_global<uint32_t>(0x00922ea8),
                             0x3f800000, rate, 0x3f800000);  // g_anim_balcony_stumble_forward
    uint8_t moved;
    if (!*BroScr_AnimMoveUntilObstacle(&moved, ent, 0x3f800000)) return false;
    set_anim_mode(ent, 0x0083ac50);
    BroScr_RecordAnimCommand(ent, 3, 0xaeb1527d, title_global<uint32_t>(fall), title_global<uint32_t>(0x008cf300),
                             0x3f800000, rate, 0x3f800000);  // g_anim_root
    uint8_t ended;
    return *BroScr_AnimMoveUntilEndNote(&ended, ent, 0xaeb1527d) != 0;
}
}  // namespace

uint8_t* XB_CDECL BroScr_BalconyStumbleDeath(uint8_t* out, uint32_t ent) {
    anim_scripted_begin(ent);
    clear_anim_commands(ent);
    if (balcony_fall(ent, 0x008cd7c4)) {
        anim_scripted_end(ent);
        *out = 1;
        return out;
    }
    anim_scripted_end(ent);
    anim_scripted_begin(ent);
    clear_anim_commands(ent);
    if (balcony_fall(ent, 0x008cd7d0)) {
        anim_scripted_end(ent);
        *out = 1;
        return out;
    }
    anim_scripted_end(ent);
    clear_anim_commands(ent);
    *out = 0;
    return out;
}

uint8_t* XB_CDECL BroScr_StandShootSideStep(uint8_t* out, uint32_t ent) {
    {
        ScriptString team;
        const bool allies = ScriptString_Equals(ScrCall_ScrSentient_GetTeam(&ent, &team), pointer<const char>(0x00832084));
        scr_release(&team);
        uint32_t type;
        if (allies || *BroScr_GetWeaponType(&type, ent) == 0xe5aa1ee7) {
            *out = 0;
            return out;
        }
    }
    uint32_t stepped = 0;
    if (get_int_184(ent) == 0x070b89a4 &&
        static_cast<uint8_t>(slot<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00922714)(ent, 0x070b89a4))) {  // GScr_ActorIsStanceAllowed
        DynArray anims = {nullptr, 0, 0};
        uint32_t* const list = reinterpret_cast<uint32_t*>(&anims);
        ScrPtrArray_Append(list, pointer<uint32_t>(0x008cf54c));  // g_anim_stand_shoot_jump_left
        ScrPtrArray_Append(list, pointer<uint32_t>(0x008cf558));  // g_anim_stand_shoot_jump_right
        ScrPtrArray_Append(list, pointer<uint32_t>(0x008cf5d0));  // g_anim_stand_shoot_walk_left
        ScrPtrArray_Append(list, pointer<uint32_t>(0x008cf5dc));  // g_anim_stand_shoot_walk_right
        ScrPtrArray_Append(list, pointer<uint32_t>(0x008cf5a0));  // g_anim_stand_shoot_run_left
        ScrPtrArray_Append(list, pointer<uint32_t>(0x008cf5ac));  // g_anim_stand_shoot_run_right
        using At = uint32_t* XB_THISCALL(DynArray*, uint32_t);  // FUN_0045ceb0: &data[i], grown first
        const int32_t start = at<int32_t>(anim_object(), 0x38);
        int32_t tries = 0;
        if (static_cast<int32_t>(anims.size) > 0) {
            do {
                // Every try takes the same element (the index never advances).
                const int32_t index = start < static_cast<int32_t>(anims.size) ? start : start - static_cast<int32_t>(anims.size);
                ScrVec3 delta;
                delta.v[0] = delta.v[1] = delta.v[2] = undefined_lane();
                uint32_t point[3] = {undefined_lane(), undefined_lane(), undefined_lane()};
                const uint32_t anim = *title_function<At>(0x0045ceb0)(&anims, index);
                slot<void XB_CDECL(ScrVec3*, uint32_t, uint32_t, uint32_t)>(0x00922408)(&delta, anim, 0,
                                                                                      0x3f800000);  // GScr_GetMoveDelta
                slot<void XB_CDECL(uint32_t, ScrVec3*, uint32_t*)>(0x0092263c)(ent, &delta, point);  // GScr_LocalToWorldCoords
                uint8_t can_shoot;
                if (static_cast<uint8_t>(slot<uint32_t XB_CDECL(uint32_t, uint32_t*)>(0x009226f4)(ent, point)) &&
                    *BroScr_CanShootFromPose(&can_shoot, ent, 0x070b89a4, delta, title_global<uint32_t>(0x00922ebc))) {
                    if (*ScrField_anim_alertness(ent) == 0xc4a729a7) {
                        title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, 0x070b89a4, 0);  // BroScr_SetPoseMovement
                    } else {
                        title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, 0x070b89a4,
                                                                                               0x00385d90);
                        BroScr_AimIdleAtTarget(ent, title_global<uint32_t>(0x00922ea4), title_global<uint32_t>(0x00922ea4));
                    }
                    uint32_t notify = 0xf519d3c8;
                    ScrCall_GScr_SetFlaggedAnimKnobAll_2(&ent, &notify, *title_function<At>(0x0045ceb0)(&anims, index),
                                                         body_root(), 0x3f800000, 0x3e19999a, 0x3f800000);
                    {
                        const float delay = title_global<float>(0x0083eca0);  // 0.2
                        const uint32_t end = 0xeac9abbd;
                        using CallEntHashF = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                                               const uint32_t*, const float*);
                        title_function<CallEntHashF>(0x001e4dc0)(0, 0, 0, 0, 0x001ca3c0, ent, &end,
                                                                 &delay);  // Scr_CallThread_ent_hash_f_4(BroScr_RechamberRifleDelayed)
                    }
                    handle_note_tracks(ent, 0xf519d3c8);
                    queue_notify(ent, 0xeac9abbd);
                    stepped = 1;
                    break;
                }
                ++tries;
            } while (tries < static_cast<int32_t>(anims.size));
        }
        at<int32_t>(anim_object(), 0x38) = tries;
        if (anims.data) Scr_Free(anims.data);
    }
    *out = static_cast<uint8_t>(stepped);
    return out;
}

namespace {
// The corner scripts' anim plays: GScr_SetFlaggedAnimKnobAll(ent, notify, anim, g_anim_Body, 1, blend,
// rate, 1) with the rate from field 0x6937c3b9 (read first) or 1.
inline void corner_play(uint32_t ent, uint32_t notify, uint32_t anim, uint32_t blend = 0x3dcccccd) {
    const uint32_t rate = *ScrField_h6937c3b9(ent);
    slot<KnobAll>(0x009225e4)(ent, notify, title_global<uint32_t>(anim), body_root(), 0x3f800000, blend, rate, 1);
}
inline void corner_play_one(uint32_t ent, uint32_t notify, uint32_t anim) {
    slot<KnobAll>(0x009225e4)(ent, notify, title_global<uint32_t>(anim), body_root(), 0x3f800000, 0x3dcccccd, 0x3f800000,
                              1);
}
// The same through ScrCall_GScr_SetFlaggedAnimKnobAll (ent and notify by address).
inline void corner_play_ref(uint32_t* ent, uint32_t notify, uint32_t anim) {
    const uint32_t rate = *ScrField_h6937c3b9(*ent);
    ScrCall_GScr_SetFlaggedAnimKnobAll(ent, &notify, title_global<uint32_t>(anim), body_root(), 0x3f800000, 0x3dcccccd,
                                       rate);
}
inline void corner_wait(uint32_t range) {
    Scr_Wait(static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922230)(range) +
                                title_global<float>(0x0083eba8)));  // Scr_RandomFloat(range) + 2
}
// GScr_ActorSetOrientMode(ent, "face angle", yaw (+ the float at `add` when set), 0).
inline void corner_orient(uint32_t ent, uint32_t yaw, uint32_t add) {
    ScriptString mode;
    scr_string_from(&mode, 0x0083adfc);  // "face angle"
    float angle = as_float(yaw);
    if (add) angle = static_cast<float>(angle + title_global<float>(add));
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t)>(0x00922740)(ent, &mode, bits(angle), 0);
    scr_release(&mode);
}

// The corner run-to-alert scripts (eight copies that differ in their anims and small details; each
// variant is a traits struct): early_set = anim_idleset / anim_special set before the first orient;
// yaw_add = the first orient's added float (0: the yaw as given); reorient_casual / _alert / _pick =
// an orient to yaw + 0x83eb64 before the casual pick / before the alertness test of the casual
// branch / after it; pose_rate, aim_rate = the alert pose / the aim loop at the field rate (else 1);
// wait_pose / wait_aim = Scr_RandomFloat ranges; aim_or_look = half the time the look anim instead of
// the aim sequence; back_notify = the attack-to-casual play's.
template <class c>
inline void corner_run_to_alert(uint32_t ent, uint32_t yaw) {
    if (c::early_set) {
        *handle_slot(ent, 0xbfc60f36) = c::idleset;  // anim_idleset
        *handle_slot(ent, 0x175567a8) = c::special;  // anim_special
    }
    bool ran = false;
    corner_orient(ent, yaw, c::yaw_add);
    if (*handle_slot(ent, 0x0ccee5c1) != 0x00385d90) {  // anim_movement
        corner_play(ent, 0x9a7145f5, c::run2alert, 0x3e800000);
        handle_note_tracks(ent, 0x9a7145f5);
        uint8_t ready;
        if (!*BroScr_UpdateCombatDelay(&ready, ent)) BroScr_SetCombatDelay(ent, 0);
        ran = true;
    }
    *handle_slot(ent, 0xbfc60f36) = c::idleset;
    *handle_slot(ent, 0x175567a8) = c::special;
    *handle_slot(ent, 0x0ccee5c1) = 0x00385d90;
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009226b4)(ent, 0x3f000000);  // pScr slot 0x9226b4(ent, 0.5)
    set_int_184(ent, c::pose);
    BroScr_TryReload(ent, title_global<uint32_t>(0x0083eb24), title_global<uint32_t>(c::reload));
    // A missing 0x602384d4 word: the undefined bool under stale stack bytes in the original; 0 here.
    if (*reinterpret_cast<uint8_t*>(key_slot(ent, 0x602384d4, title_global<uint8_t>(0x00922ed0)))) {  // casual
        if (c::reorient_casual) corner_orient(ent, yaw, 0x0083eb64);
        if (random_int(100) < 90)
            corner_play(ent, 0x9a7145f5, c::casual_idle);
        else
            corner_play(ent, 0x9a7145f5, c::casual_alert);
        handle_note_tracks(ent, 0x9a7145f5);
        return;
    }
    uint8_t ready;
    if (*BroScr_UpdateCombatDelay(&ready, ent)) {
        if (*ScrField_anim_alertness(ent) == 0xc028584c && !ran) {
            corner_play_ref(&ent, 0x9b0142c5, c::casual2attack);
            handle_note_tracks(ent, 0x9b0142c5);
        }
        *ScrField_anim_alertness(ent) = 0x06279a34;
        if (c::pose_rate)
            corner_play(ent, 0x9a7145f5, c::alert_pose);
        else
            corner_play_one(ent, 0x9a7145f5, c::alert_pose);
        corner_wait(c::wait_pose);
        if (!c::aim_or_look || random_int(100) < 50) {
            corner_play(ent, c::aim_notify, c::alert2aim);
            handle_note_tracks(ent, c::aim_notify);
            if (c::aim_rate)
                corner_play(ent, c::aim_notify, c::aim);
            else
                corner_play_one(ent, c::aim_notify, c::aim);
            corner_wait(c::wait_aim);
            corner_play(ent, c::aim_notify, c::aim2alert);
            handle_note_tracks(ent, c::aim_notify);
        } else {
            corner_play(ent, 0x9a7145f5, c::look);
            handle_note_tracks(ent, 0x9a7145f5);
        }
        return;
    }
    if (c::reorient_alert) corner_orient(ent, yaw, 0x0083eb64);
    if (*ScrField_anim_alertness(ent) != 0xc028584c || ran) {
        corner_play(ent, c::back_notify, c::attack2casual);
        handle_note_tracks(ent, c::back_notify);
        *ScrField_anim_alertness(ent) = 0xc028584c;
    }
    if (c::reorient_pick) corner_orient(ent, yaw, 0x0083eb64);
    const int32_t r = random_int(100);
    if (r < 60)
        corner_play(ent, c::casual_notify, c::casual_idle);
    else if (r < 90)
        corner_play_ref(&ent, c::casual_notify, c::casual_alert);
    else
        corner_play_ref(&ent, c::casual_notify, c::casual_third);
    handle_note_tracks(ent, c::casual_notify);
}

const uint32_t kRight = 0xa1cb941d, kLeft = 0x38948440, kStand = 0x070b89a4, kCrouch = 0xc1165808;
const uint32_t kN = 0x9a7145f5, kNB = 0x9b0142c5, kNC = 0x80bd2428;
const uint32_t k4 = 0x40800000, k3 = 0x40400000, k2 = 0x40000000, k1 = 0x3f800000;

struct kCornerRun2AlertRight {
    static const uint32_t idleset = 0x61;
    static const uint32_t special = kRight;
    static const uint32_t pose = kStand;
    static const bool early_set = false;
    static const uint32_t yaw_add = 0x0083eb60;
    static const bool reorient_casual = false;
    static const bool reorient_alert = false;
    static const bool reorient_pick = false;
    static const bool pose_rate = true;
    static const uint32_t wait_pose = k4;
    static const bool aim_or_look = true;
    static const uint32_t aim_notify = kN;
    static const bool aim_rate = true;
    static const uint32_t wait_aim = k2;
    static const uint32_t back_notify = kN;
    static const uint32_t casual_notify = kN;
    static const uint32_t run2alert = 0x008cdcec;
    static const uint32_t reload = 0x008cf288;
    static const uint32_t casual_idle = 0x008cd8c0;
    static const uint32_t casual_alert = 0x008cd860;
    static const uint32_t casual2attack = 0x008cd890;
    static const uint32_t alert_pose = 0x008ce424;
    static const uint32_t alert2aim = 0x008cddac;
    static const uint32_t aim = 0x008cdd64;
    static const uint32_t aim2alert = 0x008cdd1c;
    static const uint32_t look = 0x008ce40c;
    static const uint32_t attack2casual = 0x008cd878;
    static const uint32_t casual_third = 0x008cd8a8;
};
struct kCornerBRun2AlertRight {
    static const uint32_t idleset = 0x62;
    static const uint32_t special = kRight;
    static const uint32_t pose = kStand;
    static const bool early_set = false;
    static const uint32_t yaw_add = 0;
    static const bool reorient_casual = true;
    static const bool reorient_alert = true;
    static const bool reorient_pick = false;
    static const bool pose_rate = true;
    static const uint32_t wait_pose = k4;
    static const bool aim_or_look = true;
    static const uint32_t aim_notify = kN;
    static const bool aim_rate = true;
    static const uint32_t wait_aim = k1;
    static const uint32_t back_notify = kN;
    static const uint32_t casual_notify = kN;
    static const uint32_t run2alert = 0x008ce118;
    static const uint32_t reload = 0x008cf2b8;
    static const uint32_t casual_idle = 0x008cd938;
    static const uint32_t casual_alert = 0x008cd8d8;
    static const uint32_t casual2attack = 0x008cd908;
    static const uint32_t alert_pose = 0x008ce220;
    static const uint32_t alert2aim = 0x008ce1f0;
    static const uint32_t aim = 0x008ce1a8;
    static const uint32_t aim2alert = 0x008ce160;
    static const uint32_t look = 0x008ce238;
    static const uint32_t attack2casual = 0x008cd8f0;
    static const uint32_t casual_third = 0x008cd920;
};
struct kCornerCrouchRun2AlertRight {
    static const uint32_t idleset = 0x61;
    static const uint32_t special = kRight;
    static const uint32_t pose = kCrouch;
    static const bool early_set = false;
    static const uint32_t yaw_add = 0x0083eb60;
    static const bool reorient_casual = false;
    static const bool reorient_alert = false;
    static const bool reorient_pick = false;
    static const bool pose_rate = false;
    static const uint32_t wait_pose = k4;
    static const bool aim_or_look = false;
    static const uint32_t aim_notify = kNC;
    static const bool aim_rate = false;
    static const uint32_t wait_aim = k1;
    static const uint32_t back_notify = kNB;
    static const uint32_t casual_notify = kNC;
    static const uint32_t run2alert = 0x008cdc80;
    static const uint32_t reload = 0x008cf270;
    static const uint32_t casual_idle = 0x008cd9b0;
    static const uint32_t casual_alert = 0x008cd950;
    static const uint32_t casual2attack = 0x008cd980;
    static const uint32_t alert_pose = 0x008ce3a0;
    static const uint32_t alert2aim = 0x008cdba8;
    static const uint32_t aim = 0x008cdb60;
    static const uint32_t aim2alert = 0x008cdb18;
    static const uint32_t look = 0;
    static const uint32_t attack2casual = 0x008cd968;
    static const uint32_t casual_third = 0x008cd998;
};
struct kCornerBCrouchRun2AlertRight {
    static const uint32_t idleset = 0x62;
    static const uint32_t special = kRight;
    static const uint32_t pose = kCrouch;
    static const bool early_set = false;
    static const uint32_t yaw_add = 0;
    static const bool reorient_casual = true;
    static const bool reorient_alert = false;
    static const bool reorient_pick = true;
    static const bool pose_rate = true;
    static const uint32_t wait_pose = k4;
    static const bool aim_or_look = true;
    static const uint32_t aim_notify = kN;
    static const bool aim_rate = false;
    static const uint32_t wait_aim = k1;
    static const uint32_t back_notify = kNB;
    static const uint32_t casual_notify = kNC;
    static const uint32_t run2alert = 0x008ce0b8;
    static const uint32_t reload = 0x008cf2a0;
    static const uint32_t casual_idle = 0x008cda28;
    static const uint32_t casual_alert = 0x008cd9c8;
    static const uint32_t casual2attack = 0x008cd9f8;
    static const uint32_t alert_pose = 0x008cdfbc;
    static const uint32_t alert2aim = 0x008cdf8c;
    static const uint32_t aim = 0x008cdf44;
    static const uint32_t aim2alert = 0x008cdefc;
    static const uint32_t look = 0x008cdfe0;
    static const uint32_t attack2casual = 0x008cd9e0;
    static const uint32_t casual_third = 0x008cda10;
};
struct kCornerRun2AlertLeft {
    static const uint32_t idleset = 0x61;
    static const uint32_t special = kLeft;
    static const uint32_t pose = kStand;
    static const bool early_set = false;
    static const uint32_t yaw_add = 0x0083eb60;
    static const bool reorient_casual = false;
    static const bool reorient_alert = false;
    static const bool reorient_pick = false;
    static const bool pose_rate = false;
    static const uint32_t wait_pose = k4;
    static const bool aim_or_look = true;
    static const uint32_t aim_notify = kN;
    static const bool aim_rate = true;
    static const uint32_t wait_aim = k1;
    static const uint32_t back_notify = kNB;
    static const uint32_t casual_notify = kN;
    static const uint32_t run2alert = 0x008cdce0;
    static const uint32_t reload = 0x008cf27c;
    static const uint32_t casual_idle = 0x008cd8b4;
    static const uint32_t casual_alert = 0x008cd854;
    static const uint32_t casual2attack = 0x008cd884;
    static const uint32_t alert_pose = 0x008ce418;
    static const uint32_t alert2aim = 0x008cdd88;
    static const uint32_t aim = 0x008cdd40;
    static const uint32_t aim2alert = 0x008cdcf8;
    static const uint32_t look = 0x008ce400;
    static const uint32_t attack2casual = 0x008cd86c;
    static const uint32_t casual_third = 0x008cd89c;
};
struct kCornerBRun2AlertLeft {
    static const uint32_t idleset = 0x62;
    static const uint32_t special = kLeft;
    static const uint32_t pose = kStand;
    static const bool early_set = true;
    static const uint32_t yaw_add = 0;
    static const bool reorient_casual = true;
    static const bool reorient_alert = true;
    static const bool reorient_pick = false;
    static const bool pose_rate = true;
    static const uint32_t wait_pose = k4;
    static const bool aim_or_look = true;
    static const uint32_t aim_notify = kN;
    static const bool aim_rate = true;
    static const uint32_t wait_aim = k1;
    static const uint32_t back_notify = kNB;
    static const uint32_t casual_notify = kN;
    static const uint32_t run2alert = 0x008ce10c;
    static const uint32_t reload = 0x008cf2ac;
    static const uint32_t casual_idle = 0x008cd92c;
    static const uint32_t casual_alert = 0x008cd8cc;
    static const uint32_t casual2attack = 0x008cd8fc;
    static const uint32_t alert_pose = 0x008ce214;
    static const uint32_t alert2aim = 0x008ce1cc;
    static const uint32_t aim = 0x008ce184;
    static const uint32_t aim2alert = 0x008ce13c;
    static const uint32_t look = 0x008ce22c;
    static const uint32_t attack2casual = 0x008cd8e4;
    static const uint32_t casual_third = 0x008cd914;
};
struct kCornerCrouchRun2AlertLeft {
    static const uint32_t idleset = 0x61;
    static const uint32_t special = kLeft;
    static const uint32_t pose = kCrouch;
    static const bool early_set = false;
    static const uint32_t yaw_add = 0x0083eb60;
    static const bool reorient_casual = false;
    static const bool reorient_alert = false;
    static const bool reorient_pick = false;
    static const bool pose_rate = true;
    static const uint32_t wait_pose = k3;
    static const bool aim_or_look = false;
    static const uint32_t aim_notify = kN;
    static const bool aim_rate = true;
    static const uint32_t wait_aim = k1;
    static const uint32_t back_notify = kNB;
    static const uint32_t casual_notify = kNC;
    static const uint32_t run2alert = 0x008cdc74;
    static const uint32_t reload = 0x008cf264;
    static const uint32_t casual_idle = 0x008cd9a4;
    static const uint32_t casual_alert = 0x008cd944;
    static const uint32_t casual2attack = 0x008cd974;
    static const uint32_t alert_pose = 0x008ce394;
    static const uint32_t alert2aim = 0x008cdb84;
    static const uint32_t aim = 0x008cdb3c;
    static const uint32_t aim2alert = 0x008cdaf4;
    static const uint32_t look = 0;
    static const uint32_t attack2casual = 0x008cd95c;
    static const uint32_t casual_third = 0x008cd98c;
};
struct kCornerBCrouchRun2AlertLeft {
    static const uint32_t idleset = 0x62;
    static const uint32_t special = kLeft;
    static const uint32_t pose = kCrouch;
    static const bool early_set = false;
    static const uint32_t yaw_add = 0;
    static const bool reorient_casual = false;
    static const bool reorient_alert = false;
    static const bool reorient_pick = false;
    static const bool pose_rate = true;
    static const uint32_t wait_pose = k1;
    static const bool aim_or_look = false;
    static const uint32_t aim_notify = kN;
    static const bool aim_rate = true;
    static const uint32_t wait_aim = k1;
    static const uint32_t back_notify = kNB;
    static const uint32_t casual_notify = kNC;
    static const uint32_t run2alert = 0x008ce0ac;
    static const uint32_t reload = 0x008cf294;
    static const uint32_t casual_idle = 0x008cda1c;
    static const uint32_t casual_alert = 0x008cd9bc;
    static const uint32_t casual2attack = 0x008cd9ec;
    static const uint32_t alert_pose = 0x008cdfb0;
    static const uint32_t alert2aim = 0x008cdf68;
    static const uint32_t aim = 0x008cdf20;
    static const uint32_t aim2alert = 0x008cded8;
    static const uint32_t look = 0;
    static const uint32_t attack2casual = 0x008cd9d4;
    static const uint32_t casual_third = 0x008cda04;
};
}  // namespace

// Anims in order: run2alert, reload, casual idle / alert / casual2attack, alert pose, alert2aim, aim,
// aim2alert, look, attack2casual, casual third (flinch).
void XB_CDECL BroScr_CornerRun2AlertRight(uint32_t ent, uint32_t yaw) { corner_run_to_alert<kCornerRun2AlertRight>(ent, yaw); }
void XB_CDECL BroScr_CornerBRun2AlertRight(uint32_t ent, uint32_t yaw) { corner_run_to_alert<kCornerBRun2AlertRight>(ent, yaw); }
void XB_CDECL BroScr_CornerCrouchRun2AlertRight(uint32_t ent, uint32_t yaw) { corner_run_to_alert<kCornerCrouchRun2AlertRight>(ent, yaw); }
void XB_CDECL BroScr_CornerBCrouchRun2AlertRight(uint32_t ent, uint32_t yaw) { corner_run_to_alert<kCornerBCrouchRun2AlertRight>(ent, yaw); }
void XB_CDECL BroScr_CornerRun2AlertLeft(uint32_t ent, uint32_t yaw) { corner_run_to_alert<kCornerRun2AlertLeft>(ent, yaw); }
void XB_CDECL BroScr_CornerBRun2AlertLeft(uint32_t ent, uint32_t yaw) { corner_run_to_alert<kCornerBRun2AlertLeft>(ent, yaw); }
void XB_CDECL BroScr_CornerCrouchRun2AlertLeft(uint32_t ent, uint32_t yaw) { corner_run_to_alert<kCornerCrouchRun2AlertLeft>(ent, yaw); }
void XB_CDECL BroScr_CornerBCrouchRun2AlertLeft(uint32_t ent, uint32_t yaw) { corner_run_to_alert<kCornerBCrouchRun2AlertLeft>(ent, yaw); }

void XB_CDECL BroScr_AnimScript_Aim(uint32_t ent) {
    end_on(ent, 0x07b4e7f7);
    BroScr_InitAnimScript(ent, 0x0001894d);
    BroScr_ApplyDesiredAnimPose(ent);
    if (get_int_184(ent) == 0x19669cd9) {
        ScriptString unused;
        scr_string_from(&unused, 0x00826928);
        BroScr_PlayFaceAnim_2(ent, unused, 0);
    }
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, 0, 0x00385d90);  // BroScr_SetPoseMovement
    BroScr_SetAimBlendTimes(title_global<uint32_t>(0x009230ec), title_global<uint32_t>(0x0092379c));
    BroScr_PlayIdleAnim(ent);
    BroScr_KeepAimingAtTarget(ent, title_global<uint32_t>(0x009230ec), title_global<uint32_t>(0x0092379c));
}

void XB_CDECL BroScr_MiscCombatTimer(uint32_t ent) {
    BroScr_BattleChatterInit(ent);
    uint8_t ready;
    if (!*BroScr_UpdateCombatDelay(&ready, ent)) {
        const int32_t now = get_time();
        *int_slot(ent, 0xad8f34a0) = static_cast<uint32_t>(now);
        // ScrField_hbe944753 (a bool field; a missing word's upper bytes are the caller's stale ECX).
        *reinterpret_cast<uint8_t*>(key_slot(ent, 0xbe944753, title_global<uint8_t>(0x00922ed0))) = 0;
        return;
    }
    uint8_t chattered;
    if (*BroScr_ChatterEnemySighted(&chattered, ent)) return;
    const float chance = as_float(*ScrField_hb3845cc4(ent));
    int32_t next = static_cast<int32_t>(chance * title_global<float>(0x00805688) + title_global<float>(0x0083ede0));
    next += static_cast<int32_t>(*ScrField_had8f34a0(ent));
    if (get_time() > next) {
        ScriptString type;
        scr_string_from(&type, 0x00826948);
        BroScr_BattleChatter(ent, type);
        const int32_t now = get_time();
        *ScrField_had8f34a0(ent) = static_cast<uint32_t>(now);
    }
}

void XB_CDECL BroScr_ForceShootAtPoint(uint32_t ent, ScrVec3 point, uint32_t delay, ScriptString name, uint32_t unused7,
                                       uint32_t target, uint32_t unused9) {
    end_on(ent, 0x06730ae8);  // death
    end_on(ent, 0x0039894e);
    end_on(ent, 0x2cf20de4);
    *int_slot(ent, 0x88c31fb5) = 1;
    {
        uint32_t* const thread = int_slot(ent, 0x60d7df22);
        *thread = slot<uint32_t XB_CDECL()>(0x0092219c)();  // Scr_GetCurrentThreadHandle
    }
    Scr_WaitFrames(0);
    uint32_t type;
    BroScr_GetWeaponType(&type, ent);  // read and dropped
    uint32_t shoot = 0;
    if (get_int_184(ent) == 0x070b89a4)
        shoot = title_global<uint32_t>(0x008cf504);  // g_anim_stand_shoot
    else if (get_int_184(ent) == 0xc1165808)
        shoot = title_global<uint32_t>(0x008ce5f8);  // g_anim_crouch_shoot
    slot<SetAnim6>(0x009225d8)(ent, title_global<uint32_t>(0x008cf318), 0x3f800000, 0x3e19999a, 0x3f800000,
                               0);  // GScr_SetAnimKnob(g_anim_Shoot)
    queue_notify(ent, 0x3c77a48b);
    using CallEntEntPtr = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*);
    if (entity_handle_is_valid(target)) {
        title_function<CallEntEntPtr>(0x004b09e0)(0, 0, 0, 0, 0x001c02d0, ent, &target);  // Scr_CallThread_ent_ent_ORAN(BroScr_LookAtEntityLoop)
        title_function<CallEntEntPtr>(0x004b09e0)(0, 0, 0, 0, 0x001abe30, ent, &target);  // (BroScr_UpdateOrientToEntity)
        title_function<CallEntEntPtr>(0x004b09e0)(0, 0, 0, 0, 0x001c6ba0, ent, &target);  // (BroScr_ForceShootTargetDeathWatch)
    } else {
        using CallEntVec = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const ScrVec3*);
        title_function<CallEntVec>(0x001e4f00)(0, 0, 0, 0, 0x001c0130, ent, &point);  // Scr_CallThread_ent_vec(BroScr_LookAtPositionLoop)
        ScriptString mode;
        scr_string_from(&mode, 0x0083ad6c);
        slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, const ScrVec3*)>(0x00922740)(ent, &mode, 0,
                                                                                         &point);  // GScr_ActorSetOrientMode
        scr_release(&mode);
    }
    *key_slot(ent, 0x27cc2178, 0) = target;
    {
        uint32_t* const aim = reinterpret_cast<uint32_t*>(ScrField_h7aeb941a(ent));
        aim[0] = point.v[0];
        aim[1] = point.v[1];
        aim[2] = point.v[2];
    }
    const float lane = as_float(undefined_lane());
    if (as_float(point.v[0]) != lane || as_float(point.v[1]) != lane || as_float(point.v[2]) != lane)
        *int_slot(ent, 0x33a4ea6c) = 1;
    {
        const float first = title_global<float>(0x0083eb24);
        using CallEntFF = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const float*,
                                            const uint32_t*);
        title_function<CallEntFF>(0x001e4f80)(0, 0, 0, 0, 0x001c9890, ent, &first,
                                             pointer<uint32_t>(0x00922ea4));  // Scr_CallThread_ent_f_f_5(BroScr_AimIdleAtTarget)
    }
    Scr_WaitTill(ent, 0xf378e0c7);
    {
        ScriptString copy;
        ScriptString_Copy(&copy, reinterpret_cast<const ScriptString*>(anim_object() + 0x194));  // read and dropped
        scr_release(&copy);
    }
    Scr_Wait(as_float(delay));
    slot<SetAnim6>(0x009225d8)(ent, shoot, 0x3f800000, 0x3e19999a, 0, 0);  // GScr_SetAnimKnob
    Scr_Wait(0.5f);
    slot<SetAnim7>(0x009225e0)(ent, 0x1973c1fe, shoot, 0x3f800000, 0, 0x3f800000, 1);  // GScr_SetFlaggedAnimKnob
    using GetVec = uint32_t* XB_CDECL(uint32_t*, uint32_t);
    if (entity_handle_is_valid(target)) {
        uint32_t at_point[3] = {undefined_lane(), undefined_lane(), undefined_lane()};
        if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922220)(target)) {  // GScr_IsSentient
            uint32_t eye[3] = {undefined_lane(), undefined_lane(), undefined_lane()};
            slot<void XB_CDECL(uint32_t*, uint32_t)>(0x00922528)(eye, target);  // GScr_GetEye
            at_point[0] = eye[0];
            at_point[1] = eye[1];
            at_point[2] = eye[2];
        } else {
            uint32_t out[3];
            const uint32_t* const origin = slot<GetVec>(0x00922b20)(out, target);  // ScrEnt_GetOrigin
            at_point[0] = origin[0];
            at_point[1] = origin[1];
            at_point[2] = origin[2];
        }
        slot<void XB_CDECL(uint32_t, uint32_t, const uint32_t*)>(0x00922698)(ent, 0x3f800000, at_point);  // GScr_ActorShootAt
    } else {
        slot<void XB_CDECL(uint32_t, uint32_t, const uint32_t*)>(0x00922698)(ent, 0x3f800000, point.v);
    }
    uint32_t interval;
    BroScr_GetShotInterval(&interval, ent);  // read and dropped
    Scr_Wait(0.25f);
    scr_release(&name);
}

void XB_CDECL BroScr_ForceShootThink(uint32_t ent, ScrVec3 point, uint32_t delay, ScriptString name, uint32_t a7,
                                     uint32_t target, uint32_t wait_stop) {
    end_on(ent, 0x06730ae8);  // death
    if (!name.rep) ScriptString_AssignCString(&name, pointer<const char>(0x007f63f0));
    uint32_t a7_or_0 = a7;
    if (a7_or_0 == undefined_int()) a7_or_0 = 0;
    uint32_t type;
    if (*BroScr_GetWeaponType(&type, ent) != 0x0036b1a5) {
        bool go = true;
        if (*handle_slot(ent, 0x0ccee5c1) != 0x00385d90) {  // moving
            if (wait_stop == undefined_int() || wait_stop == 0) {
                go = false;
            } else {
                while (*handle_slot(ent, 0x0ccee5c1) != 0x00385d90) Scr_Wait(0.2f);
            }
        }
        if (go && get_int_184(ent) != 0x0738b1a5) {
            // ScrField_hec299128 (a bool field; a missing word's upper bytes are the caller's stale ECX).
            *reinterpret_cast<uint8_t*>(key_slot(ent, 0xec299128, title_global<uint8_t>(0x00922ed0))) = 0;
            ScriptString copy;
            ScriptString_Copy(&copy, &name);
            BroScr_ForceShootAtPoint(ent, point, delay, copy, a7_or_0, target, wait_stop);
        }
    }
    scr_release(&name);
}

namespace {
using CallShoot = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                    const uint32_t*, const ScriptString*, const uint32_t*, const uint32_t*);
// The single shots: ScrCall_GScr_SetFlaggedAnimKnob(ent, 0x1973c1fe, anim, 1, 0, 1), then shoot at
// `target`'s origin when valid, else straight.
inline void cover_single_shot(uint32_t* ent, uint32_t anim, const uint32_t* target) {
    uint32_t notify = 0x1973c1fe;
    ScrCall_GScr_SetFlaggedAnimKnob(ent, &notify, anim, 0x3f800000, 0, 0x3f800000);
    if (static_cast<uint8_t>(ScrCall_Scr_EntityHandleIsValid(target))) {
        uint32_t origin[3];
        slot<void XB_CDECL(uint32_t, uint32_t, const uint32_t*)>(0x00922698)(
            *ent, 0x3f800000, ScrCall_ScrEnt_GetOrigin(target, origin));  // GScr_ActorShootAt
    } else {
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x0092269c)(*ent, 0x3f800000);  // GScr_ActorShoot
    }
}
inline void drop_anim_string(uint32_t offset) {
    ScriptString copy;
    ScriptString_Copy(&copy, reinterpret_cast<const ScriptString*>(anim_object() + offset));  // read and dropped
    scr_release(&copy);
}
inline bool is_launcher(uint32_t type) { return type == 0xa39d17dd || type == 0x318fbe80 || type == 0x45963119; }
}  // namespace

uint8_t* XB_CDECL BroScr_CoverShootBehavior(uint8_t* out, uint32_t ent, uint32_t wait_after, ScriptString mode,
                                            uint32_t target) {
    uint32_t type;
    BroScr_GetWeaponType(&type, ent);
    if (!mode.rep) ScriptString_AssignCString(&mode, pointer<const char>(0x00826938));
    uint32_t distance = bits(static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922690)(ent)));  // GScr_SentientNearestEnemyDist
    if (as_float(distance) == title_global<float>(0x00922eac)) distance = title_global<uint32_t>(0x0083ebec);
    uint8_t ok;
    const char* const any = pointer<const char>(0x007f63f0);
    if ((!(at<float>(anim_object(), 0x9c) >= as_float(distance)) || ScriptString_Equals(&mode, any)) &&
        static_cast<int32_t>(*int_slot(ent, 0x0feddbea)) > 0 && type != 0x0036b1a5 &&
        *BroScr_GetMuzzleOriginCall(&ok, ent, target, 0)) {
        uint32_t automatic, single;
        if (get_int_184(ent) == 0x070b89a4) {
            automatic = title_global<uint32_t>(0x008cf510);  // g_anim_stand_shoot_auto
            single = title_global<uint32_t>(0x008cf504);     // g_anim_stand_shoot
        } else {
            automatic = title_global<uint32_t>(0x008ce604);  // g_anim_crouch_shoot_auto
            single = title_global<uint32_t>(0x008ce5f8);     // g_anim_crouch_shoot
        }
        title_function<BeginThreadFunctor1>(0x0011b4d0)(0, 0, 0, 0, 0x001bfe70, ent);  // BroScr_LookAtEnemyLoop
        slot<SetAnim6>(0x009225d8)(ent, title_global<uint32_t>(0x008cf318), 0x3f800000, 0x3e19999a, 0x3f800000,
                                   0);  // GScr_SetAnimKnob(g_anim_Shoot)
        uint32_t value;
        {
            const uint32_t* const near = BroScr_GetFloatOrDefault500(&value, ent);
            *ScrField_ha3b8e90e(ent) = *near;
        }
        if (*BroScr_IsFlamethrower(&ok, ent)) {
            drop_anim_string(0x160);
            slot<SetAnim7>(0x009225e0)(ent, 0x54e3b2b6, automatic, 0x3f800000, 0x3e19999a, 0, 0);  // GScr_SetFlaggedAnimKnob
            Scr_Wait(0.2f);
            const uint32_t interval = *BroScr_GetShotInterval_2(&value, ent);
            uint32_t notify = 0x1973c1fe;
            ScrCall_GScr_SetFlaggedAnimKnob(&ent, &notify, automatic, 0x3f800000, 0x3d4ccccd, interval);
            int32_t shots = random_int(40) + 80;
            if (static_cast<int32_t>(*ScrField_h0feddbea(ent)) < shots) shots = static_cast<int32_t>(*ScrField_h0feddbea(ent));
            const uint32_t done = 0x3457a259;
            const uint32_t count = static_cast<uint32_t>(shots);
            title_function<CallShoot>(0x001e4d00)(0, 0, 0, 0, 0x001ab050, ent, &done, &count, &mode,
                                                  pointer<uint32_t>(0x00922ebc),
                                                  pointer<uint32_t>(0x00922e84));  // Scr_CallThread_ent_hash_i_str_i_ent(BroScr_ShootAtTarget)
            Scr_WaitTill(ent, 0x3457a259);
            Scr_Wait(0.3f);
            queue_notify(ent, 0x933be1f6);
        } else if (*BroScr_IsWeaponFireTypeAuto(&ok, ent)) {
            drop_anim_string(0x160);
            uint32_t notify = 0x54e3b2b6;
            ScrCall_SetFlaggedAnimKnobByRef(&ent, &notify, automatic, 0x3f800000, 0x3e19999a, 0);
            Scr_Wait(0.2f);
            const uint32_t interval = *BroScr_GetShotInterval_2(&value, ent);
            notify = 0x1973c1fe;
            ScrCall_GScr_SetFlaggedAnimKnob(&ent, &notify, automatic, 0x3f800000, 0x3d4ccccd, interval);
            int32_t shots = random_int(8) + 6;
            if (static_cast<int32_t>(*ScrField_h0feddbea(ent)) < shots) shots = static_cast<int32_t>(*ScrField_h0feddbea(ent));
            {
                const uint32_t done = 0x36f77b94;
                const float time = static_cast<float>(shots) * title_global<float>(0x0083ebf8) / as_float(interval);
                using CallEntHashF = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                                       const uint32_t*, const float*);
                title_function<CallEntHashF>(0x001e4dc0)(0, 0, 0, 0, 0x0018c6a0, ent, &done,
                                                         &time);  // Scr_CallThread_ent_hash_f_4(BroScr_NotifyAfterTimeChecked)
            }
            const uint32_t done = 0x36f77b94;
            const uint32_t count = static_cast<uint32_t>(shots);
            title_function<CallShoot>(0x001e4d00)(0, 0, 0, 0, 0x001ab530, ent, &done, &count, &mode,
                                                  pointer<uint32_t>(0x00922ebc),
                                                  pointer<uint32_t>(0x00922e84));  // (BroScr_ShootAtTarget_2)
            Scr_WaitTill(ent, 0x36f77b94);
            if (wait_after) Scr_Wait(0.25f);
            queue_notify(ent, 0x933be1f6);
        } else if (*BroScr_IsWeaponFireTypeSemi(&ok, ent)) {
            drop_anim_string(0x194);
            ScrCall_GScr_SetAnimKnob(&ent, single, 0x3f800000, 0x3e19999a, 0);
            Scr_Wait(0.2f);
            const int32_t count = random_int(2) + 2;
            for (int32_t i = 0; i < count; ++i) {
                const float last = as_float(*ScrField_ha3b8e90e(ent));
                if (static_cast<float>(last > at<float>(anim_object(), 0x9c) ? 1 : 0) == title_global<float>(0x0083eafc) &&
                    !ScriptString_Equals(&mode, any))
                    break;
                if (!(static_cast<int32_t>(*ScrField_h0feddbea(ent)) > 0)) break;
                cover_single_shot(&ent, single, &target);
                const uint32_t rounds = *ScrField_h0feddbea(ent);
                *ScrField_h0feddbea(ent) = rounds - 1;
                const uint32_t interval = *BroScr_GetShotInterval(&value, ent);
                const float half = title_global<float>(0x0083ec24);  // 0.5
                Scr_Wait(half);
                if (wait_after || i < count - 1) {
                    if (static_cast<float>(as_float(*BroScr_GetFloatOrDefault500(&value, ent)) > at<float>(anim_object(), 0x9c)
                                               ? 1
                                               : 0) != 0.0f &&
                        static_cast<float>(as_float(interval) > half ? 1 : 0) != 0.0f)
                        Scr_Wait(as_float(interval) - half);
                }
                const uint32_t* const near = BroScr_GetFloatOrDefault500(&value, ent);
                *ScrField_ha3b8e90e(ent) = *near;
            }
        } else {
            BroScr_ReloadLauncher(ent);
            if (!(at<float>(anim_object(), 0x9c) < as_float(*BroScr_GetFloatOrDefault500(&value, ent))) &&
                !ScriptString_Equals(&mode, any)) {
                queue_notify(ent, 0x807e5a80);
                *out = 0;
                scr_release(&mode);
                return out;
            }
            drop_anim_string(0x194);
            ScrCall_GScr_SetAnimKnob(&ent, single, 0x3f800000, 0x3e19999a, 0);
            Scr_Wait(is_launcher(type) ? 0.5f : 0.2f);
            cover_single_shot(&ent, single, &target);
            *ScrField_hc786fa48(ent) = 1;
            const uint32_t rounds = *ScrField_h0feddbea(ent);
            *ScrField_h0feddbea(ent) = rounds - 1;
            const uint32_t interval = *BroScr_GetShotInterval(&value, ent);
            Scr_Wait(is_launcher(type) ? as_float(interval) : title_global<float>(0x0083ec24));
        }
        slot<SetAnim6>(0x009225dc)(ent, title_global<uint32_t>(0x008cf318), 0, 0x3e4ccccd, 0x3f800000,
                                   0);  // GScr_SetAnim(g_anim_Shoot, 0)
        queue_notify(ent, 0x807e5a80);
        *out = 1;
    } else {
        *out = 0;
    }
    scr_release(&mode);
    return out;
}

void XB_CDECL BroScr_FallbackerTriggerThink(uint32_t trigger) {
    bool start;
    bool defined;
    if (!*ScrField_fallback_IsDefined(&defined, level_handle())) {
        start = true;
    } else {
        ScriptKeyTable* table = title_function<ScriptKeyTable* XB_CDECL(uint32_t)>(0x0011db20)(level_handle());
        if (!table) table = pointer<ScriptKeyTable>(0x00922e94);
        DynArray fallbacks;
        DynArray* const list = static_cast<DynArray*>(ScrField_fallback_Copy(reinterpret_cast<uint8_t*>(table) + 0xc, &fallbacks));
        const uint32_t index = *int_slot(trigger, 0x8bbe4822);  // script_fallback
        if (index >= list->size)
            title_function<void XB_THISCALL(DynArray*, uint32_t, uint32_t)>(0x0045cad0)(list, index + 1,
                                                                                       index + 4);  // DynArray_Resize4_PROLOGUE
        start = list->data[index] == 0xfefefefe;  // no fallback entry yet
        if (fallbacks.data) Scr_Free(fallbacks.data);
    }
    if (start) {
        const uint32_t* const level = pointer<uint32_t>(title_global<uint32_t>(0x00922ef8)
                                                           ? title_global<uint32_t>(0x00922ef8) + 4
                                                           : 0);
        const uint32_t* const b = int_slot(trigger, 0x1aa720d8);
        const uint32_t* const a = int_slot(trigger, 0x8bbe4822);
        const uint32_t owner = *level;
        uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x14);  // ScrIface_Alloc
        if (functor) {
            functor[0] = 0x007ffb9c;
            functor[1] = 0x001d3980;  // BroScr_FallbackThink
            functor[2] = owner;
            functor[3] = *a;
            functor[4] = *b;
        }
        slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(
            0, 0, 0, owner, functor, 0);  // Scr_CreateThread
    }
    Scr_WaitTill(trigger, 0xe2a210cf);  // trigger
    {
        const uint32_t* const level = pointer<uint32_t>(title_global<uint32_t>(0x00922ef8)
                                                           ? title_global<uint32_t>(0x00922ef8) + 4
                                                           : 0);
        const uint32_t index = *int_slot(trigger, 0x8bbe4822);
        ScriptString text;
        ScriptString_FromCStringPlusInt(&text, pointer<const char>(0x00826458), static_cast<int32_t>(index));
        uint32_t hash;
        const uint32_t value = *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&text));
        queue_notify(*level, value);
        scr_release(&text);
    }
    BroScr_DeleteFloodSpawner(trigger);
}

void XB_CDECL BroScr_AnimScript_ConcealmentStand(uint32_t ent) {
    end_on(ent, 0x07b4e7f7);
    BroScr_InitAnimScript(ent, 0xd8562c89);
    BroScr_ApplyDesiredAnimPose(ent);
    if (get_int_184(ent) == 0x19669cd9) {
        ScriptString unused;
        scr_string_from(&unused, 0x00826974);
        BroScr_PlayFaceAnim_3(ent, unused);
    }
    BroScr_MiscCombatTimer(ent);
    const uint32_t blend = title_global<uint32_t>(0x0083ec24);
    for (;;) {
        title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, 0x070b89a4, 0x00385d90);  // BroScr_SetPoseMovement
        set_orient_mode(ent, 0x0083adc0);
        drop_anim_string(0x194);
        BroScr_SetAimBlendTimes(blend, title_global<uint32_t>(0x00922ea4));
        BroScr_PlayIdleAnim(ent);
        BroScr_KeepAimingAtTarget(ent, title_global<uint32_t>(0x009230ec), title_global<uint32_t>(0x0092379c));
        uint32_t can;
        if (*BroScr_CheckCanShoot(&can, ent, title_global<uint32_t>(0x00922ebc))) {
            ScriptString empty;
            ScriptString_Copy(&empty, pointer<ScriptString>(0x00922e90));
            uint8_t shot;
            BroScr_CoverShootBehavior(&shot, ent, 0, empty, title_global<uint32_t>(0x00922e84));
        }
        BroScr_TryReload(ent, 0, title_global<uint32_t>(0x00922ea8));
    }
}

namespace {
// Scr_CallThread_ent_str_PROLOGUE(function) on the entity with a temporary title string.
inline void call_thread_str(uint32_t ent, uint32_t function, uint32_t text) {
    ScriptString arg;
    scr_string_from(&arg, text);
    using CallEntStr = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const ScriptString*);
    title_function<CallEntStr>(0x0045b2b0)(0, 0, 0, 0, function, ent, &arg);
    scr_release(&arg);
}
// The anim-script start: ends on 0x07b4e7f7, BroScr_InitAnimScript(script), the desired pose.
inline void anim_script_begin(uint32_t ent, uint32_t script) {
    end_on(ent, 0x07b4e7f7);
    BroScr_InitAnimScript(ent, script);
    BroScr_ApplyDesiredAnimPose(ent);
}
// A wounded actor (pose 0x19669cd9) first plays its face anim (BroScr_PlayFaceAnim_2, `pose`).
inline void wounded_face_anim(uint32_t ent, uint32_t pose) {
    if (get_int_184(ent) != 0x19669cd9) return;
    ScriptString unused;
    scr_string_from(&unused, 0x00826928);
    BroScr_PlayFaceAnim_2(ent, unused, pose);
}
}  // namespace

void XB_CDECL BroScr_CoverProneBehavior(uint32_t ent) {
    uint32_t spot[3];
    BroScr_GetCoverNodeOrOrigin(spot, ent);
    wounded_face_anim(ent, 0x0738b1a5);
    BroScr_MiscCombatTimer(ent);
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, 0x0738b1a5, 0x00385d90);  // BroScr_SetPoseMovement
    slot<void XB_CDECL(uint32_t, uint32_t*, uint32_t, uint32_t)>(0x009226f8)(ent, spot, 0, 0);  // GScr_ActorTryTeleport
    BroScr_ReloadLauncher(ent);
    set_orient_mode(ent, 0x0083adc0);
    call_thread_str(ent, 0x001dcd30, 0x00826d28);  // BroScr_AnimScript_Combat
}

void XB_CDECL BroScr_AnimScript_ConcealmentProne(uint32_t ent) {
    anim_script_begin(ent, 0xd8651488);
    BroScr_CoverProneBehavior(ent);
}

void XB_CDECL BroScr_AnimScript_CoverProne(uint32_t ent) {
    anim_script_begin(ent, 0x38b92491);
    BroScr_CoverProneBehavior(ent);
}

void XB_CDECL BroScr_AnimScript_CoverCrouch(uint32_t ent) {
    anim_script_begin(ent, 0x3124fe8f);
    wounded_face_anim(ent, 0xc1165808);
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, 0xc1165808, 0x00385d90);  // BroScr_SetPoseMovement
    call_thread_str(ent, 0x001d6a10, 0x00826b90);  // Bro_cover_crouch__cover_crouch
}

void XB_CDECL BroScr_AnimScript_ConcealmentCrouch(uint32_t ent) {
    anim_script_begin(ent, 0x2aa2fdb3);
    wounded_face_anim(ent, 0xc1165808);
    BroScr_MiscCombatTimer(ent);
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, 0xc1165808, 0x00385d90);
    call_thread_str(ent, 0x001d6a10, 0x00826b74);  // Bro_cover_crouch__cover_crouch
}

void XB_CDECL BroScr_AnimScript_CoverStand(uint32_t ent) {
    anim_script_begin(ent, 0x388a1c90);
    wounded_face_anim(ent, 0);
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, 0x070b89a4, 0x00385d90);
    call_thread_str(ent, 0x001dc0c0, 0x00826d4c);  // Bro_cover_stand__cover_stand
}

void XB_CDECL BroScr_AnimScript_Combat(uint32_t ent, ScriptString unused) {
    end_on(ent, 0x07b4e7f7);
    {
        ScriptString message;
        scr_string_from(&message, 0x00826c88);  // a debug print, built and dropped
        scr_release(&message);
    }
    {
        const uint32_t then = 0x001d9750;  // BroScr_CoverProne_2
        const uint32_t done = 0x6e5fd3a8;
        using Call = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                       const uint32_t*);
        title_function<Call>(0x001e4c00)(0, 0, 0, 0, 0x001b99c0, ent, &then, &done);  // Scr_CallThread_vt00825be4_2(BroScr_TurnToEnemy_2)
    }
    Scr_WaitTill(ent, 0x6e5fd3a8);
    call_thread_str(ent, 0x001de080, 0x00826c74);  // Bro_combat__RangedCombat
    scr_release(&unused);
}

namespace {
// The stop-at-corner pick: the actor already stopped at this corner (pose, movement 0x385d90,
// anim_special `special`, anim_idleset 'a' / 'b') keeps its idleset; else corner A when the yaw off
// the actor's ((yaw -/+ 0x83ef10 - angles) % 360) is within 180 (right: <= 180, left: >= 180).
inline bool at_corner(uint32_t ent, uint32_t pose, uint32_t special) {
    return get_int_184(ent) == pose && *handle_slot(ent, 0x0ccee5c1) == 0x00385d90 &&
           *ScrField_anim_special(ent) == special &&
           (*ScrField_anim_idleset(ent) == 0x61 || *ScrField_anim_idleset(ent) == 0x62);
}
inline bool corner_a_by_yaw(uint32_t ent, uint32_t yaw, bool left) {
    uint32_t out[3];
    const uint32_t* const angles = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922ba0)(out, ent);  // ScrEnt_GetAngles
    const uint32_t copy[3] = {angles[0], angles[1], angles[2]};
    float off = as_float(yaw) - as_float(copy[1]);
    off = left ? off + title_global<float>(0x0083ef10) : off - title_global<float>(0x0083ef10);
    const float turn = static_cast<float>(static_cast<int32_t>(off) % 360);
    return left ? turn >= title_global<float>(0x0083eb60) : title_global<float>(0x0083eb60) >= turn;
}
inline void stop_at_corner_stand(uint32_t ent, uint32_t yaw, ScriptString& unused, bool left) {
    ScriptString name;
    ScrCall_ScrActor_GetString_1384(&ent, &name);  // held to the end
    const uint32_t special = left ? 0x38948440 : 0xa1cb941d;
    bool a;
    if (at_corner(ent, 0x070b89a4, special))
        a = *ScrField_anim_idleset(ent) == 0x61;
    else
        a = corner_a_by_yaw(ent, yaw, left);
    if (a) {
        if (left) BroScr_CornerRun2AlertLeft(ent, yaw);
        else BroScr_CornerRun2AlertRight(ent, yaw);
    } else {
        if (left) BroScr_CornerBRun2AlertLeft(ent, yaw);
        else BroScr_CornerBRun2AlertRight(ent, yaw);
    }
    scr_release(&name);
    scr_release(&unused);
}
inline void stop_at_corner_crouch(uint32_t ent, uint32_t yaw, ScriptString& unused, bool left) {
    ScriptString name;
    ScrCall_ScrActor_GetString_1384(&ent, &name);  // held to the end
    uint32_t type;
    BroScr_GetWeaponType(&type, ent);
    const uint32_t special = left ? 0x38948440 : 0xa1cb941d;
    uint32_t set;
    if (at_corner(ent, 0xc1165808, special))
        set = *ScrField_anim_idleset(ent);
    else
        set = corner_a_by_yaw(ent, yaw, left) ? 0x61 : 0x62;
    uint8_t armed;
    if (!*BroScr_HasWeapon(&armed, ent)) {
        BroScr_UnarmedCrouchIdle(ent);
    } else if (type == 0xe6a85d8c || type == 0x0036b1a5) {  // pistol / none
        if (set == 0x61) {
            if (left) BroScr_CoverCrouchPlayIdle_5(ent);
            else BroScr_CoverCrouchPlayIdle_3(ent);
        } else {
            if (left) BroScr_CoverCrouchPlayIdle_6(ent);
            else BroScr_CoverCrouchPlayIdle_4(ent);
        }
    } else if (set == 0x61) {
        if (left) BroScr_CornerCrouchRun2AlertLeft(ent, yaw);
        else BroScr_CornerCrouchRun2AlertRight(ent, yaw);
    } else {
        if (left) BroScr_CornerBCrouchRun2AlertLeft(ent, yaw);
        else BroScr_CornerBCrouchRun2AlertRight(ent, yaw);
    }
    scr_release(&name);
    scr_release(&unused);
}
}  // namespace

void XB_CDECL BroScr_StopAtCornerStandRight(uint32_t ent, uint32_t yaw, ScriptString unused) {
    stop_at_corner_stand(ent, yaw, unused, false);
}
void XB_CDECL BroScr_StopAtCornerCrouchRight(uint32_t ent, uint32_t yaw, ScriptString unused) {
    stop_at_corner_crouch(ent, yaw, unused, false);
}
void XB_CDECL BroScr_StopAtCornerStandLeft(uint32_t ent, uint32_t yaw, ScriptString unused) {
    stop_at_corner_stand(ent, yaw, unused, true);
}
void XB_CDECL BroScr_StopAtCornerCrouchLeft(uint32_t ent, uint32_t yaw, ScriptString unused) {
    stop_at_corner_crouch(ent, yaw, unused, true);
}

void XB_CDECL BroScr_SpawnerFinishSpawn(uint32_t spawner, uint32_t ent, ScriptString name) {
    {
        ScriptString copy;
        ScriptString_Copy(&copy, &name);
        BroScr_SpawnerSetupAI(spawner, ent, copy);
    }
    if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922210)(ent)) {  // GScr_IsAlive
        // ScrField_finished_spawning (a bool field; a missing word's upper bytes are the caller's stale ECX).
        *reinterpret_cast<uint8_t*>(key_slot(ent, 0xc0bb2676, title_global<uint8_t>(0x00922ed0))) = 1;
        uint32_t hash;
        queue_notify(ent, *Scr_HashName(&hash, pointer<const char>(0x007f6000)));
    }
    scr_release(&name);
}

void XB_CDECL BroScr_TriggerNotifyLevelTargetname(uint32_t trigger) {
    Scr_WaitTill(trigger, 0xe2a210cf);  // trigger
    const uint32_t* const level = pointer<uint32_t>(title_global<uint32_t>(0x00922ef8) ? title_global<uint32_t>(0x00922ef8) + 4
                                                                                      : 0);
    {
        ScriptString name;
        uint32_t hash;
        const uint32_t value = *Scr_HashScriptString(
            &hash, reinterpret_cast<const uint8_t* const*>(ScrCall_ScrEnt_GetTargetname(&trigger, &name)));
        queue_notify(*level, value);
        scr_release(&name);
    }
    {
        ScriptString name;
        uint32_t hash;
        const uint32_t* const key = Scr_HashScriptString(
            &hash, reinterpret_cast<const uint8_t* const*>(ScrCall_ScrEnt_GetTargetname(&trigger, &name)));
        const uint32_t map = title_global<uint32_t>(0x00922ef8) + 0xfc;
        uint32_t pair[2];  // the found iterator, then the inserted (key, value) pair
        title_function<void XB_THISCALL(uint32_t, uint32_t*, const uint32_t*)>(0x0045e6d0)(map, &pair[0], key);  // HashMap_Find_PROLOGUE
        uint32_t node = pair[0];
        if (node == *reinterpret_cast<uint32_t*>(map + 8)) {  // end
            pair[0] = *key;
            pair[1] = title_global<uint8_t>(0x00922ed0);  // g_scrUndefinedBool (upper bytes stale in the original; 0 here)
            uint32_t result[2];
            using Insert = uint32_t* XB_THISCALL(uint32_t, uint32_t*, const uint32_t*);
            node = *title_function<Insert>(0x004608a0)(map, result, pair);  // HashMap_Insert_PROLOGUE_2
        }
        *reinterpret_cast<uint8_t*>(node + 0xc) = 1;  // the level flag of the targetname set
        scr_release(&name);
    }
    slot<void XB_CDECL(uint32_t)>(0x00922540)(trigger);  // GScr_Delete
}

void XB_CDECL BroScr_AnimScript_Reacquire(uint32_t ent) {
    anim_script_begin(ent, 0x812b5b15);
    for (;;) {
        if (get_int_184(ent) == 0x19669cd9) {
            ScriptString unused;
            scr_string_from(&unused, 0x00826f88);
            BroScr_PlayFaceAnim(ent, unused);
        }
        if (*handle_slot(ent, 0x764918ae) == 0xc4a729a7 && *handle_slot(ent, 0x0ccee5c1) == 0x00385d90)  // alertness, movement
            BroScr_KeepAimingAtTarget(ent, title_global<uint32_t>(0x00922ea4), title_global<uint32_t>(0x00922ea4));
        else
            Bro_stop__main(ent);
    }
}

namespace {
// The cover pose picks: the pose scripts take (ent, debug text, the node yaw, the cover node or
// origin, a flag byte; the original writes only the slot's low byte). _1 takes the text by address
// (released here), _2 by value.
using CoverPose1 = void XB_CDECL(uint32_t, const ScriptString*, uint32_t, const uint32_t*, uint32_t);
using CoverPose2 = void XB_CDECL(uint32_t, ScriptString, uint32_t, const uint32_t*, uint32_t);
inline void cover_pose_1(CoverPose1* function, uint32_t ent, uint32_t yaw, const uint32_t* node, uint32_t flag) {
    ScriptString text;
    scr_string_from(&text, 0x00826bbc);
    function(ent, &text, yaw, node, flag);
    scr_release(&text);
}
inline void cover_pose_2(CoverPose2* function, uint32_t ent, uint32_t yaw, const uint32_t* node, uint32_t flag) {
    ScriptString text;
    scr_string_from(&text, 0x00826ba0);
    function(ent, text, yaw, node, flag);
}
// Forever: stand / crouch allowed (a launcher never stands), 90 % chance of a pose change; the
// pose script for the current pose (`checked`: the last case also needs standing allowed).
inline void choose_pose_random(uint32_t ent, CoverPose1* pose_1, CoverPose2* pose_2, bool checked) {
    Scr_EndOnString(ent, pointer<const char>(0x008325dc));
    uint32_t out;
    const uint32_t yaw = *title_function<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00184310)(&out, ent);  // BroScr_GetNodeOrActorYaw
    uint32_t node[3];
    BroScr_GetCoverNodeOrOrigin(node, ent);
    for (;;) {
        uint32_t stand_ok = static_cast<uint8_t>(slot<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00922714)(ent, 0x070b89a4));
        const uint32_t crouch_ok = static_cast<uint8_t>(slot<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00922714)(ent, 0xc1165808));
        uint32_t type;
        if (*BroScr_GetWeaponType(&type, ent) == 0xa39d17dd) stand_ok = 0;
        const bool change = random_int(100) < 90;
        if ((get_int_184(ent) == 0xc1165808 && !stand_ok) || (get_int_184(ent) == 0x070b89a4 && !crouch_ok) || change) {
            if (get_int_184(ent) == 0xc1165808 && crouch_ok)
                cover_pose_2(pose_2, ent, yaw, node, 0);
            else
                cover_pose_1(pose_1, ent, yaw, node, 0);
        } else if (get_int_184(ent) == 0x070b89a4 && crouch_ok) {
            cover_pose_2(pose_2, ent, yaw, node, 1);
        } else if (!checked || (get_int_184(ent) == 0xc1165808 && stand_ok)) {
            cover_pose_1(pose_1, ent, yaw, node, 1);
        }
    }
}
}  // namespace

void XB_CDECL BroScr_ChoosePoseRandom(uint32_t ent) {
    choose_pose_random(ent, &BroScr_CoverLeftPose_1, &BroScr_CoverLeftPose_2, true);
}

void XB_CDECL BroScr_ChoosePoseRandom_2(uint32_t ent) {
    choose_pose_random(ent, &BroScr_CoverRightPose_1, &BroScr_CoverRightPose_2, false);
}

namespace {
using IsDefinedFn = bool* XB_CDECL(bool*, uint32_t);
inline bool field_defined(IsDefinedFn* is_defined, uint32_t ent) {
    bool defined;
    return *is_defined(&defined, ent);
}
using SetWord = void XB_CDECL(uint32_t, uint32_t);
inline void set_word(uint32_t address, uint32_t ent, uint32_t value) { slot<SetWord>(address)(ent, value); }
// Scr_CreateThread of a {0x007f5ba0, function, ent} functor on `ent`.
inline void create_entity_thread(uint32_t function, uint32_t ent) {
    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0xc);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f5ba0;
        functor[1] = function;
        functor[2] = ent;
    }
    slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(
        0, 0, 0, ent, functor, 0);  // Scr_CreateThread
}
// ScrEnt_SetTargetname(ent, a copy of the C string; the temporary released after).
inline void set_targetname_text(uint32_t ent, uint32_t text) {
    ScriptString name;
    scr_string_from(&name, text);
    ScriptString copy;
    ScriptString_Copy(&copy, &name);
    slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922b54)(ent, copy);  // ScrEnt_SetTargetname (releases its copy)
    scr_release(&name);
}
inline bool team_is(uint32_t* ent, uint32_t text) {
    ScriptString team;
    const bool same = ScriptString_Equals(ScrCall_ScrSentient_GetTeam(ent, &team), pointer<const char>(text));
    scr_release(&team);
    return same;
}
inline bool actor_name_is(uint32_t* ent, ScriptString* temp, uint32_t text) {
    return ScriptString_Equals(ScrCall_ScrActor_GetString_ec(ent, temp), pointer<const char>(text));
}
}  // namespace

namespace {
// GScr_CheckGrenadeThrow(ent, target out, offset, method) with a temporary method string.
inline void check_grenade_throw(uint32_t ent, float* target, const ScrVec3* offset, uint32_t method) {
    ScriptString text;
    scr_string_from(&text, method);
    slot<void XB_CDECL(uint32_t, float*, const ScrVec3*, ScriptString*)>(0x00922720)(ent, target, offset, &text);
    scr_release(&text);
}
inline bool vector_defined(const float* v) {
    const float lane = title_global<float>(0x00922eb8);  // g_scrUndefinedVecLane
    return v[0] != lane || v[1] != lane || v[2] != lane;
}
}  // namespace

uint32_t* XB_CDECL BroScr_TryGrenadeThrow(uint32_t* out, uint32_t ent, uint32_t anim, ScrVec3 offset) {
    float world[3];
    world[0] = world[1] = world[2] = title_global<float>(0x00922eb8);
    uint32_t throw_anim = undefined_int();
    if (anim) {
        throw_anim = anim;
        handle_slot(ent, 0x1de0c645);  // read, unused
    } else {
        const uint32_t weapon = *handle_slot(ent, 0x175567a8);
        if (weapon == 0x0036b1a5 || weapon == 0x3124fe8f) {
            const uint32_t pose = get_int_184(ent);
            offset.v[0] = 0;
            offset.v[1] = 0;
            uint32_t height;
            if (pose == 0x070b89a4) {  // stand
                height = title_global<uint32_t>(0x0083ee48);  // 80
                throw_anim = title_global<uint32_t>(0x008cf4b0);  // stand_grenade_throw
            } else {
                height = title_global<uint32_t>(0x007f5cfc);  // 65
                throw_anim = title_global<uint32_t>(0x008ce5c8);  // crouch_grenade_throw
            }
            offset.v[2] = height;
        }
    }
    if (throw_anim == undefined_int()) {
        *out = 0;
        return out;
    }
    float target[3];
    target[0] = target[1] = target[2] = title_global<float>(0x00922eb8);
    check_grenade_throw(ent, target, &offset, 0x0083ac94);  // "min energy"
    if (!vector_defined(target)) check_grenade_throw(ent, target, &offset, 0x0083ac88);  // "min time"
    if (!vector_defined(target)) check_grenade_throw(ent, target, &offset, 0x0083acb0);  // "max time"
    if (!vector_defined(target)) {
        slot<void XB_CDECL(uint32_t, const ScrVec3*, float*)>(0x0092263c)(ent, &offset, world);  // GScr_LocalToWorldCoords
        *out = 0;
        return out;
    }
    slot<void XB_CDECL(uint32_t, const ScrVec3*, float*)>(0x0092263c)(ent, &offset, world);
    slot<KnobAll>(0x009225e4)(ent, 0x59414211, throw_anim, body_root(), 0x3f800000, 0x3dcccccd, 0x3f800000, 1);
    *ScrField_he5f891d2(ent) = 1;
    Scr_WaitTillArgs(ent, 0x59414211, 0x0032aa25, 0, 0);  // the release note
    {
        ScriptString type;
        scr_string_from(&type, 0x008244e4);  // "grenadeattack"
        BroScr_BattleChatter(ent, type);
    }
    slot<void XB_CDECL(uint32_t)>(0x00922724)(ent);  // GScr_ActorThrowGrenade
    *ScrField_he5f891d2(ent) = 0;
    handle_note_tracks(ent, 0x59414211);
    *out = 0x19c8ae44;
    return out;
}

namespace {
// The chatter type again (BroScr_BattleChatter takes a copy), then the chatter time 0xad8f34a0 = now
// and the chatter chance 0xb3845cc4 = random(1).
inline void chatter_and_stamp(uint32_t ent, const ScriptString* type) {
    ScriptString copy;
    ScriptString_Copy(&copy, type);
    BroScr_BattleChatter(ent, copy);
    const uint32_t now = static_cast<uint32_t>(get_time());
    *ScrField_had8f34a0(ent) = now;
    float* const chance = reinterpret_cast<float*>(ScrField_hb3845cc4(ent));
    *chance = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922230)(0x3f800000));  // Scr_RandomFloat(1)
}
// Scr_FloatToInt(chatter chance * 15000 + 20000): the wait after the last chatter.
inline int32_t chatter_wait(uint32_t ent) {
    const float wait = as_float(*ScrField_hb3845cc4(ent)) * title_global<float>(0x007f6a60) + title_global<float>(0x00805688);
    int32_t out;
    title_function<int32_t XB_THISCALL(int32_t*, float)>(0x0011a750)(&out, wait);  // Scr_FloatToInt
    return out;
}
}  // namespace

void XB_CDECL BroScr_ChooseFlankOrCoverMe(uint32_t ent, ScriptString type) {
    BroScr_BattleChatterInit(ent);
    uint8_t out;
    if (!*BroScr_UpdateCombatDelay(&out, ent)) {
        const uint32_t now = static_cast<uint32_t>(get_time());
        *int_slot(ent, 0xad8f34a0) = now;
        *reinterpret_cast<uint8_t*>(ScrField_hbe944753(ent, 0)) = 0;
    } else if (!*BroScr_ChatterEnemySighted(&out, ent)) {
        if (ScriptString_Equals(&type, pointer<const char>(0x00826968)) ||    // "flankLeft"
            ScriptString_Equals(&type, pointer<const char>(0x0082695c))) {    // "flankRight"
            const float wait = as_float(*ScrField_hb3845cc4(ent)) * title_global<float>(0x0083ede0) +
                               title_global<float>(0x0083ede0);  // * 30000 + 30000
            uint32_t per_team;
            const int32_t until = static_cast<int32_t>(float_to_script_int(wait)) +
                                  static_cast<int32_t>(*BroScr_GetPerTeamValue_d4(&per_team, ent));
            if (get_time() > until) {
                chatter_and_stamp(ent, &type);
                BroScr_SetPerTeamValue_d4(ent, static_cast<uint32_t>(get_time()));
            }
        } else if (ScriptString_Equals(&type, pointer<const char>(0x00826954))) {  // "coverme"
            const int32_t wait = chatter_wait(ent);
            const int32_t until = static_cast<int32_t>(*ScrField_had8f34a0(ent)) + wait;
            if (get_time() > until) {
                bool chatter = false;
                uint32_t ref;
                ScriptString team;
                bool have_team = false;
                bool check = ScrCall_Scr_EntityHandleIsValid(BroScr_GetSentientRef3c(&ent, &ref)) != 0;
                if (!check) {
                    have_team = true;
                    check = ScriptString_Equals(ScrCall_ScrSentient_GetTeam(&ent, &team), pointer<const char>(0x0083208c));  // "axis"
                }
                if (check) {
                    uint32_t value;
                    const int32_t next = static_cast<int32_t>(*BroScr_GetPerTeamValue_d8(&value, ent));
                    chatter = get_time() > next;
                }
                if (have_team) scr_release(&team);
                if (chatter) {
                    uint32_t player_at[3];
                    const uint32_t* const player = ScrCall_ScrEnt_GetOrigin(pointer<uint32_t>(0x009236dc), player_at);
                    uint32_t at[3];
                    const uint32_t* const origin = ScrCall_ScrEnt_GetOrigin(&ent, at);
                    if (ScrCall_Scr_DistanceSquared(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(origin)),
                                                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(player))) <
                        title_global<float>(0x0083ee50)) {  // 128 squared
                        chatter_and_stamp(ent, &type);
                        BroScr_SetPerTeamValue_d8(ent, static_cast<uint32_t>(get_time() + 3000));
                    }
                }
            }
        } else {
            const int32_t wait = chatter_wait(ent);
            const int32_t until = static_cast<int32_t>(*ScrField_had8f34a0(ent)) + wait;
            if (get_time() > until) chatter_and_stamp(ent, &type);
        }
    }
    scr_release(&type);
}

namespace {
// The anim set's three anims at `index` (words): SetAnimKnob, SetFlaggedAnimKnob(0x54e3b2b6), SetAnimKnob
// (1, 0.1, 1, 0), then the note tracks of 0x54e3b2b6.
inline void cover_knob_triple(uint32_t ent, const uint32_t* set, uint32_t index) {
    const uint32_t a = set[index], b = set[index + 1], c = set[index + 2];
    slot<SetAnim6>(0x009225d8)(ent, a, 0x3f800000, 0x3dcccccd, 0x3f800000, 0);  // GScr_SetAnimKnob
    slot<SetAnim7>(0x009225e0)(ent, 0x54e3b2b6, b, 0x3f800000, 0x3dcccccd, 0x3f800000, 0);  // GScr_SetFlaggedAnimKnob
    slot<SetAnim6>(0x009225d8)(ent, c, 0x3f800000, 0x3dcccccd, 0x3f800000, 0);
    handle_note_tracks(ent, 0x54e3b2b6);
}
inline void aim_knobs(uint32_t ent, uint32_t flag, const uint32_t* set, uint32_t index, uint32_t rate) {
    BroScr_SetAimAnimKnobs(ent, flag, set[index], set[index + 1], set[index + 2], 0x3f800000, 0x3dcccccd, rate);
}
// BroScr_AimUntilCanShoot with the set's aim anims (0x58), 3 s, "Waiting to see enemy".
inline void aim_until_can_shoot(uint32_t ent, const uint32_t* set) {
    ScriptString name;
    scr_string_from(&name, 0x008268f0);
    BroScr_AimUntilCanShoot(ent, set[0x16], set[0x17], set[0x18], title_global<uint32_t>(0x0083ec9c), name);
}
inline bool can_shoot(uint32_t ent) {
    uint32_t out;
    return *BroScr_CheckCanShoot(&out, ent, title_global<uint32_t>(0x00922ebc)) != 0;
}
// The rounds field 0x0feddbea minus one (read, then the slot again).
inline void use_round(uint32_t ent) {
    const uint32_t rounds = *ScrField_h0feddbea(ent);
    *ScrField_h0feddbea(ent) = rounds - 1;
}
using CallEntHashFloat = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                           const float*);
using CallEntHashIntFloat = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                              const int32_t*, const uint32_t*);
// An automatic burst of `shots` (at most the rounds left): the shoot knobs, the done timer
// (BroScr_NotifyAfterTimeChecked, shots * 0.1 / interval) and BroScr_CoverShootOnNotify threads, a
// wait for 0x36f77b94.
inline void cover_auto_burst(uint32_t ent, const uint32_t* set, uint32_t interval, int32_t shots, const uint32_t* aim) {
    aim_knobs(ent, 0x54e3b2b6, set, 0x19, interval);
    if (static_cast<int32_t>(*ScrField_h0feddbea(ent)) < shots) shots = static_cast<int32_t>(*ScrField_h0feddbea(ent));
    const float time = static_cast<float>(shots) * title_global<float>(0x0083ebf8) / as_float(interval);
    const uint32_t done = 0x36f77b94;
    title_function<CallEntHashFloat>(0x001e4dc0)(0, 0, 0, 0, 0x0018c6a0, ent, &done,
                                                 &time);  // Scr_CallThread_ent_hash_f_4(BroScr_NotifyAfterTimeChecked)
    title_function<CallEntHashIntFloat>(0x001e5080)(0, 0, 0, 0, 0x0019da60, ent, &done, &shots,
                                                    aim);  // Scr_CallThread_ent_hash_i_f_2(BroScr_CoverShootOnNotify)
    Scr_WaitTill(ent, 0x36f77b94);
}
}  // namespace

void XB_CDECL BroScr_CoverAttack(uint32_t ent, uint32_t yaw, const uint32_t* set, ScriptString text) {
    ScriptString name;
    ScrCall_ScrActor_GetString_1384(&ent, &name);  // held to the end
    slot<KnobAllNoFlag>(0x009225c8)(ent, title_global<uint32_t>(0x008ce448), body_root(), 0x3f800000, 0x3e4ccccd,
                                    0x3f800000, 0);  // GScr_SetAnimKnobAll(g_anim_COVER)
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x009225d4)(ent, title_global<uint32_t>(0x008ce448), 0x3e4ccccd);  // GScr_ClearAnim
    {
        const uint32_t notify = 0x281d9462;
        using Call = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                       const uint32_t*, const uint32_t*, const uint32_t*);
        title_function<Call>(0x001e5000)(0, 0, 0, 0, 0x001b6fe0, ent, &notify, set + 0x10, set + 9,
                                         &yaw);  // Scr_CallThread_ent_hash_AnimDirections_AnimAngles_f_2(BroScr_BlendAimAnimsByYawLoop)
    }
    cover_knob_triple(ent, set, 0x13);  // set + 0x4c
    *handle_slot(ent, 0x764918ae) = 0xc4a729a7;  // alertness: alert
    drop_anim_string(0x194);
    for (uint32_t i = 0x1c; i < 0x1f; ++i)  // set + 0x70: the three aim-down knobs
        slot<SetAnim6>(0x009225d8)(ent, set[i], 0x3f800000, 0x3dcccccd, 0, 1);
    Scr_Wait(as_float(0x3dcccccd));  // 0.1
    begin_entity_thread(0x001bfe70, ent);  // BroScr_LookAtEnemyLoop
    uint32_t aim;  // the shot accuracy
    if (team_is(&ent, 0x00832084))  // "allies"
        aim = at<uint32_t>(anim_object(), 0x15c);
    else if (team_is(&ent, 0x0083208c))  // "axis"
        aim = at<uint32_t>(anim_object(), 0x54);
    else
        aim = title_global<uint32_t>(0x0083eb20);  // 1
    uint32_t type;
    bool out_of_rounds = false;
    if (*BroScr_GetWeaponFireType(&type, ent) == 0x00315aef) {  // automatic
        const int32_t total = random_int(9) + 7;
        const int32_t first = random_int(2) + 3;
        uint32_t value;
        const uint32_t interval = *BroScr_GetShotInterval_2(&value, ent);
        if (can_shoot(ent)) cover_auto_burst(ent, set, interval, first, &aim);
        if (static_cast<int32_t>(*int_slot(ent, 0x0feddbea)) <= 0) {
            out_of_rounds = true;
        } else {
            aim_until_can_shoot(ent, set);
            if (!slot<uint32_t XB_CDECL(uint32_t)>(0x0092271c)(ent) && can_shoot(ent)) {  // GScr_ActorHasPendingSentientSlot
                const uint32_t one = title_global<uint32_t>(0x0083eb20);
                cover_auto_burst(ent, set, interval, total - first, &one);
            }
        }
    } else if (*BroScr_GetWeaponFireType(&type, ent) == 0x003819c9) {  // semi-automatic
        const int32_t total = random_int(4) + 3;
        const int32_t first = random_int(total - 2);
        uint32_t value;
        const uint32_t interval = *BroScr_GetShotInterval(&value, ent);
        if (can_shoot(ent)) {
            for (int32_t i = 0; i < first; ++i) {
                if (static_cast<int32_t>(*ScrField_h0feddbea(ent)) <= 0) break;
                aim_knobs(ent, 0x5f978625, set, 0x1c, 0x3f800000);
                slot<SetWord>(0x0092269c)(ent, aim);  // GScr_ActorShoot
                use_round(ent);
                Scr_Wait(as_float(interval));
            }
        }
        if (static_cast<int32_t>(*int_slot(ent, 0x0feddbea)) <= 0) {
            out_of_rounds = true;
        } else {
            aim_until_can_shoot(ent, set);
            if (can_shoot(ent)) {
                for (int32_t i = first; i < total - 1; ++i) {
                    if (static_cast<int32_t>(*int_slot(ent, 0x0feddbea)) <= 0) break;
                    aim_knobs(ent, 0x5f978625, set, 0x1c, 0x3f800000);
                    slot<SetWord>(0x0092269c)(ent, 0x3f800000);
                    use_round(ent);
                    Scr_Wait(as_float(interval));
                }
                aim_knobs(ent, 0x5f978625, set, 0x1c, 0x3f800000);
                slot<SetWord>(0x0092269c)(ent, 0x3f800000);
                Scr_Wait(0.25f);
            }
        }
    } else {  // a bolt action: one shot
        {
            ScriptString message;
            scr_string_from(&message, 0x008268c4);  // "Cover attack - forgot to rechamber first!" (not printed)
            ScrField_hc786fa48(ent);
            scr_release(&message);
        }
        if (team_is(&ent, 0x0083208c)) Scr_Wait(as_float(0x3ecccccd));  // "axis": 0.4
        aim_until_can_shoot(ent, set);
        if (can_shoot(ent)) {
            uint32_t value;
            BroScr_GetShotInterval(&value, ent);  // unused
            aim_knobs(ent, 0x5f978625, set, 0x1f, 0x3f800000);  // set + 0x7c
            slot<SetWord>(0x0092269c)(ent, 0x3f800000);
            *ScrField_hc786fa48(ent) = 1;  // needs a rechamber
            use_round(ent);
            Scr_Wait(0.25f);
        }
    }
    if (out_of_rounds) {
        queue_notify(ent, 0x807e5a80);
    } else {
        if (!slot<uint32_t XB_CDECL(uint32_t)>(0x0092271c)(ent)) {
            aim_knobs(ent, 0x5f978625, set, 0x16, 0x3f800000);  // set + 0x58
            Scr_Wait(0.25f);
        }
        drop_anim_string(0x98);
        cover_knob_triple(ent, set, 0x22);  // set + 0x88
        *handle_slot(ent, 0x764918ae) = 0x06279a34;
        queue_notify(ent, 0x281d9462);
        queue_notify(ent, 0x807e5a80);
    }
    scr_release(&name);
    scr_release(&text);
}

namespace {
// The wide cover stance knob: corner stand pose when standing is allowed, else crouch, with look_straight.
inline void cover_wide_pose(uint32_t ent, uint32_t stand, uint32_t crouch) {
    if (static_cast<uint8_t>(slot<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00922714)(ent, 0x070b89a4)))  // GScr_ActorIsStanceAllowed
        slot<KnobAllNoFlag>(0x009225c8)(ent, title_global<uint32_t>(stand), title_global<uint32_t>(0x008ce9ac), 0x3f800000,
                                        0x3dcccccd, 0x3f800000, 0);  // GScr_SetAnimKnobAll
    else
        slot<KnobAllNoFlag>(0x009225c8)(ent, title_global<uint32_t>(crouch), title_global<uint32_t>(0x008ce9ac), 0x3f800000,
                                        0x3dcccccd, 0x3f800000, 0);
}
// The wide cover anim scripts: face the node yaw + 0x83eb60, wounded face anim, the cover blend; forever:
// cover type `cover` (idleset 0x61), face, teleport to the node, misc timer, launcher reload, the pose
// for random(1) s, then 1 s steps while a sentient slot is pending.
inline void anim_script_cover_wide(uint32_t ent, uint32_t script, uint32_t cover, uint32_t stand, uint32_t crouch) {
    anim_script_begin(ent, script);
    uint32_t out;
    const uint32_t yaw = *title_function<GetRef>(0x00184310)(&out, ent);  // BroScr_GetNodeOrActorYaw
    {
        ScriptString mode;
        scr_string_from(&mode, 0x0083adfc);  // "face angle"
        slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t)>(0x00922740)(
            ent, &mode, bits(as_float(yaw) + title_global<float>(0x0083eb60)), 0);  // GScr_ActorSetOrientMode
        scr_release(&mode);
    }
    uint32_t node[3];
    BroScr_GetCoverNodeOrOrigin(node, ent);
    wounded_face_anim(ent, 0);
    slot<KnobAllNoFlag>(0x009225c8)(ent, title_global<uint32_t>(0x008ce448), title_global<uint32_t>(0x008cf300), 0x3f800000,
                                    0x3dcccccd, 0x3f800000, 0);  // GScr_SetAnimKnobAll(COVER, root)
    const uint32_t facing = bits(as_float(yaw) + title_global<float>(0x0083eb60));
    for (;;) {
        *handle_slot(ent, 0x175567a8) = cover;
        *handle_slot(ent, 0xbfc60f36) = 0x61;
        {
            ScriptString mode;
            scr_string_from(&mode, 0x0083adfc);
            slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t)>(0x00922740)(ent, &mode, facing, 0);
            scr_release(&mode);
        }
        slot<void XB_CDECL(uint32_t, uint32_t*, uint32_t, uint32_t)>(0x009226f8)(ent, node, 0, 0);  // GScr_ActorTryTeleport
        BroScr_MiscCombatTimer(ent);
        BroScr_ReloadLauncher(ent);
        const float wait = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922230)(0x3f800000));  // Scr_RandomFloat(1)
        cover_wide_pose(ent, stand, crouch);
        Scr_Wait(wait);
        while (slot<uint32_t XB_CDECL(uint32_t)>(0x0092271c)(ent)) {  // GScr_ActorHasPendingSentientSlot
            cover_wide_pose(ent, stand, crouch);
            Scr_Wait(1.0f);
        }
    }
}
// The cover left / right anim scripts: their anim sets, movement stop, keeps a current idleset of the
// same cover type across the script start, wounded face anim, flank / cover-me chatter, then the
// random pose pick as a call thread.
inline void anim_script_cover_side(uint32_t ent, void (*init)(), uint32_t cover, uint32_t chatter, uint32_t pick,
                                   uint32_t pick_text) {
    init();
    *handle_slot(ent, 0x0ccee5c1) = 0x00385d90;
    end_on(ent, 0x07b4e7f7);
    uint32_t idleset = undefined_handle();
    bool same = false;
    if (*handle_slot(ent, 0x175567a8) == cover) {
        same = true;
        idleset = *handle_slot(ent, 0xbfc60f36);
    }
    BroScr_InitAnimScript(ent, cover);
    BroScr_ApplyDesiredAnimPose(ent);
    if (same) {
        *handle_slot(ent, 0x175567a8) = cover;
        *handle_slot(ent, 0xbfc60f36) = idleset;
    }
    wounded_face_anim(ent, 0);
    {
        ScriptString type;
        scr_string_from(&type, chatter);
        BroScr_ChooseFlankOrCoverMe(ent, type);
    }
    call_thread_str(ent, pick, pick_text);
}
}  // namespace

void XB_CDECL BroScr_AnimScript_CoverWideLeft(uint32_t ent) {
    anim_script_cover_wide(ent, 0x9c8414fc, 0xa1cb941d, 0x008ce424, 0x008ce3a0);  // cornerstandpose_right / cornercrouchpose_right
}
void XB_CDECL BroScr_AnimScript_CoverWideRight(uint32_t ent) {
    anim_script_cover_wide(ent, 0x91649867, 0x38948440, 0x008ce418, 0x008ce394);  // cornerstandpose_left / cornercrouchpose_left
}
void XB_CDECL BroScr_AnimScript_CoverLeft(uint32_t ent) {
    anim_script_cover_side(ent, &BroScr_CoverLeftInitAnimSets, 0xa1cb941d, 0x00826d1c, 0x001dbc90, 0x00826d10);  // BroScr_ChoosePoseRandom
}
void XB_CDECL BroScr_AnimScript_CoverRight(uint32_t ent) {
    anim_script_cover_side(ent, &BroScr_CoverRightInitAnimSets, 0x38948440, 0x00826d40, 0x001dbeb0,
                           0x00826d10);  // BroScr_ChoosePoseRandom_2
}

namespace {
// The prone cover loop: while prone is allowed and the actor can shoot from prone (with `may_move`:
// and may move to its own spot prone), the prone shoot loop, a reload, the nearest-enemy distance to
// 0xa3b8e90e; ends off a "Cover Prone" node with the enemy nearer than anim object + 0x90.
inline void cover_prone_loop(uint32_t ent, void(XB_CDECL* shoot_loop)(uint32_t), bool may_move) {
    Scr_Wait(0.0f);
    while (static_cast<uint8_t>(slot<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00922714)(ent, 0x0738b1a5))) {  // GScr_ActorIsStanceAllowed
        const uint32_t not_prone = get_int_184(ent) == 0x0738b1a5 ? 0 : 1;
        ScrVec3 none;
        none.v[0] = title_global<uint32_t>(0x00922ec4);
        none.v[1] = title_global<uint32_t>(0x00922ec8);
        none.v[2] = title_global<uint32_t>(0x00922ecc);
        uint8_t can;
        BroScr_CanShootFromPose(&can, ent, 0x0738b1a5, none, not_prone);
        if (may_move) {
            uint32_t buffer[3];
            const uint32_t* const a = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922ba0)(buffer, ent);  // ScrEnt_GetAngles
            uint32_t angles[3];
            angles[0] = a[0];
            angles[2] = a[2];
            const uint32_t yaw = a[1];
            uint32_t buffer2[3];
            const uint32_t* const o = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer2, ent);  // ScrEnt_GetOrigin
            uint32_t origin[3] = {o[0], o[1], o[2]};
            // The prone flag byte is written over the low byte of the `ent` argument slot; the
            // upper bytes passed are ent's.
            const uint32_t prone = (ent & 0xffffff00u) | (get_int_184(ent) == 0x0738b1a5 ? 1u : 0u);
            const uint8_t ok = static_cast<uint8_t>(slot<uint32_t XB_CDECL(uint32_t, uint32_t*, uint32_t, uint32_t)>(0x0092278c)(
                ent, origin, yaw, prone));  // GScr_ActorMayMoveToPoint
            (void)angles;
            if (!ok || !can) break;
        } else if (!can) {
            break;
        }
        shoot_loop(ent);  // BroScr_ProneShootLoop / _2
        BroScr_TryReload(ent, 0, title_global<uint32_t>(0x00922ea8));
        float* const last = reinterpret_cast<float*>(key_slot(ent, 0xa3b8e90e, undefined_float()));
        *last = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922690)(ent));  // GScr_SentientNearestEnemyDist
        bool stop = false;
        ScriptString type;
        if (!ScriptString_Equals(BroScr_GetNodeTypeAtPosition(&type, ent), pointer<const char>(0x00833b84))) {  // "Cover Prone"
            const float distance = as_float(*key_slot(ent, 0xa3b8e90e, undefined_float()));
            stop = static_cast<float>(at<float>(anim_object(), 0x90) > distance ? 1 : 0) != title_global<float>(0x0083eafc);
        }
        scr_release(&type);
        if (stop) break;
    }
}
}  // namespace

void XB_CDECL BroScr_CoverProne(uint32_t ent, ScriptString end) {
    end_on(ent, 0x07b4e7f7);
    end_on(ent, 0x06730ae8);
    Scr_EndOnString(ent, end.rep ? end.rep->text : nullptr);
    cover_prone_loop(ent, BroScr_ProneShootLoop, true);
    queue_notify(ent, 0xc3cc3e6a);
    scr_release(&end);
}

void XB_CDECL BroScr_CoverProne_2(uint32_t ent, uint32_t end) {
    end_on(ent, 0x07b4e7f7);
    end_on(ent, 0x06730ae8);
    end_on(ent, end);
    cover_prone_loop(ent, BroScr_ProneShootLoop_2, false);
    queue_notify(ent, 0xf3e6fbcb);
    queue_notify(ent, 0x6e5fd3a8);
}

void XB_CDECL BroScr_BipodGunnerStateMain(uint32_t ent, ScriptString unused) {
    end_on(ent, 0x07b4e7f7);
    {
        ScriptString reason;
        scr_string_from(&reason, 0x00826c88);  // "Script state called without reason."
        scr_release(&reason);
    }
    const uint32_t done = 0xc3cc3e6a;
    using CallEntHash = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*);
    title_function<CallEntHash>(0x004b08e0)(0, 0, 0, 0, 0x001e2e10, ent, &done);  // Scr_CallThread_ent_hash(BroScr_BipodGunner)
    Scr_WaitTill(ent, 0xc3cc3e6a);
    call_thread_str(ent, 0x001de080, 0x00826c74);  // Bro_combat__RangedCombat, "Dist < ProneRangeSq"
    scr_release(&unused);
}

void XB_CDECL BroScr_CoverStandIdle(uint32_t ent, ScrVec3 node, ScrVec3 direction, ScriptString text) {
    ScrVec3 none;
    none.v[0] = none.v[1] = none.v[2] = 0;  // 0.0f
    ScriptString name;
    ScrCall_ScrActor_GetString_1384(&ent, &name);  // held to the end
    BroScr_MiscCombatTimer(ent);
    {
        ScriptString mode;
        scr_string_from(&mode, 0x0083adcc);  // "face direction"
        slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, const ScrVec3*)>(0x00922740)(ent, &mode, 0,
                                                                                         &direction);  // GScr_ActorSetOrientMode
        scr_release(&mode);
    }
    BroScr_CoverStandIdleTransition(ent);
    slot<void XB_CDECL(uint32_t, ScrVec3*, uint32_t, uint32_t)>(0x009226f8)(ent, &node, 0, 0);  // GScr_ActorTryTeleport
    uint32_t thrown;
    BroScr_TryGrenadeThrow(&thrown, ent, title_global<uint32_t>(0x00922ea8), none);
    BroScr_TryReload(ent, title_global<uint32_t>(0x0083eca0), title_global<uint32_t>(0x00922ea8));
    BroScr_ReloadLauncher(ent);
    while (slot<uint32_t XB_CDECL(uint32_t)>(0x0092271c)(ent)) {  // GScr_ActorHasPendingSentientSlot
        Bro_cover_crouch__idle(ent);
        BroScr_TryGrenadeThrow(&thrown, ent, title_global<uint32_t>(0x00922ea8), none);
    }
    BroScr_FaceEnemyAnim(ent);
    scr_release(&name);
    scr_release(&text);
}

void XB_CDECL BroScr_StopCrouchIdle(uint32_t ent) {
    uint8_t out;
    if (!*BroScr_HasWeapon(&out, ent)) {
        BroScr_UnarmedCrouchIdle(ent);
        return;
    }
    uint32_t type;
    if (*BroScr_GetWeaponType(&type, ent) == 0xe6a85d8c) {  // pistol
        slot<KnobAll>(0x009225e4)(ent, 0xde61361c, title_global<uint32_t>(0x008ced00), body_root(), 0x3f800000, 0x3dcccccd,
                                  0x3f800000, 1);  // pistol_crouchaim_idle
        handle_note_tracks(ent, 0xde61361c);
        return;
    }
    if (*BroScr_UpdateCombatDelay(&out, ent)) {
        title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, 0xc1165808, 0x00385d90);  // BroScr_SetPoseMovement
        BroScr_TryReload(ent, title_global<uint32_t>(0x007c6490), title_global<uint32_t>(0x00922ea8));
        slot<KnobAllNoFlag>(0x009225c8)(ent, title_global<uint32_t>(0x008ce520), body_root(), 0x3f800000, 0x3e4ccccd,
                                        0x3f800000, 0);  // GScr_SetAnimKnobAll(crouch_aim_straight)
        slot<SetAnim7>(0x009225e0)(ent, 0xde61361c, title_global<uint32_t>(0x008ce520), 0x3f800000, 0x3ecccccd, 0x3f800000,
                                   1);  // GScr_SetFlaggedAnimKnob
        handle_note_tracks(ent, 0xde61361c);
        return;
    }
    bool first;
    if (get_int_184(ent) == 0xc1165808 && *ScrField_anim_movement(ent) == 0x00385d90 &&
        *ScrField_anim_alertness(ent) != 0xc4a729a7 &&
        (*ScrField_anim_idleset(ent) == 0x61 || *ScrField_anim_idleset(ent) == 0x62))
        first = *ScrField_anim_idleset(ent) == 0x61;  // corner A / B
    else
        first = random_int(100) < 50;
    if (first)
        BroScr_CrouchIdleTwitch_1(ent);
    else
        BroScr_CrouchIdleTwitch_2(ent);
}

namespace {
// A vector field kept as a pointer to a Scr_Alloc'd vector (added undefined when missing).
inline uint32_t* vector_field(uint32_t ent, uint32_t key) {
    ScriptKeyTable* const table = ent_script_object(ent);
    uint32_t* value = ScriptKeyTable_Find(table, key);
    if (!value) {
        const uint32_t lane = title_global<uint32_t>(0x00922eb8);
        uint32_t none[3] = {lane, lane, lane};
        uint32_t* const vector = Scr_Alloc(0xc, 4);
        if (vector) {
            vector[0] = none[0];
            vector[1] = none[1];
            vector[2] = none[2];
        }
        value = ScriptKeyTable_Set(table, key, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(vector)));
    }
    return pointer<uint32_t>(*value);
}
inline void start_turret_think(uint32_t ent, uint32_t* turret, uint32_t* flag) {
    using Start = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*, const uint32_t*);
    title_function<Start>(0x005656f0)(0, 0, 0, 0, 0x001deab0, ent, turret, flag);  // Scr_StartThread_ent_ent_b(BroScr_MgTurretThink)
}
// To the turret's goal spot (0x1b227) at the level's goal radius (+0x14c), then on "goal" uses the turret
// in mode 0xad76f051 (`target`: aimed at the player, else no target).
inline void turret_manual(uint32_t ent, uint32_t turret, bool target) {
    slot<void XB_CDECL(uint32_t, const uint32_t*)>(0x00922684)(ent, vector_field(turret, 0x0001b227));  // GScr_SetGoalPos
    slot<SetWord>(0x00922df4)(ent, at<uint32_t>(title_global<uint8_t*>(0x00922ef8), 0x14c));  // ScrSentient_SetFloat_18
    Scr_WaitTillString(ent, pointer<const char>(0x0083ad18));  // "goal"
    slot<SetWord>(0x00922584)(turret, 0xad76f051);  // GScr_Turret_SetMode
    if (target)
        slot<SetWord>(0x00922590)(turret, title_global<uint32_t>(0x009236dc));  // GScr_Turret_SetTargetEntity(the player)
    else
        slot<void XB_CDECL(uint32_t)>(0x00922594)(turret);  // GScr_Turret_ClearTargetEntity
    slot<SetWord>(0x0092272c)(ent, turret);  // GScr_ActorUseTurret
    Scr_WaitTillString(ent, pointer<const char>(0x0083a95c));  // "manual_ai"
}
}  // namespace

void XB_CDECL BroScr_TurretSetAIMode(uint32_t ent, uint32_t turret, uint32_t flag, uint32_t mode) {
    Scr_EndOnString(ent, pointer<const char>(0x00831f60));  // "death"
    end_on(ent, 0x2b740dd3);
    if (mode == 0x6c271e5a) {  // auto first
        for (;;) {
            start_turret_think(ent, &turret, &flag);
            Scr_WaitTillString(ent, pointer<const char>(0x0083a97c));  // "auto_ai"
            turret_manual(ent, turret, false);
        }
    }
    for (;;) {
        turret_manual(ent, turret, true);
        start_turret_think(ent, &turret, &flag);
        Scr_WaitTillString(ent, pointer<const char>(0x0083a97c));
    }
}

void XB_CDECL BroScr_CombatMain(uint32_t ent) {
    anim_script_begin(ent, 0xc0c70454);
    {
        ScriptString text;
        scr_string_from(&text, 0x00826d04);  // "CombatStart"
        scr_release(&text);
    }
    if (get_int_184(ent) == 0x19669cd9) {  // wounded
        ScriptString reason;
        scr_string_from(&reason, 0x00826cf4);  // "pose is wounded"
        BroScr_PlayFaceAnim_3(ent, reason);
        ScriptString text;
        scr_string_from(&text, 0x00826ce8);  // "bad pose"
        get_int_184(ent);
        scr_release(&text);
    }
    *handle_slot(ent, 0x2afa5406) = 0x0001894d;
    BroScr_MiscCombatTimer(ent);
    for (;;) {
        uint8_t out;
        if (*BroScr_MeleeCharge(&out, ent)) {
            call_thread_str(ent, 0x001dd8d0, 0x00826cc0);  // Bro_combat__MeleeCombat, "TryMeleeCharge passed"
            return;
        }
        if (!*title_function<uint8_t* XB_CDECL(uint8_t*, uint32_t)>(0x001b6ab0)(&out, ent)) break;  // FUN_001b6ab0 (other list)
    }
    float distance = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922690)(ent));  // GScr_SentientNearestEnemyDist
    if (distance == title_global<float>(0x00922eac)) distance = title_global<float>(0x0083ebec);
    *reinterpret_cast<float*>(ScrField_ha3b8e90e(ent)) = distance;
    uint32_t type;
    if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922d30)(ent) != undefined_handle() &&  // ScrActor_GetInt_f0
        *BroScr_GetWeaponFireType(&type, ent) == 0x0001bdc7) {
        call_thread_str(ent, 0x001e3430, 0x00826cd8);  // BroScr_BipodGunnerStateMain, "Carrying LMG"
        return;
    }
    uint32_t angles[3];
    const uint32_t yaw = ScrCall_ScrEnt_GetAngles(&ent, angles)[1];
    uint32_t at[3];
    const uint32_t* const origin = ScrCall_ScrEnt_GetOrigin(&ent, at);
    ScrVec3 point;
    point.v[0] = origin[0];
    point.v[1] = origin[1];
    point.v[2] = origin[2];
    uint32_t prone;
    if (*BroScr_ShouldGoProne_2(&prone, ent, point, yaw)) {
        call_thread_str(ent, 0x001dcd30, 0x00826cac);  // BroScr_AnimScript_Combat, "Dist > ProneRangeSq"
        return;
    }
    BroScr_TryReload(ent, 0, title_global<uint32_t>(0x00922ea8));
    BroScr_ReloadLauncher(ent);
    call_thread_str(ent, 0x001de080, 0x00826c74);  // Bro_combat__RangedCombat, "Dist < ProneRangeSq"
}

void XB_CDECL BroScr_CornerBehavior(uint32_t ent, ScrVec3 node, uint32_t yaw, const uint32_t* set) {
    slot<void XB_CDECL(uint32_t, ScrVec3*, uint32_t, uint32_t)>(0x009226f8)(ent, &node, 0, 0);  // GScr_ActorTryTeleport
    drop_anim_string(0x98);
    const ScrVec3* const throw_offset = reinterpret_cast<const ScrVec3*>(set + 0xc);  // set + 0x30
    {
        ScriptString text;
        scr_string_from(&text, 0x008269bc);
        BroScr_CornerPlayAnimForTime(ent, title_global<float>(0x0083eb24), set[4], text);  // set + 0x10
    }
    uint32_t thrown;  // the result word (0 / 0x19c8ae44), unused
    BroScr_TryGrenadeThrow(&thrown, ent, set[3], *throw_offset);
    BroScr_TryReload(ent, title_global<uint32_t>(0x0083ed2c), set[2]);
    BroScr_ReloadLauncher(ent);
    // A missing 0x602384d4 word: the undefined bool under stale stack bytes in the original; 0 here.
    if (*reinterpret_cast<uint8_t*>(key_slot(ent, 0x602384d4, title_global<uint8_t>(0x00922ed0)))) {  // casual
        ScriptString text;
        scr_string_from(&text, 0x008269bc);
        BroScr_CornerPlayAnimForTime(ent, title_global<float>(0x0083eb20), set[4], text);
        return;
    }
    bool waited = false;
    if (slot<uint32_t XB_CDECL(uint32_t)>(0x0092271c)(ent)) {  // GScr_ActorHasPendingSentientSlot
        do {
            if (waited) {
                ScriptString text;
                scr_string_from(&text, 0x008269a8);
                BroScr_ChooseFlankOrCoverMe(ent, text);
            }
            waited = true;
            ScriptString text;
            scr_string_from(&text, 0x0082699c);
            const float time = static_cast<float>(static_cast<int32_t>(slot<uint32_t XB_CDECL(uint32_t)>(0x00922d20)(ent)));  // ScrActor_GetInt_1308
            BroScr_CornerPlayAnimForTime(ent, time, set[4], text);
            if (!slot<uint32_t XB_CDECL(uint32_t)>(0x00922d20)(ent)) break;
            BroScr_TryGrenadeThrow(&thrown, ent, set[3], *throw_offset);
        } while (slot<uint32_t XB_CDECL(uint32_t)>(0x0092271c)(ent));
    }
    const int32_t r = random_int(100);
    if (waited && r < 50 && set[1]) {  // set + 4: the peek anim
        slot<KnobAll>(0x009225e4)(ent, 0x54e3b2b6, set[1], body_root(), 0x3f800000, 0x3dcccccd, 0x3f800000, 0);
        handle_note_tracks(ent, 0x54e3b2b6);
        return;
    }
    const uint32_t* const chance = ScrField_h01757a0e(ent);
    const int32_t roll = random_int(100);
    bool rambo = false;
    if (static_cast<float>(as_float(*chance) > static_cast<float>(roll) ? 1 : 0) != title_global<float>(0x0083eafc) &&
        set[6]) {
        ScriptString team;
        rambo = !ScriptString_Equals(ScrCall_ScrSentient_GetTeam(&ent, &team), pointer<const char>(0x00832084));
        scr_release(&team);
    }
    if (rambo) {
        BroScr_CornerRamboFire(ent, set[6], set[7]);  // set + 0x18 / 0x1c
        return;
    }
    ScriptString text;
    scr_string_from(&text, 0x0082698c);
    BroScr_CoverAttack(ent, yaw, set, text);
}

namespace {
// The cover pose scripts: the cover idle (0x001b7360 left / 0x001b7670 right) at the yaw, movement
// stop, a prone actor 0.5 to slot 0x9226b4; with `flag` the corner A / B stance change; then
// BroScr_CornerBehavior with the corner anim set. `right` differs in the order of the first steps.
inline void cover_pose(uint32_t ent, uint32_t yaw, const uint32_t* node, uint32_t flag, bool right, bool to_crouch,
                       uint32_t idle, uint32_t anim_a, uint32_t anim_b, uint32_t anim_set) {
    ScriptString name;
    ScrCall_ScrActor_GetString_1384(&ent, &name);  // held to the end
    title_function<void XB_CDECL(uint32_t, uint32_t)>(idle)(ent, yaw);
    uint32_t type = 0;
    if (!right) {
        if (get_int_184(ent) == 0x0738b1a5) slot<void XB_CDECL(uint32_t, uint32_t)>(0x009226b4)(ent, 0x3f000000);
        if (to_crouch) {
            *handle_slot(ent, 0x0ccee5c1) = 0x00385d90;
            BroScr_GetWeaponType(&type, ent);
        }
    } else {
        *handle_slot(ent, 0x0ccee5c1) = 0x00385d90;
        if (to_crouch) BroScr_GetWeaponType(&type, ent);
        if (get_int_184(ent) == 0x0738b1a5) slot<void XB_CDECL(uint32_t, uint32_t)>(0x009226b4)(ent, 0x3f000000);
    }
    if (to_crouch && type == 0xa39d17dd)
        title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, 0xc1165808, 0x00385d90);  // BroScr_SetPoseMovement
    if (static_cast<uint8_t>(flag)) {
        const uint32_t pose = to_crouch ? 0xc1165808 : 0x070b89a4;
        if (*handle_slot(ent, 0xbfc60f36) == 0x61) {
            if (to_crouch)
                BroScr_PlayStanceTransitionAnim(ent, title_global<uint32_t>(anim_a), title_global<float>(0x0083eb24), pose,
                                                0x00385d90, 0);
            else
                play_cover_anim(ent, title_global<uint32_t>(anim_a), pose);
        } else {
            play_cover_anim(ent, title_global<uint32_t>(anim_b), pose);
        }
    }
    const uint32_t* const set = pointer<uint32_t>(title_function<uint32_t XB_CDECL(uint32_t)>(anim_set)(ent));
    ScrVec3 at;
    at.v[0] = node[0];
    at.v[1] = node[1];
    at.v[2] = node[2];
    BroScr_CornerBehavior(ent, at, yaw, set);
    scr_release(&name);
}
}  // namespace

// Note: CoverLeftPose plays the "right" corner stance anims and CoverRightPose the "left" ones.
void XB_CDECL BroScr_CoverLeftPose_1(uint32_t ent, const ScriptString* text, uint32_t yaw, const uint32_t* node,
                                     uint32_t flag) {
    (void)text;
    cover_pose(ent, yaw, node, flag, false, false, 0x001b7360, 0x008ce358, 0x008cdecc,
               0x001ac2c0);  // BroScr_CoverLeftIdle, cornercrouch2stand_right / cornerb_, BroScr_GetCoverLeftAnimSet
}
void XB_CDECL BroScr_CoverLeftPose_2(uint32_t ent, ScriptString text, uint32_t yaw, const uint32_t* node, uint32_t flag) {
    cover_pose(ent, yaw, node, flag, false, true, 0x001b7360, 0x008ce3d0, 0x008ce130, 0x001ac2c0);  // stand2crouch_right
    scr_release(&text);
}
void XB_CDECL BroScr_CoverRightPose_1(uint32_t ent, const ScriptString* text, uint32_t yaw, const uint32_t* node,
                                      uint32_t flag) {
    (void)text;
    cover_pose(ent, yaw, node, flag, true, false, 0x001b7670, 0x008ce34c, 0x008cdec0,
               0x001ac360);  // BroScr_CoverRightIdle, cornercrouch2stand_left / cornerb_, BroScr_GetCoverRightAnimSet
}
void XB_CDECL BroScr_CoverRightPose_2(uint32_t ent, ScriptString text, uint32_t yaw, const uint32_t* node, uint32_t flag) {
    cover_pose(ent, yaw, node, flag, true, true, 0x001b7670, 0x008ce3c4, 0x008ce124, 0x001ac360);  // stand2crouch_left
    scr_release(&text);
}

}  // namespace game


namespace game {

using namespace bro_ai;


void XB_CDECL BroScr_SpawnerSetupAI(uint32_t spawner, uint32_t ent, ScriptString name) {
    (void)spawner;
    if (field_defined(ScrField_hc07c63aa_IsDefined, ent)) create_entity_thread(0x0019a200, ent);  // BroScr_SetIdlePoseByAllowedStance
    if (field_defined(ScrField_h95b9edc3_IsDefined, ent)) create_entity_thread(0x001816c0, ent);  // BroScr_EmptyFunc_40
    {
        uint32_t hash;
        slot<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x009221bc)(
            ent, 0xce270885, *Scr_HashName(&hash, pointer<const char>(0x007ff4e0)));  // GScr_SetEntVar("_spawner_gmi::tanksquish")
    }
    Scr_Wait(as_float(0x3d4ccccd));  // 0.05
    // `fixed`: the actor keeps its radius and goal (field 0x4a3b5a6c == 1, the special spawner names,
    // field 0xa287dc52).
    uint32_t fixed = 0;
    if (field_defined(ScrField_h4a3b5a6c_IsDefined, ent) && *int_slot(ent, 0x4a3b5a6c) == 1) fixed = 1;
    if (team_is(&ent, 0x00832084)) field_defined(ScrField_h7a6bf1dd_IsDefined, ent);  // "allies"; the result is unused
    DynArray found = {nullptr, 0, 0};
    if (name.rep) {
        const ScriptStringRep* const rep = name.rep;
        const uint32_t length = rep->length;
        if (length > 4 && rep->text[0] == 'a' && rep->text[1] == 'u' && rep->text[2] == 't' &&
            static_cast<uint8_t>(ScriptString_GetChar(&name, 3)) == 'o') {  // "auto..."
            Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&name)), 0xee2890d4, reinterpret_cast<uint8_t*>(&found), 0);
            using At = uint32_t* XB_THISCALL(DynArray*, uint32_t);
            bool named = false;
            if (found.size) {
                ScriptString target;
                named = ScrCall_ScrEnt_GetTargetname(title_function<At>(0x0045cb70)(&found, 0), &target)->rep != nullptr;  // DynArray_At
                scr_release(&target);
            }
            if (named) {
                ScriptString target;
                ScriptString_Assign(&name, ScrCall_ScrEnt_GetTargetname(title_function<At>(0x0045cb70)(&found, 0), &target));
                scr_release(&target);
            }
        }
        if (ScriptString_Equals(&name, pointer<const char>(0x007f49a8)) ||   // "mg42"
            ScriptString_Equals(&name, pointer<const char>(0x00824e44)) ||   // "grenade_spawner"
            ScriptString_Equals(&name, pointer<const char>(0x0081351c)) ||   // "friendly_wave"
            ScriptString_Equals(&name, pointer<const char>(0x00826eac)))     // "grenade_spawner_auto"
            fixed = 1;
        if (ScriptString_Equals(&name, pointer<const char>(0x00826e94))) {  // "secondfloor_gren_guy"
            set_targetname_text(ent, 0x00826e94);
            set_word(0x00922c8c, ent, 0x47afc800);  // ScrActor_SetFloat_82c(90000)
        }
    }
    if (field_defined(ScrField_h0c4d3fa0_IsDefined, ent)) {
        ScriptString copy;
        ScriptString_Copy(&copy, title_function<ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t)>(0x00458da0)(
                                     ent_script_object(ent), 0x0c4d3fa0));  // ScriptKeyTable_GetString
        slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922b54)(ent, copy);  // ScrEnt_SetTargetname
    }
    if (field_defined(ScrField_hdd7e1820_IsDefined, ent)) *reinterpret_cast<uint8_t*>(ScrField_hed6f4ccc(ent, 0)) = 1;
    // A missing 0xa287dc52 bool: the undefined byte under stale stack bytes in the original; 0 here.
    if (*reinterpret_cast<uint8_t*>(key_slot(ent, 0xa287dc52, title_global<uint8_t>(0x00922ed0)))) {
        fixed = 1;
    } else if (!fixed) {
        if (team_is(&ent, 0x00832084)) set_word(0x00922d54, ent, 0x3f666666);  // "allies": ScrActor_SetFloat_130c(0.9)
        if (team_is(&ent, 0x0083208c)) {  // "axis": grenade awareness by the skill
            ScriptString first;
            bool hard = ScriptString_Equals(ScrCall_GScr_GetCvar(&first, 0x00832fec), pointer<const char>(0x008351bc));  // g_gameskill "2"
            if (!hard) {
                ScriptString second;
                hard = ScriptString_Equals(ScrCall_GScr_GetCvar(&second, 0x00832fec), pointer<const char>(0x008398e0));  // "3"
                scr_release(&second);
            }
            scr_release(&first);
            uint32_t awareness;
            if (hard)
                awareness = random_int(3) ? 0x3f000000 : 0x3e4ccccd;  // 0.5 / 0.2
            else
                awareness = random_int(3) ? 0x3e4ccccd : 0;  // 0.2 / 0
            set_word(0x00922d54, ent, awareness);
        }
        set_word(0x00922d24, ent, 2);  // ScrActor_SetInt_1308
    }
    if (field_defined(ScrField_script_grenadeawareness_IsDefined, ent))
        set_word(0x00922d54, ent, *ScrField_script_grenadeawareness(ent));  // ScrActor_SetFloat_130c
    if (field_defined(ScrField_script_suppression_IsDefined, ent))
        set_word(0x00922d24, ent, static_cast<uint32_t>(static_cast<int32_t>(as_float(*ScrField_script_suppression(ent)))));
    if (field_defined(ScrField_h6a80ec39_IsDefined, ent)) set_word(0x00922d0c, ent, *ScrField_h6a80ec39(ent));  // SetFloat_7e4
    if (field_defined(ScrField_script_accuracy_IsDefined, ent)) set_word(0x00922c5c, ent, *ScrField_script_accuracy(ent));  // SetFloat_d4
    if (field_defined(ScrField_h66db43f0_IsDefined, ent)) set_word(0x00922c64, ent, *ScrField_h66db43f0(ent));  // SetFloat_d8
    if (team_is(&ent, 0x00832084)) set_word(0x00922cfc, ent, 0x41800000);  // "allies": ScrActor_SetFloat_7a4(16)
    if (field_defined(ScrField_script_ignoreme_IsDefined, ent)) set_word(0x00922e0c, ent, 1);  // ScrSentient_SetInt_30
    if (field_defined(ScrField_drophealth_IsDefined, ent) && *int_slot(ent, 0xd66dcf8c)) {
        uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0xc);  // ScrIface_Alloc
        if (functor) {
            functor[0] = 0x007f5ba0;
            functor[1] = 0x001e09e0;  // BroScr_DropHealth
            functor[2] = ent;
        }
        slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*)>(0x00922170)(
            0, 0, 0, ent, ent, 0x06730ae8, functor);  // Scr_BeginThreadEndOn
    }
    if (field_defined(ScrField_script_sightrange_IsDefined, ent))
        set_word(0x00922c8c, ent, bits(static_cast<float>(static_cast<int32_t>(*int_slot(ent, 0xd4133af2)))));  // SetFloat_82c
    if (!fixed) {
        if (field_defined(ScrField_script_radius_IsDefined, ent))
            set_word(0x00922df4, ent, bits(static_cast<float>(static_cast<int32_t>(*ScrField_script_radius(ent)))));  // SetFloat_18
        else
            set_word(0x00922df4, ent, 0x44000000);  // 512
    }
    if (!team_is(&ent, 0x0083208c)) {  // not "axis"
        if (field_defined(ScrField_h77123c0f_IsDefined, ent)) set_word(0x00922ca4, ent, *ScrField_h77123c0f(ent));  // SetInt_7b0
        if (field_defined(ScrField_h77123d19_IsDefined, ent)) set_word(0x00922cac, ent, *ScrField_h77123d19(ent));  // SetInt_7b4
        BroScr_SetCreditsName(ent, undefined_int());
        ScriptString a;
        bool stavro = actor_name_is(&ent, &a, 0x00826e88);  // "Cpl. Stavro"
        if (!stavro) {
            ScriptString b;
            stavro = actor_name_is(&ent, &b, 0x00826e7c);  // "Sgt. Stavro"
            if (!stavro) {
                ScriptString c;
                stavro = actor_name_is(&ent, &c, 0x00826e70);  // "Lt. Stavro"
                scr_release(&c);
            }
            scr_release(&b);
        }
        scr_release(&a);
        if (stavro) {
            ScriptString text;
            scr_string_from(&text, 0x00826e64);  // "Cpt. Stavro"
            ScriptString copy;
            ScriptString_Copy(&copy, &text);
            slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922d2c)(ent, copy);  // ScrActor_SetString_ec
            scr_release(&text);
        }
        const uint32_t level = *pointer<uint32_t>(title_global<uint32_t>(0x00922ef8) ? title_global<uint32_t>(0x00922ef8) + 4 : 0);
        uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
        if (functor) {
            functor[0] = 0x007f4a28;
            functor[1] = 0x00181c80;  // BroScr_EmptyFunc_44
            functor[2] = level;
            functor[3] = ent;
        }
        slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(
            0, 0, 0, level, functor, 0);  // Scr_CreateThread
    }
    if (field_defined(ScrField_h9f19950c_IsDefined, ent) &&
        ScriptString_Equals(title_function<ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t)>(0x00458da0)(
                                ent_script_object(ent), 0x9f19950c),
                            pointer<const char>(0x0083ac1c))) {  // "player"
        set_word(0x00922d74, ent, title_global<uint32_t>(0x009236dc));  // ScrActor_SetRef_12a0(the player)
        set_targetname_text(title_global<uint32_t>(0x009236dc), 0x0083ac1c);
    }
    if (field_defined(ScrField_script_grenades_IsDefined, ent)) set_word(0x00922d6c, ent, *int_slot(ent, 0x599fdcf6));  // SetInt_1328
    if (field_defined(ScrField_script_pacifist_IsDefined, ent)) set_word(0x00922d14, ent, 1);  // SetInt_7e8
    if (field_defined(ScrField_h2ebdbc24_IsDefined, ent)) set_word(0x00922d94, ent, 0);  // SetInt_1378
    if (field_defined(ScrField_h227464a0_IsDefined, ent)) set_word(0x00922b94, ent, *int_slot(ent, 0x227464a0));  // ScrEnt_SetHealth
    if (field_defined(ScrField_script_battle_IsDefined, ent) && *int_slot(ent, 0x026e8e55))
        title_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x0011b4d0)(
            0, 0, 0, 0, 0x001bc150, ent);  // Scr_BeginThreadFunctor1(FUN_001bc150)
    if (field_defined(ScrField_hb61a7388_IsDefined, ent)) {
        set_word(0x0092268c, ent, title_global<uint32_t>(0x009236dc));  // GScr_SetGoalEntity(the player)
    } else if (!field_defined(ScrField_h4b1fafe5_IsDefined, ent)) {
        if (field_defined(ScrField_hee885a52_IsDefined, ent)) {
            // (The original takes the bool through the dead `ent` argument slot.)
            if (!field_defined(ScrField_script_radius_IsDefined, ent)) set_word(0x00922df4, ent, 0x44480000);  // 800
            set_word(0x0092268c, ent, title_global<uint32_t>(0x009236dc));
            using GetFloat = long double XB_CDECL(uint32_t);
            while (slot<uint32_t XB_CDECL(uint32_t)>(0x00922210)(ent)) {  // GScr_IsAlive
                if (static_cast<float>(slot<GetFloat>(0x00922df0)(ent) > title_global<float>(0x0083ef08) ? 1 : 0) !=
                    title_global<float>(0x0083eafc))  // radius > 200
                    set_word(0x00922df4, ent,
                             bits(static_cast<float>(slot<GetFloat>(0x00922df0)(ent) - title_global<float>(0x0083ef08))));
                Scr_Wait(6.0f);
            }
        } else if (!fixed && !*reinterpret_cast<uint8_t*>(ScrField_using_respawner(ent, 0))) {
            ScriptString target;
            const bool has_target = ScrCall_ScrEnt_GetTarget(&ent, &target)->rep != nullptr;
            scr_release(&target);
            if (has_target)
                title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x0045aad0)(
                    0, 0, 0, 0, 0x001b5280, ent, 0x00922ea0);  // Scr_StartThread_ent_pathnode(BroScr_GoToBalconyNode)
        }
    }
    if (found.data) Scr_Free(found.data);
    scr_release(&name);
}

namespace {
using Vec3Out = uint32_t* XB_CDECL(uint32_t*, uint32_t);
// ScrEnt_GetOrigin(out, ent) copied to `v`.
inline void mg_origin(uint32_t ent, float* v) {
    uint32_t at[3];
    const uint32_t* const origin = slot<Vec3Out>(0x00922b20)(at, ent);
    std::memcpy(v, origin, 12);
}
inline void mg_set_origin(uint32_t ent, float x, float y, float z) {
    ScrVec3 v;
    v.v[0] = bits(x);
    v.v[1] = bits(y);
    v.v[2] = bits(z);
    slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922b24)(ent, v);  // ScrEnt_SetOrigin
}
// GScr_GetOrigin(the player, v) into undefined lanes.
inline void mg_player_origin(float* v) {
    const float lane = as_float(undefined_lane());
    v[0] = lane;
    v[1] = lane;
    v[2] = lane;
    slot<void XB_CDECL(uint32_t, float*)>(0x00922524)(title_global<uint32_t>(0x009236dc), v);
}
// Entry i, the array grown to (i + 1, i + 4) first when i is past the count.
inline uint32_t mg_at(DynArray* array, uint32_t i) {
    if (i >= array->size) DynArray_Resize(reinterpret_cast<uint8_t*>(array), i + 1, i + 4);
    return array->data[i];
}
inline long double mg_random_float(uint32_t max) { return slot<long double XB_CDECL(uint32_t)>(0x00922230)(max); }
inline bool mg_can_target() {
    uint8_t out;
    return *BroScr_MgTurretCanTargetStance(&out) != 0;
}
inline uint32_t mg_level_word(uint32_t offset) { return at<uint32_t>(title_global<uint8_t*>(0x00922ef8), offset); }
inline const uint32_t* mg_level_target_owner() {  // the level's +0x190 handle, as ScrEnt_GetTarget's self
    return reinterpret_cast<const uint32_t*>(title_global<uint8_t*>(0x00922ef8) + 0x190);
}
inline void mg_notify(uint32_t ent, uint32_t text) {
    uint32_t hash;
    queue_notify(ent, *Scr_HashName(&hash, pointer<const char>(text)));
}
// A bool field slot, added as the undefined bool when missing (the original's upper bytes are stale; 0 here).
inline uint8_t* mg_bool_slot(uint32_t ent, uint32_t key) {
    return reinterpret_cast<uint8_t*>(key_slot(ent, key, title_global<uint8_t>(0x00922ed0)));
}
inline uint32_t mg_spawn_at_zero(uint32_t classname_text) {
    ScriptString classname;
    scr_string_from(&classname, classname_text);
    ScrVec3 zero;
    zero.v[0] = bits(0.0f);
    zero.v[1] = bits(0.0f);
    zero.v[2] = bits(0.0f);
    const uint32_t spawned = slot<uint32_t XB_CDECL(ScriptString*, ScrVec3*, uint32_t)>(0x009222f0)(&classname, &zero, 0);  // GScr_Spawn
    scr_release(&classname);
    return spawned;
}
// A BroScr_DeleteEntsOnNotify thread on the level (Scr_StartThread_ent_ent_str_ent_ent_4).
inline void mg_delete_on(uint32_t* ent, uint32_t text, uint32_t* first, uint32_t* second) {
    const uint32_t object = title_global<uint32_t>(0x00922ef8);
    const uint32_t* const level = pointer<uint32_t>(object ? object + 4 : 0);
    ScriptString notify;
    scr_string_from(&notify, text);
    using Start = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                    const ScriptString*, const uint32_t*, const uint32_t*);
    title_function<Start>(0x001e4ae0)(0, 0, 0, 0, 0x0018ba10, *level, ent, &notify, first, second);
    scr_release(&notify);
}
// To the turret's spot at the level's goal radius, at the goal uses the turret.
inline void mg_goal_and_use(uint32_t ent, uint32_t turret, const uint32_t* spot) {
    slot<void XB_CDECL(uint32_t, const uint32_t*)>(0x00922684)(ent, spot);  // GScr_SetGoalPos
    slot<SetWord>(0x00922df4)(ent, mg_level_word(0x14c));                 // ScrSentient_SetFloat_18
    Scr_WaitTillString(ent, pointer<const char>(0x0083ad18));             // "goal"
    slot<SetWord>(0x0092272c)(ent, turret);                               // GScr_ActorUseTurret
}
// The burst targets: a random entity the level's +0x190 handle targets (key 0x0d786b4c), offset by
// (rand 30 - 15, rand 30 - 15, rand 40 - 60) (the y and x randoms in that order); returns the array.
inline uint32_t mg_burst_target(DynArray* found, float* offset) {
    {
        ScriptString name;
        const ScriptString* const target = ScrCall_ScrEnt_GetTarget(mg_level_target_owner(), &name);
        Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target)), 0x0d786b4c,
                        reinterpret_cast<uint8_t*>(found), 0);
        scr_release(&name);
    }
    const uint32_t pick = mg_at(found, static_cast<uint32_t>(random_int(static_cast<int32_t>(found->size))));
    const float y = static_cast<float>(mg_random_float(0x41f00000));  // 30
    const float x = static_cast<float>(mg_random_float(0x41f00000));
    const long double z = mg_random_float(0x42200000);              // 40
    offset[0] = x - title_global<float>(0x0083ef10);                // 15
    offset[2] = static_cast<float>(z - title_global<float>(0x0083eb3c));  // 60
    offset[1] = y - title_global<float>(0x0083ef10);
    return pick;
}
// One step of `target` toward the path entry the actor's index field 0x99cb8902 names: `step` units
// (or a unit vector with `unit`) while farther than `step`, else "next_target" on `ent`.
inline void mg_follow_step(uint32_t ent, uint32_t target, DynArray* path, float step, bool unit, float* cur) {
    mg_origin(target, cur);
    float goal[3];
    mg_origin(mg_at(path, *int_slot(ent, 0x99cb8902)), goal);
    const int32_t far = slot<long double XB_CDECL(const float*, const float*)>(0x00922268)(cur, goal) > step ? 1 : 0;
    if (static_cast<float>(far) != title_global<float>(0x0083eafc)) {
        float next[3];
        mg_origin(mg_at(path, *int_slot(ent, 0x99cb8902)), next);
        float diff[3] = {next[0] - cur[0], next[1] - cur[1], next[2] - cur[2]};
        float dir[3];
        const float lane = as_float(undefined_lane());
        dir[0] = lane;
        dir[1] = lane;
        dir[2] = lane;
        slot<void XB_CDECL(float*, const float*)>(0x00922280)(dir, diff);  // Scr_VectorNormalize
        if (unit) {
            cur[0] = cur[0] + dir[0];
            cur[1] = cur[1] + dir[1];
            cur[2] = cur[2] + dir[2];
        } else {
            const float dx = dir[0] * step;
            const float dy = step * dir[1];
            const float dz = step * dir[2];
            cur[0] = dx + cur[0];
            cur[1] = dy + cur[1];
            cur[2] = dz + cur[2];
        }
    } else {
        mg_notify(ent, 0x00825fec);  // "next_target"
    }
    mg_set_origin(target, cur[0], cur[1], cur[2]);
    Scr_Wait(0.1f);
}
}  // namespace

void XB_CDECL BroScr_MgTurretThink(uint32_t ent, uint32_t turret, uint8_t flag) {
    Scr_EndOnString(ent, pointer<const char>(0x00831f60));  // "death"
    Scr_EndOnString(ent, pointer<const char>(0x0083a97c));  // "auto_ai"
    {
        uint32_t unused[3];
        slot<Vec3Out>(0x00922b20)(unused, ent);
    }
    {
        ScriptString text;
        scr_string_from(&text, 0x00826e5c);  // "MANUAL"
        scr_release(&text);
    }
    slot<SetWord>(0x00922d14)(ent, 1);  // ScrActor_SetInt_7e8
    slot<void XB_CDECL(uint32_t, const float*)>(0x00922684)(ent, ScrField_org(turret));  // GScr_SetGoalPos
    slot<SetWord>(0x00922df4)(ent, mg_level_word(0x14c));
    Scr_WaitTillString(ent, pointer<const char>(0x0083ad18));  // "goal"
    if (flag != title_global<uint8_t>(0x00922ed0)) {
        // Unless the level flag of the turret's target is set, the level waits for it.
        bool clear;
        {
            ScriptString name;
            uint32_t hash;
            const uint32_t* const key = Scr_HashScriptString(
                &hash, reinterpret_cast<const uint8_t* const*>(ScrCall_ScrEnt_GetTarget(&turret, &name)));
            using Index = uint8_t* XB_THISCALL(uint32_t, const uint32_t*);
            clear = *title_function<Index>(0x004611e0)(title_global<uint32_t>(0x00922ef8) + 0xfc, key) == 0;
            scr_release(&name);
        }
        if (clear) {
            const uint32_t object = title_global<uint32_t>(0x00922ef8);
            const uint32_t* const level = pointer<uint32_t>(object ? object + 4 : 0);
            ScriptString name;
            const ScriptString* const target = ScrCall_ScrEnt_GetTarget(&turret, &name);
            Scr_WaitTillString(*level, target->rep ? target->rep->text : nullptr);
            scr_release(&name);
        }
    }
    slot<SetWord>(0x00922d14)(ent, 0);
    slot<SetWord>(0x00922584)(turret, 0xad76f051);  // GScr_Turret_SetMode(manual)
    slot<void XB_CDECL(uint32_t)>(0x00922594)(turret);  // GScr_Turret_ClearTargetEntity
    uint32_t target = mg_spawn_at_zero(0x0083ab88);  // "script_origin": what the turret aims at
    uint32_t model = mg_spawn_at_zero(0x0083ab98);   // "script_model"
    *key_slot(model, 0x070009e5, undefined_float()) = title_global<uint32_t>(0x0083ec9c);  // 3
    bool show;
    {
        ScriptString value;
        show = !ScriptString_Equals(ScrCall_GScr_GetCvar(&value, 0x007f49a8), pointer<const char>(0x0080da58));  // mg42, "off"
        scr_release(&value);
    }
    if (show) {
        ScriptString name;
        scr_string_from(&name, 0x00826e50);  // "xmodel/temp"
        slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(model, &name, 0);  // GScr_SetModel
        scr_release(&name);
    }
    {
        uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x14);  // ScrIface_Alloc
        if (functor) {
            functor[0] = 0x007f4a10;
            functor[1] = 0x00199940;  // BroScr_MgFollowEntOrigin
            functor[2] = model;
            functor[3] = turret;
            functor[4] = target;
        }
        slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(
            0, 0, 0, model, functor, 0);  // Scr_CreateThread
    }
    mg_delete_on(&ent, 0x00831f60, &target, &model);  // "death"
    mg_delete_on(&ent, 0x0083a97c, &target, &model);  // "auto_ai"
    *mg_bool_slot(turret, 0x19a3d654) = 0;
    DynArray path = {nullptr, 0, 0};
    {
        ScriptString name;
        scr_string_from(&name, 0x00826e44);  // "mg42_target"
        Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&name)), 0x0d786b4c,
                        reinterpret_cast<uint8_t*>(&path), 0);
        scr_release(&name);
    }
    if (static_cast<int32_t>(path.size) > 0) {
        // Targets along the mg42_target path (the array is never freed: the loops never end).
        float cur[3];
        mg_origin(mg_at(&path, static_cast<uint32_t>(random_int(static_cast<int32_t>(path.size)))), cur);
        using StartPath = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const DynArray*);
        title_function<StartPath>(0x004b1850)(0, 0, 0, 0, 0x001a5100, ent, &path);  // BroScr_CycleTargetsThread
        slot<void XB_CDECL(uint32_t, const float*)>(0x00922684)(ent, ScrField_org(turret));
        slot<SetWord>(0x00922df4)(ent, mg_level_word(0x14c));
        Scr_WaitTillString(ent, pointer<const char>(0x0083ad18));  // "goal"
        slot<SetWord>(0x0092272c)(ent, turret);  // GScr_ActorUseTurret
        *key_slot(ent, 0x7d9f2c49, 0) = target;
        slot<SetWord>(0x00922584)(turret, 0x6c271e5a);  // auto
        slot<SetWord>(0x00922590)(turret, target);     // GScr_Turret_SetTargetEntity
        mg_notify(turret, 0x00825fe0);                 // "StartFiring"
        const float step = title_global<float>(0x0083ef10);  // 15
        const float rate = title_global<float>(0x0083edc8);  // 0.05
        {
            float start[3];
            mg_origin(mg_at(&path, static_cast<uint32_t>(random_int(static_cast<int32_t>(path.size)))), start);
            mg_set_origin(target, start[0], start[1], start[2]);
        }
        // Along the path until the level's +0x190 entity is valid.
        while (!entity_handle_is_valid(mg_level_word(0x190))) mg_follow_step(ent, target, &path, step, false, cur);
        for (;;) {
            // Toward the player's eye (the fraction is a script int: it stays 0 until the stance check).
            int32_t t = 0;
            do {
                const float f = static_cast<float>(t);
                float player[3];
                mg_player_origin(player);
                uint32_t offset_out[3];
                const float* const offset = reinterpret_cast<const float*>(BroScr_GetPlayerStanceOffset(offset_out));
                const float eye_x = player[0] + offset[0];
                const float eye_z = offset[2] + player[2];
                const float eye_y = offset[1] + player[1];
                const float rest = title_global<float>(0x0083eb20) - f;  // 1 - f
                mg_set_origin(target, f * eye_x + rest * cur[0], f * eye_y + rest * cur[1], f * eye_z + rest * cur[2]);
                if (mg_can_target()) t = 2;
                Scr_Wait(0.08f);
                t = static_cast<int32_t>(float_to_script_int(static_cast<float>(t) + rate));
            } while (t < 1);
            // Follows the player's motion until the stance can be targeted.
            float last[3];
            mg_player_origin(last);
            while (!mg_can_target()) {
                float player[3];
                mg_player_origin(player);
                mg_set_origin(target, player[0], player[1], player[2]);
                float now[3];
                mg_origin(target, now);
                const float dz = now[2] - last[2];
                const float dy = now[1] - last[1];
                const float dx = now[0] - last[0];
                float again[3];
                mg_origin(target, again);
                const float sz = dz + again[2];
                const float sy = dy + again[1];
                const float sx = again[0] + dx;
                uint32_t offset_out[3];
                const float* const offset = reinterpret_cast<const float*>(BroScr_GetPlayerStanceOffset(offset_out));
                mg_set_origin(target, sx + offset[0], offset[1] + sy, offset[2] + sz);
                mg_player_origin(last);
                Scr_Wait(0.1f);
            }
            if (mg_can_target()) {
                // Bursts around the level entity's targets for 1.5-5.5 s.
                const int32_t soon = get_time() + 1500;
                const float end = static_cast<float>(soon + mg_random_float(0x457a0000));  // 4000
                for (;;) {
                    bool burst = false;
                    if (mg_can_target()) {
                        ScriptString name;
                        if (ScrCall_ScrEnt_GetTarget(mg_level_target_owner(), &name)->rep) {
                            const int32_t later = end > static_cast<float>(get_time()) ? 1 : 0;
                            burst = static_cast<float>(later) != title_global<float>(0x0083eafc);
                        }
                        scr_release(&name);
                    }
                    if (!burst) break;
                    DynArray found = {nullptr, 0, 0};
                    float offset[3];
                    const uint32_t pick = mg_burst_target(&found, offset);
                    float at[3];
                    mg_origin(pick, at);
                    mg_set_origin(target, at[0] + offset[0], at[1] + offset[1], at[2] + offset[2]);
                    Scr_Wait(0.1f);
                    if (found.data) Scr_Free(found.data);
                }
            }
            mg_notify(ent, 0x00825fec);  // "next_target"
            if (!mg_can_target()) continue;
            // Back along the path (unit steps) while the stance can be targeted.
            do {
                mg_follow_step(ent, target, &path, step, true, cur);
            } while (mg_can_target());
        }
    }
    // No path: manual turret, aimed at the player while the level's +0x190 entity is not valid, then
    // bursts in auto mode, then manual again.
    for (;;) {
        mg_goal_and_use(ent, turret, vector_field(turret, 0x0001b227));
        while (!entity_handle_is_valid(mg_level_word(0x190))) {
            if (!*mg_bool_slot(turret, 0x19a3d654)) {
                slot<SetWord>(0x00922590)(turret, title_global<uint32_t>(0x009236dc));  // at the player
                *mg_bool_slot(turret, 0x19a3d654) = 1;
                *key_slot(model, 0xc51219a9, 0) = title_global<uint32_t>(0x009236dc);
            }
            Scr_Wait(0.2f);
        }
        slot<SetWord>(0x00922584)(turret, 0x6c271e5a);  // auto
        mg_goal_and_use(ent, turret, vector_field(turret, 0x0001b227));
        mg_notify(turret, 0x00825fe0);  // "StartFiring"
        const int32_t soon = get_time() + 1500;
        const float end = static_cast<float>(soon + mg_random_float(0x457a0000));  // 4000
        for (;;) {
            const int32_t later = end > static_cast<float>(get_time()) ? 1 : 0;
            if (static_cast<float>(later) == title_global<float>(0x0083eafc)) break;
            if (!entity_handle_is_valid(mg_level_word(0x190))) break;
            DynArray found = {nullptr, 0, 0};
            float offset[3];
            const uint32_t pick = mg_burst_target(&found, offset);
            float at[3];
            mg_origin(pick, at);
            mg_set_origin(target, at[0] + offset[0], at[1] + offset[1], at[2] + offset[2]);
            slot<SetWord>(0x00922590)(turret, target);
            *key_slot(model, 0xc51219a9, 0) = target;
            Scr_Wait(static_cast<float>(mg_random_float(0x3f800000)));  // 1
            if (found.data) Scr_Free(found.data);
        }
        mg_notify(turret, 0x00825d0c);  // "StopFiring"
        mg_goal_and_use(ent, turret, vector_field(turret, 0x0001b227));
        if (*mg_bool_slot(turret, 0x19a3d654)) {
            slot<SetWord>(0x00922584)(turret, 0xad76f051);  // manual
            slot<void XB_CDECL(uint32_t)>(0x00922594)(turret);
            *mg_bool_slot(turret, 0x19a3d654) = 0;
            *key_slot(model, 0xc51219a9, 0) = model;
            mg_set_origin(model, 0.0f, 0.0f, 0.0f);
        }
        while (entity_handle_is_valid(mg_level_word(0x190))) Scr_Wait(0.2f);
        Scr_Wait(static_cast<float>(mg_random_float(0x3e4ccccd) + title_global<float>(0x0083eb58)));  // rand 0.2 + 0.75
    }
}

namespace {
// The rounds field 0x0feddbea minus one, both slots inline.
inline void use_round_inline(uint32_t ent) {
    const uint32_t rounds = *int_slot(ent, 0x0feddbea);
    *int_slot(ent, 0x0feddbea) = rounds - 1;
}
inline void actor_shoot(uint32_t ent) { slot<SetWord>(0x0092269c)(ent, 0x3f800000); }  // GScr_ActorShoot(1)
inline void prone_aim(uint32_t ent, const uint32_t* anims, uint32_t time, uint32_t rate) {
    BroScr_SetAimAnimKnobs(ent, 0x19714516, anims[0], anims[1], anims[2], title_global<uint32_t>(0x0083eb20), time, rate);
}
// The prone shoot loop: prone pose, busy (0xb66101f1) for the loop, the prone aim knobs, a random wait
// up to 1 s (`random_first`: drawn before the knobs), then by fire type an automatic burst of 6-13 shots
// on the "fire" note (0x0032aa25), 2-4 semi-automatic shots, or a launcher reload and one shot.
inline void prone_shoot_loop(uint32_t ent, bool random_first) {
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, 0x0738b1a5, 0x00385d90);  // BroScr_SetPoseMovement
    drop_anim_string(0x194);
    *int_slot(ent, 0xb66101f1) = 1;
    queue_notify(ent, 0x31e22e08);
    slot<SetAnim6>(0x009225d8)(ent, title_global<uint32_t>(0x008cf0b4), 0x3f800000, 0x3e19999a, 0x3f800000,
                               0);  // GScr_SetAnimKnob(g_anim_Prone)
    float wait = 0.0f;
    if (random_first) wait = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922230)(0x3f800000));
    const uint32_t aim[3] = {title_global<uint32_t>(0x008cf1a4), title_global<uint32_t>(0x008cf1bc),
                             title_global<uint32_t>(0x008cf1b0)};  // prone_shoot_left / straight / right
    const uint32_t automatic[3] = {title_global<uint32_t>(0x008cf180), title_global<uint32_t>(0x008cf198),
                                   title_global<uint32_t>(0x008cf18c)};  // prone_shoot_auto_left / straight / right
    slot<SetAnim6>(0x009225d8)(ent, aim[0], 0x3f800000, 0x3e19999a, 0, 0);
    slot<SetAnim7>(0x009225e0)(ent, 0x19714516, aim[1], 0x3f800000, 0x3e19999a, 0, 0);  // GScr_SetFlaggedAnimKnob
    slot<SetAnim6>(0x009225d8)(ent, aim[2], 0x3f800000, 0x3e19999a, 0, 0);
    if (!random_first) wait = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922230)(0x3f800000));
    Scr_Wait(wait);
    uint32_t type;
    if (*BroScr_GetWeaponFireType(&type, ent) == 0x00315aef) {  // automatic
        drop_anim_string(0x160);
        prone_aim(ent, automatic, title_global<uint32_t>(0x0083ed2c), bits(0.0f));
        Scr_Wait(0.2f);
        uint32_t value;
        const uint32_t interval = *BroScr_GetShotInterval_2(&value, ent);
        prone_aim(ent, automatic, title_global<uint32_t>(0x0083edc8), interval);  // 0.05
        const int32_t shots = random_int(8) + 6;
        if (shots > 0) {
            for (int32_t left = shots; left != 0; --left) {
                Scr_WaitTillArgs(ent, 0x19714516, 0x0032aa25, 0, 0);
                actor_shoot(ent);
                use_round_inline(ent);
            }
        }
    } else if (*BroScr_GetWeaponFireType(&type, ent) == 0x003819c9) {  // semi-automatic
        prone_aim(ent, aim, title_global<uint32_t>(0x0083eca0), bits(0.0f));
        Scr_Wait(0.2f);
        const int32_t shots = random_int(3) + 2;
        if (shots > 0) {
            const float gap = title_global<float>(0x0083ec24);
            for (int32_t i = 0; i < shots; ++i) {
                prone_aim(ent, aim, bits(0.0f), title_global<uint32_t>(0x0083eb20));
                actor_shoot(ent);
                use_round_inline(ent);
                uint32_t value;
                const float interval = as_float(*BroScr_GetShotInterval(&value, ent));
                Scr_Wait(gap);
                if (i < shots - 1) {
                    const int32_t longer = interval > gap ? 1 : 0;
                    if (static_cast<float>(longer) != title_global<float>(0x0083eafc)) Scr_Wait(interval - gap);
                }
            }
        }
    } else {
        BroScr_ReloadLauncher(ent);
        prone_aim(ent, aim, title_global<uint32_t>(0x0083eca0), bits(0.0f));
        Scr_Wait(0.2f);
        prone_aim(ent, aim, bits(0.0f), title_global<uint32_t>(0x0083eb20));
        actor_shoot(ent);
        use_round(ent);
        *ScrField_hc786fa48(ent) = 1;
        Scr_Wait(0.25f);
    }
    *int_slot(ent, 0xb66101f1) = 0;
}
}  // namespace

void XB_CDECL BroScr_ProneShootLoop(uint32_t ent) { prone_shoot_loop(ent, false); }
void XB_CDECL BroScr_ProneShootLoop_2(uint32_t ent) { prone_shoot_loop(ent, true); }

namespace {
inline uint32_t enemy_of(uint32_t ent) {
    uint32_t ref;
    return *slot<GetRef>(0x00922e00)(&ref, ent);  // ScrSentient_GetRef_3c
}
inline bool is_alive(uint32_t ent) { return slot<uint32_t XB_CDECL(uint32_t)>(0x00922210)(ent) != 0; }  // GScr_IsAlive
inline long double length_squared(const float* v) { return slot<long double XB_CDECL(const float*)>(0x00922274)(v); }
// GScr_GetOrigin(`ent`, v) into undefined lanes.
inline void gscr_origin(uint32_t ent, float* v) {
    const float lane = as_float(undefined_lane());
    v[0] = lane;
    v[1] = lane;
    v[2] = lane;
    slot<void XB_CDECL(uint32_t, float*)>(0x00922524)(ent, v);
}
// The melee charge spot: 32 units short of the enemy toward the actor, 32 above the enemy.
inline void charge_point(const float* enemy, const float* dir, float* point) {
    const float scale = title_global<float>(0x0083eeac);  // 32
    const float x = enemy[0] - dir[0] * scale;
    const float y = enemy[1] - dir[1] * scale;
    point[0] = x;
    point[1] = y;
    point[2] = enemy[2] + scale;
}
}  // namespace

uint8_t* XB_CDECL BroScr_MeleeCharge(uint8_t* out, uint32_t ent) {
    if (!is_alive(enemy_of(ent)) || slot<uint32_t XB_CDECL(uint32_t)>(0x00922dc0)(ent) != 0x070b89a4) {  // standing
        *out = 0;
        return out;
    }
    float enemy[3];
    gscr_origin(enemy_of(ent), enemy);
    float range[3];
    {
        const float lane = as_float(undefined_lane());
        range[0] = lane;
        range[1] = lane;
        range[2] = lane;
        float self[3];
        mg_origin(ent, self);
        range[2] = self[2] - enemy[2];
        range[0] = self[0] - enemy[0];
        range[1] = self[1] - enemy[1];
    }
    {
        float* const distance = reinterpret_cast<float*>(ScrField_ha3b8e90e(ent));
        *distance = static_cast<float>(length_squared(range));
    }
    if (at<float>(anim_object(), 0x9c) >= as_float(*ScrField_ha3b8e90e(ent))) {  // already in melee range
        *out = 1;
        return out;
    }
    {
        const int32_t far = as_float(*ScrField_ha3b8e90e(ent)) > at<float>(anim_object(), 0x118) ? 1 : 0;
        if (static_cast<float>(far) != title_global<float>(0x0083eafc)) {
            *out = 0;
            return out;
        }
    }
    uint8_t linked;
    if (*BroScr_MeleePair_Relink(&linked, ent, enemy_of(ent))) {
        title_function<BeginThreadFunctor1>(0x0011b4d0)(0, 0, 0, 0, 0x001bfe70, ent);  // BroScr_LookAtEnemyLoop
        float delta[3];
        {
            const float lane = as_float(undefined_lane());
            delta[0] = lane;
            delta[1] = lane;
            delta[2] = lane;
            slot<void XB_CDECL(float*, uint32_t, uint32_t, uint32_t)>(0x00922408)(
                delta, title_global<uint32_t>(0x008ce9d0), 0, 0x3f800000);  // GScr_GetMoveDelta(melee_charge2right)
        }
        const float note_time = title_global<float>(0x0083eca0);  // 0.2
        float last = as_float(*ScrField_ha3b8e90e(ent));  // the distance to the charge spot's projection
        float point[3];
        uint8_t safe;
        {
            float toward[3];
            const float lane = as_float(undefined_lane());
            toward[0] = lane;
            toward[1] = lane;
            toward[2] = lane;
            uint32_t at_self[3];
            const float* const self = reinterpret_cast<const float*>(ScrCall_ScrEnt_GetOrigin(&ent, at_self));
            toward[0] = enemy[0] - self[0];
            toward[1] = enemy[1] - self[1];
            toward[2] = enemy[2] - self[2];
            uint32_t at_dir[3];
            const float* const dir = reinterpret_cast<const float*>(
                ScrCall_Scr_VectorNormalize(at_dir, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(toward))));
            charge_point(enemy, dir, point);
            const float radius = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922df0)(ent));  // ScrSentient_GetFloat_18
            const uint32_t wide = title_global<uint32_t>(0x007e5bf4);  // 96
            ScrCall_ScrSentient_SetFloat_18(&ent, &wide);
            safe = static_cast<uint8_t>(slot<uint32_t XB_CDECL(uint32_t, const float*)>(0x009226f4)(ent, point)) != 0;  // GScr_ActorIsSafeMovePoint
            const uint32_t restore = bits(radius);
            ScrCall_ScrSentient_SetFloat_18(&ent, &restore);
        }
        bool first = true;
        for (;;) {
            if (!safe) break;
            if (!(at<float>(anim_object(), 0x118) >= as_float(*ScrField_ha3b8e90e(ent)))) break;
            const float near = at<float>(anim_object(), 0x9c);
            const int32_t outside = as_float(*ScrField_ha3b8e90e(ent)) > near ? 1 : 0;
            if (static_cast<float>(outside) == 0.0f) break;
            const int32_t spot_outside = last > near ? 1 : 0;
            if (static_cast<float>(spot_outside) == 0.0f) break;
            if (first) {
                ScriptString type;
                scr_string_from(&type, 0x00826908);  // "meleecharge"
                BroScr_BattleChatter(ent, type);
                BroScr_MeleePair_Link(ent, enemy_of(ent));
                first = false;
            }
            slot<KnobAll>(0x009225e4)(ent, 0x486d3e59, title_global<uint32_t>(0x008ce9c4), title_global<uint32_t>(0x008cd7dc),
                                      0x3f800000, 0x3e19999a, 0x3f800000, 0);  // GScr_SetFlaggedAnimKnobAll(melee_charge, Body)
            {
                ScriptString empty;
                ScriptString_Copy(&empty, pointer<ScriptString>(0x00922e90));
                title_function<HandleNoteTracksForTime>(0x001aeca0)(ent, note_time, 0x486d3e59, 0, empty);  // BroScr_HandleNoteTracksForTime
            }
            *ScrField_anim_movement(ent) = 0x0001c6ce;
            if (!is_alive(enemy_of(ent))) {
                queue_notify(ent, 0x807e5a80);
                uint32_t ref;
                BroScr_MeleePair_Unlink(ent, *BroScr_GetSentientRef3c(&ent, &ref));
                *out = 0;
                return out;
            }
            gscr_origin(enemy_of(ent), enemy);
            float range2[3];
            {
                const float lane = as_float(undefined_lane());
                range2[0] = lane;
                range2[1] = lane;
                range2[2] = lane;
                float self[3];
                mg_origin(ent, self);
                range2[0] = self[0] - enemy[0];
                range2[1] = self[1] - enemy[1];
                range2[2] = self[2] - enemy[2];
            }
            {
                float* const distance = reinterpret_cast<float*>(ScrField_ha3b8e90e(ent));
                *distance = static_cast<float>(length_squared(range2));
            }
            float world[3];
            {
                const float lane = as_float(undefined_lane());
                world[0] = lane;
                world[1] = lane;
                world[2] = lane;
                slot<void XB_CDECL(uint32_t, const float*, float*)>(0x0092263c)(ent, delta, world);  // GScr_LocalToWorldCoords
            }
            {
                const float ahead[3] = {world[0] - enemy[0], world[1] - enemy[1], world[2] - enemy[2]};
                last = static_cast<float>(length_squared(ahead));
            }
            float self[3];
            mg_origin(ent, self);
            float toward[3] = {enemy[0] - self[0], enemy[1] - self[1], enemy[2] - self[2]};
            float dir[3];
            {
                const float lane = as_float(undefined_lane());
                dir[0] = lane;
                dir[1] = lane;
                dir[2] = lane;
            }
            slot<void XB_CDECL(float*, const float*)>(0x00922280)(dir, toward);  // Scr_VectorNormalize
            charge_point(enemy, dir, point);
            const float radius = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922df0)(ent));
            slot<SetWord>(0x00922df4)(ent, 0x42c00000);  // 96
            slot<uint32_t XB_CDECL(uint32_t, const float*)>(0x009226f4)(ent, point);  // the result is dropped
            slot<SetWord>(0x00922df4)(ent, bits(radius));
        }
        if (at<float>(anim_object(), 0x9c) >= last && safe && *ScrField_anim_movement(ent) == 0x0001c6ce) {
            BroScr_MeleeAttackAnim(ent);
            uint32_t ref;
            if (!is_alive(*BroScr_GetSentientRef3c(&ent, &ref))) {
                queue_notify(ent, 0x807e5a80);
                BroScr_MeleePair_Unlink(ent, *BroScr_GetSentientRef3c(&ent, &ref));
                *out = 0;
                return out;
            }
            float unused[3];
            {
                const float lane = as_float(undefined_lane());
                unused[0] = lane;
                unused[1] = lane;
                unused[2] = lane;
            }
            (void)unused;
            uint32_t at_enemy[3];
            const float* const e = reinterpret_cast<const float*>(ScrCall_GScr_GetOrigin(at_enemy, BroScr_GetSentientRef3c(&ent, &ref)));
            const float ex = e[0], ey = e[1], ez = e[2];
            uint32_t at_self[3];
            const float* const s2 = reinterpret_cast<const float*>(ScrCall_ScrEnt_GetOrigin(&ent, at_self));
            const float range3[3] = {s2[0] - ex, s2[1] - ey, s2[2] - ez};
            float* const distance = reinterpret_cast<float*>(ScrField_ha3b8e90e(ent));
            *distance = static_cast<float>(length_squared(range3));
        }
        uint32_t ref;
        BroScr_MeleePair_Unlink(ent, *BroScr_GetSentientRef3c(&ent, &ref));
    }
    uint8_t result = 0;
    if (is_alive(enemy_of(ent)) && at<float>(anim_object(), 0x9c) >= as_float(*ScrField_ha3b8e90e(ent))) result = 1;
    queue_notify(ent, 0x807e5a80);
    *out = result;
    return out;
}

namespace {
// GScr_SetFlaggedAnimKnobAll(ent, 0xde61361c, anim, Body, 1, 0.1, 1, 1), then its note tracks.
inline void stand_idle_knob(uint32_t ent, uint32_t anim) {
    slot<KnobAll>(0x009225e4)(ent, 0xde61361c, anim, title_global<uint32_t>(0x008cd7dc), 0x3f800000, 0x3dcccccd, 0x3f800000, 1);
    handle_note_tracks(ent, 0xde61361c);
}
}  // namespace

void XB_CDECL BroScr_StopStandIdle(uint32_t ent) {
    uint32_t type;
    BroScr_GetWeaponType(&type, ent);
    uint8_t out;
    bool aim = false;
    if (*BroScr_UpdateCombatDelay(&out, ent)) {
        uint32_t yaw;
        const int32_t facing = title_global<float>(0x0083ebc4) > as_float(*BroScr_GetAbsYawToTarget(&yaw, ent)) ? 1 : 0;
        aim = static_cast<float>(facing) != title_global<float>(0x0083eafc);
    }
    if (aim) {
        title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, 0x070b89a4, 0x00385d90);  // BroScr_SetPoseMovement
        BroScr_TryReload(ent, title_global<uint32_t>(0x007c6490), title_global<uint32_t>(0x00922ea8));
        if (type == 0xe6a85d8c) {  // pistol
            stand_idle_knob(ent, title_global<uint32_t>(0x008cf078));  // pistol_standaim_idle
            return;
        }
        const int32_t roll = random_int(100);
        if (roll < 25) {
            slot<KnobAll>(0x009225e4)(ent, 0xde61361c, title_global<uint32_t>(0x008cf39c), title_global<uint32_t>(0x008cd7dc),
                                      0x3f800000, 0x3dcccccd, 0x3f800000, 1);  // stand_aim_straight_loop1
        } else {
            uint32_t anim;
            if (roll < 50)
                anim = title_global<uint32_t>(0x008cf3a8);  // loop2
            else if (roll < 75)
                anim = title_global<uint32_t>(0x008cf3b4);  // loop3
            else
                anim = title_global<uint32_t>(0x008cf3c0);  // loop4
            const uint32_t notify = 0xde61361c;
            ScrCall_GScr_SetFlaggedAnimKnobAll(&ent, &notify, anim, title_global<uint32_t>(0x008cd7dc), 0x3f800000, 0x3dcccccd,
                                               0x3f800000);
        }
        handle_note_tracks(ent, 0xde61361c);
        return;
    }
    const int32_t start = get_time();
    bool defined;
    if (*ScrField_standanim_IsDefined(&defined, ent)) {
        stand_idle_knob(ent, *key_slot(ent, 0x9a7145f5, 0));  // standanim
        return;
    }
    if (type == 0xe6a85d8c) {
        stand_idle_knob(ent, title_global<uint32_t>(0x008cf078));
        return;
    }
    const int32_t* const wounded = reinterpret_cast<const int32_t*>(ScrField_anim_woundedtime(ent));
    if (get_time() < *wounded) {
        BroScr_StandIdleTwitch_1(ent);
        return;
    }
    {
        const int32_t many = static_cast<float>(static_cast<int32_t>(*ScrField_h8ef2ad7c(ent))) > title_global<float>(0x0083eb24) ? 1 : 0;
        if (static_cast<float>(many) != title_global<float>(0x0083eafc)) {
            BroScr_StandIdleTwitch_1(ent);
            return;
        }
    }
    bool second;
    if (get_int_184(ent) == 0x070b89a4 && *ScrField_anim_movement(ent) == 0x00385d90 &&
        *ScrField_anim_alertness(ent) != 0xc4a729a7 &&
        (*ScrField_anim_idleset(ent) == 0x61 || *ScrField_anim_idleset(ent) == 0x62))
        second = *ScrField_anim_idleset(ent) == 0x61;  // corner A / B
    else
        second = random_int(100) < 50;
    if (second)
        BroScr_StandIdleTwitch_2(ent);
    else
        BroScr_StandIdleTwitch_3(ent);
    if (start == get_time()) Scr_Wait(0.2f);
}

namespace {
inline float force_shoot_left(uint32_t ent) {  // the time left to the end (field 0x18cd6af8, ms)
    const int32_t end = static_cast<int32_t>(*int_slot(ent, 0x18cd6af8));
    return static_cast<float>(end - get_time());
}
}  // namespace

uint8_t* XB_CDECL BroScr_ForceShoot(uint8_t* out, uint32_t ent, ScrVec3 point, float time, ScriptString name, uint32_t a7,
                                    uint32_t target, uint32_t wait_stop) {
    end_on(ent, 0x06730ae8);  // death
    end_on(ent, 0xcd9e558b);
    const float duration = time * title_global<float>(0x0083eb9c);
    const float end = static_cast<float>(get_time()) + duration;
    *int_slot(ent, 0x18cd6af8) = float_to_script_int(end);
    if (!name.rep) ScriptString_AssignCString(&name, pointer<const char>(0x007f63f0));  // "forceShoot"
    uint32_t a7_or_0 = a7;
    if (a7_or_0 == undefined_int()) a7_or_0 = 0;
    uint8_t result = 0;
    uint32_t type;
    bool go = false;
    if (*BroScr_GetWeaponType(&type, ent) != 0x0036b1a5) {
        if (*handle_slot(ent, 0x0ccee5c1) == 0x00385d90) {  // stopped
            go = true;
        } else if (wait_stop != undefined_int() && wait_stop != 0) {
            for (;;) {
                if (*handle_slot(ent, 0x0ccee5c1) == 0x00385d90) {
                    go = true;
                    break;
                }
                Scr_Wait(0.2f);
                const int32_t limit = static_cast<int32_t>(*int_slot(ent, 0x18cd6af8));
                if (get_time() > limit) break;
            }
        }
    }
    if (go && get_int_184(ent) != 0x0738b1a5) {  // not prone
        const int32_t limit = static_cast<int32_t>(*ScrField_h18cd6af8(ent));
        float left = static_cast<float>(limit - get_time());
        *reinterpret_cast<uint8_t*>(ScrField_hec299128(ent, 0)) = 0;
        for (;;) {
            const int32_t more = left > 0.0f ? 1 : 0;
            if (static_cast<float>(more) == 0.0f) break;
            using Call = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const ScrVec3*, const float*,
                                           const ScriptString*, const uint32_t*, const uint32_t*, const uint32_t*);
            title_function<Call>(0x001e4e40)(0, 0, 0, 0, 0x001ca420, ent, &point, &left, &name, &a7_or_0, &target,
                                             &wait_stop);  // Scr_CallThread_ent_vec_f_str_i_ent_i(BroScr_ForceShootThread)
            bool finished = true;
            while (static_cast<uint8_t>(slot<uint32_t XB_CDECL(uint32_t)>(0x00922190)(*int_slot(ent, 0x60d7df22))) == 1) {  // Scr_ThreadHandleIsValid
                Scr_Wait(0.1f);
                if (!(0.0f < force_shoot_left(ent))) {
                    finished = false;
                    break;
                }
            }
            if (!finished) break;
            if (*reinterpret_cast<uint8_t*>(ScrField_h552cfcab(ent, 0)) == 1)
                *reinterpret_cast<uint8_t*>(ScrField_h552cfcab(ent, 0)) = 0;
            else if (*ScrField_h88c31fb5(ent) == 1)
                Scr_WaitTill(ent, 0x94d3b23d);
            left = force_shoot_left(ent);
        }
        queue_notify(ent, 0x2cf20de4);
        BroScr_StopForceShoot(ent);
        result = 1;
    }
    *out = result;
    scr_release(&name);
    return out;
}

namespace {
inline bool stance_allowed(uint32_t ent, uint32_t pose) {
    return static_cast<uint8_t>(slot<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00922714)(ent, pose)) != 0;  // GScr_ActorIsStanceAllowed
}
inline uint32_t allowed_move_pose(uint32_t ent, uint32_t pose) {
    uint32_t out;
    return *BroScr_GetAllowedMovePose(&out, ent, pose);
}
// Off a corner (0x175567a8 not 0xa1cb941d / 0x38948440), every 10 s the corner idle set is re-picked
// (0x61 / 0x62 at random) and the time kept in 0x77749be9 (the second store inline or through the
// accessor).
inline void corner_idleset_repick(uint32_t ent, bool accessor_store) {
    const int32_t now = get_time();
    if (now - static_cast<int32_t>(*int_slot(ent, 0x77749be9)) <= 10000) return;
    if (random_int(100) < 50)
        *ScrField_anim_idleset(ent) = 0x61;
    else
        *ScrField_anim_idleset(ent) = 0x62;
    const uint32_t time = static_cast<uint32_t>(get_time());
    if (accessor_store)
        *ScrField_h77749be9(ent) = time;
    else
        *int_slot(ent, 0x77749be9) = time;
}
}  // namespace

void XB_CDECL Bro_stop__main(uint32_t ent) {
    {
        ScriptString text;
        scr_string_from(&text, 0x00826f7c);  // "StopStart"
        scr_release(&text);
    }
    anim_script_begin(ent, 0x00385d90);
    ScriptString type;
    ScriptString_FromRep(&type, nullptr);
    if (get_int_184(ent) == 0x19669cd9) {  // wounded
        ScriptString reason;
        scr_string_from(&reason, 0x00826f70);  // "stop::main"
        BroScr_PlayFaceAnim(ent, reason);
    }
    uint32_t pose;
    for (;;) {
        uint32_t node;
        title_function<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00184230)(&node, ent);  // BroScr_GetNodeAtPosition
        uint32_t yaw;
        if (node != 0 && node != 0xffffffff) {
            uint32_t buffer[3];
            yaw = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922ac0)(buffer, node)[1];  // ScrPathNode_GetAngles
            ScriptString node_type;
            title_function<ScriptString* XB_THISCALL(const uint32_t*, ScriptString*)>(0x001e45f0)(&node, &node_type);  // ScrPathNode_CopyType
            ScriptString_Assign(&type, &node_type);
            scr_release(&node_type);
        } else {
            yaw = bits(static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922d00)(ent)));  // ScrActor_GetFloat_154
            ScriptString_AssignCString(&type, pointer<const char>(0x00826f5c));  // "node was undefined"
        }
        drop_anim_string(0x98);
        pose = allowed_move_pose(ent, 0);
        const bool corner = *handle_slot(ent, 0x175567a8) == 0xa1cb941d || *handle_slot(ent, 0x175567a8) == 0x38948440;
        uint32_t fire;
        if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922d30)(ent) != undefined_handle() &&  // ScrActor_GetInt_f0
            *BroScr_GetWeaponFireType(&fire, ent) == 0x0001bdc7)
            break;  // an LMG
        if (ScriptString_Equals(&type, pointer<const char>(0x00833b6c))) {  // "Cover Left"
            if (!corner) corner_idleset_repick(ent, false);
            if (!stance_allowed(ent, 0xc1165808) && !stance_allowed(ent, 0x070b89a4)) pose = 0xc1165808;
            ScriptString reason;
            if (pose == 0x070b89a4) {
                scr_string_from(&reason, 0x00826f48);  // "At cover left node"
                BroScr_StopAtCornerStandRight(ent, yaw, reason);
            } else {
                scr_string_from(&reason, 0x00826f48);
                BroScr_StopAtCornerCrouchRight(ent, yaw, reason);
            }
            continue;
        }
        if (ScriptString_Equals(&type, pointer<const char>(0x00833b78))) {  // "Cover Right"
            if (!corner) corner_idleset_repick(ent, true);
            if (!stance_allowed(ent, 0xc1165808) && !stance_allowed(ent, 0x070b89a4)) pose = 0xc1165808;
            ScriptString reason;
            if (pose == 0x070b89a4) {
                scr_string_from(&reason, 0x00826f34);  // "At cover right node"
                BroScr_StopAtCornerStandLeft(ent, yaw, reason);
            } else {
                scr_string_from(&reason, 0x00826f34);
                BroScr_StopAtCornerCrouchLeft(ent, yaw, reason);
            }
            continue;
        }
        if (ScriptString_Equals(&type, pointer<const char>(0x00833b90))) {  // "Cover Crouch"
            BroScr_StopCoverCrouchIdle(ent);
            for (;;) Bro_cover_crouch__idle(ent);
        }
        if (ScriptString_Equals(&type, pointer<const char>(0x00833ba0)) ||   // "Cover Stand"
            ScriptString_Equals(&type, pointer<const char>(0x00833b38)))     // "Conceal Stand"
            pose = allowed_move_pose(ent, 0x070b89a4);
        else if (ScriptString_Equals(&type, pointer<const char>(0x00833b28)))  // "Conceal Crouch"
            pose = allowed_move_pose(ent, 0xc1165808);
        else if (ScriptString_Equals(&type, pointer<const char>(0x00833b84)) ||  // "Cover Prone"
                 ScriptString_Equals(&type, pointer<const char>(0x00833b18)))    // "Conceal Prone"
            pose = allowed_move_pose(ent, 0x0738b1a5);
        if (pose == 0x070b89a4) {
            BroScr_StopStandIdle(ent);
        } else if (pose == 0xc1165808) {
            BroScr_StopCrouchIdle(ent);
        } else if (pose == 0x0738b1a5) {
            BroScr_StopProneIdle(ent);  // never returns
        }
    }
    // An LMG carrier: stand, the bipod gunner state as a call thread, then the pose's idle.
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, 0x070b89a4, 0x00385d90);  // BroScr_SetPoseMovement
    call_thread_str(ent, 0x001e3430, 0x00826cd8);  // BroScr_BipodGunnerStateMain, "Carrying LMG"
    if (pose == 0x070b89a4) {
        BroScr_StopStandIdle(ent);
    } else if (pose == 0xc1165808) {
        BroScr_StopCrouchIdle(ent);
    } else if (pose == 0x008367fc) {  // the address of "prone" (never a pose)
        BroScr_StopProneIdle(ent);  // never returns
    }
    scr_release(&type);
}

namespace {
// The inlined int field test: a valid entity with a script object whose value (missing: the undefined
// int) is not the undefined int.
inline bool int_defined(uint32_t ent, uint32_t key) {
    if (!entity_handle_is_valid(ent)) return false;
    ScriptKeyTable* const table = title_function<ScriptKeyTable* XB_CDECL(uint32_t)>(0x0011db20)(ent);
    if (!table) return false;
    const uint32_t* const value = ScriptKeyTable_Find(table, key);
    const uint32_t none = undefined_int();
    return (value ? *value : none) != none;
}
inline const uint32_t* level_handle_or_null() {
    const uint32_t object = title_global<uint32_t>(0x00922ef8);
    return pointer<uint32_t>(object ? object + 4 : 0);
}
}  // namespace

void XB_CDECL BroScr_SpawnedThink(uint32_t, uint32_t spawner) {
    bool defined;
    if (*ScrField_script_mg42_IsDefined(&defined, spawner)) {
        const uint32_t* const mg = int_slot(spawner, 0xec070be6);  // script_mg42
        uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
        if (functor) {
            functor[0] = 0x007f4a1c;
            functor[1] = 0x001a63a0;  // BroScr_MgGetNewUser
            functor[2] = spawner;
            functor[3] = *mg;
        }
        slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(
            0, 0, 0, spawner, functor, 0);  // Scr_CreateThread
    }
    if (*ScrField_script_delete_IsDefined(&defined, spawner)) BroScr_AddKeyedEnt(spawner, *int_slot(spawner, 0x0e22c955));
    uint32_t spawned = 0;
    for (;;) {
        title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t*)>(0x004b1590)(spawner, 0x262a34d8, &spawned);  // Scr_WaitTillOutputEnt_ORAN
        if (!slot<uint32_t XB_CDECL(uint32_t)>(0x00922220)(spawned)) return;  // GScr_IsSentient
        if (!slot<uint32_t XB_CDECL(uint32_t)>(0x00922210)(spawned)) return;  // GScr_IsAlive
        {
            const uint32_t* const level = level_handle_or_null();
            using StartThread = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*, uint32_t,
                                              uint32_t);
            title_function<StartThread>(0x0052c6d0)(0, 0, 0, 0, 0x001a7010, *level, &spawned, 0x00922ebc,
                                                    0x00922e90);  // Scr_StartThread_ent_ent_i_str (BroScr_SetupFriendlyFireMissionFail)
        }
        {
            const uint32_t* const level = level_handle_or_null();
            ScriptString name;
            scr_string_from(&name, 0x00826ed4);  // "SPAWNED"
            uint32_t buffer[3];
            const uint32_t* const o = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer, spawned);  // ScrEnt_GetOrigin
            uint32_t origin[3];
            origin[0] = o[0];
            origin[2] = o[2];
            origin[1] = o[1];
            using StartN = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const ScriptString*,
                                             const uint32_t*);
            title_function<StartN>(0x0017b150)(0, 0, 0, 0, 0x0018bf00, *level, &name,
                                               origin);  // Scr_StartThreadFunctorN(BroScr_DebugCvarWait)
            scr_release(&name);
        }
        if (int_defined(spawner, 0x0e22c955)) BroScr_DeleteOthersWithKey(spawner, *int_slot(spawner, 0x0e22c955));
        if (int_defined(spawner, 0xd66dcf8c) && int_defined(spawner, 0xb5ea7cdf) && *int_slot(spawner, 0xd66dcf8c) != 0 &&
            *int_slot(spawner, 0xb5ea7cdf) != 0)
            *int_slot(spawner, 0xd66dcf8c) = 0;
        bool named;
        {
            ScriptString name;
            named = ScrCall_ScrEnt_GetTargetname(&spawner, &name)->rep != nullptr;
            scr_release(&name);
        }
        const uint32_t* const level = level_handle_or_null();
        ScriptString name;
        if (named)
            ScrCall_ScrEnt_GetTargetname(&spawner, &name);
        else
            ScriptString_Copy(&name, pointer<ScriptString>(0x00922e90));
        BroScr_SpawnerFinishSpawn(*level, spawned, name);  // the level as the spawner argument
    }
}

namespace {
// The level's anim map (+0x19c) entry [animname][anime] = anim (the inner entry added as 0 when missing).
inline void set_level_anim(uint32_t animname, uint32_t anime, uint32_t anim) {
    using Index = uint32_t* XB_THISCALL(uint32_t, const uint32_t*);
    using Find = uint32_t* XB_THISCALL(uint32_t*, uint32_t*, const uint32_t*);
    const uint32_t key = anime;
    const uint32_t name = animname;
    uint32_t* const map = title_function<Index>(0x004641e0)(title_global<uint32_t>(0x00922ef8) + 0x19c, &name);  // HashMap_Index_8
    uint32_t it;
    title_function<Find>(0x0045e6d0)(map, &it, &key);  // HashMap_Find
    if (it == map[2]) {
        const uint32_t pair[2] = {key, 0};
        uint32_t inserted;
        it = *title_function<Find>(0x004608a0)(map, &inserted, pair);  // HashMap_Insert
    }
    pointer<uint32_t>(it)[3] = anim;
}
// GScr_SetFlaggedAnimKnobAll(ent, notify, anim, Body, 1, 0.1, 1, 1), then its note tracks.
inline void flagged_body_knob(uint32_t ent, uint32_t notify, uint32_t anim) {
    slot<KnobAll>(0x009225e4)(ent, notify, anim, title_global<uint32_t>(0x008cd7dc), 0x3f800000, 0x3dcccccd, 0x3f800000, 1);
    handle_note_tracks(ent, notify);
}
}  // namespace

void XB_CDECL BroScr_BipodGunner(uint32_t ent, uint32_t end) {
    end_on(ent, 0x07b4e7f7);
    end_on(ent, 0x06730ae8);  // death
    end_on(ent, end);
    bool defined;
    if (*ScrField_hf56955a1_IsDefined(&defined, ent))
        while (*int_slot(ent, 0xf56955a1) != 0) Scr_Wait(0.1f);
    Scr_WaitTill(ent, 0x0032304c);
    *handle_slot(ent, 0x2afa5406) = 0x0001894d;
    set_level_anim(0xc966b4d2, 0x00351de5, title_global<uint32_t>(0x008cd80c));  // c_mg_mg34_run
    set_level_anim(0xc966b4d2, 0xce69bd99, title_global<uint32_t>(0x008cd7f4));  // c_mg_mg34_deploy
    set_level_anim(0xc966b4d2, 0x7938b286, title_global<uint32_t>(0x008cd800));  // c_mg_mg34_go_prone
    {
        ScriptString name;
        scr_string_from(&name, 0x007fc050);  // "gunner"
        ScriptString copy;
        ScriptString_Copy(&copy, &name);
        slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922c44)(ent, copy);  // ScrEnt_SetAnimname
        scr_release(&name);
    }
    flagged_body_knob(ent, 0x365de636, title_global<uint32_t>(0x008cd7f4));  // deploy
    flagged_body_knob(ent, 0x09334428, title_global<uint32_t>(0x008cd800));  // go prone
    uint32_t angles[3];
    {
        uint32_t buffer[3];
        const uint32_t* const a = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922ba0)(buffer, ent);  // ScrEnt_GetAngles
        angles[0] = a[0];
        angles[1] = a[1];
        angles[2] = a[2];
    }
    float offset[3];
    {
        float forward[3];
        const float lane = as_float(undefined_lane());
        forward[0] = lane;
        forward[1] = lane;
        forward[2] = lane;
        slot<void XB_CDECL(float*, const uint32_t*)>(0x00922290)(forward, angles);  // Scr_AnglesToForward
        const float scale = title_global<float>(0x007fa488);
        offset[0] = forward[0] * scale;
        offset[1] = forward[1] * scale;
        offset[2] = forward[2] * scale;
    }
    ScriptString info;
    ScriptString_FromRep(&info, nullptr);
    ScriptString model;
    ScriptString_FromRep(&model, nullptr);
    const uint32_t weapon = slot<uint32_t XB_CDECL(uint32_t)>(0x00922d30)(ent);  // ScrActor_GetInt_f0
    if (weapon == 0x0033c678 || weapon == 0x00371a54) {
        ScriptString_AssignCString(&info, pointer<const char>(0x00826f0c));   // "mg34_bipod_prone"
        ScriptString_AssignCString(&model, pointer<const char>(0x00826f00));  // "w_ge_mg42"
    } else if (weapon == 0x8d298057) {
        ScriptString_AssignCString(&info, pointer<const char>(0x00826f20));  // "mg30cal_bipod_prone"
        ScriptString_AssignCString(&model, pointer<const char>(0x00826f00));
    }
    ScriptString* const zone_name = title_function<GetString>(0x00458da0)(ent_script_object(ent), 0x142a3d9e);  // ScriptKeyTable_GetString
    const uint32_t zone = slot<uint32_t XB_CDECL(ScriptString*)>(0x00922494)(zone_name);  // GScr_FindZone
    float position[3];
    {
        float origin[3];
        mg_origin(ent, origin);
        position[0] = origin[0] + offset[0];
        position[1] = origin[1] + offset[1];
        position[2] = origin[2] + offset[2];
    }
    ScriptString classname;
    scr_string_from(&classname, 0x00839e8c);  // "misc_turret"
    using SpawnTurret = uint32_t XB_CDECL(ScriptString*, const float*, ScriptString*, const uint32_t*, uint32_t);
    const uint32_t turret = slot<SpawnTurret>(0x00922304)(&classname, position, &info, angles, zone);  // GScr_SpawnTurretWithAngles
    {
        uint32_t buffer[3];
        const uint32_t* const a = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922ba0)(buffer, ent);
        ScrVec3 v;
        v.v[0] = a[0];
        v.v[1] = a[1];
        v.v[2] = a[2];
        slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922ba4)(turret, v);  // ScrEnt_SetAngles
    }
    slot<SetWord>(0x00922584)(turret, 0xad76f051);  // GScr_Turret_SetMode(manual)
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(turret, &model, 0);  // GScr_SetModel
    if (slot<long double XB_CDECL(uint32_t)>(0x00922c58)(ent) != as_float(undefined_float()))  // ScrActor_GetFloat_d4
        slot<SetWord>(0x009225a4)(turret, bits(static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922c58)(ent))));  // GScr_Turret_Set54
    ScriptString weapon_class;
    ScrCall_GScr_GetWeaponClassname(&weapon_class, slot<uint32_t XB_CDECL(uint32_t)>(0x00922d30)(ent));
    using Call = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*, const ScriptString*);
    title_function<Call>(0x0017afa0)(0, 0, 0, 0, 0x001b8b50, turret, &ent, &weapon_class);  // Scr_CallThread_ent_ent_str(BroScr_TurretOwnerChangeSwap)
    slot<SetWord>(0x00922d34)(ent, slot<uint32_t XB_CDECL(uint32_t)>(0x00922d38)(ent));  // ScrActor_SetInt_f0(GetInt_f4)
    slot<SetWord>(0x0092272c)(ent, turret);  // GScr_ActorUseTurret
    scr_release(&weapon_class);
    scr_release(&classname);
    scr_release(&model);
    scr_release(&info);
}

// The MG42 cover spot (a spawner's thread).
void XB_CDECL BroScr_Mg42CoverSpotThink(uint32_t ent) {
    {
        bool defined;
        if (!*ScrField_ai_mode_IsDefined(&defined, ent)) *handle_slot(ent, 0x97c730fd) = 0x6c271e5a;
    }
    uint32_t node;
    {
        ScriptString key;
        ScriptString_FromCString(&key, pointer<const char>(0x0082dda8));  // "targetname"
        ScriptString target;
        using GetNode = uint32_t XB_CDECL(ScriptString*, ScriptString*, uint32_t, uint32_t);
        node = slot<GetNode>(0x009221cc)(ScrCall_ScrEnt_GetTarget(&ent, &target), &key, 0, 0);  // GScr_GetNode
        if (node == 0xffffffff) node = 0;
        scr_release(&target);
        scr_release(&key);
    }
    if (node == 0 || node == 0xffffffff) return;
    using GetEnt = uint32_t XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t);
    uint32_t turret;
    {
        ScriptString target;
        turret = slot<GetEnt>(0x009221c4)(ScrCall_ScrPathNode_GetTarget(&node, &target), 0x0d786b4c, 0, 0, 0);  // GScr_GetEntArray
        scr_release(&target);
    }
    {
        uint32_t out[3];
        const uint32_t* const origin = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922ab8)(out, node);  // ScrPathNode_GetOrigin
        const uint32_t x = origin[0];
        const uint32_t y = origin[1];
        const uint32_t z = origin[2];
        uint32_t* const org = reinterpret_cast<uint32_t*>(ScrField_org(turret));
        org[0] = x;
        org[1] = y;
        org[2] = z;
    }
    uint8_t flagged = 0;
    bool has_target;
    {
        ScriptString target;
        has_target = ScrCall_ScrEnt_GetTarget(&turret, &target)->rep != nullptr;
        scr_release(&target);
    }
    if (has_target) {
        {
            HashMap copy;
            HashMap_CopyCtor_6(&copy, reinterpret_cast<const HashMap*>(title_global<uint32_t>(0x00922ef8) + 0xfc));
            title_function<void XB_THISCALL(HashMap*)>(0x0045ec50)(&copy);  // HashMap_Dtor_PROLOGUE_2
        }
        using Index = uint8_t* XB_THISCALL(uint32_t, const uint32_t*);
        bool unset;
        {
            ScriptString target;
            uint32_t hash;
            const uint32_t* const key = Scr_HashScriptString(
                &hash, reinterpret_cast<const uint8_t* const*>(ScrCall_ScrEnt_GetTarget(&turret, &target)));
            unset = *title_function<Index>(0x004611e0)(title_global<uint32_t>(0x00922ef8) + 0xfc, key) ==
                    title_global<uint8_t>(0x00922ed0);
            scr_release(&target);
        }
        if (unset) {
            // The level flag of the turret's target, cleared; the trigger notify thread on its target.
            {
                ScriptString target;
                uint32_t hash;
                const uint32_t* const key = Scr_HashScriptString(
                    &hash, reinterpret_cast<const uint8_t* const*>(ScrCall_ScrEnt_GetTarget(&turret, &target)));
                *title_function<Index>(0x004611e0)(title_global<uint32_t>(0x00922ef8) + 0xfc, key) = 0;
                scr_release(&target);
            }
            uint32_t trigger;
            {
                ScriptString target;
                trigger = slot<GetEnt>(0x009221c4)(ScrCall_ScrEnt_GetTarget(&turret, &target), 0x0d786b4c, 0, 0, 0);
                scr_release(&target);
            }
            using Start = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
            title_function<Start>(0x0011a910)(0, 0, 0, 0, 0x001de980, trigger);  // Scr_StartThreadFunctor1(BroScr_TriggerNotifyLevelTargetname)
        }
        flagged = 1;
    }
    using Count = uint32_t XB_CDECL(uint32_t);
    using Create = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);
    if (!slot<Count>(0x00922b88)(ent)) return;
    do {
        // A living guy from the spawner (one try a second), the AI mode and manual target threads on him.
        uint32_t guy = 0;
        while (!entity_handle_is_valid(guy)) {
            guy = slot<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00922518)(ent, 0);  // GScr_DoSpawn
            Scr_Wait(1.0f);
        }
        const uint32_t* const mode = handle_slot(ent, 0x97c730fd);  // ai_mode
        uint32_t* functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x18);  // ScrIface_Alloc
        if (functor) {
            functor[0] = 0x00825bb4;
            functor[1] = 0x001e1cf0;  // BroScr_TurretSetAIMode
            functor[2] = guy;
            functor[3] = turret;
            *reinterpret_cast<uint8_t*>(&functor[4]) = flagged;
            functor[5] = *mode;
        }
        slot<Create>(0x0092216c)(0, 0, 0, guy, functor, 0);  // Scr_CreateThread
        functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);
        if (functor) {
            functor[0] = 0x007f4a28;
            functor[1] = 0x001a4f40;  // BroScr_TurretManualTargetThink
            functor[2] = guy;
            functor[3] = turret;
        }
        slot<Create>(0x0092216c)(0, 0, 0, guy, functor, 0);
        Scr_WaitTillString(guy, pointer<const char>(0x00831f60));  // "death"
        bool defined;
        if (*ScrField_script_delay_IsDefined(&defined, ent)) {
            Scr_Wait(as_float(*key_slot(ent, 0x807116c4, undefined_float())));  // script_delay
        } else if (*ScrField_script_delay_min_IsDefined(&defined, ent) &&
                   *ScrField_script_delay_max_IsDefined(&defined, ent)) {
            const float max = as_float(*key_slot(ent, 0x6c663f49, undefined_float()));
            const float min = as_float(*key_slot(ent, 0x6c663e5f, undefined_float()));
            const uint32_t* const base = key_slot(ent, 0x6c663e5f, undefined_float());
            const long double random = slot<long double XB_CDECL(float)>(0x00922230)(max - min);  // Scr_RandomFloat
            Scr_Wait(static_cast<float>(as_float(*base) + random));
        } else {
            Scr_Wait(1.0f);
        }
    } while (slot<Count>(0x00922b88)(ent));
}

namespace {
inline bool pending_slot(uint32_t ent) { return slot<uint32_t XB_CDECL(uint32_t)>(0x0092271c)(ent) != 0; }  // GScr_ActorHasPendingSentientSlot
// A debug message string (built and released) with the key read its assert makes.
inline void message_read(uint32_t ent, uint32_t text, uint32_t key) {
    ScriptString message;
    scr_string_from(&message, text);
    handle_slot(ent, key);
    scr_release(&message);
}
}  // namespace

void XB_CDECL Bro_cover_crouch__cover_crouch(uint32_t ent) {
    end_on(ent, 0x07b4e7f7);
    ScrVec3 offset;
    offset.v[0] = bits(0.0f);
    offset.v[1] = bits(0.0f);
    offset.v[2] = bits(0.0f);
    uint32_t cover[3];
    BroScr_GetCoverNodeOrOrigin(cover, ent);
    for (;;) {
        if (!pending_slot(ent)) {
            do {
                if (!(static_cast<int32_t>(*int_slot(ent, 0x0feddbea)) > 0)) break;  // no rounds left
                if (*int_slot(ent, 0xc786fa48) != 0) break;                          // rechamber pending
                set_orient_mode(ent, 0x0083adc0);  // "face enemy"
                message_read(ent, 0x00826a5c, 0x0ccee5c1);  // "... About to call aim, movement is bad"
                if (team_is_axis(ent))
                    BroScr_SetAimBlendTimes(title_global<uint32_t>(0x0083eb24), title_global<uint32_t>(0x00922ea4));  // 0.5
                else
                    BroScr_SetAimBlendTimes(title_global<uint32_t>(0x0083ebf8), title_global<uint32_t>(0x00922ea4));  // 0.1
                BroScr_PlayIdleAnim(ent);
                BroScr_KeepAimingAtTarget(ent, title_global<uint32_t>(0x009230ec), title_global<uint32_t>(0x0092379c));
                slot<KnobAllNoFlag>(0x009225c8)(ent, title_global<uint32_t>(0x008ce4d8), title_global<uint32_t>(0x008cd7dc),
                                                0x3f800000, 0x3e4ccccd, 0x3f800000, 0);  // GScr_SetAnimKnobAll(crouch_aim, Body)
                {
                    const uint32_t aim = title_global<uint32_t>(0x008ce4d8);
                    ScriptString name;
                    scr_string_from(&name, 0x00826a38);  // "Not suppressed and ready to fire"
                    BroScr_AimUntilCanShoot(ent, aim, aim, aim, title_global<uint32_t>(0x0083ec9c), name);  // 3
                }
                uint32_t can;
                if (*BroScr_CheckCanShoot(&can, ent, title_global<uint32_t>(0x00922ebc))) {
                    message_read(ent, 0x008269f0, 0x764918ae);  // "... About to call ShootVolley but not aiming"
                    ScriptString mode;
                    ScriptString_Copy(&mode, pointer<ScriptString>(0x00922e90));
                    uint8_t shot;
                    BroScr_CoverShootBehavior(&shot, ent, 0, mode, title_global<uint32_t>(0x00922e84));
                }
            } while (!pending_slot(ent));
        }
        bool volley_over;
        {
            ScriptString team;
            volley_over = ScriptString_Equals(ScrCall_ScrSentient_GetTeam(&ent, &team), pointer<const char>(0x00832084)) ||  // "allies"
                          pending_slot(ent) ||
                          title_global<float>(0x0083eb24) >
                              slot<long double XB_CDECL(uint32_t)>(0x00922230)(0x3f800000);  // 0.5 > Scr_RandomFloat(1)
            scr_release(&team);
        }
        uint32_t thrown;
        if (volley_over) {
            {
                ScriptString name;
                scr_string_from(&name, 0x008269d4);  // "CoverCrouch volley over"
                BroScr_CoverCrouchReloadAnim(ent, name);
            }
            slot<void XB_CDECL(uint32_t, uint32_t*, uint32_t, uint32_t)>(0x009226f8)(ent, cover, 0, 0);  // GScr_ActorTryTeleport
            BroScr_TryGrenadeThrow(&thrown, ent, title_global<uint32_t>(0x00922ea8), offset);
            BroScr_TryReload(ent, title_global<uint32_t>(0x0083eca0), title_global<uint32_t>(0x00922ea8));  // 0.2
            BroScr_ReloadLauncher(ent);
            while (pending_slot(ent)) {
                {
                    ScriptString type;
                    scr_string_from(&type, 0x008269a8);  // "undersuppression"
                    BroScr_ChooseFlankOrCoverMe(ent, type);
                }
                Bro_cover_crouch__idle(ent);
                BroScr_TryGrenadeThrow(&thrown, ent, title_global<uint32_t>(0x00922ea8), offset);
            }
            set_orient_mode(ent, 0x0083adc0);  // "face enemy"
            BroScr_PlayCrouchIdleAnim(ent);
        } else {
            BroScr_TryGrenadeThrow(&thrown, ent, title_global<uint32_t>(0x00922ea8), offset);
            BroScr_TryReload(ent, title_global<uint32_t>(0x0083eca0), title_global<uint32_t>(0x00922ea8));
            BroScr_ReloadLauncher(ent);
        }
    }
}

namespace {
// GScr_SetFlaggedAnimKnob(ent, the melee notify 0xcc615b7e, anim, 1, blend, 1, 0).
inline void melee_knob(uint32_t ent, uint32_t anim, uint32_t blend) {
    slot<SetAnim7>(0x009225e0)(ent, 0xcc615b7e, title_global<uint32_t>(anim), 0x3f800000, blend, 0x3f800000, 0);
}
inline void message_only(uint32_t text) {
    ScriptString message;
    scr_string_from(&message, text);
    scr_release(&message);
}
}  // namespace

void XB_CDECL Bro_combat__MeleeCombat(uint32_t ent, ScriptString reason) {
    const uint32_t kStand = 0x070b89a4;
    const uint32_t kLeft = 0x003798b4;
    const uint32_t kRight = 0x07151174;
    end_on(ent, 0x07b4e7f7);
    slot<long double XB_CDECL(uint32_t)>(0x00922690)(ent);  // GScr_SentientNearestEnemyDist (unused)
    message_only(0x00826dc8);  // "combat::MeleeCombat - not in range!"
    {
        ScriptString message;
        scr_string_from(&message, 0x00826da0);  // "Can't melee - I don't have an enemy."
        is_alive(enemy_of(ent));                // the assert's read
        scr_release(&message);
    }
    message_only(0x00826c88);  // "Script state called without reason."
    uint8_t linked = 0;
    do {
        uint8_t charged;
        if (!*BroScr_MeleeCharge(&charged, ent)) break;
        uint8_t relinked;
        if (*BroScr_MeleePair_Relink(&relinked, ent, enemy_of(ent))) {
            BroScr_MeleePair_Link(ent, enemy_of(ent));
            linked = 1;
        } else {
            linked = 0;
        }
        begin_entity_thread(0x001bfe70, ent);  // BroScr_LookAtEnemyLoop
        if (*handle_slot(ent, 0x0ccee5c1) == 0x0001c6ce && get_int_184(ent) == kStand)
            BroScr_MeleeAttackAnim(ent);
        else
            title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, kStand, 0x00385d90);  // BroScr_SetPoseMovement
        message_read(ent, 0x00826d90, 0x0ccee5c1);  // "should be stop"
        if (!is_alive(enemy_of(ent))) break;
        {
            ScriptString type;
            scr_string_from(&type, 0x008267c0);  // "meleeattack"
            BroScr_BattleChatter(ent, type);
        }
        slot<KnobAllNoFlag>(0x009225c8)(ent, title_global<uint32_t>(0x008ce9b8), title_global<uint32_t>(0x008cd7dc),
                                        0x3f800000, 0x3d4ccccd, 0x3f800000, 0);  // GScr_SetAnimKnobAll(MELEE, Body)
        uint32_t state = *handle_slot(ent, 0x2afa5406);
        uint32_t stance;
        if (enemy_of(ent) == title_global<uint32_t>(0x009236dc)) {  // the player: his stance
            ScriptString name;
            ScriptString_FromRep(&name, nullptr);
            slot<void XB_CDECL(uint32_t, ScriptString*)>(0x00922600)(title_global<uint32_t>(0x009236dc), &name);  // GScr_GetStance
            uint32_t hash;
            stance = *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&name));
            scr_release(&name);
        } else {
            stance = get_int_184(enemy_of(ent));
        }
        switch (*handle_slot(ent, 0x2afa5406)) {
        case 0x0001894d:  // the first swing
            if (stance == kStand) {
                melee_knob(ent, 0x008cd77c, 0x3e19999a);  // aim2melee_right
                state = kRight;
            } else {
                melee_knob(ent, 0x008cd788, 0x3e19999a);  // aim2meleecrouch_left
                state = kLeft;
            }
            break;
        case kLeft: {
            const int32_t r = random_int(100);
            if (stance == kStand) {
                if (r < 30) {
                    melee_knob(ent, 0x008ce9dc, 0x3dcccccd);
                } else {
                    const uint32_t notify = 0xcc615b7e;
                    if (r < 60) {
                        ScrCall_SetFlaggedAnimKnobByRef(&ent, &notify, title_global<uint32_t>(0x008ce9e8), 0x3f800000, 0x3dcccccd,
                                                        0x3f800000);
                    } else {
                        ScrCall_SetFlaggedAnimKnobByRef(&ent, &notify, title_global<uint32_t>(0x008ce9f4), 0x3f800000, 0x3dcccccd,
                                                        0x3f800000);  // melee_left2right
                        state = kRight;
                    }
                }
            } else if (r < 40) {
                melee_knob(ent, 0x008cea24, 0x3dcccccd);  // meleecrouch_left2left
            } else {
                melee_knob(ent, 0x008cea30, 0x3dcccccd);  // meleecrouch_left2right
                state = kRight;
            }
            break;
        }
        case kRight: {
            const int32_t r = random_int(100);
            if (stance == kStand) {
                if (r < 30) {
                    melee_knob(ent, 0x008cea0c, 0x3dcccccd);  // melee_right2right_1
                } else if (r < 60) {
                    melee_knob(ent, 0x008cea18, 0x3dcccccd);  // melee_right2right_2
                } else {
                    melee_knob(ent, 0x008cea00, 0x3dcccccd);  // melee_right2left
                    state = kLeft;
                }
            } else if (r < 40) {
                melee_knob(ent, 0x008cea48, 0x3dcccccd);  // meleecrouch_right2right
            } else {
                melee_knob(ent, 0x008cea3c, 0x3dcccccd);  // meleecrouch_right2left
                state = kLeft;
            }
            break;
        }
        case 0xc0b14485:  // after the charge
            melee_knob(ent, 0x008ce9d0, 0x3dcccccd);  // melee_charge2right
            state = kRight;
            break;
        default:
            message_only(0x00826d54);  // "homemade_error = Unexpected_meleeState_value + meleeState"
            break;
        }
        handle_note_tracks(ent, 0xcc615b7e, 0x0018da00);  // BroScr_MeleeNoteTrackHandler
        *handle_slot(ent, 0x2afa5406) = state;
    } while (is_alive(enemy_of(ent)));
    if (linked) BroScr_MeleePair_Unlink(ent, enemy_of(ent));
    begin_entity_thread(0x001dce30, ent);  // BroScr_CombatMain
    queue_notify(ent, 0x807e5a80);
    scr_release(&reason);
}

namespace {
inline bool stance_allowed_now(uint32_t ent, uint32_t pose) {
    return static_cast<uint8_t>(slot<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00922714)(ent, pose)) != 0;  // GScr_ActorIsStanceAllowed
}
// BroScr_CanShootFromPose(ent, pose, no offset, `change`: 1 unless already in the pose).
inline bool can_shoot_from(uint32_t ent, uint32_t pose, bool already) {
    ScrVec3 none;
    none.v[0] = title_global<uint32_t>(0x00922ec4);
    none.v[1] = title_global<uint32_t>(0x00922ec8);
    none.v[2] = title_global<uint32_t>(0x00922ecc);
    uint8_t can;
    return *BroScr_CanShootFromPose(&can, ent, pose, none, already ? 0 : 1) != 0;
}
// Aims in `pose` (both blend times from 0x00922ea4).
inline void ranged_aim(uint32_t ent, uint32_t pose) {
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, pose, 0x00385d90);  // BroScr_SetPoseMovement
    BroScr_SetAimBlendTimes(title_global<uint32_t>(0x00922ea4), title_global<uint32_t>(0x00922ea4));
    BroScr_PlayIdleAnim(ent);
    BroScr_KeepAimingAtTarget(ent, title_global<uint32_t>(0x009230ec), title_global<uint32_t>(0x0092379c));
}
// BroScr_CoverShootBehavior(0, the empty string, 0x00922e84), then state slot 5.
inline void ranged_cover_shoot(uint32_t ent) {
    ScriptString mode;
    ScriptString_Copy(&mode, pointer<ScriptString>(0x00922e90));
    uint8_t shot;
    BroScr_CoverShootBehavior(&shot, ent, 0, mode, title_global<uint32_t>(0x00922e84));
    slot<void XB_CDECL(uint32_t)>(0x009226a4)(ent);  // Actor_CallStateSlot5
}
}  // namespace

void XB_CDECL Bro_combat__RangedCombat(uint32_t ent, ScriptString reason) {
    const uint32_t kStand = 0x070b89a4;
    const uint32_t kCrouch = 0xc1165808;
    end_on(ent, 0x07b4e7f7);
    message_only(0x00826c88);  // "Script state called without reason."
    set_orient_mode(ent, 0x0083adc0);  // "face enemy"
    for (;;) {
        title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, 0, 0x00385d90);  // BroScr_SetPoseMovement
        BroScr_TryReload(ent, bits(0.0f), title_global<uint32_t>(0x00922ea8));
        float dist = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922690)(ent));  // GScr_SentientNearestEnemyDist
        const bool standing = get_int_184(ent) == kStand;
        const bool crouching = get_int_184(ent) == kCrouch;
        const float range = at<float>(anim_object(), 0x198);
        if (dist == title_global<float>(0x00922eac)) {  // no enemy: just past the range for the current pose
            if (standing)
                dist = range - title_global<float>(0x0083eb20);
            else
                dist = at<float>(anim_object(), 0x198) + title_global<float>(0x0083eb20);  // 1
        }
        int pose = 0;  // 1 stand, 2 crouch, 0 neither
        const int32_t near = at<float>(anim_object(), 0x198) > dist ? 1 : 0;
        if (static_cast<float>(near) != title_global<float>(0x0083eafc)) {
            if (stance_allowed_now(ent, kStand) && can_shoot_from(ent, kStand, standing)) {
                pose = 1;
            } else {
                const bool crouch_ok = stance_allowed_now(ent, kCrouch) && can_shoot_from(ent, kCrouch, crouching);
                if (crouch_ok) {
                    pose = 2;
                } else {
                    const int32_t close = title_global<float>(0x00826e40) > dist ? 1 : 0;  // 2304
                    if (static_cast<float>(close) != title_global<float>(0x0083eafc)) {
                        const long double sees = slot<long double XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x009226d8)(
                            ent, enemy_of(ent), 0);  // GScr_ActorCanSee
                        if (sees != title_global<float>(0x0083eafc)) pose = 1;
                    }
                }
            }
        } else if (stance_allowed_now(ent, kCrouch) && can_shoot_from(ent, kCrouch, crouching)) {
            pose = 2;
        } else if (stance_allowed_now(ent, kStand) && can_shoot_from(ent, kStand, standing)) {
            pose = 1;
        }
        if (pose == 2) {
            ranged_aim(ent, kCrouch);
            ranged_cover_shoot(ent);
        } else if (pose == 1) {
            ranged_aim(ent, kStand);
            // Sometimes a side step first: more likely the nearer the enemy (not allies).
            const float chance = (at<float>(anim_object(), 0x68) - dist) /
                                 (at<float>(anim_object(), 0x68) - at<float>(anim_object(), 0x9c));
            bool step;
            {
                ScriptString team;
                step = !ScriptString_Equals(ScrCall_ScrSentient_GetTeam(&ent, &team), pointer<const char>(0x00832084)) &&  // "allies"
                       static_cast<float>(chance > slot<long double XB_CDECL(uint32_t)>(0x00922230)(0x3f800000) ? 1 : 0) !=
                           title_global<float>(0x0083eafc);  // > Scr_RandomFloat(1)
                scr_release(&team);
            }
            if (step) {
                uint8_t stepped;
                BroScr_StandShootSideStep(&stepped, ent);
            }
            ranged_cover_shoot(ent);
        } else {
            // The original tests the whole word its byte result sits in (stale upper bytes: 0 here).
            uint32_t stepped = 0;
            if (!*reinterpret_cast<uint32_t*>(BroScr_StandShootSideStep(reinterpret_cast<uint8_t*>(&stepped), ent))) {
                title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, 0, 0x00385d90);
                message_read(ent, 0x00826e04, 0x0ccee5c1);  // "combat::RangedCombat: About to call aim, movement is bad"
                BroScr_SetAimBlendTimes(title_global<uint32_t>(0x0083ebf8), title_global<uint32_t>(0x00922ea4));  // 0.1
                BroScr_PlayIdleAnim(ent);
                BroScr_KeepAimingAtTarget(ent, title_global<uint32_t>(0x009230ec), title_global<uint32_t>(0x0092379c));
            }
        }
        {
            float enemy_dist = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922690)(ent));
            if (enemy_dist == title_global<float>(0x00922eac)) enemy_dist = title_global<float>(0x0083ebec);  // 500
            *key_slot(ent, 0xa3b8e90e, undefined_float()) = bits(enemy_dist);
        }
        uint32_t angles_at[3];
        const uint32_t yaw = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922ba0)(angles_at, ent)[1];  // ScrEnt_GetAngles
        uint32_t origin_at[3];
        const uint32_t* const origin = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(origin_at, ent);  // ScrEnt_GetOrigin
        ScrVec3 point;
        point.v[0] = origin[0];
        point.v[1] = origin[1];
        point.v[2] = origin[2];
        uint32_t prone;
        if (*BroScr_ShouldGoProne_2(&prone, ent, point, yaw)) {
            call_thread_str(ent, 0x001dcd30, 0x00826dec);  // BroScr_AnimScript_Combat, "enemydist > pronerange"
            break;
        }
        uint8_t charged;
        if (*BroScr_MeleeCharge(&charged, ent)) {
            call_thread_str(ent, 0x001dd8d0, 0x00826cc0);  // Bro_combat__MeleeCombat, "TryMeleeCharge passed"
            break;
        }
        ScrVec3 none;
        none.v[0] = title_global<uint32_t>(0x00922ec4);
        none.v[1] = title_global<uint32_t>(0x00922ec8);
        none.v[2] = title_global<uint32_t>(0x00922ecc);
        uint32_t thrown;
        BroScr_TryGrenadeThrow(&thrown, ent, title_global<uint32_t>(0x00922ea8), none);
        slot<void XB_CDECL(uint32_t)>(0x009226a4)(ent);  // Actor_CallStateSlot5
    }
    scr_release(&reason);
}

namespace {
// BroScr_SetAimBlendTimes(first, 0x00922ea4), the idle, keep aiming at the target.
inline void aim_idle(uint32_t ent, uint32_t first) {
    BroScr_SetAimBlendTimes(first, title_global<uint32_t>(0x00922ea4));
    BroScr_PlayIdleAnim(ent);
    BroScr_KeepAimingAtTarget(ent, title_global<uint32_t>(0x009230ec), title_global<uint32_t>(0x0092379c));
}
// BroScr_CoverShootBehavior(0, the empty string, 0x00922e84) into `out`.
inline uint8_t* cover_shoot(uint8_t* out, uint32_t ent) {
    ScriptString mode;
    ScriptString_Copy(&mode, pointer<ScriptString>(0x00922e90));
    return BroScr_CoverShootBehavior(out, ent, 0, mode, title_global<uint32_t>(0x00922e84));
}
}  // namespace

void XB_CDECL Bro_cover_stand__cover_stand(uint32_t ent) {
    end_on(ent, 0x07b4e7f7);
    ScrVec3 offset;
    offset.v[0] = bits(0.0f);
    offset.v[1] = bits(0.0f);
    offset.v[2] = bits(0.0f);
    ScrVec3 cover;
    BroScr_GetCoverNodeOrOrigin(cover.v, ent);
    ScrVec3 forward;
    title_function<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00184390)(forward.v, ent);  // BroScr_GetCoverNodeForward
    const uint32_t first_blend = title_global<uint32_t>(0x0083eba8);  // 2
    for (;;) {
        aim_idle(ent, first_blend);
        if (!pending_slot(ent)) {
            do {
                if (!(static_cast<int32_t>(*int_slot(ent, 0x0feddbea)) > 0)) break;  // no rounds left
                if (*int_slot(ent, 0xc786fa48) != 0) break;                          // rechamber pending
                set_orient_mode(ent, 0x0083adc0);  // "face enemy"
                if (team_is_axis(ent))
                    BroScr_SetAimBlendTimes(title_global<uint32_t>(0x0083eb24), title_global<uint32_t>(0x00922ea4));  // 0.5
                else
                    BroScr_SetAimBlendTimes(title_global<uint32_t>(0x0083ebf8), title_global<uint32_t>(0x00922ea4));  // 0.1
                BroScr_PlayIdleAnim(ent);
                BroScr_KeepAimingAtTarget(ent, title_global<uint32_t>(0x009230ec), title_global<uint32_t>(0x0092379c));
                slot<KnobAllNoFlag>(0x009225c8)(ent, title_global<uint32_t>(0x008cf354), title_global<uint32_t>(0x008cd7dc),
                                                0x3f800000, 0x3e4ccccd, 0x3f800000, 0);  // GScr_SetAnimKnobAll(stand_aim, Body)
                {
                    const uint32_t aim = title_global<uint32_t>(0x008cf354);
                    ScriptString name;
                    scr_string_from(&name, 0x00826a38);  // "Not suppressed and ready to fire"
                    BroScr_AimUntilCanShoot(ent, aim, aim, aim, title_global<uint32_t>(0x0083ec9c), name);  // 3
                }
                uint32_t can;
                if (*BroScr_CheckCanShoot(&can, ent, title_global<uint32_t>(0x00922ebc))) {
                    // The original tests the whole word its byte result sits in (stale upper bytes: 0 here).
                    uint32_t shot = 0;
                    cover_shoot(reinterpret_cast<uint8_t*>(&shot), ent);
                    ++at<int32_t>(anim_object(), 0x190);
                    if (!shot) aim_idle(ent, title_global<uint32_t>(0x0083ebf8));  // 0.1
                    if (at<int32_t>(anim_object(), 0x190) > 10) {  // every tenth volley another
                        at<int32_t>(anim_object(), 0x190) = 0;
                        uint8_t again;
                        cover_shoot(&again, ent);
                    }
                    uint32_t thrown;
                    BroScr_TryGrenadeThrow(&thrown, ent, title_global<uint32_t>(0x00922ea8), offset);
                } else {
                    // A machine gunner (fire type 0x315aef) that sees the enemy node: suppression (70%).
                    uint32_t type;
                    if (*BroScr_GetWeaponFireType(&type, ent) == 0x00315aef && random_int(100) < 70 &&
                        slot<uint32_t XB_CDECL(uint32_t)>(0x00922780)(ent)) {  // GScr_ActorNodeCanSeeEnemyNode
                        message_read(ent, 0x00826c00, 0x764918ae);  // "... About to call ShootVolley but not aiming"
                        while (static_cast<int32_t>(*int_slot(ent, 0x0feddbea)) > 0) {
                            {
                                ScriptString type_name;
                                scr_string_from(&type_name, 0x00826bec);  // "doingsuppression"
                                BroScr_ChooseFlankOrCoverMe(ent, type_name);
                            }
                            const long double random = slot<long double XB_CDECL(uint32_t)>(0x00922230)(0x3f333333);  // Scr_RandomFloat(0.7)
                            aim_idle(ent, bits(static_cast<float>(random + title_global<float>(0x0083eca0))));  // + 0.2
                            uint8_t shot;
                            cover_shoot(&shot, ent);
                            uint32_t thrown;
                            BroScr_TryGrenadeThrow(&thrown, ent, title_global<uint32_t>(0x00922ea8), offset);
                        }
                    }
                }
            } while (!pending_slot(ent));
        }
        bool grenade;
        {
            ScriptString team;
            grenade = ScriptString_Equals(ScrCall_ScrSentient_GetTeam(&ent, &team), pointer<const char>(0x0083208c)) &&  // "axis"
                      !pending_slot(ent) &&
                      slot<long double XB_CDECL(uint32_t)>(0x00922230)(0x3f800000) > title_global<float>(0x0083eb24);  // > 0.5
            scr_release(&team);
        }
        if (grenade) {
            uint32_t thrown;
            BroScr_TryGrenadeThrow(&thrown, ent, title_global<uint32_t>(0x00922ea8), offset);
            if (!pending_slot(ent)) {
                BroScr_TryReload(ent, title_global<uint32_t>(0x0083eca0), title_global<uint32_t>(0x00922ea8));  // 0.2
                BroScr_ReloadLauncher(ent);
            }
        } else {
            ScriptString text;
            scr_string_from(&text, 0x00826bd4);  // "Bottom of CoverStand"
            BroScr_CoverStandIdle(ent, cover, forward, text);
        }
    }
}

namespace {
using ArrayGrow = void XB_THISCALL(DynArray*, uint32_t, uint32_t);
// Element i of a script array, the array grown to i + 1 (capacity i + 4) first by `grow`:
// DynArray_Resize (0x00015a90), DynArray_ReserveInt0 (0x0045d480) or DynArray_ResizeInt (0x0017b9a0).
inline uint32_t* fb_at(DynArray* array, uint32_t i, uint32_t grow) {
    if (i >= array->size) title_function<ArrayGrow>(grow)(array, i + 1, i + 4);
    return &array->data[i];
}
inline uint32_t* fb_level_at(uint32_t offset, uint32_t n) {
    return fb_at(pointer<DynArray>(title_global<uint32_t>(0x00922ef8) + offset), n, 0x0017b9a0);
}
// Whether the entity's table holds `key` with a value other than the undefined int (nothing added).
inline bool fb_has_int_key(uint32_t ent, uint32_t key) {
    if (!entity_handle_is_valid(ent)) return false;
    ScriptKeyTable* const table = title_function<ScriptKeyTable* XB_CDECL(uint32_t)>(0x0011db20)(ent);
    if (!table) return false;
    const uint32_t* const value = ScriptKeyTable_Find(table, key);
    return (value ? *value : undefined_int()) != undefined_int();
}
// A 0x14-byte {vtable, function, a, b, c} thread functor on `ent` (Scr_CreateThread).
inline void fb_thread(uint32_t vtable, uint32_t function, uint32_t ent, uint32_t b, const uint32_t* c) {
    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x14);  // ScrIface_Alloc
    if (functor) {
        functor[0] = vtable;
        functor[1] = function;
        functor[2] = ent;
        functor[3] = b;
        functor[4] = *c;
    }
    slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(
        0, 0, 0, ent, functor, 0);  // Scr_CreateThread
}
}  // namespace

void XB_CDECL BroScr_FallbackThink(uint32_t level, int32_t n, uint32_t group) {
    const uint32_t kFallback = 0x8bbe4822;  // script_fallback
    DynArray nodes = {nullptr, 0, 0};
    DynArray found = {nullptr, 0, 0};
    Scr_GetNodeArrayNoKey(reinterpret_cast<uint32_t*>(&nodes));
    // The fallback's nodes: script_fallback n.
    if (static_cast<int32_t>(nodes.size) > 0) {
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(nodes.size); ++i) {
            const uint32_t node = *fb_at(&nodes, i, 0x0045d480);
            using GetIntKey = uint32_t XB_CDECL(uint32_t, uint32_t);
            const bool keyed = node != 0 && node != 0xffffffff &&
                               slot<GetIntKey>(0x00922960)(node, kFallback) != undefined_int();  // ScrPathNode_GetIntKey
            if (keyed && slot<GetIntKey>(0x00922960)(*fb_at(&nodes, i, 0x0045d480), kFallback) == static_cast<uint32_t>(n))
                ScrPtrArray_Append(reinterpret_cast<uint32_t*>(&found), fb_at(&nodes, i, 0x0045d480));
        }
    }
    if (static_cast<int32_t>(nodes.size) > 0 && found.capacity) {
        *fb_level_at(0x6c, n) = 0;
        *fb_level_at(0x0c, n) = 0;
        *fb_level_at(0xe4, n) = 0;
        // Spawners of the fallback with a count: a spawner thread to a random node, counted at level +0xc.
        DynArray spawners = {nullptr, 0, 0};
        slot<void XB_CDECL(DynArray*)>(0x009223d8)(&spawners);  // GScr_GetSpawnerArray
        if (static_cast<int32_t>(spawners.size) > 0) {
            for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(spawners.size); ++i) {
                if (!fb_has_int_key(*fb_at(&spawners, i, 0x00015a90), kFallback)) continue;
                if (*int_slot(*fb_at(&spawners, i, 0x00015a90), kFallback) != static_cast<uint32_t>(n)) continue;
                if (!(static_cast<int32_t>(slot<uint32_t XB_CDECL(uint32_t)>(0x00922b88)(*fb_at(&spawners, i, 0x00015a90))) > 0))
                    continue;
                const uint32_t pick = static_cast<uint32_t>(random_int(static_cast<int32_t>(found.size)));
                const uint32_t* const node = fb_at(&found, pick, 0x0045d480);
                fb_thread(0x0080e554, 0x001c5910, *fb_at(&spawners, i, 0x00015a90), n, node);  // BroScr_FallbackSpawnerThink
                ++*fb_level_at(0x0c, n);
            }
        }
        // The AI of the fallback join it.
        DynArray ai = {nullptr, 0, 0};
        slot<void XB_CDECL(DynArray*)>(0x009223d4)(&ai);  // GScr_GetAIAll
        if (static_cast<int32_t>(ai.size) > 0) {
            for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ai.size); ++i) {
                if (!fb_has_int_key(*fb_at(&ai, i, 0x00015a90), kFallback)) continue;
                if (*int_slot(*fb_at(&ai, i, 0x00015a90), kFallback) != static_cast<uint32_t>(n)) continue;
                using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const int32_t*,
                                                      const uint32_t*, const ScriptString*);
                title_function<StartThread>(0x001e4b70)(0, 0, 0, 0, 0x001bea50, *fb_at(&ai, i, 0x00015a90), &n,
                                                        pointer<uint32_t>(0x00922ea0), pointer<ScriptString>(0x00922e90));
                // Scr_StartThread_ent_i_pathnode_str_3(BroScr_FallbackerJoinFallback)
            }
        }
        if (*fb_level_at(0x6c, n) == 0 && *fb_level_at(0x0c, n) == 0) {  // nobody in it
            if (ai.data) Scr_Free(ai.data);
            if (spawners.data) Scr_Free(spawners.data);
            if (found.data) Scr_Free(found.data);
            if (nodes.data) Scr_Free(nodes.data);
            return;
        }
        title_function<ArrayGrow>(0x00015a90)(&spawners, 0, 0);  // DynArray_Resize
        title_function<ArrayGrow>(0x00015a90)(&ai, 0, 0);
        fb_thread(0x007ffb9c, 0x001bec70, level, n, &group);  // BroScr_FallbackerSetup
        // Its trigger, then the fallback starts.
        ScriptString name;
        ScriptString_FromCString(&name, pointer<const char>(0x00826458));  // "fallbacker_trigger"
        ScriptString_AppendInt(&name, n);
        {
            const uint32_t* const owner = pointer<uint32_t>(title_global<uint32_t>(0x00922ef8) ? title_global<uint32_t>(0x00922ef8) + 4 : 0);
            uint32_t hash;
            const uint32_t notify = *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&name));
            Scr_WaitTill(*owner, notify);
        }
        {
            ScriptString value;
            ScriptString_Equals(ScrCall_GScr_GetCvar(&value, 0x00823550), pointer<const char>(0x0082e058));  // "fallback", "1" (unused)
            scr_release(&value);
        }
        *fb_level_at(0xe4, n) = 1;
        slot<void XB_CDECL(DynArray*)>(0x009223d4)(&ai);  // GScr_GetAIAll
        DynArray chosen = {nullptr, 0, 0};
        if (static_cast<int32_t>(ai.size) > 0) {
            for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ai.size); ++i) {
                bool join = fb_has_int_key(*fb_at(&ai, i, 0x00015a90), kFallback) &&
                            *int_slot(*fb_at(&ai, i, 0x00015a90), kFallback) == static_cast<uint32_t>(n);
                if (!join)
                    join = fb_has_int_key(*fb_at(&ai, i, 0x00015a90), 0x1aa720d8) && group != undefined_int() &&
                           *int_slot(*fb_at(&ai, i, 0x00015a90), 0x1aa720d8) == group;
                if (join) DynArray_PushBack(reinterpret_cast<uint8_t*>(&chosen), fb_at(&ai, i, 0x00015a90));
            }
        }
        title_function<ArrayGrow>(0x00015a90)(&ai, 0, 0);
        if (chosen.capacity) {
            // 40% of them go first; the fallback waits for each to reach its goal, then the rest go.
            const float part = static_cast<float>(static_cast<int32_t>(chosen.size)) * title_global<float>(0x0083ec44);  // 0.4
            {
                ScriptString_AssignCString(&name, pointer<const char>(0x00826914));  // "fallback initiated "
                ScriptString_AppendInt(&name, n);
                const uint32_t* const owner =
                    pointer<uint32_t>(title_global<uint32_t>(0x00922ef8) ? title_global<uint32_t>(0x00922ef8) + 4 : 0);
                uint32_t hash;
                queue_notify(*owner, *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&name)));
            }
            {
                DynArray copy = {nullptr, 0, 0};
                title_function<void XB_THISCALL(DynArray*, const DynArray*)>(0x0045c270)(&copy, &chosen);  // DynArray_Assign4_PROLOGUE
                BroScr_FallbackSelectActors(copy, 0, static_cast<int32_t>(part));
            }
            for (int32_t i = 0;; ++i) {
                const int32_t more = part > static_cast<float>(i) ? 1 : 0;
                if (static_cast<float>(more) == title_global<float>(0x0083eafc)) break;
                const uint32_t pick = static_cast<uint32_t>(random_int(static_cast<int32_t>(found.size)));
                const uint32_t* const node = fb_at(&found, pick, 0x0045d480);
                fb_thread(0x0080e554, 0x0019a5c0, *fb_at(&chosen, static_cast<uint32_t>(i), 0x00015a90), n, node);  // BroScr_FallbackGoalThink
            }
            for (int32_t i = 0;; ++i) {
                const int32_t more = part > static_cast<float>(i) ? 1 : 0;
                if (static_cast<float>(more) == title_global<float>(0x0083eafc)) break;
                ScriptString_AssignCString(&name, pointer<const char>(0x00825ac4));  // "fallback_reached_goal"
                ScriptString_AppendInt(&name, n);
                const uint32_t* const owner =
                    pointer<uint32_t>(title_global<uint32_t>(0x00922ef8) ? title_global<uint32_t>(0x00922ef8) + 4 : 0);
                uint32_t hash;
                const uint32_t notify = *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&name));
                Scr_WaitTill(*owner, notify);
            }
            for (int32_t i = static_cast<int32_t>(float_to_script_int(part)); i < static_cast<int32_t>(chosen.size); ++i) {
                if (!slot<uint32_t XB_CDECL(uint32_t)>(0x00922210)(*fb_at(&chosen, static_cast<uint32_t>(i), 0x00015a90))) continue;  // GScr_IsAlive
                const uint32_t pick = static_cast<uint32_t>(random_int(static_cast<int32_t>(found.size)));
                const uint32_t* const node = fb_at(&found, pick, 0x0045d480);
                fb_thread(0x0080e554, 0x0019a5c0, *fb_at(&chosen, static_cast<uint32_t>(i), 0x00015a90), n, node);
            }
        }
        if (chosen.data) Scr_Free(chosen.data);
        scr_release(&name);
        if (ai.data) Scr_Free(ai.data);
        if (spawners.data) Scr_Free(spawners.data);
    }
    if (found.data) Scr_Free(found.data);
    if (nodes.data) Scr_Free(nodes.data);
}

}  // namespace game
