#include "bro_scr_nodes_spawner.h"

#include <cstring>

#include "bro_scr_cover_turret.h"
#include "g_scr_runtime.h"
#include "qcommon_fs.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "script_string.h"
#include "title_data.h"
#include "bro_scr_actor.h"
#include "scr_access.h"
#include "bro_scr_mission.h"
#include "g_script.h"
#include "g_scr_builtins.h"

namespace game {

namespace {

template <typename T>
T& at(const void* base, uint32_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + offset);
}
template <typename T>
T* pointer(uint32_t address) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}
uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// An engine call through the interface block (pScr_* slot).
template <typename F>
F* slot(uint32_t address) {
    return title_function<F>(title_global<uint32_t>(address));
}

uint8_t* level_object() { return title_global<uint8_t*>(0x00922ef8); }   // g_levelScriptObject
uint8_t* anim_object() { return title_global<uint8_t*>(0x00922efc); }    // the anim scripts' object
uint32_t player() { return title_global<uint32_t>(0x009236dc); }

uint32_t entity_handle_is_valid(uint32_t handle) {
    return slot<uint32_t XB_CDECL(uint32_t)>(0x00922214)(handle);
}
uint32_t actor_get_int_240(uint32_t actor) { return slot<uint32_t XB_CDECL(uint32_t)>(0x00922ce8)(actor); }
void get_ent_var(uint32_t ent, uint32_t key, uint32_t callback) {
    slot<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x009221c0)(ent, key, callback);  // GScr_GetEntVar
}

// The entity's script key table (ScrCall_GScr_GetEntScriptObject), the shared empty one without.
ScriptKeyTable* ent_script_object(uint32_t ent) {
    ScriptKeyTable* table = own_function<ScriptKeyTable* XB_CDECL(uint32_t)>(&ScrCall_GScr_GetEntScriptObject)(ent);
    return table ? table : pointer<ScriptKeyTable>(0x00922e94);
}

// The level object's own entity handle (+4; read at address 4 for a null object, as the original).
uint32_t level_handle() {
    uint8_t* const obj = level_object();
    return title_global<uint32_t>(obj ? address_of(obj) + 4 : 0);
}

// Friendly-fire mission failure (BroScr_FriendlyFireMissionFail 0x00181f70) as a new thread on the level.
void start_friendly_fire_fail() {
    const uint32_t handle = level_handle();
    own_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(&Scr_StartThreadFunctor1_entry)(
        0, 0, 0, 0, own_address(&BroScr_FriendlyFireMissionFail), handle);  // Scr_StartThreadFunctor1
}

// Field 0x7599373d defined and 0: this friend does not count.
bool friend_ignored(uint32_t self) {
    bool defined;
    if (!*ScrField_h7599373d_IsDefined(&defined, self)) return false;
    return *ScrField_h7599373d(self) == 0;
}

// The actor's +0x240 int is defined and 0x36b1a5.
bool friend_excluded(uint32_t self) {
    if (actor_get_int_240(self) == title_global<uint32_t>(0x00922eb4)) return false;  // g_scrUndefinedHandle
    return actor_get_int_240(self) == 0x0036b1a5;
}

bool node_valid(uint32_t node) { return node != 0 && node != 0xffffffffu; }

// A thiscall node predicate on the node's argument slot (AL).
bool node_test(uint32_t function, const uint32_t* node) {
    return title_function<uint8_t XB_THISCALL(const uint32_t*)>(function)(node) != 0;
}

bool int_key_defined(uint32_t node, uint32_t key) {
    const uint32_t value = slot<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00922960)(node, key);  // GetIntKey
    return value != title_global<uint32_t>(0x00922ec0);                                        // g_scrUndefinedInt
}

// The x87 result against g_scrUndefinedFloat (FUCOMIP: unordered counts as different).
bool float_key_defined(uint32_t getter, uint32_t node, uint32_t key) {
    const long double value = slot<long double XB_CDECL(uint32_t, uint32_t)>(getter)(node, key);
    return !(value == static_cast<long double>(title_global<float>(0x00922eac)));
}


