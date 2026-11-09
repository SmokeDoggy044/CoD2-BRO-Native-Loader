// See image.h.

#include "image.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#include "stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

namespace modding {
namespace {

uint32_t u32(const uint8_t* p, size_t at) {
    return uint32_t(p[at]) | uint32_t(p[at + 1]) << 8 | uint32_t(p[at + 2]) << 16 | uint32_t(p[at + 3]) << 24;
}
uint16_t u16(const uint8_t* p, size_t at) { return uint16_t(uint16_t(p[at]) | uint16_t(p[at + 1]) << 8); }

struct Pixel { uint8_t r = 0, g = 0, b = 0, a = 255; };

Pixel rgb565(uint16_t colour, uint8_t alpha) {
    const uint32_t r = (colour >> 11) & 31, g = (colour >> 5) & 63, b = colour & 31;
    return {static_cast<uint8_t>(r * 255 / 31), static_cast<uint8_t>(g * 255 / 63),
            static_cast<uint8_t>(b * 255 / 31), alpha};
}

Pixel mix(Pixel a, Pixel b, uint32_t wa, uint32_t wb) {
    const auto blend = [&](uint8_t x, uint8_t y) {
        return static_cast<uint8_t>((uint32_t(x) * wa + uint32_t(y) * wb) / (wa + wb));
    };
    return {blend(a.r, b.r), blend(a.g, b.g), blend(a.b, b.b), blend(a.a, b.a)};
}

// The 4x4 colour block shared by DXT1/3/5; `alpha` fills in the alpha channel.
void decode_colour_block(const uint8_t* block, Pixel out[16], bool dxt1_alpha) {
    const uint16_t c0 = u16(block, 0), c1 = u16(block, 2);
    const uint32_t indices = u32(block, 4);
    Pixel colours[4] = {rgb565(c0, 255), rgb565(c1, 255), {}, {}};
    if (c0 > c1 || !dxt1_alpha) {
        colours[2] = mix(colours[0], colours[1], 2, 1);
        colours[3] = mix(colours[0], colours[1], 1, 2);
    } else {
        colours[2] = mix(colours[0], colours[1], 1, 1);
        colours[3] = {0, 0, 0, 0};
    }
    for (uint32_t i = 0; i < 16; ++i) out[i] = colours[(indices >> (2 * i)) & 3];
}

void decode_dxt3_alpha(const uint8_t* block, Pixel out[16]) {
    for (uint32_t i = 0; i < 16; ++i) {
        const uint8_t nibble = (block[i / 2] >> ((i & 1) * 4)) & 15;
        out[i].a = static_cast<uint8_t>(nibble * 17);
    }
}

void decode_dxt5_alpha(const uint8_t* block, Pixel out[16]) {
    const uint8_t a0 = block[0], a1 = block[1];
    uint64_t bits = 0;
    for (uint32_t i = 0; i < 6; ++i) bits |= uint64_t(block[2 + i]) << (8 * i);
    uint8_t table[8] = {a0, a1, 0, 0, 0, 0, 0, 255};
    if (a0 > a1) {
        for (uint32_t i = 0; i < 6; ++i) table[2 + i] = static_cast<uint8_t>(((6 - i) * a0 + (1 + i) * a1) / 7);
    } else {
        for (uint32_t i = 0; i < 4; ++i) table[2 + i] = static_cast<uint8_t>(((4 - i) * a0 + (1 + i) * a1) / 5);
        table[6] = 0;
        table[7] = 255;
    }
    for (uint32_t i = 0; i < 16; ++i) out[i].a = table[(bits >> (3 * i)) & 7];
}

// Expands one channel from a bit mask to 0..255.
uint8_t channel(uint32_t value, uint32_t mask) {
    if (!mask) return 0;
    uint32_t shift = 0;
    while (!(mask & 1)) { ++shift; mask >>= 1; }
    const uint32_t max = mask;
    const uint32_t bits = (value >> shift) & max;
    return static_cast<uint8_t>(max ? bits * 255 / max : 0);
}

bool decode_compressed(const uint8_t* data, size_t size, size_t offset, uint32_t width, uint32_t height,
                       uint32_t four_cc, Image& image) {
    const uint32_t block_bytes = four_cc == '1TXD' ? 8 : 16;  // DXT1: 8, DXT3/5: 16
    const uint32_t blocks_x = (width + 3) / 4, blocks_y = (height + 3) / 4;
    const size_t needed = size_t(blocks_x) * blocks_y * block_bytes;
    if (offset + needed > size) return false;
    image.width = width;
    image.height = height;
    image.rgba.assign(size_t(width) * height * 4, 0);
    for (uint32_t by = 0; by < blocks_y; ++by) {
        for (uint32_t bx = 0; bx < blocks_x; ++bx) {
            const uint8_t* block = data + offset + (size_t(by) * blocks_x + bx) * block_bytes;
            Pixel pixels[16];
            if (four_cc == '1TXD') {
                decode_colour_block(block, pixels, true);
            } else {
                if (four_cc == '3TXD') {
                    decode_colour_block(block + 8, pixels, false);
                    decode_dxt3_alpha(block, pixels);
                } else {
                    decode_colour_block(block + 8, pixels, false);
                    decode_dxt5_alpha(block, pixels);
                }
            }
            for (uint32_t y = 0; y < 4; ++y) {
                for (uint32_t x = 0; x < 4; ++x) {
                    const uint32_t px = bx * 4 + x, py = by * 4 + y;
                    if (px >= width || py >= height) continue;
                    const Pixel& p = pixels[y * 4 + x];
                    uint8_t* out = image.rgba.data() + (size_t(py) * width + px) * 4;
                    out[0] = p.r;
                    out[1] = p.g;
                    out[2] = p.b;
                    out[3] = p.a;
                }
            }
        }
    }
    return true;
}

}  // namespace

bool decode_dds(const uint8_t* data, size_t size, Image& image, std::string& error) {
    if (size < 128 || std::memcmp(data, "DDS ", 4) != 0) {
        error = "not a DDS file";
        return false;
    }
    const uint32_t height = u32(data, 12), width = u32(data, 16), pitch = u32(data, 20);
    const uint32_t flags = u32(data, 80), four_cc = u32(data, 84), bit_count = u32(data, 88);
    uint32_t masks[4] = {u32(data, 92), u32(data, 96), u32(data, 100), u32(data, 104)};  // R,G,B,A
    size_t offset = 128;
    uint32_t effective_four_cc = four_cc;
    uint32_t forced_bpp = 0;
    bool dx10 = false;
    if ((flags & 0x4) && four_cc == '01XD') {  // "DX10"
        if (size < 148) { error = "truncated DX10 header"; return false; }
        const uint32_t dxgi = u32(data, 128);
        offset = 148;
        dx10 = true;
        switch (dxgi) {
            case 71: effective_four_cc = '1TXD'; break;  // BC1
            case 74: effective_four_cc = '3TXD'; break;  // BC2
            case 77: effective_four_cc = '5TXD'; break;  // BC3
            case 28:  // R8G8B8A8
                masks[0] = 0x000000FF; masks[1] = 0x0000FF00;
                masks[2] = 0x00FF0000; masks[3] = 0xFF000000;
                forced_bpp = 4;
                break;
            case 87:  // B8G8R8A8
                masks[0] = 0x00FF0000; masks[1] = 0x0000FF00;
                masks[2] = 0x000000FF; masks[3] = 0xFF000000;
                forced_bpp = 4;
                break;
            case 61: masks[0] = masks[1] = masks[2] = 0xFF; masks[3] = 0; forced_bpp = 1; break;  // R8
            default: error = "unsupported DX10 format " + std::to_string(dxgi); return false;
        }
    }
    if (!width || !height || width > 16384 || height > 16384) {
        error = "bad DDS dimensions";
        return false;
    }
    if ((flags & 0x4) && !dx10) {
        switch (effective_four_cc) {
            case '1TXD':  // "DXT1"
            case '3TXD':  // "DXT3"
            case '5TXD':  // "DXT5"
            case '2TXD':  // "DXT2" (premultiplied DXT3)
            case '4TXD':  // "DXT4" (premultiplied DXT5)
                return decode_compressed(data, size, offset, width, height,
                                         effective_four_cc == '2TXD' ? '3TXD'
                                         : effective_four_cc == '4TXD' ? '5TXD' : effective_four_cc,
                                         image);
            default: error = "unsupported DDS fourCC"; return false;
        }
    }
    if (effective_four_cc == '1TXD' || effective_four_cc == '3TXD' || effective_four_cc == '5TXD')
        return decode_compressed(data, size, offset, width, height, effective_four_cc, image);

    const bool paletted = (flags & 0x20) != 0;  // DDPF_PALETTEINDEXED8
    const uint32_t bytes_per_pixel = paletted ? 1 : forced_bpp ? forced_bpp : std::max<uint32_t>(1, (bit_count + 7) / 8);
    const uint32_t row = pitch ? pitch : width * bytes_per_pixel;
    const uint8_t* palette = nullptr;
    if (paletted) {
        if (offset + 1024 > size) { error = "missing palette"; return false; }
        palette = data + offset;
        offset += 1024;
    }
    if (offset + size_t(row) * height > size) {
        error = "truncated DDS pixels";
        return false;
    }
    image.width = width;
    image.height = height;
    image.rgba.assign(size_t(width) * height * 4, 0);
    const bool luminance = (flags & 0x20000) != 0;  // DDPF_LUMINANCE
    const bool alpha_only = (flags & 0x2) != 0;     // DDPF_ALPHA
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t* src = data + offset + size_t(y) * row;
        for (uint32_t x = 0; x < width; ++x) {
            uint32_t value = 0;
            for (uint32_t b = 0; b < bytes_per_pixel; ++b) value |= uint32_t(src[x * bytes_per_pixel + b]) << (8 * b);
            uint8_t* out = image.rgba.data() + (size_t(y) * width + x) * 4;
            if (paletted) {
                const uint32_t colour = u32(palette, std::min<uint32_t>(value & 255, 255) * 4);
                out[0] = colour & 255;
                out[1] = (colour >> 8) & 255;
                out[2] = (colour >> 16) & 255;
                out[3] = (colour >> 24) & 255;
                if (out[3] == 0 && (flags & 0x1)) out[3] = 255;
            } else if (alpha_only) {
                out[0] = out[1] = out[2] = 255;
                out[3] = channel(value, masks[3] ? masks[3] : 0xFF);
            } else if (luminance) {
                const uint8_t l = channel(value, masks[0] ? masks[0] : 0xFF);
                out[0] = out[1] = out[2] = l;
                out[3] = masks[3] ? channel(value, masks[3]) : 255;
            } else {
                out[0] = channel(value, masks[0]);
                out[1] = channel(value, masks[1]);
                out[2] = channel(value, masks[2]);
                out[3] = masks[3] ? channel(value, masks[3]) : 255;
            }
        }
    }
    return true;
}

