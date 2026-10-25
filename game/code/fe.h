#pragma once

// The front end (FE_*: menu set-up and labels of the main/bonus/options menus), the UI menu base and panel lookups,
// profile defaults. Menu items and panels are title C++ objects; their methods are called through their vtables
// (thiscall): +0x30 set text by string id, +0x34 set text, +0x6c set label, +0x7c set alpha, +0x140 set wrapped
// text (width, -1.0).

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x0030bf90 (menu in ESI): "%i / %i" of the collector's-edition category's current item (+0x130 + index * 4 by
// the index +0x22) and its count (38, 15, 15, 3, 9, 11 for indexes 0-5, else 0) into the label +0x3c.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_BonusCESetText(uint8_t* menu);
extern "C" void FE_BonusCESetText_entry(void);
// 0x0030c070 (menu in ESI): the six art category item texts (BONUS_ART_DIR .. BONUS_OVERHEAD).
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_BonusArtCategoryLabel(uint8_t* menu);
extern "C" void FE_BonusArtCategoryLabel_entry(void);
// 0x0030c130 (menu in ESI): the five movie item texts (BONUS_BRINGING .. BONUS_CE_ARTS), then the selection +0x22
// back to the saved one (+0x40) and +0x3c cleared.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_BonusMovieLabel(uint8_t* menu);
extern "C" void FE_BonusMovieLabel_entry(void);
// 0x0030cd70 / 0x0030d0b0 (play time in EAX: seconds +0x28, minutes +0x2c, hours +0x30, days +0x34): va
// "%s%i:%s%i:%s%i" of hours (days * 24 + hours, at most 999: then the record is set to 41 days 15 hours), minutes,
// seconds, each below 10 with a leading "0". Two identical copies.
const char* XB_REGS1 XB_CDECL FE_FormatPlayTime(int32_t* time);
const char* XB_REGS1 XB_CDECL FE_FormatPlayTime2(int32_t* time);
// 0x0030dbc0 (menu in ESI): BONUS_CHAR_<n>_NAME into the label +0x68, _STATS and _BIO wrapped (300, -1.0) into
// +0x6c / +0x70; n = the index +0x22 + 1.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_BonusCharacterSetText(uint8_t* menu);
extern "C" void FE_BonusCharacterSetText_entry(void);
// 0x0030dfd0 (menu in ESI, ECX = the push ecx slot's value): FEMENU_COP_4X3 / _16X9 by +0x50 into the label +0x3c,
// then the first item's color (vtable +0x84 into a local) onto the label (+0x90).
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_AspectOptionLabel(uint8_t* menu, uint32_t ecx);
extern "C" void FE_AspectOptionLabel_entry(void);
// 0x0030e0f0 (key in EDI): the FEMENU_CMD_* display name of the command bound to the key (__stricmp against the
// 18 input commands; key -1 counts as bound to ""), "" for an unbound key; else FEMENU_CMD for Enter (13) / Escape
// (27) or FEMENU_NA.
extern "C" PLATFORM_HIDDEN const char* XB_CDECL FE_CommandDisplayName(int32_t key);
extern "C" void FE_CommandDisplayName_entry(void);
// 0x00312690 (menu in ESI): with the card corrupt (+0x36) FEMENU_OP_CORRUPT on all four slot items; else per slot
// FEMENU_OP_NEW (empty, +0x6c + slot * 12) or its name (+0x64 + slot * 12), then the help bar +0x40:
// FEMENU_OP_HELPBAR when +0x4c is set or the selected slot is empty, else FEMENU_LOAD_HELPBAR_XBOX.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SaveSlotsUpdateText(uint32_t entry, uint8_t* menu, uint32_t edi);
extern "C" void FE_SaveSlotsUpdateText_entry(void);
// 0x00312ad0 (menu in ESI): "%i / %i" of the concept category's current item (+0x8c + index * 4) and its count
// (the 0x48-byte level records at 0x0095f968, entry index + 1; 0 while that level is locked: 0x008dce54 clear and
// 0x008dce44 not above it) into the label +0x3c.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_BonusConceptSetText(uint8_t* menu);
extern "C" void FE_BonusConceptSetText_entry(void);
// 0x00312bd0 (menu in EDI): the 13 bonus level items: shown (+0x20 (0)), hidden again (+0x20 (1)) while the level
// (0x0095f9b0 + i * 0x48; locked as in FE_BonusConceptSetText) has nothing, the item's selection +0x8c clamped up
// to the stored best (0x008dced8) + 1, the text BONUS_BONUS_LEVEL_<i+1> or BONUS_LOCKED when the item is locked
// (+0x24); then 0x00312b40 (the menu), alpha +0x40 = 1.0 and +0x44 cleared.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_BonusLevelLabel(uint8_t* menu);
extern "C" void FE_BonusLevelLabel_entry(void);
// 0x0031be80 / 0x0031cc30 (menu in ESI): the current vehicle / weapon (index +0x22, item +0x70 / +0x84 + index * 4)
// BONUS_VEH_/BONUS_WEAP_<n>_<item>_NAME into the label +0x3c, _INFO wrapped (300, -1.0) into +0x44, then "%i /
// %i" of the item and the category's count from the unlock counts 0x00312e30 fills (0x0095f924; weapons 0-9 at
// words 0-9, vehicles 0-5 at words 10-15, others 0) into +0x40.
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_BonusVehicleSetText(uint8_t* menu);
extern "C" void FE_BonusVehicleSetText_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_BonusWeaponSetText(uint8_t* menu);
extern "C" void FE_BonusWeaponSetText_entry(void);

// 0x00315210 (menu in ESI, item count in EDI; owner, a2, a3, a4, flags on the stack, ret 0x14): the UI menu base
// constructor - vtable 0x0082a628, a zeroed item array (Heap_Alloc16), the arguments at +0x8/+0xc/+0x10/+0x28 and
// flags without bit 9 at +0x2a, selection -1, +0x33 = the owner's vtable +0x24 result (0 without owner). Returns
// the menu.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL UIMenu_Init(uint8_t* menu, int32_t count, uint8_t* owner, uint32_t a2,
                                                         uint32_t a3, uint16_t a4, uint32_t flags);
