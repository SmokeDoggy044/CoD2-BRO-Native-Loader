// Virtual memory, contiguous memory and the page budget (xbox/kernel_memory.cpp), called
// through the export table the way the title calls them. Uses the default layout:
// 64 MB physical at 0x80000000, virtual memory at 0x10000000-0x50000000.

#include <cstring>

#include "kernel_memory.h"
#include "test_support.h"
#include "types.h"

using namespace xbox;

namespace {

constexpr uint32_t kCommit = 0x1000, kReserve = 0x2000, kDecommit = 0x4000, kRelease = 0x8000, kFree = 0x10000;
constexpr uint32_t kPrivate = 0x20000, kTopDown = 0x100000;
constexpr uint32_t kReadOnly = 0x02, kReadWrite = 0x04, kWriteCombine = 0x400;
constexpr uint32_t kConflictingAddresses = 0xC0000018, kFreeVmNotAtBase = 0xC000009F, kNoMemory = 0xC0000017;
constexpr uint32_t kMemoryNotAllocated = 0xC00000A0, kInvalidPageProtection = 0xC0000045;
constexpr uint32_t kMB = 1024 * 1024;

using NtAllocateFn = uint32_t(XB_STDCALL*)(XbPtr*, uint32_t, uint32_t*, uint32_t, uint32_t);
using NtFreeFn = uint32_t(XB_STDCALL*)(XbPtr*, uint32_t*, uint32_t);
using NtQueryFn = uint32_t(XB_STDCALL*)(XbPtr, XbMemoryBasicInformation*);
using ContiguousExFn = XbPtr(XB_STDCALL*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
using ContiguousFn = XbPtr(XB_STDCALL*)(uint32_t);
using AddressFn = void(XB_STDCALL*)(XbPtr);
using QueryAddressFn = uint32_t(XB_STDCALL*)(XbPtr);
using SetProtectFn = void(XB_STDCALL*)(XbPtr, uint32_t, uint32_t);
using StatisticsFn = uint32_t(XB_STDCALL*)(XbMmStatistics*);
using CreateStackFn = XbPtr(XB_STDCALL*)(uint32_t, uint32_t);
using DeleteStackFn = void(XB_STDCALL*)(XbPtr, XbPtr);

uint8_t* bytes(XbPtr address) { return from_xbox_ptr<uint8_t>(address); }

bool all_zero(XbPtr address, uint32_t size) {
    for (uint32_t i = 0; i < size; ++i)
        if (bytes(address)[i]) return false;
    return true;
}

}  // namespace

int main() {
    test::init_kernel();
    const auto allocate = test::find_export<NtAllocateFn>("NtAllocateVirtualMemory");
    const auto free_vm = test::find_export<NtFreeFn>("NtFreeVirtualMemory");
    const auto query = test::find_export<NtQueryFn>("NtQueryVirtualMemory");
    const auto contiguous_ex = test::find_export<ContiguousExFn>("MmAllocateContiguousMemoryEx");
    const auto contiguous = test::find_export<ContiguousFn>("MmAllocateContiguousMemory");
    const auto free_contiguous = test::find_export<AddressFn>("MmFreeContiguousMemory");
    const auto allocation_size = test::find_export<QueryAddressFn>("MmQueryAllocationSize");
    const auto physical_address = test::find_export<QueryAddressFn>("MmGetPhysicalAddress");
    const auto query_protect = test::find_export<QueryAddressFn>("MmQueryAddressProtect");
    const auto set_protect = test::find_export<SetProtectFn>("MmSetAddressProtect");
    const auto statistics = test::find_export<StatisticsFn>("MmQueryStatistics");
    const auto create_stack = test::find_export<CreateStackFn>("MmCreateKernelStack");
    const auto delete_stack = test::find_export<DeleteStackFn>("MmDeleteKernelStack");

    auto available_pages = [&] {
        XbMmStatistics s{};
        s.length = sizeof(s);
        CHECK(statistics(&s) == 0);
        return s.available_pages;
    };
    const uint32_t start_pages = available_pages();
    CHECK(start_pages == 64 * kMB / 0x1000, "available %u", start_pages);

    // Reserve, then commit pages inside, as RtlCreateHeap does.
    XbPtr heap = 0;
    uint32_t size = 0x100000 + 1;
    CHECK(allocate(&heap, 0, &size, kReserve, kReadWrite) == 0);
    CHECK(heap >= 0x10000000 && heap < 0x50000000 && heap % 0x10000 == 0, "base %08x", heap);
    CHECK(size == 0x101000, "size rounded to pages: %x", size);
    CHECK(available_pages() == start_pages, "reserving costs no pages");

    XbPtr page = heap + 0x1234;
    uint32_t page_size = 0x10;
    CHECK(allocate(&page, 0, &page_size, kCommit, kReadWrite) == 0);
    CHECK(page == heap + 0x1000 && page_size == 0x1000, "commit rounds to pages: %08x+%x", page, page_size);
    CHECK(all_zero(page, 0x1000), "fresh commit is zero");
    std::memset(bytes(page), 0xAB, 0x1000);
    CHECK(available_pages() == start_pages - 1);

    XbMemoryBasicInformation info{};
    CHECK(query(heap + 0x1800, &info) == 0);
    CHECK(info.base_address == heap + 0x1000 && info.allocation_base == heap && info.region_size == 0x1000);
    CHECK(info.state == kCommit && info.protect == kReadWrite && info.type == kPrivate);
    CHECK(query(heap, &info) == 0);
    CHECK(info.state == kReserve && info.region_size == 0x1000 && info.protect == 0, "reserved run before the page");
    CHECK(query(heap + 0x2000, &info) == 0);
    CHECK(info.state == kReserve && info.region_size == 0xFF000, "reserved run to the end: %x", info.region_size);
    CHECK(query(heap + size, &info) == 0);
    CHECK(info.state == kFree && info.allocation_base == 0);

    // Re-committing keeps contents; decommit + commit gives zeros again.
    CHECK(allocate(&page, 0, &page_size, kCommit, kReadWrite) == 0 && bytes(page)[0] == 0xAB);
    CHECK(free_vm(&page, &page_size, kDecommit) == 0);
    CHECK(available_pages() == start_pages);
    CHECK(allocate(&page, 0, &page_size, kCommit, kReadWrite) == 0 && all_zero(page, 0x1000));

    // Errors: commit outside a reservation, overlapping reserve, release not at base.
    XbPtr outside = heap + size + 0x10000;
    uint32_t one_page = 0x1000;
    CHECK(allocate(&outside, 0, &one_page, kCommit, kReadWrite) == kConflictingAddresses);
    XbPtr overlap = heap + 0x20000;
    CHECK(allocate(&overlap, 0, &one_page, kReserve, kReadWrite) == kConflictingAddresses);
    XbPtr inside = heap + 0x1000;
    uint32_t zero = 0;
    CHECK(free_vm(&inside, &zero, kRelease) == kFreeVmNotAtBase);
    XbPtr nowhere = 0x4F000000;
    CHECK(free_vm(&nowhere, &zero, kRelease) == kMemoryNotAllocated);
    XbPtr bad = 0;
    CHECK(allocate(&bad, 0, &one_page, kCommit, 0x03) == kInvalidPageProtection);

    // Release the whole reservation.
    XbPtr release_base = heap;
    uint32_t release_size = 0;
    CHECK(free_vm(&release_base, &release_size, kRelease) == 0 && release_size == 0x101000);
    CHECK(query(heap, &info) == 0 && info.state == kFree);
    CHECK(available_pages() == start_pages);

    // Reserve + commit at once, at a given address and top-down.
    XbPtr fixed = 0x20000000;
    uint32_t fixed_size = 0x3000;
    CHECK(allocate(&fixed, 0, &fixed_size, kReserve | kCommit, kReadWrite) == 0 && fixed == 0x20000000);
    XbPtr top = 0;
    uint32_t top_size = 0x1000;
    CHECK(allocate(&top, 0, &top_size, kReserve | kTopDown, kReadWrite) == 0 && top == 0x4FFF0000, "top %08x", top);

    // Contiguous memory: address range, alignment, physical addresses, size, protection.
    const XbPtr a = contiguous(0x1800);
    CHECK(a >= 0x80000000 && a % 0x1000 == 0, "contiguous %08x", a);
    CHECK(allocation_size(a) == 0x2000 && physical_address(a) == a - 0x80000000);
    CHECK(all_zero(a, 0x2000));
    const XbPtr aligned = contiguous_ex(0x1000, 0, 0xFFFFFFFF, 0x10000, kReadWrite | kWriteCombine);
    CHECK(aligned && (aligned - 0x80000000) % 0x10000 == 0, "aligned %08x", aligned);
    CHECK(query_protect(aligned) == (kReadWrite | kWriteCombine));
    const XbPtr high = contiguous_ex(0x1000, 0x2000000, 0x2FFFFFF, 0, kReadWrite);
    CHECK(high - 0x80000000 >= 0x2000000 && high - 0x80000000 < 0x3000000, "in range: %08x", high);
    CHECK(contiguous_ex(0x2000, 0x1000, 0x1FFF, 0, kReadWrite) == 0, "range too small");
    set_protect(a, 0x1000, kReadOnly);
    CHECK(query_protect(a) == kReadOnly && query_protect(a + 0x1000) == kReadWrite);
    set_protect(a, 0x1000, kReadWrite);
    bytes(a)[0] = 1;  // writable again
    free_contiguous(a);
    CHECK(allocation_size(a) == 0 && contiguous(0x1000) == a, "freed range is reused");
    free_contiguous(a);
    free_contiguous(aligned);
    free_contiguous(high);  // it splits physical memory at 32 MB

    // Kernel stacks.
    const XbPtr stack_top = create_stack(0x4000, 0);
    CHECK(stack_top && query_protect(stack_top - 0x1000) == kReadWrite);
    bytes(stack_top - 4)[0] = 1;
    delete_stack(stack_top, stack_top - 0x4000);
    CHECK(query_protect(stack_top - 0x1000) == 0);

    // The budget: what the game asks for at startup fits, 64 MB more does not, and
    // image pages count too.
    const uint32_t before = available_pages();
    const XbPtr arena = contiguous_ex(0x232B000, 0, 0xFFFFFFFF, 0x1000, kReadWrite);
    CHECK(arena != 0, "the game's 35 MB arena fits");
    CHECK(available_pages() == before - 0x232B000 / 0x1000);
    CHECK(contiguous(64 * kMB) == 0, "over budget");
    XbPtr big = 0;
    uint32_t big_size = 40 * kMB;
    CHECK(allocate(&big, 0, &big_size, kReserve, kReadWrite) == 0, "reserving more than the budget is fine");
    CHECK(allocate(&big, 0, &big_size, kCommit, kReadWrite) == kNoMemory, "committing it is not");
    CHECK(query(big, &info) == 0 && info.state == kReserve, "failed commit leaves pages reserved");
    charge_image_pages(100);
    CHECK(available_pages() == before - 0x232B000 / 0x1000 - 100);
    charge_image_pages(-100);
    free_contiguous(arena);
    CHECK(available_pages() == before);

    return test::finish("memory_test");
}
