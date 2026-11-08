#pragma once

// Text drawn over the presented frame (diagnostics), never into the title's frame or
// its screenshots. Text is rendered with SDL's debug font into a surface and drawn as
// one textured quad on the swap chain's back buffer.

#include <cstdint>
#include <string>
#include <vector>

#include "d3d9_api.h"

struct SDL_Surface;
struct SDL_Renderer;

namespace render {

class D3D9Overlay {
public:
    explicit D3D9Overlay(IDirect3DDevice9* device);
    ~D3D9Overlay();
    D3D9Overlay(const D3D9Overlay&) = delete;
    D3D9Overlay& operator=(const D3D9Overlay&) = delete;

    static constexpr int kCentre = -1;

    // Draws `lines` in a dark box at (x, y) on `target` (the swap chain's back buffer),
    // `scale` times the font's size, leaving the device state as it was. x = kCentre
    // centres the box horizontally in the first `area_width` pixels (0 = all).
    void draw(IDirect3DSurface9* target, const std::vector<std::string>& lines, int x, int y, int scale = 1,
              int area_width = 0);

private:
    bool render_text(const std::vector<std::string>& lines, uint32_t& width, uint32_t& height);

    IDirect3DDevice9* device_;
    IDirect3DStateBlock9* saved_ = nullptr;
    IDirect3DTexture9* texture_ = nullptr;
    uint32_t texture_width_ = 0, texture_height_ = 0;
    SDL_Surface* surface_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    std::vector<std::string> last_lines_;
    uint32_t last_width_ = 0, last_height_ = 0;
};

}  // namespace render
