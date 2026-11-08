#include "bro_scr_actor_threads.h"

#include "bro_scr_ai_anim.h"
#include "bro_scr_ai_anim_threads.h"
#include "bro_scr_ai_look.h"
#include "bro_scr_ai_common.h"
#include "bro_scr_common.h"
#include "bro_scr_cover_turret.h"
#include "bro_scr_math.h"
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
#include "bro_scr_actor.h"
#include "bro_scr_note_handlers.h"
#include "bro_scr_notify_threads.h"
#include "levels_twins.h"

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
    own_function<HandleNoteTracks>(&BroScr_HandleNoteTracks)(&result, ent, notify, 0, empty);  // BroScr_HandleNoteTracks
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
        DynArray* nodes = own_function<GetArray>(&ScriptKeyTable_GetArray)(ent_script_object(ent), 0x0605894e);  // ScriptKeyTable_GetArray
        if (nodes->size == 0) DynArray_Resize(reinterpret_cast<uint8_t*>(nodes), 1, 4);
        BroScr_DebugDrawRadiusCircle(nodes->data[0]);
        nodes = own_function<GetArray>(&ScriptKeyTable_GetArray)(ent_script_object(ent), 0x0605894e);
        if (nodes->size <= 1) DynArray_Resize(reinterpret_cast<uint8_t*>(nodes), 2, 5);
        BroScr_DebugDrawRadiusCircle(nodes->data[1]);
        Scr_WaitFrames(0);
    }
}

void XB_CDECL BroScr_AnimScript_TurretCall_1(uint32_t ent, uint32_t a2) {
    own_function<Turret>(&BroScr_AnimScript_Turret_2)(ent, 0x0018c4e0, 0x0019cf40, 0x0018c540, a2);  // BroScr_AnimScript_Turret_2
    for (;;) {}
}

void XB_CDECL BroScr_AnimScript_TurretCall_2(uint32_t ent, uint32_t a2) {
    own_function<Turret>(&BroScr_AnimScript_Turret_2)(ent, 0x0018c5a0, 0, 0x0018c5f0, a2);  // BroScr_AnimScript_Turret_2
    for (;;) {}
}

void XB_CDECL BroScr_AnimScript_TurretCall_3(uint32_t ent, uint32_t a2) {
    own_function<Turret>(&BroScr_AnimScript_Turret_3)(ent, 0x0018da20, 0x0019f320, 0x0018da70, a2);  // BroScr_AnimScript_Turret_3
    for (;;) {}
}

void XB_CDECL BroScr_AnimScript_TurretCall_4(uint32_t ent, uint32_t a2) {
    own_function<Turret>(&BroScr_AnimScript_Turret)(ent, 0x0018dad0, 0x0019f6f0, 0x0018db30, a2);  // BroScr_AnimScript_Turret
    for (;;) {}
}

void XB_CDECL BroScr_AnimScript_TurretCall_5(uint32_t ent, uint32_t a2) {
    own_function<Turret>(&BroScr_AnimScript_Turret)(ent, 0x0019f930, 0, 0x0018db80, a2);  // BroScr_AnimScript_Turret
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
        own_function<Find>(&HashMap_Find)(map, &it, &anim_key);  // HashMap_Find
        if (it == *reinterpret_cast<uint32_t*>(map + 8)) {
            const uint32_t pair[2] = {anim_key, 0};
            uint32_t inserted;
            it = *own_function<Find>(&HashMap_Insert_PROLOGUE_2)(map, &inserted, pair);  // HashMap_Insert
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
        own_function<void XB_CDECL(uint32_t, uint32_t)>(&BroScr_Mg42ShootLoop_3)(ent, turret);  // BroScr_Mg42ShootLoop_3
    }
    anim_knob(ent, 0x008cf630);  // standmg42gunner_aim
}

void XB_CDECL BroScr_SetDirectionalMoveAnims(uint32_t ent, uint32_t forward, uint32_t back, uint32_t side_a,
                                             uint32_t side_b, uint32_t diagonal_a, uint32_t diagonal_b, uint32_t blend,
                                             uint32_t) {
    using SetAnim = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    slot<void XB_CDECL(uint32_t)>(0x009224b0)(5);  // GScr_EmptyStub220
    // The original keeps the yaw in ent's argument slot (FSTP there, ent stays in ESI).
    const uint32_t yaw = bits(static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922744)(ent)));  // GScr_ActorGetMotionYaw
    BroScr_ComputeMoveDirectionWeights(ent, as_float(yaw));
    // The weights block (0x00922efc) re-read for each call: +0x140 forward, +0x13c back, +0x148 / +0x144 the
    // sides, +0x14c / +0x150 the diagonals (only with their anim set).
    slot<SetAnim>(0x009225dc)(ent, forward, at<uint32_t>(anim_object(), 0x140), blend, 0x3f800000, 0);  // GScr_SetAnim
    slot<SetAnim>(0x009225dc)(ent, back, at<uint32_t>(anim_object(), 0x13c), blend, 0x3f800000, 0);
    slot<SetAnim>(0x009225dc)(ent, side_b, at<uint32_t>(anim_object(), 0x148), blend, 0x3f800000, 0);
    slot<SetAnim>(0x009225dc)(ent, side_a, at<uint32_t>(anim_object(), 0x144), blend, 0x3f800000, 0);
    if (diagonal_a) slot<SetAnim>(0x009225dc)(ent, diagonal_a, at<uint32_t>(anim_object(), 0x14c), blend, 0x3f800000, 0);
    if (diagonal_b) slot<SetAnim>(0x009225dc)(ent, diagonal_b, at<uint32_t>(anim_object(), 0x150), blend, 0x3f800000, 0);
    slot<void XB_CDECL(uint32_t)>(0x009224b4)(5);  // GScr_EmptyStub221
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
    const uint32_t value = *own_function<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(&BroScr_GetNodeOrActorYaw_entry)(&yaw, ent);  // BroScr_GetNodeOrActorYaw
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
    DynArray* const nodes = own_function<DynArray* XB_THISCALL(ScriptKeyTable*, uint32_t)>(&ScriptKeyTable_GetArray)(
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
    if (!*own_function<uint8_t* XB_CDECL(uint8_t*, uint32_t)>(&BroScr_WaitFinishedSpawning)(&finished, spawned)) {  // BroScr_WaitFinishedSpawning
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

namespace {

inline void copy3(uint32_t* dst, const uint32_t* src) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
}
inline void undefined3(uint32_t* v) { v[0] = v[1] = v[2] = undefined_lane(); }
inline void get_eye(uint32_t ent, uint32_t* out) {
    slot<void XB_CDECL(uint32_t, uint32_t*)>(0x00922528)(ent, out);  // GScr_GetEye
}
// GScr_ActorLookAt(ent, target, look rate 0x5e1a2a0c).
inline void actor_look_at(uint32_t ent, const uint32_t* target) {
    slot<void XB_CDECL(uint32_t, const uint32_t*, float)>(0x009226c4)(
        ent, target, static_cast<float>(static_cast<int32_t>(*int_slot(ent, 0x5e1a2a0c))));
}
// A vector call out = f(in) (Scr_AnglesToUp / _Right / Scr_VectorNormalize) into an undefined temp.
inline void vec_call(uint32_t address, uint32_t* out, const uint32_t* in) {
    uint32_t tmp[3];
    undefined3(tmp);
    slot<void XB_CDECL(uint32_t*, const uint32_t*)>(address)(tmp, in);
    copy3(out, tmp);
}

}  // namespace

void XB_CDECL BroScr_HeadLookThink(uint32_t ent, uint32_t notify_type) {
    using CallThreadVec3I = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                              const uint32_t*, const uint32_t*, const uint32_t*, const int32_t*);
    end_on(ent, kDeath);
    end_on(ent, 0xe099bb44);
    end_on(ent, 0x247afc7f);
    uint32_t look_ent = 0;
    uint32_t target[3];
    undefined3(target);

    // The look target by kind 0x42f377f3: a sentient's eye, an entity's origin, else the vector 0xb217897f.
    if (*handle_slot(ent, 0x42f377f3) == 0x9b751a35) {
        look_ent = *key_slot(ent, 0xeeadf3f2, 0);
        if (!slot<uint32_t XB_CDECL(uint32_t)>(0x00922220)(look_ent)) return;  // GScr_IsSentient
        uint32_t eye[3];
        undefined3(eye);
        get_eye(look_ent, eye);
        copy3(target, eye);
    } else if (*handle_slot(ent, 0x42f377f3) == 0xccdbaaf9) {
        look_ent = *ScrField_heeadf3f2(ent);
        if (!entity_handle_is_valid(look_ent)) return;
        uint32_t buffer[3];
        copy3(target, ScrCall_ScrEnt_GetOrigin(&look_ent, buffer));
    } else {
        copy3(target, reinterpret_cast<const uint32_t*>(ScrField_hb217897f(ent)));
    }
    actor_look_at(ent, target);
    const int32_t start = get_time();
    *int_slot(ent, 0x7b74ea2f) = static_cast<uint32_t>(start);
    const int32_t t0 = get_time();
    float rate_arg = 1.0f;

    // Until the look time 0x644536d0: follow the target, every 500 ms (first 50) the look rates.
    for (;;) {
        const uint32_t* const until = int_slot(ent, 0x644536d0);
        if (!(get_time() < static_cast<int32_t>(*until))) break;
        if (*handle_slot(ent, 0x42f377f3) == 0x9b751a35) {
            if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922210)(look_ent)) {  // GScr_IsAlive
                uint32_t eye[3];
                undefined3(eye);
                get_eye(look_ent, eye);
                copy3(target, eye);
            }
            actor_look_at(ent, target);
        } else if (*handle_slot(ent, 0x42f377f3) == 0xccdbaaf9) {
            uint32_t buffer[3];
            copy3(target, slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer, look_ent));  // ScrEnt_GetOrigin
            actor_look_at(ent, target);
        }
        uint32_t eye_buffer[3];
        undefined3(eye_buffer);
        get_eye(ent, eye_buffer);
        uint32_t eye[3];
        copy3(eye, eye_buffer);
        uint32_t aim[3];
        aim[0] = target[0];
        aim[1] = target[1];
        aim[2] = bits(as_float(target[2]) - 2.0f);

        // The eye-to-target line (stripped debug thread BroScr_EmptyFunc_68).
        if (notify_type != undefined_handle() && notify_type == 0xa3c65328) {
            int32_t two = 2;
            title_function<CallThreadVec3I>(0x001e52f0)(0, 0, 0, 0, 0x00184450, ent, eye, aim, pointer<const uint32_t>(0x008ccaa0),
                                                        &two);  // Scr_CallThread_ent_vec_vec_vec_i
        } else {
            uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x34);  // ScrIface_Alloc
            if (functor) {
                functor[0] = 0x00825c38;
                functor[1] = 0x00184450;  // BroScr_EmptyFunc_68
                functor[2] = ent;
                copy3(functor + 3, eye);
                copy3(functor + 6, aim);
                copy3(functor + 9, pointer<const uint32_t>(0x008cca64));
                functor[12] = 2;
            }
            begin_thread(ent, functor);
        }

        // The head yaw / pitch toward the target (computed; the clamps keep nothing).
        uint32_t angles[3], up[3], right[3], dir[3], delta[3];
        undefined3(angles);
        undefined3(up);
        undefined3(right);
        undefined3(dir);
        undefined3(delta);
        {
            ScriptString tag;
            scr_string_from(&tag, 0x0083c714);  // "Bip01 Head"
            uint32_t tmp[3];
            undefined3(tmp);
            slot<void XB_CDECL(uint32_t, uint32_t*, ScriptString*)>(0x00922620)(ent, tmp, &tag);  // GScr_GetTagAngles
            copy3(angles, tmp);
            scr_release(&tag);
        }
        vec_call(0x00922288, up, angles);     // Scr_AnglesToUp
        vec_call(0x0092228c, right, angles);  // Scr_AnglesToRight
        uint32_t diff[3];
        diff[0] = bits(as_float(target[0]) - as_float(eye[0]));
        diff[1] = bits(as_float(target[1]) - as_float(eye[1]));
        diff[2] = bits(as_float(target[2]) - as_float(eye[2]));
        copy3(delta, diff);
        vec_call(0x00922280, dir, delta);  // Scr_VectorNormalize
        using Dot = long double XB_CDECL(const uint32_t*, const uint32_t*);
        using Asin = long double XB_CDECL(float);
        const float dot_right = static_cast<float>(slot<Dot>(0x0092227c)(right, dir));  // Scr_VectorDot
        const float dot_up = static_cast<float>(slot<Dot>(0x0092227c)(up, dir));
        const float asin_right = static_cast<float>(slot<Asin>(0x0092224c)(dot_right));  // Scr_Asin
        const float asin_up = static_cast<float>(slot<Asin>(0x0092224c)(dot_up));
        const float pitch = (asin_up + 7.0f) * 0.6f;
        const float yaw = asin_right * 0.5f;

        if ((get_time() - t0) % 500 < 50) {
            BroScr_UpdateLookRates(ent, slot<uint32_t XB_CDECL(uint32_t)>(0x00922dc0)(ent),  // ScrActor_GetInt_184
                                   *handle_slot(ent, 0x0ccee5c1), *handle_slot(ent, 0x764918ae),
                                   *handle_slot(ent, 0x175567a8), bits(rate_arg), notify_type);
        }
        const float limit = 30.0f;
        float clamped;
        if (static_cast<float>(limit * -1.0f > yaw) == 0.0f) {
            clamped = limit;
            clamped = yaw;
        }
        if (static_cast<float>(limit * -1.0f > pitch) == 0.0f) {
            clamped = limit;
            clamped = pitch;
        }
        (void)clamped;
        const int32_t now = get_time();
        *int_slot(ent, 0x7b74ea2f) = static_cast<uint32_t>(now);
        Scr_Wait(0.05f);
    }
    BroScr_StopLookingAt(ent);
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

