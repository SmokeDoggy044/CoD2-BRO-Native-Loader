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

}  // namespace xbox
