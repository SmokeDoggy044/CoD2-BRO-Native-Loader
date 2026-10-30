#include "d3d9_post.h"

#include "float_env.h"
#include "log.h"
#include "post_shaders.h"

namespace render {
namespace {

template <typename T>
void release(T*& object) {
    if (object) object->Release();
    object = nullptr;
}

}  // namespace

D3D9Post::~D3D9Post() {
    release(scratch_surface_);
    release(scratch_);
    release(state_);
    release(declaration_);
    release(sharpen_);
    release(fxaa_);
    release(vs_);
}

bool D3D9Post::create() {
    if (vs_) return true;
    if (failed_) return false;
    const NamedShader* vs = post_shader("post_vs");
    const NamedShader* fxaa = post_shader("fxaa");
    const NamedShader* sharpen = post_shader("sharpen");
    static const D3DVERTEXELEMENT9 kElements[] = {
        {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        {0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 1},
        D3DDECL_END(),
    };
    if (!vs || !fxaa || !sharpen ||
        FAILED(device_->CreateVertexShader(reinterpret_cast<const DWORD*>(vs->d3d9), &vs_)) ||
        FAILED(device_->CreatePixelShader(reinterpret_cast<const DWORD*>(fxaa->d3d9), &fxaa_)) ||
        FAILED(device_->CreatePixelShader(reinterpret_cast<const DWORD*>(sharpen->d3d9), &sharpen_)) ||
        FAILED(device_->CreateVertexDeclaration(kElements, &declaration_)) ||
        FAILED(device_->CreateStateBlock(D3DSBT_ALL, &state_))) {
        LOG_WARN("render", "enhanced graphics: cannot create the post passes; the picture stays as it is");
        release(vs_);
        failed_ = true;
        return false;
    }
    return true;
}

void D3D9Post::pass(IDirect3DPixelShader9* shader, IDirect3DTexture9* source, IDirect3DSurface9* target,
                    uint32_t width, uint32_t height, const float (&params)[4]) {
    device_->SetRenderTarget(0, target);
    const D3DVIEWPORT9 viewport{0, 0, width, height, 0.0f, 1.0f};
    device_->SetViewport(&viewport);
    device_->SetPixelShader(shader);
    device_->SetTexture(0, source);
    device_->SetPixelShaderConstantF(0, params, 1);
    const float half_pixel[4] = {-1.0f / width, 1.0f / height, 0.0f, 0.0f};
    device_->SetVertexShaderConstantF(0, half_pixel, 1);
    struct Vertex {
        float x, y, z, w, u, v;
    };
    const Vertex quad[4] = {{-1, 1, 0, 1, 0, 0}, {1, 1, 0, 1, 1, 0}, {-1, -1, 0, 1, 0, 1}, {1, -1, 0, 1, 1, 1}};
    device_->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex));
}

void D3D9Post::apply(IDirect3DTexture9* picture, IDirect3DSurface9* surface, const GraphicsFeatures& f) {
    const bool fxaa = f.fxaa;
    const bool sharpen = f.sharpen && f.sharpness > 0.0f;
    const bool vibrance = f.vibrance && f.vibrance_amount != 0.0f;
    if (!picture || !surface || (!fxaa && !sharpen && !vibrance) || !create()) return;
    platform::HostFloatEnv float_env;
    D3DSURFACE_DESC desc{};
    surface->GetDesc(&desc);
    if (scratch_) {
        D3DSURFACE_DESC have{};
        scratch_surface_->GetDesc(&have);
        if (have.Width != desc.Width || have.Height != desc.Height || have.Format != desc.Format) {
            release(scratch_surface_);
            release(scratch_);
        }
    }
    if (!scratch_ && (FAILED(device_->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET, desc.Format,
                                                    D3DPOOL_DEFAULT, &scratch_, nullptr)) ||
                      FAILED(scratch_->GetSurfaceLevel(0, &scratch_surface_)))) {
        release(scratch_);
        return;
    }

    IDirect3DSurface9* old_target = nullptr;
    IDirect3DSurface9* old_depth = nullptr;
    device_->GetRenderTarget(0, &old_target);
    device_->GetDepthStencilSurface(&old_depth);
    state_->Capture();
    device_->SetDepthStencilSurface(nullptr);
    device_->SetVertexShader(vs_);
    device_->SetVertexDeclaration(declaration_);
    device_->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    device_->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    device_->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    device_->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    device_->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    device_->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE);
    device_->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    device_->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device_->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device_->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device_->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device_->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    device_->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    device_->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    device_->SetRenderState(D3DRS_FOGENABLE, FALSE);
    for (uint32_t s = 1; s < 4; ++s) device_->SetTexture(s, nullptr);

    const float texel_x = 1.0f / desc.Width, texel_y = 1.0f / desc.Height;
    device_->BeginScene();
    // FXAA into the scratch target; sharpening and vibrance from wherever the picture is now into
    // the other one; the result copied back into the picture if it ended in the scratch target.
    bool in_scratch = false;
    if (fxaa) {
        pass(fxaa_, picture, scratch_surface_, desc.Width, desc.Height, {texel_x, texel_y, 0, 0});
        in_scratch = true;
    }
    if (sharpen || vibrance) {
        pass(sharpen_, in_scratch ? scratch_ : picture, in_scratch ? surface : scratch_surface_, desc.Width,
             desc.Height, {texel_x, texel_y, sharpen ? f.sharpness : 0.0f, vibrance ? f.vibrance_amount : 0.0f});
        in_scratch = !in_scratch;
    }
    device_->EndScene();
    if (in_scratch) device_->StretchRect(scratch_surface_, nullptr, surface, nullptr, D3DTEXF_NONE);

    state_->Apply();
    device_->SetRenderTarget(0, old_target);
    device_->SetDepthStencilSurface(old_depth);
    release(old_target);
    release(old_depth);
}

}  // namespace render
