// Xbox Direct3D 8 replacement: resource bookkeeping (D3DResource_*). See xbox_d3d_api.h.
//
// Resources live in title memory. The title creates most of them itself (XGSet*Header
// fills the struct, Register points Data at its memory); the reference count is the
// low 16 bits of Common. Surfaces of a texture (Parent set, own count 0) forward their
// references to the texture.

#include "compiler.h"
#include "d3d_internal.h"
#include "xbox_d3d.h"

namespace render {

namespace {

using xbox::from_xbox_ptr;

constexpr uint32_t kCommonInternalRefMask = 0x00780000;  // references held by the GPU / D3D

XbPtr surface_parent(const XbResource* resource) {
    if ((resource->common & kCommonTypeMask) != kCommonTypeSurface) return 0;
    return reinterpret_cast<const XbSurface*>(resource)->parent;
}

uint32_t XB_STDCALL D3DResource_AddRef(XbPtr resource_address) {
    auto* resource = from_xbox_ptr<XbResource>(resource_address);
    if ((resource->common & kCommonRefcountMask) == 0)
        if (XbPtr parent = surface_parent(resource)) D3DResource_AddRef(parent);
    return ++resource->common & kCommonRefcountMask;
}

uint32_t XB_STDCALL D3DResource_Release(XbPtr resource_address) {
    auto* resource = from_xbox_ptr<XbResource>(resource_address);
    if ((resource->common & kCommonRefcountMask) == 1) {
        if (XbPtr parent = surface_parent(resource)) D3DResource_Release(parent);
        if ((resource->common & kCommonInternalRefMask) == 0) {
            // Destroyed. Title-created resources' memory belongs to the title; of
            // ours, only texture level surfaces are freed (the back and depth
            // buffers keep D3D's own reference).
            resource->common &= ~kCommonRefcountMask;
            resource_destroyed(resource_address);
            delete_level_surface(resource_address);
            return 0;
        }
    }
    return --resource->common & kCommonRefcountMask;
}

// D3DRESOURCETYPE: 1 surface, 2 volume, 3 texture, 4 volume texture, 5 cube texture,
// 6 vertex buffer, 7 index buffer, 8 push buffer, 9 palette, 10 fixup.
uint32_t XB_STDCALL D3DResource_GetType(XbPtr resource_address) { return d3d_resource_type(resource_address); }

}  // namespace

uint32_t d3d_resource_type(XbPtr resource_address) {
    const auto* resource = from_xbox_ptr<XbResource>(resource_address);
    const auto* pixels = from_xbox_ptr<XbPixelContainer>(resource_address);
    const bool volume = (pixels->format & 0xF0) > (2u << kFormatDimensionShift);
    switch (resource->common & kCommonTypeMask) {
    case kCommonTypeVertexBuffer: return 6;
    case kCommonTypeIndexBuffer: return 7;
    case kCommonTypePushBuffer: return 8;
    case kCommonTypePalette: return 9;
    case kCommonTypeTexture: return (pixels->format & kFormatCubemap) ? 5 : volume ? 4 : 3;
    case kCommonTypeSurface: return volume ? 2 : 1;
    default: return 10;
    }
}

namespace {

// Points the resource at its memory: Data += base, as a physical address (the low 28
// bits) except for push buffers, which keep the CPU address.
void XB_STDCALL D3DResource_Register(XbPtr resource_address, XbPtr base) {
    auto* resource = from_xbox_ptr<XbResource>(resource_address);
    uint32_t data = resource->data + base;
    if ((resource->common & kCommonTypeMask) != kCommonTypePushBuffer) data &= 0x0FFFFFFF;
    resource->data = data;
}

// Everything is executed synchronously, so the GPU is never using a resource.
void XB_STDCALL D3DResource_BlockUntilNotBusy(XbPtr /*resource*/) {}
void XB_FASTCALL D3D_BlockOnResource_0(XbPtr /*resource*/) {}

}  // namespace

const xbox::KernelImpl kResourceFunctions[] = {
    D3D_IMPL(D3D_BlockOnResource_0),
    D3D_IMPL(D3DResource_AddRef),
    D3D_IMPL(D3DResource_BlockUntilNotBusy),
    D3D_IMPL(D3DResource_GetType),
    D3D_IMPL(D3DResource_Register),
    D3D_IMPL(D3DResource_Release),
};
const size_t kResourceFunctionCount = std::size(kResourceFunctions);

}  // namespace render
