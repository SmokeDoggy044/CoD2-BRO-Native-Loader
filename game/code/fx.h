#pragma once

// The effect pool (FX_*): effects of 0x1c bytes, handed out from a free list.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

struct FxEffect {
    uint32_t unknown00;
    uint32_t unknown04;
    uint32_t unknown08;
    uint32_t unknown0c;
    uint32_t unknown10;
    uint16_t unknown14;
    uint16_t unknown16;
    int32_t unknown18;  // FX_AllocEffect: -1
};

#if UINTPTR_MAX == 0xFFFFFFFF  // the title's layout (32-bit pointers)
static_assert(sizeof(FxEffect) == 0x1c);
#endif

// 0x0023b970: takes an effect off the free list and clears it; null when the pool
// is not set up or empty.
FxEffect* XB_CDECL FX_AllocEffect();

// 0x00236b40: walks the active effects (0x00988804, count 0x0098880c) from the
// newest down and returns the first one whose definition (+4) has a priority
// (flags +0x50 >> 25) above `effect`'s (+0x68) and the evictable flag 0x1000000;
// null at the first one with a priority not above it, or with fewer than two
// active effects (the single entry is never looked at, as in the original).
uint8_t* XB_CDECL FX_FindEvictableEffect(const uint8_t* effect);

}  // namespace game
