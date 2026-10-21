#pragma once

// Common engine services (Q3's qcommon): memory.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// Counts the allocation (0x009098c4) and hands it to the installed allocator
// (0x009098e8) when there is one; else flags 0x30000 = physical memory (0x20000:
// write-combined), others the CRT's aligned heap.
void* XB_CDECL Mem_AlignedAlloc(uint32_t size, uint32_t alignment, uint32_t flags);

}  // namespace game
