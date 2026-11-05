#include "bro_scr_math.h"

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

}  // namespace game
