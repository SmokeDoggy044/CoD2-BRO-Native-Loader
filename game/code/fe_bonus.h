#pragma once

// The front end's bonus-content menus: the bonus materials hub (BonusMaterialsMenu, vtable 0x0082a708, 6 rows), the
// concept art (BonusConceptsMenu, 0x00828f98, 13 rows: one per level), vehicles (BonusVehiclesMenu, 0x00829ab8, 6
// rows), weapons (BonusWeaponsMenu, 0x00828868, 10 rows) and characters (BonusCharactersMenu, 0x00829728, 7 rows).
// UIMenus (ui_menu.h) built by FrontEndMenu_Construct; methods called through their vtables (thiscall; the button
// handlers take the pad on the stack, unused). The images and texts come from fe.cpp's FE_Bonus* helpers (load image:
// menu in EAX; free image: menu in ESI).
//
// Shared: +0x34 the panel, +0x38 the help bar (UI text: +0x6c set label, +0x70 (x, y) position), +0x33 = 5.
// BonusMaterialsMenu: +0x3c a movie is playing (HandleSelect ignored), +0x40 1.0, +0x44 / +0x60.. six bytes, +0x48..
// six words, +0x61 row 1 has items past the stored best, +0x68..+0x74 / +0x80 / +0x84 / +0x8c / +0x90 cleared,
// +0x78 row 1's item count, +0x7c row 1's current item, +0x80 the row accepted, +0x88 -1.
// BonusConceptsMenu: +0x3c, +0x40 the level texts' alpha (1.0), +0x44 fading in, +0x48.. the 13 level texts, +0x7c..
// 13 bytes (level has items), +0x8c.. the current item per level (1-based), +0xc0 full screen, +0xc4 the image file
// (-1), +0xc8 / +0xcc.
// BonusVehiclesMenu / BonusWeaponsMenu: +0x3c name, +0x40 count, +0x44 info texts, +0x48 1.0, +0x4c, +0x50.. one word
// per row, then one byte per row (+0x68 / +0x78), the current item per row (+0x70 / +0x84), the full screen flag
// (+0x88 / +0xac), the image file (+0x8c / +0xb0, -1) and two words.
// BonusCharactersMenu: +0x3c 1.0, +0x40, +0x44.. seven words, +0x60.. seven bytes, +0x68 / +0x6c / +0x70 the name,
// stats and bio texts, +0x74 the image file (-1), +0x78 its buffer, +0x7c its texture, +0x80 the image quad.
// Stored bests (the profile's unlock progress): 0x008dced8.. 13 concept items per level, 0x008dceb8.. six vehicle
// items, 0x008dced0 the characters reached, 0x008dcf0c the bonus materials row 1 item.
//
// Functions that set texts (directly or through the FE_Bonus* image helpers) run every title call at the original's
// ESP (gap entries, as fe_menus.cpp): the text setters leave a stack buffer's address in a heap record.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00012f80 / 0x00013050 / 0x00330b30 / 0x00330cd0 / 0x00330a40 (menu in EAX): the class vtable, the panel +0x34
// deleted (Panel_Delete), the owned UI texts deleted (vtable +0 (1)): materials / concepts +0x38, vehicles +0x38 and
// +0x44, weapons +0x38 and +0x44 (each pointer cleared after, the panel's too), characters +0x38, +0x6c and +0x70;
// then the UIMenu part (UIMenu_FreeItems).
void XB_REGS1 XB_CDECL BonusMaterialsMenu_Dtor(uint8_t* menu);
void XB_REGS1 XB_CDECL BonusWeaponsMenu_Dtor(uint8_t* menu);
void XB_REGS1 XB_CDECL BonusVehiclesMenu_Dtor(uint8_t* menu);
void XB_REGS1 XB_CDECL BonusCharactersMenu_Dtor(uint8_t* menu);
void XB_REGS1 XB_CDECL BonusConceptsMenu_Dtor(uint8_t* menu);
// 0x00013020 / 0x00013150 / 0x000137e0 / 0x000137c0 / 0x00013800: the destructor, then Z_Free with flags bit 0.
// Return the menu.
uint8_t* XB_THISCALL BonusMaterialsMenu_DeletingDtor(uint8_t* menu, uint32_t flags);
uint8_t* XB_THISCALL BonusWeaponsMenu_DeletingDtor(uint8_t* menu, uint32_t flags);
uint8_t* XB_THISCALL BonusVehiclesMenu_DeletingDtor(uint8_t* menu, uint32_t flags);
uint8_t* XB_THISCALL BonusCharactersMenu_DeletingDtor(uint8_t* menu, uint32_t flags);
uint8_t* XB_THISCALL BonusConceptsMenu_DeletingDtor(uint8_t* menu, uint32_t flags);

