// Four packed float lanes, for code the title runs as packed SSE (MULPS/ADDPS): every
// lane is computed, so the MXCSR flags of lanes the result drops still match.
#pragma once

namespace game {
// Internal linkage per file, as before (no exported copies called through the PLT).
namespace {

struct Lanes {
    float v[4];
};

inline Lanes lanes(const float* p) { return {{p[0], p[1], p[2], p[3]}}; }

inline Lanes mul(const Lanes& a, float s) { return {{a.v[0] * s, a.v[1] * s, a.v[2] * s, a.v[3] * s}}; }

inline Lanes add(const Lanes& a, const Lanes& b) {
    return {{a.v[0] + b.v[0], a.v[1] + b.v[1], a.v[2] + b.v[2], a.v[3] + b.v[3]}};
}

inline Lanes sub(const Lanes& a, const Lanes& b) {
    return {{a.v[0] - b.v[0], a.v[1] - b.v[1], a.v[2] - b.v[2], a.v[3] - b.v[3]}};
}

inline Lanes neg(const Lanes& a) { return {{-a.v[0], -a.v[1], -a.v[2], -a.v[3]}}; }

}  // namespace
}  // namespace game
