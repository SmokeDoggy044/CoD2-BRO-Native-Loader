#include "game_cursor.h"

#include <SDL3/SDL_surface.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace render {

namespace {

struct Point {
    float x, y;
};

// The arrow in units of its height (21): tip at (0, 0), the classic pointer's shape.
constexpr std::array<Point, 7> kArrow = {{{0, 0}, {0, 17}, {4.2f, 13.2f}, {7, 20}, {10, 18.8f}, {7.3f, 12.2f}, {12.5f, 12.2f}}};
constexpr float kUnits = 21.0f;

bool inside(const std::array<Point, 7>& polygon, float x, float y) {
    bool in = false;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const Point& a = polygon[i];
        const Point& b = polygon[j];
        if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}

float edge_distance(const std::array<Point, 7>& polygon, float x, float y) {
    float best = 1e9f;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const Point& a = polygon[j];
        const Point& b = polygon[i];
        const float dx = b.x - a.x, dy = b.y - a.y;
        const float t = std::clamp(((x - a.x) * dx + (y - a.y) * dy) / (dx * dx + dy * dy), 0.0f, 1.0f);
        best = std::min(best, std::hypot(x - (a.x + t * dx), y - (a.y + t * dy)));
    }
    return best;
}

}  // namespace

SDL_Cursor* make_game_cursor(int height) {
    height = std::clamp(height, 16, 128);
    const float scale = static_cast<float>(height) / kUnits;
    const float outline = std::max(1.0f, scale * 1.1f);   // pixels
    const float shadow_offset = std::max(1.0f, scale * 1.2f);
    const int size = height + static_cast<int>(std::ceil(shadow_offset + outline)) + 2;
    const float pad = outline;  // the tip's outline needs room left of and above the tip
    SDL_Surface* surface = SDL_CreateSurface(size, size, SDL_PIXELFORMAT_RGBA32);
    if (!surface) return nullptr;
    // The menus' paper and ink.
    constexpr float kPaper[3] = {236, 226, 200}, kInk[3] = {30, 22, 16};
    constexpr int kSamples = 4;
    auto* pixels = static_cast<uint8_t*>(surface->pixels);
    for (int py = 0; py < size; ++py) {
        for (int px = 0; px < size; ++px) {
            float fill = 0, ink = 0, shade = 0;
            for (int sy = 0; sy < kSamples; ++sy) {
                for (int sx = 0; sx < kSamples; ++sx) {
                    const float x = (px + (sx + 0.5f) / kSamples - pad) / scale;
                    const float y = (py + (sy + 0.5f) / kSamples - pad) / scale;
                    const bool in = inside(kArrow, x, y);
                    const float d = edge_distance(kArrow, x, y) * scale;  // pixels from the outline
                    if (in && d > outline * 0.5f)
                        fill += 1;
                    else if (in || d <= outline * 0.5f)
                        ink += 1;
                    else if (inside(kArrow, x - shadow_offset / scale, y - shadow_offset / scale))
                        shade += 1;
                }
            }
            const float n = kSamples * kSamples;
            fill /= n, ink /= n, shade /= n;
            const float alpha = fill + ink + shade * 0.35f;
            uint8_t* out = pixels + py * surface->pitch + px * 4;
            for (int c = 0; c < 3; ++c) {
                const float colour = alpha > 0 ? (fill * kPaper[c] + ink * kInk[c]) / alpha : 0;  // the shadow is black
                out[c] = static_cast<uint8_t>(std::clamp(colour, 0.0f, 255.0f));
            }
            out[3] = static_cast<uint8_t>(std::clamp(alpha * 255.0f, 0.0f, 255.0f));
        }
    }
    const int hot = static_cast<int>(std::lround(pad));
    SDL_Cursor* cursor = SDL_CreateColorCursor(surface, hot, hot);
    SDL_DestroySurface(surface);
    return cursor;
}

}  // namespace render
