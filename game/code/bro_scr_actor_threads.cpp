#include "bro_scr_actor_threads.h"

#include "bro_scr_ai_anim.h"
#include "bro_scr_ai_anim_threads.h"
#include "bro_scr_ai_common.h"
#include "bro_scr_common.h"
#include "bro_scr_entity_threads.h"
#include "bro_scr_ai_weapons.h"
#include "bro_scr_nodes_spawner.h"
#include "g_scr_runtime.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "scr_access.h"
#include "script_string.h"
#include "title_data.h"

namespace game {

using namespace bro_ai;

namespace {

constexpr uint32_t kDeath = 0x06730ae8;
constexpr uint32_t kKillAnimScript = 0x07b4e7f7;
constexpr uint32_t kLookId = 0x747cde6c;

using HandleNoteTracks = ScriptString* XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, ScriptString);
using Knob = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using CallThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*);
using CallThread2 = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                      const uint32_t*);
using Turret = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

inline void end_on(uint32_t ent, uint32_t hash) { slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, hash); }  // Scr_EndOn
// BroScr_HandleNoteTracks(out, ent, notify, 0, a copy of the empty string); the result is released.
inline void handle_note_tracks(uint32_t ent, uint32_t notify) {
    ScriptString empty;
    ScriptString_Copy(&empty, pointer<ScriptString>(0x00922e90));
    ScriptString result;
    title_function<HandleNoteTracks>(0x001ada20)(&result, ent, notify, 0, empty);  // BroScr_HandleNoteTracks
    scr_release(&result);
}
inline void stop_if_look_id(uint32_t ent, uint32_t id) {
    if (*int_slot(ent, kLookId) == id) BroScr_StopLookingAt(ent);
}
inline void look_at_end_on_notify(uint32_t ent, uint32_t id, uint32_t end, uint32_t notify) {
    end_on(ent, kDeath);
    end_on(ent, end);
    Scr_WaitTill(ent, notify);
    queue_notify(ent, end);
    stop_if_look_id(ent, id);
}
inline void prone_recover(uint32_t ent, const uint32_t* a2, uint32_t callback) {
    title_function<CallThread>(0x004b09e0)(0, 0, 0, 0, callback, ent, a2);  // Scr_CallThread_ent_ent_ORAN
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922748)(ent, title_global<uint32_t>(0x008cf21c));  // ScrActor_SetInt_1370(recover anim)
    slot<Knob>(0x009225e0)(ent, 0x54e3b2b6, title_global<uint32_t>(0x008cf21c), 0x3f800000, 0x3e4ccccd, 0x3f800000,
                           1);  // GScr_SetFlaggedAnimKnob
    Scr_WaitTill(ent, 0x54e3b2b6);
}
inline void look_until_kill(uint32_t ent, const uint32_t* id, uint32_t callback, uint32_t end) {
    title_function<CallThread>(0x0052d0f0)(0, 0, 0, 0, callback, ent, id);  // Scr_CallThread_ent_i
    end_on(ent, kDeath);
    end_on(ent, end);
    Scr_WaitTill(ent, kKillAnimScript);
    queue_notify(ent, end);
    stop_if_look_id(ent, *id);
}

}  // namespace

void XB_CDECL BroScr_StopLookingAtOnNotify(uint32_t ent) {
    end_on(ent, kDeath);
    Scr_WaitTill(ent, 0x247afc7f);
    BroScr_StopLookingAt(ent);
}

void XB_CDECL BroScr_TurretProneMG42Recover(uint32_t ent, uint32_t a2) {
    prone_recover(ent, &a2, 0x00183a70);  // BroScr_TurretSetProneRecoverAnim
}

void XB_CDECL BroScr_TurretProneMG42Recover_2(uint32_t ent, uint32_t a2) {
    end_on(ent, kKillAnimScript);
    prone_recover(ent, &a2, 0x00183bf0);  // BroScr_EmptyFunc_60
}

void XB_CDECL BroScr_SetAnimMovementStopAfter(uint32_t ent, float time) {
    end_on(ent, kKillAnimScript);
    end_on(ent, kDeath);
    Scr_Wait(time);
    *handle_slot(ent, 0x0ccee5c1) = 0x00385d90;
}

