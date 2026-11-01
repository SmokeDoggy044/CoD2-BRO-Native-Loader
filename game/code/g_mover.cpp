#include "g_mover.h"

#include <cstring>

#include "actor.h"
#include "bg_misc.h"
#include "dobj.h"
#include "g_client.h"
#include "g_main.h"
#include "g_spawn.h"
#include "g_utils.h"
#include "g_scr_runtime.h"
#include "g_script.h"
#include "cm.h"
#include "g_vehicle.h"
#include "qcommon.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "sv.h"
#include "title_data.h"
#include "tr_main.h"
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

// The title CRT's __CIfmod (0x0033acda): x in ST1, y in ST0, fmod(x, y) left in ST0.
long double title_ci_fmod(long double x, long double y) {
    long double result;
    __asm__ volatile("call *%3"
                     : "=t"(result)
                     : "0"(y), "u"(x), "r"(uintptr_t{0x0033acda})
                     : "st(1)", "eax", "ecx", "edx", "memory");
    return result;
}

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

PLATFORM_ASM_ENTRY(G_BounceMissileAngles_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(G_BounceMissileAngles) "addl $16, %esp\n"
                   "ret")

void XB_CDECL G_BounceMissileAngles(float* angles, uint8_t* ent, const uint8_t* trace, int32_t keep_spin) {
    // The angles (apos +0x38) at the hit time (previous time + frame * trace fraction +0x20).
    // On a floor (normal z > 0.1): the pitch the plane gives the yaw (Plane_PitchAlongYaw), its
    // difference to the current pitch wrapped into [-180, 180] (__CIfmod 360, x87 compares);
    // without keep_spin the apos base/time restart there with the pitch speed (+0x50) damped to
    // 0.85..1.15 (reversed below 80 degrees off). The pitch is snapped to 16-bit angle units,
    // then eased a quarter of the way (45..80 degrees off), kept, or set from the plane pitch
    // (flipped by 180 when the snapped pitch is over 90). Off floors: a random -63..64 added
    // to the pitch speed (snapped) unless keep_spin.
    uint8_t* const tr = const_cast<uint8_t*>(trace);
    const int32_t previous = title_global<int32_t>(0x00985410);
    const float elapsed = static_cast<float>(title_global<int32_t>(0x0098540c) - previous) * field<float>(tr, 0x20);
    const int32_t hit_time = static_cast<int32_t>(elapsed) + previous;
    {
        const TitleRegs regs{static_cast<uint32_t>(hit_time), 0, address_of(angles), 0, 0, address_of(ent + 0x38)};
        g_ent_call(0x002d62c0, &regs, 0, nullptr);  // BG_EvaluateTrajectory
    }
    const float to_short = title_global<float>(0x0083ebd8);    // 65536 / 360
    const float from_short = title_global<float>(0x0083ebd4);  // 360 / 65536
    if (field<float>(tr, 0x18) > title_global<float>(0x0083ebf8)) {
        const float pitch = Plane_PitchAlongYaw(reinterpret_cast<const float*>(tr + 0x10), angles[1]);
        float delta = static_cast<float>(title_ci_fmod(static_cast<long double>(pitch) - static_cast<long double>(angles[0]),
                                                       static_cast<long double>(title_global<double>(0x0083ed40))));
        if (platform_x87_greater(delta, title_global<float>(0x0083eb60))) delta = delta - title_global<float>(0x0083ebcc);
        if (title_global<float>(0x0083ebd0) > delta) delta = delta + title_global<float>(0x0083ebcc);
        uint32_t bits;
        std::memcpy(&bits, &delta, sizeof bits);
        bits &= 0x7fffffffu;  // FABS
        float off;
        std::memcpy(&off, &bits, sizeof off);
        if (!keep_spin) {
            word_at(ent, 0x44) = reinterpret_cast<const uint32_t*>(angles)[0];
            word_at(ent, 0x48) = reinterpret_cast<const uint32_t*>(angles)[1];
            word_at(ent, 0x4c) = reinterpret_cast<const uint32_t*>(angles)[2];
            field<int32_t>(ent, 0x3c) = hit_time;
            const bool reverse = title_global<float>(0x0083ee48) > off;
            const int32_t r = title_function<int32_t XB_CDECL()>(0x001efcc0)();  // _rand
            float speed = ((static_cast<float>(r) / title_global<float>(0x0083ebc8)) * title_global<float>(0x0083066c) +
                           title_global<float>(0x0083f008)) *
                          field<float>(ent, 0x50);
            if (reverse) speed = speed * title_global<float>(0x0083eb34);
            field<float>(ent, 0x50) = speed;
        }
        float snapped = static_cast<float>(static_cast<int32_t>(angles[0] * to_short) & 0xffff) * from_short;
        if (snapped > title_global<float>(0x0083eb60)) snapped = snapped - title_global<float>(0x0083ebcc);
        angles[0] = snapped;
        if (!keep_spin && !(title_global<float>(0x0083ef1c) > off)) {
            const float value = title_global<float>(0x0083ee48) > off
                                    ? (delta * title_global<float>(0x0083ec24) + snapped) * to_short
                                    : snapped * to_short;
            angles[0] = static_cast<float>(static_cast<int32_t>(value) & 0xffff) * from_short;
            return;
        }
        std::memcpy(&bits, &snapped, sizeof bits);
        bits &= 0x7fffffffu;  // FABS
        float size;
        std::memcpy(&size, &bits, sizeof size);
        float value = pitch * to_short;
        if (platform_x87_greater(size, title_global<float>(0x0083eb64))) value = value + title_global<float>(0x0083ebc8);
        angles[0] = static_cast<float>(static_cast<int32_t>(value) & 0xffff) * from_short;
        return;
    }
    if (keep_spin) return;
    const int32_t r = title_function<int32_t XB_CDECL()>(0x001efcc0)();  // _rand
    const float value = (static_cast<float>((r & 0x7f) - 0x3f) + field<float>(ent, 0x50)) * to_short;
    field<float>(ent, 0x50) = static_cast<float>(static_cast<int32_t>(value) & 0xffff) * from_short;
}

void XB_CDECL SP_func_door_rotating(uint8_t* ent) {
    // Spawn flag 1 implies 2, flag 0x40 cleared; speed (+0x2fc) 0 -> 1000, rotation (+0x2f4)
    // 0 -> 90 (negated with flag 0x10); flag 2 sets +0x2a4 bit 0x80; "key" present -> +0x3b4.
    // The axis (+0x390 pitch / +0x394 yaw / +0x398 roll) from flags 4/8 (both or none: yaw);
    // more than one (x87 length > 1) -> G_Error and yaw. wait (+0x384) 0 -> 2, in ms; brush
    // model, InitMoverRotate; "health" -> takedamage unless a team slave; think 7 in 100 ms,
    // +0x334 = 2, +0x2a4 bit 0x1000. Keys cached under 0x0098e910.
    if (word_at(ent, 0x2a0) & 1) word_at(ent, 0x2a0) |= 2;
    const uint32_t flags = word_at(ent, 0x2a0) & 0xffffffbfu;
    word_at(ent, 0x2a0) = flags;
    if (field<float>(ent, 0x2fc) == 0.0f) field<float>(ent, 0x2fc) = title_global<float>(0x0083eb9c);
    if (field<float>(ent, 0x2f4) == 0.0f) field<float>(ent, 0x2f4) = title_global<float>(0x0083eb64);
    if (flags & 0x10) field<float>(ent, 0x2f4) = field<float>(ent, 0x2f4) * title_global<float>(0x0083eb34);
    if (flags & 2) word_at(ent, 0x2a4) |= 0x80;
    int32_t value;
    const uint32_t key = cached_key(0x0098e910, 1, 0x0098e90c, 0x0083b068);  // "key"
    word_at(ent, 0x3b4) = G_SpawnInt(key, &value, 0) != 0 ? 1 : 0;
    field<float>(ent, 0x398) = 0.0f;
    field<float>(ent, 0x394) = 0.0f;
    field<float>(ent, 0x390) = 0.0f;
    const uint32_t axis = word_at(ent, 0x2a0);
    const float one = title_global<float>(0x0083eb20);
    if ((axis & 0xc) != 0xc && (axis & 4))
        field<float>(ent, 0x398) = one;
    else if ((axis & 0xc) != 0xc && (axis & 8))
        field<float>(ent, 0x390) = one;
    else
        field<float>(ent, 0x394) = one;
    const long double x = field<float>(ent, 0x390);
    const long double y = field<float>(ent, 0x394);
    const long double z = field<float>(ent, 0x398);
    if (platform_x87_sqrt_ext((x * x + y * y) + z * z) > static_cast<long double>(one)) {
        title_function<void XB_CDECL(const char*)>(0x00248af0)(&title_global<const char>(0x0083afd0));  // G_Error
        field<float>(ent, 0x398) = 0.0f;
        field<float>(ent, 0x390) = 0.0f;
        field<float>(ent, 0x394) = title_global<float>(0x0083eb20);
    }
    if (field<float>(ent, 0x384) == 0.0f) field<float>(ent, 0x384) = title_global<float>(0x0083eba8);
    field<float>(ent, 0x384) = field<float>(ent, 0x384) * title_global<float>(0x0083eb9c);
    SV_SetBrushModel(ent);
    InitMoverRotate(ent);
    if (!(ent[0x2a4] & 0x10)) {
        const uint32_t health_key = cached_key(0x0098e910, 2, 0x0098e908, 0x008367c8);  // "health"
        G_SpawnInt(health_key, &value, 0);
        if (value) word_at(ent, 0x29c) = 1;
    }
    word_at(ent, 0x2a4) |= 0x1000;
    field<int32_t>(ent, 0x328) = title_global<int32_t>(0x0098540c) + 100;
    word_at(ent, 0x32c) = 7;
    word_at(ent, 0x334) = 2;
}

void XB_CDECL SP_func_door(uint8_t* ent) {
    // Q3's SP_func_door: +0x334 = 1; speed (+0x2fc) 0 -> 400, wait (+0x384) 0 -> 2 in ms; "key"
    // present -> +0x3b4; "lip" (8), "dmg" (+0x358, 2); pos1 (+0x2c0) = origin, brush model,
    // movedir (+0x310) from the angles; pos2 (+0x2d0) = pos1 + movedir * (|movedir| . size -
    // lip). Spawn flag 1 swaps pos1/pos2 and (with +0x300 set) speed and +0x300; flag 2 sets
    // +0x2a4 bit 0x80. InitMover; "health" -> takedamage unless a team slave; think 7 in 100 ms.
    // Keys cached under 0x0098e950.
    word_at(ent, 0x334) = 1;
    if (field<float>(ent, 0x2fc) == 0.0f) field<float>(ent, 0x2fc) = title_global<float>(0x0083f11c);
    if (field<float>(ent, 0x384) == 0.0f) field<float>(ent, 0x384) = title_global<float>(0x0083eba8);
    field<float>(ent, 0x384) = field<float>(ent, 0x384) * title_global<float>(0x0083eb9c);
    int32_t value;
    const uint32_t key = cached_key(0x0098e950, 1, 0x0098e94c, 0x0083b068);  // "key"
    word_at(ent, 0x3b4) = G_SpawnInt(key, &value, 0) != 0 ? 1 : 0;
    float lip;
    const uint32_t lip_key = cached_key(0x0098e950, 2, 0x0098e948, 0x0083b064);  // "lip"
    G_SpawnFloat(lip_key, 8.0f, &lip);
    const uint32_t dmg_key = cached_key(0x0098e950, 4, 0x0098e944, 0x0083b060);  // "dmg"
    G_SpawnInt(dmg_key, &field<int32_t>(ent, 0x358), 2);
    word_at(ent, 0x2c0) = word_at(ent, 0x140);
    word_at(ent, 0x2c4) = word_at(ent, 0x144);
    word_at(ent, 0x2c8) = word_at(ent, 0x148);
    SV_SetBrushModel(ent);
    G_SetMovedir(&field<float>(ent, 0x150), &field<float>(ent, 0x310));
    float abs_dir[3];
    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t bits = word_at(ent, 0x310 + i * 4) & 0x7fffffffu;  // FABS
        std::memcpy(&abs_dir[i], &bits, sizeof bits);
    }
    const float size_z = field<float>(ent, 0xf8) - field<float>(ent, 0xe8);
    const float size_x = field<float>(ent, 0xf0) - field<float>(ent, 0xe0);
    const float size_y = field<float>(ent, 0xf4) - field<float>(ent, 0xe4);
    const float px = size_x * abs_dir[0];
    const float py = size_y * abs_dir[1];
    const float distance = ((size_z * abs_dir[2] + py) + px) - lip;
    field<float>(ent, 0x2d0) = field<float>(ent, 0x310) * distance + field<float>(ent, 0x2c0);
    field<float>(ent, 0x2d4) = distance * field<float>(ent, 0x314) + field<float>(ent, 0x2c4);
    field<float>(ent, 0x2d8) = distance * field<float>(ent, 0x318) + field<float>(ent, 0x2c8);
    if (ent[0x2a0] & 1) {
        const uint32_t x2 = word_at(ent, 0x2d0);
        const uint32_t y2 = word_at(ent, 0x2d4);
        const uint32_t z2 = word_at(ent, 0x2d8);
        word_at(ent, 0x2d0) = word_at(ent, 0x140);
        word_at(ent, 0x2d4) = word_at(ent, 0x144);
        word_at(ent, 0x2d8) = word_at(ent, 0x148);
        word_at(ent, 0x2c0) = x2;
        word_at(ent, 0x2c4) = y2;
        word_at(ent, 0x2c8) = z2;
        if (field<float>(ent, 0x300) != title_global<float>(0x0083eafc)) {
            const uint32_t other = word_at(ent, 0x300);
            word_at(ent, 0x300) = word_at(ent, 0x2fc);
            word_at(ent, 0x2fc) = other;
        }
    }
    if (ent[0x2a0] & 2) word_at(ent, 0x2a4) |= 0x80;
    InitMover(ent);
    if (!(ent[0x2a4] & 0x10)) {
        const uint32_t health_key = cached_key(0x0098e950, 8, 0x0098e940, 0x008367c8);  // "health"
        G_SpawnInt(health_key, &value, 0);
        if (value) word_at(ent, 0x29c) = 1;
    }
    field<int32_t>(ent, 0x328) = title_global<int32_t>(0x0098540c) + 100;
    word_at(ent, 0x32c) = 7;
}

