#pragma once

// The UI menu framework: menus (UIMenu, vtable 0x0082a628; grid menus 0x008294d8), their items (UIMenuItem, vtable
// 0x008290c8; option items 0x00829680), the screen managers (UIScreenMgr, vtable 0x0082a5f8: the front end, the
// in-game screens and the dialog manager), dialogs, the button-token glyphs and the font tables. Title C++ objects
// whose methods are called through their vtables (thiscall).
//
// UIMenu: +0x04 items, +0x08 owner, +0x0c x, +0x10 y, +0x14 row spacing, +0x18 half the list height, +0x1c repeat
// timer, +0x20 first shown item, +0x22 selection, +0x24 saved selection, +0x26 item count, +0x28 rows shown, +0x2a
// flags (bit 0 scrolling, 1 wraps, 3 selection may land on hidden items, 6 skips hidden items, 7 repeats, 8 fast
// repeat, 9 laid out, 10 top-aligned, 11 bottom-aligned), +0x2c sound handle, +0x30 locked, +0x31 plays the move
// sound, +0x32 repeat state (0xff idle), +0x33 the owner's +0x24 result. vtable slots: +0x10 move sound, +0x1c
// refresh, +0x34 select default, +0x38 accept (index), +0x88 select (index, with sound), +0x8c scroll to (first),
// +0xc0 repeat.
// UIMenuItem: +0x04 menu, +0x08..+0x0e neighbours up/down/left/right (grid menus; -1 none), +0x10 the text, +0x14
// colour scheme (the menu's +0xa4), +0x15 selected, +0x16 highlighted, +0x17 owns the text. vtable slots: +0x0c draw,
// +0x10 update (dt), +0x24 hidden, +0x2c set position (x, y), +0x5c / +0x60 passed on by the menu, +0x90 colour with
// alpha (out, colour, alpha), +0x94 update colour.
// UIScreenMgr: +0x04 screens, +0x08 owner, +0x0c active screen (-1 none), +0x10 capacity, +0x14 count, +0x18 held
// stick directions (bits 4-32), +0x1a cleared. vtable slots: +0x14 update, +0x18 create / lay out screens, +0x28 /
// +0x2c button down / up (bit, pad). Screen slots: +0x1c layout, +0x30 update (dt), +0x3c activated (previous
// index), +0x44 deactivated (next screen), +0x48..+0x7c the 14 button handlers (pad), +0x80 any button (pad, bit).

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00011ba0 (item in ESI, text in EDI, menu in ECX; owns on the stack, ret 4): the item constructor around an
// existing text: neighbours -1, the menu's colour scheme, the text shown (+0x40 (1)), the colour updated. Returns
// the item.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL UIMenuItem_Ctor(uint8_t* item, uint8_t* text, uint8_t* menu,
                                                             uint8_t owns);
extern "C" void UIMenuItem_Ctor_entry(void);
// 0x00011e70: deletes the owned text, frees the item with flags bit 0.
uint8_t* XB_THISCALL UIMenuItem_DeletingDtor(uint8_t* item, uint32_t flags);
// 0x0032a7e0 (kind in EAX, item in EDI; string, menu, mode on the stack, ret 0xc): a new text (mode 1: a 0x60-byte
// UI text base, else a 0x98-byte UI text with vtable +0x13c (mode) and +0x78 (string)) of the kind (8: the menu's
// +0xa8 record's +8), then UIMenuItem_Ctor's fields; the text hidden again after the colour update. Returns the
// item.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL UIMenuItem_Init(int32_t kind, uint8_t* item, const char* string,
                                                             uint8_t* menu, int32_t mode);
extern "C" void UIMenuItem_Init_entry(void);
// 0x0031fd80: the text's colour from the scheme +0x14 (11: the text's own colour at alphas 0x8c / 0xff; else the
// 16-byte records at 0x0095fd30: normal, selected, pulse, no pulse flag), by selected / highlighted and the menu
// flags 3-5; highlighted items at alpha 0x64.
void XB_THISCALL UIMenuItem_UpdateColor(uint8_t* item);

