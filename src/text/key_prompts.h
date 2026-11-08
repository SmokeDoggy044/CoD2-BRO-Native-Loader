#pragma once

// Keyboard and mouse button prompts (PC option, --button-prompts): the game shows its
// controller buttons with pictures - an icon font (i_button_icons_xb_<language>.dds
// and .fdf in the packs: triggers, A B X Y, white/black, D-pad, start, back) and four
// single icons for the menus' footers (Xbox_A/B/X/YButton.dds). While the player uses
// the keyboard and mouse, the renderer shows keycaps and mouse pictures in their place
// (render::BackendConfig::replace_texture), with the labels the loader sets for the
// title's current controls; set_key_labels(nullopt) brings the controller icons back.

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "backend.h"

namespace text {

// The icon font's glyphs, in its order (codes 33-49), and the footer icons' buttons.
enum class PromptIcon {
    RightTrigger, A, X, Y, B, LeftTrigger, White, Black,
    DpadUp, DpadRight, DpadLeft, DpadDown, Start, DpadLeftRight, DpadUpDown, Dpad, Back,
    Count
};

// What each icon shows: a key's name ("F", "Space", "Esc"), or a picture -
// "@lmb" "@rmb" "@mmb" (a mouse with that button), "@up" "@down" "@left" "@right"
// "@leftright" "@updown" "@arrows" (arrow keys). Empty: an empty keycap.
using KeyLabels = std::array<std::string, static_cast<size_t>(PromptIcon::Count)>;

// Finds the icon textures in the packs under `assets`; `fonts_dir` has the font the
// labels are written in. Returns how many textures were found (logged).
size_t prepare_key_prompts(const std::filesystem::path& assets, const std::filesystem::path& fonts_dir);

// Keycaps with these labels from now on, or the controller icons (nullopt). Cheap
// when nothing changed; any thread.
void set_key_labels(const std::optional<KeyLabels>& labels);

// For render::BackendConfig::replace_texture: the keycap picture for an icon texture
// while labels are set, else null.
std::shared_ptr<const render::TextureReplacement> key_prompt_replacement(uint64_t content, float render_scale);

// For render::BackendConfig::replacement_version: changes whenever the labels do, for
// the icon textures only (0 for every other texture).
uint32_t key_prompt_version(uint64_t content);

}  // namespace text
