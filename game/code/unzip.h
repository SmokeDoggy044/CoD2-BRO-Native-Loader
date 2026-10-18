#pragma once

// The title's zlib (Q3's qcommon/unzip.c copy of inflate).

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x0005d8c0: the fixed Huffman tables (bit counts 9 and 5); always Z_OK. `stream`
// is unused.
int32_t XB_CDECL inflate_trees_fixed(uint32_t* bl, uint32_t* bd, void** tl, void** td, void* stream);

}  // namespace game
