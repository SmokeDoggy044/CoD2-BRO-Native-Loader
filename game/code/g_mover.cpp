#include "g_mover.h"

#include <cstring>

#include "bg_misc.h"
#include "g_client.h"
#include "g_main.h"
#include "g_spawn.h"
#include "g_utils.h"
#include "g_scr_runtime.h"
#include "g_vehicle.h"
#include "qcommon.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "sv.h"
#include "title_data.h"
#include "xanim.h"

// 0x002545e0 (phys list): a script mover trajectory's next phase - EAX, ECX, EDX = vectors,
// XMM1 = the first phase time, cdecl (trajectory, speed, word).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL ent_mover_step(uint32_t eax, uint32_t ecx, uint32_t edx, float xmm1,
                                                           uint32_t trajectory, float speed, uint32_t word);
PLATFORM_ASM_ENTRY(ent_mover_step,
                   "pushl %ebx\n"
                   "movss 20(%esp), %xmm1\n"
                   "pushl 32(%esp)\n"
                   "pushl 32(%esp)\n"
                   "pushl 32(%esp)\n"
                   "movl 20(%esp), %eax\n"
                   "movl 24(%esp), %ecx\n"
                   "movl 28(%esp), %edx\n"
                   "movl $0x002545e0, %ebx\n"
                   "call *%ebx\n"
                   "addl $12, %esp\n"
                   "popl %ebx\n"
                   "ret")

namespace game {

namespace {

uint32_t& word_at(uint8_t* base, uint32_t offset) { return *reinterpret_cast<uint32_t*>(base + offset); }

template <typename T>
T& field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

uint8_t* pointer_at(uint8_t* base, uint32_t offset) {
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(word_at(base, offset)));
}

uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// A spawn key's hash, made once under `bit` of the guard word.
uint32_t cached_key(uint32_t guard, uint32_t bit, uint32_t slot, uint32_t text) {
    uint32_t value = title_global<uint32_t>(guard);
    if (!(value & bit)) {
        value |= bit;
        title_global<uint32_t>(guard) = value;
        title_global<uint32_t>(slot) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(static_cast<uintptr_t>(text)));
    }
    return title_global<uint32_t>(slot);
}


// The "light" / "color" spawn keys as a packed RGBA (+0xa8; alpha = light / 4), each channel
// clamped at 255 from above only, written when either key is present (InitMover*).
void set_mover_light(uint8_t* ent, uint32_t light_key, uint32_t color_key) {
    float light;
    const uint32_t has_light = G_SpawnFloat(light_key, 100.0f, &light);
    const float one = title_global<float>(0x0083eb20);
    float fallback[3] = {one, one, one};
    float color[3];
    const uint32_t has_color = G_SpawnVector(color_key, color, reinterpret_cast<const uint32_t*>(fallback));
    if (!has_light && !has_color) return;
    const float scale = title_global<float>(0x0083eb8c);
    int32_t r = static_cast<int32_t>(color[0] * scale);
    if (r > 0xff) r = 0xff;
    int32_t g = static_cast<int32_t>(color[1] * scale);
    if (g > 0xff) g = 0xff;
    int32_t b = static_cast<int32_t>(color[2] * scale);
    if (b > 0xff) b = 0xff;
    int32_t a = static_cast<int32_t>(light * title_global<float>(0x0083ec24));
    if (a > 0xff) a = 0xff;
    word_at(ent, 0xa8) = static_cast<uint32_t>((((a << 8 | b) << 8 | g) << 8) | r);
}

// The title CRT's __CIsqrt / __CIacos (argument and result in ST), stored as float.
float title_ci_float(uint32_t function, float x) {
    float result;
    __asm__ volatile("flds %1\n\tcall *%2\n\tfstps %0"
                     : "=m"(result)
                     : "m"(x), "r"(static_cast<uintptr_t>(function))
                     : "eax", "ecx", "edx", "st", "memory");
    return result;
}

const uint8_t* weapon_def(uint32_t weapon) {
    const uint32_t defs = title_global<uint32_t>(0x0095f4d8);  // bg_weaponDefs
    return reinterpret_cast<const uint8_t*>(
        static_cast<uintptr_t>(*reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(defs + weapon * 4))));
}

// |Q_fmod(a, b)| (the sign bit cleared, as FABS).
float angle_delta_abs(float a, float b) {
    float d = Q_fmod(a, b);
    uint32_t bits;
    std::memcpy(&bits, &d, sizeof bits);
    bits &= 0x7fffffffu;
    std::memcpy(&d, &bits, sizeof d);
    return d;
}

// The turret info's state byte (+1) changes: "turretstatechange" when it goes from or to 0.
void set_turret_state(uint8_t* ent, uint8_t state) {
    uint8_t* const info = pointer_at(ent, 0x24c);
    if ((info[1] != 0) != (state != 0)) Scr_Notify(ent, title_global<uint32_t>(0x0097a860));
    info[1] = state;
}

}  // namespace

PLATFORM_ASM_ENTRY(InitScriptMover_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(InitScriptMover) "addl $4, %esp\n"
                   "ret")

void XB_CDECL InitScriptMover(uint8_t* ent) {
    // The key hashes, made once (guard bits in 0x0098b84c).
    uint32_t guard = title_global<uint32_t>(0x0098b84c);
    if (!(guard & 1)) {
        guard |= 1;
        title_global<uint32_t>(0x0098b84c) = guard;
        title_global<uint32_t>(0x0098b848) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x0083b07c}));  // "light"
    }
    if (!(guard & 2)) {
        guard |= 2;
        title_global<uint32_t>(0x0098b84c) = guard;
        title_global<uint32_t>(0x0098b844) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x0083b074}));  // "color"
    }
    float light;
    const uint32_t has_light = G_SpawnFloat(title_global<uint32_t>(0x0098b848), 100.0f, &light);
    const float one = title_global<float>(0x0083eb20);
    float fallback[3] = {one, one, one};
    float color[3];
    const uint32_t has_color =
        G_SpawnVector(title_global<uint32_t>(0x0098b844), color, reinterpret_cast<const uint32_t*>(fallback));
    if (has_light || has_color) {
        // A packed RGBA (alpha = light / 4), each channel clamped at 255 from above only.
        const float scale = title_global<float>(0x0083eb8c);
        int32_t r = static_cast<int32_t>(color[0] * scale);
        if (r > 0xff) r = 0xff;
        int32_t g = static_cast<int32_t>(color[1] * scale);
        if (g > 0xff) g = 0xff;
        int32_t b = static_cast<int32_t>(color[2] * scale);
        if (b > 0xff) b = 0xff;
        int32_t a = static_cast<int32_t>(light * title_global<float>(0x0083ec24));
        if (a > 0xff) a = 0xff;
        word_at(ent, 0xa8) = static_cast<uint32_t>((((a << 8 | b) << 8 | g) << 8) | r);
    }
    word_at(ent, 0x330) = 2;
    word_at(ent, 0xd4) = 0x80;
    ent[0] = 7;
    word_at(ent, 0x20) = word_at(ent, 0x140);
    word_at(ent, 0x24) = word_at(ent, 0x144);
    word_at(ent, 0x28) = word_at(ent, 0x148);
    word_at(ent, 0x14) = 0;
    word_at(ent, 0x44) = word_at(ent, 0x150);
    word_at(ent, 0x48) = word_at(ent, 0x154);
    word_at(ent, 0x4c) = word_at(ent, 0x158);
    const uint32_t flags = word_at(ent, 0x2a4) | 0x8000;
    word_at(ent, 0x38) = 0;
    word_at(ent, 0x2a4) = flags;
}

