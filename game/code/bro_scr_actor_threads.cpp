#include "bro_scr_actor_threads.h"

#include "bro_scr_ai_anim.h"
#include "bro_scr_ai_anim_threads.h"
#include "bro_scr_ai_common.h"
#include "bro_scr_common.h"
#include "bro_scr_entity_threads.h"
#include "bro_scr_ai_weapons.h"
#include "bro_scr_nodes_spawner.h"
#include "g_scr_runtime.h"
#include "g_script.h"
#include "bro_scr_ai.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "scr_access.h"
#include "scr_field.h"
#include "scr_field_instances.h"
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

namespace {

// The shared body of the MG42 anim tree threads; `put_gun` (the _3 form) first puts the gunner's gun
// back in hand when the last user leaves.
inline void mg42_anim_tree(uint32_t* ent, uint32_t gunner, bool put_gun) {
    {
        ScriptString tree;
        scr_string_from(&tree, 0x007f49a8);  // "mg42"
        slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009225f0)(*ent, &tree);  // GScr_UseAnimTree
        scr_release(&tree);
    }
    bool defined;
    if (*ScrField_habc1df0a_IsDefined(&defined, *ent))
        ++*int_slot(*ent, 0xabc1df0a);
    else
        *int_slot(*ent, 0xabc1df0a) = 0;
    Scr_WaitTill(gunner, kKillAnimScript);
    if (*int_slot(*ent, 0xabc1df0a) != 0) {
        --*int_slot(*ent, 0xabc1df0a);
        return;
    }
    if (put_gun) BroScr_PutGunInHand(gunner, 0x07151174);
    title_function<void XB_THISCALL(uint32_t*, uint32_t)>(0x0011eb30)(ent, 0xabc1df0a);  // ScrCall_GScr_GetEntScriptObject_2: removes the key
    slot<void XB_CDECL(uint32_t)>(0x009225f4)(*ent);  // GScr_ClearAnimTree
}
using SetMoveAnims = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
// The thread kept in `key` terminated, this one kept instead.
inline void replace_thread(uint32_t ent, uint32_t key) {
    slot<void XB_CDECL(uint32_t)>(0x009221a0)(*key_slot(ent, key, 0));  // Scr_TerminateThread
    uint32_t* const kept = key_slot(ent, key, 0);
    *kept = slot<uint32_t XB_CDECL()>(0x0092219c)();  // Scr_GetCurrentThreadHandle
}
inline void move_anims(uint32_t ent, const uint32_t* a, uint32_t visible) {
    title_function<SetMoveAnims>(0x0018df30)(ent, a[0], a[1], a[2], a[3], a[4], a[5], title_global<uint32_t>(0x0083eca0),
                                            visible);  // BroScr_SetDirectionalMoveAnims(.., 0.2, visible)
}
inline uint32_t visible_model(uint32_t ent) {
    return slot<uint32_t XB_CDECL(uint32_t)>(0x00922358)(ent) != 0;  // GScr_HasVisibleModel
}
inline void anim_knob(uint32_t ent, uint32_t anim) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922748)(ent, title_global<uint32_t>(anim));  // ScrActor_SetInt_1370
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009225d8)(
        ent, title_global<uint32_t>(anim), 0x3f800000, 0x3e4ccccd, 0x3f800000, 1);  // GScr_SetAnimKnob
}
inline void step_sound(uint32_t ent, uint32_t name) {
    Scr_Wait(static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922230)(0x3f333333)));  // RandomFloat(0.7)
    ScriptString sound;
    scr_string_from(&sound, name);
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t)>(0x009221dc)(ent, &sound, 0, 0, 0);  // GScr_PlaySound
    scr_release(&sound);
}

}  // namespace

void XB_CDECL BroScr_Mg42AnimTree(uint32_t ent, uint32_t gunner) { mg42_anim_tree(&ent, gunner, false); }

void XB_CDECL BroScr_Mg42AnimTree_2(uint32_t ent, uint32_t gunner) { mg42_anim_tree(&ent, gunner, false); }