// 0x00330de0 / 0x00330ab0 / 0x00330bb0 / 0x00330c40 / 0x00330d60 (owner in EAX, menu in ECX): a 6 / 13 / 6 / 10 /
// 7-row UIMenu at (320, 240), layer 8, and the fields above. Return the menu.
uint8_t* XB_REGS3 XB_CDECL BonusMaterialsMenu_Construct(uint8_t* owner, uint32_t unused_edx, uint8_t* menu);
uint8_t* XB_REGS3 XB_CDECL BonusConceptsMenu_Construct(uint8_t* owner, uint32_t unused_edx, uint8_t* menu);
uint8_t* XB_REGS3 XB_CDECL BonusVehiclesMenu_Construct(uint8_t* owner, uint32_t unused_edx, uint8_t* menu);
uint8_t* XB_REGS3 XB_CDECL BonusWeaponsMenu_Construct(uint8_t* owner, uint32_t unused_edx, uint8_t* menu);
uint8_t* XB_REGS3 XB_CDECL BonusCharactersMenu_Construct(uint8_t* owner, uint32_t unused_edx, uint8_t* menu);

// 0x0031d820: the panel, the help bar +0x38 and the three texts (+0x3c draw), the image quad +0x80
// (nglListAddQuad), then UIMenu_Draw.
void XB_THISCALL BonusCharactersMenu_Draw(uint8_t* menu);

// The gap-entry functions below: the entry hands the C version the menu (ECX), the entry EBX / EDI / ESI (the
// title callees see them as in the original) and the entry ESP.
#define BONUS_GAP_FUNCTION(name)                                                                               \
    extern "C" PLATFORM_HIDDEN void XB_CDECL name(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t esi,       \
                                                   uint32_t entry_esp);                                         \
    extern "C" void name##_entry(void);

// 0x000130d0 / 0x00013170 / 0x00013200 (ret 4): in full screen: the help bar BONUS_HELPBAR_FULLSCREEN
// (vehicles / concepts: moved to (0, 0)), full screen off, the image freed and loaded again; else back to the bonus
// materials screen (the owner's +0x0c (8), a tail jump with the pad slot).
BONUS_GAP_FUNCTION(BonusWeaponsMenu_OnBack)
BONUS_GAP_FUNCTION(BonusVehiclesMenu_OnBack)
BONUS_GAP_FUNCTION(BonusConceptsMenu_OnBack)
// 0x00013120 / 0x000131c0 / 0x00013250 (ret 4): the help bar BONUS_HELPBAR_BACK_ONLY (vehicles / concepts: moved to
// (200, 0)), full screen on, the image loaded.
BONUS_GAP_FUNCTION(BonusWeaponsMenu_OnA)
BONUS_GAP_FUNCTION(BonusVehiclesMenu_OnA)
BONUS_GAP_FUNCTION(BonusConceptsMenu_OnA)

// 0x00312cf0 (ret 4): nglResetDeviceBindings, the texture +0x7c released, the image file +0x74 closed (and its buffer
// +0x78 freed), +0x78 cleared, then row 0 selected (+0x88 (0, 1)).
BONUS_GAP_FUNCTION(BonusCharactersMenu_OnLeave)
// 0x00312d50 (ret 4): FE_BonusMainFreeImage.
BONUS_GAP_FUNCTION(BonusMaterialsMenu_OnLeave)
// 0x0031bbc0 / 0x0031c7b0 / 0x0031d520 (ret 4): the image freed, row 0 selected (+0x88 (0, 1)); in full screen as
// OnBack's first path. Vehicles first set 0x0095f478's +0x88 (1.0) and the dialog overlay's (0x0095f8f8 +0x1c) label
// FEMENU_XBOX_LIVE.
BONUS_GAP_FUNCTION(BonusConceptsMenu_OnLeave)
BONUS_GAP_FUNCTION(BonusVehiclesMenu_OnLeave)
BONUS_GAP_FUNCTION(BonusWeaponsMenu_OnLeave)

// 0x0031b930 / 0x0031b960 (ret 4; nothing in full screen): the selected level's next item (0x003129e0: EAX = 1,
// stdcall menu) / previous item (down to 1), then the level texts (0x00312b40, stdcall menu) and the image.
BONUS_GAP_FUNCTION(BonusConceptsMenu_OnRight)
BONUS_GAP_FUNCTION(BonusConceptsMenu_OnLeft)
// 0x0031b9b0 / 0x0031ba20 (ret 4; nothing in full screen): the next / previous row (+0xb4 / +0xb0); when it changed,
// the new level's item at least 1, the stored bests raised to the current items, the level texts and the image.
BONUS_GAP_FUNCTION(BonusConceptsMenu_OnDown)
BONUS_GAP_FUNCTION(BonusConceptsMenu_OnUp)

