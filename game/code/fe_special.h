#pragma once

// The remaining front-end menus: the single player hub (SinglePlayerMenu, vtable 0x00829410, 5 rows), the difficulty
// choice (DifficultyMenu, 0x00828c50), the chapter grid (ChapterMenu, 0x008299f0, 14 chapters), the collector's
// edition materials (SpecialMaterialsMenu, 0x00828ac0, 5 rows) and art (SpecialConceptMenu, 0x00829ed8, 6
// categories), and the Xbox Live menu (XboxLiveMenu, 0x0083c7e0, 3 rows). UIMenus (ui_menu.h) built by
// FrontEndMenu_Construct; methods called through their vtables (thiscall; the button handlers take the pad on the
// stack, unused). +0x33 = 5 in all.
//
// SinglePlayerMenu: +0x34 a UI text, +0x38 the panel, +0x3c, +0x40 0.0, +0x44 cleared by every update, +0x48.
// DifficultyMenu: +0x34 the panel, +0x38 a UI text.
// ChapterMenu (a grid menu): +0x34 the panel, +0x38, then six arrays of 14 UI elements, one per chapter: +0x3c..
// (alpha 1.0 on chapters past the unlocked count but the first), +0x74.., +0xac.. (these three placed by x, y),
// +0xe4.. / +0x11c.. / +0x154.. (placed by one coordinate, +0xc8 / +0x80); +0x190 the selected chapter, +0x194 the
// previous one, +0x198 the selected text's x, +0x19c 0.3, +0x1a0 320.0, +0x1a4 1.2 (the selected texts' +0x64),
// +0x1a8 moving, +0x1ac the chapter to select, +0x1b0 a button sequence is on, +0x1b4 its state (left: 4-5, right:
// 6, 7, 9, 11, button 100: 8, 10; at 12 button 100 unlocks all 14 chapters and sets 0x008dce54).
// SpecialMaterialsMenu: +0x34 the panel, +0x38 a UI text, +0x40 / +0x44, +0x48 row 2's item (0-2), +0x4c..+0x58,
// +0x5c the image file (-1), +0x60 its buffer, +0x64 its texture.
// SpecialConceptMenu: +0x34 the panel, +0x38 the help bar, +0x3c, +0x40 full screen, +0x44 the image file (-1),
// +0x48 its buffer, +0x4c its texture, +0x128 / +0x12c, +0x130.. the current item per category (1-based).
// XboxLiveMenu: +0x34 the panel, +0x38 a UI text, +0x3c -1, +0x40 / +0x48 / +0x49 bytes, +0x44.
// Globals: 0x008dce44 the chapters unlocked (at most 13 here), 0x008dce48 the current chapter.
//
// Functions that reach the FE_BonusCE* image helpers or a text setter run every title call at the original's ESP
// (gap entries, as fe_bonus.cpp).

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x000125b0 / 0x00012ea0 / 0x00013460 / 0x000134e0 / 0x00013580 (menu in EAX): the class vtable, the panel
// deleted (as Panel_Delete: SinglePlayer +0x38, the others +0x34), the UI text deleted (vtable +0 (1): SinglePlayer
// +0x34, Difficulty / SpecialMaterials / SpecialConcept +0x38; the concept menu clears both pointers), then the
// UIMenu part (UIMenu_FreeItems).
void XB_REGS1 XB_CDECL SinglePlayerMenu_Dtor(uint8_t* menu);
void XB_REGS1 XB_CDECL DifficultyMenu_Dtor(uint8_t* menu);
void XB_REGS1 XB_CDECL ChapterMenu_Dtor(uint8_t* menu);
void XB_REGS1 XB_CDECL SpecialMaterialsMenu_Dtor(uint8_t* menu);
void XB_REGS1 XB_CDECL SpecialConceptMenu_Dtor(uint8_t* menu);
// 0x00012620 / 0x00012f60 / 0x000134c0 / 0x00013560 / 0x00013650: the destructor, then Z_Free with flags bit 0.
// Return the menu.
uint8_t* XB_THISCALL SinglePlayerMenu_DeletingDtor(uint8_t* menu, uint32_t flags);
uint8_t* XB_THISCALL DifficultyMenu_DeletingDtor(uint8_t* menu, uint32_t flags);
uint8_t* XB_THISCALL ChapterMenu_DeletingDtor(uint8_t* menu, uint32_t flags);
uint8_t* XB_THISCALL SpecialMaterialsMenu_DeletingDtor(uint8_t* menu, uint32_t flags);
uint8_t* XB_THISCALL SpecialConceptMenu_DeletingDtor(uint8_t* menu, uint32_t flags);