void XB_CDECL Use_Item(uint8_t* ent, uint8_t*, uint8_t*) { RespawnItem(ent); }

void XB_CDECL G_VehiclePopOut(uint8_t* player) {
    // __break 1 "Popout disabled" first. A player in a vehicle (ps +0xc0 bit 0x100000, vehicle
    // handle +0x1a0, the vehicle not contents 0x200000) toggles ps bit 0x400000: off -> the
    // vehicle's player tag (info +0x2b0, "tag_player"), on -> its popout tag (+0x2b8,
    // "tag_popout"); unlinked, teleported to the tag and linked to it (__break 1 on failure).
    uint8_t* const ps = pointer_at(player, 0x23c);
    title_function<void XB_CDECL(int32_t, const char*)>(0x002938f0)(1, &title_global<const char>(0x00839fb0));  // __break
    const uint32_t flags = word_at(ps, 0xc0);
    if (!(flags & 0x100000)) return;
    const uint32_t handle = word_at(player, 0x1a0);
    if (!handle) return;
    const uint32_t index = handle & 0xfff;
    uint8_t* vehicle = nullptr;
    if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
        vehicle = title_global<uint8_t*>(0x0097a968 + index * 8);
    if (!word_at(vehicle, 0x1a0) || (word_at(vehicle, 0x134) & 0x200000)) return;
    int32_t tag;
    uint32_t name;
    if (flags & 0x400000) {
        word_at(ps, 0xc0) = flags & 0xffbfffffu;
        tag = field<int32_t>(pointer_at(vehicle, 0x248), 0x2b0);
        name = 0x00838a54;  // "tag_player"
    } else {
        word_at(ps, 0xc0) = flags | 0x400000;
        tag = field<int32_t>(pointer_at(vehicle, 0x248), 0x2b8);
        name = 0x00839fd4;  // "tag_popout"
    }
    using Break = void XB_CDECL(int32_t, const char*, const char*);
    const char* const text = reinterpret_cast<const char*>(uintptr_t{name});
    if (tag < 0) title_function<Break>(0x002938f0)(1, &title_global<const char>(0x00839f70), text);  // __break
    title_function<void XB_REGS1 XB_CDECL(uint8_t*)>(0x002b7900)(player);  // G_EntUnlink
    alignas(16) float matrix[16];
    title_function<void XB_CDECL(uint8_t*, int32_t, float*)>(0x002e3780)(vehicle, tag, matrix);  // G_DObjGetWorldTagMatrix
    TeleportPlayer(player, 0, matrix + 12);
    const TitleRegs regs{0, 0, name, address_of(player), 0, 0};
    const uint32_t args[3] = {address_of(vehicle), 0x0082eec0, 0x0082eec0};
    if (!g_ent_call(0x002e44b0, &regs, 3, args))  // G_EntLinkToWithOffset
        title_function<Break>(0x002938f0)(1, &title_global<const char>(0x00839f3c), text);  // __break
}

void XB_CDECL turret_use(uint8_t* ent, uint8_t* other, uint8_t*) {
    // A player already on a turret (ps +0xc0 bits 0x6000) gets off (G_ClientStopUsingTurret).
    // Else it mounts: both +0x297 = 1, the turret's user handle (+0x1a0) and ps +0x464/+0x468
    // set, info bit 3 (+3), the player's spot (info +0x58) and stance (info +0x51: 2 crouch-
    // ish ps +0x2c bit 0, else bit 1) kept; ps flags from the turret stance (info +0x50: 2 ->
    // 0x2000, 1 -> 0x4000, else both). The turret's angles (+0x3a0) kept; the view's offset
    // from them (__CIfmod 360, wrapped) clamped to the arcs (pitch: info +0x2c / +0x34 thirds,
    // yaw: +0x30 / +0x38) into +0x90 / +0x94; the view set there; "turretownerchange".
    uint8_t* const info = pointer_at(ent, 0x24c);
    if (other && pointer_at(other, 0x23c) && (word_at(pointer_at(other, 0x23c), 0xc0) & 0x6000)) {
        G_ClientStopUsingTurret(ent);
        return;
    }
    other[0x297] = 1;
    ent[0x297] = 1;
    word_at(ent, 0x1a0) = word_at(other, 0x224);
    word_at(pointer_at(other, 0x23c), 0x464) = 1;
    word_at(pointer_at(other, 0x23c), 0x468) = word_at(ent, 0x224);
    info[3] |= 8;
    word_at(info, 0x58) = word_at(other, 0x140);
    word_at(info, 0x5c) = word_at(other, 0x144);
    word_at(info, 0x60) = word_at(other, 0x148);
    const uint8_t stance_flags = pointer_at(other, 0x23c)[0x2c];
    info[0x51] = (stance_flags & 1) ? 2 : static_cast<uint8_t>((stance_flags >> 1) & 1);
    uint8_t* ps = pointer_at(other, 0x23c);
    if (info[0x50] == 2) {
        word_at(ps, 0xc0) |= 0x2000;
        ps = pointer_at(other, 0x23c);
        word_at(ps, 0xc0) &= 0xffffbfffu;
    } else if (info[0x50] == 1) {
        word_at(ps, 0xc0) |= 0x4000;
        ps = pointer_at(other, 0x23c);
        word_at(ps, 0xc0) &= 0xffffdfffu;
    } else {
        word_at(ps, 0xc0) |= 0x6000;
    }
    word_at(ent, 0x3a0) = word_at(ent, 0x150);
    word_at(ent, 0x3a4) = word_at(ent, 0x154);
    word_at(ent, 0x3a8) = word_at(ent, 0x158);
    const long double full = title_global<double>(0x0083ed40);  // 360
    const float half_turn = title_global<float>(0x0083eb60);
    const float turn = title_global<float>(0x0083ebcc);
    const float minus_half = title_global<float>(0x0083ebd0);
    float pitch = static_cast<float>(title_ci_fmod(
        static_cast<long double>(field<float>(pointer_at(other, 0x23c), 0x9c)) - field<float>(ent, 0x150), full));
    if (platform_x87_greater(pitch, half_turn)) pitch = pitch - turn;
    if (minus_half > pitch) pitch = pitch + turn;
    const float third = title_global<float>(0x0083ec9c);
    field<float>(ent, 0x90) = pitch;
    const float low = field<float>(info, 0x34) / third;
    if (pitch > low) {
        field<float>(ent, 0x90) = low;
    } else {
        const float high = field<float>(info, 0x2c) / third;
        if (high > pitch) field<float>(ent, 0x90) = high;
    }
    float yaw = static_cast<float>(title_ci_fmod(
        static_cast<long double>(field<float>(pointer_at(other, 0x23c), 0xa0)) - field<float>(ent, 0x154), full));
    if (platform_x87_greater(yaw, half_turn)) yaw = yaw - turn;
    if (minus_half > yaw) yaw = yaw + turn;
    field<float>(ent, 0x94) = yaw;
    if (yaw > field<float>(info, 0x38))
        word_at(ent, 0x94) = word_at(info, 0x38);
    else if (field<float>(info, 0x30) > yaw)
        word_at(ent, 0x94) = word_at(info, 0x30);
    float view[3];
    view[0] = field<float>(ent, 0x150) + field<float>(ent, 0x90);
    view[1] = field<float>(ent, 0x94) + field<float>(ent, 0x154);
    view[2] = 0.0f;
    SetClientViewAngle(view, other);
    Scr_Notify(ent, title_global<uint32_t>(0x0097a864));  // turretownerchange
}

void XB_CDECL turret_controller(uint8_t* ent) {
    // The aim angles {+0x90, +0x94, 0} onto "tag_aim" and "tag_aim_animated", the yaw alone onto
    // "tag_aim_animatedY", the pitch alone onto "tag_aim_animatedP" (G_DObjSetBoneRotation,
    // controller vec3_origin). Tag hashes cached under 0x0098eba4.
    using SetBoneRotation = void XB_CDECL(uint8_t*, uint32_t, const float*, int32_t, uint32_t);
    float angles[3];
    angles[1] = field<float>(ent, 0x94);
    angles[0] = field<float>(ent, 0x90);
    angles[2] = 0.0f;
    const uint32_t aim = cached_key(0x0098eba4, 1, 0x0098eba0, 0x00839a18);           // "tag_aim"
    const uint32_t animated = cached_key(0x0098eba4, 2, 0x0098eb9c, 0x00839a04);      // "tag_aim_animated"
    const uint32_t animated_y = cached_key(0x0098eba4, 4, 0x0098eb98, 0x008399f0);    // "tag_aim_animatedY"
    const uint32_t animated_p = cached_key(0x0098eba4, 8, 0x0098eb94, 0x008399dc);    // "tag_aim_animatedP"
    const uint32_t hashes[2] = {aim, animated};
    for (uint32_t i = 0; i < 2; ++i) {
        const DObj* const obj = reinterpret_cast<const DObj*>(pointer_at(ent, 0x22c));
        if (!obj) continue;
        const int32_t bone = DObj_GetTagIndex(obj, hashes[i]);
        if (bone >= 0) title_function<SetBoneRotation>(0x002de420)(ent, 0x0082eec0, angles, bone, 0);
    }
    float single[3];
    single[1] = angles[1];
    single[0] = 0.0f;
    single[2] = 0.0f;
    if (const DObj* const obj = reinterpret_cast<const DObj*>(pointer_at(ent, 0x22c))) {
        const int32_t bone = DObj_GetTagIndex(obj, animated_y);
        if (bone >= 0) title_function<SetBoneRotation>(0x002de420)(ent, 0x0082eec0, single, bone, 0);
    }
    single[0] = angles[0];
    single[1] = 0.0f;
    single[2] = 0.0f;
    if (const DObj* const obj = reinterpret_cast<const DObj*>(pointer_at(ent, 0x22c))) {
        const int32_t bone = DObj_GetTagIndex(obj, animated_p);
        if (bone >= 0) title_function<SetBoneRotation>(0x002de420)(ent, 0x0082eec0, single, bone, 0);
    }
}