// 0x0031c260 / 0x0031c290 (ret 4; nothing in full screen): the category's next / previous item (0x0031bc50: menu in
// EAX, direction 1 / 2 in EDI), the category texts (0x0031c0e0, stdcall menu) and the image.
BONUS_GAP_FUNCTION(BonusVehiclesMenu_OnRight)
BONUS_GAP_FUNCTION(BonusVehiclesMenu_OnLeft)
// 0x0031c2c0 / 0x0031c380 (ret 4; nothing in full screen): the next / previous row; when it changed, the new
// category's item at least 1, the six stored bests raised (0x008dceb8.. against +0x70, +0x74, +0x80, +0x7c, +0x78,
// +0x84), the category texts and the image.
BONUS_GAP_FUNCTION(BonusVehiclesMenu_OnDown)
BONUS_GAP_FUNCTION(BonusVehiclesMenu_OnUp)
// 0x0031c830 / 0x0031d570: row 0 (+0x22 = 0, +0x88 (0, 1)), its item 1, full screen off, the category labels
// (FE_BonusVehicleCategoryLabel: ESI / FE_BonusWeaponCategoryLabel: EAX) and the image, then UIMenu_RestoreSelection
// (tail jump).
BONUS_GAP_FUNCTION(BonusVehiclesMenu_RestoreSelection)
BONUS_GAP_FUNCTION(BonusWeaponsMenu_RestoreSelection)

// 0x0031d030 / 0x0031d060 (ret 4; nothing in full screen): the category's next / previous item (0x0031c870: menu in
// EAX, direction 1 / 2 in EDI), the category texts (0x0031ceb0: menu in EDI) and the image.
BONUS_GAP_FUNCTION(BonusWeaponsMenu_OnRight)
BONUS_GAP_FUNCTION(BonusWeaponsMenu_OnLeft)
// 0x0031d090 / 0x0031d0d0 (ret 4; nothing in full screen): the next / previous row; when it changed, 0x0031c870 with
// direction 0 (the stored bests), the category texts and the image.
BONUS_GAP_FUNCTION(BonusWeaponsMenu_OnDown)
BONUS_GAP_FUNCTION(BonusWeaponsMenu_OnUp)

// 0x0031d6f0 (ret 4): the next row; when it changed, the characters reached (0x008dced0) at least row + 1, the
// character labels (0x0031d6b0: menu in EDI) and the image. 0x0031d730: the previous row; the image when it changed.
BONUS_GAP_FUNCTION(BonusCharactersMenu_OnDown)
BONUS_GAP_FUNCTION(BonusCharactersMenu_OnUp)
// 0x0031d980: the image file closed state (+0x74 = -1, +0x78 / +0x7c cleared), row 0 (+0x22 = 0, +0x88 (0, 1)), the
// characters reached at least 1, FE_BonusCharacterLabel, the image, then UIMenu_RestoreSelection (tail jump).
BONUS_GAP_FUNCTION(BonusCharactersMenu_RestoreSelection)

// 0x0031dcb0 / 0x0031dce0 (ret 4): the next / previous row; the image (FE_BonusMainLoadImage) when it changed.
BONUS_GAP_FUNCTION(BonusMaterialsMenu_OnDown)
BONUS_GAP_FUNCTION(BonusMaterialsMenu_OnUp)
// 0x0031dd10 (ret 4): on row 1, the next item while below the count +0x78: the move sound (+0x10), the image, the
// stored best 0x008dcf0c raised to item + 1, +0x61 = best != count. 0x0031dd60: on row 1, the previous item down to 0
// with the sound and the image.
BONUS_GAP_FUNCTION(BonusMaterialsMenu_OnRight)
BONUS_GAP_FUNCTION(BonusMaterialsMenu_OnLeft)

// 0x00335730 (index in EAX, menu in ECX; called from the accept thunk 0x00012ff0): unless a movie plays (+0x3c), the
// row into +0x80; rows 0 and 1 play the selected bonus movie (0x00335640, menu in EDI), 2-5 open the front end's
// screens 9 .. 12 (+0x0c), others nothing.
extern "C" PLATFORM_HIDDEN void XB_CDECL BonusMaterialsMenu_HandleSelect(uint8_t* menu, int32_t index, uint32_t ebx,
                                                                         uint32_t edi, uint32_t esi,
                                                                         uint32_t entry_esp);
extern "C" void BonusMaterialsMenu_HandleSelect_entry(void);

}  // namespace game
