#pragma once

// Compiled level-script math helpers (results through an out pointer, returned in EAX).

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00181c90: *out = |a - b|^2 ((dx*dx + dy*dy) + dz*dz).
float* XB_CDECL BroScr_DistanceSquared(float* out, float ax, float ay, float az, float bx, float by, float bz);

// 0x00182270 / 0x00181a90: out = v * scale (scale the MULSS destination). Both use their
// argument slots as scratch.
float* XB_CDECL BroScr_VectorScale(float* out, float x, float y, float z, float scale);
float* XB_CDECL BroScr_VectorScale_2(float* out, float x, float y, float z, float scale);

// 0x001844a0: *out = (int)angle % 360 as a float; with wrap set, values above 180 get -360.
float* XB_CDECL BroScr_AngleClamp(float* out, float angle, bool wrap);

}  // namespace game
