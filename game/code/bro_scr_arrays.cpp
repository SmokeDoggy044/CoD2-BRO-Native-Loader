#include "bro_scr_arrays.h"

#include "bro_scr_ai_common.h"
#include "bro_scr_nodes_spawner.h"
#include "bro_scr_util.h"
#include "g_script.h"
#include "scr_field.h"
#include "scr_field_instances.h"
#include "g_scr_runtime.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "scr_access.h"
#include "script_string.h"
#include "title_data.h"

namespace game {

using namespace bro_ai;

namespace {

// The compiled `array[i]`: grows the array to i + 1 (capacity i + 4) when i is past its size.
uint32_t* element(DynArray* array, uint32_t index) {
    if (index >= array->size) DynArray_Resize(reinterpret_cast<uint8_t*>(array), index + 1, index + 4);
    return pointer<uint32_t>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(array->data)) + index * 4);
}

// A by-value string array's destructor: each string released, then the data freed.
void release_strings(uint32_t* data, uint32_t size) {
    if (!data) return;
    const uint32_t first = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data));
    const uint32_t end = first + size * 4;
    for (uint32_t p = first; p != end; p += 4) ScriptString_Release(pointer<ScriptString>(p));
    Scr_Free(data);
}

// The random entry index: RandomInt(size), the array grown when it lands past the size
// (DynArray_ReserveString_PROLOGUE, 0x0045c2e0).
uint32_t random_index(DynArray* strings) {
    const uint32_t index = static_cast<uint32_t>(random_int(static_cast<int32_t>(strings->size)));
    if (index >= strings->size)
        title_function<void XB_THISCALL(DynArray*, uint32_t, uint32_t)>(0x0045c2e0)(strings, index + 1, index + 4);
    return index;
}

void call_functor(void* functor, uint32_t* unused, uint32_t* entry) {
    const uint32_t vtable = *static_cast<uint32_t*>(functor);
    title_function<void XB_THISCALL(void*, uint32_t*, uint32_t*)>(title_global<uint32_t>(vtable + 4))(functor, unused,
                                                                                                   entry);
}

void array_remove(DynArray* array, uint32_t value, DynArray* out, const uint32_t* value_ptr) {
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(array->size); ++i) {
        const uint32_t* entry = element(array, i);
        if (*entry == (value_ptr ? *value_ptr : value)) continue;
        uint32_t* destination;
        if (out->capacity == 0) {
            entry = element(array, i);
            if (out->size == 0) DynArray_Resize(reinterpret_cast<uint8_t*>(out), 1, 4);
            destination = out->data;
        } else {
            entry = element(array, i);
            const uint32_t count = out->size;
            DynArray_Resize(reinterpret_cast<uint8_t*>(out), count + 1, count + 4);
            destination = pointer<uint32_t>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(out->data)) + count * 4);
        }
        *destination = *entry;
    }
}

}  // namespace

void XB_CDECL BroScr_SetRandomModel(uint32_t ent, DynArray models) {
    const uint32_t index = random_index(&models);
    const uint32_t model = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(models.data)) + index * 4;
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x00922548)(ent, model, 0);  // GScr_SetModel
    release_strings(models.data, models.size);
}

void XB_CDECL BroScr_EmptyFunc_StrArray(DynArray strings) { release_strings(strings.data, strings.size); }
void XB_CDECL BroScr_EmptyFunc_StrArray_2(uint32_t, DynArray strings) { release_strings(strings.data, strings.size); }
void XB_CDECL BroScr_EmptyFunc_StrArray_3(DynArray strings) { release_strings(strings.data, strings.size); }

ScriptString* XB_CDECL BroScr_RandomArrayString(ScriptString* out, DynArray strings) {
    const uint32_t index = random_index(&strings);
    ScriptString_Copy(out, pointer<ScriptString>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(strings.data)) +
                                                 index * 4));
    release_strings(strings.data, strings.size);
    return out;
}

