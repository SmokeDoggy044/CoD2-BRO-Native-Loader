#include "bro_scr_trigger_threads.h"

#include "bro_scr_ai_common.h"
#include "bro_scr_arrays.h"
#include "bro_scr_common.h"
#include "bro_scr_cover_turret.h"
#include "bro_scr_nodes_spawner.h"
#include "g_script.h"
#include "g_scr_runtime.h"
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

using Spawn = uint32_t XB_CDECL(ScriptString*, const uint32_t*, uint32_t);
using GetNode = uint32_t XB_CDECL(ScriptString*, ScriptString*, uint32_t, uint32_t);
using GetEntArray = uint32_t XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t);
using GetAI = void XB_CDECL(DynArray*, ScriptString*);
using EntPair = uint32_t XB_CDECL(uint32_t, uint32_t);
using EntTest = uint32_t XB_CDECL(uint32_t);
using CreateThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);
using WaitTillOutputEnt = void XB_CDECL(uint32_t, uint32_t, uint32_t*);
using Resize = void XB_THISCALL(DynArray*, uint32_t, uint32_t);

// The inlined element access: &data[i], the array resized to (i + 1, i + 4) first when i >= count.
inline uint32_t* element_at(DynArray* array, uint32_t i) {
    if (i >= array->size) own_function<Resize>(&DynArray_Resize)(array, i + 1, i + 4);  // DynArray_Resize
    return &array->data[i];
}

// A 0x14-byte thread functor {vtable, function, three words} from ScrIface_Alloc (null when it fails).
inline uint32_t* functor3(uint32_t vtable, uint32_t function, uint32_t a, uint32_t b, uint32_t c) {
    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x14);  // ScrIface_Alloc
    if (!functor) return nullptr;
    functor[0] = vtable;
    functor[1] = function;
    functor[2] = a;
    functor[3] = b;
    functor[4] = c;
    return functor;
}

inline uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// ScrEnt_GetOrigin into a copy.
inline void origin_of(uint32_t* out, uint32_t ent) {
    uint32_t buffer[3];
    const uint32_t* o = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer, ent);
    out[0] = o[0];
    out[1] = o[1];
    out[2] = o[2];
}

// The level object's handle word (g_levelScriptObject + 4; a null object reads address 0).
inline uint32_t level_handle() {
    const uint32_t object = title_global<uint32_t>(0x00922ef8);
    return *pointer<uint32_t>(object ? object + 4 : 0);
}
inline uint32_t hash_of(const ScriptString* string) {
    uint32_t hash;
    return *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(string));
}
// Element n of an int array of the level object (DynArray at g_levelScriptObject + offset), grown
// with DynArray_ResizeInt (n + 1, n + 4) first when n is past the count.
inline int32_t level_int(uint32_t offset, uint32_t n) {
    DynArray* const array = pointer<DynArray>(title_global<uint32_t>(0x00922ef8) + offset);
    if (n >= array->size) own_function<Resize>(&DynArray_ResizeInt)(array, n + 1, n + 4);  // DynArray_ResizeInt
    return static_cast<int32_t>(array->data[n]);
}
// Element n of the level object's int array at `offset`, grown like level_int first.
inline uint32_t* level_int_at(uint32_t offset, uint32_t n) {
    DynArray* const array = pointer<DynArray>(title_global<uint32_t>(0x00922ef8) + offset);
    if (n >= array->size) own_function<Resize>(&DynArray_ResizeInt)(array, n + 1, n + 4);  // DynArray_ResizeInt
    return &array->data[n];
}
// The "fallback" cvar compared with "1"; the result is not used.
inline void check_fallback_cvar() {
    ScriptString value;
    ScriptString_Equals(ScrCall_GScr_GetCvar(&value, 0x00823550), pointer<const char>(0x0082e058));
    scr_release(&value);
}
// Whether the entity's script table holds `key` with a value other than the undefined int (no
// table: false; nothing added).
inline bool has_int_key(uint32_t ent, uint32_t key) {
    if (!entity_handle_is_valid(ent)) return false;
    ScriptKeyTable* const table = title_function<ScriptKeyTable* XB_CDECL(uint32_t)>(0x0011db20)(ent);
    if (!table) return false;
    const uint32_t* const value = ScriptKeyTable_Find(table, key);
    return (value ? *value : undefined_int()) != undefined_int();
}