// 0x0032e310 (item in EBX): the "UI_Select" sound unless the item's sound (+0x24) still plays (voice states 2-5);
// +0x28 enables it. The handle back into +0x24 (0 without the alias).
extern "C" PLATFORM_HIDDEN void XB_CDECL UIOptionItem_PlaySelectSound(uint8_t* item);
extern "C" void UIOptionItem_PlaySelectSound_entry(void);
// 0x0032e540: option +0x18 one on (wrapping at the count +0x1c), the select sound, the text (+0x78) = the option's
// string (+0x20 ScriptString array) or null. Returns the text call's result | 0xffff.
uint32_t XB_THISCALL UIOptionItem_Next(uint8_t* item);
// 0x0032e5e0 (stdcall, ret 4): releases the option strings (vector-new block), stops the item's sound channel, then
// the menu item's part (deletes an owned text).
void XB_STDCALL UIOptionItem_Dtor(uint8_t* item);

// 0x00012320 (screen manager in EAX; label, handler on the stack, ret 8): the dialog's (+0x04 [+0x0c != 0]) next
// button (+0x34) gets the label (vtable +0x30), the handler (+0x38 + n * 4) and is shown (+0x20 (0)).
void XB_REGS1 XB_STDCALL UI_DialogAddButton(uint8_t* mgr, uint32_t label, uint32_t handler);
// 0x00012370: the dialog's selection +0x22 = index and +0x5a = 1.
void XB_REGS1 XB_STDCALL UI_DialogSetDefaultButton(uint8_t* mgr, int16_t index);
// 0x00012390: Dialog_LayoutButtons (0x0031f4b0: flag in AL, dialog in EDI) on the manager's dialog.
void XB_REGS1 XB_STDCALL UI_DialogLayoutButtons(uint8_t* mgr, uint8_t flag);

// 0x00013690 / 0x0032eab0: UIMenu_FreeItems (frees the menu with flags bit 0).
uint8_t* XB_THISCALL UIMenu_DeletingDtor(uint8_t* menu, uint32_t flags);
void XB_THISCALL UIMenu_Dtor(uint8_t* menu);
// 0x0030ef20 (ret 4, the argument unused): accept (vtable +0x38) the selection when it is a shown item.
void XB_THISCALL UIMenu_OnAccept(uint8_t* menu, uint32_t unused);
// 0x0030ef50: selects (+0x88 (index, 0)) the first shown item.
void XB_THISCALL UIMenu_SelectFirst(uint8_t* menu);
// 0x0030ef90 / 0x0030efd0: every item's vtable +0x60 (a, b) / +0x5c (a).
void XB_THISCALL UIMenu_SetItemsLayer(uint8_t* menu, uint32_t a, uint32_t b);
void XB_THISCALL UIMenu_SetItemsScale(uint8_t* menu, uint32_t a);
// 0x00314b20: the selection (or the saved one) re-selected when shown, else the default (+0x34); repeat idle; then
// scroll to it (scrolling menus) or refresh (+0x1c, flag 6).
void XB_THISCALL UIMenu_RestoreSelection(uint8_t* menu);
// 0x00314ba0: with repeat (flag 7) active the timer +0x1c counts down by dt and at 0 repeats (+0xc0) and restarts
// at 0.1 (flag 8: 0.01); then every item's update (+0x10 (dt)).
void XB_THISCALL UIMenu_Update(uint8_t* menu, float dt);
// 0x00314c30: draws (+0x0c) every shown item, or for a scrolling menu the rows shown from the first item on
// (wrapping with flag 1).
void XB_THISCALL UIMenu_Draw(uint8_t* menu);
// 0x00314ce0: half height +0x18 from the rows (scrolling when there are more items than rows), selects item 0, sets
// flag 9, then scrolls to 0 or places every shown item (x, y + row * spacing - half; top / bottom alignment).
void XB_THISCALL UIMenu_Layout(uint8_t* menu);
// 0x00314e70: first shown = first; places the rows shown from there (as UIMenu_Draw walks them).
void XB_THISCALL UIMenu_ScrollTo(uint8_t* menu, int32_t first);
// 0x00314f40 / 0x00315080: the previous / next shown item (wrapping with flag 1; scrolling at the first / last row),
// the move sound (+0x10) when it changed, then select (+0x88 (index, 1)). Nothing while locked (+0x30).
void XB_THISCALL UIMenu_SelectPrev(uint8_t* menu);
void XB_THISCALL UIMenu_SelectNext(uint8_t* menu);
// 0x0032e7a0: the "UI_Select" sound unless the menu's sound (+0x2c) still plays. 0x0032e8f0: always.
void XB_THISCALL UIMenu_PlaySelectSoundIfIdle(uint8_t* menu);
void XB_THISCALL UIMenu_PlaySelectSound(uint8_t* menu);

