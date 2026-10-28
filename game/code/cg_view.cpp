#include "cg_view.h"

#include <cstring>

#include "cg_ents.h"
#include "cgame.h"
#include "dobj.h"
#include "g_utils.h"
#include "lanes.h"
#include "nal_math.h"
#include "ngl.h"
#include "qcommon.h"
#include "title_data.h"

namespace game {

namespace {

// A reference: a float returned by value passes the x87 stack (i386 ABI), which quiets a
// signaling NaN where the title's MOVSS keeps it (and its later ADDSS raises MXCSR IE).
const float& g(uint32_t address) { return title_global<float>(address); }
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

PLATFORM_ASM_ENTRY(CG_UpdateWeaponLag_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(CG_UpdateWeaponLag) "addl $4, %esp\n"
                   "ret")

namespace {

// One axis of CG_UpdateWeaponLag over `dt` seconds: the offset moves by its speed and stops at
// +-limit (the speed cut there when it points outwards), the speed is pulled back by
// spring * dt, damped by speed * dt * damping and slowed by friction * dt (not past 0), then
// capped at +-max_speed.
void lag_axis(float& offset, float& speed, float limit, float dt, float spring, float damping, float friction,
              float max_speed) {
    float pos = speed * dt + offset;
    offset = pos;
    float v = speed;
    if (pos > limit) {
        offset = limit;
        if (v > 0.0f) v = 0.0f;
        pos = offset;
    } else {
        const float low = 0.0f - limit;
        if (low > pos) {
            offset = low;
            if (0.0f > v) v = 0.0f;
            pos = offset;
        }
    }
    if (pos > 0.0f)
        v = v - dt * spring;
    else if (0.0f > pos)
        v = dt * spring + v;
    v = v - v * dt * damping;
    const float drag = dt * friction;
    if (v > 0.0f) {
        v = v - drag;
        speed = v;
        if (0.0f > v) speed = v = 0.0f;
    } else {
        v = drag + v;
        speed = v;
        if (v > 0.0f) speed = v = 0.0f;
    }
    if (v > max_speed) {
        speed = max_speed;
    } else {
        const float low = 0.0f - max_speed;
        if (low > v) speed = low;
    }
}

}  // namespace

void XB_CDECL CG_UpdateWeaponLag(float* angles) {
    // The view weapon's angle lag (weapons with def +0x6dc): offsets 0x00972fb4/b8 (+0x00972fbc,
    // never moved) with speeds 0x00972fc0/c4 stepped in slices of at most 5 ms of the frame time,
    // with the spring, damping, max speed and friction of the current weapon (+0x78c.. hip,
    // +0x7d4.. while aiming, blended by 0x009725d8) and the limits +0x690/+0x694; an axis that is
    // within 0.25 and moves under 1 snaps to rest, and when both rest the slices stop. Then
    // added to the angles (ESI).
    const uint8_t* const def = title_global<uint8_t* const*>(0x0095f4d8)[title_global<int32_t>(0x009725d0)];
    const float k = g(0x009725d8);
    if (!*reinterpret_cast<const uint32_t*>(def + 0x6dc)) return;
    const float friction_d = wf(0x798) - wf(0x7e0);
    const float spring = (wf(0x78c) - wf(0x7d4)) * k + wf(0x7d4);
    const float max_speed = (wf(0x790) - wf(0x7d8)) * k + wf(0x7d8);
    const float damping = (wf(0x794) - wf(0x7dc)) * k + wf(0x7dc);
    const float friction = friction_d * k + wf(0x7e0);
    float& offset0 = title_global<float>(0x00972fb4);
    float& offset1 = title_global<float>(0x00972fb8);
    float& speed0 = title_global<float>(0x00972fc0);
    float& speed1 = title_global<float>(0x00972fc4);
    int32_t ms = gi(0x00972534);
    if (ms > 0) {
        do {
            int32_t step;
            if (ms > 5) {
                step = 5;
                ms -= 5;
            } else {
                step = ms;
                ms = 0;
            }
            const float dt = static_cast<float>(step) * g(0x0083eb4c);  // 0.001
            const float rest = g(0x0083ec24);                           // 0.25
            const float one = g(0x0083eb20);
            bool rest0 = false;
            if (platform_x87_greater(rest, abs_bits(offset0)) && platform_x87_greater(one, abs_bits(speed0))) {
                offset0 = 0.0f;
                speed0 = 0.0f;
                rest0 = true;
            } else {
                lag_axis(offset0, speed0, wf(0x690), dt, spring, damping, friction, max_speed);
            }
            if (platform_x87_greater(rest, abs_bits(offset1)) && platform_x87_greater(one, abs_bits(speed1))) {
                offset1 = 0.0f;
                speed1 = 0.0f;
                if (rest0) break;
            } else {
                lag_axis(offset1, speed1, wf(0x694), dt, spring, damping, friction, max_speed);
            }
        } while (ms > 0);
    }
    angles[0] = angles[0] + offset0;
    angles[1] = offset1 + angles[1];
    angles[2] = title_global<float>(0x00972fbc) + angles[2];
}

PLATFORM_ASM_ENTRY(CG_RotateAnglesByView_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(CG_RotateAnglesByView) "addl $4, %esp\n"
                   "ret")

void XB_CDECL CG_RotateAnglesByView(float* angles) {
    // The angles (EBX) as an axis {forward, left, up} times the view angles' axis (refdef
    // 0x00972bf0), back to angles (AxisToAngles 0x002a27f0): angles relative to the view
    // turned into world angles.
    float forward1[3], right1[3], up1[3];
    uint32_t args1[2] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(angles)),
                         static_cast<uint32_t>(reinterpret_cast<uintptr_t>(forward1))};
    AnglesToAxis(args1, right1, up1);
    const float left1[3] = {0.0f - right1[0], 0.0f - right1[1], 0.0f - right1[2]};
    float forward2[3], right2[3], up2[3];
    uint32_t args2[2] = {0x00972bf0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(forward2))};
    AnglesToAxis(args2, right2, up2);
    const float l0 = 0.0f - right2[0];
    const float l2 = 0.0f - right2[2];
    const float l1 = 0.0f - right2[1];
    const float* const w1 = forward1;
    const float* const u1 = up1;
    const float* const w2 = forward2;
    const float* const u2 = up2;
    float axis[9];
    axis[0] = ((u2[0] * w1[2]) + (w1[1] * l0)) + (w1[0] * w2[0]);
    axis[1] = ((u2[1] * w1[2]) + (w1[1] * l1)) + (w2[1] * w1[0]);
    axis[2] = ((u2[2] * w1[2]) + (w1[1] * l2)) + (w2[2] * w1[0]);
    axis[3] = ((u2[0] * left1[2]) + (l0 * left1[1])) + (w2[0] * left1[0]);
    axis[4] = ((u2[1] * left1[2]) + (w2[1] * left1[0])) + (l1 * left1[1]);
    axis[5] = ((u2[2] * left1[2]) + (w2[2] * left1[0])) + (l2 * left1[1]);
    axis[6] = ((u1[2] * u2[0]) + (u1[1] * l0)) + (u1[0] * w2[0]);
    axis[7] = ((u1[2] * u2[1]) + (u1[0] * w2[1])) + (u1[1] * l1);
    axis[8] = ((u1[2] * u2[2]) + (u1[0] * w2[2])) + (u1[1] * l2);
    TitleRegs regs{};
    regs.eax = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(angles));
    regs.ecx = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(axis));
    cg_call_regs(0x002a27f0, &regs, nullptr, 0);  // AxisToAngles
}

