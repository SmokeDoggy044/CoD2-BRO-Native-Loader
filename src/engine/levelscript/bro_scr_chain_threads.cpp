#include "bro_scr_chain_threads.h"

#include "bro_scr_actor_threads.h"
#include "bro_scr_ai_common.h"
#include "bro_scr_common.h"
#include "g_scr_runtime.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "scr_access.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "script_string.h"
#include "title_data.h"
#include "bro_scr_exploder.h"
#include "levels_twins.h"
#include "bro_scr_friendly_threads.h"
#include "bro_scr_notify_threads.h"

namespace game {

using namespace bro_ai;

namespace {

using Resize = void XB_THISCALL(DynArray*, uint32_t, uint32_t);
using GetArray = DynArray* XB_THISCALL(ScriptKeyTable*, uint32_t);
using SetArray = uint32_t* XB_THISCALL(ScriptKeyTable*, uint32_t, const uint32_t*);
using GetAI = void XB_CDECL(DynArray*, ScriptString*);
using EntPair = uint32_t XB_CDECL(uint32_t, uint32_t);
using EntTest = uint32_t XB_CDECL(uint32_t);
using CreateThread = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t);
using GetEnt = uint32_t XB_CDECL(ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t);
using Spawn = uint32_t XB_CDECL(ScriptString*, const float*, uint32_t);
using BuildTargetChain = void XB_CDECL(uint32_t, uint32_t, uint32_t);
using StartEntEnt = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*);

constexpr uint32_t kNodes = 0x0605894e;     // the chain's nodes (array)
constexpr uint32_t kAI = 0x8969d6d6;        // the chain's AI (array)
constexpr uint32_t kChainPos = 0x94ce2dcb;  // an AI's node index (chain_pos)
constexpr uint32_t kRadius = 0x2266f963;    // a node's radius
constexpr uint32_t kBusy = 0x14b95ada;      // the owner moves AI (bool)

// The inlined element access: &data[i], the array resized to (i + 1, i + 4) first when i >= count.
inline uint32_t* element_at(DynArray* array, uint32_t i) {
    if (i >= array->size) own_function<Resize>(&DynArray_Resize)(array, i + 1, i + 4);  // DynArray_Resize
    return &array->data[i];
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

// The entity's array field through ScriptKeyTable_Find, added empty (ScriptKeyTable_SetArray) when missing.
inline DynArray* found_array(uint32_t ent, uint32_t key) {
    ScriptKeyTable* const table = ent_script_object(ent);
    uint32_t* value = ScriptKeyTable_Find(table, key);
    if (!value) {
        const uint32_t empty[3] = {0, 0, 0};
        value = own_function<SetArray>(&ScriptKeyTable_SetArray)(table, key, empty);  // ScriptKeyTable_SetArray
    }
    return pointer<DynArray>(*value);
}
// The entity's array field (ScriptKeyTable_GetArray).
inline DynArray* get_array(uint32_t ent, uint32_t key) {
    return own_function<GetArray>(&ScriptKeyTable_GetArray)(ent_script_object(ent), key);
}
inline int32_t count_of(const DynArray* array) { return static_cast<int32_t>(array->size); }

// The inlined int field test: a valid entity with a script object whose value differs from the undefined int.
inline bool int_defined(uint32_t ent, uint32_t key) {
    if (!entity_handle_is_valid(ent)) return false;
    ScriptKeyTable* const table = title_function<ScriptKeyTable* XB_CDECL(uint32_t)>(0x0011db20)(ent);
    if (!table) return false;
    const uint32_t* const value = ScriptKeyTable_Find(table, key);
    return (value ? *value : undefined_int()) != undefined_int();
}
// A bool field's byte, added as the undefined bool (the original's word keeps stale stack bytes above it).
inline uint8_t* bool_slot(uint32_t ent, uint32_t key) {
    return reinterpret_cast<uint8_t*>(key_slot(ent, key, title_global<uint8_t>(0x00922ed0)));
}

inline uint32_t is_alive(uint32_t ent) { return slot<EntTest>(0x00922210)(ent); }  // GScr_IsAlive
inline int32_t level_count() { return title_global<int32_t>(0x008cd778); }      // AI kept at node 0

// The level object's handle word (g_levelScriptObject + 4; a null object reads address 0).
inline uint32_t level_handle() {
    const uint32_t object = title_global<uint32_t>(0x00922ef8);
    return *pointer<uint32_t>(object ? object + 4 : 0);
}
// ScrEnt_GetOrigin into a copy.
inline void origin_of(uint32_t* out, uint32_t ent) {
    uint32_t buffer[3];
    const uint32_t* o = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer, ent);
    out[0] = o[0];
    out[1] = o[1];
    out[2] = o[2];
}

