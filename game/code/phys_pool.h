// The title's fixed-size object pools (ragdolls, capsules, physics objects, vehicle physics):
// shared layout helpers for phys.cpp and ragdoll.cpp.
#pragma once

#include <cstdint>

namespace game {
// Internal linkage per file (no exported copies called through the PLT).
namespace {

template <typename T>
inline T& pool_at(const void* base, uint32_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + offset);
}

// The title's fixed pools: a pointer list (free entries from `count` on), each element's position in
// that list, the storage and the used count.
struct PoolLayout {
    uint32_t pointers, positions, storage, count;
    int32_t size, entries;
};
constexpr PoolLayout kCapsulePool{0xbe0, 0xc08, 0xc30, 0xc34, 0x130, 10};
constexpr PoolLayout kBonePool{0x320, 0x348, 0x370, 0x374, 0x50, 10};
constexpr PoolLayout kRagdollPool{0x10500, 0x105c0, 0x10680, 0x10684, 0x570, 48};
constexpr PoolLayout kRagdollPhysPool{0xafe0, 0xaff8, 0xb010, 0xb014, 0x1d50, 6};
constexpr PoolLayout kPhysObjPool{0x1860, 0x18d8, 0x1950, 0x1954, 0xd0, 30};
constexpr PoolLayout kVehiclePhysPool{0xaa0, 0xab4, 0xac8, 0xacc, 0x220, 5};

// Position of each element, then its pointer (storage re-read for each).
inline void pool_reset(uint8_t* pool, const PoolLayout& l) {
    for (int32_t i = 0; i < l.entries; ++i) {
        pool_at<int32_t>(pool, l.positions + i * 4) = i;
        pool_at<uint32_t>(pool, l.pointers + i * 4) = pool_at<uint32_t>(pool, l.storage) + i * l.size;
    }
    pool_at<uint32_t>(pool, l.count) = 0;
}

inline int32_t pool_index(const uint8_t* pool, const PoolLayout& l, const void* element) {
    return static_cast<int32_t>(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(element)) - pool_at<uint32_t>(pool, l.storage)) / l.size;
}

// Swaps the freed element (list position `slot`) with the last used one.
inline void pool_swap_out(uint8_t* pool, const PoolLayout& l, int32_t index, int32_t slot) {
    const int32_t count = pool_at<int32_t>(pool, l.count) - 1;
    pool_at<int32_t>(pool, l.count) = count;
    const uint32_t last = pool_at<uint32_t>(pool, l.pointers + static_cast<uint32_t>(count) * 4);
    const int32_t last_index = static_cast<int32_t>(last - pool_at<uint32_t>(pool, l.storage)) / l.size;
    const uint32_t freed = pool_at<uint32_t>(pool, l.pointers + static_cast<uint32_t>(slot) * 4);
    pool_at<uint32_t>(pool, l.pointers + static_cast<uint32_t>(slot) * 4) = last;
    pool_at<uint32_t>(pool, l.pointers + static_cast<uint32_t>(pool_at<int32_t>(pool, l.count)) * 4) = freed;
    pool_at<int32_t>(pool, l.positions + static_cast<uint32_t>(index) * 4) = pool_at<int32_t>(pool, l.count);
    pool_at<int32_t>(pool, l.positions + static_cast<uint32_t>(last_index) * 4) = slot;
}

}  // namespace
}  // namespace game
