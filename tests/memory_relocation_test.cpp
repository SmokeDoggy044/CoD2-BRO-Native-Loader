// The kernel's virtual memory range when the default one (0x10000000-0x50000000) is
// partly taken, as by a DLL loaded at its default base 0x10000000 before the loader
// (issue #16): memory_init picks another free range and allocations work in it.

#include <cstring>

#include "test_support.h"
#include "types.h"
#include "vmem.h"

using namespace xbox;

namespace {

constexpr uint32_t kCommit = 0x1000, kReserve = 0x2000, kReadWrite = 0x04;
using NtAllocateFn = uint32_t(XB_STDCALL*)(XbPtr*, uint32_t, uint32_t*, uint32_t, uint32_t);

}  // namespace

int main() {
    constexpr uint32_t kBlocker = 0x10000000, kBlockerSize = 0x10000;
    void* blocker = platform::vmem::reserve(kBlocker, kBlockerSize);
    CHECK(blocker != nullptr, "cannot place the blocker at %08x", kBlocker);
    test::init_kernel();
    const auto allocate = test::find_export<NtAllocateFn>("NtAllocateVirtualMemory");

    XbPtr base = 0;
    uint32_t size = 64 * 1024 * 1024;
    CHECK(allocate(&base, 0, &size, kReserve, kReadWrite) == 0);
    CHECK(base + size <= kBlocker || base >= kBlocker + kBlockerSize, "allocation %08x+%x overlaps the blocker",
          base, size);
    uint32_t page_size = 0x1000;
    XbPtr page = base;
    CHECK(allocate(&page, 0, &page_size, kCommit, kReadWrite) == 0);
    std::memset(from_xbox_ptr<uint8_t>(page), 0xAB, page_size);
    CHECK(from_xbox_ptr<uint8_t>(page)[page_size - 1] == 0xAB);
    return test::finish("memory_relocation_test");
}
