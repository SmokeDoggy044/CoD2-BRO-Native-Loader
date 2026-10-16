// Virtual memory, contiguous memory and the physical page budget. See memory.h.
//
// Virtual memory follows NT: reservations are 64 KB aligned, pages inside them are
// committed and decommitted individually, newly committed pages read as zero, and
// NtQueryVirtualMemory reports runs of pages with the same state. XAPI's heap
// (RtlCreateHeap) depends on all of this.
//
// Contiguous memory lives at contiguous_base + physical address, as on the Xbox.
//
// Every committed page - image, contiguous, virtual, kernel stacks - comes out of one
// budget of physical_bytes, so allocations fail where they would on the console.

#include "kernel_memory.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <map>
#include <mutex>
#include <vector>

#include "compiler.h"
#include "kernel_internal.h"
#include "log.h"
#include "objects.h"
#include "types.h"
#include "vmem.h"

namespace xbox {

namespace {

using platform::vmem::Protection;

constexpr uint32_t kPage = 0x1000;
constexpr uint32_t kGranularity = 0x10000;  // reservation alignment

// AllocationType / FreeType / State / Type.
constexpr uint32_t kMemCommit = 0x1000;
constexpr uint32_t kMemReserve = 0x2000;
constexpr uint32_t kMemDecommit = 0x4000;
constexpr uint32_t kMemRelease = 0x8000;
constexpr uint32_t kMemFree = 0x10000;
constexpr uint32_t kMemPrivate = 0x20000;
constexpr uint32_t kMemReset = 0x80000;
constexpr uint32_t kMemTopDown = 0x100000;
constexpr uint32_t kMemNoZero = 0x800000;

// Page protection.
constexpr uint32_t kPageNoAccess = 0x01;
constexpr uint32_t kPageReadWrite = 0x04;
constexpr uint32_t kPageModifiers = 0x100 | 0x200 | 0x400;  // GUARD, NOCACHE, WRITECOMBINE

constexpr uint32_t kStatusConflictingAddresses = 0xC0000018;
constexpr uint32_t kStatusUnableToFreeVm = 0xC000001A;
constexpr uint32_t kStatusInvalidPageProtection = 0xC0000045;
constexpr uint32_t kStatusFreeVmNotAtBase = 0xC000009F;
constexpr uint32_t kStatusMemoryNotAllocated = 0xC00000A0;

constexpr uint32_t round_up(uint32_t value, uint32_t alignment) { return (value + alignment - 1) & ~(alignment - 1); }
constexpr uint32_t round_down(uint32_t value, uint32_t alignment) { return value & ~(alignment - 1); }

// The Xbox protection value's access part -> host protection. Caching modifiers
// (NOCACHE, WRITECOMBINE) and GUARD are accepted and ignored.
bool to_protection(uint32_t xbox_protect, Protection& protection) {
    if (xbox_protect & ~(0xFFu | kPageModifiers)) return false;
    switch (xbox_protect & 0xFF) {
        case 0x01: protection = Protection::None; return true;               // NOACCESS
        case 0x02: protection = Protection::ReadOnly; return true;           // READONLY
        case 0x04:                                                            // READWRITE
        case 0x08: protection = Protection::ReadWrite; return true;          // WRITECOPY
        case 0x10:                                                            // EXECUTE
        case 0x20: protection = Protection::ReadExecute; return true;        // EXECUTE_READ
        case 0x40:                                                            // EXECUTE_READWRITE
        case 0x80: protection = Protection::ReadWriteExecute; return true;  // EXECUTE_WRITECOPY
        default: return false;
    }
}

enum class PageState : uint8_t { Free, Reserved, Committed };

struct Reservation {
    uint32_t size;
    uint32_t allocation_protect;
    bool kernel_stack;
};

struct MemoryState {
    std::mutex mutex;
    MemoryConfig config;
    bool initialized = false;

    // Physical page budget.
    uint32_t total_pages = 0;
    uint32_t image_pages = 0;
    uint32_t contiguous_pages = 0;
    uint32_t virtual_committed_pages = 0;  // includes kernel stacks
    uint32_t virtual_reserved_pages = 0;
    uint32_t stack_pages = 0;