// Scr_CreateThread of a {0x007f4a28, function (BroScr_ActorRunToEntIgnoringAll_2), ai, *node} functor on `ai`.
inline void run_to_node(uint32_t ai, const uint32_t* node, uint32_t function = own_address(&BroScr_ActorRunToEntIgnoringAll_2)) {
    uint32_t* functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f4a28;
        functor[1] = function;
        functor[2] = ai;
        functor[3] = *node;
    }
    slot<CreateThread>(0x0092216c)(0, 0, 0, ai, functor, 0);  // Scr_CreateThread
}

// The entity named prefix + "_" + n (GScr_GetEntArray by targetname 0x0d786b4c); temporaries released.
inline uint32_t numbered_ent(const ScriptString* prefix, int32_t n) {
    ScriptString joined;
    ScriptString name;
    ScriptString_PlusInt(&name, ScriptString_ConcatCString(&joined, prefix, pointer<const char>(0x0082b1b8)), n);  // "_"
    const uint32_t ent = slot<GetEnt>(0x009221c4)(&name, 0x0d786b4c, 0, 0, 0);  // GScr_GetEntArray
    scr_release(&name);
    scr_release(&joined);
    return ent;
}
// GScr_Spawn("script_origin", origin 0).
inline uint32_t spawn_origin() {
    ScriptString classname;
    scr_string_from(&classname, 0x0083ab88);  // "script_origin"
    const float origin[3] = {0.0f, 0.0f, 0.0f};
    const uint32_t ent = slot<Spawn>(0x009222f0)(&classname, origin, 0);  // GScr_Spawn
    scr_release(&classname);
    return ent;
}
// The entity's string field `key` = `value` (inlined: a missing field added as a copy of the empty string).
inline void set_string(uint32_t ent, uint32_t key, const ScriptString* value) {
    ScriptKeyTable* const table = ent_script_object(ent);
    uint32_t* slot_word = ScriptKeyTable_Find(table, key);
    if (!slot_word) {
        ScriptString empty;
        ScriptString copy;
        ScriptString_Copy(&copy, ScriptString_FromRep(&empty, nullptr));
        slot_word = ScriptKeyTable_Set(table, key, *reinterpret_cast<const uint32_t*>(&copy));  // takes the copy
        scr_release(&empty);
    }
    ScriptString_Assign(reinterpret_cast<ScriptString*>(slot_word), value);
}
// Scr_CreateThread of a {0x007f5ba0, function, ent} functor on `ent`.
inline void start_on(uint32_t function, uint32_t ent) {
    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0xc);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f5ba0;
        functor[1] = function;
        functor[2] = ent;
    }
    slot<CreateThread>(0x0092216c)(0, 0, 0, ent, functor, 0);  // Scr_CreateThread
}
// The inlined append: grown to count + 4 (Scr_Alloc, zeroed, copied, old block freed) when full.
inline void push_back_inline(DynArray* array, uint32_t value) {
    if (array->size >= array->capacity) {
        const uint32_t capacity = array->size + 4;
        uint32_t* const data = Scr_Alloc(capacity * 4, 4);
        for (uint32_t* p = data; p != data + capacity; ++p)
            if (p) *p = 0;
        for (uint32_t i = 0; i < array->size; ++i) data[i] = array->data[i];
        if (array->data) Scr_Free(array->data);
        array->data = data;
        array->capacity = capacity;
    }
    array->data[array->size] = value;
    ++array->size;
}
inline DynArray* level_array(uint32_t offset) { return pointer<DynArray>(title_global<uint32_t>(0x00922ef8) + offset); }

}  // namespace