// 0x00223ed0 / 0x00330560 / 0x003305e0 / 0x00331210 (owner in EAX, menu in ECX): a 3 / 6 / 5 / 5-row UIMenu at
// (320, 240), layer 8, and the fields above (the Xbox Live menu repeats: flag 7). 0x00333380: a 14-item grid menu
// (UIGridMenu_Init, repeats). Return the menu.
uint8_t* XB_REGS3 XB_CDECL XboxLiveMenu_Construct(uint8_t* owner, uint32_t unused_edx, uint8_t* menu);
uint8_t* XB_REGS3 XB_CDECL SpecialConceptMenu_Construct(uint8_t* owner, uint32_t unused_edx, uint8_t* menu);
uint8_t* XB_REGS3 XB_CDECL SpecialMaterialsMenu_Construct(uint8_t* owner, uint32_t unused_edx, uint8_t* menu);
uint8_t* XB_REGS3 XB_CDECL SinglePlayerMenu_Construct(uint8_t* owner, uint32_t unused_edx, uint8_t* menu);
uint8_t* XB_REGS3 XB_CDECL ChapterMenu_Construct(uint8_t* owner, uint32_t unused_edx, uint8_t* menu);

// 0x00222750: the panel, the text +0x38 (+0x3c draw); unless 0x0095f916, the playing movie (0x0095f454: vtable +8,
// the subtitles, its frame texture onto the quad 0x0095f458 +0x60, nglListAddQuad); then UIMenu_Draw (tail jump).
void XB_THISCALL XboxLiveMenu_Draw(uint8_t* menu);

// 0x00336460 (ret 4): SCR_LoadingPump; unless 0x0095f91e, UIMenu_Update (dt) and +0x44 cleared.
void XB_THISCALL SinglePlayerMenu_Update(uint8_t* menu, float dt);

// 0x0030c6f0 (menu in EDI, dt on the stack, ret 4): one step of the chapter list's slide: the selected chapter's x
// moves by (320 - start x +0x198) * dt / duration +0x19c, or the rest of the way once past 320 (+0x1a8 = 0); every
// chapter's elements shifted; the selected chapter's picture fades in / grows towards +0x1a4, the previous one's back.
extern "C" PLATFORM_HIDDEN void XB_CDECL ChapterMenu_SlideStep(uint8_t* menu, float dt);
extern "C" void ChapterMenu_SlideStep_entry(void);
// 0x0030c9a0 (menu in EDI): with a panel: the unlocked / current chapter, the selection +0x22 and +0x1ac clamped to
// 13; per chapter the +0x3c element's alpha (+0x7c) and the item's +0x20 (1 for chapters past the unlocked count
// but the first, else 0); the selected chapter +0x190 = +0x1ac (0..13), its x (+0x74 element's +0x9c) into +0x198;
// every chapter's six elements moved by 320 - x (x87); the selected +0xac element's alpha 1.0 and both selected
// texts' +0x64 = +0x1a4.
extern "C" PLATFORM_HIDDEN void XB_CDECL ChapterMenu_UpdateList(uint8_t* menu);
extern "C" void ChapterMenu_UpdateList_entry(void);
// 0x0030cc20 (ret 4): the button sequence: state 8 / 10 one on; at 12 all 14 chapters unlocked (0x008dce44 = 14,
// 0x008dce54 = 1), every +0x3c element's alpha 0 and item +0x20 (0).
void XB_THISCALL ChapterMenu_OnButton100(uint8_t* menu, uint32_t pad);
// 0x0031ae10 / 0x0031ae90 (ret 4): the button sequence (right: 6, 7, 9, 11 / left: 4, 5 one on); unless on the
// last / first chapter, the grid move (UIGridMenu_MoveRight / Left); when it changed: +0x190 / +0x194 the new / old
// chapter, moving, the new chapter's x (+0x74 element's +0x9c) into +0x198, the move sound (+0x10).
void XB_THISCALL ChapterMenu_OnRight(uint8_t* menu, uint32_t pad);
void XB_THISCALL ChapterMenu_OnLeft(uint8_t* menu, uint32_t pad);

