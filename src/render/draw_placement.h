#pragma once

// Where a GfxDevice draw lands on its target, shared by the back ends: the viewport and scissor in surface
// pixels (render scale) and the screen-to-clip constants (kGfxVsScreenToClip) for the title's screen-space
// vertex programs, with the PC's picture fixes (full-width draws reaching the picture's edges, UI narrowed
// for a wider picture). Back ends differ only in their clip conventions (`vulkan`).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "backend.h"
#include "gfx_device.h"

namespace render {

struct PlacementInput {
    uint32_t width = 0, height = 0;                  // the target in title pixels
    uint32_t surface_width = 0, surface_height = 0;  // and on the GPU
    Viewport viewport;                               // 0 width: the whole target
    const Rect* scissor = nullptr;                   // null: the whole target
    UiNarrowing narrow;                              // anchor < 0: none
    const float* range = nullptr;  // the draw's screen x min, max, y min, max (caller vertices), or null
    bool full_picture = false;     // the target is the back buffer (the picture shown)
    float depth_max = 16777215.0f;  // the depth buffer's largest value (D16: 65535)
    // Vulkan: clip y points down and pixel centres lie on .5; D3D9: y up, centres on integers.
    bool vulkan = false;
};

struct Placement {
    uint32_t x = 0, y = 0, width = 1, height = 1;  // viewport, surface pixels
    float min_z = 0.0f, max_z = 1.0f;
    int32_t left = 0, top = 0, right = 0, bottom = 0;  // scissor, surface pixels
    float screen_to_clip[8] = {};                      // c192, c193
};

inline Placement place_draw(const PlacementInput& in) {
    Placement out;
    const float width = static_cast<float>(in.width), height = static_cast<float>(in.height);
    const float surface_width = static_cast<float>(in.surface_width);
    const float surface_height = static_cast<float>(in.surface_height);
    const float sx = surface_width / width, sy = surface_height / height;
    Viewport v = in.viewport;
    if (!v.width || !v.height) {
        v.x = v.y = 0;
        v.width = in.width;
        v.height = in.height;
    }
    auto to_surface = [](float value, float scale, float limit) {
        return static_cast<uint32_t>(std::clamp(std::lround(value * scale), 0l, static_cast<long>(limit)));
    };
    out.x = to_surface(static_cast<float>(v.x), sx, surface_width);
    out.y = to_surface(static_cast<float>(v.y), sy, surface_height);
    out.width = std::max<uint32_t>(1, to_surface(static_cast<float>(v.x + v.width), sx, surface_width) - out.x);
    out.height = std::max<uint32_t>(1, to_surface(static_cast<float>(v.y + v.height), sy, surface_height) - out.y);
    out.min_z = v.min_z;
    out.max_z = v.max_z;
    out.right = static_cast<int32_t>(surface_width);
    out.bottom = static_cast<int32_t>(surface_height);
    if (in.scissor) {
        out.left = static_cast<int32_t>(to_surface(static_cast<float>(in.scissor->left), sx, surface_width));
        out.top = static_cast<int32_t>(to_surface(static_cast<float>(in.scissor->top), sy, surface_height));
        out.right = static_cast<int32_t>(to_surface(static_cast<float>(in.scissor->right), sx, surface_width));
        out.bottom = static_cast<int32_t>(to_surface(static_cast<float>(in.scissor->bottom), sy, surface_height));
    }
    const UiNarrowing& narrow = in.narrow;
    if (narrow.anchor >= 0.0f && !narrow.full_width_too) {  // a scope overlay fills beyond its sides
        const float at = surface_width * narrow.anchor;
        out.left = static_cast<int32_t>(std::lround(at + (out.left - at) * narrow.factor));
        out.right = static_cast<int32_t>(std::lround(at + (out.right - at) * narrow.factor));
    }

    // Screen space (title pixels, centres on integers) -> clip space of the viewport: the title's pixel x is
    // centred at x * s + (s - 1) / 2 on the surface (see D3D9Draw::draw).
    const float vx = static_cast<float>(out.x), vy = static_cast<float>(out.y);
    const float vw = static_cast<float>(out.width), vh = static_cast<float>(out.height);
    const float z_range = v.max_z > v.min_z ? v.max_z - v.min_z : 1.0f;
    float* r = out.screen_to_clip;
    r[0] = 2.0f * sx / vw;
    r[1] = -2.0f * sy / vh;
    r[2] = 1.0f / (in.depth_max * z_range);
    r[3] = 0.0f;
    r[4] = (sx - 1.0f - 2.0f * vx) / vw - 1.0f;
    r[5] = 1.0f - (sy - 1.0f - 2.0f * vy) / vh;
    r[6] = -v.min_z / z_range;
    r[7] = 0.0f;
    // In title pixels: x' = p x + q, then clip = x' * r[0] + r[4].
    float p = 1.0f, q = 0.0f;
    // Full-width draws into the back buffer (full-screen fades, letterbox bars, a scope overlay) whose edges
    // lie near the picture's are extended to them. The title's quads start at x/y = 0, half a title pixel
    // inside (pixel centres on integers): at 1x that still covers pixel 0, at a larger render scale it left
    // s/2 surface pixels of scene showing. Some bars (the pause menu's) also stop a few pixels short of the
    // bottom, which a TV's overscan hid. Edges within 3 title pixels go to the picture's (-0.5, and the
    // size), a stretch of a few pixels at most.
    float py = 1.0f, qy = 0.0f;
    const auto to_edges = [](float low, float high, float size, float& scale, float& offset) {
        if (!(high > low + 8.0f)) return;
        const float new_low = low > -0.5f && low <= 3.0f ? -0.5f : low;
        const float new_high = high >= size - 3.0f && high < size ? size : high;
        scale = (new_high - new_low) / (high - low);
        offset = new_low - low * scale;
    };
    const float* range = in.range;
    if (range && range[0] <= 3.0f && range[1] >= width - 3.0f && in.full_picture) {
        to_edges(range[0], range[1], width, p, q);
        to_edges(range[2], range[3], height, py, qy);
    }
    r[5] += qy * r[1];
    r[1] *= py;
    if (narrow.anchor >= 0.0f) {  // x'' = a + (x' - a) * k, a in title pixels
        const float a = narrow.anchor * width;
        p *= narrow.factor;
        q = a * (1.0f - narrow.factor) + q * narrow.factor;
    }
    r[4] += q * r[0];
    r[0] *= p;
    if (in.vulkan) {
        // The same window position: Vulkan's clip y = -D3D9's, and its pixel centres sit half a surface
        // pixel further on (D3D9 window x = Vulkan window x - 0.5).
        r[1] = -r[1];
        r[5] = -r[5] + 1.0f / vh;
        r[4] += 1.0f / vw;
    }
    return out;
}

// The x range (range[0..1]) and y range (range[2..3]; 1, 0 without a y) of input 0 (screen-space positions)
// of caller vertices; false if the layout has no float position there.
inline bool position_range(const VertexAttribute* attributes, uint32_t attribute_count, const void* vertices,
                           uint32_t count, uint32_t stride, float (&range)[4]) {
    if (!count) return false;
    for (uint32_t k = 0; k < attribute_count; ++k) {
        const VertexAttribute& a = attributes[k];
        if (a.input != 0 || a.stream != 0 || a.type < AttributeType::Float1 || a.type > AttributeType::Float4)
            continue;
        const auto* bytes = static_cast<const uint8_t*>(vertices) + a.offset;
        std::memcpy(&range[0], bytes, 4);
        range[1] = range[0];
        range[2] = 1.0f, range[3] = 0.0f;
        const bool has_y = a.type != AttributeType::Float1;
        if (has_y) std::memcpy(&range[2], bytes + 4, 4), range[3] = range[2];
        for (uint32_t i = 1; i < count; ++i) {
            float x;
            std::memcpy(&x, bytes + i * stride, 4);
            range[0] = std::min(range[0], x);
            range[1] = std::max(range[1], x);
            if (!has_y) continue;
            std::memcpy(&x, bytes + i * stride + 4, 4);
            range[2] = std::min(range[2], x);
            range[3] = std::max(range[3], x);
        }
        return true;
    }
    return false;
}

// UI laid out for a narrower shape than the picture's, drawn into the back buffer over a 3D scene: how it is
// narrowed (BackendConfig::narrow_ui_draw, as D3D9Draw::narrow_ui). `flat`: no 3D scene this frame.
inline UiNarrowing ui_narrowing(UiNarrowing (*narrow_ui_draw)(), const float* range, bool flat, bool full_picture,
                                uint32_t back_width) {
    if (!range || !narrow_ui_draw || flat || !full_picture) return {};
    const UiNarrowing n = narrow_ui_draw();
    if (n.anchor < 0.0f || n.factor >= 1.0f) return {};
    if (n.full_width_too) return n;
    // Full-width quads (dimming, letterbox bars) keep covering the picture.
    return range[0] <= 1.0f && range[1] >= static_cast<float>(back_width) - 1.0f ? UiNarrowing{} : n;
}

}  // namespace render