void XB_CDECL CG_ApplyVehicleTurnToView() {
    // The vehicle's turn since the last frame carried into the view: A = the axis {forward,
    // left, up} of the vehicle angles 0x009729bc, M (0x008fef7c, 3x3) the previous frame's A
    // transposed (0x008fef78 = M not valid). The view axis C (of 0x009725ec) times M A gives
    // the new view angles 0x009725ec/f0, and cl_viewangles (0x00961798..a0) = those minus the
    // delta angles (0x009725a4.. as 16-bit, 0x009725f4 for roll); 0x009725f4 = minus the roll of
    // C A^T. Zero vehicle angles, another view state (0x00972574 != 1) or flags 0x106000 only
    // store M = A^T.
    float a[9], right[3];
    uint32_t args[2] = {0x009729bc, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&a[0]))};
    AnglesToAxis(args, right, &a[6]);
    float* const m = &title_global<float>(0x008fef7c);
    uint32_t& invalid = title_global<uint32_t>(0x008fef78);
    const float* const vehicle = &title_global<float>(0x009729bc);
    a[3] = 0.0f - right[0];
    a[4] = 0.0f - right[1];
    a[5] = 0.0f - right[2];
    auto store_transposed = [m, &a]() {
        m[0] = a[0];
        m[2] = a[6];
        m[3] = a[1];
        m[5] = a[7];
        m[6] = a[2];
        m[1] = a[3];
        m[4] = a[4];
        m[7] = a[5];
        m[8] = a[8];
    };
    if (vehicle[0] == 0.0f && vehicle[1] == 0.0f && vehicle[2] == 0.0f) {
        invalid = 1;
        store_transposed();
        return;
    }
    if (gi(0x00972574) != 1 || (title_global<uint32_t>(0x00972610) & 0x106000)) {
        invalid = 1;
        store_transposed();
        return;
    }
    if (invalid) {
        // MatrixTranspose (0x00295020, EAX = dst, ECX = src) of A into M.
        uint32_t* const dst = reinterpret_cast<uint32_t*>(m);
        const uint32_t* const src = reinterpret_cast<const uint32_t*>(a);
        static const uint8_t kFrom[9] = {0, 3, 6, 1, 4, 7, 2, 5, 8};
        for (int i = 0; i < 9; ++i) dst[i] = src[kFrom[i]];
        invalid = 0;
    }
    float b[9];
    b[0] = ((m[2] * a[6]) + (m[1] * a[3])) + (m[0] * a[0]);
    b[1] = ((a[7] * m[2]) + (m[0] * a[1])) + (m[1] * a[4]);
    b[2] = ((a[8] * m[2]) + (a[2] * m[0])) + (m[1] * a[5]);
    b[3] = ((m[5] * a[6]) + (m[4] * a[3])) + (m[3] * a[0]);
    b[4] = ((a[7] * m[5]) + (m[3] * a[1])) + (m[4] * a[4]);
    b[5] = ((a[8] * m[5]) + (a[2] * m[3])) + (m[4] * a[5]);
    b[6] = ((m[8] * a[6]) + (m[7] * a[3])) + (m[6] * a[0]);
    b[7] = ((a[7] * m[8]) + (m[6] * a[1])) + (m[7] * a[4]);
    b[8] = ((a[8] * m[8]) + (a[2] * m[6])) + (m[7] * a[5]);
    store_transposed();
    float c[9], angles[3];
    uint32_t args2[2] = {0x009725ec, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&c[0]))};
    AnglesToAxis(args2, angles, &c[6]);
    c[3] = 0.0f - angles[0];
    c[4] = 0.0f - angles[1];
    c[5] = 0.0f - angles[2];
    float d[9];
    d[0] = ((c[2] * b[6]) + (c[1] * b[3])) + (c[0] * b[0]);
    d[1] = ((c[2] * b[7]) + (c[1] * b[4])) + (b[1] * c[0]);
    d[2] = ((c[2] * b[8]) + (c[1] * b[5])) + (b[2] * c[0]);
    d[3] = ((c[5] * b[6]) + (c[4] * b[3])) + (c[3] * b[0]);
    d[4] = ((c[5] * b[7]) + (c[4] * b[4])) + (c[3] * b[1]);
    d[5] = ((c[5] * b[8]) + (c[4] * b[5])) + (c[3] * b[2]);
    d[6] = ((c[8] * b[6]) + (c[7] * b[3])) + (c[6] * b[0]);
    d[7] = ((c[8] * b[7]) + (c[7] * b[4])) + (c[6] * b[1]);
    d[8] = ((c[8] * b[8]) + (c[7] * b[5])) + (c[6] * b[2]);
    TitleRegs regs{};
    regs.eax = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(angles));
    regs.ecx = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(d));
    cg_call_regs(0x002a27f0, &regs, nullptr, 0);  // AxisToAngles
    const float k = g(0x0083ebd4);
    const float pitch = angles[0];
    const float yaw = angles[1];
    float delta0 = static_cast<float>(gi(0x009725a4));
    float delta1 = static_cast<float>(gi(0x009725a8));
    float delta2 = static_cast<float>(gi(0x009725ac));
    delta1 = delta1 * k;
    delta0 = delta0 * k;
    title_global<float>(0x009725ec) = pitch;
    const float view_pitch = pitch - delta0;
    title_global<float>(0x009725f0) = yaw;
    const float view_yaw = yaw - delta1;
    delta2 = delta2 * k;
    const float view_roll = g(0x009725f4) - delta2;
    angles[2] = view_roll;
    title_global<float>(0x009617a0) = view_roll;
    angles[0] = view_pitch;
    angles[1] = view_yaw;
    title_global<float>(0x00961798) = view_pitch;
    title_global<float>(0x0096179c) = view_yaw;
    float e[9];
    e[0] = ((c[2] * a[2]) + (c[1] * a[1])) + (c[0] * a[0]);
    e[1] = ((c[2] * a[5]) + (c[1] * a[4])) + (c[0] * a[3]);
    e[2] = ((c[2] * a[8]) + (c[1] * a[7])) + (a[6] * c[0]);
    e[3] = ((c[5] * a[2]) + (c[4] * a[1])) + (c[3] * a[0]);
    e[4] = ((c[5] * a[5]) + (c[4] * a[4])) + (c[3] * a[3]);
    e[5] = ((c[5] * a[8]) + (c[4] * a[7])) + (c[3] * a[6]);
    e[6] = ((c[8] * a[2]) + (c[7] * a[1])) + (c[6] * a[0]);
    e[7] = ((c[8] * a[5]) + (c[7] * a[4])) + (c[6] * a[3]);
    e[8] = ((c[8] * a[8]) + (c[7] * a[7])) + (c[6] * a[6]);
    regs.ecx = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(e));
    cg_call_regs(0x002a27f0, &regs, nullptr, 0);  // AxisToAngles
    title_global<float>(0x009725f4) = 0.0f - angles[2];
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

namespace {

// The title CRT's __CIsqrt of an x87 value (argument and result in ST), stored as float.
float title_ci_sqrt_ext(long double x) {
    float result;
    __asm__ volatile("fldt %1\n\tcall *%2\n\tfstps %0"
                     : "=m"(result)
                     : "m"(x), "r"(uintptr_t{0x0033acf4})
                     : "eax", "ecx", "edx", "st", "memory");
    return result;
}

}  // namespace

// The original keeps the length in its cdecl argument slot: the entry hands the C version the
// slot's address, and it writes the slot's final value.
PLATFORM_ASM_ENTRY(CG_OffsetViewForward_entry,
                   "leal 4(%esp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CG_OffsetViewForward) "addl $4, %esp\n"
                   "ret")

void XB_CDECL CG_OffsetViewForward(float* distance_slot) {
    // The refdef origin (0x00972bb0) moved `distance` along the view axis' forward (0x00972bc0);
    // the view pitch (0x00972bf0) re-derived from the view angles' forward times 0x008fef74 as
    // seen from the moved origin (each component (k f + o) - o): -atan2(z, max(1, hypot(x, y)))
    // in degrees, 0 when both are 0.
    using ext = long double;
    const float distance = *distance_slot;
    float forward[3], right[3], up[3];
    uint32_t args[2] = {0x00972bf0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(forward))};
    AnglesToAxis(args, right, up);
    const float k = g(0x008fef74);
    float* const org = &title_global<float>(0x00972bb0);
    const float o1 = g(0x00972bc4) * distance + org[1];
    const float o0 = g(0x00972bc0) * distance + org[0];
    const float o2 = g(0x00972bc8) * distance + org[2];
    const float d1 = (k * forward[1] + o1) - o1;
    const float d0 = (k * forward[0] + o0) - o0;
    const float d2 = (k * forward[2] + o2) - o2;
    org[0] = o0;
    org[1] = o1;
    org[2] = o2;
    float h = title_ci_sqrt_ext(static_cast<ext>(d1) * static_cast<ext>(d1) + static_cast<ext>(d0) * static_cast<ext>(d0));
    *distance_slot = h;
    const float one = g(0x0083eb20);
    if (platform_x87_greater(one, h)) {
        h = one;
        *distance_slot = h;
    }
    const float abs_h = abs_bits(h);
    const float abs_z = abs_bits(d2);
    if (static_cast<ext>(abs_h) + static_cast<ext>(abs_z) == 0.0L) {
        title_global<float>(0x00972bf0) = 0.0f * g(0x0083f0e0);
        return;
    }
    const float length = title_ci_sqrt_ext(static_cast<ext>(h) * static_cast<ext>(h) + static_cast<ext>(d2) * static_cast<ext>(d2));
    const float inverse = static_cast<float>(static_cast<ext>(one) / static_cast<ext>(length));
    title_global<float>(0x00972bf0) = title_atan2(h, d2, inverse) * g(0x0083f0e0);
}

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