namespace {

// The inlined element access of a script array: &data[i], resized first when i is past the count.
inline uint32_t* element(DynArray* array, uint32_t i) {
    if (i >= array->size) DynArray_Resize(reinterpret_cast<uint8_t*>(array), i + 1, i + 4);
    return &array->data[i];
}
inline void create_thread(uint32_t ent, uint32_t* functor) {
    slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(0, 0, 0, ent, functor,
                                                                                               0);  // Scr_CreateThread
}
inline uint32_t get_int_184(uint32_t ent) { return slot<uint32_t XB_CDECL(uint32_t)>(0x00922dc0)(ent); }  // ScrActor_GetInt_184
// A death anim as flagged knob `notify` on the root (1, 0.05, 1) through ScrCall_GScr_SetFlaggedAnimKnobAll_2.
inline void death_knob(const uint32_t* ent, const uint32_t* notify, uint32_t anim) {
    ScrCall_GScr_SetFlaggedAnimKnobAll_2(ent, notify, anim, title_global<uint32_t>(0x008cf300), 0x3f800000, 0x3d4ccccd,
                                         0x3f800000);  // root
}
// The cover idle: crouch unless standing / crouching (0.5 to slot 0x009226b4); with the cover side
// key 0x175567a8 already `side` only the idle set fixed up (0x61 unless 0x61 / 0x62), else the idle
// set by the clamped yaw difference (left: <= 180, right: >= 180 is set a) and the anim set's idle
// (+0x14) as flagged knob 0xe227eb87 on Body after the orient; movement stop, side key = `side`.
inline void cover_idle(uint32_t ent, float yaw, uint32_t side, bool right) {
    if (get_int_184(ent) != 0x070b89a4 && get_int_184(ent) != 0xc1165808) {
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x009226b4)(ent, 0x3f000000);
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922dc4)(ent, 0xc1165808);  // ScrActor_SetInt_184: crouch
    }
    bool play;
    if (*handle_slot(ent, 0x175567a8) == side) {
        if (*handle_slot(ent, 0xbfc60f36) != 0x61 && *ScrField_anim_idleset(ent) != 0x62) *ScrField_anim_idleset(ent) = 0x61;
        play = false;
    } else {
        const ScrVec3 angles = get_vec(0x00922ba0, ent);  // ScrEnt_GetAngles
        const float difference = right ? yaw - as_float(angles.v[1]) + title_global<float>(0x0083ef10)
                                       : yaw - as_float(angles.v[1]) - title_global<float>(0x0083ef10);  // 15
        float clamped;
        BroScr_AngleClamp(&clamped, difference, false);
        const bool a = right ? clamped >= title_global<float>(0x0083eb60) : title_global<float>(0x0083eb60) >= clamped;  // 180
        *ScrField_anim_idleset(ent) = a ? 0x61 : 0x62;
        play = true;
    }
    const uint32_t* const set = pointer<uint32_t>(right ? BroScr_GetCoverRightAnimSet(ent) : BroScr_GetCoverLeftAnimSet(ent));
    {
        ScriptString mode;
        scr_string_from(&mode, 0x0083adfc);  // "face angle"
        const float angle = as_float(set[0]) + yaw;
        slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t)>(0x00922740)(ent, &mode, bits(angle), 0);  // GScr_ActorSetOrientMode
        scr_release(&mode);
    }
    if (play && set[5]) {
        const uint32_t rate = *ScrField_h6937c3b9(ent);
        slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009225e4)(
            ent, 0xe227eb87, set[5], title_global<uint32_t>(0x008cd7dc), 0x3f800000, 0x3e800000, rate,
            0);  // GScr_SetFlaggedAnimKnobAll(.., Body, ..)
        handle_note_tracks(ent, 0xe227eb87);
    }
    *handle_slot(ent, 0x0ccee5c1) = 0x00385d90;
    *handle_slot(ent, 0x175567a8) = side;
}
// The living AI of `team` touching `trigger` (none counted when fewer than `count` AI exist); `count`
// reached ends the wait (true), else a 1 s wait.
inline bool touching_count_reached(uint32_t trigger, ScriptString* team, int32_t count) {
    int32_t n = 0;
    DynArray ai = {nullptr, 0, 0};
    slot<void XB_CDECL(DynArray*, ScriptString*)>(0x009223d0)(&ai, team);  // GScr_GetAI
    if (static_cast<int32_t>(ai.size) >= count) {
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ai.size); ++i) {
            if (!slot<bool XB_CDECL(uint32_t, uint32_t)>(0x00922530)(*element(&ai, i), trigger)) continue;  // GScr_IsTouching
            if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922210)(*element(&ai, i))) ++n;  // GScr_IsAlive
        }
    }
    if (n >= count) {
        if (ai.data) Scr_Free(ai.data);
        return true;
    }
    Scr_Wait(as_float(0x3f800000));  // 1
    if (ai.data) Scr_Free(ai.data);
    return false;
}

}  // namespace

bool* XB_CDECL BroScr_PlayCoverDeathAnim(bool* out, uint32_t ent, uint32_t side) {
    const uint32_t notify = 0xaeb1527d;
    if (side == 0x38948440) {  // left
        if (get_int_184(ent) == 0x070b89a4) {
            slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009225e4)(
                ent, notify, title_global<uint32_t>(0x008ce3dc), title_global<uint32_t>(0x008cf300), 0x3f800000, 0x3d4ccccd,
                0x3f800000, 0);  // GScr_SetFlaggedAnimKnobAll(cornerstanddeath1_left, root, ..)
        } else if (get_int_184(ent) == 0xc1165808) {
            const int32_t roll = random_int(2);
            death_knob(&ent, &notify, roll == 0 ? title_global<uint32_t>(0x008cdcd4)     // corner_death_pushup
                                                : title_global<uint32_t>(0x008ce364));  // cornercrouchdeath1_left
        } else {
            *out = false;
            return out;
        }
    } else if (side == 0xa1cb941d) {  // right
        if (get_int_184(ent) == 0x070b89a4) {
            const int32_t roll = random_int(2);
            death_knob(&ent, &notify, roll == 0 ? title_global<uint32_t>(0x008ce3e8)     // cornerstanddeath1_right
                                                : title_global<uint32_t>(0x008ce3f4));  // cornerstanddeath2_right
        } else if (get_int_184(ent) == 0xc1165808) {
            death_knob(&ent, &notify, title_global<uint32_t>(0x008ce370));  // cornercrouchdeath1_right
        } else {
            *out = false;
            return out;
        }
    } else {
        *out = false;
        return out;
    }
    handle_note_tracks(ent, notify);
    *out = true;
    return out;
}