uint32_t node_at_position(uint32_t* out, uint32_t ent) {
    own_function<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(&BroScr_GetNodeAtPosition_entry)(out, ent);  // BroScr_GetNodeAtPosition
    return *out;
}

}  // namespace

uint8_t* XB_CDECL BroScr_ActorMayMoveToPointForStance_2(uint8_t* out, uint32_t actor, ScrVec3 point, uint32_t yaw) {
    // The original hands over the address of its by-value argument; the flag word's upper bytes are
    // the caller's ECX there (the callee reads only the low byte).
    const uint32_t prone = slot<uint32_t XB_CDECL(uint32_t)>(0x00922dc0)(actor) == 0x0738b1a5 ? 1 : 0;  // GetInt_184
    const ScrVec3 copy = point;
    const uint8_t ok = slot<uint8_t XB_CDECL(uint32_t, const ScrVec3*, uint32_t, uint32_t)>(0x0092278c)(actor, &copy, yaw,
                                                                                                      prone);
    *out = ok != 0;
    return out;
}

uint8_t* XB_CDECL BroScr_CompareIntEqual(uint8_t* out, const int32_t* value, int32_t other) {
    *out = *value == other;
    return out;
}

uint8_t* XB_CDECL BroScr_CompareIntNotEqual(uint8_t* out, const int32_t* value, int32_t other) {
    *out = *value != other;
    return out;
}

uint8_t* XB_CDECL BroScr_ActorWeaponInLevelTable(uint8_t* out, uint32_t actor) {
    const uint32_t weapon = slot<uint32_t XB_CDECL(uint32_t)>(0x00922d30)(actor);  // GetInt_f0
    if (weapon != title_global<uint32_t>(0x00922eb4)) {
        uint8_t* const obj = anim_object();
        uint32_t entry = at<uint32_t>(obj, 0x130);
        const uint32_t end = entry + at<uint32_t>(obj, 0x138) * 0x18;
        for (; entry != end; entry += 0x18) {
            if (at<uint32_t>(pointer<void>(entry), 0) == weapon) {
                *out = 1;
                return out;
            }
        }
    }
    *out = 0;
    return out;
}

ScriptString* XB_CDECL BroScr_GetNodeTypeAtPosition(ScriptString* out, uint32_t ent) {
    // The node handle is written over the entity's argument slot, as the original.
    const uint32_t node = node_at_position(&ent, ent);
    if (!node_valid(node)) {
        ScriptString_FromCString(out, pointer<const char>(0x00832430));  // "none"
        return out;
    }
    ScriptString type;
    own_function<ScriptString* XB_THISCALL(const uint32_t*, ScriptString*)>(&ScrPathNode_CopyType_entry)(&ent, &type);  // CopyType
    ScriptString_Copy(out, &type);
    ScriptString_Release(&type);
    return out;
}

uint32_t* XB_CDECL BroScr_GetCoverNodeOrOrigin(uint32_t* out, uint32_t ent) {
    uint32_t node_slot = ent;
    const uint32_t node = node_at_position(&node_slot, ent);
    uint32_t origin[3];
    const uint32_t* v;
    if (node_valid(node))
        v = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922ab8)(origin, node);  // ScrPathNode_GetOrigin
    else
        v = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(origin, ent);  // ScrEnt_GetOrigin
    out[0] = v[0];
    out[1] = v[1];
    out[2] = v[2];
    return out;
}

uint8_t* XB_CDECL BroScr_IsAnimDebugTarget(uint8_t* out, uint32_t ent) {
    bool target = true;
    if (slot<int32_t XB_CDECL(const char*)>(0x009222dc)(pointer<const char>(0x00825cdc)) != 1) {  // "animDebug"
        target = entity_handle_is_valid(at<uint32_t>(anim_object(), 0x128)) != 0 &&
                 at<uint32_t>(anim_object(), 0x128) == ent;
    }
    *out = target;
    return out;
}

uint8_t* XB_CDECL BroScr_IsOneOf3(uint8_t* out, uint32_t value, uint32_t a, uint32_t b, uint32_t c) {
    *out = value == a || value == b || value == c;
    return out;
}