void XB_CDECL Think_InitDoor(uint8_t* ent) {
    word_at(ent, 0x328) = title_global<uint32_t>(0x0098540c) + 100;
    if (word_at(ent, 0x2a4) & 0x10) return;  // a team slave
    if (word_at(ent, 0x29c))
        word_at(ent, 0x32c) = 0x19;
    else if (word_at(ent, 0x260) == title_global<uint32_t>(0x0097a6e0))  // "func_door_rotating"
        word_at(ent, 0x32c) = 0x1b;
    else
        word_at(ent, 0x32c) = (ent[0x2a0] & 8) ? 0x1a : 0x19;
    // The team members take the master's +0x3b4.
    uint8_t* member = ent;
    do {
        if (member != ent) word_at(member, 0x3b4) = word_at(ent, 0x3b4);
        member = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(word_at(member, 0x37c)));
    } while (member);
}

void XB_CDECL Artillery_Drop(uint8_t* ent) {
    word_at(ent, 0x328) = title_global<uint32_t>(0x0098540c) + 30000;
    word_at(ent, 0x32c) = 8;
    const float z = *reinterpret_cast<const float*>(ent + 0x148) + title_global<float>(0x0083eb9c);
    const float x = *reinterpret_cast<const float*>(ent + 0x140);
    const float y = *reinterpret_cast<const float*>(ent + 0x144);
    float* const base = reinterpret_cast<float*>(ent + 0x20);
    base[0] = x;
    base[1] = y;
    base[2] = z;
    float* const current = reinterpret_cast<float*>(ent + 0x140);
    current[0] = x;
    current[1] = y;
    current[2] = z;
    word_at(ent, 0x14) = 2;  // gravity
    word_at(ent, 0x18) = title_global<uint32_t>(0x0098540c) - 50;
}

void XB_REGS3 XB_CDECL Turret_UpdateAimState(uint32_t aiming, uint32_t, uint8_t* ent, const float* aim_point,
                                             const float* angles) {
    // Turret aim state (+1 of the info, +0x24c): 0 idle, 2 turning, 1 on target. Not aiming:
    // idle. Idle: turning once 250 ms passed since +0x1c or both deltas are under 5 degrees.
    // Turning: on target once pitch/yaw deltas are under 0.2 of the weapon's turn rates
    // (+0x828/+0x82c). Then +2 bit 0x40 and the aim point (+0x10).
    uint8_t* const info = pointer_at(ent, 0x24c);
    if (!aiming) {
        set_turret_state(ent, 0);
    } else {
        bool turning = false;
        if (info[1] == 0) {
            bool start = true;
            if (title_global<int32_t>(0x0098540c) < field<int32_t>(info, 0x1c) + 250) {
                const float five = title_global<float>(0x0083ece8);
                if (platform_x87_greater_equal(
                        angle_delta_abs(field<float>(ent, 0x98) + field<float>(ent, 0x90), angles[0]), five) ||
                    platform_x87_greater_equal(angle_delta_abs(field<float>(ent, 0x94), angles[1]), five))
                    start = false;
            }
            if (start) {
                set_turret_state(ent, 2);
                turning = true;
            }
        } else if (info[1] == 2) {
            turning = true;
        }
        if (turning) {
            const uint8_t* const def = weapon_def(ent[3]);
            const float fifth = title_global<float>(0x0083eca0);  // 0.2
            const float pitch = angle_delta_abs(field<float>(ent, 0x98) + field<float>(ent, 0x90), angles[0]);
            if (platform_x87_greater(platform_x87_mul(*reinterpret_cast<const float*>(def + 0x828), fifth), pitch)) {
                const float yaw = angle_delta_abs(field<float>(ent, 0x94), angles[1]);
                if (platform_x87_greater(platform_x87_mul(*reinterpret_cast<const float*>(def + 0x82c), fifth), yaw))
                    set_turret_state(ent, 1);
            }
        }
    }
    info[2] |= 0x40;
    const uint32_t* const p = reinterpret_cast<const uint32_t*>(aim_point);
    field<uint32_t>(info, 0x10) = p[0];
    field<uint32_t>(info, 0x14) = p[1];
    field<uint32_t>(info, 0x18) = p[2];
}

PLATFORM_ASM_ENTRY(Turret_UpdateAimWithJitter_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(Turret_UpdateAimWithJitter) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Turret_UpdateAimWithJitter(uint8_t* ent) {
    // Every 4.5..7.5 s a new random aim offset (+0x68: pitch -6..5, yaw -20..19), then the
    // turret turns toward it (AI turn rates).
    uint8_t* const info = pointer_at(ent, 0x24c);
    if (title_global<int32_t>(0x0098540c) > field<int32_t>(info, 0x1c) + 0x1194) {
        const int32_t r = title_function<int32_t XB_CDECL()>(0x001efcc0)();  // _rand
        field<int32_t>(info, 0x1c) = title_global<int32_t>(0x0098540c) - r % 3000;
        field<float>(info, 0x68) = static_cast<float>(r % 12) - title_global<float>(0x0083ecd0);  // 6
        field<float>(info, 0x6c) = static_cast<float>(r % 40) - title_global<float>(0x0083eb5c);  // 20
    }
    uint32_t args[2] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ent)),
                        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(info + 0x68))};
    Turret_UpdateTargetAngles(args, 1, ent, reinterpret_cast<const float*>(info + 0x68));
}

void XB_REGS1 XB_CDECL Turret_SetIdle(uint8_t* ent) {
    uint8_t* const info = pointer_at(ent, 0x24c);
    info[2] &= 0xb7;
    field<uint32_t>(info, 0xc) = 0;
    set_turret_state(ent, 0);
}

