#pragma once

// Image helpers for the Modding window: PNG read/write (stb, vendored) and a
// decoder for the DDS files inside the game's .cod packs. Only the top mip level
// is needed: the replacement pipeline builds mipmaps from the PNG.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "backend.h"  // render::TextureReplacement

namespace modding {

struct Image {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> rgba;  // width * height * 4, top row first
};

// Decodes the first level of a DDS file (DXT1/3/5, uncompressed RGB/A/luminance,
// paletted 8-bit and common DX10 formats). False with a short reason on failure.
bool decode_dds(const uint8_t* data, size_t size, Image& image, std::string& error);

// PNG files (RGBA, 8 bits per channel). `error` is set on failure.
bool write_png(const std::string& path, const Image& image, std::string& error);
bool read_png(const std::string& path, Image& image, std::string& error);

// A replacement as the renderer accepts it: A8R8G8B8 dwords.
std::shared_ptr<const render::TextureReplacement> to_replacement(const Image& image);

}  // namespace modding