void XB_CDECL BroScr_DetachRandomModel(uint32_t ent, DynArray models) {
    ScriptString name;
    ScriptString_FromRep(&name, nullptr);
    DynArray copy = {nullptr, 0, 0};
    title_function<void XB_THISCALL(DynArray*, const DynArray*)>(0x0052d4a0)(&copy, &models);  // DynArray_AssignString
    ScriptString chosen;
    const ScriptString* const result = BroScr_RandomArrayString(&chosen, copy);
    ScriptString_Assign(&name, result);
    ScriptString_Release(&chosen);
    slot<void XB_CDECL(uint32_t, ScriptString*)>(0x009224d4)(ent, &name);  // GScr_DetachNoTag
    ScriptString_Release(&name);
    release_strings(models.data, models.size);
}

void XB_CDECL BroScr_SetCreditsNameDefault(uint32_t actor) {
    BroScr_SetCreditsName(actor, title_global<uint32_t>(0x00922ebc));
}

void XB_CDECL BroScr_SetTargetnameFromOrigin(uint32_t ent) {
    uint32_t buffer[3];
    const uint32_t* const o = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(buffer, ent);  // ScrEnt_GetOrigin
    const ScrVec3 origin = {{o[0], o[1], o[2]}};
    ScriptString name;
    ScriptString* const text = ScriptString_FromCStringPlusVec(&name, pointer<const char>(0x00802464), origin);  // "null "
    ScriptString copy;
    ScriptString_Copy(&copy, text);
    slot<void XB_CDECL(uint32_t, ScriptString)>(0x00922b54)(ent, copy);  // ScrEnt_SetTargetname (releases its copy)
    ScriptString_Release(&name);
    *int_slot(ent, 0x070b8a34) = 1;
    uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0xc);  // ScrIface_Alloc
    if (functor) {
        functor[0] = 0x007f5ba0;
        functor[1] = 0x0019a150;  // BroScr_TrackStartFlagLoop
        functor[2] = ent;
    }
    slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(0, 0, 0, ent, functor,
                                                                                               0);  // Scr_CreateThread
}

void XB_CDECL BroScr_KillSpawners(uint32_t group) {
    DynArray spawners = {nullptr, 0, 0};
    slot<void XB_CDECL(DynArray*)>(0x009223d8)(&spawners);  // GScr_GetSpawnerArray
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(spawners.size); ++i) {
        const uint32_t spawner = *element(&spawners, i);
        if (!entity_handle_is_valid(spawner)) continue;
        ScriptKeyTable* const table = title_function<ScriptKeyTable* XB_CDECL(uint32_t)>(0x0011db20)(spawner);
        if (!table) continue;
        const uint32_t* const found = ScriptKeyTable_Find(table, 0x460bf109);
        const uint32_t value = found ? *found : undefined_int();
        if (value == undefined_int()) continue;
        if (group != *int_slot(*element(&spawners, i), 0x460bf109)) continue;
        slot<void XB_CDECL(uint32_t)>(0x00922540)(*element(&spawners, i));  // GScr_Delete
    }
    if (spawners.data) Scr_Free(spawners.data);
}

void XB_CDECL BroScr_ArrayThread(DynArray* ents, uint32_t function) {
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ents->size); ++i) {
        uint8_t* const level = title_global<uint8_t*>(0x00922ef8);  // g_levelScriptObject
        const uint32_t* const self = level ? reinterpret_cast<const uint32_t*>(level + 4) : nullptr;
        uint32_t* const ent = element(ents, i);
        title_function<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*)>(0x0045a6d0)(
            0, 0, 0, 0, function, *self, ent);  // Scr_StartThread_ent_ent
    }
}

