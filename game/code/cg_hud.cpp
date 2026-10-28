#include "cg_hud.h"

#include <cstring>

#include "actor.h"
#include "fe.h"
#include "g_utils.h"
#include "qcommon.h"
#include "ngl.h"
#include "title_data.h"

extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL title_call_regs(uint32_t function, uint32_t eax, uint32_t esi,
                                                            uint32_t edi);

namespace game {

namespace {

template <typename T>
T& field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

uint8_t* object_at(uint8_t* base, uint32_t offset) { return field<uint8_t*>(base, offset); }

// A title object's vtable slot (byte offset), thiscall.
template <typename F>
F* method(uint8_t* object, uint32_t offset) {
    return title_function<F>(field<uint32_t>(field<uint8_t*>(object, 0), offset));
}

using VoidMethod = void XB_THISCALL(uint8_t*);
using BoolMethod = uint8_t XB_THISCALL(uint8_t*);
using WordMethod = void XB_THISCALL(uint8_t*, uint32_t);

void call(uint8_t* object, uint32_t offset) { method<VoidMethod>(object, offset)(object); }
// Draws a UI element (vtable +0x44; +0x3c for texts).
void draw(uint8_t* object) { call(object, 0x44); }
void draw_text(uint8_t* object) { call(object, 0x3c); }
// The deleting destructor (vtable +0) with flag 1.
void destroy(uint8_t* object) { method<WordMethod>(object, 0)(object, 1); }

bool visible(uint8_t* self) { return field<uint8_t>(self, 4) != 0; }
bool shown(uint8_t* self) { return method<BoolMethod>(self, 0x10)(self) != 0; }

}  // namespace

void XB_THISCALL HudCompTankIcon_Draw(uint8_t* self) {
    if (!visible(self)) return;
    draw(object_at(self, 8));
    draw(object_at(self, 0xc));
}

void XB_THISCALL HudCompTankHealth_Draw(uint8_t* self) {
    if (!visible(self) || !object_at(self, 0xc)) return;
    draw(object_at(self, 0xc));
    draw(object_at(self, 8));
    draw(object_at(self, 0x10));
}

void XB_THISCALL HudCompWeaponName_Draw(uint8_t* self) {
    if (!shown(self) || field<uint8_t>(self, 0x14)) return;
    draw_text(object_at(self, 8));
    draw(object_at(self, 0xc));
}

void XB_THISCALL HudCompGrenadeCook_Draw(uint8_t* self) {
    if (!visible(self) || field<uint8_t>(self, 0x2d)) return;
    for (uint32_t i = 0; i < 6; ++i) draw(object_at(self, 8 + i * 4));
    draw(object_at(self, 0x20));
}

void XB_THISCALL HudCompGrenades_Draw(uint8_t* self) {
    if (!visible(self) || field<uint8_t>(self, 0x18)) return;
    draw(object_at(self, 8));
    draw(object_at(self, 0xc));
    draw_text(object_at(self, 0x10));
}

void XB_THISCALL HudCompAmmo_Draw(uint8_t* self) {
    if (!visible(self) || field<uint8_t>(self, 0x1c)) return;
    draw(object_at(self, 8));
    draw_text(object_at(self, 0xc));
    draw_text(object_at(self, 0x10));
}

void XB_THISCALL HudCompHealth_Draw(uint8_t* self) {
    if (!visible(self)) return;
    if (field<float>(self, 0x18) > title_global<float>(0x0083eafc)) draw(object_at(self, 8));  // 0
    draw(object_at(self, 0xc));
    draw(object_at(self, 0x10));
    if (field<uint8_t>(self, 0x20)) draw(object_at(self, 0x14));
}

void XB_THISCALL HudCompStance_Draw(uint8_t* self) {
    if (!visible(self)) return;
    draw(object_at(self, 8 + static_cast<uint32_t>(field<int32_t>(self, 0x24)) * 8));
    if (field<uint8_t>(self, 0x30)) draw(object_at(self, 0xc + static_cast<uint32_t>(field<int32_t>(self, 0x24)) * 8));
}

void XB_THISCALL HudCompHint_Draw(uint8_t* self) {
    if (!shown(self) || field<uint8_t>(self, 0x224)) return;
    const int32_t button = field<int32_t>(self, 0x21c);
    if (button >= 0 && button < 0x84) draw(object_at(self, 8 + static_cast<uint32_t>(button) * 4));
    draw_text(object_at(self, 0x218));
}

void XB_THISCALL HudCompTankReticle_Draw(uint8_t* self) {
    if (!visible(self) || !object_at(self, 8)) return;
    if (!(title_global<uint32_t>(0x00972610) & 0x100000) || title_global<int32_t>(0x00972a38) == 1) return;
    draw(object_at(self, 8));
    if (field<int32_t>(self, 0x40) < 0) return;
    for (uint32_t i = 0; i < 4; ++i) draw(object_at(self, 0xc + i * 4));
}

void XB_THISCALL HudManager_Detach(uint8_t* self, uint32_t) {
    uint8_t* const component = object_at(self, 0x3c);
    field<uint32_t>(self, 0x44) = 0;
    if (component) destroy(component);
    field<uint32_t>(self, 0x3c) = 0;
}

void XB_THISCALL HudCompIconCount_SetWidescreen(uint8_t* self, uint32_t widescreen, float center_x) {
    if (!object_at(self, 0xc)) return;
    UIPicture_ScaleXForAspect(widescreen, object_at(self, 8), center_x);
    UIPicture_ScaleXForAspect(widescreen, object_at(self, 0xc), center_x);
}

uint8_t* XB_REGS1 XB_CDECL HudCompMeter_Ctor(uint8_t* self) {
    field<uint8_t>(self, 4) = 1;
    field<uint32_t>(self, 0) = 0x008299b8;  // HudCompMeter vtable
    field<uint32_t>(self, 8) = 0;
    for (uint32_t i = 0; i < 12; ++i) {
        field<uint32_t>(self, 0xc + i * 8) = 0;
        field<uint32_t>(self, 0x10 + i * 8) = 0;
    }
    field<uint32_t>(self, 0xe4) = 0;
    field<uint32_t>(self, 0xe8) = 0;
    field<float>(self, 0xec) = 0.0f;
    return self;
}

void XB_REGS1 XB_STDCALL HudCompMeter_SetCount(uint8_t* self, uint32_t count) {
    if (!object_at(self, 8)) return;
    // The flag goes out as the count's argument word with its low byte replaced (the original
    // stores the byte into that slot and pushes the whole word).
    for (int32_t i = 0; i < 12; ++i) {
        const uint8_t on = i < static_cast<int32_t>(count) ? 1 : 0;
        uint8_t* const first = object_at(self, 0xc + static_cast<uint32_t>(i) * 8);
        method<WordMethod>(first, 0x34)(first, (count & 0xffffff00u) | on);
        uint8_t* const second = object_at(self, 0x10 + static_cast<uint32_t>(i) * 8);
        method<WordMethod>(second, 0x34)(second, (count & 0xffffff00u) | (on ? 0u : 1u));
    }
}

PLATFORM_ASM_ENTRY(HudCompGrenadeCook_Dtor_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(HudCompGrenadeCook_Dtor) "addl $4, %esp\n"
                   "ret")

void XB_CDECL HudCompGrenadeCook_Dtor(uint8_t* self) {
    field<uint32_t>(self, 0) = 0x0082a5bc;  // HudCompGrenadeCook vtable
    for (uint32_t i = 0; i < 6; ++i)
        if (uint8_t* const segment = object_at(self, 8 + i * 4)) destroy(segment);
    if (uint8_t* const center = object_at(self, 0x20)) destroy(center);
    field<uint32_t>(self, 0) = 0x008286fc;  // HudComponent vtable
}

namespace {

void z_free(void* p) { title_function<void XB_CDECL(void*)>(0x000428d0)(p); }  // Z_Free

// The deleting destructors' shared body: the class vtable, its owned UI elements deleted in
// order, the base vtable 0x008286fc, then Z_Free with flag bit 0. Returns this.
uint8_t* delete_component(uint8_t* self, uint32_t vtable, const uint32_t* children, uint32_t count, uint32_t flags) {
    field<uint32_t>(self, 0) = vtable;
    for (uint32_t i = 0; i < count; ++i)
        if (uint8_t* const child = object_at(self, children[i])) destroy(child);
    field<uint32_t>(self, 0) = 0x008286fc;  // HudComponent vtable
    if (flags & 1) z_free(self);
    return self;
}

void set_visible(uint8_t* object, uint32_t on) { method<WordMethod>(object, 0x14)(object, on); }

// Component visibility per HUD slot (+4 .. +0x40) for the layouts; -1 = untouched.
void apply_layout(uint8_t* manager, const int8_t* visible, uint8_t slot1_flag) {
    set_visible(object_at(manager, 4), static_cast<uint32_t>(visible[0]));
    field<uint8_t>(object_at(manager, 4), 0x3c) = slot1_flag;
    for (uint32_t slot = 2; slot <= 0x10; ++slot) {
        const int8_t v = visible[slot - 1];
        if (v < 0) continue;
        set_visible(object_at(manager, slot * 4), static_cast<uint32_t>(v));
    }
}

}  // namespace

uint8_t* XB_THISCALL HudCompHealth_DeletingDtor(uint8_t* self, uint32_t flags) {
    static const uint32_t kChildren[1] = {0x14};
    return delete_component(self, 0x008290a8, kChildren, 1, flags);
}

uint8_t* XB_THISCALL HudCompAmmo_DeletingDtor(uint8_t* self, uint32_t flags) {
    static const uint32_t kChildren[2] = {0xc, 0x10};
    return delete_component(self, 0x00828ddc, kChildren, 2, flags);
}

uint8_t* XB_THISCALL HudCompGrenades_DeletingDtor(uint8_t* self, uint32_t flags) {
    static const uint32_t kChildren[3] = {0x10, 0x8, 0xc};
    return delete_component(self, 0x008287e4, kChildren, 3, flags);
}

uint8_t* XB_THISCALL HudCompWeaponName_DeletingDtor(uint8_t* self, uint32_t flags) {
    static const uint32_t kChildren[2] = {0x8, 0xc};
    return delete_component(self, 0x00829d28, kChildren, 2, flags);
}

uint8_t* XB_THISCALL HudCompHint_DeletingDtor(uint8_t* self, uint32_t flags) {
    static const uint32_t kChildren[1] = {0x218};
    return delete_component(self, 0x00829d0c, kChildren, 1, flags);
}

uint8_t* XB_THISCALL HudManager_DeletingDtor(uint8_t* self, uint32_t flags) {
    title_function<void XB_THISCALL(uint8_t*)>(0x00319f20)(self);  // HudManager_Dtor
    if (flags & 1) z_free(self);
    return self;
}

uint8_t* XB_THISCALL HudCompCompass_DeletingDtor(uint8_t* self, uint32_t flags) {
    title_call_regs(0x00318380, 0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self)), 0);  // HudCompCompass_Dtor (ESI)
    if (flags & 1) z_free(self);
    return self;
}

