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

void XB_REGS1 XB_CDECL ParamList_Init(ParamList* list) {
    list->vtable = kParamListVtable;
    list->count = 0;
    std::memset(list->id_present, 0, sizeof(list->id_present));
    std::memset(list->slot_flag, 0, sizeof(list->slot_flag));
    std::memset(list->id_flag, 0, sizeof(list->id_flag));
}

void XB_REGS1 XB_CDECL ParamList_Clear(ParamList* list) {
    for (uint32_t i = 0; i < list->count; ++i) list->records[i] = nullptr;
    list->count = 0;
    std::memset(list->slot_flag, 0, sizeof(list->slot_flag));
    std::memset(list->id_flag, 0, sizeof(list->id_flag));
    std::memset(list->id_present, 0, sizeof(list->id_present));
}

PLATFORM_ASM_ENTRY(ParamList_Add_entry,
                   "pushl %esi\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(ParamList_Add) "addl $12, %esp\n"
                   "ret")

void XB_CDECL ParamList_Add(ParamList* list, uint32_t flag, uint8_t* record) {
    const bool on = static_cast<uint8_t>(flag) != 0;
    set_bit(list->slot_flag, list->count, on);
    uint16_t id;
    std::memcpy(&id, record + 2, 2);
    set_bit(list->id_flag, id, on);
    set_bit(list->id_present, id, true);
    list->slot_of_id[id] = static_cast<uint8_t>(list->count);
    list->records[list->count] = record;
    ++list->count;
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

}  // namespace game