void XB_CDECL BroScr_CoverLeftIdle(uint32_t ent, float yaw) { cover_idle(ent, yaw, 0xa1cb941d, false); }

void XB_CDECL BroScr_CoverRightIdle(uint32_t ent, float yaw) { cover_idle(ent, yaw, 0x38948440, true); }

void XB_CDECL BroScr_FloodSpawnerDeleteWatch(uint32_t ent) {
    const uint32_t group = *int_slot(ent, 0x460bf109);
    DynArray spawners = {nullptr, 0, 0};
    Scr_WaitTill(ent, 0xe2a210cf);
    slot<void XB_CDECL(DynArray*)>(0x009223d8)(&spawners);  // GScr_GetSpawnerArray
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(spawners.size); ++i) {
        if (group != *int_slot(*element(&spawners, i), 0x460bf109)) continue;
        const uint32_t object = title_global<uint32_t>(0x00922ef8);  // g_levelScriptObject
        const uint32_t* const level = pointer<uint32_t>(object ? object + 4 : 0);
        ScriptString text;
        scr_string_from(&text, 0x00825d2c);  // "DELETED"
        const ScrVec3 o = get_vec(0x00922b20, *element(&spawners, i));  // ScrEnt_GetOrigin
        uint32_t origin[3] = {o.v[0], o.v[1], o.v[2]};
        own_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, ScriptString*,
                                         uint32_t*)>(&Scr_StartThreadFunctorN_entry)(0, 0, 0, 0, 0x0018bf00, *level, &text,
                                                                 origin);  // Scr_StartThreadFunctorN(BroScr_DebugCvarWait)
        scr_release(&text);
        slot<void XB_CDECL(uint32_t)>(0x00922540)(*element(&spawners, i));  // GScr_Delete
    }
    bool remove;
    {
        ScriptString name;
        if (!ScrCall_ScrEnt_GetTargetname(&ent, &name)->rep) {
            remove = true;
        } else {
            ScriptString again;
            remove = !ScriptString_Equals(ScrCall_ScrEnt_GetTargetname(&ent, &again), pointer<const char>(0x00824e54));  // "flood_spawner"
            scr_release(&again);
        }
        scr_release(&name);
    }
    if (remove) BroScr_DeleteFloodSpawner(ent);
    if (spawners.data) Scr_Free(spawners.data);
}

void XB_CDECL BroScr_FloodSpawnerTriggerThink(uint32_t ent) {
    DynArray targets = {nullptr, 0, 0};
    DynArray spawners = {nullptr, 0, 0};
    {
        ScriptString target;
        Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ScrCall_ScrEnt_GetTarget(&ent, &target))), 0x0d786b4c,
                        reinterpret_cast<uint8_t*>(&targets), 0);  // by targetname
        scr_release(&target);
    }
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(targets.size); ++i) {
        const uint32_t target = *element(&targets, i);
        create_thread(target, ent_functor(0x001a5830, target));  // BroScr_SetTargetnameFromOrigin
    }
    Scr_WaitTill(ent, 0xe2a210cf);
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(targets.size); ++i) {
        if (entity_handle_is_valid(*element(&targets, i)))
            DynArray_PushBack(reinterpret_cast<uint8_t*>(&spawners), element(&targets, i));
    }
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(spawners.size); ++i) {
        const uint32_t spawner = *element(&spawners, i);
        uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
        if (functor) {
            functor[0] = 0x007f4a28;
            functor[1] = 0x00199bc0;  // BroScr_TriggeredSpawnerThink
            functor[2] = spawner;
            functor[3] = ent;
        }
        create_thread(spawner, functor);
    }
    Scr_WaitFrames(0);
    if (entity_handle_is_valid(ent)) {
        bool defined = false;
        if (entity_handle_is_valid(ent)) {
            if (ScriptKeyTable* const table = title_function<ScriptKeyTable* XB_CDECL(uint32_t)>(0x0011db20)(ent)) {
                const uint32_t* const value = ScriptKeyTable_Find(table, 0xa7ad4a18);
                defined = (value ? *value : undefined_int()) != undefined_int();
            }
        }
        if (!defined) BroScr_DeleteFloodSpawner(ent);
    }
    if (spawners.data) Scr_Free(spawners.data);
    if (targets.data) Scr_Free(targets.data);
}

void XB_CDECL BroScr_WaitForAliveTouchingCount(uint32_t trigger, ScriptString team, int32_t count, float timeout) {
    if (timeout == title_global<float>(0x00922eac)) {
        while (!touching_count_reached(trigger, &team, count)) {}
    } else {
        const float end = static_cast<float>(get_time()) + timeout;
        for (;;) {
            const float now = static_cast<float>(get_time());
            if (static_cast<float>(end > now ? 1 : 0) == title_global<float>(0x0083eafc)) break;  // 0: timed out
            if (touching_count_reached(trigger, &team, count)) break;
        }
    }
    scr_release(&team);
}

namespace {

inline void move_delta_world(uint32_t ent, uint32_t* delta, uint32_t* world) {
    slot<void XB_CDECL(uint32_t*, uint32_t, uint32_t, uint32_t)>(0x00922408)(delta, title_global<uint32_t>(0x008cf570), 0,
                                                                           0x3f800000);  // GScr_GetMoveDelta(stand_shoot_run_back)
    slot<void XB_CDECL(uint32_t, uint32_t*, uint32_t*)>(0x0092263c)(ent, delta, world);  // GScr_LocalToWorldCoords
}
inline uint32_t* float_slot(uint32_t ent, uint32_t key) { return key_slot(ent, key, undefined_float()); }
inline uint32_t level_handle() {
    const uint32_t object = title_global<uint32_t>(0x00922ef8);  // g_levelScriptObject
    return *pointer<uint32_t>(object ? object + 4 : 0);
}
inline uint32_t get_count(uint32_t ent) { return slot<uint32_t XB_CDECL(uint32_t)>(0x00922b88)(ent); }  // ScrEnt count
inline void set_count(uint32_t ent, uint32_t n) { slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922b8c)(ent, n); }
// The inlined `text[i]` of a script string: 0 past the end.
inline uint32_t char_at(const ScriptString* text, uint32_t i) {
    if (text->rep && i < text->rep->length) return static_cast<uint8_t>(text->rep->text[i]);
    return 0;
}
inline void reserve_strings(DynArray* array, uint32_t size, uint32_t capacity) {
    title_function<void XB_THISCALL(DynArray*, uint32_t, uint32_t)>(0x0045c2e0)(array, size,
                                                                                capacity);  // DynArray_ReserveString_PROLOGUE
}
inline uint8_t* byte_element(DynArrayBytes* array, uint32_t i) {
    if (i >= array->size) DynArray_ResizeBytes(array, i + 1, i + 4);
    return &array->data[i];
}
inline uint32_t* turret_state_functor(uint32_t turret, float time) {
    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f4a70;
        functor[1] = 0x00181960;  // BroScr_TurretStateChangeWait
        functor[2] = turret;
        functor[3] = bits(time);
    }
    return functor;
}

}  // namespace

bool* XB_CDECL BroScr_TryShootRunBack(bool* out, uint32_t ent) {
    if (get_int_184(ent) != 0x070b89a4 && get_int_184(ent) != 0xc1165808) {
        *out = false;
        return out;
    }
    const uint32_t lane = undefined_lane();
    uint32_t delta[3] = {lane, lane, lane};
    uint32_t world[3] = {lane, lane, lane};
    move_delta_world(ent, delta, world);
    {
        uint32_t* const distance = float_slot(ent, 0xa3b8e90e);
        *reinterpret_cast<float*>(distance) =
            static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922690)(ent));  // GScr_SentientNearestEnemyDist
    }
    bool defined;
    if (!*ScrField_ha3b8e90e_IsDefined(&defined, ent) || get_int_184(ent) == 0x0738b1a5) {  // prone
        *out = false;
        return out;
    }
    bool played = false;
    while (static_cast<uint8_t>(slot<uint32_t XB_CDECL(uint32_t, uint32_t*)>(0x009226f4)(ent, world))) {  // GScr_ActorIsSafeMovePoint
        if (!(at<float>(anim_object(), 0x94) >= as_float(*float_slot(ent, 0xa3b8e90e)))) break;
        const float distance = as_float(*float_slot(ent, 0xa3b8e90e));
        if (static_cast<float>(distance > at<float>(anim_object(), 0x9c) ? 1 : 0) == title_global<float>(0x0083eafc)) break;
        played = true;
        slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009225e4)(
            ent, 0xd5f0267a, title_global<uint32_t>(0x008cf570), title_global<uint32_t>(0x008cd7dc), 0x3f800000, 0x3e19999a,
            0x3f800000, 0);  // GScr_SetFlaggedAnimKnobAll(stand_shoot_run_back, Body, ..)
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922dc4)(ent, 0x070b89a4);  // ScrActor_SetInt_184: stand
        *handle_slot(ent, 0x0ccee5c1) = 0x0001c6ce;
        handle_note_tracks(ent, 0xd5f0267a);
        queue_notify(ent, 0xeac9abbd);
        move_delta_world(ent, delta, world);
    }
    *out = played;
    return out;
}

