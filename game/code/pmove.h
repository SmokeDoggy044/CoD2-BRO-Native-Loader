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

// 0x002ac770: TODO.
// 0x002ac770: the fired weapon's screen blend (def +0x660 * 255) adds to the player state's +0x4f0 (at most 255)
// unless +0x88 is 1.
void XB_CDECL PM_AddWeaponFireBlend();

}  // namespace game
