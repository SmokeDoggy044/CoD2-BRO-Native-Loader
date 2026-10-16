// Diagnostics text over the presented frame. See d3d9_overlay.h.

#include "d3d9_overlay.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>

#include "log.h"

namespace render {

namespace {

constexpr uint32_t kSurfaceWidth = 640, kSurfaceHeight = 128;  // the most text drawn at once
constexpr int kChar = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;
constexpr int kPadding = 4, kLineHeight = kChar + 3;

struct Vertex {
    float x, y, z, rhw, u, v;
};
constexpr DWORD kFvf = D3DFVF_XYZRHW | D3DFVF_TEX1;

}  // namespace

D3D9Overlay::D3D9Overlay(IDirect3DDevice9* device) : device_(device) {
    surface_ = SDL_CreateSurface(kSurfaceWidth, kSurfaceHeight, SDL_PIXELFORMAT_ARGB8888);
    if (surface_) renderer_ = SDL_CreateSoftwareRenderer(surface_);
    if (!renderer_) LOG_WARN("render", "overlay text: %s", SDL_GetError());
    device_->CreateStateBlock(D3DSBT_ALL, &saved_);
}

D3D9Overlay::~D3D9Overlay() {
    if (saved_) saved_->Release();
    if (texture_) texture_->Release();
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (surface_) SDL_DestroySurface(surface_);
}

// Renders the lines into the texture (only when they changed); returns the box size.
bool D3D9Overlay::render_text(const std::vector<std::string>& lines, uint32_t& width, uint32_t& height) {
    if (!renderer_) return false;
    if (lines == last_lines_ && texture_) {
        width = last_width_;
        height = last_height_;
        return true;
    }
    size_t longest = 0;
    for (const std::string& line : lines) longest = std::max(longest, line.size());
    width = std::min<uint32_t>(kSurfaceWidth, static_cast<uint32_t>(longest * kChar + 2 * kPadding));
    height = std::min<uint32_t>(kSurfaceHeight, static_cast<uint32_t>(lines.size() * kLineHeight + 2 * kPadding - 3));

    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
    SDL_RenderClear(renderer_);
    const SDL_FRect box{0, 0, static_cast<float>(width), static_cast<float>(height)};
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 180);
    SDL_RenderFillRect(renderer_, &box);
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    for (size_t i = 0; i < lines.size(); ++i) {
        // First line yellow (a heading), the rest white.
        if (i == 0)
            SDL_SetRenderDrawColor(renderer_, 255, 220, 64, 255);
        else
            SDL_SetRenderDrawColor(renderer_, 255, 255, 255, 255);
        SDL_RenderDebugText(renderer_, kPadding, static_cast<float>(kPadding + i * kLineHeight), lines[i].c_str());
    }
    SDL_RenderPresent(renderer_);

    if (!texture_) {
        if (FAILED(device_->CreateTexture(kSurfaceWidth, kSurfaceHeight, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED,
                                          &texture_, nullptr)))
            return false;
        texture_width_ = kSurfaceWidth;
        texture_height_ = kSurfaceHeight;
    }
    D3DLOCKED_RECT locked;
    if (FAILED(texture_->LockRect(0, &locked, nullptr, 0))) return false;
    for (uint32_t y = 0; y < kSurfaceHeight; ++y)
        std::memcpy(static_cast<uint8_t*>(locked.pBits) + y * locked.Pitch,
                    static_cast<const uint8_t*>(surface_->pixels) + y * surface_->pitch, kSurfaceWidth * 4);
    texture_->UnlockRect(0);
    last_lines_ = lines;
    last_width_ = width;
    last_height_ = height;
    return true;
}

void D3D9Overlay::draw(IDirect3DSurface9* target, const std::vector<std::string>& lines, int x, int y, int scale) {
    uint32_t width = 0, height = 0;
    if (lines.empty() || !saved_ || !render_text(lines, width, height)) return;

    IDirect3DSurface9* old_target = nullptr;
    IDirect3DSurface9* old_depth = nullptr;
    device_->GetRenderTarget(0, &old_target);
    device_->GetDepthStencilSurface(&old_depth);
    saved_->Capture();

    device_->SetRenderTarget(0, target);
    device_->SetDepthStencilSurface(nullptr);
    device_->SetVertexShader(nullptr);
    device_->SetPixelShader(nullptr);
    device_->SetFVF(kFvf);
    device_->SetTexture(0, texture_);
    for (uint32_t stage = 1; stage < 4; ++stage) device_->SetTexture(stage, nullptr);
    device_->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device_->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    device_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device_->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    device_->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    device_->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    device_->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    device_->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    device_->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    device_->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device_->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    device_->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device_->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    device_->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    device_->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device_->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device_->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);

    D3DSURFACE_DESC desc;
    target->GetDesc(&desc);
    const D3DVIEWPORT9 viewport{0, 0, desc.Width, desc.Height, 0.0f, 1.0f};
    device_->SetViewport(&viewport);
    if (x == kCentre) x = (static_cast<int>(desc.Width) - static_cast<int>(width) * scale) / 2;
    const float x0 = static_cast<float>(x) - 0.5f, y0 = static_cast<float>(y) - 0.5f;
    const float x1 = x0 + width * scale, y1 = y0 + height * scale;
    const float u1 = static_cast<float>(width) / texture_width_, v1 = static_cast<float>(height) / texture_height_;
    const Vertex quad[4] = {
        {x0, y0, 0, 1, 0, 0}, {x1, y0, 0, 1, u1, 0}, {x0, y1, 0, 1, 0, v1}, {x1, y1, 0, 1, u1, v1}};
    device_->BeginScene();
    device_->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex));
    device_->EndScene();

    saved_->Apply();
    device_->SetRenderTarget(0, old_target);
    device_->SetDepthStencilSurface(old_depth);
    if (old_target) old_target->Release();
    if (old_depth) old_depth->Release();
}

}  // namespace render
