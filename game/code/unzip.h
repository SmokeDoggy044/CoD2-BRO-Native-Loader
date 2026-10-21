#pragma once

// The title's zlib (Q3's qcommon/unzip.c copy of inflate).

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// inflate_huft: one Huffman table entry.
struct InflateHuft {
    uint8_t exop;  // extra bits or operation
    uint8_t bits;  // bits in this code or subcode
    uint8_t pad[2];
    uint32_t base;  // literal, length/distance base, or table offset
};

// The parts of inflate_blocks_state that inflate_fast uses.
struct InflateBlocksState {
    uint8_t unknown_00[0x1c];
    uint32_t bitk;  // bits in the bit buffer
    uint32_t bitb;  // bit buffer
    uint8_t unknown_24[4];
    uint8_t* window;  // sliding window
    uint8_t* end;     // one past its end
    uint8_t* read;    // window read pointer
    uint8_t* write;   // window write pointer
};

// The parts of z_stream that inflate_fast uses.
struct ZStream {
    const uint8_t* next_in;
    uint32_t avail_in;
    uint32_t total_in;
    uint8_t unknown_0c[0x0c];
    const char* msg;
};

#if UINTPTR_MAX == 0xFFFFFFFF
static_assert(sizeof(InflateHuft) == 8);
static_assert(offsetof(InflateBlocksState, bitk) == 0x1c);
static_assert(offsetof(InflateBlocksState, write) == 0x34);
static_assert(offsetof(ZStream, msg) == 0x18);
#endif

// 0x0005d8c0: the fixed Huffman tables (bit counts 9 and 5); always Z_OK. `stream`
// is unused.
int32_t XB_CDECL inflate_trees_fixed(uint32_t* bl, uint32_t* bd, void** tl, void** td, void* stream);

// 0x0005da20: decodes literal/length and distance codes while at least 258 bytes of
// window and 10 input bytes are left; Z_OK, Z_STREAM_END or Z_DATA_ERROR (msg set).
int32_t XB_CDECL inflate_fast(uint32_t bl, uint32_t bd, const InflateHuft* tl, const InflateHuft* td,
                              InflateBlocksState* s, ZStream* z);

// 0x00056b80: case-insensitive compare of two file names.
int32_t XB_REGS2 XB_CDECL strcmpcasenosensitive_internal(const char* name1, const char* name2);

}  // namespace game
