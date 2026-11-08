// Xbox texture layouts. See texture_decode.h.

#include "texture_decode.h"

#include <algorithm>
#include <cstring>

#include "xbox_d3d.h"

namespace render {

namespace {

// How a colour format's texels become A8R8G8B8.
enum class Texel {
    Argb8888, Xrgb8888, Abgr8888, Bgra8888, Rgba8888, Rgb565, Argb1555, Xrgb1555, Argb4444,
    L8, A8, AL8, A8L8, P8, Dxt1, Dxt3, Dxt5,
    Yuy2, Uyvy,  // two texels in 4 bytes (Y0 U Y1 V / U Y0 V Y1), shown as RGB (YuvEnable)
};

struct FormatInfo {
    uint32_t format;
    Texel texel;
    uint32_t bytes;  // per texel; 0 for DXT
    bool linear;
};

// Xbox D3DFORMAT numbers (= NV2A colour formats), XDK 5849.
constexpr FormatInfo kFormats[] = {
    {0x00, Texel::L8, 1, false},       {0x01, Texel::AL8, 1, false},      {0x02, Texel::Argb1555, 2, false},
    {0x03, Texel::Xrgb1555, 2, false}, {0x04, Texel::Argb4444, 2, false}, {0x05, Texel::Rgb565, 2, false},
    {0x06, Texel::Argb8888, 4, false}, {0x07, Texel::Xrgb8888, 4, false}, {0x0B, Texel::P8, 1, false},
    {0x0C, Texel::Dxt1, 0, false},     {0x0E, Texel::Dxt3, 0, false},     {0x0F, Texel::Dxt5, 0, false},
    {0x10, Texel::Argb1555, 2, true},  {0x11, Texel::Rgb565, 2, true},    {0x12, Texel::Argb8888, 4, true},
    {0x13, Texel::L8, 1, true},        {0x19, Texel::A8, 1, false},       {0x1A, Texel::A8L8, 2, false},
    {0x1B, Texel::AL8, 1, true},       {0x1C, Texel::Xrgb1555, 2, true},  {0x1D, Texel::Argb4444, 2, true},
    {0x1E, Texel::Xrgb8888, 4, true},  {0x1F, Texel::A8, 1, true},        {0x20, Texel::A8L8, 2, true},
    {0x3A, Texel::Abgr8888, 4, false}, {0x3B, Texel::Bgra8888, 4, false}, {0x3C, Texel::Rgba8888, 4, false},
    {0x3F, Texel::Abgr8888, 4, true},  {0x40, Texel::Bgra8888, 4, true},  {0x41, Texel::Rgba8888, 4, true},
    {0x24, Texel::Yuy2, 2, true},      {0x25, Texel::Uyvy, 2, true},
};

const FormatInfo* find_format(uint32_t format) {
    for (const auto& f : kFormats)
        if (f.format == format) return &f;
    return nullptr;
}

uint32_t expand(uint32_t value, uint32_t bits) {
    return bits == 8 ? value : (value << (8 - bits)) | (value >> (2 * bits - 8));
}

uint32_t argb(uint32_t a, uint32_t r, uint32_t g, uint32_t b) { return a << 24 | r << 16 | g << 8 | b; }

// ITU-R BT.601, video range (Y 16-235), as the NV2A's YUV texture conversion.
uint32_t yuv_to_argb(int y, int u, int v) {
    const int c = 298 * (y - 16), d = u - 128, e = v - 128;
    auto clamp8 = [](int x) { return static_cast<uint32_t>(std::clamp((x + 128) >> 8, 0, 255)); };
    return argb(0xFF, clamp8(c + 409 * e), clamp8(c - 100 * d - 208 * e), clamp8(c + 516 * d));
}

// Texel x of a YUY2 / UYVY row: its pair shares U and V.
uint32_t yuv_texel(Texel texel, const uint8_t* row, uint32_t x) {
    const uint8_t* pair = row + static_cast<size_t>(x & ~1u) * 2;
    if (texel == Texel::Yuy2) return yuv_to_argb(pair[(x & 1) * 2], pair[1], pair[3]);
    return yuv_to_argb(pair[1 + (x & 1) * 2], pair[0], pair[2]);
}

uint32_t to_argb(Texel texel, const uint8_t* p, const uint32_t* palette) {
    uint32_t v = 0;
    switch (texel) {
        case Texel::Argb8888: std::memcpy(&v, p, 4); return v;
        case Texel::Xrgb8888: std::memcpy(&v, p, 4); return v | 0xFF000000;
        case Texel::Abgr8888: std::memcpy(&v, p, 4); return argb(v >> 24, v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF);
        case Texel::Bgra8888: std::memcpy(&v, p, 4); return argb(v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, v >> 24);
        case Texel::Rgba8888: std::memcpy(&v, p, 4); return argb(v & 0xFF, v >> 24, (v >> 16) & 0xFF, (v >> 8) & 0xFF);
        case Texel::Rgb565:
            v = p[0] | p[1] << 8;
            return argb(0xFF, expand(v >> 11, 5), expand((v >> 5) & 0x3F, 6), expand(v & 0x1F, 5));
        case Texel::Argb1555:
        case Texel::Xrgb1555:
            v = p[0] | p[1] << 8;
            return argb(texel == Texel::Xrgb1555 || (v & 0x8000) ? 0xFF : 0, expand((v >> 10) & 0x1F, 5),
                        expand((v >> 5) & 0x1F, 5), expand(v & 0x1F, 5));
        case Texel::Argb4444:
            v = p[0] | p[1] << 8;
            return argb(expand(v >> 12, 4), expand((v >> 8) & 0xF, 4), expand((v >> 4) & 0xF, 4), expand(v & 0xF, 4));
        case Texel::L8: return argb(0xFF, p[0], p[0], p[0]);
        case Texel::A8: return argb(p[0], 0xFF, 0xFF, 0xFF);
        case Texel::AL8: return argb(p[0], p[0], p[0], p[0]);
        case Texel::A8L8: return argb(p[1], p[0], p[0], p[0]);
        case Texel::P8: return palette ? palette[p[0]] : argb(0xFF, p[0], p[0], p[0]);
        default: return 0xFFFF00FF;
    }
}

// Morton order: bits of x and y interleave from the lowest, x first, while both have
// bits left; the larger dimension's remaining bits follow.
void swizzle_masks(uint32_t width, uint32_t height, uint32_t& mask_x, uint32_t& mask_y) {
    mask_x = mask_y = 0;
    uint32_t bit = 1, mask_bit = 1;
    bool done;
    do {
        done = true;
        if (bit < width) mask_x |= mask_bit, mask_bit <<= 1, done = false;
        if (bit < height) mask_y |= mask_bit, mask_bit <<= 1, done = false;
        bit <<= 1;
    } while (!done);
}

// Deposits the low bits of `value` into the set bits of `mask`.
uint32_t deposit(uint32_t mask, uint32_t value) {
    uint32_t result = 0;
    for (uint32_t bit = 1; value && mask; bit <<= 1) {
        if (!(mask & bit)) continue;
        if (value & 1) result |= bit;
        value >>= 1;
        mask &= ~bit;
    }
    return result;
}

}  // namespace

bool describe_texture(uint32_t format_register, uint32_t size_field, TextureLayout& layout) {
    layout = TextureLayout{};
    layout.color_format = (format_register & kFormatFormatMask) >> kFormatFormatShift;
    const FormatInfo* info = find_format(layout.color_format);
    if (!info) return false;
    layout.cube = (format_register & kFormatCubemap) != 0;
    layout.volume = ((format_register >> kFormatDimensionShift) & 0xF) == 3;
    if (layout.cube || layout.volume) return false;

    layout.linear = info->linear;
    layout.swizzled = !info->linear && info->bytes != 0;
    layout.bytes_per_texel = info->bytes;
    switch (info->texel) {
        case Texel::Dxt1: layout.kind = TextureLayout::Kind::Dxt1; break;
        case Texel::Dxt3: layout.kind = TextureLayout::Kind::Dxt3; break;
        case Texel::Dxt5: layout.kind = TextureLayout::Kind::Dxt5; break;
        default: layout.kind = TextureLayout::Kind::Argb; break;
    }
    if (info->linear) {
        if (!size_field) return false;
        layout.width = (size_field & kSizeWidthMask) + 1;
        layout.height = ((size_field & kSizeHeightMask) >> kSizeHeightShift) + 1;
        layout.pitch = ((size_field >> kSizePitchShift) + 1) * 64;
        layout.levels = 1;
        layout.total_size = static_cast<size_t>(layout.pitch) * layout.height;
        return true;
    }
    layout.width = 1u << ((format_register >> kFormatUSizeShift) & 0xF);
    layout.height = 1u << ((format_register >> kFormatVSizeShift) & 0xF);
    layout.levels = std::clamp<uint32_t>((format_register >> kFormatMipmapShift) & 0xF, 1, 16);
    size_t offset = 0;
    for (uint32_t level = 0; level < layout.levels; ++level) {
        layout.level_offset[level] = offset;
        const uint32_t w = level_width(layout, level), h = level_height(layout, level);
        if (layout.kind == TextureLayout::Kind::Argb)
            offset += static_cast<size_t>(w) * h * layout.bytes_per_texel;
        else
            offset += static_cast<size_t>((w + 3) / 4) * ((h + 3) / 4) * (layout.kind == TextureLayout::Kind::Dxt1 ? 8 : 16);
    }
    layout.total_size = offset;
    return true;
}

uint32_t level_width(const TextureLayout& layout, uint32_t level) { return std::max(1u, layout.width >> level); }
uint32_t level_height(const TextureLayout& layout, uint32_t level) { return std::max(1u, layout.height >> level); }

void decode_level(const TextureLayout& layout, uint32_t level, const uint8_t* texture_memory,
                  const uint32_t* palette, uint8_t* out, uint32_t out_pitch) {
    const uint32_t w = level_width(layout, level), h = level_height(layout, level);
    const uint8_t* src = texture_memory + layout.level_offset[level];
    if (layout.kind != TextureLayout::Kind::Argb) {  // DXT: rows of 4x4 blocks, as on the PC
        const uint32_t block_bytes = layout.kind == TextureLayout::Kind::Dxt1 ? 8 : 16;
        const uint32_t row_bytes = (w + 3) / 4 * block_bytes;
        for (uint32_t row = 0; row < (h + 3) / 4; ++row)
            std::memcpy(out + static_cast<size_t>(row) * out_pitch, src + static_cast<size_t>(row) * row_bytes, row_bytes);
        return;
    }
    const FormatInfo* info = find_format(layout.color_format);
    const uint32_t bpp = layout.bytes_per_texel;
    if (layout.swizzled) {
        uint32_t mask_x, mask_y;
        swizzle_masks(w, h, mask_x, mask_y);
        for (uint32_t y = 0; y < h; ++y) {
            auto* line = reinterpret_cast<uint32_t*>(out + static_cast<size_t>(y) * out_pitch);
            const uint32_t y_bits = deposit(mask_y, y);
            for (uint32_t x = 0; x < w; ++x)
                line[x] = to_argb(info->texel, src + static_cast<size_t>(deposit(mask_x, x) | y_bits) * bpp, palette);
        }
    } else {
        for (uint32_t y = 0; y < h; ++y) {
            auto* line = reinterpret_cast<uint32_t*>(out + static_cast<size_t>(y) * out_pitch);
            const uint8_t* row = src + static_cast<size_t>(y) * layout.pitch;
            if (info->texel == Texel::Yuy2 || info->texel == Texel::Uyvy) {
                for (uint32_t x = 0; x < w; ++x) line[x] = yuv_texel(info->texel, row, x);
                continue;
            }
            for (uint32_t x = 0; x < w; ++x) line[x] = to_argb(info->texel, row + static_cast<size_t>(x) * bpp, palette);
        }
    }
}

}  // namespace render
