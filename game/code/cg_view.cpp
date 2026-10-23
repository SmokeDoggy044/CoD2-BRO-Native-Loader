#include "cg_view.h"

#include <cstring>

#include "cg_ents.h"
#include "cgame.h"
#include "lanes.h"
#include "ngl.h"
#include "qcommon.h"
#include "title_data.h"

namespace game {

namespace {

float g(uint32_t address) { return title_global<float>(address); }
int32_t gi(uint32_t address) { return title_global<int32_t>(address); }
const uint8_t* weapon() { return title_global<const uint8_t*>(0x00972acc); }  // the current weapon's definition
float wf(uint32_t offset) { return *reinterpret_cast<const float*>(weapon() + offset); }

// 0 below zero, 1 above one; the title tests below zero twice (COMISS each time).
float clamp01(float f) {
    const float one = g(0x0083eb20);
    if (0.0f > f) return 0.0f;
    if (f > one) return one;
    if (0.0f > f) return 0.0f;
    return f;
}

// The sway target of CG_CalcGunRotOffset/CG_CalcGunMoveOffset: the speed above the
// stance's threshold as 0..1 of the range up to 0x009725a0, times weight + cvar per axis.
void sway_target(float threshold, const float* weights, const uint32_t* cvars, float* target) {
    const float speed = g(0x00973a84);
    if (speed > threshold && gi(0x009725d4) != 5) {
        const float f = clamp01((speed - threshold) / (static_cast<float>(gi(0x009725a0)) - threshold));
        for (int i = 0; i < 3; ++i) target[i] = g(cvars[i]) * f + weights[i] * f;
    } else {
        target[0] = target[1] = target[2] = 0.0f;
    }
}

// Moves cur (3 floats) towards target at the weapon's rate (+rate_other, or +rate_frame
// while 0x009725fc is the frame time) plus `bias`, per ms of the frame time, at least
// 0.0001 per ms, without passing the target.
void settle(float* cur, const float* target, uint32_t rate_frame, uint32_t rate_other, uint32_t bias) {
    for (int i = 0; i < 3; ++i) {
        if (cur[i] == target[i]) continue;
        const float x = g(0x009725fc);
        const float frame = static_cast<float>(gi(0x00972970));
        const float diff = target[i] - cur[i];
        const float ms = static_cast<float>(gi(0x00972534));
        const float rate = x == frame ? wf(rate_frame) : wf(rate_other);
        float step = (rate + g(bias)) * diff * ms * g(0x0083eb4c);  // 0.001
        if (target[i] > cur[i]) {
            const float least = ms * g(0x0083ebb0);  // 0.0001
            if (least > step) step = least;
            cur[i] = cur[i] + step;
            if (cur[i] > target[i]) cur[i] = target[i];
        } else {
            const float least = ms * g(0x0083ebac);  // -0.0001
            if (step > least) step = least;
            cur[i] = cur[i] + step;
            if (target[i] > cur[i]) cur[i] = target[i];
        }
    }
}

// out += cur, or cur * (1 - 2 * aim) while aiming (0x009725d8) under half; nothing above.
void add_sway(float* out, const float* cur) {
    const float aim = g(0x009725d8);
    if (aim == 0.0f) {
        for (int i = 0; i < 3; ++i) out[i] = out[i] + cur[i];
    } else if (g(0x0083eb24) > aim) {
        const float k = g(0x0083eb20) - aim * g(0x0083eba8);
        for (int i = 0; i < 3; ++i) out[i] = cur[i] * k + out[i];
    }
}

const float* weapon_floats(uint32_t offset) { return reinterpret_cast<const float*>(weapon() + offset); }

}  // namespace

PLATFORM_ASM_ENTRY(CG_CalcGunRotOffset_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(CG_CalcGunRotOffset) "addl $4, %esp\n"
                   "ret")

void XB_CDECL CG_CalcGunRotOffset(float* out) {
    const uint32_t flags = title_global<uint32_t>(0x0097257c);
    float threshold;
    uint32_t weights;
    if (flags & 1) {
        threshold = wf(0x574);
        weights = 0x53c;
    } else if (flags & 2) {
        threshold = wf(0x570);
        weights = 0x518;
    } else {
        threshold = wf(0x56c);
        weights = 0x4f4;
    }
    static const uint32_t kCvars[3] = {0x0094b8a8, 0x0094acd0, 0x009497d0};
    float target[3];
    sway_target(threshold + g(0x0094c088), weapon_floats(weights), kCvars, target);
    const float aim = g(0x009725d8);
    if (!(aim == 0.0f)) {
        const float s = g(0x0083eb20) - aim;
        for (float& t : target) t = s * t;
    }
    float* cur = &title_global<float>(0x00972ac0);
    settle(cur, target, 0x564, 0x560, 0x00948930);
    add_sway(out, cur);
}

PLATFORM_ASM_ENTRY(CG_CalcGunMoveOffset_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(CG_CalcGunMoveOffset) "addl $4, %esp\n"
                   "ret")

void XB_CDECL CG_CalcGunMoveOffset(float* out) {
    const uint32_t flags = title_global<uint32_t>(0x0097257c);
    float threshold;
    uint32_t weights;
    if (flags & 0x10000) {
        threshold = wf(0x550);
        weights = 0x4d0;
    } else if (flags & 1) {
        threshold = wf(0x55c);
        weights = 0x530;
    } else if (flags & 2) {
        threshold = wf(0x558);
        weights = 0x50c;
    } else {
        threshold = wf(0x554);
        weights = 0x4e8;
    }
    static const uint32_t kCvars[3] = {0x0094b428, 0x0094b278, 0x0094c560};
    float target[3];
    sway_target(threshold + g(0x00948e40), weapon_floats(weights), kCvars, target);
    const int32_t state = gi(0x009725f8);
    const float* offset = nullptr;
    if (state == gi(0x00972974))
        offset = weapon_floats(0x500);
    else if (state == gi(0x00972970))
        offset = weapon_floats(0x524);
    if (offset) {
        static const uint32_t kOffsetCvars[3] = {0x0094c950, 0x0094af10, 0x00948ed0};
        for (int i = 0; i < 3; ++i) target[i] = g(kOffsetCvars[i]) + (offset[i] + target[i]);
    }
    float* cur = &title_global<float>(0x00972ab4);
    settle(cur, target, 0x54c, 0x548, 0x0094a2a0);
    add_sway(out, cur);
}

void XB_CDECL CG_UpdateGunKick() {
    int32_t remaining = gi(0x00972534);
    if (remaining <= 0) return;
    float* angles = &title_global<float>(0x00972f88);
    float* velocity = &title_global<float>(0x00972f94);
    do {
        const int32_t step = remaining > 5 ? 5 : remaining;
        const float dt = static_cast<float>(step) * g(0x0083eb4c);  // 0.001
        for (int i = 0; i < 3; ++i) {
            float& p = angles[i];
            float& v = velocity[i];
            const bool still = v == 0.0f;
            if (p == 0.0f && still) continue;
            if (!still) {
                // Pushed back against the motion.
                const float sign = v > 0.0f ? g(0x0083eb34) : g(0x0083eb20);  // -1, 1
                float push;
                if (gi(0x009725d0))
                    push = g(0x009725d8) > g(0x0083eb24) ? wf(0x7ac) * sign : wf(0x7f4) * sign;
                else
                    push = sign * g(0x0083ed18);  // 2400
                p = push * dt + p;
            }
            float d = p * dt;
            if (0.0f > v * d) d = d * g(0x0083ed14);  // 0.06 against the motion
            const float next = v + d;
            if (!(v * next >= 0.0f)) {
                v = 0.0f;
                p = 0.0f;
                continue;
            }
            v = next;
            if (next == 0.0f) {
                p = 0.0f;
            } else if (platform_x87_greater(abs_bits(next), g(0x0083eb98))) {  // |v| > 10
                v = next > 0.0f ? g(0x0083eb98) : g(0x0083ed10);
                p = 0.0f;
            }
        }
        remaining -= 5;
    } while (remaining > 0);
}

PLATFORM_ASM_ENTRY(CG_ShellShockViewKick_entry,
                   "pushl %edi\n"
                   "pushl %edx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CG_ShellShockViewKick) "addl $12, %esp\n"
                   "ret")

void XB_CDECL CG_ShellShockViewKick(int32_t duration, const uint8_t* params, int32_t elapsed) {
    float* kick = &title_global<float>(0x00973a5c);
    const int32_t remaining = duration - elapsed;
    if (remaining <= 0) {
        nglSetSceneCallback(2, nullptr, nullptr);
        kick[0] = 0.0f;
        kick[1] = 0.0f;
        return;
    }
    float s = g(0x0083eb20);
    const int32_t fade = *reinterpret_cast<const int32_t*>(params);
    if (remaining < fade) s = static_cast<float>(remaining) / static_cast<float>(fade);
    const float amplitude =
        (g(0x0083ec9c) - s * g(0x0083eba8)) * s * s * *reinterpret_cast<const float*>(params + 8);  // 3 - 2s
    const float t = static_cast<float>(elapsed) * *reinterpret_cast<const float*>(params + 4);
    const int32_t whole = static_cast<int32_t>(t);
    const float f = t - static_cast<float>(whole);
    const uint32_t index = (static_cast<uint32_t>(duration) * 0x3d + static_cast<uint32_t>(whole)) & 0x7f;
    const float* table = &title_global<float>(0x0082f4a0) + index * 2;
    for (int axis = 0; axis < 2; ++axis) {
        const float p0 = table[axis], p1 = table[2 + axis], p2 = table[4 + axis], p3 = table[6 + axis];
        const float c3 = ((p3 - p2) + p1) - p0;
        const float c2 = (p0 - p1) - c3;
        kick[axis] = ((((c2 + c3 * f) * f + (p2 - p0)) * f) + p1) * amplitude;
    }
}

PLATFORM_ASM_ENTRY(CG_ReadShellShockCvars_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(CG_ReadShellShockCvars) "addl $4, %esp\n"
                   "ret")

namespace {

// A time cvar in ms: seconds * 1000 + 0.5, truncated.
int32_t cvar_ms(uint32_t cvar) { return static_cast<int32_t>(g(cvar) * g(0x0083eb9c) + g(0x0083eb24)); }

int32_t cvar_ms_min1(uint32_t cvar) {
    const int32_t ms = cvar_ms(cvar);
    return ms < 1 ? 1 : ms;
}

}  // namespace

void XB_CDECL CG_ReadShellShockCvars(uint8_t* out) {
    auto i32 = [out](uint32_t offset) -> int32_t& { return *reinterpret_cast<int32_t*>(out + offset); };
    auto f32 = [out](uint32_t offset) -> float& { return *reinterpret_cast<float*>(out + offset); };
    i32(0xc) = cvar_ms_min1(0x0094a580);
    i32(0x10) = cvar_ms_min1(0x0094afa8);
    i32(0x0) = cvar_ms_min1(0x009480c0);
    const float least = g(0x0083eb4c);  // 0.001
    const float rate = g(0x0094a610);
    f32(0x4) = least / (least > rate ? least : rate);
    f32(0x8) = g(0x0094b6f8);
    i32(0x14) = gi(0x00949114) != 0;
    i32(0x18) = cvar_ms_min1(0x009484b0);
    i32(0x1c) = cvar_ms_min1(0x0094c680);
    i32(0x60) = cvar_ms_min1(0x0094b038);
    i32(0x64) = cvar_ms(0x0094ae80);
    title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(  // _strncpy
        reinterpret_cast<char*>(out + 0x24), &title_global<char>(0x0094c2d0), 0xf);
    out[0x33] = 0;
    f32(0x20) = clamp01(g(0x00948d20));
    i32(0x5c) = cvar_ms(0x009498f0);
    static const uint32_t kBlends[][2] = {{0x34, 0x00948420}, {0x38, 0x0094b398}, {0x44, 0x00947bb0},
                                          {0x48, 0x00947c40}, {0x40, 0x0094bed8}, {0x3c, 0x0094a850},
                                          {0x4c, 0x0094a730}, {0x50, 0x0094b0c8}, {0x54, 0x00948270},
                                          {0x58, 0x00948150}};
    for (const auto& blend : kBlends) f32(blend[0]) = clamp01(g(blend[1]));
    i32(0x68) = gi(0x0094c35c) != 0;
    i32(0x6c) = cvar_ms_min1(0x00948b70);
    f32(0x74) = g(0x0094b4b8);
    f32(0x78) = g(0x00947b20);
    f32(0x70) = g(0x0094c710);
}

void XB_REGS3 XB_CDECL CG_ShellShockScreenParams(uint32_t, int32_t elapsed, const uint8_t* params,
                                                 int32_t duration) {
    float& blend = title_global<float>(0x00973a58);
    float& blur0 = title_global<float>(0x00961778);
    float& blur1 = title_global<float>(0x0096177c);
    const float* p = reinterpret_cast<const float*>(params);
    const int32_t remaining = duration - elapsed;
    const int32_t fade = *reinterpret_cast<const int32_t*>(params + 0x6c);
    if (*reinterpret_cast<const int32_t*>(params + 0x68) && remaining >= fade) {
        blend = p[0x70 / 4];
        blur0 = p[0x74 / 4];
        blur1 = p[0x78 / 4];
        return;
    }
    if (!*reinterpret_cast<const int32_t*>(params + 0x68) || remaining <= 0) {
        blend = g(0x0083eb20);
        blur0 = 0.0f;
        blur1 = 0.0f;
        return;
    }
    const float f = static_cast<float>(remaining) / static_cast<float>(fade);
    const float one = g(0x0083eb20);
    if (f == one) {
        blend = p[0x70 / 4];
        blur0 = p[0x74 / 4];
        blur1 = p[0x78 / 4];
        return;
    }
    blend = (p[0x70 / 4] - one) * f + one;
    const float inverse = one / f;
    blur1 = p[0x78 / 4] * inverse;
    blur0 = p[0x74 / 4] * inverse;
}

namespace {

// The title CRT's __CIsqrt (argument and result in ST), stored as float.
float title_ci_sqrt(float x) {
    float result;
    __asm__ volatile("flds %1\n\tcall *%2\n\tfstps %0"
                     : "=m"(result)
                     : "m"(x), "r"(uintptr_t{0x0033acf4})
                     : "eax", "ecx", "edx", "st", "memory");
    return result;
}

}  // namespace

void XB_CDECL CG_TiltViewAxis() {
    const float kx = g(0x00973a5c);
    const float ky = g(0x00973a60);
    if (kx == 0.0f && ky == 0.0f) return;
    const float one = g(0x0083eb20);
    float a0 = one, a1 = kx, a2 = ky;
    float length = title_ci_sqrt((ky * ky + kx * kx) + one);
    if (!(length == g(0x0083eafc))) {
        const float inverse = one / length;
        a0 = inverse;
        a1 = inverse * kx;
        a2 = inverse * ky;
    }
    // Its cross with up (0, 0, 1), written out with the zero products.
    const float zero = 0.0f;
    float b0 = a2 * zero - a1;
    float b1 = a0 - a2 * zero;
    float b2 = a1 * zero - a0 * zero;
    length = title_ci_sqrt((b2 * b2 + b1 * b1) + b0 * b0);
    if (!(length == g(0x0083eafc))) {
        const float inverse = one / length;
        b1 = b1 * inverse;
        b0 = b0 * inverse;
        b2 = b2 * inverse;
    }
    const float c0 = b2 * a1 - b1 * a2;
    const float c1 = b0 * a2 - b2 * a0;
    const float c2 = b1 * a0 - b0 * a1;
    float* axis = &title_global<float>(0x00972bc0);
    float m[9];
    std::memcpy(m, axis, sizeof m);
    const float a[3] = {a0, a1, a2}, b[3] = {b0, b1, b2}, c[3] = {c0, c1, c2};
    const float* rows[3] = {a, b, c};
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k)
            axis[r * 3 + k] = (m[6 + k] * rows[r][2] + m[3 + k] * rows[r][1]) + m[k] * rows[r][0];
}

