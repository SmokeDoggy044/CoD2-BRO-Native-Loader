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

// 0x00184640: directional move weights in the block 0x00922efc points to (+0x13c..+0x150, six
// floats cleared first) from the sine/cosine (Scr_SinCosDegrees) of (int)angle % 360: backwards
// (+0x13c) and one side (+0x144 / +0x148), with 0.005 / 1 above 0.996; forward (+0x140) or pure
// sideways at 1 / 0.005 above 0.996; else the 22.5-degree turned pair with the forward or side
// weight (+0x14c / +0x150 diagonals). `self` is unused; the argument slot holds the angle (and
// the side weight on the backwards path) at the end, as the original.
void XB_CDECL BroScr_ComputeMoveDirectionWeights(uint32_t self, float angle);

}  // namespace game