void XB_CDECL BroScr_ArrayExclude(uint32_t unused, DynArray* array, DynArray* exclude, void* functor) {
    uint32_t kept[0x400] = {};
    uint32_t count = 0;
    if (exclude->size == 0) {
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(array->size); ++i)
            call_functor(functor, &unused, element(array, i));
        return;
    }
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(array->size); ++i) {
        bool excluded = false;
        for (uint32_t j = 0; static_cast<int32_t>(j) < static_cast<int32_t>(exclude->size); ++j) {
            if (*element(array, i) == exclude->data[j]) {
                excluded = true;
                break;
            }
        }
        if (excluded) continue;
        const uint32_t value = *element(array, i);
        if (count < 0x400) kept[count++] = value;
    }
    for (uint32_t k = 0; static_cast<int32_t>(k) < static_cast<int32_t>(count); ++k)
        call_functor(functor, &unused, &kept[k]);
}

void XB_CDECL BroScr_ArrayRemove(DynArray* array, const uint32_t* value, DynArray* out) {
    array_remove(array, 0, out, value);
}

void XB_CDECL BroScr_ArrayRemove_2(DynArray* array, uint32_t value, DynArray* out) {
    array_remove(array, value, out, nullptr);
}

void XB_CDECL BroScr_NotifyArray(DynArray* ents, ScriptString message) {
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ents->size); ++i) {
        const uint32_t* const ent = element(ents, i);
        uint32_t hash;
        Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&message));
        queue_notify(*ent, hash);
    }
    ScriptString_Release(&message);
}

}  // namespace game

// Entity-array scripts (trigger chains, AI lists, ranges).
namespace game {

using namespace bro_ai;

namespace {

uint32_t* element_at(DynArray* array, uint32_t index) {
    if (index >= array->size) DynArray_Resize(reinterpret_cast<uint8_t*>(array), index + 1, index + 4);
    return pointer<uint32_t>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(array->data)) + index * 4);
}

// The compiled append: an array without storage writes element 0 (grown to 1 when empty), else
// it grows by one; `fetch` re-reads the source element between the capacity test and the growth.
template <typename Fetch>
void append(DynArray* out, Fetch fetch) {
    uint32_t* destination;
    const uint32_t* source;
    if (out->capacity == 0) {
        source = fetch();
        if (out->size == 0) DynArray_Resize(reinterpret_cast<uint8_t*>(out), 1, 4);
        destination = out->data;
    } else {
        source = fetch();
        const uint32_t count = out->size;
        DynArray_Resize(reinterpret_cast<uint8_t*>(out), count + 1, count + 4);
        destination = pointer<uint32_t>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(out->data)) + count * 4);
    }
    *destination = *source;
}

void free_array(const DynArray& array) {
    if (array.data) Scr_Free(array.data);
}

// The "trigger_friendlychain" entities (by classname, key 0xa106e9bd).
void friendly_chain_triggers(DynArray* triggers) {
    ScriptString classname;
    ScriptString_FromCString(&classname, pointer<const char>(0x00839e3c));  // "trigger_friendlychain"
    Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&classname)), 0xa106e9bd,
                    reinterpret_cast<uint8_t*>(triggers), 0);
    ScriptString_Release(&classname);
}

// The trigger's script_chain string (key 0x8007a3d3) equals `chain`.
bool chain_is(uint32_t trigger, const ScriptString* chain) {
    using GetString = ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t);
    ScriptKeyTable* table = title_function<ScriptKeyTable* XB_CDECL(uint32_t)>(0x0011db20)(trigger);
    if (!table) table = pointer<ScriptKeyTable>(0x00922e94);
    return ScriptString_EqualsString(title_function<GetString>(0x00458da0)(table, 0x8007a3d3), chain);
}

bool chain_defined(uint32_t trigger) {
    bool defined;
    return *ScrField_script_chain_IsDefined(&defined, trigger);
}

long double distance(const uint32_t* a, const uint32_t* b) {
    return slot<long double XB_CDECL(const uint32_t*, const uint32_t*)>(0x00922268)(a, b);  // Scr_Distance (ST0)
}

}  // namespace

