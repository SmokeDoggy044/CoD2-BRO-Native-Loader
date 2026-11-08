#include "bro_scr_squad.h"

#include "bro_scr_array.h"
#include "bro_scr_util.h"
#include "g_scr_runtime.h"
#include "g_script.h"
#include "qcommon_core.h"
#include "scr_access.h"
#include "scr_field.h"
#include "title_data.h"
#include "bro_scr_arrays.h"
#include "levels_twins.h"

// A cdecl title call with two stack words that returns the ECX the callee leaves: the bool-field
// accessors (ScrField_BoolSlot) build a missing field's word from their caller's ECX.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL bro_squad_call2_ecx(uint32_t function, uint32_t a, uint32_t b);
PLATFORM_ASM_ENTRY(bro_squad_call2_ecx,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "call *12(%esp)\n"
                   "addl $8, %esp\n"
                   "movl %ecx, %eax\n"
                   "ret")

namespace game {

namespace {

template <typename T>
T* pointer(uint32_t address) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}
uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

template <typename F>
F* slot(uint32_t address) {
    return title_function<F>(title_global<uint32_t>(address));
}

uint8_t* bytes(DynArray* array) { return reinterpret_cast<uint8_t*>(array); }
uint32_t* as_words(DynArray* array) { return reinterpret_cast<uint32_t*>(array); }

void free_data(const DynArray& array) {
    if (array.data) Scr_Free(array.data);
}

// Element `i` of a word array, grown to i + 1 (capacity i + 4) first when needed.
uint32_t* element(DynArray* array, uint32_t i) { return BroScr_ArrayElement(as_words(array), i); }

// Appends `value` (DynArray_Resize to size + 1, capacity size + 4).
void append(DynArray* array, uint32_t value) {
    const uint32_t n = array->size;
    DynArray_Resize(bytes(array), n + 1, n + 4);
    array->data[n] = value;
}

bool valid(uint32_t ent) { return slot<int32_t XB_CDECL(uint32_t)>(0x00922214)(ent) != 0; }  // Scr_EntityHandleIsValid
void queue_notify(uint32_t ent, uint32_t notify) { slot<void XB_CDECL(uint32_t, uint32_t)>(0x009221a4)(ent, notify); }
int32_t get_count(uint32_t ent) { return slot<int32_t XB_CDECL(uint32_t)>(0x00922b88)(ent); }
void set_count(uint32_t ent, int32_t value) { slot<void XB_CDECL(uint32_t, int32_t)>(0x00922b8c)(ent, value); }

ScriptKeyTable* table_of(uint32_t ent) { return BroScr_ScriptObjectOrEmpty(ent); }

// The value word for `key`, added as `missing` when the table has none (ScriptKeyTable_Set).
uint32_t* field_slot(uint32_t ent, uint32_t key, uint32_t missing) {
    ScriptKeyTable* const table = table_of(ent);
    if (uint32_t* value = ScriptKeyTable_Find(table, key)) return value;
    return ScriptKeyTable_Set(table, key, missing);
}

const ScriptString* key_string(uint32_t ent, uint32_t key) {
    using GetString = ScriptString* XB_THISCALL(ScriptKeyTable*, uint32_t);
    return title_function<GetString>(0x00458da0)(table_of(ent), key);  // ScriptKeyTable_GetString
}

DynArray* field_array(uint32_t ent, uint32_t key) {
    using GetArray = DynArray* XB_THISCALL(ScriptKeyTable*, uint32_t);
    return own_function<GetArray>(&ScriptKeyTable_GetArray)(table_of(ent), key);  // ScriptKeyTable_GetArray
}

template <typename F>
bool string_defined(F* is_defined, uint32_t ent) {
    bool defined;
    return *is_defined(&defined, ent);
}

// Shared body of the two group-field filters (`fill` = GScr_GetAIAll / GScr_GetSpawnerArray).
void filter_by_group_field(uint32_t fill, ScriptString* value, DynArray* out) {
    DynArray all = {};
    DynArray_Resize(bytes(out), 0, 0);
    slot<void XB_CDECL(DynArray*)>(fill)(&all);
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(all.size); ++i) {
        bool defined;
        if (!*ScrField_h2dae9972_IsDefined(&defined, *element(&all, i))) continue;
        if (!ScriptString_EqualsString(ScrField_h2dae9972(*element(&all, i)), value)) continue;
        const uint32_t* const member = element(&all, i);
        const uint32_t n = out->size;
        DynArray_Resize(bytes(out), n + 1, n + 4);
        out->data[n] = *member;
    }
    free_data(all);
    ScriptString_Release(value);
}

}  // namespace

void XB_CDECL BroScr_GetAIByGroupField(ScriptString value, DynArray* out) {
    filter_by_group_field(0x009223d4, &value, out);  // GScr_GetAIAll
}