// A 0x10-byte thread functor {vtable, function, two words} from ScrIface_Alloc (null when it fails).
inline uint32_t* functor2(uint32_t vtable, uint32_t function, uint32_t a, uint32_t b) {
    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
    if (!functor) return nullptr;
    functor[0] = vtable;
    functor[1] = function;
    functor[2] = a;
    functor[3] = b;
    return functor;
}
// GScr_IsTouching(a, b) (the low byte).
inline bool is_touching(uint32_t a, uint32_t b) { return static_cast<uint8_t>(slot<EntPair>(0x00922530)(a, b)) != 0; }

}  // namespace

void XB_CDECL BroScr_FriendlyMg42TriggerThink(uint32_t /*unused*/, uint32_t trigger) {
    uint32_t node;
    {
        ScriptString key;
        scr_string_from(&key, 0x0082dda8);  // "targetname"
        ScriptString target;
        node = slot<GetNode>(0x009221cc)(ScrCall_ScrEnt_GetTarget(&trigger, &target), &key, 0, 0);  // GScr_GetNode
        if (node == 0xffffffff) node = 0;
        scr_release(&target);
        scr_release(&key);
    }
    uint32_t turret;
    {
        ScriptString target;
        turret = slot<GetEntArray>(0x009221c4)(ScrCall_ScrPathNode_GetTarget(&node, &target), 0x0d786b4c, 0, 0,
                                               0);  // GScr_GetEntArray
        scr_release(&target);
    }
    slot<EntPair>(0x00922584)(turret, 0xad76f051);  // GScr_Turret_SetMode
    slot<EntTest>(0x00922594)(turret);              // GScr_Turret_ClearTargetEntity
    uint32_t other = 0;
    for (;;) {
        title_function<WaitTillOutputEnt>(0x004b1590)(trigger, 0xe2a210cf, &other);  // Scr_WaitTillOutputEnt_ORAN
        const uint32_t user = other;
        if (slot<EntTest>(0x00922220)(user) && user == title_global<uint32_t>(0x009236dc)) continue;  // GScr_IsSentient, the player
        bool no_team;
        {
            ScriptString team;
            no_team = ScrCall_ScrSentient_GetTeam(&other, &team)->rep == nullptr;
            scr_release(&team);
        }
        if (no_team) continue;
        bool not_allies;
        {
            ScriptString team;
            not_allies = !ScriptString_Equals(ScrCall_ScrSentient_GetTeam(&other, &team), pointer<const char>(0x00832084));  // "allies"
            scr_release(&team);
        }
        if (not_allies) continue;
        bool defined;
        if (*ScrField_ha8cd6cb7_IsDefined(&defined, user) && *int_slot(user, 0xa8cd6cb7) == 0) continue;
        const uint32_t use_node = node;
        uint8_t usable;
        if (*BroScr_TurretUseTimeCheck(&usable, user, turret, use_node)) {
            uint32_t* const functor = functor3(0x0081a3a8, 0x001b58e0, user, turret, use_node);  // BroScr_FriendlyUseMg42
            slot<CreateThread>(0x0092216c)(0, 0, 0, user, functor, 0);  // Scr_CreateThread
            Scr_WaitTill(turret, 0x7b91738c);
            if (slot<EntTest>(0x00922210)(user)) {  // GScr_IsAlive
                const int32_t now = get_time();
                *ScrField_turret_use_time(user) = static_cast<uint32_t>(now + 10000);
            }
        }
        Scr_Wait(1.0f);
    }
}

