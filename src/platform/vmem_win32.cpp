#include "vmem.h"

#include <algorithm>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace platform::vmem {

namespace {

DWORD to_page_protect(Protection protection) {
    switch (protection) {
        case Protection::None: return PAGE_NOACCESS;
        case Protection::ReadOnly: return PAGE_READONLY;
        case Protection::ReadWrite: return PAGE_READWRITE;
        case Protection::ReadExecute: return PAGE_EXECUTE_READ;
        case Protection::ReadWriteExecute: return PAGE_EXECUTE_READWRITE;
    }
    return PAGE_NOACCESS;
}

}  // namespace

void* reserve(uintptr_t address, size_t size) {
    return VirtualAlloc(reinterpret_cast<void*>(address), size, MEM_RESERVE, PAGE_NOACCESS);
}

bool commit(void* address, size_t size, Protection protection) {
    // MEM_COMMIT of reserved pages gives zeroed pages; committed ones keep their data.
    return VirtualAlloc(address, size, MEM_COMMIT, to_page_protect(protection)) != nullptr;
}

bool decommit(void* address, size_t size) { return VirtualFree(address, size, MEM_DECOMMIT) != 0; }

bool protect(void* address, size_t size, Protection protection) {
    DWORD old = 0;
    return VirtualProtect(address, size, to_page_protect(protection), &old) != 0;
}

bool release(void* address, size_t) { return VirtualFree(address, 0, MEM_RELEASE) != 0; }

void walk(const std::function<void(const Region&)>& visit) {
    constexpr uint64_t kLimit = 1ull << 32;
    uint64_t address = 0;
    MEMORY_BASIC_INFORMATION info;
    while (address < kLimit && VirtualQuery(reinterpret_cast<const void*>(static_cast<uintptr_t>(address)), &info,
                                            sizeof(info)) == sizeof(info)) {
        Region region;
        region.base = reinterpret_cast<uintptr_t>(info.BaseAddress);
        region.size = std::min<uint64_t>(info.RegionSize, kLimit - region.base);
        region.state = info.State == MEM_FREE      ? Region::State::Free
                       : info.State == MEM_RESERVE ? Region::State::Reserved
                                                   : Region::State::Committed;
        region.kind = info.Type == MEM_IMAGE ? Region::Kind::Image
                      : info.Type == MEM_MAPPED ? Region::Kind::Mapped
                                                : Region::Kind::Private;
        region.allocation_base = reinterpret_cast<uintptr_t>(info.AllocationBase);
        if (region.kind == Region::Kind::Image) {
            char path[MAX_PATH];
            const DWORD n = GetModuleFileNameA(static_cast<HMODULE>(info.AllocationBase), path, sizeof(path));
            if (n) {
                region.name.assign(path, n);
                const size_t slash = region.name.find_last_of("\\/");
                if (slash != std::string::npos) region.name.erase(0, slash + 1);
            }
        }
        visit(region);
        if (!region.size) break;
        address = static_cast<uint64_t>(region.base) + region.size;
    }
}

}  // namespace platform::vmem
