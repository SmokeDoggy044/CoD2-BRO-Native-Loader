#pragma once

// The NAL pose evaluator (docs/nal_animation.md): the generic clip's byte-code program
// that writes a skeleton's bone matrices.

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x000886f0 (clip in ECX, callee pops 0xc): runs the first `size` bytes of the clip's
// program (+0x68) for group range `range` (+0x64, 20-byte {bone count, size, ...}) into
// `bones` (64-byte matrices; rows' w set to 0, 0, 0, 1 first). Opcodes (high bit: data
// from the clip's static cursor +0xb0 instead of the state's +8): 0 position, 1 rotation,
// 2 rotation + position, 3 scale, 5 two-bone IK, 12 child *= parent, 14 skip data.
uint32_t XB_THISCALL NAL_GenericAnim_EvaluatePose(uint8_t* clip, uint8_t* state, float* bones, uint32_t range);

// 0x00093250: the two-bone IK chain's bone matrices from the solved angles: the frame A
// = rows (dir, |dir x pole| x dir, |dir x pole|, origin); upper = B(a, t) * A, lower =
// C(b, t, length along x at angle a) * A, as 4x4 rows (t: the twist about x, by sine and
// cosine). Returns lower + 12 (the last row).
float* XB_CDECL NAL_IK_BuildChainMatrices(float length, float sin_a, float cos_a, float sin_b, float cos_b,
                                          const float* origin, const float* dir, const float* pole, float sin_t,
                                          float cos_t, float* upper, float* lower);

}  // namespace game
