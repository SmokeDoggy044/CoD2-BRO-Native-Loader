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

// 0x000136c0: UIOptionItem_Dtor, then Z_Free with flags bit 0. Returns the item.
uint8_t* XB_THISCALL UIOptionItem_DeletingDtor(uint8_t* item, uint32_t flags);

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
// 0x00013720: the startup storage check screen (vtable 0x00829e10, built by 0x003308a0 in FrontEndMenu_Construct; its
// update runs FE_CheckStartupStorage): listener +0x34 (Save_SetDeviceRemovedListener) reset, the UIMenu part.
uint8_t* XB_THISCALL StartupStorageMenu_DeletingDtor(uint8_t* menu, uint32_t flags);
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

// 0x00011f90: UIScreenMgr_Dtor, then Z_Free with flags bit 0. Returns the manager.
uint8_t* XB_THISCALL UIScreenMgr_DeletingDtor(uint8_t* mgr, uint32_t flags);
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
// The implementation behind UI_ShowDialog_entry (the entry's registers and ESP); UI_ShowDialog calls the title address.
extern "C" PLATFORM_HIDDEN void XB_CDECL UI_ShowDialogAt(const char* name, uint32_t entry, uint32_t ebx, uint32_t ebp,
                                                          uint32_t esi, uint32_t edi);
extern "C" void UI_ShowDialog_entry(void);

// The dialog manager (vtable 0x0082881c, a screen manager over two dialogs; +0x1c the overlay, +0x20 flags (bit 1:
// a second dialog is queued)). The overlay (0x48 bytes): +0x00..+0x0c four elements (the scroll arrows, bar and
// indicator), +0x24 a picture, +0x28 the heading and +0x2c the body (UI texts), +0x30 a panel, +0x34 the indicator
// x, +0x38 the indicator y, +0x3c the scroll offset, +0x40 the fade, +0x44 flags (bits 0 / 1 scrolling up / down,
// bit 2 the text is scrolled, bit 3 closed, bit 4 vertical buttons). The dialogs (UIDialog, vtable 0x00829d48, a
// UIMenu of three buttons): +0x34 button count, +0x38 the handlers, +0x4c the text, +0x50 the heading
// (ScriptStrings), +0x54 -1.0, +0x58 vertical buttons, +0x59 the overlay's layout flag, +0x5a select +0x22 on layout.

// 0x0031f4b0 (vertical in AL, dialog in EDI): the overlay layout (0x00313e40, the button count or 1), every button
// (+0x4c (0)) placed (+0x2c): vertical ones at x 320 below the text (10 lines at most), else two at 280 / 360; then
// the default button (+0x34), or the selection +0x22 when +0x5a asks for it.
extern "C" PLATFORM_HIDDEN void XB_CDECL Dialog_LayoutButtons(uint8_t vertical, uint8_t* dialog);
extern "C" void Dialog_LayoutButtons_entry(void);
// 0x00313f20 (overlay in ESI; text and heading ScriptStrings by value, ret 8): the body wrapped at 280 (newlines
// expanded, +0x14c) and the heading (+0x144), the body colour (black with 0x0095f8f8, else white), the scroll flag
// (more than 10 lines) shows the four scroll elements (+0x34), a scrolled text's offset / indicator reset; both
// texts +0x50 (0x40). Releases both strings.
// The entry hands the C version its entry ESP (the original's frame is rebuilt there).
extern "C" PLATFORM_HIDDEN void XB_CDECL UIDialog_SetText(uint32_t entry, uint8_t* overlay);
extern "C" void UIDialog_SetText_entry(void);
// 0x0031f690: every button's +0x58 (3 with 0x0095f8f8, else 4), then UIMenu_Draw while the overlay's fade is >= 0.
void XB_THISCALL UIDialog_Draw(uint8_t* dialog);
// 0x000122e0: releases the heading and text, UIMenu_FreeItems, frees the dialog with flags bit 0.
uint8_t* XB_THISCALL UIDialog_DeletingDtor(uint8_t* dialog, uint32_t flags);
// 0x00331920 (owner in EAX, dialog in ECX): a three-item UIMenu (layer 8), empty texts, three empty items (mode 1)
// at scale 0.6, flag 6. Returns the dialog.
uint8_t* XB_REGS3 XB_CDECL UIDialog_Construct(uint8_t* owner, uint32_t unused_edx, uint8_t* dialog);
// 0x0032a5c0 (overlay in EDI): deletes the texts, the four elements, the panel and the picture +0x24.
extern "C" PLATFORM_HIDDEN void XB_CDECL DialogMgr_DestroyContents(uint8_t* overlay);
extern "C" void DialogMgr_DestroyContents_entry(void);
// 0x0032df40: the overlay destroyed and freed, both dialogs deleted, then UIScreenMgr_Dtor.
void XB_THISCALL DialogMgr_Dtor(uint8_t* mgr);