PLATFORM_ASM_ENTRY(Door_ChooseRotateDirection_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Door_ChooseRotateDirection) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL Door_ChooseRotateDirection(uint8_t* door, uint8_t* activator) {
    // func_door_rotating without spawn flag 0x20: the door's facing from its centre (bounds
    // +0x100/+0x110) turned by the rotation (+0x2f4) on its axis (yaw +0x394, else pitch +0x390,
    // else roll +0x398); 1 when the activator stands in front of it (dot >= 0), else 0.
    if (word_at(door, 0x260) != title_global<uint32_t>(0x0097a6e0) || (door[0x2a0] & 0x20)) return 0;
    const float half = title_global<float>(0x0083eb24);
    float center[3];
    float dir[3];
    for (uint32_t i = 0; i < 3; ++i) center[i] = (field<float>(door, 0x110 + i * 4) + field<float>(door, 0x100 + i * 4)) * half;
    for (uint32_t i = 0; i < 3; ++i) dir[i] = center[i] - field<float>(door, 0x140 + i * 4);
    float angles[3];
    vectoangles(dir, angles);
    if (field<float>(door, 0x394) != 0.0f)
        angles[1] = field<float>(door, 0x2f4) + angles[1];
    else if (field<float>(door, 0x390) != 0.0f)
        angles[0] = field<float>(door, 0x2f4) + angles[0];
    else if (field<float>(door, 0x398) != 0.0f)
        angles[2] = field<float>(door, 0x2f4) + angles[2];
    float forward[3];
    AnglesToForward(angles, forward);
    float to[3];
    for (uint32_t i = 0; i < 3; ++i) to[i] = field<float>(activator, 0x140 + i * 4) - center[i];
    VectorNormalize(to);
    float dot = forward[2] * to[2] + forward[1] * to[1];
    dot = dot + forward[0] * to[0];
    return dot >= title_global<float>(0x0083eafc) ? 1 : 0;
}

PLATFORM_ASM_ENTRY(G_UpdateScriptedAnimEnd_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(G_UpdateScriptedAnimEnd) "addl $4, %esp\n"
                   "ret")

void XB_CDECL G_UpdateScriptedAnimEnd(uint8_t* ent) {
    // The scripted anim record (+0x3d4): freed without an anim tree (actors: the actor's tree,
    // made on demand from 0x00987a90) or anim (+0x40); skips its first frame (+0x48); once the
    // anim has ended: flag 0x1000000 (+0x2a4) cleared, the goal weights set back, anim cleared.
    uint8_t* const info = pointer_at(ent, 0x3d4);
    if (!info) return;
    uint8_t* tree;
    if (ent[0] != 0xb) {
        tree = pointer_at(ent, 0x254);
    } else {
        uint8_t* const actor = pointer_at(ent, 0x240);
        uint8_t* const owner = pointer_at(actor, 0);
        if (!word_at(owner, 0x254))
            word_at(pointer_at(actor, 0), 0x254) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(
                AnimTree_Create(owner, title_global<uint8_t*>(0x00987a90))));
        tree = pointer_at(pointer_at(actor, 0), 0x254);
    }
    if (!tree || !field<uint16_t>(info, 0x40)) {
        Z_Free(info);
        word_at(ent, 0x3d4) = 0;
        return;
    }
    if (!word_at(info, 0x48)) {
        word_at(info, 0x48) = 1;
        return;
    }
    if (!AnimTree_HasAnimEnded(0, 0, tree, word_at(info, 0x40))) return;
    word_at(ent, 0x2a4) &= 0xfeffffffu;
    AnimTree_SetGoalWeight(field<uint16_t>(info, 0x40), tree, 0x3f800000, 0, 0x3f800000, 0, 0, 0);
    AnimTree_SetGoalWeight(field<uint16_t>(info, 0x40), tree, 0, 0, 0x3f800000, 0, 0, 0);
    field<uint16_t>(info, 0x40) = 0;
    field<uint16_t>(info, 0x42) = 0;
}

void XB_CDECL SP_script_origin(uint8_t* ent) {
    // A script mover without a brush (+0x134 = 0), linked; flag 0x80 (+0x10) with a model
    // (+0xa8), else +0xd4 bit 0.
    InitScriptMover(ent);
    word_at(ent, 0x134) = 0;
    SV_LinkEntity(ent);
    if (word_at(ent, 0xa8))
        word_at(ent, 0x10) |= 0x80;
    else
        word_at(ent, 0xd4) |= 1;
}

void XB_CDECL SP_script_brushmodel(uint8_t* ent) {
    // The brush model, a script mover with contents 1 (+0x134), linked; spawn flag 1 sets 0x3000
    // in +0x2a4.
    SV_SetBrushModel(ent);
    InitScriptMover(ent);
    word_at(ent, 0x134) = 1;
    SV_LinkEntity(ent);
    if (ent[0x2a0] & 1) word_at(ent, 0x2a4) |= 0x3000;
}