void XB_CDECL BroScr_SetOrientFaceMotionIf(uint32_t actor, uint32_t mode) {
    if (mode != 0xe2cb8ae5) return;
    ScriptString text;
    ScriptString_FromCString(&text, pointer<const char>(0x0083ad78));  // "face motion"
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t)>(0x00922740)(actor, &text, 0, 0);  // SetOrientMode
    ScriptString_Release(&text);
}

uint8_t* XB_CDECL BroScr_PathNodeHasDoor(uint8_t* out, uint32_t node) {
    *out = node_valid(node) && node_test(own_address(&ScrPathNodeField_script_door_IsSet_entry), &node);  // ScrPathNodeField_script_door_IsSet
    return out;
}

uint8_t* XB_CDECL BroScr_PathNodeHasScriptDelay(uint8_t* out, uint32_t node) {
    *out = node_valid(node) && float_key_defined(0x00922968, node, 0x807116c4);  // ScrPathNode_GetFloatKey
    return out;
}

uint8_t* XB_CDECL BroScr_IsChainNode(uint8_t* out, uint32_t node) {
    *out = node_valid(node) && node_test(own_address(&BroScr_NodeHasScriptChain_entry), &node);  // BroScr_NodeHasScriptChain
    return out;
}

uint8_t* XB_CDECL BroScr_PathNodeHasScriptFallback(uint8_t* out, uint32_t node) {
    *out = node_valid(node) && int_key_defined(node, 0x8bbe4822);
    return out;
}

uint8_t* XB_CDECL BroScr_NodeHasScriptWaittill(uint8_t* out, uint32_t node) {
    *out = node_valid(node) && node_test(own_address(&ScrPathNodeField_script_waittill_NonEmpty_entry), &node);  // ScrPathNodeField_script_waittill_NonEmpty
    return out;
}

uint8_t* XB_CDECL BroScr_PathNodeHasScriptMg42(uint8_t* out, uint32_t node) {
    *out = node_valid(node) && int_key_defined(node, 0xec070be6);
    return out;
}

uint8_t* XB_CDECL BroScr_VehNodeHasScriptDelay(uint8_t* out, uint32_t node) {
    *out = node != 0xffffffffu && float_key_defined(0x00922988, node, 0x807116c4);  // ScrVehNode_GetFloatKey
    return out;
}

void XB_CDECL BroScr_DebugDrawRadiusCircle(uint32_t ent) {
    if (!entity_handle_is_valid(ent)) return;
    uint32_t radius_bits = title_global<uint32_t>(0x0083ebb8);  // 16.0f
    uint32_t origin[3];
    slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(origin, ent);  // ScrEnt_GetOrigin
    bool defined;
    if (*ScrField_script_radius_IsDefined(&defined, ent)) ScrField_script_radius(ent);  // read and dropped
    const float& radius = *reinterpret_cast<const float*>(&radius_bits);
    float angle = 0.0f;
    const auto cosine = slot<long double XB_CDECL(float)>(0x00922244);  // Scr_Cos
    const auto sine = slot<long double XB_CDECL(float)>(0x00922240);    // Scr_Sin
    for (int32_t i = 0;; ++i) {
        const int32_t inside = radius > static_cast<float>(i) ? 1 : 0;
        if (static_cast<float>(inside) == title_global<float>(0x0083eafc)) break;  // 0.0f
        angle = platform_x87_add(angle, title_global<float>(0x00825ce8));         // 0.3925f
        cosine(angle);
        sine(angle);
        cosine(angle);
        sine(angle);
    }
}

void XB_CDECL BroScr_SetCreditsName(uint32_t actor, uint32_t value) {
    if (value == title_global<uint32_t>(0x00922ec0) &&
        ScriptString_Equals(&at<ScriptString>(level_object(), 0x1c4), pointer<const char>(0x0082c528)))  // "credits"
        return;
    const auto set_name = slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922d2c);  // ScrActor_SetString_ec
    bool defined;
    if (*ScrField_ha1763cc1_IsDefined(&defined, actor)) {
        ScriptKeyTable* const table = ent_script_object(actor);
        const ScriptString* const name =
            title_function<const ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t)>(0x00458da0)(table, 0xa1763cc1);
        ScriptString copy;
        ScriptString_Copy(&copy, name);
        set_name(actor, copy);  // by value: the callee releases it
        return;
    }
    ScriptString current;
    const bool none = ScrCall_ScrActor_GetString_ec(&actor, &current)->rep == nullptr;
    ScriptString_Release(&current);
    if (!none) return;
    ScriptString name;
    BroScr_SetPrivateName(&name, actor);
    ScriptString copy;
    ScriptString_Copy(&copy, &name);
    set_name(actor, copy);
    ScriptString_Release(&name);
}