void XB_CDECL BroScr_HangarChainMoverThink(uint32_t ent) {
    slot<EntPair>(0x00922188)(level_handle(), 0x011066c4);  // Scr_EndOn
    DynArray axis = {};
    while (count_of(found_array(ent, kNodes)) > 1) {
        Scr_Wait(static_cast<float>(
            slot<long double XB_CDECL(float, float)>(0x00922238)(1.0f, 2.0f)));  // Scr_RandomFloatRange
        if (found_array(ent, kAI)->size == 0) continue;

        // The node-0 radius, squared.
        float radius_sq;
        if (int_defined(*element_at(found_array(ent, kNodes), 0), kRadius) &&
            static_cast<float>(static_cast<float>(static_cast<int32_t>(
                                   *int_slot(*element_at(get_array(ent, kNodes), 0), kRadius))) > 64.0f) != 0.0f) {
            const uint32_t* const a = int_slot(*element_at(get_array(ent, kNodes), 0), kRadius);
            const uint32_t* const b = int_slot(*element_at(get_array(ent, kNodes), 0), kRadius);
            const int32_t sq = static_cast<int32_t>(*b) * static_cast<int32_t>(*a);
            radius_sq = static_cast<uint32_t>(sq) == undefined_int() ? title_global<float>(0x00922eac)
                                                                      : static_cast<float>(sq);
        } else {
            const float radius = title_global<float>(0x008cd770);
            radius_sq = radius * radius;
        }

        // Any living axis AI near node 0 or node 1: wait.
        {
            ScriptString team;
            scr_string_from(&team, 0x0083208c);  // "axis"
            slot<GetAI>(0x009223d0)(&axis, &team);  // GScr_GetAI
            scr_release(&team);
        }
        int32_t near = 0;
        for (uint32_t i = 1; static_cast<int32_t>(i - 1) < count_of(&axis); ++i) {
            if (!is_alive(*element_at(&axis, i - 1))) continue;
            uint32_t from[3];
            origin_of(from, *element_at(&axis, i - 1));
            uint32_t to[3];
            origin_of(to, *element_at(found_array(ent, kNodes), 0));
            const long double distance_sq =
                slot<long double XB_CDECL(const uint32_t*, const uint32_t*)>(0x0092226c)(to, from);  // Scr_DistanceSquared
            const float radius = radius_sq;
            if (static_cast<float>(radius > distance_sq) != 0.0f) ++near;
        }
        if (near > 0) continue;
        near = 0;
        for (uint32_t i = 1; static_cast<int32_t>(i - 1) < count_of(&axis); ++i) {
            if (!is_alive(*element_at(&axis, i - 1))) continue;
            uint32_t from[3];
            origin_of(from, *element_at(&axis, i - 1));
            uint32_t to[3];
            origin_of(to, *element_at(found_array(ent, kNodes), 1));
            const long double distance_sq =
                slot<long double XB_CDECL(const uint32_t*, const uint32_t*)>(0x0092226c)(to, from);  // Scr_DistanceSquared
            const float radius = radius_sq;
            if (static_cast<float>(radius > distance_sq) != 0.0f) ++near;
        }
        if (near > 0) continue;

        // Clear: after 0xce19d6b0 (busy flag set) every living AI runs to node 1, each awaited
        // (node 1's 0x455955a2) and 0.5 s apart; then node 0 is dropped.
        Scr_WaitTill(ent, 0xce19d6b0);
        *bool_slot(ent, kBusy) = 1;
        for (uint32_t i = 1; static_cast<int32_t>(i - 1) < count_of(found_array(ent, kAI)); ++i) {
            if (!is_alive(*element_at(found_array(ent, kAI), i - 1))) continue;
            queue_notify(*element_at(found_array(ent, kAI), i - 1), 0xb633d42e);
            const uint32_t* const node = element_at(found_array(ent, kNodes), 1);
            const uint32_t ai = *element_at(found_array(ent, kAI), i - 1);
            run_to_node(ai, node, own_address(&BroScr_ActorRunToEntIgnoringAll));  // BroScr_ActorRunToEntIgnoringAll
            Scr_WaitTill(*element_at(found_array(ent, kNodes), 1), 0x455955a2);
            Scr_Wait(0.5f);
        }
        *bool_slot(ent, kBusy) = 0;
        erase_at(found_array(ent, kNodes), 0);
    }
    slot<void XB_CDECL(uint32_t)>(0x00922540)(ent);  // GScr_Delete
    if (axis.data) Scr_Free(axis.data);
}

