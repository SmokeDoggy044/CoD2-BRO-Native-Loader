#include "xgraphics.h"

#include <bit>
#include <cstdio>
#include <cstring>

namespace game {

namespace {

// Per D3D format (0x00416790): bit 0 = swizzled, bits 2-5 = bits per pixel.
constexpr uint8_t kFormatInfo[0x44] = {
    0x09, 0x09, 0x11, 0x91, 0x11, 0x91, 0xa1, 0xa1, 0x00, 0x00, 0x00, 0x09, 0x04, 0x00, 0x08, 0x08, 0x12,
    0x92, 0xa2, 0x0a, 0x00, 0x00, 0x12, 0x12, 0x00, 0x09, 0x11, 0x0a, 0x92, 0x12, 0xa2, 0x0a, 0x12, 0x00,
    0x00, 0x00, 0x10, 0x10, 0x00, 0x11, 0x11, 0x11, 0x61, 0x61, 0x51, 0x51, 0x62, 0x62, 0x52, 0x52, 0x11,
    0x21, 0x00, 0x12, 0x00, 0x12, 0x11, 0x11, 0x21, 0x21, 0x21, 0x12, 0x12, 0x22, 0x22, 0x22, 0x00, 0x00,
};

// Bytes per pixel per format (0x003fa1c1, an index into {1, 2, 4, 0}).
constexpr uint8_t kBytesPerPixel[0x42] = {
    1, 1, 2, 2, 2, 2, 4, 4, 0, 0, 0, 1, 0, 0, 1, 1, 2, 2, 4, 1, 0, 0, 2, 2, 0, 1, 2, 1, 2, 2, 4, 1, 2,
    0, 0, 0, 4, 4, 0, 2, 2, 2, 4, 4, 2, 2, 4, 4, 2, 2, 2, 4, 0, 2, 0, 2, 2, 2, 4, 4, 4, 2, 2, 4, 4, 4,
};

// 0x003f9740: the DXT formats (0x0c, 0x0e, 0x0f).
bool is_compressed(uint32_t format) { return format == 0xc || (format > 0xd && format <= 0xf); }

// 0x003f9755: BSF.
uint32_t log2_of(uint32_t value) { return static_cast<uint32_t>(std::countr_zero(value)); }

// 0x003f975f (ECX = height, AL = cube): the Format and Size words of a texture or surface header;
// returns the byte size (all levels; a cube's faces 128-aligned).
uint32_t texture_format(uint32_t width, uint32_t height, uint32_t depth, uint32_t levels, uint32_t format,
                        uint32_t pitch, bool volume, uint32_t* format_word, uint32_t* size_word, bool cube) {
    const uint8_t info = format < sizeof kFormatInfo ? kFormatInfo[format] : 0;
    const uint32_t bits = info & 0x3c;
    uint32_t size = 0, log_w = 0, log_h = 0, log_d = 0, size_w = 0, size_h = 0;
    if (!(info & 1) && !is_compressed(format)) {
        // Linear: one level, rows `pitch` bytes apart (default: 64-byte aligned).
        if (!levels) levels = 1;
        if (!pitch) pitch = ((bits * width >> 3) + 0x3f) & ~0x3fu;
        size_w = width;
        size_h = height;
        size = height * pitch;
    } else {
        log_w = log2_of(width);
        log_h = log2_of(height);
        log_d = log2_of(depth);
        const uint32_t floor = is_compressed(format) ? 2 : 0;  // 4x4 blocks
        if (!levels) {
            const uint32_t hd = log_h > log_d ? log_h : log_d;
            levels = (log_w > hd ? log_w : hd) + 1;
        }
        uint32_t w = log_w, h = log_h, d = log_d;
        for (uint32_t level = levels; level; --level) {
            const uint32_t shift = (w > floor ? w : floor) + (h > floor ? h : floor) + d;
            size += ((1u << (shift & 31)) * bits) >> 3;
            if (w) --w;
            if (h) --h;
            if (d) --d;
        }
        if (cube) size = ((size + 0x7f) & ~0x7fu) * 6;
    }
    *format_word = (((((((log_d << 4 | log_h) << 4 | log_w) << 4 | levels) << 8 | format) << 4 |
                       (volume ? 3u : 2u))
                      << 4) |
                     (cube ? 4u : 0u)) |
                   9;
    if (size_w)
        *size_word = ((((pitch >> 6) - 1) << 12 | (size_h - 1)) << 12) | (size_w - 1);
    else
        *size_word = 0;
    return size;
}

// 0x003f990a (ECX = height): the texture header both texture setters share.
uint32_t texture_header(uint32_t width, uint32_t height, uint32_t depth, uint32_t levels, uint32_t usage,
                        uint32_t format, uint32_t pitch, bool cube, bool volume, uint32_t data, uint32_t* texture) {
    const uint32_t size =
        texture_format(width, height, depth, levels, format, pitch, volume, &texture[3], &texture[4], cube);
    if (usage & 0x10000) texture[3] &= ~8u;
    texture[2] = 0;
    texture[0] = 0x40001;
    texture[1] = data;
    return size;
}

}  // namespace

uint32_t XB_STDCALL D3D_ReturnOne(uint32_t) { return 1; }

uint32_t XB_STDCALL XGSetTextureHeader(uint32_t width, uint32_t height, uint32_t levels, uint32_t usage,
                                       uint32_t format, uint32_t, uint32_t* texture, uint32_t data,
                                       uint32_t pitch) {
    return texture_header(width, height, 1, levels, usage, format, pitch, false, false, data, texture);
}

uint32_t XB_STDCALL XGSetCubeTextureHeader(uint32_t edge, uint32_t levels, uint32_t usage, uint32_t format,
                                           uint32_t, uint32_t* cube, uint32_t data, uint32_t pitch) {
    return texture_header(edge, edge, 1, levels, usage, format, pitch, true, false, data, cube);
}

uint32_t XB_STDCALL XGSetSurfaceHeader(uint32_t width, uint32_t height, uint32_t format, uint32_t* surface,
                                       uint32_t data, uint32_t pitch) {
    const uint32_t size = texture_format(width, height, 1, 1, format, pitch, false, &surface[3], &surface[4], false);
    surface[2] = 0;
    surface[5] = 0;
    surface[0] = 0x50001;
    surface[1] = data;
    return size;
}

uint32_t XB_STDCALL XGSetVertexBufferHeader(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t* buffer,
                                            uint32_t data) {
    buffer[2] = 0;
    buffer[0] = 1;
    buffer[1] = data;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffer));
}