uint32_t XB_CDECL Turret_CanTargetPoint(uint8_t* ent, const float* point, float* start, float* angles) {
    // From the flash tag ("tag_flash", cached under 0x0098ebcc; none: 0) when the model is
    // there without DObj flag 1 (+0x9c), else from the origin: start out, the direction's angles
    // (vectoangles_2) minus the turret's (+0x150), wrapped (__CIfmod 360), into angles[0..1];
    // 0 once one leaves its arc (info +0x2c..+0x38: pitch/yaw minimum, maximum).
    uint8_t* const info = pointer_at(ent, 0x24c);
    uint8_t* const obj = pointer_at(ent, 0x22c);
    float dir[3];
    if (obj && !(obj[0x9c] & 1)) {
        const uint32_t hash = cached_key(0x0098ebcc, 1, 0x0098ebc8, 0x008389c0);  // "tag_flash"
        const int32_t tag = DObj_GetTagIndex(reinterpret_cast<const DObj*>(obj), hash);
        if (tag < 0) return 0;
        alignas(16) float m[16];
        title_function<void XB_CDECL(uint8_t*, int32_t, float*)>(0x002e3780)(ent, tag, m);  // G_DObjGetWorldTagMatrix
        dir[0] = point[0] - m[12];
        dir[1] = point[1] - m[13];
        dir[2] = point[2] - m[14];
        start[0] = m[12];
        start[1] = m[13];
        start[2] = m[14];
    } else {
        dir[0] = point[0] - field<float>(ent, 0x140);
        dir[1] = point[1] - field<float>(ent, 0x144);
        dir[2] = point[2] - field<float>(ent, 0x148);
        uint32_t* const out = reinterpret_cast<uint32_t*>(start);
        out[0] = word_at(ent, 0x140);
        out[1] = word_at(ent, 0x144);
        out[2] = word_at(ent, 0x148);
    }
    float view[3];
    vectoangles_2(dir, view);
    const long double full = title_global<double>(0x0083ed40);  // 360
    for (uint32_t i = 0; i < 2; ++i) {
        float delta = static_cast<float>(
            title_ci_fmod(static_cast<long double>(view[i]) - field<float>(ent, 0x150 + i * 4), full));
        if (platform_x87_greater(delta, title_global<float>(0x0083eb60))) delta = delta - title_global<float>(0x0083ebcc);
        if (title_global<float>(0x0083ebd0) > delta) delta = delta + title_global<float>(0x0083ebcc);
        angles[i] = delta;
        if (delta > field<float>(info, 0x34 + i * 4)) return 0;
        if (field<float>(info, 0x2c + i * 4) > delta) return 0;
    }
    return 1;
}

uint32_t XB_REGS3 XB_CDECL G_GetFlashTagMatrix(int32_t index, uint32_t, uint8_t* ent, float* out) {
    // Tag hashes cached together (guard 0x0098ebd0) in 0x0098ad5c / 0x0098ad60. out: the axis
    // of the aim angles (+0x150 + +0x98 + +0x90, +0x154 + +0x94, +0x158; AnglesToAxis), the tag
    // origin (+0x24) and the tag matrix's first row (+0x30).
    if (index >= 2) return 0;
    uint32_t guard = title_global<uint32_t>(0x0098ebd0);
    if (!(guard & 1)) {
        guard |= 1;
        title_global<uint32_t>(0x0098ebd0) = guard;
        title_global<uint32_t>(0x0098ad5c) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x008389c0}));
        title_global<uint32_t>(0x0098ad60) = Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{0x008389a4}));
    }
    const DObj* const obj = reinterpret_cast<const DObj*>(pointer_at(ent, 0x22c));
    if (!obj) return 0;
    const int32_t tag = DObj_GetTagIndex(obj, title_global<uint32_t>(0x0098ad5c + static_cast<uint32_t>(index) * 4));
    if (tag < 0) return 0;
    alignas(16) float m[16];
    title_function<void XB_CDECL(uint8_t*, int32_t, float*)>(0x002e3780)(ent, tag, m);  // G_DObjGetWorldTagMatrix
    float aim[3];
    aim[1] = field<float>(ent, 0x154) + field<float>(ent, 0x94);
    aim[2] = field<float>(ent, 0x158);
    aim[0] = (field<float>(ent, 0x150) + field<float>(ent, 0x98)) + field<float>(ent, 0x90);
    uint32_t args[2] = {address_of(aim), address_of(out)};
    AnglesToAxis(args, out + 3, out + 6);  // C version: (args, EDI buffer, ESI buffer)
    out[12] = m[0];
    out[13] = m[1];
    out[14] = m[2];
    out[9] = m[12];
    out[10] = m[13];
    out[11] = m[14];
    return 1;
}

PLATFORM_ASM_ENTRY(Turret_TryAimAtTarget_entry,
                   "pushl %edi\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Turret_TryAimAtTarget) "addl $16, %esp\n"
                   "ret")

uint32_t XB_CDECL Turret_TryAimAtTarget(uint8_t* ent, const float* point, uint32_t aiming, float* angles) {
    float start[3];
    if (!Turret_CanTargetPoint(ent, point, start, angles)) {
        pointer_at(ent, 0x24c)[2] &= 0xbf;
        return 0;
    }
    Turret_UpdateAimState(aiming, 0, ent, point, angles);
    return 1;
}