int32_t XB_REGS1 XB_CDECL CG_CalcCameraShakeSlot(uint8_t* slot) {
    if (!gi(0x009496b4)) return 0;
    auto f = [slot](uint32_t offset) -> float& { return *reinterpret_cast<float*>(slot + offset); };
    const int32_t elapsed = gi(0x0097253c) - *reinterpret_cast<const int32_t*>(slot);
    if (elapsed < 0) return 0;
    const float t = static_cast<float>(elapsed);
    if (t >= f(8)) return 0;
    const Lanes origin = {{f(0x10), f(0x14), f(0x18), f(0x10)}};
    const Lanes d = sub(origin, lanes(&title_global<float>(0x00972bb0)));
    const Lanes squares = mul(d, d);
    platform_keep(squares);
    const float length2 = (squares.v[0] + squares.v[1]) + squares.v[2];
    float distance = length2;
    if (!(length2 == 0.0f || length2 != length2)) {
        const float r = platform_rsqrt_estimate(length2);
        const float lr = length2 * r;
        distance = (g(0x0082e660) - lr * r) * lr * g(0x0082e670);  // (3 - l r r) l r / 2
    }
    const float one = g(0x0083eb20);
    const float falloff = one - distance / f(0xc);
    const float time_part = (one - t / f(8)) * f(4);
    f(0x1c) = falloff >= g(0x0083eafc) ? time_part * falloff : falloff / time_part;
    f(0x20) = time_part;
    return 1;
}

