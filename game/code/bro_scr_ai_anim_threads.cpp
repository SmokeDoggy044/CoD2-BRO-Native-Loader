#include "bro_scr_ai_anim_threads.h"

#include "bro_scr_ai_anim.h"
#include "bro_scr_ai_common.h"
#include "bro_scr_ai_look.h"
#include "bro_scr_common.h"
#include "g_scr_runtime.h"
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
using StandCombatRun = void XB_CDECL(uint32_t);
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
    *handle_slot(ent, 0x8cb5047a) = 0xc1165808;
    BroScr_ApplyDesiredAnimPose(ent);
    *handle_slot(ent, 0x8cb5047a) = 0x0738b1a5;
    *handle_slot(ent, 0x0ccee5c1) = 0x00385d90;
    uint32_t turret = slot<uint32_t XB_CDECL(uint32_t)>(0x0092274c)(ent);  // GScr_ActorGetTurret
    if (!entity_handle_is_valid(turret)) return;
    title_function<CallEntEnt>(0x004b09e0)(0, 0, 0, 0, 0x0019cfc0, turret, &ent);  // Scr_CallThread_ent_ent_ORAN(BroScr_Mg30calAnimTree)
    title_function<CallEntEnt>(0x004b09e0)(0, 0, 0, 0, 0x001b6a50, ent, &turret);  // (BroScr_AnimScript_TurretCall_1)
}

void XB_CDECL BroScr_SetDesiredPoseStandStop(uint32_t ent) {
    *handle_slot(ent, 0x8cb5047a) = 0x070b89a4;
    BroScr_ApplyDesiredAnimPose(ent);
    *handle_slot(ent, 0x0ccee5c1) = 0x00385d90;
    uint32_t turret = slot<uint32_t XB_CDECL(uint32_t)>(0x0092274c)(ent);  // GScr_ActorGetTurret
    if (!entity_handle_is_valid(turret)) return;
    title_function<CallEntEnt>(0x004b09e0)(0, 0, 0, 0, 0x0019d180, turret, &ent);  // Scr_CallThread_ent_ent_ORAN(BroScr_Mg30calAnimTree_2)
    title_function<CallEntEnt>(0x004b09e0)(0, 0, 0, 0, 0x001b6a80, ent, &turret);  // (BroScr_AnimScript_TurretCall_2)
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
    using RemoveKey = void XB_THISCALL(const uint32_t*, uint32_t);
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

}  // namespace game
