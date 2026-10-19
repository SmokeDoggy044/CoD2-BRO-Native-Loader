// Four packed float lanes, for code the title runs as packed SSE (MULPS/ADDPS): every
// lane is computed, so the MXCSR flags of lanes the result drops still match.
#pragma once

#include <cstdint>
#include <cstring>

namespace game {
// Internal linkage per file, as before (no exported copies called through the PLT).
namespace {

struct Lanes {
    float v[4];
};

inline Lanes lanes(const float* p) { return {{p[0], p[1], p[2], p[3]}}; }

inline Lanes mul(const Lanes& a, float s) { return {{a.v[0] * s, a.v[1] * s, a.v[2] * s, a.v[3] * s}}; }

inline Lanes mul(const Lanes& a, const Lanes& b) {
    return {{a.v[0] * b.v[0], a.v[1] * b.v[1], a.v[2] * b.v[2], a.v[3] * b.v[3]}};
}

inline Lanes add(const Lanes& a, const Lanes& b) {
    return {{a.v[0] + b.v[0], a.v[1] + b.v[1], a.v[2] + b.v[2], a.v[3] + b.v[3]}};
}

inline Lanes sub(const Lanes& a, const Lanes& b) {
    return {{a.v[0] - b.v[0], a.v[1] - b.v[1], a.v[2] - b.v[2], a.v[3] - b.v[3]}};
}

inline Lanes neg(const Lanes& a) { return {{-a.v[0], -a.v[1], -a.v[2], -a.v[3]}}; }

inline float from_bits(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// |x| by clearing the sign bit (ANDNPS; std::fabs is called out of line at -O0).
inline float abs_bits(float x) {
    uint32_t bits;
    std::memcpy(&bits, &x, 4);
    return from_bits(bits & 0x7fffffffu);
}

// The title's packed sine polynomial (one lane): the angle folded into a quarter
// period by rounding with 1.5 * 2^23, then an odd polynomial of degree 9.
inline float scene_sine_poly(float angle) {
    const float y = -abs_bits(angle) * from_bits(0x3e22f983);  // 1 / (2 pi)
    const float magic = from_bits(0x4b400000);
    const float z = abs_bits(((y - magic) + magic) - y - 0.5f) - 0.25f;
    const float z2 = z * z;
    const float z4 = z2 * z2;
    const float z3 = z * z2;
    const float z5 = z * z4;
    float sum = z5 * z4 * from_bits(0x421ed7b7) + z3 * z4 * from_bits(0xc2992661);
    sum = sum + z5 * from_bits(0x42a33457);
    sum = sum + z3 * from_bits(0xc2255de0);
    return sum + z * from_bits(0x40c90fda);
}

}  // namespace
}  // namespace game