extern "C" void UIMenu_Init_entry(void);
// 0x00315b10 (text in ESI; 10 stack words, ret 0x28): the UI text item base constructor - vtable 0x008298b8, two
// empty ScriptStrings (+0x1c = the text a2, +0x54 = ""), the colours +0x20..+0x4c from 0x00922eb8, kind a1 at
// +0x18, position a3/a4 (+0x2c/+0x30), scale a7 (+0x38/+0x3c/+0x44/+0x48), a10 at +0x50; with a6 == 8 +0x8 =
// a5, else the vtable +0x24 sort key ((a6 * 1000 + a5 clamped to 0..1000) * 0.125); flags +0x5c = a8 | a9 | 4
// (| 1 with the top byte of a10). Returns the text.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL UIText_Init(uint8_t* text, int32_t kind, const char* string, uint32_t x,
                                                         uint32_t y, int32_t depth, int32_t layer, uint32_t scale,
                                                         uint8_t a8, uint8_t a9, uint32_t a10);
extern "C" void UIText_Init_entry(void);
// 0x00320690 (8 stack words, ret 0x20; the caller's ECX and EDX low bytes become UIText_Init's flag words a9 / a8):
// UIText_Init(text, kind a2, "", a3, a4, a5, a6, a7, EDX, ECX, a8), then the derived vtable 0x0082a450, colour -1,
// alignment -1, the rest cleared and the line height (+0x68, +0x6c) = the height of "!" in the kind's font
// (0x0095f8d4; none for kinds 7/8) at the text's scale (nglGetTextExtent; unsigned to float on the x87).
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL UIText_Construct(uint8_t* text, int32_t kind, uint32_t x, uint32_t y,
                                                              int32_t depth, int32_t layer, uint32_t scale,
                                                              uint32_t a8, uint32_t ecx, uint32_t edx);
extern "C" void UIText_Construct_entry(void);
// 0x003215c0 (name in EBX; panel on the stack, ret 4): the panel's text item (+0xc array, +0x14 count) whose name
// (vtable +0x9c, a ScriptString released after) is `name`, else null.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL Panel_FindText(const char* name, uint8_t* panel);
extern "C" void Panel_FindText_entry(void);
// 0x00321650 (picture list in EAX: +0 array, +8 count; name on the stack, ret 4): the picture whose name (+0x3c
// ScriptString) is `name`, else the shared default picture (0x00320170 with 0x0095f8d0).
uint8_t* XB_REGS1 XB_STDCALL Panel_FindPicture(uint8_t* list, const char* name);
// 0x00325d60 (menu in EDI; selection on the stack, ret 4): pictures difficulty0-2 of the list +0x34 shown (+0x34
// (1)) for the selection, hidden for the others; then the alphas of +0x3c / +0x40 by the index +0x22 (0: 0 / 1.0,
// 1: 1.0 / 1.0, else 1.0 / 0).
extern "C" PLATFORM_HIDDEN void XB_CDECL FE_SetDifficultyPicture(uint8_t* menu, int32_t selection);
extern "C" void FE_SetDifficultyPicture_entry(void);
// 0x0032ea00 (menu in ESI): deletes every item (vtable +0 (1)), frees the array (Z_Free), stops the menu's sound
// channel when its handle (+0x2c) is still live (table 0x00962860), clears the handle, array and count.
extern "C" PLATFORM_HIDDEN void XB_CDECL UIMenu_FreeItems(uint8_t* menu);
extern "C" void UIMenu_FreeItems_entry(void);

// 0x0032cf50 (profile in ESI): unless 0x0095ee09 is set, the profile's default settings (sensitivities 25, invert
// from the cvar at 0x00929c0c being negative, cg_controllerconfig, volumes ...).
extern "C" PLATFORM_HIDDEN void XB_CDECL Profile_SetDefaults(uint8_t* profile);
extern "C" void Profile_SetDefaults_entry(void);
// 0x0032d660: the "UI_Type" sound alias (S_FindSoundByHash) played at the origin (S_PlaySoundAlias, volumes -1).
void XB_CDECL UI_PlayTypeSound();
// 0x003307a0: from the pause menu: with a checkpoint to reload (0x008dd1cf == 1) g_reloading 1 and "reboot",
// returns 1; else the front end's dialog state 4, cl_paused 1 and the front end's vtable +0xc (7), returns 0.
uint8_t XB_CDECL PauseMenu_ReloadCheckpoint();

}  // namespace game