// 0x000137a0: DialogMgr_Dtor, then Z_Free with flags bit 0. Returns the manager.
uint8_t* XB_THISCALL DialogMgr_DeletingDtor(uint8_t* mgr, uint32_t flags);
// 0x00331cb0 (manager in EAX): UIScreenMgr_Init with two screens, a cleared overlay, the two dialogs. Returns the
// manager.
uint8_t* XB_REGS1 XB_CDECL DialogMgr_Construct(uint8_t* mgr);
// 0x00331c50: the active dialog's update (+0x30), the manager's (+0x14), the overlay's (0x0032df90 (dt)), then
// +0x20 (1 with 0x0095f8f8, else 4).
void XB_THISCALL DialogMgr_Update(uint8_t* mgr, uint32_t dt);
// 0x0031f890: the active dialog's +0x28, then DialogMgr_DrawOverlay.
void XB_THISCALL DialogMgr_Draw(uint8_t* mgr);
// 0x0031f770 (ret 8): the active dialog's +0x84 (pad, bit); a scrolled text stops scrolling up (bit 4) / down (bit 8)
// with that arrow back at alpha 0.5.
void XB_THISCALL DialogMgr_OnButtonUp(uint8_t* mgr, uint32_t bit, uint32_t pad);
// 0x0031f6e0 (manager in EAX): with the second dialog active and one queued (+0x20 bit 1): back to the first
// (+0x0c (0)), its text and buttons shown again; else the overlay's closed flag (bit 3).
void XB_REGS1 XB_CDECL DialogMgr_CloseDialog(uint8_t* mgr);
// 0x00314340 (overlay in EAX): the background (0x003141c0), then the body and heading in their own scene; a
// scrolled text in the viewport (180, 140) - (460, 340) at the scroll offset, lines from -1 - offset / 20.
void XB_REGS1 XB_CDECL DialogMgr_DrawOverlay(uint8_t* overlay);

// 0x003141c0 (overlay in ESI): in a scene of its own (no clear, no depth): with a dialog up (0x0095f8f8) the quad
// +0x30's +0x18 = -1, +0x10 alpha 1 and drawn, +0x14 alpha 0; else +0x18 = 0, +0x10 alpha 0 and by +0x45 either
// +0x1c / +0x20 (alpha 1, drawn) or +0x14 (alpha 1, drawn); then +0x18 drawn (+0x44). When the pause manager's
// (0x0095f900) current screen (+0x04 [+0x0c != 0]) has +0x34 > 0, the text +0x24 drawn (+0x3c) in a second scene.
extern "C" PLATFORM_HIDDEN void XB_CDECL DialogMgr_DrawBackground(uint8_t* overlay);
extern "C" void DialogMgr_DrawBackground_entry(void);

// Root menus and screen managers. UIRootMenu (vtable 0x00829280, 0x3c bytes, the UI root's +0x24): a UIMenu with
// +0x34 a panel and +0x38 an object. TitleMenu (0x008297f0): +0x34 a panel. MainMenu (0x00829b80, 0x4c bytes): +0x34
// an object, +0x38 a panel. The UI root (vtable 0x0082a5f4, g_uiRoot 0x0095f8d0): +0x04 seven words with flags
// +0x3c.., +0x20 / +0x28 / +0x2c / +0x30 / +0x34 objects (+0x34: the default picture), +0x24 the root menu, +0x54 the
// chapter table (14 records of 0x48 bytes), +0x444 a float.
// 0x00012140 / 0x00012220 / 0x00012520 (menu in EAX): the panel deleted (Panel_Delete), the object deleted, then
// the UIMenu part (UIMenu_FreeItems). 0x000121b0 / 0x00012290 / 0x00012590: the same, freed with flags bit 0.
void XB_REGS1 XB_CDECL UIRootMenu_Dtor(uint8_t* menu);
uint8_t* XB_THISCALL UIRootMenu_DeletingDtor(uint8_t* menu, uint32_t flags);
void XB_REGS1 XB_CDECL TitleMenu_Dtor(uint8_t* menu);
uint8_t* XB_THISCALL TitleMenu_DeletingDtor(uint8_t* menu, uint32_t flags);
void XB_REGS1 XB_CDECL MainMenu_Dtor(uint8_t* menu);
uint8_t* XB_THISCALL MainMenu_DeletingDtor(uint8_t* menu, uint32_t flags);
// 0x003312f0 (owner in EAX, menu in ECX): a five-row UIMenu at (320, 240), layer 8; fields cleared. Returns it.
uint8_t* XB_REGS3 XB_CDECL MainMenu_Construct(uint8_t* owner, uint32_t unused_edx, uint8_t* menu);
// 0x000122b0: UIScreenMgr_Dtor, freed with flags bit 0.
uint8_t* XB_THISCALL InGameScreens_DeletingDtor(uint8_t* mgr, uint32_t flags);
// 0x00313ce0 (ret 4): the active screen's update (+0x30 (dt)), then the manager's (+0x14).
extern "C" PLATFORM_HIDDEN void XB_CDECL InGameScreens_Update(uint32_t entry, uint8_t* mgr, uint32_t dt);
extern "C" void InGameScreens_Update_entry(void);
// 0x00331680 (stdcall): the in-game screens (vtable 0x008286cc, nine): pause, advanced options, the loading screens
// (generic, Africa, Sicily, Maubeuge), mission success / fail, autosave, live options; then +0x18 and every screen's
// +0x1c. Returns the manager.
uint8_t* XB_STDCALL InGameScreens_Construct(uint8_t* mgr);
// 0x00336ad0 (stdcall): the front end's 21 screens (vtable 0x0082905c): title, main, single player, options,
// gameplay, controller, screen, sound, the five bonus menus, difficulty (a grid menu), create profile, profile,
// 0x003308a0, chapter, special materials / concept, Xbox Live; the two background quads (0, 0) - (640, 320) and
// (0, 320) - (640, 480) at z 300; no active screen. Returns the manager.
uint8_t* XB_STDCALL FrontEndMenu_Construct(uint8_t* mgr);
// 0x0030f470: deletes the objects +0x28, +0x2c, +0x30, +0x20, +0x24, +0x34.
void XB_THISCALL UIRoot_Dtor(uint8_t* root);