// The calls run at the original's ESP with its registers, the stack blocks they get at the
// original's frame addresses (the sound starters' subtree reads words pushed by nested
// callees): the entry moves the C frame below a gap and hands over the entry ESP and
// EBX (params), ESI and EDI.
PLATFORM_ASM_ENTRY(CG_UpdateShellShockSound_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %edx\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(CG_UpdateShellShockSound) "addl $16, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL CG_UpdateShellShockSound(uint32_t entry_esp, const uint8_t* params, uint32_t esi, uint32_t edi) {
    // The shell shock's sound (cdecl elapsed, duration): the mix (10 volumes from params
    // +0x34, faded in over +0x1c ms before the end or over +0x18 ms from the start) and the
    // "shock_loop" alias (handle 0x00973a68) at 1 - the fade-out over +0x60 ms, "shock_end"
    // (0x00973a6c) started once and played from +0x64 ms before the end (0x00973a54 = the
    // time it is due). Without +0x14 the sound stops.
    const int32_t elapsed = *reinterpret_cast<const int32_t*>(static_cast<uintptr_t>(entry_esp + 4));
    const int32_t duration = *reinterpret_cast<const int32_t*>(static_cast<uintptr_t>(entry_esp + 8));
    const uint32_t frame = ((entry_esp - 4) & ~15u) - 0x60;
    auto at = [frame](uint32_t offset) { return frame + offset; };
    float& scale_slot = *reinterpret_cast<float*>(static_cast<uintptr_t>(at(8)));
    float* volumes = reinterpret_cast<float*>(static_cast<uintptr_t>(at(0x38)));
    const auto pi = [params](uint32_t offset) { return *reinterpret_cast<const int32_t*>(params + offset); };
    TitleRegs regs{};
    regs.ebx = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(params));
    regs.esi = esi;
    regs.edi = edi;
    if (!pi(0x14)) {
        title_call_at_regs(frame, 0x0027b860, &regs, 0, nullptr);  // CG_StopShellShockSound
        return;
    }
    const float one = g(0x0083eb20);
    const int32_t fade_in = pi(0x1c);
    const int32_t left = pi(0x5c) - elapsed + fade_in + duration;
    float f;
    bool clamp = true;
    if (left < fade_in) {
        f = static_cast<float>(left) / static_cast<float>(fade_in);
    } else if (elapsed < pi(0x18)) {
        f = static_cast<float>(elapsed) / static_cast<float>(pi(0x18));
    } else {
        f = one;
        clamp = false;
    }
    if (clamp && 0.0f > f) f = 0.0f;
    const float* mix = reinterpret_cast<const float*>(params + 0x34);
    for (int i = 0; i < 10; ++i) volumes[i] = (mix[i] - one) * f + one;
    const int32_t fade_out = pi(0x60);
    const int32_t until = pi(0x64) - elapsed + fade_out + duration;
    uint32_t end_handle;
    if (until > 0) {
        if (fade_out) {
            const float out = static_cast<float>(until) / static_cast<float>(fade_out);
            f = one - out;
            if (0.0f > f) f = 0.0f;
        }
        const float scale = one - f;
        scale_slot = scale;
        uint32_t channel = 0;
        uint32_t handle = title_global<uint32_t>(0x00973a68);
        bool set = false;
        if (handle) {
            channel = sound_channel(handle);
            regs.esi = channel;
            set = channel != 0;
        }
        if (!set && !(scale == 0.0f)) {
            const uint32_t sound = title_global<uint32_t>(0x0095f4c4);
            for (int k = 0; k < 4; ++k) {
                title_global<uint32_t>(at(0x10 + 4 * k)) = title_global<uint32_t>(0x0082e110 + 4 * k);
                title_global<uint32_t>(at(0x20 + 4 * k)) = title_global<uint32_t>(0x0082e290 + 4 * k);
            }
            regs.eax = title_global<uint32_t>(0x0082e298);
            regs.ecx = title_global<uint32_t>(0x0082e29c);
            regs.edx = title_global<uint32_t>(0x0082e294);
            regs.esi = 0;
            regs.edi = sound;
            const uint32_t name[1] = {0x00838b0c};  // "shock_loop"
            const uint32_t alias = title_call_at_regs(frame, 0x000c5810, &regs, 1, name);  // S_FindSoundByHash
            handle = 0;
            if (alias != 0xffffffffu) {
                regs.eax = alias;
                regs.ecx = sound;
                regs.edx = at(0xc);
                const uint32_t minus_one = 0xbf800000u;
                const uint32_t args[10] = {at(0xc), 0, 1, 0, at(0x20), at(0x10), minus_one, minus_one, minus_one, minus_one};
                title_call_at_regs(frame, 0x002c30a0, &regs, 10, args);  // S_PlaySoundAlias
                handle = title_global<uint32_t>(at(0xc));
            }
            title_global<uint32_t>(0x00973a68) = handle;
            channel = sound_channel(handle);
            regs.esi = channel;
            set = channel != 0;
        }
        if (set) {
            const int32_t voice = *reinterpret_cast<const int32_t*>(static_cast<uintptr_t>(channel));
            if (voice != -1) {
                float* fields = reinterpret_cast<float*>(static_cast<uintptr_t>(channel));
                const float value = fields[7] * scale_slot;
                uint32_t value_bits;
                std::memcpy(&value_bits, &value, 4);
                regs.eax = static_cast<uint32_t>(voice);
                regs.ecx = handle >> 12;
                regs.edx = handle >> 12;
                regs.esi = channel;
                regs.edi = static_cast<uint32_t>(elapsed);
                const uint32_t args[3] = {static_cast<uint32_t>(voice), 0, value_bits};
                title_call_at_regs(frame, 0x000c44f0, &regs, 3, args);  // SndVoice_SetParam
                fields[4] = scale_slot;
            }
        }
        regs.edi = static_cast<uint32_t>(elapsed);
        end_handle = title_global<uint32_t>(0x00973a6c);
        if (!end_handle) {
            regs.eax = title_global<uint32_t>(0x0095f4c4);
            regs.ecx = 0;
            const uint32_t args[4] = {at(0xc), 0x00838b00, 0, 1};  // "shock_end"
            const uint32_t out = title_call_at_regs(frame, 0x00025f70, &regs, 4, args);  // S_PlayLocalSoundByHash
            end_handle = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(out));
            title_global<uint32_t>(0x00973a6c) = end_handle;
        }
    } else {
        end_handle = title_global<uint32_t>(0x00973a6c);
    }
    const int32_t now = title_global<int32_t>(0x0097253c);
    const int32_t due = pi(0x64) - elapsed + now + duration;
    if (now < due) {
        if (title_global<int32_t>(0x00973a54)) title_global<int32_t>(0x00973a54) = 0;
        return;
    }
    if (due == title_global<int32_t>(0x00973a54)) return;
    title_global<int32_t>(0x00973a54) = due;
    if (!end_handle) return;
    const uint32_t channel = sound_channel(end_handle);
    if (!channel) return;
    const int32_t voice = *reinterpret_cast<const int32_t*>(static_cast<uintptr_t>(channel));
    if (voice == -1) return;
    regs.eax = static_cast<uint32_t>(voice);
    regs.ecx = end_handle >> 12;
    regs.edx = end_handle >> 12;
    regs.edi = static_cast<uint32_t>(duration);
    const uint32_t args[1] = {static_cast<uint32_t>(voice)};
    title_call_at_regs(frame, 0x000c3ff0, &regs, 1, args);  // SndVoice_Play
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
                   GAME_GAP_ESP("0x10000")
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

PLATFORM_ASM_ENTRY(CG_OffsetAltTankCamView_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %edx\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(CG_OffsetAltTankCamView) "addl $4, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL CG_OffsetAltTankCamView(uint32_t entry_esp) {
    // The tank camera (cvar 0x009481e4, view mode 2, linked flag 0x100000): behind the turret
    // ("tag_turret") along the flattened view direction, distance and height from the pitch
    // (0x008fefa0..b0) and the vehicle's +0x1c0, or - aiming down the barrel - at
    // "tag_aimdownbarrel" (else "tag_barrel" + 30 forward - 3 right + 10 up); both pulled in by
    // a box trace (CG_TracePoint, mask 0xe231) from above the turret. The view angles
    // (cl_viewangles 0x00961798) are reset from the delta angles when the mode changes
    // (0x0098ad49). Locals at the original's frame addresses, calls at its ESP.
    const uint32_t frame = (((entry_esp - 4) & ~15u) - 0xc0);
    const auto local = [frame](uint32_t offset) { return reinterpret_cast<float*>(static_cast<uintptr_t>(frame + offset)); };
    float* flat = local(0x10);
    float* axis = local(0x20);
    float* mins = local(0x60);
    float* maxs = local(0x70);
    float* turret = local(0x80);
    const float b = g(0x008fefb0);
    for (int k = 0; k < 4; ++k) {
        mins[k] = 0.0f - b;
        maxs[k] = b;
    }
    if (!title_global<uint32_t>(0x009481e4) || gi(0x00972a38) != 2 || !(title_global<uint32_t>(0x00972610) & 0x100000)) return;
    if (!G_EntHandleToEnt(&title_global<uint32_t>(0x009729b8))) return;
    uint8_t* ent = G_EntHandleToEnt(&title_global<uint32_t>(0x009729b8));
    const TitleRegs none{};
    const uint32_t lerp_args[1] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ent))};
    title_call_at_regs(frame, 0x002821e0, &none, 1, lerp_args);  // CG_CalcEntityLerpPositions
    uint32_t& once = title_global<uint32_t>(0x0098ee6c);
    const auto cached_hash = [&once](uint32_t bit, uint32_t slot, uint32_t name) {
        if (!(once & bit)) {
            once |= bit;
            title_global<uint32_t>(slot) = Com_HashKeyNoCase(0, &title_global<char>(name));
        }
        return title_global<uint32_t>(slot);
    };
    const auto tag_matrix = [frame, ent](uint32_t hash, float* out) {
        TitleRegs regs{};
        regs.edi = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ent));
        regs.esi = hash;
        const uint32_t args[1] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(out))};
        return title_call_at_regs(frame, 0x002e6420, &regs, 1, args);  // G_DObjTryGetTagMatrix
    };
    if (!tag_matrix(cached_hash(1, 0x0098ee68, 0x00838a48), turret)) return;  // "tag_turret"
    const int16_t vehicle = *reinterpret_cast<const int16_t*>(*reinterpret_cast<uint8_t* const*>(ent + 0x248) + 0x178);
    const uint8_t* vdef = title_global<const uint8_t*>(0x00950a28 + vehicle * 4);  // g_vehicleDefs[vehicle]
    const float height = *reinterpret_cast<const float*>(vdef + 0x1c0);
    float* org = &title_global<float>(0x00972bb0);
    const float k = g(0x0083ebd4);
    const bool unzoomed = g(0x009725d8) == 0.0f;
    if (unzoomed == (ent[2] != 1)) {
        if (title_global<uint8_t>(0x0098ad49)) {
            const float a = static_cast<float>(gi(0x009725a4)) * k;
            const float b2 = static_cast<float>(gi(0x009725a8)) * k;
            const float c = static_cast<float>(gi(0x009725ac)) * k;
            title_global<float>(0x00961798) = (g(0x009725ec) - a) + g(0x0083eaf4);
            title_global<float>(0x0096179c) = g(0x009725f0) - b2;
            title_global<float>(0x009617a0) = g(0x009725f4) - c;
        }
        title_global<uint8_t>(0x0098ad49) = 0;
        title_global<float>(0x00972bf8) = 0.0f;
        TitleRegs regs{};
        regs.eax = 0x00972bf0;
        const uint32_t axis_args[1] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(axis))};
        title_call_at_regs(frame, 0x002a4610, &regs, 1, axis_args);  // AnglesToAxisLeft
        flat[0] = axis[0];
        flat[1] = axis[1];
        flat[2] = 0.0f;
        regs = TitleRegs{};
        regs.esi = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(flat));
        title_call_at_regs(frame, 0x00297770, &regs, 0, nullptr);  // VectorNormalize
        const float offset = g(0x008fefac);
        const float two = g(0x0083eba8);
        const float p = g(0x00972bf0) + offset;
        float f;
        if (0.0f > p) {
            f = 0.0f;
        } else {
            f = offset * two;
            if (!(p > f)) f = p;
        }
        f = f / (offset * two);
        const float scale = g(0x008fefa8);
        const float back = f * scale - g(0x008fefa4) * scale;
        const float x = back * flat[0] + turret[12];
        const float z = back * flat[2] + turret[14];
        const float y = back * flat[1] + turret[13];
        float h = g(0x008fefa0) > f ? f / g(0x008fefa0) : g(0x0083eb20);
        h = h * g(0x008da6dc);
        const float side = g(0x0095f668);
        const float up = height + g(0x0095f66c);
        const float vx = up * axis[6] + (axis[3] * side + h * axis[0]);
        const float vy = up * axis[7] + (axis[4] * side + h * axis[1]);
        const float vz = up * axis[8] + (axis[5] * side + h * axis[2]);
        org[0] = vx + x;
        org[1] = vy + y;
        org[2] = vz + z;
    } else {
        float* barrel = axis;  // the second tag matrix shares the axis' place
        if (tag_matrix(cached_hash(2, 0x0098ee64, 0x00838a34), barrel)) {  // "tag_aimdownbarrel"
            org[0] = barrel[12];
            org[1] = barrel[13];
            org[2] = barrel[14];
        } else {
            if (!tag_matrix(cached_hash(4, 0x0098ee60, 0x00838a28), barrel)) return;  // "tag_barrel"
            const float thirty = g(0x0083ee38), three = g(0x0083ec9c), ten = g(0x0083eb98);
            const float x = (barrel[0] * thirty + barrel[12]) - barrel[4] * three;
            const float y = (barrel[1] * thirty + barrel[13]) - barrel[5] * three;
            const float z = (barrel[2] * thirty + barrel[14]) - barrel[6] * three;
            org[0] = barrel[8] * ten + x;
            org[1] = barrel[9] * ten + y;
            org[2] = barrel[10] * ten + z;
        }
        if (title_global<uint8_t>(0x0098ad49) != 1) {
            const float a = static_cast<float>(gi(0x009725a4)) * k;
            const float b2 = static_cast<float>(gi(0x009725a8)) * k;
            const float c = static_cast<float>(gi(0x009725ac)) * k;
            title_global<float>(0x00961798) = (g(0x009725ec) - a) - g(0x0083eaf4);
            title_global<float>(0x0096179c) = g(0x009725f0) - b2;
            title_global<float>(0x009617a0) = g(0x009725f4) - c;
        }
        title_global<uint8_t>(0x0098ad49) = 1;
    }
    // From above the turret to the camera.
    flat[0] = turret[12];
    flat[1] = turret[13];
    flat[2] = (height + g(0x0095f66c)) * g(0x0083066c) + turret[14];
    float* trace = axis;
    TitleRegs regs{};
    regs.eax = 0xe231;
    regs.ecx = 0x00972bb0;
    const uint32_t args[5] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(trace)),
                              static_cast<uint32_t>(reinterpret_cast<uintptr_t>(flat)),
                              static_cast<uint32_t>(reinterpret_cast<uintptr_t>(mins)),
                              static_cast<uint32_t>(reinterpret_cast<uintptr_t>(maxs)), title_global<uint32_t>(0x009729b8)};
    title_call_at_regs(frame, 0x00286d00, &regs, 5, args);  // CG_TracePoint
    if (trace[8] == 1.0f) return;
    org[0] = trace[0];
    org[1] = trace[1];
    org[2] = (g(0x0083eb20) - trace[8]) * g(0x0083eeac) + trace[2];
    const uint32_t again[5] = {args[0], args[1], args[2], args[3], title_global<uint32_t>(0x009729b8)};
    title_call_at_regs(frame, 0x00286d00, &regs, 5, again);
    org[0] = trace[0];
    org[1] = trace[1];
    org[2] = trace[2];
}

