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

// Case-blind name hash: per character (A-Z lowered) rotate left 5, xor the signed char.
// 0 for null or empty text.
uint32_t XB_REGS2 XB_CDECL Com_HashKeyNoCase(uint32_t unused, const char* text);
// The title's polynomial sine of angle + 3 pi / 2 into *sine and of angle into
// *cosine (the names as the callers use them; EDX unused).
void XB_REGS3 XB_CDECL FastSinCos(float* sine, uint32_t unused, float* cosine, float angle);

}  // namespace game