void XB_CDECL BroScr_AddKeyedEnt(uint32_t ent, uint32_t key) {
    uint8_t* const obj = level_object();
    uint32_t entry = at<uint32_t>(obj, 0x50);
    const uint32_t end = entry + at<uint32_t>(obj, 0x58) * 12;
    for (; entry != end; entry += 12) {
        if (at<uint32_t>(pointer<void>(entry), 0) == key && at<uint32_t>(pointer<void>(entry), 8) == ent) return;
    }
    const uint32_t value[3] = {key, 0, ent};
    DynArray_PushBack12_2(&at<DynArray>(obj, 0x50), value);
}

namespace {

bool turret_use_time_ok(uint32_t actor, uint32_t node) {
    const auto distance = slot<long double XB_CDECL(const uint32_t*, const uint32_t*)>(0x00922268);  // Scr_Distance
    if (slot<uint8_t XB_CDECL(uint32_t)>(0x00922d80)(actor) != 0) return false;  // ScrActor_GetByte_1374
    bool defined;
    if (*ScrField_turret_use_time_IsDefined(&defined, actor)) {
        const int32_t* const until = reinterpret_cast<const int32_t*>(ScrField_turret_use_time(actor));
        if (slot<int32_t XB_CDECL()>(0x009223c4)() < *until) return false;  // GScr_GetTime
    }
    uint32_t node_origin_out[3];
    const uint32_t* const o = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922ab8)(node_origin_out, node);
    const uint32_t node_origin[3] = {o[0], o[1], o[2]};
    const uint32_t undefined = title_global<uint32_t>(0x00922eb8);  // g_scrUndefinedVecLane
    uint32_t player_origin[3] = {undefined, undefined, undefined};
    slot<void XB_CDECL(uint32_t, uint32_t*)>(0x00922524)(player(), player_origin);  // GScr_GetOrigin
    const uint32_t from[3] = {player_origin[0], player_origin[1], player_origin[2]};
    if (static_cast<long double>(title_global<float>(0x0083eb90)) > distance(from, node_origin)) return false;  // 100
    uint32_t goal;
    const auto get_goal_node = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922de8);  // ScrSentient_GetGoalNode
    const uint32_t first = *get_goal_node(&goal, actor);
    if (first != 0 && first != 0xffffffffu) {
        uint32_t goal_node = *get_goal_node(&goal, actor);
        uint32_t goal_origin[3];
        uint32_t* const g = ScrCall_ScrPathNode_GetOrigin(&goal_node, goal_origin);
        uint32_t player_at[3];
        uint32_t* const p = ScrCall_GScr_GetOrigin(player_at, pointer<const uint32_t>(0x009236dc));
        if (distance(p, g) > static_cast<long double>(title_global<float>(0x00825d7c))) return false;  // 1100
    }
    return true;
}

}  // namespace

uint8_t* XB_CDECL BroScr_TurretUseTimeCheck(uint8_t* out, uint32_t actor, uint32_t, uint32_t node) {
    *out = turret_use_time_ok(actor, node);
    return out;
}

void XB_CDECL BroScr_FriendlyFinishedMg42(uint32_t ent) {
    bool defined;
    if (!*ScrField_friendly_mg42_IsDefined(&defined, ent)) return;
    ScriptKeyTable* const table = ent_script_object(ent);
    uint32_t* value = ScriptKeyTable_Find(table, 0xaf6fd78f);
    if (!value) {
        uint32_t zero = 0;
        value = title_function<uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t, const uint32_t*)>(0x00458e10)(
            table, 0xaf6fd78f, &zero);  // ScriptKeyTable_SetValue
    }
    uint32_t hash;
    const uint32_t notify = *Scr_HashName(&hash, pointer<const char>(0x00825b18));  // "friendly_finished_using_mg42"
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(*value, notify);              // Scr_QueueNotify
}