void XB_CDECL BroScr_GetSpawnersByGroupField(ScriptString value, DynArray* out) {
    filter_by_group_field(0x009223d8, &value, out);  // GScr_GetSpawnerArray
}

void XB_CDECL BroScr_GetAIByGroupFieldCall(ScriptString value) {
    DynArray found = {};
    ScriptString copy;
    ScriptString_Copy(&copy, &value);
    BroScr_GetAIByGroupField(copy, &found);
    free_data(found);
    ScriptString_Release(&value);
}

void XB_CDECL BroScr_ReleaseGunners(uint32_t ent) {
    const auto gunners = [ent]() {
        return reinterpret_cast<DynArray*>(ScriptKeyTable_GetOrAddArray(table_of(ent), 0x2cd69a2a));
    };
    // Element i of the 8-byte {handle, bool} array, grown first when needed.
    const auto gunner = [&](uint32_t i) {
        DynArray* const array = gunners();
        if (i >= array->size) DynArray_Resize8(array, i + 1, i + 4);
        return &array->data[i * 2];
    };
    uint32_t cleared = 0;  // the original's slot keeps its upper bytes (dead stack)
    for (uint32_t i = 0; i < 3; ++i) {
        if (!valid(gunner(i)[0])) continue;
        if (!slot<int32_t XB_CDECL(uint32_t)>(0x00922210)(gunner(i)[0])) continue;  // GScr_IsAlive
        queue_notify(gunner(i)[0], 0xa0e65a8e);
        cleared = (cleared & 0xffffff00u) | title_global<uint8_t>(0x00922ed0);
        uint32_t* const slot_words = gunner(i);
        slot_words[0] = 0;
        slot_words[1] = cleared;
    }
}

void XB_CDECL BroScr_UpdateAliveDeadCount(uint32_t ent, uint32_t counted) {
    if (ScriptString_Equals(key_string(ent, 0x22cb4cdd), pointer<const char>(0x00836814))) {  // "special"
        set_count(counted, get_count(counted) + 1);
        return;
    }
    const uint32_t* const alive = field_slot(ent, 0xaa7e54b7, title_global<uint32_t>(0x00922ec0));
    const uint32_t* const dead = field_slot(ent, 0x846e35f4, title_global<uint32_t>(0x00922ec0));
    if (*dead != *alive) return;
    *ScrField_alive_num(ent) = 0;
    *ScrField_dead_num(ent) = 0;
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(field_array(ent, 0xc548bcb7)->size); ++i) {
        const uint32_t* const member = element(field_array(ent, 0xc548bcb7), i);
        const int32_t count = get_count(*member) + 1;
        set_count(*member, count);
    }
}

void XB_CDECL BroScr_GetPanzerfaustRespawns(DynArray* out) {
    DynArray found = {};
    ScriptString classname;
    ScriptString_FromCString(&classname, pointer<const char>(0x0082473c));  // "weapon_panzerfaust"
    Scr_GetEntArray(address_of(&classname), 0xa106e9bd, bytes(&found), 0);
    ScriptString_Release(&classname);
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(found.size); ++i) {
        ScriptString targetname;
        const bool respawn = ScriptString_Equals(ScrCall_ScrEnt_GetTargetname(element(&found, i), &targetname),
                                                 pointer<const char>(0x00824734));  // "respawn"
        ScriptString_Release(&targetname);
        if (!respawn) continue;
        own_function<void XB_CDECL(DynArray*, uint32_t)>(&BroScr_AppendIfValidEnt)(out, *element(&found, i));  // BroScr_AppendIfValidEnt
    }
    free_data(found);
}

uint32_t* XB_CDECL BroScr_GetClosestNodeToEnt(uint32_t* out, uint32_t ent, ScriptString name) {
    DynArray nodes = {};
    if (!name.rep) {
        Scr_GetNodeArrayNoKey(as_words(&nodes));
    } else {
        ScriptString key;
        ScriptString_FromCString(&key, pointer<const char>(0x0082dda8));  // "targetname"
        Scr_GetNodeArray(address_of(&name), address_of(&key), as_words(&nodes));
        ScriptString_Release(&key);
    }
    if (nodes.size == 0) {
        *out = title_global<uint32_t>(0x00922ea0);
    } else {
        DynArray skip = {};
        uint32_t origin_buffer[3];
        const uint32_t* const origin = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(origin_buffer, ent);
        using Closest = void XB_CDECL(uint32_t*, uint32_t, uint32_t, uint32_t, DynArray*, DynArray*);
        own_function<Closest>(&BroScr_GetClosestNodeFromArray)(out, origin[0], origin[1], origin[2], &nodes, &skip);  // BroScr_GetClosestNodeFromArray
        free_data(skip);
    }
    free_data(nodes);
    ScriptString_Release(&name);
    return out;
}