void XB_CDECL CG_CalcCubemapViewValues() {
    const int32_t size = gi(0x00971a90);
    const float a = static_cast<float>(size);
    const float b = a + g(0x0083eba8);  // size + 2
    int32_t* refdef = &title_global<int32_t>(0x00972b90);
    refdef[0] = 0;
    refdef[1] = 0;
    refdef[2] = size + 2;
    refdef[3] = size + 2;
    const float abs_a = abs_bits(a);
    const float abs_b = abs_bits(b);
    float angle = 0.0f;
    if (!(platform_x87_add(abs_b, abs_a) == 0.0f)) {
        // atan2(a, b) through asin of the smaller ratio (the title's series).
        const float one = g(0x0083eb20);
        const float half = g(0x0083eb24);
        const float inverse = platform_x87_rcp_hypot(b, a, one);
        const bool b_larger = abs_b > abs_a;
        const float t = inverse * (b_larger ? abs_a : abs_b);
        float asin;
        if (half > t) {
            const float t2 = t * t;
            const float t3 = t2 * t;
            const float t5 = t3 * t2;
            const float t7 = t5 * t2;
            asin = ((t7 * g(0x0083ee94) + t5 * g(0x0083ee98)) + t3 * g(0x0083ee9c)) + t;
        } else {
            const float s = platform_x87_sqrt_abs_sub_mul(one, t, half);
            const float s2 = s * s;
            const float s3 = s2 * s;
            const float s5 = s3 * s2;
            const float s7 = s5 * s2;
            asin = (((s7 * g(0x0083ee8c) - s5 * g(0x0083ed2c)) - s3 * g(0x0083ee90)) - s * g(0x0083eba8)) +
                   g(0x0083ee88);
        }
        angle = b_larger ? g(0x0083ed0c) - asin : asin;  // pi / 2 - asin
        if (0.0f > a) angle = g(0x0083ed00) - angle;     // pi - angle
        if (0.0f > b) angle = 0.0f - angle;
    }
    const float fov = platform_x87_mul_double(angle, title_global<double>(0x0083efc0));  // 360 / pi
    title_global<float>(0x00972ba0) = fov;
    title_global<float>(0x00972ba4) = fov;
    float* origin = &title_global<float>(0x00972bb0);
    origin[0] = g(0x00972550);
    origin[1] = g(0x00972554);
    origin[2] = g(0x009725fc) + g(0x00972558);
    // Face 1..6: forward, left, up.
    static const float kAxes[6][9] = {
        {0, 0, 1, 0, 1, 0, -1, 0, 0}, {0, 0, -1, 0, 1, 0, 1, 0, 0}, {-1, 0, 0, 0, -1, 0, 0, 0, 1},
        {1, 0, 0, 0, 1, 0, 0, 0, 1},  {0, -1, 0, 1, 0, 0, 0, 0, 1}, {0, 1, 0, -1, 0, 0, 0, 0, 1},
    };
    const uint32_t face = static_cast<uint32_t>(gi(0x00971a8c) - 1);
    if (face > 5) return;
    float* axis = &title_global<float>(0x00972bc0);
    for (int i = 0; i < 9; ++i) axis[i] = kAxes[face][i];
}

PLATFORM_ASM_ENTRY(CG_ProjectViewDirToScreen_entry,
                   "pushl %esi\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(CG_ProjectViewDirToScreen) "addl $8, %esp\n"
                   "ret")

void XB_CDECL CG_ProjectViewDirToScreen(float* out_x, float* out_y) {
    const float to_radians = g(0x0083edd8);
    float sin_yaw, cos_yaw, sin_pitch, cos_pitch;
    FastSinCos(&sin_yaw, 0, &cos_yaw, g(0x00972fa4) * to_radians);
    FastSinCos(&sin_pitch, 0, &cos_pitch, g(0x00972fa0) * to_radians);
    const float dir0 = cos_pitch * cos_yaw;
    const float dir1 = cos_pitch * sin_yaw;
    const float dir2 = 0.0f - sin_pitch;
    const float* axis = &title_global<float>(0x00972bc0);
    const float depth = (axis[2] * dir2 + axis[1] * dir1) + axis[0] * dir0;
    const float fov_x = g(0x00972ba0);
    const float fov_y = g(0x00972ba4);
    if (0.0f >= depth || 0.0f >= fov_x || 0.0f >= fov_y) {
        *out_x = 0.0f;
        *out_y = 0.0f;
        return;
    }
    const float half_degree = g(0x0083ecf0);  // pi / 360
    *out_x = platform_x87_dot3_div_tan_mul(axis[3], axis[4], axis[5], dir0, dir1, dir2, fov_x, half_degree, depth,
                                           g(0x0083f084));  // -320
    *out_y = platform_x87_dot3_div_tan_mul(axis[6], axis[7], axis[8], dir0, dir1, dir2, fov_y, half_degree, depth,
                                           g(0x0083f080));  // -240
}