void XB_CDECL BroScr_Mg42AnimTree_3(uint32_t ent, uint32_t gunner) { mg42_anim_tree(&ent, gunner, true); }

void XB_CDECL BroScr_Mg42AnimThenShootLoop_3(uint32_t ent, uint32_t turret) {
    uint32_t ref;
    if (entity_handle_is_valid(*slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922e00)(&ref, ent))) {  // ScrSentient_GetRef_3c
        anim_knob(ent, 0x008cf63c);  // standmg42gunner_fire
        title_function<void XB_CDECL(uint32_t, uint32_t)>(0x00183c20)(ent, turret);  // BroScr_Mg42ShootLoop_3
    }
    anim_knob(ent, 0x008cf630);  // standmg42gunner_aim
}

void XB_CDECL BroScr_DirectionalMoveAnimsOnNotify(uint32_t ent, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                                                 uint32_t a6, uint32_t a7) {
    end_on(ent, kKillAnimScript);
    replace_thread(ent, 0xa78b2c24);
    const uint32_t anims[6] = {a2, a3, a4, a5, a6, a7};
    for (;;) {
        Scr_WaitTill(ent, 0x2faeed05);
        move_anims(ent, anims, visible_model(ent));
    }
}

void XB_CDECL BroScr_DirectionalMoveAnimsLoop(uint32_t ent, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5, uint32_t a6,
                                             uint32_t a7) {
    end_on(ent, kKillAnimScript);
    replace_thread(ent, 0xf8b8e073);
    using Call = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t*,
                                   uint32_t*, uint32_t*, uint32_t*, uint32_t*);
    title_function<Call>(0x001e53e0)(0, 0, 0, 0, 0x0019fe80, ent, &a2, &a3, &a4, &a5, &a6,
                                     &a7);  // Scr_CallThread_ent_anim_anim_anim_anim_anim_anim(OnNotify)
    const uint32_t anims[6] = {a2, a3, a4, a5, a6, a7};
    move_anims(ent, anims, 1);
    for (;;) {
        Scr_Wait(as_float(0x3e4ccccd));  // 0.2
        move_anims(ent, anims, visible_model(ent));
    }
}

void XB_CDECL BroScr_ProneAimFaceDefault(uint32_t ent, float time, uint32_t end, uint32_t pose, uint32_t movement,
                                         uint32_t flag) {
    end_on(ent, kKillAnimScript);
    end_on(ent, kDeath);
    end_on(ent, end);
    Scr_Wait(time);
    const uint32_t current = slot<uint32_t XB_CDECL(uint32_t)>(0x00922dc0)(ent);  // ScrActor_GetInt_184
    if (pose == 0x0738b1a5) {
        if (current != pose) {
            slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009226bc)(
                ent, title_global<uint32_t>(0x008cf1d4), title_global<uint32_t>(0x008cf1c8), 0x3f800000, 0x3dcccccd,
                0x3f800000);  // GScr_ActorSetAimAnimWeights(prone shootfeet up / down)
            slot<void XB_CDECL(uint32_t, uint32_t)>(0x009226b0)(ent, 0x3f800000);
        }
    } else if (current == 0x0738b1a5) {
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x009226b4)(ent, 0x3f800000);
        ScriptString mode;
        scr_string_from(&mode, 0x0083addc);  // "face default"
        slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t)>(0x00922740)(ent, &mode, 0, 0);  // GScr_ActorSetOrientMode
        scr_release(&mode);
    }
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922dc4)(ent, pose);  // ScrActor_SetInt_184
    *handle_slot(ent, 0x0ccee5c1) = movement;
    *handle_slot(ent, 0x764918ae) = static_cast<uint8_t>(flag) ? 0xc4a729a7 : 0xc028584c;
}

void XB_CDECL BroScr_PlayStepSounds(uint32_t ent) {
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, 0x070b89a4, 0x0001c6ce);  // BroScr_SetPoseMovement
    *handle_slot(ent, 0x0ccee5c1) = 0x0001c6ce;
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922dc4)(ent, 0x070b89a4);  // ScrActor_SetInt_184
    step_sound(ent, 0x00825b98);  // "misc_step1"
    step_sound(ent, 0x00825b8c);  // "misc_step2"
    step_sound(ent, 0x00825b98);
    step_sound(ent, 0x00825b8c);
    Scr_Wait(as_float(0x3f800000));
}