// 0x003122e0 (ret 4): nglResetDeviceBindings, the texture +0x4c released and cleared, the image file +0x44 closed
// (and its buffer +0x48 freed), +0x48 cleared, then row 0 selected (+0x88 (0, 1)).
void XB_THISCALL SpecialConceptMenu_OnLeave(uint8_t* menu, uint32_t pad);
// 0x00312340 (ret 4): the same for +0x64 / +0x5c / +0x60 (the texture pointer kept), no selection.
void XB_THISCALL SpecialMaterialsMenu_OnLeave(uint8_t* menu, uint32_t pad);

// The gap-entry functions below: the entry hands the C version the menu (ECX), the entry EBX / EDI / ESI (the
// title callees see them as in the original) and the entry ESP.
#define SPECIAL_GAP_FUNCTION(name)                                                                             \
    extern "C" PLATFORM_HIDDEN void XB_CDECL name(uint8_t* menu, uint32_t ebx, uint32_t edi, uint32_t esi,       \
                                                   uint32_t entry_esp);                                         \
    extern "C" void name##_entry(void);

// 0x00013630 (ret 4): the help bar BONUS_HELPBAR_BACK_ONLY, full screen on, the image (FE_BonusCELoadImage, menu in
// EAX).
SPECIAL_GAP_FUNCTION(SpecialConceptMenu_OnA)
// 0x0031a750 (ret 4; nothing in full screen): the category's next item (0x0031a390: EAX = 1, menu in ECX / ESI),
// the image. 0x0031a770: the previous item down to 1 with the move sound (+0x10), the image.
SPECIAL_GAP_FUNCTION(SpecialConceptMenu_OnRight)
SPECIAL_GAP_FUNCTION(SpecialConceptMenu_OnLeft)
// 0x0031a7b0 / 0x0031a800 (ret 4; nothing in full screen): the next / previous row (+0xb4 / +0xb0); when it
// changed, the new category's item at least 1 and the image.
SPECIAL_GAP_FUNCTION(SpecialConceptMenu_OnDown)
SPECIAL_GAP_FUNCTION(SpecialConceptMenu_OnUp)
// 0x0031a8c0: row 0 (+0x22 = 0, its item 1, +0x88 (0, 1)), full screen off, the category labels
// (FE_BonusArtCategoryLabel: ESI) and the image, then UIMenu_RestoreSelection (tail jump).
SPECIAL_GAP_FUNCTION(SpecialConceptMenu_RestoreSelection)
// 0x0031aa20 / 0x0031aa50 (ret 4): on row 2, the next item below 2 / previous above 0: the image
// (FE_BonusCEMainLoadImage, menu in EAX), then the move sound (+0x10).
SPECIAL_GAP_FUNCTION(SpecialMaterialsMenu_OnRight)
SPECIAL_GAP_FUNCTION(SpecialMaterialsMenu_OnLeft)
// 0x0031aa80 / 0x0031aab0 (ret 4): the next / previous row; the image when it changed.
SPECIAL_GAP_FUNCTION(SpecialMaterialsMenu_OnDown)
SPECIAL_GAP_FUNCTION(SpecialMaterialsMenu_OnUp)

}  // namespace game
