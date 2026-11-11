#pragma once

#include <cstdint>

#include "compiler.h"

// Our own heap for the title's CRT and XAPI allocations (own engine host, 0.3): every path that reached the
// title's Rtl heap (XAPILIB RtlAllocateHeap & co., the HeapAlloc/LocalAlloc/XMemAlloc wrappers, LIBCMT malloc and
// friends) lands here. Not the original's allocator, so block addresses differ from the title's, but they are the
// same in every run and build: the memory comes from the kernel's NtAllocateVirtualMemory in 1 MB segments (where
// the Rtl heap's came from, so the engine's contiguous memory keeps its addresses and replays match the
// recordings frame for frame), blocks first fit from size-ordered bins. Heap handles are accepted and ignored (one
// heap); blocks are 16-byte aligned; RtlSizeHeap returns the size asked for.

namespace game {

// The core, also for our C: like RtlAllocateHeap with HEAP_ZERO_MEMORY when `zero`.
uint32_t title_heap_alloc(uint32_t size, bool zero);
void title_heap_free(uint32_t block);
uint32_t title_heap_size(uint32_t block);  // the size asked for; 0xffffffff if not a block of ours
// Totals for the check / diagnostics: bytes in use (asked sizes), blocks in use, bytes in segments.
void title_heap_stats(uint32_t& in_use, uint32_t& blocks, uint32_t& reserved);
// An in-process title restart (own engine host 5b): no blocks, no segments (the title's memory is reset with it).
void title_heap_reset();

// XAPILIB (Rtl / kernel32-style).
uint32_t XB_STDCALL RtlAllocateHeap(uint32_t heap, uint32_t flags, uint32_t size);                // 0x001ec413
uint8_t XB_STDCALL RtlFreeHeap(uint32_t heap, uint32_t flags, uint32_t block);                    // 0x001ecb8e
uint32_t XB_STDCALL RtlReAllocateHeap(uint32_t heap, uint32_t flags, uint32_t block, uint32_t size);  // 0x001ecd82
uint32_t XB_STDCALL RtlSizeHeap(uint32_t heap, uint32_t flags, uint32_t block);                   // 0x001eba71
uint32_t XB_STDCALL RtlCreateHeap(uint32_t flags, uint32_t base, uint32_t reserve, uint32_t commit, uint32_t lock,
                                  uint32_t parameters);                                           // 0x001ebfef
uint32_t XB_STDCALL RtlDestroyHeap(uint32_t heap);                                                // 0x001eb5e4
uint32_t XB_STDCALL GetProcessHeap();                                                             // 0x001ea25b
uint32_t XB_STDCALL HeapCreate(uint32_t options, uint32_t initial, uint32_t maximum);             // 0x001ea1d9
uint32_t XB_STDCALL HeapDestroy(uint32_t heap);                                                   // 0x001ea228
uint32_t XB_STDCALL HeapFree(uint32_t heap, uint32_t flags, uint32_t block);                      // 0x001ea244
uint32_t XB_STDCALL LocalAlloc(uint32_t flags, uint32_t size);                                    // 0x001ea261
uint32_t XB_STDCALL LocalFree(uint32_t block);                                                    // 0x001ea27e
uint32_t XB_STDCALL XMemAlloc(uint32_t size, uint32_t attributes);                                // 0x001ea11f
void XB_STDCALL XMemFree(uint32_t block, uint32_t attributes);                                    // 0x001ea1bf

// LIBCMT.
uint32_t XB_CDECL title_crt_heap_alloc(uint32_t size);                       // 0x001f0cc1 __heap_alloc
uint32_t XB_CDECL title_crt_nh_malloc(uint32_t size, uint32_t new_handler);     // 0x001f0ce8 __nh_malloc
uint32_t XB_CDECL title_crt_malloc(uint32_t size);                              // 0x001f0d14 _malloc
void XB_CDECL title_crt_free(uint32_t block);                                    // 0x001f0c69 _free
uint32_t XB_CDECL title_crt_calloc(uint32_t count, uint32_t size);              // 0x001f72cc _calloc
uint32_t XB_CDECL title_crt_realloc(uint32_t block, uint32_t size);             // 0x001f1c7d _realloc
uint32_t XB_CDECL title_crt_msize(uint32_t block);                              // 0x001f32ad __msize
uint32_t XB_CDECL title_crt_aligned_offset_malloc(uint32_t size, uint32_t alignment, uint32_t offset);  // 0x001f10bf
uint32_t XB_CDECL title_crt_aligned_malloc(uint32_t size, uint32_t alignment);  // 0x001f114f
void XB_CDECL title_crt_aligned_free(uint32_t block);                            // 0x001f113a

}  // namespace game
