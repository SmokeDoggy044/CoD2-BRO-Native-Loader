#include "param_set.h"

#include <cstring>

#include "title_data.h"

namespace game {

namespace {

constexpr uint32_t kParamListVtable = 0x008376c0;
constexpr uint32_t kParamSetVtable = 0x008376b8;

void Z_Free(void* p) { title_function<void XB_CDECL(void*)>(0x000428d0)(p); }

void set_bit(uint32_t* bits, uint32_t index, bool on) {
    const uint32_t mask = 1u << (index & 0x1f);
    uint32_t& word = bits[static_cast<int32_t>(index) >> 5];
    word = on ? word | mask : word & ~mask;
}

// The shared body of ParamSet_AddString/AddValue: `size` bytes of `value` after the 4-byte header.
void add_record(ParamSet* set, uint32_t id, const uint32_t* value, uint32_t flag, uint32_t size) {
    if (set->failed) return;
    const uint8_t* definition = set->definition;
    const uint16_t key = static_cast<uint16_t>(id);
    const uint32_t count = *reinterpret_cast<const uint16_t*>(definition + 0x1e);
    const uint8_t* types = *reinterpret_cast<const uint8_t* const*>(definition + 0x30);
    uint32_t i = 0;
    if (count != 0) {
        const uint16_t* const* entries = *reinterpret_cast<const uint16_t* const* const*>(definition + 0x20);
        while (*entries[i] != key) {
            if (++i >= count) break;
        }
    }
    if (i >= count || static_cast<uint16_t>(i) == 0xffff) {
        if (!static_cast<uint8_t>(flag)) set->failed = 1;
        return;
    }
    const uintptr_t base = reinterpret_cast<uintptr_t>(set);
    if ((set->used + base + 1) & 3) {
        uint32_t used = set->used;
        do ++used;
        while ((used + base + 1) & 3);
        set->used = used;
    }
    uint8_t* record = set->buffer + set->used;
    set->used += size + 4;
    if (record) {
        const uint8_t* kinds = *reinterpret_cast<const uint8_t* const*>(types + 8);
        const uint8_t* type_of = *reinterpret_cast<const uint8_t* const*>(types + 4);
        record[1] = kinds[key];
        record[0] = type_of[key];
        std::memcpy(record + 2, &key, 2);
        std::memcpy(record + 4, value, size);
    }
    ParamList_Add(&set->list, static_cast<uint8_t>(flag), record);
}

}  // namespace

ParamList* XB_REGS1 XB_CDECL ParamList_Init(ParamList* list) {
    list->vtable = kParamListVtable;
    list->count = 0;
    std::memset(list->id_present, 0, sizeof(list->id_present));
    std::memset(list->slot_flag, 0, sizeof(list->slot_flag));
    std::memset(list->id_flag, 0, sizeof(list->id_flag));
    return list;
}

ParamList* XB_REGS1 XB_CDECL ParamList_Clear(ParamList* list) {
    for (uint32_t i = 0; i < list->count; ++i) list->records[i] = nullptr;
    list->count = 0;
    std::memset(list->slot_flag, 0, sizeof(list->slot_flag));
    std::memset(list->id_flag, 0, sizeof(list->id_flag));
    std::memset(list->id_present, 0, sizeof(list->id_present));
    return list;
}

PLATFORM_ASM_ENTRY(ParamList_Add_entry,
                   "pushl %esi\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(ParamList_Add) "addl $12, %esp\n"
                   "ret")

ParamList* XB_CDECL ParamList_Add(ParamList* list, uint32_t flag, uint8_t* record) {
    const bool on = static_cast<uint8_t>(flag) != 0;
    set_bit(list->slot_flag, list->count, on);
    uint16_t id;
    std::memcpy(&id, record + 2, 2);
    set_bit(list->id_flag, id, on);
    set_bit(list->id_present, id, true);
    list->slot_of_id[id] = static_cast<uint8_t>(list->count);
    list->records[list->count] = record;
    ++list->count;
    return list;
}

ParamList* XB_THISCALL ParamList_Delete(ParamList* list, uint32_t flags) {
    list->vtable = kParamListVtable;
    ParamList_Clear(list);
    if (flags & 1) Z_Free(list);
    return list;
}

void XB_THISCALL ParamSet_Destruct(ParamSet* set) {
    set->vtable = kParamSetVtable;
    set->used = 0;
    ParamList_Clear(&set->list);
    set->failed = 0;
    set->list.vtable = kParamListVtable;
    ParamList_Clear(&set->list);
}

ParamSet* XB_THISCALL ParamSet_Delete(ParamSet* set, uint32_t flags) {
    ParamSet_Destruct(set);
    if (flags & 1) Z_Free(set);
    return set;
}

void XB_REGS2 XB_STDCALL ParamSet_AddString(uint32_t, ParamSet* set, uint32_t id, const uint32_t* value, uint32_t flag) {
    add_record(set, id, value, flag, 0x80);
}

void XB_REGS2 XB_STDCALL ParamSet_AddValue(uint32_t, ParamSet* set, uint32_t id, const uint32_t* value, uint32_t flag) {
    add_record(set, id, value, flag, 4);
}

PLATFORM_ASM_ENTRY(ParamSet_AddFxOverrides_entry,
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(ParamSet_AddFxOverrides) "addl $8, %esp\n"
                   "ret")

void XB_CDECL ParamSet_AddFxOverrides(const uint8_t* source, ParamSet* set) {
    struct Override {
        uint8_t id;
        uint16_t offset;
        bool name;
    };
    static const Override kOrder[] = {
        {7, 0x18, true},   {4, 0x98, false},  {5, 0x9c, false},  {11, 0xa0, false},
        {2, 0xa4, false},  {8, 0xa8, true},   {9, 0x128, false}, {10, 0x12c, false},
        {1, 0x130, false}, {6, 0x134, true},  {0, 0x1b4, false}, {3, 0x1b8, false},
    };
    const uint32_t enabled = *reinterpret_cast<const uint32_t*>(source);
    const uint32_t flags = *reinterpret_cast<const uint32_t*>(source + 0xc);
    for (const Override& o : kOrder) {
        if (!((enabled >> o.id) & 1)) continue;
        const uint32_t flag = (flags >> o.id) & 0xffffff01;
        const uint32_t* value = reinterpret_cast<const uint32_t*>(source + o.offset);
        if (o.name)
            ParamSet_AddString(0, set, o.id, value, flag);
        else
            ParamSet_AddValue(0, set, o.id, value, flag);
    }
}

PLATFORM_ASM_ENTRY(ParamChoice_GetValue_entry,
                   "pushl %ebx\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(ParamChoice_GetValue) "addl $8, %esp\n"
                   "ret")

uint32_t* XB_CDECL ParamChoice_GetValue(const uint8_t* choice, uint32_t id) {
    const uint8_t* definition = *reinterpret_cast<const uint8_t* const*>(choice + 4);
    const uint32_t count = *reinterpret_cast<const uint16_t*>(definition + 0x1e);
    uint8_t* const* entries = *reinterpret_cast<uint8_t* const* const*>(definition + 0x20);
    int32_t i = -1;
    for (uint32_t j = 0; j < count; ++j) {
        if (*reinterpret_cast<const uint16_t*>(entries[j]) == static_cast<uint16_t>(id)) {
            i = static_cast<int16_t>(j);
            break;
        }
    }
    if (i == -1) return nullptr;
    const int16_t* selection = *reinterpret_cast<const int16_t* const*>(choice);
    const int32_t selected = selection[i];
    if (selected == -1) return nullptr;
    uint32_t* values = *reinterpret_cast<uint32_t* const*>(entries[static_cast<uint16_t>(i)] + 8);
    return values + static_cast<uint16_t>(selected);
}

namespace {

const uint8_t* record_of(const ParamSet* set, uint32_t id) {
    return set->list.records[set->list.slot_of_id[id]];
}

int32_t Stricmp(const char* a, const char* b) {
    return title_function<int32_t XB_CDECL(const char*, const char*)>(0x001f1443)(a, b);  // _stricmp
}

// MSVC's inline strcmp: 0, or -1/1 by the first differing byte (unsigned).
int32_t inline_strcmp(const char* a, const char* b) {
    for (;; ++a, ++b) {
        const uint8_t x = static_cast<uint8_t>(*a), y = static_cast<uint8_t>(*b);
        if (x != y) return x < y ? -1 : 1;
        if (!x) return 0;
    }
}

void debug_break() { title_function<void XB_CDECL()>(0x00042460)(); }  // Sys_DebugBreak

// The definition's entry for parameter `id` (entries[-1] when missing, as the original reads).
const uint8_t* param_entry(const uint8_t* definition, uint16_t id) {
    const uint32_t count = *reinterpret_cast<const uint16_t*>(definition + 0x1e);
    const uint8_t* const* entries = *reinterpret_cast<const uint8_t* const* const*>(definition + 0x20);
    int16_t index = -1;
    for (uint32_t i = 0; i < count; ++i) {
        if (*reinterpret_cast<const uint16_t*>(entries[i]) == id) {
            index = static_cast<int16_t>(i);
            break;
        }
    }
    return entries[index];
}

// A node's value `index` (u16) of its parameter: entry +8 + index * stride (+4); null for -1.
const uint32_t* node_value(const uint8_t* definition, uint16_t id, uint16_t index) {
    const uint8_t* entry = param_entry(definition, id);
    return reinterpret_cast<const uint32_t*>(*reinterpret_cast<const uint32_t*>(entry + 8) +
                                             index * *reinterpret_cast<const uint32_t*>(entry + 4));
}

}  // namespace

uint32_t* XB_REGS3 XB_CDECL ParamNode_CollectChoices(uint32_t* out, uint32_t, const uint8_t* node) {
    const uint32_t count = *reinterpret_cast<const uint16_t*>(node + 4);
    uint32_t* const* children = *reinterpret_cast<uint32_t* const* const*>(node);
    int32_t& best = reinterpret_cast<int32_t&>(out[0x82]);
    for (uint32_t i = 0; i < count; ++i) {
        const int32_t priority = *reinterpret_cast<const int16_t*>(reinterpret_cast<const uint8_t*>(children[i]) + 0xa);
        if (priority >= best) {
            if (priority > best) {
                best = priority;
                out[0x41] = 0;
            }
            if (static_cast<int32_t>(out[0x41]) < 0x40) out[0x42 + out[0x41]++] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(children[i]));
        }
        if (static_cast<int32_t>(out[0]) < 0x40) out[1 + out[0]++] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(children[i]));
    }
    return out;
}

