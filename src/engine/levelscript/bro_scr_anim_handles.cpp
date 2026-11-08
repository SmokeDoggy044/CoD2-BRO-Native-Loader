#include "bro_scr_anim_handles.h"

#include <cstddef>

#include "title_data.h"

namespace game {

namespace {

struct AnimHandleEntry {
    uint32_t hash;
    uint32_t word;
};

uint32_t anim_handle(uint32_t hash, uint32_t* out, uint32_t value, const AnimHandleEntry* entries, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (entries[i].hash != hash) continue;
        uint32_t& word = title_global<uint32_t>(entries[i].word);
        if (!out) {
            word = value;
            return value;
        }
        *out = word;
        return word;
    }
    return 0;
}

uint32_t* out_pointer(uint32_t word) { return reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(word)); }

using Table = uint32_t XB_REGS3 XB_CDECL(uint32_t, uint32_t, uint32_t);

// The anim sets by name hash (both dispatchers use the same switch).
Table* table_for(uint32_t set, bool* hash_in_eax) {
    struct Set {
        uint32_t hash;
        Table* table;
        bool hash_in_eax;
    };
    static const Set kSets[] = {
        {0x1ebfddc7, &BroScr_AnimHandles_flakvierling_base, true}, {0x070bd901, &BroScr_AnimHandles_stuka, false},
        {0x3b8fe918, &BroScr_AnimHandles_generic_human, true},     {0x8c1b319d, &BroScr_AnimHandles_treeburst, false},
        {0x9b9346ce, &BroScr_AnimHandles_panzeriv, false},         {0xbffbf36a, &BroScr_AnimHandles_tankdeadguys, false},
        {0xbfabe10d, &BroScr_AnimHandles_flakvierling, false},     {0xcaf12b38, &BroScr_AnimHandles_flak88, false},
        {0xdfa4ec83, &BroScr_AnimHandles_willyjeep, false},
    };
    for (const Set& s : kSets)
        if (s.hash == set) {
            *hash_in_eax = s.hash_in_eax;
            return s.table;
        }
    return nullptr;
}

uint32_t call_table(Table* table, bool hash_in_eax, uint32_t anim, uint32_t out, uint32_t value) {
    return hash_in_eax ? table(anim, value, out) : table(out, value, anim);
}

}  // namespace

#define BRO_ANIM_HANDLE_TABLE(name, address, reg, ...)                                           \
    uint32_t XB_REGS3 XB_CDECL name(uint32_t eax, uint32_t edx, uint32_t ecx) {                  \
        static const AnimHandleEntry kEntries[] = {__VA_ARGS__};                                 \
        const bool in_eax = sizeof(#reg) == 4 && #reg[1] == 'a';                                 \
        return anim_handle(in_eax ? eax : ecx, out_pointer(in_eax ? ecx : eax), edx, kEntries, \
                           sizeof(kEntries) / sizeof(kEntries[0]));                              \
    }
#include "bro_scr_anim_handles.inc"
#undef BRO_ANIM_HANDLE_TABLE

uint32_t XB_CDECL BroScr_AnimHandle_GetSet(uint32_t set, uint32_t anim, uint32_t* out, uint32_t value) {
    using GetSet = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t*, uint32_t);
    if (title_function<GetSet>(0x0018b090)(set, anim, out, value)) return 1;  // BroScr_BAnimHandle_GetSet
    bool hash_in_eax = false;
    Table* const table = table_for(set, &hash_in_eax);
    if (!table) return 0;
    return call_table(table, hash_in_eax, anim, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(out)), value);
}

uint32_t XB_CDECL BroScr_AnimHandle_Lookup(uint32_t set, uint32_t anim) {
    if (const uint32_t handle = title_function<uint32_t XB_CDECL(uint32_t, uint32_t)>(0x0018b0f0)(set, anim))
        return handle;  // BroScr_BAnimHandle_Lookup
    bool hash_in_eax = false;
    Table* const table = table_for(set, &hash_in_eax);
    if (!table) return 0;
    uint32_t scratch = 0;
    return call_table(table, hash_in_eax, anim, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&scratch)), 0);
}

uint32_t XB_CDECL BroScr_ReturnTrue_2() {
    using ReturnTrue = uint32_t XB_CDECL();
    title_function<ReturnTrue>(0x0018b0e0)();
    const uint32_t second = title_function<ReturnTrue>(0x0018b0e0)();  // BroScr_ReturnTrue
    return (second & 0xffffff00u) | ((second & 0xff) != 0 ? 1u : 0u);
}

}  // namespace game
