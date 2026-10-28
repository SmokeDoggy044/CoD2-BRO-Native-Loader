#include "cg_hud.h"

#include "fe.h"
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

}  // namespace game
