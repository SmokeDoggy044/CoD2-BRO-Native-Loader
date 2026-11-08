#pragma once

// Our own engine's PC settings in the front end's own menus (the first engine addition, 2026-11-07): a fourth
// Options item, GRAPHICS, opens the cut Screen menu (ScreenMenu, screen 6: in the original nothing opens it, and
// its 4:3 / 16:9 choice is read but never stored, its row hidden), whose first row becomes Enhanced Graphics
// (on / off, live), with Reset to Defaults and Apply and Exit (settings.ini); Back undoes the change. The Options
// panel already has the row: its "Screen" text, which the original's setup leaves out (and closes the gap).
//
// Only with the loader's settings hooks installed (pc_settings_hooks below): without them - --compare runs, other
// hosts - every function here does what the original does (3 Options items, the Screen menu as it was).

#include <cstdint>

#include "compiler.h"

namespace game {

// The host's settings for the menus (set by the loader before the title runs; host code, called from title
// threads: the loader's side restores its floating-point environment). All null: the original menus.
struct PcSettingsHooks {
    bool (*graphics_enhanced)() = nullptr;           // Enhanced graphics is on
    void (*set_graphics_enhanced)(bool on) = nullptr;  // switch it now (the picture changes at once)
    void (*save_graphics)() = nullptr;               // store the current choice in settings.ini
};
PcSettingsHooks& pc_settings_hooks();
// Whether the PC menus are on (the hooks are installed).
bool pc_menus();

// Texts for our rows in the game's own style (global.cod's strings): menu titles and Options items uppercase
// ("SCREEN", "GAMEPLAY"), option rows in title case ("Reset to Defaults"). The text setter (+0x30 / +0x6c) shows a
// key it cannot find as given.
inline constexpr char kPcTextGraphics[] = "GRAPHICS";
inline constexpr char kPcTextEnhancedGraphics[] = "Enhanced Graphics";

// 0x00012af0 (ret 4): OptionsMenu's accept (vtable +0x38): the item index into +0x3c, then the owner's (+8)
// vtable +0xc (the active screen): Gameplay 4, Controller 5, Sound 7; with the PC menus Gameplay 4, Controller 5,
// Graphics 6 (the Screen menu), Sound 7.
void XB_THISCALL OptionsMenu_Accept(uint8_t* menu, uint32_t index);
// 0x00338ea0: ScreenMenu's selection restored (UIMenu_RestoreSelection, the background movie), +0x50 = 4:3 (the
// widescreen word 0x00948f64 is 0) and its text (FEMENU_COP_4X3 / _16X9) coloured like item 0. With the PC menus
// +0x50 = Enhanced graphics (+0x51 the same: what Back goes back to), its text ON / OFF.
void XB_THISCALL ScreenMenu_RestoreSelection(uint8_t* menu);
// 0x0030e050 (ret 4): ScreenMenu's accept: item 2 (SAVE AND EXIT) goes to screen 3 (Options); item 1 (RESET TO
// DEFAULTS) does nothing. With the PC menus item 1 sets Enhanced graphics on (the launcher's default) and item 2
// saves a changed choice first.
void XB_THISCALL ScreenMenu_Accept(uint8_t* menu, uint32_t index);
// 0x0030e030 (ret 4, the pad unused): ScreenMenu's Back: screen 3 through the front end (0x0095f8f8). With the PC
// menus the choice goes back to what it was when the menu opened.
void XB_THISCALL ScreenMenu_Back(uint8_t* menu, uint32_t pad);

}  // namespace game
