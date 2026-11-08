#pragma once

// The in-game PC menu (render::BackendConfig::pc_menu): settings that are best judged
// while playing. Opened and closed with the key below Escape (` ~); the title keeps
// running, without the player's input, meanwhile.

namespace loader {

// Makes the menu's widgets (Dear ImGui) for this frame; false closes it.
bool pc_menu();

// The menu opened or closed: blocks the title's input while it is open, and saves
// what changed to settings.ini when it closes.
void pc_menu_shown(bool open);

}  // namespace loader
