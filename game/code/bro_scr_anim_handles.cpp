#include "bro_scr_anim_handles.h"

#include "title_data.h"

namespace game {

namespace {

struct AnimHandleEntry {
    uint32_t hash;
    uint32_t global;
    uint32_t scratch;  // register the get leaf loads the global into first: 0 EAX, 1 ECX, 2 EDX
                       // (ECX leaves - BroScr_BAnimHandles_mg42 - would need an asm entry)
};

#include "bro_scr_anim_handles_tables.inc"

// One generated table as the compare tree: the keys are sorted, so a binary search finds the
// leaf the tree reaches. EDX:EAX = (EDX after the leaf, EAX); ECX is never written here.
template <size_t N>
uint64_t AnimHandles_Dispatch(const AnimHandleEntry (&table)[N], uint32_t hash, uint32_t value, uint32_t* out) {
    size_t lo = 0, hi = N;
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (table[mid].hash < hash)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (lo == N || table[lo].hash != hash) return uint64_t(value) << 32;  // xor eax,eax; EDX untouched
    uint32_t& global = title_global<uint32_t>(table[lo].global);
    if (!out) {
        global = value;
        return (uint64_t(value) << 32) | value;
    }
    const uint32_t handle = global;
    *out = handle;
    const uint32_t result = global;  // re-read after the store, as the original does
    return (uint64_t(table[lo].scratch == 2 ? handle : value) << 32) | result;
}

}  // namespace

uint64_t XB_REGS3 XB_CDECL BroScr_BAnimHandles_generic_human(uint32_t hash, uint32_t value, uint32_t* out) {
    return AnimHandles_Dispatch(kBroScr_BAnimHandles_generic_human, hash, value, out);
}

}  // namespace game