PLATFORM_ASM_ENTRY(Turret_IsPlayerWithinArc_entry,
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Turret_IsPlayerWithinArc) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL Turret_IsPlayerWithinArc(const uint8_t* turret, const uint8_t* player) {
    // 1 when the player stands within the turret's yaw arc: the arc's half width
    // ((|right| + |left|) / 2 of the turret info +0x30 / +0x38) against the angle between the
    // arc's centre direction (yaw +0x154 plus +0x30 plus the half width, as a short angle)
    // and the flat direction to the player. Both normalized with the title's x87 __CIsqrt,
    // the angle from __CIacos clamped to pi.
    const uint8_t* const info = *reinterpret_cast<uint8_t* const*>(turret + 0x24c);
    const float right = *reinterpret_cast<const float*>(info + 0x30);
    const float base = *reinterpret_cast<const float*>(turret + 0x154) + right;
    uint32_t bits;
    std::memcpy(&bits, info + 0x38, sizeof bits);
    bits &= 0x7fffffffu;
    float left_abs;
    std::memcpy(&left_abs, &bits, sizeof bits);
    std::memcpy(&bits, &right, sizeof bits);
    bits &= 0x7fffffffu;
    float right_abs;
    std::memcpy(&right_abs, &bits, sizeof bits);
    const float half_arc = (left_abs + right_abs) * title_global<float>(0x0083eb24);
    float yaw = half_arc + base;
    yaw = static_cast<float>(static_cast<int32_t>(yaw * title_global<float>(0x0083ebd8)) & 0xffff) *
          title_global<float>(0x0083ebd4);
    if (yaw > title_global<float>(0x0083eb60)) yaw = yaw - title_global<float>(0x0083ebcc);
    const float radians = platform_x87_mul(yaw, title_global<float>(0x0083edd8));
    float sine;
    float cosine;
    FastSinCos(&sine, 0, &cosine, radians);
    const float zero = 0.0f;
    float aim[3] = {cosine, sine, zero};
    const float zero_sq = zero * zero;
    const float aim_sq = sine * sine + cosine * cosine + zero_sq;
    const float aim_len = title_ci_float(0x0033acf4, aim_sq);  // __CIsqrt
    if (aim_len != 0.0f) {
        const float inv = title_global<float>(0x0083eb20) / aim_len;
        aim[0] = inv * cosine;
        aim[1] = inv * sine;
        aim[2] = inv * title_global<float>(0x0083eafc);
    }
    const float dy = *reinterpret_cast<const float*>(turret + 0x144) - *reinterpret_cast<const float*>(player + 0x144);
    const float dx = *reinterpret_cast<const float*>(turret + 0x140) - *reinterpret_cast<const float*>(player + 0x140);
    float to[3] = {dx, dy, 0.0f};
    const float to_sq = dy * dy + dx * dx + zero_sq;
    const float to_len = title_ci_float(0x0033acf4, to_sq);
    if (to_len != 0.0f) {
        const float inv = title_global<float>(0x0083eb20) / to_len;
        to[0] = inv * dx;
        to[1] = inv * dy;
        to[2] = inv * title_global<float>(0x0083eafc);
    }
    // The dot product on the x87, stored as float.
    const float dot = static_cast<float>(static_cast<long double>(aim[2]) * to[2] + static_cast<long double>(aim[1]) * to[1] +
                                         static_cast<long double>(aim[0]) * to[0]);
    float angle = title_ci_float(0x001f0ff4, dot);  // __CIacos
    const float pi = title_global<float>(0x0083ed00);
    if (platform_x87_greater(angle, pi) || title_global<float>(0x0083ed48) > angle) angle = pi;
    const float degrees = angle * title_global<float>(0x0083eb60) / pi;
    return degrees > half_arc ? 0 : 1;
}

PLATFORM_ASM_ENTRY(G_SimulateGrenadeBounce_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(G_SimulateGrenadeBounce) "addl $16, %esp\n"
                   "ret")

void XB_CDECL G_SimulateGrenadeBounce(uint8_t* trajectory, const uint8_t* trace, const uint8_t* ent, int32_t time) {
    // G_BounceMissile for the grenade path preview: the velocity at `time` reflected off the
    // trace plane (normal trace +0x10); bouncing entities (+0x10 flag 0x2000000) halve it and
    // stop on a floor (normal z > 0.7) below 20 units/s (x87 length): stationary at the end
    // point (trace +0). Else the base moves off the plane by a tenth of the normal (z not up).
    alignas(16) float position[3] = {};
    alignas(16) float velocity[3] = {};
    {
        const TitleRegs regs{static_cast<uint32_t>(time), 0, address_of(position), 0, 0, address_of(trajectory)};
        g_ent_call(0x002d62c0, &regs, 0, nullptr);  // BG_EvaluateTrajectory
    }
    BG_EvaluateTrajectoryDelta(0, time, velocity, trajectory);
    const float* const normal = reinterpret_cast<const float*>(trace + 0x10);
    float dot = normal[0] * velocity[0] + velocity[2] * normal[2];
    dot = dot + velocity[1] * normal[1];
    dot = dot * title_global<float>(0x0083ec5c);  // -2
    float vx = dot * normal[0] + velocity[0];
    field<float>(trajectory, 0x18) = vx;
    field<float>(trajectory, 0x1c) = dot * normal[1] + velocity[1];
    field<float>(trajectory, 0x20) = dot * normal[2] + velocity[2];
    if (word_at(const_cast<uint8_t*>(ent), 0x10) & 0x2000000) {
        const float half = title_global<float>(0x0083eb24);
        const float x = vx * half;
        field<float>(trajectory, 0x18) = x;
        const float y = field<float>(trajectory, 0x1c) * half;
        field<float>(trajectory, 0x1c) = y;
        const float z = field<float>(trajectory, 0x20) * half;
        field<float>(trajectory, 0x20) = z;
        if (normal[2] > title_global<float>(0x0083ed28) &&
            platform_x87_greater_than_dist3(title_global<float>(0x0083eb5c), x, 0.0f, y, 0.0f, z, 0.0f)) {
            word_at(trajectory, 0xc) = *reinterpret_cast<const uint32_t*>(trace);
            word_at(trajectory, 0x10) = *reinterpret_cast<const uint32_t*>(trace + 4);
            word_at(trajectory, 0x14) = *reinterpret_cast<const uint32_t*>(trace + 8);
            word_at(trajectory, 0) = 0;
            word_at(trajectory, 4) = 0;
            word_at(trajectory, 8) = 0;
            field<float>(trajectory, 0x20) = 0.0f;
            field<float>(trajectory, 0x1c) = 0.0f;
            field<float>(trajectory, 0x18) = 0.0f;
            return;
        }
    }
    const float tenth = title_global<float>(0x0083ebf8);
    const float nx = normal[0] * tenth;
    const float ny = normal[1] * tenth;
    float nz = normal[2] * tenth;
    if (nz > 0.0f) nz = 0.0f;
    const float bx = position[0] + nx;
    field<float>(trajectory, 0x10) = position[1] + ny;
    field<int32_t>(trajectory, 4) = time;
    field<float>(trajectory, 0xc) = bx;
    field<float>(trajectory, 0x14) = position[2] + nz;
}