bool write_png(const std::string& path, const Image& image, std::string& error) {
    if (!image.width || !image.height || image.rgba.size() < size_t(image.width) * image.height * 4) {
        error = "empty image";
        return false;
    }
    if (!stbi_write_png(path.c_str(), static_cast<int>(image.width), static_cast<int>(image.height), 4,
                        image.rgba.data(), static_cast<int>(image.width * 4))) {
        error = "cannot write PNG";
        return false;
    }
    return true;
}

bool read_png(const std::string& path, Image& image, std::string& error) {
    int width = 0, height = 0, channels = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &width, &height, &channels, 4);
    if (!pixels) {
        error = std::string("cannot read PNG: ") + (stbi_failure_reason() ? stbi_failure_reason() : "unknown error");
        return false;
    }
    image.width = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);
    image.rgba.assign(pixels, pixels + size_t(width) * height * 4);
    stbi_image_free(pixels);
    return true;
}

std::shared_ptr<const render::TextureReplacement> to_replacement(const Image& image) {
    if (!image.width || !image.height) return nullptr;
    auto replacement = std::make_shared<render::TextureReplacement>();
    replacement->width = image.width;
    replacement->height = image.height;
    replacement->pixels.resize(size_t(image.width) * image.height);
    for (size_t i = 0; i < replacement->pixels.size(); ++i) {
        const uint8_t* p = image.rgba.data() + i * 4;
        replacement->pixels[i] = uint32_t(p[3]) << 24 | uint32_t(p[0]) << 16 | uint32_t(p[1]) << 8 | uint32_t(p[2]);
    }
    return replacement;
}

}  // namespace modding