void XB_CDECL Bro__spawner_gmi__tanksquish(uint32_t self, uint32_t, uint32_t attacker) {
    if (slot<uint32_t XB_CDECL(uint32_t)>(0x00922210)(self) != 0) return;  // GScr_IsAlive
    if (!entity_handle_is_valid(attacker)) return;
    bool vehicle;
    if (!*ScrField_vehicletype_IsDefined(&vehicle, attacker)) return;
    ScriptString sound;
    ScriptString_FromCString(&sound, pointer<const char>(0x00825d80));  // "human_crunch"
    slot<void XB_CDECL(uint32_t, ScriptString*, uint32_t, uint32_t, uint32_t)>(0x009221dc)(self, &sound, 0, 0, 0);
    ScriptString_Release(&sound);
}

void XB_CDECL Bro__spawner_gmi__Allies_Death(uint32_t self, uint32_t attacker) {
    uint32_t hash;
    get_ent_var(self, 0x06730ae8, *Scr_HashName(&hash, pointer<const char>(0x007f5194)));  // "_spawner_gmi::Allies_Death"
    if (!entity_handle_is_valid(attacker) || attacker != player()) return;
    if (friend_ignored(self)) return;
    uint8_t* const obj = level_object();
    if (at<int32_t>(obj, 0xdc) >= 4) {
        at<uint32_t>(obj, 0xf4) = 0;
        at<uint32_t>(level_object(), 0xdc) = 0;
        return;
    }
    if (friend_excluded(self)) return;
    start_friendly_fire_fail();
}

void XB_CDECL Bro__spawner_gmi__KillFriends_Damage_Think(uint32_t self, int32_t damage, uint32_t attacker) {
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(level_handle(), 0xa264707f);  // Scr_EndOn
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922188)(self, 0x06730ae8);
    if (attacker != player()) return;
    if (damage == title_global<int32_t>(0x00922ec0)) return;
    if (!entity_handle_is_valid(attacker)) return;
    if (friend_ignored(self)) return;
    at<int32_t>(level_object(), 0xf4) += damage;
    uint8_t* const obj = level_object();
    const int32_t total = at<int32_t>(obj, 0xf4);
    if (total < 0x258) return;
    if (at<int32_t>(obj, 0xdc) >= 4) {
        at<uint32_t>(obj, 0xf4) = 0;
        at<uint32_t>(level_object(), 0xdc) = 0;
        return;
    }
    if (friend_excluded(self)) return;
    uint32_t hash;
    get_ent_var(self, 0xce270885,
                *Scr_HashName(&hash, pointer<const char>(0x00825d90)));  // "_spawner_gmi::KillFriends_Damage_Think"
    start_friendly_fire_fail();
}

void XB_CDECL Bro__spawner_gmi__KillFriends_MissionFail_OnDamage(uint32_t self, uint32_t attacker) {
    if (!entity_handle_is_valid(attacker) || attacker != player()) return;
    if (friend_excluded(self)) return;
    if (friend_ignored(self)) return;
    start_friendly_fire_fail();
}

void XB_CDECL Bro__spawner_gmi__KillFriends_MissionFail_OnDeath(uint32_t self, uint32_t attacker) {
    uint32_t hash;
    get_ent_var(self, 0x06730ae8,
                *Scr_HashName(&hash, pointer<const char>(0x007f5164)));  // "_spawner_gmi::KillFriends_MissionFail_OnDeath"
    if (!entity_handle_is_valid(attacker) || attacker != player()) return;
    if (friend_excluded(self)) return;
    if (friend_ignored(self)) return;
    start_friendly_fire_fail();
}

