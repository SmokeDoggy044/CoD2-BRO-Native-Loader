#pragma once

// The game's .cod packs ("KAPF"): named files (textures as DDS, font tables, sounds,
// ...) in chunks.
//
// Header: "KAPF", version, ?, section count (u32 at 0x0c); from 0x28 one 24-byte
// record per section: u16 type, u16 file count, chunk table offset, directory offset,
// 12 bytes of name. Directory: per file 20 bytes - name offset, path offset (both
// zero-terminated strings in the pack), size, chunk index, offset in that chunk.
// Chunk table: 28-byte records starting with the chunk's offset in the pack.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace platform {
class File;
}

namespace text {

class KapfPack {
public:
    struct Entry {
        std::string name;  // e.g. "garamond.dds"
        uint64_t offset = 0;
        uint32_t size = 0;
    };

    // Null if the file is missing or not a pack (logged).
    static std::unique_ptr<KapfPack> open(const std::filesystem::path& path);
    ~KapfPack();

    const std::vector<Entry>& entries() const { return entries_; }
    const Entry* find(const std::string& name) const;
    std::optional<std::vector<uint8_t>> read(const Entry& entry) const;

private:
    KapfPack() = default;
    std::unique_ptr<platform::File> file_;
    std::vector<Entry> entries_;
};

}  // namespace text