void XB_CDECL BroScr_HangarChainAIThink(uint32_t ent) {
    slot<EntPair>(0x00922188)(level_handle(), 0x011066c4);  // Scr_EndOn
    DynArray ready = {};
    DynArray allies = {};
    for (;;) {
        if (count_of(found_array(ent, kNodes)) <= 1) break;
        Scr_Wait(static_cast<float>(
            slot<long double XB_CDECL(float, float)>(0x00922238)(3.0f, 5.0f)));  // Scr_RandomFloatRange
        if (found_array(ent, kAI)->size == 0) continue;

        // The node-0 radius, squared.
        float radius_sq;
        if (int_defined(*element_at(found_array(ent, kNodes), 0), kRadius) &&
            static_cast<float>(static_cast<float>(static_cast<int32_t>(
                                   *int_slot(*element_at(get_array(ent, kNodes), 0), kRadius))) > 64.0f) != 0.0f) {
            const uint32_t* const a = int_slot(*element_at(get_array(ent, kNodes), 0), kRadius);
            const uint32_t* const b = int_slot(*element_at(get_array(ent, kNodes), 0), kRadius);
            const int32_t sq = static_cast<int32_t>(*b) * static_cast<int32_t>(*a);
            radius_sq = static_cast<uint32_t>(sq) == undefined_int() ? title_global<float>(0x00922eac)
                                                                      : static_cast<float>(sq);
        } else {
            const float radius = title_global<float>(0x008cd774);
            radius_sq = radius * radius;
        }

        // The living AI at node 0.
        own_function<Resize>(&DynArray_Resize)(&ready, 0, 0);  // DynArray_Resize
        for (uint32_t i = 1; static_cast<int32_t>(i - 1) < count_of(found_array(ent, kAI)); ++i) {
            if (!entity_handle_is_valid(*element_at(found_array(ent, kAI), i - 1))) continue;
            if (!is_alive(*element_at(found_array(ent, kAI), i - 1))) continue;
            if (*int_slot(*element_at(get_array(ent, kAI), i - 1), kChainPos) != 0) continue;
            DynArray_PushBack(reinterpret_cast<uint8_t*>(&ready), element_at(get_array(ent, kAI), i - 1));
        }

        // Too many at node 0: send the extra ones to random later nodes.
        if (count_of(&ready) > level_count()) {
            Scr_WaitTill(ent, 0xce19d6b0);
            *bool_slot(ent, kBusy) = 1;
            while (count_of(&ready) > level_count()) {
                const uint32_t pick = slot<uint32_t XB_CDECL(uint32_t)>(0x0092222c)(  // Scr_RandomInt
                                          found_array(ent, kNodes)->size - 1) + 1;
                queue_notify(*element_at(&ready, 0), 0xb633d42e);
                *int_slot(*element_at(&ready, 0), kChainPos) = pick;
                const uint32_t* const node = element_at(found_array(ent, kNodes), pick);
                run_to_node(*element_at(&ready, 0), node);
                erase_at(&ready, 0);
                Scr_Wait(1.0f);
            }
            *bool_slot(ent, kBusy) = 0;
        }

        // The allies and the player near node 0.
        {
            ScriptString team;
            scr_string_from(&team, 0x00832084);  // "allies"
            slot<GetAI>(0x009223d0)(&allies, &team);  // GScr_GetAI
            scr_release(&team);
        }
        DynArray_PushBack(reinterpret_cast<uint8_t*>(&allies), pointer<uint32_t>(0x009236dc));  // the player
        int32_t near = 0;
        for (uint32_t i = 1; static_cast<int32_t>(i - 1) < count_of(&allies); ++i) {
            if (!is_alive(*element_at(&allies, i - 1))) continue;
            uint32_t from[3];
            origin_of(from, *element_at(&allies, i - 1));
            uint32_t to[3];
            origin_of(to, *element_at(found_array(ent, kNodes), 0));
            const long double distance_sq =
                slot<long double XB_CDECL(const uint32_t*, const uint32_t*)>(0x0092226c)(to, from);  // Scr_DistanceSquared
            const float radius = radius_sq;
            if (static_cast<float>(radius > distance_sq) != 0.0f) ++near;
        }

        if (near >= 2) {
            // Move up: the AI at node 0 go to node 1, node 0 is dropped, every chain_pos counts down.
            Scr_WaitTill(ent, 0xce19d6b0);
            *bool_slot(ent, kBusy) = 1;
            for (uint32_t i = 1; static_cast<int32_t>(i - 1) < count_of(&ready); ++i) {
                queue_notify(*element_at(found_array(ent, kAI), i - 1), 0xb633d42e);
                *int_slot(*element_at(&ready, i - 1), kChainPos) = 1;
                const uint32_t* const node = element_at(found_array(ent, kNodes), 1);
                run_to_node(*element_at(&ready, i - 1), node);
                Scr_Wait(1.0f);
            }
            erase_at(found_array(ent, kNodes), 0);
            for (uint32_t i = 1; static_cast<int32_t>(i - 1) < count_of(found_array(ent, kAI)); ++i) {
                if (!entity_handle_is_valid(*element_at(found_array(ent, kAI), i - 1))) continue;
                if (!is_alive(*element_at(found_array(ent, kAI), i - 1))) continue;
                if (!int_defined(*element_at(found_array(ent, kAI), i - 1), kChainPos)) continue;
                --*int_slot(*element_at(found_array(ent, kAI), i - 1), kChainPos);
            }
            *bool_slot(ent, kBusy) = 0;
            continue;
        }

        // Too few at node 0: call back the missing count from further up.
        int32_t missing = level_count() - count_of(&ready);
        if (missing <= 0) continue;
        Scr_WaitTill(ent, 0xce19d6b0);
        *bool_slot(ent, kBusy) = 1;
        for (uint32_t i = 1; static_cast<int32_t>(i - 1) < count_of(found_array(ent, kAI)); ++i) {
            if (entity_handle_is_valid(*element_at(found_array(ent, kAI), i - 1)) &&
                is_alive(*element_at(found_array(ent, kAI), i - 1)) &&
                *int_slot(*element_at(found_array(ent, kAI), i - 1), kChainPos) != 0) {
                queue_notify(*element_at(found_array(ent, kAI), i - 1), 0xb633d42e);
                *ScrField_chain_pos(*element_at(found_array(ent, kAI), i - 1)) = 0;
                uint32_t* const node = element_at(get_array(ent, kNodes), 0);
                const uint32_t ai = *element_at(get_array(ent, kAI), i - 1);
                title_function<StartEntEnt>(0x0045a6d0)(0, 0, 0, 0, own_address(&BroScr_ActorRunToEntIgnoringAll_2), ai, node);  // Scr_StartThread_ent_ent(BroScr_ActorRunToEntIgnoringAll_2)
                --missing;
                Scr_Wait(1.0f);
            }
            if (missing == 0) break;
        }
        *bool_slot(ent, kBusy) = 0;
    }

    // One node or none: every 3-5 s each living AI runs back to node 0.
    for (;;) {
        Scr_Wait(static_cast<float>(
            slot<long double XB_CDECL(float, float)>(0x00922238)(3.0f, 5.0f)));  // Scr_RandomFloatRange
        for (uint32_t i = 1; static_cast<int32_t>(i - 1) < count_of(get_array(ent, kAI)); ++i) {
            if (!entity_handle_is_valid(*element_at(get_array(ent, kAI), i - 1))) continue;
            if (!is_alive(*element_at(get_array(ent, kAI), i - 1))) continue;
            queue_notify(*element_at(get_array(ent, kAI), i - 1), 0xb633d42e);
            *int_slot(*element_at(get_array(ent, kAI), i - 1), kChainPos) = 0;
            const uint32_t* const node = element_at(get_array(ent, kNodes), 0);
            run_to_node(*element_at(get_array(ent, kAI), i - 1), node);
            Scr_Wait(1.0f);
        }
    }
}