// 0x00013670: UIRoot_Dtor, then Z_Free with flags bit 0. Returns the root.
uint8_t* XB_THISCALL UIRoot_DeletingDtor(uint8_t* root, uint32_t flags);
// 0x00312f60 (table in EAX): the chapter table cleared, then its fixed values.
void XB_REGS1 XB_CDECL UIRoot_InitChapterTable(uint8_t* table);
// 0x00331e90 (root in ESI): the UI root's fields, chapter table and an empty root menu. Returns the root.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL UIRoot_Construct(uint8_t* root);
extern "C" void UIRoot_Construct_entry(void);
// 0x0032d130 (stdcall, the first argument unused): "%s %d %s" of MEM_ERROR_NOT_ENOUGH_BLOCKS1, the blocks needed
// (0x0031ac10) and MEM_ERROR_NOT_ENOUGH_BLOCKS2 (localized) into out.
void XB_STDCALL MemDlg_FormatNotEnoughBlocks(uint32_t unused, char* out);

// 0x00333e20 / 0x00335460: unless the profiles folder exists or there is room for a save (0x0031ac60), the "not
// enough blocks" dialog (MemDlg_FormatNotEnoughBlocks) with continue without saving (0x003125d0; 0x0031b2e0) and,
// for the second, manage memory (0x00335410). The second calls 0x00312870 on the room path unless 0x008dd1d0.
// Return 1 when there is room, else 0.
uint8_t XB_CDECL MemDlg_CheckFreeSpace3();
uint8_t XB_CDECL MemDlg_CheckFreeSpace2();
// 0x003344e0 (check in AL, menu in ECX): the same when `check` is set, with the blocks needed in the menu's +0x38,
// +0x41 = 1 and the buttons continue (0x00334410) / manage (0x0030d040).
uint8_t XB_REGS3 XB_CDECL MemDlg_CheckFreeSpace(uint8_t check, uint32_t unused_edx, uint8_t* menu);
// 0x003345d0 (device in EAX; present on the stack, ret 4): for no device (-2) or none present the "no save data"
// dialog with continue (0x00334410) and retry (0x00338470); returns 0, else 1.
uint8_t XB_REGS1 XB_STDCALL MemDlg_CheckSaveDevice(int32_t device, uint8_t present);
// Save slots (the save manager: +0x04 the write request, +0x28c four 0x3cc-byte slot records (+0x00 version
// "9-21-05" / "9-21-05CE", +0x1c the profile, +0x3c0 the save data name), +0x11c0 four write buffers, +0x11e0 the
// slot written, +0x11e6 the device byte, +0x11e7 four slot-used flags).
// 0x0030c4d0 (manager in EBX; slot, keep on the stack, ret 8): the slot's version and name, used; without keep
// 0x0030c1a0 first; the record into the slot's write buffer, then Save_BeginWrite.
// Returns Save_BeginWrite's result.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL SaveSlot_Init(uint8_t* saves, int32_t slot, uint8_t keep);
extern "C" void SaveSlot_Init_entry(void);
// 0x0032d200 (stdcall): all four slots with a default profile (Profile_SetDefaults of a local), then one write.
void XB_STDCALL SaveSlot_InitAll(uint8_t* saves);

}  // namespace game
