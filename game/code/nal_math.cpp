#include "nal_math.h"

#include <cstring>

#include "lanes.h"
#include "title_data.h"

namespace game {

namespace {

// ((r0 * v.x + r1 * v.y) + r2 * v.z), four lanes, products row first.
Lanes rows_times(const float* rows, float x, float y, float z) {
    return add(add(mul(lanes(rows), x), mul(lanes(rows + 4), y)), mul(lanes(rows + 8), z));
}

}  // namespace

float* XB_THISCALL Mat44_MultiplyInPlace(float* a, float* out, const float* b) {
    float product[16];
    for (uint32_t row = 0; row < 3; ++row) {
        const Lanes r = rows_times(b, a[row * 4], a[row * 4 + 1], a[row * 4 + 2]);
        std::memcpy(product + row * 4, r.v, 16);
    }
    const Lanes r3 =
        add(add(add(lanes(b + 12), mul(lanes(b), a[12])), mul(lanes(b + 4), a[13])), mul(lanes(b + 8), a[14]));
    std::memcpy(product + 12, r3.v, 16);
    std::memcpy(a, product, 64);
    std::memcpy(out, product, 64);
    return out;
}

// The rotation rows of Quat_ToMat33 / QuatTrans_ToMat44 (identical code): lanes 0-2 of
// rows 0-2, then the zero vector's lane 0 (0x0082e6a0) as each row's w.
void quat_rotation_rows(const float* q, float* out) {
    const float* one_x = &title_global<float>(0x0082e6b0);  // (1, 0, 0, 0)
    const Lanes d = add(lanes(q), lanes(q));                 // 2q
    const Lanes p = mul(lanes(q), d.v[3]);                   // q * 2w
    const Lanes e = sub({{p.v[3], p.v[3], p.v[3], p.v[3]}}, lanes(one_x));
    platform_keep(e);
    // Rows as the original's shuffles assemble them; lane 3 is computed, then replaced.
    const Lanes row0 = add({{e.v[0], -p.v[2], p.v[1], p.v[1]}}, mul(d, q[0]));
    const Lanes row2 = add({{-p.v[1], p.v[0], e.v[0], e.v[0]}}, mul(d, q[2]));
    const Lanes row1 = add({{p.v[2], e.v[0], -p.v[0], -p.v[0]}}, mul(d, q[1]));
    platform_keep(row0);
    platform_keep(row1);
    platform_keep(row2);
    const float zero = title_global<float>(0x0082e6a0);
    for (uint32_t i = 0; i < 3; ++i) {
        out[i] = row0.v[i];
        out[4 + i] = row1.v[i];
        out[8 + i] = row2.v[i];
    }
    out[3] = zero;
    out[7] = zero;
    out[11] = zero;
}

float* XB_THISCALL Quat_ToMat33(float* out, const float* q) {
    out[7] = 0.0f;
    out[3] = 0.0f;
    out[14] = 0.0f;
    out[13] = 0.0f;
    out[12] = 0.0f;
    out[11] = 0.0f;
    out[15] = title_global<float>(0x0083eb20);
    quat_rotation_rows(q, out);
    return out;
}

void XB_REGS3 XB_CDECL QuatTrans_ToMat44(float* out, uint32_t, const float* qt) {
    out[7] = 0.0f;
    out[3] = 0.0f;
    out[11] = 0.0f;
    out[15] = title_global<float>(0x0083eb20);
    quat_rotation_rows(qt, out);
    out[12] = qt[4];
    out[13] = qt[5];
    out[14] = qt[6];
}

float* XB_CDECL QuatTrans_Concat(float* out, const float* a, const float* b) {
    // q = a * b: (b * a.w + a * b.w) + cross, w minus the 4-lane dot (mask (0, 0, 0, 1)).
    const Lanes la = lanes(a), lb = lanes(b);
    const Lanes ab = mul(la, lb);
    const Lanes pair = add({{ab.v[2], ab.v[3], ab.v[0], ab.v[1]}}, ab);
    const Lanes cross_r = mul({{a[1], a[2], a[0], a[3]}}, lb);
    const Lanes cross_l = mul(la, {{b[1], b[2], b[0], b[3]}});
    const Lanes cross = sub(cross_l, cross_r);
    const Lanes dot = mul(add({{pair.v[1], pair.v[0], pair.v[1], pair.v[0]}}, pair), lanes(&title_global<float>(0x008375d0)));
    const Lanes q = sub(add(add(mul(lb, a[3]), mul(la, b[3])), {{cross.v[1], cross.v[2], cross.v[0], cross.v[3]}}), dot);
    platform_keep(q);
    float rotation[16];
    float moved[3];
    Vec3_MulMat44(moved, a + 4, Quat_ToMat33(rotation, b));
    const float x = moved[0] + b[4], y = moved[1] + b[5], z = moved[2] + b[6];
    std::memcpy(out, q.v, 16);
    out[4] = x;
    out[5] = y;
    out[6] = z;
    return out;
}

float* XB_THISCALL Mat44_FromRows(float* out, const float* r0, const float* r1, const float* r2, const float* r3) {
    std::memcpy(out, r0, 16);
    std::memcpy(out + 4, r1, 16);
    std::memcpy(out + 8, r2, 16);
    std::memcpy(out + 12, r3, 16);
    return out;
}

float* XB_CDECL Vec3_MulMat44(float* out, const float* v, const float* m) {
    const Lanes r = add(add(add(lanes(m + 12), mul(lanes(m), v[0])), mul(lanes(m + 4), v[1])), mul(lanes(m + 8), v[2]));
    platform_keep(r);
    out[0] = r.v[0];
    out[1] = r.v[1];
    out[2] = r.v[2];
    return out;
}

float* XB_THISCALL Mat44_InverseRigid(const float* m, float* out) {
    const float zero = 0.0f;
    const float t0 = zero - ((m[14] * m[2] + m[13] * m[1]) + m[12] * m[0]);
    const float t1 = zero - ((m[14] * m[6] + m[13] * m[5]) + m[12] * m[4]);
    const float t2 = zero - ((m[14] * m[10] + m[13] * m[9]) + m[12] * m[8]);
    const float result[16] = {m[0], m[4], m[8],  zero, m[1], m[5], m[9], zero,
                              m[2], m[6], m[10], zero, t0,   t1,   t2,   title_global<float>(0x0083eb20)};
    std::memcpy(out, result, 64);
    return out;
}

float* XB_CDECL Quat_Slerp(float* out, const float* a, const float* b, float t) {
    const Lanes la = lanes(a), lb = lanes(b);
    const Lanes ab = mul(la, lb);
    const Lanes pair = add(ab, {{ab.v[2], ab.v[3], ab.v[0], ab.v[0]}});
    platform_keep(pair);
    const float dot = pair.v[0] + pair.v[1];
    const float one = title_global<float>(0x0083eb20);
    const float zero = 0.0f;
    // The weights (1 - t, t, 1, 0), t negated (and the cosine) on the short way round.
    Lanes weights;
    float cosine;
    if (zero > dot) {
        weights = {{one - t, zero - t, one, zero}};
        cosine = zero - dot;
    } else {
        weights = {{one - t, t, one, zero}};
        cosine = dot;
    }
    if (title_global<float>(0x0083efd8) > cosine) {  // 0.999999: else linear
        // The angle: an acos series below 0.5, else 2 asin(sqrt((1 - c) / 2)) as a series.
        float angle;
        if (title_global<float>(0x0083eb24) > cosine) {
            const float c2 = cosine * cosine;
            const float c3 = c2 * cosine;
            const float c5 = c3 * c2;
            const float c7 = c5 * c2;
            angle = (((c7 * title_global<float>(0x0083efd4) - c5 * title_global<float>(0x0083ee98)) -
                      c3 * title_global<float>(0x0083ee9c)) -
                     cosine) +
                    title_global<float>(0x0083ee88);
        } else {
            const float s = platform_x87_sqrt_sub_mul(one, cosine, title_global<float>(0x0083eb24));
            const float s2 = s * s;
            const float s3 = s2 * s;
            const float twice = s * title_global<float>(0x0083eba8);
            const float s5 = s3 * s2;
            const float s7 = s5 * s2;
            angle = ((s7 * title_global<float>(0x0083efd0) + s5 * title_global<float>(0x0083ed2c)) +
                     s3 * title_global<float>(0x0083ee90)) +
                    twice;
        }
        // sin of each weighted angle (odd series, constants 0x0082e850), over sin(angle).
        const float* k = &title_global<float>(0x0082e850);
        const Lanes v = mul(weights, angle);
        const Lanes v2 = mul(v, v);
        const Lanes v3 = mul(v2, v);
        const Lanes v5 = mul(v2, v3);
        const Lanes v7 = mul(v2, v5);
        const Lanes sine = add(add(add(v, mul(v7, k[2])), mul(v5, k[1])), mul(v3, k[0]));
        const float divisor = sine.v[2];
        weights = {{sine.v[0] / divisor, sine.v[1] / divisor, sine.v[2] / divisor, sine.v[3] / divisor}};
        platform_keep(weights);
    }
    const Lanes result = add(mul(la, weights.v[0]), mul(lb, weights.v[1]));
    std::memcpy(out, result.v, 16);
    return out;
}

float* XB_CDECL Quat_FromAxis3(float* out, const float* m) {
    // Rows m[0..3], m[4..7], m[8..11]; the largest of w, x, y, z found from the trace and
    // the diagonal, the others from sums/differences, all times 0.5 / sqrt(radicand).
    const float one = title_global<float>(0x0083eb20);
    const float half = title_global<float>(0x0083eb24);
    const Lanes r0 = lanes(m), r1 = lanes(m + 4), r2 = lanes(m + 8);
    const auto splat = [](float f) { return Lanes{{f, f, f, f}}; };
    const float trace = (m[10] + m[5]) + m[0];
    Lanes q;
    float radicand;
    if (trace >= title_global<float>(0x0083efb8)) {  // -1/3
        radicand = trace + one;
        const Lanes d1 = sub(r1, splat(m[1]));
        const Lanes d0 = sub(splat(m[2]), r2);
        const Lanes d2 = sub(r2, splat(m[6]));
        platform_keep(d0);
        platform_keep(d1);
        platform_keep(d2);
        q = {{d2.v[1], d0.v[0], d1.v[0], radicand}};
    } else if (m[5] > m[0] && m[5] > m[10]) {
        radicand = ((m[5] - m[0]) - m[10]) + one;
        const Lanes d0 = sub(splat(m[2]), r2);
        const Lanes s2 = add(r2, splat(m[6]));
        const Lanes s1 = add(r1, splat(m[1]));
        platform_keep(d0);
        platform_keep(s1);
        platform_keep(s2);
        q = {{s1.v[0], radicand, s2.v[1], d0.v[0]}};
    } else if (!(m[5] > m[0]) && m[0] > m[10]) {
        radicand = ((m[0] - m[5]) - m[10]) + one;
        const Lanes d2 = sub(r2, splat(m[6]));
        const Lanes s2 = add(r2, splat(m[2]));
        const Lanes s1 = add(r1, splat(m[1]));
        platform_keep(d2);
        platform_keep(s1);
        platform_keep(s2);
        q = {{radicand, s1.v[0], s2.v[0], d2.v[1]}};
    } else {
        radicand = ((m[10] - m[0]) - m[5]) + one;
        const Lanes d1 = sub(r1, splat(m[1]));
        const Lanes s6 = add(r2, splat(m[6]));
        const Lanes s2 = add(r2, splat(m[2]));
        platform_keep(d1);
        platform_keep(s2);
        platform_keep(s6);
        q = {{s2.v[0], s6.v[1], radicand, d1.v[0]}};
    }
    const Lanes result = mul(q, platform_x87_half_over_sqrt(radicand, half));
    std::memcpy(out, result.v, 16);
    return out;
}

PLATFORM_ASM_ENTRY(NAL_IK_TwoBoneAngles_entry,
                   "leal 4(%esp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(NAL_IK_TwoBoneAngles) "addl $4, %esp\n"
                   "ret")

void XB_CDECL NAL_IK_TwoBoneAngles(IkTwoBoneArgs* args) {
    float* origin = args->origin;
    float* dir = args->dir;
    Vec3_MulMat44(origin, args->point, args->matrix);
    dir[0] = args->target[0] - origin[0];
    dir[1] = args->target[1] - origin[1];
    dir[2] = args->target[2] - origin[2];
    const float one = title_global<float>(0x0083eb20);
    const float minus_one = title_global<float>(0x0083eb34);
    const float length = platform_x87_norm3(dir[0], dir[1], dir[2]);
    // The original keeps the length in its `origin` argument slot.
    std::memcpy(&args->origin, &length, 4);
    const float inverse = one / length;
    dir[0] = dir[0] * inverse;
    dir[1] = dir[1] * inverse;
    dir[2] = dir[2] * inverse;
    *args->cos_a = length * args->scale_a + inverse * args->bias_a;
    *args->cos_b = length * args->scale_b + inverse * args->bias_b;
    float c = *args->cos_a;
    if (c > one)
        c = one;
    else if (minus_one > c)
        c = minus_one;
    *args->cos_a = c;
    c = *args->cos_b;
    if (c > one)
        c = one;
    else if (minus_one > c)
        c = minus_one;
    *args->cos_b = c;
    *args->sin_a = platform_x87_sqrt_one_minus_sq(*args->cos_a, one);
    *args->sin_b = platform_x87_sqrt_one_minus_sq(*args->cos_b, one);
}

}  // namespace game
