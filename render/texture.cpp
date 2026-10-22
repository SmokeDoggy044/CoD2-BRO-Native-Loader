// Xbox Direct3D 8 replacement: texture and surface layout, descriptions and locks.
// See xbox_d3d_api.h.
//
// Layout rules (XDK 5849): surface_layout.h (shared with the compare harness). Data is
// the physical address; the CPU address is Data | 0x80000000.

#include <algorithm>
#include <cstring>
#include <mutex>
#include <set>

#include "compiler.h"
#include "d3d_internal.h"
#include "log.h"
#include "surface_layout.h"
#include "xbox_d3d.h"

namespace render {

namespace {

using xbox::from_xbox_ptr;
using xbox::to_xbox_ptr;

using surface_layout::bits_per_pixel;
using surface_layout::format_flags;
using surface_layout::is_compressed;
using surface_layout::SurfaceLevel;

uint32_t format_of(const XbPixelContainer& pixels) { return surface_layout::format_of(pixels.format); }

// Where a face/level of a texture lives (D3D's internal helper at 003ee960).
SurfaceLevel surface_level(const XbPixelContainer& pixels, uint32_t face, uint32_t level) {
    return surface_layout::surface_level(pixels.resource.data, pixels.format, pixels.size, face, level);
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

// D3DSURFACE_DESC (7 dwords) of a level: surface_layout.h (shared with the compare harness).
void describe(XbPtr container, uint32_t level, XbPtr desc_address) {
    const auto& pixels = *from_xbox_ptr<XbPixelContainer>(container);
    surface_layout::describe_level(pixels.resource.common, pixels.format, pixels.size, level,
                                   from_xbox_ptr<uint32_t>(desc_address));
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
