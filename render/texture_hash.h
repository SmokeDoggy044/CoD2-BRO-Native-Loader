#pragma once

// The content hash the back end keys title textures by (BackendConfig::replace_texture):
// over the texture's memory as the title holds it, all levels (Xbox layout).

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace render {

inline uint64_t hash_bytes(const void* data, size_t size, uint64_t seed = 0xCBF29CE484222325ull) {
    const auto* p = static_cast<const uint8_t*>(data);
    uint64_t h = seed ^ (size * 0x9E3779B97F4A7C15ull);
    for (; size >= 8; size -= 8, p += 8) {
        uint64_t v;
        std::memcpy(&v, p, 8);
        h = (h ^ v) * 0x100000001B3ull;
        h ^= h >> 29;
    }
    for (; size; --size, ++p) h = (h ^ *p) * 0x100000001B3ull;
    return h;
}

}  // namespace render