void XB_REGS3 XB_CDECL CG_Tracer(uint32_t, const float* start, const float* end, float width) {
    const Lanes a = {{start[0], start[1], start[2], start[0]}};
    const Lanes b = {{end[0], end[1], end[2], end[0]}};
    const Lanes d = sub(b, a);
    const Lanes squares = mul(d, d);
    platform_keep(squares);
    const float length2 = (squares.v[0] + squares.v[1]) + squares.v[2];
    float scale = 0.0f;  // a zero line: the zero vector 0x0082e6a0
    if (!(length2 == 0.0f)) {
        const float l = abs_bits(length2);
        const float r = platform_rsqrt_estimate(l);
        scale = ((g(0x0082e660) - r * r * l) * r) * g(0x0082e670);
    }
    const Lanes mid = mul(add(a, b), g(0x0083eb24));
    const Lanes dir = mul(d, scale);
    const float* org = &title_global<float>(0x00972bb0);
    const Lanes view = {{org[0], org[1], org[2], org[0]}};
    const Lanes m = sub(mid, view);
    // cross(m, dir) in the packed order: (m * dir.yzx - m.yzx * dir).yzx.
    const auto yzxw = [](const Lanes& v) -> Lanes { return {{v.v[1], v.v[2], v.v[0], v.v[3]}}; };
    const Lanes side = yzxw(sub(mul(m, yzxw(dir)), mul(yzxw(m), dir)));
    const Lanes side_squares = mul(side, side);
    platform_keep(side_squares);
    const float side2 = (side_squares.v[0] + side_squares.v[1]) + side_squares.v[2];
    if (g(0x0083f050) > side2) return;  // the line points at the view
    float length = side2;
    if (!(side2 == 0.0f || side2 != side2)) {
        const float r = platform_rsqrt_estimate(side2);
        const float lr = side2 * r;
        length = (g(0x0082e660) - lr * r) * lr * g(0x0082e670);
    }
    const float one = g(0x0083eb20);
    const Lanes unit = mul(side, one / length);
    if (gi(0x00947df4)) width = width * g(0x0083100c);  // 4
    const float o0 = unit.v[0] * width, o1 = unit.v[1] * width, o2 = unit.v[2] * width;
    const float back = 0.0f - width;
    const float q0 = back * unit.v[0], q1 = back * unit.v[1], q2 = back * unit.v[2];
    // Four vertices: xyz, four floats (two texture coordinate pairs), RGBA bytes.
    struct Vertex {
        float xyz[3];
        float st[4];
        uint8_t rgba[4];
    };
    Vertex verts[4] = {
        {{o0 + end[0], o1 + end[1], o2 + end[2]}, {one, one, one, one}, {0xff, 0xff, 0xff, 0xff}},
        {{q0 + end[0], q1 + end[1], q2 + end[2]}, {one, 0.0f, one, 0.0f}, {0xff, 0xff, 0xff, 0xff}},
        {{q0 + start[0], q1 + start[1], q2 + start[2]}, {0.0f, 0.0f, 0.0f, 0.0f}, {0xff, 0xff, 0xff, 0xff}},
        {{o0 + start[0], o1 + start[1], o2 + start[2]}, {0.0f, one, 0.0f, one}, {0xff, 0xff, 0xff, 0xff}},
    };
    using AddPoly = void XB_CDECL(uint32_t, uint32_t, Vertex*);
    reinterpret_cast<AddPoly*>(title_global<uintptr_t>(0x00928518))(title_global<uint32_t>(0x00975c4c), 4, verts);
}

PLATFORM_ASM_ENTRY(CG_StartShakeCamera_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CG_StartShakeCamera) "addl $16, %esp\n"
                   "ret")

void XB_CDECL CG_StartShakeCamera(const float* origin, float scale, int32_t duration, float radius) {
    if (0.0f >= scale) return;
    if (!title_global<uint32_t>(0x009496b4)) return;
    // The new shake: {start, scale, duration, radius, origin, strength, time part}.
    alignas(4) uint8_t shake[0x24] = {};
    const int32_t now = title_global<int32_t>(0x0097253c);  // cg_time
    const float length = static_cast<float>(duration);
    std::memcpy(shake, &now, 4);
    std::memcpy(shake + 4, &scale, 4);
    std::memcpy(shake + 8, &length, 4);
    std::memcpy(shake + 0xc, &radius, 4);
    std::memcpy(shake + 0x10, origin, 12);
    CG_CalcCameraShakeSlot(shake);
    // A slot not started yet or finished, else the weakest one if weaker than this.
    uint8_t* slots = &title_global<uint8_t>(0x0097300c);
    int32_t slot = 0;
    for (; slot < 4; ++slot) {
        const uint8_t* s = slots + slot * 0x24;
        const int32_t start = *reinterpret_cast<const int32_t*>(s);
        if (start > now) break;
        const float end = static_cast<float>(start) + *reinterpret_cast<const float*>(s + 8);
        if (static_cast<float>(now) >= end) break;
    }
    if (slot == 4) {
        float strength;
        std::memcpy(&strength, shake + 0x1c, 4);
        for (int32_t i = 0; i < 3; ++i) {
            const float other = *reinterpret_cast<const float*>(slots + i * 0x24 + 0x1c);
            if (strength > other) {
                strength = other;
                slot = i;
            }
        }
        if (strength > *reinterpret_cast<const float*>(slots + 3 * 0x24 + 0x1c))
            slot = 3;
        else if (slot == 4)
            return;
    }
    std::memcpy(slots + slot * 0x24, shake, 0x24);
}

namespace {

// The screen fade (CG_SetFade inline): to `alpha` over `duration` ms from `start`.
void set_fade(int32_t start, int32_t duration, float alpha) {
    const int32_t now = gi(0x0097253c);
    title_global<int32_t>(0x009730c0) = start;
    title_global<float>(0x009730b8) = alpha;
    title_global<int32_t>(0x009730c4) = duration;
    if (start + duration <= now) title_global<float>(0x009730bc) = alpha;
}

}  // namespace

PLATFORM_ASM_ENTRY(CG_GetViewFov_entry,
                   PLATFORM_ASM_CALL(CG_GetViewFov) "subl $4, %esp\n"
                   "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $4, %esp\n"
                   "ret")

float XB_CDECL CG_GetViewFov() {
    const float one = g(0x0083eb20);
    const float zoom = g(0x009725d8);
    float fov = g(0x0094c3e8);  // cg_fov, 1..160
    if (one > fov) {
        fov = one;
    } else if (fov > g(0x0083ef28)) {
        fov = g(0x0083ef28);
    }
    const int32_t now = gi(0x0097253c);
    const uint32_t flags = title_global<uint32_t>(0x00972610);
    const int32_t flash_on_zoom = gi(0x009481e4);
    const uint32_t scoped = flags & 0x106000;
    const auto def_flag = [] {
        const uint8_t* def = title_global<const uint8_t* const*>(0x0095f4d8)[gi(0x009725d0)];  // bg_weaponDefs
        return *reinterpret_cast<const uint32_t*>(def + 0x6dc);
    };
    if (flash_on_zoom && gi(0x00972a38) == 2 && scoped) {
        // A flash (fade from 1.0 over 1 ms, 10 ms ago) when the zoom state (0x00972a34) changes.
        const uint32_t index = zoom == 0.0f ? 0 : 1;
        int32_t& last = title_global<int32_t>(0x0098bc40 + index * 4);
        const int32_t state = gi(0x00972a34);
        bool same = true;
        if (def_flag() && state != 2) {
            if (state == 1) {
                fov = fov - (fov - g(0x0083eda0)) * zoom;
                same = false;
            }
        } else if (state == 1) {
            same = false;
        }
        if (!same && (last > now || last < now - gi(0x00972534))) {
            set_fade(now - 10, 1, one);
            last = now;
            title_global<int32_t>(0x0098bc3c) = now;
        } else {
            if (gi(0x0098bc38) != state) {
                set_fade(now - 10, 1, one);
                title_global<int32_t>(0x0098bc3c) = now;
            }
            last = now;
            title_global<int32_t>(0x0098bc38) = state;
        }
    } else if (!scoped && def_flag()) {
        // Zooming in or out: towards the weapon's zoomed fov (+0x624) over the last part of
        // the zoom (+0x62c in, +0x630 out).
        const uint8_t* weapon = title_global<const uint8_t*>(0x00972acc);
        const auto wf = [weapon](uint32_t offset) { return *reinterpret_cast<const float*>(weapon + offset); };
        if (zoom == one) {
            fov = wf(0x624);
        } else if (!(zoom == 0.0f)) {
            const uint32_t part = gi(0x00972a74) ? 0x62c : 0x630;
            float t = zoom - (one - wf(part));
            if (t > 0.0f) {
                t = t / wf(part);
                if (t > 0.0f) fov = fov - (fov - wf(0x624)) * t;
            }
        }
    }
    if (flash_on_zoom && gi(0x00972a38) == 2 && !scoped) {
        if (gi(0x0098bc38) > 0) {
            set_fade(now - 10, 1, one);
            title_global<int32_t>(0x0098bc3c) = now;
        }
        title_global<int32_t>(0x0098bc38) = 0;
    }
    const int32_t flash = gi(0x0098bc3c);
    if (flash > 0 && flash < now - 0x32) {
        set_fade(now - 1, 0x2bc, 0.0f);
        title_global<int32_t>(0x0098bc3c) = -1;
    }
    const uint8_t* def = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(CG_GetCurrentWeaponDef()));
    if (def) {
        const float zero = g(0x0083eafc);
        if (zoom == one) {
            const float zoomed = *reinterpret_cast<const float*>(def + 0x624);
            if (zoomed > zero) return zoomed;
        }
        const float fixed = *reinterpret_cast<const float*>(def + 0x848);
        if (fixed > zero) return fixed;
    }
    return fov;
}