void XB_THISCALL HudCompIconCount_Draw(uint8_t* self) {
    // While visible with icons (+0xc): count (+0x10, one more with the highlight flag +0x14)
    // icons in rows of 8 from (580, 410) leftwards/upwards 24 apart; the highlighted index
    // (+0x18) uses the second picture (+0xc), the others the first (+8). Vtable +0x8c = position.
    using PositionMethod = void XB_THISCALL(uint8_t*, float, float);
    if (!visible(self) || !object_at(self, 0xc)) return;
    int32_t count = field<int32_t>(self, 0x10);
    if (field<uint8_t>(self, 0x14)) ++count;
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t n = static_cast<uint32_t>(i);
        const float x = static_cast<float>(static_cast<int32_t>(0x244 - (n % 8) * 24));
        const float y = static_cast<float>(static_cast<int32_t>(0x19a - (n / 8) * 24));
        const uint32_t which = field<uint8_t>(self, 0x14) && i == field<int32_t>(self, 0x18) ? 0xc : 8;
        uint8_t* const picture = object_at(self, which);
        method<PositionMethod>(picture, 0x8c)(picture, x, y);
        draw(object_at(self, which));
    }
}

void XB_THISCALL HudCompBomberMap_Draw(uint8_t* self) {
    // While visible with a map (+0x30): in a pushed scene without clears or depth, the map and
    // its ten markers (+8..+0x2c); then in a second one +0x34, +0x38, +0x3c.
    if (!visible(self) || !object_at(self, 0x30)) return;
    nglPushScene(0);
    nglSetClearFlags(0);
    nglSetSceneDepthTest(0);
    nglSetSceneDepthEnable(0);
    draw(object_at(self, 0x30));
    for (uint32_t i = 0; i < 10; ++i) draw(object_at(self, 8 + i * 4));
    nglPopScene();
    nglPushScene(0);
    nglSetClearFlags(0);
    nglSetSceneDepthTest(0);
    nglSetSceneDepthEnable(0);
    draw(object_at(self, 0x34));
    draw(object_at(self, 0x38));
    draw(object_at(self, 0x3c));
    nglPopScene();
}