PLATFORM_ASM_ENTRY(SP_script_model_entry,
                   "movl %esp, %ecx\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %ecx, %ebp\n"
                   "pushl %edi\n"
                   "pushl 4(%ebp)\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(SP_script_model) "addl $12, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL SP_script_model(uint32_t entry, uint8_t* ent, uint32_t edi) {
    // G_DObjUpdate, InitScriptMover; DObj bounds flag (+0xd4 bit 0x10), contents 0x2080; with a
    // brush model (+6) solid (contents 1, spawn flag 1); linked; +0x2a4 bit 0x20000. Spawn flag
    // 1 also sets 0x3000 and, outside the path editor flag 0x0095bf0c, cuts the paths it
    // blocks. +0x348 = 2.
    title_function<void XB_CDECL(uint8_t*)>(0x002e8be0)(ent);  // G_DObjUpdate
    InitScriptMover(ent);
    word_at(ent, 0xd4) |= 0x10;
    word_at(ent, 0x134) = 0x2080;
    if (*reinterpret_cast<const uint16_t*>(ent + 6)) {
        SV_SetBrushModel(ent);
        word_at(ent, 0x134) = 1;
        word_at(ent, 0x2a0) |= 1;
    }
    SV_LinkEntity(ent);
    word_at(ent, 0x2a4) |= 0x20000;
    if (ent[0x2a0] & 1) {
        word_at(ent, 0x2a4) |= 0x3000;
        if (!title_global<uint32_t>(0x0095bf0c)) {
            const TitleRegs regs{title_global<uint32_t>(0x0095f4f4), 0, 0, 0, address_of(ent), edi};
            const uint32_t args[2] = {title_global<uint32_t>(0x0095f4f4), address_of(ent)};
            title_call_at_regs(entry - 4, 0x002e8ef0, &regs, 2, args);  // Path_DisconnectPathsForEntity
        }
    }
    word_at(ent, 0x348) = 2;
}

void XB_REGS1 XB_CDECL Door_UpdatePaths(uint8_t* door) {
    // Only doors with +0x2a4 bit 0x1000: state 8, or 7 while locked (+0x3b4), cut the paths
    // through them (Path_DisconnectPathsForEntity); else they are connected again.
    if (!(word_at(door, 0x2a4) & 0x1000)) return;
    const uint8_t state = door[0x298];
    if (state == 8 || (state == 7 && word_at(door, 0x3b4))) {
        title_function<void XB_STDCALL(uint32_t, uint8_t*)>(0x002e8ef0)(title_global<uint32_t>(0x0095f4f4),
                                                                       door);  // Path_DisconnectPathsForEntity
        return;
    }
    const TitleRegs regs{address_of(door), 0, 0, 0, title_global<uint32_t>(0x0095f4f4), 0};
    g_ent_call(0x002c5ef0, &regs, 0, nullptr);  // Path_ConnectPathsForEntity
}

void XB_REGS3 XB_CDECL SetMoverState(int32_t time, uint32_t state, uint8_t* ent) {
    // Q3's SetMoverState: the state byte (+0x298), trajectory times (+0x18 pos, +0x3c apos).
    // 0/1/2: stationary at pos1/pos2/pos3 (+0x2c0/+0x2d0/+0x2e0; 0 also clears +0x297);
    // 3/4: linear pos1 -> pos2 / back over +0x320 ms (4: +0x324 when +0x300 is set);
    // 5/6: pos2 <-> pos3 over the current duration (+0x1c); 7/8: angles stationary; 9/10:
    // rotation by +0x2f4 on the axis (+0x390) at 1000 / +0x320 per s (spawn flag 0x100
    // without 0x800 (+0x2a4): half speed, twice the time) - 10 starting from the rotated
    // angles back (10 also clears +0x297). Then the current origin, linked unless an
    // unlinked DObj without contents, and the door's path links (Door_UpdatePaths).
    const uint32_t flags = word_at(ent, 0x2a4);
    const bool slow = !(flags & 0x800) && (flags & 0x100);
    ent[0x298] = static_cast<uint8_t>(state);
    field<int32_t>(ent, 0x18) = time;
    field<int32_t>(ent, 0x3c) = time;
    const float ms = title_global<float>(0x0083eb9c);
    // Linear move from base to the other point over `duration` (cvtsi2ss), type 3.
    auto linear = [&](uint32_t base, uint32_t other, int32_t duration, bool store_duration) {
        word_at(ent, 0x20) = word_at(ent, base);
        word_at(ent, 0x24) = word_at(ent, base + 4);
        word_at(ent, 0x28) = word_at(ent, base + 8);
        const float dx = field<float>(ent, other) - field<float>(ent, base);
        const float dy = field<float>(ent, other + 4) - field<float>(ent, base + 4);
        const float dz = field<float>(ent, other + 8) - field<float>(ent, base + 8);
        const float span = static_cast<float>(duration);
        if (store_duration) field<int32_t>(ent, 0x1c) = duration;
        const float f = ms / span;
        field<float>(ent, 0x2c) = f * dx;
        field<float>(ent, 0x30) = f * dy;
        field<float>(ent, 0x34) = f * dz;
        word_at(ent, 0x14) = 3;
    };
    auto stationary = [&](uint32_t base) {
        word_at(ent, 0x20) = word_at(ent, base);
        word_at(ent, 0x24) = word_at(ent, base + 4);
        word_at(ent, 0x28) = word_at(ent, base + 8);
        word_at(ent, 0x14) = 0;
    };
    switch (state) {
    case 0:
        stationary(0x2c0);
        ent[0x297] = 0;
        break;
    case 1:
        stationary(0x2d0);
        break;
    case 2:
        stationary(0x2e0);
        break;
    case 3:
        linear(0x2c0, 0x2d0, field<int32_t>(ent, 0x320), true);
        break;
    case 4:
        linear(0x2d0, 0x2c0,
               field<float>(ent, 0x300) == title_global<float>(0x0083eafc) ? field<int32_t>(ent, 0x320)
                                                                           : field<int32_t>(ent, 0x324),
               true);
        break;
    case 5:
        linear(0x2d0, 0x2e0, field<int32_t>(ent, 0x1c), false);
        break;
    case 6:
        linear(0x2e0, 0x2d0, field<int32_t>(ent, 0x1c), false);
        break;
    case 7:
    case 8:
        word_at(ent, 0x44) = word_at(ent, 0x150);
        word_at(ent, 0x48) = word_at(ent, 0x154);
        word_at(ent, 0x4c) = word_at(ent, 0x158);
        word_at(ent, 0x38) = 0;
        break;
    case 9: {
        field<float>(ent, 0x4c) = 0.0f;
        field<float>(ent, 0x48) = 0.0f;
        field<float>(ent, 0x44) = 0.0f;
        int32_t duration = field<int32_t>(ent, 0x320);
        const float span = static_cast<float>(duration);
        float rate;
        if (slow) {
            rate = title_global<float>(0x0083ebec);  // 500
            duration += duration;
        } else {
            rate = ms;
        }
        rate = rate / span;
        const float speed = field<float>(ent, 0x2f4) * rate;
        field<int32_t>(ent, 0x40) = duration;
        field<float>(ent, 0x50) = field<float>(ent, 0x390) * speed;
        field<float>(ent, 0x54) = field<float>(ent, 0x394) * speed;
        field<float>(ent, 0x58) = field<float>(ent, 0x398) * speed;
        word_at(ent, 0x38) = 3;
        break;
    }
    case 10: {
        field<float>(ent, 0x44) = field<float>(ent, 0x390) * field<float>(ent, 0x2f4);
        field<float>(ent, 0x48) = field<float>(ent, 0x394) * field<float>(ent, 0x2f4);
        field<float>(ent, 0x4c) = field<float>(ent, 0x398) * field<float>(ent, 0x2f4);
        int32_t duration = field<int32_t>(ent, 0x320);
        float rate = ms / static_cast<float>(duration);
        field<int32_t>(ent, 0x40) = duration;
        if (slow) {
            rate = rate * title_global<float>(0x0083eb24);
            duration += duration;
            field<int32_t>(ent, 0x40) = duration;
        }
        const float back = 0.0f - rate;
        field<float>(ent, 0x50) = back * field<float>(ent, 0x44);
        field<float>(ent, 0x54) = back * field<float>(ent, 0x48);
        field<float>(ent, 0x58) = back * field<float>(ent, 0x4c);
        word_at(ent, 0x38) = 3;
        ent[0x297] = 0;
        break;
    }
    default:
        break;
    }
    {
        const TitleRegs regs{title_global<uint32_t>(0x0098540c), 0, address_of(ent + 0x140), 0, 0, address_of(ent + 0x14)};
        g_ent_call(0x002d62c0, &regs, 0, nullptr);  // BG_EvaluateTrajectory
    }
    if (!(ent[0xd4] & 1) || word_at(ent, 0x134)) SV_LinkEntity(ent);
    Door_UpdatePaths(ent);
}

PLATFORM_ASM_ENTRY(Door_StartRotate_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Door_StartRotate) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Door_StartRotate(uint8_t* ent) {
    // The door turns by its rotation (+0x2f4) at once: think 0x15, state 8, yaw += rotation,
    // apos stationary at the new angles; current origin, link and path links as in
    // SetMoverState; the team master (+0x380 self or none) opens its area portal (+0x200,
    // +0x1fc; -1: none).
    word_at(ent, 0x32c) = 0x15;
    field<float>(ent, 0x154) = field<float>(ent, 0x2f4) + field<float>(ent, 0x154);
    ent[0x298] = 8;
    field<int32_t>(ent, 0x18) = title_global<int32_t>(0x0098540c);
    field<int32_t>(ent, 0x3c) = title_global<int32_t>(0x0098540c);
    word_at(ent, 0x44) = word_at(ent, 0x150);
    word_at(ent, 0x48) = word_at(ent, 0x154);
    word_at(ent, 0x4c) = word_at(ent, 0x158);
    word_at(ent, 0x38) = 0;
    {
        const TitleRegs regs{title_global<uint32_t>(0x0098540c), 0, address_of(ent + 0x140), 0, 0, address_of(ent + 0x14)};
        g_ent_call(0x002d62c0, &regs, 0, nullptr);  // BG_EvaluateTrajectory
    }
    if (!(ent[0xd4] & 1) || word_at(ent, 0x134)) SV_LinkEntity(ent);
    Door_UpdatePaths(ent);
    const uint32_t master = word_at(ent, 0x380);
    if ((master == address_of(ent) || !master) && field<int32_t>(ent, 0x200) != -1)
        CM_AdjustAreaPortalState(field<int32_t>(ent, 0x1fc), field<int32_t>(ent, 0x200), 1);
}


namespace {

// The memory context an entity's allocations use (+0x220, -1: the level's 0x0095f638 +0x34).
uint32_t entity_context(uint8_t* ent) {
    const uint32_t context = word_at(ent, 0x220);
    if (context != 0xffffffffu) return context;
    return *reinterpret_cast<const uint32_t*>(uintptr_t{title_global<uint32_t>(0x0095f638) + 0x34});
}

uint8_t* spawn_in_context(uint32_t context) {
    const TitleRegs regs{context, 0, 0, 0, 0, 0};
    return reinterpret_cast<uint8_t*>(uintptr_t{g_ent_call(0x002d7030, &regs, 0, nullptr)});  // G_Spawn
}

// The classname script string from a global (ScriptString_Assign) and its name hash (+0x260).
void set_classname(uint8_t* ent, uint32_t name) {
    title_function<void XB_THISCALL(uint32_t, uint32_t)>(0x0011d600)(address_of(ent + 0x25c), name);  // ScriptString_Assign
    const uint32_t text = word_at(ent, 0x25c);
    word_at(ent, 0x260) = Com_HashKeyNoCase(0, text ? reinterpret_cast<const char*>(uintptr_t{text + 6}) : nullptr);
}

// The weapon def's damage fields: +0x5b8 -> +0x358, +0x728/+0x72c/+0x724 -> +0x35c..+0x364.
void set_grenade_damage(uint8_t* ent, uint8_t* def) {
    word_at(ent, 0x358) = word_at(def, 0x5b8);
    word_at(ent, 0x35c) = word_at(def, 0x728);
    word_at(ent, 0x360) = word_at(def, 0x72c);
    word_at(ent, 0x364) = word_at(def, 0x724);
}

void set_grenade_model(uint8_t* ent, uint8_t* def) {
    uint32_t model = 0;
    const char* const name = reinterpret_cast<const char*>(uintptr_t{word_at(def, 0x738)});
    word_at(ent, 0x258) =
        *R_RegisterModel(name, title_global<uint32_t>(0x0095f6ac), &model, entity_context(ent));  // rm_models
}

uint8_t* weapon_def_of(uint32_t weapon) {
    return reinterpret_cast<uint8_t*>(
        uintptr_t{*reinterpret_cast<const uint32_t*>(uintptr_t{title_global<uint32_t>(0x0095f4d8) + weapon * 4})});
}

// The effects/script LCG (0x008d5a70) step: the next seed's bits 17..31.
uint32_t lcg_next() {
    uint32_t& seed = title_global<uint32_t>(0x008d5a70);
    seed = seed * 0x343fd + 0x269ec3;
    return seed >> 17;
}

}  // namespace

uint8_t* XB_REGS3 XB_CDECL fire_grenadeCooked(const float* origin, uint32_t, uint8_t* owner, int32_t fuse) {
    // A cooked grenade dropped at the origin (cluster grenades): owner's weapon (byte 3), class
    // name from 0x00984be8, think 9 at time - def +0x754 + fuse + 250, falling straight down
    // (trajectory type 0 with the def's speed +0x730 down), angles from {0, 0, -1}, no spin;
    // the def's model; G_DObjUpdate. Returns the entity.
    uint8_t* const def = weapon_def_of(owner[3]);
    const float down[3] = {0.0f, 0.0f, title_global<float>(0x0083eb34)};
    uint8_t* const ent = spawn_in_context(entity_context(owner));
    set_classname(ent, 0x00984be8);
    word_at(ent, 0x32c) = 9;
    ent[0] = 3;
    word_at(ent, 0xd4) = 0xa0;
    field<int32_t>(ent, 0x328) = title_global<int32_t>(0x0098540c) - field<int32_t>(def, 0x754) + fuse + 0xfa;
    word_at(ent, 0x10) |= 0x8000;
    ent[3] = owner[3];
    word_at(ent, 0x1a0) = word_at(owner, 0x224);
    word_at(ent, 0x2b0) = word_at(owner, 0x224);
    set_grenade_damage(ent, def);
    word_at(ent, 0x368) = 5;
    word_at(ent, 0x36c) = 6;
    word_at(ent, 0x2a8) = 0x280e091;
    word_at(ent, 0x14) = 0;
    const float x = origin[0];
    const float y = origin[1];
    const float z = origin[2];
    field<float>(ent, 0x20) = x;
    field<float>(ent, 0x24) = y;
    field<float>(ent, 0x28) = z;
    field<float>(ent, 0x2c) = static_cast<float>(field<int32_t>(def, 0x730)) * 0.0f;
    field<float>(ent, 0x30) = static_cast<float>(field<int32_t>(def, 0x730)) * 0.0f;
    field<float>(ent, 0x34) = static_cast<float>(field<int32_t>(def, 0x730)) * title_global<float>(0x0083eb34);
    field<float>(ent, 0x140) = x;
    field<float>(ent, 0x144) = y;
    field<float>(ent, 0x148) = z;
    float angles[3];
    vectoangles_call(down, angles);
    field<float>(ent, 0x150) = angles[0];
    field<float>(ent, 0x154) = angles[1];
    field<float>(ent, 0x158) = angles[2];
    word_at(ent, 0x44) = word_at(ent, 0x150);
    word_at(ent, 0x48) = word_at(ent, 0x154);
    word_at(ent, 0x4c) = word_at(ent, 0x158);
    field<float>(ent, 0x58) = 0.0f;
    field<float>(ent, 0x54) = 0.0f;
    field<float>(ent, 0x50) = 0.0f;
    word_at(ent, 0x38) = 0;
    word_at(ent, 0x3c) = 0;
    word_at(ent, 0x40) = 0;
    if (*reinterpret_cast<const char*>(uintptr_t{word_at(def, 0x738)})) set_grenade_model(ent, def);
    title_function<void XB_CDECL(uint8_t*)>(0x002e8be0)(ent);  // G_DObjUpdate
    return ent;
}

uint8_t* XB_REGS3 XB_CDECL fire_grenade(uint32_t weapon, uint8_t* owner, const float* velocity, const float* origin,
                                        int32_t fuse) {
    // Q3's fire_grenade: think 8 at the client's cooked time (ps +0x3c, then cleared) or now +
    // fuse; class name from 0x00984b64; gravity trajectory (type 5) from the origin with the
    // velocity; angles from the velocity, pitch - 120 snapped to 16 bits, a random tumble
    // (pitch 675..765, roll 315..405 degrees/s from the shared LCG; x87 int-to-float); the def's
    // model; G_DObjUpdate. Returns the entity.
    uint8_t* const ent = spawn_in_context(entity_context(owner));
    uint8_t* const ps = pointer_at(owner, 0x23c);
    if (ps && word_at(ps, 0x3c)) {
        field<int32_t>(ent, 0x328) = field<int32_t>(ps, 0x3c) + title_global<int32_t>(0x0098540c);
        word_at(pointer_at(owner, 0x23c), 0x3c) = 0;
    } else {
        field<int32_t>(ent, 0x328) = title_global<int32_t>(0x0098540c) + fuse;
    }
    word_at(ent, 0x32c) = 8;
    ent[0] = 3;
    word_at(ent, 0xd4) = 0xa0;
    ent[3] = static_cast<uint8_t>(weapon);
    word_at(ent, 0x1a0) = word_at(owner, 0x224);
    word_at(ent, 0x2b0) = word_at(owner, 0x224);
    uint8_t* const def = weapon_def_of(weapon);
    set_classname(ent, 0x00984b64);
    set_grenade_damage(ent, def);
    word_at(ent, 0x368) = 3;
    word_at(ent, 0x36c) = 4;
    word_at(ent, 0x10) = 0x3000000;
    word_at(ent, 0x2a8) = 0x280e091;
    word_at(ent, 0x14) = 5;
    word_at(ent, 0x18) = title_global<uint32_t>(0x0098540c);
    const uint32_t* const from = reinterpret_cast<const uint32_t*>(origin);
    const uint32_t* const speed = reinterpret_cast<const uint32_t*>(velocity);
    word_at(ent, 0x20) = from[0];
    word_at(ent, 0x24) = from[1];
    word_at(ent, 0x28) = from[2];
    word_at(ent, 0x2c) = speed[0];
    word_at(ent, 0x30) = speed[1];
    word_at(ent, 0x34) = speed[2];
    word_at(ent, 0x38) = 2;
    word_at(ent, 0x3c) = title_global<uint32_t>(0x0098540c);
    float* const base = &field<float>(ent, 0x44);
    vectoangles_call(velocity, base);
    const float pitch = (base[0] - title_global<float>(0x0083eb68)) * title_global<float>(0x0083ebd8);
    base[0] = static_cast<float>(static_cast<int32_t>(pitch) & 0xffff) * title_global<float>(0x0083ebd4);
    const float tumble = title_global<float>(0x0083eb64);  // 90
    const float range = title_global<float>(0x0083ebc8);   // 32768
    const float half = title_global<float>(0x0083ef1c);    // 45
    const float first = platform_x87_int_to_float(static_cast<int32_t>(lcg_next()));
    field<float>(ent, 0x50) = ((first * tumble) / range - half) + title_global<float>(0x0083f178);
    field<float>(ent, 0x54) = 0.0f;
    const float second = platform_x87_int_to_float(static_cast<int32_t>(lcg_next()));
    field<float>(ent, 0x58) = ((second * tumble) / range - half) + title_global<float>(0x0083ebcc);
    word_at(ent, 0x140) = from[0];
    word_at(ent, 0x144) = from[1];
    word_at(ent, 0x148) = from[2];
    word_at(ent, 0x150) = word_at(ent, 0x44);
    word_at(ent, 0x154) = word_at(ent, 0x48);
    word_at(ent, 0x158) = word_at(ent, 0x4c);
    set_grenade_model(ent, def);
    title_function<void XB_CDECL(uint8_t*)>(0x002e8be0)(ent);  // G_DObjUpdate
    return ent;
}

uint32_t XB_REGS3 XB_CDECL Turret_TrackTargetEnt(uint8_t* ent, uint32_t, uint8_t* target, uint32_t aiming) {
    // A new target (info +0xc) clears info +2 bit 3 and restarts the target time (+0x1c). Its
    // origin within the arcs (Turret_CanTargetPoint): aim state and turning toward it
    // (Turret_UpdateTargetAngles, AI rates), 1; else info +2 bit 0x40 cleared, 0.
    uint8_t* info = pointer_at(ent, 0x24c);
    if (word_at(info, 0xc) != address_of(target)) {
        word_at(info, 0xc) = address_of(target);
        info[2] &= 0xf7;
        word_at(info, 0x1c) = title_global<uint32_t>(0x0098540c);
    }
    const float* const point = &field<float>(target, 0x140);
    float angles[3];
    float start[3];
    if (!Turret_CanTargetPoint(ent, point, start, angles)) {
        pointer_at(ent, 0x24c)[2] &= 0xbf;
        return 0;
    }
    Turret_UpdateAimState(aiming, 0, ent, point, angles);
    uint32_t args[2] = {address_of(ent), address_of(angles)};
    Turret_UpdateTargetAngles(args, 1, ent, angles);
    return 1;
}

PLATFORM_ASM_ENTRY(Turret_ParseInfo_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Turret_ParseInfo) "addl $8, %esp\n"
                   "ret")

