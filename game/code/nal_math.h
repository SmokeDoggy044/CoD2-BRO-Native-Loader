#pragma once

// NAL engine math (animation and skeleton code): 4x4 matrices of four 16-byte rows (row
// vectors, v * M; row 3 is the translation) and {x, y, z, w} quaternions. The title does
// these as packed SSE: every lane is computed, as here (MXCSR flags).

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x0001c5b0 (A in ECX, callee pops 8): A's rotation rows times B, B's translation added
// to row 3; the product is written to A and to out (all reads come first). Returns out.
float* XB_THISCALL Mat44_MultiplyInPlace(float* a, float* out, const float* b);
// 0x0001c890 (out in ECX, callee pops 4): the rotation of the unit quaternion q (16-byte
// aligned) as three rows with w = 0; row 3 = (0, 0, 0, 1). Returns out.
float* XB_THISCALL Quat_ToMat33(float* out, const float* q);
// 0x0001cbe0 (out in EAX, {quat, position} in ECX): Quat_ToMat33's rows with the
// position as row 3.
void XB_REGS3 XB_CDECL QuatTrans_ToMat44(float* out, uint32_t unused_edx, const float* qt);
// 0x0001cae0: {quat, position} pairs (32 bytes, 16-byte aligned): out = a then b - the
// quaternion product, a's position rotated by b and moved by b's; out may be a or b.
// Returns out.
float* XB_CDECL QuatTrans_Concat(float* out, const float* a, const float* b);
// 0x0001c9f0: out = (v.x, v.y, v.z, 1) * m, three floats written. Returns out.
float* XB_CDECL Vec3_MulMat44(float* out, const float* v, const float* m);
// 0x0001c3b0: the slerp of unit quaternions a to b by t (16-byte aligned), the short way
// round; series for acos and sin, linear above cosine 0.999999. Returns out.
float* XB_CDECL Quat_Slerp(float* out, const float* a, const float* b, float t);
// 0x000262a0: the unit quaternion of three rotation rows (16-byte aligned). Returns out.
float* XB_CDECL Quat_FromAxis3(float* out, const float* m);
// 0x00086ae0 (matrix in ECX, callee pops 4): the inverse of a rigid matrix (rotation
// transposed, translation -t * R^T; w column 0, 0, 0, 1). Returns out.
float* XB_THISCALL Mat44_InverseRigid(const float* m, float* out);

}  // namespace game