uint8_t XB_REGS3 XB_CDECL ParamSet_TestCondition(const ParamSet* set, uint32_t id, const uint32_t* value) {
    const uint8_t* record = record_of(set, id);
    const uint8_t op = record[1];
    switch (record[0]) {
    case 0: case 6: case 8: {
        // *value is read only for the operators handled (the others read nothing).
        const int32_t r = *reinterpret_cast<const int32_t*>(record + 4);
        const auto v = [value] { return static_cast<int32_t>(*value); };
        switch (op) {
        case 0: return r == v();
        case 2: return r > v();
        case 3: return r < v();
        case 4: return r >= v();
        case 5: return r <= v();
        case 8: return r != v();
        default: return 0;
        }
    }
    case 2: case 0xb: {
        if (op > 8 || op == 1 || op == 6 || op == 7) return 0;
        float r, v;
        std::memcpy(&r, record + 4, 4);
        std::memcpy(&v, value, 4);
        switch (op) {
        case 0: return r == v;
        case 2: return r > v;
        case 3: return v > r;
        case 4: return r >= v;
        case 5: return v >= r;
        case 8: return r != v;
        default: return 0;
        }
    }
    case 4: {
        const char* text = reinterpret_cast<const char*>(record + 4);
        // *value only for operators 0 and 9, as with the integers.
        const auto other = [value] { return reinterpret_cast<const char*>(uintptr_t{*value}); };
        if (op == 0) return inline_strcmp(text, other()) == 0;
        if (op == 9) return Stricmp(text, other()) == 0;
        return 0;
    }
    case 9:
        return op == 0 && *reinterpret_cast<const uint32_t*>(record + 4) == *value;
    default:
        debug_break();
        return 0;
    }
}