void XB_CDECL BroScr_WaitTriggerAiCleared(uint32_t trigger, ScriptString team, DynArray* guys) {
    uint32_t counter;
    {
        ScriptString classname;
        scr_string_from(&classname, 0x0083ab88);  // "script_origin"
        ScrVec3 zero;
        zero.v[0] = bits(0.0f);
        zero.v[1] = bits(0.0f);
        zero.v[2] = bits(0.0f);
        counter = slot<uint32_t XB_CDECL(ScriptString*, ScrVec3*, uint32_t)>(0x009222f0)(&classname, &zero, 0);  // GScr_Spawn
        scr_release(&classname);
    }
    for (;;) {
        DynArray ai = {nullptr, 0, 0};
        DynArray touching = {nullptr, 0, 0};
        slot<void XB_CDECL(DynArray*, ScriptString*)>(0x009223d0)(&ai, &team);  // GScr_GetAI
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ai.size); ++i) {
            if (slot<bool XB_CDECL(uint32_t, uint32_t)>(0x00922530)(*element(&ai, i), trigger))  // GScr_IsTouching
                DynArray_PushBack(reinterpret_cast<uint8_t*>(&touching), element(&ai, i));
        }
        const int32_t n = static_cast<int32_t>(touching.size);
        if (n <= 0) {
            if (touching.data) Scr_Free(touching.data);
            if (ai.data) Scr_Free(ai.data);
            break;
        }
        *int_slot(counter, 0x97c82b3d) = static_cast<uint32_t>(n);
        for (uint32_t i = 0; static_cast<int32_t>(i) < n; ++i) {
            const uint32_t* const guy = element(&touching, i);
            uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
            if (functor) {
                functor[0] = 0x007f4a28;
                functor[1] = 0x0019bab0;  // BroScr_DecrementAiSizeOnDeath
                functor[2] = counter;
                functor[3] = *guy;
            }
            create_thread(counter, functor);
        }
        Scr_WaitTill(counter, 0xddc0d6b6);
        if (guys->capacity) {
            for (;;) {
                bool ready = true;
                if (static_cast<int32_t>(guys->size) <= 0) break;
                for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(guys->size); ++i) {
                    if (!entity_handle_is_valid(*element(guys, i))) continue;
                    bool has_model;
                    {
                        ScriptString model;
                        has_model = ScrCall_ScrEnt_GetModel(element(guys, i), &model)->rep != nullptr;
                        scr_release(&model);
                    }
                    if (has_model) {
                        const float health = static_cast<float>(
                            static_cast<int32_t>(slot<uint32_t XB_CDECL(uint32_t)>(0x00922b90)(title_global<uint32_t>(0x009236dc))) *
                            100);  // ScrEnt_GetHealth(player)
                        const float percent =
                            health / static_cast<float>(static_cast<int32_t>(slot<uint32_t XB_CDECL(uint32_t)>(0x00922c30)(
                                         title_global<uint32_t>(0x009236dc))));  // ScrEnt_GetMaxhealth
                        ready = static_cast<float>(percent > title_global<float>(0x0083f048) ? 1 : 0) !=
                                title_global<float>(0x0083eafc);  // > 85
                    } else {
                        ready = false;
                    }
                }
                if (ready) break;
                Scr_Wait(as_float(0x3f800000));  // 1
            }
        }
        if (touching.data) Scr_Free(touching.data);
        if (ai.data) Scr_Free(ai.data);
    }
    slot<void XB_CDECL(uint32_t)>(0x00922540)(counter);  // GScr_Delete
    scr_release(&team);
}

void XB_CDECL BroScr_TempEventTextThink(uint32_t, uint32_t ent, int32_t range, uint32_t, uint32_t, uint32_t) {
    if (slot<int32_t XB_CDECL(const char*)>(0x009222dc)(pointer<const char>(0x008340d0)) < 1) {  // GScr_GetCvarInt("sv_cheats")
        slot<void XB_CDECL(uint32_t)>(0x00922540)(ent);  // GScr_Delete
        return;
    }
    Scr_EndOnString(ent, pointer<const char>(0x00831f60));  // "death"
    const ScrVec3 origin = get_vec(0x00922b20, ent);  // ScrEnt_GetOrigin
    if (static_cast<uint32_t>(range) == undefined_int()) range = 0x400;
    ScriptString line;
    ScriptString_FromRep(&line, nullptr);
    ScriptString text;
    ScriptString_FromRep(&text, nullptr);
    DynArray lines = {nullptr, 0, 0};
    bool missing;
    {
        ScriptString noteworthy;
        missing = !ScrCall_ScrEnt_GetScriptNoteworthy(&ent, &noteworthy)->rep;
        scr_release(&noteworthy);
    }
    if (missing) {
        reserve_strings(&lines, 1, 4);
        ScriptString_AssignCString(reinterpret_cast<ScriptString*>(lines.data), pointer<const char>(0x008260d4));  // "Missing Script_NoteWorthy"
    } else {
        {
            ScriptString noteworthy;
            ScriptString_Assign(&text, ScrCall_ScrEnt_GetScriptNoteworthy(&ent, &noteworthy));
            scr_release(&noteworthy);
        }
        // The lines of the noteworthy text, split at ';'.
        for (uint32_t i = 0; static_cast<int32_t>(i) < (text.rep ? static_cast<int32_t>(text.rep->length) : 0); ++i) {
            if (char_at(&text, i) == ';') {
                const uint32_t n = lines.size;
                reserve_strings(&lines, n + 1, n + 4);
                ScriptString_Assign(reinterpret_cast<ScriptString*>(lines.data) + n, &line);
                ScriptString_AssignCString(&line, pointer<const char>(0x0082dfeb));  // ""
            } else {
                ScriptString_AppendChar(&line, char_at(&text, i));
            }
        }
        const uint32_t n = lines.size;
        reserve_strings(&lines, n + 1, n + 4);
        ScriptString_Assign(reinterpret_cast<ScriptString*>(lines.data) + n, &line);
    }
    // One point per line above the origin, 32 apart (the drawing itself is compiled out).
    DynArray points = {nullptr, 0, 0};
    const int32_t count = static_cast<int32_t>(lines.size);
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t point[3] = {origin.v[0], origin.v[1],
                                   bits(as_float(origin.v[2]) + static_cast<float>((count - i) * 32))};
        if (static_cast<uint32_t>(i) >= points.size)
            title_function<void XB_THISCALL(DynArray*, uint32_t, uint32_t)>(0x0045cde0)(&points, i + 1,
                                                                                        i + 4);  // DynArray_Reserve12_PROLOGUE
        uint32_t* const at_point = points.data + i * 3;
        at_point[0] = point[0];
        at_point[1] = point[1];
        at_point[2] = point[2];
    }
    slot<void XB_CDECL(uint32_t)>(0x00922540)(ent);  // GScr_Delete
    const float range_f = static_cast<float>(range);
    for (;;) {
        const ScrVec3 player = get_vec(0x00922b20, title_global<uint32_t>(0x009236dc));  // ScrEnt_GetOrigin(player)
        const long double distance =
            slot<long double XB_CDECL(const uint32_t*, const uint32_t*)>(0x00922268)(player.v, origin.v);  // Scr_Distance
        if (static_cast<float>(distance > range_f ? 1 : 0) == title_global<float>(0x0083eafc))
            Scr_Wait(as_float(0x3d4ccccd));  // 0.05 in range
        else
            Scr_Wait(as_float(0x3f800000));  // 1
    }
}

void XB_CDECL BroScr_MgBurstFire(uint32_t turret) {
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
    ScriptString state;
    scr_string_from(&state, 0x0082c0e0);  // "start"
    int32_t next = get_time();
    for (;;) {
        if (slot<bool XB_CDECL(uint32_t)>(0x00922614)(turret) && next <= get_time()) {  // GScr_Turret_GetFlag1
            if (!ScriptString_Equals(&state, pointer<const char>(0x0082c09c))) {  // "fire"
                ScriptString_AssignCString(&state, pointer<const char>(0x0082c09c));
                create_thread(turret, ent_functor(0x001818c0, turret));  // BroScr_Mg42ShootSoundLoop
            }
            const float time = static_cast<float>(slot<long double XB_CDECL(float)>(0x00922230)(burst_range) + burst_min);  // Scr_RandomFloat
            create_thread(turret, turret_state_functor(turret, time));
            Scr_WaitTill(turret, 0xeb04625e);
            const long double delay = slot<long double XB_CDECL(float)>(0x00922230)(delay_range);
            next = static_cast<int32_t>((delay + delay_min) * title_global<float>(0x0083eb9c));  // * 1000 (_ftol)
            next += get_time();
        } else {
            if (!ScriptString_Equals(&state, pointer<const char>(0x00802da0)))  // "aim"
                ScriptString_AssignCString(&state, pointer<const char>(0x00802da0));
            const float time = static_cast<float>(next - get_time()) * title_global<float>(0x0083eb4c);  // * 0.001
            create_thread(turret, turret_state_functor(turret, time));
            Scr_WaitTill(turret, 0xeb04625e);
        }
    }
}

void XB_CDECL BroScr_IncrementTriggerThink(uint32_t trigger) {
    ScriptString team;
    ScriptString_FromRep(&team, nullptr);
    DynArray ents = {nullptr, 0, 0};
    if (get_count(trigger) == undefined_int()) set_count(trigger, 1);
    while (static_cast<int32_t>(get_count(trigger)) > 0) {
        Scr_WaitTill(trigger, 0xe2a210cf);
        for (int32_t side = 0; side < 2; ++side) {
            if (side == 0)
                ScriptString_AssignCString(&team, pointer<const char>(0x00832084));  // "allies"
            else if (side == 1)
                ScriptString_AssignCString(&team, pointer<const char>(0x0083208c));  // "axis"
            Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&team)), 0xa106e9bd,
                            reinterpret_cast<uint8_t*>(&ents), 0);  // by team
            for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ents.size); ++i) {
                const uint32_t ent = *element(&ents, i);
                if (!entity_handle_is_valid(ent)) continue;
                ScriptKeyTable* const table = title_function<ScriptKeyTable* XB_CDECL(uint32_t)>(0x0011db20)(ent);
                if (!table) continue;
                const uint32_t* const found = ScriptKeyTable_Find(table, 0x8bbf13fe);
                if ((found ? *found : undefined_int()) == undefined_int()) continue;
                const uint32_t* const group = int_slot(trigger, 0x8bbf13fe);
                if (*int_slot(*element(&ents, i), 0x8bbf13fe) != *group) continue;
                const uint32_t* const member = element(&ents, i);
                set_count(*member, get_count(*member) + 1);
            }
        }
        set_count(trigger, get_count(trigger) - 1);
        Scr_Wait(as_float(0x3d4ccccd));  // 0.05
        if (slot<long double XB_CDECL(uint32_t)>(0x00922be0)(trigger) != title_global<float>(0x00922eac))  // ScrEnt_GetDelay
            Scr_Wait(static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922be0)(trigger)));
    }
    BroScr_DeleteFloodSpawner(trigger);
    if (ents.data) Scr_Free(ents.data);
    scr_release(&team);
}