void XB_CDECL Reached_ScriptMover(uint8_t* ent) {
    // The script mover's two trajectories once their time is up: position (+0x14, base
    // +0x140) and angles (+0x38, base +0x150). Each steps to its next phase (0x002545e0 with
    // the accel/decel times and speeds), is evaluated at level.time and linked; a finished
    // move notifies "movedone", a finished rotation snaps the angles to shorts (pitch and
    // roll wrapped to -180..180) and notifies "rotatedone".
    const auto evaluate = [](uint8_t* trajectory, uint8_t* result) {
        const TitleRegs regs{title_global<uint32_t>(0x0098540c), 0, address_of(result), 0, 0, address_of(trajectory)};
        g_ent_call(0x002d62c0, &regs, 0, nullptr);  // BG_EvaluateTrajectory
    };
    if (word_at(ent, 0x14) &&
        field<int32_t>(ent, 0x1c) + field<int32_t>(ent, 0x18) <= title_global<int32_t>(0x0098540c)) {
        const uint32_t done = ent_mover_step(address_of(ent + 0x2d0), address_of(ent + 0x2c0), address_of(ent + 0x2e0),
                                             field<float>(ent, 0x384), address_of(ent + 0x14), field<float>(ent, 0x2fc),
                                             word_at(ent, 0x38c));
        evaluate(ent + 0x14, ent + 0x140);
        SV_LinkEntity(ent);
        if (done) Scr_Notify(ent, title_global<uint32_t>(0x0097a72c));  // movedone
    }
    if (!word_at(ent, 0x38) ||
        field<int32_t>(ent, 0x40) + field<int32_t>(ent, 0x3c) > title_global<int32_t>(0x0098540c))
        return;
    const uint32_t done = ent_mover_step(address_of(ent + 0x390), address_of(ent + 0x310), address_of(ent + 0x3a0),
                                         field<float>(ent, 0x2f4), address_of(ent + 0x38), field<float>(ent, 0x300),
                                         word_at(ent, 0x388));
    evaluate(ent + 0x38, ent + 0x150);
    SV_LinkEntity(ent);
    if (!done) return;
    const float to_short = title_global<float>(0x0083ebd8);
    const float to_angle = title_global<float>(0x0083ebd4);
    const float half = title_global<float>(0x0083eb60);
    const float full = title_global<float>(0x0083ebcc);
    float pitch = static_cast<float>(static_cast<int32_t>(field<float>(ent, 0x150) * to_short) & 0xffff) * to_angle;
    if (pitch > half) pitch = pitch - full;
    field<float>(ent, 0x150) = pitch;
    field<float>(ent, 0x154) = static_cast<float>(static_cast<int32_t>(field<float>(ent, 0x154) * to_short) & 0xffff) * to_angle;
    float roll = static_cast<float>(static_cast<int32_t>(field<float>(ent, 0x158) * to_short) & 0xffff) * to_angle;
    if (roll > half) roll = roll - full;
    field<float>(ent, 0x158) = roll;
    Scr_Notify(ent, title_global<uint32_t>(0x0097a77c));  // rotatedone
}

void XB_CDECL Use_Func_Rotate(uint8_t* ent, uint8_t*, uint8_t*) {
    // The rotation speed (+0x2fc) into the angular delta's axis by spawn flag (4: +0x58,
    // 8: +0x50, else +0x54); spawn flag 2 clears +0x2a4 bit 0x10; linked.
    const uint32_t flags = word_at(ent, 0x2a0);
    if (flags & 4)
        word_at(ent, 0x58) = word_at(ent, 0x2fc);
    else if (flags & 8)
        word_at(ent, 0x50) = word_at(ent, 0x2fc);
    else
        word_at(ent, 0x54) = word_at(ent, 0x2fc);
    if (flags & 2) word_at(ent, 0x2a4) &= ~0x10u;
    SV_LinkEntity(ent);
}

void XB_CDECL Use_Static(uint8_t* ent, uint8_t*, uint8_t*) {
    // Toggles the link (+0xd0).
    if (word_at(ent, 0xd0)) {
        word_at(ent, 0xd0) = 0;
        SV_UnlinkEntity(ent + 0xd0);
        return;
    }
    SV_LinkEntity(ent);
}

PLATFORM_ASM_ENTRY(InitMoverRotate_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(InitMoverRotate) "addl $4, %esp\n"
                   "ret")

void XB_CDECL InitMoverRotate(uint8_t* ent) {
    // As InitScriptMover's light/colour (key hashes cached under guard 0x0098e440), then a
    // rotating mover: +0x33c = 5, +0x330 = 1 unless spawn flag 0x40, type 4, linked; speed
    // (+0x2fc, 0 -> 100) as the duration (+0x40, at least 1) in +0x320 / +0x324.
    uint32_t guard = title_global<uint32_t>(0x0098e440);
    if (!(guard & 1)) {
        guard |= 1;
        title_global<uint32_t>(0x0098e440) = guard;
        title_global<uint32_t>(0x0098e43c) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x0083b07c}));  // "light"
    }
    if (!(guard & 2)) {
        guard |= 2;
        title_global<uint32_t>(0x0098e440) = guard;
        title_global<uint32_t>(0x0098e438) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x0083b074}));  // "color"
    }
    set_mover_light(ent, title_global<uint32_t>(0x0098e43c), title_global<uint32_t>(0x0098e438));
    const bool no_flag = !(ent[0x2a0] & 0x40);
    word_at(ent, 0x33c) = 5;
    if (no_flag) word_at(ent, 0x330) = 1;
    ent[0x298] = 7;
    word_at(ent, 0xd4) = 0x80;
    ent[0] = 4;
    SV_LinkEntity(ent);
    word_at(ent, 0x14) = 0;
    word_at(ent, 0x20) = word_at(ent, 0x140);
    word_at(ent, 0x24) = word_at(ent, 0x144);
    word_at(ent, 0x28) = word_at(ent, 0x148);
    if (field<float>(ent, 0x2fc) == title_global<float>(0x0083eafc)) field<float>(ent, 0x2fc) = title_global<float>(0x0083eb90);
    const int32_t duration = static_cast<int32_t>(field<float>(ent, 0x2fc));
    field<int32_t>(ent, 0x40) = duration;
    if (duration <= 0) field<int32_t>(ent, 0x40) = 1;
    const uint32_t value = word_at(ent, 0x40);
    word_at(ent, 0x324) = value;
    word_at(ent, 0x320) = value;
}

PLATFORM_ASM_ENTRY(InitMover_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(InitMover) "addl $4, %esp\n"
                   "ret")