void XB_THISCALL HudCompStance_Update(uint8_t* self, uint32_t) {
    // The stance (prone 2, crouch 1, else 0 from the player flags 0x0097257c & 0x10003); for a
    // second after it changed (time +0x28, flags +0x2c) its highlight (+0xc + stance * 8) fades
    // (vtable +0x78 = alpha, 0.8 at the change) and +0x30 is set.
    using AlphaMethod = void XB_THISCALL(uint8_t*, float);
    if (!visible(self)) return;
    const int32_t now = title_global<int32_t>(0x0097253c);
    if (field<int32_t>(self, 0x28) > now ||
        field<uint32_t>(self, 0x2c) != (title_global<uint32_t>(0x0097257c) & 0x10003))
        field<int32_t>(self, 0x28) = now;
    const uint32_t flags = title_global<uint32_t>(0x0097257c) & 0x10003;
    field<uint32_t>(self, 0x2c) = flags;
    field<int32_t>(self, 0x24) = (flags & 1) ? 2 : (flags & 2) ? 1 : 0;
    const int32_t start = field<int32_t>(self, 0x28);
    const int32_t time = title_global<int32_t>(0x0097253c);
    if (start + 1000 > time) {
        uint8_t* const picture = object_at(self, 0xc + static_cast<uint32_t>(field<int32_t>(self, 0x24)) * 8);
        const float alpha = static_cast<float>(start - time + 1000) * title_global<float>(0x0083eb4c) *
                            title_global<float>(0x0083ebc0);  // 0.001, 0.8
        method<AlphaMethod>(picture, 0x78)(picture, alpha);
        field<uint8_t>(self, 0x30) = 1;
        return;
    }
    field<uint8_t>(self, 0x30) = 0;
}

PLATFORM_ASM_ENTRY(HudManager_SetComponentColor_entry,
                   "pushl %edi\n"
                   "pushl %edx\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(HudManager_SetComponentColor) "addl $12, %esp\n"
                   "ret")

void XB_CDECL HudManager_SetComponentColor(uint8_t* manager, int32_t component, int32_t color) {
    // Component slot (+8 + index * 4) colour 0..3 (vtable +0x70): green, yellow-green, yellow,
    // red as D3DCOLOR 0xAARRGGBB; other values red.
    static const uint32_t kColors[4] = {0xff00ff00u, 0xff7dff00u, 0xffffff00u, 0xffff0000u};
    uint8_t* const target = object_at(manager, 8 + static_cast<uint32_t>(component) * 4);
    if (!target) return;
    const uint32_t value = color >= 0 && color <= 3 ? kColors[color] : kColors[3];
    method<WordMethod>(target, 0x70)(target, value);
}

// Slot visibility per layout, slots 1..16 (+4 .. +0x40): 1 shown, 0 hidden, -1 untouched.
PLATFORM_ASM_ENTRY(HudManager_Layout2_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(HudManager_Layout2) "addl $4, %esp\n"
                   "ret")

void XB_CDECL HudManager_Layout2(uint8_t* manager) {
    static const int8_t kVisible[16] = {1, 1, 1, 0, 0, 1, 1, 1, -1, 0, 0, 0, 0, 0, -1, -1};
    apply_layout(manager, kVisible, 1);
}

PLATFORM_ASM_ENTRY(HudManager_HideAll_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(HudManager_HideAll) "addl $4, %esp\n"
                   "ret")

void XB_CDECL HudManager_HideAll(uint8_t* manager) {
    static const int8_t kVisible[16] = {0, 0, 0, 0, 0, 0, 0, 0, -1, 0, 0, 0, 0, 0, -1, 0};
    apply_layout(manager, kVisible, 0);
}

PLATFORM_ASM_ENTRY(HudManager_LayoutDefault_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(HudManager_LayoutDefault) "addl $4, %esp\n"
                   "ret")

void XB_CDECL HudManager_LayoutDefault(uint8_t* manager) {
    static const int8_t kVisible[16] = {1, 1, 1, 1, 1, 1, 0, 0, -1, 0, 0, 1, 1, 0, -1, 0};
    apply_layout(manager, kVisible, 0);
    set_visible(object_at(manager, 0x40), 1);  // slot 16 hidden, then shown again
}

void XB_REGS1 XB_CDECL HudManager_Layout3(uint8_t* manager) {
    // The default layout with slots 2, 3, 12, 13 hidden and 10, 11, 14 shown (slot n = +4n).
    HudManager_LayoutDefault(manager);
    set_visible(object_at(manager, 0x8), 0);
    set_visible(object_at(manager, 0xc), 0);
    set_visible(object_at(manager, 0x28), 1);
    set_visible(object_at(manager, 0x2c), 1);
    set_visible(object_at(manager, 0x30), 0);
    set_visible(object_at(manager, 0x34), 0);
    set_visible(object_at(manager, 0x38), 1);
}

void XB_REGS3 XB_CDECL HudManager_SetLayoutMode(uint32_t mode, uint32_t, uint8_t* manager) {
    // A new layout mode (+0x5c): 0 default, 1 none, 2 and 3 the alternatives; others only stored.
    if (mode == field<uint32_t>(manager, 0x5c)) return;
    field<uint32_t>(manager, 0x5c) = mode;
    switch (mode) {
    case 0: HudManager_LayoutDefault(manager); break;
    case 1: HudManager_HideAll(manager); break;
    case 2: HudManager_Layout2(manager); break;
    case 3: HudManager_Layout3(manager); break;
    default: break;
    }
}

// ---- The compass (HudCompCompass, vtable 0x0082884c) ----
// +8 the needle, +0xc / +0x10 the frame pictures (+0x10 also gives the centre: vtable +0x9c / +0xa0),
// +0x14 the actor marker, +0x18 / +0x30 friendly / enemy tank, +0x1c the pulse ring, +0x20 / +0x24 / +0x28
// objective above / below / level, +0x2c firing enemy; +0x34 needle speed, +0x38 needle yaw, +0x3c only
// objectives. Markers (0x28 bytes: x, y, alpha, shown +0xc, time +0x10, yaw +0x14, position +0x18, tank
// picture +0x24): 32 actors +0x40, 32 firing enemies +0x540, 25 tanks +0xe28; 16 objective markers
// +0x1210 (0x1c bytes: x, y, alpha, shown +0xc, pulse alpha +0x10 / scale +0x14, pulsing +0x18, above
// +0x19, below +0x1a); +0x13d0 / +0x13d2 hide the objective with that index.

extern "C" PLATFORM_HIDDEN float XB_CDECL cg_vectoyaw(const float* v);

namespace {

const float& gf(uint32_t address) { return title_global<float>(address); }

int32_t cg_time() { return title_global<int32_t>(0x0097253c); }

float abs_bits(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, 4);
    bits &= 0x7fffffffu;
    std::memcpy(&value, &bits, 4);
    return value;
}

float bits_float(uint32_t bits) {
    float value;
    std::memcpy(&value, &bits, 4);
    return value;
}

// The title CRT's __CIsqrt of an x87 value, stored as float.
float title_ci_sqrt(long double x) {
    float result;
    __asm__ volatile("fldt %1\n\tcall *%2\n\tfstps %0"
                     : "=m"(result)
                     : "m"(x), "r"(uintptr_t{0x0033acf4})
                     : "eax", "ecx", "edx", "st", "memory");
    return result;
}