void XB_CDECL BroScr_NotifyFallbackerDied(uint32_t, uint32_t guy, int32_t n) {
    Scr_WaitTill(guy, kDeath);
    ScriptString name;
    scr_string_from(&name, 0x00825ff8);  // "fallbacker_died"
    ScriptString_AppendInt(&name, n);
    const uint32_t object = title_global<uint32_t>(0x00922ef8);  // g_levelScriptObject
    const uint32_t* const handle = pointer<uint32_t>(object ? object + 4 : 0);
    uint32_t hash;
    queue_notify(*handle, *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&name)));
    DynArray* const counts = pointer<DynArray>(title_global<uint32_t>(0x00922ef8) + 0x6c);
    if (static_cast<uint32_t>(n) >= counts->size) DynArray_ResizeInt(counts, n + 1, n + 4);
    --counts->data[n];
    scr_release(&name);
}

namespace {

inline void ai_flags(uint32_t ent, uint32_t a, uint32_t b, uint32_t c) {
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t)>(0x009223a0)(ent, a, b, c);  // GScr_SetAIFlags
}
inline ScrVec3 get_vec(uint32_t address, uint32_t ent) {
    uint32_t buffer[3];
    const uint32_t* v = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(address)(buffer, ent);
    ScrVec3 out;
    out.v[0] = v[0];
    out.v[1] = v[1];
    out.v[2] = v[2];
    return out;
}
inline void face_angle(uint32_t ent) {
    uint32_t yaw;
    const uint32_t value = *title_function<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00184310)(&yaw, ent);  // BroScr_GetNodeOrActorYaw
    ScriptString mode;
    scr_string_from(&mode, 0x0083adfc);  // "face angle"
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t)>(0x00922740)(ent, &mode, value, 0);  // GScr_ActorSetOrientMode
    scr_release(&mode);
}
inline void twitch_knob(uint32_t ent, uint32_t anim, long double random) {
    const float rate = static_cast<float>(random + title_global<float>(0x0083ec18));  // + 0.9
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, float, uint32_t)>(0x009225e4)(
        ent, 0x54e3b2b6, anim, title_global<uint32_t>(0x008cd7dc), 0x3f800000, 0x3dcccccd, rate,
        1);  // GScr_SetFlaggedAnimKnobAll(.., Body, ..)
    handle_note_tracks(ent, 0x54e3b2b6);
}
inline void begin_thread(uint32_t ent, uint32_t* functor) {
    slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x00922168)(0, 0, 0, ent, functor,
                                                                                                   0);  // Scr_BeginThread
    if (slot<uint8_t XB_CDECL()>(0x0092218c)()) Scr_WaitFrames(0);  // Scr_InThread
}
inline uint32_t* ent_functor(uint32_t function, uint32_t ent) {
    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0xc);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f5ba0;
        functor[1] = function;
        functor[2] = ent;
    }
    return functor;
}
inline uint32_t mode_factor(uint32_t text) {
    ScriptString mode;
    scr_string_from(&mode, text);
    uint32_t out;
    return *BroScr_MgBurstModeFactor(&out, mode);
}
inline void destroy_strings(DynArray* names) {
    if (!names->data) return;
    ScriptString* const begin = reinterpret_cast<ScriptString*>(names->data);
    for (ScriptString* p = begin; p != begin + names->size; ++p) scr_release(p);
    Scr_Free(names->data);
}

}  // namespace

