#pragma once

// NAL engine math (animation and skeleton code): 4x4 matrices of four 16-byte rows (row
// vectors, v * M; row 3 is the translation) and {x, y, z, w} quaternions. The title does
// these as packed SSE: every lane is computed, as here (MXCSR flags).

#include <cstdint>

#include "compiler.h"

namespace game {

// The rotation rows of the unit quaternion q (16-byte aligned) into rows 0-2 of out, w = 0
// (the shuffle code Quat_ToMat33, QuatTrans_ToMat44 and the pose evaluator inline).
void quat_rotation_rows(const float* q, float* out);
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
// 0x0008d730 (pair in ECX, callee pops 4): the inverse of a {quat, position} pair - the
// conjugate (q * (-1, -1, -1, 1)) and (0 - position) rotated by it. Seven words written
// (not the position's w). Returns out.
float* XB_THISCALL QuatTrans_Inverse(const float* qt, float* out);
// 0x0008d7e0: `a` concatenated with itself n times (n > 0; square and multiply, starting
// from the identity pair 0x00909850). Seven words written. Returns out.
float* XB_CDECL QuatTrans_Power(float* out, const float* a, int32_t n);
// 0x000260d0 (out in ECX, callee pops 0x10): out's four rows copied from r0..r3. Returns out.
float* XB_THISCALL Mat44_FromRows(float* out, const float* r0, const float* r1, const float* r2, const float* r3);
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

// 0x000930b0 (cdecl, 13 words): the two-bone IK setup. origin = point * matrix, dir =
// the unit vector from origin to target; the bones' cosines length * scale + bias / length
// (clamped to [-1, 1]) and their sines. The entry hands the C version its argument block
// (the original keeps the length in the `origin` slot).
struct IkTwoBoneArgs {
    const float* matrix;
    const float* point;
    const float* target;
    float scale_a, scale_b, bias_a, bias_b;
    float* origin;
    float* dir;
    float* sin_a;
    float* cos_a;
    float* sin_b;
    float* cos_b;
};
extern "C" void NAL_IK_TwoBoneAngles_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL NAL_IK_TwoBoneAngles(IkTwoBoneArgs* args);

}  // namespace game
