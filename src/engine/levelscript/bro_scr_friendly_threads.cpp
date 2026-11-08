#include "bro_scr_friendly_threads.h"

#include "bro_scr_ai_anim.h"
#include "bro_scr_ai_common.h"
#include "bro_scr_ai_weapons.h"
#include "bro_scr_common.h"
#include "g_scr_runtime.h"
#include "g_script.h"
#include "qcommon_core.h"
#include "scr_access.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "script_string.h"
#include "title_data.h"
#include "bro_scr_note_handlers.h"
#include "levels_twins.h"

namespace game {

using namespace bro_ai;

namespace {

constexpr uint32_t kDeath = 0x06730ae8;

using GetNode = uint32_t XB_CDECL(ScriptString*, ScriptString*, uint32_t, uint32_t);
using CreateThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);

inline void end_on(uint32_t ent, uint32_t hash) { slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(ent, hash); }  // Scr_EndOn
inline uint32_t* alloc(uint32_t bytes) { return slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(bytes); }  // ScrIface_Alloc
inline void create_thread(uint32_t ent, uint32_t* functor) {
    slot<CreateThread>(0x0092216c)(0, 0, 0, ent, functor, 0);  // Scr_CreateThread
}
inline uint32_t* ent_functor(uint32_t function, uint32_t ent) {
    uint32_t* const functor = alloc(0xc);
    if (functor) {
        functor[0] = 0x007f5ba0;
        functor[1] = function;
        functor[2] = ent;
    }
    return functor;
}
inline uint32_t level_handle() {
    const uint32_t object = title_global<uint32_t>(0x00922ef8);  // g_levelScriptObject
    return *pointer<uint32_t>(object ? object + 4 : 0);
}
inline bool is_valid_node(uint32_t node) {
    return node != 0 && node != 0xffffffff && slot<uint32_t XB_CDECL(uint32_t)>(0x00922224)(node) != 0;  // GScr_IsValidHandle
}
inline long double node_radius(uint32_t node) {
    return slot<long double XB_CDECL(uint32_t)>(0x00922ac8)(node);  // ScrPathNode_GetRadius
}
inline void set_goal_radius(uint32_t ent, uint32_t radius) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922df4)(ent, radius);  // ScrSentient_SetFloat_18
}
// The compiled `if (node.radius > 0)`: the bool as a float against 0.
inline bool radius_positive(uint32_t node) {
    const float flag = static_cast<float>(node_radius(node) > title_global<float>(0x0083eafc) ? 1 : 0);
    return !(flag == title_global<float>(0x0083eafc));
}

inline uint32_t hash_name(const char* name) {
    uint32_t hash;
    return *Scr_HashName(&hash, name);
}
inline void set_ent_var(uint32_t ent, uint32_t var, uint32_t value) {
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x009221bc)(ent, var, value);  // GScr_SetEntVar
}
inline uint8_t* level_object() { return title_global<uint8_t*>(0x00922ef8); }  // g_levelScriptObject
inline uint32_t player() { return title_global<uint32_t>(0x009236dc); }
// The level string at +0x1c4 is "pavlov" and `attacker` a "misc_turret".
inline bool pavlov_turret(uint32_t* attacker) {
    if (!ScriptString_Equals(reinterpret_cast<ScriptString*>(level_object() + 0x1c4), pointer<const char>(0x00826074)))
        return false;  // "pavlov"
    ScriptString name;
    const bool turret = ScriptString_Equals(ScrCall_ScrEnt_GetClassname(attacker, &name), pointer<const char>(0x00839e8c));  // "misc_turret"
    scr_release(&name);
    return turret;
}
// The ent's script_noteworthy equals `text` (an empty one: false).
inline bool noteworthy_is(uint32_t* ent, uint32_t text) {
    ScriptString outer;
    bool equal = false;
    if (*reinterpret_cast<uint32_t*>(ScrCall_ScrEnt_GetScriptNoteworthy(ent, &outer)) != 0) {
        ScriptString inner;
        equal = ScriptString_Equals(ScrCall_ScrEnt_GetScriptNoteworthy(ent, &inner), pointer<const char>(text));
        scr_release(&inner);
    }
    scr_release(&outer);
    return equal;
}
inline bool team_is(uint32_t* ent, uint32_t text) {
    ScriptString team;
    const bool equal = ScriptString_Equals(ScrCall_ScrSentient_GetTeam(ent, &team), pointer<const char>(text));
    scr_release(&team);
    return equal;
}

using GetArray = DynArray* XB_THISCALL(ScriptKeyTable*, uint32_t);
inline DynArray* get_array(uint32_t ent, uint32_t key) {
    return own_function<GetArray>(&ScriptKeyTable_GetArray)(ent_script_object(ent), key);  // ScriptKeyTable_GetArray
}
// The inlined element access of a script array: &data[i], resized first when i is past the count.
inline uint32_t* element(DynArray* array, uint32_t i) {
    if (i >= array->size) DynArray_Resize(reinterpret_cast<uint8_t*>(array), i + 1, i + 4);
    return &array->data[i];
}
inline void origin_of(uint32_t* out, uint32_t ent) {
    uint32_t buffer[3];
    const uint32_t* o = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
    out[0] = o[0];
    out[1] = o[1];
    out[2] = o[2];
}
inline void set_goal_pos(uint32_t ent, const uint32_t* pos) {
    slot<void XB_CDECL(uint32_t, const uint32_t*)>(0x00922684)(ent, pos);  // GScr_SetGoalPos
}
// The compiled `if (value > limit)` on a script int: the bool as a float against 0.
inline bool int_above(uint32_t value, uint32_t limit) {
    const float flag = static_cast<float>(static_cast<float>(static_cast<int32_t>(value)) > title_global<float>(limit) ? 1 : 0);
    return !(flag == title_global<float>(0x0083eafc));
}

