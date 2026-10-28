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

// 0x00011fd0, 0x00012010, 0x00012050, 0x000120a0, 0x000120e0: deleting destructors - the
// class vtable, the owned UI elements deleted, the HudComponent vtable, Z_Free with flag bit 0.
uint8_t* XB_THISCALL HudCompHealth_DeletingDtor(uint8_t* self, uint32_t flags);
uint8_t* XB_THISCALL HudCompAmmo_DeletingDtor(uint8_t* self, uint32_t flags);
uint8_t* XB_THISCALL HudCompGrenades_DeletingDtor(uint8_t* self, uint32_t flags);
uint8_t* XB_THISCALL HudCompWeaponName_DeletingDtor(uint8_t* self, uint32_t flags);
uint8_t* XB_THISCALL HudCompHint_DeletingDtor(uint8_t* self, uint32_t flags);
// 0x000123b0, 0x00012440: HudManager_Dtor / HudCompCompass_Dtor (this in ESI), then Z_Free with flag bit 0.
uint8_t* XB_THISCALL HudManager_DeletingDtor(uint8_t* self, uint32_t flags);
uint8_t* XB_THISCALL HudCompCompass_DeletingDtor(uint8_t* self, uint32_t flags);
// 0x00310b80: the icons (count +0x10) in rows of 8, the highlighted one (+0x18) with the second picture.
void XB_THISCALL HudCompIconCount_Draw(uint8_t* self);
// 0x00310d60: the bomber map and its markers in two pushed scenes without depth.
void XB_THISCALL HudCompBomberMap_Draw(uint8_t* self);
// 0x003116f0: the stance from the player flags; its highlight fades for a second after a change.
void XB_THISCALL HudCompStance_Update(uint8_t* self, uint32_t unused);
// 0x00317ac0 (manager in ESI, component in EDX, colour 0..3 in EDI): the component's colour.
extern "C" PLATFORM_HIDDEN void XB_CDECL HudManager_SetComponentColor(uint8_t* manager, int32_t component, int32_t color);
extern "C" void HudManager_SetComponentColor_entry(void);
// 0x00319c20, 0x00319cb0, 0x00319d50 (manager in ESI): the slot visibility of HUD layouts 2, none, default.
extern "C" PLATFORM_HIDDEN void XB_CDECL HudManager_Layout2(uint8_t* manager);
extern "C" void HudManager_Layout2_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL HudManager_HideAll(uint8_t* manager);
extern "C" void HudManager_HideAll_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL HudManager_LayoutDefault(uint8_t* manager);
extern "C" void HudManager_LayoutDefault_entry(void);
// 0x003246c0 (manager in EAX): layout 3 (the default one with other slots).
void XB_REGS1 XB_CDECL HudManager_Layout3(uint8_t* manager);
// 0x0032ce80 (mode in EAX, manager in ECX): switches the layout when the mode changes.
void XB_REGS3 XB_CDECL HudManager_SetLayoutMode(uint32_t mode, uint32_t unused, uint8_t* manager);

// The compass (vtable 0x0082884c; layout in cg_hud.cpp).
// 0x00318400 (this in EDX): vtable, visible, pictures/markers/needle cleared. Returns this.
uint8_t* XB_REGS2 XB_CDECL HudCompCompass_Ctor(uint32_t unused, uint8_t* self);
// 0x00318380 (this in ESI): deletes the eight pictures, then the base vtable.
extern "C" PLATFORM_HIDDEN void XB_CDECL HudCompCompass_Dtor(uint8_t* self);
extern "C" void HudCompCompass_Dtor_entry(void);
// 0x00317fb0: the needle's rotation again (refitted in widescreen), the frame pictures scaled for the aspect.
void XB_THISCALL HudCompCompass_SetWidescreen(uint8_t* self, uint32_t widescreen, float center_x);
// 0x00318040 (vtable +0xc; no Ghidra function): while visible, the frame and the marker lists; only the
// objectives with +0x3c.
void XB_THISCALL HudCompCompass_Draw(uint8_t* self);
// 0x00311270 (this in EDI): the 16 objective markers (above / below / level picture, the pulse ring).
extern "C" PLATFORM_HIDDEN void XB_CDECL HudCompCompass_DrawMarkerList(uint8_t* self);
extern "C" void HudCompCompass_DrawMarkerList_entry(void);
// 0x00317d90 (this in EAX): the 25 tank markers with their own pictures (+0x24), rotated by yaw + 90.
void XB_REGS1 XB_CDECL HudCompCompass_DrawTankMarkers(uint8_t* self);
// 0x00317e50 / 0x00317f00 (this in EBX): the 32 actor (+0x40, picture +0x14) / firing-enemy (+0x540,
// picture +0x2c) markers rotated by the view yaw minus their yaw.
extern "C" PLATFORM_HIDDEN void XB_CDECL HudCompCompass_DrawActorMarkers(uint8_t* self);
extern "C" void HudCompCompass_DrawActorMarkers_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL HudCompCompass_DrawFiringEnemies(uint8_t* self);
extern "C" void HudCompCompass_DrawFiringEnemies_entry(void);
// 0x003111a0 (marker in ECX, start time in XMM2): the pulse (0x0094bd2c pulses over 0x0094ac44 ms):
// ring alpha +0x10 (up then down), scale +0x14 (halved after the first), pulsing +0x18.
extern "C" PLATFORM_HIDDEN void XB_CDECL HudCompCompass_UpdateMarkerPulse(uint8_t* marker, float start);
extern "C" void HudCompCompass_UpdateMarkerPulse_entry(void);
// 0x00337810 (compass in EBX, marker in ESI, distance in XMM0; stdcall angle, pulse start, pulse flag):
// the marker's screen position and alpha from the distance and angle; shown; pulse when flagged.
extern "C" PLATFORM_HIDDEN void XB_CDECL HudCompCompass_PlaceMarker(uint8_t* compass, uint8_t* marker, float distance,
                                                                  float angle, float pulse_start, uint32_t pulse);
