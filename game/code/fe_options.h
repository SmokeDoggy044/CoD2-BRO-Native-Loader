#pragma once

// The options menus: the front end's options (OptionsMenu, vtable 0x00829348), gameplay (GameplayMenu, 0x00828930),
// controller (ControllerConfigMenu, 0x00829c48) and screen (ScreenMenu, 0x00828b88) menus, and the in-game options
// screen (AdvancedOptionsMenu, 0x0082a388, the in-game screens' index 1). All are UIMenus (ui_menu.h) with +0x34 a
// panel and +0x38 a label or object; the option labels show FEMENU_COP_ON / FEMENU_COP_OFF (label vtable +0x6c),
// coloured like their items (item +0x84 into a word, label +0x90).
//
// GameplayMenu: +0x3c..+0x4c the five value labels, +0x7c..+0x80 the five flags (from 0x008dce68, 0x008dce69,
// 0x008dce6a, 0x008dce6c != 0, 0x008dce70), +0x84 0.0, +0x88 -1.
// ControllerConfigMenu: +0x3c invert aim and +0x40 vibration labels, +0x44 a third label, +0x48.. the arrow pairs
// (left, right) of rows 0, 1, 2 (row 4), +0xb8 / +0xbc two sliders (0..50, from 0x008dce74 / 0x008dce78), +0xc0 the
// controller preset (0..3, 0x008dce7c), +0xc4 0, +0xc8 the arrow flash time (0.1), +0xcc invert aim (0x008dce71),
// +0xcd vibration (0x008dce72), +0xd0 the flashing arrow row, +0xd4 0x008dce7c.
// ScreenMenu: +0x4c 0, +0x50 1, +0x54 0.0, +0x58 -1.
// AdvancedOptionsMenu (12 rows): +0x38..+0x50 the seven on / off labels, +0x54.. the arrow pairs (left, right) of the
// seven option rows, +0x8c / +0x90 / +0x94 the three slider bars, +0x98 / +0x9c / +0xa0 texts (the second drawn from
// row 10 on), +0xa4 / +0xa8 / +0xac the slider values (0..50: 0x008dce74, 0x008dce78, 0x008dce88), +0xb0 the
// flashing arrow row, +0xb4 0, +0xb8 the arrow flash time (0.1), +0xbc..+0xc2 the seven flags (+0xbc the pause menu's
// +0x40, +0xbd 0x008dce6a, +0xbe 0x008dce71, +0xbf 0x008dce72, +0xc0 0x008dce68, +0xc1 0x008dce6c != 0, +0xc2
// 0x008dce70). Rows: 0 +0xbe, 1 +0xbf, 2 slider +0xa4, 3 slider +0xa8, 4 +0xbc, 5 +0xbd, 6 slider +0xac, 7 +0xc0
// (its label only for language 0), 8 +0xc1, 9 +0xc2.

#include <cstdint>

#include "compiler.h"

// Options Draws that set the help bar text (+0x6c), which keeps a stack address: the entry moves the C frame below
// a gap and the call runs at the original's ESP (after push esi / push edi), leaving its return address there. The C
// version takes (menu, entry ESP, ESI, EDI).
#define DRAW_GAP_ENTRY(name)                                                                                      \
    PLATFORM_ASM_ENTRY(name##_entry, "movl %esp, %edx\n" GAME_GAP_ESP("0x10000") "pushl %edx\n"                   \
                                     "pushl %edi\n pushl %esi\n pushl %edx\n pushl %ecx\n" PLATFORM_ASM_CALL(name)  \
                                     "addl $16, %esp\n popl %esp\n ret")