namespace {

// A turret spawn key (hash cached under `bit` of 0x0098ee44 in `slot`): its value text, else null.
const char* turret_key(uint32_t bit, uint32_t slot, uint32_t text) {
    const uint32_t key = cached_key(0x0098ee44, bit, slot, text);
    const int32_t count = title_global<int32_t>(0x00985420);  // level.numSpawnVars
    for (int32_t i = 0; i < count; ++i) {
        if (key == title_global<uint32_t>(0x00985424 + static_cast<uint32_t>(i) * 8))
            return reinterpret_cast<const char*>(uintptr_t{title_global<uint32_t>(0x00985428 + static_cast<uint32_t>(i) * 8)});
    }
    return nullptr;
}

// A float turret key into `out`: the text by the title's atof, else the weapon def's word.
void turret_float(uint8_t* out, uint32_t bit, uint32_t slot, uint32_t text, const uint8_t* def, uint32_t offset) {
    const char* const value = turret_key(bit, slot, text);
    if (value) {
        *reinterpret_cast<float*>(out) = title_function<float XB_CDECL(const char*)>(0x001ef056)(value);  // atof
        return;
    }
    *reinterpret_cast<float*>(out) = 0.0f;
    *reinterpret_cast<uint32_t*>(out) = *reinterpret_cast<const uint32_t*>(def + offset);
}

}  // namespace

void XB_CDECL Turret_ParseInfo(uint8_t* ent, const char* weapon) {
    // ESI entity, cdecl weapon name: a free turret info (0x0094d8e8, 80 of 0x84 bytes; none:
    // error, the last one reused) cleared and in use, the weapon by name hash (none: error), the
    // spawn keys over the weapon def's defaults (right/top arcs negated and kept <= 0, left/
    // bottom >= 0, convergence time in ms, max range squared (none: FLT_MAX), damage, accuracy
    // 0..1); health 100 when unset; then the turret entity (type 10, contents 0x200004, bounds
    // -32..32 / 0..56, stationary at its origin, think 0x10 next frame) is linked.
    uint8_t* info = &title_global<uint8_t>(0x0094d8e8);
    int32_t index = 0;
    for (uint8_t* it = info;; it += 0x84) {
        info = it;
        if (!*it) break;
        ++index;
        if (it + 0x84 >= &title_global<uint8_t>(0x00950228)) break;
    }
    if (index == 0x50) title_function<void XB_CDECL(uint32_t, uint32_t)>(0x002938f0)(1, 0x0083b1f8);  // __break
    for (uint32_t i = 0; i < 0x84; i += 4) word_at(info, i) = 0;
    word_at(ent, 0x24c) = address_of(info);
    info[0] = 1;
    word_at(info, 0x74) = 0;
    uint8_t found = 0;
    if (weapon && *weapon) {
        const int32_t last = title_global<int32_t>(0x0095f4d4);  // bg_lastWeaponIndex
        for (int32_t i = 0; i <= last; ++i) {
            if (Com_HashKeyNoCase(0, weapon) == word_at(weapon_def_of(static_cast<uint32_t>(i)), 4)) {
                found = static_cast<uint8_t>(i);
                break;
            }
        }
    }
    ent[3] = found;
    if (!found)
        title_function<void XB_CDECL(uint32_t, uint32_t, const char*)>(0x002938f0)(1, 0x0083b1cc, weapon);  // __break
    uint8_t* const def = weapon_def_of(ent[3]);
    G_RegisterWeapon(0, 0, ent[3], 1);
    *reinterpret_cast<uint16_t*>(info + 4) = 0;
    info[0x50] = def[0xa4];
    info[0x51] = 0xff;
    turret_float(info + 0x30, 1, 0x0098ee40, 0x0083b1c0, def, 0x818);  // "rightarc"
    field<float>(info, 0x30) = field<float>(info, 0x30) * title_global<float>(0x0083eb34);
    if (field<float>(info, 0x30) > 0.0f) field<float>(info, 0x30) = 0.0f;
    turret_float(info + 0x38, 2, 0x0098ee3c, 0x0083b1b8, def, 0x814);  // "leftarc"
    if (0.0f > field<float>(info, 0x38)) field<float>(info, 0x38) = 0.0f;
    turret_float(info + 0x2c, 4, 0x0098ee38, 0x0083b1b0, def, 0x81c);  // "toparc"
    field<float>(info, 0x2c) = field<float>(info, 0x2c) * title_global<float>(0x0083eb34);
    if (field<float>(info, 0x2c) > 0.0f) field<float>(info, 0x2c) = 0.0f;
    turret_float(info + 0x34, 8, 0x0098ee34, 0x0083b1a4, def, 0x820);  // "bottomarc"
    if (0.0f > field<float>(info, 0x34)) field<float>(info, 0x34) = 0.0f;
    {
        const char* const value = turret_key(0x10, 0x0098ee30, 0x0083b194);  // "convergencetime"
        float time = value ? title_function<float XB_CDECL(const char*)>(0x001ef056)(value)  // atof
                           : *reinterpret_cast<const float*>(def + 0x830);
        if (0.0f > time) time = 0.0f;
        const float ms = time * title_global<float>(0x0083eb9c) + title_global<float>(0x0083eb24);
        field<int32_t>(info, 0x44) = static_cast<int32_t>(ms);
    }
    {
        const char* const value = turret_key(0x20, 0x0098ee2c, 0x0083b188);  // "maxrange"
        const float range = value ? title_function<float XB_CDECL(const char*)>(0x001ef056)(value)  // atof
                                  : *reinterpret_cast<const float*>(def + 0x834);
        const float squared = range * range;
        field<float>(info, 0x48) = range > 0.0f ? squared : title_global<float>(0x0082eed0);
        field<float>(info, 0x3c) = 0.0f;
    }
    if (!word_at(ent, 0x350)) word_at(ent, 0x350) = 100;
    {
        const char* const value = turret_key(0x40, 0x0098ee28, 0x00836254);  // "damage"
        field<int32_t>(ent, 0x358) = value ? title_function<int32_t XB_CDECL(const char*)>(0x001eeb81)(value) : 0;  // _atol
        if (!value) word_at(ent, 0x358) = word_at(def, 0x5b8);
        if (field<int32_t>(ent, 0x358) < 0) word_at(ent, 0x358) = 0;
    }
    {
        const char* const value = turret_key(0x80, 0x0098ee24, 0x00835898);  // "accuracy"
        const float one = title_global<float>(0x0083eb20);
        field<float>(info, 0x54) = value ? title_function<float XB_CDECL(const char*)>(0x001ef056)(value) : one;  // atof
        if (!value) word_at(info, 0x54) = word_at(def, 0x824);
        if (0.0f > field<float>(info, 0x54)) {
            field<float>(info, 0x54) = 0.0f;
        } else if (field<float>(info, 0x54) > one) {
            field<float>(info, 0x54) = one;
        }
    }
    *reinterpret_cast<uint16_t*>(info + 2) = 0x1003;
    info[1] = 0;
    *reinterpret_cast<uint16_t*>(info + 0x52) = 0xffff;
    word_at(info, 0x64) = 3;
    word_at(ent, 0x2a8) = 1;
    word_at(ent, 0x134) = 0x200004;
    word_at(ent, 0xd4) = 0x80;
    ent[0] = 0xa;
    word_at(ent, 0x2a4) |= 0x8000;
    title_function<void XB_CDECL(uint8_t*)>(0x002e8be0)(ent);  // G_DObjUpdate
    const float side = title_global<float>(0x0083f180);
    field<float>(ent, 0xe0) = side;
    field<float>(ent, 0xe4) = side;
    field<float>(ent, 0xe8) = 0.0f;
    field<float>(ent, 0xf0) = title_global<float>(0x0083eeac);
    field<float>(ent, 0xf4) = title_global<float>(0x0083eeac);
    field<float>(ent, 0xf8) = title_global<float>(0x0083f17c);
    for (uint32_t i = 0; i < 12; i += 4) word_at(ent, 0x20 + i) = word_at(ent, 0x140 + i);
    for (uint32_t i = 0; i < 12; i += 4) word_at(ent, 0x14 + i) = 0;
    for (uint32_t i = 0; i < 12; i += 4) word_at(ent, 0x2c + i) = 0;
    for (uint32_t i = 0; i < 12; i += 4) word_at(ent, 0x44 + i) = word_at(ent, 0x150 + i);
    for (uint32_t i = 0; i < 12; i += 4) word_at(ent, 0x38 + i) = 0;
    for (uint32_t i = 0; i < 12; i += 4) word_at(ent, 0x50 + i) = 0;
    for (uint32_t i = 0; i < 12; i += 4) word_at(ent, 0x90 + i) = 0;
    word_at(ent, 0x32c) = 0x10;
    word_at(ent, 0x38) = 3;
    word_at(ent, 0x328) = title_global<uint32_t>(0x0098540c) + 1;
    word_at(ent, 0x34c) = 1;
    word_at(ent, 0x33c) = 4;
    word_at(ent, 0x29c) = 1;
    word_at(ent, 0x348) = 2;
    SV_LinkEntity(ent);
}

uint32_t XB_CDECL Turret_GetTargetAimPoint(uint8_t* ent, uint8_t* sentient, float* eye, float* flash, float* angles) {
    // The sentient's eye origin (out `eye`) seen from the turret's "tag_flash" (out `flash`):
    // yaw within the info's arc (+0x30..+0x38, relative to the base angles +0x150) or 0; the
    // pitch (+0x2c..+0x34) to the sentient's entity origin 2 units up (state byte 1: after the
    // eye's pitch) picks the aim: inside the arc the entity point (into `eye`), past one edge
    // the pitch clamped to 0 at the flash height, else 0. Angles out: pitch, yaw.
    Sentient_EyeOrigin(sentient, 0, reinterpret_cast<uint8_t*>(eye));
    const uint32_t tag = cached_key(0x0098ee4c, 1, 0x0098ee48, 0x008389c0);  // "tag_flash"
    const DObj* const obj = reinterpret_cast<const DObj*>(pointer_at(ent, 0x22c));
    if (!obj) return 0;
    const int32_t index = DObj_GetTagIndex(obj, tag);
    if (index < 0) return 0;
    alignas(16) float m[16];  // origin row at +0x30
    title_function<uint32_t XB_CDECL(uint8_t*, int32_t, float*)>(0x002e3780)(ent, index, m);  // G_DObjGetWorldTagMatrix
    if (word_at(sentient, 0x30) == 1) return 0;
    const float delta[3] = {eye[0] - m[12], eye[1] - m[13], eye[2] - m[14]};
    uint8_t* const info = pointer_at(ent, 0x24c);
    float view[3];
    vectoangles_2(delta, view);
    angles[1] = Q_fmod(view[1], field<float>(ent, 0x154));
    if (angles[1] > field<float>(info, 0x38)) return 0;
    if (field<float>(info, 0x30) > angles[1]) return 0;
    flash[0] = m[12];
    flash[1] = m[13];
    flash[2] = m[14];
    uint8_t* const target = pointer_at(sentient, 0);
    const float up = title_global<float>(0x0083eba8);
    auto target_pitch = [&]() {
        float d[3];
        d[1] = field<float>(target, 0x144) - m[13];
        d[0] = field<float>(target, 0x140) - m[12];
        d[2] = (field<float>(target, 0x148) - m[14]) + up;
        return Q_fmod(vectopitch(d), field<float>(ent, 0x150));
    };
    auto aim_at_target = [&]() {
        word_at(reinterpret_cast<uint8_t*>(eye), 0) = word_at(target, 0x140);
        word_at(reinterpret_cast<uint8_t*>(eye), 4) = word_at(target, 0x144);
        eye[2] = field<float>(target, 0x148) + up;
        return 1u;
    };
    auto clamp_pitch = [&]() {
        angles[0] = 0.0f;
        eye[2] = m[14];
        return 1u;
    };
    bool high;
    if (info[1] != 1) {
        angles[0] = target_pitch();
        if (angles[0] > field<float>(info, 0x34)) {
            high = true;
        } else if (field<float>(info, 0x2c) > angles[0]) {
            high = false;
        } else {
            return aim_at_target();
        }
        angles[0] = Q_fmod(view[0], field<float>(ent, 0x150));
    } else {
        angles[0] = Q_fmod(view[0], field<float>(ent, 0x150));
        if (angles[0] > field<float>(info, 0x34)) {
            high = true;
        } else if (field<float>(info, 0x2c) > angles[0]) {
            high = false;
        } else {
            return 1;
        }
        angles[0] = target_pitch();
        if (!(angles[0] > field<float>(info, 0x34)) && !(field<float>(info, 0x2c) > angles[0])) return aim_at_target();
    }
    if (angles[0] > field<float>(info, 0x34)) return high ? 0 : clamp_pitch();
    if (field<float>(info, 0x2c) > angles[0]) return high ? clamp_pitch() : 0;
    return 1;
}