PLATFORM_ASM_ENTRY(AnglesToRightNoPitch_entry,
                   "pushl %esi\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(AnglesToRightNoPitch) "addl $8, %esp\n"
                   "ret")

void XB_CDECL AnglesToRightNoPitch(const float* angles, float* right) {
    // The right vector of yaw and roll only: (cos roll * sin yaw, -(cos roll * cos yaw),
    // -sin roll).
    const float to_radians = g(0x0083edd8);
    float sy, cy, sr, cr;
    FastSinCos(&sy, 0, &cy, angles[1] * to_radians);
    FastSinCos(&sr, 0, &cr, angles[2] * to_radians);
    right[0] = cr * sy;
    right[1] = 0.0f - cr * cy;
    right[2] = 0.0f - sr;
}

void XB_CDECL CG_OffsetFirstPersonView() {
    // Q3's CG_OffsetFirstPersonView: dead (+0x34 of the next snapshot >= 4: fixed death view);
    // else the gun kick angles added, the damage kick (0x00972f80/84, over 100 ms in and
    // 400 ms out, less while zoomed), the eye height, then - unless linked (0x6000) - the bob
    // (angles while zoomed with +0x648, height, side sway along the yaw/roll right vector),
    // the landing dip (cg.landChange over 150 + 300 ms), the step offset and the lean
    // (AddLeanToPosition), never below the origin + 8.
    float* angles = &title_global<float>(0x00972bf0);
    float* org = &title_global<float>(0x00972bb0);
    const uint8_t* next = title_global<const uint8_t*>(0x00971aac);
    if (*reinterpret_cast<const int32_t*>(next + 0x34) >= 4) {
        angles[2] = g(0x0083ec84);
        angles[0] = g(0x0083ef14);
        angles[1] = static_cast<float>(*reinterpret_cast<const int32_t*>(next + 0x114));
        org[2] = org[2] + g(0x009725fc);
        return;
    }
    CG_UpdateGunKick();
    const float pitch = angles[0] + g(0x00972f94);
    const float yaw = angles[1] + g(0x00972f98);
    const float roll = angles[2] + g(0x00972f9c);
    const float zoom = g(0x009725d8);
    const int32_t now = gi(0x0097253c);
    const uint8_t* weapon = title_global<const uint8_t*>(0x00972acc);
    angles[0] = pitch;
    angles[1] = yaw;
    angles[2] = roll;
    const int32_t damage_time = gi(0x00972f60);
    if (damage_time) {
        const float one = g(0x0083eb20);
        const float half_zoom = zoom * g(0x0083eb24);
        float scale = one - half_zoom;
        if (!(zoom == 0.0f) && *reinterpret_cast<const uint32_t*>(weapon + 0x638)) scale = (half_zoom + one) * scale;
        const float d = static_cast<float>(now - damage_time);
        const float two = g(0x0083eba8);
        float ratio;
        bool kick = true;
        if (g(0x0083eb90) > d) {
            const float k = d * g(0x0083eb94);
            ratio = (two - abs_bits(k)) * k;
        } else {
            const float left = one - (d - g(0x0083eb90)) * g(0x0083f0e4);
            if (left > 0.0f) {
                const float u = one - left;
                ratio = one - (two - abs_bits(u)) * u;
            } else {
                kick = false;
                ratio = 0.0f;
            }
        }
        if (kick) {
            ratio = ratio * scale;
            angles[0] = g(0x00972f80) * ratio + pitch;
            angles[2] = g(0x00972f84) * ratio + roll;
        }
    }
    org[2] = org[2] + g(0x009725fc);
    if (title_global<uint32_t>(0x00972610) & 0x6000) return;
    using ext = long double;
    const float cycle = g(0x00973a80);
    const float speed = g(0x00973a84);
    if (!(zoom == 0.0f) && !(*reinterpret_cast<const float*>(weapon + 0x648) == 0.0f)) {
        const float cap = g(0x0083ef1c);
        const ext w = *reinterpret_cast<const float*>(weapon + 0x648);
        const ext lateral = CG_BobCycleLateral(cycle, speed, cap);
        angles[0] = static_cast<float>(static_cast<ext>(angles[0]) - lateral * static_cast<ext>(g(0x009725d8)) * w);
        const ext vertical = CG_BobCycleVertical(cycle, speed, cap);
        angles[1] = static_cast<float>(static_cast<ext>(angles[1]) - vertical * static_cast<ext>(g(0x009725d8)) * w);
    }
    const float cap = g(0x009492c0);
    org[2] = static_cast<float>(static_cast<ext>(CG_BobCycleLateral(cycle, speed, cap)) + static_cast<ext>(org[2]));
    const int32_t stance = gi(0x009725f8);
    const float stance_scale = stance == gi(0x00972970) ? g(0x0094aa90) : stance == gi(0x00972974) ? g(0x00947e80) : g(0x0094a6a0);
    float amplitude = stance_scale * g(0x00973a84);
    if (amplitude > cap) amplitude = cap;
    const float sway = static_cast<float>(platform_x87_sin_ext(static_cast<ext>(g(0x00973a80))) * static_cast<ext>(amplitude));
    float right[3];
    AnglesToRightNoPitch(angles, right);
    org[0] = static_cast<float>(static_cast<ext>(right[0]) * sway + static_cast<ext>(org[0]));
    const float landed = static_cast<float>(now - gi(0x00972af4));
    org[1] = static_cast<float>(static_cast<ext>(right[1]) * sway + static_cast<ext>(org[1]));
    org[2] = static_cast<float>(static_cast<ext>(right[2]) * sway + static_cast<ext>(org[2]));
    if (0.0f > landed) title_global<int32_t>(0x00972af4) = now - 450;
    if (g(0x0083ebf0) > landed) {
        org[2] = landed * g(0x0083ef9c) * g(0x00972af0) + org[2];
    } else if (g(0x0083eba0) > landed) {
        org[2] = (g(0x0083eb20) - (landed - g(0x0083ebf0)) * g(0x0083ec20)) * g(0x00972af0) + org[2];
    }
    CG_StepOffset();
    float position[3] = {org[0], org[1], org[2]};
    cg_add_lean(position, g(0x0097259c), angles[1], 16.0f, 20.0f);
    org[0] = position[0];
    org[1] = position[1];
    const float floor = g(0x00972558) + g(0x0083ed78);
    org[2] = position[2];
    if (floor > position[2]) org[2] = floor;
}

namespace {

// v rotated into the frame of `angles` (forward * x + left * y + up * z, left = -right).
void rotate_by_angles(uint32_t angles, const float* v, float* out) {
    const float x = v[0], y = v[1], z = v[2];
    uint32_t args[2] = {angles, 0};
    float forward[3], right[3], up[3];
    args[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(forward));
    AnglesToAxis(args, right, up);
    const float ny = 0.0f - y;
    out[0] = (ny * right[0] + up[0] * z) + forward[0] * x;
    out[1] = (ny * right[1] + up[1] * z) + forward[1] * x;
    out[2] = (ny * right[2] + up[2] * z) + forward[2] * x;
}

}  // namespace