namespace game {

// The help bar +0x38: FEMENU_HELPBAR_BACK above row `back_rows`, else FEMENU_OP_HELPBAR, at entry - 8 (EDI = the
// menu, ESI = the panel); `rets`: the return addresses of the two calls.
void set_help_bar_at(uint8_t* menu, uint32_t entry_esp, uint32_t esi, uint32_t edi, const uint32_t (&rets)[2],
                     int16_t back_rows);

// 0x00012a80 / 0x00012640 / 0x00012730 / 0x00012820 (menu in EAX): the panel +0x34 deleted (Panel_Delete), the
// object +0x38 deleted (vtable +0 (1)), then the UIMenu part (UIMenu_FreeItems). 0x00012c00: the same without the
// object. The deleting destructors (0x00012b60 / 0x00012710 / 0x00012800 / 0x00012930 / 0x00012c80) free the menu
// with flags bit 0.
void XB_REGS1 XB_CDECL OptionsMenu_Dtor(uint8_t* menu);
uint8_t* XB_THISCALL OptionsMenu_DeletingDtor(uint8_t* menu, uint32_t flags);
void XB_REGS1 XB_CDECL GameplayMenu_Dtor(uint8_t* menu);
uint8_t* XB_THISCALL GameplayMenu_DeletingDtor(uint8_t* menu, uint32_t flags);
void XB_REGS1 XB_CDECL ControllerConfigMenu_Dtor(uint8_t* menu);
uint8_t* XB_THISCALL ControllerConfigMenu_DeletingDtor(uint8_t* menu, uint32_t flags);
void XB_REGS1 XB_CDECL ScreenMenu_Dtor(uint8_t* menu);
uint8_t* XB_THISCALL ScreenMenu_DeletingDtor(uint8_t* menu, uint32_t flags);
void XB_REGS1 XB_CDECL AdvancedOptionsMenu_Dtor(uint8_t* menu);
uint8_t* XB_THISCALL AdvancedOptionsMenu_DeletingDtor(uint8_t* menu, uint32_t flags);

// 0x003310d0 / 0x00331150 / 0x00330fd0 / 0x003315e0 (owner in EAX, menu in ECX): a 7 / 7 / 3 / 12-row UIMenu at
// (320, 240), layer 8, and the fields above (ScreenMenu flag 6, the others repeat flags 7 and 8). Return the menu.
uint8_t* XB_REGS3 XB_CDECL ControllerConfigMenu_Construct(uint8_t* owner, uint32_t unused_edx, uint8_t* menu);
uint8_t* XB_REGS3 XB_CDECL GameplayMenu_Construct(uint8_t* owner, uint32_t unused_edx, uint8_t* menu);
uint8_t* XB_REGS3 XB_CDECL ScreenMenu_Construct(uint8_t* owner, uint32_t unused_edx, uint8_t* menu);
uint8_t* XB_REGS3 XB_CDECL AdvancedOptionsMenu_Construct(uint8_t* owner, uint32_t unused_edx, uint8_t* menu);

// 0x000126b0 / 0x000126d0, 0x000127c0 / 0x000127e0 (ret 4, the pad unused): the menu's previous / next item (vtable
// +0xb0 / +0xb4), then the label colours (GameplayMenu: 0x0030e390, menu in EBX).
void XB_THISCALL GameplayMenu_OnUp(uint8_t* menu, uint32_t pad);
void XB_THISCALL GameplayMenu_OnDown(uint8_t* menu, uint32_t pad);
void XB_THISCALL ControllerConfigMenu_OnUp(uint8_t* menu, uint32_t pad);
void XB_THISCALL ControllerConfigMenu_OnDown(uint8_t* menu, uint32_t pad);
// 0x00313c00 / 0x00313bc0 (ret 4): the same with wrapping from row 0 to 11 / 11 to 0 (+0x88 (row, 1), the move sound
// +0x10), then the label colours (0x0030e4c0, menu in ESI).
void XB_THISCALL AdvancedOptionsMenu_OnUp(uint8_t* menu, uint32_t pad);
void XB_THISCALL AdvancedOptionsMenu_OnDown(uint8_t* menu, uint32_t pad);

// 0x0030e080 (menu in ESI): labels +0x3c / +0x40 / +0x44 coloured like items 0 / 1 / 4.
extern "C" PLATFORM_HIDDEN void XB_CDECL ControllerConfigMenu_UpdateLabelColors(uint8_t* menu);
extern "C" void ControllerConfigMenu_UpdateLabelColors_entry(void);
// 0x0030e390 (menu in EBX): the five labels +0x3c.. coloured like items 0-4 (item vtable +0x84 colour, label +0x90).
extern "C" PLATFORM_HIDDEN void XB_CDECL GameplayMenu_UpdateLabelColors(uint8_t* menu);
extern "C" void GameplayMenu_UpdateLabelColors_entry(void);
// 0x0030e4c0 (menu in ESI): the seven on / off labels +0x38.. coloured like items 0, 1, 4, 5, 7, 8, 9.
extern "C" PLATFORM_HIDDEN void XB_CDECL AdvancedOptionsMenu_UpdateLabelColors(uint8_t* menu);
extern "C" void AdvancedOptionsMenu_UpdateLabelColors_entry(void);
// 0x0030e320 / 0x0030e2f0 (menu in EAX): on / off of +0xcc into the label +0x3c / +0xcd into +0x40, then the label
// colours.
void XB_REGS1 XB_CDECL ControllerConfigMenu_UpdateInvertAimText(uint8_t* menu);
void XB_REGS1 XB_CDECL ControllerConfigMenu_UpdateVibrationText(uint8_t* menu);
// 0x00335fe0 (menu in ESI, right in BL): the selected row's value: rows 0 / 1 toggle invert aim / vibration, rows 2
// / 3 move the sliders +0xb8 / +0xbc by one (0..50; the bars 0x0031e9d0 / 0x0031e810, EAX = menu; the sound +0x14
// unless clamped), row 4 the preset +0xc0 (0..3, FE_ControlsUpdateText); the move sound +0x10 after rows 0, 1, 4.
// Rows 0, 1, 4 then flash their left / right arrow (+0x48 + (row' * 2 + right) * 4: colour white through +0x70;
// +0xd0 = row', +0xc8 = 0.1).
extern "C" PLATFORM_HIDDEN void XB_CDECL ControllerConfigMenu_ChangeValue(uint8_t* menu, uint8_t right);
extern "C" void ControllerConfigMenu_ChangeValue_entry(void);

// 0x0031f050 (menu in EAX, right in BL): the selected row's value (rows 0-9 as listed above: flags toggle and show
// on / off, then the move sound +0x10; sliders move by one in 0..50 with the sound +0x14 unless clamped, then the
// bar 0x0031ee10); rows other than sliders flash their arrow (+0x54 + (row' * 2 + right) * 4, white through +0x70;
// +0xb0 = row', +0xb8 = 0.1), then the label colours (0x0030e4c0). Rows past 9 do nothing.
extern "C" PLATFORM_HIDDEN void XB_CDECL AdvancedOptionsMenu_ToggleSelected(uint8_t* menu, uint8_t right);
extern "C" void AdvancedOptionsMenu_ToggleSelected_entry(void);
// 0x003291a0 (menu in ESI): the flags and slider values from the settings (above), every on / off label, the three
// bars (0x0031ee10: bar in EAX; menu, value on the stack).
extern "C" PLATFORM_HIDDEN void XB_CDECL AdvancedOptionsMenu_LoadSettings(uint8_t* menu);
extern "C" void AdvancedOptionsMenu_LoadSettings_entry(void);
// 0x00329330 (menu in ESI): the defaults (flags +0xbc, +0xbd, +0xbf on, the rest off; sliders 25, 25, 50), the bars,
// then the labels.
extern "C" PLATFORM_HIDDEN void XB_CDECL AdvancedOptionsMenu_ResetDefaults(uint8_t* menu);
extern "C" void AdvancedOptionsMenu_ResetDefaults_entry(void);
// 0x00329c50: UIMenu_RestoreSelection, the active pad's button 14 press consumed, the settings loaded, the label
// colours.
void XB_THISCALL AdvancedOptionsMenu_RestoreSelection(uint8_t* menu);
// 0x00331590 (ret 4): FE_ApplyControlSettings (menu in ESI), unpaused (0x0095f919 = 0, S_ResumeAllChannels,
// cl_paused 0), 0x008dce69 = the pause menu's +0x40.
void XB_THISCALL AdvancedOptionsMenu_ResumeGame(uint8_t* menu, uint32_t pad);

// 0x0031e790 / 0x0031eb90 / 0x0031ec10: the panel +0x34, the help bar +0x38 (FEMENU_HELPBAR_BACK above row 1 / 5 /
// 5, else FEMENU_OP_HELPBAR; drawn through +0x3c, GameplayMenu: when set), UIMenu_Draw, then the background movie's
// frame (Movie_DrawFrame's steps).
void XB_THISCALL ScreenMenu_Draw(uint8_t* menu);
extern "C" PLATFORM_HIDDEN void XB_CDECL ControllerConfigMenu_Draw(uint8_t* menu, uint32_t entry_esp, uint32_t esi,
                                                                    uint32_t edi);
extern "C" void ControllerConfigMenu_Draw_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL GameplayMenu_Draw(uint8_t* menu, uint32_t entry_esp, uint32_t esi, uint32_t edi);
extern "C" void GameplayMenu_Draw_entry(void);
// 0x0031f3e0: the texts +0x98, +0x9c (from row 10 on) and +0xa0 (+0x3c), the panel, then UIMenu_Draw.
void XB_THISCALL AdvancedOptionsMenu_Draw(uint8_t* menu);

// 0x00339110 / 0x00339050 / 0x00338f50: UIMenu_RestoreSelection; the background movie "Propgnda" opened full screen
// (0x0095f91a once open); OptionsMenu: the selection +0x88 (+0x3c, 1) first; GameplayMenu: the five flags and their
// labels (the first only for language 0), the label colours; ControllerConfigMenu: the settings (above; preset 4
// reset to 0 in 0x008dce7c too), both on / off labels, the bars, FE_ControlsUpdateText.
void XB_THISCALL OptionsMenu_RestoreSelection(uint8_t* menu);
void XB_THISCALL GameplayMenu_RestoreSelection(uint8_t* menu);
void XB_THISCALL ControllerConfigMenu_RestoreSelection(uint8_t* menu);

}  // namespace game