PLATFORM_ASM_ENTRY(weapon_grenadelauncher_fire_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(weapon_grenadelauncher_fire) "addl $12, %esp\n"
                   "ret")

uint8_t* XB_CDECL weapon_grenadelauncher_fire(const float* params, uint8_t* owner, uint32_t weapon) {
    // EAX fire parameters (forward +0, muzzle +0x24, weapon def +0x3c), EDI owner: a grenade
    // (fuse 5000) at the muzzle pushed 0x008fefe4 along the forward, launched at the def's speed
    // (+0x730) plus its upward speed (+0x734); the owner's velocity along the launch direction
    // is added to the grenade's.
    const uint8_t* const def = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(
        *reinterpret_cast<const uint32_t*>(params + 15)));
    const float speed = static_cast<float>(*reinterpret_cast<const int32_t*>(def + 0x730));
    float velocity[3];
    velocity[0] = speed * params[0];
    velocity[1] = params[1] * speed;
    velocity[2] = static_cast<float>(*reinterpret_cast<const int32_t*>(def + 0x734)) + params[2] * speed;
    const float push = title_global<float>(0x008fefe4);
    float origin[3];
    origin[0] = params[0] * push + params[9];
    origin[1] = params[1] * push + params[10];
    origin[2] = params[2] * push + params[11];
    uint8_t* const ent = fire_grenade(weapon, owner, velocity, origin, 0x1388);
    const float length = title_ci_float(0x0033acf4, velocity[2] * velocity[2] + velocity[1] * velocity[1] +
                                                         velocity[0] * velocity[0]);  // __CIsqrt
    float dir[3] = {velocity[0], velocity[1], velocity[2]};
    if (length != 0.0f) {
        const float inv = title_global<float>(0x0083eb20) / length;
        dir[0] = inv * velocity[0];
        dir[2] = inv * velocity[2];
        dir[1] = inv * velocity[1];
    }
    uint8_t* const ps = pointer_at(owner, 0x23c);
    const float along = field<float>(ps, 0x18) * dir[2] + field<float>(ps, 0x14) * dir[1] + field<float>(ps, 0x10) * dir[0];
    const float x = along * dir[0] + field<float>(ent, 0x2c);
    const float y = along * dir[1] + field<float>(ent, 0x30);
    const float z = along * dir[2] + field<float>(ent, 0x34);
    field<float>(ent, 0x2c) = x;
    field<float>(ent, 0x30) = y;
    field<float>(ent, 0x34) = z;
    return ent;
}

PLATFORM_ASM_ENTRY(G_FindUnstuckPosition_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %eax, %ebp\n"
                   "pushl (%esp)\n"
                   "pushl %edi\n"
                   "pushl %ebx\n"
                   "pushl %esi\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(G_FindUnstuckPosition) "addl $20, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret")

uint32_t XB_CDECL G_FindUnstuckPosition(uint32_t entry, uint8_t* ent, uint32_t ebx, uint32_t edi, uint32_t ebp) {
    // ESI entity: around its origin at radius 1, 33, .. 129 in the 26 directions of 0x0082fe00, from
    // 32 up (rising by 16 up to four times while the box starts solid), a box trace 16 up and one
    // 64 down; the first clear drop moves the entity (origin, +0x20, the player state's origin)
    // there: 1, else 0. SV_Trace runs where the original's call puts it (frame: aligned base -
    // 0x90; start +0x20, end +0x30, drop end +0x40, results +0x50).
    auto word = [](uint32_t address) -> uint32_t& { return *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(address)); };
    auto real = [](uint32_t address) -> float& { return *reinterpret_cast<float*>(static_cast<uintptr_t>(address)); };
    word(entry - 4) = ebp;
    const uint32_t frame = ((entry - 4) & ~15u) - 0x90;
    word(frame) = edi;
    word(frame + 4) = ebx;
    const uint32_t start = frame + 0x20, end = frame + 0x30, drop = frame + 0x40, results = frame + 0x50;
    const uint32_t mins = address_of(ent + 0xe0), maxs = address_of(ent + 0xf0), self = address_of(ent);
    auto trace = [&](uint32_t to, uint32_t ret) {
        const TitleRegs regs{results, start, word_at(ent, 0x2a8), to, self, maxs};
        const uint32_t args[11] = {results, mins, maxs, to, word_at(ent, 0x224), word_at(ent, 0x2a8), 1, 0, 0, 0, 0};
        title_call_at_regs(frame, 0x002e9af0, &regs, 11, args);  // SV_Trace
        word(frame - 0x30) = ret;
        return reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(results))[0x3d] != 0;
    };
    for (word(frame + 0x14) = 1; static_cast<int32_t>(word(frame + 0x14)) <= 0xa0; word(frame + 0x14) += 0x20) {
        const float radius = static_cast<float>(static_cast<int32_t>(word(frame + 0x14)));
        real(frame + 0x1c) = radius;
        for (word(frame + 0x10) = 0; word(frame + 0x10) < 0x138; word(frame + 0x10) += 0xc) {
            const float* const dir = &title_global<float>(0x0082fe00 + word(frame + 0x10));
            const float z = dir[2] * radius + field<float>(ent, 0x148) + title_global<float>(0x0083eeac);
            const float x = dir[0] * radius + field<float>(ent, 0x140);
            const float y = dir[1] * radius + field<float>(ent, 0x144);
            real(start + 8) = z;
            real(start) = x;
            real(start + 4) = y;
            real(end) = x;
            real(end + 4) = y;
            real(end + 8) = z + title_global<float>(0x0083ebb8);
            bool clear = false;
            for (word(frame + 0x18) = 0;;) {
                if (!trace(end, 0x00261ee3)) {
                    clear = true;
                    break;
                }
                const float step = title_global<float>(0x0083ebb8);
                real(start + 8) = real(start + 8) + step;
                real(end + 8) = real(end + 8) + step;
                word(frame + 0x18) += 0x10;
                if (static_cast<int32_t>(word(frame + 0x18)) > 0x40) break;
            }
            if (!clear) continue;
            word(drop) = word(start);
            word(drop + 4) = word(start + 4);
            real(drop + 8) = real(start + 8) - title_global<float>(0x00830c1c);
            if (trace(drop, 0x00261f8a)) continue;
            const uint32_t p[3] = {word(results), word(results + 4), word(results + 8)};
            for (uint32_t i = 0; i < 3; ++i) word_at(ent, 0x140 + i * 4) = p[i];
            for (uint32_t i = 0; i < 3; ++i) word_at(ent, 0x20 + i * 4) = p[i];
            if (word_at(ent, 0x23c)) {
                for (uint32_t i = 0; i < 3; ++i) word_at(pointer_at(ent, 0x23c), i * 4) = p[i];
            }
            return 1;
        }
    }
    return 0;
}

namespace {

// G_Damage(target, inflictor = attacker = mover, no direction, damage, mod 0x10, no
// location), EAX = no point.
void mover_crush(uint8_t* target, uint8_t* mover, uint32_t damage) {
    const TitleRegs regs{0, 0, 0, 0, 0, 0};
    const uint32_t args[9] = {address_of(target), address_of(mover), address_of(mover), 0, damage, 0, 0x10, 0,
                              0xffffffffu};
    g_ent_call(0x00250340, &regs, 9, args);  // G_Damage
}

void free_blocker(uint8_t* other) {
    title_function<void XB_CDECL(uint8_t*, uint32_t)>(0x002be420)(other, 0);  // G_FreeEntity
}

}  // namespace

void XB_CDECL Blocked_DoorRotate(uint8_t* ent, uint8_t* other) {
    // A rotating door's blocked callback: a blocker without a client (+0x244) is freed; a dead
    // one (health +0x350 <= 0) is killed (99999), then the door's damage (+0x358) applies.
    // Then every team member reverses its rotation (state 9 <-> 10, from the mirrored time:
    // 2 * now - duration - start) by SetMoverState (inlined there) and is linked again.
    if (other) {
        if (!word_at(other, 0x244)) {
            free_blocker(other);
            return;
        }
        if (field<int32_t>(other, 0x350) <= 0) mover_crush(other, ent, 99999);
        if (word_at(ent, 0x358)) mover_crush(other, ent, word_at(ent, 0x358));
    }
    for (uint8_t* member = ent; member; member = pointer_at(member, 0x37c)) {
        const int32_t time = static_cast<int32_t>(title_global<uint32_t>(0x0098540c) * 2 - word_at(member, 0x40) -
                                                 word_at(member, 0x3c));
        SetMoverState(time, member[0x298] == 9 ? 10 : 9, member);
        SV_LinkEntity(member);
    }
}

void XB_CDECL Blocked_Door(uint8_t* ent, uint8_t* other) {
    // Q3's Blocked_Door: a blocker without a client (+0x244) is freed - a "chair" item (type 4)
    // is destroyed by damage (99999) instead; others take the door's damage (+0x358). Unless
    // crusher (spawn flag 4), every team member reverses (1to2 <-> 2to1 from the mirrored time)
    // by SetMoverState (inlined there) and is linked again.
    if (other) {
        if (!word_at(other, 0x244)) {
            if (other[0] == 4) {
                const uint32_t name = word_at(other, 0x25c);
                const char* const text = name ? reinterpret_cast<const char*>(uintptr_t{name + 6}) : nullptr;
                if (title_function<const char* XB_CDECL(const char*, const char*)>(0x001efb10)(
                        text, reinterpret_cast<const char*>(uintptr_t{0x0083b06c}))) {  // _strstr "chair"
                    mover_crush(other, ent, 99999);
                    return;
                }
            }
            free_blocker(other);
            return;
        }
        if (word_at(ent, 0x358)) mover_crush(other, ent, word_at(ent, 0x358));
    }
    if (ent[0x2a0] & 4) return;
    for (uint8_t* member = ent; member; member = pointer_at(member, 0x37c)) {
        const int32_t time = static_cast<int32_t>(title_global<uint32_t>(0x0098540c) * 2 - word_at(member, 0x1c) -
                                                 word_at(member, 0x18));
        SetMoverState(time, member[0x298] == 3 ? 4 : 3, member);
        SV_LinkEntity(member);
    }
}

void XB_CDECL Reached_BinaryMover(uint8_t* ent) {
    // Q3's Reached_BinaryMover (reached callback): 1to2 -> pos2 (state 1), 2to1 -> pos1 (0), the
    // rotations 9 -> 8 and 10 -> 7, each by the inlined SetMoverState. At pos2 / rotated open:
    // the activator (+0x378) defaults to the mover; spawn flag 0x80 returns at once (think
    // 0x14 / 0x15 now), else after the wait (+0x384 ms; -1000: stays). Back at pos1 the team
    // master closes its area portal; back unrotated it drops +0x2a4 bit 0x100 and closes it.
    const uint8_t state = ent[0x298];
    ent[1] = 0;
    const int32_t now = title_global<int32_t>(0x0098540c);
    auto wait_return = [&](uint32_t think) {
        if (!word_at(ent, 0x378)) word_at(ent, 0x378) = address_of(ent);
        if (static_cast<int8_t>(ent[0x2a4]) < 0) {
            ent[0x297] = 0;
            word_at(ent, 0x32c) = think;
            word_at(ent, 0x328) = 0;
            return;
        }
        if (field<float>(ent, 0x384) == title_global<float>(0x0083ec74)) return;  // -1000: never
        word_at(ent, 0x32c) = think;
        field<int32_t>(ent, 0x328) = static_cast<int32_t>(field<float>(ent, 0x384)) + title_global<int32_t>(0x0098540c);
    };
    auto team_master = [&]() {
        const uint32_t master = word_at(ent, 0x380);
        return master == address_of(ent) || !master;
    };
    switch (state) {
    case 3:
        SetMoverState(now, 1, ent);
        wait_return(0x14);
        return;
    case 4:
        SetMoverState(now, 0, ent);
        if (team_master() && field<int32_t>(ent, 0x200) != -1)
            CM_AdjustAreaPortalState(field<int32_t>(ent, 0x1fc), field<int32_t>(ent, 0x200), 0);
        return;
    case 9:
        SetMoverState(now, 8, ent);
        wait_return(0x15);
        return;
    case 10:
        SetMoverState(now, 7, ent);
        word_at(ent, 0x2a4) &= ~0x100u;
        if (team_master()) SV_AdjustAreaPortalState(ent, 0, 0);
        return;
    default:
        title_function<void XB_CDECL(uint32_t)>(0x00248af0)(0x0083b084);  // G_Error "bad moverState"
        return;
    }
}