void XB_CDECL BroScr_GoToChainNode(uint32_t ent, uint32_t owner) {
    end_on(ent, 0xb633d42e);
    end_on(ent, kDeath);
    ai_flags(ent, 1, 1, 0);
    DynArray* const nodes = title_function<DynArray* XB_THISCALL(ScriptKeyTable*, uint32_t)>(0x0045cd50)(
        ent_script_object(owner), 0x0605894e);  // ScriptKeyTable_GetArray
    if (nodes->size == 0) DynArray_Resize(reinterpret_cast<uint8_t*>(nodes), 1, 4);
    ScrVec3 goal = get_vec(0x00922b20, nodes->data[0]);  // ScrEnt_GetOrigin
    slot<void XB_CDECL(uint32_t, ScrVec3*)>(0x00922684)(ent, &goal);  // GScr_SetGoalPos
    Scr_WaitTill(ent, 0x0032304c);
    ai_flags(ent, 0, 0, 0);
    uint32_t node = slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x009226ec)(ent, title_global<uint32_t>(0x008cd76c), 1);
    if (node == 0xffffffff) node = 0;
    slot<void XB_CDECL(uint32_t, uint32_t*)>(0x00922688)(ent, &node);  // GScr_SetGoalPosFromNode
}

void XB_CDECL BroScr_TurretOwnerChangeSwap(uint32_t ent, uint32_t owner, ScriptString classname) {
    Scr_Wait(as_float(0x3dcccccd));  // 0.1
    Scr_WaitTill(ent, 0x2b740dd3);
    ScrVec3 at = get_vec(0x00922b20, ent);  // ScrEnt_GetOrigin
    at.v[2] = bits(as_float(at.v[2]) + title_global<float>(0x0083eb20));  // + 1
    const uint32_t spawned = slot<uint32_t XB_CDECL(ScriptString*, ScrVec3*, uint32_t)>(0x009222f0)(&classname, &at, 0);  // GScr_Spawn
    slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922ba4)(spawned, get_vec(0x00922ba0, ent));  // ScrEnt_SetAngles(GetAngles)
    BroScr_PutGunInHand(owner, 0x07151174);
    queue_notify(owner, 0x2b740dd3);
    slot<void XB_CDECL(uint32_t)>(0x00922540)(ent);  // GScr_Delete
    scr_release(&classname);
}

void XB_CDECL BroScr_CoverCrouchIdleTwitch(uint32_t ent) {
    face_angle(ent);
    const int32_t roll = slot<int32_t XB_CDECL(uint32_t)>(0x0092222c)(100);  // Scr_RandomInt
    const long double random = slot<long double XB_CDECL(uint32_t)>(0x00922230)(0x3e4ccccd);  // Scr_RandomFloat(0.2)
    twitch_knob(ent, roll < 50 ? title_global<uint32_t>(0x008ce8bc) : title_global<uint32_t>(0x008ce8c8), random);  // hideLowWall_1 / _2
}

void XB_CDECL BroScr_CoverCrouchIdleTwitch_2(uint32_t ent) {
    face_angle(ent);
    const int32_t roll = slot<int32_t XB_CDECL(uint32_t)>(0x0092222c)(100);  // Scr_RandomInt
    const long double random = slot<long double XB_CDECL(uint32_t)>(0x00922230)(0x3e4ccccd);  // Scr_RandomFloat(0.2)
    uint32_t anim;
    if (roll < 50)
        anim = title_global<uint32_t>(0x008ce910);  // hidelowwallc_idle1
    else
        anim = roll < 75 ? title_global<uint32_t>(0x008ce91c) : title_global<uint32_t>(0x008ce928);
    twitch_knob(ent, anim, random);
}

void XB_CDECL BroScr_OnSpawnedReattachGun(uint32_t, uint32_t, uint32_t, uint32_t spawner, DynArray names) {
    uint32_t spawned = 0;
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t*)>(0x004b1590)(spawner, 0x262a34d8, &spawned);  // Scr_WaitTillOutputEnt_ORAN
    uint8_t finished;
    if (!*title_function<uint8_t* XB_CDECL(uint8_t*, uint32_t)>(0x0019ce50)(&finished, spawned)) {  // BroScr_WaitFinishedSpawning
        BroScr_ReattachGunInHand(spawned);
        DynArray copy = {nullptr, 0, 0};
        title_function<void XB_THISCALL(DynArray*, const DynArray*)>(0x0052d4a0)(&copy, &names);  // DynArray_AssignString_KASSPASS
        destroy_strings(&copy);
    }
    destroy_strings(&names);
}

