// Xbox Direct3D 8 replacement: texture and surface layout, descriptions and locks.
// See xbox_d3d_api.h.
//
// Layout rules (XDK 5849):
// - Swizzled and compressed textures have power-of-two sizes stored as log2 in Format
//   (U, V, P = width, height, depth). Mip level n halves each down to 1; compressed
//   formats (DXT) never go below a 4x4 block. Levels follow each other without
//   padding; cube faces are the whole mip chain each, rounded up to 128 bytes.
// - Linear textures (Size != 0) have one level with the pitch in Size.
// - Data is the physical address; the CPU address is Data | 0x80000000.

#include <algorithm>
#include <cstring>
#include <mutex>
#include <set>

#include "compiler.h"
#include "d3d_internal.h"
#include "log.h"
#include "xbox_d3d.h"

namespace render {

namespace {

using xbox::from_xbox_ptr;
using xbox::to_xbox_ptr;

// D3D's per-format table (XDK 5849, D3DFORMAT 0x00..0x41): bits 2-5 bits per pixel,
// 0x80 usable as a render target, 0x40 as a depth buffer.
const uint8_t kFormatInfo[] = {
    0x09, 0x09, 0x11, 0x91, 0x11, 0x91, 0xa1, 0xa1, 0x00, 0x00, 0x00, 0x09, 0x04, 0x00, 0x08, 0x08,
    0x12, 0x92, 0xa2, 0x8a, 0x00, 0x00, 0x12, 0x92, 0x00, 0x09, 0x11, 0x0a, 0x92, 0x12, 0xa2, 0x0a,
    0x12, 0x00, 0x00, 0x00, 0x12, 0x12, 0x00, 0x11, 0x11, 0x11, 0x61, 0x61, 0x51, 0x51, 0x62, 0x62,
    0x52, 0x52, 0x11, 0x21, 0x00, 0x12, 0x00, 0x12, 0x11, 0x11, 0x21, 0x21, 0x21, 0x12, 0x12, 0x22,
    0x22, 0x22,
};

uint32_t format_of(const XbPixelContainer& pixels) { return (pixels.format & kFormatFormatMask) >> kFormatFormatShift; }

uint32_t bits_per_pixel(uint32_t format) {
    return format < std::size(kFormatInfo) ? kFormatInfo[format] & 0x3C : 0;
}

bool is_compressed(uint32_t format) { return format == 0x0C || format == 0x0E || format == 0x0F; }

uint32_t format_flags(uint32_t format) { return format < std::size(kFormatInfo) ? kFormatInfo[format] : 0; }

struct LevelLayout {
    uint32_t width, height, depth;
    uint32_t pitch;       // bytes per row (of blocks, for compressed formats)
    uint32_t slice_size;  // bytes per depth slice
};

// One mip level's size (D3D's internal helper at 003ee760).
LevelLayout level_layout(const XbPixelContainer& pixels, uint32_t level) {
    LevelLayout out{};
    if (pixels.size != 0) {
        out.width = (pixels.size & kSizeWidthMask) + 1;
        out.height = ((pixels.size & kSizeHeightMask) >> kSizeHeightShift) + 1;
        out.depth = 1;
        out.pitch = ((pixels.size >> kSizePitchShift) + 1) * 64;
        out.slice_size = out.height * out.pitch;
        return out;
    }
    const uint32_t format = format_of(pixels);
    const uint32_t bpp = bits_per_pixel(format);
    const int32_t u = static_cast<int32_t>((pixels.format >> kFormatUSizeShift) & 0xF) - static_cast<int32_t>(level);
    const int32_t v = static_cast<int32_t>((pixels.format >> kFormatVSizeShift) & 0xF) - static_cast<int32_t>(level);
    const int32_t p = static_cast<int32_t>(pixels.format >> kFormatPSizeShift) - static_cast<int32_t>(level);
    out.width = 1u << std::max(u, 0);
    out.height = 1u << std::max(v, 0);
    out.depth = 1u << std::max(p, 0);
    const int32_t min_log = is_compressed(format) ? 2 : 0;
    const uint32_t padded_width = 1u << std::max(u, min_log);
    const uint32_t padded_height = 1u << std::max(v, min_log);
    if (format == 0x0C)
        out.pitch = padded_width * 2;  // DXT1: 8 bytes per 4x4 block
    else if (is_compressed(format))
        out.pitch = padded_width * 4;  // DXT3/5: 16 bytes per block
    else
        out.pitch = padded_width * bpp / 8;
    out.slice_size = padded_height * padded_width * bpp / 8;
    return out;
}

struct SurfaceLevel {
    uint32_t address;  // CPU address
    uint32_t pitch;
    uint32_t size;     // one slice
    uint32_t format;   // Format field for a surface of this level
    uint32_t size_field;
};

// Where a face/level of a texture lives (D3D's internal helper at 003ee960).
SurfaceLevel surface_level(const XbPixelContainer& pixels, uint32_t face, uint32_t level) {
    SurfaceLevel out{};
    out.address = pixels.resource.data | 0x80000000;
    if (pixels.size != 0) {
        out.pitch = ((pixels.size >> kSizePitchShift) + 1) * 64;
        out.size = (((pixels.size & kSizeHeightMask) >> kSizeHeightShift) + 1) * out.pitch;
        out.format = pixels.format;
        out.size_field = pixels.size;
        return out;
    }
    const uint32_t format = format_of(pixels);
    const uint32_t bpp = bits_per_pixel(format);
    const uint32_t min_log = is_compressed(format) ? 2 : 0;
    uint32_t u = (pixels.format >> kFormatUSizeShift) & 0xF;
    uint32_t v = (pixels.format >> kFormatVSizeShift) & 0xF;
    uint32_t p = pixels.format >> kFormatPSizeShift;
    if (face != 0) {
        const uint32_t levels = (pixels.format >> kFormatMipmapShift) & 0xF;
        uint32_t face_size = 0, fu = u, fv = v;
        for (uint32_t i = 0; i < levels; ++i) {
            face_size += (1u << (std::max(fv, min_log) + std::max(fu, min_log))) * bpp / 8;
            if (fu) --fu;
            if (fv) --fv;
        }
        out.address += ((face_size + 0x7F) & ~0x7Fu) * face;
    }
    for (uint32_t i = 0; i < level; ++i) {
        out.address += (1u << (std::max(v, min_log) + std::max(u, min_log) + p)) * bpp / 8;
        if (u) --u;
        if (v) --v;
        if (p) --p;
    }
    const uint32_t pu = std::max(u, min_log), pv = std::max(v, min_log);
    if (format == 0x0C)
        out.pitch = (1u << pu) * 2;
    else if (is_compressed(format))
        out.pitch = (1u << pu) * 4;
    else
        out.pitch = (1u << pu) * bpp / 8;
    out.size = (1u << (pv + pu)) * bpp / 8;
    out.format = (((p << 4 | v) << 4 | u) << kFormatUSizeShift) | (pixels.format & 0xFFFFF);
    out.size_field = pixels.size;
    return out;
}

// Surfaces we create for texture levels; deleted when released to zero.
std::mutex g_surfaces_mutex;
std::set<XbPtr> g_level_surfaces;

XbSurface* create_level_surface(XbPtr texture, const SurfaceLevel& level) {
    auto* surface = new XbSurface{};
    surface->container.resource.common = kCommonTypeSurface | kCommonD3DCreated | 1;
    surface->container.resource.data = level.address & 0x0FFFFFFF;
    surface->container.format = level.format;
    surface->container.size = level.size_field;
    surface->parent = texture;
    // The surface holds a reference on its texture.
    ++from_xbox_ptr<XbResource>(texture)->common;
    std::lock_guard lock(g_surfaces_mutex);
    g_level_surfaces.insert(to_xbox_ptr(surface));
    return surface;
}

// --- Descriptions ------------------------------------------------------------------------

struct XbSurfaceDesc {  // D3DSURFACE_DESC
    uint32_t format;
    uint32_t type;
    uint32_t usage;
    uint32_t size;
    uint32_t multisample_type;
    uint32_t width;
    uint32_t height;
};
static_assert(sizeof(XbSurfaceDesc) == 28);

constexpr uint32_t kMultiSampleNone = 0x11;

void describe(XbPtr container, uint32_t level, XbPtr desc_address) {
    const auto& pixels = *from_xbox_ptr<XbPixelContainer>(container);
    auto* desc = from_xbox_ptr<XbSurfaceDesc>(desc_address);
    const uint32_t format = format_of(pixels);
    desc->format = format;
    desc->type = d3d_resource_type(container);
    desc->usage = 0;
    if (level == 0) {
        if (format_flags(format) & 0x80)
            desc->usage = 1;  // D3DUSAGE_RENDERTARGET
        else if (format_flags(format) & 0x40)
            desc->usage = 2;  // D3DUSAGE_DEPTHSTENCIL
    }
    desc->multisample_type = kMultiSampleNone;
    const LevelLayout layout = level_layout(pixels, level);
    desc->size = layout.slice_size;
    desc->width = layout.width;
    desc->height = layout.height;
}

uint32_t XB_STDCALL D3DBaseTexture_GetLevelCount(XbPtr texture) {
    return (from_xbox_ptr<XbPixelContainer>(texture)->format >> kFormatMipmapShift) & 0xF;
}

uint32_t XB_STDCALL D3DTexture_GetLevelDesc(XbPtr texture, uint32_t level, XbPtr desc) {
    describe(texture, level, desc);
    return kD3DOk;
}

uint32_t XB_STDCALL D3DSurface_GetDesc(XbPtr surface, XbPtr desc) {
    describe(surface, 0, desc);
    return kD3DOk;
}

XbPtr XB_STDCALL D3DTexture_GetSurfaceLevel2(XbPtr texture, uint32_t level) {
    return to_xbox_ptr(
        create_level_surface(texture, surface_level(*from_xbox_ptr<XbPixelContainer>(texture), 0, level)));
}

XbPtr XB_STDCALL D3DCubeTexture_GetCubeMapSurface2(XbPtr texture, uint32_t face, uint32_t level) {
    return to_xbox_ptr(
        create_level_surface(texture, surface_level(*from_xbox_ptr<XbPixelContainer>(texture), face, level)));
}

// --- Locks ----------------------------------------------------------------------------------

struct XbLockedRect {  // D3DLOCKED_RECT
    int32_t pitch;
    XbPtr bits;
};

// D3DLOCK_TILED (0x40) asks for the 0xF0000000 view, through which the CPU sees a tiled
// surface in linear order. Nothing is tiled here, so the plain address is that view.
// (CoD BRO's movie player locks the back buffer this way every frame.)
void lock(XbPtr container, uint32_t face, uint32_t level, XbPtr locked_address, XbPtr rect_address,
          uint32_t /*flags*/) {
    const auto& pixels = *from_xbox_ptr<XbPixelContainer>(container);
    const SurfaceLevel surface = surface_level(pixels, face, level);
    uint32_t bits = surface.address;
    if (rect_address) {
        const auto* rect = from_xbox_ptr<XbRect>(rect_address);
        bits += rect->top * surface.pitch + bits_per_pixel(format_of(pixels)) * rect->left / 8;
    }
    auto* locked = from_xbox_ptr<XbLockedRect>(locked_address);
    locked->pitch = static_cast<int32_t>(surface.pitch);
    locked->bits = bits;
    texture_written(container);
}

void XB_STDCALL D3DTexture_LockRect(XbPtr texture, uint32_t level, XbPtr locked, XbPtr rect, uint32_t flags) {
    lock(texture, 0, level, locked, rect, flags);
}

void XB_STDCALL D3DSurface_LockRect(XbPtr surface, XbPtr locked, XbPtr rect, uint32_t flags) {
    lock(surface, 0, 0, locked, rect, flags);
}

// Vertex buffers and palettes: their memory, CPU address.
XbPtr XB_STDCALL D3DVertexBuffer_Lock2(XbPtr buffer, uint32_t /*flags*/) {
    return from_xbox_ptr<XbResource>(buffer)->data | 0x80000000;
}

XbPtr XB_STDCALL D3DPalette_Lock2(XbPtr palette, uint32_t /*flags*/) {
    texture_written(palette);
    return from_xbox_ptr<XbResource>(palette)->data | 0x80000000;
}

}  // namespace

bool delete_level_surface(XbPtr surface) {
    {
        std::lock_guard lock(g_surfaces_mutex);
        if (g_level_surfaces.erase(surface) == 0) return false;
    }
    delete from_xbox_ptr<XbSurface>(surface);
    return true;
}

const xbox::KernelImpl kTextureFunctions[] = {
    D3D_IMPL(D3DBaseTexture_GetLevelCount),
    D3D_IMPL(D3DCubeTexture_GetCubeMapSurface2),
    D3D_IMPL(D3DPalette_Lock2),
    D3D_IMPL(D3DSurface_GetDesc),
    D3D_IMPL(D3DSurface_LockRect),
    D3D_IMPL(D3DTexture_GetLevelDesc),
    D3D_IMPL(D3DTexture_GetSurfaceLevel2),
    D3D_IMPL(D3DTexture_LockRect),
    D3D_IMPL(D3DVertexBuffer_Lock2),
};
const size_t kTextureFunctionCount = std::size(kTextureFunctions);

}  // namespace render
