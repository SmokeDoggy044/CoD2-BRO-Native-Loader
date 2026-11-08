#pragma once

// Xbox (NV2A) texture layouts and their conversion to PC formats: swizzled and linear
// colour formats become A8R8G8B8, DXT blocks are copied as they are.

#include <cstddef>
#include <cstdint>

namespace render {

struct TextureLayout {
    enum class Kind { Argb, Dxt1, Dxt3, Dxt5, Unsupported };
    Kind kind = Kind::Unsupported;
    uint32_t color_format = 0;  // the NV2A/Xbox D3DFORMAT number
    uint32_t width = 0, height = 0, levels = 1;
    bool swizzled = false;  // power-of-two sizes, Morton order
    bool linear = false;    // LIN_ formats: texel coordinates are not normalized
    bool cube = false;
    bool volume = false;
    uint32_t bytes_per_texel = 0;  // 0 for DXT
    uint32_t pitch = 0;            // linear formats: bytes per row
    size_t level_offset[16] = {};
    size_t total_size = 0;  // bytes of texture memory, all levels
};

// From a texture's NV2A format register and its D3D Size field (linear formats only).
// False for layouts not supported yet (cube maps, volumes, unknown formats).
bool describe_texture(uint32_t format_register, uint32_t size_field, TextureLayout& layout);

uint32_t level_width(const TextureLayout& layout, uint32_t level);
uint32_t level_height(const TextureLayout& layout, uint32_t level);

// Writes one level: A8R8G8B8 rows for Kind::Argb, block rows for DXT. `palette` is the
// D3DPalette's 256 colours (P8 only).
void decode_level(const TextureLayout& layout, uint32_t level, const uint8_t* texture_memory,
                  const uint32_t* palette, uint8_t* out, uint32_t out_pitch);

}  // namespace render
