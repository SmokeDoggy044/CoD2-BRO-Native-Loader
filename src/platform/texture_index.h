#pragma once

// Texture pack release files (docs/developers/texture-packs.md): the index of a release (cod_bro_textures.index,
// beside its data parts) and of an installed pack (<mods>/<pack>/pack.idx beside pack.dat) share one text grammar:
//
//   cod_bro_textures 1
//   release <version>
//   part <file name> <size> <sha256>          numbered from 1 in this order
//   pack <id>                                  then that pack's lines:
//   ini <mod.ini line>
//   file <id: 16 hex> <t|n|s> <part> <offset> <stored size> <size> <sha256 of the stored bytes>
//
// Each stored file = the DDS compressed with zstd, then ChaCha20 with texture_key() of the original texture the
// file id names (a surface map: its colour texture's) and texture_nonce(id, kind), block counter 0.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace platform {

struct TextureIndex {
    struct Part {
        std::string name;
        uint64_t size = 0;
        std::string sha256;
    };
    struct File {
        uint64_t id = 0;
        char kind = 't';  // t texture, n normal map, s surface map
        uint32_t part = 1;
        uint64_t offset = 0, stored = 0, size = 0;
        std::string sha256;
    };
    struct Pack {
        std::string id;
        std::vector<std::string> ini;  // mod.ini lines
        std::vector<File> files;
        uint64_t stored_bytes() const;
        std::string ini_value(std::string_view key) const;  // "" when absent
    };
    std::string release;
    std::vector<Part> parts;
    std::vector<Pack> packs;

    const Pack* find(std::string_view pack_id) const;
};

std::optional<TextureIndex> parse_texture_index(std::string_view text, std::string* error = nullptr);
std::string format_texture_index(const TextureIndex& index);

// SHA-256("cod_bro texture key v1" || the original texture's bytes as the title holds them).
std::array<uint8_t, 32> texture_key(const void* original, size_t size);
// The file id (little-endian), then the kind (0 texture, 1 normal, 2 surface; 32-bit little-endian).
std::array<uint8_t, 12> texture_nonce(uint64_t id, char kind);

}  // namespace platform