using At = uint32_t* XB_THISCALL(DynArray*, uint32_t);  // FUN_0045ceb0: &data[i], grown first
inline uint32_t at_index(DynArray* array, uint32_t i) { return *own_function<At>(&DynArray_AtInt0)(array, i); }
// A bool field's byte, added as the undefined bool (the original's word keeps stale stack bytes above it).
inline uint8_t* bool_slot(uint32_t ent, uint32_t key) {
    return reinterpret_cast<uint8_t*>(key_slot(ent, key, title_global<uint8_t>(0x00922ed0)));
}
using GetEntArray = uint32_t XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t);
// The node's target as an entity (GScr_GetEntArray by targetname, first match).
inline uint32_t node_target_ent(uint32_t* node) {
    ScriptString target;
    const uint32_t ent = slot<GetEntArray>(0x009221c4)(ScrCall_ScrPathNode_GetTarget(node, &target), 0x0d786b4c, 0, 0, 1);
    scr_release(&target);
    return ent;
}
inline bool node_has_target(uint32_t* node) {
    ScriptString target;
    const bool has = *reinterpret_cast<uint32_t*>(ScrCall_ScrPathNode_GetTarget(node, &target)) != 0;
    scr_release(&target);
    return has;
}
// A valid "misc_mg42" or "misc_turret".
inline bool is_mg(uint32_t* turret) {
    if (!entity_handle_is_valid(*turret)) return false;
    ScriptString outer;
    bool mg = true;
    if (!ScriptString_Equals(ScrCall_ScrEnt_GetClassname(turret, &outer), pointer<const char>(0x00839e80))) {  // "misc_mg42"
        ScriptString inner;
        mg = ScriptString_Equals(ScrCall_ScrEnt_GetClassname(turret, &inner), pointer<const char>(0x00839e8c));  // "misc_turret"
        scr_release(&inner);
    }
    scr_release(&outer);
    return mg;
}

inline void begin_thread(uint32_t ent, uint32_t* functor) {
    slot<CreateThread>(0x00922168)(0, 0, 0, ent, functor, 0);  // Scr_BeginThread
    if (slot<uint8_t XB_CDECL()>(0x0092218c)()) Scr_WaitFrames(0);  // Scr_InThread
}
inline ScriptString* string_slot(uint32_t ent, uint32_t key) {
    return title_function<ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t)>(0x00458da0)(ent_script_object(ent),
                                                                                            key);  // ScriptKeyTable_GetString
}

using CallEntHash = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*);
using Anim6 = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
inline void call_ent_hash(uint32_t callback, uint32_t ent, uint32_t hash) {
    title_function<CallEntHash>(0x004b08e0)(0, 0, 0, 0, callback, ent, &hash);  // Scr_CallThread_ent_hash
}
inline void set_orient_mode(uint32_t ent, uint32_t text) {
    ScriptString mode;
    scr_string_from(&mode, text);
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t)>(0x00922740)(ent, &mode, 0, 0);  // GScr_ActorSetOrientMode
    scr_release(&mode);
}
// The prone aim weight from the yaw difference, clamped to [0x83eb94, 0x83ed08].
inline float prone_weight(float w) {
    if (title_global<float>(0x0083eb94) > w)
        w = title_global<float>(0x0083eb94);
    else if (w > title_global<float>(0x0083ed08))
        w = title_global<float>(0x0083ed08);
    return w;
}
// Prone straight at 1 - w, `side` at w, `other` at 0.01 (blend 0.1, rate 1).
inline void prone_blend(uint32_t ent, float w, uint32_t side, uint32_t other) {
    const float rest = title_global<float>(0x0083eb20) - w;
    slot<Anim6>(0x009225d8)(ent, title_global<uint32_t>(0x008cf1e0), bits(rest), 0x3dcccccd, 0x3f800000, 0);  // GScr_SetAnimKnob(prone_straight)
    slot<Anim6>(0x009225dc)(ent, title_global<uint32_t>(side), bits(w), 0x3dcccccd, 0x3f800000, 0);  // GScr_SetAnim
    slot<Anim6>(0x009225dc)(ent, title_global<uint32_t>(other), 0x3c23d70a, 0x3dcccccd, 0x3f800000, 0);
}
// The prone turn anim (flagged 0x59c142f9) with its note tracks, then the prone aim weights.
inline void prone_turn(uint32_t ent, uint32_t anim) {
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x009225e0)(
        ent, 0x59c142f9, title_global<uint32_t>(anim), 0x3f800000, 0x3dcccccd, 0x3f800000, 1);  // GScr_SetFlaggedAnimKnob
    {
        ScriptString empty;
        ScriptString_Copy(&empty, pointer<ScriptString>(0x00922e90));
        ScriptString result;
        using HandleNoteTracks = ScriptString* XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, ScriptString);
        own_function<HandleNoteTracks>(&BroScr_HandleNoteTracks)(&result, ent, 0x59c142f9, 0, empty);  // BroScr_HandleNoteTracks
        scr_release(&result);
    }
    slot<Anim6>(0x009226bc)(ent, title_global<uint32_t>(0x008cf1d4), title_global<uint32_t>(0x008cf1c8), 0x3f800000, 0x3dcccccd,
                            0x3f800000);  // GScr_ActorSetAimAnimWeights(prone_shootfeet 45 up / down)
}
// The notify / timeout pair: `on_notify`(ent, `notify`, done, done) and `after`(ent, delay, done, done)
// threads, then the wait for `done`.
inline void notify_or_timeout(uint32_t ent, uint32_t on_notify, uint32_t notify, uint32_t after, float delay, uint32_t done) {
    uint32_t* functor = alloc(0x18);
    if (functor) {
        functor[0] = 0x007ffb3c;
        functor[1] = on_notify;
        functor[2] = ent;
        functor[3] = notify;
        functor[4] = done;
        functor[5] = done;
    }
    begin_thread(ent, functor);
    functor = alloc(0x18);
    if (functor) {
        functor[0] = 0x00825c2c;
        functor[1] = after;
        functor[2] = ent;
        functor[3] = bits(delay);
        functor[4] = done;
        functor[5] = done;
    }
    begin_thread(ent, functor);
    Scr_WaitTill(ent, done);
}
// A compiled script condition: the bool as a float against 0 (0x0083eafc).
inline bool script_true(bool b) { return !(static_cast<float>(b ? 1 : 0) == title_global<float>(0x0083eafc)); }
inline uint32_t* angles_of(uint32_t* out, uint32_t ent) {
    uint32_t buffer[3];
    const uint32_t* a = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922ba0)(buffer, ent);  // ScrEnt_GetAngles
    out[0] = a[0];
    out[1] = a[1];
    out[2] = a[2];
    return out;
}