PLATFORM_ASM_ENTRY(CG_RotateByGunAngles_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(CG_RotateByGunAngles) "addl $4, %esp\n"
                   "ret")

void XB_CDECL CG_RotateByGunAngles(float* v) {
    // v rotated by the gun angles 0x00972fcc.
    float out[3];
    rotate_by_angles(0x00972fcc, v, out);
    v[0] = out[0];
    v[1] = out[1];
    v[2] = out[2];
}

PLATFORM_ASM_ENTRY(CG_ViewToWorld_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(CG_ViewToWorld) "addl $4, %esp\n"
                   "ret")

void XB_CDECL CG_ViewToWorld(float* v) {
    // v rotated by the view angles (0x00972bf0) and moved to the view origin (0x00972bb0).
    float out[3];
    const float in[3] = {v[0], v[1], v[2]};
    rotate_by_angles(0x00972bf0, in, out);
    const float* org = &title_global<float>(0x00972bb0);
    v[0] = out[0] + org[0];
    v[1] = out[1] + org[1];
    v[2] = out[2] + org[2];
}

void XB_REGS3 XB_CDECL CG_StoreGunOffsetFromView(uint32_t, uint32_t, const float* origin) {
    // The gun origin's offset from the view origin times the zoom (0x00972fa8), for zoom
    // weapons (+0x6dc) while zooming; else zero.
    const uint8_t* def = title_global<const uint8_t* const*>(0x0095f4d8)[gi(0x009725d0)];
    float* out = &title_global<float>(0x00972fa8);
    if (!*reinterpret_cast<const uint32_t*>(def + 0x6dc)) {
        out[0] = out[1] = out[2] = 0.0f;
        return;
    }
    const float zoom = g(0x009725d8);
    if (zoom == 0.0f) {
        out[0] = out[1] = out[2] = 0.0f;
        return;
    }
    const float* org = &title_global<float>(0x00972bb0);
    out[0] = (origin[0] - org[0]) * zoom;
    out[1] = (origin[1] - org[1]) * zoom;
    out[2] = (origin[2] - org[2]) * zoom;
}

void XB_REGS1 XB_CDECL CG_CalculateWeaponPosition(float* origin) {
    // The view weapon's origin: the lean's side shift (less while zoomed), the gun move offset
    // (also kept in 0x00972a98), rotated by the gun angles, the sway lag (0x00972c24/28), into
    // world space at the view, the landing dip (cg.landChange over 150 + 300 ms), and the zoom
    // offset (CG_StoreGunOffsetFromView).
    origin[2] = 0.0f;
    origin[1] = 0.0f;
    origin[0] = 0.0f;
    const float lean = g(0x0097259c);
    const float one = g(0x0083eb20);
    const float zoom = g(0x009725d8);
    if (!(lean == 0.0f) && one > zoom) {
        const float k = (g(0x0083eba8) - abs_bits(lean)) * lean;
        const float angles[3] = {0.0f, 0.0f, k * g(0x0083ec5c)};
        const float scale = (one - zoom) * k * g(0x0083f124);
        float right[3];
        AnglesToRightNoPitch(angles, right);
        origin[0] = right[0] * scale;
        origin[1] = right[1] * scale;
        origin[2] = right[2] * scale;
    }
    float offset[3] = {0.0f, 0.0f, 0.0f};
    CG_CalcGunMoveOffset(offset);
    title_global<float>(0x00972a98) = offset[0];
    title_global<float>(0x00972a9c) = offset[1];
    title_global<float>(0x00972aa0) = offset[2];
    origin[0] = origin[0] + offset[0];
    origin[1] = origin[1] + offset[1];
    origin[2] = offset[2] + origin[2];
    CG_RotateByGunAngles(origin);
    origin[1] = origin[1] - g(0x00972c24);
    origin[2] = g(0x00972c28) + origin[2];
    CG_ViewToWorld(origin);
    const int32_t landed = gi(0x0097253c) - gi(0x00972af4);
    if (landed < 0x96)
        origin[2] = static_cast<float>(landed) * g(0x00972af0) * g(0x0083f07c) + origin[2];
    else if (landed < 0x1c2)
        origin[2] = static_cast<float>(0x1c2 - landed) * g(0x00972af0) * g(0x0083f120) + origin[2];
    CG_StoreGunOffsetFromView(0, 0, origin);
}

PLATFORM_ASM_ENTRY(CG_AddGunMoveOffset_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(CG_AddGunMoveOffset) "addl $4, %esp\n"
                   "ret")

void XB_CDECL CG_AddGunMoveOffset(float* origin) {
    // CG_CalcGunMoveOffset into a zeroed local, kept at 0x00972a98..0x00972aa0 and added to
    // the origin (the same steps CG_CalculateWeaponPosition inlines).
    float offset[3] = {0.0f, 0.0f, 0.0f};
    CG_CalcGunMoveOffset(offset);
    title_global<float>(0x00972a98) = offset[0];
    title_global<float>(0x00972a9c) = offset[1];
    title_global<float>(0x00972aa0) = offset[2];
    const float x = origin[0] + offset[0];
    origin[1] = origin[1] + offset[1];
    origin[0] = x;
    origin[2] = origin[2] + offset[2];
}

void XB_THISCALL CG_StoreQuantizedGunAngles(const float* angles) {
    // With the weapon's ADS view flag (def +0x6dc) and a non-zero ADS fraction (0x009725d8):
    // pitch and yaw through ANGLE2SHORT / SHORT2ANGLE into 0x00972fa0 / 0x00972fa4; else the
    // angles at 0x00972bf0 / 0x00972bf4.
    const uint8_t* def = title_global<const uint8_t* const*>(0x0095f4d8)[title_global<int32_t>(0x009725d0)];
    if (*reinterpret_cast<const uint32_t*>(def + 0x6dc) && !(g(0x009725d8) == g(0x0083eafc))) {
        const float to_short = g(0x0083ebd8);  // 65536 / 360
        const float to_angle = g(0x0083ebd4);  // 360 / 65536
        title_global<float>(0x00972fa0) =
            static_cast<float>(static_cast<int32_t>(angles[0] * to_short) & 0xffff) * to_angle;
        title_global<float>(0x00972fa4) =
            static_cast<float>(static_cast<int32_t>(angles[1] * to_short) & 0xffff) * to_angle;
        return;
    }
    title_global<uint32_t>(0x00972fa0) = title_global<uint32_t>(0x00972bf0);
    title_global<uint32_t>(0x00972fa4) = title_global<uint32_t>(0x00972bf4);
}

namespace {

template <typename T>
T& field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

}  // namespace

PLATFORM_ASM_ENTRY(CameraShake_NoiseSample_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(CameraShake_NoiseSample) "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $4, %esp\n"
                   "ret")

float XB_CDECL CameraShake_NoiseSample(float* channel) {
    // 1-D gradient noise (Noise_Init's tables: permutation 0x0093e758, gradients 0x0093eff0) at
    // channel[0] (a random start in 0..16383 once out of range) + time * frequency [2], summed over
    // the octave count [1] (frequency doubling, amplitude halving), times the amplitude [3].
    if (0.0f > channel[0] || channel[0] > g(0x0083ee50))  // 16384
        channel[0] = static_cast<float>(title_function<int32_t XB_CDECL()>(0x001efcc0)() % 16384);  // rand
    const float t = static_cast<float>(gi(0x0097253c)) * g(0x0083eb4c) * channel[2] + channel[0];
    float sum = 0.0f;
    float amplitude = g(0x0083eb20);
    const uint32_t* const octaves = reinterpret_cast<const uint32_t*>(channel + 1);
    for (uint32_t i = 1; i <= *octaves; ++i) {
        const float x = platform_x87_mul(static_cast<float>(static_cast<int32_t>(i)), t);
        if (!gi(0x0095f560)) title_function<void XB_CDECL()>(0x002a7da0)();  // Noise_Init
        const float v = x + g(0x0083ed4c);                                 // 4096
        const int32_t n = static_cast<int32_t>(v);
        const uint32_t index = static_cast<uint32_t>(n) & 0xff;
        const float f = v - static_cast<float>(n);
        const float g0 = g(0x0093eff0 + static_cast<uint32_t>(gi(0x0093e758 + index * 4)) * 4);
        const float g1 = g(0x0093eff0 + static_cast<uint32_t>(gi(0x0093e758 + ((index + 1) & 0xff) * 4)) * 4);
        const float a = g0 * f;
        const float b = (f - g(0x0083eb20)) * g1 - a;
        const float s = (g(0x0083ec9c) - f * g(0x0083eba8)) * f * f;  // 3 - 2f
        const float value = b * s + a;
        sum = value * amplitude + sum;
        amplitude = amplitude * g(0x0083eb24);  // 0.5
    }
    return channel[3] * sum;
}

PLATFORM_ASM_ENTRY(CameraShake_ComputeOffset_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CameraShake_ComputeOffset) "addl $12, %esp\n"
                   "ret $4")

void XB_CDECL CameraShake_ComputeOffset(int32_t type, uint8_t* shake, float* out) {
    // Adds the shake's two noise channels (+0xc x, +0x1c y) to out's x/y (all four lanes, ADDPS):
    // scaled by the strength +4, faded in over the last 0.1 of the time left (+0x30); type 0 also by
    // +0x2c and the time left, type 1 only while time is left; other types add nothing.
    const float left = field<float>(shake, 0x30);
    float scale = field<float>(shake, 4);
    float x;
    float y;
    if (type == 0) {
        if (g(0x0083ebf8) > field<float>(shake, 0x30))
            scale = field<float>(shake, 0x30) * g(0x0083eb98) * field<float>(shake, 4);  // 0.1, 10
        y = CameraShake_NoiseSample(&field<float>(shake, 0x1c)) * field<float>(shake, 0x2c) * scale * left;
        x = CameraShake_NoiseSample(&field<float>(shake, 0xc)) * field<float>(shake, 0x2c) * scale * left;
    } else if (type == 1) {
        if (field<float>(shake, 0x30) != g(0x0083eafc) && g(0x0083ebf8) > field<float>(shake, 0x30))
            scale = field<float>(shake, 0x30) * g(0x0083eb98) * field<float>(shake, 4);
        y = CameraShake_NoiseSample(&field<float>(shake, 0x1c)) * scale;
        x = CameraShake_NoiseSample(&field<float>(shake, 0xc)) * scale;
    } else {
        return;
    }
    const float offset[4] = {x, y, 0.0f, 0.0f};
    float sum[4];
    for (uint32_t i = 0; i < 4; ++i) sum[i] = out[i] + offset[i];
    for (uint32_t i = 0; i < 4; ++i) out[i] = sum[i];
}