void XB_CDECL BroScr_WoundedWalkLoop(uint32_t ent) {
    if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922dc0)(ent) == 0x19669cd9) {  // ScrActor_GetInt_184
        const uint32_t value = *handle_slot(ent, 0xb7f3f874);
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922dc4)(
            ent, value == 0x06088a8c || value == 0x068698d3 ? 0xc1165808 : 0x070b89a4);  // ScrActor_SetInt_184
    }
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, 0x070b89a4, 0x003a09eb);  // BroScr_SetPoseMovement
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009225d8)(
        ent, title_global<uint32_t>(0x008cf780), 0x3f800000, 0x3dcccccd, 0x3f800000, 0);  // GScr_SetAnimKnob(wounded_walk_loop)
    Scr_Wait(as_float(0x41200000));  // 10
}

void XB_CDECL BroScr_WaitFriendlyFinishedMG42(uint32_t, uint32_t ai) {
    Scr_WaitTillString(ai, pointer<const char>(0x00825b18));  // "friendly_finished_using_mg42"
    BroScr_LeaveTurretToNode(ai);
}

void XB_CDECL BroScr_MagicBulletShieldDefault(uint32_t ent) {
    const uint32_t health = slot<uint32_t XB_CDECL(uint32_t)>(0x00922b90)(ent);  // ScrEnt_GetHealth
    BroScr_MagicBulletShield(ent, 100000000, title_global<float>(0x0083ece8), health, 1);  // 5 s
}

void XB_CDECL BroScr_LookAtEndOnNotify_1(uint32_t ent, uint32_t id) { look_at_end_on_notify(ent, id, 0xa538f810, 0x807e5a80); }

void XB_CDECL BroScr_LookAtEndOnNotify_2(uint32_t ent, uint32_t id) { look_at_end_on_notify(ent, id, 0x671c92d3, 0xd6604ba3); }

void XB_CDECL BroScr_LookAtEndOnNotify_3(uint32_t ent, uint32_t other, uint32_t id) {
    end_on(ent, kDeath);
    end_on(other, kDeath);
    end_on(ent, 0x1651c9e7);
    Scr_WaitTill(ent, 0x0fc3c1c9);
    queue_notify(ent, 0x1651c9e7);
    stop_if_look_id(ent, id);
}

void XB_CDECL BroScr_DebugDrawChainRadiiLoop(uint32_t ent, uint32_t) {
    using GetArray = DynArray* XB_THISCALL(ScriptKeyTable*, uint32_t);
    for (;;) {
        DynArray* nodes = title_function<GetArray>(0x0045cd50)(ent_script_object(ent), 0x0605894e);  // ScriptKeyTable_GetArray
        if (nodes->size == 0) DynArray_Resize(reinterpret_cast<uint8_t*>(nodes), 1, 4);
        BroScr_DebugDrawRadiusCircle(nodes->data[0]);
        nodes = title_function<GetArray>(0x0045cd50)(ent_script_object(ent), 0x0605894e);
        if (nodes->size <= 1) DynArray_Resize(reinterpret_cast<uint8_t*>(nodes), 2, 5);
        BroScr_DebugDrawRadiusCircle(nodes->data[1]);
        Scr_WaitFrames(0);
    }
}

void XB_CDECL BroScr_AnimScript_TurretCall_1(uint32_t ent, uint32_t a2) {
    title_function<Turret>(0x001b6600)(ent, a2, 0x0018c4e0, 0x0019cf40, 0x0018c540);  // BroScr_AnimScript_Turret_2
    for (;;) {}
}

void XB_CDECL BroScr_AnimScript_TurretCall_2(uint32_t ent, uint32_t a2) {
    title_function<Turret>(0x001b6600)(ent, a2, 0x0018c5a0, 0, 0x0018c5f0);  // BroScr_AnimScript_Turret_2
    for (;;) {}
}

void XB_CDECL BroScr_AnimScript_TurretCall_3(uint32_t ent, uint32_t a2) {
    title_function<Turret>(0x001b8ec0)(ent, a2, 0x0018da20, 0x0019f320, 0x0018da70);  // BroScr_AnimScript_Turret_3
    for (;;) {}
}