inline uint32_t* float_slot(uint32_t ent, uint32_t key) { return key_slot(ent, key, undefined_float()); }
inline long double sentient_radius(uint32_t ent) {
    return slot<long double XB_CDECL(uint32_t)>(0x00922df0)(ent);  // ScrSentient_GetFloat_18
}
inline long double distance(const uint32_t* a, const uint32_t* b) {
    return slot<long double XB_CDECL(const uint32_t*, const uint32_t*)>(0x00922268)(a, b);  // Scr_Distance
}
inline void node_origin(uint32_t* out, uint32_t node) {
    uint32_t buffer[3];
    const uint32_t* o = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922ab8)(buffer, node);  // ScrPathNode_GetOrigin
    out[0] = o[0];
    out[1] = o[1];
    out[2] = o[2];
}
// Distance from the ent to the node.
inline long double ent_node_distance(uint32_t ent, uint32_t node) {
    uint32_t at_node[3];
    node_origin(at_node, node);
    uint32_t at_ent[3];
    origin_of(at_ent, ent);
    return distance(at_ent, at_node);
}
inline void use_turret(uint32_t ent, uint32_t turret) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x0092272c)(ent, turret);  // GScr_ActorUseTurret
}

// The inlined erase of element `index` (elements after it moved down one, the count decremented).
inline void erase_at(DynArray* array, uint32_t index) {
    if (index >= array->size) return;
    uint32_t* p = array->data;
    uint32_t* end = p + array->size;
    if (p == end) return;
    for (; index; --index)
        if (++p == end) return;
    --end;
    for (; p != end; ++p) p[0] = p[1];
    --array->size;
}
inline DynArray* level_array(uint32_t offset) { return reinterpret_cast<DynArray*>(level_object() + offset); }
// One pass over a level chain list: invalid chains dropped; for each idle chain (bool 0x14b95ada
// clear) dead / invalid AI dropped from its array 0x8969d6d6 (the rest get goal radius `radius`
// when given), then notify 0xce19d6b0 on the chain.
inline void hangar_chain_pass(uint32_t offset, bool set_radius) {
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(level_array(offset)->size); ++i) {
        if (!entity_handle_is_valid(*element(level_array(offset), i))) {
            erase_at(level_array(offset), i);
            --i;
            continue;
        }
        if (*bool_slot(*element(level_array(offset), i), 0x14b95ada) != 0) continue;
        for (uint32_t j = 0;;) {
            if (!(static_cast<int32_t>(j) < static_cast<int32_t>(get_array(*element(level_array(offset), i), 0x8969d6d6)->size))) break;
            if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922210)(*element(get_array(*element(level_array(offset), i), 0x8969d6d6), j)) &&  // GScr_IsAlive
                entity_handle_is_valid(*element(get_array(*element(level_array(offset), i), 0x8969d6d6), j))) {
                if (set_radius)
                    set_goal_radius(*element(get_array(*element(level_array(offset), i), 0x8969d6d6), j), 0x43000000);  // 128
                ++j;
            } else {
                erase_at(get_array(*element(level_array(offset), i), 0x8969d6d6), j);
            }
        }
        queue_notify(*element(level_array(offset), i), 0xce19d6b0);
    }
}

}  // namespace

