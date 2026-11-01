#include "g_mover.h"

#include <cstring>

#include "g_spawn.h"
#include "g_utils.h"
#include "g_scr_runtime.h"
#include "g_vehicle.h"
#include "qcommon.h"
#include "title_data.h"

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

}  // namespace game
