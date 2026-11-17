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

// Settings pages (phase 4.2, fe_pc_pages.cpp): the host keeps the pages (generated from the settings schema), what
// each row shows and what input does; the game shows the current page in its own menus (the front end's Screen
// menu, the in-game Options screen) and passes the player's input on.
inline constexpr int32_t kPcPageMaxRows = 10;
enum PcRowKind : uint8_t {
    kPcRowValue,   // label left, value right between arrows (left / right change it)
    kPcRowLink,    // a sub-page or another screen (accept opens it), centred
    kPcRowAction,  // Reset to Defaults, Apply and Exit, Back (accept), centred
};
struct PcPageRow {
    char label[44];
    char value[36];
    uint8_t kind;
    uint8_t enabled;  // 0: dimmed, input ignored (its parent setting is off)
};
struct PcPageView {
    char title[36];
    int32_t rows;
    PcPageRow row[kPcPageMaxRows];
    int32_t selected;  // the row to select after a page change (-1: keep)
    int32_t language = 0;  // in: the game's language (g_languageIndex), for our texts (ui_text.h)
};
enum PcPageInput : int32_t { kPcInputLeft, kPcInputRight, kPcInputAccept, kPcInputBack };
enum PcPageResult : int32_t {
    kPcResultStay,        // same page (values may have changed)
    kPcResultPage,        // another page: lay the rows out again
    kPcResultClose,       // the session ended (saved or undone): back to the screen the pages came from
    kPcResultController,  // open the game's own Controller screen (front end screen 5); the session stays
};
enum PcPageRoot : int32_t { kPcRootGraphics, kPcRootSound, kPcRootControls, kPcRootInGame };

// The host's settings for the menus (set by the loader before the title runs; host code, called from title
// threads: the loader's side restores its floating-point environment). All null: the original menus.
struct PcSettingsHooks {
    bool (*graphics_enhanced)() = nullptr;           // Enhanced graphics is on
    void (*set_graphics_enhanced)(bool on) = nullptr;  // switch it now (the picture changes at once)
    void (*save_graphics)() = nullptr;               // store the current choice in settings.ini
    // Pages: a session starts at a root page (live settings snapshot) and ends with Apply (saved) or Back on the
    // root (live changes undone). page_view fills the current page; page_input handles `input` on `row`.
    void (*page_begin)(int32_t root) = nullptr;
    void (*page_view)(PcPageView* view) = nullptr;
    int32_t (*page_input)(int32_t row, int32_t input) = nullptr;
    bool (*page_active)() = nullptr;  // a session runs (the Controller screen returns to it)
    void (*quit_game)() = nullptr;    // end the program (the main menu's Quit Game)
    // Keyboard on the console screens: a movie plays (any key skips it, as a pad button), the profile name takes
    // typed text; called every frame while so. take_typed: the next typed character, 8 (backspace) or 13 (enter).
    void (*movie_playing)() = nullptr;
    void (*text_entry)() = nullptr;
    bool (*take_typed)(uint32_t* code) = nullptr;
    // Our own texts (kPcText* below) in the game's language (src/platform/ui_text.h): a text the game's text
    // functions take, valid for the program's life; null hook: English.
    const char* (*text)(int32_t language, const char* english) = nullptr;
};
// The pages are on (the page hooks are installed).
bool pc_pages();

// The page hosts: the front end's Screen menu (screen 6, the paper) and the in-game Options screen (in-game screen
// 1). While a page is shown the host menu runs with our vtable, items and value texts (fe_pc_pages.cpp); its own
// rows, labels and fields stay as they were and come back when the pages close.
enum PcPageHost : int32_t { kPcHostFrontEnd, kPcHostInGame };
// The root the host's next activation opens (OptionsMenu_Accept / PauseMenu_HandleSelect), taken by the host's
// RestoreSelection: -1 none.
void pc_pages_request(int32_t root);
int32_t pc_pages_take_request();
// Shows the pages on the host menu (a new session at `root`, or the running one when it comes back from the
// Controller screen with root -1).
void pc_pages_enter(uint8_t* menu, int32_t host, int32_t root);
// The Controller screen's way back: screen 6 while a session runs, else `screen`.
int32_t pc_page_return_screen(int32_t screen);
// The game's own master volume (the Sound menu's, 0x008dce88: 0..50) set and applied as that menu does.
void pc_set_game_volume(int32_t volume);
// Our text `english` (a kPcText*) in the game's language (PcSettingsHooks::text).
const char* pc_text(const char* english);
// Texts of our Options / pause rows.
inline constexpr char kPcTextControls[] = "CONTROLS";
inline constexpr char kPcTextSettings[] = "Settings";
inline constexpr char kPcTextPressStart[] = "Press ENTER or START";
inline constexpr char kPcTextQuitGame[] = "Quit Game";
// The main menu has a Quit Game row: a sixth row of our own (MainMenu_Construct makes room for it), below the last
// of the original's (Bonus Materials, or the special edition's own fifth row).
bool pc_quit_row();
// Makes it (FE_SetupMainMenu): a text like row `last`'s, one row spacing below it.
void pc_add_quit_row(uint8_t* menu, int32_t last);
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