int32_t XB_REGS3 XB_CDECL ParamSet_CompareValue(const ParamSet* set, uint32_t id, const uint32_t* value) {
    const uint8_t* record = record_of(set, id);
    switch (record[0]) {
    case 0: case 6: case 8: case 9: {
        const int32_t v = static_cast<int32_t>(*value), r = *reinterpret_cast<const int32_t*>(record + 4);
        if (v < r) return -1;
        return v > r;
    }
    case 2: case 0xb: {
        float v, r;
        std::memcpy(&v, value, 4);
        std::memcpy(&r, record + 4, 4);
        if (r > v) return -1;
        return v > r ? 1 : 0;
    }
    case 4: {
        const char* text = reinterpret_cast<const char*>(record + 4);
        const char* other = reinterpret_cast<const char*>(uintptr_t{*value});
        if (record[1] == 9) return -Stricmp(text, other);
        return -inline_strcmp(text, other);
    }
    default:
        debug_break();
        return 0;
    }
}

void XB_THISCALL ParamSet_Resolve(ParamSet* set, uint32_t* out) {
    if (set->failed) return;
    const uint8_t* definition = set->definition;
    // State per child: 0 = taken, 1 = not taken, 2 = to be tested.
    const uint8_t* stack[0x80];
    uint32_t state[0x100];
    stack[0] = *reinterpret_cast<const uint8_t* const*>(definition + 0x2c);
    int32_t depth = 1;
    do {
        const int32_t top = depth - 1;
        const uint8_t* node = stack[top > 0 ? top : 0];
        if (depth != 0) depth = top;
        const uint16_t word = *reinterpret_cast<const uint16_t*>(node + 0xc);
        if (word & 0x8000) {
            ParamNode_CollectChoices(out, 0, node);
            continue;
        }
        const uint32_t id = word & 0x7fff;
        const uint8_t* const* children = *reinterpret_cast<const uint8_t* const* const*>(node);
        const uint16_t* values = *reinterpret_cast<const uint16_t* const*>(node + 8);
        bool take_all = false;
        if (!(set->list.id_present[id >> 5] & (1u << (id & 0x1f)))) {
            take_all = true;
        } else {
            const int32_t count = *reinterpret_cast<const uint16_t*>(node + 4);
            for (int16_t i = 0; i < count; ++i) {
                const int32_t index = static_cast<int16_t>(values[static_cast<uint16_t>(i)]);
                const bool flagged = set->list.id_flag[id >> 5] & (1u << (id & 0x1f));
                if (static_cast<uint16_t>(index) == 0xffff || !node_value(definition, word, static_cast<uint16_t>(index)))
                    state[i] = flagged ? 0 : 1;
                else
                    state[i] = 2;
            }
            const uint8_t* types = *reinterpret_cast<const uint8_t* const*>(definition + 0x30);
            const uint8_t kind = (*reinterpret_cast<const uint8_t* const*>(types + 8))[id];
            if (kind == 0 || kind == 9) {
                // Sorted values: a binary search for the set's value.
                int32_t low = 0, high = count, mid = count / 2;
                if (mid != -1) for (;;) {
                    const uint16_t index = values[static_cast<uint16_t>(mid)];
                    const uint32_t* value = index == 0xffff ? nullptr : node_value(definition, word, index);
                    if (!value) {
                        --high;
                    } else {
                        const int32_t order = ParamSet_CompareValue(set, id, value);
                        if (order == 0) {
                            state[mid] = 0;
                            break;
                        }
                        if (order > 0)
                            high = mid;
                        else
                            low = mid;
                    }
                    const int32_t next = (low + high) / 2;
                    if (next == mid) break;
                    mid = next;
                }
            } else {
                for (uint32_t i = 0; i < *reinterpret_cast<const uint16_t*>(node + 4); ++i) {
                    if (state[i] != 2) continue;
                    const uint16_t index = values[static_cast<uint16_t>(i)];
                    const uint32_t* value = index == 0xffff ? nullptr : node_value(definition, word, index);
                    state[i] = ParamSet_TestCondition(set, word & 0x7fff, value) ? 0 : 1;
                }
            }
        }
        const uint32_t count = *reinterpret_cast<const uint16_t*>(node + 4);
        for (uint32_t i = 0; i < count; ++i) {
            if ((take_all || state[i] == 0) && depth < 0x80) stack[depth++] = children[static_cast<uint16_t>(i)];
        }
    } while (depth != 0);
}

}  // namespace game
