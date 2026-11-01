#pragma once

// Player movement and per-frame player helpers (pmove, compass markers).

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00207c60: the sentient's eye position into the two-slot history every interval.
void XB_CDECL Player_RecordEyeHistory(const uint8_t* pm);
// 0x00204640: compass marker word for the first hidden marked actor (0 without one).
uint32_t XB_CDECL Player_PackActorMarker(const float* origin);

}  // namespace game
