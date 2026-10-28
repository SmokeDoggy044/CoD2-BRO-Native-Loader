#include "cg_hud.h"

#include "fe.h"
#include "title_data.h"

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

}  // namespace game