void XB_CDECL BroScr_FallbackNodeThink(uint32_t ent) {
    end_on(ent, kDeath);
    uint32_t node;
    {
        ScriptString key;
        scr_string_from(&key, 0x0082dda8);  // "targetname"
        ScriptString target;
        node = slot<GetNode>(0x009221cc)(ScrCall_ScrEnt_GetTarget(&ent, &target), &key, 0, 0);  // GScr_GetNode
        if (node == 0xffffffff) node = 0;
        scr_release(&target);
        scr_release(&key);
    }
    *key_slot(ent, 0xa33a251d, 0) = node;
    slot<void XB_CDECL(uint32_t, uint32_t*)>(0x00922688)(ent, &node);  // GScr_SetGoalPosFromNode
    bool defined;
    if (*ScrField_h99ccf0cc_IsDefined(&defined, ent)) {
        uint32_t* const functor = alloc(0x10);
        if (functor) {
            functor[0] = 0x007f4aac;
            functor[1] = 0x0018bff0;  // BroScr_SetGoalRadiusFromNodeOnNotify
            functor[2] = ent;
            functor[3] = node;
        }
        create_thread(ent, functor);
    } else if (is_valid_node(node) && radius_positive(node)) {
        const float radius = static_cast<float>(node_radius(node));
        set_goal_radius(ent, bits(radius));
    } else {
        set_goal_radius(ent, 0x44000000);  // 512
    }
    for (;;) {
        Scr_WaitTillString(ent, pointer<const char>(0x00823550));
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922cbc)(ent, 0x41a00000);  // ScrActor_SetFloat_7b8(20)
        {
            const uint32_t level = level_handle();
            uint32_t* const functor = alloc(0x14);
            if (functor) {
                functor[0] = 0x007f4af4;
                functor[1] = 0x00181bc0;  // BroScr_NotifyFallbackReachedGoal
                functor[2] = level;
                functor[3] = ent;
                functor[4] = 0;
            }
            create_thread(level, functor);
        }
        bool debug;
        {
            ScriptString value;
            debug = ScriptString_Equals(ScrCall_GScr_GetCvar(&value, 0x00823550), pointer<const char>(0x0082e058));  // "fallback" == "1"
            scr_release(&value);
        }
        if (debug) create_thread(ent, ent_functor(0x0019a930, ent));  // BroScr_FallbackClearGoalWait_2
        bool more;
        {
            ScriptString target;
            more = *reinterpret_cast<uint32_t*>(ScrCall_ScrPathNode_GetTarget(&node, &target)) != 0;
            scr_release(&target);
        }
        if (!more) break;
        {
            ScriptString key;
            scr_string_from(&key, 0x0082dda8);  // "targetname"
            ScriptString target;
            node = slot<GetNode>(0x009221cc)(ScrCall_ScrPathNode_GetTarget(&node, &target), &key, 0, 0);  // GScr_GetNode
            if (node == 0xffffffff) node = 0;
            scr_release(&target);
            scr_release(&key);
        }
        *key_slot(ent, 0xa33a251d, 0) = node;
        const float old = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922d08)(ent));  // ScrActor_GetFloat_7e4
        uint32_t* const saved = int_slot(ent, 0x62271501);
        *saved = float_to_script_int(old);
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922d0c)(ent, 0x43fa0000);  // ScrActor_SetFloat_7e4(500)
        slot<void XB_CDECL(uint32_t, uint32_t*)>(0x00922688)(ent, &node);  // GScr_SetGoalPosFromNode
        create_thread(ent, ent_functor(0x0019a500, ent));  // BroScr_FallbackOnGoalRestore
        if (is_valid_node(node) && radius_positive(node)) {
            const float radius = static_cast<float>(node_radius(node));
            set_goal_radius(ent, bits(radius));
        }
    }
    ScriptString name;
    scr_string_from(&name, 0x00825d68);  // "fallback_arrived"
    ScriptString_AppendInt(&name, *int_slot(ent, 0x8bbe4822));
    const uint32_t object = title_global<uint32_t>(0x00922ef8);  // g_levelScriptObject
    const uint32_t* const level = pointer<uint32_t>(object ? object + 4 : 0);
    uint32_t hash;
    queue_notify(*level, *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&name)));
    scr_release(&name);
}

void XB_CDECL BroScr_SetupFriendlyFireMissionFail(uint32_t self, uint32_t ent, uint32_t mode, ScriptString team) {
    end_on(level_handle(), 0xa264707f);
    uint32_t attacker = 0;
    if (mode != undefined_int() && mode == 1) {
        if (!entity_handle_is_valid(ent)) goto done;
        if (*reinterpret_cast<uint32_t*>(&team) == 0) ScriptString_AssignCString(&team, pointer<const char>(0x00832084));  // "allies"
        title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t*)>(0x004b1590)(ent, kDeath, &attacker);  // Scr_WaitTillOutputEnt_ORAN
        if (!entity_handle_is_valid(attacker)) goto done;
        if (ScriptString_Equals(&team, pointer<const char>(0x00832084))) {  // "allies"
            if (attacker != player() && !pavlov_turret(&attacker)) goto done;
            bool defined;
            if ((*ScrField_h7599373d_IsDefined(&defined, ent) && *ScrField_h7599373d(ent) == 1) ||
                *reinterpret_cast<int32_t*>(level_object() + 0xdc) < 4) {
                using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
                title_function<StartThread>(0x0011a910)(0, 0, 0, 0, 0x00181f70, self);  // Scr_StartThreadFunctor1(BroScr_FriendlyFireMissionFail)
            } else {
                *reinterpret_cast<uint32_t*>(level_object() + 0xf4) = 0;
                *reinterpret_cast<uint32_t*>(level_object() + 0xdc) = 0;
            }
        } else if (ScriptString_Equals(&team, pointer<const char>(0x0083208c))) {  // "axis"
            if (attacker == player()) ++*reinterpret_cast<int32_t*>(level_object() + 0xdc);
            if (pavlov_turret(&attacker)) ++*reinterpret_cast<int32_t*>(level_object() + 0xdc);
        }
    } else if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922220)(ent) &&  // GScr_IsSentient
               slot<uint32_t XB_CDECL(uint32_t)>(0x00922210)(ent)) {  // GScr_IsAlive
        if (noteworthy_is(&ent, 0x00826060)) {  // "missionfail_death"
            set_ent_var(ent, 0xce270885, hash_name(pointer<const char>(0x00825d90)));  // KillFriends_Damage_Think
            set_ent_var(ent, kDeath, hash_name(pointer<const char>(0x007f5164)));  // KillFriends_MissionFail_OnDeath
        } else if (noteworthy_is(&ent, 0x0082604c)) {  // "missionfail_damage"
            set_ent_var(ent, 0xce270885, hash_name(pointer<const char>(0x00825d90)));  // KillFriends_Damage_Think
            set_ent_var(ent, 0xce270885, hash_name(pointer<const char>(0x0082601c)));  // KillFriends_MissionFail_OnDamage
        } else if (!noteworthy_is(&ent, 0x00826008)) {  // "missionfail_never"
            if (team_is(&ent, 0x0083208c)) {  // "axis"
                set_ent_var(ent, kDeath, hash_name(pointer<const char>(0x007f51b0)));  // Axis_Death
            } else if (team_is(&ent, 0x00832084)) {  // "allies"
                set_ent_var(ent, 0xce270885, hash_name(pointer<const char>(0x00825d90)));  // KillFriends_Damage_Think
                set_ent_var(ent, kDeath, hash_name(pointer<const char>(0x007f5194)));  // Allies_Death
            }
        }
    }
