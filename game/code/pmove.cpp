#include "pmove.h"

#include <cstring>

#include "actor.h"
#include "bg_misc.h"
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

}  // namespace game