void XB_CDECL CG_ShakeCamera() {
    if (gi(0x0094c7a4) || !gi(0x009496b4)) return;
    const float time = static_cast<float>(gi(0x0097253c)) * g(0x0083f07c);
    float scale = 0.0f;
    for (uint32_t slot = 0x00973010; slot < 0x009730a0; slot += 0x24) {
        if (CG_CalcCameraShakeSlot(reinterpret_cast<uint8_t*>(slot - 4)) && title_global<float>(slot) > scale)
            scale = title_global<float>(slot + 0x18);
    }
    if (g(0x009730a0) > scale) scale = g(0x009730a0);
    const auto random = [] {
        const int32_t r = title_function<int32_t XB_CDECL()>(0x001efcc0)();  // _rand
        return (static_cast<float>(r) * g(0x0083ecf8) - g(0x0083eb20));
    };
    if (scale <= 0.0f) {
        title_global<float>(0x0097309c) = random() * g(0x0083ed00);
        return;
    }
    if (scale > g(0x0083eb20)) scale = g(0x0083eb20);
    const float amplitude = scale * g(0x0083eca0);
    float offset[3];
    for (int i = 0; i < 3; ++i) offset[i] = random() * amplitude;
    using ext = long double;
    const ext phase = static_cast<ext>(g(0x0097309c));
    const ext pitch = platform_x87_sin_ext(static_cast<ext>(time) * static_cast<ext>(g(0x0083f078)) + phase);
    title_global<float>(0x00972bb0) = g(0x00972bb0) + offset[0];
    title_global<float>(0x00972bb4) = g(0x00972bb4) + offset[1];
    title_global<float>(0x00972bb8) = g(0x00972bb8) + offset[2];
    const ext s = static_cast<ext>(scale);
    title_global<float>(0x00972bf0) =
        static_cast<float>(pitch * s * s * static_cast<ext>(g(0x0083eb00)) + static_cast<ext>(g(0x00972bf0)));
    const ext yaw = platform_x87_sin_ext(static_cast<ext>(time) * static_cast<ext>(g(0x0083f074)) + phase);
    title_global<float>(0x00972bf4) =
        static_cast<float>(yaw * s * s * static_cast<ext>(g(0x0083ebb8)) + static_cast<ext>(g(0x00972bf4)));
    const ext roll = platform_x87_sin_ext(static_cast<ext>(time) * static_cast<ext>(g(0x0083f070)) + phase);
    title_global<float>(0x00972bf8) =
        static_cast<float>(roll * s * s * static_cast<ext>(g(0x0083eb98)) + static_cast<ext>(g(0x00972bf8)));
}

void XB_CDECL CG_CalcWeaponSway() {
    // The view weapon's sway (0x00972c10 pitch/yaw) lagging behind the view's turn since the
    // last frame (AnglesNormalize180 of 0x00972c00 against 0x00972bf0): per-weapon factors
    // (def +0x698.., blended to +0x6b4.. by the zoom when +0x6dc; scaled after a weapon change
    // by +0x6b0 over a smoothstep of the time left of 0x00973a4c/0x00973a50), each value
    // moving towards its target by frametime * diff * speed, snapping when within 0.005.
    const uint8_t* def = title_global<const uint8_t* const*>(0x0095f4d8)[gi(0x009725d0)];
    const auto df = [def](uint32_t offset) { return *reinterpret_cast<const float*>(def + offset); };
    const float zoom = g(0x009725d8);
    const float one = g(0x0083eb20);
    const int32_t left = static_cast<int32_t>(static_cast<uint32_t>(gi(0x00973a50)) -
                                              static_cast<uint32_t>(gi(0x0097253c)) +
                                              static_cast<uint32_t>(gi(0x00973a4c)));
    float k;
    if (left > 0) {
        const int32_t total = *title_global<const int32_t*>(0x00973a48);
        float f = one;
        if (left < total) f = static_cast<float>(left) / static_cast<float>(total);
        k = (g(0x0083ec9c) - f * g(0x0083eba8)) * f * f;
    } else {
        k = 0.0f;
    }
    const float scale = (df(0x6b0) - one) * k + one;
    float limit, speed, pitch_scale, yaw_scale, pitch_lag, yaw_lag;
    if (*reinterpret_cast<const uint32_t*>(def + 0x6dc)) {
        if (zoom > g(0x0083eafc) && *reinterpret_cast<const uint32_t*>(def + 0x638)) return;
        const auto blend = [&](uint32_t offset) { return (df(offset + 0x1c) - df(offset)) * zoom + df(offset); };
        limit = blend(0x698);
        speed = blend(0x69c);
        pitch_scale = blend(0x6a0);
        yaw_scale = blend(0x6a4);
        pitch_lag = blend(0x6a8);
        yaw_lag = blend(0x6ac);
    } else {
        limit = df(0x698);
        speed = df(0x69c);
        pitch_scale = df(0x6a0);
        yaw_scale = df(0x6a4);
        pitch_lag = df(0x6a8);
        yaw_lag = df(0x6ac);
    }
    pitch_scale = pitch_scale * scale;
    yaw_scale = yaw_scale * scale;
    pitch_lag = pitch_lag * scale;
    yaw_lag = yaw_lag * scale;
    alignas(16) float delta[4];
    TitleRegs regs{};
    regs.esi = 0x00972c00;
    regs.edi = 0x00972bf0;
    const uint32_t args[1] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(delta))};
    cg_call_regs(0x0029a060, &regs, args, 1);  // AnglesNormalize180
    const float negative = 0.0f - limit;
    float d0 = delta[0];
    if (negative > d0)
        d0 = negative;
    else if (d0 > limit)
        d0 = limit;
    float d1 = delta[1];
    if (negative > d1)
        d1 = negative;
    else if (d1 > limit)
        d1 = limit;
    const float frametime = static_cast<float>(gi(0x00972534)) * g(0x0083eb4c);
    const float snap = g(0x0083eb88);
    // Moves `current` towards `target` (the original's |x| on the x87 equals clearing the
    // sign bit).
    const auto approach = [&](float target, float current, bool step_first) {
        const float diff = target - current;
        const float step = frametime * diff * speed;
        if (abs_bits(diff) > snap && !(abs_bits(step) > abs_bits(diff))) return step_first ? step + current : current + step;
        return target;
    };
    title_global<float>(0x00972c24) = approach(d1 * pitch_lag, g(0x00972c24), false);
    title_global<float>(0x00972c28) = approach(d0 * yaw_lag, g(0x00972c28), false);
    float pitch = d0 * pitch_scale;
    float yaw = d1 * yaw_scale;
    const float half_turn = g(0x0083eb60), turn = g(0x0083ebcc);
    const float last_pitch = g(0x00972c10);
    while (pitch - last_pitch > half_turn) pitch = pitch - turn;
    const float last_yaw = g(0x00972c14);
    while (yaw - last_yaw > half_turn) yaw = yaw - turn;
    pitch = approach(pitch, last_pitch, true);
    yaw = approach(yaw, last_yaw, false);
    // Through 16-bit angles (ANGLE2SHORT / SHORT2ANGLE), back to -180..180.
    const float to_short = g(0x0083ebd8), to_angle = g(0x0083ebd4);
    float a = static_cast<float>(static_cast<int32_t>(pitch * to_short) & 0xffff) * to_angle;
    if (a > half_turn) a = a - turn;
    title_global<float>(0x00972c10) = a;
    a = static_cast<float>(static_cast<int32_t>(yaw * to_short) & 0xffff) * to_angle;
    if (a > half_turn) a = a - turn;
    title_global<float>(0x00972c14) = a;
    title_global<float>(0x00972c00) = g(0x00972bf0);
    title_global<float>(0x00972c04) = g(0x00972bf4);
    title_global<float>(0x00972c08) = g(0x00972bf8);
}