void XB_CDECL InitMover(uint8_t* ent) {
    // Q3's InitMover: the "noise" spawn var as a sound configstring index (ent byte 1), the
    // light/colour (guard 0x0098e450), func_rotating's move state (+0x33c 7, +0x330 0; others
    // 5 / 1), type 4 at pos1 (+0x2c0), linked; the duration (+0x1c, at least 1) from the
    // pos1-pos2 distance (x87) * 1000 / speed (+0x2fc, 0 -> 100) into +0x320 / +0x324, the
    // latter from the second speed (+0x300) when that is set.
    uint32_t guard = title_global<uint32_t>(0x0098e450);
    uint32_t noise;
    if (!(guard & 1)) {
        guard |= 1;
        title_global<uint32_t>(0x0098e450) = guard;
        noise = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x0083b640}));  // "noise"
        title_global<uint32_t>(0x0098e44c) = noise;
    } else {
        noise = title_global<uint32_t>(0x0098e44c);
    }
    const int32_t count = title_global<int32_t>(0x00985420);  // level.numSpawnVars
    for (int32_t i = 0; i < count; ++i) {
        if (noise != title_global<uint32_t>(0x00985424 + i * 8)) continue;
        const TitleRegs regs{0, 0x40, 0, 0, 0, 0};
        const uint32_t args[4] = {title_global<uint32_t>(0x00985428 + i * 8), 0xa0, 1, 0};
        ent[1] = static_cast<uint8_t>(g_ent_call(0x002c39a0, &regs, 4, args));  // G_FindConfigstringIndex (EBX 0x40)
        guard = title_global<uint32_t>(0x0098e450);
        break;
    }
    if (!(guard & 2)) {
        guard |= 2;
        title_global<uint32_t>(0x0098e450) = guard;
        title_global<uint32_t>(0x0098e448) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x0083b07c}));  // "light"
    }
    if (!(guard & 4)) {
        guard |= 4;
        title_global<uint32_t>(0x0098e450) = guard;
        title_global<uint32_t>(0x0098e444) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x0083b074}));  // "color"
    }
    set_mover_light(ent, title_global<uint32_t>(0x0098e448), title_global<uint32_t>(0x0098e444));
    if (word_at(ent, 0x260) == title_global<uint32_t>(0x0097a6e4)) {  // func_rotating
        word_at(ent, 0x33c) = 7;
        word_at(ent, 0x330) = 0;
    } else {
        word_at(ent, 0x33c) = 5;
        word_at(ent, 0x330) = 1;
    }
    ent[0x298] = 0;
    word_at(ent, 0xd4) = 0x80;
    ent[0] = 4;
    word_at(ent, 0x140) = word_at(ent, 0x2c0);
    word_at(ent, 0x144) = word_at(ent, 0x2c4);
    word_at(ent, 0x148) = word_at(ent, 0x2c8);
    SV_LinkEntity(ent);
    word_at(ent, 0x14) = 0;
    word_at(ent, 0x20) = word_at(ent, 0x2c0);
    word_at(ent, 0x24) = word_at(ent, 0x2c4);
    word_at(ent, 0x28) = word_at(ent, 0x2c8);
    const long double dx = static_cast<long double>(field<float>(ent, 0x2d0)) - field<float>(ent, 0x2c0);
    const long double dy = static_cast<long double>(field<float>(ent, 0x2d4)) - field<float>(ent, 0x2c4);
    const long double dz = static_cast<long double>(field<float>(ent, 0x2d8)) - field<float>(ent, 0x2c8);
    const float distance = static_cast<float>(platform_x87_sqrt_ext(dz * dz + dy * dy + dx * dx));
    if (field<float>(ent, 0x2fc) == 0.0f) field<float>(ent, 0x2fc) = title_global<float>(0x0083eb90);
    const float scaled = distance * title_global<float>(0x0083eb9c);  // 1000
    const int32_t duration = static_cast<int32_t>(scaled / field<float>(ent, 0x2fc));
    field<int32_t>(ent, 0x1c) = duration;
    if (duration <= 0) field<int32_t>(ent, 0x1c) = 1;
    const uint32_t value = word_at(ent, 0x1c);
    const float speed2 = field<float>(ent, 0x300);
    word_at(ent, 0x320) = value;
    word_at(ent, 0x324) = value;
    if (speed2 != 0.0f) {
        const int32_t duration2 = static_cast<int32_t>(scaled / speed2);
        field<int32_t>(ent, 0x324) = duration2;
        if (duration2 <= 0) field<int32_t>(ent, 0x324) = 1;
    }
}

void XB_CDECL Use_DropNamedItem(uint8_t* ent, uint8_t*, uint8_t*) {
    // Think 0x11 next frame, linked.
    word_at(ent, 0x32c) = 0x11;
    word_at(ent, 0x328) = title_global<uint32_t>(0x0098540c) + 1;
    SV_LinkEntity(ent);
}

PLATFORM_ASM_ENTRY(Turret_PlacePlayerBehind_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Turret_PlacePlayerBehind) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Turret_PlacePlayerBehind(uint8_t* player, uint8_t* turret) {
    // The player 34 units behind the turret along its aim (angles +0x150 plus the delta +0x90,
    // pitch and yaw), keeping its own height: unlinked, the player state's origin set, the
    // entity state rebuilt from it, the origin copied back, linked.
    float yaw_sin;
    float yaw_cos;
    float pitch_sin;
    float pitch_cos;
    const float pitch = field<float>(turret, 0x150) + field<float>(turret, 0x90);
    const float yaw = (field<float>(turret, 0x154) + field<float>(turret, 0x94)) * title_global<float>(0x0083edd8);
    FastSinCos(&yaw_sin, 0, &yaw_cos, yaw);
    FastSinCos(&pitch_sin, 0, &pitch_cos, pitch * title_global<float>(0x0083edd8));
    const float distance = title_global<float>(0x00830660);  // 34
    const float x = field<float>(turret, 0x140) - pitch_cos * yaw_cos * distance;
    const float y = field<float>(turret, 0x144) - pitch_cos * yaw_sin * distance;
    const uint32_t z = word_at(player, 0x148);
    word_at(player, 0xd0) = 0;
    SV_UnlinkEntity(player + 0xd0);
    field<float>(pointer_at(player, 0x23c), 0) = x;
    field<float>(pointer_at(player, 0x23c), 4) = y;
    word_at(pointer_at(player, 0x23c), 8) = z;
    BG_PlayerStateToEntityState(player, 0, pointer_at(player, 0x23c));
    word_at(player, 0x140) = word_at(pointer_at(player, 0x23c), 0);
    word_at(player, 0x144) = word_at(pointer_at(player, 0x23c), 4);
    word_at(player, 0x148) = word_at(pointer_at(player, 0x23c), 8);
    SV_LinkEntity(player);
}

void XB_CDECL Use_ToggleLink(uint8_t* ent, uint8_t*, uint8_t*) {
    // As Use_Static; linking also clears byte +0x297.
    if (word_at(ent, 0xd0)) {
        word_at(ent, 0xd0) = 0;
        SV_UnlinkEntity(ent + 0xd0);
        return;
    }
    ent[0x297] = 0;
    SV_LinkEntity(ent);
}

