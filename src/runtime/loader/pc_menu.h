#pragma once

// The in-game PC menu (render::BackendConfig::pc_menu): settings that are best judged
// while playing. Opened and closed with the key below Escape (` ~); the title keeps
// running, without the player's input, meanwhile.

#include <filesystem>

#include "settings.h"

namespace loader {

// The live settings (platform::kLive) as settings.ini text, from what runs now; and applying what differs between
// two such forms (the game's thread or the presenting one).
platform::Settings live_values();
// The pad-glyphs setting at start-up (live_values reports it; input keeps only the override).
void set_live_pad_glyphs(const std::string& value);
void apply_live(const platform::Settings& before, const platform::Settings& after);
// The settings pages of the game's own menus (menu_pages.cpp): installs the page hooks (fe_pc.h).
void install_menu_pages(const std::filesystem::path& mods_dir, bool quit_row);
// Writes every text of our own the game's menus show (page titles, row labels, values, our menu rows; English, as
// lang/*.po key them) to `path` as a gettext template. False when it cannot be written.
bool write_lang_template(const std::filesystem::path& path);

// Makes the menu's widgets (Dear ImGui) for this frame; false closes it.
bool pc_menu();

// The settings the game's own front-end menus show (our engine's PC menus, src/engine/client/fe_pc.h):
// Enhanced graphics for now. Called before the title runs; not for --compare runs (the original menus).
void install_menu_settings();

// The menu opened or closed: blocks the title's input while it is open, and saves
// what changed to settings.ini when it closes.
void pc_menu_shown(bool open);
// A window event while the menu is open (an SDL_Event): true if the menu took it (the Controls tab's key capture).
bool pc_menu_event(const void* sdl_event);

}  // namespace loader
