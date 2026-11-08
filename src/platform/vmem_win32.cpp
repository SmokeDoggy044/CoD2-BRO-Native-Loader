#include "vmem.h"

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

}  // namespace platform::vmem
