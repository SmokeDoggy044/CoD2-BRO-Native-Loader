#include "pmove.h"

#include <cstring>

#include "actor.h"
#include "bg_misc.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "cgame.h"
#include "g_vehicle.h"
#include "g_main.h"
#include "g_utils.h"
#include "lanes.h"
#include "ngl_debug.h"
#include "cm.h"
#include "fx.h"
#include "qcommon.h"
#include "sv.h"
#include "title_crt.h"
#include "title_data.h"
#include "fx_cmd.h"

namespace game {

namespace {

template <typename T>
T& at(const void* base, uint32_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + offset);
}
template <typename T>
T* pointer(uint32_t address) {
    return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}
uint32_t address_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

}  // namespace

// Every interval (0x0094d584 ms) the eye position of the sentient behind the entity handle +0x7c goes into the
// next of two history slots (0x0095f42c, 12 bytes each; time 0x0095f444; slot 0x0095f428, last time 0x0095f424).
void XB_CDECL Player_RecordEyeHistory(const uint8_t* pm) {
    const int32_t now = title_global<int32_t>(0x0098540c);  // level_time
    if (static_cast<int32_t>(static_cast<uint32_t>(now) - title_global<uint32_t>(0x0095f424)) <=
        title_global<int32_t>(0x0094d584))
        return;
    title_global<int32_t>(0x0095f424) = now;
    title_global<int32_t>(0x0095f428) = static_cast<int32_t>(title_global<uint32_t>(0x0095f428) + 1u) % 2;
    const uint32_t handle = at<uint32_t>(pm, 0x7c);
    const uint32_t index = handle & 0xfff;
    const uint8_t* ent = nullptr;
    if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
        ent = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x0097a968 + index * 8)));
    alignas(4) uint8_t eye[12];
    Sentient_EyeOrigin(at<uint8_t*>(ent, 0x244), 0, eye);
    for (uint32_t i = 0; i < 3; ++i)
        title_global<uint32_t>(0x0095f42c + title_global<int32_t>(0x0095f428) * 12 + i * 4) = at<uint32_t>(eye, i * 4);
    title_global<int32_t>(0x0095f444 + title_global<int32_t>(0x0095f428) * 4) = title_global<int32_t>(0x0098540c);
}

// Compass marker word for the first listed actor (0x0097d370, count 0x0097d36c) with a marker icon
// (+0x240 -> +0x137c set, icon +0x1382 != -1, sentient +0x244 team 2) that is not visible from origin:
// offsets (rounded, scaled to fit -1022..1024, /4 + 255) in bits 6-14 (x) and 15-23 (y), the angle
// (+0x154 * 0.711) in bits 24-31, the icon in bits 0-5. 0 without one or with markers off.
uint32_t XB_CDECL Player_PackActorMarker(const float* origin) {
    if (!title_global<int32_t>(0x00986008)) return 0;  // g_compassMarkersEnabled
    const uint32_t base = 0x0097d370;
    const uint32_t end = base + title_global<uint32_t>(0x0097d36c) * 4;
    const uint8_t* actor = nullptr;
    for (uint32_t it = base; it != end; it += 4) {
        const uint8_t* const e = *reinterpret_cast<uint8_t* const*>(uintptr_t{it});
        if (!e) continue;
        const uint8_t* const a = at<uint8_t*>(e, 0x240);
        if (!a || !at<uint32_t>(a, 0x137c)) continue;
        if (at<int32_t>(at<uint8_t*>(e, 0x244), 4) != 2) continue;
        if (at<int16_t>(a, 0x1382) == -1) continue;
        if (!SV_EntityVisibleFromPoint(origin, at<uint32_t>(e, 0x224))) {
            actor = e;
            break;
        }
    }
    if (!actor) return 0;
    int32_t icon = -1;
    const uint8_t* const a = at<uint8_t*>(actor, 0x240);
    if (a && at<uint32_t>(a, 0x137c) && at<int32_t>(at<uint8_t*>(actor, 0x244), 4) == 2) icon = at<int16_t>(a, 0x1382);
    const float half = title_global<float>(0x0083eb24);
    int32_t x = static_cast<int32_t>((at<float>(actor, 0x140) - origin[0]) + half);
    int32_t y = static_cast<int32_t>((at<float>(actor, 0x144) - origin[1]) + half);
    const float high = title_global<float>(0x0083eff8), low = title_global<float>(0x0083eff4);
    const float one = title_global<float>(0x0083eb20);
    float sx = one, sy = one;
    if (x > 0x400)
        sx = high / static_cast<float>(x);
    else if (x < -0x3fe)
        sx = low / static_cast<float>(x);
    if (y > 0x400)
        sy = high / static_cast<float>(y);
    else if (y < -0x3fe)
        sy = low / static_cast<float>(y);
    if (one > sx || one > sy) {
        if (sy > sx)
            y = static_cast<int32_t>(static_cast<float>(y) * sx);
        else if (sx > sy)
            x = static_cast<int32_t>(static_cast<float>(x) * sy);
    }
    if (x > 0x400) x = 0x400;
    else if (x < -0x3fe) x = -0x3fe;
    if (y > 0x400) y = 0x400;
    else if (y < -0x3fe) y = -0x3fe;
    const uint32_t yv = static_cast<uint32_t>((y + 2) / 4 + 0xff) << 15;
    const uint32_t xv = static_cast<uint32_t>((x + 2) / 4 + 0xff) << 6;
    uint32_t r = (yv & 0xff8000u) | (xv & 0x7fc0u);
    r |= static_cast<uint32_t>(static_cast<int32_t>(at<float>(actor, 0x154) * title_global<float>(0x0083eff0))) << 24;
    return r | (static_cast<uint32_t>(icon) & 0x3f);
}

void XB_CDECL PM_AddWeaponFireBlend() {
    uint8_t* const* const pm = title_global<uint8_t**>(0x0092eff4);
    uint8_t* const ps = *pm;
    if (at<float>(ps, 0x88) == title_global<float>(0x0083eb20)) return;  // 1
    const float limit = title_global<float>(0x0083eb8c);                // 255
    at<float>(ps, 0x4f0) = at<float>(title_global<uint8_t*>(0x00960020), 0x660) * limit + at<float>(ps, 0x4f0);
    uint8_t* const again = *pm;
    if (at<float>(again, 0x4f0) > limit) at<float>(again, 0x4f0) = limit;
}

namespace {

uint8_t* pm_ps() { return *title_global<uint8_t**>(0x0092eff4); }

// The weapon state's end of an ADS drop/raise or weapon raise (0xd / 0xc / 1): state 0 and PM_StartLegsAnim(0)
// inlined (no anim for pm_type 4 up or without +0xa; weapon type 5 clears +0x4ec; 0x17 above 0.9 ADS, else 0,
// toggle bit 0x200). 1 when it ended.
uint32_t finish_weapon_state(int32_t state) {
    uint8_t* const pm = title_global<uint8_t*>(0x0092eff4);
    uint8_t* ps = *reinterpret_cast<uint8_t**>(pm);
    if (at<int32_t>(ps, 0x84) != state) return 0;
    at<int32_t>(ps, 0x84) = 0;
    ps = *reinterpret_cast<uint8_t**>(pm);
    if (at<int32_t>(ps, 0x24) >= 4 || !pm[0xa]) return 1;
    if (at<int32_t>(title_global<uint8_t*>(0x00960020), 0x94) == 5) {
        at<uint32_t>(ps, 0x4ec) = 0;
        return 1;
    }
    uint32_t anim = 0;
    if (at<float>(ps, 0x88) > title_global<float>(0x0083ec18)) anim = 0x17;  // 0.9
    at<uint32_t>(ps, 0x4ec) = (~at<uint32_t>(ps, 0x4ec) & 0x200) | anim;
    return 1;
}

}  // namespace

void XB_CDECL PM_UpdateSprintFlag() {
    // Sprint (pm_flags 0x10000) holds while the sprint button (cmd byte +8 bit 8) is down with stamina (+0x45c
    // above 0, above 0x0083ec24 to start), not prone/ducked (0x23), moving (+0x18/+0x19), without flags 0x106000,
    // not in weapon state 0xd; with a type-1 weapon (0x00960020 +0x94) also not mid-clip-fire (+0x6e4: clip
    // below +0x618, ammo bit +0x3f0), not firing (button bit 1) and not in state 3.
    uint8_t* const pm = title_global<uint8_t*>(0x0092eff4);
    const uint32_t flags = at<uint32_t>(pm_ps(), 0x2c);
    at<uint32_t>(pm_ps(), 0x2c) = flags & 0xfffeffffu;
    uint8_t* const ps = pm_ps();
    if (at<int32_t>(ps, 0x24) >= 4) return;
    const uint8_t buttons = pm[8];
    if (!(buttons & 8)) return;
    if (title_global<float>(0x0083eafc) >= at<float>(ps, 0x45c)) return;
    if (!(flags & 0x10000) && title_global<float>(0x0083ec24) >= at<float>(ps, 0x45c)) return;
    if (at<uint8_t>(ps, 0x2c) & 0x23) return;
    if (!pm[0x18] && !pm[0x19]) return;
    if (at<uint32_t>(ps, 0xc0) & 0x106000) return;
    const int32_t state = at<int32_t>(ps, 0x84);
    if (state == 0xd) return;
    const uint8_t* const def = title_global<uint8_t*>(0x00960020);
    if (at<int32_t>(def, 0x94) == 1) {
        if (at<uint32_t>(def, 0x6e4) && at<int32_t>(ps, 0x3c) < at<int32_t>(def, 0x618) && at<int32_t>(ps, 0x3c) &&
            Com_BitCheck(0, at<int32_t>(ps, 0x80), &at<uint32_t>(ps, 0x3f0)))
            return;
        if (buttons & 1) return;
        if (state == 3) return;
    }
    at<uint32_t>(ps, 0x2c) |= 0x10000;
}

void XB_CDECL PM_UpdateButtonFlag80() {
    // pm_flags 0x80 holds while cmd button bit 0x10 is down with flag 0x20 and not prone (1), below pm_type 4,
    // without client flag 0x100000, and outside weapon states 5-9.
    uint8_t* const pm = title_global<uint8_t*>(0x0092eff4);
    at<uint32_t>(pm_ps(), 0x2c) &= 0xffffff7fu;
    uint8_t* const ps = pm_ps();
    if (at<int32_t>(ps, 0x24) >= 4 || !(pm[8] & 0x10)) return;
    const uint32_t flags = at<uint32_t>(ps, 0x2c);
    if ((flags & 1) || !(flags & 0x20)) return;
    if (at<uint32_t>(ps, 0xc0) & 0x100000) return;
    const int32_t state = at<int32_t>(ps, 0x84);
    if (state == 5 || state == 7 || state == 9 || state == 8 || state == 6) return;
    at<uint32_t>(ps, 0x2c) |= 0x80;
}

void XB_CDECL PM_SetProneFlag400() {
    // A prone player (pm_flags 1) gets flag 0x400.
    uint8_t* const ps = pm_ps();
    if (at<uint8_t>(ps, 0x2c) & 1) at<uint32_t>(ps, 0x2c) |= 0x400;
}

// EAX = time, EDI = curve, EBX = out; the value comes back in XMM0.
PLATFORM_ASM_ENTRY(PM_EvalLerpCurve_entry,
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(PM_EvalLerpCurve) "addl $12, %esp\n"
                   "subl $4, %esp\n"
                   "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $4, %esp\n"
                   "popl %esi\n"
                   "ret")

float XB_CDECL PM_EvalLerpCurve(int32_t time, const uint8_t* curve, float* out) {
    // Curve points of 12 bytes (time, value, height; ends at time -1): a point's own value and height at its
    // time, linear between the two around it, else the first point's.
    if (time) {
        for (int32_t k = 1;; ++k) {
            const uint8_t* const e = curve + k * 12;
            const int32_t t = at<int32_t>(e, 0);
            if (t == time) {
                *out = static_cast<float>(at<int32_t>(e, 8));
                return at<float>(e, 4);
            }
            if (t > time) {
                const uint8_t* const p = e - 12;
                const int32_t pt = at<int32_t>(p, 0);
                const float frac = static_cast<float>(time - pt) / static_cast<float>(t - pt);
                const int32_t ph = at<int32_t>(p, 8);
                *out = static_cast<float>(at<int32_t>(e, 8) - ph) * frac + static_cast<float>(ph);
                return (at<float>(e, 4) - at<float>(p, 4)) * frac + at<float>(p, 4);
            }
            if (at<int32_t>(e, 12) == -1) break;
        }
    }
    *out = static_cast<float>(at<int32_t>(curve, 8));
    return at<float>(curve, 4);
}

// EAX = stance, ECX = player state, cdecl flag; ECX and EDX kept (the callers read them).
PLATFORM_ASM_ENTRY(PM_ViewHeightLerpTime_entry,
                   "pushl %ecx\n"
                   "pushl %edx\n"
                   "pushl 12(%esp)\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(PM_ViewHeightLerpTime) "addl $12, %esp\n"
                   "popl %edx\n"
                   "popl %ecx\n"
                   "ret")

uint32_t XB_CDECL PM_ViewHeightLerpTime(uint32_t stance, const uint8_t* ps, uint32_t flag) {
    // 400 ms into the prone height (+0x420), into the crouch height (+0x424) 200 with the flag else 400, else 200.
    if (stance == at<uint32_t>(ps, 0x420)) return 0x190;
    if (stance == at<uint32_t>(ps, 0x424)) return flag ? 0xc8 : 0x190;
    return 0xc8;
}

uint32_t XB_REGS1 XB_CDECL PM_ViewHeightStance(const uint8_t* ps) {
    // The view height (+0xa8) as a stance: 2 at the crouch height (+0x424), 1 at the prone height (+0x420), else 0.
    const uint32_t height = at<uint32_t>(ps, 0xa8);
    if (height == at<uint32_t>(ps, 0x424)) return 2;
    return height == at<uint32_t>(ps, 0x420) ? 1 : 0;
}

uint32_t XB_CDECL PM_FinishAdsDrop() { return finish_weapon_state(0xd); }

uint32_t XB_CDECL PM_FinishAdsRaise() { return finish_weapon_state(0xc); }

uint32_t XB_CDECL PM_FinishWeaponRaise() { return finish_weapon_state(1); }

namespace {

// Weapon states 0-2 and 4-9 stay (1); 3 and 0xb block (0); others drop to state 0 (+0x34/+0x38 cleared) and play
// the caller plays its weapon anim (2).
uint32_t reset_weapon_state() {
    uint8_t* const pm = title_global<uint8_t*>(0x0092eff4);
    uint8_t* const ps = *reinterpret_cast<uint8_t**>(pm);
    const int32_t state = at<int32_t>(ps, 0x84);
    switch (state) {
    case 0: case 1: case 2: case 5: case 7: case 9: case 8: case 6: case 4:
        return 1;
    case 3: case 0xb:
        return 0;
    default:
        break;
    }
    at<uint32_t>(ps, 0x34) = 0;
    at<uint32_t>(*reinterpret_cast<uint8_t**>(pm), 0x38) = 0;
    at<uint32_t>(*reinterpret_cast<uint8_t**>(pm), 0x84) = 0;
    return 2;
}

}  // namespace

uint32_t XB_CDECL PM_ResetWeaponToAnimA() {
    // From PmoveSingle: the weapon back to state 0 with weapon anim 0xa.
    uint8_t* const pm = title_global<uint8_t*>(0x0092eff4);
    const uint32_t r = reset_weapon_state();
    if (r != 2) return r;
    if (pm[0xa] && (at<uint32_t>(*reinterpret_cast<uint8_t**>(pm), 0x4ec) & 0xfffffdffu) != 0xa) PM_StartLegsAnim(0, 0, 0xa);
    return 1;
}

uint32_t XB_CDECL PM_ResetWeaponToIdle() {
    // From PmoveSingle: blocked (0) for an ADS (pm_flags 0x20) weapon of type 3 (+0x98); else the weapon back to
    // state 0 with its idle anim (0x17 above 0.9 ADS, else 0).
    uint8_t* const pm = title_global<uint8_t*>(0x0092eff4);
    const uint8_t* const ps = *reinterpret_cast<uint8_t**>(pm);
    if ((at<uint8_t>(ps, 0x2c) & 0x20) &&
        at<int32_t>(pointer<uint8_t>(title_global<uint32_t>(title_global<uint32_t>(0x0095f4d8) + at<uint32_t>(ps, 0x80) * 4)), 0x98) == 3)
        return 0;
    const uint32_t r = reset_weapon_state();
    if (r != 2) return r;
    if (!pm[0xa]) return 1;
    const uint8_t* const now = *reinterpret_cast<uint8_t**>(pm);
    uint32_t anim = 0;
    if (at<float>(now, 0x88) > title_global<float>(0x0083ec18)) anim = 0x17;  // 0.9
    if ((at<uint32_t>(now, 0x4ec) & 0xfffffdffu) != anim) PM_StartLegsAnim(0, 0, anim);
    return 1;
}

// ECX = weapon, EDX = player state.
PLATFORM_ASM_ENTRY(BG_GetFreeWeaponSlot_entry,
                   "pushl %edx\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(BG_GetFreeWeaponSlot) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL BG_GetFreeWeaponSlot(uint32_t weapon, const uint8_t* ps) {
    // The slot (player state bytes +0x3f8 + slot) a weapon of the def's slot class (+0x9c) can take: classes 1-2
    // the first free of slots 1 and 2, classes 3-6 their own slot when free; else 0.
    const uint32_t kind = at<uint32_t>(pointer<uint8_t>(title_global<uint32_t>(title_global<uint32_t>(0x0095f4d8) + weapon * 4)), 0x9c);
    if (kind == 1 || kind == 2) {
        if (!at<uint8_t>(ps, 0x3f9)) return 1;
        if (!at<uint8_t>(ps, 0x3fa)) return 2;
        return 0;
    }
    if (kind >= 3 && kind <= 6) return at<uint8_t>(ps, 0x3f8 + kind) ? 0 : kind;
    return 0;
}

// EAX = weapon, EDI = player state, ESI = slot.
PLATFORM_ASM_ENTRY(PM_SetSlotWeapon_entry,
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(PM_SetSlotWeapon) "addl $12, %esp\n"
                   "ret")

uint32_t XB_CDECL PM_SetSlotWeapon(uint32_t weapon, uint8_t* ps, uint32_t slot) {
    // An owned weapon (bit in +0x3f0) goes into a slot its class fits (classes 1-2: slot 1 or 2; 3-6: their own):
    // the slot byte (+0x3f8 + slot) = weapon, 1; else 0.
    if (!(at<uint32_t>(ps, 0x3f0 + (static_cast<int32_t>(weapon) >> 5) * 4) & (1u << (weapon & 31)))) return 0;
    const uint32_t kind = at<uint32_t>(pointer<uint8_t>(title_global<uint32_t>(title_global<uint32_t>(0x0095f4d8) + weapon * 4)), 0x9c);
    bool fits;
    if (kind == 1 || kind == 2) fits = slot == 1 || slot == 2;
    else if (kind >= 3 && kind <= 6) fits = slot == kind;
    else fits = false;
    if (!fits) return 0;
    at<uint8_t>(ps, 0x3f8 + slot) = static_cast<uint8_t>(weapon);
    return 1;
}

namespace {

uint8_t* weapon_def_at(int32_t index) {
    return pointer<uint8_t>(title_global<uint32_t>(title_global<uint32_t>(0x0095f4d8) + static_cast<uint32_t>(index) * 4));
}

}  // namespace

void XB_CDECL BG_SetupAltWeaponIndices() {
    // Each weapon's alternate (+0x718, by the name at +0x714 against the defs' names +8, __stricmp) is linked,
    // following the chain from the first weapon of each one; the alternate must share the slot class (+0x9c)
    // and +0xa0, exist, and the chain lead back to its start (__break otherwise).
    using StrICmp = int32_t XB_CDECL(const char*, const char*);
    using Break = void XB_CDECL(int32_t, const char*, uint32_t, uint32_t);
    const auto last = [] { return title_global<int32_t>(0x0095f4d4); };
    for (int32_t i = 1; i <= last(); ++i) at<uint32_t>(weapon_def_at(i), 0x718) = 0;
    for (int32_t i = 1; i <= last(); ++i) {
        uint8_t* def = weapon_def_at(i);
        const uint8_t* const first = def;
        if (at<uint32_t>(def, 0x718) || !*at<const char*>(def, 0x714)) continue;
        for (;;) {
            int32_t alt = 1;
            for (; alt <= last(); ++alt) {
                const uint8_t* const other = weapon_def_at(alt);
                if (own_function<StrICmp>(&title_stricmp)(at<const char*>(def, 0x714), at<const char*>(other, 8))) continue;
                at<int32_t>(def, 0x718) = alt;
                if (at<uint32_t>(def, 0x9c) != at<uint32_t>(other, 0x9c))
                    title_function<Break>(0x002938f0)(1, pointer<const char>(0x00835510), at<uint32_t>(def, 8), at<uint32_t>(other, 8));
                if (at<uint32_t>(def, 0xa0) != at<uint32_t>(other, 0xa0))
                    title_function<Break>(0x002938f0)(1, pointer<const char>(0x008354c0), at<uint32_t>(def, 8), at<uint32_t>(other, 8));
                break;
            }
            if (!at<uint32_t>(def, 0x718))
                title_function<Break>(0x002938f0)(1, pointer<const char>(0x00835490), at<uint32_t>(def, 0x714), at<uint32_t>(def, 8));
            def = weapon_def_at(alt);
            if (at<uint32_t>(def, 0x718)) break;
        }
        if (def != first)
            title_function<Break>(0x002938f0)(1, pointer<const char>(0x00835468), at<uint32_t>(first, 8), at<uint32_t>(first, 0x714));
    }
}

void XB_CDECL BG_SetupWeaponItems() {
    // The weapon items (bg_itemlist 0x0096bd44, 0x34 each from weapon 1): pickup name (+0x578) and its
    // lowercase rotate-xor hash, model (+0x580, else +0x57c), icon/sounds (+0x584/+0x58c/+0xc/+0x590), type 1,
    // the weapon index, +0x598/+0x5a0. The items after them (to 0x88) of type 2 (ammo) find their weapon by
    // name prefix (+0x1c against the def names) for index and +0x598/+0x5a0; weapon 1 when none.
    uint32_t item = 0x0096bd44;
    int32_t w = 1;
    for (; w <= title_global<int32_t>(0x0095f4d4); ++w, item += 0x34) {
        const uint8_t* const def = weapon_def_at(w);
        const char* const name = at<const char*>(def, 0x578);
        uint32_t hash = 0;
        at<const char*>(pointer<uint8_t>(item), 4) = name;
        if (name)
            for (const char* c = name; *c; ++c) {
                char ch = *c;
                if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + 0x20);
                hash = ((hash >> 27) | (hash << 5)) ^ static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(ch)));
            }
        uint8_t* const it = pointer<uint8_t>(item);
        at<uint32_t>(it, 0) = hash;
        const char* model = at<const char*>(def, 0x580);
        if (!model || !*model) model = at<const char*>(def, 0x57c);
        at<const char*>(it, 0xc) = model;
        at<uint32_t>(it, 0x10) = 0;
        at<uint32_t>(it, 0x14) = at<uint32_t>(def, 0x584);
        at<uint32_t>(it, 0x18) = at<uint32_t>(def, 0x58c);
        at<uint32_t>(it, 0x1c) = at<uint32_t>(def, 0xc);
        at<int32_t>(it, 0x28) = w;
        at<uint32_t>(it, 0x20) = at<uint32_t>(def, 0x590);
        at<uint32_t>(it, 0x24) = 1;
        at<uint32_t>(it, 0x2c) = at<uint32_t>(def, 0x598);
        at<uint32_t>(it, 0x30) = at<uint32_t>(def, 0x5a0);
    }
    if (w >= 0x88) return;
    for (int32_t count = 0x88 - w; count; --count, item += 0x34) {
        uint8_t* const it = pointer<uint8_t>(item);
        if (at<int32_t>(it, 0x24) != 2) continue;
        for (int32_t k = 1; k <= title_global<int32_t>(0x0095f4d4); ++k) {
            const uint8_t* const def = weapon_def_at(k);
            const char* const name = at<const char*>(def, 8);
            if (Q_stricmpn(at<const char*>(it, 0x1c), name, static_cast<int32_t>(std::strlen(name)))) continue;
            at<int32_t>(it, 0x28) = k;
            at<uint32_t>(it, 0x2c) = at<uint32_t>(def, 0x598);
            at<uint32_t>(it, 0x30) = at<uint32_t>(def, 0x5a0);
            break;
        }
        if (at<int32_t>(it, 0x28) == -1) {
            const uint8_t* const def = weapon_def_at(1);
            at<int32_t>(it, 0x28) = 1;
            at<uint32_t>(it, 0x2c) = at<uint32_t>(def, 0x598);
            at<uint32_t>(it, 0x30) = at<uint32_t>(def, 0x5a0);
        }
    }
}

void XB_CDECL BG_SetupWeaponTransitionRates() {
    // Per weapon the raise/drop rates: +0x868 = 1 / +0x678 ms (1/300 without), +0x86c = 1 / +0x67c (0.002).
    const float one = title_global<float>(0x0083eb20);
    for (int32_t i = 1; i <= title_global<int32_t>(0x0095f4d4); ++i) {
        uint8_t* const def = weapon_def_at(i);
        const int32_t a = at<int32_t>(def, 0x678);
        at<float>(def, 0x868) = a > 0 ? one / static_cast<float>(a) : title_global<float>(0x0083ec20);
        const int32_t b = at<int32_t>(def, 0x67c);
        at<float>(def, 0x86c) = b > 0 ? one / static_cast<float>(b) : title_global<float>(0x0083ec1c);
    }
}

void XB_CDECL PM_UpdateAimDownSightFrac() {
    // The ADS fraction (+0x88) moves toward 1 while aiming (pm_flags 0x20; or def +0x6f4 rechambering in state 3)
    // at def +0x868 per ms, toward 0 at +0x86c, with the raise (0x15) / drop (0x16) weapon anims as it leaves an
    // end; weapons without ADS (+0x6dc) and no 0x106000 flags stay at 0. Not while firing/reloading (states
    // 5/0xe after the fire delay +0x80c unless type 3; with +0x708 also 6-9) or in state 4 without +0x6e0.
    // Type 8 weapons follow the ADS states (0xd drop, 0xc raise). With flags 0x106000 it jumps to the end.
    // At 1 the ADS anim (0x17) plays; at 0 anim 0 (outside states 5/7).
    uint8_t* const pm = title_global<uint8_t*>(0x0092eff4);
    const auto ps = [pm] { return *reinterpret_cast<uint8_t**>(pm); };
    const auto def = [] { return title_global<uint8_t*>(0x00960020); };
    if (!at<uint32_t>(def(), 0x6dc) && !(at<uint32_t>(ps(), 0xc0) & 0x106000)) {
        at<float>(ps(), 0x88) = 0.0f;
        return;
    }
    bool aim = false;
    bool blocked = false;
    const uint32_t alt = at<uint32_t>(def(), 0x708);
    const auto fired = [&] { return static_cast<int32_t>(at<uint32_t>(ps(), 0x34) - at<uint32_t>(def(), 0x80c)) > 0; };
    if (!alt) {
        const int32_t state = at<int32_t>(ps(), 0x84);
        if ((state == 5 || state == 0xe) && fired() && at<int32_t>(def(), 0x98) != 3) blocked = true;
    } else if (at<int32_t>(def(), 0x98) != 3) {
        const int32_t state = at<int32_t>(ps(), 0x84);
        if (state == 5 || state == 6 || state == 0xe || state == 7 || state == 8) blocked = true;
        else if (state == 9 && fired()) blocked = true;
    }
    if (!blocked && !at<uint32_t>(def(), 0x6e0) && at<int32_t>(ps(), 0x84) == 4) blocked = true;
    if (!blocked && (at<uint8_t>(ps(), 0x2c) & 0x20)) aim = true;
    if (at<uint32_t>(def(), 0x6f4) && at<uint32_t>(ps(), 0x38) && at<int32_t>(ps(), 0x84) == 3) aim = true;
    const float zero = 0.0f, one = title_global<float>(0x0083eb20);
    if (at<int32_t>(def(), 0x98) == 8) {
        if (at<int32_t>(ps(), 0x84) == 0xd) aim = false;
        else if (at<int32_t>(ps(), 0x84) == 0xc) aim = true;
    }
    if (at<float>(ps(), 0x88) == (aim ? one : zero)) return;
    if (at<uint32_t>(ps(), 0xc0) & 0x106000) {
        at<float>(ps(), 0x88) = aim ? one : zero;
    } else if (aim) {
        if (at<float>(ps(), 0x88) == zero && at<int32_t>(ps(), 0x84) != 7 && at<int32_t>(ps(), 0x84) != 0xe)
            PM_StartLegsAnim(0, 0, 0x15);
        at<float>(ps(), 0x88) = static_cast<float>(title_global<int32_t>(0x0095ffa8)) * at<float>(def(), 0x868) + at<float>(ps(), 0x88);
    } else {
        if (at<float>(ps(), 0x88) == one && at<int32_t>(ps(), 0x84) != 7) PM_StartLegsAnim(0, 0, 0x16);
        at<float>(ps(), 0x88) = at<float>(ps(), 0x88) - static_cast<float>(title_global<int32_t>(0x0095ffa8)) * at<float>(def(), 0x86c);
    }
    uint8_t* const p = ps();
    if (!(at<float>(p, 0x88) < one) && at<float>(p, 0x88) == at<float>(p, 0x88)) {
        at<float>(p, 0x88) = one;
        uint8_t* const q = ps();
        if (at<int32_t>(q, 0x24) >= 4 || !pm[0xa]) return;
        if (at<int32_t>(def(), 0x94) == 5) at<uint32_t>(q, 0x4ec) = 0x17;
        else at<uint32_t>(q, 0x4ec) = (~at<uint32_t>(q, 0x4ec) & 0x200) | 0x17;
        return;
    }
    if (zero < at<float>(p, 0x88) || at<float>(p, 0x88) != at<float>(p, 0x88)) return;
    at<float>(p, 0x88) = zero;
    const int32_t now = at<int32_t>(ps(), 0x84);
    if (now == 7 || now == 5) return;
    PM_StartLegsAnim(0, 0, 0);
}

// ECX comes back as the view height and EDX as the prone height unless crouched; XMM0 kept (the callers read them).
PLATFORM_ASM_ENTRY(PM_CheckViewHeightStance_entry,
                   "pushl %edx\n"
                   "pushl %ecx\n"
                   "subl $16, %esp\n"
                   "movups %xmm0, (%esp)\n"
                   "leal 16(%esp), %eax\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(PM_CheckViewHeightStance) "addl $4, %esp\n"
                   "movups (%esp), %xmm0\n"
                   "addl $16, %esp\n"
                   "popl %ecx\n"
                   "popl %edx\n"
                   "ret")

uint32_t XB_CDECL PM_CheckViewHeightStance(uint32_t* ecx_edx) {
    // Reads the view height (+0xa8) against the crouch (+0x424) and prone (+0x420) heights; always 1.
    const uint8_t* const ps = pm_ps();
    ecx_edx[0] = at<uint32_t>(ps, 0xa8);
    if (ecx_edx[0] != at<uint32_t>(ps, 0x424)) ecx_edx[1] = at<uint32_t>(ps, 0x420);
    return 1;
}

// EAX = from height, ECX = to height; the fraction in XMM0, EAX = the lerp time (the player state when none);
// XMM4 kept.
PLATFORM_ASM_ENTRY(PM_ViewHeightLerpFrac_entry,
                   "subl $16, %esp\n"
                   "movups %xmm4, (%esp)\n"
                   "subl $4, %esp\n"
                   "pushl %esp\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(PM_ViewHeightLerpFrac) "addl $12, %esp\n"
                   "movss (%esp), %xmm0\n"
                   "addl $4, %esp\n"
                   "movups (%esp), %xmm4\n"
                   "addl $16, %esp\n"
                   "ret")

uint32_t XB_CDECL PM_ViewHeightLerpFrac(uint32_t from, uint32_t to, float* frac) {
    // The view-height lerp's progress (start time +0xb0, target +0xb4, flag +0xb8): 0 without a lerp or for
    // other heights; else (command time - start) / PM_ViewHeightLerpTime's time, clamped to 0..1.
    const uint8_t* const pm = title_global<uint8_t*>(0x0092eff4);
    const uint8_t* const ps = pm_ps();
    const int32_t start = at<int32_t>(ps, 0xb0);
    *frac = 0.0f;
    if (!start) return address_of(ps);
    if (from != 0xffffffffu && to != 0xffffffffu) {
        bool lerp;
        if (to != at<uint32_t>(ps, 0xb4)) lerp = false;
        else if (to != at<uint32_t>(ps, 0x424)) lerp = true;
        else if (from == at<uint32_t>(ps, 0x420) && !at<uint32_t>(ps, 0xb8)) lerp = true;
        else lerp = from == at<uint32_t>(ps, 0x428) && at<uint32_t>(ps, 0xb8);
        if (!lerp) return address_of(ps);
    }
    const uint32_t target = at<uint32_t>(ps, 0xb4);
    int32_t time;
    if (target == at<uint32_t>(ps, 0x420)) time = 0x190;
    else if (target == at<uint32_t>(ps, 0x424)) time = at<uint32_t>(ps, 0xb8) ? 0xc8 : 0x190;
    else time = 0xc8;
    float f = static_cast<float>(at<int32_t>(pm, 4) - start) / static_cast<float>(time);
    const float zero = 0.0f;
    if (zero > f) f = zero;
    else if (!(f <= title_global<float>(0x0083eb20)) && f == f) f = title_global<float>(0x0083eb20);
    *frac = f;
    return static_cast<uint32_t>(time);
}

namespace {

// The ground surface type: bits 20-24 of the ground trace's surface flags (0x0095ffe4); 0 with flag 0x2000.
uint32_t ground_surface() {
    const uint32_t flags = title_global<uint32_t>(0x0095ffe4);
    if (flags & 0x2000) return 0;
    return (flags >> 20) & 0x1f;
}

}  // namespace

// EDX kept (the caller reads it).
PLATFORM_ASM_ENTRY(PM_LandEventForSurface_entry,
                   "pushl %edx\n" PLATFORM_ASM_CALL(PM_LandEventForSurface) "popl %edx\n"
                   "ret")

uint32_t XB_CDECL PM_LandEventForSurface() {
    // The landing event: 0x5d plus the ground surface type.
    return ground_surface() + 0x5d;
}

uint32_t XB_CDECL PM_RunStepEventForSurface() {
    const uint32_t surface = ground_surface();
    return surface ? surface + 1 : 0;
}

uint32_t XB_CDECL PM_WalkStepEventForSurface() {
    const uint32_t surface = ground_surface();
    return surface ? surface + 0x18 : 0;
}

uint32_t XB_REGS2 XB_CDECL PM_FootstepEventForSurface(uint32_t, uint32_t flags) {
    // EDX = pm_flags-like bits: prone (1) 0x2f, 0x10000 0x46, bit 7 or a moving player (+0x4c) 0x18, else 1; plus
    // the ground surface type (0 without one).
    const uint32_t surface = ground_surface();
    if (!surface) return 0;
    if (flags & 1) return surface + 0x2f;
    if (flags & 0x10000) return surface + 0x46;
    if (flags & 0x80) return surface + 0x18;
    if (at<float>(pm_ps(), 0x4c) == title_global<float>(0x0083eafc)) return surface + 1;
    return surface + 0x18;
}

uint32_t XB_CDECL PM_JumpEventForSurface() {
    // 0x53 with pm_flags 0x10, else 0x46 plus the ground surface type (0 without one).
    if (at<uint8_t>(pm_ps(), 0x2c) & 0x10) return 0x53;
    const uint32_t surface = ground_surface();
    return surface ? surface + 0x46 : 0;
}

PLATFORM_ASM_ENTRY(BG_GetTotalReserveAmmo_entry,
                   "pushl 4(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(BG_GetTotalReserveAmmo) "addl $8, %esp\n"
                   "ret")

int32_t XB_CDECL BG_GetTotalReserveAmmo(int32_t weapon, const uint8_t* ps) {
    // EAX = weapon, cdecl player state: the reserve ammo (+0x110 by ammo index +0x598; clip-only weapons +0x6ec
    // their clip +0x280 by +0x5a0). With a shared ammo cap (+0x5b0 >= 0) the sum over every owned weapon (bits
    // +0x3f0) of that cap, each ammo or clip slot counted once.
    const uint8_t* const def = weapon_def_at(weapon);
    const int32_t shared = at<int32_t>(def, 0x5b0);
    if (shared < 0) {
        if (at<uint32_t>(def, 0x6ec)) return at<int32_t>(ps, 0x280 + at<uint32_t>(def, 0x5a0) * 4);
        return at<int32_t>(ps, 0x110 + at<uint32_t>(def, 0x598) * 4);
    }
    uint32_t seen_ammo[0x5c] = {};
    uint32_t seen_clip[0x5c] = {};
    int32_t total = 0;
    for (int32_t w = 1; w <= title_global<int32_t>(0x0095f4d4); ++w) {
        if (!(at<uint32_t>(ps, 0x3f0 + (w >> 5) * 4) & (1u << (w & 31)))) continue;
        const uint8_t* const other = weapon_def_at(w);
        if (at<int32_t>(other, 0x5b0) != shared) continue;
        if (at<uint32_t>(other, 0x6ec)) {
            const uint32_t clip = at<uint32_t>(other, 0x5a0);
            if (seen_clip[clip]) continue;
            seen_clip[clip] = 1;
            total += at<int32_t>(ps, 0x280 + clip * 4);
        } else {
            const uint32_t ammo = at<uint32_t>(other, 0x598);
            if (seen_ammo[ammo]) continue;
            seen_ammo[ammo] = 1;
            total += at<int32_t>(ps, 0x110 + ammo * 4);
        }
    }
    return total;
}

namespace {

using ToLower = int32_t XB_CDECL(int32_t);
using Break7 = void XB_CDECL(int32_t, const char*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

void lowercase_in_place(char* text) {
    for (; *text; ++text) *text = static_cast<char>(title_tolower(static_cast<int8_t>(*text)));  // tolower
}

// The name tables behind the clip/ammo/shared-cap indices: per weapon its name (def +name_offset, lowercased in
// place) is looked up in names (count at count_address, Q_stricmpn 99999); the index goes to +index_offset, and a
// new name is added with its size (+size_offset) in sizes. A found name of another size (not index 0) is checked
// against the earlier weapons; with break_format, one of that name and the table's size is reported (__break).
void setup_name_index(uint32_t name_offset, uint32_t index_offset, uint32_t size_offset, uint32_t names, uint32_t sizes,
                      uint32_t count_address, uint32_t break_format, bool skip_empty) {
    int32_t& count = title_global<int32_t>(count_address);
    for (int32_t w = 1; w <= title_global<int32_t>(0x0095f4d4); ++w) {
        uint8_t* const def = weapon_def_at(w);
        if (skip_empty) {
            at<int32_t>(def, index_offset) = -1;
            if (!*at<char*>(def, name_offset)) continue;
        }
        lowercase_in_place(at<char*>(def, name_offset));
        int32_t index = 0;
        for (; index < count; ++index) {
            const char* const known = title_global<const char*>(names + static_cast<uint32_t>(index) * 4);
            const char* const name = at<const char*>(def, name_offset);
            if (known && name && !Q_stricmpn(known, name, 99999)) break;
        }
        if (index < count) {
            at<int32_t>(def, index_offset) = index;
            if (title_global<uint32_t>(sizes + static_cast<uint32_t>(index) * 4) != at<uint32_t>(def, size_offset) && index) {
                for (int32_t k = 1; k < w; ++k) {
                    const uint8_t* const other = weapon_def_at(k);
                    const char* const known = title_global<const char*>(names + static_cast<uint32_t>(index) * 4);
                    const char* const name = at<const char*>(other, name_offset);
                    if (!known || !name) continue;
                    if (Q_stricmpn(known, name, 99999) || !break_format) continue;
                    if (at<uint32_t>(other, size_offset) != title_global<uint32_t>(sizes + static_cast<uint32_t>(index) * 4))
                        continue;
                    title_function<Break7>(0x002938f0)(1, pointer<const char>(break_format), at<uint32_t>(def, name_offset),
                                                       at<uint32_t>(def, 8), at<uint32_t>(def, size_offset),
                                                       at<uint32_t>(other, 8), at<uint32_t>(other, size_offset));
                }
            }
        }
        if (index == count) {
            title_global<uint32_t>(names + static_cast<uint32_t>(index) * 4) = at<uint32_t>(def, name_offset);
            title_global<uint32_t>(sizes + static_cast<uint32_t>(index) * 4) = at<uint32_t>(def, size_offset);
            at<int32_t>(def, index_offset) = index;
            ++count;
        }
    }
}

}  // namespace

void XB_CDECL BG_SetupClipIndices() {
    // Clip names (+0x59c) to clip indices (+0x5a0; names 0x00928368, sizes +0x5a8 at 0x00935338, count 0x00929b14).
    setup_name_index(0x59c, 0x5a0, 0x5a8, 0x00928368, 0x00935338, 0x00929b14, 0, false);
}

void XB_CDECL BG_SetupSharedAmmoCaps() {
    // Shared ammo cap names (+0x5ac, none when empty: -1) to indices (+0x5b0; names 0x00929990, caps +0x5b4 at
    // 0x00934a68, count 0x00929c1c).
    setup_name_index(0x5ac, 0x5b0, 0x5b4, 0x00929990, 0x00934a68, 0x00929c1c, 0x00835560, true);
}

void XB_CDECL BG_SetupAmmoIndices() {
    // Ammo names (+0x594) to ammo indices (+0x598; names 0x00935128, max ammo +0x5a4 at 0x00929818, count
    // 0x0092f5ec).
    setup_name_index(0x594, 0x598, 0x5a4, 0x00935128, 0x00929818, 0x0092f5ec, 0x008355c8, false);
}

// ---- push 14: weapon states, reload, ADS transitions, jump, small pmove helpers ----

namespace {

uint8_t* pm_global() { return title_global<uint8_t*>(0x0092eff4); }
uint8_t*& pm_ps_ref(uint8_t* pm) { return *reinterpret_cast<uint8_t**>(pm); }
const uint8_t* cur_weapon_def() { return title_global<uint8_t*>(0x00960020); }

// The player state's predictable event ring (+0xc4 sequence, events +0xc8, parms +0xd8), inlined in the title.
void add_ps_event(uint8_t* ps, uint32_t event, uint32_t parm) {
    at<uint32_t>(ps, 0xc8 + (at<uint32_t>(ps, 0xc4) & 3) * 4) = event;
    at<uint32_t>(ps, 0xd8 + (at<uint32_t>(ps, 0xc4) & 3) * 4) = parm;
    ++at<uint32_t>(ps, 0xc4);
}

// sqrt(gravity * (height + height)) on the x87 (FILD/FLD/FADD/FMULP/FSQRT/FSTP), stored as float.
float x87_jump_speed(int32_t gravity, float height) {
    float r;
    __asm__("fildl %1\n\tflds %2\n\tfadd %%st(0), %%st\n\tfmulp\n\tfsqrt\n\tfstps %0"
            : "=m"(r)
            : "m"(gravity), "m"(height)
            : "st", "st(1)");
    return r;
}

// The idle weapon anim (0x17 above 0.9 ADS, else 0) unless already playing, for a player with anims (pm +0xa).
void start_idle_anim(uint8_t* pm, const uint8_t* ps) {
    if (!pm[0xa]) return;
    uint32_t anim = 0;
    if (at<float>(ps, 0x88) > title_global<float>(0x0083ec18)) anim = 0x17;  // 0.9
    if ((at<uint32_t>(ps, 0x4ec) & 0xfffffdffu) != anim) PM_StartLegsAnim(0, 0, anim);
}

}  // namespace

uint32_t XB_CDECL BG_FindItem_body(const char* name) {
    // The bg_itemlist entry (0x0096bd10, 0x34 bytes, 1..134) by name: weapon items (up to bg_lastWeaponIndex) by
    // the weapon def's name (+8), the rest by their pickup name (+0x1c) or class name (+4); 0 when none.
    int32_t index = 1;
    for (uint32_t it = 0x0096bd48; it < 0x0096d8b4; it += 0x34, ++index) {
        if (index <= title_global<int32_t>(0x0095f4d4)) {
            const char* const weapon = at<const char*>(weapon_def_at(index), 8);
            if (name && weapon && !Q_stricmpn(name, weapon, 99999))
                return 0x0096bd10 + static_cast<uint32_t>(index) * 0x34;
            continue;
        }
        const char* const pickup = title_global<const char*>(it + 0x18);
        if (pickup && name && !Q_stricmpn(pickup, name, 99999)) return it - 4;
        const char* const classname = title_global<const char*>(it);
        if (classname && name && !Q_stricmpn(classname, name, 99999)) return it - 4;
    }
    return 0;
}

// ESI = name.
PLATFORM_ASM_ENTRY(BG_FindItem_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(BG_FindItem_body) "addl $4, %esp\n"
                   "ret")

void XB_CDECL PM_WeaponMeleeStrike() {
    // The melee hit: weapon time at least the def's melee time minus its delay (+0x5ec - +0x5d8), event 0xaa,
    // weapon state 0xb (prone players get pm_flags 0x400).
    uint8_t* const pm = pm_global();
    const uint8_t* const def = cur_weapon_def();
    const int32_t time = at<int32_t>(def, 0x5ec) - at<int32_t>(def, 0x5d8);
    if (at<int32_t>(pm_ps_ref(pm), 0x34) < time) at<int32_t>(pm_ps_ref(pm), 0x34) = time;
    add_ps_event(pm_ps_ref(pm), 0xaa, 0);
    at<uint32_t>(pm_ps_ref(pm), 0x84) = 0xb;
    uint8_t* const ps = pm_ps_ref(pm);
    if (at<uint8_t>(ps, 0x2c) & 1) at<uint32_t>(ps, 0x2c) |= 0x400;
}

void XB_CDECL PM_RemoveEmptyWeapon() {
    // A weapon flagged to go when empty (+0x6ec; not type 8) with an empty clip and no reserve leaves its slot
    // (BG_RemoveWeaponFromSlot) with event 0x97.
    uint8_t* const pm = pm_global();
    uint8_t* const ps = pm_ps_ref(pm);
    const uint32_t weapon = at<uint32_t>(ps, 0x80);
    const uint8_t* const def = weapon_def_at(static_cast<int32_t>(weapon));
    if (!at<uint32_t>(def, 0x6ec)) return;
    if (at<int32_t>(cur_weapon_def(), 0x98) == 8) return;
    if (at<uint32_t>(ps, 0x280 + at<uint32_t>(def, 0x5a0) * 4)) return;
    if (at<uint32_t>(ps, 0x110 + at<uint32_t>(def, 0x598) * 4)) return;
    BG_RemoveWeaponFromSlot(weapon, ps);
    add_ps_event(pm_ps_ref(pm), 0x97, 0);
}

uint32_t XB_CDECL PM_WeaponCanReload() {
    // 1 when there is reserve ammo (+0x110) and the clip (+0x280) is below its size (0x00935338); with a reload
    // chunk (+0x704, amount +0x70c below the clip size) only when a whole chunk fits, else only from empty.
    const uint8_t* const ps = pm_ps_ref(pm_global());
    const uint8_t* const def = weapon_def_at(at<int32_t>(ps, 0x80));
    const uint32_t clip = at<uint32_t>(def, 0x5a0);
    if (!at<int32_t>(ps, 0x110 + at<uint32_t>(def, 0x598) * 4)) return 0;
    const int32_t in_clip = at<int32_t>(ps, 0x280 + clip * 4);
    const int32_t size = title_global<int32_t>(0x00935338 + clip * 4);
    if (in_clip >= size) return 0;
    const uint8_t* const cur = cur_weapon_def();
    if (!at<uint32_t>(cur, 0x704)) return 1;
    const int32_t chunk = at<int32_t>(cur, 0x70c);
    if (!chunk || chunk >= size) return in_clip ? 0 : 1;
    return size - in_clip < chunk ? 0 : 1;
}

namespace {

// The ADS raise / drop start: weapon state `target` unless already in it (or, for type-8 weapons, in `other`),
// weapon time from the def, the ADS weapon anim and an event.
void begin_ads(int32_t target, int32_t other, uint32_t time_offset, uint32_t anim, uint32_t event) {
    uint8_t* const pm = pm_global();
    uint8_t* ps = pm_ps_ref(pm);
    const int32_t state = at<int32_t>(ps, 0x84);
    if (state == other && at<int32_t>(cur_weapon_def(), 0x98) == 8) return;
    if (state == target) return;
    at<int32_t>(ps, 0x84) = target;
    at<uint32_t>(pm_ps_ref(pm), 0x34) = at<uint32_t>(cur_weapon_def(), time_offset);
    ps = pm_ps_ref(pm);
    if (at<int32_t>(ps, 0x24) < 4 && pm[0xa]) {
        if (at<int32_t>(cur_weapon_def(), 0x94) == 5)
            at<uint32_t>(ps, 0x4ec) = anim;
        else
            at<uint32_t>(ps, 0x4ec) = (~at<uint32_t>(ps, 0x4ec) & 0x200) | anim;
    }
    add_ps_event(pm_ps_ref(pm), event, 0);
}

}  // namespace

void XB_CDECL PM_Weapon_BeginAdsDrop() { begin_ads(0xd, 0xc, 0x67c, 0x14, 0xa1); }

void XB_CDECL PM_Weapon_BeginAdsRaise() { begin_ads(0xc, 0xd, 0x678, 0x13, 0xa0); }

void XB_CDECL PM_SetWeaponReady() {
    // Weapon state 0 with the idle anim.
    uint8_t* const pm = pm_global();
    start_idle_anim(pm, pm_ps_ref(pm));
    at<uint32_t>(pm_ps_ref(pm), 0x84) = 0;
}

void XB_CDECL PM_UpdateStaminaBreathEvent() {
    // While pm_flags 0x20000: every 1700 ms (+0x44 time of the last) event 0x8c; otherwise the time is cleared
    // once 1700 ms passed.
    uint8_t* const pm = pm_global();
    uint8_t* const ps = pm_ps_ref(pm);
    const int32_t last = at<int32_t>(ps, 0x44);
    if (at<uint32_t>(ps, 0x2c) & 0x20000) {
        if (static_cast<int32_t>(static_cast<uint32_t>(last) + 0x6a4u) >= at<int32_t>(pm, 4)) return;
        add_ps_event(ps, 0x8c, 0);
        at<uint32_t>(pm_ps_ref(pm), 0x44) = at<uint32_t>(pm, 4);
        return;
    }
    if (last <= 0) return;
    if (static_cast<int32_t>(static_cast<uint32_t>(last) + 0x6a4u) >= at<int32_t>(pm, 4)) return;
    at<uint32_t>(ps, 0x44) = 0;
}

void XB_CDECL PM_AddFlagChangeEvents() {
    // pm +0x121 against its value before PM_CheckDuck (0x0096001c): event 0x92 when it got set, 0x93 when cleared.
    uint8_t* const pm = pm_global();
    if (!title_global<uint32_t>(0x0096001c)) {
        if (!pm[0x121]) return;
        add_ps_event(pm_ps_ref(pm), 0x92, 0);
        if (!title_global<uint32_t>(0x0096001c)) return;
    }
    if (pm[0x121]) return;
    add_ps_event(pm_ps_ref(pm), 0x93, 0);
}

void XB_CDECL PM_StartJump(float height) {
    // Leaves the ground: vertical speed sqrt(gravity (+0x48) * 2 * height), jump flags 0x2008, jump origin z.
    uint8_t* const pm = pm_global();
    title_global<uint32_t>(0x0095ffb0) = 0;  // pml.groundPlane
    title_global<uint32_t>(0x0095ffac) = 0;  // pml.walking
    at<uint32_t>(pm_ps_ref(pm), 0x60) = 0;
    uint8_t* ps = pm_ps_ref(pm);
    at<float>(ps, 0x18) = x87_jump_speed(at<int32_t>(ps, 0x48), height);
    at<uint32_t>(pm_ps_ref(pm), 0x2c) |= 0x2008;
    at<uint32_t>(pm_ps_ref(pm), 0x30) = 0;
    ps = pm_ps_ref(pm);
    at<uint32_t>(ps, 0x74) = at<uint32_t>(ps, 8);
}

uint32_t XB_CDECL PM_FinishWeaponMelee() {
    // Not for type-1 weapons (0): state 0xa strikes (PM_WeaponMeleeStrike), state 0xb ends in state 0 with the
    // idle anim (1).
    if (at<int32_t>(cur_weapon_def(), 0x94) == 1) return 0;
    uint8_t* const pm = pm_global();
    const uint8_t* const ps = pm_ps_ref(pm);
    const int32_t state = at<int32_t>(ps, 0x84);
    if (state == 0xa) {
        PM_WeaponMeleeStrike();
        return 1;
    }
    if (state != 0xb) return 0;
    start_idle_anim(pm, ps);
    at<uint32_t>(pm_ps_ref(pm), 0x84) = 0;
    return 1;
}

void XB_CDECL PM_StartFireAnim() {
    // The fire anim by ADS: above 0.99 anim 5 (6 with an empty clip), below 0.01 anim 2 (3 when empty).
    uint8_t* const ps = pm_ps_ref(pm_global());
    if (at<float>(ps, 0x88) > title_global<float>(0x0083ed08)) {  // 0.99
        const uint8_t* const def = weapon_def_at(at<int32_t>(ps, 0x80));
        PM_StartLegsAnim(0, 0, at<uint32_t>(ps, 0x280 + at<uint32_t>(def, 0x5a0) * 4) ? 5 : 6);
        return;
    }
    if (title_global<float>(0x0083eb94) > at<float>(ps, 0x88)) {  // 0.01
        const uint8_t* const def = weapon_def_at(at<int32_t>(ps, 0x80));
        PM_StartLegsAnim(0, 0, at<uint32_t>(ps, 0x280 + at<uint32_t>(def, 0x5a0) * 4) ? 2 : 3);
    }
}

void XB_CDECL PM_Weapon_CheckAdsTransition() {
    // Weapons of type 3 or 8 (+0x98) outside weapon states 1, 2, 10, 11 (5-9 only without pm_flags 0x20): with
    // pm_flags 0x20 an ADS raise until fully aimed, without it an ADS drop until fully down.
    const int32_t type = at<int32_t>(cur_weapon_def(), 0x98);
    if (type != 3 && type != 8) return;
    const uint8_t* const ps = pm_ps_ref(pm_global());
    const int32_t state = at<int32_t>(ps, 0x84);
    switch (state) {
    case 1: case 2: case 10: case 11:
        return;
    case 5: case 6: case 7: case 8: case 9:
        if (at<uint8_t>(ps, 0x2c) & 0x20) return;
        break;
    default:
        break;
    }
    const uint32_t ads = at<uint32_t>(ps, 0x2c) & 0x20;
    if (ads && at<float>(ps, 0x88) != title_global<float>(0x0083eb20) && state != 0xc) {  // 1
        PM_Weapon_BeginAdsRaise();
        return;
    }
    if (ads) return;
    if (at<float>(ps, 0x88) != title_global<float>(0x0083eafc) && state != 0xd) PM_Weapon_BeginAdsDrop();  // 0
}

void XB_CDECL BG_SetupWeaponHintStrings() {
    // Each weapon's hint string (+0x840, when not empty) to its index (+0x844; __break when out of slots).
    for (int32_t i = 1; i <= title_global<int32_t>(0x0095f4d4); ++i) {
        uint8_t* const def = weapon_def_at(i);
        const char* const text = at<const char*>(def, 0x840);
        if (!*text) continue;
        if (!G_FindHintStringIndex(&at<int32_t>(def, 0x844), text))
            title_function<void XB_CDECL(int32_t, const char*, int32_t)>(0x002938f0)(1, &title_global<char>(0x00835410),
                                                                                    0x20);  // __break
    }
}

uint32_t XB_CDECL BG_PlayerInTrajectoryBox_body(int32_t time, const uint8_t* ent, const float* origin) {
    // 1 when the origin lies within -36..36 (x, y) and -88..18 (z) of the entity's trajectory (+0x14) at the time.
    alignas(16) float pos[4] = {};
    const TitleRegs regs{static_cast<uint32_t>(time), 0, address_of(pos), 0, address_of(origin), address_of(ent + 0x14)};
    g_ent_call(own_address(&BG_EvaluateTrajectory_entry), &regs, 0, nullptr);  // BG_EvaluateTrajectory
    const float hi = title_global<float>(0x0083f01c), lo = title_global<float>(0x0083f018);  // 36, -36
    float d = origin[0] - pos[0];
    if (d > hi || lo > d) return 0;
    d = origin[1] - pos[1];
    if (d > hi || lo > d) return 0;
    d = origin[2] - pos[2];
    if (d > title_global<float>(0x0083eb00)) return 0;  // 18
    if (title_global<float>(0x0083f014) > d) return 0;  // -88
    return 1;
}

// EAX = time, ECX = entity, ESI = origin (the player state).
PLATFORM_ASM_ENTRY(BG_PlayerInTrajectoryBox_entry,
                   "pushl %esi\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(BG_PlayerInTrajectoryBox_body) "addl $12, %esp\n"
                   "ret")

uint32_t XB_CDECL PM_ProneUndoBlockedMove_body(const float* origin, const float* velocity) {
    // A prone player whose feet no longer fit (BG_CheckProneFeet, 30 / 60) goes back to the saved origin and
    // velocity (0); 1 when not prone, else BG_CheckProneFeet's nonzero result.
    uint8_t* const pm = pm_global();
    uint8_t* const ps = pm_ps_ref(pm);
    if (!(at<uint8_t>(ps, 0x2c) & 1)) return 1;
    using CheckProneFeet = uint32_t XB_REGS1 XB_CDECL(uint32_t, uint32_t, uint8_t*, uint32_t, uint32_t, uint8_t*, uint8_t*,
                                                      uint8_t*, uint32_t, uint32_t);
    const uint32_t r = own_function<CheckProneFeet>(&BG_CheckProneFeet_entry)(0, at<uint32_t>(ps, 0x7c), ps, 0x41f00000u,
                                                                  at<uint32_t>(ps, 0x450), ps + 0x4d8, ps + 0x4dc,
                                                                  ps + 0x4e0, at<uint32_t>(pm, 0x138), 0x42700000u);
    if (r) return r;
    for (uint32_t i = 0; i < 3; ++i) at<float>(pm_ps_ref(pm), i * 4) = origin[i];
    for (uint32_t i = 0; i < 3; ++i) at<float>(pm_ps_ref(pm), 0x10 + i * 4) = velocity[i];
    return 0;
}

// EAX = saved origin, ESI = saved velocity.
PLATFORM_ASM_ENTRY(PM_ProneUndoBlockedMove_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(PM_ProneUndoBlockedMove_body) "addl $8, %esp\n"
                   "ret")

void XB_REGS3 XB_CDECL PM_EnterReloadState(uint32_t, uint32_t, uint32_t start) {
    // Reload start 0xb (clip not empty) or 0xc (empty, +0x8c set; sound 0xf instead of 0xe): the old reload sound
    // (+0x90) freed, the new one looped, weapon time from the def (+0x5f4 empty / +0x5f0), weapon state 0xe.
    uint8_t* const pm = pm_global();
    uint32_t sound = 0xe;
    if (start == 0xb) {
        at<uint8_t>(pm_ps_ref(pm), 0x8c) = 0;
    } else if (start == 0xc) {
        at<uint8_t>(pm_ps_ref(pm), 0x8c) = 1;
        sound = 0xf;
    }
    const uint32_t old = at<uint32_t>(pm_ps_ref(pm), 0x90);
    if (old) FX_FreeEffectsById(title_global<FxIdList*>(0x0095f5b4), old);
    // The sound plays at the entity behind 0x0095f534 +4 (EAX), its weapon byte +3.
    const uint8_t* const ent = at<uint8_t*>(title_global<uint8_t*>(0x0095f534), 4);
    const uint8_t* const def = weapon_def_at(ent[3]);
    const TitleRegs regs{address_of(ent), 0, address_of(def), at<uint32_t>(def, 8), 0, 0};
    const uint32_t args[3] = {at<uint32_t>(def, 8), sound, 1};
    const uint32_t id = g_ent_call(own_address(&FXCmd_PlayWeaponSoundLoop_entry), &regs, 3, args);  // FXCmd_PlayWeaponSoundLoop
    at<uint32_t>(pm_ps_ref(pm), 0x90) = id;
    uint8_t* const ps = pm_ps_ref(pm);
    const uint8_t* const cur = cur_weapon_def();
    at<uint32_t>(ps, 0x34) = ps[0x8c] ? at<uint32_t>(cur, 0x5f4) : at<uint32_t>(cur, 0x5f0);
    at<uint32_t>(pm_ps_ref(pm), 0x84) = 0xe;
}

void XB_CDECL PM_StartReload() {
    // PM_EnterReloadState 0xb, or 0xc from an empty clip for weapons of type 0.
    const uint8_t* const ps = pm_ps_ref(pm_global());
    const uint8_t* const def = weapon_def_at(at<int32_t>(ps, 0x80));
    uint32_t start = 0xb;
    if (!at<uint32_t>(ps, 0x280 + at<uint32_t>(def, 0x5a0) * 4) && !at<uint32_t>(cur_weapon_def(), 0x94)) start = 0xc;
    PM_EnterReloadState(0, 0, start);
}

namespace {

// The weapon bit in a player state bit array (+0x3f0 owned, +0x400 ...): word weapon >> 5 (signed), bit weapon & 31.
bool ps_weapon_bit(const uint8_t* ps, uint32_t offset, int32_t weapon) {
    return (at<uint32_t>(ps, offset + static_cast<uint32_t>(weapon >> 5) * 4) & (1u << (weapon & 0x1f))) != 0;
}

// |a| / 240 on the x87 (FLD/FABS/FSTP, FLD/FDIV/FSTP), both stored as float.
float x87_abs_div(float a, float d) {
    float f;
    __asm__("flds %1\n\tfabs\n\tfstps %0" : "=m"(f) : "m"(a) : "st");
    float r;
    __asm__("flds %1\n\tfdivs %2\n\tfstps %0" : "=m"(r) : "m"(f), "m"(d) : "st");
    return r;
}

// a * b + c * d on the x87 (FLD/FMUL, FLD/FMUL, FADDP), stored as float.
float x87_mul_add_mul(float a, float b, float c, float d) {
    float r;
    __asm__("flds %1\n\tfmuls %2\n\tflds %3\n\tfmuls %4\n\tfaddp\n\tfstps %0"
            : "=m"(r)
            : "m"(a), "m"(b), "m"(c), "m"(d)
            : "st", "st(1)");
    return r;
}

// sqrt(z*z + y*y + x*x) on the x87, stored as float; `below` = one > the unrounded root (FCOMIP).
float x87_length3_below(const float* v, float one, bool* below) {
    float r;
    bool b;
    __asm__("flds %4\n\tfmuls %4\n\tflds %3\n\tfmuls %3\n\tfaddp\n\tflds %2\n\tfmuls %2\n\tfaddp\n\tfsqrt\n\t"
            "fsts %0\n\tflds %5\n\tfcomip %%st(1), %%st\n\tfstp %%st(0)\n\tseta %1"
            : "=m"(r), "=q"(b)
            : "m"(v[0]), "m"(v[1]), "m"(v[2]), "m"(one)
            : "st", "st(1)", "cc");
    *below = b;
    return r;
}

}  // namespace

int32_t XB_CDECL PM_ReloadClip() {
    // Moves reserve ammo (+0x110) into the clip (+0x280): what fits, at most the reserve, in chunks of +0x70c
    // (states 7/8: +0x710, nothing without it) when the chunk is below the clip size; returns the amount.
    uint8_t* const pm = pm_global();
    uint8_t* const ps = pm_ps_ref(pm);
    const int32_t state = at<int32_t>(ps, 0x84);
    const bool shotgun = state == 7 || state == 8;
    if (shotgun && !at<int32_t>(cur_weapon_def(), 0x710)) return 0;
    const uint8_t* const def = weapon_def_at(at<int32_t>(ps, 0x80));
    const uint32_t ammo = at<uint32_t>(def, 0x598);
    const int32_t reserve = at<int32_t>(ps, 0x110 + ammo * 4);
    const uint32_t clip = at<uint32_t>(def, 0x5a0);
    const int32_t size = title_global<int32_t>(0x00935338 + clip * 4);
    int32_t n = size - at<int32_t>(ps, 0x280 + clip * 4);
    if (n > reserve) n = reserve;
    int32_t chunk;
    bool limit = true;
    if (shotgun) {
        chunk = at<int32_t>(cur_weapon_def(), 0x710);
    } else {
        chunk = at<int32_t>(cur_weapon_def(), 0x70c);
        limit = chunk != 0;
    }
    if (limit && chunk < size && n > chunk) n = chunk;
    if (!n) return 0;
    at<int32_t>(ps, 0x110 + ammo * 4) -= n;
    at<int32_t>(pm_ps_ref(pm), 0x280 + clip * 4) += n;
    return n;
}

int32_t XB_CDECL PM_SetReloadDelay() {
    // The reload's second timer (+0x38): states 7/8 the def's +0x600 (at most +0x5fc), else the reload time
    // (+0x5f4 from empty for type-0 weapons, else +0x5f0; at most +0x5f8 when set); with +0x6d8 and the weapon's
    // +0x400 bit at most +0x5e4 (the weapon time without one), 1 when that is 0. Unchanged when 0; returns it.
    uint8_t* const ps = pm_ps_ref(pm_global());
    const uint8_t* const cur = cur_weapon_def();
    const int32_t state = at<int32_t>(ps, 0x84);
    int32_t t;
    if (state == 7 || state == 8) {
        t = at<int32_t>(cur, 0x600);
        if (t && t >= at<int32_t>(cur, 0x5fc)) t = at<int32_t>(cur, 0x5fc);
    } else {
        const uint8_t* const def = weapon_def_at(at<int32_t>(ps, 0x80));
        if (!at<int32_t>(ps, 0x280 + at<uint32_t>(def, 0x5a0) * 4) && !at<int32_t>(cur, 0x94))
            t = at<int32_t>(cur, 0x5f4);
        else
            t = at<int32_t>(cur, 0x5f0);
        const int32_t cap = at<int32_t>(cur, 0x5f8);
        if (cap && cap < t) t = cap;
    }
    if (at<uint32_t>(cur, 0x6d8) && ps_weapon_bit(ps, 0x400, at<int32_t>(ps, 0x80))) {
        if (!t) t = at<int32_t>(ps, 0x34);
        if (at<int32_t>(cur, 0x5e4) < t) t = at<int32_t>(cur, 0x5e4);
        if (!t) {
            at<int32_t>(ps, 0x38) = 1;
            return 1;
        }
        at<int32_t>(ps, 0x38) = t;
        return t;
    }
    if (t) at<int32_t>(ps, 0x38) = t;
    return t;
}

void XB_CDECL PM_BeginWeaponReload() {
    // From weapon states 0/3/4 with a valid weapon: a staged reload (+0x708 with a start time +0x5fc: anim 0xd,
    // state 7, event 0x9b, PM_SetReloadDelay), else PM_StartReload.
    uint8_t* const pm = pm_global();
    const uint8_t* const ps = pm_ps_ref(pm);
    const int32_t state = at<int32_t>(ps, 0x84);
    if (state != 0 && state != 3 && state != 4) return;
    const int32_t weapon = at<int32_t>(ps, 0x80);
    if (!weapon || weapon > title_global<int32_t>(0x0095f4d4)) return;
    const uint8_t* const cur = cur_weapon_def();
    if (!at<uint32_t>(cur, 0x708) || !at<uint32_t>(cur, 0x5fc)) {
        PM_StartReload();
        return;
    }
    PM_StartLegsAnim(0, 0, 0xd);
    at<uint32_t>(pm_ps_ref(pm), 0x34) = at<uint32_t>(cur_weapon_def(), 0x5fc);
    at<uint32_t>(pm_ps_ref(pm), 0x84) = 7;
    add_ps_event(pm_ps_ref(pm), 0x9b, 0);
    PM_SetReloadDelay();
}

uint32_t XB_CDECL PM_WeaponCheckAmmo() {
    // 1 with ammo in the clip; else 0: with reserve ammo PM_BeginWeaponReload, without it event 0x97 (not for
    // type 8), the idle anim and 500 ms more weapon time.
    uint8_t* const pm = pm_global();
    uint8_t* const ps = pm_ps_ref(pm);
    const uint8_t* const def = weapon_def_at(at<int32_t>(ps, 0x80));
    if (at<int32_t>(ps, 0x280 + at<uint32_t>(def, 0x5a0) * 4) >= 1) return 1;
    if (at<int32_t>(ps, 0x110 + at<uint32_t>(def, 0x598) * 4) >= 1) {
        PM_BeginWeaponReload();
        return 0;
    }
    if (at<int32_t>(cur_weapon_def(), 0x98) != 8) add_ps_event(ps, 0x97, 0);
    start_idle_anim(pm, pm_ps_ref(pm));
    at<int32_t>(pm_ps_ref(pm), 0x34) += 0x1f4;
    return 0;
}

void XB_CDECL PM_ReloadAmmoAtTime() {
    // A staged reload (+0x6d8 and the weapon's +0x400 bit): the bit cleared, event 0xa8, and while weapon time is
    // left the next stage's delay (+0x38: as PM_SetReloadDelay less +0x5e4, or less 1, when at least 1);
    // otherwise (and in states 7/8 only with +0x600) PM_ReloadClip.
    const uint8_t* const cur = cur_weapon_def();
    uint8_t* const pm = pm_global();
    if (at<uint32_t>(cur, 0x6d8)) {
        uint8_t* ps = pm_ps_ref(pm);
        const int32_t weapon = at<int32_t>(ps, 0x80);
        if (ps_weapon_bit(ps, 0x400, weapon)) {
            at<uint32_t>(ps, 0x400 + static_cast<uint32_t>(weapon >> 5) * 4) &= ~(1u << (weapon & 0x1f));
            add_ps_event(pm_ps_ref(pm), 0xa8, 0);
            ps = pm_ps_ref(pm);
            const int32_t state = at<int32_t>(ps, 0x84);
            const uint8_t* const def_now = cur_weapon_def();
            const bool shotgun = state == 7 || state == 8;
            if (shotgun && !at<int32_t>(def_now, 0x600)) return;
            if (at<int32_t>(ps, 0x34)) {
                int32_t t;
                if (shotgun) {
                    t = at<int32_t>(def_now, 0x600);
                    if (t >= at<int32_t>(def_now, 0x5fc)) t = at<int32_t>(def_now, 0x5fc);
                } else {
                    const uint8_t* const def = weapon_def_at(at<int32_t>(ps, 0x80));
                    if (!at<int32_t>(ps, 0x280 + at<uint32_t>(def, 0x5a0) * 4) && !at<int32_t>(def_now, 0x94))
                        t = at<int32_t>(def_now, 0x5f4);
                    else
                        t = at<int32_t>(def_now, 0x5f0);
                    const int32_t cap = at<int32_t>(def_now, 0x5f8);
                    if (cap && cap < t) t = cap;
                }
                int32_t d = at<int32_t>(def_now, 0x5e4);
                if (d >= t) d = 1;
                t -= d;
                if (t >= 1) {
                    at<int32_t>(ps, 0x38) = t;
                    return;
                }
            }
        }
    }
    PM_ReloadClip();
}

uint32_t XB_CDECL PM_UpdateWeaponTimers() {
    // Counts the weapon time (+0x34) down by the frame; at its end, auto-repeat weapons (+0x6d4) held on fire with
    // the selected weapon and a loaded clip keep 1 ms and leave states 4 (PM_SetWeaponReady) / 3, 0xa, 0xb (idle
    // anim, state 0), else it is cleared. Then the second timer (+0x38): 1 when it ran out this frame.
    uint8_t* const pm = pm_global();
    uint8_t* ps = pm_ps_ref(pm);
    if (at<int32_t>(ps, 0x34)) {
        at<int32_t>(ps, 0x34) -= title_global<int32_t>(0x0095ffa8);  // pml.msec
        uint8_t* const now = pm_ps_ref(pm);
        if (at<int32_t>(now, 0x34) <= 0) {
            if (at<uint32_t>(cur_weapon_def(), 0x6d4) && (pm[8] & 1) && at<uint32_t>(now, 0x80) == pm[0xa] &&
                PM_GetClipAmmo(0, 0, at<uint32_t>(now, 0x80))) {
                at<int32_t>(now, 0x34) = 1;
                uint8_t* const p = pm_ps_ref(pm);
                const int32_t state = at<int32_t>(p, 0x84);
                if (state == 4) {
                    PM_SetWeaponReady();
                } else if (state == 3 || state == 0xa || state == 0xb) {
                    start_idle_anim(pm, p);
                    at<uint32_t>(pm_ps_ref(pm), 0x84) = 0;
                }
            } else {
                at<int32_t>(now, 0x34) = 0;
            }
        }
    }
    ps = pm_ps_ref(pm);
    if (!at<int32_t>(ps, 0x38)) return 0;
    at<int32_t>(ps, 0x38) -= title_global<int32_t>(0x0095ffa8);
    ps = pm_ps_ref(pm);
    if (at<int32_t>(ps, 0x38) > 0) return 0;
    at<int32_t>(ps, 0x38) = 0;
    return 1;
}

void XB_CDECL PM_CheckWeaponChange() {
    // Not while a type-1 clip weapon (+0x6e4) is mid-clip and owned, nor while the weapon time runs in states 3,
    // 0xa, 0xb (other states 0-2, 0xc+ only while the second timer runs). Forced drops (pm_flags 0x10, client
    // flag 0x100000 without 0x400000) put the weapon away; else a change to the command's weapon (cmd +0xa) when
    // owned or none, unless flag 0x4000 holds the current one; an unowned current weapon goes away.
    const uint8_t* const cur = cur_weapon_def();
    uint8_t* const pm = pm_global();
    if (at<int32_t>(cur, 0x94) == 1 && at<uint32_t>(cur, 0x6e4)) {
        const uint8_t* const ps = pm_ps_ref(pm);
        const int32_t clip = at<int32_t>(ps, 0x3c);
        if (clip < at<int32_t>(cur, 0x618) && clip && ps_weapon_bit(ps, 0x3f0, at<int32_t>(ps, 0x80))) return;
    }
    const uint8_t* const ps = pm_ps_ref(pm);
    if (at<int32_t>(ps, 0x34)) {
        switch (at<int32_t>(ps, 0x84)) {
        case 5: case 7: case 9: case 8: case 6: case 4:
            break;
        case 3: case 0xa: case 0xb:
            return;
        default:
            if (at<int32_t>(ps, 0x38)) return;
            break;
        }
    }
    const uint32_t flags = at<uint32_t>(ps, 0x2c);
    if (flags & 0x10 ||
        ((at<uint32_t>(ps, 0xc0) & 0x100000) && !(at<uint32_t>(ps, 0xc0) & 0x400000))) {
        const int32_t weapon = at<int32_t>(ps, 0x80);
        if (weapon) PM_BeginWeaponChange(weapon, 0);
        return;
    }
    const uint8_t wanted = pm[0xa];
    const int32_t weapon = at<int32_t>(ps, 0x80);
    if (weapon != wanted) {
        if (!(flags & 0x4000) || !weapon) {
            if (!wanted || ps_weapon_bit(ps, 0x3f0, wanted)) {
                PM_BeginWeaponChange(weapon, wanted);
                return;
            }
            if (!weapon) return;
        }
    } else if (!weapon) {
        return;
    }
    if (!ps_weapon_bit(ps, 0x3f0, weapon)) PM_BeginWeaponChange(weapon, 0);
}

void XB_REGS3 XB_CDECL PM_Accelerate(uint32_t, uint32_t, const float* wishdir, float wishspeed, float accel) {
    // Q3's PM_Accelerate: the speed along wishdir up to wishspeed by frametime * max(wishspeed, 100) * accel
    // (scaled by 1 / +0x478 while +0x60 is set).
    uint8_t* const pm = pm_global();
    uint8_t* const ps = pm_ps_ref(pm);
    const float current = at<float>(ps, 0x18) * wishdir[2] + at<float>(ps, 0x14) * wishdir[1] + wishdir[0] * at<float>(ps, 0x10);
    const float add = wishspeed - current;
    if (0.0f >= add) return;
    float speed = title_global<float>(0x0083eb90);  // 100
    if (!(speed > wishspeed)) speed = wishspeed;
    float accelspeed = title_global<float>(0x0095ffa4) * speed * accel;  // pml.frametime
    if (accelspeed > add) accelspeed = add;
    if (at<uint32_t>(ps, 0x60)) accelspeed = title_global<float>(0x0083eb20) / at<float>(ps, 0x478) * accelspeed;
    if (accelspeed > add) accelspeed = add;
    at<float>(ps, 0x10) = wishdir[0] * accelspeed + at<float>(ps, 0x10);
    uint8_t* p = pm_ps_ref(pm);
    at<float>(p, 0x14) = accelspeed * wishdir[1] + at<float>(p, 0x14);
    p = pm_ps_ref(pm);
    at<float>(p, 0x18) = wishdir[2] * accelspeed + at<float>(p, 0x18);
}

void XB_CDECL PM_Friction() {
    // Q3's PM_Friction: below speed 1 the velocity stops; else ground friction (walking, cmd +0x121 at most 1,
    // no surface flag 2 (0x0095ffe4), not pm_flags 0x200: frametime * max(speed, 100) (* 0.3 with 0x100) * 5.5)
    // plus +0x121 * frametime * speed slows it.
    uint8_t* const pm = pm_global();
    uint8_t* const ps = pm_ps_ref(pm);
    const uint32_t walking = title_global<uint32_t>(0x0095ffac);
    float vec[3] = {at<float>(ps, 0x10), at<float>(ps, 0x14), at<float>(ps, 0x18)};
    if (walking) vec[2] = 0.0f;
    bool stop;
    const float speed = x87_length3_below(vec, title_global<float>(0x0083eb20), &stop);
    if (stop) {
        at<float>(ps, 0x18) = 0.0f;
        at<float>(ps, 0x14) = 0.0f;
        at<float>(ps, 0x10) = 0.0f;
        return;
    }
    const uint8_t water = pm[0x121];
    const float frametime = title_global<float>(0x0095ffa4);
    float drop = 0.0f;
    if (water <= 1 && walking && !(title_global<uint8_t>(0x0095ffe4) & 2)) {
        const uint32_t flags = at<uint32_t>(ps, 0x2c);
        if (!(flags & 0x200)) {
            float control = title_global<float>(0x0083eb90);  // 100
            if (!(control > speed)) control = speed;
            if (flags & 0x100) control *= title_global<float>(0x0083066c);  // 0.3
            drop = frametime * control * title_global<float>(0x0082e8f8);   // 5.5
        }
    }
    if (water) drop = static_cast<float>(static_cast<int32_t>(water)) * frametime * speed + drop;
    float newspeed = speed - drop;
    if (0.0f > newspeed) newspeed = 0.0f;
    newspeed /= speed;
    at<float>(ps, 0x10) = at<float>(ps, 0x10) * newspeed;
    at<float>(ps, 0x14) = at<float>(ps, 0x14) * newspeed;
    at<float>(ps, 0x18) = newspeed * at<float>(ps, 0x18);
}

void XB_CDECL PM_UpdateStaminaMeter() {
    // Sprinting (pm_flags 0x10000; not pm_type 2, not 0x0092e79c) drains stamina (+0x45c) by msec/1000 * 2/3 and
    // stamps +0x460; below 0.5 event 0x8c and the out-of-breath flag 0x20000; at least 0. Not sprinting nor
    // holding sprint, 1 s after the stamp it refills by msec/1000 / 3 up to 1 (clearing 0x20000), then
    // PM_UpdateStaminaBreathEvent.
    uint8_t* const pm = pm_global();
    uint8_t* ps = pm_ps_ref(pm);
    if (at<uint32_t>(ps, 0x2c) & 0x10000) {
        if (at<int32_t>(ps, 0x24) == 2) return;
        if (title_global<uint32_t>(0x0092e79c)) return;
        at<uint32_t>(ps, 0x460) = at<uint32_t>(pm, 4);
        ps = pm_ps_ref(pm);
        const float drain = static_cast<float>(title_global<int32_t>(0x0095ffa8)) * title_global<float>(0x0083eb4c) *
                            title_global<float>(0x0082e908);
        at<float>(ps, 0x45c) = at<float>(ps, 0x45c) - drain;
        ps = pm_ps_ref(pm);
        if (title_global<float>(0x0083eb24) > at<float>(ps, 0x45c)) {  // 0.5
            add_ps_event(ps, 0x8c, 0);
            at<uint32_t>(pm_ps_ref(pm), 0x2c) |= 0x20000;
        }
        ps = pm_ps_ref(pm);
        if (0.0f > at<float>(ps, 0x45c)) at<float>(ps, 0x45c) = 0.0f;
        return;
    }
    if (!(pm[8] & 8)) {
        if (at<int32_t>(pm, 4) < static_cast<int32_t>(at<uint32_t>(ps, 0x460) + 0x3e8u)) return;
        at<float>(ps, 0x45c) = static_cast<float>(title_global<int32_t>(0x0095ffa8)) * title_global<float>(0x0083eb4c) *
                                   title_global<float>(0x0083ed74) +
                               at<float>(ps, 0x45c);
        ps = pm_ps_ref(pm);
        const float one = title_global<float>(0x0083eb20);
        if (at<float>(ps, 0x45c) >= one) {
            at<float>(ps, 0x45c) = one;
            ps = pm_ps_ref(pm);
            if (at<uint32_t>(ps, 0x2c) & 0x20000) at<uint32_t>(ps, 0x2c) &= 0xfffdffffu;
        }
    }
    PM_UpdateStaminaBreathEvent();
}

uint32_t XB_CDECL PM_CheckProneYaw_body(uint8_t* ps, float yaw, uint32_t arg) {
    // BG_CheckProneFeet for a prone turn toward yaw: the turn from +0xa0 (wrapped to -180..180) scaled by
    // 1 - |turn| / 240 back from yaw (0..360), feet height 60 * that + 45 * |turn| / 240.
    const float full = title_global<float>(0x0083ebcc);  // 360
    const float d = (yaw - at<float>(ps, 0xa0)) * title_global<float>(0x0083ebd8);
    float turn = static_cast<float>(static_cast<int32_t>(d) & 0xffff) * title_global<float>(0x0083ebd4);
    if (turn > title_global<float>(0x0083eb60)) turn = turn - full;  // 180
    const float frac = x87_abs_div(turn, title_global<float>(0x0083eed4));  // 240
    const float keep = title_global<float>(0x0083eb20) - frac;
    float x = yaw - keep * turn;
    if (0.0f > x) {
        do x = x + full;
        while (0.0f > x);
    } else if (x >= full) {
        do x = x - full;
        while (x >= full);
    }
    const float height = x87_mul_add_mul(keep, title_global<float>(0x0083eb3c), frac, title_global<float>(0x0083ef1c));
    uint32_t x_word, h_word;
    std::memcpy(&x_word, &x, 4);
    std::memcpy(&h_word, &height, 4);
    using CheckProneFeet = uint32_t XB_REGS1 XB_CDECL(uint32_t, uint32_t, uint8_t*, uint32_t, uint32_t, uint8_t*, uint8_t*,
                                                      uint8_t*, uint32_t, uint32_t);
    return own_function<CheckProneFeet>(&BG_CheckProneFeet_entry)(0, at<uint32_t>(ps, 0x7c), ps, 0x41f00000u, x_word, ps + 0x4d8,
                                                      ps + 0x4dc, ps + 0x4e0, arg, h_word);
}

// XMM0 = yaw, EAX = player state; one cdecl word.
PLATFORM_ASM_ENTRY(PM_CheckProneYaw_entry,
                   "pushl 4(%esp)\n"
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(PM_CheckProneYaw_body) "addl $12, %esp\n"
                   "ret")

uint32_t XB_REGS3 XB_CDECL BG_CanItemBeGrabbed(uint32_t, uint32_t touch, const uint8_t* es, const uint8_t* ps) {
    // The item (es +6, 1..0x87; __break otherwise) by type (+0x24): weapons (+0x28) when there is ammo room for an
    // owned one (or a slot-4 weapon on touch), or not owned unless touched; ammo when there is room for an owned
    // weapon or an unowned clip weapon (+0x6ec); health (3, 5) below max (+0x100 < +0x108); type 4 when any slot
    // weapon (+0x3f9..+0x3fc) has room; type 0 __break; else 0.
    using Break = void XB_CDECL(int32_t, const char*);
    const uint16_t index = at<uint16_t>(es, 6);
    if (index < 1 || index >= 0x88) title_function<Break>(0x002938f0)(1, &title_global<char>(0x008374f8));  // __break
    const uint32_t item = 0x0096bd10 + static_cast<uint32_t>(at<uint16_t>(es, 6)) * 0x34;
    switch (title_global<uint32_t>(item + 0x24)) {
    case 1: {
        const int32_t weapon = title_global<int32_t>(item + 0x28);
        if (at<int32_t>(weapon_def_at(weapon), 0x9c) == 4 && touch)
            return BG_AmmoRoomForWeapon(static_cast<uint32_t>(weapon), ps) > 0 ? 1 : 0;
        if (ps_weapon_bit(ps, 0x3f0, weapon)) return BG_AmmoRoomForWeapon(static_cast<uint32_t>(weapon), ps) > 0 ? 1 : 0;
        return touch ? 0 : 1;
    }
    case 2: {
        const int32_t weapon = title_global<int32_t>(item + 0x28);
        if (ps_weapon_bit(ps, 0x3f0, weapon)) return BG_AmmoRoomForWeapon(static_cast<uint32_t>(weapon), ps) > 0 ? 1 : 0;
        if (!at<uint32_t>(weapon_def_at(weapon), 0x6ec)) return 0;
        return BG_AmmoRoomForWeapon(static_cast<uint32_t>(weapon), ps) > 0 ? 1 : 0;
    }
    case 3:
    case 5:
        return at<int32_t>(ps, 0x100) < at<int32_t>(ps, 0x108) ? 1 : 0;
    case 4: {
        int32_t room = BG_AmmoRoomForWeapon(static_cast<uint32_t>(static_cast<int32_t>(at<int8_t>(ps, 0x3fc))), ps);
        room += BG_AmmoRoomForWeapon(static_cast<uint32_t>(static_cast<int32_t>(at<int8_t>(ps, 0x3fb))), ps);
        room += BG_AmmoRoomForWeapon(static_cast<uint32_t>(static_cast<int32_t>(at<int8_t>(ps, 0x3fa))), ps);
        room += BG_AmmoRoomForWeapon(static_cast<uint32_t>(static_cast<int32_t>(at<int8_t>(ps, 0x3f9))), ps);
        return room > 0 ? 1 : 0;
    }
    case 0:
        title_function<Break>(0x002938f0)(1, &title_global<char>(0x008374d8));  // __break
        return 0;
    default:
        return 0;
    }
}

void XB_CDECL BG_InitWeapons() {
    // Once (0x0098ad38): the weapon def table (0x0095f59c, 0x170 bytes from the hunk; 0x0095f5a0 = 1) as
    // bg_weaponDefs, the ammo and clip name tables reset to "none" (counts 1), the default weapon and the
    // per-weapon setup passes.
    if (title_global<uint8_t>(0x0098ad38)) return;
    title_global<uint8_t>(0x0098ad38) = 1;
    uint32_t defs = title_global<uint32_t>(0x0095f59c);
    if (!defs) {
        defs = address_of(Z_MallocDebug(0x170));
        title_global<uint32_t>(0x0095f59c) = defs;
        for (uint32_t i = 0; i < 0x5c; ++i) title_global<uint32_t>(defs + i * 4) = 0;
        title_global<uint32_t>(0x0095f5a0) = 1;
    } else if (!title_global<uint32_t>(0x0095f5a0)) {
        title_global<uint32_t>(0x0095f5a0) = 1;
    }
    title_global<uint32_t>(0x0095f4d8) = defs;
    if (!defs) title_function<void XB_CDECL(int32_t, const char*)>(0x002938f0)(1, &title_global<char>(0x008353e8));  // __break
    for (uint32_t i = 0; i < 0x5c; ++i) title_global<uint32_t>(0x00935128 + i * 4) = 0;
    for (uint32_t i = 0; i < 0x5c; ++i) title_global<uint32_t>(0x00928368 + i * 4) = 0;
    title_global<uint32_t>(0x00929818) = 0;
    title_global<uint32_t>(0x00935128) = 0x00832430;  // "none"
    title_global<uint32_t>(0x0092f5ec) = 1;
    title_global<uint32_t>(0x00935338) = 0;
    title_global<uint32_t>(0x00928368) = 0x00832430;
    title_global<uint32_t>(0x00929b14) = 1;
    BG_InitDefaultWeapon();
    BG_SetupWeaponTransitionRates();
    BG_SetupAmmoIndices();
    BG_SetupSharedAmmoCaps();
    BG_SetupClipIndices();
    BG_SetupWeaponItems();
    BG_SetupAltWeaponIndices();
    BG_SetupWeaponHintStrings();
}

void XB_CDECL PM_UpdateAdsState() {
    // From PmoveSingle: pm_flags 0x20 (aiming down the sights) while the ADS button (cmd +8 bit 0x10) is held
    // below pm_type 4 with an ADS weapon (+0x6dc) outside weapon states 1, 2, 0xa, 0xb, on the ground or pm_type
    // 1 (client flags 0x106000 force it); else cleared. Type-3 weapons (+0x98) also go prone (pm_flags 1, the
    // prone yaw +0x450; stance 7 (+0x47c) keeps flag 2 from +0x480 instead) when the feet fit (BG_CheckProneFeet),
    // else flag 0x8000 and event 0x8e / 0x8f (0x8f with flag 2) unless cmd +9 bit 2. Other prone players get
    // flag 0x400 unless the move bit (pm +0x34) and a move (+0x18/+0x19) hold the aim.
    uint8_t* const pm = pm_global();
    uint8_t* const ps = pm_ps_ref(pm);
    const int32_t type = at<int32_t>(ps, 0x24);
    if (type < 4 && (pm[8] & 0x10) && (at<uint32_t>(ps, 0xc0) & 0x106000)) {
        at<uint32_t>(ps, 0x2c) |= 0x20;
        return;
    }
    bool aim = type < 4 && (pm[8] & 0x10) && at<uint32_t>(cur_weapon_def(), 0x6dc);
    if (aim) {
        const int32_t state = at<int32_t>(ps, 0x84);
        aim = state != 2 && state != 1 && state != 0xa && state != 0xb;
    }
    if (aim && !title_global<uint32_t>(0x0095ffb0) && type != 1) aim = false;  // pml.groundPlane
    if (!aim) {
        at<uint32_t>(ps, 0x2c) &= 0xffffffdfu;
        return;
    }
    if (at<int32_t>(weapon_def_at(at<int32_t>(ps, 0x80)), 0x98) != 3) {
        if (!(at<uint8_t>(ps, 0x2c) & 1)) {
            at<uint32_t>(ps, 0x2c) |= 0x20;
            return;
        }
        if ((pm[0x34] & 0x10) && (pm[0x18] || pm[0x19])) return;
        at<uint32_t>(ps, 0x2c) |= 0x20;
        at<uint32_t>(pm_ps_ref(pm), 0x2c) |= 0x400;
        return;
    }
    const uint32_t flags = at<uint32_t>(ps, 0x2c);
    if (flags & 0x20) return;
    if (at<int32_t>(ps, 0x47c) == 7) {
        at<uint32_t>(ps, 0x450) = at<uint32_t>(ps, 0xa0);
        at<uint32_t>(pm_ps_ref(pm), 0x2c) |= 0x20;
        uint8_t* const p = pm_ps_ref(pm);
        const uint32_t f = at<uint32_t>(p, 0x2c);
        at<uint32_t>(p, 0x2c) = at<uint32_t>(p, 0x480) ? (f | 2) : (f & 0xfffffffdu);
        at<uint32_t>(pm_ps_ref(pm), 0x2c) &= 0xfffffffeu;
        return;
    }
    bool prone = (flags & 1) != 0;
    if (!prone && at<uint32_t>(ps, 0x60)) {
        using CheckProneFeet = uint32_t XB_REGS1 XB_CDECL(uint32_t, uint32_t, uint8_t*, uint32_t, uint32_t, uint8_t*,
                                                          uint8_t*, uint8_t*, uint32_t, uint32_t);
        prone = own_function<CheckProneFeet>(&BG_CheckProneFeet_entry)(0, at<uint32_t>(ps, 0x7c), ps, 0x41f00000u, at<uint32_t>(ps, 0xa0),
                                                           ps + 0x4d8, ps + 0x4dc, ps + 0x4e0, at<uint32_t>(pm, 0x138),
                                                           0x42700000u) != 0;
    }
    if (prone) {
        uint8_t* const p = pm_ps_ref(pm);
        if (!(at<uint8_t>(p, 0x2c) & 0x21)) at<uint32_t>(p, 0x450) = at<uint32_t>(p, 0xa0);
        at<uint32_t>(pm_ps_ref(pm), 0x2c) |= 1;
        at<uint32_t>(pm_ps_ref(pm), 0x2c) |= 0x20;
        return;
    }
    uint8_t* const p = pm_ps_ref(pm);
    if (at<uint8_t>(p, 0x2c) & 1) return;
    at<uint32_t>(p, 0x2c) |= 0x8000;
    if (pm[9] & 2) return;
    uint8_t* const q = pm_ps_ref(pm);
    add_ps_event(q, (at<uint8_t>(q, 0x2c) & 2) ? 0x8f : 0x8e, 0);
}

uint32_t XB_REGS1 XB_CDECL PM_SetWeaponFireTimers(uint32_t skip) {
    // After a shot (unless EAX is set, which comes back): type-1 clip weapons (+0x94) with an empty clip counter
    // (+0x3c) start a clip burst (+0x38 = +0x5e8; with ammo +0x3c = +0x618, anim 0x11, the HUD counter
    // (g_hudManager +0x34: +0x24 count, +0x28 size), event 0xa2, +0x38 = 10); others get the fire delay (+0x38 =
    // +0x5d4, ADS-scaled (1 - ADS) / +0x868 with +0x6f4) and fire time (+0x34 = +0x5dc), and the +0x400 bit with
    // +0x6d8. Weapon state 3 (prone: pm_flags 0x400).
    if (skip) return skip;
    const uint8_t* cur = cur_weapon_def();
    uint8_t* const pm = pm_global();
    if (at<int32_t>(cur, 0x94) == 1) {
        uint8_t* const ps = pm_ps_ref(pm);
        if (!at<int32_t>(ps, 0x3c)) {
            at<uint32_t>(ps, 0x38) = at<uint32_t>(cur, 0x5e8);
            at<uint32_t>(pm_ps_ref(pm), 0x34) = 0;
            uint8_t* const p = pm_ps_ref(pm);
            const uint8_t* const def = weapon_def_at(at<int32_t>(p, 0x80));
            if (at<uint32_t>(p, 0x280 + at<uint32_t>(def, 0x5a0) * 4)) {
                at<uint32_t>(p, 0x3c) = at<uint32_t>(cur_weapon_def(), 0x618);
                PM_StartLegsAnim(0, 0, 0x11);
                const float size = static_cast<float>(at<int32_t>(cur_weapon_def(), 0x618));
                uint8_t* const hud = at<uint8_t*>(title_global<uint8_t*>(0x0095f8f0), 0x34);
                if (hud) {
                    at<float>(hud, 0x24) = static_cast<float>(at<int32_t>(pm_ps_ref(pm), 0x3c));
                    at<float>(hud, 0x28) = size;
                }
                add_ps_event(pm_ps_ref(pm), 0xa2, 0);
                at<uint32_t>(pm_ps_ref(pm), 0x38) = 0xa;
            }
        }
    } else {
        at<uint32_t>(pm_ps_ref(pm), 0x38) = at<uint32_t>(cur, 0x5d4);
        at<uint32_t>(pm_ps_ref(pm), 0x34) = at<uint32_t>(cur_weapon_def(), 0x5dc);
        cur = cur_weapon_def();
        if (at<uint32_t>(cur, 0x6f4)) {
            uint8_t* const p = pm_ps_ref(pm);
            const float one = title_global<float>(0x0083eb20);
            const float rate = one / at<float>(cur, 0x868);
            at<int32_t>(p, 0x38) = static_cast<int32_t>((one - at<float>(p, 0x88)) * rate);
            cur = cur_weapon_def();
        }
        if (at<uint32_t>(cur, 0x6d8)) {
            uint8_t* const p = pm_ps_ref(pm);
            const int32_t weapon = at<int32_t>(p, 0x80);
            at<uint32_t>(p, 0x400 + static_cast<uint32_t>(weapon >> 5) * 4) |= 1u << (weapon & 0x1f);
        }
    }
    at<uint32_t>(pm_ps_ref(pm), 0x84) = 3;
    uint8_t* const p = pm_ps_ref(pm);
    if (at<uint8_t>(p, 0x2c) & 1) at<uint32_t>(p, 0x2c) |= 0x400;
    return address_of(p);
}

void XB_CDECL PM_BeginWeaponChange(int32_t old_weapon, int32_t new_weapon) {
    // Puts the old weapon away toward new_weapon (owned or 0; not mid-change (state 2), not while a type-3 weapon
    // aims): with an owned old weapon and an empty clip counter, event 0x9f + anim 0xf for its alternate (+0x718;
    // drop time +0x610, the new one takes its slot), else event 0x9e + anim 9 when it has ammo (drop time +0x608);
    // weapon state 2 (prone flag); otherwise state 2 at once (weapon time 0).
    if (new_weapon < 0 || new_weapon > title_global<int32_t>(0x0095f4d4)) return;
    uint8_t* const pm = pm_global();
    if (new_weapon && !ps_weapon_bit(pm_ps_ref(pm), 0x3f0, new_weapon)) return;
    uint8_t* const ps = pm_ps_ref(pm);
    if (at<int32_t>(ps, 0x84) == 2) return;
    if (at<int32_t>(cur_weapon_def(), 0x98) == 3) {
        if (at<uint8_t>(ps, 0x2c) & 0x20) return;
        if (at<float>(ps, 0x88) > title_global<float>(0x0083ed50)) return;  // 0.0001
    }
    at<uint32_t>(ps, 0x38) = 0;
    bool now = !old_weapon;
    uint8_t* p = nullptr;
    if (!now) {
        p = pm_ps_ref(pm);
        now = !Com_BitCheck(0, old_weapon, &at<uint32_t>(p, 0x3f0)) || at<int32_t>(p, 0x3c) > 0;
    }
    if (now) {
        at<uint32_t>(pm_ps_ref(pm), 0x34) = 0;
        at<uint32_t>(pm_ps_ref(pm), 0x84) = 2;
        at<uint32_t>(pm_ps_ref(pm), 0x3c) = 0;
        PM_SetProneFlag400();
        return;
    }
    const uint8_t* const def = weapon_def_at(old_weapon);
    const bool alt = new_weapon && new_weapon == at<int32_t>(def, 0x718);
    bool loaded = true;
    if (at<uint32_t>(def, 0x6ec) && !at<uint32_t>(p, 0x280 + at<uint32_t>(def, 0x5a0) * 4)) loaded = false;
    at<uint32_t>(p, 0x3c) = 0;
    if (alt) {
        add_ps_event(pm_ps_ref(pm), 0x9f, 0);
        PM_StartLegsAnim(0, 0, 0xf);
    } else if (loaded) {
        add_ps_event(pm_ps_ref(pm), 0x9e, 0);
        PM_StartLegsAnim(0, 0, 9);
    }
    at<uint32_t>(pm_ps_ref(pm), 0x84) = 2;
    PM_SetProneFlag400();
    if (!alt) {
        at<uint32_t>(pm_ps_ref(pm), 0x34) = at<uint32_t>(def, 0x608);
        return;
    }
    at<uint32_t>(pm_ps_ref(pm), 0x34) = at<uint32_t>(def, 0x610);
    uint8_t* const q = pm_ps_ref(pm);
    const uint32_t slot = BG_GetWeaponSlot(q, old_weapon, 1);
    if (slot) PM_SetSlotWeapon(static_cast<uint32_t>(new_weapon), q, slot);
}

uint32_t XB_CDECL PM_UpdateWeaponRechamber(uint32_t fired) {
    // Bolt weapons (+0x6d8) with the weapon's +0x400 bit: in state 4 after a shot (argument) the bit is cleared
    // with event 0xa8 (1 while weapon time is left); once the timers are free, state 4 ends (PM_SetWeaponReady)
    // and state 0 starts the rechamber (anim 7 above 0.75 ADS, else 4; state 4, time +0x5e0, second timer +0x5e4
    // when below it, else 1; event 0xa7). 0 otherwise.
    if (!at<uint32_t>(cur_weapon_def(), 0x6d8)) return 0;
    uint8_t* const pm = pm_global();
    uint8_t* ps = pm_ps_ref(pm);
    const int32_t weapon = at<int32_t>(ps, 0x80);
    if (!ps_weapon_bit(ps, 0x400, weapon)) return 0;
    const int32_t state = at<int32_t>(ps, 0x84);
    if (state == 4 && fired) {
        at<uint32_t>(ps, 0x400 + static_cast<uint32_t>(weapon >> 5) * 4) &= ~(1u << (weapon & 0x1f));
        add_ps_event(pm_ps_ref(pm), 0xa8, 0);
        ps = pm_ps_ref(pm);
        if (at<int32_t>(ps, 0x34)) return 1;
    } else if (at<int32_t>(ps, 0x34)) {
        if (state == 3 || state == 4 || state == 0xa || state == 0xb) return 0;
        if (at<int32_t>(ps, 0x38)) return 0;
    }
    const int32_t now = at<int32_t>(ps, 0x84);
    if (now == 4) {
        PM_SetWeaponReady();
        return 0;
    }
    if (now) return 0;
    PM_StartLegsAnim(0, 0, at<float>(ps, 0x88) > title_global<float>(0x0083eb58) ? 7 : 4);  // 0.75
    at<uint32_t>(pm_ps_ref(pm), 0x84) = 4;
    at<uint32_t>(pm_ps_ref(pm), 0x34) = at<uint32_t>(cur_weapon_def(), 0x5e0);
    const uint8_t* const cur = cur_weapon_def();
    const int32_t delay = at<int32_t>(cur, 0x5e4);
    if (delay && delay < at<int32_t>(cur, 0x5e0))
        at<int32_t>(pm_ps_ref(pm), 0x38) = delay;
    else
        at<int32_t>(pm_ps_ref(pm), 0x38) = 1;
    add_ps_event(pm_ps_ref(pm), 0xa7, 0);
    return 0;
}

void XB_CDECL PM_WeaponFire(uint32_t skip_timers) {
    // A shot: not while sprinting (pm_flags 0x10000), not mid ADS for bolt weapons (+0x6d8: 0.01..0.99), type 8
    // only aimed with +0x6f4, scoped type 3 only fully aimed when 0x0095f4dc; the fire timers, then with ammo and
    // no second timer one round used (not for infinite clips (-1) or client flags 0x106000), the fire time for
    // types 1 and 5, the fire anim, event 0xa6 (clip now empty) or 0xa3, the screen blend and PM_RemoveEmptyWeapon.
    uint8_t* const pm = pm_global();
    const uint8_t* const ps = pm_ps_ref(pm);
    const uint32_t flags = at<uint32_t>(ps, 0x2c);
    if (flags & 0x10000) return;
    const uint8_t* cur = cur_weapon_def();
    const float high = title_global<float>(0x0083ed08);  // 0.99
    if (at<uint32_t>(cur, 0x6d8) && high > at<float>(ps, 0x88) && at<float>(ps, 0x88) > title_global<float>(0x0083eb94))
        return;  // 0.01
    const int32_t type = at<int32_t>(cur, 0x98);
    if (type == 8) {
        if (!(flags & 0x20)) return;
        if (!at<uint32_t>(cur, 0x6f4)) return;
    }
    if (title_global<uint8_t>(0x0095f4dc) && type == 3) {
        if (!(flags & 0x20)) return;
        if (high > at<float>(ps, 0x88)) return;
    }
    PM_SetWeaponFireTimers(skip_timers);
    if (!PM_WeaponCheckAmmo()) return;
    uint8_t* p = pm_ps_ref(pm);
    if (at<int32_t>(p, 0x38)) return;
    const uint8_t* const def = weapon_def_at(at<int32_t>(p, 0x80));
    if (at<int32_t>(p, 0x280 + at<uint32_t>(def, 0x5a0) * 4) != -1 && !(at<uint32_t>(p, 0xc0) & 0x106000))
        PM_WeaponUseAmmo(0, 0, at<uint32_t>(p, 0x80), 1);
    cur = cur_weapon_def();
    if (at<int32_t>(cur, 0x94) == 1) {
        at<uint32_t>(pm_ps_ref(pm), 0x34) = at<uint32_t>(cur, 0x5dc);
        cur = cur_weapon_def();
    }
    if (at<int32_t>(cur, 0x94) == 5) at<uint32_t>(pm_ps_ref(pm), 0x34) = at<uint32_t>(cur, 0x5dc);
    PM_StartFireAnim();
    p = pm_ps_ref(pm);
    add_ps_event(p, PM_ClipIsEmpty(0, 0, at<uint32_t>(p, 0x80)) ? 0xa6 : 0xa3, 0);
    PM_AddWeaponFireBlend();
    PM_RemoveEmptyWeapon();
}

uint32_t XB_CDECL PM_CheckFireButtonRelease(uint32_t held) {
    // 1 when the fire button's release ends the shot: type-1 clip weapons (+0x6e4) mid-clip keep firing (0) while
    // the button for their class (slot class 4 = grenades: cmd bit 2, else bit 1) is up. Non-grenades, button up
    // and not held: state 3 back to state 0 with the idle anim. Grenades on release (bit 2 up, not held): the
    // throw (+0x38 = +0x5dc, time 0), PM_WeaponCheckAmmo (else the grenade leaves its slot), CG_SelectWeapon of
    // +0x94, state 0, the HUD clip counter cleared (-1) for type 1.
    const uint8_t* cur = cur_weapon_def();
    uint8_t* pm = pm_global();
    const uint8_t buttons = pm[8];
    const bool fire = (buttons & 1) != 0;
    const bool grenade = at<int32_t>(cur, 0x9c) == 4;
    if (!fire && !grenade && at<int32_t>(cur, 0x94) == 1 && at<uint32_t>(cur, 0x6e4)) {
        const int32_t clip = at<int32_t>(pm_ps_ref(pm), 0x3c);
        if (clip < at<int32_t>(cur, 0x618) && clip) return 0;
    }
    const bool throw_btn = (buttons & 2) != 0;
    if (!throw_btn && grenade && at<int32_t>(cur, 0x94) == 1 && at<uint32_t>(cur, 0x6e4)) {
        const int32_t clip = at<int32_t>(pm_ps_ref(pm), 0x3c);
        if (clip < at<int32_t>(cur, 0x618) && clip) return 0;
    }
    if (!fire && !grenade && !held) {
        const uint8_t* const ps = pm_ps_ref(pm);
        if (at<int32_t>(ps, 0x84) == 3) start_idle_anim(pm, ps);
        at<uint32_t>(pm_ps_ref(pm), 0x84) = 0;
        return 1;
    }
    if (throw_btn || !grenade || held) return 0;
    const uint8_t* const ps = pm_ps_ref(pm);
    if (at<int32_t>(ps, 0x84) == 3) {
        start_idle_anim(pm, ps);
        cur = cur_weapon_def();
    }
    at<uint32_t>(pm_ps_ref(pm), 0x38) = at<uint32_t>(cur, 0x5dc);
    at<uint32_t>(pm_ps_ref(pm), 0x34) = 0;
    const uint32_t ammo = PM_WeaponCheckAmmo();
    pm = pm_global();
    if (!ammo) BG_RemoveWeaponFromSlot(at<uint32_t>(pm_ps_ref(pm), 0x80), pm_ps_ref(pm));
    CG_SelectWeapon(at<int32_t>(pm_ps_ref(pm), 0x94));
    at<uint32_t>(pm_ps_ref(pm_global()), 0x84) = 0;
    if (at<int32_t>(cur_weapon_def(), 0x94) == 1) {
        uint8_t* const hud = at<uint8_t*>(title_global<uint8_t*>(0x0095f8f0), 0x34);
        if (hud) {
            const float none = title_global<float>(0x0083eb34);  // -1
            at<float>(hud, 0x24) = none;
            at<float>(hud, 0x28) = none;
        }
    }
    return 1;
}

uint32_t XB_CDECL PM_CheckJump() {
    // Q3's PM_CheckJump: 500 ms after landing (+0x70), not with pm_flags 0x800, crouched/prone view heights or a
    // scoped type-3 weapon aimed, with the jump input (cmd +0x1a >= 10; flag 8 eats it): PM_StartJump(39); a
    // jump off a ladder (flag 0x10) scales the vertical speed by 0.75 and pushes 128 along the forward vector
    // (reflected off the ladder normal +0x64 when facing it); the jump event and +64 screen blend (at most 255).
    uint8_t* const pm = pm_global();
    const uint8_t* const ps = pm_ps_ref(pm);
    if (static_cast<int32_t>(at<uint32_t>(pm, 4) - at<uint32_t>(ps, 0x70)) < 0x1f4) return 0;
    const uint32_t flags = at<uint32_t>(ps, 0x2c);
    if (flags & 0x800) return 0;
    const uint32_t height = at<uint32_t>(ps, 0xa8);
    if (height == at<uint32_t>(ps, 0x424) || height == at<uint32_t>(ps, 0x420)) return 0;
    if ((flags & 0x20) && at<int32_t>(weapon_def_at(at<int32_t>(ps, 0x80)), 0x98) == 3) return 0;
    if (at<int8_t>(pm, 0x1a) < 10) return 0;
    if (flags & 8) {
        pm[0x1a] = 0;
        return 0;
    }
    PM_StartJump(39.0f);
    uint8_t* p = pm_ps_ref(pm);
    if (at<uint8_t>(p, 0x2c) & 0x10) {
        at<float>(p, 0x18) = at<float>(p, 0x18) * title_global<float>(0x0083eb58);  // 0.75
        float v[3] = {title_global<float>(0x0095ff80), title_global<float>(0x0095ff84), 0.0f};  // pml.forward
        VectorNormalize(v);
        uint8_t* const q = pm_ps_ref(pm);
        const float d = at<float>(q, 0x64) * title_global<float>(0x0095ff80) + at<float>(q, 0x6c) * title_global<float>(0x0095ff88) +
                        at<float>(q, 0x68) * title_global<float>(0x0095ff84);
        if (0.0f > d) {
            float e = at<float>(q, 0x64) * v[0] + at<float>(q, 0x6c) * v[2] + at<float>(q, 0x68) * v[1];
            e = e * title_global<float>(0x0083ec5c);  // -2
            const float x = at<float>(q, 0x64) * e + v[0];
            const float y = e * at<float>(q, 0x68) + v[1];
            const float z = e * at<float>(q, 0x6c) + v[2];
            v[0] = x;
            v[1] = y;
            v[2] = z;
            VectorNormalize(v);
        }
        const float push = title_global<float>(0x0083ebe8);  // 128
        at<float>(q, 0x10) = v[0] * push;
        at<float>(pm_ps_ref(pm), 0x14) = v[1] * push;
        at<uint32_t>(pm_ps_ref(pm), 0x2c) &= 0xffffffefu;
    }
    PM_AddEvent(0, 0, PM_JumpEventForSurface());
    p = pm_ps_ref(pm);
    at<float>(p, 0x4f0) = at<float>(p, 0x4f0) + title_global<float>(0x00830c1c);  // 64
    p = pm_ps_ref(pm);
    const float limit = title_global<float>(0x0083eb8c);  // 255
    if (at<float>(p, 0x4f0) > limit) at<float>(p, 0x4f0) = limit;
    return 1;
}

namespace {

// sqrt(x*x + y*y + z*z) - sub on the x87 (the title's FLD/FMUL/FADDP order), stored as float; `above` =
// 0 < the unrounded result or unordered (FCOMIP + JB).
float x87_length3_minus(const float* v, float sub, float zero, bool* above) {
    float r;
    bool b;
    __asm__("flds %4\n\tflds %3\n\tflds %2\n\tfld %%st(0)\n\tfmul %%st(1), %%st\n\tfld %%st(2)\n\tfmul %%st(3), %%st\n\t"
            "faddp\n\tfld %%st(3)\n\tfmul %%st(4), %%st\n\tfaddp\n\tfsqrt\n\tfstp %%st(3)\n\tfstp %%st(0)\n\t"
            "fstp %%st(0)\n\tfsubs %5\n\tfsts %0\n\tflds %6\n\tfcomip %%st(1), %%st\n\tfstp %%st(0)\n\tsetb %1"
            : "=m"(r), "=q"(b)
            : "m"(v[0]), "m"(v[1]), "m"(v[2]), "m"(sub), "m"(zero)
            : "st", "st(1)", "st(2)", "st(3)", "st(4)", "cc");
    *above = b;
    return r;
}

}  // namespace

void XB_CDECL PM_ReduceGroundSpeed() {
    // From PmoveSingle while walking: the velocity's length less 20; at or below 0 the velocity stops, else it is
    // normalized (packed over +0x10..+0x1c: rsqrt estimate with a Newton step; no divide for length 0) and scaled
    // to the reduced speed.
    if (!title_global<uint32_t>(0x0095ffac)) return;  // pml.walking
    uint8_t* const pm = pm_global();
    uint8_t* const ps = pm_ps_ref(pm);
    bool moving;
    const float speed = x87_length3_minus(&at<float>(ps, 0x10), title_global<float>(0x0083eb5c),
                                          title_global<float>(0x0083eafc), &moving);  // 20, 0
    if (!moving) {
        at<float>(ps, 0x18) = 0.0f;
        at<float>(pm_ps_ref(pm), 0x14) = 0.0f;
        at<float>(pm_ps_ref(pm), 0x10) = 0.0f;
        return;
    }
    float* const v = &at<float>(ps, 0x10);
    const float sq[4] = {v[0] * v[0], v[1] * v[1], v[2] * v[2], v[3] * v[3]};
    platform_keep(sq[3]);
    const float sum = sq[0] + sq[1] + sq[2];
    float len;
    const float zero = title_global<float>(0x0082e680);
    if (__builtin_isunordered(sum, zero) || sum == zero) {  // UCOMISS + JNE: unordered counts as equal
        len = sum;
    } else {
        const float r = platform_rsqrt_estimate(sum);
        const float sr = sum * r;
        len = (title_global<float>(0x0082e660) - sr * r) * sr * title_global<float>(0x0082e670);  // 3, 0.5
    }
    if (len != title_global<float>(0x0083eafc)) {
        float q[4];
        for (uint32_t k = 0; k < 4; ++k) q[k] = v[k] / len;
        for (uint32_t k = 0; k < 4; ++k) v[k] = q[k];
    }
    uint8_t* p = pm_ps_ref(pm);
    at<float>(p, 0x10) = speed * at<float>(p, 0x10);
    p = pm_ps_ref(pm);
    at<float>(p, 0x14) = at<float>(p, 0x14) * speed;
    p = pm_ps_ref(pm);
    at<float>(p, 0x18) = at<float>(p, 0x18) * speed;
}

uint8_t* XB_REGS3 XB_CDECL BG_PlayerStateToEntityStateExtraPolate(uint8_t* es, uint32_t, uint8_t* ps, uint32_t time) {
    // BG_PlayerStateToEntityState with an extrapolated trajectory (type 3 from the origin and velocity at the
    // time, 50 ms), the lean unwrapped, and the seen-event counter (+0xe8) clamped to the sequence first.
    at<uint32_t>(es, 0x14) = 3;
    for (uint32_t i = 0; i < 12; i += 4) at<uint32_t>(es, 0x20 + i) = at<uint32_t>(ps, i);
    for (uint32_t i = 0; i < 12; i += 4) at<uint32_t>(es, 0x2c + i) = at<uint32_t>(ps, 0x10 + i);
    at<uint32_t>(es, 0x18) = time;
    at<uint32_t>(es, 0x1c) = 0x32;
    at<uint32_t>(es, 0x38) = 1;
    for (uint32_t i = 0; i < 12; i += 4) at<uint32_t>(es, 0x44 + i) = at<uint32_t>(ps, 0x9c + i);
    at<float>(es, 0x94) = static_cast<float>(at<int32_t>(ps, 0x78));
    at<uint32_t>(es, 0x10) = at<uint32_t>(ps, 0xc0);

    const uint32_t entity_sequence = at<uint32_t>(ps, 0xfc);
    const uint32_t sequence = at<uint32_t>(ps, 0xc4);
    if (static_cast<int32_t>(entity_sequence - sequence) >= 0) {
        es[4] = 0;
    } else {
        if (static_cast<int32_t>(sequence - entity_sequence) > 4) at<uint32_t>(ps, 0xfc) = sequence - 4;
        es[4] = ps[0xd8 + (at<uint32_t>(ps, 0xfc) & 3) * 4];
        ++at<uint32_t>(ps, 0xfc);
    }

    const uint32_t now = at<uint32_t>(ps, 0xc4);
    if (static_cast<int32_t>(at<uint32_t>(ps, 0xe8) - now) > 0) at<uint32_t>(ps, 0xe8) = now;
    uint32_t seen = at<uint32_t>(ps, 0xe8);
    if (seen != now) {
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
                es[0xb4 + (at<uint32_t>(es, 0xb0) & 3)] = ps[0xc8 + slot];
                es[0xb8 + (at<uint32_t>(es, 0xb0) & 3)] = ps[0xd8 + slot];
                ++at<uint32_t>(es, 0xb0);
            }
            ++seen;
        } while (seen != at<uint32_t>(ps, 0xc4));
    }
    at<uint32_t>(ps, 0xe8) = at<uint32_t>(ps, 0xc4);
    es[3] = ps[0x80];
    at<uint32_t>(es, 0xc) = at<uint32_t>(ps, 0x60);
    es[0] = 1;
    return es;
}

namespace {

// The title CRT's __CIsqrt of an x87 value, stored as float.
float pm_ci_sqrt(long double x) {
    float result;
    __asm__ volatile("fldt %1\n\tcall *%2\n\tfstps %0"
                     : "=m"(result)
                     : "m"(x), "r"(uintptr_t{0x0033acf4})
                     : "eax", "ecx", "edx", "st", "memory");
    return result;
}

// a == b on the x87 (FLD/FUCOMIP, LAHF + TEST AH,0x44 + JNP): only an ordered equal.
bool x87_equal(float a, float b) {
    bool equal, ordered;
    __asm__("flds %3\n\tflds %2\n\tfucomip %%st(1), %%st\n\tfstp %%st(0)\n\tsete %0\n\tsetnp %1"
            : "=q"(equal), "=q"(ordered)
            : "m"(a), "m"(b)
            : "st", "st(1)", "cc");
    return equal && ordered;
}

// (z*z + y*y) + x*x on the x87, stored as float.
float x87_sum_squares_zyx(float x, float y, float z) {
    float r;
    __asm__("flds %3\n\tfmuls %3\n\tflds %2\n\tfmuls %2\n\tfaddp\n\tflds %1\n\tfmuls %1\n\tfaddp\n\tfstps %0"
            : "=m"(r)
            : "m"(x), "m"(y), "m"(z)
            : "st", "st(1)");
    return r;
}

// The angle a - b as the title rounds it to 16 bits (x 182.044, truncated, & 0xffff, x 0.00549316) and wraps it
// above 180.
float short_angle_delta(float a, float b) {
    const int32_t quantized = static_cast<int32_t>((a - b) * title_global<float>(0x0083ebd8)) & 0xffff;
    float r = static_cast<float>(quantized) * title_global<float>(0x0083ebd4);
    if (r > title_global<float>(0x0083eb60)) r -= title_global<float>(0x0083ebcc);  // 180, 360
    return r;
}

// The movement direction byte: beyond +-90 it becomes 90 (positive) or -90, then the low byte sign-extended.
int32_t clamp_move_dir(int32_t dir) {
    const uint32_t sign = static_cast<uint32_t>(dir >> 31);
    const int32_t magnitude = static_cast<int32_t>((static_cast<uint32_t>(dir) ^ sign) - sign);
    if (magnitude > 0x5a) dir = dir > 0 ? 0x5a : -0x5a;
    return static_cast<int8_t>(static_cast<uint8_t>(dir));
}

// Q3's PM_Friction for the noclip moves: frametime * control * 8.25 (+ 0) off the speed, at least 0.
void noclip_friction(uint8_t* pm, float speed, float control) {
    const float drop = title_global<float>(0x0095ffa4) * control * title_global<float>(0x0083ef90) + 0.0f;
    float newspeed = speed - drop;
    if (0.0f > newspeed) newspeed = 0.0f;
    newspeed /= speed;
    at<float>(pm_ps_ref(pm), 0x10) = at<float>(pm_ps_ref(pm), 0x10) * newspeed;
    at<float>(pm_ps_ref(pm), 0x14) = at<float>(pm_ps_ref(pm), 0x14) * newspeed;
    at<float>(pm_ps_ref(pm), 0x18) = at<float>(pm_ps_ref(pm), 0x18) * newspeed;
}

// The noclip moves' tail: wishvel normalized (left as is for length 0, x87 compare), wishspeed scaled, an inlined
// PM_Accelerate (pm_accelerate 9), then origin += velocity * frametime. `control_first`: control * frametime
// instead of frametime * control.
void noclip_accelerate(uint8_t* pm, float wx, float wy, float wz, float scale, bool control_first) {
    float wishspeed = pm_ci_sqrt(platform_x87_ld((wy * wy + wx * wx) + wz * wz));
    float dx = wx, dy = wy, dz = wz;
    if (!x87_equal(wishspeed, 0.0f)) {
        const float inv = title_global<float>(0x0083eb20) / wishspeed;
        dx = inv * wx;
        dy = inv * wy;
        dz = inv * wz;
    }
    wishspeed = wishspeed * scale;
    uint8_t* ps = pm_ps_ref(pm);
    const float frametime = title_global<float>(0x0095ffa4);
    const float current = (at<float>(ps, 0x18) * dz + at<float>(ps, 0x14) * dy) + at<float>(ps, 0x10) * dx;
    const float add = wishspeed - current;
    if (!(0.0f >= add)) {
        float control = title_global<float>(0x0083eb90);  // 100
        if (!(control > wishspeed)) control = wishspeed;
        float accel = control_first ? control * frametime * title_global<float>(0x0083eef8)
                                    : frametime * control * title_global<float>(0x0083eef8);  // 9
        if (accel > add) accel = add;
        if (at<uint32_t>(ps, 0x60)) accel = title_global<float>(0x0083eb20) / at<float>(ps, 0x478) * accel;
        if (accel > add) accel = add;
        at<float>(ps, 0x10) = accel * dx + at<float>(ps, 0x10);
        ps = pm_ps_ref(pm);
        at<float>(ps, 0x14) = accel * dy + at<float>(ps, 0x14);
        ps = pm_ps_ref(pm);
        at<float>(ps, 0x18) = accel * dz + at<float>(ps, 0x18);
    }
    for (uint32_t i = 0; i < 3; ++i) {
        ps = pm_ps_ref(pm);
        at<float>(ps, i * 4) = at<float>(ps, 0x10 + i * 4) * title_global<float>(0x0095ffa4) + at<float>(ps, i * 4);
    }
}

// The fall height: v = -(vel + t * -gravity) for t = (-vel - sqrt(den)) / (a + a), v * v / (gravity + gravity);
// all on the x87 (FSUBP/FDIVP as the title has them), stored as float.
float x87_fall_height(float vel, float den, float a, float neg_gravity, float gravity, float minus_one) {
    float r;
    __asm__("flds %1\n\tfchs\n\tflds %2\n\tfsqrt\n\t.byte 0xde, 0xe9\n\t"  // fsubp st(1),st
            "flds %3\n\tfadd %%st(0), %%st\n\t.byte 0xde, 0xf9\n\t"          // fdivp st(1),st
            "fmuls %4\n\tfadds %1\n\tfmuls %6\n\tfld %%st(0)\n\tfmul %%st(1), %%st\n\t"
            "flds %5\n\tfadd %%st(0), %%st\n\t.byte 0xde, 0xf9\n\tfstps %0\n\tfstp %%st(0)"
            : "=m"(r)
            : "m"(vel), "m"(den), "m"(a), "m"(neg_gravity), "m"(gravity), "m"(minus_one)
            : "st", "st(1)", "st(2)");
    return r;
}

}  // namespace

uint32_t XB_CDECL PM_FinishWeaponChange() {
    // The end of a weapon change (state 2): the commanded weapon (pm +0xa) if owned, allowed (not pm_flags 0x10,
    // not +0xc0 0x100000 without 0x400000) and valid, else 0, becomes the weapon. The same weapon: state 0 with the
    // idle anim (0 for mode 5). From none: state 1, raise time +0x60c, blend 255, anim 0xa. Else the old one is
    // remembered (+0x94, not for mode 5), state 1; mode-3 weapons drop ADS; the old weapon's alternate raises with
    // +0x614 (blend at least 128, anim 0x10), others with event 0x9d, +0x60c, blend 255 and anim 0xa. 0 when not
    // changing, else 1.
    uint8_t* const pm = pm_global();
    uint8_t* ps = pm_ps_ref(pm);
    if (at<int32_t>(ps, 0x84) != 2) return 0;
    int32_t weapon = pm[0xa];
    if (at<uint8_t>(ps, 0x2c) & 0x10) {
        weapon = 0;
    } else if (!ps_weapon_bit(ps, 0x3f0, weapon)) {
        weapon = 0;
    } else {
        const uint32_t f = at<uint32_t>(ps, 0xc0);
        if (((f & 0x100000) && !(f & 0x400000)) || weapon < 0 || weapon > title_global<int32_t>(0x0095f4d4)) weapon = 0;
    }
    if (!ps_weapon_bit(ps, 0x3f0, weapon)) weapon = 0;
    const int32_t old = at<int32_t>(ps, 0x80);
    at<uint32_t>(ps, 0x80) = static_cast<uint8_t>(weapon);
    uint8_t* const* const defs = title_global<uint8_t* const*>(0x0095f4d8);  // bg_weaponDefs
    title_global<uint8_t*>(0x00960020) = defs[at<int32_t>(pm_ps_ref(pm), 0x80)];
    const auto set_anim_a = [pm]() {
        uint8_t* const p = pm_ps_ref(pm);
        if (at<int32_t>(p, 0x24) >= 4 || !pm[0xa]) return;
        if (at<int32_t>(cur_weapon_def(), 0x94) == 5)
            at<uint32_t>(p, 0x4ec) = 0xa;
        else
            at<uint32_t>(p, 0x4ec) = (~at<uint32_t>(p, 0x4ec) & 0x200) | 0xa;
    };
    if (old == weapon) {
        ps = pm_ps_ref(pm);
        at<uint32_t>(ps, 0x84) = 0;
        ps = pm_ps_ref(pm);
        if (at<int32_t>(ps, 0x24) >= 4 || !pm[0xa]) return 1;
        if (at<int32_t>(cur_weapon_def(), 0x94) == 5) {
            at<uint32_t>(ps, 0x4ec) = 0;
            return 1;
        }
        uint32_t anim = 0;
        if (at<float>(ps, 0x88) > title_global<float>(0x0083ec18)) anim = 0x17;  // 0.9
        at<uint32_t>(ps, 0x4ec) = (~at<uint32_t>(ps, 0x4ec) & 0x200) | anim;
        return 1;
    }
    if (!old) {
        at<uint32_t>(pm_ps_ref(pm), 0x84) = 1;
        at<uint32_t>(pm_ps_ref(pm), 0x34) = at<uint32_t>(defs[weapon], 0x60c);
        at<float>(pm_ps_ref(pm), 0x4f0) = title_global<float>(0x0083eb8c);  // 255
        set_anim_a();
        ps = pm_ps_ref(pm);
        if (at<uint8_t>(ps, 0x2c) & 1) at<uint32_t>(ps, 0x2c) |= 0x400;
        return 1;
    }
    if (at<int32_t>(defs[old], 0x98) != 5) at<int32_t>(pm_ps_ref(pm), 0x94) = old;
    at<uint32_t>(pm_ps_ref(pm), 0x84) = 1;
    ps = pm_ps_ref(pm);
    if (at<uint8_t>(ps, 0x2c) & 1) at<uint32_t>(ps, 0x2c) |= 0x400;
    if (at<int32_t>(defs[weapon], 0x98) == 3) at<float>(pm_ps_ref(pm), 0x88) = 0.0f;
    uint32_t time;
    if (weapon && weapon == at<int32_t>(defs[old], 0x718)) {
        time = at<uint32_t>(defs[weapon], 0x614);
    } else {
        add_ps_event(pm_ps_ref(pm), 0x9d, 0);
        time = at<uint32_t>(defs[weapon], 0x60c);
    }
    at<uint32_t>(pm_ps_ref(pm), 0x34) = time;
    if (weapon && weapon == at<int32_t>(defs[old], 0x718)) {
        ps = pm_ps_ref(pm);
        const float half = title_global<float>(0x0083ebe8);  // 128
        if (half > at<float>(ps, 0x4f0)) at<float>(ps, 0x4f0) = half;
        PM_StartLegsAnim(0, 0, 0x10);
        return 1;
    }
    at<float>(pm_ps_ref(pm), 0x4f0) = title_global<float>(0x0083eb8c);  // 255
    set_anim_a();
    return 1;
}

void XB_CDECL PM_CrashLand() {
    // Q3's PM_CrashLand: the impact speed from the fall (x87 quadratic) gives a damage 0-100 between the fall
    // thresholds (0x00934be0, 0x0092bd68; halved at waterlevel 2; none with surface flag 1 or dead) and a land
    // parameter (above 12: (h - 12) / 26 + 1, x4, at most 24). Damage below 100 without flag 2 slows the player
    // (pm_time 500 + 35 per point up to 2000, factor 0.5..0.2, time-knockback flag 0x100), else x0.67; then the
    // fall event (0x74 + surface type, 0x74 with surface flag 0x2000) with the damage. Without damage: steps above
    // 4 (walk below 8, run below 12) or the land event (x0.67) with the parameter.
    uint8_t* const pm = pm_global();
    const uint8_t water = pm[0x121];
    if (water == 3) return;
    uint8_t* const ps = pm_ps_ref(pm);
    const float vel = title_global<float>(0x00960018);  // pml.previous_velocity[2]
    const float half = title_global<float>(0x0083eb24), four = title_global<float>(0x0083100c);
    const float gravity = static_cast<float>(at<int32_t>(ps, 0x48));
    const float neg_gravity = 0.0f - gravity;
    const float a = neg_gravity * half;
    const float dist = title_global<float>(0x0096000c) - at<float>(ps, 0x8);  // pml.previous_origin[2]
    const float den = vel * vel - dist * a * four;
    if (0.0f > den) return;
    const float lo = title_global<float>(0x00934be0), hi = title_global<float>(0x0092bd68);
    const float h = x87_fall_height(vel, den, a, neg_gravity, gravity, title_global<float>(0x0083eb34));
    const uint8_t surface = title_global<uint8_t>(0x0095ffe4);
    int32_t damage = 0;
    if (!(lo >= hi) && !(0.0f > lo) && !(lo >= h) && !(surface & 1) && at<int32_t>(ps, 0x24) < 4) {
        if (!(h >= hi)) {
            damage = static_cast<int32_t>((h - lo) / (hi - lo) * title_global<float>(0x0083eb90));
            if (damage < 0)
                damage = 0;
            else if (damage > 100)
                damage = 100;
        } else {
            damage = 100;
        }
    }
    if (water == 2) damage = static_cast<int32_t>(static_cast<float>(damage) * half);
    const float twelve = title_global<float>(0x0083eccc);
    int32_t parm = 0;
    if (!(twelve >= h)) {
        parm = static_cast<int32_t>(((h - twelve) * title_global<float>(0x0083ee7c) + title_global<float>(0x0083eb20)) *
                                    four);
        if (parm > 0x18) parm = 0x18;
    }
    const float slow = title_global<float>(0x0083ec3c);  // 0.67
    if (damage) {
        if (damage < 100 && !(surface & 2)) {
            float factor = half;
            int32_t time = damage * 0x23 + 0x1f4;
            if (time > 0x7d0) {
                factor = title_global<float>(0x0083eca0);
                time = 0x7d0;
            } else if (time > 0x1f4) {
                if (time >= 0x5dc)
                    factor = title_global<float>(0x0083eca0);  // 0.2
                else
                    factor = half - (static_cast<float>(time) - title_global<float>(0x0083ebec)) *
                                        title_global<float>(0x0083eb4c) * title_global<float>(0x0083066c);
            }
            at<int32_t>(ps, 0x30) = time;
            at<uint32_t>(pm_ps_ref(pm), 0x2c) |= 0x100;
            for (uint32_t i = 0; i < 3; ++i) at<float>(pm_ps_ref(pm), 0x10 + i * 4) = at<float>(pm_ps_ref(pm), 0x10 + i * 4) * factor;
        } else {
            at<float>(ps, 0x10) = at<float>(ps, 0x10) * slow;
            for (uint32_t i = 1; i < 3; ++i) at<float>(pm_ps_ref(pm), 0x10 + i * 4) = at<float>(pm_ps_ref(pm), 0x10 + i * 4) * slow;
        }
        const uint32_t flags = title_global<uint32_t>(0x0095ffe4);
        const uint32_t type = (flags & 0x2000) ? 0 : (static_cast<uint32_t>(static_cast<int32_t>(flags) >> 0x14) & 0x1f);
        BG_AddPredictableEventToPlayerstate(pm_ps_ref(pm), 0, 0x74 + type, static_cast<uint32_t>(damage));
        return;
    }
    if (!(h > four)) return;
    if (title_global<float>(0x0083ed78) > h) {  // 8
        PM_AddEvent(0, 0, PM_WalkStepEventForSurface());
        return;
    }
    if (twelve > h) {
        PM_AddEvent(0, 0, PM_RunStepEventForSurface());
        return;
    }
    at<float>(ps, 0x10) = at<float>(ps, 0x10) * slow;
    for (uint32_t i = 1; i < 3; ++i) at<float>(pm_ps_ref(pm), 0x10 + i * 4) = at<float>(pm_ps_ref(pm), 0x10 + i * 4) * slow;
    const uint32_t event = PM_LandEventForSurface();
    BG_AddPredictableEventToPlayerstate(pm_ps_ref(pm), 0, event, static_cast<uint32_t>(parm));
}

void XB_CDECL PM_NoclipMove() {
    // Q3's PM_NoclipMove: +0xa8 / +0x428 from 0x009341c4; friction (stop below speed 1, x87 compare), the wish
    // velocity from forward/right (pml axes) plus up as is, then the inlined accelerate and the origin move.
    uint8_t* const pm = pm_global();
    at<uint32_t>(pm_ps_ref(pm), 0xa8) = title_global<uint32_t>(0x009341c4);
    at<uint32_t>(pm_ps_ref(pm), 0x428) = title_global<uint32_t>(0x009341c4);
    uint8_t* const ps = pm_ps_ref(pm);
    const float vx = at<float>(ps, 0x10), vy = at<float>(ps, 0x14), vz = at<float>(ps, 0x18);
    const float speed = pm_ci_sqrt(platform_x87_ld((vx * vx + vy * vy) + vz * vz));
    if (platform_x87_greater(title_global<float>(0x0083eb20), speed)) {
        at<float>(ps, 0x10) = 0.0f;
        at<float>(pm_ps_ref(pm), 0x14) = 0.0f;
        at<float>(pm_ps_ref(pm), 0x18) = 0.0f;
    } else {
        float control = title_global<float>(0x0083eb90);  // 100
        if (!(control > speed)) control = speed;
        const float drop = title_global<float>(0x0095ffa4) * control * title_global<float>(0x0083ef90) + 0.0f;
        float newspeed = speed - drop;
        if (0.0f > newspeed) newspeed = 0.0f;
        newspeed /= speed;
        at<float>(ps, 0x10) = at<float>(ps, 0x10) * newspeed;
        at<float>(pm_ps_ref(pm), 0x14) = at<float>(pm_ps_ref(pm), 0x14) * newspeed;
        at<float>(pm_ps_ref(pm), 0x18) = at<float>(pm_ps_ref(pm), 0x18) * newspeed;
    }
    const float scale = PM_CmdScale(reinterpret_cast<const int8_t*>(pm + 4));
    const float f = static_cast<float>(static_cast<int8_t>(pm[0x18]));
    const float s = static_cast<float>(static_cast<int8_t>(pm[0x19]));
    const int32_t u = static_cast<int8_t>(pm[0x1a]);
    const float wx = title_global<float>(0x0095ff8c) * s + title_global<float>(0x0095ff80) * f;
    const float wy = title_global<float>(0x0095ff90) * s + title_global<float>(0x0095ff84) * f;
    const float wz = static_cast<float>(u) + (title_global<float>(0x0095ff94) * s + title_global<float>(0x0095ff88) * f);
    noclip_accelerate(pm, wx, wy, wz, scale, false);
}

void XB_CDECL PM_NoclipFlatMove() {
    // A noclip move along the horizontal: up/down from buttons 0x10 / 0x20 (x127 into the up move byte); with only
    // an up move the velocity becomes (0, 0, up) and the friction runs with speed and control 127. The wish
    // direction uses forward = up x right (the title multiplies by the zero left in XMM6) and twice the up move.
    uint8_t* const pm = pm_global();
    at<uint32_t>(pm_ps_ref(pm), 0xa8) = title_global<uint32_t>(0x009341c4);
    at<uint32_t>(pm_ps_ref(pm), 0x428) = title_global<uint32_t>(0x009341c4);
    const uint8_t buttons = pm[9];
    const uint8_t up = static_cast<uint8_t>((buttons & 0x10) - (buttons & 0x20));
    pm[0x1a] = static_cast<uint8_t>(pm[0x1a] + static_cast<uint8_t>(up * 0x7f));
    const float zero = 0.0f;
    if (!pm[0x18] && !pm[0x19]) {
        if (static_cast<float>(static_cast<int8_t>(pm[0x1a])) == zero) {
            at<float>(pm_ps_ref(pm), 0x10) = zero;
            at<float>(pm_ps_ref(pm), 0x14) = zero;
            at<float>(pm_ps_ref(pm), 0x18) = zero;
        } else {
            const float full = title_global<float>(0x0083ed88);  // 127
            at<float>(pm_ps_ref(pm), 0x14) = zero;
            at<float>(pm_ps_ref(pm), 0x10) = zero;
            at<float>(pm_ps_ref(pm), 0x18) = static_cast<float>(static_cast<int8_t>(pm[0x1a]));
            noclip_friction(pm, full, full);
        }
    } else {
        uint8_t* const ps = pm_ps_ref(pm);
        const float vx = at<float>(ps, 0x10), vy = at<float>(ps, 0x14), vz = at<float>(ps, 0x18);
        const float speed = pm_ci_sqrt(platform_x87_ld((vx * vx + vy * vy) + vz * vz));
        if (platform_x87_greater(title_global<float>(0x0083eb20), speed)) {
            at<float>(pm_ps_ref(pm), 0x10) = zero;
            at<float>(pm_ps_ref(pm), 0x14) = zero;
            at<float>(pm_ps_ref(pm), 0x18) = zero;
        } else {
            float control = title_global<float>(0x0083eb90);  // 100
            if (!(control > speed)) control = speed;
            noclip_friction(pm, speed, control);
        }
    }
    const float scale = PM_CmdScale(reinterpret_cast<const int8_t*>(pm + 4));
    const float s = static_cast<float>(static_cast<int8_t>(pm[0x19]));
    const float f = static_cast<float>(static_cast<int8_t>(pm[0x18]));
    const int32_t u = static_cast<int8_t>(pm[0x1a]);
    const float r0 = title_global<float>(0x0095ff8c), r1 = title_global<float>(0x0095ff90);
    const float r2 = title_global<float>(0x0095ff94);
    const float r2z = r2 * zero;
    const float ax = r2z - r1;
    const float ay = r0 - r2z;
    const float az = (r1 * zero - r0 * zero) * f;
    const float wx = r0 * s + ax * f;
    const float wz = static_cast<float>(u) * title_global<float>(0x0083eba8) + (az + r2 * s);  // 2
    const float wy = ay * f + r1 * s;
    noclip_accelerate(pm, wx, wy, wz, scale, true);
}

void XB_CDECL PM_SetMovementDir() {
    // The movement direction (+0x78, -90..90 as a byte): prone (not +0xc0 0x106000) the body yaw (+0x450) less the
    // view yaw; on a ladder (pm_flags 0x10) the ladder normal's yaw + 180 less the view yaw; else, moving on the
    // ground faster than 5 / s, the yaw of the frame's move less the view yaw (+180 when backing up), else 0.
    uint8_t* const pm = pm_global();
    uint8_t* const ps = pm_ps_ref(pm);
    const uint32_t flags = at<uint32_t>(ps, 0x2c);
    if ((flags & 1) && !(at<uint32_t>(ps, 0xc0) & 0x106000)) {
        const float d = short_angle_delta(at<float>(ps, 0x450), at<float>(ps, 0xa0));
        at<int32_t>(ps, 0x78) = clamp_move_dir(static_cast<int32_t>(d));
        return;
    }
    if (flags & 0x10) {
        const float yaw = vectoyaw(&at<float>(ps, 0x64));
        const float d = short_angle_delta(yaw + title_global<float>(0x0083eb60), at<float>(ps, 0xa0));
        at<int32_t>(ps, 0x78) = clamp_move_dir(static_cast<int32_t>(d));
        return;
    }
    float move[3];
    move[0] = at<float>(ps, 0) - title_global<float>(0x00960004);  // pml.previous_origin
    move[1] = at<float>(ps, 4) - title_global<float>(0x00960008);
    move[2] = at<float>(ps, 8) - title_global<float>(0x0096000c);
    const int8_t forward = static_cast<int8_t>(pm[0x18]);
    if ((!forward && !pm[0x19]) || !at<uint32_t>(ps, 0x60)) {
        at<int32_t>(ps, 0x78) = 0;
        return;
    }
    const float length = pm_ci_sqrt(platform_x87_ld(x87_sum_squares_zyx(move[0], move[1], move[2])));
    if (x87_equal(length, title_global<float>(0x0083eafc)) ||
        !(length > title_global<float>(0x0095ffa4) * title_global<float>(0x0083ece8))) {  // 5
        at<int32_t>(ps, 0x78) = 0;
        return;
    }
    float angles[3];
    VectorNormalize2(move, angles);
    vectoangles(angles, angles);
    int32_t dir = static_cast<int32_t>(AngleDeltaQuantized(angles[1], at<float>(ps, 0xa0)));
    if (forward < 0)
        dir = static_cast<int32_t>(AngleNormalize180Quantized(static_cast<float>(dir) + title_global<float>(0x0083eb60)));
    at<int32_t>(ps, 0x78) = clamp_move_dir(dir);
}

namespace {

// Moves a prone tilt angle toward target: the 16-bit delta (above 180 wrapped), at most frametime * 70 per frame
// (sign from the delta's bits), then wrapped into (-180, 180]. `step_first`: step + angle instead of angle + delta
// for small deltas (the two angles add in different orders).
void approach_prone_angle(uint8_t* pm, uint32_t offset, float target, bool delta_first) {
    uint8_t* ps = pm_ps_ref(pm);
    float d = short_angle_delta(target, at<float>(ps, offset));
    if (d == 0.0f) return;
    const float step = title_global<float>(0x0095ffa4) * title_global<float>(0x0083eda0);  // 70
    uint32_t bits;
    std::memcpy(&bits, &d, 4);
    const uint32_t abs_bits = bits & 0x7fffffffu;
    float magnitude;
    std::memcpy(&magnitude, &abs_bits, 4);
    if (magnitude > step) {
        const int32_t sign = 1 - (static_cast<int32_t>(bits) < 0 ? 1 : 0) * 2;
        at<float>(ps, offset) = static_cast<float>(sign) * step + at<float>(ps, offset);
    } else if (delta_first) {
        at<float>(ps, offset) = d + at<float>(ps, offset);
    } else {
        at<float>(ps, offset) = at<float>(ps, offset) + d;
    }
    ps = pm_ps_ref(pm);
    float v = at<float>(ps, offset);
    const float lo = title_global<float>(0x0083ebd0), hi = title_global<float>(0x0083eb60);  // -180, 180
    const float turn = title_global<float>(0x0083ebcc);
    if (lo >= v) {
        do v += turn;
        while (lo >= v);
    } else if (v > hi) {
        do v -= turn;
        while (v > hi);
    }
    at<float>(ps, offset) = v;
}

}  // namespace

void XB_CDECL PM_UpdateProneTilt() {
    // Prone players: in the air, feet that no longer fit (BG_CheckProneFeet 30 / 60) or water add event 0x8f and
    // pm_flags 0x8000; on steep ground (normal z below 0.7) only the event. Then the body pitch (+0x454) and roll
    // (+0x458) approach the ground plane's pitch along the body yaw (+0x450) and the view yaw (+0xa0), 0 without
    // a ground plane.
    uint8_t* const pm = pm_global();
    uint8_t* const ps = pm_ps_ref(pm);
    if (!(at<uint8_t>(ps, 0x2c) & 1)) return;
    const bool plane = title_global<uint32_t>(0x0095ffb0) != 0;  // pml.groundPlane
    const float* const normal = &title_global<float>(0x0095ffd0);
    if (!at<uint32_t>(ps, 0x60)) {
        using CheckProneFeet = uint32_t XB_REGS1 XB_CDECL(uint32_t, uint32_t, uint8_t*, uint32_t, uint32_t, uint8_t*,
                                                          uint8_t*, uint8_t*, uint32_t, uint32_t);
        const uint32_t r = own_function<CheckProneFeet>(&BG_CheckProneFeet_entry)(0, at<uint32_t>(ps, 0x7c), ps, 0x41f00000u,
                                                                      at<uint32_t>(ps, 0x450), ps + 0x4d8, ps + 0x4dc,
                                                                      ps + 0x4e0, at<uint32_t>(pm, 0x138), 0x42700000u);
        uint8_t* const p = pm_global();
        if (!r || p[0x121]) {
            add_ps_event(pm_ps_ref(p), 0x8f, 0);
            at<uint32_t>(pm_ps_ref(p), 0x2c) |= 0x8000;
        }
    } else if (plane) {
        if (title_global<float>(0x0083ed28) > title_global<float>(0x0095ffd8)) add_ps_event(ps, 0x8f, 0);  // 0.7
    }
    uint8_t* const p = pm_global();
    float target = 0.0f;
    if (title_global<uint32_t>(0x0095ffb0)) target = Plane_PitchAlongYaw(normal, at<float>(pm_ps_ref(p), 0x450));
    approach_prone_angle(p, 0x454, target, false);
    target = 0.0f;
    if (title_global<uint32_t>(0x0095ffb0)) target = Plane_PitchAlongYaw(normal, at<float>(pm_ps_ref(p), 0xa0));
    approach_prone_angle(p, 0x458, target, true);
}

namespace {

using PlayerTrace = void XB_CDECL(uint8_t* trace, const float* start, const float* mins, const float* maxs,
                                  const float* end, uint32_t pass, uint32_t mask);

// The trace's fraction (+0x20) below 1 with surface flag 8 (ladder).
bool trace_hit_ladder(const uint8_t* trace) {
    return title_global<float>(0x0083eb20) > at<float>(trace, 0x20) && (at<uint8_t>(trace, 0x24) & 8);
}

// All three lanes of max(a - b, c - d) below e (CMPLTPS + MOVMSKPS & 7).
bool box_inside(const float* a, const float* b, const float* c, const float* d, const float* e) {
    bool inside = true;
    for (uint32_t i = 0; i < 4; ++i) {
        const float x = a[i] - b[i], y = c[i] - d[i];
        const float m = x > y ? x : y;  // MAXPS: the second operand unless the first is greater
        const bool lt = m < e[i];
        if (i < 3 && !lt) inside = false;
    }
    return inside;
}

}  // namespace

void XB_CDECL PM_DetectLadder() {
    // The ladder check: not during a time knockback (pm_time with pm_flags 0x300), not dead (that clears the
    // ground), not right after a mount (+0xa8 = +0x420 unless = +0x424; 300 ms after +0x70). A box (bounds +6 / -6,
    // z from 8) traced 8 (walking) or 30 ahead (or into the ladder normal while on one in the air) must hit a
    // ladder surface; on an entity with brush lists (+0x450, +0x454) the swept box must lie inside the entity's
    // bounds and touch a ladder brush (flag 8). Walking needs a forward move. A hit stores the normal (+0x64) and
    // sets pm_flags 0x10 - at once when already on a ladder, else after a second trace into the normal.
    uint8_t* const pm = pm_global();
    uint8_t* ps = pm_ps_ref(pm);
    alignas(16) uint8_t trace[0x40] = {};
    if (at<uint32_t>(ps, 0x30) && (at<uint32_t>(ps, 0x2c) & 0x300)) return;
    const float eight = title_global<float>(0x0083ed78);
    const float dist = title_global<uint32_t>(0x0095ffac) ? eight : title_global<float>(0x0083ee38);  // 30
    const uint32_t on_ladder = (at<uint32_t>(ps, 0x2c) >> 4) & 1;
    alignas(16) float dir[4] = {};
    if (on_ladder && !at<uint32_t>(ps, 0x60)) {
        dir[0] = 0.0f - at<float>(ps, 0x64);
        dir[1] = 0.0f - at<float>(ps, 0x68);
        dir[2] = 0.0f - at<float>(ps, 0x6c);
    } else {
        dir[0] = title_global<float>(0x0095ff80);
        dir[1] = title_global<float>(0x0095ff84);
        dir[2] = 0.0f;
        VectorNormalize(dir);
    }
    at<uint32_t>(pm_ps_ref(pm), 0x2c) &= ~0x10u;
    ps = pm_ps_ref(pm);
    if (at<int32_t>(ps, 0x24) >= 4) {
        at<uint32_t>(ps, 0x60) = 0;
        title_global<uint32_t>(0x0095ffb0) = 0;
        title_global<uint32_t>(0x0095ffac) = 0;
        return;
    }
    if (at<uint32_t>(ps, 0xa8) != at<uint32_t>(ps, 0x424) && at<uint32_t>(ps, 0xa8) == at<uint32_t>(ps, 0x420)) return;
    if (static_cast<int32_t>(at<uint32_t>(pm, 4) - at<uint32_t>(ps, 0x70)) < 0x12c) return;
    const float six = title_global<float>(0x0083ecd0);
    alignas(16) float mins[4] = {};
    alignas(16) float maxs[4] = {};
    mins[0] = at<float>(pm, 0x100) + six;
    mins[1] = at<float>(pm, 0x104) + six;
    mins[2] = eight;
    maxs[0] = at<float>(pm, 0x110) - six;
    maxs[1] = at<float>(pm, 0x114) - six;
    maxs[2] = at<float>(pm, 0x118);
    if (eight > maxs[2]) maxs[2] = eight;
    alignas(16) float end[4] = {};
    end[0] = dir[0] * dist + at<float>(pm_ps_ref(pm), 0);
    end[1] = dir[1] * dist + at<float>(pm_ps_ref(pm), 4);
    end[2] = dir[2] * dist + at<float>(pm_ps_ref(pm), 8);
    ps = pm_ps_ref(pm);
    const uint8_t* const ent = G_EntHandleToEnt(&at<uint32_t>(ps, 0x7c));
    if (!ent) return;
    const uint8_t* const lists_b = at<uint8_t*>(ent, 0x450);
    if (!lists_b) return;
    const uint8_t* const lists_a = at<uint8_t*>(ent, 0x454);
    if (!lists_a) return;
    const Lanes origin = lanes(&at<float>(ps, 0));
    const Lanes e = lanes(end);
    alignas(16) float hi[4], lo[4];
    for (uint32_t i = 0; i < 4; ++i) {
        hi[i] = origin.v[i] > e.v[i] ? origin.v[i] : e.v[i];  // MAXPS(origin, end)
        lo[i] = origin.v[i] < e.v[i] ? origin.v[i] : e.v[i];  // MINPS(origin, end)
    }
    const float half = title_global<float>(0x0083eb24);
    float extent[4];
    for (uint32_t i = 0; i < 4; ++i) extent[i] = maxs[i] - (mins[i] + maxs[i]) * half;
    float r = extent[0];
    if (r > extent[2]) r = extent[2];
    const float shrink[4] = {r, r, 0.0f, r};
    for (uint32_t i = 0; i < 4; ++i) lo[i] = lo[i] - shrink[i];
    const float grow[4] = {r, r, r * title_global<float>(0x0083ece8), r};  // 5
    for (uint32_t i = 0; i < 4; ++i) hi[i] = hi[i] + grow[i];
    const float* const zero = &title_global<float>(0x0082e3c0);
    if (box_inside(&at<float>(ent, 0x430), lo, hi, &at<float>(ent, 0x440), zero)) {
        bool found = false;
        const int32_t count_a = at<int32_t>(lists_a, 0x804);
        const uint8_t* const* list_a = at<const uint8_t* const*>(lists_a, 0x800);
        for (int32_t i = 0; i < count_a; ++i) {
            const uint8_t* const brush = list_a[i];
            if (!(brush[4] & 8)) continue;
            if (box_inside(&at<float>(brush, 0x10), hi, lo, &at<float>(brush, 0x10), &at<float>(brush, 0x20))) {
                found = true;
                break;
            }
        }
        bool hit = false;
        const int32_t count_b = at<int32_t>(lists_b, 0x1004);
        const uint8_t* const list_b = at<const uint8_t*>(lists_b, 0x1000);
        for (int32_t i = 0; i < count_b; ++i) {
            const uint8_t* const brush = at<const uint8_t*>(list_b, i * 8);
            if (!(brush[4] & 8)) continue;
            if (box_inside(&at<float>(brush, 0x10), hi, lo, &at<float>(brush, 0x10), &at<float>(brush, 0x20))) {
                hit = true;
                break;
            }
        }
        if (!hit && !found) return;
    }
    own_function<PlayerTrace>(&PM_PlayerTrace)(trace, &at<float>(ps, 0), mins, maxs, end, at<uint32_t>(ps, 0x7c),
                                           at<uint32_t>(pm, 0x5c));
    if (!trace_hit_ladder(trace)) return;
    uint8_t* const p = pm_global();
    if (title_global<uint32_t>(0x0095ffac) && static_cast<int8_t>(p[0x18]) <= 0) return;
    for (uint32_t i = 0; i < 3; ++i) at<float>(pm_ps_ref(p), 0x64 + i * 4) = at<float>(trace, 0x10 + i * 4);
    if (on_ladder) {
        at<uint32_t>(pm_ps_ref(p), 0x2c) |= 0x10;
        return;
    }
    uint8_t* q = pm_ps_ref(p);
    const float n0 = 0.0f - at<float>(q, 0x64), n1 = 0.0f - at<float>(q, 0x68), n2 = 0.0f - at<float>(q, 0x6c);
    end[0] = n0 * dist + at<float>(q, 0);
    end[1] = n1 * dist + at<float>(pm_ps_ref(p), 4);
    end[2] = n2 * dist + at<float>(pm_ps_ref(p), 8);
    q = pm_ps_ref(p);
    own_function<PlayerTrace>(&PM_PlayerTrace)(trace, &at<float>(q, 0), mins, maxs, end, at<uint32_t>(q, 0x7c),
                                           at<uint32_t>(p, 0x5c));
    if (trace_hit_ladder(trace)) at<uint32_t>(pm_ps_ref(pm_global()), 0x2c) |= 0x10;
}

void XB_CDECL PM_PeriodicContentsEvent() {
    // The xy speed (pm +0x124) below the start speed (int 0x00927ecc): a stamp (+0x40) older than 0x0092f00c ms is
    // cleared. At or above it, the interval goes from 0x0092f0ac to 0x00929c44 ms over the speeds up to 0x0093e6cc;
    // once it has passed, a box of the player's bounds (x0.75, top x0.9) at the origin that starts in contents 2 adds event 0x8b
    // and restamps.
    uint8_t* const pm = pm_global();
    alignas(16) uint8_t trace[0x40] = {};
    const int32_t start = title_global<int32_t>(0x00927ecc);
    const float level = static_cast<float>(start);
    uint8_t* const ps = pm_ps_ref(pm);
    if (level > at<float>(pm, 0x124)) {
        if (at<int32_t>(ps, 0x40) + title_global<int32_t>(0x0092f00c) < at<int32_t>(pm, 4)) at<uint32_t>(ps, 0x40) = 0;
        return;
    }
    float frac = (at<float>(pm, 0x124) - level) / static_cast<float>(title_global<int32_t>(0x0093e6cc) - start);
    const float one = title_global<float>(0x0083eb20);
    if (frac > one) frac = one;
    const int32_t lo = title_global<int32_t>(0x0092f0ac);
    const int32_t interval =
        static_cast<int32_t>(static_cast<float>(title_global<int32_t>(0x00929c44) - lo) * frac + static_cast<float>(lo));
    if (interval + at<int32_t>(ps, 0x40) >= at<int32_t>(pm, 4)) return;
    const float k = title_global<float>(0x0083eb58);  // 0.75
    // 16-byte aligned vec4s like the original's ([esp+0x20] / [esp+0x10], lane 3 unwritten): CM_CandidateCapsuleTrace
    // reads them with MOVAPS (float[3] crashed on movement once the stack depth left them misaligned, 11-05).
    alignas(16) float mins[4], maxs[4];
    mins[0] = at<float>(pm, 0x100) * k;
    mins[1] = at<float>(pm, 0x104) * k;
    mins[2] = at<float>(pm, 0x108) * k;
    maxs[0] = at<float>(pm, 0x110) * k;
    maxs[1] = at<float>(pm, 0x114) * k;
    maxs[2] = at<float>(pm, 0x118) * title_global<float>(0x0083ec18);  // 0.9
    own_function<PlayerTrace>(&PM_PlayerTrace)(trace, &at<float>(ps, 0), mins, maxs, &at<float>(ps, 0), at<uint32_t>(ps, 0x7c),
                                           2);
    if (!trace[0x3d]) return;
    uint8_t* const p = pm_global();
    add_ps_event(pm_ps_ref(p), 0x8b, 0);
    at<uint32_t>(pm_ps_ref(p), 0x40) = at<uint32_t>(p, 4);
}


namespace {

// The candidate lists PM_PlayerTrace builds on its stack: brushes {entries, pointer +0x800, count +0x804}, triangles
// {entries, pointer +0x1000, count +0x1004} (layout as cm.h).
struct PlayerTraceLists {
    alignas(16) uint8_t brushes[0x810];
    alignas(16) uint8_t triangles[0x1010];
};

using PmTraceCallback = void XB_CDECL(uint8_t* results, const float* start, const float* mins, const float* maxs,
                                      const float* end, uint32_t pass, uint32_t mask);

void player_trace_cached(uint8_t* results, const float* start, const float* mins, const float* maxs, const float* end,
                         uint32_t pass, uint32_t mask, const uint8_t* ent, PlayerTraceLists* lists, const float* lo,
                         const float* hi) {
    CM_FilterBoxCandidates(lists->brushes, 0, at<uint8_t*>(ent, 0x454), lo, hi, mask, at<uint8_t*>(ent, 0x450),
                           lists->triangles);
    // By title address: the C SV_CapsuleTrace leaves other dead-stack words (0x804fff2c / 0x81e23c5c) in tests.
    using CapsuleTrace = void XB_CDECL(uint32_t, uint32_t, uint8_t*, const float*, const float*, const float*,
                                       const float*, uint32_t, uint32_t);
    own_function<CapsuleTrace>(&SV_CapsuleTrace)(address_of(lists->triangles), address_of(lists->brushes), results, start,
                                             mins, maxs, end, pass, mask);
}

}  // namespace

void XB_CDECL PM_PlayerTrace(uint8_t* results, const float* start, const float* mins, const float* maxs,
                             const float* end, uint32_t pass, uint32_t mask) {
    // pm's trace with the pass entity's cached candidate lists (+0x450 triangles, +0x454 brushes, box +0x430/+0x440;
    // the swept box grown by min(extent x, z) (z top x4) and regathered 50 wider when it leaves the cached box):
    // filtered into stack lists and traced with SV_CapsuleTrace; else pm's trace callback (+0x130). A start-solid
    // result in a player/vehicle content (+0x28 & 0x200c000) adds the hit entity (+0x30) to the touch list, drops
    // those contents from pm's mask and from this trace, and traces again.
    PlayerTraceLists lists;
    at<uint8_t*>(lists.triangles, 0x1000) = lists.triangles;
    at<uint32_t>(lists.triangles, 0x1004) = 0;
    at<uint8_t*>(lists.brushes, 0x800) = lists.brushes;
    at<uint32_t>(lists.brushes, 0x804) = 0;
    const uint32_t index = pass & 0xfff;
    uint8_t* ent = nullptr;
    if (index < 0x540 && (pass >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
        ent = title_global<uint8_t*>(0x0097a968 + index * 8);
    alignas(16) float lo[4], hi[4];
    const bool cached = ent && at<uint32_t>(ent, 0x450) && at<uint32_t>(ent, 0x454);
    if (cached) {
        const float half = title_global<float>(0x0083eb24);
        float extent[4];
        for (uint32_t i = 0; i < 4; ++i) {
            hi[i] = start[i] > end[i] ? start[i] : end[i];  // MAXPS(start, end)
            lo[i] = start[i] < end[i] ? start[i] : end[i];  // MINPS(start, end)
            extent[i] = maxs[i] - (mins[i] + maxs[i]) * half;
        }
        float r = extent[0];
        if (r > extent[2]) r = extent[2];
        const float shrink[4] = {r, r, 0.0f, r};
        for (uint32_t i = 0; i < 4; ++i) lo[i] = lo[i] - shrink[i];
        const float grow[4] = {r, r, r * title_global<float>(0x0083100c), r};  // 4
        for (uint32_t i = 0; i < 4; ++i) hi[i] = hi[i] + grow[i];
        float* const box_lo = &at<float>(ent, 0x430);
        float* const box_hi = &at<float>(ent, 0x440);
        if (!box_inside(box_lo, lo, hi, box_hi, &title_global<float>(0x0082e3c0))) {
            uint32_t& guard = title_global<uint32_t>(0x0098eed0);
            if (!(guard & 1)) {
                guard |= 1;
                const float wide = title_global<float>(0x0083ec30);  // 50
                for (uint32_t i = 0; i < 4; ++i) title_global<float>(0x0098eec0 + i * 4) = wide;
            }
            float new_lo[4], new_hi[4];
            for (uint32_t i = 0; i < 4; ++i) new_lo[i] = lo[i] - title_global<float>(0x0098eec0 + i * 4);
            std::memcpy(box_lo, new_lo, 16);
            for (uint32_t i = 0; i < 4; ++i) new_hi[i] = hi[i] + title_global<float>(0x0098eec0 + i * 4);
            std::memcpy(box_hi, new_hi, 16);
            CM_GatherBoxCandidates(box_lo, at<uint8_t*>(ent, 0x450), at<uint8_t*>(ent, 0x454), box_hi);
        }
        player_trace_cached(results, start, mins, maxs, end, pass, mask, ent, &lists, lo, hi);
    } else {
        title_function<PmTraceCallback>(at<uint32_t>(pm_global(), 0x130))(results, start, mins, maxs, end, pass, mask);
    }
    if (!results[0x3d] || !(at<uint32_t>(results, 0x28) & 0x200c000)) return;
    PM_AddTouchEnt(at<int32_t>(results, 0x30));
    at<uint32_t>(pm_global(), 0x5c) &= 0xfdff3fffu;
    const uint32_t masked = mask & 0xfdff3fffu;
    if (cached) {
        player_trace_cached(results, start, mins, maxs, end, pass, masked, ent, &lists, lo, hi);
        return;
    }
    title_function<PmTraceCallback>(at<uint32_t>(pm_global(), 0x130))(results, start, mins, maxs, end, pass, masked);
}

void XB_CDECL PM_GroundTrace() {
    // Q3's PM_GroundTrace: a trace 1 down from 15 up (pm's bounds) into pml.groundTrace (0x0095ffc0, 0x40 bytes).
    // All solid: G_PushCapsuleOutOfSolids at heights 15, 17 ... retraced until free, above 25 the player is in the
    // air (pm_flags 0x2000 and +0x74 cleared). Start solid: a retrace from 2 up still solid leaves the ground. Then
    // a fraction of 1, a jump off (no ladder flag 0x10, rising, velocity . normal > 10) or a steep plane (normal z
    // below 0.7: ground plane only) leave the ground; else walking, PM_CrashLand on landing, the ground entity
    // (+0x60) and touch.
    uint8_t* const pm = pm_global();
    alignas(16) uint8_t trace[0x40] = {};
    const uint32_t mask = at<uint32_t>(pm, 0x5c);
    alignas(16) float start[4], end[4];
    const auto set_points = [&start, &end](const uint8_t* ps) {
        std::memcpy(start, ps, 16);
        start[2] = start[2] + title_global<float>(0x0083ef10);  // 15
        std::memcpy(end, ps, 16);
        end[2] = end[2] - title_global<float>(0x0083eb20);
    };
    const auto trace_down = [&](uint32_t m) {
        uint8_t* const p = pm_global();
        own_function<PlayerTrace>(&PM_PlayerTrace)(trace, start, &at<float>(p, 0x100), &at<float>(p, 0x110), end,
                                               at<uint32_t>(pm_ps_ref(p), 0x7c), m);  // PM_PlayerTrace
    };
    const auto leave_ground = [](uint8_t* ps, uint32_t plane) {
        at<uint32_t>(ps, 0x60) = 0;
        title_global<uint32_t>(0x0095ffb0) = plane;
        title_global<uint32_t>(0x0095ffac) = 0;
    };
    set_points(pm_ps_ref(pm));
    trace_down(mask);
    std::memcpy(&title_global<uint8_t>(0x0095ffc0), trace, 0x40);
    bool solid_check = true;
    if (trace[0x3c]) {
        float height = title_global<float>(0x0083ec78);  // 13
        for (;;) {
            height = height + title_global<float>(0x0083eba8);  // 2
            G_PushCapsuleOutOfSolids(pm_global(), height);
            set_points(pm_ps_ref(pm_global()));
            trace_down(mask);
            std::memcpy(&title_global<uint8_t>(0x0095ffc0), trace, 0x40);
            if (height > title_global<float>(0x0083ebc4)) {  // 25
                uint8_t* const p = pm_global();
                leave_ground(pm_ps_ref(p), 0);
                at<uint32_t>(pm_ps_ref(p), 0x2c) &= ~0x2000u;
                at<float>(pm_ps_ref(p), 0x74) = 0.0f;
                break;
            }
            if (!trace[0x3c]) break;
        }
    }
    uint8_t* const p = pm_global();
    if (solid_check && trace[0x3d]) {
        start[2] = at<float>(pm_ps_ref(p), 8) + title_global<float>(0x0083eba8);  // 2
        trace_down(at<uint32_t>(p, 0x5c));
        if (trace[0x3d]) {
            leave_ground(pm_ps_ref(pm_global()), 0);
            return;
        }
        std::memcpy(&title_global<uint8_t>(0x0095ffc0), trace, 0x40);
    }
    if (at<float>(trace, 0x20) == title_global<float>(0x0083eb20)) {
        leave_ground(pm_ps_ref(p), 0);
        return;
    }
    uint8_t* const ps = pm_ps_ref(p);
    const float nz = at<float>(trace, 0x18);
    if (!(at<uint8_t>(ps, 0x2c) & 0x10) && at<float>(ps, 0x18) > title_global<float>(0x0083eafc)) {
        const float d = (at<float>(ps, 0x10) * at<float>(trace, 0x10) + at<float>(ps, 0x14) * at<float>(trace, 0x14)) +
                        at<float>(ps, 0x18) * nz;
        if (d > title_global<float>(0x0083eb98)) {  // 10
            leave_ground(ps, 0);
            return;
        }
    }
    if (title_global<float>(0x0083ed28) > nz) {  // 0.7
        leave_ground(ps, 1);
        at<uint32_t>(pm_ps_ref(p), 0x2c) &= ~0x2000u;
        at<float>(pm_ps_ref(p), 0x74) = 0.0f;
        return;
    }
    title_global<uint32_t>(0x0095ffb0) = 1;
    title_global<uint32_t>(0x0095ffac) = 1;
    if (!at<uint32_t>(pm_ps_ref(p), 0x60)) PM_CrashLand();
    at<uint32_t>(pm_ps_ref(p), 0x60) = at<uint32_t>(trace, 0x30);
    PM_AddTouchEnt(at<int32_t>(trace, 0x30));
}

void XB_REGS2 XB_CDECL PM_FootstepEvent(int32_t old_cycle, uint32_t allowed, int32_t new_cycle) {
    // When the bob cycle crosses a half (bit 7 of cycle + 0x40 changes): on the ground the surface's footstep event,
    // on a ladder (in the air, pm_flags 0x10) a short trace (bounds +6 / -6, z from 8) into the ladder normal (x
    // 0x0083f1c0) gives the ladder surface's event (type + 1; 14 for no hit or type 0); only with `allowed`. In
    // water levels 1-2 a wade event goes straight into the ring: 0x43 prone, 0x2c crouched or moving vertically,
    // else 0x5a sprinting or 0x15.
    if (!((static_cast<uint8_t>(old_cycle + 0x40) ^ static_cast<uint8_t>(new_cycle + 0x40)) & 0x80)) return;
    uint8_t* const pm = pm_global();
    const uint8_t water = pm[0x121];
    if (water) {
        if (water != 1 && water != 2) return;
        uint8_t* const ps = pm_ps_ref(pm);
        const uint32_t flags = at<uint32_t>(ps, 0x2c);
        uint32_t event;
        if (flags & 1)
            event = 0x43;
        else if ((flags & 0x80) || !(at<float>(ps, 0x4c) == 0.0f))
            event = 0x2c;
        else
            event = (flags & 0x10000) ? 0x5a : 0x15;
        add_ps_event(ps, event, 0);
        return;
    }
    uint8_t* const ps = pm_ps_ref(pm);
    if (at<uint32_t>(ps, 0x60)) {
        if (!allowed) return;
        PM_AddEvent(0, 0, PM_FootstepEventForSurface(0, at<uint32_t>(ps, 0x2c)));
        return;
    }
    if (!allowed || !(at<uint8_t>(ps, 0x2c) & 0x10)) return;
    alignas(16) uint8_t trace[0x40] = {};
    const float six = title_global<float>(0x0083ecd0);
    const float eight = title_global<float>(0x0083ed78);
    alignas(16) float mins[4], maxs[4], end[4] = {};
    std::memcpy(mins, pm + 0x100, 16);
    std::memcpy(maxs, pm + 0x110, 16);
    mins[0] = mins[0] + six;
    mins[1] = mins[1] + six;
    maxs[0] = maxs[0] - six;
    maxs[1] = maxs[1] - six;
    mins[2] = eight;
    if (eight > maxs[2]) maxs[2] = eight;
    const float reach = title_global<float>(0x0083f1c0);  // 31
    for (uint32_t i = 0; i < 3; ++i) end[i] = at<float>(ps, i * 4) - at<float>(ps, 0x64 + i * 4) * reach;
    own_function<PlayerTrace>(&PM_PlayerTrace)(trace, &at<float>(ps, 0), mins, maxs, end, at<uint32_t>(ps, 0x7c),
                                           at<uint32_t>(pm, 0x5c) & 0xfdfeffffu);  // PM_PlayerTrace
    uint32_t type = static_cast<uint32_t>(at<int32_t>(trace, 0x24) >> 0x14) & 0x1f;
    if (at<float>(trace, 0x20) == title_global<float>(0x0083eb20) || !type) type = 0xd;
    PM_AddEvent(0, 0, type + 1);
}

namespace {

// sqrt(x * x + y * y) on the x87 (FLD/FMUL/FADDP/FSQRT), stored as float.
float x87_length2(float x, float y) {
    float r;
    __asm__("flds %2\n\tflds %1\n\tfld %%st(0)\n\tfmul %%st(1), %%st\n\tfld %%st(2)\n\tfmul %%st(3), %%st\n\t"
            "faddp\n\tfsqrt\n\tfstp %%st(2)\n\tfstp %%st(0)\n\tfstps %0"
            : "=m"(r)
            : "m"(x), "m"(y)
            : "st", "st(1)", "st(2)");
    return r;
}

}  // namespace

void XB_CDECL PM_Footsteps() {
    // Q3's PM_Footsteps: pm +0x124 = the xy speed (x87). On a ladder the bob cycle (+0x28) runs with the vertical
    // speed (300 ms after a mount). Not on the ground (and not pm_type 1), a stance change since the frame's start
    // is still needed. Below speed 10 (or pm_type 1) nothing (below 1 the cycle resets); else the bob rate is the
    // xy speed over the scaled stance speed (move scales +0x444 / +0x448, stance lerps +0x43c / +0x440 through
    // PM_ViewHeightLerpFrac both ways) times a per-stance factor, and moving gives PM_FootstepEvent.
    uint8_t* pm = pm_global();
    uint8_t* ps = pm_ps_ref(pm);
    if (at<int32_t>(ps, 0x24) >= 4) return;
    at<float>(pm, 0x124) = x87_length2(at<float>(ps, 0x10), at<float>(ps, 0x14));
    if (at<uint32_t>(ps, 0xc0) & 0x106000) return;
    int32_t mount;
    if (at<uint32_t>(ps, 0xa8) == at<uint32_t>(ps, 0x424))
        mount = 2;
    else
        mount = at<uint32_t>(ps, 0xa8) == at<uint32_t>(ps, 0x420) ? 1 : 0;
    if (!at<uint32_t>(ps, 0x60) && at<int32_t>(ps, 0x24) != 1) {
        const uint32_t flags = at<uint32_t>(ps, 0x2c);
        if (flags & 0x10) {
            if (static_cast<int32_t>(at<uint32_t>(pm, 4) - at<uint32_t>(ps, 0x70)) < 0x12c) return;
            const float vz = at<float>(ps, 0x18);
            const float k = title_global<float>(0x0083f21c);
            float rate;
            if (!(flags & 0x80) && at<float>(ps, 0x4c) == 0.0f)
                rate = vz / (at<float>(ps, 0x434) * k) * title_global<float>(0x0083f034);
            else
                rate = vz / (at<float>(ps, 0x430) * k) * title_global<float>(0x0083f064);
            const int32_t old = at<int32_t>(ps, 0x28);
            at<int32_t>(ps, 0x28) =
                static_cast<int32_t>(static_cast<float>(title_global<int32_t>(0x0095ffa8)) * rate + static_cast<float>(old)) &
                0xff;
            PM_FootstepEvent(old, 1, at<int32_t>(pm_ps_ref(pm), 0x28));
            pm = pm_global();
        }
        if (static_cast<uint32_t>(mount) == (at<uint32_t>(pm_ps_ref(pm), 0x2c) & 3)) return;
    }
    ps = pm_ps_ref(pm);
    const uint32_t flags = at<uint32_t>(ps, 0x2c);
    const uint32_t crouched = flags & 0x80;
    const uint32_t sprint = flags & 0x10000;
    if (title_global<float>(0x0083eb98) > at<float>(pm, 0x124) || at<int32_t>(ps, 0x24) == 1) {  // 10
        if (title_global<float>(0x0083eb20) > at<float>(pm, 0x124)) at<uint32_t>(ps, 0x28) = 0;
        return;
    }
    const float speed = static_cast<float>(at<int32_t>(ps, 0x50));
    const float one = title_global<float>(0x0083eb20);
    const float half = title_global<float>(0x0083eb24);
    const int8_t forward = static_cast<int8_t>(pm[0x18]);
    float scale = speed;
    if (forward) {
        if (pm[0x19]) {
            scale = ((at<float>(ps, 0x444) - one) * title_global<float>(0x0083eb58) + one + one) * speed * half;
            if (forward < 0) scale = (at<float>(ps, 0x448) + one) * scale * half;
        } else if (forward < 0) {
            scale = at<float>(ps, 0x448) * speed;
        }
    } else if (pm[0x19]) {
        scale = ((at<float>(ps, 0x444) - one) * title_global<float>(0x0083eb58) + one) * speed;  // 0.75
    }
    float stance = crouched ? at<float>(ps, 0x430) : (sprint ? at<float>(ps, 0x438) : at<float>(ps, 0x434));
    const uint32_t prone_height = at<uint32_t>(ps, 0x420), crouch_height = at<uint32_t>(ps, 0x424);
    stance = stance * scale;
    float frac;
    PM_ViewHeightLerpFrac(crouch_height, prone_height, &frac);
    if (frac != 0.0f) {
        stance = ((one - frac) * at<float>(ps, 0x440) + at<float>(ps, 0x43c) * frac) * stance;
    } else {
        PM_ViewHeightLerpFrac(prone_height, crouch_height, &frac);
        if (frac != 0.0f)
            stance = ((one - frac) * at<float>(ps, 0x43c) + at<float>(ps, 0x440) * frac) * stance;
        else if (mount == 1)
            stance = at<float>(ps, 0x43c) * stance;
        else if (mount == 2)
            stance = at<float>(ps, 0x440) * stance;
    }
    float rate = at<float>(pm, 0x124) / stance;
    if (mount == 1)
        rate = rate * title_global<float>(crouched ? 0x0083f218 : 0x0083ec24);
    else if (mount == 2)
        rate = rate * title_global<float>(crouched ? 0x0083f214 : 0x0083f0c8);
    else if (at<uint8_t>(ps, 0x2c) & 0x40)
        rate = rate * title_global<float>(crouched ? 0x0083f210 : 0x0083f20c);
    else
        rate = rate * title_global<float>(crouched ? 0x0083f208 : 0x0083f204);
    uint32_t heights[2] = {0, 0};
    const uint32_t allowed = PM_CheckViewHeightStance(heights);
    const int32_t old = at<int32_t>(ps, 0x28);
    at<int32_t>(ps, 0x28) =
        static_cast<int32_t>(static_cast<float>(title_global<int32_t>(0x0095ffa8)) * rate + static_cast<float>(old)) & 0xff;
    if (!pm[0x18] && !pm[0x19]) return;
    PM_FootstepEvent(old, allowed, at<int32_t>(pm_ps_ref(pm), 0x28));
}

namespace {

// 1 - sqrt(1 - fraction) on the x87 (FLD/FSUB/FSQRT/FSUBR), stored as float.
float x87_lean_room(float one, float fraction) {
    float r;
    __asm__("flds %1\n\tfsubs %2\n\tfsqrt\n\tfsubrs %1\n\tfstps %0" : "=m"(r) : "m"(one), "m"(fraction) : "st");
    return r;
}

// |a| > b on the x87 (FABS, FCOMIP; false when unordered).
bool x87_abs_greater(float a, float b) {
    bool r;
    __asm__("flds %2\n\tflds %1\n\tfabs\n\tfcomip %%st(1), %%st\n\tfstp %%st(0)\n\tseta %0"
            : "=q"(r)
            : "m"(a), "m"(b)
            : "st", "st(1)", "cc");
    return r;
}

int32_t sign_of_bits(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return 1 - (static_cast<int32_t>(bits) < 0 ? 1 : 0) * 2;
}

}  // namespace

PLATFORM_ASM_ENTRY(PM_UpdateLean_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(PM_UpdateLean) "addl $12, %esp\n"
                   "ret")

void XB_CDECL PM_UpdateLean(const uint8_t* cmd, uint8_t* ps, uint32_t trace_fn) {
    // The lean (+0x4c, -rate..rate; rate 0.5, prone 0x0083ec24) moves toward the held lean buttons (cmd +5: 0x10
    // left unless +0x2c 0x400000, 0x20 right unless 0x800000; on the ground or pm_type 1, not +0x2c 0x4000 or dead;
    // none on +0xc0 0x106000 or aiming a mode-3 weapon) by msec * 0.0029 * rate, back toward 0 by msec * 0.0036 *
    // rate. A lean is then limited by a trace (box +-8, mask 0x381c011, the given trace function) from the eye to
    // the leaned eye (AddLeanToPosition 16 / 20): |lean| at most 1 - sqrt(1 - fraction).
    int32_t dir = 0;
    const uint8_t buttons = cmd[5];
    if (buttons & 0x30) {
        const uint32_t flags = at<uint32_t>(ps, 0x2c);
        const int32_t type = at<int32_t>(ps, 0x24);
        if (!(flags & 0x4000) && type < 4 && (at<uint32_t>(ps, 0x60) || type == 1)) {
            if ((buttons & 0x10) && !(flags & 0x400000)) dir = -1;
            if ((buttons & 0x20) && !(flags & 0x800000)) ++dir;
        }
    }
    if ((at<uint32_t>(ps, 0xc0) & 0x106000) ||
        ((at<uint8_t>(ps, 0x2c) & 0x20) &&
         at<int32_t>(title_global<uint8_t* const*>(0x0095f4d8)[at<int32_t>(ps, 0x80)], 0x98) == 3))
        dir = 0;
    float rate = title_global<float>(0x0083eb24);  // 0.5
    if (at<uint32_t>(ps, 0xa8) != at<uint32_t>(ps, 0x424) && at<uint32_t>(ps, 0xa8) == at<uint32_t>(ps, 0x420))
        rate = title_global<float>(0x0083ec24);
    float lean = at<float>(ps, 0x4c);
    const float zero = 0.0f;
    const float msec = static_cast<float>(title_global<int32_t>(0x0095ffa8));
    if (!dir) {
        if (lean > zero) {
            lean = lean - msec * title_global<float>(0x0083f0a4) * rate;
            if (zero > lean) lean = zero;
        } else if (zero > lean) {
            lean = msec * title_global<float>(0x0083f0a4) * rate + lean;
            if (lean > zero) lean = zero;
        }
    } else if (dir > 0) {
        if (rate > lean) lean = msec * title_global<float>(0x0083f0a0) * rate + lean;
        if (lean > rate) lean = rate;
    } else {
        const float low = zero - rate;
        if (lean > low) lean = lean - msec * title_global<float>(0x0083f0a0) * rate;
        if (low > lean) lean = low;
    }
    at<float>(ps, 0x4c) = lean;
    if (lean == zero || at<int32_t>(ps, 0x24) == 1) return;
    alignas(16) float start[4], end[4], mins[4], maxs[4];
    alignas(16) uint8_t trace[0x40] = {};
    start[0] = at<float>(ps, 0);
    start[1] = at<float>(ps, 4);
    start[2] = at<float>(ps, 0xac) + at<float>(ps, 8);
    float pos[3] = {start[0], start[1], start[2]};
    AddLeanToPosition(pos, static_cast<float>(sign_of_bits(lean)), at<float>(ps, 0xa0), 16.0f, 20.0f);
    end[0] = pos[0];
    end[1] = pos[1];
    end[2] = pos[2];
    end[3] = pos[0];
    start[3] = start[0];
    const float eight = title_global<float>(0x0083ed78), minus_eight = title_global<float>(0x003f48ac);
    for (uint32_t i = 0; i < 4; ++i) {
        maxs[i] = eight;
        mins[i] = minus_eight;
    }
    using TraceFn = void XB_CDECL(uint8_t*, const float*, const float*, const float*, const float*, uint32_t, uint32_t);
    title_function<TraceFn>(trace_fn)(trace, start, mins, maxs, end, at<uint32_t>(ps, 0x7c), 0x381c011);
    const float room = x87_lean_room(title_global<float>(0x0083eb20), at<float>(trace, 0x20));
    if (x87_abs_greater(at<float>(ps, 0x4c), room))
        at<float>(ps, 0x4c) = static_cast<float>(sign_of_bits(at<float>(ps, 0x4c))) * room;
}

namespace {

void clear_weapon_bit(uint8_t* ps, uint32_t offset) {
    const int32_t weapon = at<int32_t>(ps, 0x80);
    at<uint32_t>(ps, offset + static_cast<uint32_t>(weapon >> 5) * 4) &= ~(1u << (weapon & 0x1f));
}

}  // namespace

uint32_t XB_CDECL PM_UpdateWeaponReload(uint32_t fired) {
    // The reload states: 7 / 8 (reloading; ammo at the reload time when `fired`) wait for the weapon time, then
    // reload again while possible (state 8 only with an empty clip), else end with the reload-end time (+0x604:
    // state 9, anim 0xe, event 0x9c) or state 0; state 9 ends in state 0. States 0xe / 5 / 6 the same, with a
    // non-reloading weapon (+0x708 clear) back to state 0 and state 6 never restarting. Clears the weapon's +0x400
    // bit when done. 1 while busy.
    uint8_t* const pm = pm_global();
    uint8_t* ps = pm_ps_ref(pm);
    const int32_t state = at<int32_t>(ps, 0x84);
    if (state == 7 || state == 8) {
        if (fired) PM_ReloadAmmoAtTime();
        ps = pm_ps_ref(pm);
        if (at<uint32_t>(ps, 0x34)) return 1;
        const uint8_t* const def = title_global<uint8_t* const*>(0x0095f4d8)[at<int32_t>(ps, 0x80)];
        if (at<int32_t>(ps, 0x84) != 8 || !at<uint32_t>(ps, 0x280 + at<uint32_t>(def, 0x5a0) * 4)) {
            if (PM_WeaponCanReload()) {
                PM_StartReload();
                return 1;
            }
        }
        clear_weapon_bit(ps, 0x400);
        if (!at<uint32_t>(cur_weapon_def(), 0x604)) {
            at<uint32_t>(pm_ps_ref(pm), 0x84) = 0;
            PM_StartLegsAnim(0, 0, 0);
            return 0;
        }
        at<uint32_t>(pm_ps_ref(pm), 0x84) = 9;
        uint8_t* const p = pm_ps_ref(pm);
        if (at<int32_t>(p, 0x24) < 4 && pm[0xa]) {
            if (at<int32_t>(cur_weapon_def(), 0x94) == 5)
                at<uint32_t>(p, 0x4ec) = 0xe;
            else
                at<uint32_t>(p, 0x4ec) = (~at<uint32_t>(p, 0x4ec) & 0x200) | 0xe;
        }
        at<uint32_t>(pm_ps_ref(pm), 0x34) = at<uint32_t>(cur_weapon_def(), 0x604);
        add_ps_event(pm_ps_ref(pm), 0x9c, 0);
        return 1;
    }
    if (state == 9) {
        at<uint32_t>(ps, 0x84) = 0;
        PM_StartLegsAnim(0, 0, 0);
        return 0;
    }
    if (state != 0xe && state != 5 && state != 6) return 0;
    if (fired) {
        PM_ReloadAmmoAtTime();
        ps = pm_ps_ref(pm);
        if (at<uint32_t>(ps, 0x34)) return 1;
    } else if (at<uint32_t>(ps, 0x34)) {
        return 0;
    }
    clear_weapon_bit(ps, 0x400);
    const uint8_t* const def = cur_weapon_def();
    if (!at<uint32_t>(def, 0x708)) {
        at<uint32_t>(pm_ps_ref(pm), 0x84) = 0;
        PM_StartLegsAnim(0, 0, 0);
        return fired ? 1 : 0;
    }
    uint8_t* const p = pm_ps_ref(pm);
    if (at<int32_t>(p, 0x84) != 6 && PM_WeaponCanReload()) {
        PM_StartReload();
        return 1;
    }
    if (!at<uint32_t>(def, 0x604)) {
        at<uint32_t>(p, 0x84) = 0;
        PM_StartLegsAnim(0, 0, 0);
        return fired ? 1 : 0;
    }
    at<uint32_t>(p, 0x84) = 9;
    PM_StartLegsAnim(0, 0, 0xe);
    at<uint32_t>(pm_ps_ref(pm), 0x34) = at<uint32_t>(cur_weapon_def(), 0x604);
    add_ps_event(pm_ps_ref(pm), 0x9c, 0);
    return 1;
}

void XB_CDECL PM_FinishReloadEffect() {
    // Weapon state 14: while the reload effect (id +0x90 in the list at 0x0095f5b4) still has live components the
    // weapon time is the start-reload time (+0x5f4 with +0x8c, else +0x5f0); then the effect stops (+0x90 cleared
    // for reloading weapons), anim 0xb / 0xc, that time, state 5 (6 from state 8) and PM_SetReloadDelay.
    uint8_t* const pm = pm_global();
    FxIdList* const list = title_global<FxIdList*>(0x0095f5b4);
    const uint32_t* const words = reinterpret_cast<const uint32_t*>(list);
    const uint32_t count = words[0];
    const uint32_t id = at<uint32_t>(pm_ps_ref(pm), 0x90);
    for (uint32_t i = 0; i < count; ++i) {
        if (at<uint32_t>(reinterpret_cast<const uint8_t*>(uintptr_t{words[1 + i]}), 0x4c) != id) continue;
        const uint8_t* const effect = reinterpret_cast<const uint8_t*>(uintptr_t{words[1 + i]});
        if (!effect || FX_EffectComponentsFinished(effect)) break;
        uint8_t* const ps = pm_ps_ref(pm_global());
        at<uint32_t>(ps, 0x34) = at<uint32_t>(cur_weapon_def(), at<uint8_t>(ps, 0x8c) ? 0x5f4 : 0x5f0);
        return;
    }
    FX_StopEffectById(at<uint32_t>(pm_ps_ref(pm_global()), 0x90), 0, list);
    uint8_t* const p = pm_global();
    if (at<uint32_t>(cur_weapon_def(), 0x708)) at<uint32_t>(pm_ps_ref(p), 0x90) = 0;
    const uint8_t started = at<uint8_t>(pm_ps_ref(p), 0x8c);
    const uint32_t time = at<uint32_t>(cur_weapon_def(), started ? 0x5f4 : 0x5f0);
    PM_StartLegsAnim(0, 0, (started ? 1u : 0u) + 0xb);
    at<uint32_t>(pm_ps_ref(p), 0x34) = time;
    uint8_t* const ps = pm_ps_ref(p);
    at<uint32_t>(ps, 0x84) = (at<int32_t>(ps, 0x84) == 8 ? 1u : 0u) + 5;
    PM_SetReloadDelay();
}

void XB_CDECL PM_CheckWeaponReload() {
    // Fire during a reload (states 7 / 5, newly pressed attack) asks to stop it (8 / 6). Not with a mode-3 weapon
    // while 0x0095f4dc is set unless aiming fully (ADS above 0.99), not sprinting; state 14 goes on with
    // PM_FinishReloadEffect, states 1, 2 and 5-13 wait; else the reload button (cmd +5 0x8) when it can reload,
    // or an empty clip with reserve ammo (not state 3; prone only standing still) start PM_BeginWeaponReload.
    uint8_t* const pm = pm_global();
    const uint32_t reload_button = pm[9] & 8;
    uint32_t start = 0;
    if (at<uint32_t>(cur_weapon_def(), 0x708)) {
        uint8_t* const ps = pm_ps_ref(pm);
        const int32_t state = at<int32_t>(ps, 0x84);
        if ((state == 7 || state == 5) && (pm[8] & 1) && !(pm[0x34] & 1)) {
            if (state == 7)
                at<uint32_t>(ps, 0x84) = 8;
            else if (state == 5)
                at<uint32_t>(ps, 0x84) = 6;
        }
    }
    if (title_global<uint8_t>(0x0095f4dc) && at<int32_t>(cur_weapon_def(), 0x98) == 3) {
        const uint8_t* const ps = pm_ps_ref(pm);
        if (!(at<uint8_t>(ps, 0x2c) & 0x20) || title_global<float>(0x0083ed08) > at<float>(ps, 0x88)) return;  // 0.99
    }
    uint8_t* const ps = pm_ps_ref(pm);
    const uint32_t flags = at<uint32_t>(ps, 0x2c);
    if (flags & 0x10000) return;
    const int32_t state = at<int32_t>(ps, 0x84);
    if (state == 14) {
        PM_FinishReloadEffect();
        return;
    }
    if (state == 1 || state == 2 || (state >= 5 && state <= 13)) return;
    const uint8_t* const def = title_global<uint8_t* const*>(0x0095f4d8)[at<int32_t>(ps, 0x80)];
    const uint32_t ammo_slot = at<uint32_t>(def, 0x598);
    const uint32_t clip_slot = at<uint32_t>(def, 0x5a0);
    if (reload_button && PM_WeaponCanReload()) start = 1;
    if (!at<uint32_t>(ps, 0x280 + clip_slot * 4) && at<uint32_t>(ps, 0x110 + ammo_slot * 4) && state != 3 &&
        (!(flags & 1) || (!pm[0x18] && !pm[0x19]))) {
        PM_BeginWeaponReload();
        return;
    }
    if (start) PM_BeginWeaponReload();
}

void XB_CDECL PM_CheckWeaponMelee(uint32_t busy) {
    // The melee button (cmd +4 0x20; a target actor to activate takes it first): weapons with melee (+0x5cc), not
    // aiming, type-1 weapons not while firing or in state 3, not when `busy`, not mid-shot (+0x38) outside the reload
    // states 5-9. Released: pm_flags 0x1000 cleared; newly pressed (no 0x1000 / 0x10000): 0x1000 and, outside states
    // 1, 2, 10 and 11, anim 8, event 0xa9 and a melee with a charge time (+0x5d8: weapon time +0x5ec, state 10,
    // PM_SetProneFlag400) or PM_WeaponMeleeStrike at once.
    uint8_t* pm = pm_global();
    if ((pm[8] & 0x20) && PM_ActivateTargetActor()) return;
    pm = pm_global();
    const uint8_t* const def = cur_weapon_def();
    if (!at<uint32_t>(def, 0x5cc)) return;
    uint8_t* const ps = pm_ps_ref(pm);
    const uint32_t flags = at<uint32_t>(ps, 0x2c);
    if (flags & 0x20) return;
    if (at<int32_t>(def, 0x94) == 1 && ((pm[8] & 3) || at<int32_t>(ps, 0x84) == 3)) return;
    if (busy) return;
    if (at<uint32_t>(ps, 0x38)) {
        const int32_t s = at<int32_t>(ps, 0x84);
        if (s != 5 && s != 7 && s != 9 && s != 8 && s != 6) return;
    }
    if (!(pm[8] & 0x20)) {
        at<uint32_t>(ps, 0x2c) &= ~0x1000u;
        return;
    }
    if (flags & 0x11000) return;
    at<uint32_t>(ps, 0x2c) |= 0x1000;
    const int32_t state = at<int32_t>(pm_ps_ref(pm), 0x84);
    if (state == 1 || state == 2 || state == 10 || state == 11) return;
    PM_StartLegsAnim(0, 0, 8);
    add_ps_event(pm_ps_ref(pm), 0xa9, 0);
    if (!at<uint32_t>(cur_weapon_def(), 0x5d8)) {
        PM_WeaponMeleeStrike();
        return;
    }
    at<uint32_t>(pm_ps_ref(pm), 0x34) = at<uint32_t>(cur_weapon_def(), 0x5ec);
    at<uint32_t>(pm_ps_ref(pm), 0x38) = at<uint32_t>(cur_weapon_def(), 0x5d8);
    at<uint32_t>(pm_ps_ref(pm), 0x84) = 10;
    PM_SetProneFlag400();
}

uint32_t XB_CDECL PM_UpdateGrenadeThrow() {
    // Grenades (+0x9c 4): the throw button (cmd +4 0x2) only works for type-1 grenades (else 1). State 1 (raised):
    // released throws (state 3, times +0x5d4 / +0x5dc, fire anim); held with a loaded clip starts the cook (fuse
    // +0x3c = +0x618, event 0xa2, time +0x5e8, state 0xf). State 0xf released throws; state 3 releases the grenade
    // when its time is up (ammo, event 0xa3, blend, state 0xb); state 0xb when done re-selects (the empty weapon
    // leaves its slot), state 0, the HUD fuse (-1) and fuse cleared. 1 while handled.
    const uint8_t* def = cur_weapon_def();
    if (at<int32_t>(def, 0x9c) != 4) return 0;
    uint8_t* const pm = pm_global();
    const uint8_t throw_held = pm[8] & 2;
    if (throw_held && at<int32_t>(def, 0x94) != 1) return 1;
    uint8_t* ps = pm_ps_ref(pm);
    const int32_t state = at<int32_t>(ps, 0x84);
    if (state == 1) {
        if (!throw_held) {
            at<uint32_t>(ps, 0x3c) = 0;
            at<uint32_t>(pm_ps_ref(pm), 0x38) = at<uint32_t>(cur_weapon_def(), 0x5d4);
            at<uint32_t>(pm_ps_ref(pm), 0x34) = at<uint32_t>(cur_weapon_def(), 0x5dc);
            at<uint32_t>(pm_ps_ref(pm), 0x84) = 3;
            PM_StartFireAnim();
            return 1;
        }
        const uint8_t* const wdef = title_global<uint8_t* const*>(0x0095f4d8)[at<int32_t>(ps, 0x80)];
        if (at<uint32_t>(ps, 0x280 + at<uint32_t>(wdef, 0x5a0) * 4) && !at<uint32_t>(ps, 0x3c)) {
            at<uint32_t>(ps, 0x3c) = at<uint32_t>(def, 0x618);
            add_ps_event(pm_ps_ref(pm), 0xa2, 0);
            at<uint32_t>(pm_ps_ref(pm), 0x38) = at<uint32_t>(cur_weapon_def(), 0x5e8);
            at<uint32_t>(pm_ps_ref(pm), 0x34) = 0;
            at<uint32_t>(pm_ps_ref(pm), 0x84) = 0xf;
        }
        return 1;
    }
    if (state == 0xf && at<int32_t>(def, 0x94) == 1) {
        if (!throw_held) {
            at<uint32_t>(ps, 0x38) = at<uint32_t>(def, 0x5d4);
            at<uint32_t>(pm_ps_ref(pm), 0x34) = at<uint32_t>(cur_weapon_def(), 0x5dc);
            at<uint32_t>(pm_ps_ref(pm), 0x84) = 3;
            PM_StartFireAnim();
        }
        return 1;
    }
    if (state == 3 && at<int32_t>(def, 0x94) == 1) {
        if (at<int32_t>(ps, 0x38) <= 0) {
            PM_WeaponUseAmmo(0, 0, at<uint32_t>(ps, 0x80), 1);
            add_ps_event(pm_ps_ref(pm), 0xa3, 0);
            PM_AddWeaponFireBlend();
            at<uint32_t>(pm_ps_ref(pm), 0x84) = 0xb;
        }
        return 1;
    }
    if (state == 0xb && at<int32_t>(ps, 0x34) <= 0) {
        at<uint32_t>(ps, 0x38) = at<uint32_t>(def, 0x5dc);
        at<uint32_t>(pm_ps_ref(pm), 0x34) = 0;
        const uint32_t ammo = PM_WeaponCheckAmmo();
        uint8_t* p = pm_global();
        if (!ammo) BG_RemoveWeaponFromSlot(at<uint32_t>(pm_ps_ref(p), 0x80), pm_ps_ref(p));
        CG_SelectWeapon(at<int32_t>(pm_ps_ref(p), 0x94));
        p = pm_global();
        at<uint32_t>(pm_ps_ref(p), 0x84) = 0;
        uint8_t* const hud = at<uint8_t*>(title_global<uint8_t*>(0x0095f8f0), 0x34);
        if (hud) {
            at<float>(hud, 0x24) = title_global<float>(0x0083eb34);
            at<float>(hud, 0x28) = title_global<float>(0x0083eb34);
        }
        at<uint32_t>(pm_ps_ref(p), 0x3c) = 0;
        return 1;
    }
    if (static_cast<int8_t>(at<uint8_t>(ps, 0x3fc)) == at<int32_t>(def, 0) && at<int32_t>(def, 0x94) == 1 && state == 3)
        return 1;
    return 0;
}

void XB_CDECL PM_Weapon() {
    // Q3's PM_Weapon. Not with pm_flags 0x800; dead clears the weapon; +0xc0 0x6000 nothing, 0x100000 only the ADS
    // update and the weapon back to idle. Else the ADS update, the HUD fuse reset, a cooking grenade's fuse (+0x3c:
    // cook start at +0x618 - 10 with event 0xb7, then down by msec; at 0 it explodes in hand: ammo, event 0xc6, time
    // 1600, PM_RemoveEmptyWeapon), then the timers, ADS, weapon change, reload and melee checks, the rechamber, the
    // full blend for moving prone / sprinting / melee, and the first of reload, melee end, weapon change end,
    // grenade, raise / ADS ends, fire release and fire (not while 0x0095bf9c or a target actor takes the use).
    uint8_t* pm = pm_global();
    uint8_t* ps = pm_ps_ref(pm);
    if (at<uint32_t>(ps, 0x2c) & 0x800) return;
    const uint32_t blocked = title_global<uint32_t>(0x0095bf9c) ? 1 : 0;
    if (at<int32_t>(ps, 0x24) >= 4) {
        at<uint32_t>(ps, 0x80) = 0;
        return;
    }
    const uint32_t c0 = at<uint32_t>(ps, 0xc0);
    if (c0 & 0x6000) return;
    if (c0 & 0x106000) {
        PM_UpdateAimDownSightFrac();
        if (at<uint32_t>(pm_ps_ref(pm), 0x84)) PM_StartLegsAnim(0, 0, 0);
        at<uint32_t>(pm_ps_ref(pm), 0x34) = 0;
        at<uint32_t>(pm_ps_ref(pm), 0x38) = 0;
        at<uint32_t>(pm_ps_ref(pm), 0x84) = 0;
        if (!pm[0xa]) return;
        const uint8_t* const p = pm_ps_ref(pm);
        uint32_t anim = 0;
        if (platform_greater(at<float>(p, 0x88), title_global<float>(0x0083ec18))) anim = 0x17;  // 0.9
        if ((at<uint32_t>(p, 0x4ec) & 0xfffffdffu) != anim) PM_StartLegsAnim(0, 0, anim);
        return;
    }
    PM_UpdateAimDownSightFrac();
    uint8_t* hud = at<uint8_t*>(title_global<uint8_t*>(0x0095f8f0), 0x34);
    if (hud) {
        at<float>(hud, 0x24) = title_global<float>(0x0083eb34);  // -1
        at<float>(hud, 0x28) = title_global<float>(0x0083eb34);
    }
    const uint8_t* def = cur_weapon_def();
    if (at<int32_t>(def, 0x94) == 1) {
        const int32_t base = title_global<int32_t>(0x008fef44);
        ps = pm_ps_ref(pm);
        if (at<uint32_t>(def, 0x6e4) && at<int32_t>(ps, 0x3c) >= at<int32_t>(def, 0x618)) {
            at<int32_t>(ps, 0x3c) = at<int32_t>(ps, 0x3c) - 10;
            const float total = static_cast<float>(at<int32_t>(cur_weapon_def(), 0x618) - base);
            hud = at<uint8_t*>(title_global<uint8_t*>(0x0095f8f0), 0x34);
            if (hud) {
                at<float>(hud, 0x24) = static_cast<float>(at<int32_t>(pm_ps_ref(pm), 0x3c) - base);
                at<float>(hud, 0x28) = total;
            }
            add_ps_event(pm_ps_ref(pm), 0xb7, 0);
            def = cur_weapon_def();
        }
        ps = pm_ps_ref(pm);
        if (at<int32_t>(ps, 0x3c) > 0) {
            if (at<uint32_t>(def, 0x6e4) && at<int32_t>(ps, 0x3c) < at<int32_t>(def, 0x618)) {
                at<int32_t>(ps, 0x3c) = at<int32_t>(ps, 0x3c) - title_global<int32_t>(0x0095ffa8);
                const float total = static_cast<float>(at<int32_t>(cur_weapon_def(), 0x618) - base);
                hud = at<uint8_t*>(title_global<uint8_t*>(0x0095f8f0), 0x34);
                if (hud) {
                    at<float>(hud, 0x24) = static_cast<float>(at<int32_t>(pm_ps_ref(pm), 0x3c) - base);
                    at<float>(hud, 0x28) = total;
                }
                def = cur_weapon_def();
            }
            ps = pm_ps_ref(pm);
            if (at<int32_t>(ps, 0x3c) <= 0) {
                PM_WeaponUseAmmo(0, 0, at<uint32_t>(ps, 0x80), 1);
                add_ps_event(pm_ps_ref(pm), 0xc6, 0);
                at<uint32_t>(pm_ps_ref(pm), 0x38) = 0;
                at<uint32_t>(pm_ps_ref(pm), 0x34) = 0x640;
                PM_RemoveEmptyWeapon();
                return;
            }
            if ((pm[8] & 1) && at<int32_t>(def, 0x9c) != 4) {
                const int32_t msec = title_global<int32_t>(0x0095ffa8);
                if (at<int32_t>(ps, 0x38) - msec <= 0) at<int32_t>(ps, 0x38) = msec + 1;
            }
        }
    }
    const uint32_t timers = PM_UpdateWeaponTimers();
    PM_Weapon_CheckAdsTransition();
    PM_CheckWeaponChange();
    PM_CheckWeaponReload();
    PM_CheckWeaponMelee(timers);
    if (PM_UpdateWeaponRechamber(timers)) return;
    pm = pm_global();
    ps = pm_ps_ref(pm);
    const uint32_t flags = at<uint32_t>(ps, 0x2c);
    const bool moving = pm[0x18] || pm[0x19];
    const int32_t state = at<int32_t>(ps, 0x84);
    if (((flags & 1) && moving) || ((flags & 0x10000) && moving) || state == 10 || state == 11)
        at<float>(ps, 0x4f0) = title_global<float>(0x0083eb8c);  // 255
    if (!timers && (at<uint32_t>(pm_ps_ref(pm), 0x34) || at<uint32_t>(pm_ps_ref(pm), 0x38))) return;
    if (PM_UpdateWeaponReload(timers)) return;
    if (PM_FinishWeaponMelee()) return;
    if (PM_FinishWeaponChange()) return;
    if (PM_UpdateGrenadeThrow()) return;
    if (PM_FinishWeaponRaise()) return;
    if (PM_FinishAdsRaise()) return;
    if (PM_FinishAdsDrop()) return;
    if (!at<uint32_t>(pm_ps_ref(pm_global()), 0x80)) return;
    if (PM_CheckFireButtonRelease(timers)) return;
    if (blocked) return;
    if (PM_ActivateTargetActor()) return;
    PM_WeaponFire(timers);
}

namespace {

// (y*y + x*x) + extra on the x87 (FLD/FMUL x2, FADDP, FADD), stored as float.
float x87_sum_squares_yx_plus(float x, float y, float extra) {
    float r;
    __asm__("flds %2\n\tfmuls %2\n\tflds %1\n\tfmuls %1\n\tfaddp\n\tfadds %3\n\tfstps %0"
            : "=m"(r)
            : "m"(x), "m"(y), "m"(extra)
            : "st", "st(1)");
    return r;
}

}  // namespace

void XB_CDECL PM_AirMove() {
    // Q3's PM_AirMove: friction, the move scale of a copy of the command, the pml forward / right flattened and
    // normalized (forward's length on SSE, right's on the x87; the z products with the zero are kept), the wish
    // velocity, an inlined accelerate (air accelerate 1), the velocity clipped against a ground plane (overclip
    // 1.001), then PM_StepSlideMove(1), PM_SetMovementDir and G_SeparateFromSolids.
    PM_Friction();
    uint8_t* const pm = pm_global();
    const float fmove = static_cast<float>(static_cast<int8_t>(pm[0x18]));
    const float smove = static_cast<float>(static_cast<int8_t>(pm[0x19]));
    alignas(4) uint8_t cmd[0x2c];
    std::memcpy(cmd, pm + 4, sizeof(cmd));
    const float scale = PM_CmdScale(reinterpret_cast<const int8_t*>(cmd));
    const float zero = 0.0f;
    float* const forward = &title_global<float>(0x0095ff80);
    float* const right = &title_global<float>(0x0095ff8c);
    forward[2] = zero;
    right[2] = zero;
    const float zero_sq = zero * zero;
    const float f0 = forward[0], f1 = forward[1];
    float length = pm_ci_sqrt(platform_x87_ld((f1 * f1 + f0 * f0) + zero_sq));
    const float one = title_global<float>(0x0083eb20);
    if (!x87_equal(length, title_global<float>(0x0083eafc))) {
        const float inv = one / length;
        forward[0] = forward[0] * inv;
        forward[1] = forward[1] * inv;
        forward[2] = inv * title_global<float>(0x0083eafc);
    }
    length = pm_ci_sqrt(platform_x87_ld(x87_sum_squares_yx_plus(right[0], right[1], zero_sq)));
    float r0 = right[0], r1 = right[1];
    if (!x87_equal(length, title_global<float>(0x0083eafc))) {
        const float inv = one / length;
        r0 = r0 * inv;
        r1 = r1 * inv;
        right[0] = r0;
        right[1] = r1;
        right[2] = inv * title_global<float>(0x0083eafc);
    }
    const float w0 = r0 * smove + forward[0] * fmove;
    const float w1 = r1 * smove + forward[1] * fmove;
    float wishspeed = pm_ci_sqrt(platform_x87_ld((w1 * w1 + w0 * w0) + zero_sq));
    float dx = w0, dy = w1, dz = zero;
    if (!x87_equal(wishspeed, title_global<float>(0x0083eafc))) {
        const float inv = one / wishspeed;
        dx = inv * w0;
        dy = inv * w1;
        dz = inv * zero;
    }
    wishspeed = wishspeed * scale;
    uint8_t* ps = pm_ps_ref(pm);
    const float current = (at<float>(ps, 0x18) * dz + at<float>(ps, 0x14) * dy) + at<float>(ps, 0x10) * dx;
    const float add = wishspeed - current;
    if (!(zero >= add)) {
        float control = title_global<float>(0x0083eb90);  // 100
        if (!(control > wishspeed)) control = wishspeed;
        float accel = title_global<float>(0x0095ffa4) * control;
        if (accel > add) accel = add;
        if (at<uint32_t>(ps, 0x60)) accel = one / at<float>(ps, 0x478) * accel;
        if (accel > add) accel = add;
        at<float>(ps, 0x10) = accel * dx + at<float>(ps, 0x10);
        ps = pm_ps_ref(pm);
        at<float>(ps, 0x14) = accel * dy + at<float>(ps, 0x14);
        ps = pm_ps_ref(pm);
        at<float>(ps, 0x18) = accel * dz + at<float>(ps, 0x18);
    }
    if (title_global<uint32_t>(0x0095ffb0)) {
        uint8_t* const p = pm_ps_ref(pm);
        const float* const n = &title_global<float>(0x0095ffd0);
        float backoff = (n[0] * at<float>(p, 0x10) + n[1] * at<float>(p, 0x14)) + n[2] * at<float>(p, 0x18);
        if (zero > backoff)
            backoff = backoff * title_global<float>(0x0083f1bc);  // 1.001
        else
            backoff = backoff / title_global<float>(0x0083f1bc);
        for (uint32_t i = 0; i < 3; ++i) at<float>(p, 0x10 + i * 4) = at<float>(p, 0x10 + i * 4) - n[i] * backoff;
    }
    own_function<void XB_CDECL(uint32_t)>(&PM_StepSlideMove)(1);  // PM_StepSlideMove
    PM_SetMovementDir();
    G_SeparateFromSolids();
}

PLATFORM_ASM_ENTRY(Pmove_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Pmove) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Pmove(uint8_t* pm, uint32_t arg) {
    // Q3's Pmove: frozen players (pm_flags 0x4000) lose their moves and buttons (but 0xc0 of cmd +5); the command
    // time catches up with the server time (cmd +0) in PmoveSingle steps of at most 666 ms (+0x12c with +0x128),
    // at most 1000 ms behind (50 with pm_flags 0x40000); a step leaving pm_flags 8 sets the up move to 20. The touch
    // count is cleared; pm (0x0092eff4) is set for the steps, the eye history recorded, pm cleared.
    uint8_t* ps = pm_ps_ref(pm);
    if (at<uint32_t>(ps, 0x2c) & 0x4000) {
        const uint8_t buttons = pm[9] & 0xc0;
        pm[0x18] = 0;
        pm[0x19] = 0;
        pm[0x1a] = 0;
        pm[8] = 0;
        pm[9] = buttons;
    }
    const int32_t final_time = at<int32_t>(pm, 4);
    if (final_time < at<int32_t>(ps, 0x20)) return;
    if (final_time > at<int32_t>(ps, 0x20) + 0x3e8) at<int32_t>(ps, 0x20) = final_time - 0x3e8;
    ps = pm_ps_ref(pm);
    if ((at<uint32_t>(ps, 0x2c) & 0x40000) && final_time - at<int32_t>(ps, 0x20) > 0x32)
        at<int32_t>(ps, 0x20) = final_time - 0x32;
    at<uint32_t>(pm, 0x7c) = 0;
    title_global<uint8_t*>(0x0092eff4) = pm;
    while (at<int32_t>(pm_ps_ref(pm), 0x20) != final_time) {
        const int32_t time = at<int32_t>(pm_ps_ref(pm), 0x20);
        int32_t msec = final_time - time;
        if (at<uint32_t>(pm, 0x128)) {
            if (msec > at<int32_t>(pm, 0x12c)) msec = at<int32_t>(pm, 0x12c);
        } else if (msec > 0x29a) {
            msec = 0x29a;
        }
        at<int32_t>(pm, 4) = time + msec;
        own_function<void XB_CDECL(uint8_t*, uint32_t)>(&PmoveSingle)(pm, arg);  // PmoveSingle
        if (at<uint8_t>(pm_ps_ref(pm), 0x2c) & 8) pm[0x1a] = 0x14;
    }
    Player_RecordEyeHistory(pm_ps_ref(pm));
    title_global<uint8_t*>(0x0092eff4) = nullptr;
}


namespace {

// pm_time (+0x30) counts down by msec; at the end the time-knockback flags (0x40000, 0x2000, 0x200, 0x100) go.
void pm_drop_time(uint8_t* pm) {
    uint8_t* const ps = pm_ps_ref(pm);
    const int32_t time = at<int32_t>(ps, 0x30);
    if (!time) return;
    const int32_t msec = title_global<int32_t>(0x0095ffa8);
    if (msec >= time) {
        at<uint32_t>(ps, 0x2c) &= 0xfffbdcffu;
        at<uint32_t>(pm_ps_ref(pm), 0x30) = 0;
    } else {
        at<int32_t>(ps, 0x30) = time - msec;
    }
}

void pm_leave_ground(uint8_t* pm) {
    at<uint32_t>(pm_ps_ref(pm), 0x2c) &= ~0x10u;
    at<uint32_t>(pm_ps_ref(pm), 0x60) = 0;
    title_global<uint32_t>(0x0095ffb0) = 0;
    title_global<uint32_t>(0x0095ffac) = 0;
}

}  // namespace

void XB_CDECL PmoveSingle(uint8_t* pm, uint32_t arg) {
    // Q3's PmoveSingle: the lean move from the right stick (0x00929c2c +0x20: right move beyond +-0.5 becomes lean
    // button 0x20 / 0x10), frozen or menu (0x0093fcd8 +0x20 = 1) commands cleared, prone weapon reset on a stronger
    // move, the ADS / attack flags (+0xc0 0x200, pm_flags 0x400 / 0x800), pml cleared and filled (msec 1-200,
    // previous origin / velocity, frametime), the view angles and axes, then by pm_type: 1 / 5 dead-ish (duck,
    // timers, stamina, weapon, footsteps), 2 noclip, 3 flat noclip, else frozen (+0xc0 0x106000) or the full move
    // (ground trace, prone tilt, ladder check, ladder / walk / air move, footsteps, weapon, contents event, flag
    // events) with the velocity recomputed from the move when it is more than twice the moved distance per second.
    const bool no_weapon = static_cast<uint8_t>(arg) != 0;
    const uint32_t count = title_global<uint32_t>(0x0095f4d0);
    const uint8_t* const lean_cvar = title_global<uint8_t*>(0x00929c2c);
    pm[0x120] = 0;
    pm[0x121] = 0;
    title_global<uint8_t*>(0x0092eff4) = pm;
    title_global<uint32_t>(0x0095f4d0) = count + 1;
    if (at<uint32_t>(lean_cvar, 0x20)) {
        const float right = static_cast<float>(static_cast<int8_t>(pm[0x19]));
        if (right > title_global<float>(0x0083eb24))
            pm[9] |= 0x20;
        else if (title_global<float>(0x0083eb28) > right)
            pm[9] |= 0x10;
        pm[0x19] = 0;
    }
    uint8_t* ps = pm_ps_ref(pm);
    if ((at<uint32_t>(ps, 0x2c) & 0x4000) || at<int32_t>(title_global<uint8_t*>(0x0093fcd8), 0x20) == 1) {
        const uint8_t buttons = pm[9] & 0xc2;
        pm[8] = 0;
        pm[9] = buttons;
        pm[0x18] = 0;
        pm[0x19] = 0;
        pm[0x1a] = 0;
    }
    at<uint32_t>(ps, 0x2c) &= ~0x8000u;
    ps = pm_ps_ref(pm);
    if (at<int32_t>(ps, 0x24) >= 4) at<uint32_t>(pm, 0x5c) &= 0xfdff3fffu;
    uint8_t* const* const defs = title_global<uint8_t* const*>(0x0095f4d8);
    title_global<uint8_t*>(0x00960020) = defs[at<int32_t>(ps, 0x80)];
    ps = pm_ps_ref(pm);
    const uint32_t flags = at<uint32_t>(ps, 0x2c);
    bool clear_400 = true;
    if ((flags & 1) && at<int32_t>(cur_weapon_def(), 0x94) != 1) {
        const auto stronger = [](uint8_t now, uint8_t old) {
            const int32_t a = static_cast<int8_t>(now), b = static_cast<int8_t>(old);
            return now != old && (a < 0 ? -a : a) > (b < 0 ? -b : b);
        };
        if (stronger(pm[0x18], pm[0x44]) || stronger(pm[0x19], pm[0x45])) {
            clear_400 = false;
            if (PM_ResetWeaponToIdle()) {
                at<uint32_t>(pm_ps_ref(pm), 0x2c) &= ~0x400u;
                at<uint32_t>(pm_ps_ref(pm), 0x2c) &= ~0x20u;
            }
        } else if (flags & 0x20) {
            clear_400 = false;
        } else {
            const int32_t state = at<int32_t>(ps, 0x84);
            clear_400 = state == 0 || state == 1 || state == 2 || state == 5;
        }
    }
    if (clear_400) at<uint32_t>(ps, 0x2c) &= ~0x400u;
    if (at<uint32_t>(pm_ps_ref(pm), 0x2c) & 0x10000) PM_ResetWeaponToAnimA();
    ps = pm_ps_ref(pm);
    int32_t mount;
    if (at<uint32_t>(ps, 0xa8) == at<uint32_t>(ps, 0x424))
        mount = 2;
    else
        mount = at<uint32_t>(ps, 0xa8) == at<uint32_t>(ps, 0x420) ? 1 : 0;
    if ((at<uint8_t>(ps, 0x2c) & 0x20) && mount == 1) {
        pm[0x18] = 0;
        pm[0x19] = 0;
    }
    ps = pm_ps_ref(pm);
    if ((at<uint8_t>(ps, 0x2c) & 0x20) && at<int32_t>(defs[at<int32_t>(ps, 0x80)], 0x98) == 3) {
        pm[0x18] = 0;
        pm[0x19] = 0;
    }
    at<uint32_t>(pm_ps_ref(pm), 0xc0) &= ~0x200u;
    ps = pm_ps_ref(pm);
    const uint32_t f2 = at<uint32_t>(ps, 0x2c);
    if (!(f2 & 0x800)) {
        const int32_t state = at<int32_t>(ps, 0x84);
        if (state == 0 || state == 3) {
            const uint32_t clip = at<uint32_t>(ps, 0x280 + at<uint32_t>(defs[at<int32_t>(ps, 0x80)], 0x5a0) * 4);
            if ((clip || (at<uint32_t>(ps, 0xc0) & 0x6000)) && !(f2 & 0x10000) && (pm[8] & 1))
                at<uint32_t>(ps, 0xc0) |= 0x200;
        }
    }
    ps = pm_ps_ref(pm);
    if (at<int32_t>(ps, 0x24) < 4 && !(pm[8] & 1)) at<uint32_t>(ps, 0x2c) &= ~0x800u;
    for (uint32_t i = 0; i < 0x2c; ++i) title_global<uint32_t>(0x0095ff80 + i * 4) = 0;
    const int32_t msec = at<int32_t>(pm, 4) - at<int32_t>(pm_ps_ref(pm), 0x20);
    title_global<int32_t>(0x0095ffa8) = msec;
    if (msec < 1)
        title_global<int32_t>(0x0095ffa8) = 1;
    else if (msec > 0xc8)
        title_global<int32_t>(0x0095ffa8) = 0xc8;
    at<uint32_t>(pm_ps_ref(pm), 0x20) = at<uint32_t>(pm, 4);
    for (uint32_t i = 0; i < 3; ++i) title_global<uint32_t>(0x00960004 + i * 4) = at<uint32_t>(pm_ps_ref(pm), i * 4);
    for (uint32_t i = 0; i < 3; ++i)
        title_global<uint32_t>(0x00960010 + i * 4) = at<uint32_t>(pm_ps_ref(pm), 0x10 + i * 4);
    title_global<float>(0x0095ffa4) =
        static_cast<float>(title_global<int32_t>(0x0095ffa8)) * title_global<float>(0x0083eb4c);  // 0.001
    ps = pm_ps_ref(pm);
    title_global<uint8_t*>(0x00960020) = defs[at<int32_t>(ps, 0x80)];
    own_function<void XB_REGS1 XB_CDECL(uint32_t)>(&PM_UpdateAimSpread)(at<uint32_t>(ps, 0x80));
    using ViewAngles = void XB_REGS1 XB_CDECL(uint8_t*, uint8_t*, uint8_t*, uint32_t);
    own_function<ViewAngles>(&PM_UpdateViewAngles)(pm_ps_ref(pm), pm + 4, pm + 0x30, at<uint32_t>(pm, 0x138));  // PM_UpdateViewAngles
    uint8_t* const p = pm_global();
    uint32_t axis_args[2] = {address_of(pm_ps_ref(p) + 0x9c), 0x0095ff80};
    AnglesToAxis(axis_args, &title_global<float>(0x0095ff8c), &title_global<float>(0x0095ff98));
    if (static_cast<int8_t>(p[0x1a]) < 10) at<uint32_t>(pm_ps_ref(p), 0x2c) &= ~8u;
    const int8_t forward = static_cast<int8_t>(p[0x18]);
    if (forward < 0)
        at<uint32_t>(pm_ps_ref(p), 0x2c) |= 0x40;
    else if (forward > 0 || p[0x19])
        at<uint32_t>(pm_ps_ref(p), 0x2c) &= ~0x40u;
    ps = pm_ps_ref(p);
    if (at<int32_t>(ps, 0x24) >= 4) {
        p[0x18] = 0;
        p[0x19] = 0;
        p[0x1a] = 0;
    }
    if (mount == 1 && (at<uint32_t>(ps, 0x2c) & 0x400)) {
        p[0x18] = 0;
        p[0x19] = 0;
    }
    using Void = void XB_CDECL();
    switch (at<int32_t>(ps, 0x24)) {
    case 1:
    case 5:
        pm_leave_ground(p);
        PM_UpdateAdsState();
        PM_UpdateButtonFlag80();
        PM_UpdateSprintFlag();
        own_function<Void>(&PM_CheckDuck)();  // PM_CheckDuck
        PM_DropTimers();
        PM_UpdateStaminaMeter();
        if (!no_weapon) PM_Weapon();
        PM_Footsteps();
        return;
    case 2:
    case 3:
        at<uint32_t>(ps, 0x2c) &= ~0x10u;
        PM_UpdateAdsState();
        PM_UpdateButtonFlag80();
        PM_UpdateSprintFlag();
        if (at<int32_t>(ps, 0x24) == 2)
            PM_NoclipMove();
        else
            PM_NoclipFlatMove();
        if (!no_weapon) PM_Weapon();
        PM_DropTimers();
        PM_UpdateStaminaMeter();
        return;
    default:
        break;
    }
    if (at<uint32_t>(ps, 0xc0) & 0x106000) {
        pm_leave_ground(p);
        PM_UpdateAdsState();
        PM_UpdateButtonFlag80();
        PM_UpdateSprintFlag();
        own_function<Void>(&PM_CheckDuck)();  // PM_CheckDuck
        pm_drop_time(pm_global());
        PM_UpdateStaminaMeter();
        PM_UpdateAimDownSightFrac();
        PM_Footsteps();
        return;
    }
    title_global<uint32_t>(0x0096001c) = pm[0x121];
    own_function<Void>(&PM_CheckDuck)();  // PM_CheckDuck
    PM_GroundTrace();
    PM_UpdateAdsState();
    PM_UpdateButtonFlag80();
    PM_UpdateSprintFlag();
    PM_UpdateProneTilt();
    if (at<int32_t>(pm_ps_ref(pm_global()), 0x24) == 4) PM_ReduceGroundSpeed();  // ESI = 4 here
    PM_DetectLadder();
    uint8_t* const q = pm_global();
    pm_drop_time(q);
    PM_UpdateStaminaMeter();
    if (at<uint8_t>(pm_ps_ref(q), 0x2c) & 0x10)
        own_function<Void>(&PM_LadderMove)();  // PM_LadderMove
    else if (title_global<uint32_t>(0x0095ffac))
        own_function<Void>(&PM_WalkMove)();  // PM_WalkMove
    else if (title_global<uint8_t>(0x008fef48))
        PM_AirMove();
    PM_GroundTrace();
    PM_Footsteps();
    if (!no_weapon) {
        PM_Weapon();
        PM_PeriodicContentsEvent();
    }
    PM_AddFlagChangeEvents();
    uint8_t* const r = pm_ps_ref(pm_global());
    const float vx = at<float>(r, 0x10), vy = at<float>(r, 0x14), vz = at<float>(r, 0x18);
    const float dy = at<float>(r, 4) - title_global<float>(0x00960008);
    const float dz = at<float>(r, 8) - title_global<float>(0x0096000c);
    const float dx = at<float>(r, 0) - title_global<float>(0x00960004);
    const float ft = title_global<float>(0x0095ffa4);
    const float moved = ((dz * dz + dy * dy) + dx * dx) / (ft * ft);
    const float speed = ((vx * vx + vy * vy) + vz * vz) * title_global<float>(0x0083ec24);  // 0.25
    if (!(speed > moved)) return;
    const float one = title_global<float>(0x0083eb20);
    at<float>(r, 0x10) = one / title_global<float>(0x0095ffa4) * dx;
    at<float>(pm_ps_ref(pm_global()), 0x14) = one / title_global<float>(0x0095ffa4) * dy;
    at<float>(pm_ps_ref(pm_global()), 0x18) = one / title_global<float>(0x0095ffa4) * dz;
}

namespace {

// The title CRT's __CIfmod (0x0033acda): x in ST1, y in ST0, fmod(x, y) left in ST0.
long double pm_ci_fmod(long double x, long double y) {
    long double result;
    __asm__ volatile("call *%3"
                     : "=t"(result)
                     : "0"(y), "u"(x), "r"(uintptr_t{0x0033acda})
                     : "st(1)", "eax", "ecx", "edx", "memory");
    return result;
}

// FILD + FSTP DWORD: an integer to float on the x87.
float x87_int_to_float(int32_t i) { return static_cast<float>(platform_x87_ld(i)); }

}  // namespace

PLATFORM_ASM_ENTRY(PM_WalkCmdScale_entry,
                   "pushl 4(%esp)\n" PLATFORM_ASM_CALL(PM_WalkCmdScale) "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $4, %esp\n"
                   "ret")

float XB_CDECL PM_WalkCmdScale(const int8_t* cmd) {
    // PM_CmdScale for walking: the larger of |forward| (backwards x ps +0x448) and |right| x ps +0x444 (x87),
    // 0 when that is 0; speed (+0x50) x it / (sqrt(forward^2 + right^2) x 127), x the stance scale (+0x430 when
    // flag 0x80, leaning (+0x4c) or an ADS weapon (state 0xd, type 3); else +0x438 with flag 0x10000, else +0x434).
    // pm_type 2 x3, 3 x6; else blended with the prone (+0x43c) / crouch (+0x440) scales by the view-height lerp
    // (PM_ViewHeightLerpFrac both ways) or picked by the stance, x (1 - pm +0x121 / 6); x0.4 with cmd +5 bit 4.
    using ext = long double;
    uint8_t* const ps = pm_ps();
    ext f = platform_x87_ld(static_cast<int32_t>(cmd[0x14]));
    if (cmd[0x14] < 0) f = f * platform_x87_ld(at<float>(ps, 0x448));
    const float forward = static_cast<float>(__builtin_fabsl(f));
    const ext right = __builtin_fabsl(platform_x87_ld(static_cast<int32_t>(cmd[0x15])) *
                                      platform_x87_ld(at<float>(ps, 0x444)));
    float max = platform_x87_ld(forward) > right ? forward : static_cast<float>(right);
    if (max == 0.0f) return 0.0f;
    const int32_t fwd = cmd[0x14], rt = cmd[0x15];
    const ext speed = platform_x87_ld(at<int32_t>(ps, 0x50)) * platform_x87_ld(max);
    const ext sum = platform_x87_ld(rt * rt) + platform_x87_ld(fwd) * platform_x87_ld(fwd);
    const ext total = platform_x87_sqrt_ext(sum) * platform_x87_ld(title_global<float>(0x0083ed88));  // 127
    const float scale = static_cast<float>(speed / total);
    const uint32_t flags = at<uint32_t>(ps, 0x2c);
    float stance;
    const bool ads = at<int32_t>(ps, 0x84) == 0xd &&
                     at<int32_t>(title_global<uint8_t**>(0x0095f4d8)[at<int32_t>(ps, 0x80)], 0x98) == 3;
    if ((flags & 0x80) || !(at<float>(ps, 0x4c) == 0.0f) || ads)
        stance = at<float>(ps, 0x430);
    else
        stance = at<float>(ps, (flags & 0x10000) ? 0x438 : 0x434);
    float result = stance * scale;
    const int32_t type = at<int32_t>(ps, 0x24);
    if (type == 2) {
        result = result * title_global<float>(0x0083ec9c);  // 3
    } else if (type == 3) {
        result = result * title_global<float>(0x0083ecd0);  // 6
    } else {
        const uint32_t current = PM_ViewHeightStance(ps);
        const uint32_t prone = at<uint32_t>(ps, 0x420), crouch = at<uint32_t>(ps, 0x424);
        const float one = title_global<float>(0x0083eb20);
        float frac;
        PM_ViewHeightLerpFrac(crouch, prone, &frac);
        if (!(frac == 0.0f)) {
            result = ((one - frac) * at<float>(ps, 0x440) + at<float>(ps, 0x43c) * frac) * result;
        } else {
            PM_ViewHeightLerpFrac(prone, crouch, &frac);
            if (!(frac == 0.0f))
                result = ((one - frac) * at<float>(ps, 0x43c) + at<float>(ps, 0x440) * frac) * result;
            else if (current == 1)
                result = at<float>(ps, 0x43c) * result;
            else if (current == 2)
                result = at<float>(ps, 0x440) * result;
        }
        const uint8_t slow = pm_global()[0x121];
        if (slow) {
            const float k = static_cast<float>(static_cast<int32_t>(slow)) * title_global<float>(0x0083ed74) *
                            title_global<float>(0x0083eb24);  // 1/3, 0.5
            result = (one - k) * result;
        }
    }
    if (cmd[5] & 4) result = result * title_global<float>(0x0083ec44);  // 0.4
    return result;
}

void XB_CDECL PM_UpdateAimSpread() {
    // The aim spread (ps +0x4f0, 0..255) from the weapon def (pml +0xa0 = 0x00960020): with a decay rate (+0x65c)
    // it falls by frametime x rate (half in the air; x +0x670 / +0x66c with ps +0xc0 bits 0x40 / 0x20) and rises
    // by the view-angle change (pm +0x38/+0x3c old vs +0xc/+0x10 new, short units, wrapped to +-180) x +0x664 / 100
    // per second and by the larger move axis / 128 x +0x668 (x87), plus 2.56 in the air, x frametime; x255.
    // Fully aimed down the sights (ps +0x88 = 1) it only decays; without a rate it falls to 0.
    using ext = long double;
    const uint8_t* const def = title_global<uint8_t*>(0x00960020);
    const uint8_t* const pm = pm_global();
    const float zero = 0.0f;
    const float frametime = title_global<float>(0x0095ffa4);
    float rise, decay;
    float rate = at<float>(def, 0x65c);
    if (rate == zero) {
        decay = title_global<float>(0x0083eb20);  // 1
        rise = zero;
    } else {
        const uint8_t* const ps = pm_ps_ref(const_cast<uint8_t*>(pm));
        const uint32_t ground = at<uint32_t>(ps, 0x60);
        const bool air = !ground && at<int32_t>(ps, 0x24) != 1;
        if (air) {
            rate = rate * title_global<float>(0x0083eb24);  // 0.5
        } else {
            const uint32_t flags = at<uint32_t>(ps, 0xc0);
            if (flags & 0x40)
                rate = at<float>(def, 0x670) * rate;
            else if (flags & 0x20)
                rate = at<float>(def, 0x66c) * rate;
        }
        decay = frametime * rate;
        if (at<float>(ps, 0x88) == title_global<float>(0x0083eb20)) {
            rise = zero;
        } else {
            float sum = zero;
            if (!(at<float>(def, 0x664) == zero)) {
                const float unit = title_global<float>(0x0083ebd4);  // 360 / 65536
                for (uint32_t i = 0; i < 2; ++i) {
                    const float old_angle = static_cast<float>(
                        platform_x87_ld(x87_int_to_float(at<int32_t>(pm, 0x38 + i * 4))) * platform_x87_ld(unit));
                    const float new_angle = static_cast<float>(
                        platform_x87_ld(x87_int_to_float(at<int32_t>(pm, 0xc + i * 4))) * platform_x87_ld(unit));
                    float d = static_cast<float>(pm_ci_fmod(platform_x87_ld(new_angle) - platform_x87_ld(old_angle),
                                                            title_global<double>(0x0083ed40)));  // 360
                    if (platform_x87_ld(d) > platform_x87_ld(title_global<float>(0x0083eb60)))  // 180
                        d = d - title_global<float>(0x0083ebcc);                               // 360
                    if (title_global<float>(0x0083ebd0) > d) d = d + title_global<float>(0x0083ebcc);  // -180
                    const float a = static_cast<float>(__builtin_fabsl(platform_x87_ld(d)));
                    sum = static_cast<float>(platform_x87_ld(a) * platform_x87_ld(at<float>(def, 0x664)) *
                                                 platform_x87_ld(title_global<float>(0x0083eb94)) /  // 0.01
                                                 platform_x87_ld(frametime) +
                                             platform_x87_ld(sum));
                }
            }
            if (!(at<float>(def, 0x668) == zero)) {
                const int32_t forward = static_cast<int8_t>(pm[0x18]), right = static_cast<int8_t>(pm[0x19]);
                const auto magnitude = [](int32_t v) { return v < 0 ? -v : v; };
                int32_t axis = 0;
                bool moving = true;
                if (magnitude(right) > magnitude(forward))
                    axis = right;
                else if (forward)
                    axis = forward;
                else
                    moving = false;
                if (moving) {
                    const float f = static_cast<float>(platform_x87_ld(x87_int_to_float(axis)) /
                                                       platform_x87_ld(title_global<float>(0x0083ebe8)));  // 128
                    const float a = static_cast<float>(__builtin_fabsl(platform_x87_ld(f)));
                    sum = static_cast<float>(platform_x87_ld(a) * platform_x87_ld(at<float>(def, 0x668)) +
                                             platform_x87_ld(sum));
                }
            }
            if (air) {
                const float k = title_global<float>(0x0083ee78);  // 1.28
                rise = frametime * ((sum + k) + k);
            } else {
                rise = frametime * sum;
            }
        }
    }
    uint8_t* const ps = pm_ps();
    const float top = title_global<float>(0x0083eb8c);  // 255
    at<float>(ps, 0x4f0) = (rise - decay) * top + at<float>(ps, 0x4f0);
    if (zero > at<float>(pm_ps(), 0x4f0))
        at<float>(pm_ps(), 0x4f0) = zero;
    else if (at<float>(pm_ps(), 0x4f0) > top)
        at<float>(pm_ps(), 0x4f0) = top;
}

void XB_CDECL PM_ViewHeightAdjust() {
    // The view height (+0xac) toward its target (+0xa8): straight to it when either is 0; other targets move at
    // 180 units/s. Stance heights (prone +0x420, crouch +0x424, stand +0x428) follow a lerp started at +0xb0
    // (target +0xb4, downward flag +0xb8) along the stance's curve (0x008d2270 prone, crouch 0x008d2190 down /
    // 0x008d2318 up, 0x008d2200 stand) in percent of PM_ViewHeightLerpTime; the curve's height (+0xbc) moving
    // more than 0x0083edc8 steps the player along the forward axis (PM_StepSlideMove, velocity restored). A
    // target changed against the lerp's direction turns it around at the same point; a new target starts one.
    uint8_t* const pm = pm_global();
    uint8_t* ps = pm_ps_ref(pm);
    const int32_t target = at<int32_t>(ps, 0xa8);
    const float zero = 0.0f;
    if (!target || at<float>(ps, 0xac) == zero) {
        at<float>(ps, 0xac) = static_cast<float>(target);
        return;
    }
    if (at<float>(ps, 0xac) == static_cast<float>(target) && !at<uint32_t>(ps, 0xb0)) return;
    int32_t percent = 0;
    const auto curve_for = [](const uint8_t* p, uint32_t height) -> uint32_t {
        if (height == at<uint32_t>(p, 0x420)) return 0x008d2270;
        if (height == at<uint32_t>(p, 0x424)) return at<uint32_t>(p, 0xb8) ? 0x008d2190 : 0x008d2318;
        return 0x008d2200;
    };
    const uint32_t utarget = static_cast<uint32_t>(target);
    if (utarget != at<uint32_t>(ps, 0x420) && utarget != at<uint32_t>(ps, 0x424) && utarget != at<uint32_t>(ps, 0x428)) {
        at<uint32_t>(ps, 0xb0) = 0;
        ps = pm_ps_ref(pm);
        const float step = title_global<float>(0x0095ffa4) * title_global<float>(0x0083eb60);  // 180
        if (static_cast<float>(at<int32_t>(ps, 0xa8)) > at<float>(ps, 0xac)) {
            at<float>(ps, 0xac) = step + at<float>(ps, 0xac);
            ps = pm_ps_ref(pm);
            const float t = static_cast<float>(at<int32_t>(ps, 0xa8));
            if (at<float>(ps, 0xac) >= t) at<float>(ps, 0xac) = t;
        } else {
            at<float>(ps, 0xac) = at<float>(ps, 0xac) - step;
            ps = pm_ps_ref(pm);
            const float t = static_cast<float>(at<int32_t>(ps, 0xa8));
            if (t >= at<float>(ps, 0xac)) at<float>(ps, 0xac) = t;
        }
        return;
    }
    const int32_t start = at<int32_t>(ps, 0xb0);
    if (start) {
        const uint32_t down = at<uint32_t>(ps, 0xb8);
        const uint32_t lerp_target = at<uint32_t>(ps, 0xb4);
        const int32_t time = static_cast<int32_t>(PM_ViewHeightLerpTime(lerp_target, ps, down));
        percent = (at<int32_t>(pm, 4) - start) * 100 / time;
        bool finish = false;
        if (percent < 0)
            percent = 0;
        else if (percent >= 100) {
            percent = 100;
            finish = true;
        }
        if (finish) {
            at<float>(ps, 0xac) = static_cast<float>(static_cast<int32_t>(lerp_target));
            at<uint32_t>(pm_ps_ref(pm), 0xb0) = 0;
            at<float>(pm_ps_ref(pm), 0xbc) = zero;
        } else {
            float height;
            const float value = PM_EvalLerpCurve(percent, pointer<const uint8_t>(curve_for(ps, lerp_target)), &height);
            at<float>(pm_ps_ref(pm), 0xac) = value;
            uint8_t* const p = pm_ps_ref(pm);
            using ext = long double;
            if (__builtin_fabsl(platform_x87_ld(at<float>(p, 0xbc)) - platform_x87_ld(height)) >
                platform_x87_ld(title_global<float>(0x0083edc8))) {
                float saved[3];
                for (uint32_t i = 0; i < 3; ++i) saved[i] = at<float>(p, 0x10 + i * 4);
                float speed = height - at<float>(p, 0xbc);
                if (!at<uint32_t>(p, 0x60)) speed = speed * title_global<float>(0x0083eb24);  // 0.5
                speed = speed / title_global<float>(0x0095ffa4);
                float dir[3] = {title_global<float>(0x0095ff80), title_global<float>(0x0095ff84), zero};
                VectorNormalize(dir);
                at<float>(p, 0x10) = dir[0] * speed;
                at<float>(pm_ps_ref(pm), 0x14) = dir[1] * speed;
                at<float>(pm_ps_ref(pm), 0x18) = dir[2] * speed;
                own_function<void XB_CDECL(uint32_t)>(&PM_StepSlideMove)(1);  // PM_StepSlideMove
                uint8_t* const q = pm_ps_ref(pm_global());
                for (uint32_t i = 0; i < 3; ++i) at<float>(q, 0x10 + i * 4) = saved[i];
                at<float>(q, 0xbc) = height;
            }
        }
    }
    uint8_t* const pm2 = pm_global();
    ps = pm_ps_ref(pm2);
    if (!at<uint32_t>(ps, 0xb0)) {
        const int32_t height = at<int32_t>(ps, 0xa8);
        if (at<float>(ps, 0xac) == static_cast<float>(height)) return;
        at<int32_t>(ps, 0xb0) = at<int32_t>(pm2, 4);
        ps = pm_ps_ref(pm2);
        const uint32_t h = at<uint32_t>(ps, 0xa8);
        if (h == at<uint32_t>(ps, 0x420)) {
            at<uint32_t>(ps, 0xb8) = 1;
            ps = pm_ps_ref(pm2);
            const uint32_t crouch = at<uint32_t>(ps, 0x424);
            at<uint32_t>(ps, 0xb4) =
                at<float>(ps, 0xac) > static_cast<float>(static_cast<int32_t>(crouch)) ? crouch : at<uint32_t>(ps, 0x420);
        } else if (h == at<uint32_t>(ps, 0x424)) {
            at<uint32_t>(ps, 0xb8) = at<float>(ps, 0xac) > static_cast<float>(static_cast<int32_t>(h)) ? 1 : 0;
            ps = pm_ps_ref(pm2);
            at<uint32_t>(ps, 0xb4) = at<uint32_t>(ps, 0x424);
        } else if (h == at<uint32_t>(ps, 0x428)) {
            at<uint32_t>(ps, 0xb8) = 0;
            ps = pm_ps_ref(pm2);
            const uint32_t crouch = at<uint32_t>(ps, 0x424);
            at<uint32_t>(ps, 0xb4) =
                static_cast<float>(static_cast<int32_t>(crouch)) > at<float>(ps, 0xac) ? crouch : at<uint32_t>(ps, 0x428);
        }
        return;
    }
    const int32_t height = at<int32_t>(ps, 0xa8), lerp_target = at<int32_t>(ps, 0xb4);
    if (height == lerp_target) return;
    const bool flip = height < lerp_target ? !at<uint32_t>(ps, 0xb8) : at<uint32_t>(ps, 0xb8) != 0;
    if (!flip) return;
    at<uint32_t>(ps, 0xb8) ^= 1;
    ps = pm_ps_ref(pm2);
    const int32_t rest = 100 - percent;
    const uint32_t t = at<uint32_t>(ps, 0xb4);
    if (at<uint32_t>(ps, 0xb8)) {
        if (t == at<uint32_t>(ps, 0x428))
            at<uint32_t>(ps, 0xb4) = at<uint32_t>(ps, 0x424);
        else if (t == at<uint32_t>(ps, 0x424))
            at<uint32_t>(ps, 0xb4) = at<uint32_t>(ps, 0x420);
    } else {
        if (t == at<uint32_t>(ps, 0x420))
            at<uint32_t>(ps, 0xb4) = at<uint32_t>(ps, 0x424);
        else if (t == at<uint32_t>(ps, 0x424))
            at<uint32_t>(ps, 0xb4) = at<uint32_t>(ps, 0x428);
    }
    if (rest == 100) {
        at<float>(ps, 0xac) = static_cast<float>(at<int32_t>(ps, 0xb4));
        at<uint32_t>(pm_ps_ref(pm2), 0xb0) = 0;
        at<float>(pm_ps_ref(pm2), 0xbc) = zero;
        return;
    }
    ps = pm_ps_ref(pm2);
    const int32_t time = static_cast<int32_t>(PM_ViewHeightLerpTime(at<uint32_t>(ps, 0xb4), ps, at<uint32_t>(ps, 0xb8)));
    const float elapsed = static_cast<float>(time) * static_cast<float>(rest) * title_global<float>(0x0083eb94);  // 0.01
    at<int32_t>(ps, 0xb0) = at<int32_t>(pm2, 4) - static_cast<int32_t>(elapsed);
    ps = pm_ps_ref(pm2);
    float out;
    PM_EvalLerpCurve(rest, pointer<const uint8_t>(curve_for(ps, at<uint32_t>(ps, 0xb4))), &out);
    at<float>(pm_ps_ref(pm2), 0xbc) = out;
}

void XB_CDECL PM_LadderMove() {
    // Q3's PM_LadderMove (ladder normal ps +0x64): a jump leaves the ladder (PM_AirMove, +0x70 = command time).
    // Climb scale = (forward z + 0.25) x 0x0083eaf8 clamped to 0x0083eb34..1; forward and right flattened and
    // normalized, right projected off the normal; wish = forward move x scale x climb / 2 up plus right move x
    // scale x 0x0083eca0 along right; PM_Accelerate at 9. Without forward move gravity brings z velocity to 0;
    // without right move the sideways velocity bleeds off (frametime x 0x0083ebb8, at least 1). Off the ground
    // the velocity loses its normal part and gets 0x0083f240 (climbing) / 0x0083f23c of the normal; then
    // PM_StepSlideMove(0) and the ladder yaw offset (+0x78, +-75) from the normal's yaw vs +0xa0.
    using ext = long double;
    if (PM_CheckJump()) {
        PM_AirMove();
        uint8_t* const pm = pm_global();
        at<uint32_t>(pm_ps_ref(pm), 0x70) = at<uint32_t>(pm, 4);
        return;
    }
    float* const forward = &title_global<float>(0x0095ff80);
    float* const right = &title_global<float>(0x0095ff8c);
    const float zero = 0.0f;
    const float one = title_global<float>(0x0083eb20);
    float climb = (forward[2] + title_global<float>(0x0083ec24)) * title_global<float>(0x0083eaf8);  // 0.25
    if (climb > one)
        climb = one;
    else if (title_global<float>(0x0083eb34) > climb)
        climb = title_global<float>(0x0083eb34);
    forward[2] = zero;
    const float zero_sq = zero * zero;
    {
        const float f1 = forward[1], f0 = forward[0];
        const float sum = (f1 * f1 + f0 * f0) + zero_sq;
        const float length = pm_ci_sqrt(platform_x87_ld(sum));
        if (!x87_equal(length, title_global<float>(0x0083eafc))) {
            const float inv = one / length;
            forward[0] = forward[0] * inv;
            forward[1] = forward[1] * inv;
            forward[2] = inv * title_global<float>(0x0083eafc);
        }
    }
    float r0, r1, r2;
    {
        right[2] = zero;
        const ext sum = platform_x87_ld(right[1]) * platform_x87_ld(right[1]) +
                        platform_x87_ld(right[0]) * platform_x87_ld(right[0]) + platform_x87_ld(zero_sq);
        const float length = pm_ci_sqrt(platform_x87_ld(static_cast<float>(sum)));
        if (!x87_equal(length, title_global<float>(0x0083eafc))) {
            const float inv = one / length;
            r0 = right[0] * inv;
            r1 = right[1] * inv;
            r2 = inv * title_global<float>(0x0083eafc);
        } else {
            r0 = r1 = r2 = zero;
        }
    }
    uint8_t* const pm = pm_global();
    {
        const uint8_t* const ps = pm_ps_ref(pm);
        const float n0 = at<float>(ps, 0x64), n1 = at<float>(ps, 0x68), n2 = at<float>(ps, 0x6c);
        const float inv = one / ((n0 * n0 + n1 * n1) + n2 * n2);
        const float d = ((n0 * r0 + n2 * r2) + n1 * r1) * inv;
        r0 = r0 - n0 * inv * d;
        r1 = r1 - n1 * inv * d;
        r2 = r2 - n2 * inv * d;
        right[0] = r0;
        right[1] = r1;
        right[2] = r2;
    }
    const float scale = PM_CmdScale(reinterpret_cast<const int8_t*>(pm + 4));
    float wish[3] = {zero, zero, zero};
    const int8_t forward_move = static_cast<int8_t>(pm[0x18]);
    if (forward_move)
        wish[2] = static_cast<float>(static_cast<int32_t>(forward_move)) * scale * climb *
                  title_global<float>(0x0083eb24);  // 0.5
    const int8_t right_move = static_cast<int8_t>(pm[0x19]);
    if (right_move) {
        const float m = static_cast<float>(static_cast<int32_t>(right_move)) * scale * title_global<float>(0x0083eca0);
        wish[0] = m * r0 + zero;
        wish[1] = m * r1 + zero;
        wish[2] = m * r2 + wish[2];
    }
    const ext wish_sq = platform_x87_ld(wish[0]) * platform_x87_ld(wish[0]) +
                        platform_x87_ld(wish[1]) * platform_x87_ld(wish[1]) +
                        platform_x87_ld(wish[2]) * platform_x87_ld(wish[2]);
    const float wishspeed = pm_ci_sqrt(platform_x87_ld(static_cast<float>(wish_sq)));
    float wishdir[3];
    if (!x87_equal(wishspeed, title_global<float>(0x0083eafc))) {
        const float inv = one / wishspeed;
        for (uint32_t i = 0; i < 3; ++i) wishdir[i] = inv * wish[i];
    } else {
        wishdir[0] = wishdir[1] = wishdir[2] = zero;
    }
    PM_Accelerate(0, 0, wishdir, wishspeed, 9.0f);
    if (!pm[0x18]) {
        uint8_t* const ps = pm_ps_ref(pm);
        const bool rising = at<float>(ps, 0x18) > zero;
        const float g = static_cast<float>(at<int32_t>(ps, 0x48)) * title_global<float>(0x0095ffa4);
        if (rising) {
            at<float>(ps, 0x18) = at<float>(ps, 0x18) - g;
            if (zero > at<float>(pm_ps_ref(pm), 0x18)) at<float>(pm_ps_ref(pm), 0x18) = zero;
        } else {
            at<float>(ps, 0x18) = g + at<float>(ps, 0x18);
            if (at<float>(pm_ps_ref(pm), 0x18) > zero) at<float>(pm_ps_ref(pm), 0x18) = zero;
        }
    }
    if (!pm[0x19]) {
        const float s0 = right[0], s1 = right[1];
        const float length = pm_ci_sqrt(platform_x87_ld(s1 * s1 + s0 * s0));
        float d0 = s0, d1 = s1;
        if (!x87_equal(length, title_global<float>(0x0083eafc))) {
            const float inv = one / length;
            d0 = inv * s0;
            d1 = inv * s1;
        }
        uint8_t* const ps = pm_ps_ref(pm);
        const float side = at<float>(ps, 0x14) * d1 + at<float>(ps, 0x10) * d0;
        if (!(side == title_global<float>(0x0083eafc))) {
            const float back = zero - side;
            at<float>(ps, 0x10) = back * d0 + at<float>(ps, 0x10);
            at<float>(pm_ps_ref(pm), 0x14) = back * d1 + at<float>(pm_ps_ref(pm), 0x14);
            const float f = static_cast<float>(platform_x87_ld(title_global<float>(0x0095ffa4)) * platform_x87_ld(side) *
                                               platform_x87_ld(title_global<float>(0x0083ebb8)));
            const float af = static_cast<float>(__builtin_fabsl(platform_x87_ld(f)));
            const float as = static_cast<float>(__builtin_fabsl(platform_x87_ld(side)));
            if (platform_x87_ld(as) > platform_x87_ld(af)) {
                float keep = f;
                if (one > af) {
                    uint32_t bits;
                    std::memcpy(&bits, &f, 4);
                    keep = static_cast<float>(1 - (static_cast<int32_t>(bits) < 0 ? 2 : 0));
                }
                const float rest = side - keep;
                at<float>(pm_ps_ref(pm), 0x10) = rest * d0 + at<float>(pm_ps_ref(pm), 0x10);
                at<float>(pm_ps_ref(pm), 0x14) = rest * d1 + at<float>(pm_ps_ref(pm), 0x14);
            }
        }
    }
    if (!title_global<uint32_t>(0x0095ffac)) {
        uint8_t* const ps = pm_ps_ref(pm);
        const float into = zero - (at<float>(ps, 0x68) * at<float>(ps, 0x14) + at<float>(ps, 0x10) * at<float>(ps, 0x64));
        at<float>(ps, 0x10) = into * at<float>(ps, 0x64) + at<float>(ps, 0x10);
        at<float>(pm_ps_ref(pm), 0x14) = at<float>(pm_ps_ref(pm), 0x68) * into + at<float>(pm_ps_ref(pm), 0x14);
        const float push = wish[2] > zero ? title_global<float>(0x0083f240) : title_global<float>(0x0083f23c);
        at<float>(pm_ps_ref(pm), 0x10) = at<float>(pm_ps_ref(pm), 0x64) * push + at<float>(pm_ps_ref(pm), 0x10);
        at<float>(pm_ps_ref(pm), 0x14) = at<float>(pm_ps_ref(pm), 0x68) * push + at<float>(pm_ps_ref(pm), 0x14);
    }
    own_function<void XB_CDECL(uint32_t)>(&PM_StepSlideMove)(0);  // PM_StepSlideMove
    uint8_t* const ps = pm_ps_ref(pm_global());
    const float half = title_global<float>(0x0083eb60);  // 180
    const float yaw = ((vectoyaw(&at<float>(ps, 0x64)) + half) - at<float>(ps, 0xa0)) * title_global<float>(0x0083ebd8);
    float delta = static_cast<float>(static_cast<int32_t>(yaw) & 0xffff) * title_global<float>(0x0083ebd4);
    if (delta > half) delta = delta - title_global<float>(0x0083ebcc);  // 360
    int32_t offset = static_cast<int32_t>(delta);
    if ((offset < 0 ? -offset : offset) > 0x4b) offset = offset <= 0 ? -0x4b : 0x4b;
    at<int32_t>(ps, 0x78) = static_cast<int8_t>(offset);
}

namespace {

// Q3's PM_ClipVelocity with the title's overbounce: d = (in . n) x 0x0083f1bc when negative, else x 0x0083f1b8;
// the dot's sum order is the caller's ((a + b) + c over the lanes given).
float overclip(float d) {
    return 0.0f > d ? d * title_global<float>(0x0083f1bc) : d * title_global<float>(0x0083f1b8);
}

// The x87 dot product (a0*b0 + a1*b1) + a2*b2 against the double 0.1 (0x0083f1b0): true when below (or unordered).
bool x87_dot_below_tenth(const float* a, const float* b, uint32_t i0, uint32_t i1, uint32_t i2, float* stored) {
    using ext = long double;
    const ext d = platform_x87_ld(a[i0]) * platform_x87_ld(b[i0]) + platform_x87_ld(a[i1]) * platform_x87_ld(b[i1]) +
                  platform_x87_ld(a[i2]) * platform_x87_ld(b[i2]);
    if (stored) *stored = static_cast<float>(d);
    return !(d >= platform_x87_ld(title_global<double>(0x0083f1b0)));
}

}  // namespace

uint32_t XB_CDECL PM_SlideMove(uint32_t gravity) {
    // Q3's PM_SlideMove: up to 4 bumps through PM_PlayerTrace with up to 8 clip planes (16-byte entries: the
    // ground plane, the velocity's direction (Vec4Normalize), the hit normals; a repeated normal (dot > 0x0083ee20)
    // is added to the velocity instead). With gravity the end velocity falls by gravity x frametime and the
    // velocity takes the average. Planes the velocity goes into (x87 dot below 0.1) clip it (impact speed
    // 0x00960000); two planes slide along their crease (packed cross product, rsqrt-Newton normalize); a third
    // stops it. A pm_time (+0x30) restores the first velocity. 1 when it bumped (or stuck / out of planes).
    uint8_t* pm = pm_global();
    alignas(16) uint8_t trace[0x40];
    at<uint32_t>(trace, 0x30) = 0;
    at<uint32_t>(trace, 0x34) = 0;
    uint8_t* ps = pm_ps_ref(pm);
    const float primal[3] = {at<float>(ps, 0x10), at<float>(ps, 0x14), at<float>(ps, 0x18)};
    float primal2 = primal[2];
    float end_velocity[3] = {0.0f, 0.0f, 0.0f};  // never set without gravity (dead stack: 0 in tests)
    const float zero = 0.0f;
    if (gravity) {
        const float g = static_cast<float>(at<int32_t>(ps, 0x48)) * title_global<float>(0x0095ffa4);
        end_velocity[0] = at<float>(ps, 0x10);
        end_velocity[1] = at<float>(ps, 0x14);
        end_velocity[2] = at<float>(ps, 0x18) - g;
        at<float>(ps, 0x18) = (at<float>(ps, 0x18) + end_velocity[2]) * title_global<float>(0x0083eb24);  // 0.5
        primal2 = end_velocity[2];
        if (title_global<uint32_t>(0x0095ffb0)) {
            ps = pm_ps_ref(pm);
            const float* const n = &title_global<float>(0x0095ffd0);
            float* const v = &at<float>(ps, 0x10);
            const float d = overclip((v[0] * n[0] + v[1] * n[1]) + v[2] * n[2]);
            v[0] = v[0] - d * n[0];
            v[1] = v[1] - d * n[1];
            v[2] = v[2] - d * n[2];
        }
    }
    float time_left = title_global<float>(0x0095ffa4);
    alignas(16) float planes[8][4] = {};
    uint32_t count = 0;
    if (title_global<uint32_t>(0x0095ffb0)) {
        for (uint32_t i = 0; i < 3; ++i) planes[0][i] = title_global<float>(0x0095ffd0 + i * 4);
        count = 1;
    }
    Vec4Normalize(&at<float>(pm_ps_ref(pm), 0x10), planes[count]);
    ++count;
    float* const normal = reinterpret_cast<float*>(trace + 0x10);
    uint32_t bump = 0;
    for (; bump < 4; ++bump) {
        ps = pm_ps_ref(pm);
        alignas(16) float end[4] = {0.0f, 0.0f, 0.0f, 0.0f};  // PM_PlayerTrace loads it with MOVAPS; w never set (0 in tests)
        for (uint32_t i = 0; i < 3; ++i) end[i] = at<float>(ps, 0x10 + i * 4) * time_left + at<float>(ps, i * 4);
        own_function<PlayerTrace>(&PM_PlayerTrace)(trace, &at<float>(ps, 0), &at<float>(pm, 0x100), &at<float>(pm, 0x110), end,
                                               at<uint32_t>(ps, 0x7c), at<uint32_t>(pm, 0x5c));  // PM_PlayerTrace
        if (trace[0x3c]) {
            at<float>(pm_ps_ref(pm_global()), 0x18) = zero;
            return 1;
        }
        pm = pm_global();
        const float fraction = at<float>(trace, 0x20);
        if (fraction > zero)
            for (uint32_t i = 0; i < 3; ++i) at<float>(pm_ps_ref(pm), i * 4) = at<float>(trace, i * 4);
        if (at<float>(trace, 0x20) == title_global<float>(0x0083eb20)) break;  // 1
        PM_AddTouchEnt(at<int32_t>(trace, 0x30));
        time_left = time_left - at<float>(trace, 0x20) * time_left;
        if (count >= 8) {
            uint8_t* const p = pm_ps_ref(pm);
            at<float>(p, 0x18) = zero;
            at<float>(pm_ps_ref(pm), 0x14) = zero;
            at<float>(pm_ps_ref(pm), 0x10) = zero;
            return 1;
        }
        bool repeated = false;
        for (uint32_t i = 0; i < count; ++i) {
            const float* const p = planes[i];
            if ((p[0] * normal[0] + p[1] * normal[1]) + normal[2] * p[2] > title_global<float>(0x0083ee20)) {
                for (uint32_t k = 0; k < 3; ++k)
                    at<float>(pm_ps_ref(pm), 0x10 + k * 4) = normal[k] + at<float>(pm_ps_ref(pm), 0x10 + k * 4);
                repeated = true;
                break;
            }
        }
        if (repeated) continue;
        for (uint32_t k = 0; k < 3; ++k) planes[count][k] = normal[k];
        ++count;
        uint32_t i = 0;
        float into = zero;
        for (; i < count; ++i)
            if (x87_dot_below_tenth(planes[i], &at<float>(pm_ps_ref(pm), 0x10), 2, 0, 1, &into)) break;
        if (i == count) continue;
        const float impact = zero - into;
        if (impact > title_global<float>(0x00960000)) title_global<float>(0x00960000) = impact;
        const float* const pi = planes[i];
        const float* const v = &at<float>(pm_ps_ref(pm), 0x10);
        float clip[3], end_clip[3];
        {
            const float d = overclip((v[2] * pi[2] + v[0] * pi[0]) + v[1] * pi[1]);
            for (uint32_t k = 0; k < 3; ++k) clip[k] = v[k] - pi[k] * d;
            const float e = overclip((end_velocity[0] * pi[0] + end_velocity[1] * pi[1]) + end_velocity[2] * pi[2]);
            for (uint32_t k = 0; k < 3; ++k) end_clip[k] = end_velocity[k] - e * pi[k];
        }
        bool stopped = false;
        for (uint32_t j = 0; j < count; ++j) {
            if (j == i) continue;
            const float* const pj = planes[j];
            if (!x87_dot_below_tenth(clip, pj, 0, 1, 2, nullptr)) continue;
            const float d = overclip((clip[0] * pj[0] + clip[1] * pj[1]) + clip[2] * pj[2]);
            for (uint32_t k = 0; k < 3; ++k) clip[k] = clip[k] - d * pj[k];
            const float e = overclip((end_clip[0] * pj[0] + end_clip[1] * pj[1]) + end_clip[2] * pj[2]);
            for (uint32_t k = 0; k < 3; ++k) end_clip[k] = end_clip[k] - e * pj[k];
            if ((clip[1] * pi[1] + clip[2] * pi[2]) + clip[0] * pi[0] >= zero) continue;
            // CrossProduct(planes[i], planes[j]) packed, w lanes included, then the packed normalize.
            float a[4], b[4];
            const float ishuf[4] = {pi[1], pi[2], pi[0], pi[3]};
            const float jshuf[4] = {pj[1], pj[2], pj[0], pj[3]};
            for (uint32_t k = 0; k < 4; ++k) {
                a[k] = ishuf[k] * pj[k];
                b[k] = pi[k] * jshuf[k];
            }
            float c[4];
            for (uint32_t k = 0; k < 4; ++k) c[k] = b[k] - a[k];
            float dir[4] = {c[1], c[2], c[0], c[3]};
            const float sq[4] = {dir[0] * dir[0], dir[1] * dir[1], dir[2] * dir[2], dir[3] * dir[3]};
            platform_keep(sq[3]);
            const float sum = (sq[0] + sq[1]) + sq[2];
            float len;
            if (__builtin_isunordered(sum, title_global<float>(0x0082e680)) || sum == title_global<float>(0x0082e680)) {
                len = sum;
            } else {
                const float r = platform_rsqrt_estimate(sum);
                const float sr = sum * r;
                len = (title_global<float>(0x0082e660) - sr * r) * sr * title_global<float>(0x0082e670);  // 3, 0.5
            }
            if (len != zero) {
                float q[4];
                for (uint32_t k = 0; k < 4; ++k) q[k] = dir[k] / len;
                for (uint32_t k = 0; k < 4; ++k) dir[k] = q[k];
            }
            const float* const vel = &at<float>(pm_ps_ref(pm), 0x10);
            const float dv = (vel[1] * dir[1] + vel[2] * dir[2]) + dir[0] * vel[0];
            for (uint32_t k = 0; k < 3; ++k) clip[k] = dir[k] * dv;
            const float de = (dir[1] * end_velocity[1] + dir[2] * end_velocity[2]) + dir[0] * end_velocity[0];
            for (uint32_t k = 0; k < 3; ++k) end_clip[k] = dir[k] * de;
            for (uint32_t k = 0; k < count; ++k) {
                if (k == i || k == j) continue;
                const float* const pk = planes[k];
                if (!((pk[0] * clip[0] + pk[1] * clip[1]) + clip[2] * pk[2] >= title_global<float>(0x0083ebf8))) {
                    uint8_t* const p = pm_ps_ref(pm);
                    at<float>(p, 0x18) = zero;
                    at<float>(pm_ps_ref(pm), 0x14) = zero;
                    at<float>(pm_ps_ref(pm), 0x10) = zero;
                    stopped = true;
                    break;
                }
            }
            if (stopped) return 1;
        }
        for (uint32_t k = 0; k < 3; ++k) at<float>(pm_ps_ref(pm), 0x10 + k * 4) = clip[k];
        for (uint32_t k = 0; k < 3; ++k) end_velocity[k] = end_clip[k];
    }
    if (gravity)
        for (uint32_t k = 0; k < 3; ++k) at<float>(pm_ps_ref(pm), 0x10 + k * 4) = end_velocity[k];
    if (at<uint32_t>(pm_ps_ref(pm), 0x30)) {
        const float first[3] = {primal[0], primal[1], primal2};
        for (uint32_t k = 0; k < 3; ++k) at<float>(pm_ps_ref(pm), 0x10 + k * 4) = first[k];
    }
    return bump != 0;
}

namespace {

// The ground-trace style clip of the velocity off a trace normal: d = (n0 v0 + n1 v1) + n2 v2 (the sum order the
// callers give), overbounced, v -= n d.
void clip_velocity_by(uint8_t* ps, const float* n, float d) {
    d = overclip(d);
    float* const v = &at<float>(ps, 0x10);
    v[0] = v[0] - n[0] * d;
    v[1] = v[1] - n[1] * d;
    v[2] = v[2] - n[2] * d;
}

}  // namespace

void XB_CDECL PM_StepSlideMove(uint32_t gravity) {
    // Q3's PM_StepSlideMove: PM_SlideMove from the start; when it bumped (and on the ground, on a ladder going up,
    // or landing a jump below its peak +0x74 + 0x0083f05c) a retry from up to a step higher (0x0083eb98 with
    // pm_flags 1, else 0x0083eb00; a jump's step is what is left to its peak, at least 1), then a trace back down
    // (on the ground 0x0083eef8 deeper); a down hit on an entity with +0x23c set keeps the plain move. The step
    // move is dropped when it went no further in xy (0x0083eb88 slack) or overshot the jump's peak; on the ground
    // the plain move is then pushed down too. A jump's climb is limited to the peak (sqrt(2 g h)). A walking step
    // on the ground (pm_type below 4, PM_ProneUndoBlockedMove) adds event 0x91 with the step height (-16..24),
    // slows the velocity by the step's share, and steps above 3 advance the bob cycle (PM_FootstepEvent).
    using ext = long double;
    uint8_t* pm = pm_global();
    alignas(16) uint8_t trace[0x40];
    at<uint32_t>(trace, 0x30) = 0;
    at<uint32_t>(trace, 0x34) = 0;
    const float zero = 0.0f;
    const float one = title_global<float>(0x0083eb20);
    float step_size = zero;
    uint32_t ground;
    {
        uint8_t* const ps = pm_ps_ref(pm);
        const uint32_t flags = at<uint32_t>(ps, 0x2c);
        if (flags & 0x10) {
            at<uint32_t>(ps, 0x2c) = flags & 0xffffdfffu;
            ground = 0;
            at<float>(pm_ps_ref(pm), 0x74) = zero;
        } else if (title_global<uint32_t>(0x0095ffb0)) {
            ground = 1;
        } else {
            ground = 0;
            if ((flags & 0x2000) && at<uint32_t>(ps, 0x30)) {
                at<uint32_t>(ps, 0x2c) &= 0xffffdfffu;
                at<float>(pm_ps_ref(pm), 0x74) = zero;
            }
        }
    }
    alignas(16) float start_o[4] = {0.0f, 0.0f, 0.0f, 0.0f};  // a MOVAPS trace start; w never set (0 in tests)
    float start_v[3];
    {
        const uint8_t* const ps = pm_ps_ref(pm);
        for (uint32_t i = 0; i < 3; ++i) start_o[i] = at<float>(ps, i * 4);
        for (uint32_t i = 0; i < 3; ++i) start_v[i] = at<float>(ps, 0x10 + i * 4);
    }
    const uint32_t bumped = PM_SlideMove(gravity);
    pm = pm_global();
    uint8_t* ps = pm_ps_ref(pm);
    const float base_step = title_global<float>(0x0083eb00);
    float step = (at<uint32_t>(ps, 0x2c) & 1) ? title_global<float>(0x0083eb98) : base_step;
    bool jumping = false;
    if (!at<uint32_t>(ps, 0x60)) {
        const uint32_t flags = at<uint32_t>(ps, 0x2c);
        if ((flags & 0x2000) && at<uint32_t>(ps, 0x30)) {
            at<uint32_t>(ps, 0x2c) = at<uint32_t>(ps, 0x2c) & 0xffffdfffu;
            at<float>(pm_ps_ref(pm), 0x74) = zero;
        }
        bool landing = false;
        ps = pm_ps_ref(pm);
        if (bumped && (at<uint32_t>(ps, 0x2c) & 0x2000)) {
            const float peak = at<float>(ps, 0x74) + title_global<float>(0x0083f05c);
            if (peak > start_o[2]) {
                step = base_step;
                if (start_o[2] + base_step > peak) {
                    step = peak - start_o[2];
                    if (one > step) return;
                }
                jumping = true;
                landing = true;
            }
        }
        if (!landing) {
            if (!(at<uint8_t>(ps, 0x2c) & 0x10)) return;
            if (!(at<float>(ps, 0x18) > title_global<float>(0x0083eafc))) return;
        }
    }
    float down_o[3], down_v[3];
    for (uint32_t i = 0; i < 3; ++i) down_o[i] = at<float>(ps, i * 4);
    for (uint32_t i = 0; i < 3; ++i) down_v[i] = at<float>(ps, 0x10 + i * 4);
    const float dx = down_o[0] - start_o[0];
    const float dy = down_o[1] - start_o[1];
    using Trace = void XB_CDECL(uint8_t*, const float*, const float*, const float*, const float*, uint32_t, uint32_t);
    const auto player_trace = [&trace](uint8_t* p, const float* start, const float* end) {
        own_function<Trace>(&PM_PlayerTrace)(trace, start, &at<float>(p, 0x100), &at<float>(p, 0x110), end,
                                          at<uint32_t>(pm_ps_ref(p), 0x7c), at<uint32_t>(p, 0x5c));  // PM_PlayerTrace
    };
    if (bumped) {
        alignas(16) const float up[4] = {start_o[0], start_o[1], (start_o[2] + step) + one, 0.0f};
        player_trace(pm, start_o, up);
        const float s = (step + one) * at<float>(trace, 0x20) - one;
        step_size = s;
        if (one > s) {
            step_size = zero;
        } else {
            uint8_t* const p = pm_ps_ref(pm_global());
            at<float>(p, 0) = up[0];
            at<float>(pm_ps_ref(pm_global()), 4) = up[1];
            at<float>(pm_ps_ref(pm_global()), 8) = start_o[2] + s;
            for (uint32_t i = 0; i < 3; ++i) at<float>(pm_ps_ref(pm_global()), 0x10 + i * 4) = start_v[i];
            PM_SlideMove(gravity);
        }
    }
    pm = pm_global();
    if (ground || !(step_size == title_global<float>(0x0083eafc))) {
        ps = pm_ps_ref(pm);
        alignas(16) float down[4] = {at<float>(ps, 0), at<float>(ps, 4), at<float>(ps, 8) - step_size, 0.0f};
        if (ground) down[2] = down[2] - title_global<float>(0x0083eef8);
        player_trace(pm, &at<float>(ps, 0), down);
        const uint32_t hit = at<uint32_t>(trace, 0x30);
        if (hit) {
            const uint32_t index = hit & 0xfff;
            const uint8_t* ent = nullptr;
            if (index < 0x540 && (hit >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
                ent = title_global<uint8_t*>(0x0097a968 + index * 8);
            if (at<uint32_t>(ent, 0x23c)) {
                for (uint32_t i = 0; i < 3; ++i) at<float>(pm_ps_ref(pm_global()), i * 4) = down_o[i];
                for (uint32_t i = 0; i < 3; ++i) at<float>(pm_ps_ref(pm_global()), 0x10 + i * 4) = down_v[i];
                return;
            }
        }
        pm = pm_global();
        if (one > at<float>(trace, 0x20)) {
            for (uint32_t i = 0; i < 3; ++i) at<float>(pm_ps_ref(pm), i * 4) = at<float>(trace, i * 4);
            uint8_t* const p = pm_ps_ref(pm);
            const float* const n = reinterpret_cast<const float*>(trace + 0x10);
            clip_velocity_by(p, n, (n[0] * at<float>(p, 0x10) + n[1] * at<float>(p, 0x14)) + n[2] * at<float>(p, 0x18));
        } else if (!(step_size == title_global<float>(0x0083eafc))) {
            uint8_t* const p = pm_ps_ref(pm);
            at<float>(p, 8) = at<float>(p, 8) - step_size;
        }
    }
    ps = pm_ps_ref(pm);
    {
        const float mx = at<float>(ps, 0) - start_o[0];
        const float my = at<float>(ps, 4) - start_o[1];
        const float moved = at<float>(ps, 0x14) * my + mx * at<float>(ps, 0x10);
        const float plain = (at<float>(ps, 0x14) * dy + dx * at<float>(ps, 0x10)) + title_global<float>(0x0083eb88);
        bool use_plain = plain >= moved;
        if (!use_plain) {
            if (!jumping) goto step_event;
            use_plain = !(at<float>(ps, 8) < at<float>(ps, 0x74) + title_global<float>(0x0083f05c));
        }
        if (use_plain) {
            for (uint32_t i = 0; i < 3; ++i) at<float>(pm_ps_ref(pm), i * 4) = down_o[i];
            for (uint32_t i = 0; i < 3; ++i) at<float>(pm_ps_ref(pm), 0x10 + i * 4) = down_v[i];
            if (ground) {
                uint8_t* const p = pm_ps_ref(pm);
                alignas(16) const float down[4] = {at<float>(p, 0), at<float>(p, 4),
                                                   at<float>(p, 8) - title_global<float>(0x0083eef8), 0.0f};
                player_trace(pm, &at<float>(p, 0), down);
                pm = pm_global();
                if (one > at<float>(trace, 0x20)) {
                    for (uint32_t i = 0; i < 3; ++i) at<float>(pm_ps_ref(pm), i * 4) = at<float>(trace, i * 4);
                    uint8_t* const q = pm_ps_ref(pm);
                    const float* const n = reinterpret_cast<const float*>(trace + 0x10);
                    clip_velocity_by(q, n,
                                     (n[1] * at<float>(q, 0x14) + n[2] * at<float>(q, 0x18)) + at<float>(q, 0x10) * n[0]);
                }
            }
        }
    }
    if (jumping) {
        uint8_t* const p = pm_ps_ref(pm);
        if (at<float>(p, 8) - down_o[2] > zero) {
            const float h = (at<float>(p, 0x74) + title_global<float>(0x0083f05c)) - at<float>(p, 8);
            if (title_global<float>(0x0083ebf8) > h) {
                at<float>(p, 0x18) = zero;
            } else {
                const ext root =
                    platform_x87_sqrt_ext(platform_x87_ld(at<int32_t>(p, 0x48)) * (platform_x87_ld(h) + platform_x87_ld(h)));
                if (platform_x87_ld(at<float>(p, 0x18)) > root) at<float>(p, 0x18) = static_cast<float>(root);
            }
        }
    }
step_event:
    if (!ground) return;
    {
        uint8_t* const p = pm_ps_ref(pm);
        if (at<int32_t>(p, 0x24) >= 4) return;
    }
    if (!PM_ProneUndoBlockedMove_body(start_o, start_v)) return;
    pm = pm_global();
    uint8_t* const p = pm_ps_ref(pm);
    const float rise = static_cast<float>(platform_x87_ld(at<float>(p, 8)) - platform_x87_ld(down_o[2]));
    if (!(__builtin_fabsl(platform_x87_ld(at<float>(p, 8)) - platform_x87_ld(down_o[2])) >
          platform_x87_ld(title_global<float>(0x0083eb24))))  // 0.5
        return;
    int32_t height = static_cast<int32_t>(rise + title_global<float>(0x0083eb24));
    if (!height) return;
    if (height < -16)
        height = -16;
    else if (height > 0x18)
        height = 0x18;
    const uint32_t slot = at<uint32_t>(p, 0xc4) & 3;
    at<uint32_t>(p, 0xc8 + slot * 4) = 0x91;
    at<uint32_t>(p, 0xd8 + (at<uint32_t>(p, 0xc4) & 3) * 4) = static_cast<uint8_t>(height + 0x80);
    ++at<uint32_t>(p, 0xc4);
    {
        uint8_t* const q = pm_ps_ref(pm);
        float climbed = at<float>(q, 8) - start_o[2];
        uint32_t bits;
        std::memcpy(&bits, &climbed, 4);
        bits &= 0x7fffffffu;
        std::memcpy(&climbed, &bits, 4);
        const float factor = (one - climbed / step) * title_global<float>(0x0083ebc0) + title_global<float>(0x0083f200);
        at<float>(q, 0x10) = factor * at<float>(q, 0x10);
        at<float>(pm_ps_ref(pm), 0x14) = at<float>(pm_ps_ref(pm), 0x14) * factor;
        at<float>(pm_ps_ref(pm), 0x18) = at<float>(pm_ps_ref(pm), 0x18) * factor;
    }
    const int32_t magnitude = height < 0 ? -height : height;
    if (magnitude <= 3) return;
    uint8_t* const r = pm_ps_ref(pm);
    if (!at<uint32_t>(r, 0x60)) return;
    uint32_t scratch[2] = {0, 0};
    if (!PM_CheckViewHeightStance(scratch)) return;
    int32_t bob = magnitude / 2;
    if (bob > 4) bob = 4;
    const int32_t old_cycle = at<int32_t>(r, 0x28);
    const float cycle = static_cast<float>(bob) * title_global<float>(0x0083f1fc) + title_global<float>(0x0083f1f8) +
                        static_cast<float>(old_cycle);
    at<int32_t>(r, 0x28) = static_cast<int32_t>(cycle) & 0xff;
    PM_FootstepEvent(old_cycle, 1, at<int32_t>(pm_ps_ref(pm_global()), 0x28));
}

namespace {

// Q3's PM_ClipVelocity of a flattened axis {x, y, 0} off the ground normal (0x0095ffd0) with overbounce
// 0x0083f1bc (multiplied when the dot is negative, else divided).
void clip_axis_to_ground(float x, float y, float* out) {
    const float* const n = &title_global<float>(0x0095ffd0);
    const float zero = 0.0f;
    float d = (n[1] * y + n[2] * zero) + n[0] * x;
    if (zero > d)
        d = d * title_global<float>(0x0083f1bc);
    else
        d = d / title_global<float>(0x0083f1bc);
    out[0] = x - n[0] * d;
    out[1] = y - n[1] * d;
    out[2] = zero - n[2] * d;
}

}  // namespace

void XB_CDECL PM_WalkMove() {
    // Q3's PM_WalkMove: the landing slowdown first (pm_flags 0x2000: past 1800 ms it ends and the velocity
    // scales by 0x0083f08c; at time 0 it starts, 1800 ms x 0x0083f08c above the jump origin +0x74 + 0x0083eb00,
    // else 1200 ms x 0.5); a jump goes to PM_AirMove. Then friction, PM_WalkCmdScale on a copy of the command,
    // forward/right clipped to the ground plane and normalized, the wish velocity, an inlined PM_Accelerate
    // (accel 1 on slick ground (surface 2) or pm_flags 0x200, else by stance 0x0082e8e8 prone / 0x0083eccc
    // crouch / 0x0083eef8; x0.25 with flag 0x100; minimum wish speed 0x0083eb90; on a ground entity x 1 / +0x478),
    // gravity on slick ground, the velocity clipped to the plane at its old speed (packed normalize) and
    // PM_StepSlideMove(0) when moving in xy; PM_SetMovementDir, G_SeparateFromSolids.
    using ext = long double;
    uint8_t* const pm = pm_global();
    {
        uint8_t* ps = pm_ps_ref(pm);
        if (at<uint32_t>(ps, 0x2c) & 0x2000) {
            const int32_t time = at<int32_t>(ps, 0x30);
            bool scale_velocity = true;
            float k = 0.0f;
            if (time > 0x708) {
                at<uint32_t>(ps, 0x2c) &= ~0x2000u;
                at<float>(pm_ps_ref(pm), 0x74) = 0.0f;
                k = title_global<float>(0x0083f08c);
            } else if (!time) {
                if (at<float>(ps, 0x74) + title_global<float>(0x0083eb00) > at<float>(ps, 8)) {
                    k = title_global<float>(0x0083f08c);
                    at<int32_t>(ps, 0x30) = 0x708;
                } else {
                    k = title_global<float>(0x0083eb24);  // 0.5
                    at<int32_t>(ps, 0x30) = 0x4b0;
                }
            } else {
                scale_velocity = false;
            }
            if (scale_velocity)
                for (uint32_t i = 0; i < 3; ++i) at<float>(pm_ps_ref(pm), 0x10 + i * 4) = at<float>(pm_ps_ref(pm), 0x10 + i * 4) * k;
        }
    }
    if (PM_CheckJump()) {
        PM_AirMove();
        uint8_t* const p = pm_global();
        at<uint32_t>(pm_ps_ref(p), 0x70) = at<uint32_t>(p, 4);
        return;
    }
    PM_Friction();
    const float fmove = static_cast<float>(static_cast<int32_t>(static_cast<int8_t>(pm[0x18])));
    const float smove = static_cast<float>(static_cast<int32_t>(static_cast<int8_t>(pm[0x19])));
    alignas(4) uint8_t cmd[0x2c];
    std::memcpy(cmd, pm + 4, sizeof(cmd));
    const float scale = PM_WalkCmdScale(reinterpret_cast<const int8_t*>(cmd));
    float* const forward = &title_global<float>(0x0095ff80);
    float* const right = &title_global<float>(0x0095ff8c);
    const float zero = 0.0f;
    const float one = title_global<float>(0x0083eb20);
    float f[3], r[3];
    clip_axis_to_ground(forward[0], forward[1], f);
    clip_axis_to_ground(right[0], right[1], r);
    forward[2] = f[2];
    forward[1] = f[1];
    forward[0] = f[0];
    const ext forward_sq = platform_x87_ld(forward[1]) * platform_x87_ld(forward[1]) +
                           platform_x87_ld(forward[0]) * platform_x87_ld(forward[0]) +
                           platform_x87_ld(forward[2]) * platform_x87_ld(forward[2]);
    for (uint32_t i = 0; i < 3; ++i) right[i] = r[i];
    const float forward_length = pm_ci_sqrt(platform_x87_ld(static_cast<float>(forward_sq)));
    if (!x87_equal(forward_length, title_global<float>(0x0083eafc))) {
        const float inv = one / forward_length;
        forward[0] = inv * forward[0];
        forward[1] = inv * forward[1];
        forward[2] = inv * forward[2];
    }
    const ext right_sq = platform_x87_ld(r[2]) * platform_x87_ld(r[2]) + platform_x87_ld(r[1]) * platform_x87_ld(r[1]) +
                         platform_x87_ld(r[0]) * platform_x87_ld(r[0]);
    const float right_length = pm_ci_sqrt(platform_x87_ld(static_cast<float>(right_sq)));
    if (!x87_equal(right_length, title_global<float>(0x0083eafc))) {
        const float inv = one / right_length;
        for (uint32_t i = 0; i < 3; ++i) right[i] = r[i] * inv;
    }
    float wish[3];
    for (uint32_t i = 0; i < 3; ++i) wish[i] = right[i] * smove + forward[i] * fmove;
    const float wish_sq = (wish[2] * wish[2] + wish[1] * wish[1]) + wish[0] * wish[0];
    const float wishspeed_raw = pm_ci_sqrt(platform_x87_ld(wish_sq));
    float dir[3];
    if (!x87_equal(wishspeed_raw, title_global<float>(0x0083eafc))) {
        const float inv = one / wishspeed_raw;
        for (uint32_t i = 0; i < 3; ++i) dir[i] = inv * wish[i];
    } else {
        for (uint32_t i = 0; i < 3; ++i) dir[i] = wish[i];
    }
    uint8_t* ps = pm_ps_ref(pm);
    const uint32_t view = at<uint32_t>(ps, 0xa8);
    const uint32_t stance = view == at<uint32_t>(ps, 0x424) ? 2 : (view == at<uint32_t>(ps, 0x420) ? 1 : 0);
    const float wishspeed = wishspeed_raw * scale;
    const bool slick = (title_global<uint8_t>(0x0095ffe4) & 2) || (at<uint32_t>(ps, 0x2c) & 0x200);
    float accel;
    if (slick)
        accel = one;
    else if (stance == 1)
        accel = title_global<float>(0x0082e8e8);
    else if (stance == 2)
        accel = title_global<float>(0x0083eccc);
    else
        accel = title_global<float>(0x0083eef8);
    if (at<uint32_t>(ps, 0x2c) & 0x100) accel = accel * title_global<float>(0x0083ec24);  // 0.25
    {
        const float current = (at<float>(ps, 0x18) * dir[2] + at<float>(ps, 0x14) * dir[1]) + dir[0] * at<float>(ps, 0x10);
        const float add = wishspeed - current;
        if (!(zero >= add)) {
            float speed = title_global<float>(0x0083eb90) > wishspeed ? title_global<float>(0x0083eb90) : wishspeed;
            speed = speed * title_global<float>(0x0095ffa4) * accel;
            if (speed > add) speed = add;
            if (at<uint32_t>(ps, 0x60)) speed = one / at<float>(ps, 0x478) * speed;
            if (speed > add) speed = add;
            at<float>(ps, 0x10) = speed * dir[0] + at<float>(ps, 0x10);
            at<float>(pm_ps_ref(pm), 0x14) = speed * dir[1] + at<float>(pm_ps_ref(pm), 0x14);
            at<float>(pm_ps_ref(pm), 0x18) = speed * dir[2] + at<float>(pm_ps_ref(pm), 0x18);
        }
    }
    if ((title_global<uint8_t>(0x0095ffe4) & 2) || (at<uint32_t>(pm_ps_ref(pm), 0x2c) & 0x200)) {
        uint8_t* const p = pm_ps_ref(pm);
        at<float>(p, 0x18) = at<float>(p, 0x18) - static_cast<float>(at<int32_t>(p, 0x48)) * title_global<float>(0x0095ffa4);
    }
    ps = pm_ps_ref(pm);
    float* const v = &at<float>(ps, 0x10);
    const float old0 = v[0], old1 = v[1], old2 = v[2];
    const float speed_sq = (old0 * old0 + old1 * old1) + old2 * old2;
    const float speed = pm_ci_sqrt(platform_x87_ld(speed_sq));
    {
        const float* const n = &title_global<float>(0x0095ffd0);
        float d = (old0 * n[0] + old2 * n[2]) + old1 * n[1];
        if (zero > d)
            d = d * title_global<float>(0x0083f1bc);
        else
            d = d / title_global<float>(0x0083f1bc);
        v[0] = v[0] - n[0] * d;
        v[1] = v[1] - n[1] * d;
        v[2] = v[2] - n[2] * d;
    }
    float* const w = &at<float>(pm_ps_ref(pm), 0x10);
    if ((w[2] * old2 + w[1] * old1) + old0 * w[0] > zero) {
        const float sq[4] = {w[0] * w[0], w[1] * w[1], w[2] * w[2], w[3] * w[3]};
        platform_keep(sq[3]);
        const float sum = sq[0] + sq[1] + sq[2];
        float len;
        if (__builtin_isunordered(sum, title_global<float>(0x0082e680)) || sum == title_global<float>(0x0082e680)) {
            len = sum;
        } else {
            const float e = platform_rsqrt_estimate(sum);
            const float se = sum * e;
            len = (title_global<float>(0x0082e660) - se * e) * se * title_global<float>(0x0082e670);  // 3, 0.5
        }
        if (len != zero) {
            float q[4];
            for (uint32_t k = 0; k < 4; ++k) q[k] = w[k] / len;
            for (uint32_t k = 0; k < 4; ++k) w[k] = q[k];
        }
        for (uint32_t i = 0; i < 3; ++i) at<float>(pm_ps_ref(pm), 0x10 + i * 4) = speed * at<float>(pm_ps_ref(pm), 0x10 + i * 4);
    }
    const uint8_t* const p = pm_ps_ref(pm);
    if (!(at<float>(p, 0x10) == zero) || !(at<float>(p, 0x14) == zero))
        own_function<void XB_CDECL(uint32_t)>(&PM_StepSlideMove)(0);  // PM_StepSlideMove
    PM_SetMovementDir();
    G_SeparateFromSolids();
}

namespace {

// The player state's predictable event ring (+0xc4 sequence, events +0xc8, parms +0xd8; 4 slots).
void duck_event(uint8_t* ps, uint32_t event) {
    at<uint32_t>(ps, 0xc8 + (at<uint32_t>(ps, 0xc4) & 3) * 4) = event;
    at<uint32_t>(ps, 0xd8 + (at<uint32_t>(ps, 0xc4) & 3) * 4) = 0;
    ++at<uint32_t>(ps, 0xc4);
}

}  // namespace

void XB_CDECL PM_CheckDuck() {
    // Q3's PM_CheckDuck with prone: pm's bounds from the player state (+0x408..+0x418; top +0x41c, 50 crouched /
    // prone, 0x0083ee38 prone when lying), dead (pm_type 4 up: dead view height +0x42c), mounted (eFlags 0x100000
    // without 0x400000: standing, event 0x8e) and forced stances (eFlags 0x2000 prone / 0x4000 crouch). The stance
    // requests (pm +9: 0x40 prone, 0x80 crouch) from the stance buttons (pm_flags 0x80000 / 0x100000 / 0x200000,
    // events 0x8e stand / 0x8f crouch / 0x90 prone) are checked with pm's trace (+0x138, player/vehicle contents
    // masked) and BG_CheckProneFeet; pm_flags 1 = prone, 2 = crouched. Then the view height target (+0xa8, with
    // the stance lerp +0xb0), PM_ViewHeightAdjust, eFlags 0x20 / 0x40, the trace function (+0x130 = +0x134 prone,
    // else +0x138) and, newly prone, the ground pitch under the player (+0x454, via Plane_PitchAlongYaw) and its
    // limited copy (+0x458, within 0x0083ef18..0x0083ef1c of the pitch).
    using Trace = void XB_CDECL(uint8_t*, const float*, const float*, const float*, const float*, uint32_t, uint32_t);
    using CheckProneFeet = uint32_t XB_REGS1 XB_CDECL(uint32_t, uint32_t, uint8_t*, uint32_t, uint32_t, uint8_t*, uint8_t*,
                                                      uint8_t*, uint32_t, uint32_t);
    using Break = void XB_CDECL(int32_t, const char*);
    uint8_t* pm = pm_global();
    alignas(16) uint8_t trace[0x40];
    at<uint32_t>(trace, 0x30) = 0;
    at<uint32_t>(trace, 0x34) = 0;
    uint8_t* ps = pm_ps_ref(pm);
    at<uint32_t>(pm, 0x100) = at<uint32_t>(ps, 0x408);
    const uint32_t was_prone = at<uint32_t>(ps, 0x2c) & 1;
    at<uint32_t>(pm, 0x104) = at<uint32_t>(pm_ps_ref(pm), 0x40c);
    at<uint32_t>(pm, 0x110) = at<uint32_t>(pm_ps_ref(pm), 0x414);
    at<uint32_t>(pm, 0x114) = at<uint32_t>(pm_ps_ref(pm), 0x418);
    at<uint32_t>(pm, 0x108) = at<uint32_t>(pm_ps_ref(pm), 0x410);
    ps = pm_ps_ref(pm);
    const float fifty = title_global<float>(0x0083ec30);
    const auto stance_trace = [&]() {
        uint8_t* const p = pm_ps_ref(pm);
        title_function<Trace>(at<uint32_t>(pm, 0x138))(trace, &at<float>(p, 0), &at<float>(pm, 0x100), &at<float>(pm, 0x110),
                                                      &at<float>(p, 0), at<uint32_t>(p, 0x7c),
                                                      at<uint32_t>(pm, 0x5c) & 0xfdff3fffu);
        pm = pm_global();
        return trace[0x3c] != 0;
    };
    const auto event = [&](uint32_t e) { duck_event(pm_ps_ref(pm), e); };
    if (at<int32_t>(ps, 0x24) >= 4) {
        at<uint32_t>(pm, 0x118) = at<uint32_t>(ps, 0x41c);
        uint8_t* const p = pm_ps_ref(pm);
        at<uint32_t>(p, 0xa8) = at<uint32_t>(p, 0x42c);
        uint8_t* const q = pm_ps_ref(pm);
        at<uint32_t>(pm, 0x130) = (at<uint8_t>(q, 0x2c) & 1) ? at<uint32_t>(pm, 0x134) : at<uint32_t>(pm, 0x138);
        at<uint32_t>(q, 0xc0) |= 0x10;
        PM_ViewHeightAdjust();
        return;
    }
    const uint32_t eflags = at<uint32_t>(ps, 0xc0);
    if ((eflags & 0x100000) && !(eflags & 0x400000)) {
        at<uint32_t>(pm, 0x118) = at<uint32_t>(ps, 0x41c);
        uint8_t* p = pm_ps_ref(pm);
        at<uint32_t>(p, 0xa8) = at<uint32_t>(p, 0x428);
        pm[9] &= 0xbf;
        p = pm_ps_ref(pm);
        at<uint32_t>(p, 0x2c) &= 0xfffffffcu;
        event(0x8e);
        at<uint32_t>(pm, 0x130) = at<uint32_t>(pm, 0x138);
        at<uint32_t>(pm_ps_ref(pm), 0xc0) |= 0x10;
        PM_ViewHeightAdjust();
        return;
    }
    uint32_t ev = 0;
    if (eflags & 0x106000) {
        const uint32_t forced_prone = eflags & 0x2000;
        if (forced_prone && !(eflags & 0x4000)) goto set_prone;
        if (!forced_prone && (eflags & 0x4000)) {
            uint8_t* const p = pm_ps_ref(pm);
            at<uint32_t>(p, 0x2c) |= 2;
            at<uint32_t>(pm_ps_ref(pm), 0x2c) &= 0xfffffffeu;
            goto view_height;
        }
        goto clear_both;
    }
    {
        const uint32_t pflags = at<uint32_t>(ps, 0x2c);
        if (pflags & 0x4000) goto view_height;
        if ((pflags & 0x20) &&
            at<int32_t>(title_global<uint8_t**>(0x0095f4d8)[at<int32_t>(ps, 0x80)], 0x98) == 3)
            goto view_height;
        if (at<int32_t>(ps, 0x24) == 1 || (eflags & 0x400000)) {
            // 0x2ef6f6: no new stance from a stance button; requests become events only.
            const uint32_t buttons = pflags & 0x180000;
            if (buttons || (pm[9] & 0x40)) {
                if (buttons == 0x180000) title_function<Break>(0x002938f0)(1, pointer<const char>(0x008368c0));
                const uint8_t req = pm[9];
                uint8_t* const p = pm_ps_ref(pm);
                const uint32_t f = at<uint32_t>(p, 0x2c);
                bool fire = true;
                if (req & 0x40) {
                    ev = ((f & 3) || (f & 0x80000)) && !(f & 0x100000) ? 0x8f : 0x8e;
                } else if (req & 0x80) {
                    if (f & 0x100000) ev = 0x8e; else fire = false;
                } else {
                    if (f & 0x80000) ev = 0x8f; else fire = false;
                }
                if (fire) {
                    pm[9] = req & 0x3f;
                    if (ev == 0x8f) pm[9] = (req & 0x3f) | 0x80;
                    BG_AddPredictableEventToPlayerstate(p, 0, ev, 0);
                }
            }
            if (pm[9] & 0xc0) goto crouch_not_prone;
            goto clear_both;
        }
        if ((pflags & 0x10) && (pm[9] & 0xc0)) {
            pm[9] &= 0x3f;
            duck_event(ps, 0x8e);
        }
        const uint32_t buttons = at<uint32_t>(pm_ps_ref(pm), 0x2c) & 0x380000;
        if (buttons) {
            if (buttons == 0x380000) title_function<Break>(0x002938f0)(1, pointer<const char>(0x00836898));
            const uint8_t req = pm[9];
            uint8_t* const p = pm_ps_ref(pm);
            const uint32_t f = at<uint32_t>(p, 0x2c);
            bool fire = true;
            if (req & 0x40) {
                if (!(f & 0x200000))
                    fire = false;
                else if ((f & 3) || (f & 0x80000))
                    ev = (f & 0x100000) ? 0x8e : 0x8f;
                else
                    ev = 0x8e;
            } else if (req & 0x80) {
                if (!(f & 0x100000))
                    fire = false;
                else if ((f & 3) || (f & 0x200000))
                    ev = (f & 0x80000) ? 0x90 : 0x8e;
                else
                    ev = 0x90;
            } else {
                if (!(f & 0x80000))
                    fire = false;
                else if ((f & 1) && !(f & 0x200000))
                    ev = 0x90;
                else
                    ev = (f & 0x100000) ? 0x90 : 0x8f;
            }
            if (fire) {
                const uint8_t base = req & 0x3f;
                pm[9] = base;
                if (ev == 0x90)
                    pm[9] = base | 0x40;
                else if (ev == 0x8f)
                    pm[9] = base | 0x80;
                BG_AddPredictableEventToPlayerstate(p, 0, ev, 0);
            }
        }
        // 0x2ef378: act on the request.
        const uint8_t req = pm[9];
        if (req & 0x40) {
            uint8_t* p = pm_ps_ref(pm);
            if (at<uint8_t>(p, 0x2c) & 1) goto set_prone;
            if (at<uint32_t>(p, 0x60) && !pm[0x121]) {
                const uint32_t r = own_function<CheckProneFeet>(&BG_CheckProneFeet_entry)(
                    0, at<uint32_t>(p, 0x7c), p, 0x41f00000u, at<uint32_t>(p, 0xa0), p + 0x4d8, p + 0x4dc, p + 0x4e0,
                    at<uint32_t>(pm, 0x138), 0x42700000u);
                pm = pm_global();
                if (r) goto set_prone;
            }
            p = pm_ps_ref(pm);
            if (!at<uint32_t>(p, 0x60)) goto view_height;
            at<uint32_t>(p, 0x2c) |= 0x8000;
            if (pm[9] & 2) goto view_height;
            p = pm_ps_ref(pm);
            duck_event(p, (at<uint8_t>(p, 0x2c) & 2) ? 0x8f : 0x8e);
            goto view_height;
        }
        if (req & 0x80) {
            if (!(at<uint8_t>(pm_ps_ref(pm), 0x2c) & 1)) goto set_crouch;
            at<float>(pm, 0x118) = fifty;
            if (!stance_trace()) goto crouch_not_prone;
            if (pm[9] & 2) goto view_height;
            event(0x90);
            goto view_height;
        }
        const uint32_t f = at<uint32_t>(pm_ps_ref(pm), 0x2c);
        if (f & 1) {
            at<uint32_t>(pm, 0x118) = at<uint32_t>(pm_ps_ref(pm), 0x41c);
            if (!stance_trace()) goto clear_both;
            at<float>(pm, 0x118) = fifty;
            if (!stance_trace()) goto crouch_not_prone;
            if (pm[9] & 2) goto view_height;
            event(0x90);
            goto view_height;
        }
        if (f & 2) {
            at<uint32_t>(pm, 0x118) = at<uint32_t>(pm_ps_ref(pm), 0x41c);
            if (!stance_trace()) {
                at<uint32_t>(pm_ps_ref(pm), 0x2c) &= 0xfffffffdu;
                goto view_height;
            }
            if (pm[9] & 2) goto view_height;
            event(0x8f);
        }
        goto view_height;
    }
set_prone:
    at<uint32_t>(pm_ps_ref(pm), 0x2c) |= 1;
    at<uint32_t>(pm_ps_ref(pm), 0x2c) &= 0xfffffffdu;
    goto view_height;
clear_both:
    at<uint32_t>(pm_ps_ref(pm), 0x2c) &= 0xfffffffcu;
    goto view_height;
crouch_not_prone:
    at<uint32_t>(pm_ps_ref(pm), 0x2c) &= 0xfffffffeu;
set_crouch:
    at<uint32_t>(pm_ps_ref(pm), 0x2c) |= 2;
view_height:
    ps = pm_ps_ref(pm);
    if (!at<uint32_t>(ps, 0xb0)) {
        if (at<uint8_t>(ps, 0x2c) & 1) {
            if (at<uint32_t>(ps, 0xa8) == at<uint32_t>(ps, 0x428)) {
                at<uint32_t>(ps, 0xa8) = at<uint32_t>(ps, 0x424);
            } else {
                if (title_global<int32_t>(0x00950bcc) == 2) {
                    own_function<CheckProneFeet>(&BG_CheckProneFeet_entry)(0, at<uint32_t>(ps, 0x7c), ps, 0x41f00000u,
                                                               at<uint32_t>(ps, 0xa0), nullptr, nullptr, nullptr,
                                                               at<uint32_t>(pm, 0x138), 0x42700000u);
                    pm = pm_global();
                }
                uint8_t* const p = pm_ps_ref(pm);
                if (at<uint32_t>(p, 0xa8) != at<uint32_t>(p, 0x420)) {
                    at<uint32_t>(p, 0xa8) = at<uint32_t>(p, 0x420);
                    at<uint32_t>(pm_ps_ref(pm), 0x2c) |= 0x2000;
                    at<uint32_t>(pm_ps_ref(pm), 0x30) = 0x708;
                }
            }
        } else if (at<uint32_t>(ps, 0xa8) == at<uint32_t>(ps, 0x420) || (at<uint8_t>(ps, 0x2c) & 2)) {
            at<uint32_t>(ps, 0xa8) = at<uint32_t>(ps, 0x424);
        } else {
            at<uint32_t>(ps, 0xa8) = at<uint32_t>(ps, 0x428);
        }
    }
    PM_ViewHeightAdjust();
    pm = pm_global();
    ps = pm_ps_ref(pm);
    const uint32_t view = at<uint32_t>(ps, 0xa8);
    uint32_t ef;
    if (view == at<uint32_t>(ps, 0x424)) {
        if (at<uint8_t>(ps, 0x2c) & 3)
            at<float>(pm, 0x118) = fifty;
        else
            at<uint32_t>(pm, 0x118) = at<uint32_t>(ps, 0x41c);
        at<uint32_t>(pm_ps_ref(pm), 0xc0) |= 0x20;
        ef = at<uint32_t>(pm_ps_ref(pm), 0xc0) & 0xffffffbfu;
    } else if (view == at<uint32_t>(ps, 0x420)) {
        if (at<uint8_t>(ps, 0x2c) & 3)
            at<float>(pm, 0x118) = title_global<float>(0x0083ee38);
        else
            at<uint32_t>(pm, 0x118) = at<uint32_t>(ps, 0x41c);
        at<uint32_t>(pm_ps_ref(pm), 0xc0) |= 0x40;
        ef = at<uint32_t>(pm_ps_ref(pm), 0xc0) & 0xffffffdfu;
    } else {
        at<uint32_t>(pm, 0x118) = at<uint32_t>(ps, 0x41c);
        ef = at<uint32_t>(pm_ps_ref(pm), 0xc0) & 0xffffff9fu;
    }
    ps = pm_ps_ref(pm);
    at<uint32_t>(ps, 0xc0) = ef;
    ps = pm_ps_ref(pm);
    if (!(at<uint8_t>(ps, 0x2c) & 1)) {
        at<uint32_t>(pm, 0x130) = at<uint32_t>(pm, 0x138);
        at<uint32_t>(ps, 0xc0) |= 0x10;
        return;
    }
    at<uint32_t>(pm, 0x130) = at<uint32_t>(pm, 0x134);
    at<uint32_t>(ps, 0xc0) |= 0x10;
    if (was_prone) return;
    if (pm[0x18] || pm[0x19]) {
        at<uint32_t>(pm_ps_ref(pm), 0x2c) &= 0xfffffbffu;
        at<uint32_t>(pm_ps_ref(pm), 0x2c) &= 0xffffffdfu;
    }
    // Newly prone: the ground under the player, traced up 0x0083eb98, back down to the origin, then 0.25 below.
    ps = pm_ps_ref(pm);
    const auto prone_trace = [&](const float* start, const float* end) {
        uint8_t* const p = pm_ps_ref(pm);
        title_function<Trace>(at<uint32_t>(pm, 0x134))(trace, start, &at<float>(pm, 0x100), &at<float>(pm, 0x110), end,
                                                      at<uint32_t>(p, 0x7c), at<uint32_t>(pm, 0x5c) & 0xfdff3fffu);
        pm = pm_global();
    };
    alignas(16) float up[4] = {at<float>(ps, 0), at<float>(ps, 4), at<float>(ps, 8) + title_global<float>(0x0083eb98),
                               at<float>(ps, 0)};
    prone_trace(&at<float>(ps, 0), up);
    alignas(16) float top[4] = {at<float>(trace, 0), at<float>(trace, 4), at<float>(trace, 8), at<float>(trace, 0)};
    prone_trace(top, &at<float>(pm_ps_ref(pm), 0));
    for (uint32_t i = 0; i < 3; ++i) at<float>(pm_ps_ref(pm), i * 4) = at<float>(trace, i * 4);
    ps = pm_ps_ref(pm);
    at<uint32_t>(ps, 0x450) = at<uint32_t>(ps, 0xa0);
    ps = pm_ps_ref(pm);
    alignas(16) float below[4] = {at<float>(ps, 0), at<float>(ps, 4), at<float>(ps, 8) - title_global<float>(0x0083ec24),
                                  at<float>(ps, 0)};
    prone_trace(&at<float>(ps, 0), below);
    if (!trace[0x3d] && title_global<float>(0x0083eb20) > at<float>(trace, 0x20)) {
        uint8_t* const p = pm_ps_ref(pm_global());
        at<float>(p, 0x454) = Plane_PitchAlongYaw(reinterpret_cast<const float*>(trace + 0x10), at<float>(p, 0x450));
    } else {
        at<float>(pm_ps_ref(pm_global()), 0x454) = 0.0f;
    }
    uint8_t* const p = pm_ps_ref(pm_global());
    float d = static_cast<float>(static_cast<int32_t>((at<float>(p, 0x454) - at<float>(p, 0x9c)) *
                                                      title_global<float>(0x0083ebd8)) & 0xffff) *
              title_global<float>(0x0083ebd4);
    if (d > title_global<float>(0x0083eb60)) d = d - title_global<float>(0x0083ebcc);  // 180, 360
    const float limit = title_global<float>(0x0083ef1c);
    if (title_global<float>(0x0083ef18) > d)
        at<float>(p, 0x458) = at<float>(p, 0x9c) - limit;
    else if (d > limit)
        at<float>(p, 0x458) = at<float>(p, 0x9c) + limit;
    else
        at<uint32_t>(p, 0x458) = at<uint32_t>(p, 0x454);
}

PLATFORM_ASM_ENTRY(Vec3_BendTowardAxis_entry,
                   "pushl %ebx\n"
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Vec3_BendTowardAxis) "addl $12, %esp\n"
                   "ret")

void XB_CDECL Vec3_BendTowardAxis(const float* axis, const float* point, float* out) {
    // ESI = axis, EDI = point, EBX = out (no caller: dead). The unit direction from the point to the
    // origin, bent toward the axis (half the axis added and renormalised per step) until its dot with the
    // axis reaches 0.3 (0.7 for an axis z above 0.8). A zero length leaves a vector unnormalised; the
    // axis is used as given (up only for a negative length, which never happens).
    float limit = title_global<float>(0x0083066c);  // 0.3
    const float zero = 0.0f;
    const float len = pm_ci_sqrt(platform_x87_ld((axis[0] * axis[0] + axis[1] * axis[1]) + axis[2] * axis[2]));
    float nx, ny, nz;
    if (platform_x87_greater(zero, len)) {
        nx = zero;
        ny = zero;
        nz = title_global<float>(0x0083eb20);
    } else {
        nx = axis[0];
        ny = axis[1];
        nz = axis[2];
    }
    float dy = zero - point[1];
    float dz = zero - point[2];
    float dx = zero - point[0];
    float l = pm_ci_sqrt(platform_x87_ld((dz * dz + dy * dy) + dx * dx));
    float vx, vy, vz;
    if (!x87_equal(l, title_global<float>(0x0083eafc))) {
        const float inv = title_global<float>(0x0083eb20) / l;
        vx = inv * dx;
        vy = inv * dy;
        vz = inv * dz;
    } else {
        vx = dx;
        vy = dy;
        vz = dz;
    }
    if (axis[2] > title_global<float>(0x0083ebc0)) limit = title_global<float>(0x0083ed28);  // 0.8 -> 0.7
    const float dot = (vz * nz + vy * ny) + vx * nx;
    if (limit > dot) {
        const float half = title_global<float>(0x0083eb24);
        const float hx = nx * half, hy = ny * half, hz = nz * half;
        float d;
        do {
            const float wx = hx + vx;
            const float wz = hz + vz;
            const float wy = hy + vy;
            float sx = wx, sy = wy, sz = wz;
            l = pm_ci_sqrt(platform_x87_ld((wz * wz + wy * wy) + wx * wx));
            if (!x87_equal(l, title_global<float>(0x0083eafc))) {
                const float inv = title_global<float>(0x0083eb20) / l;
                sx = inv * sx;
                sy = inv * sy;
                sz = inv * sz;
            }
            d = (sz * nz + sy * ny) + sx * nx;
            vx = sx;
            vy = sy;
            vz = sz;
        } while (limit > d);
    }
    out[0] = vx;
    out[1] = vy;
    out[2] = vz;
}

namespace {

// Runs a title callee at the original's ESP (top) and writes back the original call's return address.
uint32_t pm_call_at(uint32_t top, uint32_t ret, uint32_t function, const TitleRegs& regs, uint32_t count = 0,
                    const uint32_t* args = nullptr) {
    const uint32_t result = title_call_at_regs(top, function, &regs, count, args);
    *pointer<uint32_t>(top - count * 4 - 4) = ret;
    return result;
}

// An angle through the 16-bit short form (x * 65536/360, truncated, low 16 bits, back), in (-180, 180].
float pm_short_angle(float degrees) {
    const int32_t s = static_cast<int32_t>(degrees * title_global<float>(0x0083ebd8)) & 0xffff;
    float v = static_cast<float>(s) * title_global<float>(0x0083ebd4);
    if (v > title_global<float>(0x0083eb60)) v = v - title_global<float>(0x0083ebcc);
    return v;
}

}  // namespace

// The trace reads its vectors' fourth words, and the original reads one never-set local (B + 0xdc): the frame
// sits at the original's addresses (gap entry; B = aligned base - 0xe0), callees at the original's ESP.
PLATFORM_ASM_ENTRY(BG_CheckProneFeet_entry,
                   "movl %esp, %ecx\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %eax\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(BG_CheckProneFeet) "addl $24, %esp\n"
                   "popl %esp\n"
                   "ret")

uint32_t XB_CDECL BG_CheckProneFeet(uint32_t entry_esp, uint32_t eax, uint32_t ebx, uint32_t esi, uint32_t edi,
                                    uint32_t ebp) {
    // EAX = flag (contents 0x00820011 when set, else 0x01810011); cdecl (pass entity, origin, height, yaw,
    // out delta z, out front pitch, out back pitch, trace, back distance). A 12-unit box (+-6) is traced
    // from 24 units ahead along yaw - 180 (z +- height) and from back distance - 6 units along it (start
    // z + height, end the front foot's z - height); each foot is the hit point (else the trace end), 6
    // below unless the never-set local word's low bytes are set (then the origin z). Pitches from origin
    // to front foot and front to back foot (short-angle rounded); the back pitch is pulled to within
    // -50..70 of the front by the wrapped difference. Debug lines while 0x00950bcc. Returns 1.
    const uint32_t E = entry_esp;
    *pointer<uint32_t>(E - 4) = ebp;
    const uint32_t B = ((E - 4) & ~15u) - 0xe0;
    auto wd = [B](uint32_t off) -> uint32_t& { return *pointer<uint32_t>(B + off); };
    auto fl = [B](uint32_t off) -> float& { return *pointer<float>(B + off); };
    const uint32_t* const arg = pointer<uint32_t>(E + 4);
    const uint32_t pass = arg[0];
    const float* const origin = pointer<float>(arg[1]);
    float height, yaw, back_distance;
    std::memcpy(&height, &arg[2], 4);
    std::memcpy(&yaw, &arg[3], 4);
    float* const out_dz = pointer<float>(arg[4]);
    float* const out_front = pointer<float>(arg[5]);
    float* const out_back = pointer<float>(arg[6]);
    const uint32_t trace = arg[7];
    std::memcpy(&back_distance, &arg[8], 4);
    const uint32_t mask = (eax ? 0xff010000u : 0u) + 0x01810011u;
    wd(8) = ebx;
    wd(0x20) = mask;
    wd(4) = esi;
    wd(0) = edi;
    const float zero = 0.0f;
    wd(0xd0) = 0;
    wd(0xd4) = 0;
    if (out_dz) *out_dz = zero;
    if (out_front) *out_front = zero;
    if (out_back) *out_back = zero;
    const float six_neg = title_global<float>(0x0083f0ac), six = title_global<float>(0x0083ecd0);
    fl(0x80) = six_neg;
    fl(0x84) = six_neg;
    fl(0x88) = six_neg;
    fl(0x70) = six;
    fl(0x74) = six;
    fl(0x78) = six;
    fl(0x30) = zero;
    fl(0x34) = yaw - title_global<float>(0x0083eb60);
    fl(0x38) = zero;
    {
        const uint32_t args[2] = {B + 0x30, B + 0x24};
        pm_call_at(B, 0x002dc7be, own_address(&AnglesToAxis_entry), TitleRegs{B + 0x24, ebx, B + 0x30, 0, B + 0x94, B + 0x54}, 2,
                   args);  // AnglesToAxis (forward B+0x24, right B+0x54, up B+0x94)
    }
    const uint32_t org = address_of(origin);
    const auto foot = [&](uint32_t to, uint32_t ret, uint32_t line_ret, uint32_t top, const TitleRegs& regs) {
        const uint32_t args[7] = {B + 0xa0, B + 0x60, B + 0x80, B + 0x70, B + 0x30, pass, mask};
        pm_call_at(top, ret, trace, regs, 7, args);  // the trace callback
        float z;
        if (title_global<float>(0x0083eb20) > fl(0xc0)) {
            fl(to) = fl(0xa0);
            fl(to + 4) = fl(0xa4);
            z = fl(0xa8);
        } else {
            fl(to) = fl(0x30);
            fl(to + 4) = fl(0x34);
            z = fl(0x38);
        }
        const uint16_t flags = *pointer<uint16_t>(B + 0xdc);
        fl(to + 8) = z - six;
        if ((flags >> 8) || (flags & 0xff)) fl(to + 8) = origin[2];
        if (title_global<uint32_t>(0x00950bcc)) {
            const uint32_t line[6] = {B + 0x60, 0x0082eadc, title_global<uint32_t>(0x009509a4), 1, 1, 0};
            pm_call_at(B, line_ret, own_address(&DebugDraw_AddLine), TitleRegs{B + 0x30, org, 0, line[2], trace, mask}, 6,
                       line);  // DebugDraw_AddLine
        }
    };
    {
        const float reach = title_global<float>(0x0083ef04);  // 24
        const float px = fl(0x24) * reach + origin[0];
        const float py = fl(0x28) * reach + origin[1];
        const float pz = fl(0x2c) * reach + origin[2];
        fl(0x60) = px;
        fl(0x30) = px;
        fl(0x64) = py;
        fl(0x34) = py;
        fl(0x68) = pz + height;
        fl(0x38) = pz - height;
        foot(0x48, 0x002dc85c, 0x002dc909, B - 8, TitleRegs{B + 0x60, org, B + 0xa0, B + 0x80, trace, mask});
    }
    {
        const float d = back_distance - six;
        const float qx = d * fl(0x24) + origin[0];
        const float qz = d * fl(0x2c) + origin[2];
        const float qy = d * fl(0x28) + origin[1];
        fl(0x60) = qx;
        fl(0x30) = qx;
        fl(0x68) = qz + height;
        const float end_z = fl(0x50) - height;
        fl(0x64) = qy;
        fl(0x34) = qy;
        fl(0x38) = end_z;
        foot(0x54, 0x002dc99f, 0x002dca4c, B, TitleRegs{B + 0xa0, org, B + 0x80, B + 0x60, trace, mask});
    }
    fl(0x9c) = origin[2];
    fl(0x24) = origin[0] - fl(0x48);
    fl(0x28) = origin[1] - fl(0x4c);
    fl(0x2c) = fl(0x9c) - fl(0x50);
    const float front = pm_short_angle(Vec3_PitchDegrees(&fl(0x24)));
    fl(0x1c) = front;
    fl(0x24) = fl(0x48) - fl(0x54);
    fl(0x28) = fl(0x4c) - fl(0x58);
    fl(0x2c) = fl(0x50) - fl(0x5c);
    const float back = pm_short_angle(Vec3_PitchDegrees(&fl(0x24)));
    fl(0x18) = back;
    fl(0x20) = back;
    const float full = title_global<float>(0x0083ebcc);  // 360
    float r = static_cast<float>(pm_ci_fmod(platform_x87_ld(front) - platform_x87_ld(back),
                                            platform_x87_ld(title_global<double>(0x0083ed40))));
    fl(0x18) = r;
    *pointer<uint32_t>(B - 4) = 0x002dcb60;  // __CIfmod's return address (the last call)
    if (platform_x87_greater(r, title_global<float>(0x0083eb60))) r = r - full;
    if (title_global<float>(0x0083ebd0) > r) r = r + full;  // -180
    const float low = title_global<float>(0x0083f0a8), high = title_global<float>(0x0083eda0);  // -50, 70
    if (low > r)
        fl(0x20) = pm_short_angle(fl(0x1c) - low);
    else if (r > high)
        fl(0x20) = pm_short_angle(fl(0x1c) - high);
    if (out_dz) *out_dz = fl(0x9c) - origin[2];
    if (out_front) *out_front = fl(0x1c);
    if (out_back) *out_back = fl(0x20);
    return 1;
}

// The traces go through the caller's callbacks at the original's ESP with every vector in the original's frame
// (gap entry; B = aligned base - 0xf0). EDX keeps the entry ESP.
PLATFORM_ASM_ENTRY(BG_CheckProne_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %ecx\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(BG_CheckProne) "addl $24, %esp\n"
                   "popl %esp\n"
                   "ret")

uint32_t XB_CDECL BG_CheckProne(uint32_t entry_esp, uint32_t ecx, uint32_t ebx, uint32_t esi, uint32_t edi,
                                uint32_t ebp) {
    // ECX = skip the start check; cdecl (pass entity, origin, radius, height, yaw, out delta z, out front pitch,
    // out back pitch, ground check, ground normal, trace, start trace, flag, length). Box traces (mask 0x00820011
    // with the flag, else 0x01810031): without ECX a radius/height box straight up 10 from the origin
    // (start-solid fails); with the ground check a normal z below 0.7 fails. Then 6-unit boxes along yaw - 180:
    // from the origin at height - 6 out to length - 6 (a short hit fails without the ground check, or below
    // radius + 2; below (height - 6) * 0.7 + 24 it retries 22 higher, failing again below that), a step down
    // from 24 ahead (2.5 radius) onto ground of normal z >= 0.7 (the front point), from a short body hit a
    // midpoint retrace (18 higher on a hit) when the drop from front to body is within -0.75 x, then down
    // twice the front's height over the back (+ radius) for the back point and down 1.5 radius at the origin
    // (the mid point); the body pitch difference (Q_fmod of the two pitches) must be within -50..70 and both
    // 5-up lines clear. Success: out dz = front z - origin z - 6, the pitches (front to mid, mid to back, each
    // AngleNormalize180Quantized), 1. A failure after the first step returns 0 with the ground check, else 1 with
    // zero outputs. Debug lines while 0x00950bcc.
    const uint32_t E = entry_esp;
    *pointer<uint32_t>(E - 4) = ebp;
    const uint32_t B = ((E - 4) & ~15u) - 0xf0;
    auto wd = [B](uint32_t off) -> uint32_t& { return *pointer<uint32_t>(B + off); };
    auto fl = [B](uint32_t off) -> float& { return *pointer<float>(B + off); };
    const uint32_t* const arg = pointer<uint32_t>(E + 4);
    const uint32_t pass = arg[0];
    const float* const origin = pointer<float>(arg[1]);
    const uint32_t org = arg[1];
    float radius, height, yaw, length;
    std::memcpy(&radius, &arg[2], 4);
    std::memcpy(&height, &arg[3], 4);
    std::memcpy(&yaw, &arg[4], 4);
    float* const out_dz = pointer<float>(arg[5]);
    float* const out_front = pointer<float>(arg[6]);
    float* const out_back = pointer<float>(arg[7]);
    const uint32_t ground_check = arg[8];
    const float* const normal = pointer<float>(arg[9]);
    const uint32_t trace = arg[10];
    const uint32_t start_trace = arg[11];
    std::memcpy(&length, &arg[13], 4);
    const uint32_t mask = (arg[12] ? 0xff00ffe0u : 0u) + 0x01810031u;
    wd(8) = ebx;
    wd(4) = esi;
    wd(0) = edi;
    const float zero = 0.0f;
    const float six = title_global<float>(0x0083ecd0), one = title_global<float>(0x0083eb20);
    const float seven_tenths = title_global<float>(0x0083ed28);
    {
        const float neg = zero - radius;
        fl(0x74) = neg + origin[1];
        fl(0x78) = origin[2] + zero;
        fl(0x80) = origin[0] + radius;
        fl(0x70) = origin[0] + neg;
        fl(0x84) = origin[1] + radius;
        wd(0xd0) = 0;
        wd(0xd4) = 0;
        wd(0x90) = 0;
        fl(0x88) = origin[2] + height;
        wd(0x5c) = mask;
        if (!ecx) {
            fl(0x70) = neg;
            fl(0x74) = neg;
            fl(0x10) = origin[0];
            fl(0x14) = origin[1];
            fl(0x18) = origin[2];
            fl(0x20) = origin[0];
            fl(0x24) = origin[1];
            const float up = origin[2] + title_global<float>(0x0083eb98);  // 10
            fl(0x78) = zero;
            fl(0x80) = radius;
            fl(0x84) = radius;
            fl(0x88) = height;
            fl(0x28) = up;
            const uint32_t args[7] = {B + 0xa0, B + 0x10, B + 0x70, B + 0x80, B + 0x20, pass, mask};
            pm_call_at(B, 0x002dcdb9, start_trace, TitleRegs{B + 0x70, org, B + 0x10, B + 0xa0, 0, edi}, 7,
                       args);  // the start trace callback
            if (*pointer<uint8_t>(B + 0xdc)) return 0;
        }
    }
    if (ground_check && seven_tenths > normal[2]) return 0;
    const auto debug = [&]() { return title_global<uint32_t>(0x00950bcc) != 0; };
    // DebugDraw_AddLine (EAX = the first point; second point, colour, the 0x009509a4 time, 1, 1, 0).
    const auto line = [&](uint32_t ret, uint32_t first, uint32_t second, uint32_t color) {
        const uint32_t args[6] = {second, color, title_global<uint32_t>(0x009509a4), 1, 1, 0};
        pm_call_at(B, ret, own_address(&DebugDraw_AddLine), TitleRegs{first, org, 0, args[2], B + 0x44, B + 0x60}, 6, args);
    };
    const auto do_trace = [&](uint32_t ret, const TitleRegs& regs, uint32_t top = 0) {
        const uint32_t args[7] = {B + 0xa0, B + 0x10, B + 0x70, B + 0x80, B + 0x20, pass, mask};
        pm_call_at(top ? top : B, ret, trace, regs, 7, args);  // the trace callback
        return fl(0xc0);                                       // its fraction
    };
    const uint32_t red = 0x0082eadc, green = 0x0082eaec;
    const float six_neg = title_global<float>(0x0083f0ac);
    fl(0x70) = six_neg;
    fl(0x74) = six_neg;
    fl(0x78) = six_neg;
    fl(0x80) = six;
    fl(0x84) = six;
    fl(0x88) = six;
    {
        const float back_yaw = yaw - title_global<float>(0x0083eb60);  // 180
        const uint32_t args[2] = {B + 0x20, B + 0x60};
        fl(0x20) = zero;
        fl(0x24) = back_yaw;
        fl(0x28) = zero;
        pm_call_at(B, 0x002dce5b, own_address(&AnglesToAxis_entry), TitleRegs{ground_check ? arg[9] : 0, org, B + 0x60, B + 0x20,
                                                        B + 0x50, B + 0x44},
                   2, args);  // AnglesToAxis (forward B+0x60, right B+0x44, up B+0x50)
    }
    {
        const float body = height - six;
        const float sz = origin[2] + body;
        fl(0x98) = body;
        const float d = length - six;
        const float ex = d * fl(0x60) + origin[0];
        fl(0x10) = origin[0];
        float ey = d * fl(0x64);
        fl(0xec) = d;
        float ez = d * fl(0x68);
        ey = ey + origin[1];
        ez = ez + sz;
        fl(0x14) = origin[1];
        fl(0x18) = sz;
        fl(0x20) = ex;
        fl(0x24) = ey;
        fl(0x28) = ez;
        const float frac = do_trace(0x002dcf18, TitleRegs{B + 0xa0, org, B + 0x70, B + 0x10, B + 0x50, B + 0x44},
                                    B - 8);
        if (one > frac) {
            if (!ground_check) {
                if (debug()) line(0x002dcf68, B + 0xa0, B + 0x10, red);
                return 0;
            }
            float reach = fl(0xec) * frac + six;
            const float low = radius + title_global<float>(0x0083eba8);  // 2
            wd(0x90) = 1;
            fl(0x94) = reach;
            if (low > reach) {
                if (debug()) line(0x002dcfd5, B + 0xa0, B + 0x10, red);
                return 0;
            }
            const float enough = fl(0x98) * seven_tenths + title_global<float>(0x0083ef04);  // 24
            fl(0x6c) = enough;
            if (enough > reach) {
                if (debug()) line(0x002dd030, B + 0xa0, B + 0x10, red);
                const float dx = fl(0x20) - fl(0x10);
                const float z = fl(0x28) + title_global<float>(0x0083edd4);  // 22
                fl(0x44) = dx;
                const float dy = fl(0x24) - fl(0x14);
                fl(0x28) = z;
                const float dz = z - fl(0x18);
                wd(0x90) = 0;
                fl(0x48) = dy;
                fl(0x4c) = dz;
                const float len = VectorNormalize2(&fl(0x44), &fl(0x60));
                fl(0x9c) = len;
                const float frac2 = do_trace(0x002dd0c6, TitleRegs{B + 0x10, org, B + 0xa0, B + 0x70, B + 0x44,
                                                                   B + 0x60});
                if (one > frac2) {
                    reach = fl(0x9c) * frac2 + six;
                    wd(0x90) = 1;
                    fl(0x94) = reach;
                    if (fl(0x6c) > reach) {
                        if (debug()) line(0x002dd147, B + 0xa0, B + 0x10, red);
                        return 0;
                    }
                    if (debug()) line(0x002dd17e, B + 0xa0, B + 0x10, 0x0082eb1c);
                } else {
                    if (debug()) line(0x002dd1fa, B + 0xa0, B + 0x10, green);
                    fl(0x94) = length;
                }
            } else if (debug()) {
                line(0x002dd1c9, B + 0xa0, B + 0x10, 0x0082eb1c);
            }
        } else {
            if (debug()) line(0x002dd1fa, B + 0xa0, B + 0x10, green);
            fl(0x94) = length;
        }
    }
    // The front point: down from 24 ahead along the (possibly re-aimed) direction B+0x60.
    {
        const float reach = title_global<float>(0x0083ef04);  // 24
        fl(0x50) = fl(0xa0);
        fl(0x54) = fl(0xa4);
        fl(0x58) = fl(0xa8);
        const float x = fl(0x60) * reach + origin[0];
        fl(0x10) = x;
        fl(0x20) = x;
        float drop = radius * title_global<float>(0x0083eaf8);  // 2.5
        float z = fl(0x68) * reach + origin[2];
        const float y = fl(0x64) * reach + origin[1];
        const float body = fl(0x98);
        z = z + body;
        drop = drop + body;
        drop = drop - six;
        fl(0x18) = z;
        fl(0x14) = y;
        fl(0x24) = y;
        fl(0x6c) = drop;
        fl(0x28) = z - drop;
    }
    
    float frac = do_trace(0x002dd2f3, TitleRegs{B + 0x70, org, B + 0x10, B + 0xa0, mask, trace});
    if (frac == one) {
        if (debug()) line(0x002ddd2a, B + 0xa0, B + 0x10, red);
    } else {
        if (seven_tenths > fl(0xb8)) return 0;
        if (debug()) line(0x002dd352, B + 0xa0, B + 0x10, green);
        float a = fl(0x6c) * fl(0xc0);
        fl(0x38) = fl(0xa0);
        fl(0x3c) = fl(0xa4);
        a = a + six;
        fl(0x40) = fl(0xa8);
        float px, py, pz;
        bool done = false;
        if (wd(0x90)) {
            const float gap = fl(0x94) - a;
            if (a * title_global<float>(0x0083f0b0) > gap) {  // -0.75
                if (debug()) line(0x002ddd2a, B + 0x38, B + 0x50, red);
                done = true;
            } else {
                if (debug()) line(0x002dd415, B + 0x38, B + 0x50, 0x0082eb7c);
                const float dx = fl(0x50) - fl(0x38);
                const float dy = fl(0x54) - fl(0x3c);
                const float dz = fl(0x58) - fl(0x40);
                const float vx = fl(0x60) * six + dx;
                fl(0x48) = fl(0x64) * six + dy;
                const float vz = fl(0x68) * six + dz + six;
                fl(0x44) = vx;
                fl(0x4c) = vz;
                VectorNormalize(&fl(0x44));
                const float full = fl(0xec);
                const float d = full - title_global<float>(0x0083ef04);  // 24
                const float bx = d * fl(0x44) + fl(0x10);
                const float bz = d * fl(0x4c) + fl(0x18);
                const float by = d * fl(0x48) + fl(0x14);
                float fx = full * fl(0x60);
                float fy = full * fl(0x64);
                fx = fx + origin[0];
                fy = fy + origin[1];
                fl(0x28) = bz;
                const float half = title_global<float>(0x0083eb24);  // 0.5
                fx = fx + bx;
                fy = fy + by;
                fl(0x20) = fx * half;
                fl(0x24) = fy * half;
                bool hit = false;
                if (one > do_trace(0x002dd53a, TitleRegs{B + 0x70, org, B + 0x10, B + 0xa0, B + 0x44, trace})) {
                    if (debug()) line(0x002dd57e, B + 0xa0, B + 0x10, red);
                    const float lift = title_global<float>(0x0083eb00);  // 18
                    fl(0x10) = fl(0xa0);
                    fl(0x14) = fl(0xa4);
                    fl(0x18) = fl(0xa8) + lift;
                    fl(0x28) = fl(0x28) + lift;
                    hit = one > do_trace(0x002dd5f7, TitleRegs{B + 0x70, org, B + 0x10, B + 0xa0, B + 0x44, trace});
                }
                if (hit) {
                    if (debug()) line(0x002ddd2a, B + 0xa0, B + 0x10, red);
                    done = true;
                } else {
                    if (debug()) line(0x002dd667, B + 0xa0, B + 0x10, green);
                    px = fl(0xa0);
                    py = fl(0xa4);
                    pz = fl(0xa8);
                }
            }
        } else {
            px = fl(0x50);
            py = fl(0x54);
            pz = fl(0x58);
        }
        if (!done) {
            // The back point: down from the body point by twice the front's height over it, plus the radius.
            fl(0x10) = px;
            fl(0x20) = px;
            float drop = pz - fl(0x40);
            drop = drop * title_global<float>(0x0083eba8);  // 2
            drop = drop + radius;
            fl(0x18) = pz;
            fl(0x14) = py;
            fl(0x24) = py;
            fl(0x28) = pz - drop;
            frac = do_trace(0x002dd704, TitleRegs{B + 0xa0, org, B + 0x70, B + 0x10, mask, trace});
            if (frac == one) {
                if (debug()) line(0x002ddd2a, B + 0xa0, B + 0x10, red);
            } else {
                if (seven_tenths > fl(0xb8)) return 0;
                if (debug()) line(0x002dd78d, B + 0xa0, B + 0x10, green);
                // The mid point: down 1.5 radius at the origin.
                const float down = radius * title_global<float>(0x0083eaf4);  // 1.5
                fl(0x50) = fl(0xa0);
                fl(0x54) = fl(0xa4);
                fl(0x58) = fl(0xa8);
                fl(0x10) = origin[0];
                fl(0x14) = origin[1];
                fl(0x18) = origin[2] + fl(0x98);
                fl(0x20) = origin[0];
                fl(0x24) = origin[1];
                fl(0x28) = origin[2] - down;
                frac = do_trace(0x002dd840, TitleRegs{B + 0x10, org, B + 0xa0, B + 0x70, mask, trace});
                if (frac == one) {
                    if (debug()) line(0x002ddd2a, B + 0xa0, B + 0x10, red);
                } else {
                    if (seven_tenths > fl(0xb8)) return 0;
                    if (debug()) line(0x002dd8cb, B + 0xa0, B + 0x10, green);
                    const float mx = fl(0xa0), my = fl(0xa4), mz = fl(0xa8);
                    const float vx = fl(0x38) - mx;
                    fl(0x60) = mx;
                    fl(0x48) = fl(0x3c) - my;
                    const float vz = fl(0x40) - mz;
                    fl(0x64) = my;
                    fl(0x68) = mz;
                    fl(0x44) = vx;
                    fl(0x4c) = vz;
                    const float p1 = Vec3_PitchDegrees(&fl(0x44));
                    fl(0x6c) = p1;
                    fl(0x44) = fl(0x50) - fl(0x38);
                    fl(0x48) = fl(0x54) - fl(0x3c);
                    fl(0x4c) = fl(0x58) - fl(0x40);
                    const float p2 = Vec3_PitchDegrees(&fl(0x44));
                    fl(0x9c) = p2;
                    const float d = Q_fmod(p2, fl(0x6c));
                    if (title_global<float>(0x0083f0a8) > d || d > title_global<float>(0x0083eda0)) {  // -50, 70
                        if (debug()) {
                            line(0x002ddcfe, B + 0x38, B + 0x60, 0x0082eb4c);
                            if (debug()) line(0x002ddd2a, B + 0x50, B + 0x38, 0x0082eb4c);
                        }
                    } else {
                        // Both body lines 5 up must be clear.
                        const float five = title_global<float>(0x0083ece8);
                        fl(0x70) = zero;
                        fl(0x74) = zero;
                        fl(0x78) = zero;
                        fl(0x80) = zero;
                        fl(0x84) = zero;
                        fl(0x88) = zero;
                        fl(0x10) = fl(0xa0);
                        fl(0x14) = fl(0xa4);
                        fl(0x18) = fl(0xa8) + five;
                        fl(0x20) = fl(0x38);
                        fl(0x24) = fl(0x3c);
                        fl(0x28) = fl(0x40) + five;
                        if (one > do_trace(0x002dda6f, TitleRegs{B + 0x10, org, B + 0xa0, B + 0x70, mask, trace})) {
                            if (debug()) line(0x002ddd2a, B + 0x20, B + 0x10, red);
                        } else {
                            if (debug()) line(0x002ddad5, B + 0x20, B + 0x10, green);
                            fl(0x10) = fl(0x20);
                            fl(0x14) = fl(0x24);
                            fl(0x18) = fl(0x28);
                            fl(0x20) = fl(0x50);
                            fl(0x24) = fl(0x54);
                            fl(0x28) = fl(0x58) + five;
                            if (one > do_trace(0x002ddb51, TitleRegs{B + 0x70, org, B + 0x10, B + 0xa0, mask, trace})) {
                                if (debug()) line(0x002ddd2a, B + 0x20, B + 0x10, red);
                            } else {
                                if (debug()) {
                                    line(0x002ddbb6, B + 0x20, B + 0x10, green);
                                    if (debug()) {
                                        line(0x002ddbe2, B + 0x38, B + 0x60, 0x0082eb5c);
                                        if (debug()) line(0x002ddc0d, B + 0x50, B + 0x38, 0x0082eb5c);
                                    }
                                }
                                const float front_z = fl(0x68);
                                if (out_dz) *out_dz = front_z - origin[2] - six;
                                if (out_front) {
                                    fl(0x44) = fl(0x60) - fl(0x38);
                                    const float z = front_z - fl(0x40);
                                    fl(0x48) = fl(0x64) - fl(0x3c);
                                    fl(0x4c) = z;
                                    *out_front = AngleNormalize180Quantized(Vec3_PitchDegrees(&fl(0x44)));
                                }
                                if (out_back) {
                                    fl(0x44) = fl(0x38) - fl(0x50);
                                    fl(0x48) = fl(0x3c) - fl(0x54);
                                    fl(0x4c) = fl(0x40) - fl(0x58);
                                    *out_back = AngleNormalize180Quantized(Vec3_PitchDegrees(&fl(0x44)));
                                }
                                return 1;
                            }
                        }
                    }
                }
            }
        }
    }
    if (ground_check) return 0;
    if (out_dz) *out_dz = zero;
    if (out_front) *out_front = zero;
    if (out_back) *out_back = zero;
    return 1;
}

namespace {

// b > |a| and b >= |a| on the x87 (FABS, FCOMIP with b on top; false when unordered).
bool x87_greater_abs(float b, float a) {
    bool r;
    __asm__("flds %1\n\tfabs\n\tflds %2\n\tfcomip %%st(1), %%st\n\tfstp %%st(0)\n\tseta %0"
            : "=q"(r)
            : "m"(a), "m"(b)
            : "st", "st(1)", "cc");
    return r;
}
bool x87_greater_equal_abs(float b, float a) {
    bool r;
    __asm__("flds %1\n\tfabs\n\tflds %2\n\tfcomip %%st(1), %%st\n\tfstp %%st(0)\n\tsetae %0"
            : "=q"(r)
            : "m"(a), "m"(b)
            : "st", "st(1)", "cc");
    return r;
}

float bits_abs(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    bits &= 0x7fffffffu;
    std::memcpy(&f, &bits, 4);
    return f;
}

// x wrapped into [0, 360) by whole turns (the title's add/subtract loops).
float wrap360(float x) {
    const float full = title_global<float>(0x0083ebcc);
    if (0.0f > x) {
        do x = x + full;
        while (0.0f > x);
    } else if (x >= full) {
        do x = x - full;
        while (x >= full);
    }
    return x;
}

// The title's 16-bit angle step: (int)(a * 182.044) & 0xffff.
int32_t angle_short(float a) { return static_cast<int32_t>(a * title_global<float>(0x0083ebd8)) & 0xffff; }

// PM_CheckProneYaw (0x002e2e40: player state in EAX, yaw in XMM0, one cdecl word).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL pmva_check_prone_yaw(uint8_t* ps, float yaw, uint32_t trace);
PLATFORM_ASM_ENTRY(pmva_check_prone_yaw,
                   "movl 4(%esp), %eax\n"
                   "movss 8(%esp), %xmm0\n"
                   "pushl 12(%esp)\n"
                   PLATFORM_ASM_CALL(PM_CheckProneYaw_entry)
                   "addl $4, %esp\n"
                   "ret")

// BG_CheckProneFeet (0x002dc6f0) for the player at `yaw`: height 30, back distance 45, no outputs.
uint32_t pmva_feet_fit(uint8_t* ps, uint32_t yaw_bits, uint32_t trace) {
    using CheckProneFeet = uint32_t XB_REGS1 XB_CDECL(uint32_t, uint32_t, uint8_t*, uint32_t, uint32_t, uint32_t, uint32_t,
                                                      uint32_t, uint32_t, uint32_t);
    return own_function<CheckProneFeet>(&BG_CheckProneFeet_entry)(0, at<uint32_t>(ps, 0x7c), ps, 0x41f00000u, yaw_bits, 0, 0, 0, trace,
                                                      0x42340000u);
}

uint32_t float_bits(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

}  // namespace

void XB_REGS1 XB_CDECL PM_UpdateViewAngles(uint8_t* ps, const uint8_t* cmd, const uint8_t* old, uint32_t trace) {
    // Q3's PM_UpdateViewAngles with BRO's limits: pm_type 4+ only leans. The pitch delta (+0x54) keeps cmd pitch +
    // delta within +-16000 short units (+-12000 on a turret, +0xc0 0x100000; +-6000 when +0x4e8 = 2), then the view
    // angles (+0x9c) from cmd +8 + the deltas. On a turret the pm's arcs (pm +0x70, about its +0x64) clamp all
    // three; else, flag 0x10 without a ground entity +0x60, the yaw stays within 0x009352a4 degrees of the turned-
    // away direction +0x64. Prone (flag 1, or 0x20 aiming a mode-3 weapon; not +0xc0 0x106000) keeps the yaw within
    // 0x0092e95c (mode 3: 0x00927dfc) of the prone yaw +0x450, turning the body after it when the feet fit
    // (PM_CheckProneYaw, BG_CheckProneFeet; 55 degrees a second x 0x0095ffa4), the pitch within 45 of +0x458;
    // a stopped turn (flag 0x8000) eases a reversed turn back by 0.98. Then sticky aim and the lean (not pm_type
    // 2 / 3). 0x00950bcc draws the prone direction.
    if (at<int32_t>(ps, 0x24) >= 4) {
        const TitleRegs regs{address_of(cmd), 0, 0, 0, 0, address_of(ps)};
        g_ent_call(own_address(&PM_UpdateLean_entry), &regs, 1, &trace);  // PM_UpdateLean
        return;
    }
    const float scale = title_global<float>(0x0083ebd4);  // 360 / 65536
    const float half = title_global<float>(0x0083eb60);   // 180
    const float full = title_global<float>(0x0083ebcc);   // 360
    const float old_yaw = at<float>(ps, 0xa0);
    int16_t pitch = static_cast<int16_t>(at<uint16_t>(cmd, 8) + at<uint16_t>(ps, 0x54));
    int32_t limit = 0x3e80;
    if (at<uint32_t>(ps, 0xc0) & 0x100000) limit = at<int32_t>(ps, 0x4e8) == 2 ? 0x1770 : 0x2ee0;
    if (pitch > limit) {
        at<int32_t>(ps, 0x54) = limit - at<int32_t>(cmd, 8);
        pitch = static_cast<int16_t>(limit);
    } else if (pitch < -limit) {
        at<int32_t>(ps, 0x54) = -(at<int32_t>(cmd, 8) + limit);
        pitch = static_cast<int16_t>(-limit);
    }
    float* const view = &at<float>(ps, 0x9c);
    view[0] = static_cast<float>(pitch) * scale;
    view[1] = static_cast<float>(static_cast<int16_t>(at<uint16_t>(cmd, 0xc) + at<uint16_t>(ps, 0x58))) * scale;
    view[2] = static_cast<float>(static_cast<int16_t>(at<uint16_t>(ps, 0x5c) + at<uint16_t>(cmd, 0x10))) * scale;
    const float new_yaw = view[1];
    if (at<uint32_t>(ps, 0xc0) & 0x100000) {
        const uint8_t* const pmv = title_global<uint8_t*>(0x0092eff4);
        if (pmv) {
            const float* const arc = &at<float>(pmv, 0x70);
            if (!(arc[0] == 0.0f && arc[1] == 0.0f && arc[2] == 0.0f)) {
                for (uint32_t k = 0; k < 3; ++k) {
                    if (x87_greater_abs(title_global<float>(0x0083eb20), arc[k])) continue;  // under 1 degree: free
                    const float* const centre = &at<float>(pmv, 0x64 + k * 4);
                    const float d = short_angle_delta(*centre, view[k]);
                    if (!x87_abs_greater(d, bits_abs(arc[k]))) continue;
                    const float excess = d > arc[k] ? d - arc[k] : arc[k] + d;
                    at<int32_t>(ps, 0x54 + k * 4) += angle_short(excess);
                    view[k] = excess > 0.0f ? wrap360(*centre - arc[k]) : wrap360(arc[k] + *centre);
                }
            }
        }
    } else if ((at<uint8_t>(ps, 0x2c) & 0x10) && !at<uint32_t>(ps, 0x60) && title_global<int32_t>(0x009352a4)) {
        const int32_t arc = title_global<int32_t>(0x009352a4);
        const float away = vectoyaw(&at<float>(ps, 0x64)) + half;
        const float d = short_angle_delta(away, at<float>(ps, 0xa0));
        const float arc_f = static_cast<float>(arc);
        if (d > arc_f || static_cast<float>(-arc) > d) {
            const float excess = d > arc_f ? d - arc_f : arc_f + d;
            at<int32_t>(ps, 0x58) += angle_short(excess);
            const float arc_now = static_cast<float>(title_global<int32_t>(0x009352a4));
            at<float>(ps, 0xa0) = excess > 0.0f ? wrap360(away - arc_now) : wrap360(arc_now + away);
        }
    }
    if (!(at<uint32_t>(ps, 0xc0) & 0x106000)) {
        const uint32_t flags = at<uint32_t>(ps, 0x2c);
        const uint8_t* const wdef = title_global<uint8_t*>(title_global<uint32_t>(0x0095f4d8) + at<uint32_t>(ps, 0x80) * 4);
        if ((flags & 1) || ((flags & 0x20) && at<int32_t>(wdef, 0x98) == 3)) {
            if (title_global<uint32_t>(0x00950bcc)) {
                const float start[3] = {at<float>(ps, 0), at<float>(ps, 4),
                                        static_cast<float>(at<int32_t>(ps, 0x420)) + at<float>(ps, 8)};
                const float deg = title_global<float>(0x0083edd8);
                float ys, yc, pps, pc;
                FastSinCos(&ys, 0, &yc, at<float>(ps, 0xa0) * deg);
                FastSinCos(&pps, 0, &pc, at<float>(ps, 0x9c) * deg);
                const float len = title_global<float>(0x0083eb00);  // 18
                const float up = 0.0f - pps;
                float end[3];
                end[0] = (pc * yc) * len + start[0];
                end[1] = (pc * ys) * len + start[1];
                end[2] = up * len + start[2];
                DebugDraw_AddLine(end, start, &title_global<float>(0x0082eb9c), 1, 1, 1, 0);
            }
            float d = short_angle_delta(at<float>(ps, 0x450), at<float>(ps, 0xa0));
            uint32_t turned = 0;
            const int32_t mode = at<int32_t>(title_global<uint8_t*>(title_global<uint32_t>(0x0095f4d8) + at<uint32_t>(ps, 0x80) * 4), 0x98);
            float lim = static_cast<float>(title_global<int32_t>(0x0092e95c));
            if (mode == 3) lim = static_cast<float>(title_global<int32_t>(0x00927dfc));
            const uint32_t flags2 = at<uint32_t>(ps, 0x2c);
            if ((flags2 & 1) && !(mode == 3 && (flags2 & 0x20))) {
                const float lim5 = lim - title_global<float>(0x0083ece8);  // 5
                bool turn = d > lim5 || 0.0f - lim5 > d;
                if (!turn && (at<uint8_t>(cmd, 0x14) || at<uint8_t>(cmd, 0x15)) && !(d == 0.0f)) turn = true;
                if (turn) {
                    // Turn the body (prone yaw) toward the view by up to 55 degrees a second, then degree by degree.
                    const float step = platform_x87_mul(title_global<float>(0x0095ffa4), title_global<float>(0x0083f14c));
                    float target;
                    if (x87_greater_abs(step, d))
                        target = at<float>(ps, 0xa0);
                    else if (d > 0.0f)
                        target = at<float>(ps, 0x450) - step;
                    else
                        target = step + at<float>(ps, 0x450);
                    bool fit = pmva_check_prone_yaw(ps, target, trace) != 0;
                    uint32_t going = 1;
                    while (!fit && going) {
                        const float t = short_angle_delta(at<float>(ps, 0x450), target);
                        float s;
                        if (x87_abs_greater(t, title_global<float>(0x0083eb20))) {
                            going = 1;
                            s = t > 0.0f ? title_global<float>(0x0083eb20) : title_global<float>(0x0083eb34);  // 1 / -1
                        } else {
                            going = 0;
                            turned = 1;
                            s = t;
                        }
                        target = wrap360(target + s);
                        fit = pmva_check_prone_yaw(ps, target, trace) != 0;
                    }
                    if (fit) {
                        if (!pmva_feet_fit(ps, at<uint32_t>(ps, 0xa0), trace) || !pmva_feet_fit(ps, float_bits(target), trace))
                            turned = 1;
                        else
                            at<float>(ps, 0x450) = target;
                    }
                }
            }
            float d2 = short_angle_delta(at<float>(ps, 0x450), at<float>(ps, 0xa0));
            if (!(d2 == 0.0f) && (at<uint8_t>(ps, 0x2c) & 1) &&
                at<int32_t>(title_global<uint8_t*>(title_global<uint32_t>(0x0095f4d8) + at<uint32_t>(ps, 0x80) * 4), 0x98) != 3) {
                // The feet don't fit the prone yaw: bring the view toward it degree by degree.
                float target = at<float>(ps, 0x450);
                uint32_t going = 1;
                for (;;) {
                    const uint32_t fit = pmva_feet_fit(ps, float_bits(target), trace);
                    if (fit && pmva_check_prone_yaw(ps, target, trace)) {
                        at<float>(ps, 0x450) = target;
                        break;
                    }
                    if (!going) break;
                    float s;
                    if (x87_abs_greater(d2, title_global<float>(0x0083eb20))) {
                        going = 1;
                        s = d2 > 0.0f ? title_global<float>(0x0083eb20) : title_global<float>(0x0083eb34);
                    } else {
                        going = 0;
                        s = d2;
                    }
                    at<int32_t>(ps, 0x58) += angle_short(s);
                    turned = 1;
                    const float yaw = wrap360(s + at<float>(ps, 0xa0));
                    at<float>(ps, 0xa0) = yaw;
                    d2 = short_angle_delta(at<float>(ps, 0x450), yaw);
                    if (!fit) target = wrap360(d2 + target);
                }
            }
            if (d2 > lim || 0.0f - lim > d2) {
                const float excess = d2 > lim ? d2 - lim : lim + d2;
                at<int32_t>(ps, 0x58) += angle_short(excess);
                at<float>(ps, 0xa0) = excess > 0.0f ? wrap360(at<float>(ps, 0x450) - lim) : wrap360(at<float>(ps, 0x450) + lim);
            }
            if (turned) {
                at<uint32_t>(ps, 0x2c) |= 0x8000;
                const float back = short_angle_delta(old_yaw, at<float>(ps, 0xa0));
                if (x87_greater_equal_abs(title_global<float>(0x0083eb20), back)) {
                    const float ahead = short_angle_delta(new_yaw, at<float>(ps, 0xa0));
                    if (ahead * back > 0.0f) {
                        const float s = back * title_global<float>(0x0083f148);  // 0.98
                        const float yaw = wrap360(s + at<float>(ps, 0xa0));
                        at<int32_t>(ps, 0x58) += angle_short(s);
                        at<float>(ps, 0xa0) = yaw;
                    }
                }
            }
            const float dp = short_angle_delta(at<float>(ps, 0x458), at<float>(ps, 0x9c));
            const float pitch_arc = title_global<float>(0x0083ef1c);  // 45
            if (dp > pitch_arc || title_global<float>(0x0083ef18) > dp) {
                const float excess = dp > pitch_arc ? dp - pitch_arc : dp + pitch_arc;
                at<int32_t>(ps, 0x54) += angle_short(excess);
                float x = excess > 0.0f ? at<float>(ps, 0x458) - pitch_arc : at<float>(ps, 0x458) + pitch_arc;
                const float low = title_global<float>(0x0083ebd0);  // -180
                if (low >= x) {
                    do x = x + full;
                    while (low >= x);
                } else if (x > half) {
                    do x = x - full;
                    while (x > half);
                }
                at<float>(ps, 0x9c) = x;
            }
        }
    }
    using StickyAim = void XB_REGS3 XB_CDECL(uint32_t, uint32_t, uint8_t*, const uint8_t*, const uint8_t*);
    own_function<StickyAim>(&PM_UpdateStickyAim)(0, 0, ps, cmd, old);  // PM_UpdateStickyAim
    const int32_t type = at<int32_t>(ps, 0x24);
    if (type != 3 && type != 2) {
        const TitleRegs regs{address_of(cmd), 0, 0, 0, address_of(cmd), address_of(ps)};
        g_ent_call(own_address(&PM_UpdateLean_entry), &regs, 1, &trace);  // PM_UpdateLean
    }
}

namespace {

// The title's packed length: lane 0 = sqrt of (x*x + y*y) + z*z by RSQRTSS and one Newton step
// (((3 - a * r) * a) * 0.5, a = len2 * r); a 0 or NaN square keeps the square.
float sticky_length(const Lanes& v) {
    const Lanes sq = mul(v, v);
    const float len2 = (sq.v[0] + sq.v[1]) + sq.v[2];
    if (len2 == title_global<float>(0x0082e680) || len2 != len2) return len2;  // UCOMISS + JNE
    const float r = platform_rsqrt_estimate(len2);
    const float a = len2 * r;
    return ((title_global<float>(0x0082e660) - a * r) * a) * title_global<float>(0x0082e670);  // 3, 0.5
}

const uint8_t* entity_of_handle(uint32_t handle) {
    const uint32_t index = handle & 0xfff;
    if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
        return title_global<const uint8_t*>(0x0097a968 + index * 8);
    return nullptr;
}

Lanes const_lanes(uint32_t address) { return lanes(&title_global<float>(address)); }

void sticky_line(const Lanes& end, const Lanes& start, uint32_t color, int32_t frames) {
    DebugDraw_AddLine(end.v, start.v, &title_global<float>(color), 1, frames, 1, 0);
}

}  // namespace

namespace {
bool g_aim_assist = true;
}  // namespace

void set_aim_assist(bool on) { g_aim_assist = on; }

void XB_REGS3 XB_CDECL PM_UpdateStickyAim(uint32_t, uint32_t, uint8_t* ps, const uint8_t* cmd, const uint8_t* old) {
    if (!g_aim_assist) {
        at<uint32_t>(ps, 0x500) = 0;
        return;
    }
    // Player state in ECX. Aim assist: the eye ray (view angles) against each active squad-1 actor's vertical
    // plane facing the eye (RayPlaneIntersectFrac); a hit nearer than the best so far (first the client's +0x5fc +
    // 35) inside a box growing with distance (radius (s + 1) * 25, height 70 / 60 crouched / 30 prone times s * 0.1
    // + 1, down to -5 x that; s = distance / 410 in 0..2) is the target. While locked (+0x500, entity +0x504, offset
    // +0x508) on a target (not mode-8 weapons), the aim moves 0.35..0.65 (by distance 200..750) of the way from the
    // ray's hit toward the locked point and the view angles (and deltas) follow when the command turned or moved
    // or +0x88 is in (0, 1). A target beyond 130 locks (again), else the lock ends. 0x00929b50 +0x20 = 1 draws it.
    const float* const view = &at<float>(ps, 0x9c);
    const Lanes eye{{at<float>(ps, 0), at<float>(ps, 4), at<float>(ps, 0xac) + at<float>(ps, 8), at<float>(ps, 0)}};
    const float deg = title_global<float>(0x0083edd8);
    float ys, yc, pps, pc;
    FastSinCos(&ys, 0, &yc, view[1] * deg);
    FastSinCos(&pps, 0, &pc, view[0] * deg);
    const float fx = pc * yc;
    const Lanes fwd{{fx, pc * ys, 0.0f - pps, fx}};
    uint8_t found = 0;
    const uint8_t* best = nullptr;
    Lanes best_hit{};
    const uint8_t* const self = entity_of_handle(at<uint32_t>(ps, 0x7c));
    float range = at<float>(at<uint8_t*>(self, 0x23c), 0x5fc) + title_global<float>(0x0083f09c);  // 35
    const bool draw = at<int32_t>(title_global<uint8_t*>(0x00929b50), 0x20) == 1;
    for (uint32_t slot = 0x00985384; slot < 0x00985404; slot += 4) {
        const uint8_t* const actor = title_global<uint8_t*>(slot);
        if (!actor || at<int32_t>(actor, 0x1360) < 0 || at<int32_t>(at<uint8_t*>(actor, 4), 4) != 1) continue;
        const uint8_t* const e = at<uint8_t*>(actor, 0);
        const Lanes pos{{at<float>(e, 0x140), at<float>(e, 0x144), at<float>(e, 0x148), at<float>(e, 0x140)}};
        alignas(16) Lanes d = sub(eye, pos);
        const float dist = sticky_length(d);
        d.v[2] = 0.0f;
        alignas(16) const Lanes pos_a = pos, eye_a = eye, fwd_a = fwd;
        const float t = RayPlaneIntersectFrac(pos_a.v, d.v, eye_a.v, fwd_a.v);
        if (!platform_x87_greater(t, title_global<float>(0x0083eafc)) || !(range > t)) continue;
        const Lanes hit = add(eye, mul(fwd, t));
        Lanes off = sub(hit, pos);
        const float off_z = off.v[2];
        off.v[2] = 0.0f;
        const float flat = sticky_length(off);
        float s = dist * title_global<float>(0x0083f098);  // 1 / 410
        if (0.0f > s)
            s = 0.0f;
        else if (s > title_global<float>(0x0083eba8))  // 2
            s = title_global<float>(0x0083eba8);
        const float one = title_global<float>(0x0083eb20);
        const float grow = s * title_global<float>(0x0083ebf8) + one;  // 0.1
        const float radius = (s + one) * title_global<float>(0x0083ebc4);  // 25
        float top = grow * title_global<float>(0x0083eda0);  // 70
        const float bottom = grow * title_global<float>(0x0083f094);  // -5
        const uint32_t stance = at<uint32_t>(at<uint8_t*>(e, 0x240), 0x184);
        if (stance == title_global<uint32_t>(0x0097a764))  // prone
            top = grow * title_global<float>(0x0083ee38);  // 30
        else if (stance == title_global<uint32_t>(0x0097a674))  // crouch
            top = grow * title_global<float>(0x0083eb3c);  // 60
        if (top > off_z && off_z > bottom && radius > flat) {
            found = 1;
            best_hit = hit;
            best = e;
            range = t;
        }
        if (!draw) continue;
        // The target box: up / down edges and the side vector (d turned 90 degrees, scaled to the radius).
        const Lanes up = mul(const_lanes(0x0082e180), top);
        const Lanes down{{0.0f, 0.0f, bottom, 0.0f}};
        const Lanes turn = mul(Lanes{{d.v[1], d.v[0], d.v[2], d.v[3]}}, const_lanes(0x0082e830));
        const float side_len = sticky_length(turn);
        const Lanes side = mul(turn, radius / side_len);
        const Lanes top_c = add(pos, up);
        sticky_line(add(top_c, side), sub(top_c, side), 0x0082eaec, 0);
        sticky_line(add(add(pos, side), down), add(add(pos, up), side), 0x0082eaec, 0);
        sticky_line(add(sub(pos, side), down), add(add(pos, side), down), 0x0082eaec, 0);
        const Lanes left = sub(pos, side);
        sticky_line(add(left, up), add(left, down), 0x0082eaec, 0);
    }
    const uint8_t* const wdef = title_global<uint8_t*>(title_global<uint32_t>(0x0095f4d8) + at<uint32_t>(ps, 0x80) * 4);
    const bool allow = !(wdef && at<int32_t>(wdef, 0x98) == 8);
    Lanes aim_at = best_hit;
    const uint8_t* const locked = at<uint32_t>(ps, 0x500) ? entity_of_handle(at<uint32_t>(ps, 0x504)) : nullptr;
    if (locked) {
        if (!found) {
            at<uint32_t>(ps, 0x500) = 0;
            return;
        }
        if (allow) {
            const Lanes target{{at<float>(locked, 0x140), at<float>(locked, 0x144), at<float>(locked, 0x148),
                                at<float>(locked, 0x140)}};
            const Lanes aim = add(target, lanes(&at<float>(ps, 0x508)));
            const Lanes v = sub(aim, best_hit);
            float frac = title_global<float>(0x0083f064);  // 0.35
            if (range > title_global<float>(0x0083ef08)) {  // 200
                if (range > title_global<float>(0x0083f090))  // 750
                    frac = title_global<float>(0x0083f08c);  // 0.65
                else
                    frac = (range - title_global<float>(0x0083ef08)) * title_global<float>(0x0083f088) + title_global<float>(0x0083f064);
            }
            if (draw) {
                using Va = const char* XB_CDECL(const char*, double);
                const char* const text = own_function<Va>(&va_entry)(&title_global<char>(0x00836890), frac);  // "%.2f"
                DebugDraw_AddString(&title_global<float>(0x0082eb9c), &at<float>(best, 0x140), from_bits(0x3f333333u),  // 0.7
                                    text, 1);
            }
            const Lanes next = add(best_hit, mul(v, frac));
            const Lanes to = sub(next, eye);
            const Lanes sq = mul(to, to);
            const float len2 = (sq.v[0] + sq.v[1]) + sq.v[2];
            float inv;
            if (len2 == title_global<float>(0x0083eafc)) {  // UCOMISS + JNP: only an exact 0
                inv = title_global<float>(0x0082e6a0);
            } else {
                const float a = abs_bits(len2);
                const float r = platform_rsqrt_estimate(a);
                inv = ((title_global<float>(0x0082e660) - (r * r) * a) * r) * title_global<float>(0x0082e670);
            }
            alignas(16) const Lanes dir = mul(to, inv);
            aim_at = next;
            if (draw) sticky_line(next, aim, 0x0082eb1c, 0);
            const bool steered = at<int32_t>(cmd, 0xc) != at<int32_t>(old, 0xc) || at<int32_t>(cmd, 8) != at<int32_t>(old, 8) ||
                                 !(static_cast<float>(at<int8_t>(cmd, 0x14)) == 0.0f) ||
                                 !(static_cast<float>(at<int8_t>(cmd, 0x15)) == 0.0f) ||
                                 (at<float>(ps, 0x88) > 0.0f && title_global<float>(0x0083eb20) > at<float>(ps, 0x88));
            if (steered) {
                float angles[3];
                vectoangles_2(dir.v, angles);
                const float scale = title_global<float>(0x0083ebd8);
                at<int32_t>(ps, 0x54) += static_cast<int32_t>((angles[0] - at<float>(ps, 0x9c)) * scale) & 0xffff;
                const int32_t yaw_step = static_cast<int32_t>((angles[1] - at<float>(ps, 0xa0)) * scale) & 0xffff;
                const int32_t roll_step = static_cast<int32_t>((angles[2] - at<float>(ps, 0xa4)) * scale) & 0xffff;
                at<int32_t>(ps, 0x58) += yaw_step;
                at<int32_t>(ps, 0x5c) += roll_step;
                at<float>(ps, 0x9c) = angles[0];
                at<float>(ps, 0xa0) = angles[1];
                at<float>(ps, 0xa4) = angles[2];
            }
        }
    }
    if (found && range > title_global<float>(0x0083ef3c)) {  // 130
        at<uint32_t>(ps, 0x500) = 1;
        at<uint32_t>(ps, 0x504) = at<uint32_t>(best, 0x224);
        const Lanes target{{at<float>(best, 0x140), at<float>(best, 0x144), at<float>(best, 0x148), at<float>(best, 0x140)}};
        const Lanes off = sub(aim_at, target);
        at<float>(ps, 0x508) = off.v[0];
        at<float>(ps, 0x50c) = off.v[1];
        at<float>(ps, 0x510) = off.v[2];
        return;
    }
    at<uint32_t>(ps, 0x500) = 0;
}

}  // namespace game