void XB_CDECL BroScr_TriggerOff(uint32_t trigger) {
    if (trigger == 0 || !entity_handle_is_valid(trigger)) return;
    bool defined;
    if (!*ScrField_realorigin_IsDefined(&defined, trigger)) {
        float* const saved = ScrField_realorigin(trigger);
        uint32_t origin[3];
        const uint32_t* const o = ScrCall_ScrEnt_GetOrigin(&trigger, origin);
        uint32_t* const s = reinterpret_cast<uint32_t*>(saved);
        s[0] = o[0];
        s[1] = o[1];
        s[2] = o[2];
    }
    const float* const real = ScrField_realorigin(trigger);
    uint32_t origin_out[3];
    const uint32_t* const o = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(origin_out, trigger);
    const uint32_t current_bits[3] = {o[0], o[1], o[2]};
    const float* const current = reinterpret_cast<const float*>(current_bits);
    if (!(current[0] == real[0])) return;
    if (!(current[1] == real[1])) return;
    if (!(current[2] == real[2])) return;
    uint32_t again[3];
    const float* const v = reinterpret_cast<const float*>(ScrCall_ScrEnt_GetOrigin(&trigger, again));
    ScrVec3 lowered;
    float* const l = reinterpret_cast<float*>(lowered.v);
    l[0] = v[0];
    l[1] = v[1];
    l[2] = v[2] - title_global<float>(0x0083f010);  // 10000
    ScrCall_ScrEnt_SetOrigin(&trigger, &lowered);
}

void XB_CDECL BroScr_TriggerOn(uint32_t trigger) {
    bool defined;
    if (!*ScrField_realorigin_IsDefined(&defined, trigger)) return;
    const uint32_t* const real = reinterpret_cast<const uint32_t*>(ScrField_realorigin(trigger));
    ScrVec3 origin;
    origin.v[0] = real[0];
    origin.v[1] = real[1];
    origin.v[2] = real[2];
    slot<void XB_CDECL(uint32_t, ScrVec3)>(0x00922b24)(trigger, origin);  // ScrEnt_SetOrigin
}

void XB_CDECL BroScr_PushActorFloat82c(uint32_t actor) {
    const float value = slot<float XB_CDECL(uint32_t)>(0x00922c88)(actor);  // ScrActor_GetFloat_82c
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    ScriptKeyTable* const table = ent_script_object(actor);
    uint32_t* saved = ScriptKeyTable_Find(table, 0x98b54d9f);
    if (!saved) saved = ScriptKeyTable_Set(table, 0x98b54d9f, title_global<uint32_t>(0x00922eac));
    *saved = bits;
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922c8c)(actor, 0x40400000);  // ScrActor_SetFloat_82c(3.0)
}

void XB_CDECL BroScr_PopActorFloat82c(uint32_t actor) {
    ScriptKeyTable* const table = ent_script_object(actor);
    uint32_t* saved = ScriptKeyTable_Find(table, 0x98b54d9f);
    if (!saved) saved = ScriptKeyTable_Set(table, 0x98b54d9f, title_global<uint32_t>(0x00922eac));
    slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922c8c)(actor, *saved);
}

}  // namespace game


namespace game {

// The ScrNode_StringNonEmpty shape (scr_field.cpp) for script_chain: the copier runs at the
// original's ESP (frame: push ecx (the temporary string), push ebx).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL BroScr_NodeHasScriptChain_c(uint32_t entry_esp, uint32_t node) {
    const uint32_t got = entry_esp - 4;
    TitleRegs regs{};
    regs.ecx = node;
    const uint32_t args[1] = {got};
    title_call_at_regs(entry_esp - 8, own_address(ScrPathNodeField_script_chain_Copy), &regs, 1, args);  // ScrPathNodeField_script_chain_Copy
    const bool non_empty = title_global<uint32_t>(got) != 0;
    own_function<void XB_THISCALL(uint32_t)>(&ScriptString_Release_entry)(got);  // ScriptString_Release
    return non_empty;
}

void* XB_THISCALL ScrField_fallback_Copy(uint8_t* self, void* out) {
    ScriptKeyTable_GetArrayCopyOrEmpty_4(reinterpret_cast<ScriptKeyTable*>(self - 0xc), static_cast<uint32_t*>(out),
                                         0x9ef08b23);
    return out;
}

}  // namespace game

PLATFORM_ASM_ENTRY(BroScr_NodeHasScriptChain_entry,
                   "movl %esp, %edx\n" GAME_GAP_ESP("0x10000")
                   "pushl %ecx\n pushl %edx\n" PLATFORM_ASM_CALL(BroScr_NodeHasScriptChain_c)
                   "addl $8, %esp\n popl %esp\n ret")
