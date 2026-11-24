#include "stack_arena.h"

#include <map>
#include <mutex>
#include <vector>

#include "log.h"
#include "vmem.h"

namespace platform::stack_arena {

namespace {

// Free in every process we measured (11-22: 0x14000000-0x7fc3d000 unused; the game's virtual range ends at
// 0x14000000, its contiguous memory starts at 0x80000000). 1 GB: about 1000 script fibers of 1 MB. 512 MB was full
// on Omaha Beach (11-23: 512+ script threads alive at once, deepest stack use 396 KB).
constexpr uintptr_t kPreferredBase = 0x20000000;
constexpr size_t kBytes = 1024u << 20;
constexpr size_t kPage = 4096;

struct Arena {
    std::mutex mutex;
    bool tried = false;
    uintptr_t base = 0, end = 0, next = 0;
    std::map<size_t, std::vector<uintptr_t>> free;  // by size, last released at the back
    bool warned = false;
};

Arena& arena() {
    static Arena a;
    return a;
}

}  // namespace

void* reserve(size_t bytes) {
    bytes = (bytes + kPage - 1) & ~(kPage - 1);
    Arena& a = arena();
    {
        std::lock_guard lock(a.mutex);
        if (!a.tried) {
            a.tried = true;
            if (void* p = vmem::reserve(kPreferredBase, kBytes)) {
                a.base = a.next = reinterpret_cast<uintptr_t>(p);
                a.end = a.base + kBytes;
            } else {
                LOG_WARN("memory", "the stack range at %08zx is taken: stacks go where the system puts them (runs may "
                         "differ where the game reads stale stack words)", static_cast<size_t>(kPreferredBase));
                a.warned = true;
            }
        }
        if (auto it = a.free.find(bytes); it != a.free.end() && !it->second.empty()) {
            const uintptr_t p = it->second.back();
            it->second.pop_back();
            return reinterpret_cast<void*>(p);
        }
        if (a.base && a.end - a.next >= bytes) {
            const uintptr_t p = a.next;
            a.next += bytes;
            return reinterpret_cast<void*>(p);
        }
        if (a.base && !a.warned) {
            a.warned = true;
            LOG_WARN("memory", "the stack range is full (%zu MB): further stacks go where the system puts them",
                     kBytes >> 20);
        }
    }
    return vmem::reserve(0, bytes);
}

void release(void* address, size_t bytes) {
    bytes = (bytes + kPage - 1) & ~(kPage - 1);
    const uintptr_t p = reinterpret_cast<uintptr_t>(address);
    Arena& a = arena();
    std::lock_guard lock(a.mutex);
    if (p >= a.base && p < a.end) {
        vmem::decommit(address, bytes);
        a.free[bytes].push_back(p);
    } else {
        vmem::release(address, bytes);
    }
}

bool contains(uint32_t address) {
    // The replay hash masks these words as stack pointers, so the range stays the first 512 MB (the arena's size
    // when the recordings were made): a wider one also masks floats from 2.0 up and changes every stored hash.
    // Stacks above it have fixed addresses too, so they need no masking to compare runs.
    constexpr size_t kHashedBytes = 512u << 20;
    const Arena& a = arena();
    return a.base && address >= a.base && address < a.base + kHashedBytes;
}

}  // namespace platform::stack_arena
