#pragma once

// The in-game HUD: the HudManager (0x0031a020) and its components (health, ammo, grenades,
// compass, weapon name, hints, tank parts, meters), each a small class whose methods drive the
// front end's UI elements (UIPicture/UIText vtable calls: +0x3c and +0x44 draw them).

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00311330: draws the two pictures (+8, +0xc) while visible (+4).
void XB_THISCALL HudCompTankIcon_Draw(uint8_t* self);
// 0x003113d0: while visible and with a frame (+0xc): frame, bar (+8), overlay (+0x10).
void XB_THISCALL HudCompTankHealth_Draw(uint8_t* self);
// 0x00311400: while shown (vtable +0x10) and not suppressed (+0x14): text (+8, +0x3c), icon (+0xc).
void XB_THISCALL HudCompWeaponName_Draw(uint8_t* self);
// 0x003115b0: while visible and not suppressed (+0x2d): the six segments (+8..+0x1c), then +0x20.
void XB_THISCALL HudCompGrenadeCook_Draw(uint8_t* self);
// 0x00311610: while visible and not suppressed (+0x18): icons +8, +0xc, count text +0x10 (+0x3c).
void XB_THISCALL HudCompGrenades_Draw(uint8_t* self);
// 0x00311640: while visible and not suppressed (+0x1c): icon +8, texts +0xc and +0x10 (+0x3c).
void XB_THISCALL HudCompAmmo_Draw(uint8_t* self);
// 0x00311670: while visible: the damage overlay +8 when the fade (+0x18) > 0, the pictures +0xc,
// +0x10, and +0x14 when flagged (+0x20).
void XB_THISCALL HudCompHealth_Draw(uint8_t* self);
// 0x003116c0: while visible: the stance pair's picture (+8 + stance * 8, stance +0x24) and its
// second picture (+0xc + stance * 8) when flagged (+0x30).
void XB_THISCALL HudCompStance_Draw(uint8_t* self);
// 0x003110e0: while shown (vtable +0x10) and not suppressed (+0x224): the hint's button picture
// (index +0x21c, 0..0x83, list +8), then the text (+0x218, +0x3c).
void XB_THISCALL HudCompHint_Draw(uint8_t* self);
// 0x00311350: while visible, with a reticle (+8), in a vehicle (0x00972610 bit 0x100000) and
// not in view mode 1 (0x00972a38): the reticle, then the four markers (+0xc..+0x18) when +0x40 >= 0.
void XB_THISCALL HudCompTankReticle_Draw(uint8_t* self);
// 0x00311ae0: deletes the attached component (+0x3c; deleting destructor, flag 1), clears +0x44
// and +0x3c. The stack argument is unused.
void XB_THISCALL HudManager_Detach(uint8_t* self, uint32_t unused);
// 0x00317550: UIPicture_ScaleXForAspect on both pictures (+8, +0xc) when the second is set.
void XB_THISCALL HudCompIconCount_SetWidescreen(uint8_t* self, uint32_t widescreen, float center_x);
// 0x00317bd0 (this in EAX): vtable 0x008299b8, visible, no owner, the 12 picture pairs (+0xc)
// and +0xe4/+0xe8 cleared, +0xec = 0. Returns this.
uint8_t* XB_REGS1 XB_CDECL HudCompMeter_Ctor(uint8_t* self);
// 0x00311090 (this in EAX): with an owner (+8), pair i shows its first picture (+0xc + i * 8)
// for i < count and its second (+0x10 + i * 8) otherwise (vtable +0x34 = set visible).
void XB_REGS1 XB_STDCALL HudCompMeter_SetCount(uint8_t* self, uint32_t count);
// 0x00318f30 (this in EDI): deletes the six segments (+8..+0x1c) and +0x20, then the base vtable.
extern "C" PLATFORM_HIDDEN void XB_CDECL HudCompGrenadeCook_Dtor(uint8_t* self);
extern "C" void HudCompGrenadeCook_Dtor_entry(void);

}  // namespace game
