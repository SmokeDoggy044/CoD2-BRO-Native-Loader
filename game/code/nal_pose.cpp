#include "nal_pose.h"

#include <cstring>

#include "lanes.h"
#include "nal_math.h"
#include "title_data.h"

namespace game {

namespace {

// Bone matrix `index` of the pose (64-byte rows, see nal_math.h).
float* bone(float* bones, uint8_t index) { return bones + index * 16; }


// Opcode 5's operands: five bone indices and five floats (25 bytes, unaligned).
struct IkOperands {
    uint8_t root, upper, lower, end, extra;
    float f0, f1, f2, f3, f4;
};

// Opcode 5's record from the animated data (0x50 bytes): the upper and end bones'
// rotations for forward kinematics, the IK target's rotation and position, the IK weight
// (+0x3c), the knee angle and the chain's twist about the root-target line (int16 each).
struct IkRecord {
    alignas(16) float upper[4];
    float end_rotation[4];
    float target_rotation[4];
    float target[3];
    float weight;
    int16_t knee;
    int16_t twist;
    uint8_t pad[12];
};
static_assert(sizeof(IkRecord) == 0x50);

// The three bones as plain forward kinematics (weight 0): the knee turns about -y by
// the stored angle; the lower and end bones sit -f4 and -0.5 / f1 along x.
void ik_forward(const IkOperands& o, const IkRecord& r, float* bones) {
    float* upper = bone(bones, o.upper);
    quat_rotation_rows(r.upper, upper);
    const float half = title_global<float>(0x0083eb24);
    const float angle = (static_cast<float>(r.knee) - half) * title_global<float>(0x0083edac) *
                        title_global<float>(0x007c3264) * half;
    const float zero = title_global<float>(0x0083eafc);
    alignas(16) const float knee[4] = {zero, -platform_x87_sin(angle), zero, platform_x87_cos(angle)};
    float* lower = bone(bones, o.lower);
    quat_rotation_rows(knee, lower);
    lower[12] = 0.0f - o.f4;
    lower[13] = 0.0f;
    lower[14] = 0.0f;
    float* end = bone(bones, o.end);
    quat_rotation_rows(r.end_rotation, end);
    end[12] = 0.0f - half / o.f1;
    end[13] = 0.0f;
    end[14] = 0.0f;
    alignas(16) float product[16];
    Mat44_MultiplyInPlace(upper, product, bone(bones, o.root));
    Mat44_MultiplyInPlace(lower, product, upper);
    Mat44_MultiplyInPlace(end, product, lower);
}

// An int16 angle of the record in radians: ((a - 0.5) * 2pi / 32768).
float record_angle(int16_t a) {
    return (static_cast<float>(a) - title_global<float>(0x0083eb24)) * title_global<float>(0x0083edac) *
           title_global<float>(0x007c3264);
}

// The pole of the IK plane for the axis selector `extra`: 0 = the target's y row x dir;
// 1/2 = the root's y row (2: negated) x dir, leaning towards (z - x) or -(z + x) of the
// root by the cosine between dir and that row. Other values keep `pole`.
void ik_pole(uint8_t extra, const float* root, const float* target_bone, const float* d, float* pole) {
    const float zero = 0.0f;
    const float one = title_global<float>(0x0083eb20);
    float c[3], dot;
    if (extra == 0) {
        const float* n = target_bone + 4;
        pole[0] = n[2] * d[1] - n[1] * d[2];
        pole[1] = n[0] * d[2] - d[0] * n[2];
        pole[2] = d[0] * n[1] - n[0] * d[1];
        return;
    } else if (extra == 1) {
        const float* n = root + 4;
        c[0] = n[2] * d[1] - n[1] * d[2];
        c[1] = n[0] * d[2] - d[0] * n[2];
        c[2] = d[0] * n[1] - n[0] * d[1];
        dot = (d[0] * n[0] + n[2] * d[2]) + n[1] * d[1];
    } else if (extra == 2) {
        const float n[3] = {zero - root[4], zero - root[5], zero - root[6]};
        c[0] = d[2] * n[1] - d[1] * n[2];
        c[1] = d[0] * n[2] - n[0] * d[2];
        c[2] = n[0] * d[1] - d[0] * n[1];
        dot = (n[0] * d[0] + d[2] * n[2]) + d[1] * n[1];
    } else {
        return;
    }
    const float ahead[3] = {(zero - root[0]) + root[8], (zero - root[1]) + root[9], (zero - root[2]) + root[10]};
    const float behind[3] = {(zero - root[0]) - root[8], (zero - root[1]) - root[9], (zero - root[2]) - root[10]};
    if (dot >= zero) {  // NaN: the other branch (comiss/jb)
        const float rest = one - dot;
        for (int i = 0; i < 3; ++i) pole[i] = ahead[i] * dot + rest * c[i];
    } else {
        const float m = zero - dot, k = dot + one;
        for (int i = 0; i < 3; ++i) pole[i] = behind[i] * m + k * c[i];
    }
}

// Rows 0-2 of an IK-placed bone turned to the skeleton's axes: (-r0, -r2, -r1), all lanes
// as 0 - x.
void ik_flip_rows(float* m) {
    float r[12];
    for (int i = 0; i < 12; ++i) r[i] = 0.0f - m[i];
    std::memcpy(m, r, 16);
    std::memcpy(m + 4, r + 8, 16);
    std::memcpy(m + 8, r + 4, 16);
}

// The IK solve shared by both weights: the two angles (NAL_IK_TwoBoneAngles from the
// upper bone's position to the target), the pole, then 0x00093250 builds the upper and
// lower bones' matrices; their rows are turned to the skeleton's axes.
void ik_solve(const IkOperands& o, const IkRecord& r, float* root, const float* target_bone, const float* upper_position,
              float* upper_out, float* lower_out, float* pole) {
    const float twist = record_angle(r.twist);
    const float point[3] = {upper_position[0], upper_position[1], upper_position[2]};
    float origin[3], dir[3], sin_a, cos_a, sin_b, cos_b;
    IkTwoBoneArgs args = {root, point, r.target, o.f0, o.f1, o.f2, o.f3, origin, dir, &sin_a, &cos_a, &sin_b, &cos_b};
    NAL_IK_TwoBoneAngles(&args);
    ik_pole(o.extra, root, target_bone, dir, pole);
    title_function<void XB_CDECL(float, float, float, float, float, const float*, const float*, const float*, float,
                                 float, float*, float*)>(0x00093250)(o.f4, sin_a, cos_a, sin_b, cos_b, origin, dir, pole,
                                                                     platform_x87_sin(twist), platform_x87_cos(twist),
                                                                     upper_out, lower_out);
    ik_flip_rows(upper_out);
    ik_flip_rows(lower_out);
}

// The target's matrix: its rotation's rows, the target position as row 3 (w 1).
void ik_target_matrix(const IkRecord& r, float* m) {
    quat_rotation_rows(r.target_rotation, m);
    m[12] = r.target[0];
    m[13] = r.target[1];
    m[14] = r.target[2];
}

// Opcode 5: a two-bone IK chain root -> upper -> lower -> end. Weight 0 is forward
// kinematics, 1 the IK solution; other weights slerp each bone's local rotation from the
// animation to the IK one. `pole` stays from the previous chain for selectors over 2.
// Returns the code after the operands.
const uint8_t* nal_pose_ik(const uint8_t* code, float* bones, uint8_t** anim_cursor, float* pole) {
    IkOperands o;
    o.root = code[0];
    o.upper = code[1];
    o.lower = code[2];
    o.end = code[3];
    o.extra = code[4];
    std::memcpy(&o.f0, code + 5, 20);
    IkRecord r;
    std::memcpy(&r, *anim_cursor, sizeof r);
    *anim_cursor += sizeof r;
    const float one = title_global<float>(0x0083eb20);
    float* root = bone(bones, o.root);
    float* upper = bone(bones, o.upper);
    float* lower = bone(bones, o.lower);
    float* end = bone(bones, o.end);
    alignas(16) float product[16];
    if (r.weight == 0.0f) {
        ik_forward(o, r, bones);
        return code + 25;
    }
    if (r.weight == one) {
        ik_target_matrix(r, end);
        ik_solve(o, r, root, end, upper + 12, upper, lower, pole);
        return code + 25;
    }
    // Blend: the IK matrices in locals, made local to their parents, as quaternions.
    alignas(16) float target[16];
    alignas(16) float upper_ik[16];
    alignas(16) float lower_ik[16];
    alignas(16) float inverse[16];
    ik_target_matrix(r, target);
    target[15] = one;
    ik_solve(o, r, root, target, upper + 12, upper_ik, lower_ik, pole);
    Mat44_MultiplyInPlace(target, product, Mat44_InverseRigid(lower_ik, inverse));
    Mat44_MultiplyInPlace(lower_ik, product, Mat44_InverseRigid(upper_ik, inverse));
    Mat44_MultiplyInPlace(upper_ik, product, Mat44_InverseRigid(root, inverse));
    alignas(16) float q_upper[4], q_lower[4], q_end[4], q[4];
    Quat_FromAxis3(q_upper, upper_ik);
    Quat_FromAxis3(q_lower, lower_ik);
    Quat_FromAxis3(q_end, target);
    const float half = title_global<float>(0x0083eb24);
    const float knee_angle = record_angle(r.knee) * half;
    const float zero = title_global<float>(0x0083eafc);
    alignas(16) const float knee[4] = {zero, -platform_x87_sin(knee_angle), zero, platform_x87_cos(knee_angle)};
    quat_rotation_rows(Quat_Slerp(q, r.upper, q_upper, r.weight), upper);
    quat_rotation_rows(Quat_Slerp(q, knee, q_lower, r.weight), lower);
    lower[12] = 0.0f - o.f4;
    lower[13] = 0.0f;
    lower[14] = 0.0f;
    quat_rotation_rows(Quat_Slerp(q, r.end_rotation, q_end, r.weight), end);
    end[12] = 0.0f - half / o.f1;
    end[13] = 0.0f;
    end[14] = 0.0f;
    Mat44_MultiplyInPlace(upper, product, root);
    Mat44_MultiplyInPlace(lower, product, upper);
    Mat44_MultiplyInPlace(end, product, lower);
    return code + 25;
}

}  // namespace

uint32_t XB_THISCALL NAL_GenericAnim_EvaluatePose(uint8_t* clip, uint8_t* state, float* bones, uint32_t range) {
    const uint8_t* ranges = *reinterpret_cast<uint8_t**>(clip + 0x64) + range * 20;
    const uint32_t count = *reinterpret_cast<const uint32_t*>(ranges);
    const float one = title_global<float>(0x0083eb20);
    for (uint32_t i = 0; i < count; ++i) {
        float* m = bones + i * 16;
        m[11] = 0.0f;
        m[7] = 0.0f;
        m[3] = 0.0f;
        m[15] = one;
    }
    // The byte code: the first `size` bytes of the clip's program (+0x68). Each opcode's
    // high bit picks the data cursor: clip +0xb0 (static data) or state +8 (animated).
    const uint8_t* code = *reinterpret_cast<uint8_t**>(clip + 0x68);
    const uint8_t* end = code + *reinterpret_cast<const uint32_t*>(ranges + 4);
    uint8_t* static_cursor = *reinterpret_cast<uint8_t**>(clip + 0xb0);
    uint8_t* anim_cursor = *reinterpret_cast<uint8_t**>(state + 8);
    float pole[3] = {};  // the original's is uninitialized until an IK chain sets it
    while (code < end) {
        const uint8_t op = *code++;
        uint8_t*& cursor = (op & 0x80) ? static_cursor : anim_cursor;
        const float* data = reinterpret_cast<const float*>(cursor);
        switch (op & 0x7f) {
        case 0: {  // position into row 3
            float* m = bone(bones, *code++);
            m[12] = data[0];
            m[13] = data[1];
            m[14] = data[2];
            cursor += 12;
            break;
        }
        case 1:  // rotation (quaternion) into rows 0-2
            quat_rotation_rows(data, bone(bones, *code++));
            cursor += 16;
            break;
        case 2: {  // {quaternion, position}
            float* m = bone(bones, *code++);
            quat_rotation_rows(data, m);
            m[12] = data[4];
            m[13] = data[5];
            m[14] = data[6];
            cursor += 32;
            break;
        }
        case 3: {  // scale rows 0-2 (all four lanes)
            float* m = bone(bones, *code++);
            const float s[3] = {data[0], data[1], data[2]};
            for (uint32_t row = 0; row < 3; ++row) {
                const Lanes r = mul(lanes(m + row * 4), s[row]);
                std::memcpy(m + row * 4, r.v, 16);
            }
            cursor += 12;
            break;
        }
        case 5:
            code = nal_pose_ik(code, bones, &anim_cursor, pole);
            break;
        case 12: {  // child = child * parent
            float* parent = bone(bones, code[0]);
            float* child = bone(bones, code[1]);
            code += 2;
            alignas(16) float product[16];
            Mat44_MultiplyInPlace(child, product, parent);
            break;
        }
        case 14:  // skip data
            cursor += *code++;
            break;
        default:
            break;
        }
    }
    return 0;
}


float* XB_CDECL NAL_IK_BuildChainMatrices(float length, float sin_a, float cos_a, float sin_b, float cos_b,
                                          const float* origin, const float* dir, const float* pole, float sin_t,
                                          float cos_t, float* upper, float* lower) {
    // The chain's frame A: rows dir, m = n x dir, n = |dir x pole| (the plane normal), origin
    // (w 0, 0, 0, 1). The upper bone is B * A and the lower C * A (all lanes, as
    // Mat44_MultiplyInPlace); B turns by angle a, C by b and sits `length` along B's x, both
    // twisted about x by t.
    const float zero = 0.0f;
    const float one = title_global<float>(0x0083eb20);
    const float c0 = pole[2] * dir[1] - dir[2] * pole[1];
    const float c1 = dir[2] * pole[0] - dir[0] * pole[2];
    const float c2 = dir[0] * pole[1] - pole[0] * dir[1];
    const float inv = platform_x87_rcp_norm4(c0, c1, c2, 0.0f, one);  // 1 / sqrt on the x87
    const float n2 = c2 * inv;
    const float n0 = inv * c0;
    const float n1 = inv * c1;
    const float m0 = dir[2] * n1 - n2 * dir[1];
    const float m1 = dir[0] * n2 - dir[2] * n0;
    const float m2 = n0 * dir[1] - dir[0] * n1;
    alignas(16) float a[16] = {dir[0], dir[1], dir[2], zero, m0, m1, m2, zero,
                               n0, n1, n2, zero, origin[0], origin[1], origin[2], one};
    const float neg_sin_t = zero - sin_t;
    alignas(16) float b[16] = {cos_a, sin_a * cos_t, sin_a * sin_t, zero, zero - sin_a, cos_a * cos_t, cos_a * sin_t, zero,
                               zero, neg_sin_t, cos_t, zero, zero, zero, zero, one};
    alignas(16) float product[16];
    Mat44_MultiplyInPlace(b, product, a);
    std::memcpy(upper, product, 64);
    const float reach = length * sin_a;
    alignas(16) float c[16] = {cos_b, zero - sin_b * cos_t, zero - sin_b * sin_t, zero,
                               sin_b, cos_b * cos_t, cos_b * sin_t, zero,
                               zero, neg_sin_t, cos_t, zero,
                               length * cos_a, reach * cos_t, reach * sin_t, one};
    Mat44_MultiplyInPlace(c, product, a);
    std::memcpy(lower, product, 64);
    return lower + 12;
}

}  // namespace game