// 0x00331dc0 (menu in ESI, item count in EDI; owner, flags on the stack, ret 8): the grid menu constructor (as
// UIMenu_Init with one row; owner->+0x24 called without a null check). Returns the menu.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL UIGridMenu_Init(uint8_t* menu, int32_t count, uint8_t* owner,
                                                             uint32_t flags);
extern "C" void UIGridMenu_Init_entry(void);
// 0x003148b0 / 0x00314920 / 0x00314990 / 0x00314a00: selects (+0x88 (index, 1)) the right / left / down / up
// neighbour (+0x0e / +0x0c / +0x0a / +0x08 of the item), past hidden ones unless flag 3.
void XB_THISCALL UIGridMenu_MoveRight(uint8_t* menu);
void XB_THISCALL UIGridMenu_MoveLeft(uint8_t* menu);
void XB_THISCALL UIGridMenu_MoveDown(uint8_t* menu);
void XB_THISCALL UIGridMenu_MoveUp(uint8_t* menu);

// 0x003147d0 (manager in ESI, capacity in EAX; owner on the stack, ret 4): the screen manager constructor: the
// screen array (Heap_Alloc16, not cleared), no active screen, then +0x18 (create) and every screen's +0x1c. Returns
// the manager.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL UIScreenMgr_Init(uint8_t* mgr, int32_t capacity, uint8_t* owner);
extern "C" void UIScreenMgr_Init_entry(void);
// 0x00314790: deletes every screen but index 16, frees the array.
void XB_THISCALL UIScreenMgr_Dtor(uint8_t* mgr);
// 0x0030eea0: the active screen's update (+0x30 (dt)), then the manager's (+0x14).
void XB_THISCALL UIScreenMgr_Update(uint8_t* mgr, uint32_t dt);
// 0x0030eed0: +0x18, then every screen's +0x1c.
void XB_THISCALL UIScreenMgr_LayoutAll(uint8_t* mgr);
// 0x0032e6a0: the old screen's +0x44 (the new one), the new one's +0x3c (the old index) and the "UI_Highlight"
// sound, then +0x18; active = index.
void XB_THISCALL UIScreenMgr_SetActiveScreen(uint8_t* mgr, int32_t index);
// 0x0030eb40: the pad button number of a UI button bit (1 -> 15, 4 -> 3, 8 -> 1, 0x10 -> 0, 0x20 -> 2, 0x40 -> 5,
// 0x80 -> 6, 0x100 -> 4, 0x200 -> 7, 0x400 -> 9, 0x800 -> 8, 0x1000 -> 11, 0x2000 -> 10, else 14).
int32_t XB_REGS1 XB_CDECL UIScreenMgr_PadBitToButton(int32_t bit);
// 0x0030ec70: the active screen's +0x80 (pad, bit), then the handler of the bit (+0x48 + 4 * log2 bit) with the pad.
void XB_THISCALL UIScreenMgr_OnButtonDown(uint8_t* mgr, int32_t bit, int32_t pad);
// 0x00314750: held stick directions +0x18 (bits 4-32) from 0x0030ea00.
void XB_THISCALL UIScreenMgr_ReadStickDirections(uint8_t* mgr);
// 0x0031fc10: per UI button bit (1 .. 0x2000): without an active pad (0x008d50d8 = -1) the first pad pressing it
// becomes active (also 0x008dcf18 and the live manager's +0x44d0); stick directions (bits 4-32) by their held state
// (+0x18), others by the pad's pressed / released edges: button down (+0x28) / up (+0x2c) (bit, pad), the first
// one found only.
void XB_THISCALL UIScreenMgr_HandleInput(uint8_t* mgr);

