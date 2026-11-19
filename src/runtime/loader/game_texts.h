#pragma once

// The game's own texts, changed by us (game::loc_set_overrides, src/engine/qcommon/loc_overrides.h):
// - PC wording: while the keyboard and mouse are in use, the few texts that name the Xbox controller in words
//   ("Move the left thumbstick") say the bound keys instead, in the game's language (lang/*.po translate them);
// - corrections: <user data>/text_overrides.po replaces any text, by its key hash (msgctxt "0x1234abcd"); the
//   template a session writes (--game-text-template) lists them with the disc's text.
// A correction wins over the PC wording.

#include <filesystem>
#include <vector>

namespace loader {

// Reads the corrections file and sets the overrides; call before the title runs. `template_path` (or empty):
// every text the session's string tables have is appended there as it loads, as a gettext template.
void start_game_texts(const std::filesystem::path& template_path);

// The prompts' device (update_button_prompts, every input poll): the PC wording follows it and the bindings.
void update_game_texts(bool keyboard);

// The PC wording's English texts that need translating (the language template lists them).
std::vector<const char*> game_text_texts();

}  // namespace loader