PLATFORM_ASM_ENTRY(PerpendicularVector_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(PerpendicularVector) "addl $8, %esp\n"
                   "ret")

void XB_CDECL PerpendicularVector(const float* src, float* dst) {
    // Q3's PerpendicularVector: the axis src is least along projected onto src's plane,
    // normalized (the |x| tests on the x87 equal clearing the sign bit).
    int32_t pos = 0;
    float min_elem = g(0x0083eb20);
    const float a0 = abs_bits(src[0]);
    if (g(0x0083eb20) > a0) {
        pos = 0;
        min_elem = a0;
    }
    const float a1 = abs_bits(src[1]);
    if (min_elem > a1) {
        pos = 1;
        min_elem = a1;
    }
    if (min_elem > abs_bits(src[2])) pos = 2;
    float t[3] = {0.0f, 0.0f, 0.0f};
    t[pos] = g(0x0083eb20);
    const float inverse = g(0x0083eb20) / ((src[0] * src[0] + src[1] * src[1]) + src[2] * src[2]);
    const float d = ((src[1] * t[1] + src[2] * t[2]) + src[0] * t[0]) * inverse;
    const float p0 = t[0] - src[0] * inverse * d;
    const float p2 = t[2] - src[2] * inverse * d;
    const float p1 = t[1] - src[1] * inverse * d;
    dst[0] = p0;
    dst[1] = p1;
    dst[2] = p2;
    const float length = title_ci_sqrt((p0 * p0 + p2 * p2) + p1 * p1);
    if (!(length == g(0x0083eafc))) {
        const float scale = g(0x0083eb20) / length;
        dst[0] = p0 * scale;
        dst[1] = p1 * scale;
        dst[2] = p2 * scale;
    }
}

namespace {

// The title's inline asin series (CG_CalcCubemapViewValues, CG_CalcFov): below one half the
// odd powers, else pi/2 less the series of sqrt(|(1 - t) / 2|).
float title_asin_series(float t) {
    const float half = g(0x0083eb24);
    if (half > t) {
        const float t2 = t * t;
        const float t3 = t2 * t;
        const float t5 = t3 * t2;
        const float t7 = t5 * t2;
        return ((t7 * g(0x0083ee94) + t5 * g(0x0083ee98)) + t3 * g(0x0083ee9c)) + t;
    }
    const float s = platform_x87_sqrt_abs_sub_mul(g(0x0083eb20), t, half);
    const float s2 = s * s;
    const float s3 = s2 * s;
    const float s5 = s3 * s2;
    const float s7 = s5 * s2;
    return (((s7 * g(0x0083ee8c) - s5 * g(0x0083ed2c)) - s3 * g(0x0083ee90)) - s * g(0x0083eba8)) + g(0x0083ee88);
}

// atan2(y, x) the title's way, from 1 / hypot already computed.
float title_atan2(float y, float x, float inverse) {
    const float abs_y = abs_bits(y), abs_x = abs_bits(x);
    float angle;
    if (abs_x > abs_y)
        angle = g(0x0083ed0c) - title_asin_series(inverse * abs_y);
    else
        angle = title_asin_series(inverse * abs_x);
    if (0.0f > y) angle = g(0x0083ed00) - angle;
    if (0.0f > x) angle = 0.0f - angle;
    return angle;
}

}  // namespace

uint32_t XB_CDECL CG_CalcFov() {
    // Q3's CG_CalcFov: fov_x from CG_GetViewFov at a 4:3 aspect, fov_y = atan2(3 / ...) (or
    // 2 * atan(9/16) with 0x00948f64), under water (CG_PointContents of the view origin,
    // mask 0x20) both wobbling by sin(cg.time * 0.0025); the zoom sensitivity 0x00972c38 and
    // the fov ratio 0x008da6d8 to cg_fov. Returns whether under water.
    using ext = long double;
    if (gi(0x00972574) >= 4) {
        title_global<int32_t>(0x00972c34) = 0;
        title_global<float>(0x00972c3c) = 0.0f;
    }
    float fov_x = CG_GetViewFov();
    float& sensitivity = title_global<float>(0x00972c38);
    sensitivity = (fov_x - g(0x008fefb4)) / (g(0x0094c3e8) - g(0x008fefb4));
    if (g(0x0083f138) > sensitivity) sensitivity = g(0x0083f138);
    if (title_global<uint32_t>(0x00972610) & 0x200)
        sensitivity = sensitivity * (title_global<uint32_t>(0x00972a50) ? g(0x0083ec18) : g(0x0083ed28));
    const uint8_t* def = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(CG_GetCurrentWeaponDef()));
    if (def) {
        const float v = *reinterpret_cast<const float*>(def + (g(0x009725d8) == g(0x0083eb20) ? 0x628 : 0x61c));
        if (v > g(0x0083eafc)) sensitivity = v * sensitivity;
    }
    const float to_half_radians = g(0x0083ecf0);
    // fov_y = atan2(x, 3) * 360 / pi with x = 4 / tan(fov_x / 2).
    const float x = static_cast<float>(static_cast<ext>(g(0x0083100c)) /
                                       platform_x87_tan_ext(static_cast<ext>(fov_x) * static_cast<ext>(to_half_radians)));
    float angle = 0.0f;
    const float three = static_cast<float>(title_global<double>(0x0083f130));
    if (!(abs_bits(three) + abs_bits(x) == 0.0f)) {
        const float inverse = static_cast<float>(
            static_cast<ext>(g(0x0083eb20)) /
            platform_x87_sqrt_ext(static_cast<ext>(x) * static_cast<ext>(x) + static_cast<ext>(g(0x0083eef8))));
        angle = title_atan2(x, three, inverse);
    }
    float fov_y = angle * g(0x0083efbc);
    float a, b;
    if (title_global<uint32_t>(0x00948f64)) {
        a = g(0x0083eef8);
        b = g(0x0083ebb8);
    } else {
        a = static_cast<float>(static_cast<ext>(g(0x0083ec9c)) /
                               platform_x87_tan_ext(static_cast<ext>(fov_y) * static_cast<ext>(to_half_radians)));
        b = g(0x0083100c);
    }
    angle = 0.0f;
    if (!(abs_bits(b) + abs_bits(a) == 0.0f)) angle = title_atan2(a, b, platform_x87_rcp_hypot(b, a, g(0x0083eb20)));
    fov_x = angle * g(0x0083efbc);
    float* view = &title_global<float>(0x00972bb0);
    uint32_t& rdflags = title_global<uint32_t>(0x00972be8);
    uint32_t under_water;
    if (CG_PointContents(view, 0, 0x20)) {
        const ext phase =
            platform_x87_sin_ext(static_cast<ext>(gi(0x0097253c)) * static_cast<ext>(g(0x0083f128)));
        fov_x = static_cast<float>(static_cast<ext>(fov_x) + phase);
        fov_y = static_cast<float>(static_cast<ext>(fov_y) - phase);
        rdflags |= 0x20;
        under_water = 1;
    } else {
        rdflags &= ~0x20u;
        under_water = 0;
    }
    if (CG_PointContents(view, 0, 0x20))
        rdflags |= 0x20;
    else
        rdflags &= ~0x20u;
    title_global<float>(0x00972ba0) = fov_x;
    title_global<float>(0x00972ba4) = fov_y;
    float ratio = g(0x0083eb20);
    if (fov_x > 0.0f && g(0x0094c3e8) > 0.0f) {
        const float z = fov_x / g(0x0094c3e8);
        if (g(0x0083eb20) > z) ratio = z;
    }
    title_global<float>(0x008da6d8) = ratio;
    return under_water;
}

