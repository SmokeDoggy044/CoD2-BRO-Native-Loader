#pragma once

// Where one face/level of an Xbox texture lives (D3D's internal helper at 003ee960):
// shared by render (D3DTexture_GetSurfaceLevel2) and the compare harness's stand-in
// for it, which runs in test processes where only the harness's own section is
// executable. So: no library calls, and every function takes its linkage and section
// from SURFACE_LAYOUT_FN (default `static inline`; the harness defines it first).
//
// Layout rules (XDK 5849): swizzled and compressed textures have power-of-two sizes
// stored as log2 in Format (U, V, P = width, height, depth). Mip level n halves each
// down to 1; compressed formats (DXT) never go below a 4x4 block. Levels follow each
// other without padding; cube faces are the whole mip chain each, rounded up to 128
// bytes. Linear textures (Size != 0) have one level with the pitch in Size.

#include <cstdint>

#ifndef SURFACE_LAYOUT_FN
#define SURFACE_LAYOUT_FN static inline
#endif

namespace render::surface_layout {

// D3D's per-format table (XDK 5849, D3DFORMAT 0x00..0x41): bits 2-5 bits per pixel,
// 0x80 usable as a render target, 0x40 as a depth buffer.
static const uint8_t kFormatInfo[] = {
    0x09, 0x09, 0x11, 0x91, 0x11, 0x91, 0xa1, 0xa1, 0x00, 0x00, 0x00, 0x09, 0x04, 0x00, 0x08, 0x08,
    0x12, 0x92, 0xa2, 0x8a, 0x00, 0x00, 0x12, 0x92, 0x00, 0x09, 0x11, 0x0a, 0x92, 0x12, 0xa2, 0x0a,
    0x12, 0x00, 0x00, 0x00, 0x12, 0x12, 0x00, 0x11, 0x11, 0x11, 0x61, 0x61, 0x51, 0x51, 0x62, 0x62,
    0x52, 0x52, 0x11, 0x21, 0x00, 0x12, 0x00, 0x12, 0x11, 0x11, 0x21, 0x21, 0x21, 0x12, 0x12, 0x22,
    0x22, 0x22,
};
constexpr uint32_t kFormatCount = sizeof(kFormatInfo);

// Format and Size fields (the same bits as xbox_d3d.h's kFormat* / kSize*).
constexpr uint32_t kFormatFormatShift = 8;
constexpr uint32_t kFormatFormatMask = 0x0000FF00;
constexpr uint32_t kFormatMipmapShift = 16;
constexpr uint32_t kFormatUSizeShift = 20;
constexpr uint32_t kFormatVSizeShift = 24;
constexpr uint32_t kFormatPSizeShift = 28;
constexpr uint32_t kSizeHeightMask = 0x00FFF000;
constexpr uint32_t kSizeHeightShift = 12;
constexpr uint32_t kSizePitchShift = 24;

SURFACE_LAYOUT_FN uint32_t format_of(uint32_t format_field) {
    return (format_field & kFormatFormatMask) >> kFormatFormatShift;
}

SURFACE_LAYOUT_FN uint32_t format_flags(uint32_t format) { return format < kFormatCount ? kFormatInfo[format] : 0; }

SURFACE_LAYOUT_FN uint32_t bits_per_pixel(uint32_t format) { return format_flags(format) & 0x3C; }

SURFACE_LAYOUT_FN bool is_compressed(uint32_t format) { return format == 0x0C || format == 0x0E || format == 0x0F; }

SURFACE_LAYOUT_FN uint32_t at_least(uint32_t value, uint32_t minimum) { return value > minimum ? value : minimum; }

struct SurfaceLevel {
    uint32_t address;  // CPU address
    uint32_t pitch;
    uint32_t size;     // one slice
    uint32_t format;   // Format field for a surface of this level
    uint32_t size_field;
};

// The texture's Data, Format and Size fields in; face 0 for plain textures.
SURFACE_LAYOUT_FN SurfaceLevel surface_level(uint32_t data, uint32_t format_field, uint32_t size_field, uint32_t face,
                                             uint32_t level) {
    SurfaceLevel out{};
    out.address = data | 0x80000000;
    if (size_field != 0) {
        out.pitch = ((size_field >> kSizePitchShift) + 1) * 64;
        out.size = (((size_field & kSizeHeightMask) >> kSizeHeightShift) + 1) * out.pitch;
        out.format = format_field;
        out.size_field = size_field;
        return out;
    }
    const uint32_t format = format_of(format_field);
    const uint32_t bpp = bits_per_pixel(format);
    const uint32_t min_log = is_compressed(format) ? 2 : 0;
    uint32_t u = (format_field >> kFormatUSizeShift) & 0xF;
    uint32_t v = (format_field >> kFormatVSizeShift) & 0xF;
    uint32_t p = format_field >> kFormatPSizeShift;
    if (face != 0) {
        const uint32_t levels = (format_field >> kFormatMipmapShift) & 0xF;
        uint32_t face_size = 0, fu = u, fv = v;
        for (uint32_t i = 0; i < levels; ++i) {
            face_size += (1u << (at_least(fv, min_log) + at_least(fu, min_log))) * bpp / 8;
            if (fu) --fu;
            if (fv) --fv;
        }
        out.address += ((face_size + 0x7F) & ~0x7Fu) * face;
    }
    for (uint32_t i = 0; i < level; ++i) {
        out.address += (1u << (at_least(v, min_log) + at_least(u, min_log) + p)) * bpp / 8;
        if (u) --u;
        if (v) --v;
        if (p) --p;
    }
    const uint32_t pu = at_least(u, min_log), pv = at_least(v, min_log);
    if (format == 0x0C)
        out.pitch = (1u << pu) * 2;
    else if (is_compressed(format))
        out.pitch = (1u << pu) * 4;
    else
        out.pitch = (1u << pu) * bpp / 8;
    out.size = (1u << (pv + pu)) * bpp / 8;
    out.format = (((p << 4 | v) << 4 | u) << kFormatUSizeShift) | (format_field & 0xFFFFF);
    out.size_field = size_field;
    return out;
}

// D3DSURFACE_DESC {Format, Type, Usage, Size, MultiSampleType, Width, Height} of one level
// of a pixel container (its Common, Format and Size fields): render's D3DTexture_GetLevelDesc
// and D3DSurface_GetDesc (level 0), and the harness's GetDesc stand-in.
SURFACE_LAYOUT_FN void describe_level(uint32_t common, uint32_t format_field, uint32_t size_field, uint32_t level,
                                      uint32_t* desc) {
    const uint32_t format = format_of(format_field);
    const bool volume = (format_field & 0xF0) > (2u << 4);
    uint32_t type = 10;
    switch (common & 0x00070000) {
    case 0x00000000: type = 6; break;  // vertex buffer
    case 0x00010000: type = 7; break;  // index buffer
    case 0x00020000: type = 8; break;  // push buffer
    case 0x00030000: type = 9; break;  // palette
    case 0x00040000: type = (format_field & 0x4) ? 5 : volume ? 4 : 3; break;  // texture (cube, volume)
    case 0x00050000: type = volume ? 2 : 1; break;  // surface
    default: break;
    }
    uint32_t usage = 0;
    if (level == 0) {
        if (format_flags(format) & 0x80)
            usage = 1;  // D3DUSAGE_RENDERTARGET
        else if (format_flags(format) & 0x40)
            usage = 2;  // D3DUSAGE_DEPTHSTENCIL
    }
    uint32_t width, height, slice_size;
    if (size_field != 0) {
        width = (size_field & 0x00000FFF) + 1;
        height = ((size_field & kSizeHeightMask) >> kSizeHeightShift) + 1;
        slice_size = height * (((size_field >> kSizePitchShift) + 1) * 64);
    } else {
        const int32_t u = static_cast<int32_t>((format_field >> kFormatUSizeShift) & 0xF) - static_cast<int32_t>(level);
        const int32_t v = static_cast<int32_t>((format_field >> kFormatVSizeShift) & 0xF) - static_cast<int32_t>(level);
        const int32_t min_log = is_compressed(format) ? 2 : 0;
        width = 1u << (u > 0 ? u : 0);
        height = 1u << (v > 0 ? v : 0);
        const uint32_t padded_width = 1u << (u > min_log ? u : min_log);
        const uint32_t padded_height = 1u << (v > min_log ? v : min_log);
        slice_size = padded_height * padded_width * bits_per_pixel(format) / 8;
    }
    desc[0] = format;
    desc[1] = type;
    desc[2] = usage;
    desc[3] = slice_size;
    desc[4] = 0x11;  // D3DMULTISAMPLE_NONE
    desc[5] = width;
    desc[6] = height;
}

}  // namespace render::surface_layout