void XB_CDECL BroScr_WaitUntilTeamNotTouching(uint32_t trigger, ScriptString team) {
    uint32_t origin_ent;
    {
        ScriptString classname;
        scr_string_from(&classname, 0x0083ab88);  // "script_origin"
        const uint32_t origin[3] = {0, 0, 0};
        origin_ent = slot<Spawn>(0x009222f0)(&classname, origin, 0);  // GScr_Spawn
        scr_release(&classname);
    }
    DynArray ai = {nullptr, 0, 0};
    bool clear;
    do {
        clear = true;
        own_function<Resize>(&DynArray_Resize)(&ai, 0, 0);  // DynArray_Resize
        slot<GetAI>(0x009223d0)(&ai, &team);            // GScr_GetAI
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ai.size); ++i) {
            if (static_cast<uint8_t>(slot<EntPair>(0x00922530)(*element_at(&ai, i), trigger))) {  // GScr_IsTouching
                clear = false;
                break;
            }
        }
        Scr_Wait(1.0f);
    } while (!clear);
    slot<EntTest>(0x00922540)(origin_ent);  // GScr_Delete
    if (ai.data) Scr_Free(ai.data);
    scr_release(&team);
}

void XB_CDECL BroScr_GrenadeThrowTrigger(uint32_t trigger, uint32_t timer) {
    DynArray starts = {nullptr, 0, 0};
    DynArray ends = {nullptr, 0, 0};
    uint32_t velocity[3] = {undefined_lane(), undefined_lane(), undefined_lane()};
    uint32_t thrower = 0;
    title_function<WaitTillOutputEnt>(0x004b1590)(trigger, 0x262a34d8, &thrower);  // Scr_WaitTillOutputEnt_ORAN
    const uint32_t ent = thrower;
    slot<EntPair>(0x00922188)(ent, 0x06730ae8);  // Scr_EndOn "death"
    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f4aac;
        functor[1] = 0x001b5280;  // BroScr_GoToBalconyNode
        functor[2] = ent;
        functor[3] = title_global<uint32_t>(0x00922ea0);
    }
    slot<CreateThread>(0x0092216c)(0, 0, 0, ent, functor, 0);  // Scr_CreateThread
    slot<EntPair>(0x00922d74)(ent, title_global<uint32_t>(0x009236dc));  // ScrActor_SetRef_12a0(the player)
    {
        ScriptString target;
        Scr_GetEntArray(address_of(ScrCall_ScrEnt_GetTarget(&thrower, &target)), 0x0d786b4c,
                        reinterpret_cast<uint8_t*>(&starts), 0);
        scr_release(&target);
    }
    const uint32_t key = 0x0d786b4c;
    for (;;) {
        const uint32_t start = *element_at(&starts, slot<uint32_t XB_CDECL(uint32_t)>(0x0092222c)(starts.size));  // Scr_RandomInt
        {
            uint32_t start_copy = start;
            ScriptString target;
            Scr_GetEntArray(address_of(ScrCall_ScrEnt_GetTarget(&start_copy, &target)), key,
                            reinterpret_cast<uint8_t*>(&ends), 0);
            scr_release(&target);
        }
        const uint32_t end = *element_at(&ends, slot<uint32_t XB_CDECL(uint32_t)>(0x0092222c)(ends.size));
        uint32_t from[3], to[3];
        origin_of(from, start);
        origin_of(to, end);
        float delta[3] = {as_float(to[0]) - as_float(from[0]), as_float(to[1]) - as_float(from[1]),
                          as_float(to[2]) - as_float(from[2])};
        uint32_t dir[3] = {undefined_lane(), undefined_lane(), undefined_lane()};
        slot<void XB_CDECL(uint32_t*, const float*)>(0x00922280)(dir, delta);  // Scr_VectorNormalize
        velocity[0] = dir[0];
        velocity[1] = dir[1];
        velocity[2] = dir[2];
        uint32_t a[3], b[3];
        origin_of(b, end);
        origin_of(a, start);
        const float speed = static_cast<float>(
            slot<long double XB_CDECL(const uint32_t*, const uint32_t*)>(0x00922268)(a, b) * 2);  // Scr_Distance x 2
        velocity[1] = bits(as_float(velocity[1]) * speed);
        velocity[2] = bits(as_float(velocity[2]) * speed);
        velocity[0] = bits(speed * as_float(velocity[0]));
        uint32_t origin[3];
        origin_of(origin, start);
        slot<void XB_CDECL(uint32_t, uint32_t*, uint32_t*, uint32_t)>(0x0092260c)(ent, origin, velocity,
                                                                                    timer);  // GScr_MagicGrenadeManual
        Scr_Wait(1.0f);
        Scr_WaitTillString(trigger, pointer<const char>(0x007fa3ec));  // "throw"
    }
}