using PositionMethod = void XB_THISCALL(uint8_t*, uint32_t, uint32_t);
using RotationMethod = void XB_THISCALL(uint8_t*, float, uint32_t);
using PairMethod = void XB_THISCALL(uint8_t*, uint32_t, uint32_t);
using GetMethod = long double XB_THISCALL(uint8_t*);
using FitMethod = float XB_THISCALL(uint8_t*, float, float, uint32_t);
using SizeMethod = void XB_THISCALL(uint8_t*, uint32_t, float);

void reset_rotation(uint8_t* p) { call(p, 0x90); }
void set_position(uint8_t* p, uint32_t x, uint32_t y) { method<PositionMethod>(p, 0x8c)(p, x, y); }
void set_alpha(uint8_t* p, uint32_t alpha) { method<WordMethod>(p, 0x78)(p, alpha); }
void set_rotation(uint8_t* p, float radians) { method<RotationMethod>(p, 0x4c)(p, radians, 1); }

// The widescreen needle: vtable +0xa0 (0.75, 1, 1) fits it, +0x50 (50, fit) sizes it.
void fit_needle(uint8_t* compass) {
    uint8_t* const needle = object_at(compass, 8);
    const float fit = method<FitMethod>(needle, 0xa0)(needle, 0.75f, 1.0f, 1);
    uint8_t* const again = object_at(compass, 8);
    method<SizeMethod>(again, 0x50)(again, 0x42480000u, fit);  // 50.0
}

// An angle in degrees through the 16-bit angle (182.044 per degree, & 0xffff, 360 / 65536).
float short_angle(float degrees) {
    const int32_t n = static_cast<int32_t>(degrees * gf(0x0083ebd8)) & 0xffff;
    return static_cast<float>(n) * gf(0x0083ebd4);
}

uint8_t* const* entity_list_begin() { return &title_global<uint8_t* const>(0x0097d370); }
uint32_t entity_list_end() { return 0x0097d370u + static_cast<uint32_t>(title_global<int32_t>(0x0097d36c)) * 4; }

// The yaw the markers are placed against: the needle's (0x00948db4 set) or the view's.
float marker_reference(uint8_t* compass) {
    return title_global<int32_t>(0x00948db4) ? field<float>(compass, 0x38) : gf(0x00972bf4);
}

// Packed snapshot position offsets (9 bits each, * 4 - 1020) as floats.
void unpack_offset(uint32_t packed, float* out) {
    const int32_t x = ((static_cast<int32_t>(packed) >> 6) & 0x1ff) * 4 - 0x3fc;
    const int32_t y = ((static_cast<int32_t>(packed) >> 15) & 0x1ff) * 4 - 0x3fc;
    out[0] = static_cast<float>(x);
    out[1] = static_cast<float>(y);
}

bool offset_is_edge(const float* v) {
    return v[0] == gf(0x0083eff8) || v[0] == gf(0x0083f0b8) || v[1] == gf(0x0083eff8) || v[1] == gf(0x0083f0b8);
}

// An edge offset is a direction: normalized (__CIsqrt length, none when 0) into position +0x18 / +0x1c, and
// +0x20 gets the never-set stack word the original keeps there.
void store_direction(uint8_t* marker, const float* v, uint32_t stale) {
    const float length = title_ci_sqrt(static_cast<float>(static_cast<long double>(v[1]) * v[1] +
                                                          static_cast<long double>(v[0]) * v[0]));
    float x = v[0];
    float y = v[1];
    if (length != 0.0f) {
        const float inv = gf(0x0083eb20) / length;
        x = inv * v[0];
        y = inv * v[1];
    }
    field<float>(marker, 0x1c) = y;
    field<float>(marker, 0x18) = x;
    field<uint32_t>(marker, 0x20) = stale;
}

// The timed markers (time +0x10) of one list placed for 800 ms: a position within 1 of the origin is a
// direction (fixed distance 0x00947cd0), else relative to the view origin (0x00972bb0) with its distance.
void place_timed_markers(uint8_t* compass, uint32_t first, int32_t count, float* v) {
    const float reference = marker_reference(compass);
    for (int32_t i = 0; i < count; ++i) {
        uint8_t* const marker = compass + first + static_cast<uint32_t>(i) * 0x28;
        field<uint8_t>(marker, 0xc) = 0;
        if (field<int32_t>(marker, 0x10) > cg_time()) field<int32_t>(marker, 0x10) = 0;
        if (field<int32_t>(marker, 0x10) < cg_time() - 800) continue;
        const float one = gf(0x0083eb20);
        float angle;
        float distance;
        if (platform_x87_greater_equal(one, abs_bits(field<float>(marker, 0x18))) &&
            platform_x87_greater_equal(one, abs_bits(field<float>(marker, 0x1c)))) {
            angle = short_angle(cg_vectoyaw(&field<float>(marker, 0x18)) - reference);
            distance = gf(0x00947cd0);
        } else {
            v[0] = field<float>(marker, 0x18) - gf(0x00972bb0);
            v[1] = field<float>(marker, 0x1c) - gf(0x00972bb4);
            angle = short_angle(cg_vectoyaw(v) - reference);
            distance = title_ci_sqrt(v[1] * v[1] + v[0] * v[0]);
        }
        HudCompCompass_PlaceMarker(compass, marker, distance, angle, gf(0x0083eb34), 0);  // pulse -1
    }
}

// Draws one marker list (vtable +0x90 reset, +0x8c position, +0x4c rotation, +0x78 alpha).
void draw_markers(uint8_t* compass, uint32_t first, uint32_t picture_offset) {
    for (uint32_t i = 0; i < 32; ++i) {
        uint8_t* const marker = compass + first + i * 0x28;
        if (!field<uint8_t>(marker, 0xc)) continue;
        uint8_t* const picture = object_at(compass, picture_offset);
        reset_rotation(picture);
        const float angle = short_angle(gf(0x00972bf4) - field<float>(marker, 0x14));
        set_position(picture, field<uint32_t>(marker, 0), field<uint32_t>(marker, 4));
        set_rotation(picture, angle * gf(0x0083ed00) / gf(0x0083eb60));  // pi / 180
        set_alpha(picture, field<uint32_t>(marker, 8));
        draw(picture);
    }
}

}  // namespace

