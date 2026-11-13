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
// Gap functions (m3/m5 recordings, 11-12). 0x00310f70 / 0x00310ea0 (this in ESI, index in ECX, target picture slot
// in EDI): the target picture +0x6c + slot * 4 shown, placed (+0x8c (x, y)), drawn and hidden - in a row under
// background04 (index 0..2, spread by the count +0xe4) / in the grid around backgrounds 03/05/2 (index 0..11, four
// corners each; the x step wider on widescreen).
extern "C" PLATFORM_HIDDEN void XB_CDECL HudCompMeter_DrawTargetRow(uint8_t* self, int32_t index, uint32_t slot);
extern "C" void HudCompMeter_DrawTargetRow_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL HudCompMeter_DrawTargetGrid(uint8_t* self, int32_t index, uint32_t slot);
extern "C" void HudCompMeter_DrawTargetGrid_entry(void);
// 0x00323550 (vtable 0x008299b8 +0x0c): led1 / led2 from +0xe0, the panel, the row targets (+0xa4.., count +0xe4
// clamped to 3) and grid targets (+0xb0.., count +0xe8); on widescreen the pillarbox bars and the background
// positions read again. 0x00323730 (+0x08, dt): led1 at full alpha while the timer +0xec is 0, else the timer runs
// down and led1 blinks (alpha from the time left).
void XB_THISCALL HudCompMeter_Draw(uint8_t* self);
void XB_THISCALL HudCompMeter_Update(uint8_t* self, float dt);
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
// 0x003374e0: vtable +0xc: the map, markers and plane icons turned to the player's yaw; dt (float) fades the cursor.
void XB_THISCALL HudCompBomberMap_Update(uint8_t* self, uint32_t dt_bits);
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
// The anchor x (of 640) of the component at manager offset +4..+0x40 (HudManager_SetWidescreen's centres; 320
// for the attached component).
float hud_component_anchor(uint32_t offset);
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
// 0x00319370: vtable +0xc: the health bar (+8, coloured) and its falling trail (+0x14).
void XB_THISCALL HudCompHealth_Update(uint8_t* self, uint32_t dt);

// 0x00318840 (vtable +8): the tank health bar's quad from the player tank's health.
void XB_THISCALL HudCompTankHealth_Update(uint8_t* self, uint32_t dt);
// 0x00330350 (vtable +4): the HUD panel to every component's setup, the hint text and scroll bar.
void XB_THISCALL HudManager_Setup(uint8_t* self, uint8_t* panel);

// The tank shells indicator (vtable 0x00829664; no Ghidra functions): 0x00012400 deleting destructor
// (no children), 0x003242b0 setup ("tankshells03" / "tankshells02"), 0x00318780 update (the player
// tank's shell state, game import +0x798, into +0x10), 0x003113a0 draw (+8 with +0x10, else +0xc),
// 0x00318750 widescreen (both pictures scaled for the aspect).
uint8_t* XB_THISCALL HudCompTankShells_DeletingDtor(uint8_t* self, uint32_t flags);
void XB_THISCALL HudCompTankShells_Setup(uint8_t* self, uint8_t* panel);
void XB_THISCALL HudCompTankShells_Update(uint8_t* self, uint32_t dt);
void XB_THISCALL HudCompTankShells_Draw(uint8_t* self);
void XB_THISCALL HudCompTankShells_SetWidescreen(uint8_t* self, uint32_t widescreen, float center_x);
// 0x00011fc0 (no Ghidra function): the shared HUD component vtable +0x14, visible (+4) = the flag's byte.
void XB_THISCALL HudComponent_SetVisible(uint8_t* self, uint32_t on);
// 0x00012120: UIMeshList_ScaleXForAspect on the list +8 when set (HudCompMeter vtable 0x008299b8 +0x18).
void XB_THISCALL HudCompMeter_SetWidescreen(uint8_t* self, uint32_t widescreen, float center_x);
// 0x003184a0 / 0x00318810: UIPicture_ScaleXForAspect on +8 then +0xc / +0xc then +8 (HudCompTankIcon vtable
// 0x00829164 / HudCompTankHealth vtable 0x008299d4, +0x18).
void XB_THISCALL HudCompTankIcon_SetWidescreen(uint8_t* self, uint32_t widescreen, float center_x);
void XB_THISCALL HudCompTankHealth_SetWidescreen(uint8_t* self, uint32_t widescreen, float center_x);
// 0x00317590 (HudCompIconCount vtable 0x00828800 +0x14): visible (+4) = the flag's byte; when set, +0x14 (byte)
// and +0x10 cleared.
void XB_THISCALL HudCompIconCount_SetVisible(uint8_t* self, uint32_t on);
// 0x00311ab0 (HudManager vtable +0x18): the hint text +0x3c's vtable +0x3c (draw) while the hint timer +0x60 > -1.
void XB_THISCALL HudManager_DrawHint(uint8_t* self);