void XB_CDECL BroScr_FallbackerSetup(uint32_t /*unused*/, uint32_t n, uint32_t group) {
    ScriptString name;
    scr_string_from(&name, 0x00826458);  // "fallbacker_trigger"
    ScriptString_AppendInt(&name, static_cast<int32_t>(n));
    slot<EntPair>(0x00922188)(level_handle(), hash_of(&name));  // Scr_EndOn
    check_fallback_cvar();
    for (int32_t i = 0;; ++i) {
        const bool more = i < level_int(0x0c, n);
        check_fallback_cvar();
        if (!more) break;
        ScriptString_AssignCString(&name, pointer<const char>(0x00826444));  // "fallback_firstspawn"
        ScriptString_AppendInt(&name, static_cast<int32_t>(n));
        Scr_WaitTill(level_handle(), hash_of(&name));
    }
    DynArray ai = {nullptr, 0, 0};
    slot<void XB_CDECL(DynArray*)>(0x009223d4)(&ai);  // GScr_GetAIAll
    using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                          const uint32_t*, const ScriptString*);
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ai.size); ++i) {
        bool join = has_int_key(*element_at(&ai, i), 0x8bbe4822) && *int_slot(*element_at(&ai, i), 0x8bbe4822) == n;
        if (!join)
            join = has_int_key(*element_at(&ai, i), 0x1aa720d8) && group != undefined_int() &&
                   *int_slot(*element_at(&ai, i), 0x1aa720d8) == group;
        if (join)
            own_function<StartThread>(&Scr_StartThread_ent_i_pathnode_str_3_entry)(0, 0, 0, 0, 0x001bea50, *element_at(&ai, i), &n,
                                                    pointer<uint32_t>(0x00922ea0), pointer<ScriptString>(0x00922e90));
        // Scr_StartThread_ent_i_pathnode_str_3(BroScr_FallbackerJoinFallback)
    }
    own_function<Resize>(&DynArray_Resize)(&ai, 0, 0);  // DynArray_Resize
    const float half = static_cast<float>(level_int(0x6c, n)) * title_global<float>(0x0083eb24);  // 0.5
    for (;;) {
        const int32_t above = static_cast<float>(level_int(0x6c, n)) > half ? 1 : 0;
        if (static_cast<float>(above) == title_global<float>(0x0083eafc)) break;  // 0
        check_fallback_cvar();
        ScriptString_AssignCString(&name, pointer<const char>(0x00825ff8));  // "fallbacker_died"
        ScriptString_AppendInt(&name, static_cast<int32_t>(n));
        Scr_WaitTill(level_handle(), hash_of(&name));
    }
    ScriptString_AssignCString(&name, pointer<const char>(0x00826458));  // "fallbacker_trigger"
    ScriptString_AppendInt(&name, static_cast<int32_t>(n));
    queue_notify(level_handle(), hash_of(&name));
    if (ai.data) Scr_Free(ai.data);
    scr_release(&name);
}

