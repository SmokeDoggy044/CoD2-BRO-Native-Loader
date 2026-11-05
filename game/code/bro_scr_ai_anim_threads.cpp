#include "bro_scr_ai_anim_threads.h"

#include <cstring>

#include "bro_scr_actor.h"
#include "bro_scr_ai_anim.h"
#include "bro_scr_ai_common.h"
#include "bro_scr_ai_look.h"
#include "bro_scr_ai_weapons.h"
#include "bro_scr_nodes_spawner.h"
#include "bro_scr_common.h"
#include "bro_scr_cover_turret.h"
#include "g_scr_runtime.h"
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
// A recorded anim command (13 words, the array under key 0x1791f035), passed by value.
struct AnimCommand {
    uint32_t w[13];
};
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

uint8_t* XB_CDECL BroScr_AnimMoveUntilEndNote(uint8_t* out, uint32_t ent, uint32_t forward, uint32_t notify) {
    ScriptString note;
    ScriptString_FromRep(&note, nullptr);
    for (;;) {
        uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
        if (functor) {
            functor[0] = 0x007f4b00;
            functor[1] = 0x001acae0;  // BroScr_ForwardNotifyString
            functor[2] = ent;
            functor[3] = forward;
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

}  // namespace game