void XB_CDECL BroLvl_oran_SetupEvent4Chains(ScriptString movers, ScriptString chains) {
    queue_notify(level_handle(), 0x011066c4);
    Scr_WaitFrames(0);
    // The previous event's movers and chain owners deleted.
    for (uint32_t i = 0; static_cast<int32_t>(i) < count_of(level_array(0x8c)); ++i)
        slot<void XB_CDECL(uint32_t)>(0x00922540)(*element_at(level_array(0x8c), i));  // GScr_Delete
    for (uint32_t i = 0; static_cast<int32_t>(i) < count_of(level_array(0x98)); ++i)
        slot<void XB_CDECL(uint32_t)>(0x00922540)(*element_at(level_array(0x98), i));  // GScr_Delete
    own_function<Resize>(&DynArray_Resize)(level_array(0x8c), 0, 0);  // DynArray_Resize
    own_function<Resize>(&DynArray_Resize)(level_array(0x98), 0, 0);

    if (movers.rep) {
        int32_t n = 0;
        for (uint32_t ent = numbered_ent(&movers, n); entity_handle_is_valid(ent); ent = numbered_ent(&movers, n)) {
            uint32_t mover = spawn_origin();
            ScriptString name;
            set_string(mover, 0x99ca82d7, ScrCall_ScrEnt_GetTargetname(&ent, &name));
            scr_release(&name);
            own_function<BuildTargetChain>(&BroScr_BuildTargetChain)(mover, ent, 1);  // BroScr_BuildTargetChain (third word unused)
            start_on(own_address(&BroScr_HangarChainMoverThink), mover);  // BroScr_HangarChainMoverThink
            DynArray_PushBack(reinterpret_cast<uint8_t*>(level_array(0x8c)), &mover);
            ++n;
        }
    }
    if (chains.rep) {
        int32_t n = 0;
        for (uint32_t ent = numbered_ent(&chains, n); entity_handle_is_valid(ent); ent = numbered_ent(&chains, n)) {
            const uint32_t owner = spawn_origin();
            ScriptString name;
            set_string(owner, 0x99ca82d7, ScrCall_ScrEnt_GetTargetname(&ent, &name));
            scr_release(&name);
            own_function<BuildTargetChain>(&BroScr_BuildTargetChain)(owner, ent, 0);  // BroScr_BuildTargetChain
            start_on(own_address(&BroScr_HangarChainAIThink), owner);  // BroScr_HangarChainAIThink
            push_back_inline(level_array(0x98), owner);
            ++n;
        }
    }
    title_global<uint8_t*>(0x00922ef8)[0x15c] = 1;
    start_on(own_address(&BroScr_HangarChainUpdateLoop), level_handle());  // BroScr_HangarChainUpdateLoop
    queue_notify(level_handle(), 0x492f14bc);
    scr_release(&movers);
    scr_release(&chains);
}