uint32_t* XB_CDECL BroScr_TakeSpawnerSaveState(uint32_t* out) {
    DynArray spawners = {};
    ScriptString team;
    ScriptString_FromCString(&team, pointer<const char>(0x0083208c));  // "axis"
    slot<void XB_CDECL(DynArray*, ScriptString*)>(0x009223dc)(&spawners, &team);  // GScr_GetSpawnerTeamArray
    ScriptString_Release(&team);
    const uint32_t index = *pointer<uint32_t>(title_global<uint32_t>(0x00922ee0) + 0x324);
    const uint32_t spawner = *element(&spawners, index);
    const int32_t count = get_count(spawner);
    *field_slot(spawner, 0x56aed3da, title_global<uint32_t>(0x00922ec0)) = static_cast<uint32_t>(count);
    uint32_t origin_buffer[3];
    const uint32_t* const origin = slot<uint32_t* XB_CDECL(uint32_t*, uint32_t)>(0x00922b20)(origin_buffer, spawner);
    const uint32_t x = origin[0], y = origin[1], z = origin[2];
    uint32_t* const saved = reinterpret_cast<uint32_t*>(ScrField_hcc045c84(spawner));
    saved[0] = x;
    saved[1] = y;
    saved[2] = z;
    set_count(spawner, 1);
    *out = spawner;
    free_data(spawners);
    return out;
}

void XB_CDECL BroScr_SetRandomGoalNode(uint32_t ent) {
    DynArray nodes = {};
    ScriptString key;
    ScriptString_FromCString(&key, pointer<const char>(0x0082dda8));  // "targetname"
    const uint32_t* const node = field_slot(ent, 0x05374cc6, 0);
    ScriptString target;
    Scr_GetNodeArray(address_of(ScrCall_ScrPathNode_GetTarget(node, &target)), address_of(&key), as_words(&nodes));
    ScriptString_Release(&target);
    ScriptString_Release(&key);
    using Reserve = void XB_THISCALL(DynArray*, uint32_t, uint32_t);
    if (nodes.size == 0) {
        const uint32_t self = ent;
        title_function<void XB_THISCALL(const uint32_t*, uint32_t)>(0x0011eb30)(&self, 0x05374cc6);  // field removed
    } else {
        const uint32_t index = slot<uint32_t XB_CDECL(uint32_t)>(0x0092222c)(nodes.size);  // Scr_RandomInt
        if (index >= nodes.size) title_function<Reserve>(0x0045d480)(&nodes, index + 1, index + 4);  // DynArray_ReserveInt0
        slot<void XB_CDECL(uint32_t, const uint32_t*)>(0x00922688)(ent, &nodes.data[index]);  // GScr_SetGoalPosFromNode
        if (index >= nodes.size) title_function<Reserve>(0x0045d480)(&nodes, index + 1, index + 4);
        const uint32_t* const chosen = &nodes.data[index];
        *field_slot(ent, 0x05374cc6, 0) = *chosen;
    }
    free_data(nodes);
}

void XB_CDECL BroScr_MortarEntInit(uint32_t ent) {
    *reinterpret_cast<uint8_t*>(field_slot(ent, 0xb2f8556f, title_global<uint8_t>(0x00922ed0))) = 0;
    const uint32_t self = ent;
    ScriptString target;
    const bool has_target = ScrCall_ScrEnt_GetTarget(&self, &target)->rep != nullptr;
    ScriptString_Release(&target);
    if (!has_target) return;
    DynArray targets = {};
    Scr_GetEntArray(address_of(ScrCall_ScrEnt_GetTarget(&self, &target)), 0x0d786b4c, bytes(&targets), 0);
    ScriptString_Release(&target);
    own_function<void XB_THISCALL(DynArray*, DynArray*)>(&DynArray_Assign4_PROLOGUE)(field_array(ent, 0x05c4ceb7), &targets);  // DynArray_Assign4
    *reinterpret_cast<uint8_t*>(ScrField_hb2f8556f(ent, 0)) = 1;
    free_data(targets);
    if (!string_defined(ScrField_h05c4ceb7_IsDefined, ent)) {
        *reinterpret_cast<uint8_t*>(ScrField_hb2f8556f(ent, 0)) = 0;
        return;
    }
    if (!string_defined(ScrField_h16e6cf7e_IsDefined, ent)) return;
    using GetEnt = uint32_t XB_CDECL(const ScriptString*, uint32_t, uint32_t, uint32_t, uint32_t);
    const uint32_t found = slot<GetEnt>(0x009221c4)(ScrField_h16e6cf7e(ent), 0x0d786b4c, 0, 0, 0);  // GScr_GetEntArray
    *ScrField_h28667a5b(ent) = found;
    if (!string_defined(ScrField_h28667a5b_IsDefined, ent)) return;
    slot<void XB_CDECL(uint32_t)>(0x00922568)(*ScrField_h28667a5b(ent));  // GScr_SetEntFlag400
}

