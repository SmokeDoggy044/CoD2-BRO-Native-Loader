#pragma once

// Enhanced graphics' post passes on the finished picture (before the brightness pass and the
// window): anti-aliasing (FXAA), then sharpening and vibrance (render/post/*.hlsl).

#include <cstdint>

#include "d3d9_api.h"
#include "enhanced_graphics.h"

namespace render {

class D3D9Post {
public:
    explicit D3D9Post(IDirect3DDevice9* device) : device_(device) {}
    ~D3D9Post();
    D3D9Post(const D3D9Post&) = delete;
    D3D9Post& operator=(const D3D9Post&) = delete;

    // Applies the features that are on to `picture` (a render target texture; `surface` its level 0)
    // in place. Leaves the device's state as it found it.
    void apply(IDirect3DTexture9* picture, IDirect3DSurface9* surface, const GraphicsFeatures& features);

private:
    bool create();  // the shaders, declaration and state block, once; false if they cannot be made
    void pass(IDirect3DPixelShader9* shader, IDirect3DTexture9* source, IDirect3DSurface9* target, uint32_t width,
              uint32_t height, const float (&params)[4]);

    IDirect3DDevice9* device_;
    bool failed_ = false;
    IDirect3DVertexShader9* vs_ = nullptr;
    IDirect3DPixelShader9* fxaa_ = nullptr;
    IDirect3DPixelShader9* sharpen_ = nullptr;
    IDirect3DVertexDeclaration9* declaration_ = nullptr;
    IDirect3DStateBlock9* state_ = nullptr;
    IDirect3DTexture9* scratch_ = nullptr;  // the picture's size and format
    IDirect3DSurface9* scratch_surface_ = nullptr;
};

}  // namespace render
