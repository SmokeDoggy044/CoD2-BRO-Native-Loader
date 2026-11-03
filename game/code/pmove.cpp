#include "pmove.h"

#include <cstring>

#include "actor.h"
#include "bg_misc.h"
#include "qcommon_fs.h"
#include "cgame.h"
#include "g_vehicle.h"
#include "g_main.h"
#include "g_utils.h"
#include "lanes.h"
#include "cm.h"
#include "fx.h"
#include "qcommon.h"
#include "sv.h"
#include "title_data.h"

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
                if (title_function<StrICmp>(0x001f1443)(at<const char*>(def, 0x714), at<const char*>(other, 8))) continue;
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
    for (; *text; ++text) *text = static_cast<char>(title_function<ToLower>(0x001eed48)(static_cast<int8_t>(*text)));  // tolower
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
    g_ent_call(0x002d62c0, &regs, 0, nullptr);  // BG_EvaluateTrajectory
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
    const uint32_t r = title_function<CheckProneFeet>(0x002dc6f0)(0, at<uint32_t>(ps, 0x7c), ps, 0x41f00000u,
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
    const uint32_t id = g_ent_call(0x002a63d0, &regs, 3, args);  // FXCmd_PlayWeaponSoundLoop
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
    return title_function<CheckProneFeet>(0x002dc6f0)(0, at<uint32_t>(ps, 0x7c), ps, 0x41f00000u, x_word, ps + 0x4d8,
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
        prone = title_function<CheckProneFeet>(0x002dc6f0)(0, at<uint32_t>(ps, 0x7c), ps, 0x41f00000u, at<uint32_t>(ps, 0xa0),
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
    float wishspeed = pm_ci_sqrt(static_cast<long double>((wy * wy + wx * wx) + wz * wz));
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
    const float speed = pm_ci_sqrt(static_cast<long double>((vx * vx + vy * vy) + vz * vz));
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
        const float speed = pm_ci_sqrt(static_cast<long double>((vx * vx + vy * vy) + vz * vz));
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
    const float length = pm_ci_sqrt(static_cast<long double>(x87_sum_squares_zyx(move[0], move[1], move[2])));
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
        const uint32_t r = title_function<CheckProneFeet>(0x002dc6f0)(0, at<uint32_t>(ps, 0x7c), ps, 0x41f00000u,
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
    title_function<PlayerTrace>(0x002ea190)(trace, &at<float>(ps, 0), mins, maxs, end, at<uint32_t>(ps, 0x7c),
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
    title_function<PlayerTrace>(0x002ea190)(trace, &at<float>(q, 0), mins, maxs, end, at<uint32_t>(q, 0x7c),
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
    float mins[3], maxs[3];
    mins[0] = at<float>(pm, 0x100) * k;
    mins[1] = at<float>(pm, 0x104) * k;
    mins[2] = at<float>(pm, 0x108) * k;
    maxs[0] = at<float>(pm, 0x110) * k;
    maxs[1] = at<float>(pm, 0x114) * k;
    maxs[2] = at<float>(pm, 0x118) * title_global<float>(0x0083ec18);  // 0.9
    title_function<PlayerTrace>(0x002ea190)(trace, &at<float>(ps, 0), mins, maxs, &at<float>(ps, 0), at<uint32_t>(ps, 0x7c),
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
    title_function<CapsuleTrace>(0x002e95b0)(address_of(lists->triangles), address_of(lists->brushes), results, start,
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
        title_function<PlayerTrace>(0x002ea190)(trace, start, &at<float>(p, 0x100), &at<float>(p, 0x110), end,
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
    title_function<PlayerTrace>(0x002ea190)(trace, &at<float>(ps, 0), mins, maxs, end, at<uint32_t>(ps, 0x7c),
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
        if (at<float>(p, 0x88) > title_global<float>(0x0083ec18)) anim = 0x17;  // 0.9
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
    float length = pm_ci_sqrt(static_cast<long double>((f1 * f1 + f0 * f0) + zero_sq));
    const float one = title_global<float>(0x0083eb20);
    if (!x87_equal(length, title_global<float>(0x0083eafc))) {
        const float inv = one / length;
        forward[0] = forward[0] * inv;
        forward[1] = forward[1] * inv;
        forward[2] = inv * title_global<float>(0x0083eafc);
    }
    length = pm_ci_sqrt(static_cast<long double>(x87_sum_squares_yx_plus(right[0], right[1], zero_sq)));
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
    float wishspeed = pm_ci_sqrt(static_cast<long double>((w1 * w1 + w0 * w0) + zero_sq));
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
    title_function<void XB_CDECL(uint32_t)>(0x002ec330)(1);  // PM_StepSlideMove
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
        title_function<void XB_CDECL(uint8_t*, uint32_t)>(0x002f1590)(pm, arg);  // PmoveSingle
        if (at<uint8_t>(pm_ps_ref(pm), 0x2c) & 8) pm[0x1a] = 0x14;
    }
    Player_RecordEyeHistory(pm_ps_ref(pm));
    title_global<uint8_t*>(0x0092eff4) = nullptr;
}

}  // namespace game