uint8_t* XB_REGS2 XB_CDECL HudCompCompass_Ctor(uint32_t, uint8_t* self) {
    for (uint32_t offset = 8; offset <= 0x30; offset += 4) field<uint32_t>(self, offset) = 0;
    field<uint8_t>(self, 0x3c) = 0;
    field<uint8_t>(self, 4) = 1;
    field<uint32_t>(self, 0) = 0x0082884c;  // HudCompCompass vtable
    field<float>(self, 0x34) = 0.0f;
    field<float>(self, 0x38) = 0.0f;
    std::memset(self + 0x40, 0, 0x500);
    std::memset(self + 0x540, 0, 0x500);
    std::memset(self + 0xa40, 0, 0x3e8);
    std::memset(self + 0xe28, 0, 0x3e8);
    std::memset(self + 0x1210, 0, 0x1c0);
    field<uint16_t>(self, 0x13d0) = 0;
    field<uint16_t>(self, 0x13d2) = 0;
    field<uint32_t>(self, 0x13d4) = 0;
    field<uint32_t>(self, 0x13d8) = 0;
    return self;
}

PLATFORM_ASM_ENTRY(HudCompCompass_Dtor_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(HudCompCompass_Dtor) "addl $4, %esp\n"
                   "ret")

void XB_CDECL HudCompCompass_Dtor(uint8_t* self) {
    static const uint32_t kChildren[8] = {0x14, 0x18, 0x2c, 0x30, 0x20, 0x24, 0x28, 0x1c};
    field<uint32_t>(self, 0) = 0x0082884c;  // HudCompCompass vtable
    for (uint32_t i = 0; i < 8; ++i)
        if (uint8_t* const child = object_at(self, kChildren[i])) destroy(child);
    field<uint32_t>(self, 0) = 0x008286fc;  // HudComponent vtable
}

void XB_THISCALL HudCompCompass_SetWidescreen(uint8_t* self, uint32_t widescreen, float center_x) {
    reset_rotation(object_at(self, 8));
    set_rotation(object_at(self, 8), field<float>(self, 0x38) * gf(0x0083edd8));  // pi / 180
    if (uint8_t(widescreen)) fit_needle(self);
    UIPicture_ScaleXForAspect(widescreen, object_at(self, 0xc), center_x);
    UIPicture_ScaleXForAspect(widescreen, object_at(self, 0x10), center_x);
}

void XB_THISCALL HudCompCompass_Draw(uint8_t* self) {
    if (!visible(self)) return;
    if (field<uint8_t>(self, 0x3c)) {
        HudCompCompass_DrawMarkerList(self);
        return;
    }
    draw(object_at(self, 8));
    draw(object_at(self, 0xc));
    draw(object_at(self, 0x10));
    HudCompCompass_DrawActorMarkers(self);
    if (title_global<int32_t>(0x0094d7d4) == 1) HudCompCompass_DrawFiringEnemies(self);
    HudCompCompass_DrawTankMarkers(self);
    HudCompCompass_DrawMarkerList(self);
}

PLATFORM_ASM_ENTRY(HudCompCompass_DrawMarkerList_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(HudCompCompass_DrawMarkerList) "addl $4, %esp\n"
                   "ret")

void XB_CDECL HudCompCompass_DrawMarkerList(uint8_t* self) {
    for (uint32_t i = 0; i < 16; ++i) {
        uint8_t* const marker = self + 0x1210 + i * 0x1c;
        if (!field<uint8_t>(marker, 0xc)) continue;
        uint8_t* const picture = field<uint8_t>(marker, 0x19)   ? object_at(self, 0x20)
                                 : field<uint8_t>(marker, 0x1a) ? object_at(self, 0x24)
                                                                : object_at(self, 0x28);
        if (picture) {
            set_position(picture, field<uint32_t>(marker, 0), field<uint32_t>(marker, 4));
            set_alpha(picture, field<uint32_t>(marker, 8));
            draw(picture);
        }
        if (!field<uint8_t>(marker, 0x18)) continue;
        reset_rotation(object_at(self, 0x1c));
        set_alpha(object_at(self, 0x1c), field<uint32_t>(marker, 0x10));
        set_position(object_at(self, 0x1c), field<uint32_t>(marker, 0), field<uint32_t>(marker, 4));
        uint8_t* const ring = object_at(self, 0x1c);
        method<PairMethod>(ring, 0x5c)(ring, field<uint32_t>(marker, 0x14), 1);
        draw(object_at(self, 0x1c));
    }
}

void XB_REGS1 XB_CDECL HudCompCompass_DrawTankMarkers(uint8_t* self) {
    for (uint32_t i = 0; i < 25; ++i) {
        uint8_t* const marker = self + 0xe28 + i * 0x28;
        if (!field<uint8_t>(marker, 0xc) || !object_at(marker, 0x24)) continue;
        const float angle = short_angle(gf(0x00972bf4) - field<float>(marker, 0x14));
        reset_rotation(object_at(marker, 0x24));
        set_position(object_at(marker, 0x24), field<uint32_t>(marker, 0), field<uint32_t>(marker, 4));
        set_rotation(object_at(marker, 0x24), (angle + gf(0x0083eb64)) * gf(0x0083ed00) / gf(0x0083eb60));  // +90
        set_alpha(object_at(marker, 0x24), field<uint32_t>(marker, 8));
        draw(object_at(marker, 0x24));
    }
}

PLATFORM_ASM_ENTRY(HudCompCompass_DrawActorMarkers_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(HudCompCompass_DrawActorMarkers) "addl $4, %esp\n"
                   "ret")

void XB_CDECL HudCompCompass_DrawActorMarkers(uint8_t* self) { draw_markers(self, 0x40, 0x14); }

PLATFORM_ASM_ENTRY(HudCompCompass_DrawFiringEnemies_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(HudCompCompass_DrawFiringEnemies) "addl $4, %esp\n"
                   "ret")

void XB_CDECL HudCompCompass_DrawFiringEnemies(uint8_t* self) { draw_markers(self, 0x540, 0x2c); }

PLATFORM_ASM_ENTRY(HudCompCompass_UpdateMarkerPulse_entry,
                   "subl $4, %esp\n"
                   "movss %xmm2, (%esp)\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(HudCompCompass_UpdateMarkerPulse) "addl $8, %esp\n"
                   "ret")

void XB_CDECL HudCompCompass_UpdateMarkerPulse(uint8_t* marker, float start) {
    field<uint8_t>(marker, 0x18) = 0;
    const int32_t pulses = title_global<int32_t>(0x0094bd2c);
    if (pulses <= 0) return;
    const int32_t duration = title_global<int32_t>(0x0094ac44);
    if (duration <= 0) return;
    if (gf(0x0083eb34) >= start) return;  // -1
    const float now = static_cast<float>(cg_time());
    const float total = static_cast<float>(duration);
    if (now >= total + start) return;
    const float period = total / static_cast<float>(pulses);
    float t = now - start;
    int32_t passed = 0;
    while (t > period) {
        t = t - period;
        ++passed;
    }
    t = t / period;
    const float half = gf(0x0083eb24);
    if (t > half)
        field<float>(marker, 0x10) = gf(0x0083eb20) - (t - half) * gf(0x0083eba8);
    else
        field<float>(marker, 0x10) = t * gf(0x0083eba8);
    field<float>(marker, 0x14) = t;
    if (passed) field<float>(marker, 0x14) = t * half;
    field<uint8_t>(marker, 0x18) = 1;
}