// Gap functions (11-11). 0x00310cc0 (vtable 0x0082908c +0x14): unless +0x30 is null, the ten pictures +0x08..+0x2c
// and the objects +0x30..+0x40 shown / hidden (+0x34); the component's own +4 untouched.
void XB_THISCALL HudCompBomberMap_SetVisible(uint8_t* self, uint32_t on);
// 0x00322d30 (vtable 0x0082908c +0x04, panel): the gun pictures (+0x08..+0x2c, green), background +0x30, the chin
// gun's position (+0x58/+0x5c) and the three objective markers + ring (+0x34..+0x40) built, placed and hidden;
// +0x44 = -1, +0x54 = 0 (m5 recording, 11-12).
void XB_THISCALL HudCompBomberMap_Setup(uint8_t* self, uint8_t* panel);
// 0x003184d0 (vtable 0x00829164 +0x08): while visible, the "player_tank" entity's turret yaw (+0x248 -> +0x294 *
// 0x0083edd8) as the picture +8's rotation (+0x4c (angle, 1)).
void XB_THISCALL HudCompTankIcon_Update(uint8_t* self, uint32_t dt);
// The +0x18 widescreen slots (widescreen in the low byte, centre x): pictures scaled for the aspect
// (UIPicture_ScaleXForAspect), texts narrowed (+0x5c (x, y)) or reset (+0x60 (y)).
// 0x00318590: the reticle's five pictures +8..+0x18. 0x00318ae0: weapon name picture +0xc, text +8 (0.405 / 0.54).
// 0x00318fb0: grenade pictures +8 / +0xc, count text +0x10 (0.75 / 1). 0x00319030: ammo picture +8, texts +0xc /
// +0x10 (0.45 / 0.6), then the texts' x (+0x80) beside the picture (x87). 0x00319310: health pictures +8..+0x14,
// then +8 and +0x14 refreshed (+0xc4).
void XB_THISCALL HudCompTankReticle_SetWidescreen(uint8_t* self, uint32_t widescreen, float center_x);
void XB_THISCALL HudCompWeaponName_SetWidescreen(uint8_t* self, uint32_t widescreen, float center_x);
void XB_THISCALL HudCompGrenades_SetWidescreen(uint8_t* self, uint32_t widescreen, float center_x);
void XB_THISCALL HudCompAmmo_SetWidescreen(uint8_t* self, uint32_t widescreen, float center_x);
void XB_THISCALL HudCompHealth_SetWidescreen(uint8_t* self, uint32_t widescreen, float center_x);
// The +0x04 setup slots (panel): 0x00323970 hint pictures and a three-line white copy of "Press[Use]" (+0x218);
// 0x003242e0 tank health pictures and the bar's width +0x14; 0x00324560 health pictures and a new picture +0x14
// copied from the bar, one deeper; 0x00324640 the seven stance pictures.
void XB_THISCALL HudCompHint_Setup(uint8_t* self, uint8_t* panel);
void XB_THISCALL HudCompTankHealth_Setup(uint8_t* self, uint8_t* panel);
void XB_THISCALL HudCompHealth_Setup(uint8_t* self, uint8_t* panel);
void XB_THISCALL HudCompStance_Setup(uint8_t* self, uint8_t* panel);

// --- Gap functions (no Ghidra function until 2026-11-11) ---
// 0x00317c10 (vtable +0x18): the 0x84 button pictures scaled for the aspect; the text +0x218 scale (0.525, 0.7) in
// widescreen, else 0.7.
void XB_THISCALL HudCompHint_SetWidescreen(uint8_t* self, uint32_t widescreen, float center_x);
// 0x0032cd60 (vtable +8): the grenade count (CG_GetGrenadeAmmoCount, at most 999, +0x14) as "x%i" into the text
// +0x10, the frag (+8) or stick (+0xc) grenade icon shown (+0x1c = 0 / 1); hidden (+0x18) in a vehicle, without a
// snapshot or grenade weapon (0x0097294c) or another weapon name.
void XB_THISCALL HudCompGrenades_Update(uint8_t* self, uint32_t dt);
// 0x00318df0 (vtable +8): the six segments (+8..+0x1c) full alpha in 0x64ff0000, then segments 0-4 cleared as the
// fuse left (+0x24 / +0x28) falls below 0.83 / 0.67 / 0.5 / 0.33 / 0.17; hidden (+0x2d) with a negative fuse, in a
// vehicle or without a snapshot.
void XB_THISCALL HudCompGrenadeCook_Update(uint8_t* self, uint32_t dt);