void XB_CDECL BroScr_StopRespawning(uint32_t spawner, uint32_t owner) {
    bool defined;
    if (valid(owner)) {
        // The ECX that ScrField_h5f01d656_IsDefined leaves, low byte = its result (the bool slot's word).
        const uint32_t ecx = bro_squad_call2_ecx(0x0012b730, address_of(&defined), owner);
        if (defined) {
            if (*reinterpret_cast<uint8_t*>(ScrField_h18b0887b(spawner, (ecx & 0xffffff00u) | 1))) return;
            *reinterpret_cast<uint8_t*>(ScrField_h18b0887b(spawner, 0)) = 1;
            --*reinterpret_cast<int32_t*>(ScrField_h5f01d656(owner));
            if (static_cast<int32_t>(*ScrField_h5f01d656(owner)) >= 0) return;
            queue_notify(owner, 0x0c34930d);
            DynArray group = {};
            Scr_GetEntArray(address_of(ScrField_hdfe2186c(owner)), 0x2d36ed4e, bytes(&group), 0);
            for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(group.size); ++i)
                queue_notify(*element(&group, i), 0x0c34930d);
            const uint32_t notify_ecx =
                bro_squad_call2_ecx(title_global<uint32_t>(0x009221a4), spawner, 0x0c34930d);  // Scr_QueueNotify
            *reinterpret_cast<uint8_t*>(ScrField_h788065eb(spawner, notify_ecx)) = 0;
            const uint8_t* const level = pointer<uint8_t>(title_global<uint32_t>(0x00922ee0));
            const uint32_t* const level_ent = level ? reinterpret_cast<const uint32_t*>(level + 4) : nullptr;
            ScriptString name;
            ScriptString_ConcatCString(&name, key_string(owner, 0xdfe2186c), pointer<const char>(0x00824824));  // "_stopped_respawning"
            uint32_t hash;
            const uint32_t notify = *Scr_HashScriptString(&hash, reinterpret_cast<const uint8_t* const*>(&name));
            queue_notify(*level_ent, notify);
            ScriptString_Release(&name);
            free_data(group);
            return;
        }
    }
    const uint32_t ecx = bro_squad_call2_ecx(0x0012b730, address_of(&defined), spawner);
    if (!defined) return;
    if (*reinterpret_cast<uint8_t*>(ScrField_h18b0887b(spawner, (ecx & 0xffffff00u) | 1))) return;
    *reinterpret_cast<uint8_t*>(ScrField_h18b0887b(spawner, 0)) = 1;
    --*reinterpret_cast<int32_t*>(ScrField_h5f01d656(spawner));
    const uint32_t count = *ScrField_h5f01d656(spawner);
    if (static_cast<int32_t>(count) >= 0) return;
    *reinterpret_cast<uint8_t*>(ScrField_h788065eb(spawner, count)) = 0;
}

void XB_CDECL BroScr_GetNonSentientsByGroupname(ScriptString name, int32_t count) {
    DynArray group = {};
    Scr_GetEntArray(address_of(&name), 0x2d36ed4e, bytes(&group), 0);
    DynArray picked = {};
    for (uint32_t i = 0; static_cast<int32_t>(i) < static_cast<int32_t>(group.size); ++i) {
        if (slot<int32_t XB_CDECL(uint32_t)>(0x00922220)(*element(&group, i))) continue;  // GScr_IsSentient
        DynArray_PushBack(bytes(&picked), element(&group, i));
    }
    if (static_cast<int32_t>(picked.size) >= 1) {
        uint32_t next = 0;
        for (int32_t n = 0; n < count; ++n) {
            ScriptKeyTable* table = table_of(*element(&picked, next));
            const uint32_t* const value = ScriptKeyTable_Find(table, 0x78063481);
            if (!value || *value == title_global<uint32_t>(0x00922ec0))
                ScriptKeyTable_Set(table_of(*element(&picked, next)), 0x78063481, 0);
            table = table_of(*element(&picked, next));
            const uint32_t* const current = ScriptKeyTable_Find(table, 0x78063481);
            const uint32_t old = current ? *current : title_global<uint32_t>(0x00922ec0);
            ScriptKeyTable_Set(table, 0x78063481, old + 1);
            ++next;
            if (static_cast<int32_t>(next) >= static_cast<int32_t>(picked.size)) next = 0;
        }
    }
    free_data(picked);
    free_data(group);
    ScriptString_Release(&name);
}

}  // namespace game