PLATFORM_ASM_ENTRY(Mover_ReverseTeam_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ebx\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(Mover_ReverseTeam) "addl $12, %esp\n"
                   "ret")

void XB_CDECL Mover_ReverseTeam(uint8_t* leader, int32_t time, uint32_t state) {
    // EDI team leader, EBX time, cdecl state: every team member (+0x37c chain) turns its
    // rotation (+0x2f4) around, takes the leader's +0x2a4 bit 0x100 and goes to `state`.
    for (uint8_t* member = leader; member; member = pointer_at(member, 0x37c)) {
        field<float>(member, 0x2f4) = field<float>(member, 0x2f4) * title_global<float>(0x0083eb34);
        if (word_at(leader, 0x2a4) & 0x100) word_at(member, 0x2a4) |= 0x100;
        SetMoverState(time, state, member);
    }
}

PLATFORM_ASM_ENTRY(MatchTeam_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(MatchTeam) "addl $12, %esp\n"
                   "ret")

void XB_CDECL MatchTeam(uint8_t* leader, uint32_t state, int32_t time) {
    // Q3's MatchTeam: EDI team leader, cdecl (state, time); each member (+0x37c chain) takes the
    // leader's +0x2a4 bit 0x100 and goes to `state`.
    for (uint8_t* member = leader; member; member = pointer_at(member, 0x37c)) {
        if (word_at(leader, 0x2a4) & 0x100) word_at(member, 0x2a4) |= 0x100;
        SetMoverState(time, state, member);
    }
}

void XB_CDECL SP_turret(uint8_t* ent) {
    // The "weaponinfo" spawn key (cached under 0x0098f128 bit 1) names the turret's weapon; a
    // missing key is an error (then the empty name 0x0082dfeb). Turret_ParseInfo does the rest.
    const uint32_t key = cached_key(0x0098f128, 1, 0x0098f124, 0x0083b278);
    const int32_t count = title_global<int32_t>(0x00985420);
    for (int32_t i = 0; i < count; ++i) {
        if (key != title_global<uint32_t>(0x00985424 + static_cast<uint32_t>(i) * 8)) continue;
        Turret_ParseInfo(ent, reinterpret_cast<const char*>(
                                  uintptr_t{title_global<uint32_t>(0x00985428 + static_cast<uint32_t>(i) * 8)}));
        return;
    }
    title_function<void XB_CDECL(uint32_t, uint32_t)>(0x002938f0)(1, 0x0083b164);  // __break
    Turret_ParseInfo(ent, reinterpret_cast<const char*>(uintptr_t{0x0082dfeb}));
}

void XB_CDECL Think_MatchTeam(uint8_t* ent) {
    // The team follows the entity's own state (MatchTeam now).
    MatchTeam(ent, ent[0x298], title_global<int32_t>(0x0098540c));
}

void XB_CDECL ReturnToPos1Rotate(uint8_t* ent) {
    // The team rotates back (state 10, MatchTeam now).
    MatchTeam(ent, 10, title_global<int32_t>(0x0098540c));
}

void XB_CDECL Think_MatchTeam2To3(uint8_t* ent) {
    // The team moves pos2 -> pos3 (state 5, MatchTeam now).
    MatchTeam(ent, 5, title_global<int32_t>(0x0098540c));
}

void XB_CDECL Think_MatchTeam3To2(uint8_t* ent) {
    // The team moves pos3 -> pos2 (state 6, MatchTeam now).
    MatchTeam(ent, 6, title_global<int32_t>(0x0098540c));
}

void XB_CDECL ReturnToPos1(uint8_t* ent) {
    // Q3's ReturnToPos1: the team moves back (state 4, MatchTeam now).
    MatchTeam(ent, 4, title_global<int32_t>(0x0098540c));
}

uint32_t XB_CDECL Artillery_Incoming(uint8_t* ent) {
    // The incoming whistle (0x0083b140 alias on the entity, FXCmd_PlaySoundAlias) and think 9 in 1100 ms.
    word_at(ent, 0x328) = title_global<uint32_t>(0x0098540c) + 0x44c;
    word_at(ent, 0x32c) = 9;
    const TitleRegs regs{word_at(ent, 0x328), 0, address_of(ent), 0, 0, 0};
    const uint32_t args[4] = {0x0083b140, 0, 0xffffffffu, 0};
    return g_ent_call(0x002a6990, &regs, 4, args);  // FXCmd_PlaySoundAlias (ECX entity)
}
namespace {

uint32_t& stack_at(uint32_t address) { return *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(address)); }
float& stack_float(uint32_t address) { return *reinterpret_cast<float*>(static_cast<uintptr_t>(address)); }

// A title callee where the original's call puts it (ESP = top - 4 * count), with the original's
// return address written back into the dead stack.
uint32_t call_at(uint32_t top, uint32_t ret, uint32_t function, const TitleRegs& regs, uint32_t count,
                 const uint32_t* args) {
    const uint32_t result = title_call_at_regs(top, function, &regs, count, args);
    stack_at(top - count * 4 - 4) = ret;
    return result;
}

}  // namespace