void XB_CDECL BroScr_TempEventText(uint32_t, uint32_t, uint32_t, uint32_t p4, uint32_t p5, uint32_t p6, uint32_t p7) {
    Scr_Wait(as_float(0x3dcccccd));  // 0.1
    DynArray texts = {nullptr, 0, 0};
    {
        ScriptString name;
        scr_string_from(&name, 0x0082640c);  // "temp_event_text"
        Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&name)), 0x0d786b4c,
                        reinterpret_cast<uint8_t*>(&texts), 0);  // by targetname
        scr_release(&name);
    }
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(texts.size); ++i) {
        const uint32_t object = title_global<uint32_t>(0x00922ef8);  // g_levelScriptObject
        const uint32_t* const handle = pointer<uint32_t>(object ? object + 4 : 0);
        if (i >= texts.size) DynArray_Resize(reinterpret_cast<uint8_t*>(&texts), i + 1, i + 4);
        const uint32_t* const text = &texts.data[i];
        const uint32_t level = *handle;
        uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x20);  // ScrIface_Alloc
        if (functor) {
            functor[0] = 0x00825bd8;
            functor[1] = 0x001aad20;  // BroScr_TempEventTextThink
            functor[2] = level;
            functor[3] = *text;
            functor[4] = p4;
            functor[5] = p5;
            functor[6] = p6;
            functor[7] = p7;
        }
        slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(
            0, 0, 0, level, functor, 0);  // Scr_CreateThread
    }
    if (texts.data) Scr_Free(texts.data);
}

void XB_CDECL BroScr_UpdateOrientToEntity(uint32_t ent, uint32_t target) {
    {
        ScriptString message;
        scr_string_from(&message, 0x00826108);  // "UpdateOrientToEntity() called without an entity to look at"
        entity_handle_is_valid(target);
        scr_release(&message);
    }
    queue_notify(ent, 0x1e4aecac);
    end_on(ent, kDeath);
    end_on(target, kDeath);
    end_on(ent, 0x1e4aecac);
    title_function<CallThread>(0x004b09e0)(0, 0, 0, 0, 0x0019d7b0, ent, &target);  // Scr_CallThread_ent_ent_ORAN(BroScr_OrientToEntityKillWatch)
    for (;;) {
        const uint32_t lane = undefined_lane();
        uint32_t point[3] = {lane, lane, lane};
        if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922220)(target)) {  // GScr_IsSentient
            uint32_t eye[3] = {lane, lane, lane};
            slot<void XB_CDECL(uint32_t, uint32_t*)>(0x00922528)(target, eye);  // GScr_GetEye
            point[0] = eye[0];
            point[1] = eye[1];
            point[2] = eye[2];
        } else {
            const ScrVec3 o = get_vec(0x00922b20, target);  // ScrEnt_GetOrigin
            point[0] = o.v[0];
            point[1] = o.v[1];
            point[2] = o.v[2];
        }
        ScriptString mode;
        scr_string_from(&mode, 0x0083ad6c);  // "face point"
        slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t*)>(0x00922740)(ent, &mode, 0, point);  // GScr_ActorSetOrientMode
        scr_release(&mode);
        Scr_Wait(as_float(0x3e4ccccd));  // 0.2
    }
}

void XB_CDECL BroScr_ShuffleForwardAnim(uint32_t ent) {
    title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x001a00c0)(ent, 0x070b89a4, 0x00385d90);  // BroScr_SetPoseMovement
    if (*handle_slot(ent, 0xbfc60f36) != 0x0036b1a5 && *handle_slot(ent, 0xbfc60f36) == 0x77) *handle_slot(ent, 0xbfc60f36) = 0x61;
    const uint32_t anim = *handle_slot(ent, 0xbfc60f36) == 0x61 ? title_global<uint32_t>(0x008cf408)   // stand_alerta_shuffle_forward
                                                                : title_global<uint32_t>(0x008cf42c);  // stand_alertb_shuffle_forward
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009225e4)(
        ent, 0x5fa04364, anim, title_global<uint32_t>(0x008cd7dc), 0x3f800000, 0x3d4ccccd, 0x3f800000,
        0);  // GScr_SetFlaggedAnimKnobAll(.., Body, ..)
    handle_note_tracks(ent, 0x5fa04364);
}