void XB_CDECL BroScr_AnimScript_TurretCall_4(uint32_t ent, uint32_t a2) {
    title_function<Turret>(0x001b9340)(ent, a2, 0x0018dad0, 0x0019f6f0, 0x0018db30);  // BroScr_AnimScript_Turret
    for (;;) {}
}

void XB_CDECL BroScr_AnimScript_TurretCall_5(uint32_t ent, uint32_t a2) {
    title_function<Turret>(0x001b9340)(ent, a2, 0x0019f930, 0, 0x0018db80);  // BroScr_AnimScript_Turret
    for (;;) {}
}

void XB_CDECL BroScr_LookAtUntilKillAnimscript_1(uint32_t ent, uint32_t id) {
    look_until_kill(ent, &id, 0x001abc70, 0xa538f810);  // BroScr_LookAtEndOnNotify_1
}

void XB_CDECL BroScr_LookAtUntilKillAnimscript_2(uint32_t ent, uint32_t id) {
    look_until_kill(ent, &id, 0x001abd00, 0x671c92d3);  // BroScr_LookAtEndOnNotify_2
}

void XB_CDECL BroScr_LookAtUntilKillAnimscript_3(uint32_t ent, uint32_t other, uint32_t id) {
    title_function<CallThread2>(0x0052ced0)(0, 0, 0, 0, 0x001abd90, ent, &other, &id);  // Scr_CallThread_ent_ent_i(_3)
    end_on(ent, kDeath);
    end_on(other, kDeath);
    end_on(ent, 0x1651c9e7);
    Scr_WaitTill(ent, kKillAnimScript);
    queue_notify(ent, 0x1651c9e7);
    stop_if_look_id(ent, id);
}

void XB_CDECL BroScr_BlendAimAnimsByYawLoop(uint32_t ent, uint32_t end, uint32_t anim_a, uint32_t anim_b, uint32_t anim_c,
                                           uint32_t a_bits, uint32_t b_bits, uint32_t c_bits, uint32_t base_bits) {
    end_on(ent, kKillAnimScript);
    end_on(ent, end);
    for (;;) {
        BroScr_BlendAimAnimsByYaw(ent, anim_a, anim_b, anim_c, a_bits, b_bits, c_bits, base_bits);
        Scr_Wait(as_float(0x3e4ccccd));  // 0.2
    }
}

void XB_CDECL BroScr_CoverCrouchRandomIdleAnim(uint32_t ent) {
    const int32_t roll = slot<int32_t XB_CDECL(uint32_t)>(0x0092222c)(100);  // Scr_RandomInt
    const long double random = slot<long double XB_CDECL(uint32_t)>(0x00922230)(0x3e4ccccd);  // Scr_RandomFloat(0.2)
    const uint32_t anim = roll < 75 ? title_global<uint32_t>(0x008ce8e0)    // hidelowwallb_idle1
                                    : title_global<uint32_t>(0x008ce8f8);  // hidelowwallb_twitch1
    const float rate = static_cast<float>(random + title_global<float>(0x0083ec18));  // + 0.9
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, float, uint32_t)>(0x009225e4)(
        ent, 0x54e3b2b6, anim, title_global<uint32_t>(0x008cd7dc), 0x3f800000, 0x3dcccccd, rate,
        1);  // GScr_SetFlaggedAnimKnobAll(.., Body, ..)
    handle_note_tracks(ent, 0x54e3b2b6);
}

void XB_CDECL BroScr_AnimFaceWaitNotetracks(uint32_t ent) {
    end_on(ent, kDeath);
    end_on(ent, 0xad9b8a7b);
    handle_note_tracks(ent, 0x1c859d65);
    *int_slot(ent, 0xfd6181bd) = 1;
    queue_notify(ent, 0x1c87198d);
}

void XB_CDECL BroScr_PlayStandMG42GunnerPain(uint32_t ent) {
    slot<Knob>(0x009225e0)(ent, 0x94d136d5, title_global<uint32_t>(0x008cf648), 0x3f800000, 0x3dcccccd, 0x3f800000,
                           0);  // GScr_SetFlaggedAnimKnob(standMG42gunner_pain)
    handle_note_tracks(ent, 0x94d136d5);
}

