#include "g_mover.h"

#include <cstring>

#include "actor.h"
#include "bg_misc.h"
#include "cg_ents.h"
#include "cgame.h"
#include "dobj.h"
#include "fx.h"
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
#include "pmove.h"
#include "sv.h"
#include "title_data.h"
#include "tr_main.h"
#include "xanim.h"
#include "g_pathnode.h"
#include "script_string.h"
#include "cl.h"
#include "fx_cmd.h"
#include "g_combat.h"
#include "g_scr_builtins.h"
#include "nal_math.h"
#include "ngl.h"
#include "stream.h"

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
    const float dot = static_cast<float>(platform_x87_ld(aim[2]) * to[2] + platform_x87_ld(aim[1]) * to[1] +
                                         platform_x87_ld(aim[0]) * to[0]);
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
        g_ent_call(own_address(&BG_EvaluateTrajectory_entry), &regs, 0, nullptr);  // BG_EvaluateTrajectory
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
        g_ent_call(own_address(&BG_EvaluateTrajectory_entry), &regs, 0, nullptr);  // BG_EvaluateTrajectory
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
        ent[1] = static_cast<uint8_t>(g_ent_call(own_address(&G_FindConfigstringIndex_entry), &regs, 4, args));  // G_FindConfigstringIndex (EBX 0x40)
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
    const long double dx = platform_x87_ld(field<float>(ent, 0x2d0)) - field<float>(ent, 0x2c0);
    const long double dy = platform_x87_ld(field<float>(ent, 0x2d4)) - field<float>(ent, 0x2c4);
    const long double dz = platform_x87_ld(field<float>(ent, 0x2d8)) - field<float>(ent, 0x2c8);
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
            own_function<int32_t XB_CDECL(const char*)>(&G_Error_entry)(
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
    long double frequency = platform_x87_ld(length) * title_global<float>(0x0083ec9c);  // 3
    frequency = platform_x87_ld(title_global<float>(0x0094d450)) / frequency;            // gravity
    frequency = platform_x87_sqrt_ext(frequency) * title_global<float>(0x0083f118);              // 1 / 2pi
    const int32_t duration =
        static_cast<int32_t>(static_cast<int64_t>(platform_x87_ld(title_global<float>(0x0083eb9c)) / frequency));
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
        g_ent_call(own_address(&BG_EvaluateTrajectory_entry), &regs, 0, nullptr);  // BG_EvaluateTrajectory
    }
    const float to_short = title_global<float>(0x0083ebd8);    // 65536 / 360
    const float from_short = title_global<float>(0x0083ebd4);  // 360 / 65536
    if (field<float>(tr, 0x18) > title_global<float>(0x0083ebf8)) {
        const float pitch = Plane_PitchAlongYaw(reinterpret_cast<const float*>(tr + 0x10), angles[1]);
        float delta = static_cast<float>(title_ci_fmod(platform_x87_ld(pitch) - platform_x87_ld(angles[0]),
                                                       platform_x87_ld(title_global<double>(0x0083ed40))));
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
    if (platform_x87_sqrt_ext((x * x + y * y) + z * z) > platform_x87_ld(one)) {
        own_function<void XB_CDECL(const char*)>(&G_Error_entry)(&title_global<const char>(0x0083afd0));  // G_Error
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
    own_function<void XB_REGS1 XB_CDECL(uint8_t*)>(&G_EntUnlink)(player);  // G_EntUnlink
    alignas(16) float matrix[16];
    own_function<void XB_CDECL(uint8_t*, int32_t, float*)>(&G_DObjGetWorldTagMatrix)(vehicle, tag, matrix);  // G_DObjGetWorldTagMatrix
    TeleportPlayer(player, 0, matrix + 12);
    const TitleRegs regs{0, 0, name, address_of(player), 0, 0};
    const uint32_t args[3] = {address_of(vehicle), 0x0082eec0, 0x0082eec0};
    if (!g_ent_call(own_address(&G_EntLinkToWithOffset), &regs, 3, args))  // G_EntLinkToWithOffset
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
        platform_x87_ld(field<float>(pointer_at(other, 0x23c), 0x9c)) - field<float>(ent, 0x150), full));
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
        platform_x87_ld(field<float>(pointer_at(other, 0x23c), 0xa0)) - field<float>(ent, 0x154), full));
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
    alignas(16) float angles[3];
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
        if (bone >= 0) own_function<SetBoneRotation>(&G_DObjSetBoneRotation_entry)(ent, 0x0082eec0, angles, bone, 0);
    }
    alignas(16) float single[3];
    single[1] = angles[1];
    single[0] = 0.0f;
    single[2] = 0.0f;
    if (const DObj* const obj = reinterpret_cast<const DObj*>(pointer_at(ent, 0x22c))) {
        const int32_t bone = DObj_GetTagIndex(obj, animated_y);
        if (bone >= 0) own_function<SetBoneRotation>(&G_DObjSetBoneRotation_entry)(ent, 0x0082eec0, single, bone, 0);
    }
    single[0] = angles[0];
    single[1] = 0.0f;
    single[2] = 0.0f;
    if (const DObj* const obj = reinterpret_cast<const DObj*>(pointer_at(ent, 0x22c))) {
        const int32_t bone = DObj_GetTagIndex(obj, animated_p);
        if (bone >= 0) own_function<SetBoneRotation>(&G_DObjSetBoneRotation_entry)(ent, 0x0082eec0, single, bone, 0);
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
        own_function<void XB_CDECL(uint8_t*, int32_t, float*)>(&G_DObjGetWorldTagMatrix)(ent, tag, m);  // G_DObjGetWorldTagMatrix
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
            title_ci_fmod(platform_x87_ld(view[i]) - field<float>(ent, 0x150 + i * 4), full));
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
    own_function<void XB_CDECL(uint8_t*, int32_t, float*)>(&G_DObjGetWorldTagMatrix)(ent, tag, m);  // G_DObjGetWorldTagMatrix
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
    own_function<void XB_CDECL(uint8_t*)>(&G_DObjUpdate)(ent);  // G_DObjUpdate
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
            title_call_at_regs(entry - 4, own_address(&Path_DisconnectPathsForEntity_entry), &regs, 2, args);  // Path_DisconnectPathsForEntity
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
        own_function<void XB_STDCALL(uint32_t, uint8_t*)>(&Path_DisconnectPathsForEntity_entry)(title_global<uint32_t>(0x0095f4f4),
                                                                       door);  // Path_DisconnectPathsForEntity
        return;
    }
    const TitleRegs regs{address_of(door), 0, 0, 0, title_global<uint32_t>(0x0095f4f4), 0};
    g_ent_call(own_address(&Path_ConnectPathsForEntity_entry), &regs, 0, nullptr);  // Path_ConnectPathsForEntity
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
        g_ent_call(own_address(&BG_EvaluateTrajectory_entry), &regs, 0, nullptr);  // BG_EvaluateTrajectory
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
        g_ent_call(own_address(&BG_EvaluateTrajectory_entry), &regs, 0, nullptr);  // BG_EvaluateTrajectory
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
    return reinterpret_cast<uint8_t*>(uintptr_t{g_ent_call(own_address(&G_Spawn), &regs, 0, nullptr)});  // G_Spawn
}

// The classname script string from a global (ScriptString_Assign) and its name hash (+0x260).
void set_classname(uint8_t* ent, uint32_t name) {
    own_function<void XB_THISCALL(uint32_t, uint32_t)>(&ScriptString_Assign_entry)(address_of(ent + 0x25c), name);  // ScriptString_Assign
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
    own_function<void XB_CDECL(uint8_t*)>(&G_DObjUpdate)(ent);  // G_DObjUpdate
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
    own_function<void XB_CDECL(uint8_t*)>(&G_DObjUpdate)(ent);  // G_DObjUpdate
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
    own_function<void XB_CDECL(uint8_t*)>(&G_DObjUpdate)(ent);  // G_DObjUpdate
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
    own_function<uint32_t XB_CDECL(uint8_t*, int32_t, float*)>(&G_DObjGetWorldTagMatrix)(ent, index, m);  // G_DObjGetWorldTagMatrix
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
        title_call_at_regs(frame, own_address(&SV_Trace_entry), &regs, 11, args);  // SV_Trace
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
    g_ent_call(own_address(&G_Damage_entry), &regs, 9, args);  // G_Damage
}

void free_blocker(uint8_t* other) {
    own_function<void XB_CDECL(uint8_t*, uint32_t)>(&G_FreeEntity)(other, 0);  // G_FreeEntity
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
        own_function<void XB_CDECL(uint32_t)>(&G_Error_entry)(0x0083b084);  // G_Error "bad moverState"
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
    return g_ent_call(own_address(&FXCmd_PlaySoundAlias_entry), &regs, 4, args);  // FXCmd_PlaySoundAlias (ECX entity)
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
            call_at(frame, 0x00262f1a, own_address(&Sentient_EyeOrigin), regs, 0, nullptr);  // Sentient_EyeOrigin
        }
        stack_at(frame + 0x10) = stack_at(frame + 0x30);
        stack_at(frame + 0x14) = stack_at(frame + 0x34);
        stack_at(frame + 0x18) = stack_at(frame + 0x38);
        const TitleRegs regs{frame + 0x24, self_ent, frame + 0x10, frame + 0x30, address_of(actor), address_of(turret)};
        const uint32_t args[5] = {address_of(turret), self_ent, frame + 0x30, frame + 0x10, frame + 0x24};
        const uint32_t aimed = call_at(frame, 0x00262f54, own_address(&Turret_GetTargetAimPoint), regs, 5, args);  // Turret_GetTargetAimPoint
        stack_at(frame + 0x30) = stack_at(frame + 0x10);
        stack_at(frame + 0x34) = stack_at(frame + 0x14);
        stack_at(frame + 0x38) = stack_at(frame + 0x18);
        if (aimed) return 1;
    } else {
        const TitleRegs regs{address_of(memory), self_ent, address_of(enemy_ent), address_of(info), frame + 0x24,
                             address_of(turret + 0x150)};
        call_at(frame, 0x00262f9f, own_address(&AnglesToForward_entry), regs, 0, nullptr);  // AnglesToForward
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
        if (!call_at(frame, 0x00263013, own_address(&G_DObjTryGetTagMatrix_entry), regs, 1, args)) return 0;  // G_DObjTryGetTagMatrix
    }
    stack_at(frame + 0x30) = stack_at(matrix + 0x30);
    stack_at(frame + 0x34) = stack_at(matrix + 0x34);
    const uint32_t aim_tag = cached_key(0x0098f134, 2, 0x0098f12c, 0x00839a18);  // "tag_aim"
    ecx = title_global<uint8_t>(0x0098f134);
    uint32_t found;
    {
        const TitleRegs regs{matrix, self_ent, ecx, 0, aim_tag, address_of(turret)};
        const uint32_t args[1] = {matrix};
        found = call_at(frame, 0x00263077, own_address(&G_DObjTryGetTagMatrix_entry), regs, 1, args);  // G_DObjTryGetTagMatrix
        if (!found) return 0;
    }
    stack_at(frame + 0x38) = stack_at(matrix + 0x38);
    stack_float(frame + 0x24) = stack_float(frame + 0x30) - stack_float(matrix + 0x30);
    stack_float(frame + 0x28) = stack_float(frame + 0x34) - stack_float(matrix + 0x34);
    {
        const TitleRegs regs{found, self_ent, 0, 0, frame + 0x24, address_of(turret)};
        call_at(frame, 0x002630c9, own_address(&Vec2Normalize_entry), regs, 0, nullptr);  // Vec2Normalize
    }
    const float side = title_global<float>(0x0083ee38);
    stack_float(frame + 0x30) = stack_float(frame + 0x24) * side + stack_float(frame + 0x30);
    stack_float(frame + 0x34) = stack_float(frame + 0x28) * side + stack_float(frame + 0x34);
    {
        const TitleRegs regs{self_ent, self_ent, 0, 0, frame + 0x50, address_of(turret)};
        call_at(frame, 0x00263108, own_address(&Sentient_EyeOrigin_2_entry), regs, 0, nullptr);  // Sentient_EyeOrigin_2
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
    call_at(frame, 0x0026317e, own_address(&G_SightTrace_entry), regs, 6, args);  // G_SightTrace
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
        if (call_at(frame, 0x002635e9, own_address(&G_ClientInVehicle), regs, 0, nullptr)) return 0;  // G_ClientInVehicle
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
            if (!call_at(frame, 0x00263659, own_address(&Turret_GetTargetAimPoint), regs, 5, args)) return 0;  // Turret_GetTargetAimPoint
        }
        const TitleRegs regs{word_at(target, 0x244), ent, frame + 0x14, 0, info_word, edi};
        call_at(frame, 0x00263673, own_address(&Sentient_EyeOrigin), regs, 0, nullptr);  // Sentient_EyeOrigin
        tx = stack_float(frame + 0x14);
        ty = stack_float(frame + 0x18);
        tz = stack_float(frame + 0x1c);
    } else {
        const uint32_t origin = address_of(target + 0x140);
        const TitleRegs regs{frame + 0x30, ent, address_of(turret), frame + 0x20, info_word, origin};
        const uint32_t args[4] = {address_of(turret), origin, frame + 0x20, frame + 0x30};
        if (!call_at(frame, 0x00263698, own_address(&Turret_CanTargetPoint), regs, 4, args)) return 0;  // Turret_CanTargetPoint
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
                if (call_at(frame, 0x002637c1, own_address(&CM_ClipSegmentToBrush), regs, 4, args) & 0xff) return 0;  // CM_ClipSegmentToBrush
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
    call_at(frame, 0x00263867, own_address(&G_SightTrace_entry), regs, 6, args);  // G_SightTrace
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
            if (!g_ent_call(own_address(&G_DObjTryGetTagMatrix_entry), &regs, 1, args)) return 0;  // G_DObjTryGetTagMatrix
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

// --- Prone entry, door triggers, item drops, damage reach, projectiles, turret init ---

PLATFORM_ASM_ENTRY(Actor_EnterProne_entry,
                   "pushl %ebx\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(Actor_EnterProne) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Actor_EnterProne(uint8_t* actor, int32_t duration) {
    // The twin of Actor_ExitProne. An expired blend of the prone state (+0x198 {state, start,
    // blend}) is cleared; unless one with another duration runs (then started earlier by its
    // blend fraction), the state restarts now with BG_CheckProne into +0x194.
    uint8_t* const prone = actor + 0x198;
    const int32_t start = static_cast<int32_t>(word_at(prone, 4));
    bool restart = true;
    if (start && !word_at(prone, 0)) {
        restart = false;
        const int32_t blend = static_cast<int32_t>(word_at(prone, 8));
        if (blend > 0) {
            if (blend + start < title_global<int32_t>(0x0098540c)) word_at(prone, 8) = 0;
        } else if (blend < 0 && start - blend < title_global<int32_t>(0x0098540c)) {
            word_at(prone, 4) = 0;
            restart = true;
        }
    }
    if (restart) {
        word_at(prone, 0) = 0;
        word_at(actor, 0x19c) = title_global<uint32_t>(0x0098540c);
        word_at(actor, 0x1a0) = static_cast<uint32_t>(duration);
        alignas(16) uint32_t bounds[4];
        for (uint32_t i = 0; i < 4; ++i) bounds[i] = title_global<uint32_t>(0x0082e110 + i * 4);
        uint8_t* const ent = pointer_at(actor, 0);
        const uint32_t maxs = title_global<uint32_t>(0x0098a550);  // g_actorMaxs
        const uint32_t args[14] = {word_at(ent, 0x224), address_of(ent + 0x140), maxs, 0x41c00000, word_at(ent, 0x154),
                                   address_of(actor + 0x1a4), address_of(actor + 0x1a8), address_of(actor + 0x1ac), 1,
                                   address_of(bounds), own_address(&G_TraceCapsule), own_address(&G_Trace), 1, 0x42340000};
        const TitleRegs regs{address_of(ent + 0x140), static_cast<uint32_t>(duration), 0, maxs, address_of(prone),
                             address_of(actor)};
        word_at(actor, 0x194) = g_ent_call(own_address(&BG_CheckProne_entry), &regs, 14, args);  // BG_CheckProne (G_TraceCapsule, G_Trace)
        return;
    }
    const int32_t current = static_cast<int32_t>(word_at(actor, 0x1a0));
    if (!current || duration == current) return;
    const uint32_t bits = actor_get_prone_blend_call(prone, title_global<int32_t>(0x0098540c));
    float fraction;
    std::memcpy(&fraction, &bits, sizeof fraction);
    if (title_global<float>(0x0083eb20) > fraction) {
        const int32_t now = title_global<int32_t>(0x0098540c);
        const float elapsed = static_cast<float>(duration) * fraction;
        word_at(actor, 0x19c) = static_cast<uint32_t>(now - static_cast<int32_t>(elapsed));
    }
    word_at(actor, 0x1a0) = static_cast<uint32_t>(duration);
}

PLATFORM_ASM_ENTRY(Think_SpawnNewDoorTrigger_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Think_SpawnNewDoorTrigger) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL Think_SpawnNewDoorTrigger(uint8_t* ent) {
    // Q3's Think_SpawnNewDoorTrigger: the team (+0x37c chain) is marked (+0x29c = 1); the
    // trigger covers the team's absolute bounds (packed MINPS/MAXPS over all four lanes), grown
    // by 120 on its thinnest of x/y (z when that one is thicker); a level-context entity (the
    // inlined G_Spawn) with contents 0x40000000 and touch 8, linked; then MatchTeam now.
    for (uint8_t* e = ent; e; e = pointer_at(e, 0x37c)) word_at(e, 0x29c) = 1;
    float mins[4] = {field<float>(ent, 0x100), field<float>(ent, 0x104), field<float>(ent, 0x108), 0.0f};
    float maxs[4] = {field<float>(ent, 0x110), field<float>(ent, 0x114), field<float>(ent, 0x118), 0.0f};
    for (uint8_t* e = pointer_at(ent, 0x37c); e; e = pointer_at(e, 0x37c)) {
        const float* const lo = &field<float>(e, 0x100);
        const float* const hi = &field<float>(e, 0x110);
        for (int32_t i = 0; i < 4; ++i) {
            mins[i] = mins[i] < lo[i] ? mins[i] : lo[i];  // MINPS: the second operand on NaN
            maxs[i] = maxs[i] > lo[i] ? maxs[i] : lo[i];
            mins[i] = mins[i] < hi[i] ? mins[i] : hi[i];
            maxs[i] = maxs[i] > hi[i] ? maxs[i] : hi[i];
        }
    }
    const float dy = maxs[1] - mins[1];
    const float dx = maxs[0] - mins[0];
    const float dz = maxs[2] - mins[2];
    int32_t axis = dx > dy ? 1 : 0;
    if (maxs[axis] - mins[axis] > dz) axis = 2;
    const float pad = title_global<float>(0x0083eb68);  // 120
    maxs[axis] = maxs[axis] + pad;
    mins[axis] = mins[axis] - pad;
    const TitleRegs spawn_regs{0xffffffffu, 0, 0, 0, 0, 0};
    uint8_t* const trigger = reinterpret_cast<uint8_t*>(uintptr_t{g_ent_call(own_address(&G_Spawn), &spawn_regs, 0, nullptr)});  // G_Spawn
    for (uint32_t i = 0; i < 3; ++i) {
        field<float>(trigger, 0xe0 + i * 4) = mins[i];
        field<float>(trigger, 0xf0 + i * 4) = maxs[i];
    }
    word_at(trigger, 0x2b0) = word_at(ent, 0x224);
    word_at(trigger, 0x134) = 0x40000000;
    word_at(trigger, 0x338) = 8;
    SV_LinkEntity(trigger);
    MatchTeam(ent, ent[0x298], title_global<int32_t>(0x0098540c));
    return trigger;
}

void XB_CDECL Think_SpawnNewDoorTriggerRotate(uint8_t* ent) {
    // The rotating door's trigger (contents 0x1c0000); a door spawned open (+0x2a0 bit 1) turns.
    uint8_t* const trigger = Think_SpawnNewDoorTrigger(ent);
    word_at(trigger, 0x134) = 0x1c0000;
    if (ent[0x2a0] & 1) Door_StartRotate(ent);
}

void XB_CDECL Think_SpawnNewDoorTrigger_Entry(uint8_t* ent) { Think_SpawnNewDoorTrigger(ent); }

// Frame entries for the functions below: every title callee runs where the original's call
// puts it (title_call_at_regs), the C frame below a gap; the C body gets the entry ESP and
// the entry's callee-saved registers to write the original's saves.
PLATFORM_ASM_ENTRY(Think_DropToFloor_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x30000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Think_DropToFloor) "addl $20, %esp\n"
                   "popl %esp\n"
                   "ret")

uint32_t XB_CDECL Think_DropToFloor(uint32_t entry, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // Without a cell yet (+0x458 < 0) the cell of the origin (20 higher when outside); in
    // water-free cells (no 0x0095bf0c, cell +0x3c clear) the position resets (+0x20 = origin,
    // velocity 0). Then a box trace (0x0097a8a0 / 0x00984d00, contents 0x381c011) 128 up, 256
    // down from there, and a zero-length one at the floor: the ground entity (+0xc) and the
    // origin (+0x140, +0x20) at the end, velocity 0. Frame: aligned base - 0x70 (start +0x10,
    // end +0x20, trace +0x30).
    stack_at(entry - 4) = ebp;
    const uint32_t L = ((entry - 4) & ~15u) - 0x70;
    stack_at(L + 4) = ebx;
    stack_at(L) = esi;
    uint8_t* const ent = reinterpret_cast<uint8_t*>(uintptr_t{stack_at(entry + 4)});
    const uint32_t self = address_of(ent);
    stack_at(L + 0x60) = 0;
    if (ent && field<int32_t>(ent, 0x458) < 0) {
        for (uint32_t i = 0; i < 4; ++i) stack_at(L + 0x10 + i * 4) = word_at(ent, 0x140 + i * 4);
        const TitleRegs regs{self + 0x140, 0, word_at(ent, 0x148), L + 0x10, self, edi};
        const int32_t cell = static_cast<int32_t>(call_at(L, 0x0026567a, own_address(&CM_CellForPoint), regs, 0, nullptr));  // CM_CellForPoint
        field<int32_t>(ent, 0x458) = cell;
        if (cell < 0) {
            stack_float(L + 0x18) = stack_float(L + 0x18) + title_global<float>(0x0083eb5c);  // 20
            const TitleRegs again{static_cast<uint32_t>(cell), 0, word_at(ent, 0x148), L + 0x10, self, edi};
            field<int32_t>(ent, 0x458) =
                static_cast<int32_t>(call_at(L, 0x0026569d, own_address(&CM_CellForPoint), again, 0, nullptr));  // CM_CellForPoint
        }
    }
    uint32_t ecx = 0;
    if (!title_global<uint32_t>(0x0095bf0c)) {
        int32_t cell = field<int32_t>(ent, 0x458);
        bool reset = true;
        if (cell >= 0) {
            uint8_t* const cm = title_global<uint8_t*>(0x0095f6b8);
            if (static_cast<uint32_t>(cell) >= word_at(cm, 0x18)) cell = 0;
            reset = word_at(pointer_at(cm, 0x1c), static_cast<uint32_t>(cell) * 0x50 + 0x3c) == 0;
        }
        if (reset) {
            for (uint32_t i = 0; i < 3; ++i) {
                word_at(ent, 0x20 + i * 4) = word_at(ent, 0x140 + i * 4);
                word_at(ent, 0x14 + i * 4) = 0;
                word_at(ent, 0x2c + i * 4) = 0;
            }
            ecx = word_at(ent, 0x148);
        }
    }
    const uint32_t start = L + 0x10, end = L + 0x20, results = L + 0x30;
    const uint32_t pass = word_at(ent, 0x224);
    stack_float(start) = field<float>(ent, 0x140);
    stack_float(start + 4) = field<float>(ent, 0x144);
    stack_float(start + 8) = field<float>(ent, 0x148);
    stack_float(end) = field<float>(ent, 0x140);
    stack_float(end + 4) = field<float>(ent, 0x144);
    stack_float(end + 8) = field<float>(ent, 0x148) + title_global<float>(0x0083ebe8);  // 128
    const uint32_t up_args[11] = {results, 0x0097a8a0, 0x00984d00, end, pass, 0x0381c011, 1, 0, 0, 0, 0};
    call_at(L, 0x002657c0, own_address(&SV_Trace_entry), TitleRegs{results, start, ecx, end, self, edi}, 11, up_args);  // SV_Trace
    const float z = stack_float(results + 8);
    const float x = stack_float(results);
    const float y = stack_float(results + 4);
    stack_float(start + 8) = z;
    stack_float(start) = x;
    stack_float(start + 4) = y;
    stack_float(end) = x;
    stack_float(end + 4) = y;
    stack_float(end + 8) = z - title_global<float>(0x0083ecf4);  // 256
    call_at(L - 0x2c, 0x00265833, own_address(&SV_Trace_entry), TitleRegs{pass, start, end, results, self, edi}, 11, up_args);  // SV_Trace
    word_at(ent, 0xc) = stack_at(results + 0x30);
    stack_float(start) = stack_float(results);
    stack_float(start + 4) = stack_float(results + 4);
    stack_float(start + 8) = stack_float(results + 8);
    const uint32_t at_args[11] = {results, 0x0097a8a0, 0x00984d00, start, pass, 0x0381c011, 1, 0, 0, 0, 0};
    call_at(L, 0x00265894, own_address(&SV_Trace_entry), TitleRegs{pass, start, start, results, self, edi}, 11, at_args);  // SV_Trace
    for (uint32_t i = 0; i < 3; ++i) {
        const uint32_t v = stack_at(results + i * 4);
        word_at(ent, 0x20 + i * 4) = v;
        word_at(ent, 0x14 + i * 4) = 0;
        word_at(ent, 0x2c + i * 4) = 0;
        word_at(ent, 0x140 + i * 4) = v;
    }
    return 0;
}

PLATFORM_ASM_ENTRY(CanDamage_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x30000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(CanDamage) "addl $20, %esp\n"
                   "popl %esp\n"
                   "ret")

uint32_t XB_CDECL CanDamage(uint32_t entry, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // Q3's CanDamage (EDI origin, cdecl target): 1 when a point trace (contents 0x280e091) from
    // one of five points reaches the origin whole (fraction 1). Sentients: the middle of the
    // origin and eye, and that point moved sideways (radius 15, players 8) across the flat
    // direction to the origin and up/down by half the eye height; others: the bounds center and
    // its four corners at radius 15. Frame: aligned base - 0xf0 (radius/counter +0xc, points
    // +0x30, boxes +0x70 / +0x80, start +0x90, end +0xa0, trace +0xb0).
    stack_at(entry - 4) = ebp;
    const uint32_t L = ((entry - 4) & ~15u) - 0xf0;
    stack_at(L + 4) = ebx;
    stack_at(L) = esi;
    uint8_t* const targ = reinterpret_cast<uint8_t*>(uintptr_t{stack_at(entry + 4)});
    const float* const origin = reinterpret_cast<const float*>(uintptr_t{edi});
    auto f = [](uint32_t address) -> float& { return stack_float(address); };
    const float radius = title_global<float>(0x0083ef10);  // 15
    f(L + 0xc) = radius;
    uint8_t* const sentient = pointer_at(targ, 0x244);
    if (sentient) {
        if (word_at(targ, 0x23c)) stack_at(L + 0xc) = title_global<uint32_t>(0x0083ed78);  // 8
        call_at(L, 0x002650fe, own_address(&Sentient_EyeOrigin), TitleRegs{address_of(sentient), address_of(targ), L + 0x20, 0, esi, edi}, 0,
                nullptr);  // Sentient_EyeOrigin
        const float half = title_global<float>(0x0083eb24);
        f(L + 0x6c) = (f(L + 0x28) - field<float>(targ, 0x148)) * half;
        const float dy = origin[1] - field<float>(targ, 0x144);
        const float dx = origin[0] - field<float>(targ, 0x140);
        const float zero = 0.0f;
        f(L + 0x14) = dy;
        f(L + 0x10) = dx;
        f(L + 0x18) = zero;
        f(L + 8) = dy * dy + dx * dx + zero * zero;
        f(L + 8) = title_ci_float(0x0033acf4, f(L + 8));  // __CIsqrt
        uint32_t length_bits;
        std::memcpy(&length_bits, &f(L + 8), 4);
        float side_y;
        if (length_bits & 0x7fffffffu) {  // FUCOMIP: NaN normalizes too
            const float inv = title_global<float>(0x0083eb20) / f(L + 8);
            f(L + 0x10) = inv * f(L + 0x10);
            side_y = inv * f(L + 0x14);
            f(L + 0x18) = inv * zero;
        } else {
            side_y = f(L + 0x14);
        }
        const float cx = (field<float>(targ, 0x140) + f(L + 0x20)) * half;
        const float cz = (f(L + 0x28) + field<float>(targ, 0x148)) * half;
        const float neg_y = zero - side_y;
        const float cy = (field<float>(targ, 0x144) + f(L + 0x24)) * half;
        const float r = f(L + 0xc);
        const float dir_x = f(L + 0x10), dir_z = f(L + 0x18), h = f(L + 0x6c);
        const float p1y = dir_x * r + cy;
        const float up = dir_z * r + cz;
        const float neg_r = zero - r;
        const float p1x = neg_y * r + cx;
        const float down = neg_r * dir_z + cz;
        const float p3y = neg_r * dir_x + cy;
        const float p3x = neg_r * neg_y + cx;
        f(L + 0x30) = cx;
        f(L + 0x34) = cy;
        f(L + 0x38) = cz;
        f(L + 0x3c) = p1x;
        f(L + 0x40) = p1y;
        f(L + 0x44) = up + h;
        f(L + 0x48) = p1x;
        f(L + 0x4c) = p1y;
        f(L + 0x50) = up - h;
        f(L + 0x54) = p3x;
        f(L + 0x58) = p3y;
        f(L + 0x5c) = down + h;
        f(L + 0x60) = p3x;
        f(L + 0x64) = p3y;
        f(L + 0x68) = down - h;
    } else {
        const float half = title_global<float>(0x0083eb24);
        const float z = (field<float>(targ, 0x118) + field<float>(targ, 0x108)) * half;
        const float y = (field<float>(targ, 0x114) + field<float>(targ, 0x104)) * half;
        const float x = (field<float>(targ, 0x110) + field<float>(targ, 0x100)) * half;
        f(L + 0x30) = x;
        f(L + 0x34) = y;
        f(L + 0x38) = z;
        f(L + 0x3c) = x + radius;
        f(L + 0x40) = y + radius;
        f(L + 0x44) = z;
        f(L + 0x48) = x + radius;
        f(L + 0x4c) = y - radius;
        f(L + 0x50) = z;
        f(L + 0x54) = x - radius;
        f(L + 0x58) = y + radius;
        f(L + 0x5c) = z;
        f(L + 0x60) = x - radius;
        f(L + 0x64) = y - radius;
        f(L + 0x68) = z;
    }
    for (int32_t i = 0; i < 5; ++i) {
        stack_at(L + 0xc) = static_cast<uint32_t>(i);
        const uint32_t point = L + 0x30 + static_cast<uint32_t>(i) * 0xc;
        stack_at(L + 0x70) = title_global<uint32_t>(0x0082e290);
        stack_at(L + 0x74) = title_global<uint32_t>(0x0082e294);
        stack_at(L + 0x78) = title_global<uint32_t>(0x0082e298);
        stack_at(L + 0x7c) = title_global<uint32_t>(0x0082e29c);
        stack_at(L + 0x80) = title_global<uint32_t>(0x0082e290);
        stack_at(L + 0x84) = title_global<uint32_t>(0x0082e294);
        stack_at(L + 0x88) = title_global<uint32_t>(0x0082e298);
        stack_at(L + 0x8c) = title_global<uint32_t>(0x0082e29c);
        const uint32_t ox = *reinterpret_cast<const uint32_t*>(origin);
        const uint32_t oy = *reinterpret_cast<const uint32_t*>(origin + 1);
        const uint32_t oz = *reinterpret_cast<const uint32_t*>(origin + 2);
        const uint32_t end_words[4] = {ox, oy, oz, ox};
        const uint32_t px = stack_at(point), py = stack_at(point + 4), pz = stack_at(point + 8);
        const uint32_t start_words[4] = {px, py, pz, px};
        for (uint32_t k = 0; k < 4; ++k) {
            stack_at(L + 0x20 + k * 4) = end_words[k];
            stack_at(L + 0xa0 + k * 4) = end_words[k];
            stack_at(L + 0x10 + k * 4) = start_words[k];
            stack_at(L + 0x90 + k * 4) = start_words[k];
        }
        const uint32_t args[11] = {L + 0xb0, L + 0x80, L + 0x70, L + 0xa0, word_at(targ, 0x224), 0x0280e091, 0, 1,
                                   0x008da6f0, 1, 0};
        call_at(L, 0x002654b1, own_address(&SV_Trace_entry), TitleRegs{L + 0x70, L + 0x90, L + 0x80, L + 0xb0, point + 4, edi}, 11,
                args);  // SV_Trace
        if (stack_at(L + 0xd0) == 0x3f800000u) return 1;  // fraction == 1.0 (FUCOMIP against the double)
    }
    stack_at(L + 0xc) = 5;
    return 0;
}

PLATFORM_ASM_ENTRY(fire_projectile_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x30000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %ecx\n"
                   "pushl %eax\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(fire_projectile) "addl $28, %esp\n"
                   "popl %esp\n"
                   "ret")

uint8_t* XB_CDECL fire_projectile(uint32_t entry, float* dir, const float* start, uint32_t ebx, uint32_t esi,
                                  uint32_t edi, uint32_t ebp) {
    // EAX direction (normalized in place), ECX start; cdecl (owner, fuse seconds). A missile
    // entity in the owner's context ("rocket"-class script string 0x00984be8 and its hash): type
    // 3, think 8 at level.time + fuse * 1000, the owner's weapon (+3), owner (+0x1a0, +0x2b0),
    // the weapon def's damage words (+0x5b8, +0x728, +0x72c, +0x724 -> +0x358..+0x364), contents
    // 0x280e091, a linear trajectory (+0x14 = 2, 50 ms back) from start at speed (+0x730),
    // angles from the direction, the projectile model (+0x738); weapon class 6 plays its
    // sound 0x2b. Every callee runs at the original's ESP (frame top = entry - 0x20).
    stack_at(entry - 0x14) = ebx;
    stack_at(entry - 0x18) = ebp;
    stack_at(entry - 0x1c) = esi;
    stack_at(entry - 0x20) = edi;
    const uint32_t top = entry - 0x20;
    const uint32_t dir_at = address_of(dir), start_at = address_of(start);
    stack_float(entry - 0x10) = dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2];
    stack_float(entry - 0x10) = title_ci_float(0x0033acf4, stack_float(entry - 0x10));  // __CIsqrt
    if (stack_at(entry - 0x10) & 0x7fffffffu) {  // FUCOMIP: NaN normalizes too
        const float inv = title_global<float>(0x0083eb20) / stack_float(entry - 0x10);
        dir[0] = dir[0] * inv;
        dir[1] = inv * dir[1];
        dir[2] = inv * dir[2];
    }
    uint8_t* const owner = reinterpret_cast<uint8_t*>(uintptr_t{stack_at(entry + 4)});
    const float fuse = stack_float(entry + 8);
    const uint32_t weapon = owner[3];
    uint32_t context = word_at(owner, 0x220);
    if (context == 0xffffffffu) context = word_at(title_global<uint8_t*>(0x0095f638), 0x34);
    const uint32_t defs = title_global<uint32_t>(0x0095f4d8);  // bg_weaponDefs
    uint8_t* const def = reinterpret_cast<uint8_t*>(uintptr_t{stack_at(defs + weapon * 4)});
    uint8_t* const ent = reinterpret_cast<uint8_t*>(uintptr_t{
        call_at(top, 0x0026771f, own_address(&G_Spawn), TitleRegs{context, ebx, weapon, defs, dir_at, start_at}, 0, nullptr)});  // G_Spawn
    const uint32_t self = address_of(ent);
    const uint32_t name_arg[1] = {0x00984be8};
    call_at(top, 0x00267731, own_address(&ScriptString_Assign_entry), TitleRegs{self, self, self + 0x25c, defs, dir_at, start_at}, 1,
            name_arg);  // ScriptString_Assign
    const uint32_t rep = word_at(ent, 0x25c);
    const uint32_t text = rep ? rep + 6 : 0;
    word_at(ent, 0x260) =
        call_at(top, 0x00267747, own_address(&Com_HashKeyNoCase), TitleRegs{rep, self, 0, text, dir_at, start_at}, 0, nullptr);  // Com_HashKeyNoCase
    const uint32_t flags = word_at(ent, 0x10);
    const float delay = fuse * title_global<float>(0x0083eb9c);  // 1000
    const float now = static_cast<float>(title_global<int32_t>(0x0098540c));
    word_at(ent, 0x32c) = 8;
    ent[0] = 3;
    word_at(ent, 0xd4) = 0xa0;
    word_at(ent, 0x328) = static_cast<uint32_t>(static_cast<int32_t>(delay + now));
    word_at(ent, 0x10) = flags | 0x8000;
    ent[3] = owner[3];
    word_at(ent, 0x2f0) = title_global<uint32_t>(0x0098540c);
    word_at(ent, 0x1a0) = word_at(owner, 0x224);
    word_at(ent, 0x2b0) = word_at(owner, 0x224);
    word_at(ent, 0x358) = word_at(def, 0x5b8);
    word_at(ent, 0x35c) = word_at(def, 0x728);
    word_at(ent, 0x360) = word_at(def, 0x72c);
    word_at(ent, 0x364) = word_at(def, 0x724);
    word_at(ent, 0x368) = 5;
    word_at(ent, 0x36c) = 6;
    word_at(ent, 0x2a8) = 0x0280e091;
    word_at(ent, 0x14) = 2;
    word_at(ent, 0x18) = title_global<uint32_t>(0x0098540c) - 50;
    for (uint32_t i = 0; i < 3; ++i) word_at(ent, 0x20 + i * 4) = reinterpret_cast<const uint32_t*>(start)[i];
    for (uint32_t i = 0; i < 3; ++i)
        field<float>(ent, 0x2c + i * 4) = static_cast<float>(field<int32_t>(def, 0x730)) * dir[i];
    for (uint32_t i = 0; i < 3; ++i) word_at(ent, 0x140 + i * 4) = reinterpret_cast<const uint32_t*>(start)[i];
    const uint32_t angles = entry - 0xc;
    call_at(top, 0x0026788b, own_address(&vectoangles_entry),
            TitleRegs{word_at(ent, 0x148), self, word_at(ent, 0x140), word_at(ent, 0x144), dir_at, angles}, 0,
            nullptr);  // vectoangles
    for (uint32_t i = 0; i < 3; ++i) {
        word_at(ent, 0x150 + i * 4) = stack_at(angles + i * 4);
        word_at(ent, 0x44 + i * 4) = stack_at(angles + i * 4);
        word_at(ent, 0x38 + i * 4) = 0;
        word_at(ent, 0x50 + i * 4) = 0;
    }
    uint32_t model_context = word_at(ent, 0x220);
    uint32_t ecx = word_at(ent, 0x150);
    if (model_context == 0xffffffffu) {
        ecx = title_global<uint32_t>(0x0095f638);
        model_context = word_at(title_global<uint8_t*>(0x0095f638), 0x34);
    }
    const uint32_t model_args[3] = {title_global<uint32_t>(0x0095f6ac), entry + 4, model_context};  // rm_models
    const uint32_t handle = call_at(top, 0x0026793c, own_address(&R_RegisterModel),
                                    TitleRegs{word_at(def, 0x738), self, ecx, entry + 4, dir_at, angles}, 3,
                                    model_args);  // R_RegisterModel (stdcall)
    word_at(ent, 0x258) = stack_at(handle);
    uint32_t eax = handle;
    if (word_at(def, 0x98) == 6) {
        const uint32_t sound_args[2] = {word_at(def, 8), 0x2b};
        eax = call_at(top, 0x0026795a, own_address(&FXCmd_PlayWeaponSound_entry), TitleRegs{self, self, word_at(ent, 0x258), word_at(def, 8), dir_at, angles},
                      2, sound_args);  // FXCmd_PlayWeaponSound
        word_at(ent, 0x3b4) = eax;
    }
    const bool sound = word_at(def, 0x98) == 6;
    const uint32_t update_args[1] = {self};
    call_at(top, 0x00267969, own_address(&G_DObjUpdate),
            TitleRegs{eax, self, sound ? 0 : word_at(ent, 0x258), sound ? word_at(def, 8) : entry + 4, dir_at, angles}, 1,
            update_args);  // G_DObjUpdate
    return ent;
}

PLATFORM_ASM_ENTRY(FinishSpawningItem_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x30000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(FinishSpawningItem) "addl $20, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL FinishSpawningItem(uint32_t entry, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // Q3's FinishSpawningItem. Suspended items (+0x2a0 bit 1) stay where they are (+0x20 =
    // origin, velocity 0). Else (+0x10 |= 0x10, +0xd4 |= 0x200) a box trace (item type +0x24 1:
    // a flat box at -1, else -1..2 high; contents 0x411) 4096 down, again from 15 lower when it
    // starts solid; still solid: freed (G_FreeEntity with the caller's second word). Else the
    // item lands at the end (ground entity +0xc) and, on a slope (fraction < 1), turns to the
    // ground normal (axis {forward x normal, ...}, item class 1: roll + 90). Frame: aligned base
    // - 0xc0 (angles/end +0x10, mins +0x20, maxs +0x30, axis +0x4c, start +0x70, trace +0x80).
    stack_at(entry - 4) = ebp;
    const uint32_t L = ((entry - 4) & ~15u) - 0xc0;
    stack_at(L + 8) = ebx;
    stack_at(L + 4) = esi;
    stack_at(L) = edi;
    uint8_t* const ent = reinterpret_cast<uint8_t*>(uintptr_t{stack_at(entry + 4)});
    const uint32_t self = address_of(ent);
    auto f = [](uint32_t address) -> float& { return stack_float(address); };
    stack_at(L + 0xb0) = 0;
    if (ent[0x2a0] & 1) {
        for (uint32_t i = 0; i < 3; ++i) {
            word_at(ent, 0x20 + i * 4) = word_at(ent, 0x140 + i * 4);
            word_at(ent, 0x14 + i * 4) = 0;
            word_at(ent, 0x2c + i * 4) = 0;
        }
        call_at(L, 0x00264be9, own_address(&SV_LinkEntity_entry), TitleRegs{word_at(ent, 0x148), ebx, word_at(ent, 0x140), word_at(ent, 0x144), esi, self},
                0, nullptr);  // SV_LinkEntity
        return;
    }
    const uint32_t minus_one = title_global<uint32_t>(0x0083eb34), one = title_global<uint32_t>(0x0083eb20);
    stack_at(L + 0x24) = minus_one;
    stack_at(L + 0x20) = minus_one;
    uint32_t top_z = minus_one;
    if (word_at(pointer_at(ent, 0x3b0), 0x24) == 1) {
        stack_at(L + 0x28) = minus_one;
    } else {
        stack_at(L + 0x28) = 0;
        top_z = title_global<uint32_t>(0x0083eba8);  // 2
    }
    stack_at(L + 0x30) = one;
    stack_at(L + 0x34) = one;
    word_at(ent, 0x10) |= 0x10;
    word_at(ent, 0xd4) |= 0x200;
    stack_at(L + 0x38) = top_z;
    const uint32_t origin = self + 0x140;
    const float depth = title_global<float>(0x0083ed4c);  // 4096
    f(L + 0x10) = field<float>(ent, 0x140);
    f(L + 0x14) = field<float>(ent, 0x144);
    f(L + 0x18) = field<float>(ent, 0x148) - depth;
    const uint32_t args[11] = {L + 0x80, L + 0x20, L + 0x30, L + 0x10, word_at(ent, 0x224), 0x411, 1, 0, 0, 0, 0};
    call_at(L, 0x00264cd8, own_address(&SV_Trace_entry), TitleRegs{L + 0x20, origin, L + 0x80, L + 0x30, origin, self}, 11, args);  // SV_Trace
    if (stack_at(L + 0xbc) & 0xff00u) {
        f(L + 0x70) = field<float>(ent, 0x140);
        f(L + 0x74) = field<float>(ent, 0x144);
        f(L + 0x78) = field<float>(ent, 0x148) - title_global<float>(0x0083ef10);  // 15
        f(L + 0x10) = field<float>(ent, 0x140);
        f(L + 0x14) = field<float>(ent, 0x144);
        f(L + 0x18) = field<float>(ent, 0x148) - depth;
        call_at(L, 0x00264d86, own_address(&SV_Trace_entry), TitleRegs{L + 0x30, L + 0x70, L + 0x20, L + 0x80, origin, self}, 11,
                args);  // SV_Trace
        if (stack_at(L + 0xbc) & 0xff00u) {
            call_at(L, 0x00264d9b, own_address(&vtos), TitleRegs{origin, L + 0x70, 0, 0, origin, self}, 0, nullptr);  // vtos
            const uint32_t free_args[2] = {self, stack_at(entry + 8)};
            call_at(L, 0x00264da5, own_address(&G_FreeEntity), TitleRegs{free_args[1], L + 0x70, 0, 0, origin, self}, 2,
                    free_args);  // G_FreeEntity
            return;
        }
    }
    const uint32_t ground = stack_at(L + 0xb0);
    for (uint32_t i = 0; i < 3; ++i) word_at(ent, 0x2c + i * 4) = 0;
    word_at(ent, 0xc) = ground;
    for (uint32_t i = 0; i < 3; ++i) {
        word_at(ent, 0x20 + i * 4) = stack_at(L + 0x80 + i * 4);
        word_at(ent, 0x14 + i * 4) = 0;
    }
    for (uint32_t i = 0; i < 3; ++i) word_at(ent, 0x140 + i * 4) = stack_at(L + 0x80 + i * 4);
    if (title_global<float>(0x0083eb20) > f(L + 0xa0)) {
        for (uint32_t i = 0; i < 3; ++i) stack_at(L + 0x64 + i * 4) = stack_at(L + 0x90 + i * 4);
        call_at(L, 0x00264e66, own_address(&AnglesToForward_entry), TitleRegs{ground, ebx, 0, ground, L + 0x4c, self + 0x150}, 0,
                nullptr);  // AnglesToForward
        const float fz = f(L + 0x54), ny = f(L + 0x68), fy = f(L + 0x50), nz = f(L + 0x6c), fx = f(L + 0x4c),
                    nx = f(L + 0x64);
        const float a = fz * ny - fy * nz;
        const float c = fy * nx - fx * ny;
        const float b = fx * nz - fz * nx;
        f(L + 0x4c) = b * nz - c * ny;
        f(L + 0x58) = a;
        f(L + 0x60) = c;
        f(L + 0x5c) = b;
        const float row_y = c * nx - a * nz;
        const float row_z = a * ny - b * nx;
        f(L + 0x50) = row_y;
        f(L + 0x54) = row_z;
        call_at(L, 0x00264f13, own_address(&AxisToAngles), TitleRegs{L + 0x10, ebx, L + 0x4c, ground, L + 0x4c, self + 0x150}, 0,
                nullptr);  // AxisToAngles
        const uint32_t item = *reinterpret_cast<const uint16_t*>(ent + 6);
        if (title_global<uint32_t>(0x0096bd34 + item * 0x34) == 1)
            f(L + 0x18) = f(L + 0x18) + title_global<float>(0x0083eb64);  // 90
        call_at(L, 0x00264f43, own_address(&G_SetAngle2), TitleRegs{self, ebx, L + 0x10, ground, L + 0x4c, self + 0x150}, 0,
                nullptr);  // G_SetAngle2
        call_at(L, 0x00264f4b, own_address(&SV_LinkEntity_entry), TitleRegs{self, ebx, 0, ground, L + 0x4c, self}, 0, nullptr);  // SV_LinkEntity
        return;
    }
    call_at(L, 0x00264f4b, own_address(&SV_LinkEntity_entry), TitleRegs{self, ebx, 0, ground, origin, self}, 0, nullptr);  // SV_LinkEntity
}

PLATFORM_ASM_ENTRY(turret_think_init_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x30000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(turret_think_init) "addl $20, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL turret_think_init(uint32_t entry, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // Think 0xf next frame. With both "tag_aim" and "tag_butt" (hashes cached under 0x0098f140
    // bits 1 / 2): from the aim tag's point in the world (entity axis {forward, -right, up},
    // origin), pitches 0, -3, .. -90 of the butt-to-aim offset turned by the pitch and placed
    // back in the world: a point trace (contents 0x11) to each; the first that hits sets the
    // turret info's +0x3c to that pitch. Frame: aligned base - 0x100 (counter +0x14, angles
    // +0x18, axis + origin +0x24, offset +0x54, right +0x64, end +0x70, start +0x80, forward
    // +0x9c, up +0xb4, trace +0xc0).
    stack_at(entry - 4) = ebp;
    const uint32_t L = ((entry - 4) & ~15u) - 0x100;
    stack_at(L + 8) = ebx;
    stack_at(L + 4) = esi;
    stack_at(L) = edi;
    uint8_t* const ent = reinterpret_cast<uint8_t*>(uintptr_t{stack_at(entry + 4)});
    const uint32_t self = address_of(ent);
    auto f = [](uint32_t address) -> float& { return stack_float(address); };
    stack_at(L + 0x60) = word_at(ent, 0x24c);
    word_at(ent, 0x32c) = 0xf;
    word_at(ent, 0x328) = title_global<uint32_t>(0x0098540c) + 1;
    auto tag = [&](uint32_t bit, uint32_t slot, uint32_t text, uint32_t ret_hash, uint32_t ret_tag, uint32_t ebx_now) {
        if (!(title_global<uint8_t>(0x0098f140) & bit)) {
            const uint32_t guard = title_global<uint32_t>(0x0098f140) | bit;
            title_global<uint32_t>(0x0098f140) = guard;
            title_global<uint32_t>(slot) =
                call_at(L, ret_hash, own_address(&Com_HashKeyNoCase), TitleRegs{bit, ebx_now, guard, text, self, edi}, 0, nullptr);  // Com_HashKeyNoCase
        }
        return call_at(L, ret_tag, own_address(&G_DObjGetTagMatrixPtr), TitleRegs{self, ebx_now, title_global<uint32_t>(slot), 0, self, edi}, 0,
                       nullptr);  // G_DObjGetTagMatrixPtr
    };
    const uint32_t aim_at = tag(1, 0x0098f13c, 0x00839a18, 0x002631fa, 0x0026320c, ebx);
    if (!aim_at) return;
    const uint32_t butt_at = tag(2, 0x0098f138, 0x0083b294, 0x0026323d, 0x0026324f, aim_at);
    stack_at(L + 0x14) = butt_at;
    if (!butt_at) return;
    const float* const aim = reinterpret_cast<const float*>(uintptr_t{aim_at});
    const float* const butt = reinterpret_cast<const float*>(uintptr_t{butt_at});
    const uint32_t axis_args[2] = {self + 0x150, L + 0x24};
    call_at(L, 0x00263274, own_address(&AnglesToAxis_entry), TitleRegs{butt_at, aim_at, title_global<uint32_t>(0x0098f138), L + 0x24, L + 0x3c, L + 0x18},
            2, axis_args);  // AnglesToAxis
    const float ox = field<float>(ent, 0x140), oy = field<float>(ent, 0x144), oz = field<float>(ent, 0x148);
    const float zero = 0.0f;
    f(L + 0x50) = oz;
    f(L + 0x54) = butt[12] - aim[12];
    f(L + 0x58) = butt[13] - aim[13];
    f(L + 0x5c) = butt[14] - aim[14];
    const float nrx = zero - f(L + 0x18);
    const float px = aim[12] * f(L + 0x24) + aim[14] * f(L + 0x3c) + aim[13] * nrx + ox;
    f(L + 0x30) = nrx;
    f(L + 0x80) = px;
    const float nry = zero - f(L + 0x1c);
    const float py = aim[14] * f(L + 0x40) + aim[13] * nry + aim[12] * f(L + 0x28) + oy;
    f(L + 0x34) = nry;
    f(L + 0x84) = py;
    const float nrz = zero - f(L + 0x20);
    const float pz = aim[14] * f(L + 0x44) + aim[13] * nrz + aim[12] * f(L + 0x2c) + f(L + 0x50);
    f(L + 0x38) = nrz;
    f(L + 0x48) = ox;
    f(L + 0x4c) = oy;
    f(L + 0x88) = pz;
    stack_at(L + 0x14) = 0;
    f(L + 0x1c) = zero;
    f(L + 0x20) = zero;
    for (int32_t i = 0; i <= 30; ++i) {
        stack_at(L + 0x14) = static_cast<uint32_t>(i);
        f(L + 0x18) = static_cast<float>(i) * title_global<float>(0x0083f1a0);  // -3
        const uint32_t pitch_args[2] = {L + 0x18, L + 0x9c};
        call_at(L, 0x002633f1, own_address(&AnglesToAxis_entry), TitleRegs{L + 0x9c, aim_at, L + 0x18, L + 0x24, L + 0xb4, L + 0x64}, 2,
                pitch_args);  // AnglesToAxis
        const float dx = f(L + 0x54), dy = f(L + 0x58), dz = f(L + 0x5c);
        const float qx = f(L + 0xb4) * dz + (zero - f(L + 0x64)) * dy + f(L + 0x9c) * dx;
        const float qy = f(L + 0xb8) * dz + f(L + 0xa0) * dx + (zero - f(L + 0x68)) * dy;
        const float qz = f(L + 0xbc) * dz + f(L + 0xa4) * dx + (zero - f(L + 0x6c)) * dy;
        const float wx = aim[12] + qx;
        const float wz = qz + aim[14];
        const float wy = aim[13] + qy;
        f(L + 0x70) = f(L + 0x3c) * wz + f(L + 0x30) * wy + f(L + 0x24) * wx + f(L + 0x48);
        f(L + 0x74) = f(L + 0x40) * wz + f(L + 0x28) * wx + f(L + 0x34) * wy + f(L + 0x4c);
        f(L + 0x78) = f(L + 0x44) * wz + f(L + 0x2c) * wx + f(L + 0x38) * wy + f(L + 0x50);
        const uint32_t trace_args[7] = {L + 0xc0, L + 0x80, L + 0x70, word_at(ent, 0x224), 0x11, 0x008da6f0, 0};
        call_at(L - 8, 0x00263570, own_address(&G_LocationalTrace_entry), TitleRegs{L + 0x70, aim_at, L + 0x80, L + 0xc0, L + 0xb4, L + 0x64}, 7,
                trace_args);  // G_LocationalTrace
        if (title_global<float>(0x0083eb20) > f(L + 0xe0)) {
            field<float>(reinterpret_cast<uint8_t*>(uintptr_t{stack_at(L + 0x60)}), 0x3c) = f(L + 0x18);
            return;
        }
    }
    stack_at(L + 0x14) = 31;
}

// --- Artillery, item pickups, turret targeting, player death/melee, spawn dispatch ---

uint32_t XB_CDECL Artillery_Fire(uint8_t* ent) {
    // The weapon's fire delay (def +0x754) to think 0xb, and the "MP_Artillery_Fire" sound on the
    // entity (FXCmd_PlaySoundAlias); returns the sound call's EAX.
    const uint32_t now = title_global<uint32_t>(0x0098540c);
    const uint32_t next = *reinterpret_cast<const uint32_t*>(weapon_def(ent[3]) + 0x754) + now;
    word_at(ent, 0x328) = next;
    word_at(ent, 0x32c) = 0xb;
    const TitleRegs regs{now, 0, address_of(ent), next, 0, 0};
    const uint32_t args[4] = {0x0083b150, 0, 0xffffffffu, 0};
    return g_ent_call(own_address(&FXCmd_PlaySoundAlias_entry), &regs, 4, args);  // FXCmd_PlaySoundAlias (ECX entity)
}

void XB_CDECL Think_DropNamedItem(uint8_t* ent) {
    // The item named by the script string +0x3b8 (BG_FindItem, ESI name) dropped in place
    // (Drop_Item, no throw); when that fails the origin goes through vtos (result unused).
    const uint32_t rep = word_at(ent, 0x3b8);
    const TitleRegs find{0, 0, 0, 0, rep ? rep + 6 : 0, address_of(ent)};
    const uint32_t item = g_ent_call(own_address(&BG_FindItem_entry), &find, 0, nullptr);  // BG_FindItem
    const TitleRegs drop{0, 0, 0, 0, address_of(ent), address_of(ent)};
    const uint32_t args[2] = {item, 0};
    if (!g_ent_call(own_address(&Drop_Item_entry), &drop, 2, args)) vtos(&field<float>(ent, 0x140));  // Drop_Item
}

PLATFORM_ASM_ENTRY(Actor_TurretCheckNearEnemy_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x30000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Actor_TurretCheckNearEnemy) "addl $24, %esp\n"
                   "popl %esp\n"
                   "ret")

uint32_t XB_CDECL Actor_TurretCheckNearEnemy(uint32_t entry, uint8_t* actor, uint32_t ebx, uint32_t esi, uint32_t edi,
                                             uint32_t ebp) {
    // ECX actor, cdecl turret: 1 without an enemy (sentient +0x3c) or with one 256 units away or
    // more (packed squares over all four lanes, x + y + z kept at the aligned frame), else
    // Actor_TurretCanTargetEnemy at the original's ESP.
    stack_at(entry - 4) = ebp;
    const uint32_t L = ((entry - 4) & ~15u) - 0x10;
    uint8_t* const enemy = pointer_at(pointer_at(actor, 4), 0x3c);
    if (!enemy) return 1;
    uint8_t* const self_ent = pointer_at(actor, 0);
    uint8_t* const enemy_ent = pointer_at(enemy, 0);
    float sq[4];
    for (uint32_t i = 0; i < 4; ++i) {
        const float d = field<float>(enemy_ent, 0x140 + i * 4) - field<float>(self_ent, 0x140 + i * 4);
        sq[i] = d * d;
    }
    const float sum = sq[0] + sq[1] + sq[2];
    stack_float(L) = sum;
    stack_float(L + 4) = sq[1];
    stack_float(L + 8) = sq[2];
    stack_float(L + 0xc) = sq[3];
    if (sum >= title_global<float>(0x0083eb40)) return 1;  // 65536
    const uint32_t turret = stack_at(entry + 4);
    const uint32_t args[2] = {turret, address_of(actor)};
    return call_at(L, 0x00267ab6, own_address(&Actor_TurretCanTargetEnemy_entry), TitleRegs{address_of(enemy_ent), ebx, turret, address_of(self_ent), esi, edi},
                   2, args);  // Actor_TurretCanTargetEnemy
}

PLATFORM_ASM_ENTRY(Turret_PickSentientTarget_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x30000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Turret_PickSentientTarget) "addl $20, %esp\n"
                   "popl %esp\n"
                   "ret")

uint32_t XB_CDECL Turret_PickSentientTarget(uint32_t entry, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // An auto-targeting turret's sentient: the current target (info +0xc, flag 0x40 in +2) is kept
    // for 4 s after +0x1c; else it stays a candidate while targetable. Then the next sentient of
    // the enemy teams (team +0x64 -> mask 1 << {0, 2, 1, 0, 0}) after the last one checked (+0x52,
    // 0xffff = from the first) replaces it when targetable (Turret_CanTargetSentient) and nearer.
    // Frame: aligned base - 0x40 (candidate +0x18, distance +0x1c, team table +0x20).
    stack_at(entry - 4) = ebp;
    const uint32_t L = ((entry - 4) & ~15u) - 0x40;
    stack_at(L + 8) = ebx;
    stack_at(L + 4) = esi;
    stack_at(L) = edi;
    uint8_t* const turret = reinterpret_cast<uint8_t*>(uintptr_t{stack_at(entry + 4)});
    const uint32_t self = address_of(turret);
    uint8_t* const info = pointer_at(turret, 0x24c);
    const uint32_t info_at = address_of(info);
    stack_at(L + 0x18) = 0;
    if (info[2] & 0x40) {
        uint8_t* const target = pointer_at(info, 0xc);
        if (target) {
            uint8_t* const current = pointer_at(target, 0x244);
            if (current) {
                const int32_t now = title_global<int32_t>(0x0098540c);
                if (now <= field<int32_t>(info, 0x1c) + 4000) return address_of(current);
                stack_at(L + 0x18) = address_of(current);
                if (word_at(current, 0x30)) {
                    stack_at(L + 0x18) = 0;
                } else {
                    const uint32_t args[3] = {self, word_at(current, 0), L + 0x1c};
                    if (!call_at(L, 0x00267b21, own_address(&Turret_CanTargetSentient_entry),
                                 TitleRegs{word_at(current, 0), info_at, self, L + 0x1c, static_cast<uint32_t>(now), 0}, 3,
                                 args))  // Turret_CanTargetSentient
                        stack_at(L + 0x18) = 0;
                }
            }
        }
    }
    const uint32_t team = word_at(info, 0x64);
    const int16_t last = field<int16_t>(info, 0x52);
    stack_at(L + 0x28) = 1;
    stack_at(L + 0x20) = 0;
    stack_at(L + 0x24) = 2;
    stack_at(L + 0x2c) = 0;
    stack_at(L + 0x30) = 0;
    const uint32_t shift = stack_at(L + 0x20 + team * 4);
    const uint32_t mask = 1u << (shift & 31);
    const uint32_t sentients = title_global<uint32_t>(0x00985378);  // g_sentients
    uint32_t next;
    if (last > -1) {
        const uint32_t previous = sentients + static_cast<uint32_t>(static_cast<int32_t>(last)) * 0x60;
        next = call_at(L, 0x00267b74, own_address(&Sentient_NextOfTeams_entry), TitleRegs{previous, info_at, sentients, 0, mask, mask}, 0,
                       nullptr);  // Sentient_NextOfTeams
    } else {
        next = call_at(L, 0x00267b7d, own_address(&Sentient_FirstOfTeams_entry), TitleRegs{static_cast<uint32_t>(static_cast<int32_t>(last)), info_at, shift, 0,
                                                            mask, 0},
                       0, nullptr);  // Sentient_FirstOfTeams
    }
    if (!next) {
        field<uint16_t>(info, 0x52) = 0xffff;
        return stack_at(L + 0x18);
    }
    uint8_t* const candidate = reinterpret_cast<uint8_t*>(uintptr_t{next});
    field<uint16_t>(info, 0x52) =
        static_cast<uint16_t>(static_cast<int32_t>(next - title_global<uint32_t>(0x00985378)) / 0x60);
    if (word_at(candidate, 0x30)) return stack_at(L + 0x18);
    const uint32_t args[3] = {self, word_at(candidate, 0), L + 0x1c};
    const uint32_t ok = call_at(L, 0x00267bbc, own_address(&Turret_CanTargetSentient_entry), TitleRegs{next, self, L + 0x1c, word_at(candidate, 0), next, 0},
                                3, args);  // Turret_CanTargetSentient
    const uint32_t best = stack_at(L + 0x18);
    if (!ok) return best;
    if (!best) return next;
    uint8_t* const best_ent = pointer_at(reinterpret_cast<uint8_t*>(uintptr_t{best}), 0);
    float sq[4];
    for (uint32_t i = 0; i < 4; ++i) {
        const float d = field<float>(turret, 0x140 + i * 4) - field<float>(best_ent, 0x140 + i * 4);
        sq[i] = d * d;
    }
    const float sum = sq[0] + sq[1] + sq[2];
    stack_float(L + 0x20) = sum;
    stack_float(L + 0x24) = sq[1];
    stack_float(L + 0x28) = sq[2];
    stack_float(L + 0x2c) = sq[3];
    if (sum > stack_float(L + 0x1c)) return next;
    return best;
}

PLATFORM_ASM_ENTRY(Turret_AimAtTarget_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ecx\n"
                   "pushl %edx\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(Turret_AimAtTarget) "addl $16, %esp\n"
                   "ret")

uint32_t XB_CDECL Turret_AimAtTarget(uint8_t* ent, uint32_t aiming, int32_t converge, uint8_t* sentient) {
    // EDI turret, EDX aiming, ECX convergence ms, cdecl sentient: 0 when it cannot aim
    // (Turret_ComputeAimPoint); else it turns toward the point (Turret_UpdateTargetAngles). With
    // a pitch offset (+0x98) running: the pitches from "tag_flash" (hash cached under 0x0098f1b8)
    // to the sentient's eye and to its origin 2 up bound the current pitch (+0x90); the offset
    // moves toward the nearer bound (only toward it), 0 inside. 0 without the tag.
    float point[4] = {};
    if (!Turret_ComputeAimPoint(ent, sentient, aiming, converge, point)) return 0;
    uint32_t args[2] = {address_of(ent), address_of(point)};
    Turret_UpdateTargetAngles(args, 1, ent, point);
    if (field<float>(ent, 0x98) == title_global<float>(0x0083eafc)) return 1;
    Sentient_EyeOrigin(sentient, 0, reinterpret_cast<uint8_t*>(point));
    const uint32_t hash = cached_key(0x0098f1b8, 1, 0x0098f1b4, 0x008389c0);
    const uint32_t obj = word_at(ent, 0x22c);
    if (!obj) return 0;
    const int32_t tag = DObj_GetTagIndex(reinterpret_cast<const DObj*>(uintptr_t{obj}), hash);
    if (tag < 0) return 0;
    alignas(16) float matrix[16];
    own_function<void XB_CDECL(uint8_t*, int32_t, float*)>(&G_DObjGetWorldTagMatrix)(ent, tag, matrix);  // G_DObjGetWorldTagMatrix
    float dir[3] = {point[0] - matrix[12], point[1] - matrix[13], point[2] - matrix[14]};
    float lo = vectopitch(dir);
    uint8_t* const target = pointer_at(sentient, 0);
    dir[0] = field<float>(target, 0x140) - matrix[12];
    dir[1] = field<float>(target, 0x144) - matrix[13];
    dir[2] = field<float>(target, 0x148) - matrix[14] + title_global<float>(0x0083eba8);  // 2
    float hi = vectopitch(dir);
    if (lo > hi) {
        const float swap = hi;
        hi = lo;
        lo = swap;
    }
    const float current = field<float>(ent, 0x90);
    if (!(current >= lo)) {
        const float d = lo - field<float>(ent, 0x90);
        if (field<float>(ent, 0x98) > d) field<float>(ent, 0x98) = d;
        return 1;
    }
    if (!(hi >= current)) {
        const float d = hi - field<float>(ent, 0x90);
        if (d > field<float>(ent, 0x98)) field<float>(ent, 0x98) = d;
        return 1;
    }
    field<float>(ent, 0x98) = 0.0f;
    return 1;
}

PLATFORM_ASM_ENTRY(Pickup_Health_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(Pickup_Health) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL Pickup_Health(uint8_t* other, uint8_t* ent) {
    // EDI player, cdecl item entity: health (+0x350) up by the item's amount (+0x20, or the
    // entity's +0x370) as a percentage of the max (client +0x108; amounts 5 and 100 may reach
    // twice the max), in whole percent steps (at least 1, at most 100); the client's +0x100
    // follows. Then "gm \"%i\"" to the player, "trigger" on the item, the HEALTH_PICKUP sound,
    // "pickup" on the player. Returns -1 (no respawn).
    int32_t quantity = field<int32_t>(pointer_at(ent, 0x3b0), 0x20);
    uint8_t* const client = pointer_at(other, 0x23c);
    const int32_t max_health = field<int32_t>(client, 0x108);
    const int32_t max = (quantity == 5 || quantity == 100) ? max_health * 2 : max_health;
    if (field<int32_t>(ent, 0x370)) quantity = field<int32_t>(ent, 0x370);
    const int32_t health = field<int32_t>(other, 0x350);
    float amount = static_cast<float>(max_health) * static_cast<float>(quantity);
    amount = amount * title_global<float>(0x0083f1ec);  // -0.01
    const int32_t raised = health - static_cast<int32_t>(amount);
    field<int32_t>(other, 0x350) = raised;
    if (raised > max) {
        field<int32_t>(other, 0x350) = max;
    } else {
        const int32_t full = field<int32_t>(client, 0x108);
        const float full_f = static_cast<float>(full);
        int32_t now = static_cast<int32_t>(static_cast<float>(static_cast<int32_t>(static_cast<uint32_t>(raised) * 100u)) / full_f);
        if (now < 1) now = 1;
        else if (now > 100) now = 100;
        int32_t was = static_cast<int32_t>(static_cast<float>(static_cast<int32_t>(static_cast<uint32_t>(health) * 100u)) / full_f);
        if (was < 1) was = 1;
        was += quantity;
        if (was > 100) was = 100;
        if (now != was) field<int32_t>(other, 0x350) = static_cast<int32_t>(static_cast<uint32_t>(full) * static_cast<uint32_t>(was)) / 100;
    }
    field<int32_t>(client, 0x100) = field<int32_t>(other, 0x350);
    const uint32_t handle = word_at(other, 0x224);
    const char* const text = own_function<const char* XB_CDECL(uint32_t, int32_t)>(&va_entry)(0x0083b71c, quantity);  // va
    SV_GameSendServerCommand(text, handle);
    Scr_Notify(ent, title_global<uint32_t>(0x0097a818));  // "trigger"
    const TitleRegs regs{0, 0, address_of(ent), 0, address_of(ent), address_of(other)};
    const uint32_t args[4] = {0x0083b70c, 0, 0xffffffffu, 0};
    g_ent_call(own_address(&FXCmd_PlaySoundAlias_entry), &regs, 4, args);  // FXCmd_PlaySoundAlias "HEALTH_PICKUP"
    if (word_at(other, 0x23c)) Scr_Notify(other, title_global<uint32_t>(0x0097a758));  // "pickup"
    return 0xffffffffu;
}

void XB_CDECL player_die(uint8_t* self, uint8_t* inflictor, uint8_t* attacker, int32_t damage, uint32_t mod,
                         uint32_t weapon) {
    // Q3's player_die (die callback): unless already dead (client +0x24 >= 4), "death" with the
    // attacker's number (the original keeps it in its self slot), the attacker (+0x374),
    // LookAtKiller, the HUD's +0x24/+0x28 to -1, dead type 5 from 1 else 4, G_PlayerDeathSetup
    // (ECX self, EAX means of death, cdecl weapon).
    (void)damage;
    if (field<int32_t>(pointer_at(self, 0x23c), 0x24) >= 4) return;
    uint32_t number = word_at(attacker, 0x224);
    const uint32_t death = title_global<uint32_t>(0x0097a684);
    const TitleRegs notify{death, address_of(attacker), number, address_of(&number), address_of(self), address_of(self)};
    const uint32_t args[2] = {death, address_of(&number)};
    g_ent_call(own_address(&G_NotifyWithEntity_entry), &notify, 2, args);  // G_NotifyWithEntity (callee pops 8)
    word_at(self, 0x374) = address_of(attacker);
    LookAtKiller(attacker, inflictor, self);
    uint8_t* const hud = pointer_at(title_global<uint8_t*>(0x0095f8f0), 0x34);  // g_hudManager
    if (hud) {
        word_at(hud, 0x24) = title_global<uint32_t>(0x0083eb34);
        word_at(hud, 0x28) = title_global<uint32_t>(0x0083eb34);
    }
    uint8_t* const client = pointer_at(self, 0x23c);
    word_at(client, 0x24) = word_at(client, 0x24) == 1 ? 5 : 4;
    const TitleRegs setup{mod, address_of(attacker), address_of(self), address_of(client), address_of(self), 0};
    const uint32_t setup_args[1] = {weapon};
    g_ent_call(own_address(&G_PlayerDeathSetup), &setup, 1, setup_args);  // G_PlayerDeathSetup
}

// The callees keep stack addresses in 0x00929b40 (PathNode_FindNearest under
// Sentient_CachedNearestNode): every callee runs at the original's ESP (frame entry - 0xc).
PLATFORM_ASM_ENTRY(G_UpdatePlayerNodeClaim_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x30000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(G_UpdatePlayerNodeClaim) "addl $20, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL G_UpdatePlayerNodeClaim(uint32_t entry, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // The player's sentient (+0x244) claims its cached nearest node (radius 32) when within 32
    // units (squared 1024) on x/y; a changed claim drops the old one (Sentient_ClaimNodeTimed when
    // the client's +0x5f4 actor has no valid node, else Sentient_ClearNodeClaim; handle +0x48 = 0),
    // and the new node is taken (PathNode_SetClaim, handle +0x48) unless its claimer stands
    // within 32 units of it.
    (void)ebp;
    const uint32_t L = entry - 0xc;
    stack_at(entry - 4) = ebx;
    stack_at(entry - 8) = esi;
    stack_at(entry - 0xc) = edi;
    uint8_t* const ent = reinterpret_cast<uint8_t*>(uintptr_t{stack_at(entry + 4)});
    const uint32_t self = address_of(ent);
    const uint32_t first = word_at(ent, 0x244);
    const uint32_t near_args[6] = {0, 0, 0, 1, 0x42000000, 0};
    const uint32_t found = call_at(L, 0x00269591, own_address(&Sentient_CachedNearestNode_entry), TitleRegs{0, self, 0, 0, esi, first}, 6,
                                   near_args);  // Sentient_CachedNearestNode (EDI sentient, callee pops)
    uint8_t* node = reinterpret_cast<uint8_t*>(uintptr_t{found});
    uint8_t* const s = pointer_at(ent, 0x244);
    uint8_t* const s_ent = pointer_at(s, 0);
    const float range = title_global<float>(0x0083eff8);  // 1024
    if (node) {
        const float dy = field<float>(s_ent, 0x144) - field<float>(node, 0x48);
        const float dx = field<float>(s_ent, 0x140) - field<float>(node, 0x44);
        if (dy * dy + dx * dx > range) node = nullptr;
    }
    const uint32_t handle_at = address_of(s) + 0x48;
    const uint16_t handle = field<uint16_t>(s, 0x48);
    uint8_t* claimed = nullptr;
    uint32_t edi_now = first;
    if (handle != 0 && handle != 0xffff) {
        edi_now = title_global<uint32_t>(0x0095f4f4);
        const uint32_t block =
            title_global<uint32_t>(edi_now + 8 + static_cast<uint32_t>(static_cast<uint16_t>((handle >> 9) - 1)) * 4);
        if (block) {
            edi_now = title_global<uint32_t>(block + 0x10);
            claimed = reinterpret_cast<uint8_t*>(uintptr_t{edi_now + (handle & 0x1ffu) * 0x80});
        }
    }
    if (node == claimed) return;
    const uint32_t node_at = address_of(node);
    if (call_at(L, 0x00269626, own_address(&PathNode_HandleIsValid), TitleRegs{handle_at, self, address_of(claimed), 0, node_at, edi_now}, 0, nullptr) &
        0xff) {  // PathNode_HandleIsValid (EAX handle)
        uint8_t* const actor = pointer_at(pointer_at(ent, 0x23c), 0x5f4);
        bool timed = false;
        if (actor && word_at(actor, 0x240)) {
            const uint32_t other = word_at(actor, 0x244) + 0x48;
            timed = !(call_at(L, 0x00269652, own_address(&PathNode_HandleIsValid), TitleRegs{other, self, word_at(actor, 0x240), 0, node_at, edi_now}, 0,
                              nullptr) &
                      0xff);  // PathNode_HandleIsValid
        }
        const uint32_t mine = word_at(ent, 0x244) + 0x48;
        const uint32_t old = call_at(L, timed ? 0x00269664 : 0x0026967b, own_address(&PathNode_FromHandle2),
                                     TitleRegs{mine, self, 0, 0, node_at, edi_now}, 0, nullptr);  // PathNode_FromHandle2
        if (timed)
            call_at(L, 0x0026966b, own_address(&Sentient_ClaimNodeTimed), TitleRegs{old, self, 0, old, node_at, edi_now}, 0,
                    nullptr);  // Sentient_ClaimNodeTimed (EDX)
        else
            call_at(L, 0x00269682, own_address(&Sentient_ClearNodeClaim), TitleRegs{old, self, 0, old, node_at, edi_now}, 0,
                    nullptr);  // Sentient_ClearNodeClaim (EDX)
        field<uint16_t>(pointer_at(ent, 0x244), 0x48) = 0;
    }
    if (!node) return;
    uint8_t* const claimer = pointer_at(node, 4);
    if (claimer) {
        const uint8_t* const c_ent = pointer_at(claimer, 0);
        const float dx = field<float>(node, 0x44) - *reinterpret_cast<const float*>(c_ent + 0x140);
        const float dy = field<float>(node, 0x48) - *reinterpret_cast<const float*>(c_ent + 0x144);
        if (range > dy * dy + dx * dx) return;
    }
    call_at(L, 0x002696e7, own_address(&PathNode_SetClaim_entry), TitleRegs{word_at(ent, 0x244), self, 0, 0, node_at, edi_now}, 0,
            nullptr);  // PathNode_SetClaim (EAX sentient, ESI node)
    field<uint16_t>(pointer_at(ent, 0x244), 0x48) = field<uint16_t>(node, 0);
}

PLATFORM_ASM_ENTRY(G_PlayerMelee_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x30000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(G_PlayerMelee) "addl $20, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL G_PlayerMelee(uint32_t entry, uint8_t* ent, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // EBX player: unless on a turret/vehicle (client +0xc0 & 0x106000 with +0x297 set), the view
    // axis (AnglesToAxis of the client's view angles +0x9c) with the view origin
    // (Player_GetViewOrigin) as its fourth row and the weapon def at +0x3c of that block go to
    // Weapon_MeleeTrace (EAX block). Frame: aligned base - 0x60 (origin copy +0x10, block +0x20).
    stack_at(entry - 4) = ebp;
    const uint32_t L = ((entry - 4) & ~15u) - 0x60;
    stack_at(L + 4) = esi;
    stack_at(L) = edi;
    const uint32_t self = address_of(ent);
    uint8_t* const client = pointer_at(ent, 0x23c);
    if ((word_at(client, 0xc0) & 0x106000) && ent[0x297]) return;
    const uint32_t def = address_of(weapon_def(ent[3]));
    stack_at(L + 0x5c) = def;
    const uint32_t angles = address_of(client) + 0x9c;
    const uint32_t axis_args[2] = {angles, L + 0x20};
    call_at(L, 0x00269754, own_address(&AnglesToAxis_entry), TitleRegs{angles, self, def, L + 0x20, L + 0x38, L + 0x2c}, 2,
            axis_args);  // AnglesToAxis
    for (uint32_t i = 0; i < 3; ++i) stack_at(L + 0x10 + i * 4) = stack_at(L + 0x44 + i * 4);
    const uint32_t ent_arg[1] = {self};
    call_at(L - 8, 0x00269782, own_address(&Player_GetViewOrigin_entry), TitleRegs{angles, self, def, L + 0x20, L + 0x38, L + 0x10}, 1,
            ent_arg);  // Player_GetViewOrigin (EDI out)
    for (uint32_t i = 0; i < 3; ++i) stack_at(L + 0x44 + i * 4) = stack_at(L + 0x10 + i * 4);
    call_at(L - 0xc, 0x002697b0, own_address(&Weapon_MeleeTrace_entry), TitleRegs{L + 0x20, self, 0, 0, L + 0x38, L + 0x10}, 1,
            ent_arg);  // Weapon_MeleeTrace (EAX block)
}

// The spawn functions keep stack addresses in 0x00929b40: every callee runs at the original's
// ESP (frame entry - 0xc: key +4, map iterator +8).
PLATFORM_ASM_ENTRY(G_CallSpawnByClassname_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x30000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(G_CallSpawnByClassname) "addl $20, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL G_CallSpawnByClassname(uint32_t entry, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // Q3's G_CallSpawn (EDI entity): the spawn function for the class hash (+0x260) from the
    // spawn map (0x00983e1c, built on first use), else Actor_Spawn for "actor_*" classes, else
    // G_SpawnItem for a matching bg_itemlist entry (0x34 bytes, hash +0, ends at a null +4);
    // a spawned entity's string hashes are refreshed.
    (void)ebp;
    const uint32_t L = entry - 0xc;
    uint8_t* const ent = reinterpret_cast<uint8_t*>(uintptr_t{edi});
    if (!title_global<uint32_t>(0x00983e28))
        call_at(entry - 8, 0x00269cc1, own_address(&G_InitSpawnFuncMap), TitleRegs{0, ebx, 0, 0, esi, edi}, 0, nullptr);  // G_InitSpawnFuncMap
    stack_at(L) = esi;
    stack_at(L + 4) = word_at(ent, 0x260);
    call_at(L, 0x00269cde, own_address(&UintHashMap_Find_entry), TitleRegs{L + 8, ebx, L + 4, 0, 0x00983e1c, edi}, 0, nullptr);  // UintHashMap_Find
    const uint32_t found = stack_at(L + 8);
    auto hashes = [&](uint32_t ret) {
        call_at(L, ret, own_address(&G_UpdateEntityStringHashes_entry), TitleRegs{0, ebx, 0, 0, edi, edi}, 0, nullptr);  // G_UpdateEntityStringHashes (ESI)
    };
    if (found != title_global<uint32_t>(0x00983e24)) {
        const uint32_t index = title_global<uint32_t>(found + 0xc);
        const uint32_t args[1] = {edi};
        call_at(L, 0x00269cf5, title_global<uint32_t>(0x009885e4 + index * 8), TitleRegs{found, ebx, index, 0, 0x00983e1c, edi},
                1, args);
        hashes(0x00269cff);
        return;
    }
    const uint32_t rep = word_at(ent, 0x25c);
    const uint32_t name = rep ? rep + 6 : 0;
    const uint32_t compare_args[3] = {name, 0x00839dc8, 6};
    if (!call_at(L, 0x00269d22, 0x001eeaf0, TitleRegs{name, ebx, 0, 0, 0x00983e1c, edi}, 3, compare_args)) {  // _strncmp "actor_"
        call_at(L, 0x00269d30, own_address(&Actor_Spawn_entry), TitleRegs{edi, ebx, 0, 0, 0x00983e1c, edi}, 0, nullptr);  // Actor_Spawn (EAX)
        hashes(0x00269d37);
        return;
    }
    if (!title_global<uint32_t>(0x0096bd48)) return;
    for (uint32_t item = 0x0096bd44;; item += 0x34) {  // bg_itemlist
        if (title_global<uint32_t>(item) == word_at(ent, 0x260)) {
            call_at(L, 0x00269d6a, own_address(&G_SpawnItem_entry), TitleRegs{edi, ebx, 0, 0, item, edi}, 0, nullptr);  // G_SpawnItem (EAX, ESI item)
            hashes(0x00269d71);
            return;
        }
        if (!title_global<uint32_t>(item + 0x38)) return;
    }
}

void XB_CDECL SP_script_vehicle(uint8_t* ent) {
    // The "vehicletype" spawn key (hash cached under 0x0098f208) names the vehicle (Vehicle_Init,
    // EAX entity); physics vehicles (def +0x20 == 2) load their physics state and, without
    // spawnflag 0x400000 (+0x2a4), take their origin/angles (+0xc0/+0xe0 of the vehicle).
    const uint32_t key = cached_key(0x0098f208, 1, 0x0098f204, 0x00839f30);
    uint32_t value = 0;
    const int32_t count = title_global<int32_t>(0x00985420);
    for (int32_t i = 0; i < count; ++i) {
        if (key != title_global<uint32_t>(0x00985424 + static_cast<uint32_t>(i) * 8)) continue;
        value = title_global<uint32_t>(0x00985428 + static_cast<uint32_t>(i) * 8);
        break;
    }
    const TitleRegs init_regs{address_of(ent), 0, 0, 0, address_of(ent), 0};
    const uint32_t init_args[2] = {value, 1};
    g_ent_call(own_address(&Vehicle_Init_entry), &init_regs, 2, init_args);  // Vehicle_Init
    uint8_t* const vehicle = pointer_at(ent, 0x248);
    const uint8_t* const def = title_global<const uint8_t*>(0x00950a28 + field<int16_t>(vehicle, 0x178) * 4);
    if (*reinterpret_cast<const int16_t*>(def + 0x20) != 2) return;
    const TitleRegs load_regs{0, 0, address_of(ent), 0, address_of(ent), address_of(vehicle) + 0xc0};
    g_ent_call(own_address(&Vehicle_LoadPhysicsState), &load_regs, 0, nullptr);  // Vehicle_LoadPhysicsState (ECX)
    if (word_at(ent, 0x2a4) & 0x400000) return;
    const TitleRegs set_regs{address_of(ent), 0, 0, address_of(vehicle) + 0xe0, address_of(ent), address_of(vehicle) + 0xc0};
    const uint32_t set_args[2] = {address_of(vehicle) + 0xc0, address_of(vehicle) + 0xe0};
    g_ent_call(own_address(&Vehicle_SetOriginAndAngles_entry), &set_regs, 2, set_args);  // Vehicle_SetOriginAndAngles (EAX)
}

// --- Radius damage, mover unsticking, turret use and auto-targeting ---

namespace {

// sqrt(z*z + y*y + x*x) of v on the x87 (FLD/FMUL/FADDP, FSQRT), stored as float.
float x87_length(const float* v) {
    float result;
    __asm__("flds 8(%1)\n\tfmuls 8(%1)\n\tflds 4(%1)\n\tfmuls 4(%1)\n\tfaddp\n\tflds (%1)\n\tfmuls (%1)\n\tfaddp\n\t"
            "fsqrt\n\tfstps %0"
            : "=m"(result)
            : "r"(v)
            : "st", "st(1)", "memory");
    return result;
}

// The entity of a handle (index & 0xfff below 0x540, generation >> 12 matching), else 0.
uint32_t entity_of_handle(uint32_t handle) {
    const uint32_t index = handle & 0xfff;
    if (index >= 0x540) return 0;
    if ((handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return 0;
    return title_global<uint32_t>(0x0097a968 + index * 8);  // g_entityHandleTable
}

}  // namespace

PLATFORM_ASM_ENTRY(G_RadiusDamage_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x30000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(G_RadiusDamage) "addl $20, %esp\n"
                   "popl %esp\n"
                   "ret")

uint32_t XB_CDECL G_RadiusDamage(uint32_t entry, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // Q3's G_RadiusDamage (origin, inflictor, attacker, max damage, min damage, radius, ignore, means of
    // death): nothing without an attacker; radius at least 1 (kept in its argument slot). The
    // entities in the box of half-size radius * sqrt 2 (SV_AreaEntities, 0x540 handles) that take
    // damage (+0x29c), are not ignored and (the player attacking) not the player's allies (team 2),
    // nearer than the radius (to the origin, brush models to their bounds; x87 length): unless god
    // mode (0x0098600c) protects a client, damage lerped max..min by distance where CanDamage, flags
    // 9, direction to the origin 50 up (G_Damage). Returns 1 when another client with health was
    // hit. Frame: aligned base - 0x1550 (mins +0x10, index +0x20, result +0x24, length +0x28,
    // damage +0x2c, maxs/direction +0x30, count +0x4c, handles +0x50).
    stack_at(entry - 4) = ebp;
    const uint32_t L = ((entry - 4) & ~15u) - 0x1550;
    stack_at(L + 8) = ebx;
    stack_at(L + 4) = esi;
    stack_at(L) = edi;
    auto f = [](uint32_t address) -> float& { return stack_float(address); };
    const uint32_t origin_at = stack_at(entry + 4);
    const float* const origin = reinterpret_cast<const float*>(uintptr_t{origin_at});
    for (uint32_t i = 0; i < 0x540; ++i) stack_at(L + 0x50 + i * 4) = 0;
    stack_at(L + 0x24) = 0;
    const uint32_t attacker = stack_at(entry + 0xc);
    if (!attacker) return 0;
    const float one = title_global<float>(0x0083eb20);
    if (one > f(entry + 0x18)) f(entry + 0x18) = one;
    const float reach = f(entry + 0x18) * title_global<float>(0x0083f1f0);  // sqrt 2
    f(L + 0x10) = origin[0] - reach;
    f(L + 0x30) = reach + origin[0];
    f(L + 0x14) = origin[1] - reach;
    f(L + 0x34) = origin[1] + reach;
    f(L + 0x18) = origin[2] - reach;
    f(L + 0x38) = origin[2] + reach;
    const uint32_t area_args[3] = {L + 0x50, 0x540, 0xffffffffu};
    const int32_t count = static_cast<int32_t>(call_at(L, 0x00268f27, own_address(&SV_AreaEntities),
                                                       TitleRegs{L + 0x30, origin_at, L + 0x10, 0, esi, L + 0x1550}, 3,
                                                       area_args));  // SV_AreaEntities
    stack_at(L + 0x4c) = static_cast<uint32_t>(count);
    stack_at(L + 0x20) = 0;
    for (int32_t i = 0; i < count; ++i) {
        stack_at(L + 0x20) = static_cast<uint32_t>(i);
        const uint32_t ent_at = entity_of_handle(stack_at(L + 0x50 + static_cast<uint32_t>(i) * 4));
        if (!ent_at || ent_at == stack_at(entry + 0x1c)) continue;
        uint8_t* const ent = reinterpret_cast<uint8_t*>(uintptr_t{ent_at});
        if (!word_at(ent, 0x29c)) continue;
        uint8_t* const sentient = pointer_at(ent, 0x244);
        if (sentient && word_at(sentient, 4) == 2) {
            const uint32_t player = word_at(title_global<uint8_t*>(0x0095f534), 4);
            if (attacker == player && ent_at != player) continue;
        }
        if (!word_at(ent, 0xdc)) {
            for (uint32_t k = 0; k < 3; ++k) f(L + 0x10 + k * 4) = field<float>(ent, 0x140 + k * 4) - origin[k];
        } else {
            for (uint32_t k = 0; k < 3; ++k) {
                const float lo = field<float>(ent, 0x100 + k * 4);
                float v;
                if (lo > origin[k])
                    v = lo - origin[k];
                else if (origin[k] > field<float>(ent, 0x110 + k * 4))
                    v = origin[k] - field<float>(ent, 0x110 + k * 4);
                else
                    v = 0.0f;
                f(L + 0x10 + k * 4) = v;
            }
        }
        f(L + 0x28) = x87_length(&f(L + 0x10));
        if (platform_x87_greater_equal(f(L + 0x28), f(entry + 0x18))) continue;
        if (word_at(ent, 0x23c) && title_global<uint32_t>(0x0098600c)) continue;
        const float scaled = one - f(L + 0x28) / f(entry + 0x18);
        const float range = f(entry + 0x10) - f(entry + 0x14);
        f(L + 0x2c) = scaled * range + f(entry + 0x14);
        const uint32_t can_args[1] = {ent_at};
        if (!call_at(L, 0x002690d0, own_address(&CanDamage_entry), TitleRegs{0, origin_at, 0, 0, ent_at, origin_at}, 1,
                     can_args))  // CanDamage (EDI origin)
            continue;
        if (word_at(ent, 0x29c) && ent_at != attacker && word_at(ent, 0x23c)) {
            uint8_t* const attacker_ent = reinterpret_cast<uint8_t*>(uintptr_t{attacker});
            if (word_at(attacker_ent, 0x23c) && field<int32_t>(pointer_at(ent, 0x23c), 0x100) > 0) stack_at(L + 0x24) = 1;
        }
        const int32_t damage = static_cast<int32_t>(f(L + 0x2c));
        f(L + 0x30) = field<float>(ent, 0x140) - origin[0];
        f(L + 0x34) = field<float>(ent, 0x144) - origin[1];
        f(L + 0x38) = field<float>(ent, 0x148) - origin[2] + title_global<float>(0x0083ec30);  // 50
        const uint32_t damage_args[9] = {ent_at, stack_at(entry + 8), attacker, L + 0x30, static_cast<uint32_t>(damage), 9,
                                         stack_at(entry + 0x20), 0, 0xffffffffu};
        call_at(L, 0x00269175, own_address(&G_Damage_entry), TitleRegs{origin_at, origin_at, static_cast<uint32_t>(damage), L + 0x30, ent_at, origin_at},
                9, damage_args);  // G_Damage (EAX point)
    }
    if (count > 0) stack_at(L + 0x20) = static_cast<uint32_t>(count);
    return stack_at(L + 0x24);
}

PLATFORM_ASM_ENTRY(Mover_UnstickPlayer_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x30000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(Mover_UnstickPlayer) "addl $24, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL Mover_UnstickPlayer(uint32_t entry, uint8_t* mover, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // EAX mover (a vehicle: +0x248): when the player is stuck in it (G_TestEntityPosition at the
    // player's origin), the player is pushed by the mover's move since the saved position/angles
    // (0x00988470 / 0x00988490; G_TryPushingEntity), else moved to a free spot
    // (G_FindUnstuckPosition), else killed (G_Damage health + 50, flags 0xa0, means 0x11).
    // Frame: aligned base - 0x30 (amove +0x10, move +0x20).
    stack_at(entry - 4) = ebp;
    const uint32_t L = ((entry - 4) & ~15u) - 0x30;
    stack_at(L + 8) = ebx;
    stack_at(L + 4) = esi;
    stack_at(L) = edi;
    const uint32_t self = address_of(mover);
    const uint32_t player = word_at(title_global<uint8_t*>(0x0095f534), 4);
    const uint32_t vehicle = word_at(mover, 0x248) + 0xc0;
    const uint32_t point = player + 0x140;
    stack_at(L + 0xc) = point;
    const uint32_t test_args[1] = {point};
    if (call_at(L, 0x0026a164, own_address(&G_TestEntityPosition), TitleRegs{player, player, 0, 0, self, vehicle}, 1, test_args) != self)
        return;  // G_TestEntityPosition (EAX entity)
    const float* const now = reinterpret_cast<const float*>(uintptr_t{vehicle});
    stack_float(L + 0x20) = now[0] - title_global<float>(0x00988470);
    stack_float(L + 0x24) = now[1] - title_global<float>(0x00988474);
    stack_float(L + 0x28) = now[2] - title_global<float>(0x00988478);
    stack_float(L + 0x10) = now[8] - title_global<float>(0x00988490);
    stack_float(L + 0x14) = now[9] - title_global<float>(0x00988494);
    stack_float(L + 0x18) = now[10] - title_global<float>(0x00988498);
    const uint32_t push_args[3] = {player, self, L + 0x10};
    if (call_at(L, 0x0026a1f0, own_address(&G_TryPushingEntity_entry), TitleRegs{L + 0x20, player, L + 0x10, 0, self, vehicle}, 3,
                push_args))  // G_TryPushingEntity (EAX move)
        return;
    if (call_at(L, 0x0026a1fc, own_address(&G_FindUnstuckPosition_entry), TitleRegs{0, player, L + 0x10, 0, self, vehicle}, 0,
                nullptr))  // G_FindUnstuckPosition
        return;
    const uint32_t health = word_at(reinterpret_cast<uint8_t*>(uintptr_t{player}), 0x350) + 0x32;
    const uint32_t damage_args[9] = {player, self, self, 0x0082eec0, health, 0xa0, 0x11, 0, 0xffffffffu};
    call_at(L, 0x0026a225, own_address(&G_Damage_entry), TitleRegs{point, player, L + 0x10, health, self, vehicle}, 9,
            damage_args);  // G_Damage (EAX point)
}

PLATFORM_ASM_ENTRY(Actor_CanUseTurret_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x30000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Actor_CanUseTurret) "addl $24, %esp\n"
                   "popl %esp\n"
                   "ret")

uint32_t XB_CDECL Actor_CanUseTurret(uint32_t entry, uint8_t* actor, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // ECX actor, EDI turret: 1 when the actor already uses it (the turret at actor +0x136c holds
    // the actor's entity in its +0x1a0 handle); 0 when the turret is in use (+0x297); else
    // Actor_TurretCanTargetEnemy (turret info flag 2) or Actor_TurretCheckNearEnemy, both at the
    // original's ESP.
    (void)ebp;
    uint8_t* const turret = reinterpret_cast<uint8_t*>(uintptr_t{edi});
    uint8_t* const used = pointer_at(actor, 0x136c);
    if (used) {
        const uint32_t handle = word_at(used, 0x1a0);
        stack_at(entry - 4) = esi;  // the original's push esi / push ebx
        if ((handle & 0xfff) < 0x540) stack_at(entry - 8) = ebx;
        if (entity_of_handle(handle) == word_at(actor, 0)) return 1;
    }
    if (turret[0x297]) return 0;
    const uint32_t info = word_at(turret, 0x24c);
    if (reinterpret_cast<const uint8_t*>(uintptr_t{info})[2] & 2) {
        const uint32_t args[2] = {edi, address_of(actor)};
        return call_at(entry, 0x0026c533, own_address(&Actor_TurretCanTargetEnemy_entry), TitleRegs{info, ebx, address_of(actor), 0, esi, edi}, 2,
                       args);  // Actor_TurretCanTargetEnemy
    }
    const uint32_t args[1] = {edi};
    return call_at(entry, 0x0026c53d, own_address(&Actor_TurretCheckNearEnemy_entry), TitleRegs{info, ebx, address_of(actor), 0, esi, edi}, 1,
                   args);  // Actor_TurretCheckNearEnemy (ECX actor)
}

PLATFORM_ASM_ENTRY(Turret_ThinkAutoTarget_entry,
                   "movl %esp, %ecx\n"
                   GAME_GAP_ESP("0x30000")
                   "pushl %edx\n"
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(Turret_ThinkAutoTarget) "addl $28, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL Turret_ThinkAutoTarget(uint32_t entry, uint8_t* turret, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp,
                                     uint32_t edx) {
    // EAX turret: aims at Turret_PickSentientTarget's sentient (convergence +0x44 of the info);
    // failing that (and not the same entity) at the previous target entity (info +8) while
    // targetable - a sentient's eye or the entity itself (Turret_TrackTargetEnt). Else the target
    // is dropped (flags 0x48 off, +0xc = 0), the state goes idle ("turretstatechange") and the
    // turret turns toward angles 0. Every callee at the original's ESP (frame entry - 0x18).
    const uint32_t L = entry - 0x18;
    stack_at(entry - 0xc) = ebx;
    stack_at(entry - 0x10) = ebp;
    stack_at(entry - 0x14) = esi;
    stack_at(entry - 0x18) = edi;
    const uint32_t self = address_of(turret);
    const uint32_t info = word_at(turret, 0x24c);
    auto info_word = [&](uint32_t offset) { return title_global<uint32_t>(info + offset); };
    const uint32_t pick_args[1] = {self};
    const uint32_t picked = call_at(L, 0x0026cd85, own_address(&Turret_PickSentientTarget_entry), TitleRegs{self, info, 0, edx, esi, self}, 1,
                                    pick_args);  // Turret_PickSentientTarget
    const uint32_t previous = info_word(8);
    uint32_t last_esi = picked;
    uint32_t last_edx = edx;
    bool try_previous = previous != 0;
    if (picked) {
        const uint32_t aim_args[1] = {picked};
        if (call_at(L, 0x0026cd9f, own_address(&Turret_AimAtTarget_entry), TitleRegs{picked, info, info_word(0x44), 1, picked, self}, 1,
                    aim_args))  // Turret_AimAtTarget
            return;
        last_edx = 1;
        if (previous == title_global<uint32_t>(picked)) try_previous = false;
    }
    if (try_previous) {
        const uint32_t can_args[3] = {self, previous, 0};
        if (call_at(L, 0x0026cdbb, own_address(&Turret_CanTargetSentient_entry), TitleRegs{picked, info, 0, last_edx, last_esi, self}, 3,
                    can_args)) {  // Turret_CanTargetSentient
            const uint32_t sentient = word_at(reinterpret_cast<uint8_t*>(uintptr_t{previous}), 0x244);
            uint32_t ok;
            if (sentient) {
                const uint32_t aim_args[1] = {sentient};
                ok = call_at(L, 0x0026cdda, own_address(&Turret_AimAtTarget_entry), TitleRegs{sentient, info, info_word(0x44), 1, last_esi, self}, 1,
                             aim_args);  // Turret_AimAtTarget
            } else {
                const uint32_t track_args[1] = {1};
                ok = call_at(L, 0x0026cde7, own_address(&Turret_TrackTargetEnt), TitleRegs{self, info, previous, last_edx, last_esi, self}, 1,
                             track_args);  // Turret_TrackTargetEnt
            }
            if (ok) return;
        }
    }
    uint8_t* info_now = pointer_at(turret, 0x24c);
    info_now[2] &= 0xb7;
    word_at(info_now, 0xc) = 0;
    info_now = pointer_at(turret, 0x24c);
    if (info_now[1]) {
        const uint32_t notify = title_global<uint32_t>(0x0097a860);
        const uint32_t notify_args[1] = {notify};
        call_at(L, 0x0026ce19, own_address(&Scr_Notify_entry), TitleRegs{notify, address_of(info_now), 0, last_edx, self, self}, 1,
                notify_args);  // Scr_Notify (ESI entity, "turretstatechange")
        last_esi = self;
    }
    info_now[1] = 0;
    stack_at(L + 0x10) = 0;
    stack_at(L + 0x14) = 0;
    const uint32_t angle_args[2] = {self, L + 0x10};
    call_at(L, 0x0026ce3c, own_address(&Turret_UpdateTargetAngles_entry), TitleRegs{0, address_of(info_now), L + 0x10, 1, last_esi, self}, 2,
            angle_args);  // Turret_UpdateTargetAngles (EDX 1)
}

void XB_CDECL Touch_Item(uint8_t* ent, uint8_t* other, uint32_t touched) {
    // Q3's Touch_Item (item, player, touched flag): once per arming (+0x297), for a living player
    // (health +0x350) not on a turret (client +0xc0 & 0x100000) and not its own thrown item
    // (+0x14 == 5, owner +0x1a0): when it cannot be grabbed (BG_CanItemBeGrabbed) a weapon item
    // shows why (already held: "gm" with the weapon's name; else the class's message - primary,
    // pistol, grenade, special). Else the pickup by type (1 weapon, 2 ammo, 3/5 health, 4 clips),
    // the pickup event (0x94, 0x95 with +0x295) and the respawn: wait (+0x384) -1 hides it for
    // good, else wait plus random spread (+0x388, at least 1) seconds to think 0x13, -1 frees it.
    uint32_t event = 0x94;
    if (!ent[0x297]) return;
    ent[0x297] = 0;
    uint8_t* const client = pointer_at(other, 0x23c);
    if (!client) return;
    if (field<int32_t>(other, 0x350) < 1) return;
    if (word_at(client, 0xc0) & 0x100000) return;
    if (word_at(ent, 0x14) == 5 && word_at(ent, 0x1a0) == word_at(other, 0x224)) return;
    const TitleRegs grab{0, address_of(ent), address_of(ent), touched, address_of(other), touched};
    const uint32_t grab_args[1] = {address_of(client)};
    if (!g_ent_call(own_address(&BG_CanItemBeGrabbed), &grab, 1, grab_args)) {  // BG_CanItemBeGrabbed (ECX item)
        if (touched) return;
        uint8_t* const item = pointer_at(ent, 0x3b0);
        if (word_at(item, 0x24) != 1) return;
        const int32_t weapon = field<int32_t>(item, 0x28);
        const uint32_t* const held = reinterpret_cast<const uint32_t*>(pointer_at(other, 0x23c) + 0x3f0);
        if (Com_BitCheck(0, weapon, held)) {
            const uint32_t name = *reinterpret_cast<const uint32_t*>(weapon_def(static_cast<uint32_t>(weapon)) + 0xc);
            const uint32_t number = word_at(other, 0x224);
            SV_GameSendServerCommand(own_function<const char* XB_CDECL(uint32_t, uint32_t)>(&va_entry)(0x0083b6c8, name),
                                     number);  // va
            return;
        }
        uint32_t message;
        switch (*reinterpret_cast<const uint32_t*>(weapon_def(static_cast<uint32_t>(weapon)) + 0x9c)) {
        case 1:
        case 2: message = 0x0083b738; break;  // primary
        case 3: message = 0x0083b6a0; break;  // pistol
        case 4: message = 0x0083b678; break;  // grenade
        case 6: message = 0x0083b650; break;  // special
        default: return;
        }
        const uint32_t number = word_at(other, 0x224);
        SV_GameSendServerCommand(own_function<const char* XB_CDECL(uint32_t)>(&va_entry)(message), number);  // va
        return;
    }
    int32_t respawn;
    switch (word_at(pointer_at(ent, 0x3b0), 0x24)) {
    case 1: {
        const TitleRegs regs{0, address_of(ent), 0, 0, address_of(other), touched};
        const uint32_t args[3] = {address_of(other), address_of(&event), touched};
        respawn = static_cast<int32_t>(g_ent_call(own_address(&Pickup_Weapon_entry), &regs, 3, args));  // Pickup_Weapon (EBX item)
        break;
    }
    case 2: respawn = Pickup_Ammo(other, 0, ent); break;
    case 3:
    case 5: respawn = static_cast<int32_t>(Pickup_Health(other, ent)); break;
    case 4: respawn = Pickup_ClipsForAllWeapons(other); break;
    default: return;
    }
    if (!respawn) return;
    if (ent[0x295]) event = 0x95;
    G_AddEvent(other, event, *reinterpret_cast<const uint16_t*>(ent + 6));
    if (field<float>(ent, 0x384) == title_global<float>(0x0083eb34)) {  // -1: gone for good
        word_at(ent, 0x2a4) |= 0x400;
        word_at(ent, 0xd4) |= 1;
        word_at(ent, 0x10) |= 0x80;
        word_at(ent, 0x134) = 0;
        word_at(ent, 0x1a4) |= 2;
        return;
    }
    const float zero = 0.0f;
    if (field<float>(ent, 0x384) != zero) respawn = static_cast<int32_t>(field<float>(ent, 0x384));
    if (field<float>(ent, 0x388) != zero) {
        const int32_t r = title_function<int32_t XB_CDECL()>(0x001efcc0)();  // _rand
        float spread = static_cast<float>(r) * title_global<float>(0x0083ecf8);
        spread = spread - title_global<float>(0x0083eb20);
        spread = spread * field<float>(ent, 0x388);
        respawn += static_cast<int32_t>(spread);
        if (respawn < 1) respawn = 1;
    }
    const uint32_t flags = word_at(ent, 0x2a4);
    if (flags & 0x40) word_at(ent, 0x1a4) |= 1;
    word_at(ent, 0x2a4) = flags | 0x400;
    word_at(ent, 0xd4) |= 1;
    word_at(ent, 0x134) = 0;
    if (respawn > 0) {
        word_at(ent, 0x328) = static_cast<uint32_t>(respawn) * 1000u + title_global<uint32_t>(0x0098540c);
        word_at(ent, 0x32c) = 0x13;
    } else {
        word_at(ent, 0x328) = 0;
        word_at(ent, 0x32c) = 0;
    }
    if (respawn == -1) {
        own_function<void XB_CDECL(uint8_t*, uint32_t)>(&G_FreeEntity)(ent, 0);  // G_FreeEntity
        return;
    }
    SV_LinkEntity(ent);
}

// Frame entries with an EAX argument: the C body gets (entry ESP, EAX, EBX, ESI, EDI, EBP) and runs
// every title callee where the original's call puts it (call_at), the C frame below a gap.
#define ENT_EAX_FRAME_ENTRY(name)                                                                  \
    PLATFORM_ASM_ENTRY(name##_entry, "movl %esp, %edx\n" GAME_GAP_ESP("0x30000") "pushl %ebp\n"     \
                                     "pushl %edi\n pushl %esi\n pushl %ebx\n pushl %eax\n"          \
                                     "pushl %edx\n" PLATFORM_ASM_CALL(name) "addl $24, %esp\n"      \
                                     "popl %esp\n ret")

void XB_CDECL Touch_Item_Auto(uint8_t* ent, uint8_t* other, uint32_t touched) {
    // Touch_Item re-armed (+0x297) every touch, except a touch by a player whose client has +0x630 set.
    if (touched) {
        uint8_t* const client = pointer_at(other, 0x23c);
        if (client && word_at(client, 0x630)) return;
    }
    ent[0x297] = 1;
    Touch_Item(ent, other, touched);
}

// title_call_at_regs with an XMM5 input (AnimTree_SetKnob's goal): title_call_at_regs and its floor
// helpers leave XMM5 alone.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL ent_call_at_xmm5_raw(uint32_t top, uint32_t function, const TitleRegs* regs,
                                                                  uint32_t count, const uint32_t* args, uint32_t xmm5);
PLATFORM_ASM_ENTRY(ent_call_at_xmm5_raw,
                   "movss 24(%esp), %xmm5\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n"
                   "pushl 20(%esp)\n" PLATFORM_ASM_CALL(title_call_at_regs) "addl $20, %esp\n"
                   "ret")

// Every title callee runs where the original's call puts it, with its registers: the anim state's two
// 8-word records copy a frame word the original never writes (+0x8c), and the traces and callees read
// partly written vectors.
PLATFORM_ASM_ENTRY(G_AnimScriptedStart_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %edx\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(G_AnimScriptedStart) "addl $28, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL G_AnimScriptedStart(uint32_t entry, uint32_t ecx, uint32_t edx, uint32_t ebx, uint32_t esi,
                                  uint32_t edi, uint32_t ebp) {
    // cdecl (ent, origin, angles, anim handle, root anim, rate, mode): the entity starts a scripted anim.
    // The tree is the actor's (type 11; AnimTree_Create on first use) or the entity's own (+0x254); the
    // anim state (+0x3d4, 0x60 bytes, allocated once) keeps +0x40 the handle, +0x44 the root anim, +0x48 0,
    // +0x4c the mode. Mode 1 places the entity: the origin dropped to the floor (a trace 36 up to 18 down
    // with the actor box, only while the entity's cell is valid), the angles with pitch 0, position,
    // angles and both trajectories set, stopped; then the anim's root delta (AnimTree_GetRootDelta) moved
    // into that frame gives the end point and the end yaw, a second floor trace (128 + the delta's length
    // up and down) the ground tilt (+0x54/+0x58, else 0) and +0x5c = -1 for anims shorter than 1 s.
    // Both modes store the start pose (+0x00 quaternion of the axis, origin) and an identity (+0x20). With
    // an actor that still blends (+0x1f8 nonzero, +0x204 set) the actor's three anims keep their weights
    // (+0x210 goal weight, +0x214/+0x218 knob), else the root anim's children are cleared; the anim then
    // gets weight 1 (no restart for flag-0 anims). +0x2a4 bit 0x1000000 set.
    // Frame F: +0x1b cell valid, +0x1c state, +0x20 right / position, +0x34 length / yaw, +0x38 tree,
    // +0x3c blending / weight, +0x40 axis (forward, left, up), +0x64 origin, +0x70 angles / quaternion
    // record, +0x90 direction / trace end, +0xa8 actor, +0xac length, +0xb0 matrices, +0xfc yaw axis,
    // +0x120 trace.
    stack_at(entry - 4) = ebp;
    const uint32_t F = ((entry - 4) & ~15u) - 0x160;
    stack_at(F + 8) = ebx;
    stack_at(F + 4) = esi;
    stack_at(F) = edi;
    const uint32_t ent = stack_at(entry + 4), origin = stack_at(entry + 8), angles = stack_at(entry + 0xc);
    const uint32_t handle = stack_at(entry + 0x10), root = stack_at(entry + 0x14), rate = stack_at(entry + 0x18);
    const uint32_t mode = stack_at(entry + 0x1c);
    auto byte_at = [](uint32_t address) -> uint8_t& { return *reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(address)); };
    auto half_at = [](uint32_t address) -> uint16_t& { return *reinterpret_cast<uint16_t*>(static_cast<uintptr_t>(address)); };
    auto bits_of = [](float f) {
        uint32_t b;
        std::memcpy(&b, &f, 4);
        return b;
    };
    auto xmm0_call = [&](uint32_t top, uint32_t ret, uint32_t function, const TitleRegs& regs, uint32_t count,
                         const uint32_t* args) {
        const uint32_t bits = actor_call_at_xmm0_raw(top, function, &regs, count, args);
        stack_at(top - count * 4 - 4) = ret;
        float result;
        std::memcpy(&result, &bits, 4);
        return result;
    };
    const float one = title_global<float>(0x0083eb20);
    ebx = ent;
    const uint32_t actor = stack_at(ent + 0x240);
    esi = actor;
    edi = 0;
    stack_at(F + 0x150) = 0;
    stack_at(F + 0x154) = 0;
    stack_at(F + 0xa8) = actor;
    stack_at(F + 0x3c) = actor && !(stack_float(actor + 0x1f8) == title_global<float>(0x0083eafc)) &&
                                 stack_at(actor + 0x204)
                             ? 1
                             : 0;
    uint32_t eax;
    const uint32_t type = byte_at(ent);
    if (type == 0xb) {
        eax = stack_at(actor);
        if (!stack_at(eax + 0x254)) {
            edx = title_global<uint32_t>(0x00987a90);
            const uint32_t args[2] = {eax, edx};
            eax = call_at(F, 0x0026d714, own_address(&AnimTree_Create), TitleRegs{eax, ebx, ecx, edx, esi, edi}, 2, args);  // AnimTree_Create
            ecx = stack_at(actor);
            stack_at(ecx + 0x254) = eax;
        }
        edx = stack_at(actor);
        eax = stack_at(edx + 0x254);
        stack_at(F + 0x38) = eax;
    } else if (type == 0xd) {
        ecx = stack_at(ent + 0x254);
        stack_at(F + 0x38) = ecx;
        eax = 0;
    } else {
        eax = stack_at(ent + 0x254);
        stack_at(F + 0x38) = eax;
    }
    eax = stack_at(ent + 0x3d4);
    if (!eax) {
        const uint32_t args[2] = {0x10, 0x60};
        eax = call_at(F, 0x0026d73e, 0x00042c50, TitleRegs{eax, ebx, ecx, edx, esi, edi}, 2, args);  // Heap_AllocAligned
        stack_at(ent + 0x3d4) = eax;
    }
    const uint32_t state = eax;
    stack_at(F + 0x1c) = state;
    ecx = handle;
    edx = root;
    esi = mode;
    stack_at(state + 0x48) = 0;
    stack_at(state + 0x40) = handle;
    stack_at(state + 0x44) = root;
    stack_at(state + 0x4c) = mode;
    if (title_global<uint32_t>(0x0095bf0c)) {
        byte_at(F + 0x1b) = 1;
    } else {
        ecx = stack_at(ent + 0x458);
        if (static_cast<int32_t>(ecx) < 0) {
            byte_at(F + 0x1b) = 0;
        } else {
            const uint32_t cm = title_global<uint32_t>(0x0095f6b8);
            if (ecx >= stack_at(cm + 0x18)) ecx = 0;
            edx = stack_at(cm + 0x1c);
            ecx *= 0x50;
            byte_at(F + 0x1b) = stack_at(ecx + edx + 0x3c) != 0;
        }
    }
    const bool cell = byte_at(F + 0x1b) != 0;
    // The actor box trace from start (+0x70 in the first trace, +0x20 in the second) to end; ESI/EDI as the
    // original has them.
    auto floor_trace = [&](uint32_t ret, uint32_t start, uint32_t end, uint32_t eax_in, uint32_t ecx_in,
                           uint32_t edx_in, uint32_t esi_in, uint32_t edi_in) {
        const uint32_t pass = stack_at(ent + 0x224);
        const uint32_t args[11] = {F + 0x120, 0x0098a560, 0x0098a550, end, pass, 0x00820011, 1, 0, 0, 0, 0};
        call_at(F, ret, own_address(&SV_Trace_entry), TitleRegs{eax_in, start, ecx_in, edx_in, esi_in, edi_in}, 11, args);  // SV_Trace
    };
    float left0, left1, left2;
    if (mode == 1) {
        esi = origin;
        const float x = stack_float(origin), y = stack_float(origin + 4);
        const float up = stack_float(origin + 8) + title_global<float>(0x0083f01c);
        const float down = stack_float(origin + 8) - title_global<float>(0x0083eb00);
        if (cell) {
            const float end[4] = {x, y, down, x}, start[4] = {x, y, up, x};
            for (uint32_t i = 0; i < 4; ++i) {
                stack_float(F + 0x20 + i * 4) = end[i];
                stack_float(F + 0x70 + i * 4) = start[i];
            }
            floor_trace(0x0026d87f, F + 0x70, F + 0x20, F + 0x20, F + 0x120, edx, esi, edi);
            ebx = ent;
        }
        if (one > stack_float(F + 0x140) && cell) {
            for (uint32_t i = 0; i < 3; ++i) stack_at(F + 0x64 + i * 4) = stack_at(F + 0x120 + i * 4);
        } else {
            for (uint32_t i = 0; i < 3; ++i) stack_at(F + 0x64 + i * 4) = stack_at(origin + i * 4);
        }
        const float zero = 0.0f;
        stack_float(state + 0x50) = zero;
        stack_float(F + 0x74) = stack_float(angles + 4);
        stack_float(F + 0x78) = stack_float(angles + 8);
        stack_float(F + 0x70) = zero;
        {
            const uint32_t args[2] = {F + 0x70, F + 0x40};
            call_at(F, 0x0026d92d, own_address(&AnglesToAxis_entry), TitleRegs{F + 0x70, ebx, ecx, F + 0x40, F + 0x58, F + 0x20}, 2,
                    args);  // AnglesToAxis (ESI up, EDI right)
        }
        uint8_t* const e = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(ent));
        const float ox = stack_float(F + 0x64), oy = stack_float(F + 0x68), oz = stack_float(F + 0x6c);
        stack_float(F + 0x4c) = zero - stack_float(F + 0x20);
        field<float>(e, 0x24) = oy;
        word_at(e, 0x14) = 0;
        word_at(e, 0x18) = 0;
        word_at(e, 0x1c) = 0;
        field<float>(e, 0x28) = oz;
        field<float>(e, 0x34) = zero;
        field<float>(e, 0x30) = zero;
        field<float>(e, 0x2c) = zero;
        stack_float(F + 0x50) = zero - stack_float(F + 0x24);
        stack_float(F + 0x54) = zero - stack_float(F + 0x28);
        field<float>(e, 0x20) = ox;
        field<float>(e, 0x140) = ox;
        const float yaw = stack_float(F + 0x74), roll = stack_float(F + 0x78);
        field<float>(e, 0x144) = oy;
        field<float>(e, 0x148) = oz;
        word_at(e, 0x38) = 0;
        word_at(e, 0x3c) = 0;
        word_at(e, 0x40) = 0;
        field<float>(e, 0x44) = zero;
        field<float>(e, 0x48) = yaw;
        field<float>(e, 0x4c) = roll;
        field<float>(e, 0x58) = zero;
        field<float>(e, 0x54) = zero;
        field<float>(e, 0x50) = zero;
        field<float>(e, 0x150) = zero;
        field<float>(e, 0x154) = yaw;
        field<float>(e, 0x158) = roll;
        // The root delta of the anim (tree entry handle >> 16, 0 when out of range).
        const uint32_t trees = stack_at(title_global<uint32_t>(0x0095f524) + 4);
        esi = stack_at(trees);
        ecx = handle >> 16;
        if (ecx >= esi) ecx = 0;
        eax = handle & 0xffff;
        ecx = stack_at(trees + 4) + ecx * 12;
        {
            const uint32_t args[1] = {0x3f800000};
            call_at(F, 0x0026da37, own_address(&AnimTree_GetRootDelta_entry), TitleRegs{eax, ebx, ecx, stack_at(trees + 4), F + 0x20, F + 0x90}, 1,
                    args);  // AnimTree_GetRootDelta (EDI direction, ESI position)
        }
        const float px = stack_float(F + 0x20), py = stack_float(F + 0x24), pz = stack_float(F + 0x28);
        stack_float(F + 0x34) = static_cast<float>(platform_x87_ld(pz) * pz +
                                                   platform_x87_ld(py) * py +
                                                   platform_x87_ld(px) * px);
        stack_float(F + 0x34) = platform_x87_sqrt(stack_float(F + 0x34));
        const float length = stack_float(F + 0x34);
        for (uint32_t i = 0; i < 3; ++i) {
            float v = stack_float(F + 0x58 + i * 4) * pz;
            v = v + stack_float(F + 0x4c + i * 4) * py;
            v = v + stack_float(F + 0x40 + i * 4) * px;
            stack_float(F + 0x20 + i * 4) = v + stack_float(F + 0x64 + i * 4);
        }
        stack_float(F + 0xac) = length;
        const float delta_yaw = xmm0_call(F - 4, 0x0026db1e, own_address(&vectoyaw_2_entry),
                                          TitleRegs{eax, ebx, F + 0x90, edx, F + 0x20, F + 0x90}, 0, nullptr);  // vectoyaw_2
        stack_float(F + 0x34) = delta_yaw;
        {
            const uint32_t args[1] = {stack_at(F + 0x34)};
            call_at(F - 4, 0x0026db35, own_address(&YawToAxis_entry), TitleRegs{args[0], ebx, F + 0x90, edx, F + 0xfc, F + 0x90}, 1,
                    args);  // YawToAxis (ESI axis)
        }
        // End axis = yaw axis x start axis, (z * up + y * left) + x * forward per row.
        for (uint32_t i = 0; i < 3; ++i) {
            const float yx = stack_float(F + 0xfc + i * 12), yy = stack_float(F + 0x100 + i * 12),
                        yz = stack_float(F + 0x104 + i * 12);
            for (uint32_t j = 0; j < 3; ++j) {
                float v = yz * stack_float(F + 0x58 + j * 4);
                v = v + yy * stack_float(F + 0x4c + j * 4);
                stack_float(F + 0xb0 + i * 12 + j * 4) = v + yx * stack_float(F + 0x40 + j * 4);
            }
        }
        call_at(F, 0x0026dd0e, own_address(&AxisToAngles), TitleRegs{F + 0x70, ebx, F + 0xb0, edx, F + 0xfc, F + 0x90}, 0,
                nullptr);  // AxisToAngles (EAX angles, ECX axis)
        const float reach = stack_float(F + 0xac) + title_global<float>(0x0083ebe8);
        const float z = stack_float(F + 0x28);
        const float high = reach + z, low = z - reach;
        if (cell) {
            const float ex = stack_float(F + 0x20), ey = stack_float(F + 0x24);
            const float end[4] = {ex, ey, low, ex}, start[4] = {ex, ey, high, ex};
            for (uint32_t i = 0; i < 4; ++i) {
                stack_float(F + 0x90 + i * 4) = end[i];
                stack_float(F + 0x20 + i * 4) = start[i];
            }
            floor_trace(0x0026ddd7, F + 0x20, F + 0x90, cell, F + 0x90, F + 0x120, F + 0xfc, F + 0x90);
            ebx = ent;
        }
        if (one > stack_float(F + 0x140) && cell) {
            edi = state;
            const uint32_t pass = stack_at(ent + 0x224);
            const uint32_t args[6] = {pass, 0x00820011, stack_at(F + 0x74), state + 0x54, state + 0x58, 0};
            call_at(F, 0x0026de22, own_address(&G_ComputeGroundTilt_entry), TitleRegs{pass, ebx, args[2], state + 0x54, F + 0x120, edi}, 6,
                    args);  // G_ComputeGroundTilt (ESI point)
            esi = F + 0x120;
        } else {
            stack_float(state + 0x54) = zero;
            stack_float(state + 0x58) = zero;
            edi = state;
            esi = F + 0xfc;
        }
        float length_s;
        {
            const uint32_t args[1] = {handle};
            length_s = xmm0_call(F, 0x0026de43, own_address(&AnimTree_EnsureAnimLoaded_entry), TitleRegs{eax, ebx, ecx, handle, esi, edi}, 1,
                                 args);  // AnimTree_EnsureAnimLoaded
        }
        stack_at(state + 0x5c) = one > length_s ? title_global<uint32_t>(0x0083eb34) : bits_of(zero);
        left0 = stack_float(F + 0x4c);
        left1 = stack_float(F + 0x50);
        left2 = stack_float(F + 0x54);
    } else {
        {
            const uint32_t args[2] = {angles, F + 0x40};
            call_at(F, 0x0026de9a, own_address(&AnglesToAxis_entry), TitleRegs{F + 0x40, ebx, angles, edx, F + 0x58, F + 0x70}, 2,
                    args);  // AnglesToAxis (ESI up, EDI right)
        }
        esi = F + 0x58;
        edi = state;
        const float zero = 0.0f;
        for (uint32_t i = 0; i < 3; ++i) stack_at(F + 0x64 + i * 4) = stack_at(origin + i * 4);
        left0 = zero - stack_float(F + 0x70);
        left1 = zero - stack_float(F + 0x74);
        left2 = zero - stack_float(F + 0x78);
    }
    // The start pose as a 4x4 (forward, left, up, origin) -> quaternion + origin record, then an identity.
    const float zero = 0.0f;
    stack_float(F + 0xc0) = left0;
    stack_float(F + 0xd0) = stack_float(F + 0x58);
    stack_float(F + 0xdc) = zero;
    stack_float(F + 0xcc) = zero;
    stack_float(F + 0xbc) = zero;
    stack_float(F + 0xd4) = stack_float(F + 0x5c);
    stack_float(F + 0xec) = one;
    stack_float(F + 0xd8) = stack_float(F + 0x60);
    stack_float(F + 0xb0) = stack_float(F + 0x40);
    stack_float(F + 0xe0) = stack_float(F + 0x64);
    stack_float(F + 0xb4) = stack_float(F + 0x44);
    stack_float(F + 0xe4) = stack_float(F + 0x68);
    stack_float(F + 0xb8) = stack_float(F + 0x48);
    stack_float(F + 0xc4) = left1;
    stack_float(F + 0xc8) = left2;
    stack_float(F + 0xe8) = stack_float(F + 0x6c);
    {
        const uint32_t args[2] = {F + 0x70, F + 0xb0};
        call_at(F, 0x0026dfc5, own_address(&Quat_FromAxis3), TitleRegs{F + 0x70, ebx, ecx, F + 0xb0, esi, edi}, 2,
                args);  // Quat_FromAxis3
    }
    const uint32_t blending = stack_at(F + 0x3c);
    for (uint32_t i = 0; i < 3; ++i) stack_at(F + 0x80 + i * 4) = stack_at(F + 0xe0 + i * 4);
    std::memcpy(reinterpret_cast<void*>(static_cast<uintptr_t>(state)),
                reinterpret_cast<const void*>(static_cast<uintptr_t>(F + 0x70)), 0x20);
    stack_float(F + 0x7c) = one;
    stack_float(F + 0x78) = zero;
    stack_float(F + 0x74) = zero;
    stack_float(F + 0x70) = zero;
    stack_float(F + 0x80) = zero;
    stack_float(F + 0x84) = zero;
    stack_float(F + 0x88) = zero;
    std::memcpy(reinterpret_cast<void*>(static_cast<uintptr_t>(state + 0x20)),
                reinterpret_cast<const void*>(static_cast<uintptr_t>(F + 0x70)), 0x20);
    const uint32_t tree = stack_at(F + 0x38);
    edi = state + 0x40;
    esi = F + 0x90;
    ecx = 0;
    if (blending) {
        edi = actor;
        esi = tree;
        // The weight of the actor's anim of +0x210 (0 when the tree has no info for it); a knob for +0x214
        // or else +0x218 when its weight is above 0.
        auto weight_of = [&](uint32_t slot, float* weight) {
            const uint32_t info = half_at(tree + half_at(actor + slot) * 2 + 0x18);
            if (!info) return false;
            *weight = title_global<float>(0x00965d14 + info * 0x30);
            return true;
        };
        float weight;
        stack_float(F + 0x3c) = weight_of(0x210, &weight) ? weight : zero;
        float knob;
        uint32_t knob_slot = 0;
        if (weight_of(0x214, &knob) && knob > zero) knob_slot = 0x214;
        else if (weight_of(0x218, &knob) && knob > zero) knob_slot = 0x218;
        if (knob_slot) {
            const uint32_t index = half_at(actor + knob_slot);
            const uint32_t info = half_at(tree + index * 2 + 0x18);
            const uint32_t args[6] = {tree, 0x3e4ccccd, 0x3f800000, 0, 0, 0};
            const TitleRegs regs{index, ebx, info * 0x30, edx, esi, edi};
            ent_call_at_xmm5_raw(F, own_address(&AnimTree_SetKnob_entry), &regs, 6, args, bits_of(knob));  // AnimTree_SetKnob (XMM5 goal)
            stack_at(F - 0x1c) = 0x0026e114;
        }
        {
            const uint32_t w = stack_at(F + 0x3c);
            const uint32_t args[7] = {tree, w, 0x3e4ccccd, 0x3f800000, 0, 0, 0};
            call_at(F, 0x0026e139, own_address(&AnimTree_SetGoalWeight), TitleRegs{half_at(actor + 0x210), ebx, ecx, w, esi, edi}, 7,
                    args);  // AnimTree_SetGoalWeight
        }
        if (root & 0xffff) {
            const uint32_t args[2] = {tree, 0};
            call_at(F, 0x0026e14f, own_address(&AnimTree_ClearChildGoalWeights), TitleRegs{root & 0xffff, ebx, ecx, edx, esi, edi}, 2,
                    args);  // AnimTree_ClearChildGoalWeights
        }
    } else {
        const uint32_t args[2] = {tree, 0};
        call_at(F, 0x0026e161, own_address(&AnimTree_ClearChildGoalWeights), TitleRegs{root & 0xffff, ebx, tree, edx, esi, edi}, 2,
                args);  // AnimTree_ClearChildGoalWeights
        esi = tree;
    }
    edi = handle;
    uint32_t restart;
    {
        const uint32_t args[1] = {handle};
        restart = call_at(F, 0x0026e171, own_address(&G_GetAnimFlag0), TitleRegs{eax, ebx, ecx, edx, esi, edi}, 1, args);  // G_GetAnimFlag0
    }
    {
        const uint32_t args[7] = {tree, 0x3f800000, 0, 0x3f800000, rate, 0, restart ? 0u : 1u};
        call_at(F - 4, 0x0026e192, own_address(&AnimTree_SetGoalWeight), TitleRegs{handle & 0xffff, ebx, ecx, rate, esi, edi}, 7,
                args);  // AnimTree_SetGoalWeight
    }
    stack_at(ent + 0x2a4) |= 0x1000000;
}

uint8_t XB_STDCALL G_EntityInValidCell(uint8_t* ent) {
    // Always with no cell data loaded (0x0095bf0c); else the entity's cell (+0x458, out of range ->
    // cell 0) must have its +0x3c set. A negative cell is invalid.
    if (title_global<uint32_t>(0x0095bf0c)) return 1;
    const int32_t cell = field<int32_t>(ent, 0x458);
    if (cell < 0) return 0;
    uint8_t* const cm = title_global<uint8_t*>(0x0095f6b8);
    uint32_t index = static_cast<uint32_t>(cell);
    if (index >= word_at(cm, 0x18)) index = 0;
    return word_at(pointer_at(cm, 0x1c), index * 0x50 + 0x3c) != 0;
}

void XB_CDECL Use_BinaryMover(uint8_t* ent, uint8_t* other, uint8_t* activator) {
    // Q3's Use_BinaryMover on the team master (team slaves +0x2a4 & 0x10 pass it up through +0x380,
    // a locked one (0x100, not 0x800) locking the next). State +0x298: closed (0) or rotating closed
    // (7) doors that open away from the activator reverse the team (9); else the activator is kept
    // (+0x378) and the team opens (3, or 9 for 7) 50 ms on, an actor activator after the first 4 s
    // of an unlocked door sends point event 0xc; the master opens its area portal. Open (1) / open
    // rotating (8): the close time (+0x328) restarts (wait +0x384, 50 ms with +0x2a4 bit 7; -1000 =
    // stays open for state 1). Opening / closing ones (3, 4 / 9, 10) reverse as when blocked.
    (void)other;
    uint8_t* e = ent;
    uint32_t locked;
    uint32_t early;
    int32_t time;
    for (;;) {
        const uint32_t flags = word_at(e, 0x2a4);
        locked = 0;
        early = 0;
        if (flags & 0x100) locked = 1;
        if (flags & 0x800) locked = 0;
        time = title_global<int32_t>(0x0098540c);  // level_time
        if (time <= 4000) early = 1;
        if (!(flags & 0x10)) break;
        if (locked) word_at(pointer_at(e, 0x380), 0x2a4) |= 0x100;
        e = pointer_at(e, 0x380);
    }
    const uint32_t self = address_of(e);
    auto point_event = [&](uint32_t ebx) {
        if (early || locked || !activator || !word_at(activator, 0x244)) return;
        const TitleRegs regs{0, ebx, 0, 0, self, self};
        const uint32_t args[4] = {address_of(activator), 0xc, self + 0x140, 0};
        g_ent_call(own_address(&Actor_SendPointEvent_entry), &regs, 4, args);  // Actor_SendPointEvent
    };
    auto open_portal = [&]() {
        uint8_t* const master = pointer_at(e, 0x380);
        if (master != e && master) return;
        const int32_t area2 = field<int32_t>(e, 0x200);
        if (area2 == -1) return;
        CM_AdjustAreaPortalState(field<int32_t>(e, 0x1fc), area2, 1);
    };
    const uint8_t state = e[0x298];
    if ((state == 0 || state == 7) && Door_ChooseRotateDirection(e, activator)) {
        Mover_ReverseTeam(e, time + 0x32, 9);
        point_event(static_cast<uint32_t>(time + 0x32));
        open_portal();
        return;
    }
    word_at(e, 0x378) = address_of(activator);
    if (state == 0) {
        MatchTeam(e, 3, title_global<int32_t>(0x0098540c) + 0x32);
        open_portal();
        return;
    }
    if (state == 7) {
        MatchTeam(e, 9, title_global<int32_t>(0x0098540c) + 0x32);
        point_event(0);
        uint8_t* const master = pointer_at(e, 0x380);
        if (master != e && master) return;
        SV_AdjustAreaPortalState(e, 0, 1);
        return;
    }
    if (state == 1 || state == 8) {
        if (static_cast<int8_t>(e[0x2a4]) < 0) {
            word_at(e, 0x328) = static_cast<uint32_t>(title_global<int32_t>(0x0098540c) + 0x32);
            return;
        }
        const float wait = field<float>(e, 0x384);
        if (state == 1 && wait == title_global<float>(0x0083ec74)) return;  // -1000: stays open
        const int32_t now = title_global<int32_t>(0x0098540c);
        word_at(e, 0x328) = static_cast<uint32_t>(static_cast<int32_t>(wait) + now);
        return;
    }
    if (state == 4 || state == 3) {
        Blocked_Door(e, nullptr);
        return;
    }
    if (state == 10 || state == 9) Blocked_DoorRotate(e, nullptr);
}

void XB_REGS1 XB_CDECL TryDoor(uint8_t* door, uint8_t* activator) {
    // The activator uses a door that is idle (+0x38, +0x14 clear), not already used (+0x297) and
    // not script-controlled (+0x3b4): "trigger" notify with the activator's number, then
    // Use_BinaryMover on the door's named target (+0x380 with a +0x2f8 name, not itself) or on the
    // door; a locked door (+0x2a4 & 0x100) locks the one moved. Unlocked or 0x800 doors send point
    // event 0xc from the door's origin.
    uint32_t send_event = 0;
    const uint32_t locked = word_at(door, 0x2a4) & 0x100;
    if (word_at(door, 0x38) || word_at(door, 0x14) || door[0x297] || word_at(door, 0x3b4)) return;
    uint32_t number = word_at(activator, 0x224);
    const uint32_t trigger = title_global<uint32_t>(0x0097a818);  // scr_const_trigger
    {
        const TitleRegs regs{number, 0, address_of(&number), trigger, locked, address_of(door)};
        const uint32_t args[2] = {trigger, address_of(&number)};
        g_ent_call(own_address(&G_NotifyWithEntity_entry), &regs, 2, args);  // G_NotifyWithEntity
    }
    uint8_t* const target = pointer_at(door, 0x380);
    uint8_t* mover = door;
    if (target && !own_function<uint8_t XB_THISCALL(uint8_t*)>(&ScriptString_IsEmpty)(door + 0x2f8) &&
        door != target) {  // ScriptString_IsEmpty
        target[0x297] = 1;
        if (locked)
            word_at(pointer_at(door, 0x380), 0x2a4) |= 0x100;
        else
            send_event = 1;
        mover = pointer_at(door, 0x380);
    } else {
        door[0x297] = 1;
        if (locked)
            word_at(door, 0x2a4) |= 0x100;
        else
            send_event = 1;
    }
    Use_BinaryMover(mover, activator, activator);
    if (!(word_at(door, 0x2a4) & 0x800) && !send_event) return;
    const TitleRegs regs{0, 0, 0, 0, locked, address_of(door) + 0x140};
    const uint32_t args[4] = {address_of(activator), 0xc, address_of(door) + 0x140, 0};
    g_ent_call(own_address(&Actor_SendPointEvent_entry), &regs, 4, args);  // Actor_SendPointEvent
}

void XB_CDECL Touch_DoorTrigger(uint8_t* ent, uint8_t* other, uint32_t touched) {
    // The trigger's door (handle +0x2b0): gone -> the trigger stops touching (+0x338) and frees
    // itself in 100 ms (think 0xc); else an unscripted (+0x3b4) closed / closed-rotating / closing
    // door (state 0, 7, 10) is used with the trigger as other and the toucher as activator.
    (void)touched;
    uint8_t* const door = reinterpret_cast<uint8_t*>(uintptr_t{entity_of_handle(word_at(ent, 0x2b0))});
    if (!door) {
        word_at(ent, 0x338) = 0;
        word_at(ent, 0x328) = static_cast<uint32_t>(title_global<int32_t>(0x0098540c) + 100);
        word_at(ent, 0x32c) = 0xc;
        return;
    }
    if (word_at(door, 0x3b4)) return;
    const uint8_t state = door[0x298];
    if (state != 0 && state != 7 && state != 10) return;
    Use_BinaryMover(door, ent, other);
}

ENT_EAX_FRAME_ENTRY(Turret_FireFromFlashTags)

void XB_CDECL Turret_FireFromFlashTags(uint32_t entry, uint8_t* user, uint32_t ebx, uint32_t esi, uint32_t edi,
                                       uint32_t ebp) {
    // EAX user; cdecl (turret, target, aim): one shot from each flash tag (G_GetFlashTagMatrix 0, 1)
    // of the turret's weapon (+3) - bullets (spread 0 when aiming, else 1) or a projectile for
    // weapons with a projectile type (+0x94). Aiming: the info's bit 7 (+2) says the target point
    // (info +0x10) is behind the tag, and a hit-chance roll (info +0x54) picks a hit or miss
    // direction, used when within acos 0.995 of the tag's. The attacker is the user with a
    // nonzero number, else the world entity. Fire event 0xad after any shot. Frame: entry - 0x68
    // (spread +0x10, attacker +0x14, tag +0x18, aim dir +0x1c, tag matrix +0x28, weapon def +0x64).
    const uint32_t L = entry - 0x68;
    stack_at(L + 0xc) = ebx;
    stack_at(L + 8) = ebp;
    stack_at(L + 4) = esi;
    stack_at(L) = edi;
    const uint32_t turret = stack_at(entry + 4), target = stack_at(entry + 8), aim = stack_at(entry + 0xc);
    uint8_t* const t = reinterpret_cast<uint8_t*>(uintptr_t{turret});
    stack_float(L + 0x10) = aim ? 0.0f : title_global<float>(0x0083eb20);
    stack_at(L + 0x14) = user && word_at(user, 0x224) ? address_of(user) : word_at(title_global<uint8_t*>(0x0095f534), 8);
    uint32_t b = ebx & ~0xffu;
    stack_at(L + 0x18) = 0;
    const uint32_t m = L + 0x28;
    float* const matrix = &stack_float(m);
    for (;;) {
        const uint32_t tag = stack_at(L + 0x18);
        {
            const uint32_t args[1] = {m};
            if (!call_at(L, 0x00270c6c, own_address(&G_GetFlashTagMatrix), TitleRegs{tag, b, turret, m, esi, edi}, 1, args)) {  // G_GetFlashTagMatrix
                if (!(b & 0xff)) return;
                break;
            }
        }
        stack_at(L + 0x64) = address_of(weapon_def(t[3]));
        if (aim) {
            uint8_t* const info = pointer_at(t, 0x24c);
            const uint32_t info_word = address_of(info);
            const float d2 = field<float>(info, 0x18) - matrix[9 + 2];
            const float d1 = field<float>(info, 0x14) - matrix[9 + 1];
            const float d0 = field<float>(info, 0x10) - matrix[9];
            const float dot = ((matrix[2] * d2) + (matrix[1] * d1)) + (matrix[0] * d0);
            if (0.0f >= dot)
                info[2] |= 0x80;
            else
                info[2] &= 0x7f;
            const int32_t r = static_cast<int32_t>(
                call_at(L, 0x00270cfd, 0x001efcc0, TitleRegs{info_word, info_word + 0x10, 0, 0, esi, edi}, 0, nullptr));  // _rand
            const float roll = static_cast<float>(r) * title_global<float>(0x0083ebe0);
            esi = m;
            edi = L + 0x1c;
            b = info_word + 0x10;
            const TitleRegs regs{info_word, b, 0, 0, esi, edi};
            if (roll >= field<float>(info, 0x54))
                call_at(L, 0x00270d1c, own_address(&Turret_AimDirMiss_entry), regs, 0, nullptr);  // Turret_AimDirMiss
            else
                call_at(L, 0x00270d23, own_address(&Turret_AimDirHit_entry), regs, 0, nullptr);  // Turret_AimDirHit
            const float* const dir = &stack_float(L + 0x1c);
            const float z = dir[2], y = dir[1], x = dir[0];
            const float along = ((z * matrix[2]) + (y * matrix[1])) + (x * matrix[0]);
            if (along > title_global<float>(0x0083eef0)) {
                matrix[0] = x;
                matrix[1] = y;
                matrix[2] = z;
            }
        }
        const uint8_t* const def = reinterpret_cast<const uint8_t*>(uintptr_t{stack_at(L + 0x64)});
        const uint32_t kind = *reinterpret_cast<const uint32_t*>(def + 0x94);
        if (!kind) {
            const uint32_t args[5] = {stack_at(L + 0x14), stack_at(L + 0x10), target, turret, 0};
            call_at(L, 0x00270d9c, own_address(&Weapon_FireBullet_entry), TitleRegs{m, b, target, stack_at(L + 0x10), esi, edi}, 5,
                    args);  // Weapon_FireBullet
        } else {
            esi = m;
            const uint32_t args[3] = {0, 0x40a00000, 0};  // 5.0f
            call_at(L, 0x00270db3, own_address(&Weapon_FireProjectile_entry), TitleRegs{turret, b, kind, 0, esi, edi}, 3, args);  // Weapon_FireProjectile
        }
        const uint32_t next = stack_at(L + 0x18) + 1;
        b = (b & ~0xffu) | 1;
        stack_at(L + 0x18) = next;
        if (static_cast<int32_t>(next) >= 2) break;
    }
    const uint32_t args[2] = {0xad, 0};
    call_at(L, 0x00270dde, own_address(&G_AddEvent), TitleRegs{turret, b, 0, 0, esi, edi}, 2, args);  // G_AddEvent
}

ENT_EAX_FRAME_ENTRY(Turret_FireForUser)

void XB_CDECL Turret_FireForUser(uint32_t entry, uint8_t* turret, uint32_t ebx, uint32_t esi, uint32_t edi,
                                 uint32_t ebp) {
    // EAX turret, ESI user: a player user fires unaimed (and its client's +0x464 becomes 2), anyone
    // else with the info's aim bit (+2 bit 6); target +0x358.
    (void)ebp;
    uint8_t* const user = reinterpret_cast<uint8_t*>(uintptr_t{esi});
    const uint32_t target = word_at(turret, 0x358);
    if (user && word_at(user, 0x23c)) {
        const uint32_t args[3] = {address_of(turret), target, 0};
        call_at(entry, 0x002712bf, own_address(&Turret_FireFromFlashTags_entry), TitleRegs{esi, ebx, target, 0, esi, edi}, 3,
                args);  // Turret_FireFromFlashTags
        word_at(pointer_at(user, 0x23c), 0x464) = 2;
        return;
    }
    const uint32_t aim = (pointer_at(turret, 0x24c)[2] >> 6) & 1;
    const uint32_t args[3] = {address_of(turret), target, aim};
    call_at(entry, 0x002712f4, own_address(&Turret_FireFromFlashTags_entry), TitleRegs{esi, ebx, target, aim, esi, edi}, 3,
            args);  // Turret_FireFromFlashTags
}

ENT_EAX_FRAME_ENTRY(G_RunMover)

void XB_CDECL G_RunMover(uint32_t entry, uint8_t* ent, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // EAX mover; cdecl word for the think. A scripted anim (+0x3d4) ends or holds the mover at its
    // origin / angles (stationary trajectories, linked; still playing: type 1 at the current spot)
    // before the think. Linked movers (+0x3cc) follow their parent (G_GeneralLink); team slaves
    // only unlink when they are a tramcar or rotating mover (+0x260); others move the team
    // (G_MoverTeam) while a trajectory runs. Then water floating (0x20000000) and the think.
    // Frame: entry - 0x10 (EBX, EBP, ESI, EDI saved).
    const uint32_t L = entry - 0x10;
    stack_at(entry - 4) = ebx;
    stack_at(entry - 8) = ebp;
    stack_at(entry - 0xc) = esi;
    stack_at(entry - 0x10) = edi;
    const uint32_t self = address_of(ent);
    const uint32_t arg = stack_at(entry + 4);
    const uint32_t think_args[1] = {arg};
    uint32_t edi_now = edi;
    auto rest = [&]() {
        const TitleRegs regs{self, 0, 0, 0, self, edi_now};
        if (word_at(ent, 0x3cc)) {
            call_at(L, 0x0026f80d, own_address(&G_GeneralLink), regs, 0, nullptr);  // G_GeneralLink
        } else if (word_at(ent, 0x2a4) & 0x10) {
            if (!word_at(ent, 0xd0)) return;
            const uint32_t name = word_at(ent, 0x260);
            if (name != title_global<uint32_t>(0x0097a6e8) && name != title_global<uint32_t>(0x0097a6e4)) return;
            word_at(ent, 0xd0) = 0;  // func_tramcar / func_rotating
            SV_UnlinkEntity(ent + 0xd0);
            return;
        } else if (word_at(ent, 0x14) || word_at(ent, 0x38)) {
            const uint32_t args[1] = {self};
            call_at(L, 0x0026f85f, own_address(&G_MoverTeam_entry), regs, 1, args);  // G_MoverTeam
        }
        if (word_at(ent, 0x2a4) & 0x20000000) call_at(L, 0x0026f873, own_address(&G_FloatOnWater_entry), regs, 0, nullptr);  // G_FloatOnWater
        call_at(L, 0x0026f879, own_address(&G_RunThink_entry), regs, 1, think_args);  // G_RunThink
    };
    if (!word_at(ent, 0x3d4)) return rest();
    edi_now = self;
    call_at(L, 0x0026f6f1, own_address(&G_UpdateScriptedAnimEnd_entry), TitleRegs{word_at(ent, 0x3d4), 0, 0, 0, self, self}, 0,
            nullptr);  // G_UpdateScriptedAnimEnd
    for (uint32_t i = 0; i < 3; ++i) word_at(ent, 0x20 + i * 4) = word_at(ent, 0x140 + i * 4);
    for (uint32_t i = 0; i < 3; ++i) word_at(ent, 0x14 + i * 4) = 0;
    field<float>(ent, 0x34) = 0.0f;
    field<float>(ent, 0x30) = 0.0f;
    field<float>(ent, 0x2c) = 0.0f;
    for (uint32_t i = 0; i < 3; ++i) word_at(ent, 0x44 + i * 4) = word_at(ent, 0x150 + i * 4);
    for (uint32_t i = 0; i < 3; ++i) word_at(ent, 0x38 + i * 4) = 0;
    field<float>(ent, 0x58) = 0.0f;
    field<float>(ent, 0x54) = 0.0f;
    field<float>(ent, 0x50) = 0.0f;
    const TitleRegs link{word_at(ent, 0x150), 0, word_at(ent, 0x154), word_at(ent, 0x158), self, self};
    call_at(L, 0x0026f7a7, own_address(&SV_LinkEntity_entry), link, 0, nullptr);  // SV_LinkEntity
    if (!word_at(ent, 0x3d4)) return rest();
    word_at(ent, 0x14) = 1;
    word_at(ent, 0x38) = 1;
    for (uint32_t i = 0; i < 3; ++i) word_at(ent, 0x2c + i * 4) = word_at(ent, 0x140 + i * 4);
    for (uint32_t i = 0; i < 3; ++i) word_at(ent, 0x50 + i * 4) = word_at(ent, 0x150 + i * 4);
    call_at(L, 0x0026f7f6, own_address(&G_RunThink_entry), TitleRegs{1, 0, word_at(ent, 0x154), word_at(ent, 0x158), self, self}, 1,
            think_args);  // G_RunThink
}

ENT_EAX_FRAME_ENTRY(G_RunItem)

void XB_CDECL G_RunItem(uint32_t entry, uint8_t* ent, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // EAX item; cdecl word for the think. Q3's G_RunItem: an item without a ground entity that
    // may fall (+0x2a0 bit 0 clear) gets gravity (type 5 from now); stationary (0) / type 8 items
    // or ones outside a valid cell only think. Else the move to the trajectory's spot is traced
    // (capsule for +0x10 & 0x10; mask +0x2a8 or 0x491), the item placed at the end (start solid:
    // fraction 0), linked and thinks; when it still exists (its own handle) and the trace hit,
    // it is freed in a nodrop volume (0x80000000), else it bounces. Frame: aligned base - 0x60
    // (origin +0x10, trace +0x20, handle +0xc).
    stack_at(entry - 4) = ebp;
    const uint32_t L = ((entry - 4) & ~15u) - 0x60;
    stack_at(L + 8) = ebx;
    stack_at(L + 4) = esi;
    stack_at(L) = edi;
    const uint32_t self = address_of(ent);
    const uint32_t arg = stack_at(entry + 4);
    const uint32_t think_args[1] = {arg};
    stack_at(L + 0x50) = 0;
    stack_at(L + 0x54) = 0;
    if (!word_at(ent, 0xc) && !(ent[0x2a0] & 1) && word_at(ent, 0x14) != 5) {
        word_at(ent, 0x14) = 5;
        word_at(ent, 0x18) = title_global<uint32_t>(0x0098540c);
    }
    const uint32_t type = word_at(ent, 0x14);
    if (type == 0 || type == 8) {
        call_at(L, 0x0026fc9a, own_address(&G_RunThink_entry), TitleRegs{arg, 0, 0, 0, self, self + 0x14}, 1, think_args);  // G_RunThink
        return;
    }
    {
        const uint32_t args[1] = {self};
        const uint32_t valid = call_at(L, 0x0026fb5c, own_address(&G_EntityInValidCell), TitleRegs{type, 0, 0, 0, self, self + 0x14}, 1, args);
        if (!(valid & 0xff)) {  // G_EntityInValidCell
            call_at(L, 0x0026fc9a, own_address(&G_RunThink_entry), TitleRegs{arg, 0, 0, 0, self, self + 0x14}, 1, think_args);  // G_RunThink
            return;
        }
    }
    const uint32_t origin = L + 0x10, trace = L + 0x20;
    call_at(L, 0x0026fb72, own_address(&BG_EvaluateTrajectory_entry), TitleRegs{title_global<uint32_t>(0x0098540c), 0, origin, 0, self, self + 0x14}, 0,
            nullptr);  // BG_EvaluateTrajectory
    uint32_t mask = word_at(ent, 0x2a8);
    if (!mask) mask = 0x491;
    const uint32_t start = self + 0x140;
    {
        const uint32_t args[7] = {trace, start, self + 0xe0, self + 0xf0, origin, word_at(ent, 0x1a0), mask};
        if (ent[0x10] & 0x10)
            call_at(L, 0x0026fbb3, own_address(&G_TraceCapsule), TitleRegs{self + 0xf0, start, self + 0xe0, trace, self, self + 0x14}, 7,
                    args);  // G_TraceCapsule
        else
            call_at(L, 0x0026fbda, own_address(&G_Trace), TitleRegs{self + 0xe0, start, trace, self + 0xf0, self, self + 0x14}, 7,
                    args);  // G_Trace
    }
    field<float>(ent, 0x140) = stack_float(trace);
    const uint8_t start_solid = *reinterpret_cast<const uint8_t*>(uintptr_t{trace + 0x3d});
    field<float>(ent, 0x144) = stack_float(trace + 4);
    field<float>(ent, 0x148) = stack_float(trace + 8);
    if (start_solid) stack_float(trace + 0x20) = 0.0f;
    call_at(L, 0x0026fc1b, own_address(&SV_LinkEntity_entry), TitleRegs{start_solid, start, 0, 0, self, self}, 0, nullptr);  // SV_LinkEntity
    call_at(L, 0x0026fc24, own_address(&G_RunThink_entry), TitleRegs{start_solid, start, 0, 0, self, arg}, 1, think_args);  // G_RunThink
    const uint32_t number = word_at(ent, 0x224);
    stack_at(L + 0xc) = number;
    if (!call_at(L, 0x0026fc3a, own_address(&G_EntHandleToEnt), TitleRegs{L + 0xc, start, 0, number, self, arg}, 0, nullptr))
        return;  // G_EntHandleToEnt
    if (stack_float(trace + 0x20) == title_global<float>(0x0083eb20)) return;
    const uint32_t args[3] = {start, 0, 0x80000000};
    const uint32_t contents =
        call_at(L, 0x0026fc65, own_address(&SV_PointContents), TitleRegs{L - 8, start, 0, 0, self, arg}, 3, args);  // SV_PointContents
    if (contents) {
        const uint32_t free_args[2] = {self, arg};
        call_at(L, 0x0026fc73, own_address(&G_FreeEntity), TitleRegs{contents, start, 0, 0, self, arg}, 2, free_args);  // G_FreeEntity
        return;
    }
    const uint32_t bounce_args[1] = {self};
    call_at(L, 0x0026fc87, own_address(&G_BounceItem_entry), TitleRegs{trace, start, 0, 0, self, arg}, 1, bounce_args);  // G_BounceItem
}

ENT_EAX_FRAME_ENTRY(FireWeapon)

void XB_CDECL FireWeapon(uint32_t entry, uint8_t* ent, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // EAX player: unless on a turret / vehicle (client +0xc0 & 0x106000 with +0x297), the level's
    // player fires its weapon (+3) from the view (Player_GetViewOriginAndAxis): the spread blends
    // from BG_GetWeaponSpread toward the def's +0x658 by the client's +0x5ec, ADS when +0x88 is 1.
    // By the def's type (+0x94): 0 bullets (range +0x5b8, cone +0x860 ADS / +0x85c hip, degrees),
    // 1 a launched grenade, 2 a projectile (speed 10, 1), 3 a cluster at the trace; 4, 5 nothing;
    // else G_Error. Frame: entry - 0x54 (spread +0x10, view +0x14, weapon def at entry - 4).
    stack_at(entry - 0x48) = edi;
    const uint32_t self = address_of(ent);
    if ((word_at(pointer_at(ent, 0x23c), 0xc0) & 0x106000) && ent[0x297]) return;
    if (ent != pointer_at(title_global<uint8_t*>(0x0095f534), 4)) return;
    const uint32_t weapon = ent[3];
    stack_at(entry - 0x4c) = ebx;
    stack_at(entry - 0x50) = ebp;
    const uint32_t def = address_of(weapon_def(weapon));
    stack_at(entry - 4) = def;
    const uint32_t view = entry - 0x40;
    {
        const uint32_t args[2] = {self, view};
        call_at(entry - 0x50, 0x00270e44, own_address(&Player_GetViewOriginAndAxis), TitleRegs{def, ebx, view, weapon, esi, self}, 2,
                args);  // Player_GetViewOriginAndAxis
    }
    uint8_t* const client = pointer_at(ent, 0x23c);
    const float ads_frac = field<float>(client, 0x88);
    const float blend = field<float>(client, 0x5ec);
    const uint32_t ads = ads_frac == title_global<float>(0x0083eb20) ? 1 : 0;
    const uint32_t b = (ebx & ~0xffu) | ads;
    stack_at(entry - 0x54) = esi;
    const uint32_t L = entry - 0x54;
    float base;
    {
        const uint32_t time = title_global<uint32_t>(0x0098540c);
        const uint32_t args[2] = {time, ads};
        const TitleRegs regs{time, b, address_of(client), weapon, weapon, self};
        const uint32_t bits = actor_call_at_xmm0_raw(L, own_address(&BG_GetWeaponSpread_entry), &regs, 2, args);  // BG_GetWeaponSpread
        stack_at(L - 0xc) = 0x00270e8c;
        std::memcpy(&base, &bits, 4);
    }
    const uint8_t* const d = reinterpret_cast<const uint8_t*>(uintptr_t{stack_at(entry - 4)});
    const float spread = (*reinterpret_cast<const float*>(d + 0x658) - base) * blend + base;
    stack_float(L + 0x10) = spread;
    const uint32_t spread_bits = stack_at(L + 0x10);
    const uint32_t kind = *reinterpret_cast<const uint32_t*>(d + 0x94);
    switch (kind) {
    case 0: {
        const uint8_t* const wd = weapon_def(weapon);
        float cone = ads ? *reinterpret_cast<const float*>(wd + 0x860) : *reinterpret_cast<const float*>(wd + 0x85c);
        cone = cone * title_global<float>(0x0083eda8);
        uint32_t cone_bits;
        std::memcpy(&cone_bits, &cone, 4);
        const uint32_t range = *reinterpret_cast<const uint32_t*>(d + 0x5b8);
        const uint32_t args[5] = {self, spread_bits, range, self, cone_bits};
        call_at(L, 0x00270ef6, own_address(&Weapon_FireBullet_entry), TitleRegs{view, b, range, spread_bits, address_of(wd), self}, 5,
                args);  // Weapon_FireBullet
        return;
    }
    case 1: {
        const uint32_t args[1] = {weapon};
        call_at(L, 0x00270f10, own_address(&weapon_grenadelauncher_fire_entry), TitleRegs{view, b, address_of(d), spread_bits, weapon, self}, 1,
                args);  // weapon_grenadelauncher_fire
        return;
    }
    case 2: {
        const uint32_t args[3] = {spread_bits, 0x41200000, 1};  // 10.0f
        call_at(L, 0x00270f37, own_address(&Weapon_FireProjectile_entry), TitleRegs{self, b, address_of(d), spread_bits, view, self}, 3,
                args);  // Weapon_FireProjectile
        return;
    }
    case 3: {
        const uint32_t args[2] = {self, spread_bits};
        call_at(L, 0x00270f56, own_address(&Weapon_FireClusterAtTrace_entry), TitleRegs{kind, view, spread_bits, spread_bits, weapon, self}, 2,
                args);  // Weapon_FireClusterAtTrace
        return;
    }
    case 4:
    case 5: return;
    default: {
        const uint32_t name = *reinterpret_cast<const uint32_t*>(d + 8);
        const uint32_t args[3] = {0x00839c38, kind, name};  // "Unknown weapon type %i for %s\n"
        call_at(L, 0x00270f7a, own_address(&G_Error_entry), TitleRegs{kind, b, address_of(d), name, weapon, self}, 3, args);  // G_Error
        return;
    }
    }
}
ENT_EAX_FRAME_ENTRY(G_MoverTeam)

void XB_CDECL G_MoverTeam(uint32_t entry, uint8_t* unused_eax, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // cdecl mover: Q3's G_MoverTeam - each team part (chain +0x37c) moves (G_MoverPush) by its
    // trajectories' step (+0x14 pos, +0x38 apos) to now. All moved: actors that were pushed
    // (pushed list 0x00955298 .. pushed_p 0x0095bef8, 0x14 each) trim their paths (Path_AccumulateTrim
    // and their path state cleared), then each part's reached callback (+0x330) once a trajectory
    // has run its time. Blocked: the pushed entities go back (origin; client delta yaw / actor
    // yaws by the turn, as shorts), linked; the team's trajectories are delayed by the frame, put
    // back and linked; the blocked callback (+0x334) gets the obstacle. Frame: aligned base -
    // 0x2a50 (obstacle +0x10, move +0x18, amove +0x24, angles +0x30, origin +0x40, lists +0x50 and
    // +0x1550).
    (void)unused_eax;
    stack_at(entry - 4) = ebp;
    const uint32_t L = ((entry - 4) & ~15u) - 0x2a50;
    stack_at(L + 8) = ebx;
    stack_at(L + 4) = esi;
    stack_at(L) = edi;
    uint8_t* const ent = reinterpret_cast<uint8_t*>(uintptr_t{stack_at(entry + 4)});
    const uint32_t self = address_of(ent);
    for (uint32_t i = 0; i < 0x540; ++i) stack_at(L + 0x1550 + i * 4) = 0;
    for (uint32_t i = 0; i < 0x540; ++i) stack_at(L + 0x50 + i * 4) = 0;
    stack_at(L + 0x10) = 0;
    title_global<uint32_t>(0x0095bef8) = 0x00955298;  // pushed_p = pushed
    const uint32_t origin = L + 0x40, angles = L + 0x30, move = L + 0x18, amove = L + 0x24;
    auto evaluate = [&](uint32_t ret, uint32_t part, uint32_t out, uint32_t tr) {
        call_at(L, ret, own_address(&BG_EvaluateTrajectory_entry), TitleRegs{title_global<uint32_t>(0x0098540c), self, out, 0, part, tr}, 0,
                nullptr);  // BG_EvaluateTrajectory
    };
    bool blocked = false;
    for (uint8_t* part = ent; part; part = pointer_at(part, 0x37c)) {
        const uint32_t p = address_of(part);
        evaluate(0x0026e4b1, p, origin, p + 0x14);
        evaluate(0x0026e4c2, p, angles, p + 0x38);
        for (uint32_t i = 0; i < 3; ++i) stack_float(move + i * 4) = stack_float(origin + i * 4) - field<float>(part, 0x140 + i * 4);
        for (uint32_t i = 0; i < 3; ++i) stack_float(amove + i * 4) = stack_float(angles + i * 4) - field<float>(part, 0x150 + i * 4);
        const uint32_t args[5] = {move, amove, L + 0x10, L + 0x1550, L + 0x50};
        if (!call_at(L, 0x0026e55d, own_address(&G_MoverPush_entry), TitleRegs{p, self, move, L + 0x10, p, p + 0x38}, 5, args)) {  // G_MoverPush
            blocked = true;
            break;
        }
    }
    if (!blocked) {
        uint32_t p = title_global<uint32_t>(0x0095bef8) - 0x14;
        stack_at(L + 0x10) = p;
        while (p >= 0x00955298) {
            uint8_t* const pushed = reinterpret_cast<uint8_t*>(uintptr_t{stack_at(p)});
            uint8_t* const actor = pointer_at(pushed, 0x240);
            stack_at(L + 0x14) = address_of(pushed);
            if (actor) {
                const uint32_t a = address_of(actor);
                call_at(L, 0x0026e5b3, own_address(&Path_AccumulateTrim_entry), TitleRegs{p, word_at(actor, 0) + 0x140, address_of(pushed), 0, a + 0x3c0, a},
                        0, nullptr);  // Path_AccumulateTrim
                *reinterpret_cast<uint16_t*>(actor + 0x740) = 0;
                *reinterpret_cast<uint16_t*>(actor + 0x742) = 0;
                word_at(actor, 0x7c4) = 0;
                pointer_at(pointer_at(pushed, 0x240), 4)[0x5a] = 0;
                p = stack_at(L + 0x10);
            }
            p -= 0x14;
            stack_at(L + 0x10) = p;
        }
        for (uint8_t* part = ent; part; part = pointer_at(part, 0x37c)) {
            const uint32_t p = address_of(part);
            if (word_at(part, 0x14) &&
                title_global<int32_t>(0x0098540c) >= field<int32_t>(part, 0x1c) + field<int32_t>(part, 0x18) &&
                word_at(part, 0x330)) {
                const uint32_t args[1] = {p};
                call_at(L, 0x0026e61e, title_global<uint32_t>(0x008da7fc + word_at(part, 0x330) * 4),
                        TitleRegs{word_at(part, 0x330), self, 0, 0, p, 0}, 1, args);  // entityReachedFuncs
            }
            if (word_at(part, 0x38) &&
                title_global<int32_t>(0x0098540c) >= field<int32_t>(part, 0x40) + field<int32_t>(part, 0x3c) &&
                word_at(part, 0x330)) {
                const uint32_t args[1] = {p};
                call_at(L, 0x0026e64c, title_global<uint32_t>(0x008da7fc + word_at(part, 0x330) * 4),
                        TitleRegs{word_at(part, 0x330), self, 0, 0, p, 0}, 1, args);  // entityReachedFuncs
            }
        }
        return;
    }
    for (uint32_t p = title_global<uint32_t>(0x0095bef8) - 0x14; p >= 0x00955298; p -= 0x14) {
        uint8_t* const pushed = reinterpret_cast<uint8_t*>(uintptr_t{stack_at(p)});
        for (uint32_t i = 0; i < 3; ++i) word_at(pushed, 0x140 + i * 4) = stack_at(p + 4 + i * 4);
        for (uint32_t i = 0; i < 3; ++i) word_at(pushed, 0x20 + i * 4) = stack_at(p + 4 + i * 4);
        const float yaw = stack_float(p + 0x10);
        if (uint8_t* const client = pointer_at(pushed, 0x23c)) {
            const int32_t turn = static_cast<int32_t>(yaw * title_global<float>(0x0083ebd8)) & 0xffff;
            field<int32_t>(client, 0x58) = field<int32_t>(client, 0x58) - turn;
            for (uint32_t i = 0; i < 3; ++i) word_at(pointer_at(pushed, 0x23c), i * 4) = stack_at(p + 4 + i * 4);
        } else if (uint8_t* const actor = pointer_at(pushed, 0x240)) {
            const float to_short = title_global<float>(0x0083ebd8), to_degrees = title_global<float>(0x0083ebd4);
            int32_t s = static_cast<int32_t>((field<float>(actor, 0x138) - yaw) * to_short) & 0xffff;
            field<float>(actor, 0x138) = static_cast<float>(s) * to_degrees;
            uint8_t* const again = pointer_at(pushed, 0x240);
            s = static_cast<int32_t>((field<float>(again, 0x14c) - stack_float(p + 0x10)) * to_short) & 0xffff;
            field<float>(again, 0x14c) = static_cast<float>(s) * to_degrees;
        }
        call_at(L, 0x0026e76d, own_address(&SV_LinkEntity_entry), TitleRegs{0, self, 0, 0, p, address_of(pushed)}, 0, nullptr);  // SV_LinkEntity
    }
    for (uint8_t* part = ent; part; part = pointer_at(part, 0x37c)) {
        const uint32_t p = address_of(part);
        int32_t step = title_global<int32_t>(0x0098540c) - title_global<int32_t>(0x00985410);
        field<int32_t>(part, 0x18) = field<int32_t>(part, 0x18) + step;
        step = title_global<int32_t>(0x0098540c) - title_global<int32_t>(0x00985410);
        field<int32_t>(part, 0x3c) = field<int32_t>(part, 0x3c) + step;
        evaluate(0x0026e7be, p, p + 0x140, p + 0x14);
        evaluate(0x0026e7d1, p, p + 0x150, p + 0x38);
        call_at(L, 0x0026e7d8, own_address(&SV_LinkEntity_entry), TitleRegs{0, self, p + 0x150, 0, p, p}, 0, nullptr);  // SV_LinkEntity
    }
    if (!word_at(ent, 0x334)) return;
    const uint32_t args[2] = {self, stack_at(L + 0x10)};
    call_at(L, 0x0026e7fd, title_global<uint32_t>(0x008da808 + word_at(ent, 0x334) * 4),
            TitleRegs{word_at(ent, 0x334), self, 0, stack_at(L + 0x10), 0, 0}, 2, args);  // entityBlockedFuncs
}
namespace {

// The packed squared distance of two entities' origins (+0x140, four lanes as MOVAPS/SUBPS/MULPS):
// lane 0 = (x² + y²) + z², lanes 1-3 the squares of y, z and w, stored at `out`.
float origin_distance_sq(const uint8_t* a, const uint8_t* b, uint32_t out) {
    float d[4];
    for (uint32_t i = 0; i < 4; ++i) {
        const float s = reinterpret_cast<const float*>(a + 0x140)[i] - reinterpret_cast<const float*>(b + 0x140)[i];
        d[i] = s * s;
    }
    d[0] = (d[0] + d[1]) + d[2];
    for (uint32_t i = 0; i < 4; ++i) stack_float(out + i * 4) = d[i];
    return d[0];
}

}  // namespace

ENT_EAX_FRAME_ENTRY(Turret_ThinkActorUser)

uint32_t XB_CDECL Turret_ThinkActorUser(uint32_t entry, uint8_t* turret, uint32_t ebx, uint32_t esi, uint32_t edi,
                                        uint32_t ebp) {
    // EAX turret; cdecl actor using it (or 0). Without manual aim (info +2 bit 0x20) the actor's
    // aim angles (info +0x3c/+0x40) drive the turret. With a script target (info +0x4c) or an enemy
    // (actor +4 -> +0x3c) within the turret's range (info +0x48) and 256 of the actor, the turret
    // picks its own target (Turret_UpdateTarget). Else bit 0x10 is cleared and the enemy's threat
    // time pushed 2 s on (all threats cleared without one); a target entity (info +8) in range is
    // tracked (non-sentients) or aimed at (sentients); failing that the actor's turret holds still
    // with a target, or the turret goes idle (bits 0x48, +0xc, state 0 with "turretstatechange").
    // Frame: aligned base - 0x20 (angles / packed distance +0x10).
    (void)edi;
    stack_at(entry - 4) = ebp;
    const uint32_t L = ((entry - 4) & ~15u) - 0x20;
    stack_at(L + 8) = ebx;
    stack_at(L + 4) = esi;
    stack_at(L) = edi;
    const uint32_t self = address_of(turret);
    uint8_t* const actor = reinterpret_cast<uint8_t*>(uintptr_t{stack_at(entry + 4)});
    uint8_t* const info = pointer_at(turret, 0x24c);
    const uint32_t info_word = address_of(info);
    const uint32_t angles = L + 0x10;
    uint8_t* target = nullptr;
    if (actor) {
        const uint16_t bits = *reinterpret_cast<uint16_t*>(info + 2);
        if (!(bits & 0x20)) {
            stack_float(angles) = field<float>(info, 0x3c);
            stack_float(angles + 4) = field<float>(info, 0x40);
            const uint32_t args[2] = {self, angles};
            call_at(L, 0x0026eafb, own_address(&Turret_UpdateTargetAngles_entry), TitleRegs{angles, ebx, address_of(actor), 0, info_word, self}, 2,
                    args);  // Turret_UpdateTargetAngles
            return 1;
        }
        uint32_t enemy = word_at(info, 0x4c);
        bool pick = enemy != 0;
        if (!pick) {
            enemy = word_at(pointer_at(actor, 4), 0x3c);
            if (enemy) {
                uint8_t* const enemy_ent = reinterpret_cast<uint8_t*>(uintptr_t{stack_at(enemy)});
                if (field<float>(info, 0x48) > origin_distance_sq(enemy_ent, turret, L + 0x10) &&
                    title_global<float>(0x0083eb40) > origin_distance_sq(enemy_ent, pointer_at(actor, 0), L + 0x10))
                    pick = true;
            }
        }
        if (pick) {
            const uint32_t args[2] = {self, address_of(actor)};
            return call_at(L, 0x0026eba6, own_address(&Turret_UpdateTarget_entry), TitleRegs{enemy, ebx, address_of(actor), bits, info_word, self}, 2,
                           args);  // Turret_UpdateTarget
        }
        *reinterpret_cast<uint16_t*>(info + 2) = static_cast<uint16_t>(bits & 0xffef);
        if (!enemy) {
            const uint32_t args[1] = {address_of(actor)};
            call_at(L, 0x0026ebc4, own_address(&Actor_ClearSentientThreatTimes), TitleRegs{0, ebx, address_of(actor), static_cast<uint32_t>(bits & 0xffef), info_word, self}, 1,
                    args);  // Actor_ClearSentientThreatTimes
        } else {
            const int32_t index = static_cast<int32_t>(enemy - title_global<uint32_t>(0x00985378)) / 0x60;  // g_sentients
            word_at(actor, static_cast<uint32_t>(index) * 0x50 + 0x86c) =
                static_cast<uint32_t>(title_global<int32_t>(0x0098540c) + 2000);
        }
    }
    target = pointer_at(info, 8);
    const uint32_t aiming = (info[2] >> 2) & 1;
    if (target && field<float>(info, 0x48) > origin_distance_sq(turret, target, L + 0x10)) {
        const uint32_t sentient = word_at(target, 0x244);
        if (!sentient) {
            const uint32_t args[1] = {aiming};
            call_at(L, 0x0026ec5c, own_address(&Turret_TrackTargetEnt), TitleRegs{self, address_of(target), address_of(target), aiming, info_word, self}, 1,
                    args);  // Turret_TrackTargetEnt
            return 1;
        }
        const uint32_t args[1] = {sentient};
        if (call_at(L, 0x0026ec73, own_address(&Turret_AimAtTarget_entry), TitleRegs{sentient, address_of(target), 0, aiming, info_word, self}, 1,
                    args))  // Turret_AimAtTarget
            return 1;
    }
    if (actor && target) {
        stack_float(angles) = 0.0f;
        stack_float(angles + 4) = 0.0f;
        const uint32_t args[2] = {self, angles};
        call_at(L, 0x0026eca4, own_address(&Turret_UpdateTargetAngles_entry), TitleRegs{0, address_of(target), angles, 1, info_word, self}, 2,
                args);  // Turret_UpdateTargetAngles
        return 1;
    }
    uint8_t* const now = pointer_at(turret, 0x24c);
    now[2] &= 0xb7;
    word_at(now, 0xc) = 0;
    uint8_t* const state = pointer_at(turret, 0x24c);
    if (state[1]) {
        const uint32_t args[1] = {title_global<uint32_t>(0x0097a860)};  // scr_const_turretstatechange
        call_at(L, 0x0026ecdf, own_address(&Scr_Notify_entry), TitleRegs{state[1], address_of(state), 0, 0, self, self}, 1, args);  // Scr_Notify
    }
    state[1] = 0;
    return 1;
}

ENT_EAX_FRAME_ENTRY(G_SimulateGrenadePath)

uint32_t XB_CDECL G_SimulateGrenadePath(uint32_t entry, uint8_t* ent, uint32_t ebx, uint32_t esi, uint32_t edi,
                                        uint32_t ebp) {
    // EAX grenade; cdecl (duration, out position, bounce, out time): the grenade's path in 100 ms
    // steps on a copy of its trajectory (a scratch gentity on the stack, G_InitGentity / freed after)
    // with G_MissileTrace (0x008da6f0, mask +0x2a8, owner +0x1a0). A step that stops on the ground
    // (fraction 1 from a 1.5 drop test, or a hit with normal z > 0.7) settles the trajectory's z. A
    // start-solid hit or surface 0x10 gives 0. Bouncing grenades (+0x10 & 0x3000000) bounce at the
    // hit time (G_SimulateGrenadeBounce) until the trajectory stops. Out: the last position and
    // time (fuse +0x328 first); returns the time, or the fuse for bouncing ones. Frame: aligned
    // base - 0x500 (time +0xc, position +0x10, next +0x20, trace +0x30, trajectory +0x7c, gentity +0xa0).
    stack_at(entry - 4) = ebp;
    const uint32_t L = ((entry - 4) & ~15u) - 0x500;
    stack_at(L + 8) = ebx;
    stack_at(L + 4) = esi;
    stack_at(L) = edi;
    const uint32_t self = address_of(ent);
    const uint32_t duration = stack_at(entry + 4), out_pos = stack_at(entry + 8), bounce = stack_at(entry + 0xc);
    const uint32_t out_time = stack_at(entry + 0x10);
    const uint32_t pos = L + 0x10, next = L + 0x20, trace = L + 0x30, traj = L + 0x7c, temp = L + 0xa0;
    stack_at(L + 0x60) = 0;
    stack_at(L + 0x64) = 0;
    {
        const uint32_t args[1] = {0xffffffffu};
        call_at(L, 0x0026e839, own_address(&G_InitGentity), TitleRegs{temp, self, 0, 0, esi, edi}, 1, args);  // G_InitGentity
    }
    for (uint32_t i = 0; i < 9; ++i) stack_at(traj + i * 4) = word_at(ent, 0x14 + i * 4);
    call_at(L, 0x0026e859, own_address(&BG_EvaluateTrajectory_entry), TitleRegs{title_global<uint32_t>(0x0098540c), self, pos, 0, self + 0x38, traj}, 0,
            nullptr);  // BG_EvaluateTrajectory
    stack_at(out_time) = word_at(ent, 0x328);
    int32_t t = title_global<int32_t>(0x0098540c) + 100;
    stack_at(L + 0xc) = static_cast<uint32_t>(t);
    const uint32_t mask = word_at(ent, 0x2a8);
    auto missile_trace = [&](uint32_t ret) {
        const uint32_t args[3] = {trace, pos, word_at(ent, 0x1a0)};
        call_at(L, ret, own_address(&G_MissileTrace_entry), TitleRegs{0x008da6f0, self, mask, trace, static_cast<uint32_t>(t), next}, 3,
                args);  // G_MissileTrace
    };
    auto free_temp = [&](uint32_t ret) {
        call_at(L, ret, own_address(&G_FreeEntityContents_entry), TitleRegs{0, self, temp, 0, static_cast<uint32_t>(t), next}, 0,
                nullptr);  // G_FreeEntityContents
    };
    bool stored = false;
    if (t < title_global<int32_t>(0x0098540c) + static_cast<int32_t>(duration)) {
        for (;;) {
            call_at(L, 0x0026e88f, own_address(&BG_EvaluateTrajectory_entry), TitleRegs{static_cast<uint32_t>(t), self, next, 0, static_cast<uint32_t>(t), traj},
                    0, nullptr);  // BG_EvaluateTrajectory
            missile_trace(0x0026e8b4);
            const uint8_t solid = *reinterpret_cast<const uint8_t*>(uintptr_t{trace + 0x3d});
            const float x = stack_float(trace), y = stack_float(trace + 4), z = stack_float(trace + 8);
            stack_float(pos) = x;
            stack_float(pos + 4) = y;
            stack_float(pos + 8) = z;
            if (solid) {
                free_temp(0x0026ea37);
                return 0;
            }
            float fraction = stack_float(trace + 0x20);
            const float one = title_global<float>(0x0083eb20);
            bool ground = fraction == one;
            if (!ground && one > fraction && stack_float(trace + 0x18) > title_global<float>(0x0083ed28)) ground = true;
            if (ground) {
                stack_float(next) = x;
                stack_float(next + 4) = y;
                stack_float(next + 8) = z - title_global<float>(0x0083eaf4);  // 1.5
                missile_trace(0x0026e959);
                fraction = stack_float(trace + 0x20);
                if (fraction == title_global<float>(0x0083eb20)) {
                    t += 100;
                    stack_at(L + 0xc) = static_cast<uint32_t>(t);
                    if (t < title_global<int32_t>(0x0098540c) + static_cast<int32_t>(duration)) continue;
                    break;
                }
                const float top = stack_float(trace + 8) + title_global<float>(0x0083eaf4);
                stack_float(traj + 0x14) = (top - stack_float(pos + 8)) + stack_float(traj + 0x14);
                stack_float(pos) = stack_float(trace);
                stack_float(pos + 4) = stack_float(trace + 4);
                stack_float(pos + 8) = top;
            }
            if (*reinterpret_cast<const uint8_t*>(uintptr_t{trace + 0x24}) & 0x10) {
                free_temp(0x0026ea37);
                return 0;
            }
            if (!bounce || !(word_at(ent, 0x10) & 0x3000000)) {
                stack_at(out_time) = static_cast<uint32_t>(t);
                stored = true;
                break;
            }
            const int32_t hit = t - static_cast<int32_t>(fraction * title_global<float>(0x0083eb38)) - 100;
            {
                const uint32_t args[2] = {self, static_cast<uint32_t>(hit)};
                call_at(L, 0x0026e9f4, own_address(&G_SimulateGrenadeBounce_entry), TitleRegs{traj, self, mask, trace, trace, next}, 2,
                        args);  // G_SimulateGrenadeBounce
            }
            t = static_cast<int32_t>(stack_at(L + 0xc));
            const uint32_t type = stack_at(traj);
            stack_at(traj + 4) = static_cast<uint32_t>(t);
            if (!type) {
                stack_at(out_time) = static_cast<uint32_t>(t);
                stored = true;
                break;
            }
            t += 100;
            stack_at(L + 0xc) = static_cast<uint32_t>(t);
            if (t >= title_global<int32_t>(0x0098540c) + static_cast<int32_t>(duration)) break;
        }
    }
    (void)stored;
    for (uint32_t i = 0; i < 3; ++i) stack_at(out_pos + i * 4) = stack_at(pos + i * 4);
    if (bounce && (word_at(ent, 0x10) & 0x3000000)) {
        const uint32_t fuse = word_at(ent, 0x328);
        free_temp(0x0026ea8a);
        return fuse;
    }
    free_temp(0x0026ea9f);
    return static_cast<uint32_t>(t);
}

ENT_EAX_FRAME_ENTRY(G_BounceItem)

void XB_CDECL G_BounceItem(uint32_t entry, uint8_t* tr, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // EAX trace; cdecl item. Q3's G_BounceItem with bounce factor 0: the velocity at the hit time
    // reflected off the plane (-2 dot) and scaled by 0. Start-solid: velocity 0 and a trace from
    // 64 above (capsule items; else from the origin) to 128 below, mask 0x411. On a floor
    // (normal z > 0) with dz < 40 it stops: corpses (type 0xd) are placed with the ground tilt,
    // other items raised by 0.5..1 and turned to lie on the plane (yaw from their angles; +90
    // roll for items of class 1 in the item table 0x0096bd34). Else it moves off by the normal and
    // restarts its trajectory there. Frame: aligned base - 0x60 (velocity / trace end +0x10, trace
    // start +0x20, axis +0x3c, normal +0x54).
    stack_at(entry - 4) = ebp;
    const uint32_t L = ((entry - 4) & ~15u) - 0x60;
    stack_at(L + 8) = ebx;
    stack_at(L + 4) = esi;
    stack_at(L) = edi;
    const uint32_t trace = address_of(tr);
    uint8_t* const ent = reinterpret_cast<uint8_t*>(uintptr_t{stack_at(entry + 4)});
    const uint32_t self = address_of(ent);
    const int32_t previous = title_global<int32_t>(0x00985410);
    const int32_t frame = title_global<int32_t>(0x0098540c) - previous;
    const int32_t hit_time = static_cast<int32_t>(static_cast<float>(frame) * field<float>(tr, 0x20)) + previous;
    const uint32_t velocity = L + 0x10;
    {
        const uint32_t args[1] = {self + 0x14};
        call_at(L, 0x0026ed2a, own_address(&BG_EvaluateTrajectoryDelta), TitleRegs{self + 0x14, ebx, velocity, static_cast<uint32_t>(hit_time), trace, self},
                1, args);  // BG_EvaluateTrajectoryDelta
    }
    const float vx = stack_float(velocity), vz = stack_float(velocity + 8), vy = stack_float(velocity + 4);
    float dot = ((field<float>(tr, 0x10) * vx) + (field<float>(tr, 0x18) * vz)) + (field<float>(tr, 0x14) * vy);
    dot = dot * title_global<float>(0x0083ec5c);  // -2
    const float dx = dot * field<float>(tr, 0x10) + vx;
    field<float>(ent, 0x2c) = dx;
    field<float>(ent, 0x30) = dot * field<float>(tr, 0x14) + vy;
    field<float>(ent, 0x34) = dot * field<float>(tr, 0x18) + vz;
    const float zero = 0.0f;
    field<float>(ent, 0x2c) = dx * zero;
    field<float>(ent, 0x30) = field<float>(ent, 0x30) * zero;
    field<float>(ent, 0x34) = field<float>(ent, 0x34) * zero;
    if (tr[0x3d]) {
        field<float>(ent, 0x34) = 0.0f;
        field<float>(ent, 0x30) = 0.0f;
        field<float>(ent, 0x2c) = 0.0f;
        const uint32_t end = L + 0x10, top = L + 0x20;
        stack_float(end) = field<float>(ent, 0x140);
        stack_float(top) = field<float>(ent, 0x140);
        stack_float(end + 4) = field<float>(ent, 0x144);
        stack_float(top + 4) = field<float>(ent, 0x144);
        const float z = field<float>(ent, 0x148);
        stack_float(end + 8) = z - title_global<float>(0x0083ebe8);  // 128
        stack_float(top + 8) = z + title_global<float>(0x00830c1c);  // 64
        const uint32_t capsule = (ent[0x10] & 0x10) ? 1 : 0;
        const uint32_t start = capsule ? top : self + 0x140;
        const uint32_t pass = word_at(ent, 0x224);
        const uint32_t args[11] = {trace, self + 0xe0, self + 0xf0, end, pass, 0x411, capsule, 0, 0, 0, 0};
        call_at(L, 0x0026ee82, own_address(&SV_Trace_entry), TitleRegs{self + 0xe0, start, end, self + 0xf0, trace, self}, 11, args);  // SV_Trace
    }
    if (!(field<float>(tr, 0x18) > 0.0f) || !(title_global<float>(0x0083ec84) > field<float>(ent, 0x34))) {
        field<float>(ent, 0x140) = field<float>(tr, 0x10) + field<float>(ent, 0x140);
        field<float>(ent, 0x144) = field<float>(ent, 0x144) + field<float>(tr, 0x14);
        field<float>(ent, 0x148) = field<float>(ent, 0x148) + field<float>(tr, 0x18);
        for (uint32_t i = 0; i < 3; ++i) word_at(ent, 0x20 + i * 4) = word_at(ent, 0x140 + i * 4);
        word_at(ent, 0x18) = title_global<uint32_t>(0x0098540c);
        return;
    }
    if (ent[0] == 0xd) {
        call_at(L, 0x0026eeb6, own_address(&G_SetOrigin), TitleRegs{self, ebx, trace, 0, trace, self}, 0, nullptr);  // G_SetOrigin
        word_at(ent, 0xc) = word_at(tr, 0x30);
        const uint32_t args[1] = {0};
        call_at(L, 0x0026eec3, own_address(&G_UpdateGroundTilt_entry), TitleRegs{self, ebx, word_at(tr, 0x30), 0, trace, self}, 1,
                args);  // G_UpdateGroundTilt
        call_at(L, 0x0026eecb, own_address(&SV_LinkEntity_entry), TitleRegs{self, ebx, 0, 0, trace, self}, 0, nullptr);  // SV_LinkEntity
        return;
    }
    const int32_t r = static_cast<int32_t>(call_at(L, 0x0026eed7, 0x001efcc0, TitleRegs{0, ebx, 0, 0, trace, self}, 0,
                                                   nullptr));  // _rand
    float lift = static_cast<float>(r) / title_global<float>(0x0083ebc8);
    lift = (lift + title_global<float>(0x0083eb20)) * title_global<float>(0x0083eb24);
    field<float>(tr, 8) = lift + field<float>(tr, 8);
    call_at(L, 0x0026ef06, own_address(&G_SetOrigin), TitleRegs{self, ebx, trace, 0, trace, self}, 0, nullptr);  // G_SetOrigin
    word_at(ent, 0xc) = word_at(tr, 0x30);
    const uint32_t axis = L + 0x3c, normal = L + 0x54;
    for (uint32_t i = 0; i < 3; ++i) stack_at(normal + i * 4) = word_at(tr, 0x10 + i * 4);
    call_at(L, 0x0026ef3c, own_address(&AnglesToForward_entry), TitleRegs{self, ebx, trace, 0, axis, self + 0x150}, 0, nullptr);  // AnglesToForward
    const float fz = stack_float(axis + 8), ny = stack_float(normal + 4), fy = stack_float(axis + 4);
    const float nz = stack_float(normal + 8), fx = stack_float(axis), nx = stack_float(normal);
    const float a0 = fz * ny - fy * nz;
    const float a2 = fy * nx - fx * ny;
    const float a1 = fx * nz - fz * nx;
    stack_float(axis) = a1 * nz - a2 * ny;
    stack_float(axis + 0xc) = a0;
    stack_float(axis + 0x14) = a2;
    stack_float(axis + 0x10) = a1;
    const float b1 = a2 * nx - a0 * nz;
    const float b2 = a0 * ny - a1 * nx;
    stack_float(axis + 4) = b1;
    stack_float(axis + 8) = b2;
    const uint32_t angles = L + 0x10;
    call_at(L, 0x0026efe9, own_address(&AxisToAngles), TitleRegs{angles, ebx, axis, 0, axis, self + 0x150}, 0, nullptr);  // AxisToAngles
    const uint32_t item = *reinterpret_cast<uint16_t*>(ent + 6);
    if (title_global<uint32_t>(0x0096bd34 + item * 0x34) == 1)
        stack_float(angles + 8) = stack_float(angles + 8) + title_global<float>(0x0083eb64);  // 90
    const uint32_t moved = call_at(L, 0x0026f019, own_address(&G_SetAngle2), TitleRegs{self, ebx, angles, 0, axis, self + 0x150}, 0,
                                   nullptr);  // G_SetAngle2
    call_at(L, 0x0026f020, own_address(&SV_LinkEntity_entry), TitleRegs{moved, ebx, 0, 0, axis, moved}, 0, nullptr);  // SV_LinkEntity
}

namespace {

// (heat - 0x0083ee68) * 0x0083f1d8 * 0x008ff008 on the x87 (doubles, then the float), stored as float.
float turret_emission_rate(float heat) {
    long double v = platform_x87_ld(heat);
    v = v - platform_x87_ld(title_global<double>(0x0083ee68));
    v = v * platform_x87_ld(title_global<double>(0x0083f1d8));
    v = v * platform_x87_ld(title_global<float>(0x008ff008));
    return static_cast<float>(v);
}

}  // namespace

ENT_EAX_FRAME_ENTRY(turret_think)

void XB_CDECL turret_think(uint32_t entry, uint8_t* unused_eax, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // cdecl (turret, msec): thinks every frame. Linked turrets follow their parent. Heat (info
    // +0x78): reaching 1 overheats it (+0x7c, sound 0x2a, "overheated", the smoke effect +0x80 at
    // full rate); it cools by msec * the def's +0x850 / 1000 and the overheat ends at 0.5 (smoke
    // freed). Above 0.25 the smoke runs (sound 0x29 starts it) with a rate from the heat; below it
    // is freed. A player user does the rest itself; else auto-targeting (info +2: 2 without 1),
    // the actor user's manual aim (bit 1: Turret_UpdateTarget, else it leaves), aim angles from
    // the info (+0x3c), or Turret_ThinkActorUser (the actor leaves when it fails). Frame: entry -
    // 0x18 (angles at entry - 8).
    (void)unused_eax;
    stack_at(entry - 0xc) = ebp;
    stack_at(entry - 0x10) = esi;
    stack_at(entry - 0x14) = edi;
    const uint32_t T0 = entry - 0x14;
    uint8_t* const ent = reinterpret_cast<uint8_t*>(uintptr_t{stack_at(entry + 4)});
    const uint32_t self = address_of(ent);
    const int32_t msec = static_cast<int32_t>(stack_at(entry + 8));
    word_at(ent, 0x328) = title_global<uint32_t>(0x0098540c) + 1;
    uint8_t* const info = pointer_at(ent, 0x24c);
    const uint32_t info_word = address_of(info);
    if (word_at(ent, 0x3cc))
        call_at(T0, 0x0026f8bd, own_address(&G_GeneralLink), TitleRegs{self, ebx, 0, 0, esi, info_word}, 0, nullptr);  // G_GeneralLink
    const uint8_t* const def = weapon_def(ent[3]);
    const uint32_t def_word = address_of(def);
    stack_at(entry - 0x18) = ebx;
    const uint32_t T = entry - 0x18;
    const uint32_t system = 0x0095f5b4;
    auto free_smoke = [&](uint32_t ret) {
        const uint32_t args[1] = {word_at(info, 0x80)};
        call_at(T, ret, own_address(&FX_FreeEffectsById_entry), TitleRegs{word_at(info, 0x80), def_word, 0, 0, title_global<uint32_t>(system), info_word}, 1,
                args);  // FX_FreeEffectsById
        word_at(info, 0x80) = 0;
    };
    const uint32_t name = *reinterpret_cast<const uint32_t*>(def + 8);
    if (field<float>(info, 0x78) >= title_global<float>(0x0083eb20)) {
        info[0x7c] = 1;
        {
            const uint32_t args[2] = {name, 0x2a};
            call_at(T, 0x0026f8ea, own_address(&FXCmd_PlayWeaponSound_entry), TitleRegs{self, def_word, 0, 0, esi, info_word}, 2, args);  // FXCmd_PlayWeaponSound
        }
        {
            const uint32_t args[1] = {title_global<uint32_t>(0x0097a888)};  // scr_const_overheated
            call_at(T, 0x0026f8fb, own_address(&Scr_Notify_entry), TitleRegs{self, def_word, args[0], 0, self, info_word}, 1, args);  // Scr_Notify
        }
        if (word_at(info, 0x80)) {
            const uint32_t args[4] = {title_global<uint32_t>(system), word_at(info, 0x80), 0x0083b284, 0x43480000};  // 200
            call_at(T, 0x0026f91c, own_address(&FXParam_Broadcast), TitleRegs{args[1], def_word, 0, args[0], self, info_word}, 4,
                    args);  // FXParam_Broadcast "EmissionRate"
        }
    } else if (info[0x7c] && title_global<float>(0x0083eb24) >= field<float>(info, 0x78)) {
        info[0x7c] = 0;
        if (word_at(info, 0x80)) free_smoke(0x0026f94d);
    }
    const float heat = field<float>(info, 0x78);
    if (heat > 0.0f)
        field<float>(info, 0x78) =
            heat - (static_cast<float>(msec) * *reinterpret_cast<const float*>(def + 0x850)) * title_global<float>(0x0083eb4c);
    else
        field<float>(info, 0x78) = 0.0f;
    if (field<float>(info, 0x78) > title_global<float>(0x0083ec24)) {
        if (!word_at(info, 0x80)) {
            const uint32_t args[2] = {name, 0x29};
            word_at(info, 0x80) =
                call_at(T, 0x0026f9af, own_address(&FXCmd_PlayWeaponSound_entry), TitleRegs{self, def_word, 0, 0, esi, info_word}, 2, args);  // FXCmd_PlayWeaponSound
        }
        const float rate = turret_emission_rate(field<float>(info, 0x78));
        uint32_t rate_bits;
        std::memcpy(&rate_bits, &rate, 4);
        const uint32_t args[4] = {title_global<uint32_t>(system), word_at(info, 0x80), 0x0083b284, rate_bits};
        call_at(T, 0x0026f9e9, own_address(&FXParam_Broadcast), TitleRegs{0, def_word, args[1], args[0], esi, info_word}, 4,
                args);  // FXParam_Broadcast "EmissionRate"
    } else if (word_at(info, 0x80)) {
        free_smoke(0x0026fa01);
    }
    uint8_t* const user = reinterpret_cast<uint8_t*>(uintptr_t{entity_of_handle(word_at(ent, 0x1a0))});
    if (user && word_at(user, 0x23c)) return;
    const uint32_t flags = *reinterpret_cast<const uint16_t*>(info + 2);
    if ((flags & 2) && !(flags & 1)) {
        call_at(T0, 0x0026fa61, own_address(&Turret_ThinkAutoTarget_entry), TitleRegs{self, ebx, flags & 2, address_of(user), esi, info_word}, 0,
                nullptr);  // Turret_ThinkAutoTarget
        return;
    }
    uint8_t* actor = user ? pointer_at(user, 0x240) : nullptr;
    if (actor && word_at(actor, 8) == 3 && (flags & 2)) {
        const uint32_t args[2] = {self, address_of(actor)};
        if (call_at(T0, 0x0026fac1, own_address(&Turret_UpdateTarget_entry), TitleRegs{flags, ebx, flags & 2, address_of(user), address_of(actor), info_word},
                    2, args))  // Turret_UpdateTarget
            return;
        call_at(T0, 0x0026facd, own_address(&Actor_LeaveTurret_entry), TitleRegs{0, ebx, 0, 0, address_of(actor), info_word}, 0, nullptr);  // Actor_LeaveTurret
        return;
    }
    if (actor && word_at(actor, 8) != 3) actor = nullptr;
    const uint32_t actor_word = address_of(actor);
    if (flags & 1) {
        const uint32_t angles = entry - 8;
        stack_float(angles) = field<float>(pointer_at(ent, 0x24c), 0x3c);
        stack_float(angles + 4) = field<float>(pointer_at(ent, 0x24c), 0x40);
        const uint32_t args[2] = {self, angles};
        call_at(T0, 0x0026faab, own_address(&Turret_UpdateTargetAngles_entry), TitleRegs{angles, ebx, flags & 2, 0, actor_word, info_word}, 2,
                args);  // Turret_UpdateTargetAngles
        return;
    }
    const uint32_t args[1] = {actor_word};
    if (call_at(T0, 0x0026fadc, own_address(&Turret_ThinkActorUser_entry), TitleRegs{self, ebx, flags & 2, address_of(user), actor_word, info_word}, 1,
                args))  // Turret_ThinkActorUser
        return;
    if (actor)
        call_at(T0, 0x0026faec, own_address(&Actor_LeaveTurret_entry), TitleRegs{0, ebx, 0, 0, actor_word, info_word}, 0, nullptr);  // Actor_LeaveTurret
}

ENT_EAX_FRAME_ENTRY(G_RunEntity)

void XB_CDECL G_RunEntity(uint32_t entry, uint8_t* ent, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // EAX entity, EDI the word its runners get: once per frame (+0x2ac = level_framenum). Non-client
    // entities mirror +0x2a4 bit 0x400 into +0x10 bit 0x80; +0xd4 bit 0x200 follows +0x10 bit 0x10;
    // sentients of kind 1 (+0x244 -> +4) get +0x10 bit 0x100. A pending event (+0x1a8) older than
    // 200 ms frees the entity (+0x1a4 bit 0) or unlinks it (bit 1). Linked ones follow their parent;
    // then by type: missiles, items (2, or +0x294), movers (4, 7, 0xb with 0x4000000), else water
    // floating for type 0 and the think for non-clients. Frame: entry - 4 (ESI saved).
    (void)ebp;
    stack_at(entry - 4) = esi;
    const uint32_t T = entry - 4;
    const uint32_t self = address_of(ent);
    const uint32_t args[1] = {edi};
    const uint32_t frame = title_global<uint32_t>(0x00985408);  // level_framenum
    if (word_at(ent, 0x2ac) == frame) return;
    word_at(ent, 0x2ac) = frame;
    if (!word_at(ent, 0x23c)) {
        if (word_at(ent, 0x2a4) & 0x400)
            word_at(ent, 0x10) |= 0x80;
        else
            word_at(ent, 0x10) &= 0xffffff7fu;
    }
    const uint32_t flags = word_at(ent, 0x10);
    if (flags & 0x10)
        word_at(ent, 0xd4) |= 0x200;
    else
        word_at(ent, 0xd4) &= 0xfffffdffu;
    uint8_t* const sentient = pointer_at(ent, 0x244);
    if (sentient && word_at(sentient, 4) == 1) word_at(ent, 0x10) = flags | 0x100;
    const uint32_t event_time = word_at(ent, 0x1a8);
    if (event_time && title_global<int32_t>(0x0098540c) - static_cast<int32_t>(event_time) > 200) {
        const uint32_t event_flags = word_at(ent, 0x1a4);
        if (event_flags & 1) {
            const uint32_t free_args[2] = {self, edi};
            call_at(T, 0x00274585, own_address(&G_FreeEntity), TitleRegs{event_flags, ebx, 0, 0, self, edi}, 2, free_args);  // G_FreeEntity
            return;
        }
        if (event_flags & 2) {
            word_at(ent, 0x1a4) = event_flags & ~2u;
            word_at(ent, 0xd0) = 0;
            SV_UnlinkEntity(ent + 0xd0);
        }
        word_at(ent, 0x1a8) = 0;
    }
    if (word_at(ent, 0x3cc))
        call_at(T, 0x002745c3, own_address(&G_GeneralLink), TitleRegs{self, ebx, 0, 0, self, edi}, 0, nullptr);  // G_GeneralLink
    const uint8_t type = ent[0];
    if (type == 3) {
        stack_at(T - 4) = ebx;
        call_at(T - 4, 0x002745d2, own_address(&G_RunMissile_entry), TitleRegs{type, self, 0, 0, self, edi}, 1, args);  // G_RunMissile
        return;
    }
    if (type == 2 && word_at(ent, 0x3cc)) {
        call_at(T, 0x002745ec, own_address(&G_RunThink_entry), TitleRegs{word_at(ent, 0x3cc), ebx, 0, 0, self, edi}, 1, args);  // G_RunThink
        return;
    }
    if (type == 2 || ent[0x294]) {
        call_at(T, 0x00274603, own_address(&G_RunItem_entry), TitleRegs{self, ebx, ent[0x294], 0, self, edi}, 1, args);  // G_RunItem
        return;
    }
    if (type == 4 || type == 7 || (type == 0xb && (word_at(ent, 0x2a4) & 0x4000000))) {
        call_at(T, 0x00274652, own_address(&G_RunMover_entry), TitleRegs{self, ebx, 0, 0, self, edi}, 1, args);  // G_RunMover
        return;
    }
    if (type == 0 && (word_at(ent, 0x2a4) & 0x20000000))
        call_at(T, 0x00274635, own_address(&G_FloatOnWater_entry), TitleRegs{type, ebx, 0, 0, self, edi}, 0, nullptr);  // G_FloatOnWater
    if (word_at(ent, 0x23c)) return;
    call_at(T, 0x00274645, own_address(&G_RunThink_entry), TitleRegs{0, ebx, 0, 0, self, edi}, 1, args);  // G_RunThink
}

// As ENT_EAX_FRAME_ENTRY, with the entry ECX as a last parameter.
#define ENT_FULL_FRAME_ENTRY(name)                                                                 \
    PLATFORM_ASM_ENTRY(name##_entry, "movl %esp, %edx\n" GAME_GAP_ESP("0x30000") "pushl %ecx\n"     \
                                     "pushl %ebp\n pushl %edi\n pushl %esi\n pushl %ebx\n"          \
                                     "pushl %eax\n pushl %edx\n" PLATFORM_ASM_CALL(name)            \
                                     "addl $28, %esp\n popl %esp\n ret")

ENT_EAX_FRAME_ENTRY(Turret_UpdatePlayerFire)

void XB_CDECL Turret_UpdatePlayerFire(uint32_t entry, uint8_t* unused_eax, uint32_t ebx, uint32_t esi, uint32_t edi,
                                      uint32_t ebp) {
    // EBX turret; cdecl player on it: view clamp, player placed behind, client +0x464 = 1. The
    // fire delay (info +4, short) runs down by the frame (0x008dda9c); at 0, with fire held
    // (client +0x594 bit 0) and not overheated (+0x7c), a shot adds heat (fire time +0x5dc * the
    // def's +0x84c / 1000) and view kick (0x0095f648, * 0.005), restarts the delay and fires.
    // Frame: entry - 0xc (EBP, ESI, EDI saved).
    (void)unused_eax;
    stack_at(entry - 4) = ebp;
    stack_at(entry - 8) = esi;
    stack_at(entry - 0xc) = edi;
    const uint32_t T = entry - 0xc;
    uint8_t* const turret = reinterpret_cast<uint8_t*>(uintptr_t{ebx});
    const uint32_t player = stack_at(entry + 4);
    uint8_t* const info = pointer_at(turret, 0x24c);
    const uint32_t info_word = address_of(info);
    {
        const uint32_t args[1] = {player};
        call_at(T, 0x00271b75, own_address(&Turret_ClampPlayerViewAngles), TitleRegs{ebx, ebx, 0, 0, esi, info_word}, 1,
                args);  // Turret_ClampPlayerViewAngles
    }
    call_at(T, 0x00271b81, own_address(&Turret_PlacePlayerBehind_entry), TitleRegs{player, ebx, 0, 0, ebx, info_word}, 0, nullptr);  // Turret_PlacePlayerBehind
    const uint8_t* const def = weapon_def(turret[3]);
    uint8_t* const p = reinterpret_cast<uint8_t*>(uintptr_t{player});
    word_at(pointer_at(p, 0x23c), 0x464) = 1;
    int16_t& delay = *reinterpret_cast<int16_t*>(info + 4);
    delay = static_cast<int16_t>(delay - title_global<int16_t>(0x008dda9c));
    if (delay > 0) return;
    delay = 0;
    if (!(pointer_at(p, 0x23c)[0x594] & 1)) return;
    if (info[0x7c]) return;
    const int32_t fire_time = *reinterpret_cast<const int32_t*>(def + 0x5dc);
    const float heat_rate = *reinterpret_cast<const float*>(def + 0x84c);
    field<float>(info, 0x78) = (static_cast<float>(fire_time) * heat_rate) * title_global<float>(0x0083eb4c) + field<float>(info, 0x78);
    title_global<float>(0x0095f648) =
        (static_cast<float>(*reinterpret_cast<const int32_t*>(def + 0x5dc)) * *reinterpret_cast<const float*>(def + 0x84c)) *
            title_global<float>(0x0083eb88) +
        title_global<float>(0x0095f648);
    delay = *reinterpret_cast<const int16_t*>(def + 0x5dc);
    call_at(T, 0x00271c2c, own_address(&Turret_FireForUser_entry), TitleRegs{ebx, ebx, 0, 0, player, info_word}, 0, nullptr);  // Turret_FireForUser
}

ENT_FULL_FRAME_ENTRY(G_SpawnFinished)

void XB_CDECL G_SpawnFinished(uint32_t entry, uint32_t eax, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp,
                              uint32_t ecx) {
    // The level's entities are spawned: actors finish spawning (0x00986018 set meanwhile), entity
    // paths disconnect, spawning ends; the player's view hands model (client +0x98) registered
    // when missing; 0x00985414 = level time and the registered items saved when they changed.
    // Frame: entry - 4 (the model handle, the pushed ECX).
    (void)eax;
    (void)ebp;
    stack_at(entry - 4) = ecx;
    const uint32_t T = entry - 4;
    title_global<uint32_t>(0x00986018) = 1;
    call_at(T, 0x00272770, own_address(&Actor_FinishSpawnAll_entry), TitleRegs{0, ebx, ecx, 0, esi, edi}, 0, nullptr);  // Actor_FinishSpawnAll
    call_at(T, 0x00272775, own_address(&Path_DisconnectAllEntities_entry), TitleRegs{0, ebx, ecx, 0, esi, edi}, 0, nullptr);  // Path_DisconnectAllEntities
    title_global<uint32_t>(0x00985fe0) = 0;  // level_spawning
    uint8_t* const client = pointer_at(pointer_at(title_global<uint8_t*>(0x0095f534), 4), 0x23c);
    if (!word_at(client, 0x98)) {
        const uint32_t args[3] = {title_global<uint32_t>(0x0095f6ac), T, word_at(title_global<uint8_t*>(0x0095f638), 0x34)};
        call_at(T, 0x002727b5, own_address(&R_RegisterModel), TitleRegs{0x0083b300, ebx, args[2], T, esi, edi}, 3,
                args);  // R_RegisterModel "xmodel/viewmodel_hands_us"
        word_at(pointer_at(pointer_at(title_global<uint8_t*>(0x0095f534), 4), 0x23c), 0x98) = stack_at(T);
    }
    const uint32_t changed = title_global<uint32_t>(0x00986004);
    title_global<uint32_t>(0x00986018) = 0;
    title_global<uint32_t>(0x00985414) = title_global<uint32_t>(0x0098540c);
    if (changed) call_at(T, 0x002727f1, own_address(&SaveRegisteredItems), TitleRegs{changed, ebx, 0, 0, esi, edi}, 0, nullptr);  // SaveRegisteredItems
}

ENT_EAX_FRAME_ENTRY(ClientBegin)

void XB_CDECL ClientBegin(uint32_t entry, uint8_t* unused_eax, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // cdecl handle: Q3's ClientBegin - the client's state (+0x520) becomes 2 and it spawns at the
    // initial spawn point. Frame: entry - 0x24 (angles +0xc, origin +0x18).
    (void)unused_eax;
    (void)ebp;
    const uint32_t L = entry - 0x24;
    stack_at(L + 8) = ebx;
    stack_at(L + 4) = esi;
    stack_at(L) = edi;
    const uint32_t handle = stack_at(entry + 4);
    const uint32_t index = handle & 0xfff;
    const uint32_t ent = entity_of_handle(handle);
    uint8_t* const client = pointer_at(reinterpret_cast<uint8_t*>(uintptr_t{ent}), 0x23c);
    word_at(client, 0x520) = 2;
    const uint32_t angles = L + 0xc, origin = L + 0x18;
    {
        const uint32_t args[1] = {angles};
        call_at(L, 0x00272ee1, own_address(&SelectInitialSpawnPoint), TitleRegs{origin, ebx, handle >> 12, address_of(client), index, handle}, 1,
                args);  // SelectInitialSpawnPoint
    }
    const uint32_t again = entity_of_handle(handle);
    call_at(L, 0x00272f10, own_address(&ClientSpawn_entry), TitleRegs{angles, again, origin, 0, index, handle >> 12}, 0, nullptr);  // ClientSpawn
}
ENT_EAX_FRAME_ENTRY(ClientEvents)

void XB_CDECL ClientEvents(uint32_t entry, uint32_t old_sequence, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // EAX old event sequence; cdecl player. Q3's ClientEvents over the client's last (at most 4)
    // events (+0xc4 sequence, +0xc8 events, +0xd8 parms): falling damage (0x74..0x8a, players
    // only: parm / 100 of +0x108, 1.1 from 100; the event ends the scan for others; +0x634 =
    // pain time), weapon fire (0xa3, 0xa4, 0xa6, 0xad-0xaf), melee (0xaa), suicide (0xc6, killed by
    // itself, means 0x15); other events 1..0x2e and 0x46..0x73 alert the enemy team's actors
    // (team -> mask by {0, 2, 1, 0, 0}) with point event 3 (0x18..0x2e) or 2 at the player.
    // Frame: aligned base - 0x70 (index +0xc, points +0x30 / +0x40, team table +0x5c).
    stack_at(entry - 4) = ebp;
    const uint32_t L = ((entry - 4) & ~15u) - 0x70;
    stack_at(L + 8) = ebx;
    stack_at(L + 4) = esi;
    stack_at(L) = edi;
    uint8_t* const ent = reinterpret_cast<uint8_t*>(uintptr_t{stack_at(entry + 4)});
    const uint32_t self = address_of(ent);
    uint8_t* const client = pointer_at(ent, 0x23c);
    const uint32_t client_word = address_of(client);
    int32_t i = static_cast<int32_t>(old_sequence);
    const int32_t sequence = field<int32_t>(client, 0xc4);
    if (i < sequence - 4) i = sequence - 4;
    stack_at(L + 0xc) = static_cast<uint32_t>(i);
    if (i >= sequence) return;
    uint32_t mask_reg = esi;
    for (;;) {
        const uint32_t slot = static_cast<uint32_t>(i) & 3;
        const int32_t event = field<int32_t>(client, 0xc8 + slot * 4);
        const int32_t parm = field<int32_t>(client, 0xd8 + slot * 4);
        if (event >= 0x74 && event < 0x8b) {
            if (ent[0] != 1) return;
            float scale;
            bool hurt = true;
            if (parm >= 100) {
                scale = title_global<float>(0x0083f278);
            } else {
                scale = static_cast<float>(parm) * title_global<float>(0x0083eb94);
                if (scale == title_global<float>(0x0083eafc)) hurt = false;
            }
            if (hurt) {
                const float base = static_cast<float>(field<int32_t>(client, 0x108));
                const int32_t time = title_global<int32_t>(0x0098540c);
                const int32_t damage = static_cast<int32_t>(base * scale);
                word_at(pointer_at(ent, 0x23c), 0x634) = static_cast<uint32_t>(time + 200);
                const uint32_t args[9] = {self, 0, 0, 0, static_cast<uint32_t>(damage), 0, 0x14, 0, 0xffffffffu};
                call_at(L, 0x00271657, own_address(&G_Damage_entry), TitleRegs{0, self, word_at(ent, 0x23c), static_cast<uint32_t>(damage),
                                                                mask_reg, client_word},
                        9, args);  // G_Damage
            }
        } else {
            const uint32_t kind = static_cast<uint32_t>(event - 0x8b);
            const uint8_t which = kind <= 0x3b ? title_global<uint8_t>(0x00271830 + kind) : 4;
            if (which == 1) {
                call_at(L, 0x0027167f, own_address(&FireWeapon_entry), TitleRegs{self, self, 0, static_cast<uint32_t>(event), mask_reg, client_word}, 0,
                        nullptr);  // FireWeapon
            } else if (which == 2) {
                call_at(L, 0x00271689, own_address(&G_PlayerMelee_entry), TitleRegs{2, self, 0, static_cast<uint32_t>(event), mask_reg, client_word}, 0,
                        nullptr);  // G_PlayerMelee
            } else if (which == 3) {
                uint8_t* const c = pointer_at(ent, 0x23c);
                if (c && !(ent[0x2a4] & 1)) {
                    word_at(ent, 0x350) = 0;
                    word_at(c, 0x100) = 0;
                    const uint32_t args[9] = {self, self, self, 100000, 0x15, ent[3], 0, 0, 0};
                    call_at(L, 0x002716ce, own_address(&player_die), TitleRegs{address_of(c), self, ent[3], static_cast<uint32_t>(event),
                                                                    mask_reg, client_word},
                            9, args);  // player_die
                }
            } else if (which == 4) {
                uint8_t* const c = pointer_at(ent, 0x23c);
                const bool alerting = (event >= 1 && event < 0x2f) || (event >= 0x46 && event < 0x8b);
                uint8_t* const sentient = pointer_at(ent, 0x244);
                if (!(c[0x2c] & 2) && !(c[0x528] & 0x10) && alerting && sentient) {
                    const uint32_t team = word_at(sentient, 4);
                    mask_reg = 0;
                    stack_at(L + 0x5c) = 0;
                    stack_at(L + 0x60) = 2;
                    stack_at(L + 0x64) = 1;
                    stack_at(L + 0x68) = 0;
                    stack_at(L + 0x6c) = 0;
                    const uint32_t bit = stack_at(L + 0x5c + team * 4);
                    if (bit) {
                        mask_reg = 1u << (bit & 31);
                        uint8_t* const who = pointer_at(sentient, 0);
                        const uint32_t x = word_at(who, 0x140), y = word_at(who, 0x144), z = word_at(who, 0x148);
                        const bool loud = event >= 0x18 && event < 0x2f;
                        const uint32_t temp = loud ? L + 0x10 : L + 0x20;
                        const uint32_t point = loud ? L + 0x30 : L + 0x40;
                        stack_at(temp) = x;
                        stack_at(temp + 4) = y;
                        stack_at(temp + 8) = z;
                        stack_at(temp + 0xc) = x;
                        for (uint32_t k = 0; k < 4; ++k) stack_at(point + k * 4) = stack_at(temp + k * 4);
                        const uint32_t args[4] = {self, loud ? 3u : 2u, point, 0};
                        call_at(L, 0x002717fc, own_address(&Actor_SendPointEvent_entry), TitleRegs{mask_reg, self, address_of(who), point, mask_reg, client_word}, 4,
                                args);  // Actor_SendPointEvent
                    }
                }
            }
        }
        i = static_cast<int32_t>(stack_at(L + 0xc)) + 1;
        stack_at(L + 0xc) = static_cast<uint32_t>(i);
        if (i >= field<int32_t>(client, 0xc4)) return;
    }
}

ENT_EAX_FRAME_ENTRY(G_vmMain)

uint32_t XB_CDECL G_vmMain(uint32_t entry, uint8_t* unused_eax, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // cdecl (command, arguments...): Q3's vmMain for the game module. 0 init (its result), 1 shutdown,
    // 2 connect (its result), 3 begin, 4 disconnect, 5 command, 6 think, 7 spawning finished, 8
    // reload, 10 the frame's clock (previous time, time += msec, frame number), 12 console command
    // (its result), 17 0x00985414, 18 a DObj skeleton for an entity handle (written over the
    // command slot), 19 level time, 21 registered items sync; 11 and 13 return 0, others -1.
    // Frame: entry - 4 (ESI saved).
    (void)unused_eax;
    (void)ebp;
    stack_at(entry - 4) = esi;
    const uint32_t T = entry - 4;
    const uint32_t command = stack_at(entry + 4), a1 = stack_at(entry + 8);
    const uint32_t a2 = stack_at(entry + 0xc), a3 = stack_at(entry + 0x10);
    const uint32_t one[1] = {a1};
    auto call1 = [&](uint32_t ret, uint32_t function, uint32_t eax, uint32_t ecx, uint32_t edx) {
        return call_at(T, ret, function, TitleRegs{eax, ebx, ecx, edx, 0, edi}, 1, one);
    };
    switch (command) {
    case 0: {
        const uint32_t args[3] = {a1, a2, a3};
        return call_at(T, 0x00273d9b, own_address(&G_InitGame_entry), TitleRegs{a3, ebx, a2, a1, 0, edi}, 3, args);  // G_InitGame
    }
    case 1: call1(0x00273dac, own_address(&G_ShutdownGame), a1, 0, 0); return 0;            // G_ShutdownGame
    case 2: return call1(0x00273dbd, own_address(&ClientConnect), command, a1, 0);         // ClientConnect
    case 3: call1(0x00273df0, own_address(&ClientBegin_entry), command, a1, 0); return 0;      // ClientBegin
    case 4: call1(0x00273ddf, own_address(&ClientDisconnect), a1, 0, 0); return 0;            // ClientDisconnect
    case 5: call1(0x00273e01, own_address(&ClientCommand), command, 0, a1); return 0;      // ClientCommand
    case 6: call1(0x00273dce, own_address(&ClientThink_entry), command, 0, a1); return 0;      // ClientThink
    case 7:
        call_at(T, 0x00273e0d, own_address(&G_SpawnFinished_entry), TitleRegs{command, ebx, 0, 0, 0, edi}, 0, nullptr);  // G_SpawnFinished
        return 0;
    case 8: call1(0x00273e1b, own_address(&G_ReloadLevel_entry), a1, 0, 0); return 0;            // G_ReloadLevel
    case 10: {
        const uint32_t time = title_global<uint32_t>(0x0098540c);
        const uint32_t frame = title_global<uint32_t>(0x00985408) + 1;
        title_global<uint32_t>(0x00985410) = time;  // level_previousTime
        title_global<uint32_t>(0x0098540c) = time + a1;
        title_global<uint32_t>(0x00985408) = frame;  // level_framenum
        return 0;
    }
    case 11:
    case 13: return 0;
    case 12:
        return call_at(T, 0x00273e4d, own_address(&ConsoleCommand), TitleRegs{command, ebx, 0, 0, 0, edi}, 0, nullptr);  // ConsoleCommand
    case 17: return title_global<uint32_t>(0x00985414);
    case 18: {
        stack_at(T - 4) = edi;
        stack_at(entry + 4) = a1;
        const uint32_t ent =
            call_at(T - 4, 0x00273e6d, own_address(&G_EntHandleToEnt), TitleRegs{entry + 4, ebx, a1, 0, 0, edi}, 0, nullptr);  // G_EntHandleToEnt
        call_at(T - 4, 0x00273e74, own_address(&SV_DObjEnsureSkeleton_entry), TitleRegs{ent, ebx, a1, 0, 0, ent}, 0, nullptr);  // SV_DObjEnsureSkeleton
        return 0;
    }
    case 19: return title_global<uint32_t>(0x0098540c);
    case 21:
        call_at(T, 0x00273e88, own_address(&G_SyncRegisteredItems), TitleRegs{command, ebx, 0, 0, 0, edi}, 0, nullptr);  // G_SyncRegisteredItems
        return 0;
    default: return 0xffffffffu;
    }
}

namespace {

// ScriptMover_SetupMove (0x00301240: EAX trajectory, ESI current value, EBX target, 9 stack words) for
// one trajectory of the mover, then SV_LinkEntity.
void script_mover_setup_move(uint8_t* ent, uint32_t trajectory, uint32_t current, const uint32_t (&fields)[6],
                             uint32_t target, uint32_t time, uint32_t accel, uint32_t decel) {
    TitleRegs regs{};
    regs.eax = address_of(ent + trajectory);
    regs.ebx = target;
    regs.ecx = accel;
    regs.edx = time;
    regs.esi = address_of(ent + current);
    regs.edi = address_of(ent);
    const uint32_t args[9] = {time, accel, decel, address_of(ent + fields[0]), address_of(ent + fields[1]),
                              address_of(ent + fields[2]), address_of(ent + fields[3]), address_of(ent + fields[4]),
                              address_of(ent + fields[5])};
    cg_call_regs(own_address(&ScriptMover_SetupMove_entry), &regs, args, 9);  // ScriptMover_SetupMove
    SV_LinkEntity(ent);
}

}  // namespace

void XB_REGS1 XB_CDECL ScriptMover_RotateVelocity(uint8_t* ent, uint32_t velocity, uint32_t time, uint32_t accel,
                                                  uint32_t decel) {
    // ScriptMover_SetupRotateVelocity (0x00300f20: ESI angles trajectory +0x38, EBX velocity, 10 stack words).
    TitleRegs regs{};
    regs.ebx = velocity;
    regs.ecx = decel;
    regs.edx = accel;
    regs.esi = address_of(ent + 0x38);
    regs.edi = address_of(ent);
    const uint32_t args[10] = {time, accel, decel, address_of(ent + 0x150), address_of(ent + 0x300),
                               address_of(ent + 0x2f4), address_of(ent + 0x388), address_of(ent + 0x310),
                               address_of(ent + 0x390), address_of(ent + 0x3a0)};
    regs.eax = time;  // EAX/ECX/EDX = the last three words loaded
    cg_call_regs(own_address(&ScriptMover_SetupRotateVelocity_entry), &regs, args, 10);  // ScriptMover_SetupRotateVelocity
    SV_LinkEntity(ent);
}

void XB_REGS1 XB_CDECL ScriptMover_RotateTo(uint8_t* ent, uint32_t target, uint32_t time, uint32_t accel,
                                            uint32_t decel) {
    // The angles trajectory (+0x38) from the current angles (+0x150).
    static const uint32_t fields[6] = {0x300, 0x2f4, 0x388, 0x310, 0x390, 0x3a0};
    script_mover_setup_move(ent, 0x38, 0x150, fields, target, time, accel, decel);
}

PLATFORM_ASM_ENTRY(ScriptMover_MoveGravity_entry,
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(ScriptMover_MoveGravity) "addl $12, %esp\n"
                   "ret")

void XB_CDECL ScriptMover_MoveGravity(const uint32_t* velocity, uint8_t* ent, float time) {
    // A gravity trajectory (type 5) for the origin (+0x14) from the current origin (+0x140) at level_time, lasting
    // time * 1000 ms; the origin evaluated at once (BG_EvaluateTrajectory), then SV_LinkEntity.
    const float scaled = time * title_global<float>(0x0083eb9c);  // 1000
    uint8_t* const tr = ent + 0x14;
    word_at(ent, 0x18) = title_global<uint32_t>(0x0098540c);  // level_time
    word_at(tr, 0x8) = static_cast<uint32_t>(static_cast<int32_t>(scaled));
    word_at(tr, 0xc) = word_at(ent, 0x140);
    word_at(tr, 0x10) = word_at(ent, 0x144);
    word_at(tr, 0x14) = word_at(ent, 0x148);
    word_at(tr, 0x18) = velocity[0];
    word_at(tr, 0x1c) = velocity[1];
    word_at(tr, 0x20) = velocity[2];
    word_at(tr, 0) = 5;
    TitleRegs regs{};
    regs.eax = title_global<uint32_t>(0x0098540c);
    regs.ecx = address_of(ent + 0x140);
    regs.esi = address_of(ent);
    regs.edi = address_of(tr);
    cg_call_regs(own_address(&BG_EvaluateTrajectory_entry), &regs, nullptr, 0);  // BG_EvaluateTrajectory
    SV_LinkEntity(ent);
}

void XB_REGS1 XB_CDECL ScriptMover_MoveTo(uint8_t* ent, uint32_t target, uint32_t time, uint32_t accel,
                                          uint32_t decel) {
    // The origin trajectory (+0x14) from the current origin (+0x140).
    static const uint32_t fields[6] = {0x2fc, 0x384, 0x38c, 0x2c0, 0x2d0, 0x2e0};
    script_mover_setup_move(ent, 0x14, 0x140, fields, target, time, accel, decel);
}

namespace {

float& tr_float(uint8_t* tr, uint32_t offset) { return *reinterpret_cast<float*>(tr + offset); }

}  // namespace

// EAX goal, ECX start, EDX end, XMM1 time; cdecl trajectory, speed, second time.
PLATFORM_ASM_ENTRY(ScriptMover_NextTrajectoryPhase_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "subl $4, %esp\n"
                   "movss %xmm1, (%esp)\n"
                   "pushl %edx\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(ScriptMover_NextTrajectoryPhase) "addl $28, %esp\n"
                   "ret")

uint32_t XB_CDECL ScriptMover_NextTrajectoryPhase(const float* goal, const float* start, const float* end, float time,
                                                  uint8_t* tr, float speed, float time2) {
    const int32_t type = static_cast<int32_t>(word_at(tr, 0));
    const float zero = 0.0f;
    if (type == 9) {
        if (time > zero) {
            // Linear (3) from start to goal over time * 1000 ms.
            const float thousand = title_global<float>(0x0083eb9c);
            word_at(tr, 4) = title_global<uint32_t>(0x0098540c);  // level_time
            const int32_t duration = static_cast<int32_t>(time * thousand);
            word_at(tr, 8) = static_cast<uint32_t>(duration);
            word_at(tr, 0xc) = reinterpret_cast<const uint32_t*>(start)[0];
            word_at(tr, 0x10) = reinterpret_cast<const uint32_t*>(start)[1];
            word_at(tr, 0x14) = reinterpret_cast<const uint32_t*>(start)[2];
            const float dx = goal[0] - start[0];
            const float dy = goal[1] - start[1];
            const float dz = goal[2] - start[2];
            const float scale = thousand / static_cast<float>(duration);
            const float vx = scale * dx;
            const float vy = scale * dy;
            const float vz = scale * dz;
            tr_float(tr, 0x18) = vx;
            tr_float(tr, 0x20) = vz;
            word_at(tr, 0) = 3;
            tr_float(tr, 0x1c) = vy;
            return 0;
        }
        if (!(zero >= time)) goto settle;  // NaN
    } else if (type != 3) {
        goto settle;
    }
    if (time2 > zero) {
        // Toward end at speed (type 10) for time2 * 1000 ms from the goal.
        const float scaled = time2 * title_global<float>(0x0083eb9c);
        word_at(tr, 4) = title_global<uint32_t>(0x0098540c);
        word_at(tr, 8) = static_cast<uint32_t>(static_cast<int32_t>(scaled));
        word_at(tr, 0xc) = reinterpret_cast<const uint32_t*>(goal)[0];
        word_at(tr, 0x10) = reinterpret_cast<const uint32_t*>(goal)[1];
        word_at(tr, 0x14) = reinterpret_cast<const uint32_t*>(goal)[2];
        alignas(16) float dir[4] = {};
        dir[0] = end[0] - goal[0];
        dir[1] = end[1] - goal[1];
        dir[2] = end[2] - goal[2];
        VectorNormalize(dir);
        const float vx = dir[0] * speed;
        const float vy = dir[1] * speed;
        const float vz = dir[2] * speed;
        tr_float(tr, 0x18) = vx;
        tr_float(tr, 0x20) = vz;
        word_at(tr, 0) = 10;
        tr_float(tr, 0x1c) = vy;
        return 0;
    }
settle:
    // Stopped (type 0) at level_time: a gravity trajectory where it is now, else at end.
    if (type == 5) {
        alignas(16) uint32_t now[4] = {};
        now[0] = word_at(tr, 0xc);
        now[1] = word_at(tr, 0x10);
        now[2] = word_at(tr, 0x14);
        TitleRegs regs{};
        regs.eax = title_global<uint32_t>(0x0098540c);
        regs.ecx = address_of(now);
        regs.edi = address_of(tr);
        cg_call_regs(own_address(&BG_EvaluateTrajectory_entry), &regs, nullptr, 0);  // BG_EvaluateTrajectory
        word_at(tr, 0xc) = now[0];
        word_at(tr, 0x10) = now[1];
        word_at(tr, 0x14) = now[2];
    } else {
        word_at(tr, 0xc) = reinterpret_cast<const uint32_t*>(end)[0];
        word_at(tr, 0x10) = reinterpret_cast<const uint32_t*>(end)[1];
        word_at(tr, 0x14) = reinterpret_cast<const uint32_t*>(end)[2];
    }
    word_at(tr, 4) = title_global<uint32_t>(0x0098540c);
    word_at(tr, 0) = 0;
    return 1;
}

namespace {

// BG_EvaluateTrajectory (0x002d62c0: time in EAX, out in ECX, trajectory in EDI) by its title address.
void evaluate_trajectory(const uint8_t* tr, int32_t time, float* out) {
    const TitleRegs regs{static_cast<uint32_t>(time), 0, address_of(out), 0, 0, address_of(tr)};
    g_ent_call(own_address(&BG_EvaluateTrajectory_entry), &regs, 0, nullptr);  // BG_EvaluateTrajectory
}

// A trajectory's start (+4 level_time), duration (+8 = seconds * 1000 truncated), base (+0xc), delta (+0x18)
// and type (+0).
void set_phase(uint8_t* tr, float seconds, const float* base, const float* delta, uint32_t type) {
    *reinterpret_cast<int32_t*>(tr + 4) = title_global<int32_t>(0x0098540c);
    *reinterpret_cast<int32_t*>(tr + 8) = static_cast<int32_t>(seconds * title_global<float>(0x0083eb9c));
    for (uint32_t i = 0; i < 3; ++i) {
        reinterpret_cast<uint32_t*>(tr + 0xc)[i] = reinterpret_cast<const uint32_t*>(base)[i];
    }
    for (uint32_t i = 0; i < 3; ++i) {
        reinterpret_cast<uint32_t*>(tr + 0x18)[i] = reinterpret_cast<const uint32_t*>(delta)[i];
    }
    *reinterpret_cast<uint32_t*>(tr) = type;
}

void copy3(float* dst, const float* src) {
    for (uint32_t i = 0; i < 3; ++i) reinterpret_cast<uint32_t*>(dst)[i] = reinterpret_cast<const uint32_t*>(src)[i];
}

// The title CRT's __CIsqrt of a float, stored as float.
float title_ci_sqrt(float x) {
    float result;
    __asm__ volatile("flds %1\n\tcall *%2\n\tfstps %0"
                     : "=m"(result)
                     : "m"(x), "r"(uintptr_t{0x0033acf4})
                     : "eax", "ecx", "edx", "st", "memory");
    return result;
}

}  // namespace

PLATFORM_ASM_ENTRY(ScriptMover_SetupRotateVelocity_entry,
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl 40(%esp)\n"
                   "pushl %esi\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(ScriptMover_SetupRotateVelocity) "addl $48, %esp\n"
                   "ret")

void XB_CDECL ScriptMover_SetupRotateVelocity(const float* velocity, uint8_t* tr, float time, float accel, float decel,
                                              float* current, float* speed, float* mid_time, float* decel_time,
                                              float* accel_end, float* mid_end, float* end) {
    // The current value from the running trajectory first (type set). No accel/decel: one linear phase
    // (type 3) for `time` s. Else the speed (x87 length of the velocity), the linear time (time - accel
    // - decel) and three phases: accelerate (type 9) to accel_end, linear to mid_end, decelerate
    // (type 10) to end; the trajectory holds the first phase that lasts, the current value re-evaluated.
    const int32_t now = title_global<int32_t>(0x0098540c);
    if (*reinterpret_cast<const uint32_t*>(tr) != 0) evaluate_trajectory(tr, now, current);
    if (accel == 0.0f && decel == 0.0f) {
        *reinterpret_cast<int32_t*>(tr + 4) = title_global<int32_t>(0x0098540c);
        *reinterpret_cast<int32_t*>(tr + 8) = static_cast<int32_t>(time * title_global<float>(0x0083eb9c));
        *mid_time = time;
        *decel_time = 0.0f;
        for (uint32_t i = 0; i < 3; ++i) {
            reinterpret_cast<uint32_t*>(tr + 0xc)[i] = reinterpret_cast<const uint32_t*>(current)[i];
        }
        for (uint32_t i = 0; i < 3; ++i) {
            reinterpret_cast<uint32_t*>(tr + 0x18)[i] = reinterpret_cast<const uint32_t*>(velocity)[i];
        }
        *reinterpret_cast<uint32_t*>(tr) = 3;
        evaluate_trajectory(tr, title_global<int32_t>(0x0098540c), current);
        evaluate_trajectory(tr, *reinterpret_cast<const int32_t*>(tr + 8) + title_global<int32_t>(0x0098540c), end);
        return;
    }
    *mid_time = (time - accel) - decel;
    *decel_time = decel;
    *speed = platform_x87_norm3(velocity[0], velocity[1], velocity[2]);
    if (accel != 0.0f) {
        set_phase(tr, accel, current, velocity, 9);
        evaluate_trajectory(tr, *reinterpret_cast<const int32_t*>(tr + 8) + title_global<int32_t>(0x0098540c),
                            accel_end);
    } else {
        copy3(accel_end, current);
        if (*mid_time != 0.0f) {
            set_phase(tr, *mid_time, current, velocity, 3);
        } else {
            set_phase(tr, *decel_time, current, velocity, 10);
        }
    }
    mid_end[0] = *mid_time * velocity[0] + accel_end[0];
    mid_end[1] = velocity[1] * *mid_time + accel_end[1];
    mid_end[2] = *mid_time * velocity[2] + accel_end[2];
    if (*decel_time != 0.0f) {
        // The deceleration phase in a stack trajectory from mid_end.
        uint8_t phase[0x24];
        set_phase(phase, *decel_time, mid_end, velocity, 10);
        evaluate_trajectory(phase, *reinterpret_cast<const int32_t*>(phase + 8) + *reinterpret_cast<const int32_t*>(phase + 4),
                            end);
    } else {
        copy3(end, mid_end);
    }
    evaluate_trajectory(tr, title_global<int32_t>(0x0098540c), current);
}

PLATFORM_ASM_ENTRY(ScriptMover_SetupMove_entry,
                   "pushl 36(%esp)\n"
                   "pushl 36(%esp)\n"
                   "pushl 36(%esp)\n"
                   "pushl 36(%esp)\n"
                   "pushl 36(%esp)\n"
                   "pushl 36(%esp)\n"
                   "pushl 36(%esp)\n"
                   "pushl 36(%esp)\n"
                   "pushl 36(%esp)\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(ScriptMover_SetupMove) "addl $48, %esp\n"
                   "ret")

void XB_CDECL ScriptMover_SetupMove(uint8_t* tr, const float* target, float* current, float time, float accel,
                                    float decel, float* speed, float* mid_time, float* decel_time, float* accel_end,
                                    float* mid_end, float* end) {
    // The move from the current value (re-evaluated first when the trajectory runs) to target in `time`
    // s. No accel/decel: one linear phase (type 3, delta = move / duration ms * 1000). Else the speed
    // 2 * length / (2 * time - accel - decel) along the move's direction and three phases: accelerate
    // (type 9) to accel_end, linear (delta scaled to the phase's whole ms) to mid_end, decelerate
    // (type 10); end = target; the trajectory holds the first phase that lasts.
    const float d0 = target[0] - current[0];
    const float d1 = target[1] - current[1];
    const float d2 = target[2] - current[2];
    if (*reinterpret_cast<const uint32_t*>(tr) != 0) {
        evaluate_trajectory(tr, title_global<int32_t>(0x0098540c), current);
    }
    const float thousand = title_global<float>(0x0083eb9c);
    if (accel == 0.0f && decel == 0.0f) {
        *reinterpret_cast<int32_t*>(tr + 4) = title_global<int32_t>(0x0098540c);
        *reinterpret_cast<int32_t*>(tr + 8) = static_cast<int32_t>(time * thousand);
        *mid_time = time;
        *decel_time = 0.0f;
        copy3(end, target);
        for (uint32_t i = 0; i < 3; ++i) {
            reinterpret_cast<uint32_t*>(tr + 0xc)[i] = reinterpret_cast<const uint32_t*>(current)[i];
        }
        const float scale = thousand / static_cast<float>(*reinterpret_cast<const int32_t*>(tr + 8));
        float* const delta = reinterpret_cast<float*>(tr + 0x18);
        delta[0] = scale * d0;
        delta[1] = scale * d1;
        delta[2] = scale * d2;
        *reinterpret_cast<uint32_t*>(tr) = 3;
        evaluate_trajectory(tr, title_global<int32_t>(0x0098540c), current);
        return;
    }
    *mid_time = (time - accel) - decel;
    *decel_time = decel;
    const float length_sq = (d2 * d2 + d1 * d1) + d0 * d0;
    const float two = title_global<float>(0x0083eba8);
    const float rate = (title_ci_sqrt(length_sq) * two) / ((time * two - accel) - decel);
    *speed = rate;
    const float length = title_ci_sqrt(length_sq);
    float dir0 = 0.0f, dir1 = 0.0f, dir2 = 0.0f;
    if (length != title_global<float>(0x0083eafc)) {  // FUCOMIP: no MXCSR flags; length is never a signalling NaN
        const float inv = title_global<float>(0x0083eb20) / length;
        dir0 = inv * d0;
        dir1 = inv * d1;
        dir2 = inv * d2;
    }
    float velocity[3];
    velocity[0] = rate * dir0;
    velocity[1] = rate * dir1;
    velocity[2] = rate * dir2;
    if (accel != 0.0f) {
        set_phase(tr, accel, current, velocity, 9);
        evaluate_trajectory(tr, *reinterpret_cast<const int32_t*>(tr + 8) + title_global<int32_t>(0x0098540c),
                            accel_end);
    } else {
        copy3(accel_end, current);
        if (*mid_time != 0.0f) {
            *reinterpret_cast<int32_t*>(tr + 4) = title_global<int32_t>(0x0098540c);
            *reinterpret_cast<int32_t*>(tr + 8) = static_cast<int32_t>(*mid_time * thousand);
            const float ms = static_cast<float>(*reinterpret_cast<const int32_t*>(tr + 8));
            for (uint32_t i = 0; i < 3; ++i) {
                reinterpret_cast<uint32_t*>(tr + 0xc)[i] = reinterpret_cast<const uint32_t*>(current)[i];
            }
            const float m0 = *mid_time, m1 = *mid_time, m2 = *mid_time;
            const float scale = thousand / ms;
            const float p0 = m0 * velocity[0];
            float* const delta = reinterpret_cast<float*>(tr + 0x18);
            const float r0 = scale * p0;
            const float p1 = m1 * velocity[1];
            const float p2 = m2 * velocity[2];
            const float r1 = scale * p1;
            const float r2 = scale * p2;
            delta[0] = r0;
            delta[1] = r1;
            delta[2] = r2;
            *reinterpret_cast<uint32_t*>(tr) = 3;
        } else {
            set_phase(tr, *decel_time, current, velocity, 10);
        }
    }
    mid_end[0] = *mid_time * velocity[0] + accel_end[0];
    mid_end[1] = *mid_time * velocity[1] + accel_end[1];
    mid_end[2] = *mid_time * velocity[2] + accel_end[2];
    copy3(end, target);
    evaluate_trajectory(tr, title_global<int32_t>(0x0098540c), current);
}

void XB_REGS1 XB_CDECL Turret_ClampPlayerViewAngles(uint8_t* turret, uint8_t* player) {
    // The player's view (client +0x9c) relative to the turret's base angles (+0x150), wrapped to
    // [-180, 180] (x87 fmod 360), clamped to the info's arc (min +0x2c, max +0x34 per axis) and to a
    // turn of frame time * 300 from the turret's current angles (+0x90): the turret takes the result
    // (roll 0) and the view follows when anything was clamped. Info flag 0x800 (+2) moves to the
    // entity's +0x10 bit 8; then Turret_UpdateTurnFxAndBones.
    uint8_t* const info = pointer_at(turret, 0x24c);
    field<uint32_t>(pointer_at(player, 0x23c), 0x464) = 1;
    field<uint32_t>(pointer_at(player, 0x23c), 0x468) = field<uint32_t>(turret, 0x224);
    const float max_turn = title_global<float>(0x008ddaa0) * title_global<float>(0x0083ed04);  // * 300
    const float* const view = reinterpret_cast<const float*>(pointer_at(player, 0x23c) + 0x9c);
    float* const current = reinterpret_cast<float*>(turret + 0x90);
    const float* const base = reinterpret_cast<const float*>(turret + 0x150);
    const float* const arc = reinterpret_cast<const float*>(info + 0x2c);
    const long double full_turn = platform_x87_ld(title_global<double>(0x0083ed40));  // 360.0
    const float full = title_global<float>(0x0083ebcc);                                        // 360
    const float half = title_global<float>(0x0083eb60);                                        // 180
    const float minus_half = title_global<float>(0x0083ebd0);                                  // -180
    bool changed = false;
    float out[3];
    for (uint32_t i = 0; i < 2; ++i) {
        float delta = static_cast<float>(
            title_ci_fmod(platform_x87_ld(view[i]) - platform_x87_ld(base[i]), full_turn));
        if (platform_x87_greater(delta, half)) delta = delta - full;
        if (minus_half > delta) delta = delta + full;
        out[i] = delta;
        if (delta > arc[i + 2]) {
            out[i] = arc[i + 2];
            changed = true;
        } else if (arc[i] > delta) {
            out[i] = arc[i];
            changed = true;
        }
        float turn = static_cast<float>(
            title_ci_fmod(platform_x87_ld(out[i]) - platform_x87_ld(current[i]), full_turn));
        if (platform_x87_greater(turn, half)) turn = turn - full;
        if (minus_half > turn) turn = turn + full;
        uint32_t bits;
        std::memcpy(&bits, &turn, sizeof bits);
        bits &= 0x7fffffffu;  // FABS
        float size;
        std::memcpy(&size, &bits, sizeof size);
        if (platform_x87_greater(size, max_turn)) {
            changed = true;
            if (turn > 0.0f) {
                out[i] = max_turn + current[i];
            } else {
                out[i] = current[i] - max_turn;
            }
        }
    }
    const float yaw_pitch0 = out[0];
    const float yaw_pitch1 = out[1];
    current[0] = yaw_pitch0;
    current[1] = yaw_pitch1;
    current[2] = 0.0f;
    uint16_t& flags = field<uint16_t>(info, 2);
    if (flags & 0x800) {
        flags = static_cast<uint16_t>(flags & 0xf7ff);
        field<uint32_t>(turret, 0x10) ^= 8;
    }
    if (changed) {
        out[0] = field<float>(turret, 0x150) + yaw_pitch0;
        out[1] = field<float>(turret, 0x154) + yaw_pitch1;
        out[2] = 0.0f;
        SetClientViewAngle(out, player);
    }
    const TitleRegs regs{0, 0, 0, 1, 0, address_of(turret)};
    g_ent_call(own_address(&Turret_UpdateTurnFxAndBones_entry), &regs, 0, nullptr);  // Turret_UpdateTurnFxAndBones (DL = 1)
}

namespace {

// sqrt((dz*dz + dy*dy) + dx*dx) > limit, d = a - b per axis, all on the x87 (FCOMIP; false when unordered).
bool x87_dist3_greater(float limit, float ax, float bx, float ay, float by, float az, float bz) {
    bool result;
    __asm__("flds %1\n\tfsubs %2\n\tflds %3\n\tfsubs %4\n\tflds %5\n\tfsubs %6\n\t"
            "fld %%st(0)\n\tfmul %%st(1), %%st\n\tfld %%st(2)\n\tfmul %%st(3), %%st\n\tfaddp\n\t"
            "fld %%st(3)\n\tfmul %%st(4), %%st\n\tfaddp\n\tfsqrt\n\t"
            "fstp %%st(3)\n\tfstp %%st(0)\n\tfstp %%st(0)\n\t"
            "flds %7\n\tfxch %%st(1)\n\tfcomip %%st(1), %%st\n\tfstp %%st(0)\n\tseta %0"
            : "=q"(result)
            : "m"(ax), "m"(bx), "m"(ay), "m"(by), "m"(az), "m"(bz), "m"(limit)
            : "st", "st(1)", "st(2)", "st(3)", "st(4)", "cc");
    return result;
}

// (float) a > (double) b on the x87 (FLD/FCOMIP; false when unordered).
bool x87_greater_double(float a, const double& b) {
    bool result;
    __asm__("fldl %2\n\tflds %1\n\tfcomip %%st(1), %%st\n\tfstp %%st(0)\n\tseta %0"
            : "=q"(result)
            : "m"(a), "m"(b)
            : "st", "st(1)", "cc");
    return result;
}

}  // namespace

PLATFORM_ASM_ENTRY(G_BounceMissile_entry,
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(G_BounceMissile) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL G_BounceMissile(const uint8_t* trace, uint8_t* ent) {
    // Q3's G_BounceMissile: the velocity at the hit time (previous time + frame * fraction +0x20)
    // reflected off the trace plane (normal +0x10) into the position delta (+0x2c); a floor
    // (normal z > 0.7) sets the ground entity (+0xc = trace +0x30). Bouncing entities (+0x10 flag
    // 0x2000000) keep half (an eighth in water - contents 0x20 - or on surface flags 0x200c000) and
    // stop on a floor below 20 units/s (origin and angles set; returns 0). Else the base moves off
    // the plane by a tenth of the normal (z not up), the angles bounce (G_BounceMissileAngles), the
    // actors are told (G_GrenadeLandedNotifyActors); returns 1 when out of water and the velocity
    // changed by more than 100.
    float* const origin = reinterpret_cast<float*>(ent + 0x140);
    const uint32_t contents = SV_PointContents(origin, 0, 0x20);
    const int32_t previous = title_global<int32_t>(0x00985410);
    const int32_t hit_time =
        static_cast<int32_t>(static_cast<float>(title_global<int32_t>(0x0098540c) - previous) *
                             *reinterpret_cast<const float*>(trace + 0x20)) +
        previous;
    alignas(16) float velocity[3] = {};
    BG_EvaluateTrajectoryDelta(0, hit_time, velocity, ent + 0x14);
    const float* const normal = reinterpret_cast<const float*>(trace + 0x10);
    const float v0 = velocity[0], v1 = velocity[1], v2 = velocity[2];
    float dot = normal[2] * v2 + normal[1] * v1;
    dot = dot + v0 * normal[0];
    dot = dot * title_global<float>(0x0083ec5c);  // -2
    field<float>(ent, 0x2c) = dot * normal[0] + v0;
    field<float>(ent, 0x30) = normal[1] * dot + v1;
    field<float>(ent, 0x34) = normal[2] * dot + v2;
    const double& floor_z = title_global<double>(0x0083f288);  // 0.7
    if (x87_greater_double(normal[2], floor_z)) word_at(ent, 0xc) = *reinterpret_cast<const uint32_t*>(trace + 0x30);
    if (word_at(ent, 0x10) & 0x2000000) {
        float scale;
        if (contents != 0 || (*reinterpret_cast<const uint32_t*>(trace + 0x28) & 0x200c000) != 0) {
            scale = title_global<float>(0x0083ec68);  // 0.125
        } else {
            scale = title_global<float>(0x0083eb24);  // 0.5
        }
        field<float>(ent, 0x2c) = field<float>(ent, 0x2c) * scale;
        field<float>(ent, 0x30) = field<float>(ent, 0x30) * scale;
        field<float>(ent, 0x34) = field<float>(ent, 0x34) * scale;
        if (x87_greater_double(normal[2], floor_z) &&
            platform_x87_greater_than_dist3(title_global<float>(0x0083eb5c), field<float>(ent, 0x2c), 0.0f,
                                            field<float>(ent, 0x30), 0.0f, field<float>(ent, 0x34), 0.0f)) {
            alignas(16) float angles[3] = {};
            G_SetOrigin(ent, 0, origin);
            {
                const TitleRegs regs{0, 0, 0, 0, address_of(angles), address_of(ent)};
                const uint32_t args[3] = {address_of(ent), address_of(trace), 1};
                g_ent_call(own_address(&G_BounceMissileAngles_entry), &regs, 3, args);  // G_BounceMissileAngles
            }
            G_SetAngle(ent, 0, angles);
            return 0;
        }
    }
    const float tenth = title_global<float>(0x0083ebf8);
    float nz = normal[2] * tenth;
    const float nx = normal[0] * tenth;
    const float ny = normal[1] * tenth;
    if (nz > 0.0f) nz = 0.0f;
    origin[0] = origin[0] + nx;
    origin[1] = origin[1] + ny;
    origin[2] = origin[2] + nz;
    word_at(ent, 0x20) = word_at(ent, 0x140);
    word_at(ent, 0x24) = word_at(ent, 0x144);
    word_at(ent, 0x28) = word_at(ent, 0x148);
    field<int32_t>(ent, 0x18) = title_global<int32_t>(0x0098540c);
    alignas(16) float angles[3] = {};
    {
        const TitleRegs regs{0, 0, 0, 0, address_of(angles), address_of(ent)};
        const uint32_t args[3] = {address_of(ent), address_of(trace), 0};
        g_ent_call(own_address(&G_BounceMissileAngles_entry), &regs, 3, args);  // G_BounceMissileAngles
    }
    word_at(ent, 0x44) = reinterpret_cast<const uint32_t*>(angles)[0];
    word_at(ent, 0x48) = reinterpret_cast<const uint32_t*>(angles)[1];
    word_at(ent, 0x4c) = reinterpret_cast<const uint32_t*>(angles)[2];
    field<int32_t>(ent, 0x3c) = title_global<int32_t>(0x0098540c);
    {
        const uint32_t handle = *reinterpret_cast<const uint32_t*>(trace + 0x30);
        const uint32_t index = handle & 0xfff;
        uint32_t hit = 0;
        if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8)) {
            hit = title_global<uint32_t>(0x0097a968 + index * 8);
        }
        const TitleRegs regs{hit, 0, 0, 0, 0, address_of(ent)};
        const uint32_t args[1] = {address_of(ent)};
        g_ent_call(own_address(&G_GrenadeLandedNotifyActors_entry), &regs, 1, args);  // G_GrenadeLandedNotifyActors
    }
    if (contents != 0) return 0;
    return x87_dist3_greater(title_global<float>(0x0083eb90), field<float>(ent, 0x2c), v0, field<float>(ent, 0x30), v1,
                             field<float>(ent, 0x34), v2)
               ? 1
               : 0;
}

void XB_REGS1 XB_CDECL ClientEndFrame(uint8_t* ent, uint32_t arg) {
    // Q3's ClientEndFrame: turret use (client +0xc0 flags 0x6000: the entity of handle +0x468),
    // "cl_bomberKeyboardMode" following those flags, the player vehicles' thinks (the argument as
    // EBX), the dead type and linked origin (+0x3cc linked: G_ClientUpdateLinkTo), the nearest
    // node, the look-at entity and cursor hints (after 500 ms), damage feedback, the
    // recently-hurt flag (+0x10 0x800, under 1 s), health (+0x100 = +0x350), the entity state,
    // a weapon-type-1 point event while +0x3c runs (< 3000), the compass actor marker from the
    // leaned eye position; +0x4d4 cleared, so the vehicle slot (+0x604) is always 0.
    uint8_t* client = pointer_at(ent, 0x23c);
    if (field<uint32_t>(client, 0xc0) & 0x6000) {
        const uint32_t handle = field<uint32_t>(client, 0x468);
        const uint32_t index = handle & 0xfff;
        uint32_t turret = 0;
        if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8)) {
            turret = title_global<uint32_t>(0x0097a968 + index * 8);
        }
        const TitleRegs regs{turret, 0, index, handle >> 12, 0x6000, address_of(ent)};
        g_ent_call(own_address(&Player_UpdateTurretUse_entry), &regs, 0, nullptr);  // Player_UpdateTurretUse
    }
    const uint32_t keyboard = title_global<uint32_t>(0x00954e0c);
    using CvarSet = uint32_t XB_CDECL(const char*, const char*, uint32_t);
    if (field<uint32_t>(pointer_at(ent, 0x23c), 0xc0) & 0x6000) {
        if (keyboard == 0) {
            own_function<CvarSet>(&Cvar_Set2)(reinterpret_cast<const char*>(uintptr_t{0x00834300}),
                                                 reinterpret_cast<const char*>(uintptr_t{0x0082e058}), 1);  // "1"
        }
    } else if (keyboard != 0) {
        own_function<CvarSet>(&Cvar_Set2)(reinterpret_cast<const char*>(uintptr_t{0x00834300}),
                                             reinterpret_cast<const char*>(uintptr_t{0x0082e098}), 1);  // "0"
    }
    {
        const TitleRegs regs{0, arg, 0, 0, 0x6000, address_of(ent)};
        g_ent_call(own_address(&G_RunPlayerVehicleThinks_entry), &regs, 0, nullptr);  // G_RunPlayerVehicleThinks
    }
    client = pointer_at(ent, 0x23c);
    if (field<uint32_t>(client, 0x584) == 0 && field<uint32_t>(client, 0x588) == 0) {
        if (field<uint32_t>(ent, 0x3cc) != 0) {
            field<uint32_t>(client, 0x24) = field<int32_t>(client, 0x100) <= 0 ? 5 : 1;
            G_ClientUpdateLinkTo(ent);
            field<uint32_t>(pointer_at(ent, 0x23c), 0) = field<uint32_t>(ent, 0x140);
            field<uint32_t>(pointer_at(ent, 0x23c), 4) = field<uint32_t>(ent, 0x144);
            field<uint32_t>(pointer_at(ent, 0x23c), 8) = field<uint32_t>(ent, 0x148);
        } else if (field<uint32_t>(client, 0x24) == 1 || field<uint32_t>(client, 0x24) == 5) {
            field<uint32_t>(client, 0x24) -= 1;
        } else {
            field<float>(client, 0x610) = 0.0f;
            field<float>(pointer_at(ent, 0x23c), 0x60c) = 0.0f;
            field<float>(pointer_at(ent, 0x23c), 0x608) = 0.0f;
        }
    }
    Sentient_UpdateNearestNode(pointer_at(ent, 0x244));
    if (field<uint32_t>(pointer_at(ent, 0x23c), 0x20) != 0 && title_global<int32_t>(0x0098540c) > 500) {
        {
            const TitleRegs regs{0, 0, 0, 0, address_of(ent), address_of(ent)};
            g_ent_call(own_address(&Player_UpdateLookAtEntity_entry), &regs, 0, nullptr);  // Player_UpdateLookAtEntity
        }
        {
            const TitleRegs regs{0, 0, 0, 0, address_of(ent), address_of(ent)};
            const uint32_t args[1] = {address_of(ent)};
            g_ent_call(own_address(&Player_UpdateCursorHints_entry), &regs, 1, args);  // Player_UpdateCursorHints
        }
    }
    P_DamageFeedback(ent);
    client = pointer_at(ent, 0x23c);
    if (title_global<int32_t>(0x0098540c) - field<int32_t>(client, 0x590) > 1000) {
        field<uint32_t>(ent, 0x10) |= 0x800;
    } else {
        field<uint32_t>(ent, 0x10) &= ~0x800u;
    }
    field<uint32_t>(client, 0x100) = field<uint32_t>(ent, 0x350);
    client = pointer_at(ent, 0x23c);
    ent[1] = 0;
    BG_PlayerStateToEntityStateExtraPolate(ent, 0, client, field<uint32_t>(client, 0x20));
    client = pointer_at(ent, 0x23c);
    const uint8_t* const def = weapon_def(field<uint32_t>(client, 0x80));
    if (*reinterpret_cast<const uint32_t*>(def + 0x94) == 1) {
        const int32_t timer = field<int32_t>(client, 0x3c);
        if (timer != 0 && timer < 3000) {
            const TitleRegs regs{0, 0, 0, 0, 0, address_of(ent)};
            const uint32_t args[4] = {address_of(ent), 7, address_of(client), 0};
            g_ent_call(own_address(&Actor_SendPointEvent_entry), &regs, 4, args);  // Actor_SendPointEvent
        }
    }
    uint8_t* const ps = pointer_at(ent, 0x23c);
    float eye[3];
    eye[0] = field<float>(ps, 0);
    eye[1] = field<float>(ps, 4);
    eye[2] = field<float>(ps, 0xac) + field<float>(ps, 8);
    AddLeanToPosition(eye, field<float>(ps, 0x4c), field<float>(ps, 0xa0), 16.0f, 20.0f);
    field<uint32_t>(ps, 0x4d0) = Player_PackActorMarker(eye);
    client = pointer_at(ent, 0x23c);
    const uint32_t marker = field<uint32_t>(client, 0x4d0);
    if (marker != 0) {
        uint8_t* const actor_ent = *reinterpret_cast<uint8_t**>(title_global<uint8_t*>(0x00985384 + (marker & 0x3f) * 4));
        field<uint32_t>(client, 0x600) = field<uint32_t>(actor_ent, 0x224);
    } else {
        field<uint32_t>(client, 0x600) = 0;
    }
    field<uint32_t>(pointer_at(ent, 0x23c), 0x4d4) = 0;
    // The vehicle slot from +0x4d4 (Vehicle_GetTankEntityNum) is never reached: +0x4d4 was just cleared.
    field<uint32_t>(pointer_at(ent, 0x23c), 0x604) = 0;
}

namespace {

// |a - b| with the difference rounded to float on the x87 (FSUB, FSTP, FABS): no MXCSR flags.
float x87_abs_diff(float a, float b) {
    float diff;
    __asm__("flds %1\n\tfsubs %2\n\tfstps %0" : "=m"(diff) : "m"(a), "m"(b) : "st");
    uint32_t bits;
    std::memcpy(&bits, &diff, sizeof bits);
    bits &= 0x7fffffffu;  // FABS
    std::memcpy(&diff, &bits, sizeof diff);
    return diff;
}

}  // namespace

PLATFORM_ASM_ENTRY(Turret_UpdateTurnFxAndBones_entry,
                   "pushl %edi\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(Turret_UpdateTurnFxAndBones) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Turret_UpdateTurnFxAndBones(uint32_t player_turn, uint8_t* turret) {
    // Not for an AI-held turret (DL 0 and the handle +0x1a0's entity an actor (+0x297 == 1) with health).
    // A player's turn (DL set): moving more than 0.1 from the last angles (0x0095f64c/650) starts the
    // def's (+8) turn loop effect 0x22 (state 0x0095f654 = 2; id 0x008d9d50), stopping frees it and
    // plays the stop effect 0x23 once (id 0x008d9d54; state 1, then 0); the angles are remembered.
    // Then the aim bones: "tag_aim" and "tag_aim_animated" get (pitch, yaw, 0), "tag_aim_animatedY"
    // (0, yaw, 0), "tag_aim_animatedP" (pitch, 0, 0) (G_DObjSetBoneRotation).
    const bool turning = static_cast<uint8_t>(player_turn) != 0;
    if (!turning) {
        const uint32_t handle = field<uint32_t>(turret, 0x1a0);
        const uint32_t index = handle & 0xfff;
        if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8)) {
            uint8_t* const user = title_global<uint8_t*>(0x0097a968 + index * 8);
            if (user != nullptr && user[0x297] == 1 && field<int32_t>(user, 0x350) > 0) return;
        }
    }
    const uint8_t* const def = weapon_def(turret[3]);
    if (turning) {
        uint32_t& state = title_global<uint32_t>(0x0095f654);
        int32_t& loop = title_global<int32_t>(0x008d9d50);
        int32_t& stop = title_global<int32_t>(0x008d9d54);
        const auto play = [&](uint32_t fx) {
            const TitleRegs regs{address_of(turret), 0, 0, 0, 0, address_of(turret)};
            const uint32_t args[2] = {*reinterpret_cast<const uint32_t*>(def + 8), fx};
            return static_cast<int32_t>(g_ent_call(own_address(&FXCmd_Type8_entry), &regs, 2, args));  // FXCmd_Type8
        };
        const auto free_loop = [&]() {
            if (loop != -1) {
                FX_FreeEffectsById(title_global<FxIdList*>(0x0095f5b4), static_cast<uint32_t>(loop));
                loop = -1;
            }
        };
        const float pitch_change = x87_abs_diff(title_global<float>(0x0095f64c), field<float>(turret, 0x90));
        const float yaw_change = x87_abs_diff(title_global<float>(0x0095f650), field<float>(turret, 0x94));
        const float threshold = title_global<float>(0x0083ebf8);  // 0.1
        if (platform_x87_greater(pitch_change, threshold) || yaw_change > threshold) {
            if (state == 0 || state == 1) state = 2;
            if (state == 2) {
                if (loop == -1) loop = play(0x22);
            } else {
                free_loop();
                if (stop != -1) stop = -1;
            }
        } else if (state == 2) {
            state = 1;
            free_loop();
            if (stop == -1) stop = play(0x23);
        } else {
            if (state == 1) state = 0;
            free_loop();
            if (stop != -1) stop = -1;
        }
        title_global<uint32_t>(0x0095f64c) = field<uint32_t>(turret, 0x90);
        title_global<uint32_t>(0x0095f650) = field<uint32_t>(turret, 0x94);
    }
    alignas(16) float angles[3];
    angles[0] = field<float>(turret, 0x90);
    angles[1] = field<float>(turret, 0x94);
    angles[2] = 0.0f;
    uint32_t& guard = title_global<uint32_t>(0x0098f1e4);
    static const uint32_t kNames[4] = {0x00839a18, 0x00839a04, 0x008399f0, 0x008399dc};
    static const uint32_t kHashes[4] = {0x0098f1e0, 0x0098f1dc, 0x0098f1d8, 0x0098f1d4};
    for (uint32_t i = 0; i < 4; ++i) {
        if (!(guard & (1u << i))) {
            guard |= 1u << i;
            title_global<uint32_t>(kHashes[i]) =
                Com_HashKeyNoCase(0, reinterpret_cast<const char*>(uintptr_t{kNames[i]}));
        }
    }
    using SetBoneRotation = void XB_CDECL(uint8_t*, const float*, const float*, int32_t, uint32_t);
    for (uint32_t i = 0; i < 4; ++i) {
        if (i == 2) {
            angles[0] = 0.0f;
            angles[1] = field<float>(turret, 0x94);
            angles[2] = 0.0f;
        } else if (i == 3) {
            angles[0] = field<float>(turret, 0x90);
            angles[1] = 0.0f;
            angles[2] = 0.0f;
        }
        const DObj* const obj = reinterpret_cast<const DObj*>(uintptr_t{field<uint32_t>(turret, 0x22c)});
        if (obj == nullptr) continue;
        const int32_t bone = DObj_GetTagIndex(obj, title_global<uint32_t>(kHashes[i]));
        if (bone < 0) continue;
        own_function<SetBoneRotation>(&G_DObjSetBoneRotation_entry)(turret, reinterpret_cast<const float*>(uintptr_t{0x0082eec0}), angles,
                                                    bone, 0);  // G_DObjSetBoneRotation
    }
}

namespace {

// g_entityHandleTable lookup: the entity of a handle, or null.
uint8_t* handle_entity(uint32_t handle) {
    const uint32_t index = handle & 0xfff;
    if (index >= 0x540 || (handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return nullptr;
    return title_global<uint8_t*>(0x0097a968 + index * 8);
}

}  // namespace

void XB_REGS1 XB_CDECL G_MissileImpact(uint8_t* ent, uint8_t* trace) {
    // Q3's G_MissileImpact: the hit entity (trace +0x30), the attacker (owner +0x1a0; a turret's
    // (type 10) own owner). Not for mod 3: an "attacked" line event to the actors. Bouncing missiles
    // (+0x10 0x3000000) not hitting a damageable entity bounce (sound event 0xb6 with the surface,
    // unless trace +0x3d) and stop there unless the def explodes on impact (+0x748). A damageable
    // hit takes the impact damage (+0x358) along the velocity (G_Damage); defs without +0x748
    // bounce off it. Else trigger_damage checks, the explosion event (0xba, 0xbb when a client
    // was hit or trace +0x34) with the normal byte, the missile stopped at the snapped end point
    // and the splash (+0x35c) from 4 units off the surface (SV_Trace, G_RadiusDamage).
    uint32_t hit_client = 0;
    uint8_t* const other = handle_entity(*reinterpret_cast<const uint32_t*>(trace + 0x30));
    uint8_t* attacker = nullptr;
    const uint32_t owner_handle = field<uint32_t>(ent, 0x1a0);
    if (handle_entity(owner_handle) != nullptr) {
        attacker = handle_entity(owner_handle);
        if (attacker[0] == 0xa && handle_entity(field<uint32_t>(attacker, 0x1a0)) != nullptr) {
            attacker = G_EntHandleToEnt(reinterpret_cast<const uint32_t*>(attacker + 0x1a0));
        }
    }
    const uint8_t* const def = weapon_def(ent[3]);
    if (field<uint32_t>(ent, 0x368) != 3 && attacker != nullptr) {
        const TitleRegs regs{0, address_of(trace), 0, 0, 0, address_of(ent)};
        const uint32_t args[6] = {address_of(handle_entity(owner_handle)), 0x11, 0, address_of(attacker + 0x20),
                                  address_of(trace), 0};
        g_ent_call(own_address(&Actor_SendLineEvent_entry), &regs, 6, args);  // Actor_SendLineEvent
    }
    const auto bounce = [&]() {
        const TitleRegs regs{address_of(trace), address_of(trace), 0, 0, 0, address_of(ent)};
        return g_ent_call(own_address(&G_BounceMissile_entry), &regs, 0, nullptr);  // G_BounceMissile
    };
    const bool damageable = other != nullptr && field<uint32_t>(other, 0x29c) != 0;
    if (!damageable && (field<uint32_t>(ent, 0x10) & 0x3000000)) {
        if (bounce() && trace[0x3d] == 0) {
            G_AddEvent(ent, 0xb6, DirToByte(reinterpret_cast<const float*>(trace + 0x10)));
            ent[5] = static_cast<uint8_t>((*reinterpret_cast<const int32_t*>(trace + 0x24) >> 20) & 0x1f);
        }
        if (*reinterpret_cast<const uint32_t*>(def + 0x748) == 0) return;
    }
    if (damageable) {
        const int32_t damage = field<int32_t>(ent, 0x358);
        if (damage != 0) {
            const float amount = static_cast<float>(damage);
            const uint8_t* const by = attacker ? attacker : G_EntHandleToEnt(reinterpret_cast<const uint32_t*>(ent + 0x1a0));
            if (G_CheckOtherClientField100(other, 0, by)) hit_client = 1;
            alignas(16) float velocity[3] = {};
            BG_EvaluateTrajectoryDelta(0, title_global<int32_t>(0x0098540c), velocity, ent + 0x14);
            const long double sq = platform_x87_ld(velocity[2]) * velocity[2] +
                                   platform_x87_ld(velocity[1]) * velocity[1] +
                                   platform_x87_ld(velocity[0]) * velocity[0];
            const float length = title_ci_sqrt(static_cast<float>(sq));
            if (length == title_global<float>(0x0083eafc)) velocity[2] = title_global<float>(0x0083eb20);  // straight up
            uint32_t args[9] = {address_of(other), address_of(ent), address_of(attacker), address_of(velocity),
                                static_cast<uint32_t>(static_cast<int32_t>(amount)), 0, field<uint32_t>(ent, 0x368), 0,
                                0xffffffffu};
            const TitleRegs regs{address_of(ent + 0x140), address_of(trace), 0, 0, 0, address_of(ent)};
            g_ent_call(own_address(&G_Damage_entry), &regs, 9, args);  // G_Damage
        }
        if (*reinterpret_cast<const uint32_t*>(def + 0x748) == 0) {
            bounce();
            return;
        }
    }
    if (field<uint32_t>(ent, 0x358) != 0) {
        const uint8_t* const by = attacker ? attacker : pointer_at(title_global<uint8_t*>(0x0095f534), 8);
        const TitleRegs regs{0, address_of(trace), 0, 0, 0, address_of(ent)};
        const uint32_t args[5] = {address_of(by), address_of(ent + 0x140), address_of(trace), field<uint32_t>(ent, 0x358),
                                  field<uint32_t>(ent, 0x368)};
        g_ent_call(own_address(&G_CheckHitTriggerDamage), &regs, 5, args);  // G_CheckHitTriggerDamage
    }
    hit_client = (hit_client != 0 || *reinterpret_cast<const uint32_t*>(trace + 0x34) != 0) ? 1 : 0;
    // DirToByte inlined: the bytedirs entry (0x008d52d8) with the largest dot product above 0.
    const float* const normal = reinterpret_cast<const float*>(trace + 0x10);
    float best = 0.0f;
    uint8_t best_index = 0;
    for (uint32_t i = 0; i < 0xa2; ++i) {
        const float* const d = reinterpret_cast<const float*>(uintptr_t{0x008d52d8 + i * 12});
        float dot = d[0] * normal[0] + d[2] * normal[2];
        dot = dot + d[1] * normal[1];
        if (dot > best) {
            best = dot;
            best_index = static_cast<uint8_t>(i);
        }
    }
    G_AddEvent(ent, 0xba + hit_client, best_index);
    ent[5] = def[0x73c];
    ent[0] = 0;
    ent[2] = static_cast<uint8_t>((*reinterpret_cast<const int32_t*>(trace + 0x24) >> 20) & 0x1f);
    field<uint32_t>(ent, 0x1a4) |= 1;
    float* const end = reinterpret_cast<float*>(trace);
    for (uint32_t i = 0; i < 3; ++i) end[i] = static_cast<float>(static_cast<int32_t>(end[i]));  // SnapVector
    for (uint32_t i = 0; i < 3; ++i) field<uint32_t>(ent, 0x20 + i * 4) = reinterpret_cast<const uint32_t*>(trace)[i];
    field<float>(ent, 0x34) = 0.0f;
    field<float>(ent, 0x30) = 0.0f;
    field<float>(ent, 0x2c) = 0.0f;
    field<uint32_t>(ent, 0x14) = 0;
    field<uint32_t>(ent, 0x18) = 0;
    field<uint32_t>(ent, 0x1c) = 0;
    for (uint32_t i = 0; i < 3; ++i) field<uint32_t>(ent, 0x140 + i * 4) = reinterpret_cast<const uint32_t*>(trace)[i];
    for (uint32_t i = 0; i < 4; ++i) field<uint32_t>(ent, 0x60 + i * 4) = reinterpret_cast<const uint32_t*>(trace)[i];
    if (field<uint32_t>(ent, 0x35c) != 0) {
        // The splash point: 4 units off the surface, traced from the end point (point box: zero
        // vectors with w = 1; the trace lands in the impact trace).
        const float four = title_global<float>(0x0083100c);
        alignas(16) float start[4];
        std::memcpy(start, trace, 16);
        alignas(16) float point[4];
        for (uint32_t i = 0; i < 4; ++i) point[i] = normal[i] * four + start[i];
        alignas(16) float box[8];
        for (uint32_t c = 0; c < 2; ++c) {
            for (uint32_t i = 0; i < 4; ++i) box[c * 4 + i] = title_global<float>(0x0082e290 + i * 4);
        }
        {
            const TitleRegs regs{0, address_of(start), 0, 0, 0, address_of(ent)};
            const uint32_t args[11] = {address_of(trace), address_of(box), address_of(box + 4), address_of(point),
                                       field<uint32_t>(ent, 0x224), 0x11, 0, 0, 0, 0, 0};
            g_ent_call(own_address(&SV_Trace_entry), &regs, 11, args);  // SV_Trace (start in EBX)
        }
        const uint8_t* const owner = handle_entity(field<uint32_t>(ent, 0x2b0));
        const float radius = static_cast<float>(field<int32_t>(ent, 0x364));
        const float low = static_cast<float>(field<int32_t>(ent, 0x360));
        const float high = static_cast<float>(field<int32_t>(ent, 0x35c));
        uint32_t args[8];
        args[0] = address_of(point);
        args[1] = address_of(ent);
        args[2] = address_of(owner);
        std::memcpy(&args[3], &high, 4);
        std::memcpy(&args[4], &low, 4);
        std::memcpy(&args[5], &radius, 4);
        args[6] = address_of(other);
        args[7] = field<uint32_t>(ent, 0x36c);
        const TitleRegs regs{0, address_of(trace), 0, 0, 0, address_of(ent)};
        g_ent_call(own_address(&G_RadiusDamage_entry), &regs, 8, args);  // G_RadiusDamage
    }
    SV_LinkEntity(ent);
}

void XB_CDECL G_ExplodeMissile(uint8_t* ent) {
    // Q3's G_ExplodeMissile (the fuse think): the attacker (owner +0x2b0; a turret's own owner).
    // A grenade still held (+0x378 = the holder with +0x240, +0xd4 bit 0) drops from the holder's
    // "tag_weapon_right" (origin of the tag matrix, zero when the tag is missing) and is detached;
    // else it stops where its trajectory is now. Then the actors forget it, it explodes (event 0xba
    // with the normal byte of a 16-unit trace down), the surface type (0x14 in water), the snapped
    // origin, and the splash damage from 10 units above it (SV_Trace, G_RadiusDamage).
    alignas(16) uint8_t trace[0x40] = {};
    uint8_t* attacker = nullptr;
    uint8_t* const owner = handle_entity(field<uint32_t>(ent, 0x2b0));
    if (owner != nullptr) {
        attacker = owner;
        if (owner[0] == 0xa && field<uint32_t>(owner, 0x1a0) != 0) {
            attacker = G_EntHandleToEnt(reinterpret_cast<const uint32_t*>(owner + 0x1a0));
        }
    }
    const uint8_t* const def = weapon_def(ent[3]);
    alignas(16) float end[4] = {};
    uint8_t* const holder = pointer_at(ent, 0x378);
    if (holder != nullptr && field<uint32_t>(holder, 0x240) != 0 && (ent[0xd4] & 1)) {
        const uint32_t hash = cached_key(0x0098f210, 1, 0x0098f20c, 0x00831e14);  // "tag_weapon_right"
        alignas(16) float matrix[16] = {};
        const DObj* const obj = reinterpret_cast<const DObj*>(uintptr_t{field<uint32_t>(holder, 0x22c)});
        if (obj != nullptr) {
            const int32_t bone = DObj_GetTagIndex(obj, hash);
            if (bone >= 0) G_DObjGetWorldTagMatrix(holder, static_cast<uint32_t>(bone), matrix);
        }
        G_SetOrigin2(ent, 0, matrix + 12);
        field<uint32_t>(ent, 0xd4) &= ~1u;
        G_EntDetach(pointer_at(ent, 0x378),
                    reinterpret_cast<const char*>(uintptr_t{*reinterpret_cast<const uint32_t*>(def + 0x57c)}),
                    reinterpret_cast<const char*>(uintptr_t{0x00831e14}));
    } else {
        BG_EvaluateTrajectory(title_global<int32_t>(0x0098540c), end, ent + 0x14);
        const uint32_t* const position = reinterpret_cast<const uint32_t*>(end);
        for (uint32_t i = 0; i < 3; ++i) field<uint32_t>(ent, 0x20 + i * 4) = position[i];
        field<uint32_t>(ent, 0x14) = 0;
        field<uint32_t>(ent, 0x18) = 0;
        field<uint32_t>(ent, 0x1c) = 0;
        field<float>(ent, 0x34) = 0.0f;
        field<float>(ent, 0x30) = 0.0f;
        field<float>(ent, 0x2c) = 0.0f;
        for (uint32_t i = 0; i < 3; ++i) field<uint32_t>(ent, 0x140 + i * 4) = position[i];
    }
    Actor_ForgetGrenade(ent);
    ent[0] = 0;
    field<uint32_t>(ent, 0xd4) |= 0x20;
    field<uint32_t>(ent, 0x10) |= 0x80;
    field<uint32_t>(ent, 0x2a4) |= 0x400;
    // A point trace 16 units down from the origin (point box: zero vectors with w = 1).
    alignas(16) float box[8];
    for (uint32_t c = 0; c < 2; ++c) {
        for (uint32_t i = 0; i < 4; ++i) box[c * 4 + i] = title_global<float>(0x0082e290 + i * 4);
    }
    end[0] = field<float>(ent, 0x140);
    end[1] = field<float>(ent, 0x144);
    end[2] = field<float>(ent, 0x148) - title_global<float>(0x0083ebb8);
    {
        const TitleRegs regs{0, address_of(ent + 0x140), 0, 0, address_of(ent), address_of(ent + 0x140)};
        const uint32_t args[11] = {address_of(trace), address_of(box + 4), address_of(box), address_of(end),
                                   field<uint32_t>(ent, 0x224), 0x11, 0, 0, 0, 0, 0};
        g_ent_call(own_address(&SV_Trace_entry), &regs, 11, args);  // SV_Trace (start in EBX)
    }
    // DirToByte inlined: the bytedirs entry (0x008d52d8) with the largest dot product above 0.
    const float* const normal = reinterpret_cast<const float*>(trace + 0x10);
    float best = 0.0f;
    uint8_t best_index = 0;
    for (uint32_t i = 0; i < 0xa2; ++i) {
        const float* const d = reinterpret_cast<const float*>(uintptr_t{0x008d52d8 + i * 12});
        float dot = d[1] * normal[1] + d[0] * normal[0];
        dot = dot + normal[2] * d[2];
        if (dot > best) {
            best = dot;
            best_index = static_cast<uint8_t>(i);
        }
    }
    G_AddEvent(ent, 0xba, best_index);
    ent[5] = def[0x73c];
    if (SV_PointContents(reinterpret_cast<const float*>(ent + 0x140), 0, 0x20) != 0) {
        ent[2] = 0x14;
    } else {
        ent[2] = static_cast<uint8_t>((*reinterpret_cast<const int32_t*>(trace + 0x24) >> 20) & 0x1f);
    }
    field<uint32_t>(ent, 0x1a4) |= 1;
    float* const endpos = reinterpret_cast<float*>(trace);
    for (uint32_t i = 0; i < 3; ++i) endpos[i] = static_cast<float>(static_cast<int32_t>(endpos[i]));  // SnapVector
    for (uint32_t i = 0; i < 3; ++i) field<uint32_t>(ent, 0x20 + i * 4) = reinterpret_cast<const uint32_t*>(trace)[i];
    field<uint32_t>(ent, 0x14) = 0;
    field<uint32_t>(ent, 0x18) = 0;
    field<uint32_t>(ent, 0x1c) = 0;
    field<float>(ent, 0x34) = 0.0f;
    field<float>(ent, 0x30) = 0.0f;
    field<float>(ent, 0x2c) = 0.0f;
    for (uint32_t i = 0; i < 3; ++i) field<uint32_t>(ent, 0x140 + i * 4) = reinterpret_cast<const uint32_t*>(trace)[i];
    for (uint32_t i = 0; i < 4; ++i) field<uint32_t>(ent, 0x60 + i * 4) = reinterpret_cast<const uint32_t*>(trace)[i];
    if (field<uint32_t>(ent, 0x35c) != 0) {
        // The splash point: traced from the origin to 10 units above it.
        alignas(16) float up[4];
        std::memcpy(up, ent + 0x140, 16);
        up[2] = up[2] + title_global<float>(0x0083eb98);
        {
            const TitleRegs regs{0, address_of(ent + 0x140), 0, 0, address_of(ent), address_of(ent + 0x140)};
            const uint32_t args[11] = {address_of(trace), address_of(box), address_of(box + 4), address_of(up),
                                       field<uint32_t>(ent, 0x224), 0x11, 0, 0, 0, 0, 0};
            g_ent_call(own_address(&SV_Trace_entry), &regs, 11, args);  // SV_Trace (start in EBX)
        }
        const float radius = static_cast<float>(field<int32_t>(ent, 0x364));
        const float low = static_cast<float>(field<int32_t>(ent, 0x360));
        const float high = static_cast<float>(field<int32_t>(ent, 0x35c));
        uint32_t args[8];
        args[0] = address_of(trace);
        args[1] = address_of(ent);
        args[2] = address_of(attacker);
        std::memcpy(&args[3], &high, 4);
        std::memcpy(&args[4], &low, 4);
        std::memcpy(&args[5], &radius, 4);
        args[6] = address_of(ent);
        args[7] = field<uint32_t>(ent, 0x36c);
        const TitleRegs regs{0, address_of(ent + 0x140), 0, 0, address_of(ent), address_of(ent + 0x140)};
        g_ent_call(own_address(&G_RadiusDamage_entry), &regs, 8, args);  // G_RadiusDamage
    }
    SV_LinkEntity(ent);
}

ENT_EAX_FRAME_ENTRY(G_RunMissile)

void XB_CDECL G_RunMissile(uint32_t entry, uint8_t* unused_eax, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // Q3's G_RunMissile, EBX missile; the cdecl word goes on to the think / free. A point event to
    // the actors (8 every 500 ms for mod 3, else 9), the new position (BG_EvaluateTrajectory); under
    // 0.005 units of movement, or past g_missileMaxLifetime, only the think. Fast vertical missiles
    // (|delta z| > 30) out of water trace with mask + 0x20 (G_MissileTrace), else a locational
    // trace; a water hit (surface 20) makes a splash temp entity (0xb3) and traces again without
    // the water bit. Mod 3 missiles retrace with a hit 0x80000 entity's contents cleared. Bouncing
    // missiles resting on the ground (fraction 1, or a floor above 0.7) are traced 1.5 units down
    // and put on it. Then link, damage triggers (mod 3) and, on a hit: sky (surface bit 4) adds the
    // DObj entity to the list at 0x00960070 (32 max), nodraw (0x10) frees it, else G_MissileImpact.
    // Frame: S = ((entry - 4) & ~15) - 0x80: scratch +0x10, dir +0x14, end +0x20, old origin +0x30,
    // trace +0x40.
    (void)unused_eax;
    uint8_t* const ent = reinterpret_cast<uint8_t*>(uintptr_t{ebx});
    const uint32_t self = ebx;
    const uint32_t arg = stack_at(entry + 4);
    const uint32_t args1[1] = {arg};
    stack_at(entry - 4) = ebp;
    const uint32_t A = (entry - 4) & ~15u;
    stack_at(A - 0x7c) = esi;
    stack_at(A - 0x80) = edi;
    const uint32_t S = A - 0x80;
    const uint32_t origin = self + 0x140;
    const uint32_t end = S + 0x20;
    const uint32_t old = S + 0x30;
    const uint32_t tr = S + 0x40;
    uint8_t* const trace = reinterpret_cast<uint8_t*>(uintptr_t{tr});
    const uint32_t owner = word_at(ent, 0x1a0);
    stack_at(S + 0x70) = 0;
    stack_at(S + 0x74) = 0;
    const int32_t now = title_global<int32_t>(0x0098540c);
    if (word_at(ent, 0x368) == 3) {
        const int32_t since = now - field<int32_t>(ent, 0x2f0);
        if (since >= 0x1f4) {
            const uint32_t args[4] = {self, 8, origin, 0};
            call_at(S, 0x00273f37, own_address(&Actor_SendPointEvent_entry), TitleRegs{0xffffffffu, self, static_cast<uint32_t>(since), origin, esi, edi}, 4,
                    args);  // Actor_SendPointEvent
            word_at(ent, 0x2f0) = title_global<uint32_t>(0x0098540c);
        }
    } else {
        const uint32_t args[4] = {self, 9, origin, 0};
        call_at(S, 0x00273f57, own_address(&Actor_SendPointEvent_entry), TitleRegs{0xffffffffu, self, origin, 0, esi, edi}, 4,
                args);  // Actor_SendPointEvent
    }
    for (uint32_t i = 0; i < 3; ++i) stack_at(old + i * 4) = word_at(ent, 0x140 + i * 4);
    call_at(S, 0x00273f98, own_address(&BG_EvaluateTrajectory_entry),
            TitleRegs{title_global<uint32_t>(0x0098540c), self, end, 0, origin, self + 0x14}, 0,
            nullptr);  // BG_EvaluateTrajectory
    {
        const float dz = stack_float(end + 8) - field<float>(ent, 0x148);
        const float dy = stack_float(end + 4) - field<float>(ent, 0x144);
        const float dx = stack_float(end) - field<float>(ent, 0x140);
        const float sq = (dz * dz + dy * dy) + dx * dx;
        stack_float(S + 0x10) = sq;
        const float length = title_ci_sqrt(sq);
        stack_float(S + 0x10) = length;
        if (platform_x87_greater(title_global<float>(0x0083eb88), length)) {
            call_at(S, 0x002744cc, own_address(&G_RunThink_entry), TitleRegs{0, self, 0, arg, self, self + 0x14}, 1, args1);  // G_RunThink
            return;
        }
    }
    const float lifetime = title_global<float>(0x00985ffc);  // g_missileMaxLifetime
    if (lifetime != title_global<float>(0x0083eafc)) {
        const int32_t elapsed = title_global<int32_t>(0x0098540c) - field<int32_t>(ent, 0x2f0);
        if (static_cast<float>(elapsed) >= lifetime) {
            call_at(S, 0x0027403e, own_address(&G_RunThink_entry), TitleRegs{static_cast<uint32_t>(elapsed), self, arg, 0, self, self + 0x14},
                    1, args1);  // G_RunThink
            return;
        }
    }
    {
        const uint32_t bits = word_at(ent, 0x34) & 0x7fffffffu;  // fabs on the x87
        float speed;
        std::memcpy(&speed, &bits, 4);
        stack_at(S + 0x10) = bits;
        bool in_air = false;
        if (platform_x87_greater(speed, title_global<float>(0x0083ee38))) {
            const uint32_t args[3] = {origin, 0, 0x20};
            in_air = call_at(S, 0x0027407b, own_address(&SV_PointContents), TitleRegs{S - 8, self, 0, 0, origin, self + 0x14}, 3, args) ==
                     0;  // SV_PointContents
        }
        if (in_air) {
            const uint32_t args[3] = {tr, origin, owner};
            call_at(S, 0x002740a6, own_address(&G_MissileTrace_entry), TitleRegs{0x008da6f0, self, word_at(ent, 0x2a8) | 0x20, tr, origin, end}, 3,
                    args);  // G_MissileTrace
        } else {
            const uint32_t args[7] = {tr, origin, end, owner, word_at(ent, 0x2a8), 0x008da6f0, 0};
            call_at(S, 0x002740d0, own_address(&G_LocationalTrace_entry), TitleRegs{tr, self, owner, end, origin, self + 0x14}, 7,
                    args);  // G_LocationalTrace
        }
    }
    if ((stack_at(tr + 0x24) & 0x1f00000) == 0x1400000) {
        const uint32_t dir = S + 0x14;
        call_at(S, 0x002740f5, own_address(&VectorNormalize2_entry), TitleRegs{0, self, 0x1400000, 0, self + 0x2c, dir}, 0,
                nullptr);  // VectorNormalize2
        const float z = stack_float(dir + 8);
        if (0.0f > z) stack_float(dir + 8) = z * title_global<float>(0x0083eb34);
        const uint32_t targs[2] = {origin, 0xb3};
        const uint32_t tent_word =
            call_at(S, 0x00274122, own_address(&G_TempEntity), TitleRegs{origin, self, 0x1400000, 0, self + 0x2c, dir}, 2,
                    targs);  // G_TempEntity
        uint8_t* const tent = reinterpret_cast<uint8_t*>(uintptr_t{tent_word});
        const uint32_t byte1 = call_at(S - 8, 0x0027412d, own_address(&DirToByte_entry), TitleRegs{tent_word, self, 0, 0, tr + 0x10, tent_word},
                                       0, nullptr);  // DirToByte
        tent[4] = static_cast<uint8_t>(byte1);
        const uint32_t byte2 =
            call_at(S - 8, 0x00274139, own_address(&DirToByte_entry), TitleRegs{byte1, self, 0, 0, dir, tent_word}, 0, nullptr);  // DirToByte
        tent[5] = static_cast<uint8_t>(byte2);
        tent[2] = static_cast<uint8_t>((static_cast<int32_t>(stack_at(tr + 0x24)) >> 20) & 0x1f);
        tent[3] = ent[3];
        word_at(tent, 8) = word_at(ent, 0x224);
        tent[3] = ent[3];
        const uint32_t args[3] = {tr, origin, word_at(ent, 0x1a0)};
        call_at(S - 8, 0x00274185, own_address(&G_MissileTrace_entry), TitleRegs{0x008da6f0, self, word_at(ent, 0x2a8), args[2], dir, end}, 3,
                args);  // G_MissileTrace
    }
    if (word_at(ent, 0x368) == 3) {
        const uint32_t handle = stack_at(tr + 0x30);
        const uint32_t index = handle & 0xfff;
        if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8) &&
            title_global<uint32_t>(0x0097a968 + index * 8) != 0) {
            const uint32_t hit = call_at(S, 0x002741cd, own_address(&G_EntHandleToEnt), TitleRegs{tr + 0x30, self, 0, 0, origin, end}, 0,
                                         nullptr);  // G_EntHandleToEnt
            if (word_at(reinterpret_cast<uint8_t*>(uintptr_t{hit}), 0x2a4) & 0x80000) {
                const uint32_t e_word = call_at(S, 0x002741e2, own_address(&G_EntHandleToEnt), TitleRegs{tr + 0x30, self, 0, 0, origin, end}, 0,
                                                nullptr);  // G_EntHandleToEnt
                uint8_t* const e = reinterpret_cast<uint8_t*>(uintptr_t{e_word});
                const uint32_t contents = word_at(e, 0x134);
                word_at(e, 0x134) = 0;
                stack_at(S + 0xc) = contents;
                const uint32_t args[3] = {tr, origin, word_at(ent, 0x1a0)};
                call_at(S, 0x0027421f, own_address(&G_MissileTrace_entry), TitleRegs{0x008da6f0, self, word_at(ent, 0x2a8), args[2], e_word, end}, 3,
                        args);  // G_MissileTrace
                word_at(e, 0x134) = stack_at(S + 0xc);
            }
        }
    }
    for (uint32_t i = 0; i < 3; ++i) word_at(ent, 0x140 + i * 4) = stack_at(tr + i * 4);
    float fraction;
    if (trace[0x3d] != 0) {
        fraction = 0.0f;
        stack_float(tr + 0x20) = fraction;
    } else {
        fraction = stack_float(tr + 0x20);
    }
    if (word_at(ent, 0x10) & 0x3000000) {
        const float one = title_global<float>(0x0083eb20);
        bool ground = fraction == one;
        if (!ground) ground = one > fraction && stack_float(tr + 0x18) > title_global<float>(0x0083ed28);
        if (ground) {
            stack_float(end) = field<float>(ent, 0x140);
            stack_float(end + 4) = field<float>(ent, 0x144);
            stack_float(end + 8) = field<float>(ent, 0x148) - title_global<float>(0x0083eaf4);
            const uint32_t args[3] = {tr, origin, word_at(ent, 0x1a0)};
            call_at(S, 0x002742fe, own_address(&G_MissileTrace_entry), TitleRegs{0x008da6f0, self, word_at(ent, 0x2a8), args[2], origin, end}, 3,
                    args);  // G_MissileTrace
            if (stack_float(tr + 0x20) != title_global<float>(0x0083eb20)) {
                const float z = stack_float(tr + 8);
                const float half = title_global<float>(0x0083eaf4);
                field<float>(ent, 0x28) = ((z + half) - field<float>(ent, 0x148)) + field<float>(ent, 0x28);
                word_at(ent, 0x140) = stack_at(tr);
                word_at(ent, 0x144) = stack_at(tr + 4);
                field<float>(ent, 0x148) = z;
                field<float>(ent, 0x148) = z + half;
            }
        }
    }
    call_at(S, 0x0027436e, own_address(&SV_LinkEntity_entry), TitleRegs{0, self, 0, 0, origin, self}, 0, nullptr);  // SV_LinkEntity
    if (word_at(ent, 0x368) == 3) {
        const uint32_t args[5] = {self, old, origin, word_at(ent, 0x35c), 3};
        call_at(S, 0x0027438c, own_address(&G_MissileActivateDamageTriggers), TitleRegs{0, self, args[3], old, origin, self}, 5,
                args);  // G_MissileActivateDamageTriggers
    }
    if (stack_float(tr + 0x20) == title_global<float>(0x0083eb20)) {
        const float x = field<float>(ent, 0x2c);
        const float y = field<float>(ent, 0x30);
        const float z = field<float>(ent, 0x34);
        const float sq = (x * x + y * y) + z * z;
        stack_float(S + 0x10) = sq;
        const float length = title_ci_sqrt(sq);
        stack_float(S + 0x10) = length;
        if (!(length == title_global<float>(0x0083eafc))) word_at(ent, 0xc) = 0;
        call_at(S, 0x002744cc, own_address(&G_RunThink_entry), TitleRegs{0, self, 0, arg, self, self}, 1, args1);  // G_RunThink
        return;
    }
    const uint8_t surface = trace[0x24];
    if (surface & 4) {
        if (word_at(ent, 0x22c) != 0 && !(word_at(ent, 0x2a4) & 0x200000)) {
            word_at(ent, 0x2a4) |= 0x200000;
            const int32_t count = title_global<int32_t>(0x00960070);
            if (count < 0x20) {
                title_global<uint32_t>(0x00960074 + count * 4) = word_at(ent, 0x224);
                const uint32_t next = static_cast<uint32_t>(title_global<int32_t>(0x00960070) + 1);
                title_global<uint32_t>(0x00960070) = next;
                call_at(S, 0x0027445e, own_address(&G_RunThink_entry), TitleRegs{next, self, static_cast<uint32_t>(count), arg, self, self}, 1,
                        args1);  // G_RunThink
                return;
            }
        }
        call_at(S, 0x002744cc, own_address(&G_RunThink_entry), TitleRegs{0, self, 0, arg, self, self}, 1, args1);  // G_RunThink
        return;
    }
    if (surface & 0x10) {
        const uint32_t args[2] = {self, arg};
        call_at(S, 0x002743bc, own_address(&G_FreeEntity), TitleRegs{arg, self, 0, 0, origin, self}, 2, args);  // G_FreeEntity
        return;
    }
    title_global<uint32_t>(0x0095f704) = self;
    const uint32_t args[1] = {tr};
    call_at(S, 0x002743d7, own_address(&G_MissileImpact), TitleRegs{self, self, tr, 0, origin, self}, 1, args);  // G_MissileImpact
    title_global<uint32_t>(0x0095f704) = 0;
    if (ent[0] != 3) return;
    call_at(S, 0x002743f9, own_address(&G_RunThink_entry), TitleRegs{3, self, 0, arg, self, self}, 1, args1);  // G_RunThink
}

PLATFORM_ASM_ENTRY(Pickup_Weapon_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Pickup_Weapon) "addl $16, %esp\n"
                   "ret")

int32_t XB_CDECL Pickup_Weapon(uint8_t* ent, uint8_t* other, uint32_t* event, uint32_t touched) {
    // Q3's Pickup_Weapon (EBX item; cdecl player, &event, touched flag). The item's ammo (+0x370)
    // and clip (+0x3bc): 0 = rolled once (ammo in the def's range +0x71c..+0x720, or around half a
    // clip when the range is 0), negative = none; capped by the max ammo (0x00929818) and clip size
    // (0x00935338). A held weapon only takes the ammo (event 0x96, "gm" message when some was
    // added; what is left stays on the item, 0 back while some is left and 0x00950d7c is set).
    // Else the weapon needs a slot: a free one, else the one its class takes, else the current
    // weapon (same class, or grenade/pistol slot byte 3/4; else slot 1 when the new weapon has no
    // ammo) is dropped where the item lies ("can't get primary" otherwise), then given (selected
    // for the local player unless touched or kind 5) with its clip and ammo. Then the "trigger" /
    // "pickup" notifies; -1, or 0x00950914 with the counts cleared for +0x2a0 bit 3.
    const int32_t weapon = field<int32_t>(pointer_at(ent, 0x3b0), 0x28);
    const uint8_t* const def = weapon_def(static_cast<uint32_t>(weapon));
    const auto clip_size = [&]() {
        return title_global<int32_t>(0x00935338 + *reinterpret_cast<const uint32_t*>(weapon_def(weapon) + 0x5a0) * 4);
    };
    int32_t ammo;
    int32_t count = field<int32_t>(ent, 0x370);
    if (count < 0) {
        ammo = 0;
    } else {
        if (count == 0) {
            const int32_t hi = *reinterpret_cast<const int32_t*>(def + 0x720);
            const int32_t lo = *reinterpret_cast<const int32_t*>(def + 0x71c);
            const int32_t high = hi < lo ? lo : hi;
            const int32_t low = hi < lo ? hi : lo;
            if (high == 0 && low == 0) {
                const int32_t clip = title_global<int32_t>(0x00935338 + *reinterpret_cast<const uint32_t*>(def + 0x5a0) * 4);
                const int32_t r = title_function<int32_t XB_CDECL()>(0x001efcc0)();  // rand
                float f = static_cast<float>(r) * title_global<float>(0x0083ebe0) + title_global<float>(0x0083eb20);
                f = f * static_cast<float>(clip - 1);
                const float half = title_global<float>(0x0083eb24);
                f = f * half + half;
                field<int32_t>(ent, 0x370) = static_cast<int32_t>(f) + 1;
            } else if (high < 0) {
                field<int32_t>(ent, 0x370) = 0;
            } else {
                int32_t value;
                if (high != 0 && high == low) {
                    value = low;
                } else {
                    const int32_t r = title_function<int32_t XB_CDECL()>(0x001efcc0)();  // rand
                    value = r % (high - low) + low;
                }
                field<int32_t>(ent, 0x370) = value;
                if (value <= 0) field<int32_t>(ent, 0x370) = 0;
            }
        }
        const int32_t max_ammo =
            title_global<int32_t>(0x00929818 + *reinterpret_cast<const uint32_t*>(weapon_def(weapon) + 0x598) * 4);
        if (field<int32_t>(ent, 0x370) > max_ammo) field<int32_t>(ent, 0x370) = max_ammo;
        ammo = field<int32_t>(ent, 0x370);
        count = ammo;
    }
    int32_t clip;
    const int32_t item_clip = field<int32_t>(ent, 0x3bc);
    if (item_clip < 0) {
        clip = 0;
    } else {
        if (item_clip == 0) {
            if (count < 0) {
                field<int32_t>(ent, 0x3bc) = 0;
            } else {
                const int32_t size = clip_size();
                field<int32_t>(ent, 0x3bc) = size;
                if (size > field<int32_t>(ent, 0x370)) field<int32_t>(ent, 0x3bc) = field<int32_t>(ent, 0x370);
                field<int32_t>(ent, 0x370) = field<int32_t>(ent, 0x370) - field<int32_t>(ent, 0x3bc);
                ammo = field<int32_t>(ent, 0x370);
            }
        }
        const int32_t size = clip_size();
        if (field<int32_t>(ent, 0x3bc) > size) field<int32_t>(ent, 0x3bc) = size;
        clip = field<int32_t>(ent, 0x3bc);
    }
    uint8_t* const client = pointer_at(other, 0x23c);
    const auto holds = [&](uint32_t w) {
        return (word_at(client, 0x3f0 + (static_cast<int32_t>(w) >> 5) * 4) & (1u << (w & 0x1f))) != 0;
    };
    const auto send = [&](const char* text) { SV_GameSendServerCommand(text, word_at(other, 0x224)); };
    const auto va1 = [](uint32_t format) { return own_function<const char* XB_CDECL(uint32_t)>(&va_entry)(format); };
    const auto va2 = [](uint32_t format, uint32_t a) {
        return own_function<const char* XB_CDECL(uint32_t, uint32_t)>(&va_entry)(format, a);
    };
    if (holds(static_cast<uint32_t>(weapon))) {
        ammo = ammo + clip;
        *event = 0x96;
        const int32_t added = static_cast<int32_t>(Add_Ammo(other, static_cast<uint32_t>(weapon), ammo, 0));
        if (added != 0) {
            const uint32_t name = *reinterpret_cast<const uint32_t*>(def + 0xc);
            const char* const text = *reinterpret_cast<const uint32_t*>(weapon_def(weapon) + 0x6ec) != 0
                                         ? va2(0x0083b77c, name)
                                         : va2(0x0083b760, name);
            send(text);
        }
        if (added != ammo) {
            int32_t left = field<int32_t>(ent, 0x370) - added;
            field<int32_t>(ent, 0x370) = left;
            if (left <= 0) {
                const int32_t clip_left = field<int32_t>(ent, 0x3bc) + left;
                field<int32_t>(ent, 0x3bc) = clip_left;
                field<int32_t>(ent, 0x370) = -1;
                if (clip_left <= 0) field<int32_t>(ent, 0x3bc) = -1;
            }
            if ((field<int32_t>(ent, 0x370) > 0 || field<int32_t>(ent, 0x3bc) > 0) && title_global<uint32_t>(0x00950d7c))
                return 0;
        }
    } else {
        bool give = false;
        const uint32_t current = word_at(client, 0x80);
        if (current != 0) {
            if (!holds(current)) return 0;
            if (BG_GetWeaponSlot(client, static_cast<int32_t>(word_at(client, 0x80)), 1) == 0) {
                uint8_t* const ps = pointer_at(other, 0x23c);
                const uint32_t cls = *reinterpret_cast<const uint32_t*>(weapon_def(word_at(ps, 0x80)) + 0x9c);
                if (BG_ChooseWeaponSlot(cls, word_at(ps, 0x80), ps) == 0 &&
                    BG_GetFreeWeaponSlot(static_cast<uint32_t>(weapon), client) == 0)
                    return 0;
            }
        }
        if (BG_GetFreeWeaponSlot(static_cast<uint32_t>(weapon), client) != 0) {
            give = true;
        } else {
            uint8_t* const ps = pointer_at(other, 0x23c);
            const uint32_t cls = *reinterpret_cast<const uint32_t*>(weapon_def(word_at(ps, 0x80)) + 0x9c);
            if (BG_ChooseWeaponSlot(cls, static_cast<uint32_t>(weapon), ps) != 0) give = true;
        }
        if (!give) {
            const uint32_t cls = *reinterpret_cast<const uint32_t*>(def + 0x9c);
            const uint32_t cur = word_at(client, 0x80);
            uint32_t dropped;
            if (cls == *reinterpret_cast<const uint32_t*>(weapon_def(cur) + 0x9c)) {
                dropped = Drop_Weapon(other, cur, nullptr);
            } else if (cls == 3 || cls == 4) {
                dropped = Drop_Weapon(other, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(client[0x3f8 + cls]))),
                                      nullptr);
            } else {
                const uint8_t* const new_def = weapon_def(weapon);
                const uint32_t stock = word_at(client, 0x110 + *reinterpret_cast<const uint32_t*>(new_def + 0x598) * 4);
                if (stock != 0 || word_at(client, 0x280 + *reinterpret_cast<const uint32_t*>(new_def + 0x5a0) * 4) != 0) {
                    send(va1(0x0083b738));  // "can't get primary"
                    return 0;
                }
                dropped = Drop_Weapon(other, static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(client[0x3f9]))),
                                      nullptr);
            }
            if (dropped == 0) return 0;
            uint8_t* const drop = reinterpret_cast<uint8_t*>(uintptr_t{dropped});
            alignas(4) float origin[3];
            origin[0] = field<float>(ent, 0x140);
            origin[1] = field<float>(ent, 0x144);
            origin[2] = field<float>(ent, 0x148);
            uint8_t* const placed = G_SetOrigin2(drop, 0, origin);
            SV_LinkEntity(G_SetAngle(placed, 0, reinterpret_cast<const float*>(ent + 0x150)));
        }
        BG_GivePlayerWeapon(static_cast<uint32_t>(weapon), pointer_at(other, 0x23c));
        if (touched == 0 && other == pointer_at(title_global<uint8_t*>(0x0095f534), 4) &&
            *reinterpret_cast<const uint32_t*>(def + 0x98) != 5)
            CG_SelectWeapon(weapon);
        if (*reinterpret_cast<const uint32_t*>(def + 0x98) == 5) {
            ammo = ammo + clip;
        } else if (clip >= 0) {
            const uint32_t index = *reinterpret_cast<const uint32_t*>(weapon_def(weapon) + 0x5a0);
            int32_t value = clip;
            const int32_t size = title_global<int32_t>(0x00935338 + index * 4);
            if (clip > size) {
                ammo = ammo + (clip - size);
                clip = size;
                value = size;
            }
            word_at(pointer_at(other, 0x23c), 0x280 + index * 4) = static_cast<uint32_t>(value);
        }
        Add_Ammo(other, static_cast<uint32_t>(weapon), ammo, clip == -1 ? 1 : 0);
    }
    Scr_Notify(ent, title_global<uint32_t>(0x0097a818));  // trigger
    if (word_at(other, 0x23c) != 0) Scr_Notify(other, title_global<uint32_t>(0x0097a758));  // pickup
    if (!(ent[0x2a0] & 8)) return -1;
    word_at(ent, 0x370) = 0;
    word_at(ent, 0x3bc) = 0;
    return title_global<int32_t>(0x00950914);
}

ENT_EAX_FRAME_ENTRY(G_MoverPush)

uint32_t XB_CDECL G_MoverPush(uint32_t entry, uint8_t* pusher, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // Q3's G_MoverPush (EAX pusher; cdecl move, amove, &obstacle, entity list, pushed list). The
    // box to search: absmin/absmax + move, or (rotating: angles or amove not all 0) the origin
    // +- RadiusFromBounds (x87 sums); the swept box spans both. The pusher moves and turns
    // (relinked); entities of type 1, 2, 3, 0xb, 0xd or items (+0x294) standing on it (+0xc =
    // its number) or inside the moved box and blocked by it (G_TestEntityPosition) are pushed
    // (unlinked, then G_TryPushingEntity with vec4 move/amove, pushed_p record {entity, origin,
    // yaw delta}); a blocked one that is not an item/0xd is crushed (G_Damage 99999, mod 0x10)
    // by type-4 movers, else it is the obstacle and the push fails (0). 1 after relinking them.
    // Frame: S = ((entry - 4) & ~15) - 0xb0: radius +0xc, swept max +0x10, count index +0x3c,
    // swept min +0x20, move min +0x40, move max +0x50, area parms +0x60.
    const uint32_t A = (entry - 4) & ~15u;
    stack_at(entry - 4) = ebp;
    stack_at(A - 0xa8) = ebx;
    stack_at(A - 0xac) = esi;
    stack_at(A - 0xb0) = edi;
    const uint32_t S = A - 0xb0;
    const uint32_t self = address_of(pusher);
    const float* const move = reinterpret_cast<const float*>(uintptr_t{stack_at(entry + 4)});
    const float* const amove = reinterpret_cast<const float*>(uintptr_t{stack_at(entry + 8)});
    uint32_t* const obstacle = reinterpret_cast<uint32_t*>(uintptr_t{stack_at(entry + 0xc)});
    const uint32_t list = stack_at(entry + 0x10);
    uint32_t* const pushed = reinterpret_cast<uint32_t*>(uintptr_t{stack_at(entry + 0x14)});
    const uint32_t move_w = address_of(move), amove_w = address_of(amove);
    *obstacle = 0;
    const float zero = 0.0f;
    const auto nonzero = [&](float v) { return !(v == zero); };
    float min_x, min_y, min_z, max_z;
    if (nonzero(field<float>(pusher, 0x150)) || nonzero(field<float>(pusher, 0x154)) ||
        nonzero(field<float>(pusher, 0x158)) || nonzero(amove[0]) || nonzero(amove[1]) || nonzero(amove[2])) {
        const long double r_x87 = RadiusFromBounds(reinterpret_cast<const float*>(pusher + 0xf0), 0,
                                                   reinterpret_cast<const float*>(pusher + 0xe0));
        stack_at(S - 4) = 0x0026babc;
        const float r = static_cast<float>(r_x87);
        stack_float(S + 0xc) = r;
        const long double rl = platform_x87_ld(r);
        const float ox = field<float>(pusher, 0x140), oy = field<float>(pusher, 0x144), oz = field<float>(pusher, 0x148);
        stack_float(S + 0x50) = static_cast<float>((platform_x87_ld(move[0]) + ox) + rl);
        min_x = ox - r;
        stack_float(S + 0x40) = move[0] + min_x;
        stack_float(S + 0x10) = static_cast<float>(rl + ox);
        min_y = oy - r;
        stack_float(S + 0x44) = move[1] + min_y;
        min_z = oz - r;
        stack_float(S + 0x48) = min_z + move[2];
        stack_float(S + 0x54) = static_cast<float>((rl + move[1]) + oy);
        stack_float(S + 0x58) = (oz + move[2]) + r;
        max_z = oz + r;
        stack_float(S + 0x14) = static_cast<float>(rl + oy);
    } else {
        stack_float(S + 0x40) = move[0] + field<float>(pusher, 0x100);
        stack_float(S + 0x50) = field<float>(pusher, 0x110) + move[0];
        stack_float(S + 0x44) = move[1] + field<float>(pusher, 0x104);
        stack_float(S + 0x54) = move[1] + field<float>(pusher, 0x114);
        stack_float(S + 0x48) = field<float>(pusher, 0x108) + move[2];
        stack_float(S + 0x58) = move[2] + field<float>(pusher, 0x118);
        min_x = field<float>(pusher, 0x100);
        min_y = field<float>(pusher, 0x104);
        min_z = field<float>(pusher, 0x108);
        max_z = field<float>(pusher, 0x118);
        stack_float(S + 0x10) = field<float>(pusher, 0x110);
        stack_float(S + 0x14) = field<float>(pusher, 0x114);
    }
    stack_float(S + 0x20) = min_x;
    stack_float(S + 0x24) = min_y;
    stack_float(S + 0x28) = min_z;
    stack_float(S + 0x18) = max_z;
    if (move[0] > zero)
        stack_float(S + 0x10) = move[0] + stack_float(S + 0x10);
    else
        stack_float(S + 0x20) = move[0] + min_x;
    if (move[1] > zero)
        stack_float(S + 0x14) = move[1] + stack_float(S + 0x14);
    else
        stack_float(S + 0x24) = move[1] + min_y;
    if (move[2] > zero)
        stack_float(S + 0x18) = max_z + move[2];
    else
        stack_float(S + 0x28) = min_z + move[2];
    word_at(pusher, 0xd0) = 0;
    call_at(S, 0x0026bc05, own_address(&SV_UnlinkEntity), TitleRegs{self + 0xd0, move_w, 0, 0, self, amove_w}, 0,
            nullptr);  // SV_UnlinkEntity
    // The area parms (+0x10 mins, +0x20 maxs with the w words the frame holds, list, count, max, mask).
    for (uint32_t i = 0; i < 4; ++i) stack_at(S + 0x70 + i * 4) = stack_at(S + 0x20 + i * 4);
    for (uint32_t i = 0; i < 4; ++i) stack_at(S + 0x80 + i * 4) = stack_at(S + 0x10 + i * 4);
    stack_at(S + 0x90) = list;
    stack_at(S + 0x94) = 0;
    stack_at(S + 0x98) = 0x540;
    stack_at(S + 0x9c) = 0x600c180;
    {
        const uint32_t args[2] = {0x00935664, S + 0x60};
        call_at(S, 0x0026bc8b, own_address(&SV_AreaEntities_r), TitleRegs{list, move_w, S + 0x60, stack_at(S + 0x1c), self, amove_w}, 2,
                args);  // SV_AreaEntities_r
    }
    field<float>(pusher, 0x140) = move[0] + field<float>(pusher, 0x140);
    field<float>(pusher, 0x144) = move[1] + field<float>(pusher, 0x144);
    field<float>(pusher, 0x148) = field<float>(pusher, 0x148) + move[2];
    field<float>(pusher, 0x150) = amove[0] + field<float>(pusher, 0x150);
    field<float>(pusher, 0x154) = field<float>(pusher, 0x154) + amove[1];
    field<float>(pusher, 0x158) = field<float>(pusher, 0x158) + amove[2];
    call_at(S, 0x0026bd11, own_address(&SV_LinkEntity_entry), TitleRegs{0, move_w, 0, 0, self, self}, 0, nullptr);  // SV_LinkEntity
    const auto entity_of = [](uint32_t handle) -> uint32_t {
        const uint32_t index = handle & 0xfff;
        if (index >= 0x540 || (handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return 0;
        return title_global<uint32_t>(0x0097a968 + index * 8);
    };
    stack_at(S + 0xc) = 0;
    for (int32_t i = 0; i < static_cast<int32_t>(stack_at(S + 0x94)); ++i) {
        const uint32_t handle = reinterpret_cast<const uint32_t*>(uintptr_t{list})[i];
        const uint32_t e_word = entity_of(handle);
        uint8_t* const e = reinterpret_cast<uint8_t*>(uintptr_t{e_word});
        const uint8_t type = e[0];
        if (!(type == 3 || type == 2 || type == 1 || type == 0xb || type == 0xd || e[0x294] != 0)) continue;
        bool take = word_at(e, 0xc) == word_at(pusher, 0x224);
        if (!take) {
            if (field<float>(e, 0x100) >= stack_float(S + 0x50)) continue;
            if (field<float>(e, 0x104) >= stack_float(S + 0x54)) continue;
            if (field<float>(e, 0x108) >= stack_float(S + 0x58)) continue;
            if (stack_float(S + 0x40) >= field<float>(e, 0x110)) continue;
            if (stack_float(S + 0x44) >= field<float>(e, 0x114)) continue;
            if (stack_float(S + 0x48) >= field<float>(e, 0x118)) continue;
            const uint32_t args[1] = {e_word + 0x140};
            take = call_at(S, 0x0026bdeb, own_address(&G_TestEntityPosition), TitleRegs{e_word, move_w, word_at(e, 0xc), e_word + 0x140,
                                                                 self, static_cast<uint32_t>(i)},
                           1, args) == self;  // G_TestEntityPosition
        }
        if (take) {
            const uint32_t n = stack_at(S + 0xc);
            pushed[n] = reinterpret_cast<const uint32_t*>(uintptr_t{list})[i];
            stack_at(S + 0xc) = n + 1;
        }
    }
    const int32_t count = static_cast<int32_t>(stack_at(S + 0xc));
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t e_word = entity_of(pushed[i]);
        word_at(reinterpret_cast<uint8_t*>(uintptr_t{e_word}), 0xd0) = 0;
        call_at(S, 0x0026be5b, own_address(&SV_UnlinkEntity), TitleRegs{e_word + 0xd0, move_w, 0, e_word, self, static_cast<uint32_t>(i)}, 0,
                nullptr);  // SV_UnlinkEntity
    }
    stack_at(S + 0x3c) = 0;
    for (int32_t i = 0; i < count; ++i) {
        stack_at(S + 0x3c) = static_cast<uint32_t>(i);
        const uint32_t e_word = entity_of(pushed[i]);
        uint8_t* const e = reinterpret_cast<uint8_t*>(uintptr_t{e_word});
        uint32_t* const record = reinterpret_cast<uint32_t*>(uintptr_t{title_global<uint32_t>(0x0095bef8)});  // pushed_p
        record[0] = e_word;
        record[1] = word_at(e, 0x140);
        record[2] = word_at(e, 0x144);
        record[3] = word_at(e, 0x148);
        record[4] = reinterpret_cast<const uint32_t*>(amove)[1];
        const uint32_t* const am = reinterpret_cast<const uint32_t*>(amove);
        const uint32_t* const mv = reinterpret_cast<const uint32_t*>(move);
        stack_at(S + 0x28) = am[2];
        stack_at(S + 0x2c) = am[0];
        stack_at(S + 0x24) = am[1];
        stack_at(S + 0x20) = am[0];
        for (uint32_t k = 0; k < 4; ++k) stack_at(S + 0x40 + k * 4) = stack_at(S + 0x20 + k * 4);
        stack_at(S + 0x18) = mv[2];
        stack_at(S + 0x1c) = mv[0];
        stack_at(S + 0x14) = mv[1];
        stack_at(S + 0x10) = mv[0];
        for (uint32_t k = 0; k < 4; ++k) stack_at(S + 0x50 + k * 4) = stack_at(S + 0x10 + k * 4);
        const uint32_t args[3] = {e_word, self, S + 0x40};
        const uint32_t moved = call_at(S, 0x0026bf4a, own_address(&G_TryPushingEntity_entry), TitleRegs{S + 0x50, move_w, amove_w, am[1], self, e_word}, 3,
                                       args);  // G_TryPushingEntity
        if (moved == 0 && e[0] != 2 && e[0] != 0xd) {
            if (word_at(pusher, 0x14) != 4 && word_at(pusher, 0x38) != 4) {
                *obstacle = e_word;
                return 0;
            }
            const uint32_t dargs[9] = {e_word, self, self, 0, 0x1869f, 0, 0x10, 0, 0xffffffffu};
            call_at(S, 0x0026bf89, own_address(&G_Damage_entry), TitleRegs{0, move_w, word_at(pusher, 0x14), 0, self, e_word}, 9,
                    dargs);  // G_Damage
        } else {
            call_at(S, 0x0026bf93, own_address(&SV_LinkEntity_entry), TitleRegs{moved, move_w, 0, 0, self, e_word}, 0, nullptr);  // SV_LinkEntity
        }
    }
    if (count > 0) stack_at(S + 0x3c) = static_cast<uint32_t>(count);
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t e_word = entity_of(pushed[i]);
        call_at(S, 0x0026bfe2, own_address(&SV_LinkEntity_entry), TitleRegs{pushed[i] & 0xfff, move_w, pushed[i] >> 12, 0, static_cast<uint32_t>(i),
                                                     e_word},
                0, nullptr);  // SV_LinkEntity
    }
    return 1;
}

ENT_FULL_FRAME_ENTRY(ClientSpawn)

void XB_CDECL ClientSpawn(uint32_t entry, uint32_t angles, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp,
                          uint32_t origin) {
    // Q3's ClientSpawn (EAX angles, EBX entity, ECX origin): the client state cleared except the
    // persistant block (+0x520, 100 bytes) and +0x98; client number, run cvar for the local
    // player (else +0xd4 bit 8), the "player" classname (+0x25c, hash +0x260), contents, clip
    // mask, bounds (0x0097a8a0 / 0x00984d00) and the movement tunables, health (+0x100 =
    // +0x108), the origin, the per-client settings block (0x00927e8c + 0x30, 44 bytes; __break
    // for a bad client), view angles, the +0x430/+0x440 +-FLT_MAX bounds, two lazily allocated
    // buffers (+0x450 0x1000, +0x454 0x800: zone context or heap, self pointer at the end), a
    // first ClientThink, the local player's streaming loaded around the spawn, ClientEndFrame.
    // Frame: S = ((entry - 4) & ~15) - 0xa0: angles / FLT_MAX vec +0x10, flags +0x20, client
    // +0x28, origin +0x2c, persistant copy +0x38.
    const uint32_t A = (entry - 4) & ~15u;
    stack_at(entry - 4) = ebp;
    stack_at(A - 0x9c) = esi;
    stack_at(A - 0xa0) = edi;
    const uint32_t S = A - 0xa0;
    uint8_t* const ent = reinterpret_cast<uint8_t*>(uintptr_t{ebx});
    const uint32_t self = ebx;
    uint8_t* client = pointer_at(ent, 0x23c);
    const uint32_t client_w = address_of(client);
    stack_at(S + 0x28) = client_w;
    stack_at(S + 0x24) = client_w + 0x520;
    word_at(client, 0x520) = 2;
    call_at(S, 0x00272832, own_address(&G_FreeEntityPhysics), TitleRegs{self, self, origin, 0, origin, angles}, 0,
            nullptr);  // G_FreeEntityPhysics
    for (uint32_t i = 0; i < 3; ++i) stack_at(S + 0x2c + i * 4) = stack_at(origin + i * 4);
    const uint32_t flags = ~word_at(pointer_at(ent, 0x23c), 0xc0) & 8;
    stack_at(S + 0x10) = stack_at(angles);
    stack_at(S + 0x14) = stack_at(angles + 4);
    stack_at(S + 0x20) = flags;
    for (uint32_t i = 0; i < 0x19; ++i) stack_at(S + 0x38 + i * 4) = word_at(client, 0x520 + i * 4);
    const uint32_t saved = word_at(client, 0x98);
    stack_at(S + 0x24) = saved;
    for (uint32_t i = 0; i < 0x190; ++i) word_at(client, i * 4) = 0;
    word_at(client, 0x98) = saved;
    for (uint32_t i = 0; i < 0x19; ++i) word_at(client, 0x520 + i * 4) = stack_at(S + 0x38 + i * 4);
    word_at(client, 0x7c) = word_at(ent, 0x224);
    word_at(client, 0x108) = word_at(client, 0x580);
    word_at(client, 0xc0) = flags | 0x10;
    word_at(ent, 0xd4) |= 0x200;
    stack_at(S + 0x18) = stack_at(angles + 8);
    if (ent == pointer_at(title_global<uint8_t*>(0x0095f534), 4)) {
        const uint32_t args[3] = {0x0082c538, 0x0082e058, 1};
        call_at(S, 0x00272927, own_address(&Cvar_Set2), TitleRegs{word_at(ent, 0xd4), self, word_at(client, 0x580),
                                                     title_global<uint32_t>(0x0095f534), S + 0x9c, client_w + 0x584},
                3, args);  // Cvar_Set2 "cl_run" "1"
    } else {
        word_at(ent, 0xd4) |= 8;
    }
    word_at(ent, 0xc) = 0;
    word_at(ent, 0x29c) = 1;
    {
        const uint32_t args[1] = {0x00984bcc};
        call_at(S, 0x0027294b, own_address(&ScriptString_Assign_entry), TitleRegs{0, self, self + 0x25c, 0, self + 0x25c, 0}, 1,
                args);  // ScriptString_Assign "player"
    }
    const uint32_t rep = word_at(ent, 0x25c);
    word_at(ent, 0x260) = Com_HashKeyNoCase(0, rep ? reinterpret_cast<const char*>(uintptr_t{rep + 6}) : nullptr);
    stack_at(S - 4) = 0x0027295d;
    word_at(ent, 0x134) = 0x2000000;
    word_at(ent, 0x2a8) = 0x381c011;
    word_at(ent, 0x344) = 3;
    word_at(ent, 0x2a4) = 0xc000;
    call_at(S, 0x00272992, own_address(&G_UpdateEntityStringHashes_entry), TitleRegs{word_at(ent, 0x260), self, 0, 0, self, 0}, 0,
            nullptr);  // G_UpdateEntityStringHashes
    for (uint32_t i = 0; i < 3; ++i) word_at(ent, 0xe0 + i * 4) = title_global<uint32_t>(0x0097a8a0 + i * 4);
    for (uint32_t i = 0; i < 3; ++i) word_at(ent, 0xf0 + i * 4) = title_global<uint32_t>(0x00984d00 + i * 4);
    for (uint32_t i = 0; i < 3; ++i) word_at(client, 0x408 + i * 4) = word_at(ent, 0xe0 + i * 4);
    for (uint32_t i = 0; i < 3; ++i) word_at(client, 0x414 + i * 4) = word_at(ent, 0xf0 + i * 4);
    word_at(client, 0x420) = title_global<uint32_t>(0x00929b6c);
    word_at(client, 0x424) = title_global<uint32_t>(0x009354b4);
    word_at(client, 0x428) = title_global<uint32_t>(0x009341c4);
    word_at(client, 0x42c) = 8;
    word_at(client, 0xa8) = title_global<uint32_t>(0x009341c4);
    field<float>(client, 0x438) = title_global<float>(0x0083f124);
    field<float>(client, 0xac) = static_cast<float>(title_global<int32_t>(0x009341c4));
    const float one = title_global<float>(0x0083eb20);
    const float point4 = title_global<float>(0x0083ec44);
    field<float>(client, 0x43c) = title_global<float>(0x0083ed2c);
    field<float>(client, 0x430) = point4;
    field<float>(client, 0x434) = one;
    field<float>(client, 0x440) = title_global<float>(0x0083f08c);
    field<float>(client, 0x444) = one;
    field<float>(client, 0x448) = one;
    field<float>(client, 0x44c) = point4;
    field<float>(client, 0x478) = one;
    field<float>(client, 0x45c) = one;
    word_at(client, 0xb0) = 0;
    field<float>(client, 0xbc) = 0.0f;
    const uint32_t health = word_at(client, 0x108);
    word_at(client, 0x100) = health;
    word_at(ent, 0x350) = health;
    const uint32_t ox = stack_at(S + 0x2c), oy = stack_at(S + 0x30), oz = stack_at(S + 0x34);
    word_at(ent, 0x20) = ox;
    word_at(ent, 0x24) = oy;
    word_at(ent, 0x28) = oz;
    word_at(ent, 0x14) = 0;
    word_at(ent, 0x18) = 0;
    word_at(ent, 0x1c) = 0;
    field<float>(ent, 0x34) = 0.0f;
    field<float>(ent, 0x30) = 0.0f;
    field<float>(ent, 0x2c) = 0.0f;
    word_at(ent, 0x140) = ox;
    word_at(ent, 0x144) = oy;
    word_at(ent, 0x148) = oz;
    word_at(client, 0) = ox;
    word_at(client, 4) = oy;
    word_at(client, 8) = oz;
    word_at(client, 0x2c) = (word_at(client, 0x2c) & 0xffffffdfu) | 0x800;
    const int32_t number = static_cast<int32_t>(client_w - title_global<uint32_t>(0x00985370)) / 0x640;  // level_clients
    if (number < 0 || number >= 1) {
        const uint32_t args[3] = {1, 0x00832e34, static_cast<uint32_t>(number)};
        call_at(S, 0x00272bcc, 0x002938f0, TitleRegs{static_cast<uint32_t>(number), self, 0, 0, 0, 0}, 3,
                args);  // __break
    }
    {
        uint8_t* const to = pointer_at(ent, 0x23c) + 0x524;
        const uint8_t* const from =
            reinterpret_cast<const uint8_t*>(uintptr_t{title_global<uint32_t>(0x00927e8c) + number * 0xec0 + 0x30});
        for (uint32_t i = 0; i < 0xb; ++i) reinterpret_cast<uint32_t*>(to)[i] = reinterpret_cast<const uint32_t*>(from)[i];
    }
    call_at(S, 0x00272bf1, own_address(&SetClientViewAngle_entry),
            TitleRegs{S + 0x10, self, 0, self, title_global<uint32_t>(0x00927e8c) + number * 0xec0 + 0x30 + 0x2c,
                      address_of(pointer_at(ent, 0x23c)) + 0x524 + 0x2c},
            0,
            nullptr);  // SetClientViewAngle
    for (uint32_t i = 0; i < 4; ++i) stack_at(S + 0x10 + i * 4) = title_global<uint32_t>(0x0082eed0);  // FLT_MAX
    for (uint32_t i = 0; i < 4; ++i) word_at(ent, 0x430 + i * 4) = stack_at(S + 0x10 + i * 4);
    for (uint32_t i = 0; i < 4; ++i) stack_at(S + 0x10 + i * 4) = word_at(ent, 0x430 + i * 4) ^ title_global<uint32_t>(0x0082e0b0 + i * 4);
    for (uint32_t i = 0; i < 4; ++i) word_at(ent, 0x440 + i * 4) = stack_at(S + 0x10 + i * 4);
    const auto allocate = [&](uint32_t slot, uint32_t size, uint32_t ret_context, uint32_t ret_heap) {
        if (word_at(ent, slot) != 0) return;
        int32_t zone = field<int32_t>(ent, 0x220);
        uint8_t* const ctx = title_global<uint8_t*>(0x0095f638);
        uint32_t p = 0;
        bool heap = false;
        if (zone == -1) {
            zone = field<int32_t>(ctx, 0x34);
            heap = zone == -1;
        }
        if (!heap) {
            const uint32_t context = (zone >= 0 && zone < 0x63) ? word_at(ctx, 0x38 + static_cast<uint32_t>(zone) * 4) : 0;
            const uint32_t args[2] = {0x10, size};
            p = call_at(S, ret_context, own_address(&MemContext_Alloc_r), TitleRegs{context, self, context, 0, 0, 0}, 2,
                        args);  // MemContext_Alloc_r
        }
        if (p == 0) {
            const uint32_t args[2] = {0x10, size};
            p = call_at(S, ret_heap, 0x00042c50, TitleRegs{0, self, 0, 0, 0, 0}, 2, args);  // Heap_AllocAligned
        }
        if (p != 0) {
            uint8_t* const block = reinterpret_cast<uint8_t*>(uintptr_t{p});
            word_at(block, size - 0x10) = p;
            word_at(block, size - 0xc) = 0;
        }
        word_at(ent, slot) = p;
    };
    allocate(0x450, 0x1010, 0x00272cb5, 0x00272cc5);
    allocate(0x454, 0x810, 0x00272d22, 0x00272d32);
    call_at(S, 0x00272d56, own_address(&SV_LinkEntity_entry), TitleRegs{word_at(ent, 0x454), self, 0, 0, 0, self}, 0, nullptr);  // SV_LinkEntity
    client = reinterpret_cast<uint8_t*>(uintptr_t{stack_at(S + 0x28)});
    word_at(client, 0x30) = 100;
    word_at(client, 0x2c) |= 0x200;
    word_at(client, 0x5e8) = title_global<uint32_t>(0x0098540c);
    word_at(client, 0x59c) = 0;
    word_at(client, 0x5a8) = 0;
    field<float>(client, 0x610) = 0.0f;
    field<float>(client, 0x60c) = 0.0f;
    field<float>(client, 0x608) = 0.0f;
    field<float>(client, 0x61c) = 0.0f;
    field<float>(client, 0x618) = 0.0f;
    field<float>(client, 0x614) = 0.0f;
    word_at(client, 0x20) = title_global<uint32_t>(0x0098540c) - 100;
    word_at(pointer_at(ent, 0x23c), 0x524) = title_global<uint32_t>(0x0098540c);
    const uint32_t cw = stack_at(S + 0x28);
    const uint32_t edi_now = word_at(client, 0x2c);
    {
        const uint32_t args[1] = {word_at(ent, 0x224)};
        call_at(S, 0x00272de2, own_address(&ClientThink_entry), TitleRegs{args[0], self, 0, 0, cw, edi_now}, 1, args);  // ClientThink
    }
    call_at(S, 0x00272dee, own_address(&BG_PlayerStateToEntityState), TitleRegs{self, self, cw, 0, cw, edi_now}, 0,
            nullptr);  // BG_PlayerStateToEntityState
    for (uint32_t i = 0; i < 3; ++i) word_at(ent, 0x140 + i * 4) = word_at(pointer_at(ent, 0x23c), i * 4);
    call_at(S, 0x00272e21, own_address(&SV_LinkEntity_entry), TitleRegs{word_at(ent, 0x148), self, 0, 0, cw, self}, 0, nullptr);  // SV_LinkEntity
    uint32_t edi_end = self;
    if (ent == pointer_at(title_global<uint8_t*>(0x0095f534), 4)) {
        const uint32_t args[4] = {title_global<uint32_t>(0x0095f640), word_at(ent, 0x458), self + 0x140, 1};
        call_at(S, 0x00272e47, own_address(&Stream_SetViewPosition), TitleRegs{args[0], self, title_global<uint32_t>(0x0095f534), args[1], cw, self},
                4, args);  // Stream_SetViewPosition
        uint8_t* const system = title_global<uint8_t*>(0x0095f638);
        const uint32_t sys = address_of(system);
        stack_at(S + 0x20) = sys;
        system[0x14] = 0;
        system[0x360] = 0;
        do {
            call_at(S, 0x00272e69, own_address(&Stream_Update), TitleRegs{stack_at(S + 0x20), self, 0, 0, cw, sys + 0x14}, 0,
                    nullptr);  // Stream_Update
        } while (system[0x14] == 0);
        edi_end = sys + 0x14;
    }
    {
        const uint32_t args[1] = {title_global<uint32_t>(0x008dda9c)};
        call_at(S, 0x00272e7c, own_address(&ClientEndFrame), TitleRegs{self, self, args[0], 0, cw, edi_end}, 1, args);  // ClientEndFrame
    }
    call_at(S, 0x00272e88, own_address(&BG_PlayerStateToEntityState), TitleRegs{self, self, cw, 0, cw, edi_end}, 0,
            nullptr);  // BG_PlayerStateToEntityState
}

ENT_EAX_FRAME_ENTRY(G_InitGame)

uint32_t XB_CDECL G_InitGame(uint32_t entry, uint8_t* unused_eax, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // cdecl (random seed, restart, savegame): Q3's G_InitGame. On a restart the entities are freed and the world and
    // player entities allocated again, the level main reruns, the zones and streaming are reset around the
    // checkpoint's (0x0095f4e0) or the loader's view position (two loading frames, then Stream_Update until the
    // load is done), path node runtime state, effects and rumble cleared; else the level script module loads.
    // Then the level locals (three bytes kept), skill cvars, weapons, vehicle files, anim trees, clients,
    // sentients, actors, location damage tables, bad places, the vehicle pool, the entities spawned, actors
    // dropped to the floor, vehicle links and teams. Returns 0.
    // Frame: push ebp; and esp,-16; sub esp,0x94; EBX, ESI, EDI saved (top); vector +0x20, quad +0x38,
    // out word +0x18, restored-checkpoint flag +0x1c, kept bytes +0x16/+0x17.
    (void)unused_eax;
    stack_at(entry - 4) = ebp;
    const uint32_t base = (entry - 4) & ~15u;
    stack_at(base - 0x98) = ebx;
    stack_at(base - 0x9c) = esi;
    stack_at(base - 0xa0) = edi;
    const uint32_t T = base - 0xa0;
    const uint32_t seed = stack_at(entry + 4), restart = stack_at(entry + 8), savegame = stack_at(entry + 0xc);
    auto call0 = [&](uint32_t ret, uint32_t function, const TitleRegs& regs) {
        return call_at(T, ret, function, regs, 0, nullptr);
    };
    auto byte_at = [](uint32_t address) -> uint8_t& { return *reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(address)); };
    if (restart) {
        const uint32_t state = title_global<uint32_t>(0x0095f534);
        const uint32_t one[1] = {state};
        call_at(T, 0x002737b5, own_address(&G_FreeAllEntities), TitleRegs{state, ebx, 0, 0, esi, edi}, 1, one);  // G_FreeAllEntities
        edi = title_global<uint32_t>(0x0095f534);
        call0(0x002737c0, own_address(&G_AllocWorldEntity_entry), TitleRegs{0, ebx, 0, 0, esi, edi});  // G_AllocWorldEntity (EDI)
        edi = title_global<uint32_t>(0x0095f534);
        call0(0x002737cb, own_address(&G_AllocPlayerEntity_entry), TitleRegs{0, ebx, 0, 0, esi, edi});  // G_AllocPlayerEntity (EDI)
        const uint32_t main = title_global<uint32_t>(0x00927090);  // g_levelScriptMain
        if (main) call0(0x002737d6, main, TitleRegs{main, ebx, 0, 0, esi, edi});
        edi = title_global<uint32_t>(0x0095f638);
        esi = edi;
        const uint32_t both[1] = {1};
        call_at(T, 0x002737e5, own_address(&Pak_ResetZoneDistances_entry), TitleRegs{0, ebx, 0, 0, esi, edi}, 1, both);  // Pak_ResetZoneDistances
        esi = title_global<uint32_t>(0x0095f4f8);  // scn_zoneScenes
        call0(0x002737f0, own_address(&Scn_ResetZoneFlags_entry), TitleRegs{0, ebx, 0, 0, esi, edi});  // Scn_ResetZoneFlags (ESI)
        const uint32_t id[1] = {stack_at(edi + 0x34)};
        const uint32_t context =
            call_at(T, 0x002737fb, own_address(&MemContext_FindById), TitleRegs{id[0], ebx, edi, 0, esi, edi}, 1, id);  // MemContext_FindById
        if (context) {
            stack_float(context + 0x34) = 0.0f;
            ++title_global<uint32_t>(0x008d9d38);
        }
        const uint32_t checkpoint = title_global<uint32_t>(0x0095f4e0);
        esi = title_global<uint32_t>(0x0095f640);
        const uint32_t vector = T + 0x20;
        uint32_t cell;
        if (byte_at(checkpoint + 0x390) && byte_at(checkpoint)) {
            // The checkpoint's view position {x, y, z, x}.
            const uint32_t x = stack_at(checkpoint + 0x384);
            stack_at(vector + 0xc) = x;
            stack_at(vector + 8) = stack_at(checkpoint + 0x38c);
            stack_at(vector + 4) = stack_at(checkpoint + 0x388);
            stack_at(vector) = x;
            cell = call0(0x0027386a, own_address(&CM_CellForPoint), TitleRegs{checkpoint, ebx, 0, vector, esi, edi});  // CM_CellForPoint
        } else {
            for (uint32_t i = 0; i < 0x10; i += 4) stack_at(vector + i) = stack_at(esi + 0x1a0 + i);
            cell = stack_at(esi + 0x1c0);
        }
        const uint32_t view[4] = {esi, cell, vector, 1};
        call_at(T, 0x002738a1, own_address(&Stream_SetViewPosition), TitleRegs{cell, ebx, vector, 0, esi, edi}, 4, view);  // Stream_SetViewPosition
        // Two loading frames: a cleared screen with a white quad and the loading icon.
        const uint32_t quad = T + 0x38;
        for (esi = 2; esi; --esi) {
            const TitleRegs regs{0, ebx, 0, 0, esi, edi};
            call0(0x002738ab, own_address(&nglResetDeviceBindings), regs);  // nglResetDeviceBindings
            call0(0x002738b0, own_address(&nglBeginFrame), regs);  // nglBeginFrame
            const uint32_t flags[1] = {0xf3};
            call_at(T, 0x002738ba, own_address(&nglSetClearFlags), regs, 1, flags);  // nglSetClearFlags
            const uint32_t q[1] = {quad};
            call_at(T - 4, 0x002738c4, own_address(&nglInitQuad), TitleRegs{0, ebx, 0, quad, esi, edi}, 1, q);  // nglInitQuad
            const uint32_t colour[2] = {quad, 0xffffffffu};
            call_at(T - 8, 0x002738d0, own_address(&nglSetQuadColor), TitleRegs{quad, ebx, 0, 0, esi, edi}, 2, colour);  // nglSetQuadColor
            call_at(T - 0x10, 0x002738da, own_address(&nglListAddQuad), TitleRegs{0, ebx, quad, 0, esi, edi}, 1, q);  // nglListAddQuad
            const uint32_t full[1] = {0};
            call_at(T - 0x14, 0x002738e1, own_address(&SCR_UpdateLoadingIcon), regs, 1, full);  // SCR_UpdateLoadingIcon
            call0(0x002738e9, own_address(&nglEndFrame), regs);  // nglEndFrame
        }
        // Stream until the load is done, cls_state 1 meanwhile (EBX keeps the old one).
        const uint32_t system = title_global<uint32_t>(0x0095f638);
        ebx = title_global<uint32_t>(0x0092bdf0);  // cls_state
        title_global<uint32_t>(0x0092bdf0) = 1;
        esi = system + 0x14;
        stack_at(system + 0x35c) = 0x00299780;
        edi = system;
        byte_at(esi) = 0;
        byte_at(system + 0x360) = 0;
        do {
            call0(0x00273927, own_address(&Stream_Update), TitleRegs{edi, ebx, 0, 0, esi, edi});  // Stream_Update (EAX)
        } while (!byte_at(esi));
        edi = 0;
        stack_at(title_global<uint32_t>(0x0095f638) + 0x35c) = 0;
        title_global<uint32_t>(0x0092bdf0) = ebx;
        const uint32_t blocks[1] = {title_global<uint32_t>(0x0095f4f4)};  // g_pathNodeBlocks
        call_at(T, 0x0027394b, own_address(&PathNode_ResetAllRuntimeState), TitleRegs{blocks[0], ebx, 0, 0, esi, edi}, 1,
                blocks);  // PathNode_ResetAllRuntimeState
        const uint32_t hud = title_global<uint32_t>(0x0095f8f0);  // g_hudManager
        const uint32_t panel = stack_at(hud + 4);
        esi = title_global<uint32_t>(0x0095f5b4);
        *reinterpret_cast<uint16_t*>(static_cast<uintptr_t>(panel + 0x13d0)) = 0;
        *reinterpret_cast<uint16_t*>(static_cast<uintptr_t>(panel + 0x13d2)) = 0;
        call0(0x0027396d, own_address(&FX_FreeAllEffects_entry), TitleRegs{panel, ebx, hud, 0, esi, edi});  // FX_FreeAllEffects (ESI)
        const uint32_t pad = call0(0x00273972, own_address(&IN_PadCreate), TitleRegs{0, ebx, 0, 0, esi, edi});  // IN_PadCreate
        call0(0x00273979, own_address(&IN_PadStopAllRumble), TitleRegs{pad, ebx, pad, 0, esi, edi});                 // IN_PadStopAllRumble
        for (uint32_t i = 0; i < 0x7c0; i += 0x7c) title_global<uint32_t>(0x0094cc88 + i) = 0;
        const TitleRegs regs{0, ebx, 0, 0, esi, edi};
        call0(0x00273995, own_address(&FX_InitEffectPool), regs);  // FX_InitEffectPool
        call0(0x0027399a, own_address(&FX_ClearEffects), regs);  // FX_ClearEffects
        call0(0x0027399f, own_address(&FX_ResetParticleSystem), regs);  // FX_ResetParticleSystem
        call0(0x002739a4, own_address(&FX_FreeAllEffectLights), regs);  // FX_FreeAllEffectLights
    } else {
        call0(0x002739ab, own_address(&G_LoadLevelScriptModule_entry), TitleRegs{0, ebx, 0, 0, esi, edi});  // G_LoadLevelScriptModule
    }
    // A restored checkpoint copies its saved map (+0x350) into +0x56c.
    {
        const uint32_t checkpoint = title_global<uint32_t>(0x0095f4e0);
        if (byte_at(checkpoint) && byte_at(checkpoint + 0x390)) {
            const uint32_t source[1] = {checkpoint + 0x350};
            call_at(T, 0x002739d4, own_address(&HashMap_CopyFrom_7), TitleRegs{source[0], ebx, checkpoint + 0x56c, 0, esi, edi}, 1,
                    source);  // HashMap_CopyFrom_7
        }
    }
    uint32_t& reloaded = stack_at(T + 0x1c);
    reloaded = restart && savegame ? 1 : 0;
    call0(0x002739f7, own_address(&Swap_Init), TitleRegs{0, ebx, 0, 0, esi, edi});  // Swap_Init
    // Three bytes of the level locals survive their clear; 0x00986018 is 1 until the end.
    const uint8_t kept14 = title_global<uint8_t>(0x00986014);
    const uint8_t kept16 = title_global<uint8_t>(0x00986016);
    const uint8_t kept15 = title_global<uint8_t>(0x00986015);
    byte_at(T + 0x17) = kept14;
    byte_at(T + 0x16) = kept16;
    ebx = (ebx & ~0xffu) | kept15;
    esi = 0x00985370;
    call0(0x00273a1a, own_address(&G_ClearLevelLocals_entry), TitleRegs{0, ebx, 0, 0, esi, edi});  // G_ClearLevelLocals (ESI)
    title_global<uint8_t>(0x00986015) = kept15;
    ebx = 0;
    title_global<uint32_t>(0x00986018) = 1;
    title_global<uint8_t>(0x00986014) = kept14;
    title_global<uint8_t>(0x00986016) = kept16;
    if (savegame) reloaded = 0;
    esi = restart;
    if (!restart) {
        std::memset(&title_global<uint32_t>(0x00987a88), 0, 0x240 * 4);
        edi = 0x00988388;  // REP STOSD's end
    }
    const uint32_t seed_arg[1] = {seed};
    call_at(T, 0x00273a69, 0x001efcb3, TitleRegs{seed, ebx, 0, 0, esi, edi}, 1, seed_arg);  // srand
    const uint32_t ms = call_at(T - 4, 0x00273a6e, own_address(&Sys_Milliseconds), TitleRegs{0, ebx, 0, 0, esi, edi}, 0,
                                nullptr);  // Sys_Milliseconds
    const uint32_t restore[1] = {esi};
    call_at(T - 4, 0x00273a79, own_address(&G_InitSkillCvars), TitleRegs{ms, ebx, 0, 0, esi, edi}, 1, restore);  // G_InitSkillCvars
    title_global<uint32_t>(0x008d5a70) = ms;
    const float scale = title_global<float>(0x00950638);
    const float half = scale * title_global<float>(0x0083eb24);    // 0.5
    const float neg_half = scale * title_global<float>(0x0083eb28);  // -0.5
    title_global<float>(0x00984d04) = half;
    title_global<float>(0x00984d00) = half;
    title_global<uint32_t>(0x00927314) = 0;  // g_freezeControls
    title_global<uint32_t>(0x0098ab08) = 0;
    title_global<float>(0x0097a8a4) = neg_half;
    title_global<float>(0x0097a8a0) = neg_half;
    title_global<float>(0x00984d08) = title_global<float>(0x00950880);
    call0(0x00273ad8, own_address(&BG_InitWeapons), TitleRegs{0, ebx, 0, 0, esi, edi});  // BG_InitWeapons
    {
        const uint32_t hud = title_global<uint32_t>(0x0095f8f0);
        const uint32_t icons = stack_at(hud + 0x18);
        if (icons) {
            const uint32_t pictures[1] = {stack_at(hud + 0x44)};
            call_at(T, 0x00273aee, own_address(&Hud_LoadWeaponIcons), TitleRegs{icons, ebx, pictures[0], 0, esi, edi}, 1,
                    pictures);  // Hud_LoadWeaponIcons
        }
    }
    title_global<uint16_t>(0x0095e228) = 0;  // g_vehicleDefCount
    // Every "VEHICLEFILE" asset of the zone set (+0x34): callback 0x0024df70. The last pushed word is the out
    // word (+0x18) with only its low byte set.
    auto for_each_asset = [&](uint32_t ret, uint32_t prefix, uint32_t callback, uint32_t esi_value, uint32_t edi_value) {
        byte_at(T + 0x18) = 0;
        const uint32_t cfg = title_global<uint32_t>(0x0095f5d4);  // rm_cfgstr
        const uint32_t set = stack_at(title_global<uint32_t>(0x0095f638) + 0x34);
        const uint32_t args[6] = {cfg, T + 0x18, prefix, 11, callback, stack_at(T + 0x18)};
        call_at(T, ret, own_address(&Stream_ForEachAssetWithPrefix), TitleRegs{cfg, ebx, T - 0x10, set, esi_value, edi_value}, 6,
                args);  // Stream_ForEachAssetWithPrefix
    };
    for_each_asset(0x00273b44, 0x0083a00c, 0x0024df70, 0x0083a00d, 0x0024df70);
    esi = 1;
    edi = 0x0024df70;
    const int32_t clients = title_global<int32_t>(0x00950c5c);
    title_global<int32_t>(0x00985418) = clients < 1 ? 1 : clients > 0x10 ? 0x10 : clients;
    if (!reloaded) {
        const uint32_t reload[1] = {restart ? 1u : 0u};
        call_at(T, 0x00273b80, own_address(&G_InitAnimTrees), TitleRegs{0, ebx, 0, reload[0], esi, edi}, 1, reload);  // G_InitAnimTrees
        for (uint32_t a = 0x009881c8; a < 0x00988388; a += 0x1c) title_global<uint32_t>(a) = 0;  // g_actorCorpses
    }
    call0(0x00273ba1, own_address(&GScr_LoadConsts), TitleRegs{0, ebx, 0, 0, esi, edi});  // GScr_LoadConsts
    title_global<uint32_t>(0x00985404) = esi;  // level_maxclients = 1
    esi = 0x00984d10;
    call0(0x00273bb1, own_address(&G_ClearClient_entry), TitleRegs{0, ebx, 0, 0, esi, edi});  // G_ClearClient (ESI)
    title_global<uint32_t>(0x00985370) = esi;  // level_clients
    title_global<uint32_t>(0x00985378) = 0x00983e48;  // g_sentients
    call0(0x00273bc6, own_address(&Sentient_ClearAll), TitleRegs{0, ebx, 0, 0, esi, edi});  // Sentient_ClearAll
    call0(0x00273bcb, own_address(&Actor_ClearAll), TitleRegs{0, ebx, 0, 0, esi, edi});  // Actor_ClearAll
    {
        const uint32_t ents = title_global<uint32_t>(0x0095f534) + 4;
        for (int32_t i = 0; i < title_global<int32_t>(0x00985404); ++i)
            stack_at(stack_at(ents + i * 4) + 0x23c) = title_global<uint32_t>(0x00985370) + i * 0x640;
    }
    if (!restart) {
        for_each_asset(0x00273c54, 0x0083b9ac, 0x0024e980, 0x0024e980, 0x0083b9ad);  // "LOCDMGTABLE"
        std::memset(&title_global<uint32_t>(0x00950230), 0, 0x100 * 4);
        title_global<uint32_t>(0x00950230) = 1;
    }
    esi = 1;
    title_global<uint32_t>(0x00929c34) = 0;
    std::memset(&title_global<uint32_t>(0x0098a570), 0, 0x140 * 4);  // g_badplaces
    edi = 0x0098aa70;
    title_global<uint32_t>(0x0094d4d8) = 0;  // g_numVehNodes
    call0(0x00273c93, own_address(&Vehicle_ResetPool), TitleRegs{0, ebx, 0, 0, esi, edi});  // Vehicle_ResetPool
    for (uint32_t a = 0x0094d8e8; a < 0x00950228; a += 0x84) title_global<uint8_t>(a) = 0;
    title_global<uint32_t>(0x00985380) = 0x0094d8e8;
    title_global<uint32_t>(0x00985fe0) = (savegame ? 1u : 0u) + 1;  // level_spawning
    const uint32_t slots[1] = {0x20};
    call_at(T, 0x00273cd1, own_address(&G_SetupVehiclePool), TitleRegs{0, ebx, 0, 0, esi, edi}, 1, slots);  // G_SetupVehiclePool
    const uint32_t scenes[1] = {title_global<uint32_t>(0x0095f4f8)};
    call_at(T, 0x00273cdf, own_address(&G_SpawnEntitiesFromString_entry), TitleRegs{scenes[0], ebx, 0, 0, esi, edi}, 1,
            scenes);  // G_SpawnEntitiesFromString
    title_global<uint32_t>(0x00985fe0) = 0;
    call0(0x00273cea, own_address(&G_DropActorsToFloorAtInit_entry), TitleRegs{0, ebx, 0, 0, esi, edi});  // G_DropActorsToFloorAtInit
    const uint32_t linked = call0(0x00273cef, own_address(&VehNode_LinkAll), TitleRegs{0, ebx, 0, 0, esi, edi});  // VehNode_LinkAll
    call0(0x00273cf4, own_address(&Vehicle_InitBrushModels_entry), TitleRegs{linked, ebx, 0, 0, esi, edi});  // Vehicle_InitBrushModels
    call0(0x00273cf9, own_address(&G_FindTeams), TitleRegs{0, ebx, 0, 0, esi, edi});  // G_FindTeams
    title_global<uint32_t>(0x00986004) = esi;
    title_global<uint32_t>(0x00985414) = title_global<uint32_t>(0x0098540c);  // level_time
    call0(0x00273d10, own_address(&SaveRegisteredItems), TitleRegs{0, ebx, 0, 0, esi, edi});  // SaveRegisteredItems
    title_global<uint32_t>(0x00986008) = esi;  // g_compassMarkersEnabled
    title_global<uint32_t>(0x00985fcc) = title_global<uint32_t>(0x0082eed0);  // FLT_MAX
    title_global<uint32_t>(0x00985fd0) = title_global<uint32_t>(0x0082eed0);
    for (uint32_t i = 0; i < 0x10; ++i) {
        const uint32_t args[2] = {i + 0x10, 0};
        call_at(T, 0x00273d3a, own_address(&SV_SetConfigstring), TitleRegs{0, ebx, 0, i + 0x10, i, edi}, 2, args);  // SV_SetConfigstring
    }
    title_global<uint32_t>(0x00986018) = 0;
    title_global<float>(0x00985ffc) = 0.0f;  // g_missileMaxLifetime
    stack_float(title_global<uint32_t>(0x0095f4e0) + 0x34c) = 0.0f;
    return 0;
}

// Every title callee runs where the original's call puts it (the sight traces read the unset fourth words of the
// eye vectors: the squared-distance vectors stored there before).
PLATFORM_ASM_ENTRY(Turret_UpdateTarget_entry,
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
                   "pushl %ebp\n" PLATFORM_ASM_CALL(Turret_UpdateTarget) "addl $28, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret")

uint32_t XB_CDECL Turret_UpdateTarget(uint32_t entry, uint8_t* turret, uint8_t* actor, uint32_t ebx, uint32_t esi,
                                      uint32_t edi, uint32_t ebp) {
    // An actor's turret (cdecl turret, actor): inactive (info +2 bit 0x20 clear) it turns to its rest angles
    // (info +0x3c/+0x40). Else the target sentient (the actor's enemy, or info +0x4c with bit 0x10) has its
    // memory (actor +0x850, 0x50 per sentient) kept for 2 s; in range and seen, the turret aims at it; once a
    // second (actor +0x24) a target in front, seen within 5 s near where it was seen and in sight (trace
    // 0x801003 eye to eye) becomes info +0x4c, which is checked the same way (cleared when it fails). Then the
    // suppression target (info +0xc: its sentient's last known spot, or info +0x18 high with bit 0x40) and the
    // entity target (info +8: seen point or its sentient) are tried; with nothing to aim at the turret goes
    // back to its idle state (Scr_Notify "turretstatechange" once) and jitters. Returns 0 only when the turret
    // turned toward a far target it could not reach yet, else 1.
    // Frame: base - 0x80 (EDI, ESI, EBX); info +0x18, memory +0x1c, target +0x20, entity target +0x24, ahead
    // +0x28, once-a-second flag / forward / trace result +0x2c, squared distance +0x38, delta / eye +0x40,
    // boxes +0x50/+0x60, eye / squared vector +0x70.
    stack_at(entry - 4) = ebp;
    const uint32_t F = ((entry - 4) & ~15u) - 0x80;
    stack_at(F + 8) = ebx;
    stack_at(F + 4) = esi;
    stack_at(F) = edi;
    const uint32_t ent = address_of(turret), act = address_of(actor);
    const uint32_t info = stack_at(ent + 0x24c);
    stack_at(F + 0x18) = info;
    auto byte_at = [](uint32_t address) -> uint8_t& { return *reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(address)); };
    auto memory_of = [&](uint32_t sentient) {
        const int32_t index = static_cast<int32_t>(sentient - title_global<uint32_t>(0x00985378)) / 0x60;  // g_sentients
        return act + 0x850 + static_cast<uint32_t>(index) * 0x50;
    };
    // (a - b) squared per lane, lane 0 the sum of the first three (MOVAPS of the whole register).
    auto squared = [&](uint32_t a, uint32_t b, uint32_t out) {
        float lane[4];
        for (uint32_t i = 0; i < 4; ++i) {
            const float d = stack_float(a + i * 4) - stack_float(b + i * 4);
            lane[i] = d * d;
        }
        lane[0] = lane[0] + lane[1];
        lane[0] = lane[0] + lane[2];
        for (uint32_t i = 0; i < 4; ++i) stack_float(out + i * 4) = lane[i];
    };
    // Eyes of the actor (+0x70) and the target (+0x40), a zero box, the trace: result word +0x2c.
    auto sight_trace = [&](uint32_t eye_ret, uint32_t target_eye_ret, uint32_t trace_ret, uint32_t target,
                           uint32_t mins, uint32_t maxs, uint32_t edi_second) {
        call_at(F, eye_ret, own_address(&Sentient_EyeOrigin_2_entry), TitleRegs{stack_at(act + 4), ebx, 0, 0, F + 0x70, target}, 0,
                nullptr);  // Sentient_EyeOrigin_2
        call_at(F, target_eye_ret, own_address(&Sentient_EyeOrigin_2_entry), TitleRegs{target, ebx, 0, 0, F + 0x40, edi_second}, 0,
                nullptr);  // Sentient_EyeOrigin_2
        for (uint32_t i = 0; i < 0x10; i += 4) {
            stack_at(F + 0x50 + i) = title_global<uint32_t>(0x0082e290 + i);  // {0, 0, 0, 1}
            stack_at(F + 0x60 + i) = title_global<uint32_t>(0x0082e290 + i);
        }
        stack_at(F + 0x2c) = 0;
        const uint32_t actor_handle = stack_at(stack_at(act) + 0x224);
        const uint32_t target_handle = stack_at(stack_at(target) + 0x224);
        const uint32_t args[6] = {F + 0x2c, mins, actor_handle, target_handle, 0x00801003, 0};
        const TitleRegs regs{F + 0x40, maxs, actor_handle, mins, title_global<uint32_t>(0x0082e29c), F + 0x70};
        call_at(F, trace_ret, own_address(&G_SightTrace_entry), regs, 6, args);  // G_SightTrace
        return stack_at(F + 0x2c);
    };
    const uint32_t flags = *reinterpret_cast<uint16_t*>(static_cast<uintptr_t>(info + 2));
    if (!(flags & 0x20)) {
        stack_at(F + 0x38) = stack_at(info + 0x3c);
        stack_at(F + 0x3c) = stack_at(info + 0x40);
        const uint32_t args[2] = {ent, F + 0x38};
        call_at(F, 0x0026c594, own_address(&Turret_UpdateTargetAngles_entry), TitleRegs{F + 0x38, ebx, ent, 0, info, edi}, 2,
                args);  // Turret_UpdateTargetAngles
        return 1;
    }
    ebx = act;
    uint32_t target = stack_at(stack_at(act + 4) + 0x3c);
    stack_at(F + 0x20) = target;
    if ((flags & 0x10) && stack_at(info + 0x4c)) {
        target = stack_at(info + 0x4c);
        stack_at(F + 0x20) = target;
    }
    stack_at(F + 0x24) = stack_at(info + 8);
    uint32_t time = title_global<uint32_t>(0x0098540c);  // level_time
    stack_at(F + 0x2c) = static_cast<int32_t>(time - stack_at(act + 0x24)) >= 1000 ? 1 : 0;
    uint32_t memory, current, m, sentient, aim, entity;
    esi = info;
    if (!target) {
        stack_at(F + 0x1c) = 0;
        if (const uint32_t held = stack_at(info + 0x4c))
            if (static_cast<int32_t>(stack_at(memory_of(held) + 0x1c)) <= static_cast<int32_t>(time))
                stack_at(info + 0x4c) = 0;
        const uint32_t args[1] = {act};
        call_at(F, 0x0026c627, own_address(&Actor_ClearSentientThreatTimes), TitleRegs{0, ebx, 0, 0, esi, edi}, 1, args);  // Actor_ClearSentientThreatTimes
        goto suppression;
    }
    {
        memory = memory_of(target);
        stack_at(memory + 0x1c) = time + 2000;
        const uint32_t te = stack_at(target);
        const float dy = stack_float(te + 0x144) - stack_float(ent + 0x144);
        const float dz = stack_float(te + 0x148) - stack_float(ent + 0x148);
        const float dx = stack_float(te + 0x140) - stack_float(ent + 0x140);
        stack_float(F + 0x48) = dz;
        stack_float(F + 0x44) = dy;
        float d2 = dz * dz;
        d2 = d2 + dy * dy;
        d2 = d2 + dx * dx;
        stack_at(F + 0x1c) = memory;
        stack_float(F + 0x40) = dx;
        stack_float(F + 0x38) = d2;
        if (stack_at(target + 0x30) || !(stack_float(info + 0x48) > d2)) {
            stack_at(F + 0x1c) = 0;
            goto entity_check;
        }
        if (byte_at(memory)) {
            const uint32_t args[1] = {target};
            const uint32_t aimed = call_at(F, 0x0026c6f8, own_address(&Turret_AimAtTarget_entry),
                                           TitleRegs{info, ebx, stack_at(info + 0x44), 1, memory, ent}, 1,
                                           args);  // Turret_AimAtTarget
            if (aimed) {
                byte_at(info + 2) &= 0xef;
                stack_at(memory + 0x1c) = 0;
                return 1;
            }
        }
        if (!stack_at(F + 0x2c)) goto entity_check;
        call_at(F, 0x0026c738, own_address(&AnglesToForward_entry), TitleRegs{stack_at(F + 0x2c), ebx, target, 0, F + 0x2c, ent + 0x150}, 0,
                nullptr);  // AnglesToForward
        float dot = stack_float(F + 0x34) * stack_float(F + 0x48);
        dot = dot + stack_float(F + 0x30) * stack_float(F + 0x44);
        dot = dot + stack_float(F + 0x2c) * stack_float(F + 0x40);
        stack_at(F + 0x28) = dot >= title_global<float>(0x0083eafc) ? 1 : 0;
        time = title_global<uint32_t>(0x0098540c);
        if (const uint32_t held = stack_at(info + 0x4c)) {
            if (held == target) goto check_held;
            if (static_cast<int32_t>(stack_at(memory_of(held) + 0x1c)) > static_cast<int32_t>(time)) goto check_held;
        }
        if (!byte_at(memory) && stack_at(F + 0x28)) goto check_held;
        if (static_cast<int32_t>(time - stack_at(memory + 0x18)) >= 5000) goto check_held;
        squared(stack_at(target) + 0x140, memory + 0x20, F + 0x70);
        if (!(title_global<float>(0x0083ed4c) > stack_float(F + 0x70))) goto check_held;  // 64 units
        if (sight_trace(0x0026c83b, 0x0026c846, 0x0026c8bf, target, F + 0x50, F + 0x60, target)) {
            ebx = act;
            goto check_held;
        }
        stack_at(info + 0x4c) = target;
        if (title_global<float>(0x0083eb40) > stack_float(F + 0x38)) stack_at(stack_at(F + 0x1c) + 0x1c) = 0;
        ebx = act;
        goto entity_check;
    }
check_held:
    // The held target (info +0x4c): seen within 5 s, near where it was seen and in sight, else dropped.
    current = stack_at(info + 0x4c);
    if (!current) goto entity_check;
    m = memory_of(current);
    if (static_cast<int32_t>(time - stack_at(m + 0x18)) >= 5000) {
        stack_at(info + 0x4c) = 0;
        goto entity_check;
    }
    squared(stack_at(current) + 0x140, m + 0x20, F + 0x60);
    if (!(title_global<float>(0x0083ed4c) > stack_float(F + 0x60))) {
        stack_at(info + 0x4c) = 0;
        goto entity_check;
    }
    if (sight_trace(0x0026ca28, 0x0026ca38, 0x0026cab4, current, F + 0x60, F + 0x50, info)) {
        stack_at(info + 0x4c) = 0;
        ebx = act;
        goto entity_check;
    }
    if (stack_at(info + 0x4c) != stack_at(F + 0x20)) {
        ebx = act;
        goto entity_check;
    }
    if ((byte_at(info + 2) & 0x10) || !stack_at(F + 0x28)) {
        stack_at(stack_at(F + 0x1c) + 0x1c) = 0;
        return 0;
    }
    {
        stack_float(F + 0x2c) = 0.0f;
        stack_float(F + 0x30) = 0.0f;
        const uint32_t args[2] = {ent, F + 0x2c};
        const uint32_t done = call_at(F, 0x0026cb12, own_address(&Turret_UpdateTargetAngles_entry), TitleRegs{ent, ebx, 0, 1, esi, edi}, 2,
                                      args);  // Turret_UpdateTargetAngles
        if (done) {
            stack_at(stack_at(F + 0x1c) + 0x1c) = 0;
            return 0;
        }
        if (title_global<float>(0x0083eb40) > stack_float(F + 0x38)) stack_at(stack_at(F + 0x1c) + 0x1c) = 0;
        return 1;
    }
entity_check:
    esi = info;
    if (stack_at(F + 0x24) == stack_at(stack_at(F + 0x20))) stack_at(F + 0x24) = 0;
suppression:
    // The suppression target (info +0xc's sentient): its last known spot, 32 up (or info +0x18 with bit 0x40).
    sentient = stack_at(info + 0xc) ? stack_at(stack_at(info + 0xc) + 0x244) : 0;
    if (stack_at(info + 0xc)) stack_at(F + 0x38) = sentient;
    if (sentient) {
        m = memory_of(sentient);
        stack_at(F + 0x1c) = m;
        if (stack_at(sentient + 0x30) ||
            static_cast<int32_t>(title_global<uint32_t>(0x0098540c) - stack_at(m + 0x18)) >= 5000) {
            stack_at(m + 0x18) = 0;
            esi = info;
            goto entity;
        }
        const float z = (byte_at(info + 2) & 0x40) ? stack_float(info + 0x18)
                                                    : stack_float(m + 0x28) + title_global<float>(0x0083eeac);
        const uint32_t x = stack_at(m + 0x20);
        stack_float(F + 0x68) = z;
        stack_at(F + 0x6c) = x;
        stack_at(F + 0x64) = stack_at(m + 0x24);
        stack_at(F + 0x60) = x;
        for (uint32_t i = 0; i < 0x10; i += 4) stack_at(F + 0x40 + i) = stack_at(F + 0x60 + i);
        const uint32_t args[2] = {F + 0x40, 1};
        aim = call_at(F, 0x0026cbcc, own_address(&Turret_TryAimAtTarget_entry), TitleRegs{ent, ebx, 0, F + 0x40, m, F + 0x2c}, 2,
                      args);  // Turret_TryAimAtTarget
        if (aim) {
            const uint32_t turn[2] = {ent, F + 0x2c};
            call_at(F, 0x0026cbe4, own_address(&Turret_UpdateTargetAngles_entry), TitleRegs{F + 0x2c, ebx, ent, 1, m, F + 0x2c}, 2,
                    turn);  // Turret_UpdateTargetAngles
            stack_at(m + 0x1c) = 0;
            if (stack_at(info + 0x4c) == stack_at(F + 0x38)) stack_at(info + 0x4c) = 0;
            return 1;
        }
        esi = info;
    } else if (stack_at(F + 0x1c)) {
        stack_at(stack_at(F + 0x1c) + 0x18) = 0;
    }
entity:
    // The entity target (info +8): in range, either a plain entity the actor can see (Turret_TrackTargetEnt)
    // or a seen sentient (Turret_AimAtTarget).
    entity = stack_at(F + 0x24);
    if (entity) {
        squared(entity + 0x140, ent + 0x140, F + 0x70);
        if (stack_float(info + 0x48) > stack_float(F + 0x70)) {
            const uint32_t own = stack_at(entity + 0x244);
            if (!own) {
                const uint32_t args[4] = {entity + 0x140, stack_at(act + 0x828), stack_at(act + 0x82c), 0};
                const TitleRegs regs{act, ebx, stack_at(act + 0x82c), stack_at(act + 0x828), esi, own};
                const uint32_t bits = actor_call_at_xmm0_raw(F, own_address(&Actor_CanSeePoint_entry), &regs, 4, args);  // Actor_CanSeePoint
                stack_at(F - 0x14) = 0x0026cc92;
                float seen;
                std::memcpy(&seen, &bits, 4);
                if (seen != title_global<float>(0x0083eafc)) {
                    const uint32_t one[1] = {1};
                    if (call_at(F, 0x0026ccad, own_address(&Turret_TrackTargetEnt), TitleRegs{ent, ebx, entity, 0, esi, own}, 1,
                                one))  // Turret_TrackTargetEnt
                        return 1;
                }
            } else if (byte_at(memory_of(own))) {
                const uint32_t args[1] = {own};
                if (call_at(F, 0x0026cd15, own_address(&Turret_AimAtTarget_entry), TitleRegs{0, ebx, stack_at(info + 0x44), 1, esi, ent}, 1,
                            args))  // Turret_AimAtTarget
                    return 1;
            }
        }
    }
    m = stack_at(F + 0x1c);
    if (!m || !byte_at(m)) {
        esi = ent;
        const uint32_t idle = stack_at(ent + 0x24c);
        byte_at(idle + 2) &= 0xb7;
        stack_at(idle + 0xc) = 0;
        const uint32_t state = stack_at(ent + 0x24c);
        if (byte_at(state + 1)) {
            const uint32_t args[1] = {title_global<uint32_t>(0x0097a860)};  // scr_const_turretstatechange
            call_at(F, 0x0026cd56, own_address(&Scr_Notify_entry), TitleRegs{0, ebx, 0, 0, ent, state}, 1, args);  // Scr_Notify
        }
        byte_at(state + 1) = 0;
    }
    call_at(F, 0x0026cd62, own_address(&Turret_UpdateAimWithJitter_entry), TitleRegs{0, ebx, 0, 0, esi, ent}, 0, nullptr);  // Turret_UpdateAimWithJitter
    return 1;
}

ENT_EAX_FRAME_ENTRY(G_RunFrame)

uint32_t XB_CDECL G_RunFrame(uint32_t entry, uint8_t* unused_eax, uint32_t ebx, uint32_t esi, uint32_t edi, uint32_t ebp) {
    // cdecl msec: Q3's G_RunFrame. Vehicle poses into physics, the game cvars updated, the mission end check, the
    // entity list changes; per listed entity its LOD and (not a client, no flag 0x10000) its DObj animation
    // (blend, advance, skeleton) and a turret's turn effects. The player's box from the stance cvars and three
    // more client values when they changed, bad places expired, the actors' previous origins (and angles for
    // +0x38 == 1), every entity run (a tag-linked entity's parents first), the physics frame, the clients' end
    // frames, the corpse queue, the actors' frame data, the codnotes, and every DObj marked dirty (+0x9c bit 0).
    // Returns the entity list count (EAX). Frame: ECX slot entry - 4 (the float frame time), EBX, ESI, EDI, EBP.
    (void)unused_eax;
    stack_at(entry - 8) = ebx;
    stack_at(entry - 0xc) = esi;
    stack_at(entry - 0x10) = edi;
    const uint32_t T0 = entry - 0x10, T = entry - 0x14;
    const uint32_t msec = stack_at(entry + 4);
    call_at(T0, 0x00274669, own_address(&Vehicle_PhysicsPreFrame), TitleRegs{0, ebx, 0, 0, esi, edi}, 0, nullptr);  // Vehicle_PhysicsPreFrame
    esi = 0x008da8e8;  // gameCvarTable (0x18 each), gameCvarTableSize 0x008dae40
    for (edi = 0; static_cast<int32_t>(edi) < title_global<int32_t>(0x008dae40); ++edi, esi += 0x18) {
        ebx = stack_at(esi);
        if (!ebx) continue;
        call_at(T0, 0x0027468b, own_address(&Cvar_Update_entry), TitleRegs{0, ebx, 0, 0, esi, edi}, 0, nullptr);  // Cvar_Update (EBX)
        const uint32_t modified = stack_at(stack_at(esi) + 4);
        if (stack_at(esi + 0x10) != modified) stack_at(esi + 0x10) = modified;
    }
    stack_at(T) = ebp;
    call_at(T, 0x002746ab, own_address(&G_CheckMissionEnd_entry), TitleRegs{0, ebx, 0, 0, esi, edi}, 0, nullptr);  // G_CheckMissionEnd
    call_at(T, 0x002746b0, own_address(&G_FlushEntityListChanges), TitleRegs{0, ebx, 0, 0, esi, edi}, 0, nullptr);  // G_FlushEntityListChanges
    auto listed = [](uint32_t i) -> uint32_t {
        const uint32_t handle = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x00984cec)))[i];
        const uint32_t index = handle & 0xfff;
        if (index >= 0x540) return 0;
        if ((handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return 0;
        return title_global<uint32_t>(0x0097a968 + index * 8);  // g_entityHandleTable
    };
    const float seconds_scale = title_global<float>(0x0083eb4c);  // 0.001
    esi = msec;
    stack_at(entry - 4) = title_global<uint32_t>(0x00984cf4);
    edi = 1;
    for (int32_t i = 0; i < static_cast<int32_t>(stack_at(entry - 4)); ++i) {
        const uint32_t ent = listed(static_cast<uint32_t>(i));
        if (!ent) continue;
        ebx = ent;
        call_at(T, 0x00274713, own_address(&G_UpdateEntityLOD), TitleRegs{0, ebx, ent, 0, esi, edi}, 0, nullptr);  // G_UpdateEntityLOD (ECX)
        if (stack_at(ent + 0x23c)) continue;
        if (stack_at(ent + 0x2a4) & 0x10000) continue;
        edi = stack_at(ent + 0x22c);
        if (edi)
            AnimTree_BlendWeightsForModel(reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(edi)),
                                          static_cast<float>(static_cast<int32_t>(esi)) * seconds_scale);
        if (const uint32_t obj = stack_at(ent + 0x22c)) {
            const float dt = static_cast<float>(static_cast<int32_t>(msec)) * seconds_scale;
            uint32_t bits;
            std::memcpy(&bits, &dt, 4);
            const uint32_t args[3] = {obj, bits, 1};
            call_at(T, 0x00274770, own_address(&AnimTree_AdvanceTimeForModel_entry), TitleRegs{obj, ebx, bits, 0, 0, edi}, 3,
                    args);  // AnimTree_AdvanceTimeForModel
            esi = msec;
        }
        if (const uint32_t obj = stack_at(ent + 0x22c)) {
            const uint32_t args[1] = {obj};
            call_at(T, 0x00274787, own_address(&DObj_UpdateSkeletons), TitleRegs{obj, ebx, 0, 0, esi, edi}, 1, args);  // DObj_UpdateSkeletons
        }
        if (*reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(ent)) == 0xa) {
            edi = ent;
            call_at(T, 0x0027479e, own_address(&Turret_UpdateTurnFxAndBones_entry), TitleRegs{0, ebx, 0, 0, esi, edi}, 0,
                    nullptr);  // Turret_UpdateTurnFxAndBones (DL 0, EDI)
        }
    }
    edi = 1;
    // The player's box from the stance cvars when they changed (0x00985fe8/0x00985fec keep the last ones).
    {
        const float height = title_global<float>(0x00950638), depth = title_global<float>(0x00950880);
        if (title_global<float>(0x00985fe8) != height || title_global<float>(0x00985fec) != depth) {
            title_global<float>(0x00985fe8) = height;
            title_global<float>(0x00984d08) = depth;
            title_global<float>(0x00985fec) = depth;
            const float half = height * title_global<float>(0x0083eb24);
            const float neg_half = height * title_global<float>(0x0083eb28);
            title_global<float>(0x00984d04) = half;
            title_global<float>(0x00984d00) = half;
            title_global<float>(0x0097a8a4) = neg_half;
            title_global<float>(0x0097a8a0) = neg_half;
            const uint32_t player = stack_at(title_global<uint32_t>(0x0095f534) + 4);
            stack_float(player + 0xe0) = neg_half;
            stack_at(player + 0xe4) = title_global<uint32_t>(0x0097a8a4);
            stack_at(player + 0xe8) = title_global<uint32_t>(0x0097a8a8);
            stack_at(player + 0xf0) = title_global<uint32_t>(0x00984d00);
            stack_at(player + 0xf4) = title_global<uint32_t>(0x00984d04);
            stack_at(player + 0xf8) = title_global<uint32_t>(0x00984d08);
            for (uint32_t k = 0; k < 0xc; k += 4) {
                stack_at(stack_at(player + 0x23c) + 0x408 + k) = stack_at(player + 0xe0 + k);
            }
            for (uint32_t k = 0; k < 0xc; k += 4) {
                stack_at(stack_at(player + 0x23c) + 0x414 + k) = stack_at(player + 0xf0 + k);
            }
            call_at(T, 0x00274903, own_address(&SV_LinkEntity_entry), TitleRegs{0, ebx, 0, 0, esi, player}, 0, nullptr);  // SV_LinkEntity (EDI)
            edi = 1;
        }
    }
    {
        const float a = title_global<float>(0x009341c0), b = title_global<float>(0x009354b0);
        const float c = title_global<float>(0x00929b68);
        if (title_global<float>(0x00985ff0) != a || title_global<float>(0x00985ff4) != b ||
            title_global<float>(0x00985ff8) != c) {
            title_global<float>(0x00985ff0) = a;
            title_global<float>(0x00985ff4) = b;
            title_global<float>(0x00985ff8) = c;
            const uint32_t player = stack_at(title_global<uint32_t>(0x0095f534) + 4);
            stack_at(stack_at(player + 0x23c) + 0x420) = title_global<uint32_t>(0x00929b6c);
            stack_at(stack_at(player + 0x23c) + 0x424) = title_global<uint32_t>(0x009354b4);
            stack_at(stack_at(player + 0x23c) + 0x428) = title_global<uint32_t>(0x009341c4);
        }
    }
    call_at(T, 0x002749ae, own_address(&G_ExpireBadPlaces), TitleRegs{0, ebx, 0, 0, esi, edi}, 0, nullptr);  // G_ExpireBadPlaces
    // The 32 actors (0x00985384): frame time, previous origin, and angles for +0x38 == 1.
    for (uint32_t k = 0; k < 0x20; ++k) {
        const uint32_t actor = title_global<uint32_t>(0x00985384 + k * 4);
        if (!actor || static_cast<int32_t>(stack_at(actor + 0x1360)) < 0) continue;
        stack_at(actor + 0x2e0) = esi;
        const uint32_t e = stack_at(actor);
        stack_at(e + 0x20) = stack_at(e + 0x140);
        stack_at(e + 0x24) = stack_at(e + 0x144);
        stack_at(e + 0x28) = stack_at(e + 0x148);
        if (stack_at(e + 0x38) != edi) continue;
        stack_at(e + 0x44) = stack_at(e + 0x150);
        stack_at(e + 0x48) = stack_at(e + 0x154);
        stack_at(e + 0x4c) = stack_at(e + 0x158);
    }
    // Every entity of the run list (0x0097d370, count 0x0097d36c); 0x0095f52c = the one whose tag-link parents
    // (+0x3cc) run first.
    {
        const uint32_t end = title_global<uint32_t>(0x0097d36c) * 4 + 0x0097d370;
        ebp = end;
        for (ebx = 0x0097d370; ebx != end; ebx += 4) {
            esi = stack_at(ebx);
            if (!esi || !stack_at(esi + 0x224)) continue;
            const uint32_t link = stack_at(esi + 0x3cc);
            title_global<uint32_t>(0x0095f52c) = esi;
            if (link) {
                const uint32_t outer = stack_at(stack_at(link) + 0x3cc);
                if (outer)
                    call_at(T, 0x00274cb7, own_address(&G_RunEntity_entry), TitleRegs{stack_at(outer), ebx, 0, 0, esi, msec}, 0,
                            nullptr);  // G_RunEntity
                if (!stack_at(esi + 0x224)) continue;
                call_at(T, 0x00274cd2, own_address(&G_RunEntity_entry), TitleRegs{stack_at(stack_at(esi + 0x3cc)), ebx, 0, 0, esi, msec}, 0,
                        nullptr);  // G_RunEntity
            }
            const uint32_t used = stack_at(esi + 0x224);
            title_global<uint32_t>(0x0095f52c) = 0;
            if (!used) continue;
            call_at(T, 0x00274cf1, own_address(&G_RunEntity_entry), TitleRegs{esi, ebx, 0, 0, esi, msec}, 0, nullptr);  // G_RunEntity
        }
    }
    {
        const float frame_time = platform_x87_int_mul(static_cast<int32_t>(msec), seconds_scale);
        std::memcpy(&stack_at(entry - 4), &frame_time, 4);
        const uint32_t args[1] = {stack_at(entry - 4)};
        call_at(T, 0x00274d10, own_address(&G_VehiclePhysicsFrame), TitleRegs{0, ebx, 0, args[0], esi, msec}, 1, args);  // G_VehiclePhysicsFrame
    }
    ebx = 1;
    if (title_global<uint32_t>(0x00985fe4)) {
        const uint32_t args[2] = {1, 0x0083b2a0};
        call_at(T, 0x00274d2c, 0x002938f0, TitleRegs{0, ebx, 0, 0, esi, msec}, 2, args);  // __break
    }
    for (esi = 0; static_cast<int32_t>(esi) < title_global<int32_t>(0x00985404); ++esi) {  // level_maxclients
        const uint32_t ent = stack_at(title_global<uint32_t>(0x0095f534) + esi * 4 + 4);
        if (!stack_at(ent + 0x244)) continue;
        const uint32_t args[1] = {msec};
        call_at(T, 0x00274d59, own_address(&ClientEndFrame), TitleRegs{ent, ebx, stack_at(ent + 0x244), 0, esi, msec}, 1,
                args);  // ClientEndFrame (EAX)
    }
    call_at(T, 0x00274d6b, own_address(&G_TrimCorpseQueue), TitleRegs{0, ebx, 0, 0, esi, msec}, 0, nullptr);  // G_TrimCorpseQueue
    // The actors' frame data: +0x14 = 1, flag 0x10000 of +0x10 while +0x350 is over 1000, the origin copy +0x2c.
    for (uint32_t k = 0; k < 0x20; ++k) {
        const uint32_t actor = title_global<uint32_t>(0x00985384 + k * 4);
        if (!actor || static_cast<int32_t>(stack_at(actor + 0x1360)) < 0) continue;
        const uint32_t e = stack_at(actor);
        uint32_t bits = stack_at(e + 0x10);
        stack_at(e + 0x14) = 1;
        bits = static_cast<int32_t>(stack_at(e + 0x350)) > 1000 ? (bits | 0x10000) : (bits & 0xfffeffff);
        stack_at(e + 0x10) = bits;
        stack_at(e + 0x2c) = stack_at(e + 0x140);
        stack_at(e + 0x30) = stack_at(e + 0x144);
        stack_at(e + 0x34) = stack_at(e + 0x148);
    }
    {
        const uint32_t args[1] = {stack_at(entry - 4)};
        title_global<uint8_t>(0x00935538) = 0;
        call_at(T, 0x00274fc7, own_address(&G_ProcessCodNotes), TitleRegs{0, 1, args[0], 0, 0x10000, 0xfffeffff}, 1,
                args);  // G_ProcessCodNotes
    }
    const uint32_t count = title_global<uint32_t>(0x00984cf4);
    for (int32_t i = 0; i < static_cast<int32_t>(title_global<uint32_t>(0x00984cf4)); ++i) {
        const uint32_t ent = listed(static_cast<uint32_t>(i));
        if (!ent) continue;
        if (const uint32_t obj = stack_at(ent + 0x22c)) stack_at(obj + 0x9c) |= 1;
    }
    (void)count;
    return title_global<uint32_t>(0x00984cf4);
}

}  // namespace game
