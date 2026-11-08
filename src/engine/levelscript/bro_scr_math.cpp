#include "bro_scr_math.h"

#include <cmath>

#include "title_data.h"

namespace game {

float* XB_CDECL BroScr_DistanceSquared(float* out, float ax, float ay, float az, float bx, float by, float bz) {
    const float dx = ax - bx;
    const float dy = ay - by;
    const float dz = az - bz;
    *out = platform_addss(platform_addss(dx * dx, dy * dy), dz * dz);
    return out;
}

float* XB_CDECL BroScr_VectorScale(float* out, float x, float y, float z, float scale) {
    const float rx = platform_mulss(scale, x);
    const float ry = platform_mulss(scale, y);
    const float rz = platform_mulss(scale, z);
    out[0] = rx;
    out[1] = ry;
    out[2] = rz;
    return out;
}

float* XB_CDECL BroScr_VectorScale_2(float* out, float x, float y, float z, float scale) {
    return BroScr_VectorScale(out, x, y, z, scale);
}

float* XB_CDECL BroScr_AngleClamp(float* out, float angle, bool wrap) {
    const float f = static_cast<float>(static_cast<int32_t>(angle) % 360);  // cvttss2si, idiv, cvtsi2ss
    // The original turns (f > 180) into 0/1, converts that to a float and compares it with 0.
    *out = (wrap && f > 180.0f) ? f - 360.0f : f;
    return out;
}

namespace {

float title_float(uint32_t address) { return title_global<float>(address); }

float* move_weights() { return reinterpret_cast<float*>(static_cast<uintptr_t>(title_global<uint32_t>(0x00922efc) + 0x13c)); }

void sin_cos(float angle, float* first, float* second) {
    title_function<void XB_CDECL(float, float*, float*)>(title_global<uint32_t>(0x0092223c))(angle, first, second);  // Scr_SinCosDegrees
}

float clamp(float angle) {
    float out;
    return *title_function<float* XB_CDECL(float*, float, bool)>(0x001844a0)(&out, angle, false);  // BroScr_AngleClamp
}

}  // namespace

void XB_CDECL BroScr_ComputeMoveDirectionWeights(uint32_t self, float angle) {
    (void)self;
    angle = static_cast<float>(static_cast<int32_t>(angle) % 360);  // cvttss2si, idiv; kept in the argument slot
    float s = 0, c = 0;
    sin_cos(angle, &s, &c);
    // w[0] +0x13c, w[1] +0x140, w[2] +0x144, w[3] +0x148, w[4] +0x14c, w[5] +0x150
    move_weights()[4] = 0.0f;
    move_weights()[5] = 0.0f;
    move_weights()[1] = 0.0f;
    move_weights()[0] = 0.0f;
    move_weights()[2] = 0.0f;
    move_weights()[3] = 0.0f;
    const float almost_one = title_float(0x00825bac);  // 0.996
    const float tiny = title_float(0x0083eb88);        // 0.005
    const float one = title_float(0x0083eb20);
    const float half_step = title_float(0x00825ba4);   // 22.5
    if (0.0f > c) {
        move_weights()[0] = platform_addss(0.0f, -c);
        if (0.0f > s) {
            move_weights()[3] = platform_addss(0.0f, -s);
            angle = move_weights()[3];  // through the argument slot
            if (angle > almost_one) {
                move_weights()[0] = tiny;
                move_weights()[3] = one;
            }
        } else {
            move_weights()[2] = s;
            angle = move_weights()[2];
            if (angle > almost_one) {
                move_weights()[0] = tiny;
                move_weights()[2] = one;
            }
        }
        return;
    }
    if (c > almost_one || std::fabs(s) > almost_one) {
        const bool forward = c > almost_one;
        move_weights()[1] = forward ? one : tiny;
        move_weights()[s > 0.0f ? 2 : 3] = forward ? tiny : one;
        return;
    }
    if (c > title_float(0x00825ba8)) {  // 0.707
        if (s > 0.0f) {
            angle = platform_addss(angle, half_step);
            sin_cos(clamp(angle), &s, &c);
            move_weights()[5] = s;
        } else {
            sin_cos(clamp(platform_addss(angle, -half_step)), &s, &c);
            move_weights()[4] = platform_addss(0.0f, -s);
        }
        move_weights()[1] = c;
    } else if (s > 0.0f) {
        sin_cos(clamp(platform_addss(angle, -half_step)), &s, &c);
        move_weights()[2] = s;
        move_weights()[5] = c;
    } else {
        angle = platform_addss(angle, half_step);
        sin_cos(clamp(angle), &s, &c);
        move_weights()[3] = platform_addss(0.0f, -s);
        move_weights()[4] = c;
    }
}

}  // namespace game