PLATFORM_ASM_ENTRY(HudCompCompass_PlaceMarker_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n"
                   "pushl %esi\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(HudCompCompass_PlaceMarker) "addl $24, %esp\n"
                   "ret $12")

void XB_CDECL HudCompCompass_PlaceMarker(uint8_t* compass, uint8_t* marker, float distance, float angle,
                                         float pulse_start, uint32_t pulse) {
    // The distance clamped to the far (0x00949080) .. near (0x00947cd0) cvars gives the alpha (1 at near
    // .. 0x009486f0 at far), clamped to near .. 0x00949620 the radius (0x0094ab20 .. 1 times 0x0094bc08 x
    // 43.75); placed about the frame's centre (vtable +0x9c / +0xa0) at the angle (x scaled 0.75 in
    // widescreen, 0x00948f64).
    const int32_t far_limit = static_cast<int32_t>(gf(0x00949080));
    const int32_t near_limit = static_cast<int32_t>(gf(0x00947cd0));
    const int32_t inner_limit = static_cast<int32_t>(gf(0x00949620));
    const int32_t inner_scale = static_cast<int32_t>(gf(0x0094ab20));
    const int32_t far_alpha = static_cast<int32_t>(gf(0x009486f0));
    const float one = gf(0x0083eb20);
    float clamped = distance;
    if (distance > static_cast<float>(far_limit))
        clamped = static_cast<float>(far_limit);
    else if (static_cast<float>(near_limit) > distance)
        clamped = static_cast<float>(near_limit);
    float alpha = 0.0f;
    const float alpha_range = static_cast<float>(far_limit - near_limit);
    if (alpha_range != 0.0f)
        alpha = (clamped - static_cast<float>(near_limit)) / alpha_range * (static_cast<float>(far_alpha) - one) + one;
    float inner = distance;
    if (distance > static_cast<float>(near_limit))
        inner = static_cast<float>(near_limit);
    else if (static_cast<float>(inner_limit) > distance)
        inner = static_cast<float>(inner_limit);
    float radius = 0.0f;
    const float radius_range = static_cast<float>(near_limit - inner_limit);
    if (radius_range != 0.0f) {
        const float low = static_cast<float>(inner_scale);
        radius = ((inner - static_cast<float>(inner_limit)) / radius_range * (one - low) + low) * gf(0x0094bc08) *
                 gf(0x0083f020);
    }
    float sine;
    float cosine;
    FastSinCos(&sine, 0, &cosine, angle * gf(0x0083ed00) * gf(0x0083ecfc));  // pi, 1 / 180
    const float scale = title_global<int32_t>(0x00948f64) ? gf(0x0083eb58) : one;
    uint8_t* const centre = object_at(compass, 0x10);
    const long double cx = method<GetMethod>(centre, 0x9c)(centre);
    field<float>(marker, 0) = static_cast<float>(cx - static_cast<long double>(scale) * sine * radius);
    uint8_t* const centre2 = object_at(compass, 0x10);
    const long double cy = method<GetMethod>(centre2, 0xa0)(centre2);
    field<float>(marker, 8) = alpha;
    field<uint8_t>(marker, 0xc) = 1;
    field<float>(marker, 4) = static_cast<float>(cy - static_cast<long double>(cosine) * radius);
    if (uint8_t(pulse)) HudCompCompass_UpdateMarkerPulse(marker, pulse_start);
}

void XB_THISCALL HudCompCompass_Update(uint8_t* self, uint32_t) {
    // The needle and the actor / firing-enemy (0x0094d7d4 == 1) / tank markers unless only objectives
    // show (+0x3c), then the objectives; the callees by title address (they read stack words their
    // neighbours leave, same depth in both).
    if (!visible(self)) return;
    const uint32_t me = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(self));
    if (!field<uint8_t>(self, 0x3c)) {
        title_call_regs(0x003180a0, 0, me, 0);  // HudCompCompass_UpdateNeedle (ESI)
        title_call_regs(0x00339830, me, 0, 0);  // HudCompCompass_UpdateActorMarkers (EAX)
        if (title_global<int32_t>(0x0094d7d4) == 1)
            title_function<void XB_STDCALL(uint8_t*)>(0x00337b70)(self);  // HudCompCompass_UpdateFiringEnemies
        title_call_regs(0x00339450, me, 0, 0);  // HudCompCompass_UpdateTankMarkers (EAX)
    }
    title_call_regs(0x003379b0, me, 0, 0);  // HudCompCompass_UpdateObjectives (EAX)
}

void XB_REGS1 XB_CDECL HudCompCompass_UpdateObjectives(uint8_t* compass) {
    // Active objectives (0x009730c8, 16 x 0x98: state 4, a non-zero position) except the hidden one
    // (+0x13d0 / +0x13d2): their yaw and flat distance from the view origin (0x00972bb0), pulsing from
    // their start time (+0x10); above / below when the height difference passes 0x0094aa00 / 0x00948660.
    for (int32_t i = 0; i < 16; ++i) {
        uint8_t* const objective = &title_global<uint8_t>(0x009730c8) + i * 0x98;
        uint8_t* const marker = compass + 0x1210 + static_cast<uint32_t>(i) * 0x1c;
        field<uint8_t>(marker, 0xc) = 0;
        if (field<int32_t>(objective, 0) != 4) continue;
        if (field<int16_t>(compass, 0x13d0) != 0 && field<int16_t>(compass, 0x13d2) == i) continue;
        const float* const position = &field<float>(objective, 4);
        if (!(position[0] != 0.0f || position[1] != 0.0f || position[2] != 0.0f)) continue;
        float delta[3];
        delta[0] = position[0] - gf(0x00972bb0);
        delta[1] = position[1] - gf(0x00972bb4);
        const float height = position[2] - gf(0x00972bb8);
        delta[2] = 0.0f;
        const float reference = marker_reference(compass);
        const float yaw = cg_vectoyaw(delta);
        const int32_t n = static_cast<int32_t>((yaw - reference) * gf(0x0083ebd8)) & 0xffff;
        const float angle = platform_x87_mul(static_cast<float>(n), gf(0x0083ebd4));
        const float zero = gf(0x0083eafc);
        const float sum = static_cast<float>(static_cast<long double>(delta[1]) * delta[1] +
                                             static_cast<long double>(delta[0]) * delta[0] +
                                             static_cast<long double>(zero) * zero);
        const float distance = title_ci_sqrt(sum);
        HudCompCompass_PlaceMarker(compass, marker, distance, angle,
                                   static_cast<float>(field<int32_t>(objective, 0x10)), 1);
        field<uint8_t>(marker, 0x19) = 0;
        field<uint8_t>(marker, 0x1a) = 0;
        if (height > gf(0x0094aa00))
            field<uint8_t>(marker, 0x19) = 1;
        else if (gf(0x00948660) > height)
            field<uint8_t>(marker, 0x1a) = 1;
    }
}