namespace {

// level + offset [animname hash][key]: an array of 0x28-byte hash maps (Index_6/7 then Index_8/9),
// element `index` (resized by `resize` first when past the count).
inline uint8_t* face_map(uint32_t offset, bool second, uint32_t* ent, const uint32_t* key, uint32_t index,
                         ScriptString* animname) {
    uint32_t hash;
    const uint32_t* const h =
        Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(ScrCall_ScrEnt_GetAnimname(ent, animname)));
    HashMap* const outer = reinterpret_cast<HashMap*>(title_global<uint8_t*>(0x00922ee0) + offset);
    DynArray* const maps = reinterpret_cast<DynArray*>(second ? HashMap_Index_9(reinterpret_cast<HashMap*>(HashMap_Index_7(outer, h)), key)
                                                              : HashMap_Index_8(reinterpret_cast<HashMap*>(HashMap_Index_6(outer, h)), key));
    if (index >= maps->size) {
        if (second)
            DynArray_ResizeHashMap_2(maps, index + 1, index + 4);
        else
            DynArray_ResizeHashMap(maps, index + 1, index + 4);
    }
    return reinterpret_cast<uint8_t*>(maps->data) + index * 0x28;
}

}  // namespace

void XB_CDECL BroScr_NotetrackFaceAnim(uint32_t ent, uint32_t key, uint32_t index, ScriptString note) {
    using Find = uint32_t* XB_THISCALL(uint8_t*, uint32_t*, const uint32_t*);
    using Index = uint32_t* XB_THISCALL(uint8_t*, const uint32_t*);
    uint32_t anim = 0;
    ScriptString face;
    ScriptString_FromRep(&face, nullptr);
    bool has;
    {
        ScriptString animname;
        uint8_t* const map = face_map(0x6f0, true, &ent, &key, index, &animname);
        const uint32_t anim_key = 0xca20284c;
        uint32_t it;
        title_function<Find>(0x0045e6d0)(map, &it, &anim_key);  // HashMap_Find
        if (it == *reinterpret_cast<uint32_t*>(map + 8)) {
            const uint32_t pair[2] = {anim_key, 0};
            uint32_t inserted;
            it = *title_function<Find>(0x004608a0)(map, &inserted, pair);  // HashMap_Insert
        }
        has = pointer<uint32_t>(it)[3] != 0;
        scr_release(&animname);
    }
    if (has) {
        ScriptString animname;
        uint8_t* const map = face_map(0x6f0, true, &ent, &key, index, &animname);
        const uint32_t anim_key = 0xca20284c;
        anim = *title_function<Index>(0x00461030)(map, &anim_key);  // HashMap_Index_13
        scr_release(&animname);
    }
    {
        ScriptString animname;
        uint8_t* const map = face_map(0x160, false, &ent, &key, index, &animname);
        uint32_t hash;
        ScriptString_Assign(&face, reinterpret_cast<ScriptString*>(title_function<Index>(0x00461120)(
                                       map, Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&note)))));  // HashMap_Index_11
        scr_release(&animname);
    }
    {
        ScriptString empty;
        ScriptString_Copy(&empty, pointer<ScriptString>(0x00922e90));
        ScriptString copy;
        ScriptString_Copy(&copy, &face);
        BroScr_StartAnimFaceWait(ent, anim, copy, title_global<uint32_t>(0x0083eb20), title_global<uint32_t>(0x00922ee8),
                                 empty, title_global<uint32_t>(0x00922ea4));
    }
    {
        ScriptString cleared;
        ScriptString* const value = ScriptString_FromRep(&cleared, nullptr);
        ScriptString animname;
        uint8_t* const map = face_map(0x160, false, &ent, &key, index, &animname);
        uint32_t hash;
        ScriptString_Assign(reinterpret_cast<ScriptString*>(title_function<Index>(0x00461120)(
                                map, Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&note)))),
                            value);
        scr_release(&animname);
        scr_release(&cleared);
    }
    scr_release(&face);
    scr_release(&note);
}

}  // namespace game