void XB_CDECL BroScr_GetFriendlyChainTriggers(ScriptString chain) {
    DynArray triggers = {nullptr, 0, 0};
    friendly_chain_triggers(&triggers);
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(triggers.size); ++i) {
        if (!chain_is(*element_at(&triggers, i), &chain)) continue;
        BroScr_TriggerOff(*element_at(&triggers, i));
    }
    free_array(triggers);
    ScriptString_Release(&chain);
}

void XB_CDECL BroScr_MoveFriendlyChainTrigger(ScriptString chain) {
    DynArray triggers = {nullptr, 0, 0};
    friendly_chain_triggers(&triggers);
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(triggers.size); ++i) {
        if (!chain_is(*element_at(&triggers, i), &chain)) continue;
        const uint32_t trigger = *element_at(&triggers, i);
        bool defined;
        if (!*ScrField_realorigin_IsDefined(&defined, trigger)) continue;
        const uint32_t* const o = reinterpret_cast<const uint32_t*>(ScrField_realorigin(trigger));
        slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t)>(0x00922b24)(trigger, o[0], o[1], o[2]);  // ScrEnt_SetOrigin
    }
    free_array(triggers);
    ScriptString_Release(&chain);
}

void XB_CDECL BroScr_DeleteFriendlyChainTrigger(ScriptString chain) {
    DynArray triggers = {nullptr, 0, 0};
    friendly_chain_triggers(&triggers);
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(triggers.size); ++i) {
        if (!chain_defined(*element_at(&triggers, i))) continue;
        if (!chain_is(*element_at(&triggers, i), &chain)) continue;
        slot<void XB_CDECL(uint32_t)>(0x00922540)(*element_at(&triggers, i));  // GScr_Delete
    }
    free_array(triggers);
    ScriptString_Release(&chain);
}

uint32_t* XB_CDECL BroScr_GetFriendlyChainNode(uint32_t* out, ScriptString chain) {
    DynArray triggers = {nullptr, 0, 0};
    uint32_t trigger = 0;
    friendly_chain_triggers(&triggers);
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(triggers.size); ++i) {
        if (chain_defined(*element_at(&triggers, i)) && chain_is(*element_at(&triggers, i), &chain)) {
            trigger = *element_at(&triggers, i);
            break;
        }
    }
    if (!entity_handle_is_valid(trigger)) {
        ScriptString text;
        ScriptString_FromCString(&text, pointer<const char>(0x008260c0));  // "Tried to get chain "
        ScriptString joined;
        ScriptString message;
        ScriptString_ConcatCString(&message, ScriptString_Concat(&joined, &text, &chain),
                                   pointer<const char>(0x00826088));  // " which does not exist ..."
        ScriptString_Release(&message);
        ScriptString_Release(&joined);
        ScriptString_Release(&text);
        *out = 0;
    } else {
        ScriptString key;
        ScriptString_FromCString(&key, pointer<const char>(0x0082dda8));  // "targetname"
        ScriptString target;
        ScriptString* const name = ScrCall_ScrEnt_GetTarget(&trigger, &target);
        uint32_t node = slot<uint32_t XB_CDECL(ScriptString*, ScriptString*, uint32_t, uint32_t)>(0x009221cc)(
            name, &key, 0, 0);  // GScr_GetNode
        if (node == 0xffffffffu) node = 0;
        ScriptString_Release(&target);
        ScriptString_Release(&key);
        *out = node;
    }
    free_array(triggers);
    ScriptString_Release(&chain);
    return out;
}

void XB_CDECL BroScr_AppendIfValidEnt(DynArray* array, uint32_t ent) {
    if (entity_handle_is_valid(ent)) DynArray_PushBack(reinterpret_cast<uint8_t*>(array), &ent);
}