void XB_STDCALL HudCompCompass_UpdateFiringEnemies(uint8_t* compass) {
    // Entities (0x0097d370 list) of the player's enemy team (team table 0, 2, 1, 0, 0) whose actor fired
    // within 2 s (+0xe8 + 2000 >= level time 0x0098540c) fill markers 1..31 (time, yaw +0x74, sentient
    // origin); then every timed marker is placed (as place_timed_markers, without the 800 ms window).
    if (!title_global<uint32_t>(0x00971aac)) return;  // cg_nextSnap
    uint8_t* const player = field<uint8_t*>(title_global<uint8_t*>(0x0095f534), 4);
    if (!player) return;
    uint8_t* const sentient = field<uint8_t*>(player, 0x244);
    if (!sentient) return;
    uint32_t local[5] = {0, 2, 1, 0, 0};
    const int32_t enemy = static_cast<int32_t>(local[field<int32_t>(sentient, 4)]);
    std::memset(compass + 0x540, 0, 0x500);
    int32_t found = 0;
    uint8_t* slot = compass + 0x554;
    float* const v = reinterpret_cast<float*>(local);
    for (uint8_t* const* it = entity_list_begin();
         static_cast<uint32_t>(reinterpret_cast<uintptr_t>(it)) != entity_list_end(); ++it) {
        uint8_t* const entity = *it;
        if (!entity) continue;
        uint8_t* const other = field<uint8_t*>(entity, 0x244);
        if (!other || field<int32_t>(other, 4) != enemy) continue;
        uint8_t* const actor = field<uint8_t*>(entity, 0x240);
        if (!actor || field<int32_t>(actor, 0xe8) + 2000 < title_global<int32_t>(0x0098540c)) continue;
        ++found;
        slot += 0x28;
        if (found >= 0x20) break;
        field<int32_t>(slot, -4) = cg_time();
        field<uint32_t>(slot, 0) = field<uint32_t>(entity, 0x74);
        Sentient_GetOrigin(v, 0, reinterpret_cast<uint8_t* const*>(field<uint8_t*>(entity, 0x244)));
        field<float>(slot, 4) = v[0];
        field<float>(slot, 8) = v[1];
    }
    const float reference = marker_reference(compass);
    for (uint32_t i = 0; i < 32; ++i) {
        uint8_t* const marker = compass + 0x540 + i * 0x28;
        if (!field<int32_t>(marker, 0x10)) continue;
        const float one = gf(0x0083eb20);
        float angle;
        float distance;
        if (platform_x87_greater_equal(one, abs_bits(field<float>(marker, 0x18))) &&
            platform_x87_greater_equal(one, abs_bits(field<float>(marker, 0x1c)))) {
            angle = short_angle(cg_vectoyaw(&field<float>(marker, 0x18)) - reference);
            distance = gf(0x00947cd0);
        } else {
            v[0] = field<float>(marker, 0x18) - gf(0x00972bb0);
            v[1] = field<float>(marker, 0x1c) - gf(0x00972bb4);
            angle = short_angle(cg_vectoyaw(v) - reference);
            distance = title_ci_sqrt(v[1] * v[1] + v[0] * v[0]);
        }
        HudCompCompass_PlaceMarker(compass, marker, distance, angle, gf(0x0083eb34), 0);
    }
}

// Veh_GetEntTankInfo (0x0024db50): handle on the stack, out index in ESI, enemy flag in EDI; AL.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL hud_veh_tank_info(uint32_t handle, uint8_t* enemy, int32_t* index);
PLATFORM_ASM_ENTRY(hud_veh_tank_info,
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "movl 16(%esp), %edi\n"
                   "movl 20(%esp), %esi\n"
                   "pushl 12(%esp)\n"
                   "movl $0x0024db50, %eax\n"
                   "call *%eax\n"
                   "addl $4, %esp\n"
                   "popl %edi\n"
                   "popl %esi\n"
                   "ret")

// The original reads a stack word it never sets (E-4) into a direction marker's +0x20: the entry hands it over.
PLATFORM_ASM_ENTRY(HudCompCompass_UpdateTankMarkers_entry,
                   "pushl -4(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(HudCompCompass_UpdateTankMarkers) "addl $8, %esp\n"
                   "ret")

void XB_CDECL HudCompCompass_UpdateTankMarkers(uint8_t* compass, uint32_t stale) {
    // Tank entities (type 0xe; enemies only with 0x0094d7d4) by Veh_GetEntTankInfo's index: time,
    // position, yaw, picture (+0x30 enemy, +0x18 friendly); the player's own tank from the snapshot
    // (+0x4e4: index, packed offset; yaw +0x4e7 * 1.40625); then the timed markers placed.
    if (!title_global<uint32_t>(0x00971aac)) return;
    uint8_t enemy = 0;
    int32_t index = 0;
    for (uint8_t* const* it = entity_list_begin();
         static_cast<uint32_t>(reinterpret_cast<uintptr_t>(it)) != entity_list_end(); ++it) {
        uint8_t* const entity = *it;
        if (!entity || field<uint8_t>(entity, 0) != 0xe) continue;
        if (!uint8_t(hud_veh_tank_info(field<uint32_t>(entity, 0x224), &enemy, &index))) continue;
        if (!title_global<int32_t>(0x0094d7d4) && enemy) continue;
        uint8_t* const marker = compass + 0xe28 + static_cast<uint32_t>(index) * 0x28;
        field<int32_t>(marker, 0x10) = cg_time();
        field<uint32_t>(marker, 0x18) = field<uint32_t>(entity, 0x60);
        field<uint32_t>(marker, 0x1c) = field<uint32_t>(entity, 0x64);
        field<uint32_t>(marker, 0x14) = field<uint32_t>(entity, 0x74);
        field<uint32_t>(marker, 0x24) = field<uint32_t>(compass, enemy ? 0x30 : 0x18);
    }
    float v[3] = {0.0f, 0.0f, bits_float(stale)};
    uint8_t* snap = title_global<uint8_t*>(0x00971aac);
    if (const uint32_t packed = field<uint32_t>(snap, 0x4e4)) {
        uint8_t* const marker = compass + 0xe28 + (packed & 0x3f) * 0x28;
        field<int32_t>(marker, 0x10) = cg_time();
        snap = title_global<uint8_t*>(0x00971aac);
        unpack_offset(field<uint32_t>(snap, 0x4e4), v);
        if (!offset_is_edge(v)) {
            field<float>(marker, 0x18) = field<float>(snap, 0x10) + v[0];
            field<float>(marker, 0x1c) = field<float>(snap, 0x14) + v[1];
        } else {
            store_direction(marker, v, stale);
        }
        const int8_t yaw = field<int8_t>(title_global<uint8_t*>(0x00971aac), 0x4e7);
        field<float>(marker, 0x14) = static_cast<float>(yaw) * gf(0x0083f0b4);
    }
    place_timed_markers(compass, 0xe28, 25, v);
}