// Every title callee runs where the original's call puts it, with its registers (the sight trace
// reads the unset fourth words of its vectors): frame = aligned base - 0xa0 (box +0x10, angles/
// hit +0x24, start +0x30, zero box +0x40, eye +0x50, tag matrix +0x60).
PLATFORM_ASM_ENTRY(Actor_TurretCanTargetEnemy_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %eax, %ebp\n"
                   "pushl (%esp)\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl 8(%ebp)\n"
                   "pushl 4(%ebp)\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(Actor_TurretCanTargetEnemy) "addl $28, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret")

uint32_t XB_CDECL Actor_TurretCanTargetEnemy(uint32_t entry, uint8_t* turret, uint8_t* actor, uint32_t ebx, uint32_t esi,
                                             uint32_t edi, uint32_t ebp) {
    // 1 (keep the turret) without an enemy (sentient +0x3c), when the actor's memory of it (per
    // sentient at +0x850, 0x50 each) is 5 s old, the enemy is out of the turret's range or 64
    // units from where it was last seen, or (not seen: memory byte 0) the enemy is ahead of the
    // turret, or (seen) the turret can aim at it. Else, from 30 units beside the "tag_weapon"
    // point (away from "tag_aim"), a sight trace (contents 0x801003) to the enemy's eye: 1 when
    // blocked; 0 without the tags.
    stack_at(entry - 4) = ebp;
    const uint32_t frame = ((entry - 4) & ~15u) - 0xa0;
    stack_at(frame) = edi;
    stack_at(frame + 4) = esi;
    stack_at(frame + 8) = ebx;
    uint8_t* const enemy = pointer_at(pointer_at(actor, 4), 0x3c);
    if (!enemy) return 1;
    const int32_t index = static_cast<int32_t>(address_of(enemy) - title_global<uint32_t>(0x00985378)) / 0x60;
    uint8_t* const memory = actor + 0x850 + index * 0x50;
    const int32_t age = title_global<int32_t>(0x0098540c) - field<int32_t>(memory, 0x18);
    if (age >= 5000) return 1;
    uint8_t* const enemy_ent = pointer_at(enemy, 0);
    const float dz = field<float>(enemy_ent, 0x148) - field<float>(turret, 0x148);
    const float dy = field<float>(enemy_ent, 0x144) - field<float>(turret, 0x144);
    const float dx = field<float>(enemy_ent, 0x140) - field<float>(turret, 0x140);
    uint8_t* const info = pointer_at(turret, 0x24c);
    stack_float(frame + 0x18) = dz;
    stack_float(frame + 0x14) = dy;
    const float d2 = dz * dz + dy * dy + dx * dx;
    stack_float(frame + 0x10) = dx;
    if (d2 >= field<float>(info, 0x48)) return 1;
    const float ey = field<float>(memory, 0x24) - field<float>(enemy_ent, 0x144);
    const float ex = field<float>(memory, 0x20) - field<float>(enemy_ent, 0x140);
    if (ey * ey + ex * ex >= title_global<float>(0x0083ed4c)) return 1;
    const uint32_t self_ent = address_of(enemy);
    uint32_t ecx = 0;
    if (memory[0]) {
        {
            const TitleRegs regs{self_ent, self_ent, frame + 0x10, static_cast<uint32_t>(age), address_of(actor),
                                 address_of(turret)};
            call_at(frame, 0x00262f1a, 0x002066e0, regs, 0, nullptr);  // Sentient_EyeOrigin
        }
        stack_at(frame + 0x10) = stack_at(frame + 0x30);
        stack_at(frame + 0x14) = stack_at(frame + 0x34);
        stack_at(frame + 0x18) = stack_at(frame + 0x38);
        const TitleRegs regs{frame + 0x24, self_ent, frame + 0x10, frame + 0x30, address_of(actor), address_of(turret)};
        const uint32_t args[5] = {address_of(turret), self_ent, frame + 0x30, frame + 0x10, frame + 0x24};
        const uint32_t aimed = call_at(frame, 0x00262f54, 0x0025f310, regs, 5, args);  // Turret_GetTargetAimPoint
        stack_at(frame + 0x30) = stack_at(frame + 0x10);
        stack_at(frame + 0x34) = stack_at(frame + 0x14);
        stack_at(frame + 0x38) = stack_at(frame + 0x18);
        if (aimed) return 1;
    } else {
        const TitleRegs regs{address_of(memory), self_ent, address_of(enemy_ent), address_of(info), frame + 0x24,
                             address_of(turret + 0x150)};
        call_at(frame, 0x00262f9f, 0x00013c60, regs, 0, nullptr);  // AnglesToForward
        const float dot = stack_float(frame + 0x2c) * stack_float(frame + 0x18) +
                          stack_float(frame + 0x28) * stack_float(frame + 0x14) +
                          stack_float(frame + 0x24) * stack_float(frame + 0x10);
        if (dot >= 0.0f) return 1;
    }
    const uint32_t weapon_tag = cached_key(0x0098f134, 1, 0x0098f130, 0x00838914);  // "tag_weapon"
    ecx = title_global<uint8_t>(0x0098f134);
    const uint32_t matrix = frame + 0x60;
    {
        const TitleRegs regs{matrix, self_ent, ecx, 0, weapon_tag, address_of(turret)};
        const uint32_t args[1] = {matrix};
        if (!call_at(frame, 0x00263013, 0x002e6420, regs, 1, args)) return 0;  // G_DObjTryGetTagMatrix
    }
    stack_at(frame + 0x30) = stack_at(matrix + 0x30);
    stack_at(frame + 0x34) = stack_at(matrix + 0x34);
    const uint32_t aim_tag = cached_key(0x0098f134, 2, 0x0098f12c, 0x00839a18);  // "tag_aim"
    ecx = title_global<uint8_t>(0x0098f134);
    uint32_t found;
    {
        const TitleRegs regs{matrix, self_ent, ecx, 0, aim_tag, address_of(turret)};
        const uint32_t args[1] = {matrix};
        found = call_at(frame, 0x00263077, 0x002e6420, regs, 1, args);  // G_DObjTryGetTagMatrix
        if (!found) return 0;
    }
    stack_at(frame + 0x38) = stack_at(matrix + 0x38);
    stack_float(frame + 0x24) = stack_float(frame + 0x30) - stack_float(matrix + 0x30);
    stack_float(frame + 0x28) = stack_float(frame + 0x34) - stack_float(matrix + 0x34);
    {
        const TitleRegs regs{found, self_ent, 0, 0, frame + 0x24, address_of(turret)};
        call_at(frame, 0x002630c9, 0x00297700, regs, 0, nullptr);  // Vec2Normalize
    }
    const float side = title_global<float>(0x0083ee38);
    stack_float(frame + 0x30) = stack_float(frame + 0x24) * side + stack_float(frame + 0x30);
    stack_float(frame + 0x34) = stack_float(frame + 0x28) * side + stack_float(frame + 0x34);
    {
        const TitleRegs regs{self_ent, self_ent, 0, 0, frame + 0x50, address_of(turret)};
        call_at(frame, 0x00263108, 0x00207d40, regs, 0, nullptr);  // Sentient_EyeOrigin_2
    }
    for (uint32_t i = 0; i < 16; i += 4) {
        stack_at(frame + 0x40 + i) = title_global<uint32_t>(0x0082e290 + i);  // (0, 0, 0, 1)
        stack_at(frame + 0x10 + i) = title_global<uint32_t>(0x0082e290 + i);
    }
    stack_at(frame + 0x24) = 0;
    const uint32_t actor_ent = word_at(actor, 0);
    const uint32_t actor_num = word_at(pointer_at(actor, 0), 0x224);
    const TitleRegs regs{frame + 0x50, frame + 0x40, actor_num, actor_ent, title_global<uint32_t>(0x0082e29c), frame + 0x30};
    const uint32_t args[6] = {frame + 0x24, frame + 0x10, actor_num, word_at(enemy_ent, 0x224), 0x801003, 0};
    call_at(frame, 0x0026317e, 0x002e9840, regs, 6, args);  // G_SightTrace
    return stack_at(frame + 0x24) != 0;
}

// As Actor_TurretCanTargetEnemy: every title callee at the original's ESP with its registers.
// Frame = aligned base - 0x80 (hit +0xc, fraction +0x10, eye +0x14, flash/center +0x20,
// angles/extents +0x30, to +0x50, from +0x60, squared distance +0x70).
PLATFORM_ASM_ENTRY(Turret_CanTargetSentient_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %eax, %ebp\n"
                   "pushl (%esp)\n"
                   "pushl %edx\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl 12(%ebp)\n"
                   "pushl 8(%ebp)\n"
                   "pushl 4(%ebp)\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(Turret_CanTargetSentient) "addl $36, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret")

uint32_t XB_CDECL Turret_CanTargetSentient(uint32_t entry, uint8_t* turret, uint8_t* target, float* out_dist2,
                                           uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t edx, uint32_t ebp) {
    // 1 when the turret can shoot the entity: not a client in a vehicle, within the info's range
    // (+0x48; the packed squared distance out), aimable (a sentient (+0x244): its aim point and
    // eye; else Turret_CanTargetPoint at its origin), the cached blocker (info +0x74, contents
    // 0x801003, box overlap) not clipping the flash -> target segment (type 9 through
    // CM_ClipSegmentToBrush, others through vtable +0x14), and a clear sight trace (its blocker
    // cached in +0x74).
    stack_at(entry - 4) = ebp;
    const uint32_t frame = ((entry - 4) & ~15u) - 0x80;
    stack_at(frame) = edi;
    stack_at(frame + 4) = esi;
    stack_at(frame + 8) = ebx;
    uint8_t* const info = pointer_at(turret, 0x24c);
    const uint32_t ent = address_of(target), info_word = address_of(info);
    if (word_at(target, 0x23c)) {
        const TitleRegs regs{word_at(target, 0x23c), ent, ent, edx, info_word, edi};
        if (call_at(frame, 0x002635e9, 0x0024bad0, regs, 0, nullptr)) return 0;  // G_ClientInVehicle
    }
    {
        float d[4];
        for (uint32_t i = 0; i < 4; ++i) d[i] = field<float>(turret, 0x140 + i * 4) - field<float>(target, 0x140 + i * 4);
        for (uint32_t i = 0; i < 4; ++i) d[i] = d[i] * d[i];
        stack_float(frame + 0x70) = d[0] + d[1] + d[2];
        for (uint32_t i = 1; i < 4; ++i) stack_float(frame + 0x70 + i * 4) = d[i];
    }
    if (stack_float(frame + 0x70) >= field<float>(info, 0x48)) return 0;
    float tx, ty, tz;
    const uint32_t sentient = word_at(target, 0x244);
    if (sentient) {
        {
            const TitleRegs regs{sentient, ent, address_of(turret), frame + 0x14, info_word, edi};
            const uint32_t args[5] = {address_of(turret), sentient, frame + 0x14, frame + 0x20, frame + 0x30};
            if (!call_at(frame, 0x00263659, 0x0025f310, regs, 5, args)) return 0;  // Turret_GetTargetAimPoint
        }
        const TitleRegs regs{word_at(target, 0x244), ent, frame + 0x14, 0, info_word, edi};
        call_at(frame, 0x00263673, 0x002066e0, regs, 0, nullptr);  // Sentient_EyeOrigin
        tx = stack_float(frame + 0x14);
        ty = stack_float(frame + 0x18);
        tz = stack_float(frame + 0x1c);
    } else {
        const uint32_t origin = address_of(target + 0x140);
        const TitleRegs regs{frame + 0x30, ent, address_of(turret), frame + 0x20, info_word, origin};
        const uint32_t args[4] = {address_of(turret), origin, frame + 0x20, frame + 0x30};
        if (!call_at(frame, 0x00263698, 0x0025ccd0, regs, 4, args)) return 0;  // Turret_CanTargetPoint
        tx = field<float>(target, 0x140);
        ty = field<float>(target, 0x144);
        tz = field<float>(target, 0x148);
    }
    const uint32_t from = frame + 0x60, to = frame + 0x50;
    {
        const uint32_t fx = stack_at(frame + 0x20), fy = stack_at(frame + 0x24), fz = stack_at(frame + 0x28);
        stack_at(frame + 0x38) = fz;
        stack_at(frame + 0x3c) = fx;
        stack_at(frame + 0x34) = fy;
        stack_at(frame + 0x30) = fx;
        for (uint32_t i = 0; i < 16; i += 4) stack_at(from + i) = stack_at(frame + 0x30 + i);
        stack_float(frame + 0x3c) = tx;
        stack_float(frame + 0x38) = tz;
        stack_float(frame + 0x34) = ty;
        stack_float(frame + 0x30) = tx;
        for (uint32_t i = 0; i < 16; i += 4) stack_at(to + i) = stack_at(frame + 0x30 + i);
    }
    stack_at(frame + 0xc) = 0;
    uint8_t* const obj = pointer_at(info, 0x74);
    if (obj && (word_at(obj, 8) & 0x801003)) {
        for (uint32_t i = 0; i < 16; i += 4) {
            stack_at(frame + 0x20 + i) = word_at(obj, 0x10 + i);
            stack_at(frame + 0x30 + i) = word_at(obj, 0x20 + i);
        }
        uint32_t inside = 0;
        for (uint32_t k = 0; k < 4; ++k) {
            const float f = stack_float(from + k * 4), t = stack_float(to + k * 4);
            const float lo = f < t ? f : t;
            const float hi = f > t ? f : t;
            const float center = stack_float(frame + 0x20 + k * 4);
            const float a = lo - center;
            const float b = center - hi;
            const float m = b > a ? b : a;
            if (m < stack_float(frame + 0x30 + k * 4)) inside |= 1u << k;
        }
        if ((inside & 7) == 7) {
            const uint32_t vtable = word_at(obj, 0);
            const TitleRegs vregs{7, ent, address_of(obj), vtable, info_word, 0};
            const uint32_t type = call_at(frame, 0x00263796, *reinterpret_cast<const uint32_t*>(uintptr_t{vtable + 0x20}),
                                          vregs, 0, nullptr);
            uint8_t* const cur = pointer_at(info, 0x74);
            if (type == 9) {
                stack_float(frame + 0x10) = title_global<float>(0x0083eb20);
                const TitleRegs regs{from, ent, address_of(cur), to, info_word, 0};
                const uint32_t args[4] = {from, to, address_of(cur), frame + 0x10};
                if (call_at(frame, 0x002637c1, 0x002cc0e0, regs, 4, args) & 0xff) return 0;  // CM_ClipSegmentToBrush
            } else {
                stack_at(frame + 0x10) = 0;
                const uint32_t table = word_at(cur, 0);
                const TitleRegs regs{from, ent, address_of(cur), table, info_word, 0};
                const uint32_t args[5] = {from, to, frame + 0x30, frame + 0x10, 0};
                const uint32_t clipped =
                    title_call_at_regs(frame, *reinterpret_cast<const uint32_t*>(uintptr_t{table + 0x14}), &regs, 5, args);
                stack_at(frame - 0x18) = 0x002637ef;
                if (clipped & 0xff) return 0;
            }
        }
    }
    for (uint32_t i = 0; i < 16; i += 4) {
        stack_at(frame + 0x20 + i) = title_global<uint32_t>(0x0082e290 + i);  // (0, 0, 0, 1)
        stack_at(frame + 0x30 + i) = title_global<uint32_t>(0x0082e290 + i);
    }
    title_global<uint32_t>(0x0094cba0) = 0;
    const uint32_t turret_num = word_at(turret, 0x224);
    const TitleRegs regs{to, frame + 0x20, turret_num, frame + 0x30, info_word, from};
    const uint32_t args[6] = {frame + 0xc, frame + 0x30, turret_num, word_at(target, 0x224), 0x801003, 0};
    call_at(frame, 0x00263867, 0x002e9840, regs, 6, args);  // G_SightTrace
    const uint32_t hit = stack_at(frame + 0xc);
    word_at(info, 0x74) = title_global<uint32_t>(0x0094cba0);
    if (hit) return 0;
    word_at(info, 0x74) = 0;
    if (out_dist2) *out_dist2 = stack_float(frame + 0x70);
    return 1;
}

uint32_t XB_CDECL Turret_ComputeAimPoint(uint8_t* ent, uint8_t* sentient, uint32_t aiming, int32_t converge,
                                         float* angles) {
    // A new target (info +0xc) restarts the convergence (+0x1c now, +2 bit 8 cleared). The
    // target's aim point (Turret_GetTargetAimPoint; none: 0). While converging (`converge` ms
    // since +0x1c): the first time (+2 bit 8 set) a point 32 units beside the target, on the
    // side the "tag_aim" forward faces, at its origin's height, then at the eye height, kept in
    // +0x20 when Turret_TryAimAtTarget takes it (else the eye point is kept and aimed at below);
    // later the kept point. Otherwise Turret_UpdateAimState at the eye point. 1 unless no aim.
    uint8_t* const info = pointer_at(ent, 0x24c);
    if (word_at(info, 0xc) != word_at(sentient, 0)) {
        word_at(info, 0xc) = word_at(sentient, 0);
        info[2] &= 0xf7;
        word_at(info, 0x1c) = title_global<uint32_t>(0x0098540c);
    }
    float flash[4];
    alignas(16) float eye[4];
    if (!Turret_GetTargetAimPoint(ent, sentient, eye, flash, angles)) return 0;
    float point[3] = {eye[0], eye[1], eye[2]};
    if (converge && title_global<int32_t>(0x0098540c) < field<int32_t>(info, 0x1c) + converge) {
        uint16_t& flags = *reinterpret_cast<uint16_t*>(info + 2);
        if (!(flags & 8)) {
            flags = static_cast<uint16_t>(flags | 8);
            const uint32_t tag = cached_key(0x0098f148, 1, 0x0098f144, 0x00839a18);  // "tag_aim"
            alignas(16) float m[16];
            const TitleRegs regs{0, 0, 0, 0, tag, address_of(ent)};
            const uint32_t args[1] = {address_of(m)};
            if (!g_ent_call(0x002e6420, &regs, 1, args)) return 0;  // G_DObjTryGetTagMatrix
            float side[3];
            side[0] = eye[1] - m[13];
            side[1] = m[12] - eye[0];
            Vec2Normalize(side);
            const float width = title_global<float>(0x0083eeac);
            const float sx = side[0] * width;
            const float sy = side[1] * width;
            if (m[1] * sy + m[0] * sx >= 0.0f) {
                side[0] = sx + eye[0];
                side[1] = sy + eye[1];
            } else {
                side[0] = eye[0] - sx;
                side[1] = eye[1] - sy;
            }
            side[2] = field<float>(pointer_at(sentient, 0), 0x148);
            if (Turret_TryAimAtTarget(ent, side, aiming, angles)) {
                std::memcpy(info + 0x20, side, 12);
                return 1;
            }
            side[2] = eye[2];
            if (Turret_TryAimAtTarget(ent, side, aiming, angles)) {
                std::memcpy(info + 0x20, side, 8);
                field<float>(info, 0x28) = eye[2];
                return 1;
            }
            std::memcpy(info + 0x20, eye, 12);
        } else {
            eye[2] = field<float>(info, 0x28);
            eye[3] = field<float>(info, 0x20);
            eye[1] = field<float>(info, 0x24);
            eye[0] = field<float>(info, 0x20);
            if (Turret_TryAimAtTarget(ent, eye, aiming, angles)) return 1;
        }
    }
    Turret_UpdateAimState(aiming, 0, ent, point, angles);
    return 1;
}
}  // namespace game