extern "C" void HudCompCompass_PlaceMarker_entry(void);
// 0x00339c30 (vtable +8): while visible, the needle and markers (only objectives with +0x3c).
void XB_THISCALL HudCompCompass_Update(uint8_t* self, uint32_t unused);
// 0x003379b0 (compass in EAX): the 16 objective markers.
void XB_REGS1 XB_CDECL HudCompCompass_UpdateObjectives(uint8_t* compass);
// 0x00337b70: the firing-enemy markers (enemies whose actor fired within 2 s).
void XB_STDCALL HudCompCompass_UpdateFiringEnemies(uint8_t* compass);
// 0x00339450 / 0x00339830 (compass in EAX): the tank / actor markers (entities and the snapshot's own).
extern "C" PLATFORM_HIDDEN void XB_CDECL HudCompCompass_UpdateTankMarkers(uint8_t* compass, uint32_t stale);
extern "C" void HudCompCompass_UpdateTankMarkers_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL HudCompCompass_UpdateActorMarkers(uint8_t* compass, uint32_t stale);
extern "C" void HudCompCompass_UpdateActorMarkers_entry(void);
// 0x003180a0 (compass in ESI): the needle's damped swing to the view yaw.
extern "C" PLATFORM_HIDDEN void XB_CDECL HudCompCompass_UpdateNeedle(uint8_t* compass);
extern "C" void HudCompCompass_UpdateNeedle_entry(void);

// The HUD manager and component setup (layout in cg_hud.cpp).
// 0x0031a020 (manager in ESI): vtable, the components allocated and constructed, the three 8-byte
// strings (Heap_Alloc16), the rest cleared, hint timer -1. Returns the manager.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL HudManager_Create(uint8_t* self);
extern "C" void HudManager_Create_entry(void);
// 0x00319f20: deletes the components (+0x40 before the attached +0x3c), frees the three strings.
void XB_THISCALL HudManager_Dtor(uint8_t* self);
// 0x00319df0 (vtable +0xc): the per-frame component updates (frame time on the stack).
void XB_THISCALL HudManager_Update(uint8_t* self, uint32_t dt);
// 0x00311800 (manager in ESI; stdcall widescreen flag): every component's vtable +0x18 with its centre x.
extern "C" PLATFORM_HIDDEN void XB_CDECL HudManager_SetWidescreen(uint8_t* self, uint32_t widescreen);
extern "C" void HudManager_SetWidescreen_entry(void);
// 0x0032ced0 (element in ESI, hash in EAX): the text +0x3c set from the localized string with that hash.
extern "C" PLATFORM_HIDDEN void XB_CDECL HudText_SetStringByHash(uint8_t* self, uint32_t hash, uint32_t entry_esp,
                                                               uint32_t ebx, uint32_t edi);
extern "C" void HudText_SetStringByHash_entry(void);
// 0x00318bf0 (this in EDI): vtable, the name text and background picture. Returns this.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL HudCompWeaponName_Ctor(uint8_t* self);
extern "C" void HudCompWeaponName_Ctor_entry(void);
// Component setups (vtable +4, the HUD panel on the stack): 0x00318b30 weapon name, 0x00324480 grenades,
// 0x003244c0 ammo, 0x00323fa0 tank icon, 0x00324330 grenade cook.
void XB_THISCALL HudCompWeaponName_Setup(uint8_t* self, uint8_t* panel);
void XB_THISCALL HudCompGrenades_Setup(uint8_t* self, uint8_t* panel);
void XB_THISCALL HudCompAmmo_Setup(uint8_t* self, uint8_t* panel);
void XB_THISCALL HudCompTankIcon_Setup(uint8_t* self, uint8_t* panel);
void XB_THISCALL HudCompGrenadeCook_Setup(uint8_t* self, uint8_t* panel);
// 0x00319110 (vtable +8): the clip / reserve ammo texts of the current weapon.
void XB_THISCALL HudCompAmmo_Update(uint8_t* self, uint32_t dt);

}  // namespace game