void XB_CDECL BroScr_MgUserGoToNode(uint32_t turret) {
    bool defined;
    if (!*ScrField_h7b1e85ba_IsDefined(&defined, turret)) *int_slot(turret, 0x7b1e85ba) = 0;
    {
        ScriptString name;
        const bool unnamed = ScrCall_ScrEnt_GetTargetname(&turret, &name)->rep == nullptr;
        scr_release(&name);
        if (unnamed) return;
    }
    uint32_t node;
    {
        ScriptString key;
        scr_string_from(&key, 0x00833ad0);  // "target"
        ScriptString name;
        node = slot<GetNode>(0x009221cc)(ScrCall_ScrEnt_GetTargetname(&turret, &name), &key, 0, 0);  // GScr_GetNode
        if (node == 0xffffffff) node = 0;
        scr_release(&name);
        scr_release(&key);
    }
    if (node == 0 || node == 0xffffffff) return;
    uint8_t has_mg42;
    if (!*BroScr_PathNodeHasScriptMg42(&has_mg42, node)) return;
    const uint32_t mg42 = slot<EntPair>(0x00922960)(node, 0xec070be6);  // ScrPathNode_GetIntKey
    *ScrField_script_mg42(turret) = mg42;
    bool first = true;
    DynArray ai = {nullptr, 0, 0};
    DynArray users = {nullptr, 0, 0};
    for (;;) {
        if (first) {
            first = false;
            ScriptString name;
            const bool wait =
                ScrCall_ScrPathNode_GetTargetname(&node, &name)->rep != nullptr || *ScrField_h7b1e85ba(turret) != 0;
            scr_release(&name);
            if (wait) {
                uint32_t hash;
                Scr_WaitTill(turret, *Scr_HashName(&hash, pointer<const char>(0x007fff6c)));  // "get new user"
            }
        }
        slot<void XB_CDECL(DynArray*)>(0x009223d4)(&ai);  // GScr_GetAIAll
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ai.size); ++i) {
            // Undefined bool added as a word whose upper bytes are a never-written stack slot in the original.
            uint32_t* const used = key_slot(*element_at(&ai, i), 0xa287dc52, title_global<uint8_t>(0x00922ed0));
            if (*reinterpret_cast<uint8_t*>(used) == 0) {
                bool on_mg42;
                if (*ScrField_script_mg42_IsDefined(&on_mg42, *element_at(&ai, i))) {
                    const uint32_t* const mine = int_slot(turret, 0xec070be6);
                    if (*int_slot(*element_at(&ai, i), 0xec070be6) == *mine) continue;
                }
            }
            DynArray_PushBack(reinterpret_cast<uint8_t*>(&users), element_at(&ai, i));
        }
        ScriptString empty;
        ScriptString_Copy(&empty, pointer<ScriptString>(0x00922e90));
        uint32_t buffer[3];
        const uint32_t* const o = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922ab8)(buffer, node);  // ScrPathNode_GetOrigin
        const ScrVec3 origin = {{o[0], o[1], o[2]}};
        uint32_t found;
        const uint32_t user = *BroScr_GetClosestAI(&found, origin, empty, &users);
        if (entity_handle_is_valid(user)) {
            uint32_t hash;
            queue_notify(user, *Scr_HashName(&hash, pointer<const char>(0x008263f8)));  // "stop_going_to_node"
            uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
            if (functor) {
                functor[0] = 0x007f4aac;
                functor[1] = 0x001b5280;  // BroScr_GoToBalconyNode
                functor[2] = user;
                functor[3] = node;
            }
            slot<CreateThread>(0x0092216c)(0, 0, 0, user, functor, 0);  // Scr_CreateThread
            Scr_WaitTill(user, 0x06730ae8);  // "death"
        } else {
            Scr_WaitTillString(turret, pointer<const char>(0x007fff6c));  // "get new user"
        }
    }
}

void XB_CDECL BroScr_WaitUntilTeamNotTouchingCall(uint32_t trigger, ScriptString team) {
    ScriptString copy;
    ScriptString_Copy(&copy, &team);
    BroScr_WaitUntilTeamNotTouching(trigger, copy);
    scr_release(&team);
}