// The original reads a stack word it never sets (E-0x10) into a direction marker's +0x20.
PLATFORM_ASM_ENTRY(HudCompCompass_UpdateActorMarkers_entry,
                   "pushl -16(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(HudCompCompass_UpdateActorMarkers) "addl $8, %esp\n"
                   "ret")

void XB_CDECL HudCompCompass_UpdateActorMarkers(uint8_t* compass, uint32_t stale) {
    // Actor entities (type 0xb, markers enabled 0x00986008, with a compass flag +0x137c, team 2) by
    // their marker index (+0x1382): time, yaw +0x74, position +0x60; the snapshot's (+0x4e0: index,
    // packed offset from the leaned eye position, yaw +0x4e3 * 1.40625); then the timed markers placed.
    if (!title_global<uint32_t>(0x00971aac)) return;
    for (uint8_t* const* it = entity_list_begin();
         static_cast<uint32_t>(reinterpret_cast<uintptr_t>(it)) != entity_list_end(); ++it) {
        uint8_t* const entity = *it;
        if (!entity || field<uint8_t>(entity, 0) != 0xb) continue;
        if (!title_global<uint32_t>(0x00986008)) continue;
        uint8_t* const actor = field<uint8_t*>(entity, 0x240);
        if (!actor || !field<uint32_t>(actor, 0x137c)) continue;
        if (field<int32_t>(field<uint8_t*>(entity, 0x244), 4) != 2) continue;
        const int32_t index = field<int16_t>(actor, 0x1382);
        if (index < 0) continue;
        uint8_t* const marker = compass + 0x40 + static_cast<uint32_t>(index) * 0x28;
        field<int32_t>(marker, 0x10) = cg_time();
        field<uint32_t>(marker, 0x14) = field<uint32_t>(entity, 0x74);
        field<uint32_t>(marker, 0x18) = field<uint32_t>(entity, 0x60);
        field<uint32_t>(marker, 0x1c) = field<uint32_t>(entity, 0x64);
    }
    float v[3] = {0.0f, 0.0f, bits_float(stale)};
    uint8_t* snap = title_global<uint8_t*>(0x00971aac);
    if (const uint32_t packed = field<uint32_t>(snap, 0x4e0)) {
        uint8_t* const marker = compass + 0x40 + (packed & 0x3f) * 0x28;
        field<int32_t>(marker, 0x10) = cg_time();
        snap = title_global<uint8_t*>(0x00971aac);
        unpack_offset(field<uint32_t>(snap, 0x4e0), v);
        if (!offset_is_edge(v)) {
            float eye[3] = {field<float>(snap, 0x10), field<float>(snap, 0x14),
                            field<float>(snap, 0xbc) + field<float>(snap, 0x18)};
            AddLeanToPosition(eye, field<float>(snap, 0x5c), field<float>(snap, 0xb0), 16.0f, 20.0f);
            field<float>(marker, 0x18) = eye[0] + v[0];
            field<float>(marker, 0x1c) = eye[1] + v[1];
        } else {
            store_direction(marker, v, stale);
        }
        const int8_t yaw = field<int8_t>(title_global<uint8_t*>(0x00971aac), 0x4e3);
        field<float>(marker, 0x14) = static_cast<float>(yaw) * gf(0x0083f0b4);
    }
    place_timed_markers(compass, 0x40, 32, v);
}

PLATFORM_ASM_ENTRY(HudCompCompass_UpdateNeedle_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(HudCompCompass_UpdateNeedle) "addl $4, %esp\n"
                   "ret")

void XB_CDECL HudCompCompass_UpdateNeedle(uint8_t* compass) {
    // The needle (+0x38) swings to the view yaw minus the north yaw (0x009730a4) as a damped spring
    // (speed +0x34) in 5 ms steps since the last update (0x0098ad14); snaps after a pause of more than
    // 500 ms or once close and slow (0.25 degrees, speed 1). Rotates the needle picture (refitted in
    // widescreen, 0x00948f64).
    const float target = short_angle(gf(0x00972bf4) - gf(0x009730a4));
    int32_t& last = title_global<int32_t>(0x0098ad14);
    const int32_t now = cg_time();
    float& speed = field<float>(compass, 0x34);
    if (last > now || static_cast<float>(now - last) > gf(0x0083ebec)) {
        field<float>(compass, 0x38) = target;
        last = now;
        speed = 0.0f;
        return;
    }
    int32_t elapsed = now - last;
    last = now;
    float delta = Q_fmod(field<float>(compass, 0x38), target);
    if (elapsed > 0) {
        const float zero = 0.0f;
        do {
            int32_t step;
            if (elapsed > 5) {
                step = 5;
                elapsed -= 5;
            } else {
                step = elapsed;
                elapsed = 0;
            }
            const float s = static_cast<float>(step) * gf(0x0083eb4c);  // 0.001
            if (platform_x87_greater(gf(0x0083ec24), abs_bits(delta)) &&
                platform_x87_greater(gf(0x0083eb20), abs_bits(speed))) {
                field<float>(compass, 0x38) = target;
                speed = zero;
                return;
            }
            delta = short_angle(s * speed + delta);
            if (delta > gf(0x0083eb60)) delta = delta - gf(0x0083ebcc);  // 180, 360
            if (delta > zero)
                speed = speed - s * gf(0x0083eb9c);  // 1000
            else if (zero > delta)
                speed = s * gf(0x0083eb9c) + speed;
            const float damped = speed - s * speed * gf(0x0083eba8);  // 2
            speed = damped;
            if (damped > zero) {
                if (delta > zero) speed = damped - damped * s * gf(0x0083ec98);  // 3.5
                speed = speed - s;
                if (zero > speed) speed = zero;
            } else {
                if (zero > delta) speed = damped - damped * s * gf(0x0083ec98);
                speed = s + speed;
                if (speed > zero) speed = zero;
            }
            const float clamped = speed;
            if (clamped > gf(0x0083ede0))
                speed = gf(0x0083ede0);  // 30000
            else if (gf(0x0083eddc) > clamped)
                speed = gf(0x0083eddc);
        } while (elapsed > 0);
    }
    const float yaw = short_angle(delta + target);
    field<float>(compass, 0x38) = yaw;
    const float radians = yaw * gf(0x0083edd8);
    uint8_t* const needle = object_at(compass, 8);
    if (title_global<int32_t>(0x00948f64)) {
        reset_rotation(needle);
        set_rotation(object_at(compass, 8), radians);
        fit_needle(compass);
    } else {
        set_rotation(needle, radians);
    }
}

}  // namespace game