void XB_CDECL BroScr_TurretManualTargetThink(uint32_t gunner, uint32_t turret) {
    Scr_EndOnString(gunner, pointer<const char>(0x00831f60));  // "death"
    end_on(gunner, 0x2b740dd3);
    bool defined;
    if (*ScrField_is_firing_IsDefined(&defined, gunner)) return;
    *reinterpret_cast<uint8_t*>(ScrField_is_firing(gunner, 0)) = 1;
    uint32_t target = 0;
    if (*ScrField_manual_target_IsDefined(&defined, turret)) {
        ScriptString classname;
        scr_string_from(&classname, 0x0083ab88);  // "script_origin"
        ScrVec3 zero;
        zero.v[0] = bits(0.0f);
        zero.v[1] = bits(0.0f);
        zero.v[2] = bits(0.0f);
        target = slot<uint32_t XB_CDECL(ScriptString*, ScrVec3*, uint32_t)>(0x009222f0)(&classname, &zero, 0);  // GScr_Spawn
        scr_release(&classname);
    }
    for (;;) {
        Scr_WaitTillString(turret, pointer<const char>(0x00825fe0));  // "StartFiring"
        bool manual = false;
        if (entity_handle_is_valid(turret)) {
            ScriptKeyTable* const table = title_function<ScriptKeyTable* XB_CDECL(uint32_t)>(0x0011db20)(turret);
            if (table) {
                const uint32_t* const value = ScriptKeyTable_Find(table, 0xcf1e52a5);  // manual_target
                manual = entity_handle_is_valid(value ? *value : 0) != 0;
            }
        }
        if (manual) {
            uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x14);  // ScrIface_Alloc
            if (functor) {
                functor[0] = 0x007f4a10;
                functor[1] = 0x001a4d30;  // BroScr_MgBurstFireThink
                functor[2] = gunner;
                functor[3] = turret;
                functor[4] = target;
            }
            slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(
                0, 0, 0, gunner, functor, 0);  // Scr_CreateThread
        }
        slot<void XB_CDECL(uint32_t)>(0x00922578)(turret);  // GScr_Turret_SetFlag4
        Scr_WaitTillString(turret, pointer<const char>(0x00825d0c));  // "StopFiring"
        slot<void XB_CDECL(uint32_t)>(0x0092257c)(turret);  // GScr_Turret_ClearFlag4
    }
}

void XB_CDECL BroScr_MgBurstFireThink(uint32_t, uint32_t turret, uint32_t target) {
    bool defined;
    const float delay_min = *ScrField_script_delay_min_IsDefined(&defined, turret) ? as_float(*ScrField_script_delay_min(turret))
                                                                                   : as_float(mode_factor(0x00839ef0));  // "delay"
    const float delay_range = *ScrField_script_delay_max_IsDefined(&defined, turret)
                                  ? as_float(*ScrField_script_delay_max(turret)) - delay_min
                                  : as_float(mode_factor(0x00825d00));  // "delay_range"
    const float burst_min = *ScrField_script_burst_min_IsDefined(&defined, turret) ? as_float(*ScrField_script_burst_min(turret))
                                                                                   : as_float(mode_factor(0x00825cf8));  // "burst"
    const float burst_range = *ScrField_script_burst_max_IsDefined(&defined, turret)
                                  ? as_float(*ScrField_script_burst_max(turret)) - burst_min
                                  : as_float(mode_factor(0x00825cec));  // "burst_range"
    end_on(turret, 0xcaa6bec3);
    for (;;) {
        slot<void XB_CDECL(uint32_t)>(0x00922578)(turret);  // GScr_Turret_SetFlag4
        if (entity_handle_is_valid(target)) {
            uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
            if (functor) {
                functor[0] = 0x007f4a28;
                functor[1] = 0x00199630;  // BroScr_TurretRandomSpread
                functor[2] = turret;
                functor[3] = target;
            }
            slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(
                0, 0, 0, turret, functor, 0);  // Scr_CreateThread
        }
        Scr_Wait(static_cast<float>(slot<long double XB_CDECL(float)>(0x00922230)(burst_range) + burst_min));  // Scr_RandomFloat
        slot<void XB_CDECL(uint32_t)>(0x0092257c)(turret);  // GScr_Turret_ClearFlag4
        const long double random = slot<long double XB_CDECL(float)>(0x00922230)(delay_range);
        Scr_Wait(static_cast<float>(delay_min + random));
    }
}

