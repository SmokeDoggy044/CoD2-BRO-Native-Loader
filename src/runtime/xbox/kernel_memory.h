#pragma once

// Xbox memory: virtual memory (NtAllocateVirtualMemory and friends), contiguous
// "physical" memory (MmAllocateContiguousMemory*) and the physical page budget.

#include <cstdint>

#include "kernel.h"
#include "types.h"

namespace xbox {

// Reserves the address ranges from `config`. Called by kernel_init.
bool memory_init(const MemoryConfig& config);

// Charges (positive) or returns (negative) pages for memory the kernel does not
// allocate itself: the XBE image and its sections. Returns false if the budget is
// now exceeded; the charge is kept either way.
bool charge_image_pages(int32_t pages);

// Contiguous memory for the replaced SDK libraries (e.g. D3D's frame buffers), from the
// same physical budget as the title's: an Xbox address (contiguous base + physical),
// or 0. Read/write, write-combined.
XbPtr allocate_contiguous(uint32_t bytes, uint32_t alignment);
void free_contiguous(XbPtr address);

// Our own C's platform layer (game::TitleHost under --own-main): title memory from the same allocator and budget as
// the kernel exports, so addresses stay what the plain loader hands out (replays compare state across both).
XbPtr title_allocate_contiguous(uint32_t bytes, uint32_t lowest, uint32_t highest, uint32_t alignment,
                                uint32_t protect);
void title_free_contiguous(XbPtr address);
void title_protect(XbPtr address, uint32_t bytes, uint32_t protect);
uint32_t title_protection(XbPtr address);
struct TitleMemoryStatistics {
    uint32_t total_pages, available_pages, reserved_bytes;
};
TitleMemoryStatistics title_memory_statistics();
XbPtr title_reserve(uint32_t bytes);  // reserved and committed, read/write

}  // namespace xbox