void XB_CDECL RespawnItem(uint8_t* ent) {
    // Q3's RespawnItem: with a team (+0x2f8 string) a random member of the team master's
    // chain (+0x380, next +0x37c); then solid (contents 0x407c0008), visible, linked, no think.
    const uint32_t team = word_at(ent, 0x2f8);
    if (team && team + 6 && *reinterpret_cast<const char*>(static_cast<uintptr_t>(team + 6))) {
        if (!word_at(ent, 0x380))
            title_function<int32_t XB_CDECL(const char*)>(0x00248af0)(
                reinterpret_cast<const char*>(uintptr_t{0x0083b6f0}));  // G_Error "RespawnItem: bad teammaster"
        uint8_t* const master = pointer_at(ent, 0x380);
        int32_t count = 0;
        for (uint8_t* e = master; e; e = pointer_at(e, 0x37c)) ++count;
        int32_t choice = title_function<int32_t XB_CDECL()>(0x001efcc0)() % count;  // _rand
        ent = master;
        for (; choice > 0; --choice) ent = pointer_at(ent, 0x37c);
    }
    const uint32_t flags = word_at(ent, 0x2a4) & 0xfffffbffu;
    const uint32_t svflags = word_at(ent, 0xd4) & 0xfffffffeu;
    word_at(ent, 0x134) = 0x407c0008;
    word_at(ent, 0x2a4) = flags;
    word_at(ent, 0xd4) = svflags;
    SV_LinkEntity(ent);
    word_at(ent, 0x328) = 0;
}

void XB_CDECL SP_func_pendulum(uint8_t* ent) {
    // Q3's SP_func_pendulum: "speed" (30), "dmg" (+0x358, 2), "phase"; the period from the
    // length (|mins z|, at least 8) and gravity (0x0094d450) on the x87: 1000 / (sqrt(g /
    // (3 * length)) / 2pi) ms (_ftol) for both trajectories; InitMover, pos base = origin,
    // apos a sine swing (+0x38 = 4) from the angles, roll speed. Keys cached under 0x0098e920.
    uint32_t guard = title_global<uint32_t>(0x0098e920);
    if (!(guard & 1)) {
        guard |= 1;
        title_global<uint32_t>(0x0098e920) = guard;
        title_global<uint32_t>(0x0098e91c) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x00833a44}));  // "speed"
    }
    if (!(guard & 2)) {
        guard |= 2;
        title_global<uint32_t>(0x0098e920) = guard;
        title_global<uint32_t>(0x0098e918) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x0083b060}));  // "dmg"
    }
    if (!(guard & 4)) {
        guard |= 4;
        title_global<uint32_t>(0x0098e920) = guard;
        title_global<uint32_t>(0x0098e914) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x0083b050}));  // "phase"
    }
    float speed;
    float phase;
    G_SpawnFloat(title_global<uint32_t>(0x0098e91c), 30.0f, &speed);
    G_SpawnInt(title_global<uint32_t>(0x0098e918), &field<int32_t>(ent, 0x358), 2);
    G_SpawnFloat(title_global<uint32_t>(0x0098e914), 0.0f, &phase);
    SV_SetBrushModel(ent);
    uint32_t bits = word_at(ent, 0xe8) & 0x7fffffffu;  // FABS
    float length;
    std::memcpy(&length, &bits, sizeof length);
    if (platform_x87_greater(title_global<float>(0x0083ed78), length)) length = title_global<float>(0x0083ed78);  // 8
    long double frequency = static_cast<long double>(length) * title_global<float>(0x0083ec9c);  // 3
    frequency = static_cast<long double>(title_global<float>(0x0094d450)) / frequency;            // gravity
    frequency = platform_x87_sqrt_ext(frequency) * title_global<float>(0x0083f118);              // 1 / 2pi
    const int32_t duration =
        static_cast<int32_t>(static_cast<int64_t>(static_cast<long double>(title_global<float>(0x0083eb9c)) / frequency));
    field<int32_t>(ent, 0x1c) = duration;
    InitMover(ent);
    word_at(ent, 0x20) = word_at(ent, 0x140);
    word_at(ent, 0x28) = word_at(ent, 0x148);
    word_at(ent, 0x24) = word_at(ent, 0x144);
    word_at(ent, 0x44) = word_at(ent, 0x150);
    field<int32_t>(ent, 0x40) = duration;
    word_at(ent, 0x48) = word_at(ent, 0x154);
    word_at(ent, 0x4c) = word_at(ent, 0x158);
    field<int32_t>(ent, 0x3c) = static_cast<int32_t>(static_cast<float>(duration) * phase);
    word_at(ent, 0x38) = 4;
    field<float>(ent, 0x58) = speed;
}

void XB_CDECL SP_func_bobbing(uint8_t* ent) {
    // Q3's SP_func_bobbing: "speed" (+0x2fc, default 4), "height" (32), "dmg" (+0x358, 2),
    // "phase"; brush model, InitMover, a sine trajectory (+0x14 = 4) of speed * 1000 ms
    // starting at duration * phase, the height on the spawn flag's axis (1: x, 2: y, else z).
    // Keys cached under 0x0098e934.
    uint32_t guard = title_global<uint32_t>(0x0098e934);
    if (!(guard & 1)) {
        guard |= 1;
        title_global<uint32_t>(0x0098e934) = guard;
        title_global<uint32_t>(0x0098e930) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x00833a44}));  // "speed"
    }
    if (!(guard & 2)) {
        guard |= 2;
        title_global<uint32_t>(0x0098e934) = guard;
        title_global<uint32_t>(0x0098e92c) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x0083b058}));  // "height"
    }
    if (!(guard & 4)) {
        guard |= 4;
        title_global<uint32_t>(0x0098e934) = guard;
        title_global<uint32_t>(0x0098e928) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x0083b050}));  // "phase"
    }
    float height;
    float phase;
    G_SpawnFloat(title_global<uint32_t>(0x0098e930), 4.0f, &field<float>(ent, 0x2fc));
    G_SpawnFloat(title_global<uint32_t>(0x0098e92c), 32.0f, &height);
    const uint32_t dmg = cached_key(0x0098e934, 8, 0x0098e924, 0x0083b060);  // "dmg"
    G_SpawnInt(dmg, &field<int32_t>(ent, 0x358), 2);
    G_SpawnFloat(title_global<uint32_t>(0x0098e928), 0.0f, &phase);
    SV_SetBrushModel(ent);
    InitMover(ent);
    const int32_t duration = static_cast<int32_t>(field<float>(ent, 0x2fc) * title_global<float>(0x0083eb9c));
    word_at(ent, 0x20) = word_at(ent, 0x140);
    word_at(ent, 0x24) = word_at(ent, 0x144);
    word_at(ent, 0x28) = word_at(ent, 0x148);
    field<int32_t>(ent, 0x1c) = duration;
    field<int32_t>(ent, 0x18) = static_cast<int32_t>(static_cast<float>(duration) * phase);
    word_at(ent, 0x14) = 4;
    const uint32_t flags = word_at(ent, 0x2a0);
    if (flags & 1)
        field<float>(ent, 0x2c) = height;
    else if (flags & 2)
        field<float>(ent, 0x30) = height;
    else
        field<float>(ent, 0x34) = height;
}