float* XB_THISCALL CameraShake_Evaluate(uint8_t* manager, float* view) {
    // Unless paused (0x0094c7a4): each of the 5 active shakes (+0xc, 0x40 apart; +0x3c active) advances
    // its phase (+4 += rate +8 * frame time 0x008ddaa0) and, for timed types (table 0x008d5138, 0x20
    // per type: duration +4 > 0), its time left (+0x30; off at 0); its offset (CameraShake_ComputeOffset
    // with the table's mode +2) becomes a rotation applied to the view matrix's rotation; the
    // translation row stays.
    if (gi(0x0094c7a4)) return view;
    const float dt = g(0x008ddaa0);
    uint8_t* slot = manager + 0xc;
    for (int32_t n = 5; n != 0; --n, slot += 0x40) {
        if (!field<uint32_t>(slot, 0x3c)) continue;
        const uint8_t* row = &title_global<uint8_t>(0x008d5138);
        while (*reinterpret_cast<const int16_t*>(row) != field<int32_t>(slot, 0)) row += 0x20;
        if (field<float>(slot, 8) != 0.0f) field<float>(slot, 4) = field<float>(slot, 8) * dt + field<float>(slot, 4);
        if (*reinterpret_cast<const float*>(row + 4) > 0.0f) {
            const float left = field<float>(slot, 0x30) - dt;
            field<float>(slot, 0x30) = left;
            if (0.0f >= left) field<uint32_t>(slot, 0x3c) = 0;
        }
        if (!field<uint32_t>(slot, 0x3c)) continue;
        alignas(16) float offset[4] = {g(0x0082e680), g(0x0082e684), g(0x0082e688), g(0x0082e68c)};
        CameraShake_ComputeOffset(*reinterpret_cast<const int16_t*>(row + 2), slot, offset);
        alignas(16) float q[4] = {};
        alignas(16) float m[4] = {};
        alignas(16) float matrix[16] = {};
        title_function<void XB_REGS3 XB_CDECL(uint32_t, const float*, float*)>(0x002c90c0)(0, offset, q);  // Quat_FromRotationVector
        title_function<uint32_t XB_REGS3 XB_STDCALL(const float*, uint32_t, float*, float*)>(0x002ae350)(view, 0, m, m);  // Mat44_ToQuatTranslation
        const float r1 = q[3] * m[3] - m[0] * q[0] + q[2] * m[2] + q[1] * m[1];
        const float r2 = q[2] * m[1] + q[3] * m[0] + q[0] * m[3] - m[2] * q[1];
        const float r3 = m[1] * q[3] + m[2] * q[0] + q[1] * m[3] - q[2] * m[0];
        const float r0 = m[2] * q[3] + q[1] * m[0] + q[2] * m[3] - m[1] * q[0];
        m[3] = r1;
        m[0] = r2;
        m[1] = r3;
        m[2] = r0;
        title_function<uint32_t XB_REGS3 XB_STDCALL(uint32_t, uint32_t, float*, const float*)>(0x002bdc80)(0, 0, matrix, m);  // Quat_ToMat44
        for (uint32_t i = 12; i < 16; ++i) matrix[i] = view[i];
        for (uint32_t i = 0; i < 16; ++i) view[i] = matrix[i];
    }
    return view;
}

PLATFORM_ASM_ENTRY(CG_AddAdsGunOffset_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(CG_AddAdsGunOffset) "addl $4, %esp\n"
                   "ret")

void XB_CDECL CG_AddAdsGunOffset(float* angles) {
    // For weapons with an aim-down-sight blend (+0x6dc): while the ADS fraction (0x009725d8) is
    // between 0 and 1 its direction (0x00972a74: 1 rising) is tracked against the last fraction
    // (0x00972a70; reset with 0x00972a78 = 0 when that was 0 or 1); the pitch offset is the
    // weapon's +0x770 times the fraction. CG_CalcGunRotOffset adds the sway; the offset
    // (0x00972aa4) is added to angles.
    float offset[3] = {0.0f, 0.0f, 0.0f};
    const uint8_t* const def = title_global<uint8_t* const*>(0x0095f4d8)[gi(0x009725d0)];
    if (*reinterpret_cast<const uint32_t*>(def + 0x6dc)) {
        const float ads = g(0x009725d8);
        const float one = g(0x0083eb20);
        if (ads != one && ads != 0.0f) {
            const float last = g(0x00972a70);
            uint32_t rising;
            if (ads == last)
                rising = title_global<uint32_t>(0x00972a74);
            else
                rising = ads >= last ? 1 : 0;
            if (last == one || last == 0.0f) {
                title_global<uint32_t>(0x00972a74) = rising;
                title_global<float>(0x00972a78) = 0.0f;
            }
        }
        title_global<float>(0x00972a70) = ads;
        offset[0] = wf(0x770) * ads;
    }
    CG_CalcGunRotOffset(offset);
    title_global<float>(0x00972aa4) = offset[0];
    title_global<float>(0x00972aa8) = offset[1];
    title_global<float>(0x00972aac) = offset[2];
    const float x = angles[0] + offset[0];
    angles[1] = angles[1] + offset[1];
    angles[2] = angles[2] + offset[2];
    angles[0] = x;
}

namespace {

// *out += sin(time * rate) * a * scale, all on the x87 (FILD, FMUL, FSIN, FMUL ST(1), FMUL, FADD, FSTP).
void x87_add_sine_sway(const int32_t* time, const float* rate, long double a, const float* scale, float* out) {
    __asm__ volatile(
        "fldt %1\n\t"
        "fildl %2\n\t"
        "fmuls %3\n\t"
        "fsin\n\t"
        "fmul %%st(1), %%st\n\t"
        "fmuls %4\n\t"
        "fadds %0\n\t"
        "fstps %0\n\t"
        "fstp %%st(0)"
        : "+m"(*out)
        : "m"(a), "m"(*time), "m"(*rate), "m"(*scale)
        : "st", "st(1)");
}

}  // namespace

void XB_REGS2 XB_CDECL CG_AddWeaponIdleSway(uint32_t, float* angles) {
    // The weapon's idle sway (+0x684, 80 when 0; blended to +0x680 by the ADS fraction for ADS
    // weapons) scaled by the stance factor (0x00972ab0, moving at 0.0005 per ms towards prone +0x68c /
    // crouch +0x688 / 1): sines of the time at 0.0005, 0.0007 and 0.001 rad/ms into roll (0.04), yaw
    // and pitch (0.01), on the x87.
    const uint8_t* const def = title_global<uint8_t* const*>(0x0095f4d8)[gi(0x009725d0)];
    float sway;
    if (*reinterpret_cast<const uint32_t*>(def + 0x6dc)) {
        sway = (wf(0x680) - wf(0x684)) * g(0x009725d8) + wf(0x684);
    } else {
        sway = wf(0x684);
        if (sway == g(0x0083eafc)) sway = g(0x0083ee48);  // 0 -> 80
    }
    const uint8_t flags = title_global<uint8_t>(0x0097257c);
    const float target = (flags & 1) ? wf(0x68c) : (flags & 2) ? wf(0x688) : g(0x0083eb20);
    float& factor = title_global<float>(0x00972ab0);
    const float current = factor;
    if (target != current) {
        const float step = static_cast<float>(gi(0x00972534)) * g(0x0083ee44);
        if (target > current) {
            const float next = step + current;
            factor = next;
            if (next > target) factor = target;
        } else {
            const float next = current - step;
            factor = next;
            if (target > next) factor = target;
        }
    }
    const long double a = static_cast<long double>(factor) * sway;
    const int32_t* const time = &title_global<int32_t>(0x0097253c);
    x87_add_sine_sway(time, &g(0x0083ee44), a, &g(0x0083ed7c), &angles[2]);
    x87_add_sine_sway(time, &g(0x0083ee40), a, &g(0x0083eb94), &angles[1]);
    x87_add_sine_sway(time, &g(0x0083eb4c), a, &g(0x0083eb94), &angles[0]);
}

// The original ends in a tail jump to CG_StoreQuantizedGunAngles (thiscall, ECX = angles).
PLATFORM_ASM_ENTRY(CG_CalculateWeaponAngles_entry,
                   "pushl %eax\n" PLATFORM_ASM_CALL(CG_CalculateWeaponAngles) "addl $4, %esp\n"
                   "ret")