// vectoyaw (0x002a1830): vector in ECX, yaw in XMM0 (returned here on the x87).
extern "C" PLATFORM_HIDDEN float XB_CDECL cg_vectoyaw(const float* v);
PLATFORM_ASM_ENTRY(cg_vectoyaw,
                   "movl 4(%esp), %ecx\n"
                   "movl $0x002a1830, %eax\n"
                   "call *%eax\n"
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n"
                   "flds (%esp)\n"
                   "addl $4, %esp\n"
                   "ret")

void XB_CDECL CG_DrawDamageDirectionIcons() {
    // Each running damage indicator (0x00972f00, 8 of {time, duration, yaw}) as a rotated
    // quad (shader 0x00975c58, size 0x00949230, from 0x00948a50 out to + 0x00948c00) about
    // the screen centre (or the scope's projected aim point), fading over its second half.
    float color[4] = {g(0x0083eb20), g(0x0083eb20), g(0x0083eb20), 0.0f};
    if (!title_global<uint32_t>(0x00971aa8)) return;
    float fraction;
    float x, y;
    if (CG_GetAdsOverlayFraction(0, 0, &fraction)) {
        if (!title_global<uint32_t>(0x0094b1ec)) return;
        CG_ProjectViewDirToScreen(&x, &y);
        x = x + g(0x0083ec64);
        y = y + g(0x0083eed4);
    } else {
        x = g(0x0083ec64);
        y = g(0x0083eed4);
    }
    const float size = g(0x00949230) * g(0x0083eb24);
    const float negative = 0.0f - size;
    const float offset = g(0x00948a50);
    const float far_edge = g(0x00948c00) + offset;
    const float quad[8] = {negative, offset, size, offset, size, far_edge, negative, far_edge};
    for (uint32_t slot = 0x00972f00; slot < 0x00972f60; slot += 12) {
        const int32_t elapsed = gi(0x0097253c) - title_global<int32_t>(slot);
        const int32_t duration = title_global<int32_t>(slot + 4);
        if (elapsed <= 0 || elapsed >= duration) continue;
        const float angle = cg_vectoyaw(&title_global<float>(0x00972bc0)) - title_global<float>(slot + 8);
        const float two = g(0x0083eba8);
        const float alpha = two - static_cast<float>(elapsed) * two / static_cast<float>(duration);
        color[3] = g(0x0083eb20);
        if (!(alpha > g(0x0083eb20))) color[3] = alpha;
        title_function<void XB_CDECL(const float*)>(title_global<uint32_t>(0x0092852c))(color);
        CG_DrawRotatedQuadPic(x, y, &title_global<float>(0x00839a6c), title_global<uint32_t>(0x00975c58), quad, angle);
    }
}

namespace {

// The channel of a sound handle (index in the low 12 bits of 0x00962860 {channel,
// generation}, 0x200 of them), 0 when stale.
uint32_t sound_channel(uint32_t handle) {
    const uint32_t index = handle & 0xfff;
    if (index >= 0x200 || (handle >> 12) != title_global<uint32_t>(0x00962864 + index * 8)) return 0;
    return title_global<uint32_t>(0x00962860 + index * 8);
}

void stop_channel(uint32_t channel) {
    TitleRegs regs{};
    regs.esi = channel;
    regs.edi = channel;
    cg_call_regs(0x002c3400, &regs, nullptr, 0);  // S_StopChannel
}

}  // namespace

void XB_CDECL CG_StopShellShockSound() {
    // The shell shock's sound mix restored (0x000c53b0; a reverb preset handed to the empty
    // 0x000c72f0), its flag 0x00973a54 cleared and both looping channels (handles
    // 0x00973a68/0x00973a6c) stopped. With the second handle 0 the original stops the first
    // channel a second time (its register still holds it).
    title_function<void XB_CDECL()>(0x000c53b0)();
    uint32_t preset[12] = {0xfffffc18, 0xffffff9c, 0, 0, 0, 0xfffff5d6, 0, 200, 0, 0, 0, 0};
    reinterpret_cast<float*>(preset)[2] = 0.0f;
    reinterpret_cast<float*>(preset)[3] = g(0x0083ec58);
    reinterpret_cast<float*>(preset)[4] = g(0x0083ec54);
    reinterpret_cast<float*>(preset)[6] = g(0x0083ec38);
    reinterpret_cast<float*>(preset)[8] = g(0x0083ec34);
    reinterpret_cast<float*>(preset)[9] = g(0x0083eb90);
    reinterpret_cast<float*>(preset)[10] = g(0x0083eb90);
    reinterpret_cast<float*>(preset)[11] = g(0x0083ec28);
    title_function<void XB_CDECL(const uint32_t*)>(0x000c72f0)(preset);
    if (title_global<uint32_t>(0x00973a54)) title_global<uint32_t>(0x00973a54) = 0;
    uint32_t channel = 0;
    const uint32_t first = title_global<uint32_t>(0x00973a68);
    if (first) {
        channel = sound_channel(first);
        if (channel) stop_channel(channel);
    }
    title_global<uint32_t>(0x00973a68) = 0;
    const uint32_t second = title_global<uint32_t>(0x00973a6c);
    if (second) channel = sound_channel(second);
    if (channel) stop_channel(channel);
    title_global<uint32_t>(0x00973a6c) = 0;
}

PLATFORM_ASM_ENTRY(CG_UpdateShellShock_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CG_UpdateShellShock) "addl $12, %esp\n"
                   "ret")

void XB_CDECL CG_UpdateShellShock(int32_t start, int32_t duration, const uint8_t* params) {
    // While a shell shock (start, duration ms) runs: its sound (0x0027c5c0), the screen
    // parameters and the view kick; 0x00961770 = still running. Otherwise everything off
    // (sound stopped, blend 1, blurs 0, scene callback 2 cleared, kick 0).
    const int32_t elapsed = title_global<int32_t>(0x0097253c) - start;
    if (start && elapsed >= 0 && elapsed <= duration) {
        // CG_UpdateShellShockSound reads a never-written word that holds a register pushed by an
        // earlier callee: hand it the original's EBX/ESI/EDI (params, duration, elapsed).
        TitleRegs regs{};
        regs.ebx = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(params));
        regs.esi = static_cast<uint32_t>(duration);
        regs.edi = static_cast<uint32_t>(elapsed);
        const uint32_t args[2] = {static_cast<uint32_t>(elapsed), static_cast<uint32_t>(duration)};
        cg_call_regs(0x0027c5c0, &regs, args, 2);  // CG_UpdateShellShockSound
        CG_ShellShockScreenParams(0, elapsed, params, duration);
        CG_ShellShockViewKick(duration, params, elapsed);
        title_global<uint32_t>(0x00961770) = elapsed < duration ? 1 : 0;
        return;
    }
    CG_StopShellShockSound();
    title_global<float>(0x00973a58) = g(0x0083eb20);
    title_global<float>(0x00961778) = 0.0f;
    title_global<float>(0x0096177c) = 0.0f;
    nglSetSceneCallback(2, nullptr, nullptr);
    title_global<float>(0x00973a5c) = 0.0f;
    title_global<float>(0x00973a60) = 0.0f;
    title_global<uint32_t>(0x00961770) = 0;
}