// 0x0030f9b0 (token in EAX, text in ECX): whether the text starts with the token (text capitals match the token's
// lower case; the text's NUL ends the match early); then *out = glyph, *length = the token's length.
uint8_t XB_REGS3 XB_CDECL UI_MatchButtonToken(const char* token, uint32_t unused_edx, const char* text,
                                              uint32_t* out, uint32_t glyph, int32_t* length);
// 0x00315620 (text in EDI, out in ESI): for a '~' button token ("~cross" .. "~navig_all") the glyph string into
// *out and the token's length; 0 (and *out = "" once the text starts with '~') otherwise.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL UI_TranslateButtonToken(const char* text, uint32_t* out);
extern "C" void UI_TranslateButtonToken_entry(void);

// 0x00310e10 (stdcall, ret 4): black bars 0-80 and 560-640 (full height) just in front of the depth of the
// object's +0x0c element (vtable +0x28, minus 100).
void XB_STDCALL UI_DrawPillarboxBars(uint8_t* object);

// 0x0030f380 (name in EAX, font in ESI, table in EDI): the font into the table slot (+4 + i * 4, loaded flag +0x3c
// + i) of the named font (R_FindFont of a 31-character copy). 0x00315360 (name in EAX, font in ESI): the same for
// ui_fonts (0x0095f8d4 / 0x0095f90c). Both return the index (8 when absent).
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL UIFontTable_SetByName(const char* name, uint8_t* font, uint8_t* table);
extern "C" void UIFontTable_SetByName_entry(void);
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL UI_SetFontByName(const char* name, uint8_t* font);
extern "C" void UI_SetFontByName_entry(void);
// 0x00320120 (index in EDI; table on the stack, ret 4): the font table slot = nglFindFontAddRef of the renderer
// font name (0x0095f7e8 + index * 32, hashed; nglFindTexture of it first, result unused).
extern "C" PLATFORM_HIDDEN void XB_CDECL UIFontTable_AcquireFont(int32_t index, uint8_t* table);
extern "C" void UIFontTable_AcquireFont_entry(void);

// 0x0032a0e0 (scroll bar in EDI): two black UI texts (+0x28 at the origin with +0x13c (3), +0x2c at (-40, -40)
// with +0x110 (0.8); flag 0x40, +0xe4 (20)), the pictures scroll_back (+0x08), scroll_arrow_top (+0x00),
// scroll_arrow_bottom (+0x04) and scroll_indicator (+0x0c) with one grey quad each, +0x34 = 472.5.
extern "C" PLATFORM_HIDDEN void XB_CDECL UIScrollBar_Construct(uint8_t* bar);
extern "C" void UIScrollBar_Construct_entry(void);

// 0x003319e0 (name in EAX; manager, ok, yes_no, title, keep_running on the stack, ret 0x14): a dialog: the localized
// text of the name (rm_locStrings; "NO STRING" for a null entry text; the name itself when that is empty) and the
// title, CL_PauseForDialog, UIDialog_SetText (0x00313f20, the manager's +0x1c in ESI) and 0x0030e6f0 (the dialog in
// EDI) with by-value copies of both, then an OK button (handler 0x00313dc0) or YES / NO (0x00313dc0 / 0x00313db0)
// and Dialog_LayoutButtons (AL = ok).
void XB_REGS1 XB_STDCALL UI_ShowDialog(const char* name, uint8_t* mgr, uint8_t ok, uint8_t yes_no, const char* title,
                                       uint8_t keep_running);

}  // namespace game