done:
    scr_release(&team);
}

void XB_CDECL BroScr_FriendlyChainThink(uint32_t ent, uint32_t chain) {
    const int32_t health = static_cast<int32_t>(slot<uint32_t XB_CDECL(uint32_t)>(0x00922b90)(ent));  // ScrEnt_GetHealth
    slot<void XB_CDECL(uint32_t, int32_t)>(0x00922b94)(
        ent, static_cast<int32_t>(static_cast<float>(health) * title_global<float>(0x0083eaf4)));  // ScrEnt_SetHealth
    end_on(ent, 0xb633d42e);
    end_on(ent, kDeath);
    bool defined;
    if (*ScrField_script_radius_IsDefined(&defined, *element(get_array(chain, 0x0605894e), 0)) &&
        int_above(*int_slot(*element(get_array(chain, 0x0605894e), 0), 0x2266f963), 0x007e5bf0)) {  // script_radius > 64
        // The script's radius reads, results unused.
        ScrField_script_radius(*element(get_array(chain, 0x0605894e), 0));
        ScrField_script_radius(*element(get_array(chain, 0x0605894e), 0));
    }
    int32_t free_count = 0;
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(get_array(chain, 0x8969d6d6)->size); ++i) {
        if (!entity_handle_is_valid(*element(get_array(chain, 0x8969d6d6), i))) continue;
        if (!slot<uint32_t XB_CDECL(uint32_t)>(0x00922210)(*element(get_array(chain, 0x8969d6d6), i))) continue;  // GScr_IsAlive
        if (*int_slot(*element(get_array(chain, 0x8969d6d6), i), 0x94ce2dcb) == 0) ++free_count;
    }
    uint32_t goal[3];
    if (!(free_count < title_global<int32_t>(0x008cd778)) && get_array(chain, 0x0605894e)->size != 0) {
        const uint32_t index = static_cast<uint32_t>(random_int(static_cast<int32_t>(get_array(chain, 0x0605894e)->size - 1)) + 1);
        *int_slot(ent, 0x94ce2dcb) = index;
        origin_of(goal, *element(get_array(chain, 0x0605894e), index));
    } else {
        *int_slot(ent, 0x94ce2dcb) = 0;
        origin_of(goal, *element(get_array(chain, 0x0605894e), 0));
    }
    set_goal_pos(ent, goal);
    Scr_WaitTill(ent, 0x0032304c);
    uint32_t node = slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x009226ec)(ent, title_global<uint32_t>(0x008cd76c), 1);
    if (node == 0xffffffff) node = 0;
    slot<void XB_CDECL(uint32_t, uint32_t*)>(0x00922688)(ent, &node);  // GScr_SetGoalPosFromNode
}