void XB_CDECL SP_func_rotating(uint8_t* ent) {
    // Q3's SP_func_rotating: speed (+0x2fc, 0 -> 100); apos type 2 with spawn flag 1 spinning
    // about the flag's axis (4: +0x58, 8: +0x50, else +0x54); dmg 0 -> 2; brush model,
    // InitMover, pos base = origin; spawn flag 2 starts unlinked (+0x2a4 bit 0x10), else linked.
    if (field<float>(ent, 0x2fc) == title_global<float>(0x0083eafc)) field<float>(ent, 0x2fc) = title_global<float>(0x0083eb90);
    const uint32_t flags = word_at(ent, 0x2a0);
    word_at(ent, 0x38) = 2;
    if (flags & 1) {
        if (flags & 4)
            word_at(ent, 0x58) = word_at(ent, 0x2fc);
        else if (flags & 8)
            word_at(ent, 0x50) = word_at(ent, 0x2fc);
        else
            word_at(ent, 0x54) = word_at(ent, 0x2fc);
    }
    if (!word_at(ent, 0x358)) word_at(ent, 0x358) = 2;
    SV_SetBrushModel(ent);
    InitMover(ent);
    word_at(ent, 0x20) = word_at(ent, 0x140);
    word_at(ent, 0x24) = word_at(ent, 0x144);
    word_at(ent, 0x28) = word_at(ent, 0x148);
    if (ent[0x2a0] & 2) {
        word_at(ent, 0x2a4) |= 0x10;
        word_at(ent, 0xd0) = 0;
        SV_UnlinkEntity(ent + 0xd0);
        return;
    }
    SV_LinkEntity(ent);
}

void XB_CDECL SP_func_static(uint8_t* ent) {
    // The brush model, InitMover, pos base = origin, think 0xb; spawn flag 1 unlinks it. Not
    // a team slave: "health" set makes it take damage. Spawn flags 6: takedamage, health 9999,
    // +0x38c in ms (0 -> 1000), +0x370 0 -> 4.
    SV_SetBrushModel(ent);
    InitMover(ent);
    word_at(ent, 0x20) = word_at(ent, 0x140);
    word_at(ent, 0x24) = word_at(ent, 0x144);
    word_at(ent, 0x28) = word_at(ent, 0x148);
    const bool unlinked = (ent[0x2a0] & 1) != 0;
    word_at(ent, 0x33c) = 0xb;
    if (unlinked) {
        word_at(ent, 0xd0) = 0;
        SV_UnlinkEntity(ent + 0xd0);
    }
    if (!(ent[0x2a4] & 0x10)) {
        const uint32_t key = cached_key(0x0098e93c, 1, 0x0098e938, 0x008367c8);  // "health"
        int32_t health;
        G_SpawnInt(key, &health, 0);
        if (health) word_at(ent, 0x29c) = 1;
    }
    if (!(ent[0x2a0] & 6)) return;
    const float ms = title_global<float>(0x0083eb9c);
    const float value = field<float>(ent, 0x38c) == title_global<float>(0x0083eafc) ? ms : field<float>(ent, 0x38c) * ms;
    const uint32_t count = word_at(ent, 0x370);
    field<float>(ent, 0x38c) = value;
    word_at(ent, 0x29c) = 1;
    word_at(ent, 0x350) = 9999;
    if (!count) word_at(ent, 0x370) = 4;
}

void XB_REGS1 XB_CDECL G_ClientStopUsingTurret(uint8_t* turret) {
    // The turret's user (handle +0x1a0) gets off: a pending stance event (info +0x51: 2 ->
    // 0x90, 1 -> 0x8f, else 0x8e; then 0xff), teleported to the stored spot (info +0x58) with
    // its own angles, linked; turret flags 0x6000 (ps +0xc0) and the turret fields (+0x464,
    // +0x468, both +0x297) cleared, the handle and info bit 3 (+3) too; "turretownerchange".
    uint8_t* const info = pointer_at(turret, 0x24c);
    const uint32_t handle = word_at(turret, 0x1a0);
    const uint32_t index = handle & 0xfff;
    uint8_t* player = nullptr;
    if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
        player = title_global<uint8_t*>(0x0097a968 + index * 8);
    turret[1] = 0;
    if (info[0x51] != 0xff) {
        const uint32_t event = info[0x51] == 2 ? 0x90 : info[0x51] == 1 ? 0x8f : 0x8e;
        G_AddEvent(player, event, 0);
        info[0x51] = 0xff;
    }
    TeleportPlayer(player, 0, reinterpret_cast<const float*>(info + 0x58));
    SetClientViewAngle(reinterpret_cast<const float*>(player + 0x150), player);
    SV_LinkEntity(player);
    word_at(pointer_at(player, 0x23c), 0xc0) &= 0xffff9fffu;
    word_at(pointer_at(player, 0x23c), 0x464) = 0;
    word_at(pointer_at(player, 0x23c), 0x468) = 0;
    player[0x297] = 0;
    turret[0x297] = 0;
    word_at(turret, 0x1a0) = 0;
    info[3] &= 0xf7;
    Scr_Notify(turret, title_global<uint32_t>(0x0097a864));  // turretownerchange
}

PLATFORM_ASM_ENTRY(G_DObjAdvanceAnimsUntilDone_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(G_DObjAdvanceAnimsUntilDone) "addl $4, %esp\n"
                   "ret")

void XB_CDECL G_DObjAdvanceAnimsUntilDone(uint8_t* ent) {
    // Advances the entity's model (+0x22c) by the frame time (0x008dda9c ms) up to each
    // notetrack until it runs through (or the entity is gone / flag 0x10000 in +0x2a4).
    while (ent && !(word_at(ent, 0x2a4) & 0x10000) && word_at(ent, 0x22c)) {
        const float dt = static_cast<float>(title_global<int32_t>(0x008dda9c)) * title_global<float>(0x0083eb4c);
        uint32_t bits;
        std::memcpy(&bits, &dt, sizeof bits);
        if (!AnimTree_AdvanceTimeForModel(0, pointer_at(ent, 0x22c), bits, 1)) break;
    }
}

}  // namespace game