void XB_CDECL CG_CalculateWeaponAngles(float* angles) {
    // The view weapon's angles (EAX): the lean roll ((2 - |l|) l x -2 of 0x0097259c), the ADS
    // offset and idle sway, the kick 0x00972fcc.. (minus the view kick 0x00972f94.. without the
    // lag spring), the landing dip (since 0x00972f60: eased in over 100 and out over 400 ms x
    // (1 + ADS) / 2, less with weapon +0x638 while aiming; pitch/roll by 0x00972f80 / 2, yaw by
    // 0x00972f84), the lag spring, normalized to +-180, turned by the view and stored quantized.
    angles[2] = 0.0f;
    angles[1] = 0.0f;
    angles[0] = 0.0f;
    const float lean = g(0x0097259c);
    if (!(lean == 0.0f)) angles[2] = (g(0x0083eba8) - abs_bits(lean)) * lean * g(0x0083ec5c);
    CG_AddAdsGunOffset(angles);
    CG_AddWeaponIdleSway(0, angles);
    angles[0] = angles[0] + g(0x00972fcc);
    angles[1] = angles[1] + g(0x00972fd0);
    angles[2] = g(0x00972fd4) + angles[2];
    const uint8_t* const def = title_global<uint8_t* const*>(0x0095f4d8)[gi(0x009725d0)];
    if (!*reinterpret_cast<const uint32_t*>(def + 0x6dc)) {
        angles[0] = angles[0] - g(0x00972f94);
        angles[1] = angles[1] - g(0x00972f98);
        angles[2] = angles[2] - g(0x00972f9c);
    }
    const int32_t start = gi(0x00972f60);
    if (start) {
        const float ads = g(0x009725d8);
        const float one = g(0x0083eb20);
        const float half = g(0x0083eb24);
        float scale = (ads + one) * half;
        const float in_time = scale * g(0x0083eb90);
        const float out_time = scale * g(0x0083f11c);
        if (!(ads == 0.0f) && *reinterpret_cast<const uint32_t*>(weapon() + 0x638))
            scale = (one - ads * g(0x0083eb58)) * scale;
        const float dt = static_cast<float>(gi(0x0097253c) - start);
        float f;
        bool apply = true;
        if (in_time > dt) {
            const float q = dt / in_time;
            f = (g(0x0083eba8) - abs_bits(q)) * q;
        } else {
            const float q = (dt - in_time) / out_time;
            const float r = one - q;
            if (r > 0.0f) {
                const float u = one - r;
                f = one - (g(0x0083eba8) - abs_bits(u)) * u;
            } else {
                apply = false;
                f = 0.0f;
            }
        }
        if (apply) {
            const float y = angles[1];
            f = f * scale;
            angles[0] = g(0x00972f80) * f * half + angles[0];
            angles[1] = y - g(0x00972f84) * f;
            angles[2] = g(0x00972f84) * f * half + angles[2];
        }
    }
    CG_UpdateWeaponLag(angles);
    TitleRegs regs{};
    regs.esi = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(angles));
    cg_call_regs(0x002789f0, &regs, nullptr, 0);  // Angles2Normalize180
    CG_RotateAnglesByView(angles);
    CG_StoreQuantizedGunAngles(angles);
}

// The title callees run at the original's ESP with its registers and the locals (the ADS
// fraction / scale, the draw flag, the gun angles) at the original's frame addresses: the
// entry moves the C frame below a gap (weapon registration can read files) and hands over the
// entry ESP and EBX/ESI/EDI.
PLATFORM_ASM_ENTRY(CG_AddViewWeapon_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x30000")
                   "pushl %edx\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(CG_AddViewWeapon) "addl $16, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL CG_AddViewWeapon(uint32_t entry_esp, uint32_t ebx, uint32_t esi, uint32_t edi) {
    // Q3's CG_AddViewWeapon (cdecl ps): nothing in third person (0x00972548) or with ps +0xc0 &
    // 0x106000; without a weapon (ps +0x80) the gun angles 0x00972fa0.. take the view's pitch and
    // yaw. Else the weapon registered, bob and sway, the hand (the local client's refEntity +0x80)
    // placed: position, the view-forward offset for type-3 weapons while flag 0x20 (0x008fef70),
    // the offsets 0x00949500 / 0x00947d60 / 0x009489c0 along the view axes, the angles and axis
    // (AnglesToAxisLeft2), the anims and CG_AddPlayerWeapon (drawn unless cg_drawGun 0x00949864
    // is 0, or not 2 with an ADS overlay).
    const uint32_t ps_addr = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(entry_esp + 4));
    uint8_t* const ps = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(ps_addr));
    const uint32_t frame = entry_esp - 0x18;  // after sub esp,0x14 and push ebp
    uint32_t& scale_slot = title_global<uint32_t>(entry_esp - 0x14);
    uint32_t& draw_slot = title_global<uint32_t>(entry_esp - 0x10);
    const uint32_t angles = entry_esp - 0xc;
    draw_slot = 1;
    if (gi(0x00972548)) return;
    TitleRegs regs{};
    regs.ebx = ebx;
    regs.esi = esi;
    regs.edi = edi;
    const int32_t draw_gun = gi(0x00949864);
    if (draw_gun != 2) {
        if (!draw_gun) {
            draw_slot = 0;
        } else {
            regs.eax = static_cast<uint32_t>(draw_gun);
            regs.ecx = entry_esp - 0x14;
            if (title_call_at_regs(frame, 0x002765f0, &regs, 0, nullptr)) draw_slot = 0;  // CG_GetAdsOverlayFraction
        }
    }
    if (*reinterpret_cast<const uint32_t*>(ps + 0xc0) & 0x106000) return;
    const int32_t weapon_index = *reinterpret_cast<const int32_t*>(ps + 0x80);
    if (weapon_index <= 0) {
        title_global<float>(0x00972fa0) = g(0x00972bf0);
        title_global<float>(0x00972fa4) = g(0x00972bf4);
        title_global<float>(0x00972fa8) = 0.0f;
        title_global<float>(0x00972fac) = 0.0f;
        title_global<float>(0x00972fb0) = 0.0f;
        return;
    }
    const uint32_t top = frame - 0xc;  // after push ebx, esi, edi
    regs.eax = static_cast<uint32_t>(weapon_index);
    const uint32_t weapon_arg[1] = {static_cast<uint32_t>(weapon_index)};
    regs.eax = title_call_at_regs(top, 0x002832b0, &regs, 1, weapon_arg);  // CG_RegisterWeapon
    const uint32_t rec = 0x00975fa8 + static_cast<uint32_t>(*reinterpret_cast<const int32_t*>(ps + 0x80)) * 0xc4;
    regs.ebx = rec;
    regs.eax = title_call_at_regs(top, 0x00278ae0, &regs, 0, nullptr);  // CG_CalcWeaponBob
    regs.eax = title_call_at_regs(top, 0x0027a2b0, &regs, 0, nullptr);  // CG_CalcWeaponSway
    const uint32_t cent = title_global<uint32_t>(title_global<uint32_t>(0x0095f534) + 4);
    regs.esi = cent;
    regs.eax = title_global<uint32_t>(0x0095f534);
    const uint32_t ref = title_call_at_regs(top, 0x002be200, &regs, 0, nullptr);  // CG_GetEntityRefEntity
    const uint32_t hand = ref + 0x80;
    float* const origin = reinterpret_cast<float*>(static_cast<uintptr_t>(hand + 0x3c));
    regs.esi = hand;
    regs.edi = hand + 0x3c;
    regs.eax = hand + 0x3c;
    title_call_at_regs(top, 0x00282d10, &regs, 0, nullptr);  // CG_CalculateWeaponPosition
    scale_slot = 0;
    if (title_global<uint8_t>(0x0097257c) & 0x20) {
        const uint8_t* const def = title_global<uint8_t* const*>(0x0095f4d8)[gi(0x009725d0)];
        if (*reinterpret_cast<const int32_t*>(def + 0x98) == 3) {
            const uint32_t forward = title_global<uint32_t>(0x008fef70);
            scale_slot = forward;
            regs.ecx = forward;
            regs.eax = reinterpret_cast<uintptr_t>(def);
            regs.edx = title_global<uint32_t>(0x0095f4d8);
            const uint32_t args[1] = {forward};
            title_call_at_regs(top, 0x002803d0, &regs, 1, args);  // CG_OffsetViewForward
        }
    }
    const float k0 = g(0x00949500);
    const float k1 = g(0x00947d60);
    const float k2 = g(0x009489c0);
    origin[0] = g(0x00972bc0) * k0 + origin[0];
    origin[1] = g(0x00972bc4) * k0 + origin[1];
    const float z = g(0x00972bc8) * k0 + origin[2];
    origin[2] = z;
    origin[0] = g(0x00972bcc) * k1 + origin[0];
    origin[1] = g(0x00972bd0) * k1 + origin[1];
    origin[2] = g(0x00972bd4) * k1 + z;
    origin[0] = g(0x00972bd8) * k2 + origin[0];
    origin[1] = g(0x00972bdc) * k2 + origin[1];
    origin[2] = g(0x00972be0) * k2 + origin[2];
    regs.eax = angles;
    title_call_at_regs(top, 0x00285af0, &regs, 0, nullptr);  // CG_CalculateWeaponAngles
    regs.eax = angles;
    regs.edx = hand + 0x14;
    const uint32_t axis_arg[1] = {hand + 0x14};
    title_call_at_regs(top, 0x002a45b0, &regs, 1, axis_arg);  // AnglesToAxisLeft2
    const uint32_t anim_args[2] = {ps_addr, rec};
    title_call_at_regs(top - 4, 0x002840e0, &regs, 2, anim_args);  // CG_UpdateViewWeaponAnim
    *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(hand + 4)) = 0xc;
    regs.eax = title_global<uint32_t>(title_global<uint32_t>(0x0095f534) + 4);
    regs.ecx = ps_addr;
    regs.edx = title_global<uint32_t>(0x0095f534);
    const uint32_t add_args[3] = {hand, draw_slot, scale_slot};
    title_call_at_regs(top - 0xc, 0x00288850, &regs, 3, add_args);  // CG_AddPlayerWeapon
}

namespace {

// MatrixMultiply (0x002956b0: b in EAX, out in EDX, cdecl a): out = a b, 3x3.
void matrix_multiply(const float* a, const float* b, float* out) {
    TitleRegs regs{};
    regs.eax = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(b));
    regs.edx = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(out));
    const uint32_t args[1] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(a))};
    cg_call_regs(0x002956b0, &regs, args, 1);
}

// MatrixTranspose (0x00295020) of src into dst.
void matrix_transpose(const float* src, float* dst) {
    uint32_t* const d = reinterpret_cast<uint32_t*>(dst);
    const uint32_t* const s = reinterpret_cast<const uint32_t*>(src);
    static const uint8_t kFrom[9] = {0, 3, 6, 1, 4, 7, 2, 5, 8};
    for (int i = 0; i < 9; ++i) d[i] = s[kFrom[i]];
}

void axis_to_angles(float* angles, const float* axis) {
    TitleRegs regs{};
    regs.eax = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(angles));
    regs.ecx = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(axis));
    cg_call_regs(0x002a27f0, &regs, nullptr, 0);  // AxisToAngles
}

}  // namespace