void XB_CDECL BroScr_GoToBalconyNode(uint32_t ent, uint32_t node) {
    if (*bool_slot(ent, 0xa287dc52) != 0) return;
    end_on(ent, kDeath);
    Scr_EndOnString(ent, pointer<const char>(0x008263f8));  // "stop_going_to_node"
    DynArray nodes = {};
    if (node == 0 || node == 0xffffffff) {
        {
            ScriptString key;
            scr_string_from(&key, 0x0082dda8);  // "targetname"
            ScriptString target;
            Scr_GetNodeArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ScrCall_ScrEnt_GetTarget(&ent, &target))),
                             static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&key)), reinterpret_cast<uint32_t*>(&nodes));
            scr_release(&target);
            scr_release(&key);
        }
        bool defined;
        if (*ScrField_script_random_IsDefined(&defined, ent)) {
            if (static_cast<int32_t>(nodes.size) > 1)
                node = at_index(&nodes, static_cast<uint32_t>(random_int(static_cast<int32_t>(nodes.size))));
            else
                node = at_index(&nodes, 0);
        } else {
            if (static_cast<int32_t>(nodes.size) <= 0) goto done;
            ++*reinterpret_cast<int32_t*>(level_object() + 0x198);
            while (*reinterpret_cast<int32_t*>(level_object() + 0x198) >= static_cast<int32_t>(nodes.size))
                *reinterpret_cast<int32_t*>(level_object() + 0x198) -= static_cast<int32_t>(nodes.size);
            if (*reinterpret_cast<int32_t*>(level_object() + 0x198) < 0) *reinterpret_cast<int32_t*>(level_object() + 0x198) = 0;
            node = at_index(&nodes, *reinterpret_cast<uint32_t*>(level_object() + 0x198));
        }
    }
    if (node_has_target(&node)) {
        uint32_t turret = node_target_ent(&node);
        if (is_mg(&turret)) {
            using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*);
            title_function<StartThread>(0x0045a6d0)(0, 0, 0, 0, 0x0019a400, turret, &ent);  // Scr_StartThread_ent_ent(BroScr_MgWaitNewUser)
        }
    }
    {
        bool defined;
        if (*ScrField_script_balcony_IsDefined(&defined, ent) && *ScrField_script_balcony(ent) != 0)
            *ScrField_balcony_node(ent) = node;
    }
    slot<void XB_CDECL(uint32_t, uint32_t*)>(0x00922688)(ent, &node);  // GScr_SetGoalPosFromNode
    {
        bool defined;
        if (*ScrField_h73623f0d_IsDefined(&defined, ent) && *ScrField_h73623f0d(ent) != 0) {
            using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
            title_function<StartThread>(0x0011a910)(0, 0, 0, 0, 0x00181b70, ent);  // Scr_StartThreadFunctor1(BroScr_SetAIFlagsUntilNotify)
        }
    }
    {
        bool defined;
        if (*ScrField_h99ccf0cc_IsDefined(&defined, ent)) {
            set_goal_radius(ent, 0);
            Scr_WaitTill(ent, 0x0032304c);
        }
    }
    if (static_cast<float>(node_radius(node)) != title_global<float>(0x0083eafc)) {
        const float radius = static_cast<float>(node_radius(node));
        set_goal_radius(ent, bits(radius));
    } else {
        bool defined;
        if (!*ScrField_script_radius_IsDefined(&defined, ent)) set_goal_radius(ent, 0x44000000);  // 512
    }
    if (node_has_target(&node)) {
        uint32_t turret = node_target_ent(&node);
        if (is_mg(&turret)) {
            slot<void XB_CDECL(uint32_t, uint32_t*)>(0x00922688)(ent, &node);  // GScr_SetGoalPosFromNode
            set_goal_radius(ent, 0x40800000);  // 4
            Scr_WaitTillString(ent, pointer<const char>(0x0083ad18));  // "goal"
            BroScr_UseNodeTurret(ent, turret, title_global<uint32_t>(0x00922ea8));
        }
    }
done:
    if (nodes.data) Scr_Free(nodes.data);
}

void XB_CDECL BroScr_AnimScriptEntSetDefaults(uint32_t ent) {
    *bool_slot(ent, 0x602384d4) = 0;
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922dc4)(ent, 0x070b89a4);  // ScrActor_SetInt_184
    *handle_slot(ent, 0x0ccee5c1) = 0x00385d90;
    *handle_slot(ent, 0xbfc60f36) = 0x0036b1a5;
    *handle_slot(ent, 0x175567a8) = 0x0036b1a5;
    *handle_slot(ent, 0x1de0c645) = 0x0036b1a5;
    *int_slot(ent, 0x77f1e6fa) = 0xffffffff;
    BroScr_PutGunInHand(ent, 0x07151174);
    *int_slot(ent, 0xc786fa48) = 0;
    {
        const int32_t now = get_time();
        *int_slot(ent, 0x900ed884) = static_cast<uint32_t>(now);
    }
    *handle_slot(ent, 0x2181000b) = 0x00353554;
    *handle_slot(ent, 0x764918ae) = 0xc028584c;
    *int_slot(ent, 0x32222d6e) = 0;
    *int_slot(ent, 0x8ef2ad7c) = 0;
    {
        const int32_t now = get_time();
        *int_slot(ent, 0x32292cf8) = static_cast<uint32_t>(now);
    }
    *handle_slot(ent, 0xfa79d674) = 0x0769ccd2;
    ScriptString_AssignCString(string_slot(ent, 0x28ce30a6), pointer<const char>(0x0082dfeb));
    *key_slot(ent, 0xe057503b, 0) = 0;
    *bool_slot(ent, 0x552cfcab) = 0;
    BroScr_GetCombatRunAnim(ent);
    ScriptString_Equals(string_slot(ent, 0x28ce30a6), pointer<const char>(0x00826424));  // "dogshit", result unused
    begin_thread(ent, ent_functor(0x001ac460, ent));  // BroScr_HeadLookMainLoop_Thunk
    if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922d30)(ent) != undefined_handle() &&  // ScrActor_GetInt_f0
        slot<uint32_t XB_CDECL(uint32_t)>(0x00922d30)(ent) == 0)
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922d34)(ent, 0x0036b1a5);  // ScrActor_SetInt_f0
    BroScr_RefillClip(ent);
    uint32_t type;
    if (*BroScr_GetWeaponFireType(&type, ent) == 0x00315aef) {
        *reinterpret_cast<float*>(ScrField_h01757a0e(ent)) = title_global<float>(0x0083ef10);
    } else if (*BroScr_GetWeaponFireType(&type, ent) == 0x003819c9) {
        *reinterpret_cast<float*>(ScrField_h01757a0e(ent)) = title_global<float>(0x0083f1f8);
    } else {
        *reinterpret_cast<float*>(ScrField_h01757a0e(ent)) = 0.0f;
    }
    bool axis;
    {
        ScriptString team;
        axis = ScriptString_Equals(ScrCall_ScrSentient_GetTeam(&ent, &team), pointer<const char>(0x0083208c));  // "axis"
        scr_release(&team);
    }
    if (axis) {
        float* const value = reinterpret_cast<float*>(ScrField_h01757a0e(ent));
        *value = *value * title_global<float>(0x0083eba8);
    }
    *int_slot(ent, 0x77749be9) = 0xfff55d77;
}