namespace {

// GetTeam(ent) == text; the team string released before the test.
inline bool team_is(uint32_t ent, uint32_t text) {
    ScriptString team;
    const bool equal = ScriptString_Equals(ScrCall_ScrSentient_GetTeam(&ent, &team), pointer<const char>(text));
    scr_release(&team);
    return equal;
}
// The level list's entry whose targetname (0x99ca82d7) is `ent`'s target (the last match), else `found`.
inline uint32_t chain_by_target(uint32_t ent, uint32_t offset, uint32_t found) {
    for (uint32_t i = 0; static_cast<int32_t>(i) < count_of(level_array(offset)); ++i) {
        ScriptString target;
        const ScriptString* const wanted = ScrCall_ScrEnt_GetTarget(&ent, &target);
        const uint32_t chain = *element_at(level_array(offset), i);
        const bool equal = ScriptString_EqualsString(
            wanted, title_function<ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t)>(0x00458da0)(  // ScriptKeyTable_GetString
                        ent_script_object(chain), 0x99ca82d7));
        scr_release(&target);
        if (equal) found = *element_at(level_array(offset), i);
    }
    return found;
}
// The level list's entry with the fewest AI (array 0x8969d6d6; the first of equals, below `fewest`).
inline uint32_t chain_with_fewest(uint32_t offset, uint32_t found, int32_t fewest) {
    for (uint32_t i = 0; static_cast<int32_t>(i) < count_of(level_array(offset)); ++i) {
        const int32_t count = count_of(get_array(*element_at(level_array(offset), i), kAI));
        if (count < fewest) {
            found = *element_at(level_array(offset), i);
            fewest = count;
        }
    }
    return found;
}
// Scr_CreateThread of a {0x007f4a28, function, ent, chain} functor on `ent`.
inline void start_on_chain(uint32_t function, uint32_t ent, uint32_t chain) {
    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0x10);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f4a28;
        functor[1] = function;
        functor[2] = ent;
        functor[3] = chain;
    }
    slot<CreateThread>(0x0092216c)(0, 0, 0, ent, functor, 0);  // Scr_CreateThread
}

}  // namespace

