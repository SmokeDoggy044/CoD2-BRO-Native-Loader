#pragma once

// Texture packs (phase 1b, docs/developers/own-engine-host.md): mods in <data dir>/mods/<name>/ with a mod.ini and
// textures/<16 hex digits>.dds - the runtime content hash of the title texture it replaces (texture_hash.h),
// DXT1 or DXT5 with its mip levels. Pictures load on a worker thread; until one is ready the title's own shows.
// Mods sit outside the Enhanced switch; none in --compare runs.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "backend.h"

namespace render::texture_pack {

struct Mod {
    std::string folder_name, name, version, author;
    size_t textures = 0;
};

// Scans `mods_dir`: every folder with a mod.ini and a textures/ folder, in name order; when two mods cover the same
// texture the first wins. Starts the loading thread. Returns the mods found (logged).
std::vector<Mod> open(const std::filesystem::path& mods_dir, const std::vector<std::string>& disabled = {});

// Stops the loading thread and forgets every mod.
void close();

// The pack picture for title texture contents, or null (not covered, or not loaded yet: it is queued, and
// version() moves when it is ready). The drawing thread; the loaded copy is dropped once handed out.
std::shared_ptr<const TextureReplacement> replacement(uint64_t content);

// Moves each time a picture for `content` gets ready (0: none yet, or not covered): BackendConfig::replacement_version.
uint32_t version(uint64_t content);

// A DDS file (DXT1 / DXT5, with or without mip levels) as a replacement; null if it is not one.
std::shared_ptr<TextureReplacement> read_dds(const std::filesystem::path& file);

}  // namespace render::texture_pack