void XB_CDECL BroScr_TurnToEnemy(uint32_t ent, uint32_t callback, uint32_t end) {
    end_on(ent, 0x07b4e7f7);
    end_on(ent, kDeath);
    end_on(ent, end);
    Scr_WaitTill(ent, 0xa6f66bfd);
    *int_slot(ent, 0xb66101f1) = 0;
    if (callback) call_ent_hash(callback, ent, 0xfd17b5c8);
    const float delay = title_global<float>(0x007c6490);
    for (;;) {
        if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922dc0)(ent) != 0x0738b1a5) {  // ScrActor_GetInt_184: not prone
            set_orient_mode(ent, 0x0083addc);  // "face default"
        } else {
            set_orient_mode(ent, 0x0083adec);  // "face current"
            uint32_t yaw;
            BroScr_GetYawToEnemy(&yaw, ent);
            uint32_t angles[3];
            angles_of(angles, ent);
            int32_t diff = (static_cast<int32_t>(as_float(angles[1]) - as_float(yaw)) + 360) % 360;
            if (diff > 180) diff -= 360;
            if (diff > 0) {
                if (*int_slot(ent, 0xb66101f1) != 0)
                    prone_blend(ent, prone_weight(static_cast<float>(diff) * title_global<float>(0x00826430)), 0x008cf174,
                                0x008cf120);  // right / prone_left45
                if (diff > 45) {
                    queue_notify(ent, 0xfd17b5c8);
                    BroScr_PutGunInHand(ent, 0x07151174);
                    prone_turn(ent, 0x008cf1f8);  // prone_turn_right
                    if (callback) call_ent_hash(callback, ent, 0xfd17b5c8);
                }
            } else {
                if (*int_slot(ent, 0xb66101f1) != 0)
                    prone_blend(ent, prone_weight(static_cast<float>(diff) * title_global<float>(0x0082642c)), 0x008cf120,
                                0x008cf174);  // prone_left45 / right
                if (diff < -45) {
                    queue_notify(ent, 0xfd17b5c8);
                    BroScr_PutGunInHand(ent, 0x07151174);
                    prone_turn(ent, 0x008cf1ec);  // prone_turn_left
                    if (callback) call_ent_hash(callback, ent, 0xfd17b5c8);
                }
            }
        }
        notify_or_timeout(ent, 0x00183810, 0xefd4ee43, 0x00183860, delay, 0x7fa61cea);  // BroScr_NotifyOnNotify / NotifyAfterTime
    }
}

void XB_CDECL BroScr_TurnToEnemy_2(uint32_t ent, uint32_t callback, uint32_t end) {
    end_on(ent, 0x07b4e7f7);
    end_on(ent, kDeath);
    end_on(ent, end);
    *int_slot(ent, 0xb66101f1) = 0;
    if (callback) call_ent_hash(callback, ent, 0x1c417c3e);
    const float delay = title_global<float>(0x007c6490);
    for (;;) {
        if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922dc0)(ent) != 0x0738b1a5) {  // ScrActor_GetInt_184: not prone
            set_orient_mode(ent, 0x0083addc);  // "face default"
        } else {
            set_orient_mode(ent, 0x0083adec);  // "face current"
            uint32_t yaw;
            BroScr_GetYawToEnemy(&yaw, ent);
            uint32_t angles[3];
            angles_of(angles, ent);
            float diff = static_cast<float>(
                static_cast<int32_t>(as_float(angles[1]) - as_float(yaw) + title_global<float>(0x0083ebcc)) % 360);  // + 360
            if (diff > title_global<float>(0x0083eb60)) diff = diff - title_global<float>(0x0083ebcc);  // > 180: - 360
            if (diff > 0.0f) {
                if (*int_slot(ent, 0xb66101f1) != 0)
                    prone_blend(ent, prone_weight(diff * title_global<float>(0x00826430)), 0x008cf174, 0x008cf120);
                if (script_true(diff > title_global<float>(0x0083ef1c))) {  // 45
                    queue_notify(ent, 0x1c417c3e);
                    prone_turn(ent, 0x008cf1f8);  // prone_turn_right
                    if (callback) call_ent_hash(callback, ent, 0x1c417c3e);
                }
            } else {
                if (*int_slot(ent, 0xb66101f1) != 0)
                    prone_blend(ent, prone_weight(diff * title_global<float>(0x0082642c)), 0x008cf120, 0x008cf174);
                if (script_true(title_global<float>(0x0083ef18) > diff)) {  // -45
                    queue_notify(ent, 0x1c417c3e);
                    prone_turn(ent, 0x008cf1ec);  // prone_turn_left
                    if (callback) call_ent_hash(callback, ent, 0x1c417c3e);
                }
            }
        }
        notify_or_timeout(ent, 0x00183ea0, 0x31e22e08, 0x00183ef0, delay, 0xf3e6fbcb);  // BroScr_NotifyOnNotify_2 / NotifyAfterTime_2
    }
}

