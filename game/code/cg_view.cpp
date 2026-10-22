#include "cg_view.h"

#include <cstring>

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

}  // namespace game