uint32_t XB_STDCALL XGSetPaletteHeader(uint32_t size, uint32_t* palette, uint32_t data) {
    palette[2] = 0;
    palette[0] = size << 30 | 0x30001;
    palette[1] = data;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(palette));
}

uint32_t XB_STDCALL XGBytesPerPixelFromFormat(uint32_t format) {
    return format < sizeof kBytesPerPixel ? kBytesPerPixel[format] : 0;
}

uint32_t XB_THISCALL Swizzler_Init(Swizzler* self, uint32_t width, uint32_t height, uint32_t depth) {
    self->width = width;
    self->height = height;
    self->depth = depth;
    self->mask_u = self->mask_v = self->mask_w = 0;
    self->u = self->v = self->w = 0;
    uint32_t bit = 1, size = 1, next;
    do {
        next = 0;
        if (size < width) self->mask_u |= bit, bit <<= 1, next = bit;
        if (size < height) self->mask_v |= bit, bit <<= 1, next = bit;
        if (size < depth) self->mask_w |= bit, bit <<= 1, next = bit;
        size <<= 1;
    } while (next);
    return bit;
}

namespace {

// Deposits the low bits of `value` into the bits of `mask`, low to high.
uint32_t spread(uint32_t mask, uint32_t value) {
    uint32_t result = 0;
    for (uint32_t bit = 1; bit <= mask; bit <<= 1) {
        if (bit & mask)
            result |= bit & value;
        else
            value <<= 1;
    }
    return result;
}

}  // namespace

uint32_t XB_THISCALL Swizzler_SwizzleU(const Swizzler* self, uint32_t value) { return spread(self->mask_u, value); }

uint32_t XB_THISCALL Swizzler_SwizzleV(const Swizzler* self, uint32_t value) { return spread(self->mask_v, value); }

void XB_STDCALL XGSwizzleRect(const void* source, uint32_t pitch, const int32_t* rect, void* dest, uint32_t width,
                              uint32_t height, const int32_t* point, uint32_t bytes_per_pixel) {
    if (width <= 2 || height <= 1) {
        platform_rep_movs(dest, source, width * height * bytes_per_pixel);
        return;
    }
    // The original has whole-texture fast paths (4 bytes >= 8x8, 2 bytes >= 16x8, 1 byte >= 16x8) and
    // a tiled loop for the rest; every path writes each source pixel to its swizzled offset, as here.
    uint32_t left = 0, top = 0, columns = width, rows = height;
    if (rect) {
        left = static_cast<uint32_t>(rect[0]);
        top = static_cast<uint32_t>(rect[1]);
        columns = static_cast<uint32_t>(rect[2]) - left;
        rows = static_cast<uint32_t>(rect[3]) - top;
    }
    const uint32_t x0 = point ? static_cast<uint32_t>(point[0]) : 0;
    const uint32_t y0 = point ? static_cast<uint32_t>(point[1]) : 0;
    if (!pitch) pitch = width * bytes_per_pixel;
    Swizzler swizzler;
    Swizzler_Init(&swizzler, width, height, 0);
    const uint8_t* const in = static_cast<const uint8_t*>(source);
    uint8_t* const out = static_cast<uint8_t*>(dest);
    for (uint32_t y = 0; y < rows; ++y) {
        const uint8_t* row = in + (top + y) * pitch + left * bytes_per_pixel;
        const uint32_t v = Swizzler_SwizzleV(&swizzler, y0 + y);
        for (uint32_t x = 0; x < columns; ++x)
            std::memcpy(out + (Swizzler_SwizzleU(&swizzler, x0 + x) | v) * bytes_per_pixel,
                        row + x * bytes_per_pixel, bytes_per_pixel);
    }
}

}  // namespace game

