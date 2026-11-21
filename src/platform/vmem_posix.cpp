#include "vmem.h"

#include <sys/mman.h>

#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstring>

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

void walk(const std::function<void(const Region&)>& visit) {
    constexpr uint64_t kLimit = 1ull << 32;
    std::FILE* file = std::fopen("/proc/self/maps", "r");
    if (!file) return;
    uint64_t next = 0;  // the end of the previous mapping: anything before a mapping's start is free
    char line[512];
    while (std::fgets(line, sizeof(line), file)) {
        uint64_t start = 0, end = 0, offset = 0, inode = 0;
        char perms[8] = {}, device[16] = {};
        int name_at = 0;
        if (std::sscanf(line, "%" SCNx64 "-%" SCNx64 " %7s %" SCNx64 " %15s %" SCNu64 " %n", &start, &end, perms,
                        &offset, device, &inode, &name_at) < 6)
            continue;
        if (start >= kLimit) break;
        end = std::min(end, kLimit);
        if (start > next) {
            Region gap;
            gap.base = static_cast<uintptr_t>(next);
            gap.size = start - next;
            gap.allocation_base = gap.base;
            visit(gap);
        }
        Region region;
        region.base = static_cast<uintptr_t>(start);
        region.size = end - start;
        region.allocation_base = region.base;
        region.state = std::strncmp(perms, "---", 3) == 0 ? Region::State::Reserved : Region::State::Committed;
        if (name_at > 0) {
            region.name = line + name_at;
            while (!region.name.empty() && (region.name.back() == '\n' || region.name.back() == ' '))
                region.name.pop_back();
            const size_t slash = region.name.find_last_of('/');
            if (slash != std::string::npos) region.name.erase(0, slash + 1);
        }
        if (inode)
            region.kind = region.name.find(".so") != std::string::npos || perms[2] == 'x' ? Region::Kind::Image
                                                                                       : Region::Kind::Mapped;
        visit(region);
        next = end;
    }
    std::fclose(file);
    if (next < kLimit) {
        Region gap;
        gap.base = static_cast<uintptr_t>(next);
        gap.size = kLimit - next;
        gap.allocation_base = gap.base;
        visit(gap);
    }
}

}  // namespace platform::vmem
