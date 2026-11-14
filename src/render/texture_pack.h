#pragma once

// Texture packs (phase 1b, docs/developers/own-engine-host.md): mods in <data dir>/mods/<name>/ with a mod.ini and
// textures/<16 hex digits>.dds - the runtime content hash of the title texture it replaces (texture_hash.h),
// DXT1 or DXT5 with its mip levels. Pictures load on a worker thread; until one is ready the title's own shows.
// Mods sit outside the Enhanced switch; none in --compare runs.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "backend.h"

namespace render::texture_pack {

struct Mod {
    std::string folder_name, name, version, author;
    size_t textures = 0;
};

// Scans `mods_dir` (platform::scan_mods: every folder with a mod.ini, ordered and switched by the mod-list setting
// `mod_list`); when two mods cover the same texture the earlier one wins. Starts the loading thread. Returns the mods
// that are on (logged).
std::vector<Mod> open(const std::filesystem::path& mods_dir, std::string_view mod_list = {});

// Stops the loading thread and forgets every mod.
void close();

// The pack picture for title texture contents, or null (not covered, or not loaded yet: it is queued, and
// version() moves when it is ready). The drawing thread; the loaded copy is dropped once handed out.
std::shared_ptr<const TextureReplacement> replacement(uint64_t content);

// Moves each time a picture for `content` gets ready (0: none yet, or not covered): BackendConfig::replacement_version.
uint32_t version(uint64_t content);

// Developer mode (--dev): packs switched off while running (replacement() gives none, version() moves so devices
// ask again and fall back to the title's pictures), and on again.
void set_enabled(bool enabled);
bool enabled();

struct Stats {
    size_t covered = 0;    // textures the mods replace
    size_t loaded = 0;     // read (waiting for the device or handed out)
    size_t pending = 0;    // queued for the loading thread
    size_t failed = 0;     // unreadable files
    size_t uncovered = 0;  // different title textures asked for that no mod covers
};
Stats stats();
// The mod folder covering `content`, or "" (developer mode's texture picker).
std::string covering_mod(uint64_t content);

// A DDS file (DXT1 / DXT5, with or without mip levels) as a replacement; null if it is not one.
std::shared_ptr<TextureReplacement> read_dds(const std::filesystem::path& file);

}  // namespace render::texture_pack