    // Virtual memory: per-page state over [virtual_base, virtual_end).
    std::vector<PageState> page_state;
    std::vector<uint16_t> page_protect;
    std::map<uint32_t, Reservation> reservations;  // by base address

    // Contiguous memory: allocations by first physical page, and page protection.
    std::map<uint32_t, uint32_t> contiguous;  // first page -> page count
    std::vector<uint16_t> contiguous_protect;

    uint32_t used_pages() const { return image_pages + contiguous_pages + virtual_committed_pages; }
    bool can_take(uint32_t pages) const { return used_pages() + pages <= total_pages; }
};

MemoryState& g_memory = immortal<MemoryState>();

void* host(uint32_t address) { return from_xbox_ptr<void>(address); }

void log_out_of_memory(const char* what, uint32_t bytes) {
    const MemoryState& m = g_memory;
    LOG_WARN("memory", "%s of %u bytes fails: Xbox memory budget exhausted (%u of %u pages used)", what, bytes,
             m.used_pages(), m.total_pages);
}

// --- Virtual memory (g_memory.mutex held) ----------------------------------------------

bool in_virtual_range(uint32_t start, uint32_t end) {
    return start >= g_memory.config.virtual_base && end <= g_memory.config.virtual_end && start < end;
}

uint32_t page_index(uint32_t address) { return (address - g_memory.config.virtual_base) / kPage; }

// The reservation containing `address`, or end().
std::map<uint32_t, Reservation>::iterator find_reservation(uint32_t address) {
    auto& reservations = g_memory.reservations;
    auto it = reservations.upper_bound(address);
    if (it == reservations.begin()) return reservations.end();
    --it;
    return address < it->first + it->second.size ? it : reservations.end();
}

bool range_is_free(uint32_t start, uint32_t end) {
    for (uint32_t i = page_index(start); i < page_index(end); ++i)
        if (g_memory.page_state[i] != PageState::Free) return false;
    return true;
}

// Lowest (or highest, for top_down) granularity-aligned free range of `size` bytes.
uint32_t find_free_range(uint32_t size, bool top_down) {
    const uint32_t first = round_up(g_memory.config.virtual_base, kGranularity);
    const uint32_t last = round_down(g_memory.config.virtual_end - size, kGranularity);
    if (size > g_memory.config.virtual_end - g_memory.config.virtual_base || last < first) return 0;
    if (top_down) {
        for (uint32_t base = last;; base -= kGranularity) {
            if (range_is_free(base, base + size)) return base;
            if (base == first) return 0;
        }
    }
    for (uint32_t base = first; base <= last; base += kGranularity) {
        // Skip ahead past the first page in use.
        const uint32_t end = base + size;
        uint32_t used = 0;
        for (uint32_t i = page_index(base); i < page_index(end); ++i)
            if (g_memory.page_state[i] != PageState::Free) used = g_memory.config.virtual_base + i * kPage;
        if (!used) return base;
        base = round_down(used, kGranularity);
    }
    return 0;
}

// Commits [start, end) inside one reservation. Already committed pages keep their
// contents but take the new protection.
uint32_t commit_pages(uint32_t start, uint32_t end, uint32_t protect, Protection protection) {
    uint32_t newly = 0;
    for (uint32_t i = page_index(start); i < page_index(end); ++i)
        if (g_memory.page_state[i] != PageState::Committed) ++newly;
    if (!g_memory.can_take(newly)) {
        log_out_of_memory("commit", end - start);
        return kStatusNoMemory;
    }
    if (!platform::vmem::commit(host(start), end - start, protection)) {
        LOG_ERROR("memory", "host commit of %08x-%08x failed", start, end);
        return kStatusNoMemory;
    }
    for (uint32_t i = page_index(start); i < page_index(end); ++i) {
        g_memory.page_state[i] = PageState::Committed;
        g_memory.page_protect[i] = static_cast<uint16_t>(protect);
    }
    g_memory.virtual_committed_pages += newly;
    return kStatusSuccess;
}

// Returns committed pages in [start, end) to the reserved state.
uint32_t decommit_pages(uint32_t start, uint32_t end) {
    uint32_t count = 0;
    for (uint32_t i = page_index(start); i < page_index(end); ++i) {
        if (g_memory.page_state[i] != PageState::Committed) continue;
        g_memory.page_state[i] = PageState::Reserved;
        g_memory.page_protect[i] = 0;
        ++count;
    }
    if (count) platform::vmem::decommit(host(start), end - start);
    g_memory.virtual_committed_pages -= count;
    return count;
}

uint32_t reserve_range(uint32_t base, uint32_t size, uint32_t protect, bool kernel_stack) {
    for (uint32_t i = page_index(base); i < page_index(base + size); ++i) g_memory.page_state[i] = PageState::Reserved;
    g_memory.reservations[base] = {size, protect, kernel_stack};
    g_memory.virtual_reserved_pages += size / kPage;
    return base;
}

void release_reservation(std::map<uint32_t, Reservation>::iterator it) {
    const uint32_t base = it->first;
    const uint32_t size = it->second.size;
    const uint32_t decommitted = decommit_pages(base, base + size);
    if (it->second.kernel_stack) g_memory.stack_pages -= decommitted;
    for (uint32_t i = page_index(base); i < page_index(base + size); ++i) g_memory.page_state[i] = PageState::Free;
    g_memory.virtual_reserved_pages -= size / kPage;
    g_memory.reservations.erase(it);
}

// --- Exports: virtual memory -------------------------------------------------------------

uint32_t XB_STDCALL NtAllocateVirtualMemory(XbPtr* base_address, uint32_t zero_bits, uint32_t* region_size,
                                            uint32_t allocation_type, uint32_t protect) {
    (void)zero_bits;
    Protection protection;
    if (!to_protection(protect, protection)) return kStatusInvalidPageProtection;
    if (allocation_type & ~(kMemCommit | kMemReserve | kMemReset | kMemTopDown | kMemNoZero))
        return kStatusInvalidParameter;
    if (allocation_type & kMemReset) return kStatusSuccess;  // "contents no longer needed": nothing to do
    if (!(allocation_type & (kMemCommit | kMemReserve)) || *region_size == 0) return kStatusInvalidParameter;

    const uint32_t requested = *base_address;
    const uint32_t requested_size = *region_size;
    const uint64_t requested_end = uint64_t{requested} + requested_size;
    if (requested_end > 0xFFFFFFFFull) return kStatusInvalidParameter;

    std::lock_guard lock(g_memory.mutex);
    uint32_t status = kStatusSuccess;
    if ((allocation_type & kMemReserve) || requested == 0) {
        // New reservation (a commit without a base address also reserves, as in NT).
        uint32_t base, size;
        if (requested == 0) {
            size = round_up(*region_size, kPage);
            base = find_free_range(size, allocation_type & kMemTopDown);
            if (!base) {
                LOG_WARN("memory", "no free virtual range of %u bytes", size);
                return kStatusNoMemory;
            }
        } else {
            base = round_down(requested, kGranularity);
            size = round_up(static_cast<uint32_t>(requested_end), kPage) - base;
            if (!in_virtual_range(base, base + size) || !range_is_free(base, base + size))
                return kStatusConflictingAddresses;
        }
        reserve_range(base, size, protect, false);
        if (allocation_type & kMemCommit) {
            status = commit_pages(base, base + size, protect, protection);
            if (status != kStatusSuccess) {
                release_reservation(g_memory.reservations.find(base));
                return status;
            }
        }
        *base_address = base;
        *region_size = size;
    } else {
        // Commit inside an existing reservation.
        const uint32_t start = round_down(requested, kPage);
        const uint32_t end = round_up(static_cast<uint32_t>(requested_end), kPage);
        const auto it = find_reservation(start);
        if (it == g_memory.reservations.end() || end > it->first + it->second.size) return kStatusConflictingAddresses;
        status = commit_pages(start, end, protect, protection);
        if (status != kStatusSuccess) return status;
        *base_address = start;
        *region_size = end - start;
    }
    KTRACE("NtAllocateVirtualMemory(%08x, %x, type=%x, protect=%x) -> %08x+%x", requested, requested_size,
           allocation_type, protect, *base_address, *region_size);
    return kStatusSuccess;
}

uint32_t XB_STDCALL NtFreeVirtualMemory(XbPtr* base_address, uint32_t* region_size, uint32_t free_type) {
    if (free_type != kMemDecommit && free_type != kMemRelease) return kStatusInvalidParameter;
    const uint32_t address = *base_address;
    std::lock_guard lock(g_memory.mutex);
    const auto it = find_reservation(address);
    if (it == g_memory.reservations.end()) return kStatusMemoryNotAllocated;
    const uint32_t reservation_base = it->first;
    const uint32_t reservation_end = it->first + it->second.size;

    uint32_t start, end;
    if (*region_size == 0) {
        if (free_type == kMemRelease && address != reservation_base) return kStatusFreeVmNotAtBase;
        start = round_down(address, kPage);
        end = reservation_end;
    } else {
        const uint64_t requested_end = uint64_t{address} + *region_size;
        if (requested_end > reservation_end) return kStatusUnableToFreeVm;
        start = round_down(address, kPage);
        end = round_up(static_cast<uint32_t>(requested_end), kPage);
    }

    if (free_type == kMemRelease) {
        // Only whole reservations can be released.
        if (start != reservation_base || end != reservation_end) return kStatusUnableToFreeVm;
        release_reservation(it);
    } else {
        const uint32_t count = decommit_pages(start, end);
        if (it->second.kernel_stack) g_memory.stack_pages -= count;
    }
    KTRACE("NtFreeVirtualMemory(%08x, %x, %s)", start, end - start, free_type == kMemRelease ? "release" : "decommit");
    *base_address = start;
    *region_size = end - start;
    return kStatusSuccess;
}

uint32_t XB_STDCALL NtQueryVirtualMemory(XbPtr base_address, XbMemoryBasicInformation* info) {
    std::lock_guard lock(g_memory.mutex);
    const uint32_t address = round_down(base_address, kPage);
    if (!in_virtual_range(address, address + kPage)) {
        LOG_DEBUG("memory", "NtQueryVirtualMemory(%08x) outside the virtual memory range", base_address);
        return kStatusInvalidParameter;
    }
    // Extend over following pages in the same state (and the same reservation).
    const auto it = find_reservation(address);
    const uint32_t limit = it != g_memory.reservations.end() ? it->first + it->second.size : g_memory.config.virtual_end;
    const uint32_t first = page_index(address);
    const PageState state = g_memory.page_state[first];
    const uint16_t protect = g_memory.page_protect[first];
    uint32_t end = address + kPage;
    while (end < limit && g_memory.page_state[page_index(end)] == state && g_memory.page_protect[page_index(end)] == protect)
        end += kPage;

    *info = {};
    info->base_address = address;
    info->region_size = end - address;
    if (state == PageState::Free) {
        info->state = kMemFree;
        info->protect = kPageNoAccess;
    } else {
        info->allocation_base = it->first;
        info->allocation_protect = it->second.allocation_protect;
        info->state = state == PageState::Committed ? kMemCommit : kMemReserve;
        info->protect = state == PageState::Committed ? protect : 0;
        info->type = kMemPrivate;
    }
    return kStatusSuccess;
}

XbPtr XB_STDCALL MmCreateKernelStack(uint32_t number_of_bytes, uint32_t debugger_thread) {
    (void)debugger_thread;
    const uint32_t size = round_up(number_of_bytes, kPage);
    std::lock_guard lock(g_memory.mutex);
    // One reserved guard page below the stack.
    const uint32_t base = find_free_range(size + kPage, false);
    if (!base) return 0;
    reserve_range(base, size + kPage, kPageReadWrite, true);
    if (commit_pages(base + kPage, base + kPage + size, kPageReadWrite, Protection::ReadWrite) != kStatusSuccess) {
        release_reservation(g_memory.reservations.find(base));
        return 0;
    }
    g_memory.stack_pages += size / kPage;
    return base + kPage + size;  // the stack base is its top
}

void XB_STDCALL MmDeleteKernelStack(XbPtr stack_base, XbPtr stack_limit) {
    (void)stack_base;
    std::lock_guard lock(g_memory.mutex);
    const auto it = find_reservation(stack_limit);
    if (it != g_memory.reservations.end() && it->second.kernel_stack)
        release_reservation(it);
    else
        LOG_WARN("memory", "MmDeleteKernelStack(%08x, %08x): not a kernel stack", stack_base, stack_limit);
}

// --- Contiguous memory (g_memory.mutex held) ---------------------------------------------

uint32_t contiguous_page_count() { return g_memory.config.physical_bytes / kPage; }

bool is_contiguous(uint32_t address) {
    return address >= g_memory.config.contiguous_base &&
           address - g_memory.config.contiguous_base < g_memory.config.physical_bytes;
}

// The contiguous allocation containing `address`, or end().
std::map<uint32_t, uint32_t>::iterator find_contiguous(uint32_t address) {
    if (!is_contiguous(address)) return g_memory.contiguous.end();
    const uint32_t page = (address - g_memory.config.contiguous_base) / kPage;
    auto it = g_memory.contiguous.upper_bound(page);
    if (it == g_memory.contiguous.begin()) return g_memory.contiguous.end();
    --it;
    return page < it->first + it->second ? it : g_memory.contiguous.end();
}

// --- Exports: contiguous memory --------------------------------------------------------------

XbPtr XB_STDCALL MmAllocateContiguousMemoryEx(uint32_t number_of_bytes, uint32_t lowest_acceptable_address,
                                              uint32_t highest_acceptable_address, uint32_t alignment,
                                              uint32_t protect) {
    Protection protection;
    if (number_of_bytes == 0 || !to_protection(protect, protection)) return 0;
    const uint32_t pages = round_up(number_of_bytes, kPage) / kPage;
    const uint32_t align_pages = std::max<uint32_t>(1, round_up(alignment, kPage) / kPage);

    std::lock_guard lock(g_memory.mutex);
    if (!g_memory.can_take(pages)) {
        log_out_of_memory("MmAllocateContiguousMemoryEx", number_of_bytes);
        return 0;
    }
    // First fit between existing allocations, within the acceptable physical range.
    const uint32_t lowest = round_up(lowest_acceptable_address, kPage) / kPage;
    const uint32_t limit = std::min(contiguous_page_count(), highest_acceptable_address / kPage + 1);
    uint32_t candidate = (lowest + align_pages - 1) / align_pages * align_pages;
    bool found = false;
    for (auto it = g_memory.contiguous.begin();; ++it) {
        const uint32_t gap_end = it == g_memory.contiguous.end() ? limit : std::min(it->first, limit);
        if (candidate + pages <= gap_end) {
            found = true;
            break;
        }
        if (it == g_memory.contiguous.end() || it->first >= limit) break;
        candidate = std::max(candidate, (it->first + it->second + align_pages - 1) / align_pages * align_pages);
    }
    if (!found) {
        LOG_WARN("memory", "no contiguous range of %u bytes in %08x-%08x (alignment %x)", number_of_bytes,
                 lowest_acceptable_address, highest_acceptable_address, alignment);
        return 0;
    }
    const uint32_t address = g_memory.config.contiguous_base + candidate * kPage;
    if (!platform::vmem::commit(host(address), pages * kPage, protection)) {
        LOG_ERROR("memory", "host commit of contiguous %08x+%x failed", address, pages * kPage);
        return 0;
    }
    g_memory.contiguous[candidate] = pages;
    std::fill_n(g_memory.contiguous_protect.begin() + candidate, pages, static_cast<uint16_t>(protect));
    g_memory.contiguous_pages += pages;
    KTRACE("MmAllocateContiguousMemoryEx(%x, %08x-%08x, align %x, protect %x) -> %08x", number_of_bytes,
           lowest_acceptable_address, highest_acceptable_address, alignment, protect, address);
    return address;
}

XbPtr XB_STDCALL MmAllocateContiguousMemory(uint32_t number_of_bytes) {
    return MmAllocateContiguousMemoryEx(number_of_bytes, 0, 0xFFFFFFFF, 0, kPageReadWrite);
}

void XB_STDCALL MmFreeContiguousMemory(XbPtr base_address) {
    std::lock_guard lock(g_memory.mutex);
    const auto it = find_contiguous(base_address);
    if (it == g_memory.contiguous.end() || g_memory.config.contiguous_base + it->first * kPage != base_address) {
        LOG_WARN("memory", "MmFreeContiguousMemory(%08x): not a contiguous allocation", base_address);
        return;
    }
    platform::vmem::decommit(host(base_address), it->second * kPage);
    std::fill_n(g_memory.contiguous_protect.begin() + it->first, it->second, uint16_t{0});
    g_memory.contiguous_pages -= it->second;
    g_memory.contiguous.erase(it);
    KTRACE("MmFreeContiguousMemory(%08x)", base_address);
}

void XB_STDCALL MmPersistContiguousMemory(XbPtr base_address, uint32_t number_of_bytes, uint32_t persist) {
    // Persistence across a reboot into another title; nothing to do here.
    KTRACE("MmPersistContiguousMemory(%08x, %x, %u)", base_address, number_of_bytes, persist & 0xFF);
}

uint32_t XB_STDCALL MmQueryAllocationSize(XbPtr base_address) {
    std::lock_guard lock(g_memory.mutex);
    if (const auto it = find_contiguous(base_address); it != g_memory.contiguous.end()) return it->second * kPage;
    if (in_virtual_range(base_address, base_address + 1))
        if (const auto it = find_reservation(base_address); it != g_memory.reservations.end()) return it->second.size;
    return 0;
}

XbPtr XB_STDCALL MmGetPhysicalAddress(XbPtr base_address) {
    if (is_contiguous(base_address)) return base_address - g_memory.config.contiguous_base;
    LOG_WARN("memory", "MmGetPhysicalAddress(%08x): not contiguous memory", base_address);
    return 0;
}

// Protection of one page of contiguous or virtual memory, or 0 if not allocated.
uint32_t XB_STDCALL MmQueryAddressProtect(XbPtr address) {
    std::lock_guard lock(g_memory.mutex);
    if (find_contiguous(address) != g_memory.contiguous.end())
        return g_memory.contiguous_protect[(address - g_memory.config.contiguous_base) / kPage];
    if (in_virtual_range(address, address + 1) && g_memory.page_state[page_index(address)] == PageState::Committed)
        return g_memory.page_protect[page_index(address)];
    return 0;
}

void XB_STDCALL MmSetAddressProtect(XbPtr base_address, uint32_t number_of_bytes, uint32_t protect) {
    // Only cache attributes (e.g. PAGE_WRITECOMBINE alone, as D3D titles pass for GPU
    // buffers): the access stays as it is.
    const bool modifiers_only = (protect & 0xFF) == 0 && (protect & kPageModifiers) != 0;
    Protection protection = Protection::ReadWrite;
    if (!modifiers_only && !to_protection(protect, protection)) {
        LOG_WARN("memory", "MmSetAddressProtect: invalid protection %x", protect);
        return;
    }
    const uint32_t start = round_down(base_address, kPage);
    const uint32_t end = round_up(base_address + number_of_bytes, kPage);
    std::lock_guard lock(g_memory.mutex);
    for (uint32_t page = start; page < end; page += kPage) {
        uint16_t* slot = nullptr;
        if (find_contiguous(page) != g_memory.contiguous.end())
            slot = &g_memory.contiguous_protect[(page - g_memory.config.contiguous_base) / kPage];
        else if (in_virtual_range(page, page + kPage) && g_memory.page_state[page_index(page)] == PageState::Committed)
            slot = &g_memory.page_protect[page_index(page)];
        if (!slot) {
            LOG_WARN("memory", "MmSetAddressProtect(%08x): page not allocated", page);
            continue;
        }
        if (modifiers_only) {
            *slot = static_cast<uint16_t>((*slot & 0xFF) | protect);
            continue;
        }
        *slot = static_cast<uint16_t>(protect);
        platform::vmem::protect(host(page), kPage, protection);
    }
}

uint32_t XB_STDCALL MmQueryStatistics(XbMmStatistics* statistics) {
    if (!statistics || statistics->length != sizeof(XbMmStatistics)) return kStatusInvalidParameter;
    std::lock_guard lock(g_memory.mutex);
    const MemoryState& m = g_memory;
    statistics->total_physical_pages = m.total_pages;
    statistics->available_pages = m.total_pages > m.used_pages() ? m.total_pages - m.used_pages() : 0;
    statistics->virtual_memory_bytes_committed = (m.virtual_committed_pages - m.stack_pages) * kPage;
    statistics->virtual_memory_bytes_reserved = m.virtual_reserved_pages * kPage;
    statistics->cache_pages_committed = 0;
    statistics->pool_pages_committed = 0;
    statistics->stack_pages_committed = m.stack_pages;
    statistics->image_pages_committed = m.image_pages;
    return kStatusSuccess;
}

const KernelImpl kMemoryImpls[] = {
    KIMPL(MmAllocateContiguousMemory),
    KIMPL(MmAllocateContiguousMemoryEx),
    KIMPL(MmCreateKernelStack),
    KIMPL(MmDeleteKernelStack),
    KIMPL(MmFreeContiguousMemory),
    KIMPL(MmGetPhysicalAddress),
    KIMPL(MmPersistContiguousMemory),
    KIMPL(MmQueryAddressProtect),
    KIMPL(MmQueryAllocationSize),
    KIMPL(MmQueryStatistics),
    KIMPL(MmSetAddressProtect),
    KIMPL(NtAllocateVirtualMemory),
    KIMPL(NtFreeVirtualMemory),
    KIMPL(NtQueryVirtualMemory),
};

}  // namespace

bool memory_init(const MemoryConfig& config) {
    std::lock_guard lock(g_memory.mutex);
    if (g_memory.initialized) return true;
    if (config.physical_bytes % kPage || config.contiguous_base % kGranularity || config.virtual_base % kGranularity ||
        config.virtual_end % kGranularity || config.virtual_end <= config.virtual_base) {
        LOG_ERROR("memory", "invalid memory layout");
        return false;
    }
    if (!platform::vmem::reserve(config.contiguous_base, config.physical_bytes)) {
        LOG_ERROR("memory", "cannot reserve contiguous memory at %08x+%x", config.contiguous_base, config.physical_bytes);
        return false;
    }
    if (!platform::vmem::reserve(config.virtual_base, config.virtual_end - config.virtual_base)) {
        LOG_ERROR("memory", "cannot reserve virtual memory range %08x-%08x", config.virtual_base, config.virtual_end);
        return false;
    }
    g_memory.config = config;
    g_memory.total_pages = config.physical_bytes / kPage;
    g_memory.page_state.assign((config.virtual_end - config.virtual_base) / kPage, PageState::Free);
    g_memory.page_protect.assign(g_memory.page_state.size(), 0);
    g_memory.contiguous_protect.assign(g_memory.total_pages, 0);
    g_memory.initialized = true;
    LOG_INFO("memory", "%u MB physical at %08x, virtual %08x-%08x", config.physical_bytes >> 20,
             config.contiguous_base, config.virtual_base, config.virtual_end);
    return true;
}

bool charge_image_pages(int32_t pages) {
    std::lock_guard lock(g_memory.mutex);
    g_memory.image_pages = static_cast<uint32_t>(static_cast<int32_t>(g_memory.image_pages) + pages);
    if (g_memory.used_pages() <= g_memory.total_pages) return true;
    LOG_WARN("memory", "image pages exceed the Xbox memory budget (%u of %u pages used)", g_memory.used_pages(),
             g_memory.total_pages);
    return false;
}

std::span<const KernelImpl> memory_kernel_impls() { return kMemoryImpls; }

XbPtr allocate_contiguous(uint32_t bytes, uint32_t alignment) {
    return MmAllocateContiguousMemoryEx(bytes, 0, 0xFFFFFFFF, alignment, kPageReadWrite | 0x400);
}

void free_contiguous(XbPtr address) { MmFreeContiguousMemory(address); }

}  // namespace xbox