void XB_CDECL BroScr_MgGetNewUser(uint32_t ent, uint32_t owner) {
    DynArray turrets = {nullptr, 0, 0};
    DynArray mine = {nullptr, 0, 0};
    DynArray others = {nullptr, 0, 0};
    {
        ScriptString classname;
        scr_string_from(&classname, 0x00839e80);  // "misc_mg42"
        Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&classname)), 0xa106e9bd,
                        reinterpret_cast<uint8_t*>(&turrets), 0);
        scr_release(&classname);
    }
    {
        ScriptString classname;
        scr_string_from(&classname, 0x00839e8c);  // "misc_turret"
        Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&classname)), 0xa106e9bd,
                        reinterpret_cast<uint8_t*>(&others), 0);
        scr_release(&classname);
    }
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(others.size); ++i)
        DynArray_PushBack(reinterpret_cast<uint8_t*>(&turrets), element(&others, i));
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(turrets.size); ++i) {
        const uint32_t turret = *element(&turrets, i);
        if (!entity_handle_is_valid(turret)) continue;
        ScriptKeyTable* const table = title_function<ScriptKeyTable* XB_CDECL(uint32_t)>(0x0011db20)(turret);
        if (!table) continue;
        const uint32_t* const found = ScriptKeyTable_Find(table, 0xec070be6);
        if ((found ? *found : undefined_int()) == undefined_int()) continue;
        if (*int_slot(*element(&turrets, i), 0xec070be6) != owner) continue;
        DynArray_PushBack(reinterpret_cast<uint8_t*>(&mine), element(&turrets, i));
    }
    const int32_t n = static_cast<int32_t>(mine.size);
    if (n != 0) {
        for (;;) {
            Scr_WaitTill(ent, 0x262a34d8);
            for (int32_t i = 0; i < n; ++i) {
                if (*int_slot(*element(&mine, i), 0x7b1e85ba) != 0) continue;
                uint32_t hash;
                queue_notify(*element(&mine, i), *Scr_HashName(&hash, pointer<const char>(0x007fff6c)));  // "get new user"
                break;
            }
        }
    }
    if (others.data) Scr_Free(others.data);
    if (mine.data) Scr_Free(mine.data);
    if (turrets.data) Scr_Free(turrets.data);
}

void XB_CDECL BroScr_CycleTargetsThread(uint32_t ent, uint32_t, uint32_t, int32_t count) {
    Scr_EndOnString(ent, pointer<const char>(0x00831f60));  // "death"
    const bool any = count > 0;
    for (;;) {
        DynArrayBytes used = {nullptr, 0, 0};
        if (any)
            for (int32_t i = 0; i < count; ++i) *byte_element(&used, i) = 0;
        if (any) {
            for (int32_t k = 0; k < count; ++k) {
                uint32_t* const pick = int_slot(ent, 0x99cb8902);
                *pick = static_cast<uint32_t>(random_int(count));
                Scr_WaitTillString(ent, pointer<const char>(0x00825fec));  // "next_target"
                while (*byte_element(&used, *int_slot(ent, 0x99cb8902)) != 0) {
                    ++*int_slot(ent, 0x99cb8902);
                    if (static_cast<int32_t>(*int_slot(ent, 0x99cb8902)) >= count) *int_slot(ent, 0x99cb8902) = 0;
                }
                *byte_element(&used, *int_slot(ent, 0x99cb8902)) = 1;
            }
        }
        if (used.data) Scr_Free(used.data);
    }
}

void XB_CDECL BroScr_FallbackGoalThink(uint32_t ent, int32_t n, uint32_t node) {
    queue_notify(ent, 0x93f871b8);
    slot<void XB_CDECL(uint32_t)>(0x00922730)(ent);  // GScr_ActorLeaveTurret
    const uint32_t kept = slot<uint32_t XB_CDECL(uint32_t)>(0x00922d20)(ent);  // ScrActor_GetInt_1308
    *int_slot(ent, 0xac91e5fb) = kept;
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922d24)(ent, 0);  // ScrActor_SetInt_1308
    const float old = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922d08)(ent));  // ScrActor_GetFloat_7e4
    uint32_t* const saved = int_slot(ent, 0x62271501);
    *saved = old == title_global<float>(0x00922eac) ? undefined_int() : static_cast<uint32_t>(static_cast<int32_t>(old));
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922d0c)(ent, 0x459c4000);  // ScrActor_SetFloat_7e4(5000)
    slot<void XB_CDECL(uint32_t, uint32_t*)>(0x00922688)(ent, &node);  // GScr_SetGoalPosFromNode
    if (node != 0 && node != 0xffffffff && slot<uint32_t XB_CDECL(uint32_t)>(0x00922224)(node)) {  // GScr_IsValidHandle
        const float radius = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922ac8)(node));  // ScrPathNode_GetRadius
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922df4)(ent, bits(radius));  // ScrSentient_SetFloat_18
    }
    end_on(ent, kDeath);
    {
        const uint32_t level = level_handle();
        uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x14);  // ScrIface_Alloc
        if (functor) {
            functor[0] = 0x007f4af4;
            functor[1] = 0x00181bc0;  // BroScr_NotifyFallbackReachedGoal
            functor[2] = level;
            functor[3] = ent;
            functor[4] = static_cast<uint32_t>(n);
        }
        create_thread(level, functor);
    }
    create_thread(ent, ent_functor(0x0019a500, ent));  // BroScr_FallbackOnGoalRestore
    bool debug;
    {
        ScriptString value;
        debug = ScriptString_Equals(ScrCall_GScr_GetCvar(&value, 0x00823550), pointer<const char>(0x0082e058));  // "fallback" == "1"
        scr_release(&value);
    }
    if (debug) {
        const ScrVec3 o = get_vec(0x00922ab8, node);  // ScrPathNode_GetOrigin
        uint32_t origin[3] = {o.v[0], o.v[1], o.v[2]};
        title_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*)>(0x0045a540)(
            0, 0, 0, 0, 0x0018bfc0, ent, origin);  // Scr_StartThread_ent_vec_PROLOGUE(BroScr_FallbackDebugLoop)
    }
    Scr_WaitTill(ent, 0xed4ff1b2);
    ScriptString name;
    scr_string_from(&name, 0x00825ac4);  // "fallback_reached_goal"
    ScriptString_AppendInt(&name, n);
    const uint32_t object = title_global<uint32_t>(0x00922ef8);  // g_levelScriptObject
    const uint32_t* const level = pointer<uint32_t>(object ? object + 4 : 0);
    uint32_t hash;
    queue_notify(*level, *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&name)));
    scr_release(&name);
}

namespace {

using CallThreadTime = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const float*,
                                         const uint32_t*);
inline void turret_script_start(uint32_t ent) {
    end_on(ent, kKillAnimScript);
    BroScr_InitAnimScript(ent, 0xef68c4d4);
    BroScr_PutGunInHand(ent, 0x0036b1a5);
    *handle_slot(ent, 0x175567a8) = 0x00371ab2;
}
// "fire": on the change a `fire` call thread (ent, turret).
inline void turret_fire_state(ScriptString* state, uint32_t ent, uint32_t fire, const uint32_t* turret) {
    if (ScriptString_Equals(state, pointer<const char>(0x0082c09c))) return;  // "fire"
    ScriptString_AssignCString(state, pointer<const char>(0x0082c09c));
    title_function<CallThread>(0x004b09e0)(0, 0, 0, 0, fire, ent, turret);  // Scr_CallThread_ent_ent_ORAN
}
// The burst: a `change` call thread (ent, time, turret), then the wait for 0xeb04625e.
inline void turret_burst(uint32_t ent, uint32_t change, float time, const uint32_t* turret) {
    title_function<CallThreadTime>(0x001e4c80)(0, 0, 0, 0, change, ent, &time, turret);  // Scr_CallThread_ent_f_ent_3
    Scr_WaitTill(*turret, 0xeb04625e);
}
// The next burst: (delay_min + RandomFloat(delay_range)) s from now, in ms.
inline int32_t turret_next(float delay_min, float delay_range) {
    const float random = static_cast<float>(slot<long double XB_CDECL(float)>(0x00922230)(delay_range));  // Scr_RandomFloat
    const int32_t next = static_cast<int32_t>((delay_min + random) * title_global<float>(0x0083eb9c));  // * 1000
    return next + get_time();
}
// "recover": `recover`(ent, turret) when there is one.
inline void turret_recover(ScriptString* state, uint32_t ent, uint32_t recover, uint32_t turret) {
    if (!recover) return;
    ScriptString_AssignCString(state, pointer<const char>(0x0082641c));  // "recover"
    title_function<void XB_CDECL(uint32_t, uint32_t)>(recover)(ent, turret);
}
// "aim": on the change an `aim` thread (ent, turret).
inline void turret_aim_state(ScriptString* state, uint32_t ent, uint32_t aim, uint32_t turret) {
    if (ScriptString_Equals(state, pointer<const char>(0x00802da0))) return;  // "aim"
    ScriptString_AssignCString(state, pointer<const char>(0x00802da0));
    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f4a28;
        functor[1] = aim;
        functor[2] = ent;
        functor[3] = turret;
    }
    begin_thread(ent, functor);
}
// The aim wait: a `change` thread (ent, time, turret), then the wait for 0xeb04625e.
inline void turret_aim_wait(uint32_t ent, uint32_t change, float time, uint32_t turret) {
    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x14);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f4bfc;
        functor[1] = change;
        functor[2] = ent;
        functor[3] = bits(time);
        functor[4] = turret;
    }
    begin_thread(ent, functor);
    Scr_WaitTill(turret, 0xeb04625e);
}
inline void burst_settings(uint32_t turret, uint32_t* values) {
    const uint32_t object = title_global<uint32_t>(0x00922ef8);  // g_levelScriptObject
    const uint32_t* const level = pointer<uint32_t>(object ? object + 4 : 0);
    BroScr_MgBurstSettings(*level, turret, &values[0], &values[1], &values[2], &values[3]);
}
// The inlined burst settings of _2 / _3 (as BroScr_MgBurstFire).
struct Burst {
    float delay_min, delay_range, burst_min, burst_range;
};
inline Burst burst_fields(uint32_t turret) {
    bool defined;
    Burst b;
    b.delay_min = *ScrField_script_delay_min_IsDefined(&defined, turret) ? as_float(*ScrField_script_delay_min(turret))
                                                                         : as_float(mode_factor(0x00839ef0));  // "delay"
    b.delay_range = *ScrField_script_delay_max_IsDefined(&defined, turret)
                        ? as_float(*ScrField_script_delay_max(turret)) - b.delay_min
                        : as_float(mode_factor(0x00825d00));  // "delay_range"
    b.burst_min = *ScrField_script_burst_min_IsDefined(&defined, turret) ? as_float(*ScrField_script_burst_min(turret))
                                                                         : as_float(mode_factor(0x00825cf8));  // "burst"
    b.burst_range = *ScrField_script_burst_max_IsDefined(&defined, turret)
                        ? as_float(*ScrField_script_burst_max(turret)) - b.burst_min
                        : as_float(mode_factor(0x00825cec));  // "burst_range"
    return b;
}
inline float burst_time(float burst_min, float burst_range) {
    return static_cast<float>(slot<long double XB_CDECL(float)>(0x00922230)(burst_range) + burst_min);  // Scr_RandomFloat
}

}  // namespace

