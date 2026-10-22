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
// 0x000442e0: copies at most max characters of src (null: none) and a NUL; *length =
// the characters copied.
void XB_CDECL Q_strncpyLen(char* dst, int32_t* length, int32_t max, const char* src);
// 0x000443e0: src[start .. min(start + count, size - 1)) up to a NUL into dst with a
// NUL, *length = the characters copied; nothing at all when start >= size.
void XB_CDECL Q_strncpySubLen(char* dst, int32_t* length, const char* src, int32_t start, int32_t count,
                              int32_t size);

// 0x002a2af0 (right in EDI, up in ESI; angles, forward on the stack): Q3's AngleVectors with
// FastSinCos (x87 degree-to-radian products); the original keeps the pitch and roll sines in
// its two argument slots, so the C version gets their address.
extern "C" PLATFORM_HIDDEN void XB_CDECL AnglesToAxis(uint32_t* args, float* right, float* up);
extern "C" void AnglesToAxis_entry(void);

// 0x00295060 (a in EAX, out in EDX, b in ECX): a 4x3 matrix (rows of 3 floats, translation
// last) times a 4x4 one into a 4x4 with last column {0, 0, 0, 1}.
void XB_REGS3 XB_CDECL MatrixMultiply43To44(const float* a, float* out, const float* b);

}  // namespace game