void XB_CDECL BroScr_GrenadeCowerHide(uint32_t ent) {
    if (*handle_slot(ent, 0x2181000b) != 0x0039894e && *handle_slot(ent, 0x2181000b) != kDeath) {
        uint32_t loop;
        uint32_t stop;
        if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922dc0)(ent) == 0x0738b1a5) {  // ScrActor_GetInt_184: prone
            loop = title_global<uint32_t>(0x008cd824);  // c_prone_grenade_cower_loop
            stop = title_global<uint32_t>(0x008cd83c);  // c_prone_grenade_cower_stop
        } else {
            if (ScriptString_Equals(title_function<ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t)>(0x00458da0)(
                                        ent_script_object(ent), 0x75f8caea),
                                    pointer<const char>(0x0083b7c4))) {  // "left"
                loop = title_global<uint32_t>(0x008ce880);  // grenadehide_left
                stop = title_global<uint32_t>(0x008ce88c);  // grenadehide_left2crouch
            } else {
                loop = title_global<uint32_t>(0x008ce898);  // grenadehide_right
                stop = title_global<uint32_t>(0x008ce8a4);  // grenadehide_right2crouch
            }
            slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922dc4)(ent, 0xc1165808);  // ScrActor_SetInt_184: crouch
        }
        slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009225c8)(
            ent, loop, title_global<uint32_t>(0x008cd7dc), 0x3f800000, 0x3ecccccd, 0x3f800000, 0);  // GScr_SetAnimKnobAll(.., Body)
        Scr_Wait(static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922230)(0x3e800000)));  // RandomFloat(0.25)
        slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009225e4)(
            ent, 0x8e613617, stop, title_global<uint32_t>(0x008cd7dc), 0x3f800000, 0x3ecccccd, 0x3f800000,
            1);  // GScr_SetFlaggedAnimKnobAll
        handle_note_tracks(ent, 0x8e613617);
        *ScrField_he057503b(ent) = 0;
        return;
    }
    *key_slot(ent, 0xe057503b, 0) = 0;
}

void XB_CDECL BroScr_HeadLookMainLoop(uint32_t ent) {
    end_on(ent, kDeath);
    end_on(ent, 0x247afc7f);
    *reinterpret_cast<uint32_t*>(anim_object() + 0x60) = title_global<uint32_t>(0x007f71fc);
    *key_slot(ent, 0x9a44df1e, undefined_float()) = title_global<uint32_t>(0x0083eb64);
    const int32_t now = get_time();
    *int_slot(ent, 0x644536d0) = static_cast<uint32_t>(now);
    *int_slot(ent, 0xfa9a8384) = 1;
    begin_thread(ent, ent_functor(0x0019f090, ent));  // BroScr_StopLookingAtOnNotify
    {
        ScriptString team;
        ScriptString_Equals(ScrCall_ScrSentient_GetTeam(&ent, &team), pointer<const char>(0x00832084));  // "allies" (unused)
        scr_release(&team);
    }
    begin_thread(ent, ent_functor(0x001838c0, ent));  // BroScr_EndOnDeath
    for (;;) {
        Scr_WaitTill(ent, 0x189e36ec);
        queue_notify(ent, 0xe099bb44);
        const uint32_t none = undefined_handle();
        uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
        if (functor) {
            functor[0] = 0x007f4b00;
            functor[1] = 0x0019e5c0;  // BroScr_HeadLookThink
            functor[2] = ent;
            functor[3] = none;
        }
        begin_thread(ent, functor);
    }
}

}  // namespace game