void XB_CDECL BroScr_GrenadeThrowTriggers(uint32_t ent) {
    DynArray throwers = {nullptr, 0, 0};
    DynArray magic = {nullptr, 0, 0};
    DynArray targets = {nullptr, 0, 0};
    {
        ScriptString target;
        Scr_GetEntArray(address_of(ScrCall_ScrEnt_GetTarget(&ent, &target)), 0x0d786b4c,
                        reinterpret_cast<uint8_t*>(&targets), 0);
        scr_release(&target);
    }
    // trigger_multiple targets throw magic grenades; the others are thrower triggers.
    for (uint32_t i = 1; static_cast<int32_t>(i - 1) < static_cast<int32_t>(targets.size); ++i) {
        ScriptString classname;
        const bool multiple = ScriptString_Equals(ScrCall_ScrEnt_GetClassname(element_at(&targets, i - 1), &classname),
                                                  pointer<const char>(0x00832270));  // "trigger_multiple"
        scr_release(&classname);
        if (multiple)
            DynArray_PushBack(reinterpret_cast<uint8_t*>(&magic), element_at(&targets, i - 1));
        else
            DynArray_PushBack(reinterpret_cast<uint8_t*>(&throwers), element_at(&targets, i - 1));
    }
    uint32_t delay;
    bool defined;
    if (*ScrField_script_delay_IsDefined(&defined, ent))
        delay = *key_slot(ent, 0x807116c4, undefined_float());  // script_delay
    else
        delay = bits(3.5f);
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(throwers.size); ++i) {
        const uint32_t thrower = *element_at(&throwers, i);
        slot<CreateThread>(0x0092216c)(0, 0, 0, thrower,
                                       functor2(0x007f4a70, 0x001be660, thrower, delay), 0);  // BroScr_GrenadeThrowTrigger
    }
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(magic.size); ++i) {
        const uint32_t trigger = *element_at(&magic, i);
        slot<CreateThread>(0x0092216c)(0, 0, 0, trigger,
                                       functor2(0x007f4a70, 0x001a53e0, trigger, delay), 0);  // BroScr_MagicGrenadeThrowThink
    }

    // The count (ScrEnt_GetCount 0x00922b88; 2 when undefined) of throws while the player touches the trigger.
    using GetCount = int32_t XB_CDECL(uint32_t);
    const int32_t undefined = static_cast<int32_t>(undefined_int());
    int32_t count;
    if (slot<GetCount>(0x00922b88)(ent) != static_cast<int32_t>(undefined_int()) && undefined > 0)
        count = slot<GetCount>(0x00922b88)(ent);
    else
        count = 2;
    while (count > 0) {
        Scr_WaitTill(ent, 0xe2a210cf);  // trigger
        if (!is_touching(title_global<uint32_t>(0x009236dc), ent)) continue;
        do {
            {
                ScriptString target;
                Scr_GetEntArray(address_of(ScrCall_ScrEnt_GetTarget(&ent, &target)), 0x0d786b4c,
                                reinterpret_cast<uint8_t*>(&throwers), 0);
                scr_release(&target);
            }
            ScriptString message;
            scr_string_from(&message, 0x007fa3ec);  // "throw"
            BroScr_NotifyArray(&throwers, message);
            scr_string_from(&message, 0x007fa3ec);
            BroScr_NotifyArray(&magic, message);
            bool min_defined;
            bool max_defined;
            if (*ScrField_script_delay_min_IsDefined(&min_defined, ent) &&
                *ScrField_script_delay_max_IsDefined(&max_defined, ent)) {
                const float high = as_float(*key_slot(ent, 0x6c663f49, undefined_float()));  // script_delay_max
                const float range = high - as_float(*key_slot(ent, 0x6c663e5f, undefined_float()));  // script_delay_min
                const uint32_t* const low = key_slot(ent, 0x6c663e5f, undefined_float());
                const long double random = slot<long double XB_CDECL(float)>(0x00922230)(range);  // Scr_RandomFloat
                Scr_Wait(static_cast<float>(as_float(*low) + random));
            } else {
                Scr_Wait(static_cast<float>(slot<long double XB_CDECL(float)>(0x00922230)(7.0f) + 2.0f));
            }
            --count;
            if (count <= 0) break;
        } while (is_touching(title_global<uint32_t>(0x009236dc), ent));
    }
    if (targets.data) Scr_Free(targets.data);
    if (magic.data) Scr_Free(magic.data);
    if (throwers.data) Scr_Free(throwers.data);
}