uint8_t* XB_CDECL BroScr_AnyAliveAITouching(uint8_t* out, uint32_t trigger, ScriptString team) {
    const uint32_t valid = entity_handle_is_valid(trigger);
    if (!valid) {
        *out = static_cast<uint8_t>(valid);
        ScriptString_Release(&team);
        return out;
    }
    using EntTest = uint32_t XB_CDECL(uint32_t);
    DynArray ai = {nullptr, 0, 0};
    DynArray touching = {nullptr, 0, 0};
    slot<void XB_CDECL(DynArray*, ScriptString*)>(0x009223d0)(&ai, &team);  // GScr_GetAI
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ai.size); ++i) {
        if (!static_cast<uint8_t>(slot<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x00922530)(*element_at(&ai, i), trigger)))
            continue;  // GScr_IsTouching
        if (!slot<EntTest>(0x00922210)(*element_at(&ai, i))) continue;  // GScr_IsAlive
        append(&touching, [&] { return element_at(&ai, i); });
    }
    bool any = false;
    if (touching.size != 0) {
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(touching.size); ++i) {
            if (slot<EntTest>(0x00922210)(*element_at(&touching, i))) {
                any = true;
                break;
            }
        }
    }
    *out = any;
    free_array(touching);
    free_array(ai);
    ScriptString_Release(&team);
    return out;
}

uint32_t* XB_CDECL BroScr_GetClosestAI(uint32_t* out, ScrVec3 origin, ScriptString team, uint32_t max_distance) {
    DynArray ai = {nullptr, 0, 0};
    if (team.rep) slot<void XB_CDECL(DynArray*, ScriptString*)>(0x009223d0)(&ai, &team);  // GScr_GetAI
    else slot<void XB_CDECL(DynArray*)>(0x009223d4)(&ai);                                  // GScr_GetAIAll
    if (ai.size == 0) {
        *out = title_global<uint32_t>(0x00922e84);
    } else {
        using Closest = uint32_t* XB_CDECL(uint32_t*, ScrVec3, DynArray*, uint32_t);
        title_function<Closest>(0x001a8a90)(out, origin, &ai, max_distance);  // BroScr_GetClosestFromArray
    }
    free_array(ai);
    ScriptString_Release(&team);
    return out;
}

void XB_CDECL BroScr_CheckAlliesField_ha1763cc1() {
    DynArray allies = {nullptr, 0, 0};
    ScriptString team;
    ScriptString_FromCString(&team, pointer<const char>(0x00832084));  // "allies"
    slot<void XB_CDECL(DynArray*, ScriptString*)>(0x009223d0)(&allies, &team);  // GScr_GetAI
    ScriptString_Release(&team);
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(allies.size); ++i) {
        bool defined;
        ScrField_ha1763cc1_IsDefined(&defined, *element_at(&allies, i));
    }
    free_array(allies);
}

void XB_CDECL BroScr_ArrayRemoveEnt(DynArray* array, uint32_t ent) {
    if (array->capacity == 0) return;
    if (!entity_handle_is_valid(ent)) return;
    DynArray kept = {nullptr, 0, 0};
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(array->size); ++i) {
        if (*element_at(array, i) == ent) continue;
        append(&kept, [&] { return element_at(array, i); });
    }
    if (kept.capacity == 0) {
        ScriptString name;
        ScriptString_FromCString(&name, pointer<const char>(0x0082607c));  // "emptyarray"
        Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&name)), 0xa106e9bd,
                        reinterpret_cast<uint8_t*>(&kept), 0);
        ScriptString_Release(&name);
    }
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(array->size); ++i) {
        const uint32_t* const source = element_at(&kept, i);
        *element_at(array, i) = *source;
    }
    free_array(kept);
}

