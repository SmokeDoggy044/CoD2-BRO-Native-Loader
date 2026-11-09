#pragma once

// Reading the game's .cod packs ("KAPF") and loose asset files. The layout is the
// same one text/kapf.cpp uses for the sharper fonts and tools/extract_textures.py
// for upscaling; this copy also keeps each entry's original asset path so extracted
// PNG/WAV files can be named after it.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace modding {

struct PackEntry {
    std::string name;        // e.g. "garamond.dds"
    std::string asset_path;  // original E:\asset_temp\... path from the pack
    uint64_t offset = 0;     // absolute offset in the pack file
    uint32_t size = 0;
};

struct Pack {
    std::filesystem::path file;
    std::string relative;  // path relative to the assets folder
    std::vector<PackEntry> entries;
};

// Reads only the pack's directory (the first 32 KB), not the whole file.
bool read_pack(const std::filesystem::path& path, const std::filesystem::path& assets, Pack& pack, std::string& error);

// Reads an entry's bytes, or the whole file for a loose asset.
bool read_bytes(const std::filesystem::path& file, uint64_t offset, uint32_t size, std::vector<uint8_t>& out,
                std::string& error);
bool read_bytes(const std::filesystem::path& file, std::vector<uint8_t>& out, std::string& error);
bool write_bytes(const std::filesystem::path& file, const std::vector<uint8_t>& bytes, std::string& error);

// Case-insensitive suffix test and extension.
bool ends_with_ci(std::string_view text, std::string_view suffix);
std::string lowercase(std::string text);

// "E:\\asset_temp\\assets\\textures\\foo.dds" -> "assets/textures/foo".
// Unsafe components (".", "..", drive letters) are removed; a colon becomes '_'.
std::string asset_stem(const std::string& asset_path, const std::string& fallback);

// Every asset under `root` with one of the given (lowercase) extensions, plus
// every .cod pack entry. `progress` is called with each pack scanned.
std::vector<Pack> find_packs(const std::filesystem::path& root);
std::vector<std::filesystem::path> find_files(const std::filesystem::path& root,
                                              const std::vector<std::string>& extensions);

}  // namespace modding
