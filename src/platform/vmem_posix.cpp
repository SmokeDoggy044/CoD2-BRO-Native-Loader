#include "vmem.h"

#include <sys/mman.h>

namespace platform::vmem {

namespace {

int to_prot(Protection protection) {
    switch (protection) {
        case Protection::None: return PROT_NONE;
        case Protection::ReadOnly: return PROT_READ;
        case Protection::ReadWrite: return PROT_READ | PROT_WRITE;
        case Protection::ReadExecute: return PROT_READ | PROT_EXEC;
        case Protection::ReadWriteExecute: return PROT_READ | PROT_WRITE | PROT_EXEC;
    }
    return PROT_NONE;
}

constexpr int kAnonymous = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;

}  // namespace

void* reserve(uintptr_t address, size_t size) {
    const int flags = kAnonymous | (address ? MAP_FIXED_NOREPLACE : 0);
    void* result = mmap(reinterpret_cast<void*>(address), size, PROT_NONE, flags, -1, 0);
    if (result == MAP_FAILED) return nullptr;
    if (address && result != reinterpret_cast<void*>(address)) {  // kernels without MAP_FIXED_NOREPLACE
        munmap(result, size);
        return nullptr;
    }
    return result;
}

bool commit(void* address, size_t size, Protection protection) {
    // Decommitted pages were replaced by fresh mappings, so they read as zero.
    return mprotect(address, size, to_prot(protection)) == 0;
}

bool decommit(void* address, size_t size) {
    // Mapping fresh anonymous pages over the range frees the old ones.
    return mmap(address, size, PROT_NONE, kAnonymous | MAP_FIXED, -1, 0) != MAP_FAILED;
}

bool protect(void* address, size_t size, Protection protection) {
    return mprotect(address, size, to_prot(protection)) == 0;
}

bool release(void* address, size_t size) { return munmap(address, size) == 0; }

}  // namespace platform::vmem