void XB_CDECL CG_DrawSkyBoxPortal() {
    // Q3's CG_DrawSkyBoxPortal: with a skybox configstring (0x009606e0: "x y z fov") and the
    // portal cvar 0x009493e4 the view origin from it, fov_x from CG_GetViewFov and fov_y for
    // the refdef's aspect (flags 0x18), else flag 8 without 0x10; rendered (import
    // 0x00928524) at cg.time (0x00972be4), then the refdef restored.
    const uint8_t* string = title_global<const uint8_t*>(0x009606e0);
    const char* cursor = string ? reinterpret_cast<const char*>(string + 6) : nullptr;
    if (!cursor || !*cursor) return;
    uint32_t saved[0x18];
    uint32_t* refdef = &title_global<uint32_t>(0x00972b90);
    for (int k = 0; k < 0x18; ++k) saved[k] = refdef[k];
    uint32_t& flags = title_global<uint32_t>(0x00972be8);
    if (title_global<uint32_t>(0x009493e4)) {
        const auto token = [&cursor]() {
            TitleRegs regs{};
            regs.ecx = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&cursor));
            const char* t = reinterpret_cast<const char*>(static_cast<uintptr_t>(cg_call_regs(0x002b9080, &regs, nullptr, 0)));
            if (!t || !*t) CG_Error(&title_global<char>(0x008389cc));
            return t;
        };
        const auto atof = title_function<float XB_CDECL(const char*)>(0x001ef056);
        title_global<float>(0x00972bb0) = atof(token());
        title_global<float>(0x00972bb4) = atof(token());
        title_global<float>(0x00972bb8) = atof(token());
        title_function<int32_t XB_CDECL(const char*)>(0x001eeb81)(token());  // _atol (unused)
        const float fov_x = CG_GetViewFov();
        using ext = long double;
        const float x = static_cast<float>(static_cast<ext>(gi(0x00972b98)) /
                                           platform_x87_tan_ext(static_cast<ext>(fov_x) * static_cast<ext>(g(0x0083ecf0))));
        const float y = static_cast<float>(gi(0x00972b9c));
        float angle = 0.0f;
        if (!(abs_bits(y) + abs_bits(x) == 0.0f)) angle = title_atan2(x, y, platform_x87_rcp_hypot(y, x, g(0x0083eb20)));
        title_global<float>(0x00972ba0) = fov_x;
        title_global<float>(0x00972ba4) = angle * g(0x0083efbc);
        flags |= 0x18;
    } else {
        flags = (flags & ~0x10u) | 8;
    }
    title_global<int32_t>(0x00972be4) = gi(0x0097253c);
    title_function<void XB_CDECL(void*)>(title_global<uint32_t>(0x00928524))(refdef);
    for (int k = 0; k < 0x18; ++k) refdef[k] = saved[k];
}

// Runs the trace calls at the original's ESP with the trace and view vectors at the
// original's frame addresses (as the trace functions themselves): the entry moves the C
// frame below a gap and hands over its entry ESP.
PLATFORM_ASM_ENTRY(CG_OffsetThirdPersonView_entry,
                   "movl %esp, %edx\n"
                   "subl $0x10000, %esp\n"
                   "pushl %edx\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(CG_OffsetThirdPersonView) "addl $4, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL CG_OffsetThirdPersonView(uint32_t entry_esp) {
    // Q3's CG_OffsetThirdPersonView: the camera pulled back cg_thirdPersonRange (0x0094adf0)
    // at cg_thirdPersonAngle (0x0094c8c0) from 8 units above the eye, stopped by the world
    // (CG_TracePoint, 4-unit box 0x0098ee80/70, mask 0x11; raised by the blocked fraction *
    // 32 and traced again), looking at the point 512 units ahead (pitch capped at 45; the
    // view pitch halved first). Intermission (0x00972574 >= 4) uses the yaw 0x00972654.
    const uint32_t frame = (((entry_esp - 4) & ~15u) - 0xb0);
    float* view = reinterpret_cast<float*>(static_cast<uintptr_t>(frame + 0x20));
    uint8_t* trace = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(frame + 0x60));
    uint32_t& once = title_global<uint32_t>(0x0098ee90);
    if (!(once & 1)) {
        once |= 1;
        for (int k = 0; k < 4; ++k) title_global<float>(0x0098ee80 + 4 * k) = g(0x0083f184);
    }
    if (!(once & 2)) {
        once |= 2;
        for (int k = 0; k < 4; ++k) title_global<float>(0x0098ee70 + 4 * k) = g(0x0083100c);
    }
    float* org = &title_global<float>(0x00972bb0);
    float* angles = &title_global<float>(0x00972bf0);
    org[2] = org[2] + g(0x009725fc);
    float focus_pitch = angles[0];
    if (gi(0x00972574) >= 4) angles[1] = static_cast<float>(gi(0x00972654));
    if (focus_pitch > g(0x0083ef1c)) focus_pitch = g(0x0083ef1c);
    const float to_radians = g(0x0083edd8);
    float sy, cy, sp, cp;
    FastSinCos(&sy, 0, &cy, angles[1] * to_radians);
    FastSinCos(&sp, 0, &cp, focus_pitch * to_radians);
    using ext = long double;
    angles[0] = static_cast<float>(static_cast<ext>(angles[0]) * static_cast<ext>(title_global<double>(0x0083ecb0)));
    const float reach = g(0x0083ebf4);
    alignas(16) float forward[4];
    forward[0] = cp * cy;
    forward[1] = cp * sy;
    forward[2] = 0.0f - sp;
    const float focus[3] = {forward[0] * reach + org[0], forward[1] * reach + org[1], forward[2] * reach + org[2]};
    view[0] = org[0];
    view[1] = org[1];
    view[2] = org[2] + g(0x0083ed78);
    uint32_t args[2] = {0x00972bf0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(forward))};
    alignas(16) float right[4], up[4];
    AnglesToAxis(args, right, up);
    float s, c;
    FastSinCos(&s, 0, &c, g(0x0094c8c0) / g(0x0083eb60) * g(0x0083ed00));
    const float range = g(0x0094adf0);
    const float forward_scale = 0.0f - range * c;
    const float side_scale = 0.0f - range * s;
    view[0] = side_scale * right[0] + (forward_scale * forward[0] + view[0]);
    view[1] = side_scale * right[1] + (forward_scale * forward[1] + view[1]);
    view[2] = side_scale * right[2] + (forward_scale * forward[2] + view[2]);
    TitleRegs regs{};
    regs.eax = 0x11;
    regs.ecx = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(view));
    const uint32_t skip = title_global<uint32_t>(0x009725cc);
    const uint32_t trace_args[5] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(trace)), 0x00972bb0, 0x0098ee80,
                                    0x0098ee70, skip};
    title_call_at_regs(frame - 4, 0x00286d00, &regs, 5, trace_args);  // CG_TracePoint
    if (!(*reinterpret_cast<const float*>(trace + 0x20) == 1.0f)) {
        const float* end = reinterpret_cast<const float*>(trace);
        view[0] = end[0];
        view[1] = end[1];
        view[2] = (g(0x0083eb20) - *reinterpret_cast<const float*>(trace + 0x20)) * g(0x0083eeac) + end[2];
        const uint32_t again[5] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(trace)), 0x00972bb0, 0x0098ee80,
                                   0x0098ee70, title_global<uint32_t>(0x009725cc)};
        title_call_at_regs(frame, 0x00286d00, &regs, 5, again);
        view[0] = end[0];
        view[1] = end[1];
        view[2] = end[2];
    }
    const float dx = static_cast<float>(static_cast<ext>(focus[0]) - static_cast<ext>(view[0]));
    org[0] = view[0];
    org[1] = view[1];
    const float dz = focus[2] - view[2];
    const float dy = static_cast<float>(static_cast<ext>(focus[1]) - static_cast<ext>(view[1]));
    org[2] = view[2];
    float distance = title_ci_sqrt(static_cast<float>(static_cast<ext>(dy) * dy + static_cast<ext>(dx) * dx));
    if (g(0x0083eb20) > distance) distance = g(0x0083eb20);
    float angle = 0.0f;
    if (!(abs_bits(dz) + abs_bits(distance) == 0.0f)) {
        const float length = title_ci_sqrt(static_cast<float>(static_cast<ext>(distance) * distance + static_cast<ext>(dz) * dz));
        const float inverse = static_cast<float>(static_cast<ext>(g(0x0083eb20)) / static_cast<ext>(length));
        angle = title_atan2(distance, dz, inverse);
    }
    angles[0] = angle * g(0x0083f0e0);
    angles[1] = angles[1] - g(0x0094c8c0);
}

}  // namespace game
