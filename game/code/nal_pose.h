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

}  // namespace game