void XB_CDECL BroScr_DamageAllAiInRange(ScrVec3 position, int32_t radius) {
    const uint32_t two = title_global<uint32_t>(0x0083eba8);  // 2.0f
    DynArray ai = {nullptr, 0, 0};
    slot<void XB_CDECL(DynArray*)>(0x009223d4)(&ai);  // GScr_GetAIAll
    using GetOrigin = uint32_t* XB_CDECL(uint32_t*, uint32_t);
    using DoDamage = void XB_CDECL(uint32_t, uint32_t, const uint32_t*);
    if (static_cast<int32_t>(ai.size) > 0) {
        const float range = static_cast<float>(radius);
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ai.size); ++i) {
            uint32_t buffer[3];
            const uint32_t* const o = slot<GetOrigin>(0x00922b20)(buffer, *element_at(&ai, i));
            const uint32_t origin[3] = {o[0], o[1], o[2]};
            const float d = static_cast<float>(distance(position.v, origin));
            const int32_t outside = d > range ? 1 : 0;
            if (!(static_cast<float>(outside) == 0.0f)) continue;
            const float amount = (range - d) * as_float(two);
            slot<DoDamage>(0x00922558)(*element_at(&ai, i), bits(amount), position.v);  // GScr_DoDamage
        }
    }
    const uint32_t player = title_global<uint32_t>(0x009236dc);
    uint32_t buffer[3];
    const uint32_t* o = slot<GetOrigin>(0x00922b20)(buffer, player);
    const uint32_t origin[3] = {o[0], o[1], o[2]};
    const float d = static_cast<float>(distance(position.v, origin));
    const float range = static_cast<float>(radius);
    const int32_t outside = d > range ? 1 : 0;
    if (static_cast<float>(outside) == 0.0f) {
        const uint32_t p = title_global<uint32_t>(0x009236dc);
        o = slot<GetOrigin>(0x00922b20)(buffer, p);
        const uint32_t from[3] = {o[0], o[1], o[2]};
        const float amount = (range - d) * as_float(two);
        slot<DoDamage>(0x00922558)(title_global<uint32_t>(0x009236dc), bits(amount), from);
    }
    free_array(ai);
}

}  // namespace game

namespace game {

using namespace bro_ai;

void XB_CDECL BroScr_DeleteTeamEntsWithKeyValue(ScriptString value) {
    ScriptString team;
    ScriptString_FromRep(&team, nullptr);
    DynArray ents = {nullptr, 0, 0};
    for (int32_t pass = 0; pass < 2; ++pass) {
        if (pass == 0) ScriptString_AssignCString(&team, pointer<const char>(0x0083208c));  // "axis"
        else ScriptString_AssignCString(&team, pointer<const char>(0x00832084));            // "allies"
        Scr_GetEntArray(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&team)), 0x003b984d,
                        reinterpret_cast<uint8_t*>(&ents), 0);
        for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(ents.size); ++i) {
            const uint32_t ent = *element(&ents, i);
            if (!entity_handle_is_valid(ent)) continue;
            ScriptKeyTable* const table = title_function<ScriptKeyTable* XB_CDECL(uint32_t)>(0x0011db20)(ent);
            if (!table) continue;
            const uint32_t* const found = ScriptKeyTable_Find(table, 0x8109a0a9);
            if ((found ? *found : undefined_int()) == undefined_int()) continue;
            ScriptString text;
            ScriptString_FromInt(&text, static_cast<int32_t>(*int_slot(*element(&ents, i), 0x8109a0a9)));
            const bool equal = ScriptString_EqualsString(&value, &text);
            ScriptString_Release(&text);
            if (!equal) continue;
            const uint32_t target = *element(&ents, i);
            uint32_t* const functor = slot<uint32_t* XB_CDECL(uint32_t)>(0x00922928)(0xc);  // ScrIface_Alloc
            if (functor) {
                functor[0] = 0x007f5ba0;
                functor[1] = 0x0018c090;  // BroScr_DeleteAfterFrame
                functor[2] = target;
            }
            slot<void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*, uint32_t)>(0x0092216c)(
                0, 0, 0, target, functor, 0);  // Scr_CreateThread
        }
    }
    if (ents.data) Scr_Free(ents.data);
    ScriptString_Release(&team);
    ScriptString_Release(&value);
}

}  // namespace game