void XB_CDECL BroScr_AnimScript_Turret(uint32_t ent, uint32_t fire, uint32_t recover, uint32_t aim, uint32_t turret) {
    turret_script_start(ent);
    const uint32_t none = undefined_float();
    uint32_t values[4] = {none, none, none, none};  // delay min / range, burst min / range
    burst_settings(turret, values);
    int32_t next = get_time();
    ScriptString state;
    scr_string_from(&state, 0x0082c0e0);  // "start"
    for (;;) {
        if (slot<bool XB_CDECL(uint32_t)>(0x00922614)(turret) && next <= get_time()) {  // GScr_Turret_GetFlag1
            const float time = burst_time(as_float(values[2]), as_float(values[3]));
            if (static_cast<float>(time > 0.0f ? 1 : 0) != 0.0f) {
                turret_fire_state(&state, ent, fire, &turret);
                turret_burst(ent, 0x00183ad0, time, &turret);  // BroScr_TurretStateChangeAfterTime_3
            }
            next = turret_next(as_float(values[0]), as_float(values[1]));
            turret_recover(&state, ent, recover, turret);
        } else {
            turret_aim_state(&state, ent, aim, turret);
            const float time = static_cast<float>(next - get_time()) * title_global<float>(0x0083eb4c);  // * 0.001
            turret_aim_wait(ent, 0x00183ad0, time, turret);
        }
        burst_settings(turret, values);
    }
}

void XB_CDECL BroScr_AnimScript_Turret_3(uint32_t ent, uint32_t fire, uint32_t recover, uint32_t aim, uint32_t turret) {
    turret_script_start(ent);
    const Burst b = burst_fields(turret);
    int32_t next = get_time();
    ScriptString state;
    scr_string_from(&state, 0x0082c0e0);  // "start"
    for (;;) {
        if (slot<bool XB_CDECL(uint32_t)>(0x00922614)(turret) && next <= get_time()) {  // GScr_Turret_GetFlag1
            turret_fire_state(&state, ent, fire, &turret);
            turret_burst(ent, 0x00183930, burst_time(b.burst_min, b.burst_range), &turret);  // BroScr_TurretStateChangeAfterTime_2
            next = turret_next(b.delay_min, b.delay_range);
            turret_recover(&state, ent, recover, turret);
        } else {
            turret_aim_state(&state, ent, aim, turret);
            const float time = static_cast<float>(next - get_time()) * title_global<float>(0x0083eb4c);  // * 0.001
            turret_aim_wait(ent, 0x00183930, time, turret);
        }
    }
}

void XB_CDECL BroScr_AnimScript_Turret_2(uint32_t ent, uint32_t fire, uint32_t recover, uint32_t aim, uint32_t turret) {
    turret_script_start(ent);
    const Burst b = burst_fields(turret);
    int32_t next = get_time();
    ScriptString state;
    scr_string_from(&state, 0x0082c0e0);  // "start"
    for (;;) {
        const float wait = static_cast<float>(next - get_time()) * title_global<float>(0x0083eb4c);  // * 0.001
        if (slot<bool XB_CDECL(uint32_t)>(0x00922614)(turret) && 0.0f >= wait) {  // GScr_Turret_GetFlag1
            turret_fire_state(&state, ent, fire, &turret);
            turret_burst(ent, 0x00182700, burst_time(b.burst_min, b.burst_range), &turret);  // BroScr_TurretStateChangeAfterTime
            next = turret_next(b.delay_min, b.delay_range);
            turret_recover(&state, ent, recover, turret);
        } else {
            turret_aim_state(&state, ent, aim, turret);
            turret_aim_wait(ent, 0x00182700, wait, turret);
        }
    }
}

namespace {

inline ScriptString* key_string(uint32_t ent, uint32_t key) {
    return title_function<ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t)>(0x00458da0)(ent_script_object(ent),
                                                                                            key);  // ScriptKeyTable_GetString
}
inline void remove_key(uint32_t* ent, uint32_t key) {
    title_function<void XB_THISCALL(uint32_t*, uint32_t)>(0x0011eb30)(ent, key);  // ScrCall_GScr_GetEntScriptObject_2: removes the key
}
inline bool int_defined(uint32_t ent, uint32_t key) {
    if (!entity_handle_is_valid(ent)) return false;
    ScriptKeyTable* const table = title_function<ScriptKeyTable* XB_CDECL(uint32_t)>(0x0011db20)(ent);
    if (!table) return false;
    const uint32_t* const found = ScriptKeyTable_Find(table, key);
    return (found ? *found : undefined_int()) != undefined_int();
}
// The tag's origin / angles (slot 0x0092261c / 0x00922620) on `ent` set on `to` (ScrEnt_SetOrigin / SetAngles).
inline void copy_tag(uint32_t get, uint32_t set, uint32_t ent, ScriptString* tag, uint32_t to) {
    const uint32_t lane = undefined_lane();
    ScrVec3 v;
    v.v[0] = lane;
    v.v[1] = lane;
    v.v[2] = lane;
    slot<void XB_CDECL(uint32_t, uint32_t*, ScriptString*)>(get)(ent, v.v, tag);
    slot<void XB_CDECL(uint32_t, ScrVec3)>(set)(to, v);
}
inline void tumble(uint32_t ent, uint32_t piece) {
    ScrVec3 v = get_vec(0x00922cd8, ent);  // ScrActor_GetVec_234
    title_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*)>(0x001e4f00)(
        0, 0, 0, 0, 0x001834a0, piece, v.v);  // Scr_CallThread_ent_vec(BroScr_RandomTumble)
}
using AttachSlot = void XB_CDECL(uint32_t, ScriptString*, ScriptString*);

}  // namespace

void XB_CDECL BroScr_Mg42AutoTriggerThink(uint32_t trigger) {
    Scr_WaitTill(trigger, 0xe2a210cf);
    DynArray ai = {nullptr, 0, 0};
    {
        ScriptString team;
        scr_string_from(&team, 0x0083208c);  // "axis"
        slot<void XB_CDECL(DynArray*, ScriptString*)>(0x009223d0)(&ai, &team);  // GScr_GetAI
        scr_release(&team);
    }
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ai.size); ++i) {
        if (!int_defined(*element(&ai, i), 0xbe5f9a9f)) continue;
        const uint32_t* const group = int_slot(*element(&ai, i), 0xbe5f9a9f);
        if (*int_slot(trigger, 0xbe5f9a9f) != *group) continue;
        queue_notify(*element(&ai, i), 0xad76f051);
    }
    DynArray spawners = {nullptr, 0, 0};
    slot<void XB_CDECL(DynArray*)>(0x009223d8)(&spawners);  // GScr_GetSpawnerArray
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(spawners.size); ++i) {
        if (!int_defined(*element(&spawners, i), 0xbe5f9a9f)) continue;
        const uint32_t* const group = int_slot(*element(&spawners, i), 0xbe5f9a9f);
        if (*int_slot(trigger, 0xbe5f9a9f) != *group) continue;
        *handle_slot(*element(&spawners, i), 0x97c730fd) = 0xad76f051;
    }
    BroScr_DeleteFloodSpawner(trigger);
    if (spawners.data) Scr_Free(spawners.data);
    if (ai.data) Scr_Free(ai.data);
}

void XB_CDECL BroScr_MagicGrenadeThrowThink(uint32_t trigger, uint32_t fuse) {
    Scr_EndOnString(trigger, pointer<const char>(0x00831f60));  // "death"
    DynArray targets = {nullptr, 0, 0};
    DynArray ai = {nullptr, 0, 0};
    DynArray ends = {nullptr, 0, 0};
    {
        ScriptString target;
        Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ScrCall_ScrEnt_GetTarget(&trigger, &target))),
                        0x0d786b4c, reinterpret_cast<uint8_t*>(&targets), 0);  // by targetname
        scr_release(&target);
    }
    const uint32_t lane = undefined_lane();
    uint32_t velocity[3] = {lane, lane, lane};
    uint32_t guy = 0;  // the last thrower found, kept between rounds
    for (;;) {
        {
            ScriptString team;
            scr_string_from(&team, 0x0083208c);  // "axis"
            slot<void XB_CDECL(DynArray*, ScriptString*)>(0x009223d0)(&ai, &team);  // GScr_GetAI
            scr_release(&team);
        }
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ai.size); ++i) {
            if (slot<bool XB_CDECL(uint32_t, uint32_t)>(0x00922530)(trigger, *element(&ai, i))) {  // GScr_IsTouching
                guy = *element(&ai, i);
                break;
            }
        }
        if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922210)(guy)) {  // GScr_IsAlive
            uint32_t start = *element(&targets, static_cast<uint32_t>(random_int(static_cast<int32_t>(targets.size))));
            {
                ScriptString target;
                Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ScrCall_ScrEnt_GetTarget(&start, &target))),
                                0x0d786b4c, reinterpret_cast<uint8_t*>(&ends), 0);
                scr_release(&target);
            }
            const uint32_t end = *element(&ends, static_cast<uint32_t>(random_int(static_cast<int32_t>(ends.size))));
            {
                const ScrVec3 from = get_vec(0x00922b20, start);  // ScrEnt_GetOrigin
                const ScrVec3 to = get_vec(0x00922b20, end);
                const float delta[3] = {as_float(to.v[0]) - as_float(from.v[0]), as_float(to.v[1]) - as_float(from.v[1]),
                                        as_float(to.v[2]) - as_float(from.v[2])};
                uint32_t normal[3] = {undefined_lane(), undefined_lane(), undefined_lane()};
                slot<void XB_CDECL(uint32_t*, const float*)>(0x00922280)(normal, delta);  // Scr_VectorNormalize
                velocity[0] = normal[0];
                velocity[1] = normal[1];
                velocity[2] = normal[2];
            }
            const ScrVec3 b = get_vec(0x00922b20, end);
            const ScrVec3 a = get_vec(0x00922b20, start);
            const float speed = static_cast<float>(
                slot<long double XB_CDECL(const uint32_t*, const uint32_t*)>(0x00922268)(a.v, b.v) * 2);  // Scr_Distance * 2
            velocity[0] = bits(speed * as_float(velocity[0]));
            velocity[1] = bits(as_float(velocity[1]) * speed);
            velocity[2] = bits(as_float(velocity[2]) * speed);
            ScrVec3 origin = get_vec(0x00922b20, start);
            slot<void XB_CDECL(uint32_t, uint32_t*, uint32_t*, uint32_t)>(0x0092260c)(guy, origin.v, velocity,
                                                                                   fuse);  // GScr_MagicGrenadeManual
            Scr_Wait(as_float(0x3f800000));  // 1
        }
        Scr_WaitTillString(trigger, pointer<const char>(0x007fa3ec));  // "throw"
    }
}