void XB_CDECL BroScr_FriendlyUseMg42(uint32_t ent, uint32_t turret, uint32_t node) {
    Scr_EndOnString(ent, pointer<const char>(0x00831f60));  // "death"
    Scr_EndOnString(turret, pointer<const char>(0x00825b18));  // "friendly_finished_using_mg42"
    {
        const uint32_t level = level_handle();
        uint32_t* const functor = alloc(0x14);
        if (functor) {
            functor[0] = 0x007f4a10;
            functor[1] = 0x00181df0;  // BroScr_NotifyMg42FinishedOnDeath
            functor[2] = level;
            functor[3] = ent;
            functor[4] = turret;
        }
        create_thread(level, functor);
    }
    {
        const float radius = static_cast<float>(sentient_radius(ent));
        *float_slot(ent, 0xe223c6aa) = bits(radius);
    }
    set_goal_radius(ent, 0x41e00000);  // 28
    create_thread(ent, ent_functor(0x0019b0d0, ent));  // BroScr_RestoreGoalRadiusOnGoal
    slot<void XB_CDECL(uint32_t, uint32_t*)>(0x00922688)(ent, &node);  // GScr_SetGoalPosFromNode
    {
        const uint32_t kept = slot<uint32_t XB_CDECL(uint32_t)>(0x00922d20)(ent);  // ScrActor_GetInt_1308
        *int_slot(ent, 0xac91e5fb) = kept;
    }
    {
        const float old = static_cast<float>(slot<long double XB_CDECL(uint32_t)>(0x00922d08)(ent));  // ScrActor_GetFloat_7e4
        uint32_t* const saved = int_slot(ent, 0x62271501);
        *saved = float_to_script_int(old);
    }
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922d24)(ent, 0);  // ScrActor_SetInt_1308
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922d0c)(ent, 0x47435000);  // ScrActor_SetFloat_7e4(50000)
    Scr_WaitTill(ent, 0x0032304c);
    set_goal_radius(ent, *float_slot(ent, 0xe223c6aa));
    if (script_true(title_global<float>(0x0083eeac) > sentient_radius(ent))) set_goal_radius(ent, 0x43c80000);  // 400
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922d24)(ent, *int_slot(ent, 0xac91e5fb));  // ScrActor_SetInt_1308
    {
        const float old = static_cast<float>(static_cast<int32_t>(*int_slot(ent, 0x62271501)));
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922d0c)(ent, bits(old));  // ScrActor_SetFloat_7e4
    }
    set_goal_radius(ent, *float_slot(ent, 0xe223c6aa));
    {
        uint32_t at_node[3];
        node_origin(at_node, node);
        uint32_t buffer[3] = {undefined_lane(), undefined_lane(), undefined_lane()};
        slot<void XB_CDECL(uint32_t, uint32_t*)>(0x00922524)(player(), buffer);  // GScr_GetOrigin
        const uint32_t at_player[3] = {buffer[0], buffer[1], buffer[2]};
        if (title_global<float>(0x0083eeac) > distance(at_player, at_node)) {
            queue_notify(turret, hash_name(pointer<const char>(0x00825b18)));  // "friendly_finished_using_mg42"
            return;
        }
    }
    *key_slot(ent, 0xaf6fd78f, 0) = turret;
    uint32_t* functor = alloc(0x10);
    if (functor) {
        functor[0] = 0x007f4a28;
        functor[1] = 0x00181e30;  // BroScr_UseAiOnMg42Hint
        functor[2] = ent;
        functor[3] = turret;
    }
    create_thread(ent, functor);
    functor = alloc(0x10);
    if (functor) {
        functor[0] = 0x007f4a28;
        functor[1] = 0x001a6fe0;  // BroScr_WaitFriendlyFinishedMG42
        functor[2] = ent;
        functor[3] = turret;
    }
    create_thread(ent, functor);
    use_turret(ent, turret);
    bool has_target;
    {
        ScriptString target;
        has_target = *reinterpret_cast<uint32_t*>(ScrCall_ScrEnt_GetTarget(&turret, &target)) != 0;
        scr_release(&target);
    }
    if (has_target) {
        uint32_t hint;
        {
            ScriptString target;
            hint = slot<GetEntArray>(0x009221c4)(ScrCall_ScrEnt_GetTarget(&turret, &target), 0x0d786b4c, 0, 0, 0);  // GScr_GetEntArray
            scr_release(&target);
        }
        if (entity_handle_is_valid(hint)) {
            using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                                  const uint32_t*);
            title_function<StartThread>(0x0045a620)(0, 0, 0, 0, 0x00181f20, hint, &turret,
                                                    &ent);  // Scr_StartThread_ent_ent_ent(BroScr_NotifyFinishedUsingMg42)
        }
    }
    if (title_global<float>(0x0083eeac) > ent_node_distance(ent, node)) {
        do {
            use_turret(ent, turret);
            uint32_t goal;
            const uint32_t goal_node = *slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922de8)(&goal, ent);  // ScrSentient_GetGoalNode
            if (goal_node != 0 && goal_node != 0xffffffff) {
                uint32_t again;
                uint32_t at_goal[3];
                node_origin(at_goal, *slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922de8)(&again, ent));
                uint32_t at_ent[3];
                origin_of(at_ent, ent);
                if (distance(at_ent, at_goal) > title_global<float>(0x00825d7c)) break;
            }
            Scr_Wait(1.0f);
        } while (title_global<float>(0x0083eeac) > ent_node_distance(ent, node));
    }
    queue_notify(turret, hash_name(pointer<const char>(0x00825b18)));  // "friendly_finished_using_mg42"
}

void XB_CDECL BroScr_HangarChainUpdateLoop(uint32_t /*self*/) {
    end_on(level_handle(), 0x011066c4);
    for (;;) {
        hangar_chain_pass(0x8c, false);
        hangar_chain_pass(0x98, true);
        queue_notify(*reinterpret_cast<uint32_t*>(level_object() + 4), 0x1b179a21);
        Scr_Wait(as_float(0x3e800000));  // 0.25
    }
}

}  // namespace game