void XB_CDECL BroScr_JoinHangarChain(uint32_t ent) {
    uint32_t chain = 0;
    bool has_target;
    {
        ScriptString target;
        has_target = ScrCall_ScrEnt_GetTarget(&ent, &target)->rep != nullptr;
        scr_release(&target);
    }
    if (has_target) {
        // The chain named by the AI's target: allies among the movers (+0x8c), others among the owners (+0x98).
        if (team_is(ent, 0x00832084))  // "allies"
            chain = chain_by_target(ent, 0x8c, chain);
        else
            chain = chain_by_target(ent, 0x98, chain);
    } else if (team_is(ent, 0x00832084)) {  // "allies"
        start_on(own_address(&BroScr_SetGoalRadius64UntilNotify), ent);  // BroScr_SetGoalRadius64UntilNotify
        Scr_WaitTill(level_handle(), 0x1b179a21);
        chain = chain_with_fewest(0x8c, chain, 1000);
    } else if (team_is(ent, 0x0083208c)) {  // "axis"
        slot<void XB_CDECL(uint32_t, uint32_t)>(0x00922df4)(ent, 0x42800000);  // ScrSentient_SetFloat_18 (goal radius 64)
        chain = chain_with_fewest(0x98, chain, 1000);
    }
    if (!entity_handle_is_valid(chain)) return;
    DynArray_PushBack(reinterpret_cast<uint8_t*>(get_array(chain, kAI)), &ent);
    if (team_is(ent, 0x00832084)) start_on_chain(own_address(&BroScr_GoToChainNode), ent, chain);  // "allies"
    if (team_is(ent, 0x0083208c)) start_on_chain(own_address(&BroScr_FriendlyChainThink), ent, chain);  // "axis"
}

}  // namespace game
