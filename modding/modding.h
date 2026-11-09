#pragma once

// The in-game "Modding" window (F4). Extract writes PNG textures, WAV sounds and
// the game's Xbox model/animation assets from the .cod packs / loose files into
// the user data folder; Replace loads edited PNGs live and writes edited WAVs
// and model/animation files back as game-file overrides.

#include <cstdint>
#include <filesystem>
#include <memory>

#include "backend.h"  // render::TextureReplacement

namespace modding {

// Call once after the game's asset folder is known. Creates the mods folders and
// loads any texture replacements already there.
void configure(const std::filesystem::path& assets);

// The mods folders under <user data>/mods/.
std::filesystem::path mods_root();
std::filesystem::path textures_dir();
std::filesystem::path audio_dir();
std::filesystem::path animations_dir();
std::filesystem::path overrides_dir();

// Dear ImGui window, drawn on the title's thread from present(). Returns false
// when the user closed it (the back end also toggles it with F4).
bool menu();

// For the back end: whether the window is open, and its open/close notification.
bool is_open();
void shown(bool open);

// Hooked into render::BackendConfig::replace_texture / replacement_version.
std::shared_ptr<const render::TextureReplacement> texture_replacement(uint64_t content, float render_scale);
uint32_t texture_version();

// Stops a running extract/replace job (called on shutdown).
void shutdown();

}  // namespace modding