void XB_CDECL BroScr_HelmetPop(uint32_t ent) {
    bool defined;
    if (!*ScrField_haee61f94_IsDefined(&defined, ent)) return;
    ScriptString bone;
    ScriptString_FromRep(&bone, nullptr);
    slot<void XB_CDECL(ScriptString*, ScriptString*, uint32_t)>(0x00922468)(&bone, key_string(ent, 0xaee61f94),
                                                                          0);  // GScr_GetModelBoneName
    ScrVec3 origin = get_vec(0x00922b20, ent);  // ScrEnt_GetOrigin
    origin.v[2] = bits(as_float(origin.v[2]) + title_global<float>(0x007e5bf0));  // + 64
    ScriptString classname;
    scr_string_from(&classname, 0x0083ab98);  // "script_model"
    uint32_t piece = slot<uint32_t XB_CDECL(ScriptString*, ScrVec3*, uint32_t)>(0x009222f0)(&classname, &origin, 0);  // GScr_Spawn
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(piece, key_string(ent, 0xaee61f94), 0);  // GScr_SetModel
    get_vec(0x00922b20, piece);  // ScrEnt_GetOrigin (unused)
    copy_tag(0x0092261c, 0x00922b24, ent, &bone, piece);  // tag origin -> ScrEnt_SetOrigin
    copy_tag(0x00922620, 0x00922ba4, ent, &bone, piece);  // tag angles -> ScrEnt_SetAngles
    tumble(ent, piece);
    if (*ScrField_h3bfcf651_IsDefined(&defined, ent)) {
        piece = slot<uint32_t XB_CDECL(ScriptString*, ScrVec3*, uint32_t)>(0x009222f0)(&classname, &origin, 0);  // GScr_Spawn
        slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t)>(0x00922548)(piece, key_string(ent, 0x3bfcf651), 0);  // GScr_SetModel
        {
            ScriptString tag;
            scr_string_from(&tag, 0x00825e98);  // "TAG_HELMETSIDE"
            copy_tag(0x0092261c, 0x00922b24, ent, &tag, piece);
            scr_release(&tag);
        }
        {
            ScriptString tag;
            scr_string_from(&tag, 0x00825e98);
            copy_tag(0x00922620, 0x00922ba4, ent, &tag, piece);
            scr_release(&tag);
        }
        tumble(ent, piece);
    }
    Scr_Wait(as_float(0x3d4ccccd));  // 0.05
    if (*ScrField_h3bfcf651_IsDefined(&defined, ent)) {
        ScriptString tag;
        scr_string_from(&tag, 0x00825e98);  // "TAG_HELMETSIDE"
        slot<AttachSlot>(0x009224d8)(ent, key_string(ent, 0x3bfcf651), &tag);  // GScr_Detach slot
        scr_release(&tag);
        remove_key(&ent, 0x3bfcf651);
    }
    {
        ScriptString tag;
        scr_string_from(&tag, 0x0082dfeb);  // ""
        slot<AttachSlot>(0x009224d8)(ent, key_string(ent, 0xaee61f94), &tag);
        scr_release(&tag);
    }
    remove_key(&ent, 0xaee61f94);
    scr_release(&classname);
    scr_release(&bone);
}

namespace {

// The shared shooting loop of BroScr_ShootAtTarget / _2: per shot, unless the nearest enemy is past
// the anim object's +0x9c (or the mode is "forceShoot"), with ammo (int 0x0feddbea) and the target
// within 20 degrees: 0.05 s, the aim point (target eye / origin, plus the offset field 0x7aeb941a with
// int 0x33a4ea6c set; the offset alone without a target), ActorShootAt / ActorShoot, the enemy
// distance (500 when undefined) kept, the yaw re-read; `end_time` passed ends the shots. `second` (the
// _2 form): the wait is for the notify 0x1973c1fe (0x32aa25) and each shot takes one from the ammo.
inline void shoot_at_target_loop(uint32_t ent, int32_t numshots, ScriptString* mode, int32_t end_time, uint32_t target,
                                 uint32_t yaw, bool second) {
    if (numshots <= 0) return;
    int32_t i = 0;
    do {
        bool defined;
        bool shoot = false;
        if (*ScrField_ha3b8e90e_IsDefined(&defined, ent)) {
            const float distance = as_float(*float_slot(ent, 0xa3b8e90e));
            shoot = static_cast<float>(distance > at<float>(anim_object(), 0x9c) ? 1 : 0) != title_global<float>(0x0083eafc);
        }
        if (!shoot && !ScriptString_Equals(mode, pointer<const char>(0x007f63f0))) return;  // "forceShoot"
        if (!(static_cast<int32_t>(*int_slot(ent, 0x0feddbea)) > 0)) return;
        if (static_cast<float>(title_global<float>(0x0083eb5c) > as_float(yaw) ? 1 : 0) == title_global<float>(0x0083eafc))
            return;  // 20
        if (second)
            Scr_WaitTillArgs(ent, 0x1973c1fe, 0x0032aa25, 0, 0);
        else
            Scr_Wait(as_float(0x3d4ccccd));  // 0.05
        uint32_t point[3] = {bits(0.0f), bits(0.0f), bits(0.0f)};
        uint32_t have = undefined_int();
        if (entity_handle_is_valid(target)) {
            const uint32_t lane = undefined_lane();
            uint32_t v[3] = {lane, lane, lane};
            if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922220)(target))  // GScr_IsSentient
                slot<void XB_CDECL(uint32_t, uint32_t*)>(0x00922528)(target, v);  // GScr_GetEye
            else
                slot<void XB_CDECL(uint32_t, uint32_t*)>(0x00922524)(target, v);  // GScr_GetOrigin
            point[0] = v[0];
            point[1] = v[1];
            point[2] = v[2];
            have = 1;
        }
        if (*ScrField_h33a4ea6c_IsDefined(&defined, ent) && *int_slot(ent, 0x33a4ea6c) != 0) {
            if (have != undefined_int()) {
                const uint32_t* const offset = reinterpret_cast<const uint32_t*>(ScrField_h7aeb941a(ent));
                const float x = as_float(offset[0]) + as_float(point[0]);
                const float y = as_float(offset[1]) + as_float(point[1]);
                const float z = as_float(offset[2]) + as_float(point[2]);
                point[0] = bits(x);
                point[1] = bits(y);
                point[2] = bits(z);
            } else {
                const uint32_t* const offset = reinterpret_cast<const uint32_t*>(ScrField_h7aeb941a(ent));
                point[0] = offset[0];
                point[1] = offset[1];
                point[2] = offset[2];
            }
        }
        if (have != undefined_int())
            slot<void XB_CDECL(uint32_t, uint32_t, uint32_t*)>(0x00922698)(ent, 0x3f800000, point);  // GScr_ActorShootAt
        else
            slot<void XB_CDECL(uint32_t, uint32_t)>(0x0092269c)(ent, 0x3f800000);  // GScr_ActorShoot
        if (second) {
            const uint32_t ammo = *int_slot(ent, 0x0feddbea);
            *int_slot(ent, 0x0feddbea) = ammo - 1;
        }
        float distance = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922690)(ent));  // GScr_SentientNearestEnemyDist
        if (distance == title_global<float>(0x00922eac)) distance = title_global<float>(0x0083ebec);  // 500
        *float_slot(ent, 0xa3b8e90e) = bits(distance);
        {
            uint32_t out;
            yaw = *BroScr_GetAbsYawToTarget(&out, ent);
        }
        if (second) {
            const int32_t now = get_time();
            if (static_cast<uint32_t>(end_time) != undefined_int() && end_time < now) return;
        } else if (static_cast<uint32_t>(end_time) != undefined_int() && end_time < get_time()) {
            i = numshots;
        }
        ++i;
    } while (i < numshots);
}

}  // namespace

void XB_CDECL BroScr_ShootAtTarget(uint32_t ent, uint32_t end, int32_t numshots, ScriptString mode, int32_t end_time,
                                   uint32_t target) {
    {
        ScriptString message;
        scr_string_from(&message, 0x008260f0);  // "numshots must be > 0" (the assert compiled out)
        scr_release(&message);
    }
    end_on(ent, end);
    end_on(ent, kKillAnimScript);
    end_on(ent, kDeath);
    if (entity_handle_is_valid(target)) end_on(target, kDeath);
    uint32_t yaw;
    BroScr_GetAbsYawToTarget(&yaw, ent);
    shoot_at_target_loop(ent, numshots, &mode, end_time, target, yaw, false);
    queue_notify(ent, end);
    scr_release(&mode);
}

