#pragma once

// The content hash title textures are known by (BackendConfig::replace_texture,
// GfxDevice::replacement_texture): over the texture's memory as the title holds it, all levels
// (Xbox layout). And the cheap check for changes the texture caches make each frame.

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

// Hashing every texture's whole memory every frame took up to 13% of the main thread
// on a slow PC (issue #8), though nearly all of them stay as loaded. Each frame hashes
// kSampleBlocks spread blocks of a large texture (memory loaded with another picture
// differs nearly everywhere) and the whole of it only every kFullCheckFrames checks of it
// (not frames: our draws and the Xbox path's alternate per frame with XB_DRAWS_ALTERNATE) or while it changed in the last kHotFrames.
constexpr uint32_t kSampleBlocks = 16, kSampleBlockSize = 64, kFullCheckFrames = 8, kHotFrames = 60;

inline uint64_t sample_texture(const uint8_t* memory, uint32_t size) {
    if (size <= kSampleBlocks * kSampleBlockSize * 2) return hash_bytes(memory, size);
    uint64_t h = hash_bytes(memory, kSampleBlockSize);
    for (uint32_t i = 1; i < kSampleBlocks; ++i)
        h = hash_bytes(memory + uint64_t{size - kSampleBlockSize} * i / (kSampleBlocks - 1), kSampleBlockSize, h);
    return h;
}

}  // namespace render
