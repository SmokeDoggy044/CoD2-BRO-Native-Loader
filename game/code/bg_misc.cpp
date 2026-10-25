#include "bg_misc.h"

#include <cstring>

#include "title_data.h"

namespace game {

namespace {

uint8_t* pm_ps() { return *title_global<uint8_t**>(0x0092eff4); }

template <typename T>
T& field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

int32_t& clip_ammo(uint32_t weapon) {
    const uint8_t* def = title_global<const uint8_t**>(0x0095f4d8)[weapon];  // bg_weaponDefs
    const uint32_t clip = *reinterpret_cast<const uint32_t*>(def + 0x5a0);
    return field<int32_t>(pm_ps(), 0x280 + clip * 4);
}

void add_event(uint8_t* ps, uint32_t event, uint32_t parm) {
    field<uint32_t>(ps, 0xc8 + (field<uint32_t>(ps, 0xc4) & 3) * 4) = event & 0xff;
    field<uint32_t>(ps, 0xd8 + (field<uint32_t>(ps, 0xc4) & 3) * 4) = parm;
    ++field<uint32_t>(ps, 0xc4);
}

}  // namespace

PLATFORM_ASM_ENTRY(BG_FindWeaponIndexForHash_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(BG_FindWeaponIndexForHash) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL BG_FindWeaponIndexForHash(uint32_t hash) {
    const int32_t last = title_global<int32_t>(0x0095f4d4);  // bg_lastWeaponIndex
    int32_t index = 0;
    if (last >= 0) {
        const uint8_t* const* defs = title_global<const uint8_t* const*>(0x0095f4d8);
        for (; index <= last; ++index)
            if (*reinterpret_cast<const uint32_t*>(defs[index] + 4) == hash) return static_cast<uint32_t>(index);
    }
    return static_cast<uint32_t>(index) & ~0xffu;
}

int32_t XB_REGS3 XB_CDECL PM_GetClipAmmo(uint32_t, uint32_t, uint32_t weapon) { return clip_ammo(weapon); }

uint32_t XB_REGS3 XB_CDECL PM_ClipIsEmpty(uint32_t, uint32_t, uint32_t weapon) { return clip_ammo(weapon) == 0; }

int32_t* XB_REGS3 XB_CDECL PM_WeaponUseAmmo(uint32_t, uint32_t, uint32_t weapon, int32_t amount) {
    int32_t& ammo = clip_ammo(weapon);
    ammo -= amount;
    return &ammo;
}

void XB_CDECL PM_DropTimers() {
    uint8_t* ps = pm_ps();
    const int32_t time = field<int32_t>(ps, 0x30);
    if (!time) return;
    const int32_t msec = title_global<int32_t>(0x0095ffa8);  // pml_msec
    if (msec >= time) {
        field<uint32_t>(ps, 0x2c) &= 0xfffbdcff;
        field<int32_t>(pm_ps(), 0x30) = 0;
    } else {
        field<int32_t>(ps, 0x30) -= msec;
    }
}

uint8_t* XB_REGS3 XB_CDECL BG_AddPredictableEventToPlayerstate(uint8_t* ps, uint32_t, uint32_t event, uint32_t parm) {
    if (event) add_event(ps, event, parm & 0xff);
    return ps;
}

void XB_REGS3 XB_CDECL PM_AddEvent(uint32_t, uint32_t, uint32_t event) {
    uint8_t* ps = pm_ps();
    if (event) add_event(ps, event, 0);
}

void XB_CDECL PM_AddTouchEnt(int32_t entity) {
    if (!entity) return;
    uint8_t* pm = title_global<uint8_t*>(0x0092eff4);
    const int32_t count = field<int32_t>(pm, 0x7c);
    if (count == 0x20) return;
    for (int32_t i = 0; i < field<int32_t>(pm, 0x7c); ++i)
        if (field<int32_t>(pm, 0x80 + i * 4) == entity) return;
    field<int32_t>(pm, 0x80 + count * 4) = entity;
    ++field<int32_t>(pm, 0x7c);
}

void XB_REGS3 XB_CDECL PM_StartLegsAnim(uint32_t, uint32_t, uint32_t anim) {
    uint8_t* pm = title_global<uint8_t*>(0x0092eff4);
    uint8_t* ps = *reinterpret_cast<uint8_t**>(pm);
    if (field<int32_t>(ps, 0x24) >= 4 || !pm[0xa]) return;
    if (field<int32_t>(title_global<uint8_t*>(0x00960020), 0x94) == 5) {
        field<uint32_t>(ps, 0x4ec) = anim;
        return;
    }
    if (!anim && field<float>(ps, 0x88) > title_global<float>(0x0083ec18)) anim = 0x17;  // 0.9
    field<uint32_t>(ps, 0x4ec) = (~field<uint32_t>(ps, 0x4ec) & 0x200) | anim;
}

PLATFORM_ASM_ENTRY(PM_CmdScale_entry,
                   "pushl %eax\n" PLATFORM_ASM_CALL(PM_CmdScale) "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $4, %esp\n"
                   "ret")

float XB_CDECL PM_CmdScale(const int8_t* cmd) {
    using ext = long double;
    const auto magnitude = [](int32_t v) { return v < 0 ? -v : v; };
    int32_t max = magnitude(cmd[0x14]);
    if (magnitude(cmd[0x15]) > max) max = magnitude(cmd[0x15]);
    if (magnitude(cmd[0x16]) > max) max = magnitude(cmd[0x16]);
    if (!max) return 0.0f;
    uint8_t* ps = pm_ps();
    const int32_t forward = cmd[0x14], right = cmd[0x15], up = cmd[0x16];
    const ext speed = static_cast<ext>(field<int32_t>(ps, 0x50)) * static_cast<ext>(max);
    ext sum = static_cast<ext>(right * right) + static_cast<ext>(up * up);
    sum = sum + static_cast<ext>(forward) * static_cast<ext>(forward);
    const ext total = platform_x87_sqrt_ext(sum) * title_global<float>(0x0083ed88);  // 127
    const float scale = static_cast<float>(speed / total);
    const uint32_t flags = field<uint32_t>(ps, 0x2c);
    float stance;
    if (!(flags & 0x80) && field<float>(ps, 0x4c) == title_global<float>(0x0083eafc))  // 0
        stance = field<float>(ps, (flags & 0x10000) ? 0x438 : 0x434);
    else
        stance = field<float>(ps, 0x430);
    float result = stance * scale;
    const int32_t type = field<int32_t>(ps, 0x24);
    if (type == 2) result = result * title_global<float>(0x0083ec9c);  // 3
    if (type == 3) result = result * title_global<float>(0x0083ecd0);  // 6
    return result;
}

PLATFORM_ASM_ENTRY(BG_AmmoRoomForWeapon_entry,
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(BG_AmmoRoomForWeapon) "addl $8, %esp\n"
                   "ret")

int32_t XB_CDECL BG_AmmoRoomForWeapon(uint32_t weapon, const uint8_t* ps) {
    const uint8_t* const* defs = title_global<const uint8_t* const*>(0x0095f4d8);  // bg_weaponDefs
    const uint8_t* def = defs[weapon];
    const auto word = [](const uint8_t* base, uint32_t offset) { return *reinterpret_cast<const int32_t*>(base + offset); };
    const int32_t group = word(def, 0x5b0);
    if (group < 0) {
        if (word(def, 0x6ec)) {
            const uint32_t clip = static_cast<uint32_t>(word(def, 0x5a0));
            return title_global<int32_t>(0x00935338 + clip * 4) - word(ps, 0x280 + clip * 4);
        }
        const uint32_t ammo = static_cast<uint32_t>(word(def, 0x598));
        return title_global<int32_t>(0x00929818 + ammo * 4) - word(ps, 0x110 + ammo * 4);
    }
    int32_t room = title_global<int32_t>(0x00934a68 + static_cast<uint32_t>(group) * 4);
    uint32_t ammo_seen[0x5c] = {};
    uint32_t clip_seen[0x5c] = {};
    for (int32_t other = 1; other <= title_global<int32_t>(0x0095f4d4); ++other) {  // bg_lastWeaponIndex
        if (!(word(ps, 0x3f0 + (other >> 5) * 4) & (1 << (other & 31)))) continue;
        const uint8_t* other_def = defs[other];
        if (word(other_def, 0x5b0) != group) continue;
        if (word(other_def, 0x6ec)) {
            const uint32_t clip = static_cast<uint32_t>(word(other_def, 0x5a0));
            if (clip_seen[clip]) continue;
            room -= word(ps, 0x280 + clip * 4);
            clip_seen[clip] = 1;
        } else {
            const uint32_t ammo = static_cast<uint32_t>(word(other_def, 0x598));
            if (ammo_seen[ammo]) continue;
            room -= word(ps, 0x110 + ammo * 4);
            ammo_seen[ammo] = 1;
        }
    }
    return room;
}

PLATFORM_ASM_ENTRY(BG_GetWeaponSlot_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(BG_GetWeaponSlot) "addl $12, %esp\n"
                   "ret")

uint32_t XB_CDECL BG_GetWeaponSlot(const uint8_t* ps, int32_t weapon, uint32_t follow) {
    const auto word = [](const uint8_t* base, uint32_t offset) { return *reinterpret_cast<const int32_t*>(base + offset); };
    if (!(word(ps, 0x3f0 + (weapon >> 5) * 4) & (1 << (weapon & 31)))) return 0;
    const uint8_t* const* defs = title_global<const uint8_t* const*>(0x0095f4d8);  // bg_weaponDefs
    int32_t current = weapon;
    do {
        const uint8_t* def = defs[current];
        const uint32_t type = static_cast<uint32_t>(word(def, 0x9c));
        if (type - 1 > 5) return 0;
        if (type <= 2) {
            if (static_cast<int8_t>(ps[0x3f9]) == current) return 1;
            if (static_cast<int8_t>(ps[0x3fa]) == current) return 2;
        } else if (static_cast<int8_t>(ps[0x3f8 + type]) == current) {
            return static_cast<uint32_t>(word(def, 0x9c));
        }
        if (follow && word(def, 0x718)) current = word(def, 0x718);
    } while (current != weapon);
    return 0;
}

PLATFORM_ASM_ENTRY(BG_ChooseWeaponSlot_entry,
                   "pushl %esi\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(BG_ChooseWeaponSlot) "addl $12, %esp\n"
                   "ret")

uint32_t XB_CDECL BG_ChooseWeaponSlot(uint32_t preferred, uint32_t weapon, const uint8_t* ps) {
    const uint8_t* const* defs = title_global<const uint8_t* const*>(0x0095f4d8);  // bg_weaponDefs
    const auto takes_slot = [defs](int8_t held) {
        return *reinterpret_cast<const uint32_t*>(defs[held] + 0xa0) != 0;
    };
    const uint8_t* def = defs[weapon];
    if (!*reinterpret_cast<const uint32_t*>(def + 0xa0)) return 0;
    const uint32_t type = *reinterpret_cast<const uint32_t*>(def + 0x9c);
    if (type - 1 > 5) return 0;
    if (type <= 2) {
        if (preferred == 1 || preferred == 2) {
            const int8_t held = static_cast<int8_t>(ps[0x3f8 + preferred]);
            if (!held || takes_slot(held)) return preferred;
        }
        const int8_t first = static_cast<int8_t>(ps[0x3f9]);
        if (!first || takes_slot(first)) return 1;
        const int8_t second = static_cast<int8_t>(ps[0x3fa]);
        if (!second) return 2;
        return takes_slot(second) ? 2 : 0;
    }
    const int8_t held = static_cast<int8_t>(ps[0x3f8 + type]);
    if (!held) return type;
    return takes_slot(held) ? type : 0;
}

uint8_t* XB_REGS3 XB_CDECL BG_PlayerStateToEntityState(uint8_t* es, uint32_t, uint8_t* ps) {
    es[0] = 1;
    field<uint32_t>(es, 0x14) = 1;
    for (uint32_t i = 0; i < 12; i += 4) field<uint32_t>(es, 0x20 + i) = field<uint32_t>(ps, i);
    field<uint32_t>(es, 0x38) = 1;
    for (uint32_t i = 0; i < 12; i += 4) field<uint32_t>(es, 0x44 + i) = field<uint32_t>(ps, 0x9c + i);
    const int32_t lean = field<int32_t>(ps, 0x78);
    float value = static_cast<float>(lean);
    if (lean > 0x80) value = value - title_global<float>(0x0083ecf4);  // 256
    field<float>(es, 0x94) = value;
    field<uint32_t>(es, 0x10) = field<uint32_t>(ps, 0xc0);

    const uint32_t entity_sequence = field<uint32_t>(ps, 0xfc);
    const uint32_t sequence = field<uint32_t>(ps, 0xc4);
    if (static_cast<int32_t>(entity_sequence - sequence) >= 0) {
        es[4] = 0;
    } else {
        if (static_cast<int32_t>(sequence - entity_sequence) > 4) field<uint32_t>(ps, 0xfc) = sequence - 4;
        es[4] = ps[0xd8 + (field<uint32_t>(ps, 0xfc) & 3) * 4];
        ++field<uint32_t>(ps, 0xfc);
    }

    uint32_t seen = field<uint32_t>(ps, 0xe8);
    if (seen != field<uint32_t>(ps, 0xc4)) {
        const int32_t* predictable = title_global<const int32_t*>(0x008d2188);
        do {
            const uint32_t slot = (seen & 3) * 4;
            const int32_t event = ps[0xc8 + slot];
            int32_t i = 0;
            if (predictable[0] > 0) {
                int32_t value = predictable[0];
                while (value != event) {
                    value = predictable[++i];
                    if (value <= 0) break;
                }
            }
            if (predictable[i] < 0) {
                es[0xb4 + (field<uint32_t>(es, 0xb0) & 3)] = ps[0xc8 + slot];
                es[0xb8 + (field<uint32_t>(es, 0xb0) & 3)] = ps[0xd8 + slot];
                ++field<uint32_t>(es, 0xb0);
            }
            ++seen;
        } while (seen != field<uint32_t>(ps, 0xc4));
    }
    field<uint32_t>(ps, 0xe8) = field<uint32_t>(ps, 0xc4);
    es[3] = ps[0x80];
    field<uint32_t>(es, 0xc) = field<uint32_t>(ps, 0x60);
    return es;
}

namespace {

// x87 FCOS of an extended value (the title's precision control applies).
long double x87_cos_ext(long double x) {
    long double r;
    __asm__("fcos" : "=t"(r) : "0"(x));
    return r;
}

}  // namespace

void XB_REGS3 XB_CDECL BG_EvaluateTrajectoryDelta(uint32_t, int32_t time, float* result, const uint8_t* tr) {
    const int32_t start = *reinterpret_cast<const int32_t*>(tr + 4);
    const int32_t duration = *reinterpret_cast<const int32_t*>(tr + 8);
    const float* delta = reinterpret_cast<const float*>(tr + 0x18);
    const float to_seconds = title_global<float>(0x0083eb4c);
    const float gravity = title_global<float>(0x0094d450);
    const auto zero = [result]() {
        result[1] = 0.0f;
        result[0] = 0.0f;
        result[2] = 0.0f;
    };
    switch (*reinterpret_cast<const uint32_t*>(tr)) {
    case 2:  // linear
        std::memcpy(result, delta, 12);
        return;
    case 3:  // linear, stopping after the duration
        if (time > duration + start) return zero();
        std::memcpy(result, delta, 12);
        return;
    case 4: {  // sine: the derivative's cosine factor on the x87
        const long double phase = static_cast<long double>(time - start) / static_cast<long double>(duration) *
                                  static_cast<long double>(title_global<float>(0x0083edac));
        const long double f = x87_cos_ext(phase) * static_cast<long double>(title_global<float>(0x0083eb24));
        for (int32_t i = 0; i < 3; ++i) result[i] = static_cast<float>(f * static_cast<long double>(delta[i]));
        return;
    }
    case 5:
    case 6:
    case 7: {  // gravity (full, 0.3, 0.2)
        const float t = static_cast<float>(time - start) * to_seconds;
        std::memcpy(result, delta, 12);
        float g = gravity;
        if (*reinterpret_cast<const uint32_t*>(tr) == 6) g = g * title_global<float>(0x0083066c);
        if (*reinterpret_cast<const uint32_t*>(tr) == 7) g = g * title_global<float>(0x0083eca0);
        result[2] = delta[2] - g * t;
        return;
    }
    case 9:
    case 10: {  // accelerating (t squared) / linear in t, until the duration ends
        if (time > start + duration) return zero();
        float t = static_cast<float>(time - start) * to_seconds;
        if (*reinterpret_cast<const uint32_t*>(tr) == 9) t = t * t;
        for (int32_t i = 0; i < 3; ++i) result[i] = delta[i] * t;
        return;
    }
    default:
        return zero();
    }
}

// The original returns in XMM0 (the C version stores to a slot the entry loads) and leaves XMM2-7 alone;
// FireWeapon keeps XMM2 across the call.
PLATFORM_ASM_ENTRY(BG_GetWeaponSpread_entry,
                   "subl $100, %esp\n"
                   "movups %xmm2, 4(%esp)\n"
                   "movups %xmm3, 20(%esp)\n"
                   "movups %xmm4, 36(%esp)\n"
                   "movups %xmm5, 52(%esp)\n"
                   "movups %xmm6, 68(%esp)\n"
                   "movups %xmm7, 84(%esp)\n"
                   "pushl 108(%esp)\n"
                   "pushl 108(%esp)\n"
                   "pushl %ecx\n"
                   "pushl %edx\n"
                   "leal 16(%esp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(BG_GetWeaponSpread) "addl $20, %esp\n"
                   "movss (%esp), %xmm0\n"
                   "movups 4(%esp), %xmm2\n"
                   "movups 20(%esp), %xmm3\n"
                   "movups 36(%esp), %xmm4\n"
                   "movups 52(%esp), %xmm5\n"
                   "movups 68(%esp), %xmm6\n"
                   "movups 84(%esp), %xmm7\n"
                   "addl $100, %esp\n"
                   "ret")

void XB_CDECL BG_GetWeaponSpread(float* out, uint32_t weapon, const uint8_t* ps, int32_t time, int32_t ads) {
    const uint8_t* def = title_global<const uint8_t**>(0x0095f4d8)[weapon];  // bg_weaponDefs
    const auto f = [def](uint32_t offset) -> const float& { return *reinterpret_cast<const float*>(def + offset); };
    const auto word = [ps](uint32_t offset) { return *reinterpret_cast<const int32_t*>(ps + offset); };
    // {standing, crouched, prone}
    const uint32_t stand = ads ? 0x7b8 : 0x64c, crouch = ads ? 0x7bc : 0x650, prone = ads ? 0x7c0 : 0x654;
    const int32_t target = word(0xb4);
    const int32_t start = word(0xb0);
    if (static_cast<float>(target) == *reinterpret_cast<const float*>(ps + 0xac) || !start) {
        const int32_t flags = word(0x2c);
        *out = f(flags & 1 ? prone : flags & 2 ? crouch : stand);
        return;
    }
    // view heights: prone +0x420, crouched +0x424, standing +0x428
    const int32_t prone_height = word(0x420);
    int32_t duration = 200;
    if (target == prone_height)
        duration = 400;
    else if (target == word(0x424))
        duration = word(0xb8) ? 200 : 400;
    float frac = static_cast<float>(time - start) / static_cast<float>(duration);
    if (0.0f > frac)
        frac = 0.0f;
    else if (frac > 1.0f)
        frac = 1.0f;
    // blend from the stance being left (from, to)
    uint32_t from, to;
    if (target == prone_height) {
        from = crouch, to = prone;
    } else if (target == word(0x428)) {
        from = crouch, to = stand;
    } else if (word(0xb8)) {
        from = stand, to = crouch;
    } else {
        from = prone, to = crouch;
    }
    *out = (f(to) - f(from)) * frac + f(from);
}

}  // namespace game