void XB_CDECL CG_CalcVehicleViewAngles() {
    // The view in the player's vehicle (entity handle 0x009729b8): its "tag_player" axis A
    // (G_DObjGetWorldTagMatrix after CG_CalcEntityLerpPositions); with flag 0x100000 the turn
    // since the last frame (M = 0x00972c44, the last A transposed; 0x00972c40 = not valid) is
    // carried into the view angles 0x009725ec (pitch only unless flag 0x200000; only while
    // 0x00972a34 == 1), cl_viewangles = those minus the delta angles, 0x009725f4 = minus the
    // roll of C A^T. Otherwise only M = A^T. Skipped in the tank's alternative camera
    // (0x009481e4 with 0x00972a38 == 2) unless the entity's +2 is 1.
    const uint32_t handle = title_global<uint32_t>(0x009729b8);
    const uint32_t index = handle & 0xfff;
    if (index >= 0x540) return;
    if ((handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return;
    uint8_t* const cent = title_global<uint8_t*>(0x0097a968 + index * 8);
    if (!cent) return;
    if (gi(0x009481e4) && gi(0x00972a38) == 2 && cent[2] != 1) return;
    CG_CalcEntityLerpPositions(cent);
    if (!(title_global<uint8_t>(0x0098ebe8) & 1)) {
        title_global<uint32_t>(0x0098ebe8) |= 1;
        title_global<uint32_t>(0x0098ebe4) = Com_HashKeyNoCase(0, &title_global<const char>(0x00838a54));  // "tag_player"
    }
    const DObj* const obj = *reinterpret_cast<const DObj* const*>(cent + 0x22c);
    if (!obj) return;
    const int32_t tag = DObj_GetTagIndex(obj, title_global<uint32_t>(0x0098ebe4));
    if (tag < 0) return;
    alignas(16) float tag_matrix[16];
    using TagMatrixFn = void XB_CDECL(uint8_t*, int32_t, float*);
    title_function<TagMatrixFn>(0x002e3780)(cent, tag, tag_matrix);  // G_DObjGetWorldTagMatrix
    float a[9];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) a[r * 3 + c] = tag_matrix[r * 4 + c];
    float* const m = &title_global<float>(0x00972c44);
    if (!(title_global<uint32_t>(0x00972610) & 0x100000) || !title_global<uint32_t>(0x009729b8)) {
        m[0] = a[0];
        m[1] = a[3];
        m[2] = a[6];
        m[3] = a[1];
        m[4] = a[4];
        m[5] = a[7];
        m[6] = a[2];
        m[7] = a[5];
        m[8] = a[8];
        return;
    }
    if (title_global<uint32_t>(0x00972c40)) {
        matrix_transpose(a, m);
        title_global<uint32_t>(0x00972c40) = 0;
    }
    float turn[9];
    matrix_multiply(m, a, turn);
    matrix_transpose(a, m);
    float c[9];
    title_function<void XB_REGS1 XB_CDECL(const float*, float*)>(0x002a45b0)(&title_global<float>(0x009725ec), c);  // AnglesToAxisLeft2
    float view[9];
    matrix_multiply(c, turn, view);
    float angles[3];
    axis_to_angles(angles, view);
    float pitch, yaw;
    if (gi(0x00972a34) == 1) {
        pitch = angles[0];
        title_global<float>(0x009725ec) = pitch;
        if (title_global<uint32_t>(0x00972610) & 0x200000) {
            yaw = angles[1];
            title_global<float>(0x009725f0) = yaw;
        } else {
            yaw = g(0x009725f0);
        }
    } else {
        pitch = g(0x009725ec);
        yaw = g(0x009725f0);
    }
    const float k = g(0x0083ebd4);
    float d0 = static_cast<float>(gi(0x009725a4));
    float d1 = static_cast<float>(gi(0x009725a8));
    float d2 = static_cast<float>(gi(0x009725ac));
    d0 = d0 * k;
    pitch = pitch - d0;
    float roll = g(0x009725f4);
    d1 = d1 * k;
    yaw = yaw - d1;
    d2 = d2 * k;
    roll = roll - d2;
    angles[0] = pitch;
    angles[1] = yaw;
    angles[2] = roll;
    title_global<float>(0x00961798) = pitch;
    title_global<float>(0x0096179c) = yaw;
    title_global<float>(0x009617a0) = roll;
    matrix_multiply(c, m, view);
    axis_to_angles(angles, view);
    title_global<float>(0x009725f4) = 0.0f - angles[2];
}

uint32_t XB_CDECL CG_CalcViewValues() {
    // Q3's CG_CalcViewValues: the refdef (0x00972b90, 0x18 words) cleared; a cube-map capture
    // (0x00971a8c) has its own. A scripted camera (0x00935538) takes its axis and origin from the
    // quaternion + position at 0x00960040 (through a fixed axis swap), else the vehicle views and
    // the predicted origin / view angles. The bob cycle (0x00973a80) and xy speed (0x00973a84;
    // 0x00972568 on the ground 500 ms after landing), the step offset (0x00972ad8.. fading out
    // over 0x0094b9c8 ms from 0x00972ad4), the linked view, then the third-person, tank or
    // first-person offsets; the axis from the angles and CG_CalcFov's result.
    for (uint32_t k = 0; k < 0x18; ++k) title_global<uint32_t>(0x00972b90 + k * 4) = 0;
    if (title_global<uint32_t>(0x00971a8c)) {
        CG_CalcCubemapViewValues();
        return 0;
    }
    if (title_global<uint8_t>(0x00935538)) {
        alignas(16) float r0[4], r1[4], r2[4], r3[4];
        const float one = g(0x0083eb20);
        const float minus_one = g(0x0083eb34);
        r3[0] = 0.0f; r3[1] = 0.0f; r3[2] = 0.0f; r3[3] = one;
        r2[0] = 0.0f; r2[1] = one; r2[2] = 0.0f; r2[3] = 0.0f;
        r1[0] = minus_one; r1[1] = 0.0f; r1[2] = 0.0f; r1[3] = 0.0f;
        r0[0] = 0.0f; r0[1] = 0.0f; r0[2] = minus_one; r0[3] = 0.0f;
        alignas(16) float m[16], t[16], product[16];
        Mat44_FromRows(m, r0, r1, r2, r3);
        QuatTrans_ToMat44(t, 0, &title_global<float>(0x00960040));
        Mat44_MultiplyInPlace(m, product, t);
        for (int k = 0; k < 3; ++k) {
            title_global<float>(0x00972bc0 + k * 4) = m[k];
            title_global<float>(0x00972bcc + k * 4) = m[4 + k];
            title_global<float>(0x00972bd8 + k * 4) = m[8 + k];
        }
        for (int k = 0; k < 3; ++k) title_global<float>(0x00972bb0 + k * 4) = m[12 + k];
        TitleRegs regs{};
        regs.eax = 0x00972bf0;
        regs.ecx = 0x00972bc0;
        cg_call_regs(0x002a27f0, &regs, nullptr, 0);  // AxisToAngles
    }
    CG_CalcVrect();
    const float two_pi = g(0x0083edac);
    float cycle = static_cast<float>(static_cast<int32_t>(title_global<uint32_t>(0x00972578) & 0xff)) / g(0x0083eb8c);
    cycle = cycle * two_pi + two_pi;
    title_global<float>(0x00973a80) = cycle;
    if (title_global<uint8_t>(0x0097257c) & 0x10) {
        if (gi(0x0097253c) - gi(0x009725c0) < 500)
            title_global<float>(0x00973a84) = 0.0f;
        else
            title_global<uint32_t>(0x00973a84) = title_global<uint32_t>(0x00972568);
    } else {
        const float x = g(0x00972560);
        const float y = g(0x00972564);
        title_global<float>(0x00973a84) = title_ci_sqrt(x * x + y * y);
    }
    CG_CalcVehicleViewAngles();
    CG_ApplyVehicleTurnToView();
    if (!title_global<uint8_t>(0x00935538)) {
        for (int k = 0; k < 3; ++k) {
            title_global<uint32_t>(0x00972bb0 + k * 4) = title_global<uint32_t>(0x00972550 + k * 4);
        }
        for (int k = 0; k < 3; ++k) {
            title_global<uint32_t>(0x00972bf0 + k * 4) = title_global<uint32_t>(0x009725ec + k * 4);
        }
    }
    const int32_t state = gi(0x00972574);
    if (state != 1 && state != 5) {
        const float duration = g(0x0094b9c8);
        if (duration > 0.0f) {
            const float dt = static_cast<float>(gi(0x0097253c) - gi(0x00972ad4));
            const float f = (duration - dt) / duration;
            if (f > 0.0f && g(0x0083eb20) > f) {
                for (int k = 0; k < 3; ++k)
                    title_global<float>(0x00972bb0 + k * 4) = g(0x00972ad8 + k * 4) * f + g(0x00972bb0 + k * 4);
            } else {
                title_global<int32_t>(0x00972ad4) = 0;
            }
        }
    }
    CG_CalcLinkedViewOrigin();
    if (!title_global<uint8_t>(0x00935538)) {
        if (gi(0x00972548))
            title_function<void XB_CDECL()>(0x002873e0)();  // CG_OffsetThirdPersonView
        else if (gi(0x009481e4) && gi(0x00972a38) == 2 && (title_global<uint32_t>(0x00972610) & 0x100000) &&
                 gi(0x009729b8))
            title_function<void XB_CDECL()>(0x00286da0)();  // CG_OffsetAltTankCamView
        else
            CG_OffsetFirstPersonView();
    }
    float right[3];
    uint32_t args[2] = {0x00972bf0, 0x00972bc0};
    AnglesToAxis(args, right, &title_global<float>(0x00972bd8));
    title_global<float>(0x00972bcc) = 0.0f - right[0];
    title_global<float>(0x00972bd0) = 0.0f - right[1];
    title_global<float>(0x00972bd4) = 0.0f - right[2];
    return CG_CalcFov();
}

}  // namespace game