void XB_CDECL BroScr_GrenadeSpawnerOnceTriggerThink(uint32_t trigger) {
    DynArray throwers = {nullptr, 0, 0};
    {
        ScriptString target;
        Scr_GetEntArray(address_of(ScrCall_ScrEnt_GetTarget(&trigger, &target)), 0x0d786b4c,
                        reinterpret_cast<uint8_t*>(&throwers), 0);
        scr_release(&target);
    }
    uint32_t delay;
    bool defined;
    if (*ScrField_script_delay_IsDefined(&defined, trigger))
        delay = *ScrField_script_delay(trigger);
    else
        delay = bits(2.0f);
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(throwers.size); ++i) {
        const uint32_t thrower = *element_at(&throwers, i);
        slot<CreateThread>(0x0092216c)(0, 0, 0, thrower,
                                       functor2(0x007f4a70, 0x001be660, thrower, delay), 0);  // BroScr_GrenadeThrowTrigger
    }
    Scr_WaitTill(trigger, 0xe2a210cf);  // trigger
    BroScr_DeleteFloodSpawner(trigger);
    if (throwers.data) Scr_Free(throwers.data);
}

void XB_CDECL BroScr_FallbackSpawnerThink(uint32_t spawner, uint32_t group, uint32_t node) {
    using GetCount = int32_t XB_CDECL(uint32_t);
    using StartThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t*,
                                          const uint32_t*, const ScriptString*);
    const int32_t count = slot<GetCount>(0x00922b88)(spawner);  // ScrEnt_GetCount
    const uint32_t total = count + *level_int_at(0x6c, group);
    *level_int_at(0x6c, group) = total;
    uint32_t first = 1;
    uint32_t spawned = 0;
    while (slot<GetCount>(0x00922b88)(spawner) > 0) {
        title_function<WaitTillOutputEnt>(0x004b1590)(spawner, 0x262a34d8, &spawned);  // Scr_WaitTillOutputEnt_ORAN
        if (first) {
            check_fallback_cvar();
            ScriptString name;
            scr_string_from(&name, 0x00826444);  // "fallback_firstspawn"
            ScriptString_AppendInt(&name, static_cast<int32_t>(group));
            queue_notify(level_handle(), hash_of(&name));
            first = 0;
            scr_release(&name);
        }
        Scr_Wait(as_float(0x3d4ccccd));  // 0.05
        const uint32_t ai = spawned;
        if (slot<EntTest>(0x00922210)(ai)) {  // GScr_IsAlive
            // Unless its 0xc0bb2676 is set: wait for "finished spawning".
            bool done = false;
            if (entity_handle_is_valid(ai)) {
                if (ScriptKeyTable* const table = title_function<ScriptKeyTable* XB_CDECL(uint32_t)>(0x0011db20)(ai)) {
                    const uint32_t* const value = ScriptKeyTable_Find(table, 0xc0bb2676);
                    const uint8_t undefined = title_global<uint8_t>(0x00922ed0);
                    done = (value ? static_cast<uint8_t>(*value) : undefined) != undefined;
                }
            }
            if (!done) Scr_WaitTillString(ai, pointer<const char>(0x007f6000));  // "finished spawning"
            if (slot<EntTest>(0x00922210)(ai)) {
                ScriptString kind;
                scr_string_from(&kind, 0x00826558);  // "is spawner"
                own_function<StartThread>(&Scr_StartThread_ent_i_pathnode_str_3_entry)(0, 0, 0, 0, 0x001bea50, ai, &group, &node, &kind);
                // Scr_StartThread_ent_i_pathnode_str_3(BroScr_FallbackerJoinFallback)
                scr_release(&kind);
                continue;
            }
        }
        ScriptString name;
        scr_string_from(&name, 0x00825ff8);  // "fallbacker_died"
        ScriptString_AppendInt(&name, static_cast<int32_t>(group));
        queue_notify(level_handle(), hash_of(&name));
        --*level_int_at(0x6c, group);
        scr_release(&name);
    }
}

}  // namespace game
