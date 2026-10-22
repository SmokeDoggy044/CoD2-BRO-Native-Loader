#include "g_scr_math.h"

#include <cstring>

#include "g_utils.h"
#include "qcommon.h"
#include "title_data.h"

namespace game {

namespace {

using ext = long double;

float k_zero() { return title_global<float>(0x0083eafc); }
float k_one() { return title_global<float>(0x0083eb20); }

// The LCG the script random builtins share with the effects code (0x008d5a70): 15 bits.
int32_t script_rand15() {
    uint32_t& seed = title_global<uint32_t>(0x008d5a70);
    seed = seed * 0x343fd + 0x269ec3;
    return static_cast<int32_t>(seed >> 17);
}

// A title CRT x87 intrinsic (argument and result in ST0) at its title address.
ext title_ci_unary_ext(uint32_t function, ext x) {
    ext result;
    __asm__ volatile("call *%2" : "=t"(result) : "0"(x), "r"(uintptr_t{function}) : "eax", "ecx", "edx", "memory");
    return result;
}

ext sum_of_squares(float x, float y, float z) {
    ext sum = static_cast<ext>(x) * x;
    sum = sum + static_cast<ext>(y) * y;
    return sum + static_cast<ext>(z) * z;
}

}  // namespace

ext XB_CDECL Scr_Cos(float x) { return platform_x87_cos_ext(x); }
ext XB_CDECL Scr_Sin(float x) { return platform_x87_sin_ext(x); }
ext XB_CDECL Scr_Tan(float x) { return platform_x87_tan_ext(x); }
ext XB_CDECL Scr_Abs(float x) { return platform_x87_abs_ext(x); }
ext XB_CDECL Scr_Sqrt(float x) { return platform_x87_sqrt_ext(x); }
ext XB_CDECL Scr_Atan(float x) { return platform_x87_atan_ext(x); }
ext XB_CDECL Scr_Log(float x) { return platform_x87_ln_ext(x); }

ext XB_CDECL Scr_VectorDot(const float* a, const float* b) {
    ext sum = static_cast<ext>(a[2]) * b[2];
    sum = sum + static_cast<ext>(a[1]) * b[1];
    return sum + static_cast<ext>(a[0]) * b[0];
}

ext XB_CDECL Scr_LengthSquared(const float* v) { return sum_of_squares(v[0], v[1], v[2]); }

ext XB_CDECL Scr_Length(const float* v) { return platform_x87_sqrt_ext(sum_of_squares(v[0], v[1], v[2])); }

namespace {

void difference(const float* a, const float* b, float* d) {
    for (int i = 0; i < 3; ++i) d[i] = static_cast<float>(static_cast<ext>(b[i]) - a[i]);
}

}  // namespace

float XB_CDECL Scr_Distance(const float* a, const float* b) {
    float d[3];
    difference(a, b, d);
    return static_cast<float>(title_ci_unary_ext(0x0033acf4, sum_of_squares(d[2], d[1], d[0])));  // __CIsqrt
}

ext XB_CDECL Scr_DistanceSquared(const float* a, const float* b) {
    float d[3];
    difference(a, b, d);
    return sum_of_squares(d[2], d[1], d[0]);
}

uint32_t XB_CDECL Scr_Closer(const float* a, const float* b, const float* c) {
    const float by = a[1] - b[1], bz = a[2] - b[2], bx = a[0] - b[0];
    const float to_b = (bz * bz + by * by) + bx * bx;
    const float cz = a[2] - c[2], cy = a[1] - c[1], cx = a[0] - c[0];
    const float to_c = (cz * cz + cy * cy) + cx * cx;
    return to_c > to_b ? 1 : 0;
}

void XB_CDECL Scr_VectorNormalize(float* out, const float* v) {
    uint32_t* words = reinterpret_cast<uint32_t*>(out);
    const uint32_t* from = reinterpret_cast<const uint32_t*>(v);
    words[0] = from[0];
    words[1] = from[1];
    words[2] = from[2];
    const float length = static_cast<float>(platform_x87_sqrt_ext(sum_of_squares(out[0], out[1], out[2])));
    const float scale = k_one() / length;
    out[0] = out[0] * scale;
    out[1] = out[1] * scale;
    out[2] = out[2] * scale;
}

void XB_CDECL Scr_VectorToAngles(float* angles, const float* v) { vectoangles_call(v, angles); }

void XB_CDECL Scr_SinCosDegrees(float degrees, float* sine, float* cosine) {
    const float angle = degrees * title_global<float>(0x0083ed00) * title_global<float>(0x0083ecfc);  // pi, 1/180
    FastSinCos(sine, 0, cosine, angle);
}

int32_t XB_CDECL Scr_RandomInt(int32_t n) {
    return static_cast<int32_t>(static_cast<uint32_t>(script_rand15()) * static_cast<uint32_t>(n)) >> 15;
}

int32_t XB_CDECL Scr_RandomIntRange(int32_t a, int32_t b) {
    const uint32_t r = static_cast<uint32_t>(script_rand15());
    if (b > a) return (static_cast<int32_t>(r * static_cast<uint32_t>(b - a)) >> 15) + a;
    return (static_cast<int32_t>(r * static_cast<uint32_t>(a - b)) >> 15) + b;
}

namespace {

// (high - low) * r / 32768 + low on the x87 (r = 15 random bits), stored as float.
float random_between(float high, float low, int32_t r) {
    const float fraction = static_cast<float>(static_cast<ext>(r));
    ext value = static_cast<ext>(high) - low;
    value = value * fraction;
    value = value / title_global<float>(0x0083ebc8);  // 32768
    return static_cast<float>(value + low);
}

}  // namespace

float XB_CDECL Scr_RandomFloat(float max) { return random_between(max, k_zero(), script_rand15()); }

float XB_CDECL Scr_RandomFloatRange(float a, float b) {
    const int32_t r = script_rand15();
    if (a >= b) return random_between(a, b, r);
    return random_between(b, a, r);
}

int32_t XB_REGS3 XB_STDCALL Scr_FloatToInt(uint32_t, uint32_t, int32_t* out, float value) {
    if (value == title_global<float>(0x00922eac)) {
        *out = title_global<int32_t>(0x00922ec0);
        return title_global<int32_t>(0x00922ec0);
    }
    const int32_t result = static_cast<int32_t>(value);
    *out = result;
    return result;
}

float XB_REGS3 XB_STDCALL Scr_IntToFloat(uint32_t, uint32_t, float* out, int32_t value) {
    if (value == title_global<int32_t>(0x00922ec0)) {
        *out = title_global<float>(0x00922eac);
        return title_global<float>(0x00922eac);
    }
    const float result = static_cast<float>(value);
    *out = result;
    return result;
}

namespace {

float degrees_to_radians(float degrees) { return degrees * title_global<float>(0x0083edd8); }

}  // namespace

void XB_CDECL Scr_AnglesToUp(float* out, const float* angles) {
    float sp, cp, sr, cr;
    FastSinCos(&sp, 0, &cp, degrees_to_radians(angles[0]));
    FastSinCos(&sr, 0, &cr, degrees_to_radians(angles[2]));
    out[0] = cr * sp;
    out[1] = 0.0f - sr;
    out[2] = cr * cp;
}

void XB_CDECL Scr_AnglesToForward(float* out, const float* angles) {
    float sy, cy, sp, cp;
    FastSinCos(&sy, 0, &cy, degrees_to_radians(angles[1]));
    FastSinCos(&sp, 0, &cp, degrees_to_radians(angles[0]));
    out[1] = cp * sy;
    out[0] = cp * cy;
    out[2] = 0.0f - sp;
}

void XB_CDECL Scr_AnglesToRight(float* out, const float* angles) {
    float sy, cy, sr, cr;
    FastSinCos(&sy, 0, &cy, degrees_to_radians(angles[1]));
    FastSinCos(&sr, 0, &cr, degrees_to_radians(angles[2]));
    out[0] = cr * sy;
    out[1] = 0.0f - cr * cy;
    out[2] = 0.0f - sr;
}

}  // namespace game