// 0x003185e0 (vtable +8): the player tank's reload ticks: of +0x3c ticks, the current one (+0x40, from the reload
// time left (vehicle +0x180) in steps of the weapon's reload time (+0x5dc) / ticks) fades in (+0x44), later ones
// full, earlier ones off.
void XB_THISCALL HudCompTankReticle_Update(uint8_t* self, uint32_t dt);
// 0x00318c90 (vtable +0x18): UIPicture_ScaleXForAspect on the six segments and +0x20.
void XB_THISCALL HudCompGrenadeCook_SetWidescreen(uint8_t* self, uint32_t widescreen, float center_x);
// 0x00324140 (vtable +4): "tank_reticle" (+8) and "tank_tic1".."4" (+0xc..), the reticle moved by (320 - x,
// 240 - (y - height / 2)), the ticks kept at their offsets (+0x1c.. x, +0x2c.. y); +0x3c = 4 ticks.
void XB_THISCALL HudCompTankReticle_Setup(uint8_t* self, uint8_t* panel);
// 0x00311940 (vtable +0x14): while the HUD is on (0x008d9d48), in a pushed scene (clear flags 3): the shown
// components (+4..+0x18, +0x30, +0x38, +0x34), the layout's extra slots (+0x5c: 1 +0x24, 2 +0x1c/+0x20, 3
// +0x28/+0x2c) and the attached one (+0x40); then nglPopScene (tail jump).
void XB_THISCALL HudManager_Draw(uint8_t* self);
// 0x00311430 (vtable +8): for 1.8 s after a weapon change (0x0094c23c) and not in a vehicle: the alpha (fading in
// the last 100 ms) on the text +8 and background +0xc; on a new weapon (def +0 vs +0x10) the name ("%s / %s" with
// the def's +0x90 text) and the background's rect (545 - text width - 16, 365, 545, 385).
void XB_THISCALL HudCompWeaponName_Update(uint8_t* self, uint32_t dt);

// 0x003198b0 (vtable +0x18): UIPicture_ScaleXForAspect on the three stance pairs (+8..+0x1c) and +0x20.
// (0x00319aa0 after it is a separate function, the stance constructor; HudManager_Create's C inlines it.)
void XB_THISCALL HudCompStance_SetWidescreen(uint8_t* self, uint32_t widescreen, float center_x);

// 0x003176f0 (vtable +0x18): once set up (+0x30), UIPicture_ScaleXForAspect on the ten pictures +8..+0x2c and
// +0x30..+0x40.
void XB_THISCALL HudCompBomberMap_SetWidescreen(uint8_t* self, uint32_t widescreen, float center_x);

// 0x00323a70 (vtable +4): "compass", "compassneedle", "compassglass" (+8..+0x10; the glass 20 deeper), compass
// vtable +0xc4, then the marker pictures with one quad each: friendly (+0x14) / enemy (+0x2c, tinted 0xffb40000)
// "objective_friendly" 13 units, tank (+0x30 tinted 14.56 x 7.28, +0x18 20.8 x 10.4), objective up / down /
// plain (+0x20/+0x24/+0x28, 32), objective ring (+0x1c, 64); +0x3c and the 25 tank markers' flags (+0xe34,
// 0x28 apart) cleared.
void XB_THISCALL HudCompCompass_Setup(uint8_t* self, uint8_t* panel);

// 0x0032fd80 (vtable +8): the hint (0x00972eb8, a new one from the snapshot +0x48c..+0x494 unless 0x00972548;
// time 0x00972ebc, length 0x00972ec0, string index 0x00972ec8) while its time lasts: hint <= 1 hides it, 10
// starts 0x00972eac/eb0. Same hint as +0x220: the button (+8 + index * 4) alpha, pulse scale (mode 0x0094c834: 2 =
// start % 1000 / 100, below 2 = (sin(cg_time / 150) + 1) * 5, doubled with +0x225) and the text alpha. A new one
// builds the text: weapons 12..0x8b (INGAME_PICKUP_WEAPON / INGAME_SWAP_WEAPONS, button weapon + 4), ammo
// 0x8c..0x10b (INGAME_PICKUP_AMMO), 7 the LMG mount by the aim key, a localized/config string (0x00972ec8), 8
// INGAME_HEALTH_PICKUP; then wrapped (450) into +0x218, the button +0x21c shown with the pulse. Hidden (+0x224).
void XB_THISCALL HudCompHint_Update(uint8_t* self, uint32_t dt);

}  // namespace game
