#pragma once

// The in-game PC menu (render::BackendConfig::pc_menu): settings that are best judged
// while playing. Opened and closed with the key below Escape (` ~); the title keeps
// running, without the player's input, meanwhile.

namespace loader {

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