namespace game {

namespace {

// D3DPIXELSHADERDEF (0xf0 bytes): the NV2A register combiner state of an xps shader.
struct PixelShaderDef {
    uint32_t alpha_inputs[8];
    uint32_t final_inputs_abcd, final_inputs_efg;
    uint32_t constant0[8], constant1[8];
    uint32_t alpha_outputs[8];
    uint32_t rgb_inputs[8];
    uint32_t compare_mode;
    uint32_t final_constant0, final_constant1;
    uint32_t rgb_outputs[8];
    uint32_t combiner_count;  // stages | mux/unique-constant flags
    uint32_t texture_modes, dot_mapping, input_texture;
    uint32_t c0_mapping, c1_mapping;  // per stage: the constant register each stage's c0 / c1 reads
    uint32_t final_constants;         // the final combiner's c0 / c1 registers, flags
};
static_assert(sizeof(PixelShaderDef) == 0xf0);

struct KnownShader {
    const char* source;
    const void* data;
    uint32_t size;
};

// "tex t0 / mul r0.rgb, t0.a, t0.rgb / xfc 1-zero, r0, zero, zero, zero, zero, 1-zero": one stage
// (RGB = t0.a * t0.rgb into r0), the final combiner outputs r0.rgb with alpha 1; t0 projective 2D.
constexpr PixelShaderDef kPremultiplyAlpha = {
    .alpha_inputs = {},
    .final_inputs_abcd = 0x200c0000,
    .final_inputs_efg = 0x00002080,
    .constant0 = {}, .constant1 = {},
    .alpha_outputs = {},
    .rgb_inputs = {0xd8c80000},
    .compare_mode = 0,
    .final_constant0 = 0, .final_constant1 = 0,
    .rgb_outputs = {0x000000c0},
    .combiner_count = 0x00011101,
    .texture_modes = 0x00000001,
    .dot_mapping = 0, .input_texture = 0,
    .c0_mapping = 0xffffffff, .c1_mapping = 0xffffffff,
    .final_constants = 0x000001ff,
};

// "def c0, 0.5 ... / tex t0..t3 / xmma r0 = v0.a*t0 + v0.a*t1 / xmma r1 = v0.a*t2 + v0.a*t3 /
// xfc c0.a, r0, r1, ...": two stages (RGB and alpha alike), the final combiner lerps r0 and r1 by
// c0.a (0.5, final constant 0 = 0x80808080); four projective 2D textures.
constexpr PixelShaderDef kFourTapAverage = {
    .alpha_inputs = {0xd4d8d4d9, 0xd4dad4db},
    .final_inputs_abcd = 0x110c0d00,
    .final_inputs_efg = 0x00002080,
    .constant0 = {}, .constant1 = {},
    .alpha_outputs = {0x00000c00, 0x00000d00},
    .rgb_inputs = {0xd4c8d4c9, 0xd4cad4cb},
    .compare_mode = 0,
    .final_constant0 = 0x80808080, .final_constant1 = 0,
    .rgb_outputs = {0x00000c00, 0x00000d00},
    .combiner_count = 0x00011102,
    .texture_modes = 0x00008421,
    .dot_mapping = 0, .input_texture = 0,
    .c0_mapping = 0xffffffff, .c1_mapping = 0xffffffff,
    .final_constants = 0x000001f0,
};

// "tex t0 / mul_x2 r0, v0.a, t0 / xfc 1-zero, r0, zero, zero, zero, zero, r0.a": one stage
// (r0 = v0.a * t0 * 2, RGB and alpha), the final combiner outputs r0 with r0's alpha.
constexpr PixelShaderDef kVertexAlphaTimesTwo = {
    .alpha_inputs = {0xd4d81010},
    .final_inputs_abcd = 0x200c0000,
    .final_inputs_efg = 0x00001c80,
    .constant0 = {}, .constant1 = {},
    .alpha_outputs = {0x000100c0},
    .rgb_inputs = {0xd4c80000},
    .compare_mode = 0,
    .final_constant0 = 0, .final_constant1 = 0,
    .rgb_outputs = {0x000100c0},
    .combiner_count = 0x00011101,
    .texture_modes = 0x00000001,
    .dot_mapping = 0, .input_texture = 0,
    .c0_mapping = 0xffffffff, .c1_mapping = 0xffffffff,
    .final_constants = 0x000001ff,
};

// "#pragma screenspace / mov oPos, v0 / mov oD0, v1 / mov oT0..oT3, v2..v5": the program header
// (0x2078: screen-space program, 6 instructions) and six NV2A MAC MOV instructions (128 bits each:
// input v<n> in word 1 bits 9-12, output address in word 3 bits 3-10, the last one flagged final).
constexpr uint32_t kScreenSpacePassThrough[1 + 6 * 4] = {
    0x00062078,
    0x00000000, 0x02000000, 0x0800106c, 0x20b0f804,  // MOV oPos, v0
    0x00000000, 0x02000200, 0x0800106c, 0x60b0f81c,  // MOV oD0, v1
    0x00000000, 0x02000400, 0x0800106c, 0xa0b0f84c,  // MOV oT0, v2
    0x00000000, 0x02000600, 0x0800106c, 0xe0b0f854,  // MOV oT1, v3
    0x00000000, 0x02000800, 0x0800106d, 0x20b0f85c,  // MOV oT2, v4
    0x00000000, 0x02000a00, 0x0800106d, 0x60b0f865,  // MOV oT3, v5 (final)
};

// The texts as nglAssemblePixelShader / nglAssembleVertexShader build them (title strings
// 0x007b38c8, 0x007b37b0, 0x007b3760, 0x007b3710 behind their prefixes).
const KnownShader kKnownShaders[] = {
    {"xps.1.1\ntex t0\nmul r0.rgb, t0.a, t0.rgb\nxfc 1-zero, r0, zero, zero, zero, zero, 1-zero\n",
     &kPremultiplyAlpha, sizeof kPremultiplyAlpha},
    {"xps.1.1\ndef c0, 0.5, 0.5, 0.5, 0.5\ntex t0\ntex t1\ntex t2\ntex t3\n"
     "xmma discard, discard, r0, v0.a, t0, v0.a, t1  // = (v0*2) * (t0 + t1)/2  \n"
     "xmma discard, discard, r1, v0.a, t2, v0.a, t3  // = (v0*2) * (t2 + t3)/2  \n"
     "xfc c0.a, r0, r1, zero, zero, zero, 1-zero       // = (r0 + r1)/2  \n",
     &kFourTapAverage, sizeof kFourTapAverage},
    {"xps.1.1\ntex t0\nmul_x2 r0, v0.a, t0\nxfc 1-zero, r0, zero, zero, zero, zero, r0.a\n", &kVertexAlphaTimesTwo,
     sizeof kVertexAlphaTimesTwo},
    {"xvs.1.1\n#pragma screenspace\nmov oPos, v0\nmov oD0,  v1\nmov oT0,  v2\nmov oT1,  v3\nmov oT2,  v4\n"
     "mov oT3,  v5\n",
     kScreenSpacePassThrough, sizeof kScreenSpacePassThrough},
};

}  // namespace

uint32_t XB_STDCALL XGAssembleShader(const char*, const char* source, uint32_t length, uint32_t,
                                     XgBuffer** constants, XgBuffer** compiled, XgBuffer** errors,
                                     XgBuffer** listing, uint32_t, uint32_t, uint32_t*) {
    if (constants) *constants = nullptr;
    if (errors) *errors = nullptr;
    if (listing) *listing = nullptr;
    for (const KnownShader& known : kKnownShaders) {
        if (std::strlen(known.source) != length || std::memcmp(known.source, source, length) != 0) continue;
        // Title memory (32-bit addresses); like the original's XMemAlloc'd buffer, never freed.
        uint8_t* const data = new uint8_t[known.size];
        std::memcpy(data, known.data, known.size);
        XgBuffer* const buffer = new XgBuffer{1, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(data)), known.size};
        if (compiled) *compiled = buffer;
        return 0;
    }
    std::fprintf(stderr, "XGAssembleShader: no assembled form of this text (%u bytes):\n%.*s\n", length,
                 static_cast<int>(length), source);
    if (compiled) *compiled = nullptr;
    return 0x80004005u;  // E_FAIL
}

}  // namespace game