void XB_CDECL BroScr_ShootAtTarget_2(uint32_t ent, uint32_t end, int32_t numshots, ScriptString mode, int32_t end_time,
                                     uint32_t target) {
    {
        ScriptString message;
        scr_string_from(&message, 0x008260f0);  // "numshots must be > 0" (the assert compiled out)
        scr_release(&message);
    }
    end_on(ent, end);
    end_on(ent, kKillAnimScript);
    end_on(ent, kDeath);
    if (entity_handle_is_valid(target)) end_on(target, kDeath);
    Scr_WaitFrames(0);
    uint32_t yaw;
    BroScr_GetAbsYawToTarget(&yaw, ent);
    bool defined;
    if (*ScrField_h33a4ea6c_IsDefined(&defined, ent) && *int_slot(ent, 0x33a4ea6c) != 0) yaw = bits(0.0f);
    shoot_at_target_loop(ent, numshots, &mode, end_time, target, yaw, true);
    queue_notify(ent, end);
    scr_release(&mode);
}

namespace {

inline uint8_t* level_object() { return title_global<uint8_t*>(0x00922ef8); }  // g_levelScriptObject
inline uint8_t* bool_slot(uint32_t ent, uint32_t key) {
    ScriptKeyTable* const table = ent_script_object(ent);
    uint32_t* value = ScriptKeyTable_Find(table, key);
    if (!value) value = ScriptKeyTable_Set(table, key, title_global<uint8_t>(0x00922ed0));  // g_scrUndefinedBool
    return reinterpret_cast<uint8_t*>(value);
}

}  // namespace

void XB_CDECL BroScr_FriendlyWaveSpawn(uint32_t ent) {
    DynArray ai = {nullptr, 0, 0};
    DynArray spawners = {nullptr, 0, 0};
    uint32_t zone;
    {
        const uint32_t zones = at<uint32_t>(level_object(), 0x188);
        if (zones != undefined_int() && zones != 0)
            zone = slot<uint32_t XB_CDECL(ScrVec3)>(0x0092249c)(get_vec(0x00922b20, ent));  // GScr_GetZoneAtPoint(origin)
        else
            zone = 0;
    }
    if (at<uint32_t>(level_object(), 0x158) == undefined_int()) at<uint32_t>(level_object(), 0x158) = 7;  // the friendly count
    for (;;) {
        bool go = false;
        if (entity_handle_is_valid(at<uint32_t>(level_object(), 0x1d0))) {
            ScriptString target;
            go = ScrCall_ScrEnt_GetTarget(&at<uint32_t>(level_object(), 0x1d0), &target)->rep != nullptr;
            scr_release(&target);
        }
        if (go) {
            at<uint32_t>(level_object(), 0xf8) = 0;
            {
                ScriptString team;
                scr_string_from(&team, 0x00832084);  // "allies"
                slot<void XB_CDECL(DynArray*, ScriptString*)>(0x009223d0)(&ai, &team);  // GScr_GetAI
                scr_release(&team);
            }
            for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ai.size); ++i)
                if (*bool_slot(*element(&ai, i), 0xed6f4ccc)) ++at<uint32_t>(level_object(), 0xf8);
            if (entity_handle_is_valid(at<uint32_t>(level_object(), 0x1d0))) {
                while (static_cast<int32_t>(at<uint32_t>(level_object(), 0xf8)) <
                       static_cast<int32_t>(at<uint32_t>(level_object(), 0x158))) {
                    {
                        ScriptString target;
                        Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(
                                            ScrCall_ScrEnt_GetTarget(&at<uint32_t>(level_object(), 0x1d0), &target))),
                                        0x0d786b4c, reinterpret_cast<uint8_t*>(&spawners), 0);  // by targetname
                        scr_release(&target);
                    }
                    const uint32_t r = static_cast<uint32_t>(random_int(static_cast<int32_t>(spawners.size)));
                    set_count(*element(&spawners, r), 1);
                    const uint32_t guy = slot<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00922518)(*element(&spawners, r), zone);  // GScr_DoSpawn
                    set_count(*element(&spawners, r), 0);
                    if (!entity_handle_is_valid(guy)) break;
                    ++at<uint32_t>(level_object(), 0xf8);
                    {
                        ScriptString target;
                        ScriptString name;
                        ScriptString_ConcatCString(&name, ScrCall_ScrEnt_GetTarget(&at<uint32_t>(level_object(), 0x1d0), &target),
                                                   pointer<const char>(0x00814268));  // + "_spawned"
                        ScriptString copy;
                        ScriptString_Copy(&copy, &name);
                        slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922b54)(guy, copy);  // ScrEnt_SetTargetname
                        scr_release(&name);
                        scr_release(&target);
                    }
                    *bool_slot(guy, 0xed6f4ccc) = 1;
                    if (!at<uint8_t>(level_object(), 0x5c)) {
                        ScriptString group;
                        scr_string_from(&group, 0x0081351c);  // "friendly_wave"
                        ScriptString copy;
                        ScriptString_Copy(&copy, &group);
                        slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922c1c)(guy, copy);  // ScrEnt_SetGroupname
                        scr_release(&group);
                        slot<void XB_CDECL(uint32_t, uint32_t)>(0x0092268c)(guy, title_global<uint32_t>(0x009236dc));  // GScr_SetGoalEntity(player)
                    } else {
                        const uint32_t level = at<uint32_t>(level_object(), 4);
                        const uint32_t function = at<uint32_t>(level_object(), 0x18c);
                        uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
                        if (functor) {
                            functor[0] = 0x007f4a28;
                            functor[1] = function;
                            functor[2] = level;
                            functor[3] = guy;
                        }
                        create_thread(level, functor);
                    }
                    BroScr_SetCreditsName(guy, title_global<uint32_t>(0x00922ebc));
                    Scr_Wait(as_float(0x40000000));  // 2
                    if (!entity_handle_is_valid(at<uint32_t>(level_object(), 0x1d0))) break;
                }
            }
        }
        Scr_Wait(as_float(0x40000000));  // 2
    }
}

void XB_CDECL BroScr_TriggeredSpawnerThink(uint32_t spawner, uint32_t trigger) {
    uint32_t zone;
    {
        const uint32_t zones = at<uint32_t>(level_object(), 0x188);
        if (zones != undefined_int() && zones != 0)
            zone = slot<uint32_t XB_CDECL(ScrVec3)>(0x0092249c)(get_vec(0x00922b20, spawner));  // GScr_GetZoneAtPoint(origin)
        else
            zone = 0;
    }
    bool defined;
    if (*ScrField_hfbac8485_IsDefined(&defined, spawner)) return;
    *int_slot(spawner, 0xfbac8485) = 1;
    if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922b30)(spawner) != undefined_int())  // ScrEnt_GetSpawnflags
        slot<uint32_t XB_CDECL(uint32_t)>(0x00922b30)(spawner);                   // (unused)
    float extra = 0.0f;
    float step = title_global<float>(0x0083eb20);  // 1
    if (*ScrField_hce8a66f4_IsDefined(&defined, spawner)) step = as_float(*ScrField_hce8a66f4(spawner));
    const bool player_touch = entity_handle_is_valid(trigger) && *ScrField_ha7ad4a18_IsDefined(&defined, trigger);
    if (!(static_cast<int32_t>(get_count(spawner)) > 0)) return;
    do {
        do {
            if (*int_slot(spawner, 0x070b8a34) == 0) {
                Scr_WaitTill(spawner, 0xccd105e5);
                *int_slot(spawner, 0x070b8a34) = 1;
            }
            if (player_touch)
                while (!slot<bool XB_CDECL(uint32_t, uint32_t)>(0x00922530)(title_global<uint32_t>(0x009236dc), trigger))  // GScr_IsTouching(player)
                    Scr_Wait(as_float(0x3f000000));  // 0.5
        } while (*int_slot(spawner, 0x070b8a34) == 0);
        if (*ScrField_h6e62eedd_IsDefined(&defined, spawner)) Scr_Wait(as_float(*float_slot(spawner, 0x6e62eedd)));
        uint32_t guy;
        if (int_defined(spawner, 0x02d62a20) && *int_slot(spawner, 0x02d62a20) != 0)
            guy = slot<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00922520)(spawner, zone);  // GScr_ForceSpawn
        else
            guy = slot<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00922518)(spawner, zone);  // GScr_DoSpawn
        if (entity_handle_is_valid(guy)) {
            Scr_WaitTillString(guy, pointer<const char>(0x00831f60));  // "death"
            using DebugWait = void XB_CDECL(uint32_t, ScriptString, ScrVec3);
            if (entity_handle_is_valid(guy)) {
                const ScrVec3 origin = get_vec(0x00922b20, guy);  // ScrEnt_GetOrigin
                ScriptString text;
                scr_string_from(&text, 0x00825d34);  // "DIED"
                own_function<DebugWait>(&BroScr_DebugCvarWait)(spawner, text, origin);  // BroScr_DebugCvarWait
            } else {
                ScrVec3 zero;
                zero.v[0] = bits(0.0f);
                zero.v[1] = bits(0.0f);
                zero.v[2] = bits(0.0f);
                ScriptString text;
                scr_string_from(&text, 0x00825d2c);  // "DELETED"
                own_function<DebugWait>(&BroScr_DebugCvarWait)(spawner, text, zero);
            }
            if (*ScrField_script_delay_IsDefined(&defined, spawner)) {
                Scr_Wait(as_float(*float_slot(spawner, 0x807116c4)));
            } else if (*ScrField_script_delay_min_IsDefined(&defined, spawner) &&
                       *ScrField_script_delay_max_IsDefined(&defined, spawner)) {
                const float max = as_float(*ScrField_script_delay_max(spawner));
                const float min = as_float(*ScrField_script_delay_min(spawner));
                const uint32_t* const base = ScrField_script_delay_min(spawner);
                const long double random = slot<long double XB_CDECL(float)>(0x00922230)(max - min);  // Scr_RandomFloat
                Scr_Wait(static_cast<float>(as_float(*base) + random));
            } else {
                Scr_Wait(static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922230)(0x40800000) +
                                            title_global<float>(0x0083ece8)));  // RandomFloat(4) + 5
            }
            Scr_Wait(extra);
            extra = step + extra;
        } else {
            Scr_Wait(as_float(0x40000000));  // 2
        }
    } while (static_cast<int32_t>(get_count(spawner)) > 0);
}

}  // namespace game
