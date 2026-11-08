// See key_prompts.h.

#include "key_prompts.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#include "kapf.h"
#include "log.h"
#include "texture_hash.h"

namespace text {

namespace {

namespace fs = std::filesystem;

constexpr const char* kPacks[] = {"sp/global.cod", "mp/global.cod"};
constexpr const char* kIconFonts[] = {"i_button_icons_xb_en", "i_button_icons_xb_fr", "i_button_icons_xb_it",
                                      "i_button_icons_xb_sp"};
constexpr std::pair<const char*, PromptIcon> kFooterIcons[] = {
    {"Xbox_AButton.dds", PromptIcon::A}, {"Xbox_BButton.dds", PromptIcon::B},
    {"Xbox_XButton.dds", PromptIcon::X}, {"Xbox_YButton.dds", PromptIcon::Y}};
constexpr const char* kLabelFont = "Jost-500-Medium.ttf";
constexpr int kFirstIconCode = 33;  // '!' = the right trigger, then PromptIcon's order

struct Box {
    PromptIcon icon;
    int x, y, width, height;  // in the original texture
};

// One icon texture: its size and where each icon is in it.
struct IconTexture {
    std::string name;
    uint32_t width = 0, height = 0;
    std::vector<Box> boxes;
};

// Written by prepare_key_prompts only, before the renderer asks.
std::deque<IconTexture> g_textures;  // stable addresses for g_by_content
std::unordered_map<uint64_t, const IconTexture*> g_by_content;
std::vector<uint8_t> g_ttf;
stbtt_fontinfo g_font;
bool g_have_font = false;

std::mutex g_mutex;
std::optional<KeyLabels> g_labels;  // guarded by g_mutex
std::map<std::pair<const IconTexture*, int>, std::shared_ptr<const render::TextureReplacement>> g_cache;
std::atomic<uint32_t> g_version{1};

std::vector<uint8_t> read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return in ? std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {}) : std::vector<uint8_t>();
}

// The glyph boxes of an icon font's table (" 33 x 7 y 10 w 30 gx 3 gy 8 gw 26 gh 26").
std::vector<Box> parse_icon_table(const std::vector<uint8_t>& data) {
    std::vector<Box> boxes;
    const std::string table(data.begin(), data.end());
    size_t start = 0;
    while (start < table.size()) {
        size_t end = table.find('\n', start);
        if (end == std::string::npos) end = table.size();
        const std::string line = table.substr(start, end - start);
        start = end + 1;
        int code, x, y, advance, gx, gy, gw, gh;
        if (std::sscanf(line.c_str(), " %d x %d y %d w %d gx %d gy %d gw %d gh %d", &code, &x, &y, &advance, &gx, &gy,
                        &gw, &gh) != 8)
            continue;
        const int index = code - kFirstIconCode;
        if (index < 0 || index >= static_cast<int>(PromptIcon::Count) || gw <= 0 || gh <= 0) continue;
        boxes.push_back({static_cast<PromptIcon>(index), x, y, gw, gh});
    }
    return boxes;
}

// --- Drawing -------------------------------------------------------------------------
// Straight-alpha RGBA floats; shapes are signed distance fields in output pixels,
// anti-aliased over one pixel.

struct Color {
    float r, g, b, a;
};

class Canvas {
public:
    Canvas(uint32_t width, uint32_t height) : width_(width), height_(height), pixels_(size_t{width} * height) {}

    // `coverage` (0-1) of `color` over what is there.
    void blend(int x, int y, Color color, float coverage) {
        if (x < 0 || y < 0 || x >= static_cast<int>(width_) || y >= static_cast<int>(height_)) return;
        const float a = color.a * std::clamp(coverage, 0.0f, 1.0f);
        if (a <= 0.0f) return;
        Color& d = pixels_[static_cast<size_t>(y) * width_ + x];
        const float out = a + d.a * (1.0f - a);
        d.r = (color.r * a + d.r * d.a * (1.0f - a)) / out;
        d.g = (color.g * a + d.g * d.a * (1.0f - a)) / out;
        d.b = (color.b * a + d.b * d.a * (1.0f - a)) / out;
        d.a = out;
    }

    // Fills where `distance(px, py)` (pixel centres) is negative.
    template <typename Distance>
    void fill(float left, float top, float right, float bottom, Color color, Distance distance) {
        for (int y = std::max(0, static_cast<int>(top) - 1); y < std::min<int>(height_, static_cast<int>(bottom) + 2); ++y)
            for (int x = std::max(0, static_cast<int>(left) - 1); x < std::min<int>(width_, static_cast<int>(right) + 2);
                 ++x)
                blend(x, y, color, 0.5f - distance(x + 0.5f, y + 0.5f));
    }

    std::shared_ptr<render::TextureReplacement> finish() const {
        auto replacement = std::make_shared<render::TextureReplacement>();
        replacement->width = width_;
        replacement->height = height_;
        replacement->pixels.resize(pixels_.size());
        for (size_t i = 0; i < pixels_.size(); ++i) {
            const Color& c = pixels_[i];
            // Transparent texels white, so filtering does not darken the edges.
            const auto channel = [&](float v) {
                return static_cast<uint32_t>(std::lround(std::clamp(c.a > 0.0f ? v : 1.0f, 0.0f, 1.0f) * 255.0f));
            };
            replacement->pixels[i] = channel(c.a > 0.0f ? c.a : 0.0f) << 24 | channel(c.r) << 16 | channel(c.g) << 8 |
                                     channel(c.b);
            if (c.a <= 0.0f) replacement->pixels[i] &= 0x00FFFFFF;
        }
        return replacement;
    }

private:
    uint32_t width_, height_;
    std::vector<Color> pixels_;
};

float rounded_rect(float px, float py, float cx, float cy, float half_w, float half_h, float radius) {
    const float qx = std::abs(px - cx) - half_w + radius, qy = std::abs(py - cy) - half_h + radius;
    return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - radius;
}

// A triangle pointing in direction (dx, dy) (unit, axis-aligned), centred on (cx, cy).
void arrow(Canvas& canvas, float cx, float cy, float size, int dx, int dy, Color color) {
    // Tip and the two back corners.
    const float tx = cx + dx * size * 0.5f, ty = cy + dy * size * 0.5f;
    const float bx = cx - dx * size * 0.4f, by = cy - dy * size * 0.4f;
    const float ax = bx - dy * size * 0.5f, ay = by + dx * size * 0.5f;
    const float ex = bx + dy * size * 0.5f, ey = by - dx * size * 0.5f;
    const float xs[3] = {tx, ax, ex}, ys[3] = {ty, ay, ey};
    canvas.fill(cx - size, cy - size, cx + size, cy + size, color, [&](float px, float py) {
        float d = -1e9f;
        for (int i = 0; i < 3; ++i) {  // the largest signed distance to the three edges
            const float x0 = xs[i], y0 = ys[i], x1 = xs[(i + 1) % 3], y1 = ys[(i + 1) % 3];
            const float nx = y1 - y0, ny = x0 - x1, length = std::hypot(nx, ny);
            // Edges wind so that the outside is on this normal's side for one winding;
            // take the absolute orientation from the centre.
            float side = ((px - x0) * nx + (py - y0) * ny) / length;
            const float centre = ((cx - x0) * nx + (cy - y0) * ny) / length;
            if (centre > 0) side = -side;
            d = std::max(d, side);
        }
        return d;
    });
}

void label_text(Canvas& canvas, float cx, float cy, float max_w, float max_h, const std::string& text, Color color) {
    if (!g_have_font || text.empty()) return;
    // The size that fits: the cap height fills max_h, unless the text is too wide.
    float pixel_height = max_h * 1.35f;
    float scale = stbtt_ScaleForPixelHeight(&g_font, pixel_height);
    const auto measure = [&](float s, int& x0, int& y0, int& x1, int& y1) {
        float pen = 0;
        x0 = y0 = 1 << 20;
        x1 = y1 = -(1 << 20);
        for (size_t i = 0; i < text.size(); ++i) {
            int bx0, by0, bx1, by1, advance, bearing;
            stbtt_GetCodepointBitmapBoxSubpixel(&g_font, static_cast<unsigned char>(text[i]), s, s, pen - std::floor(pen),
                                                0, &bx0, &by0, &bx1, &by1);
            x0 = std::min(x0, static_cast<int>(std::floor(pen)) + bx0);
            x1 = std::max(x1, static_cast<int>(std::floor(pen)) + bx1);
            y0 = std::min(y0, by0);
            y1 = std::max(y1, by1);
            stbtt_GetCodepointHMetrics(&g_font, static_cast<unsigned char>(text[i]), &advance, &bearing);
            pen += advance * s;
            if (i + 1 < text.size())
                pen += s * stbtt_GetCodepointKernAdvance(&g_font, static_cast<unsigned char>(text[i]),
                                                         static_cast<unsigned char>(text[i + 1]));
        }
    };
    int x0, y0, x1, y1;
    measure(scale, x0, y0, x1, y1);
    const float fit = std::min(max_w / std::max(1, x1 - x0), max_h / std::max(1, y1 - y0));
    scale *= fit;
    measure(scale, x0, y0, x1, y1);
    const float origin_x = cx - (x0 + x1) / 2.0f, origin_y = cy - (y0 + y1) / 2.0f;
    float pen = origin_x;
    std::vector<uint8_t> bitmap;
    for (size_t i = 0; i < text.size(); ++i) {
        const int code = static_cast<unsigned char>(text[i]);
        const float shift_x = pen - std::floor(pen), shift_y = origin_y - std::floor(origin_y);
        int bx0, by0, bx1, by1;
        stbtt_GetCodepointBitmapBoxSubpixel(&g_font, code, scale, scale, shift_x, shift_y, &bx0, &by0, &bx1, &by1);
        const int w = bx1 - bx0, h = by1 - by0;
        if (w > 0 && h > 0) {
            bitmap.assign(static_cast<size_t>(w) * h, 0);
            stbtt_MakeCodepointBitmapSubpixel(&g_font, bitmap.data(), w, h, w, scale, scale, shift_x, shift_y, code);
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    canvas.blend(static_cast<int>(std::floor(pen)) + bx0 + x, static_cast<int>(std::floor(origin_y)) + by0 + y,
                                 color, bitmap[static_cast<size_t>(y) * w + x] / 255.0f);
        }
        int advance, bearing;
        stbtt_GetCodepointHMetrics(&g_font, code, &advance, &bearing);
        pen += advance * scale;
        if (i + 1 < text.size())
            pen += scale * stbtt_GetCodepointKernAdvance(&g_font, code, static_cast<unsigned char>(text[i + 1]));
    }
}

constexpr Color kInk{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Color kOutline{0.86f, 0.86f, 0.86f, 1.0f};
constexpr Color kCapTop{0.24f, 0.24f, 0.26f, 1.0f};
constexpr Color kCapBottom{0.10f, 0.10f, 0.11f, 1.0f};

// A mouse with one button lit (0 left, 1 right, 2 middle).
void draw_mouse(Canvas& canvas, float left, float top, float w, float h, int button) {
    const float size = std::min(w, h);
    const float body_w = size * 0.62f, body_h = size * 0.92f;
    const float cx = left + w / 2, cy = top + h / 2;
    const float line = std::max(1.0f, size * 0.07f);
    const float hw = body_w / 2, hh = body_h / 2, radius = hw * 0.95f;
    canvas.fill(cx - hw, cy - hh, cx + hw, cy + hh, kCapBottom,
                [&](float px, float py) { return rounded_rect(px, py, cx, cy, hw, hh, radius); });
    const float split_y = cy - hh * 0.15f;  // where the buttons end
    const Color lit{0.95f, 0.75f, 0.25f, 1.0f};
    canvas.fill(cx - hw, cy - hh, cx + hw, split_y, lit, [&](float px, float py) {
        const float inside = rounded_rect(px, py, cx, cy, hw - line, hh - line, radius - line);
        float d = std::max(inside, py - split_y);
        if (button == 0) d = std::max(d, px - cx);
        if (button == 1) d = std::max(d, cx - px);
        if (button == 2) d = std::max(d, std::abs(px - cx) - size * 0.06f);
        return d;
    });
    // Outline, the line between the buttons and the one under them.
    canvas.fill(cx - hw, cy - hh, cx + hw, cy + hh, kOutline, [&](float px, float py) {
        const float body = std::abs(rounded_rect(px, py, cx, cy, hw - line / 2, hh - line / 2, radius)) - line / 2;
        const float inside = rounded_rect(px, py, cx, cy, hw - line / 2, hh - line / 2, radius);
        const float middle = std::max(std::abs(px - cx) - line * 0.35f, std::max(py - split_y, inside));
        const float under = std::max(std::abs(py - split_y) - line * 0.35f, inside);
        return std::min(body, std::min(middle, under));
    });
}

void draw_keycap(Canvas& canvas, float left, float top, float w, float h, const std::string& label) {
    if (label == "@lmb" || label == "@rmb" || label == "@mmb") {
        draw_mouse(canvas, left, top, w, h, label == "@lmb" ? 0 : label == "@rmb" ? 1 : 2);
        return;
    }
    const float size = std::min(w, h);
    const float margin = size * 0.05f, line = std::max(1.0f, size * 0.06f);
    const float cx = left + w / 2, cy = top + h / 2, hw = w / 2 - margin, hh = h / 2 - margin;
    const float radius = size * 0.2f;
    for (int y = static_cast<int>(cy - hh) - 1; y <= static_cast<int>(cy + hh) + 1; ++y) {  // the cap, top lit
        const float t = std::clamp((y + 0.5f - (cy - hh)) / (2 * hh), 0.0f, 1.0f);
        const Color c{kCapTop.r + (kCapBottom.r - kCapTop.r) * t, kCapTop.g + (kCapBottom.g - kCapTop.g) * t,
                      kCapTop.b + (kCapBottom.b - kCapTop.b) * t, 1.0f};
        canvas.fill(cx - hw, static_cast<float>(y), cx + hw, static_cast<float>(y), c, [&](float px, float py) {
            return std::max(rounded_rect(px, py, cx, cy, hw, hh, radius), std::abs(py - (y + 0.5f)) - 0.5f);
        });
    }
    canvas.fill(cx - hw, cy - hh, cx + hw, cy + hh, kOutline, [&](float px, float py) {
        return std::abs(rounded_rect(px, py, cx, cy, hw - line / 2, hh - line / 2, radius - line / 2)) - line / 2;
    });
    const float inner_w = 2 * (hw - line) * 0.88f, inner_h = 2 * (hh - line);
    const float a = size * 0.42f;  // arrow size
    if (label == "@up" || label == "@down" || label == "@left" || label == "@right") {
        const int dx = label == "@left" ? -1 : label == "@right" ? 1 : 0, dy = label == "@up" ? -1 : label == "@down" ? 1 : 0;
        arrow(canvas, cx, cy, a, dx, dy, kInk);
    } else if (label == "@leftright") {
        arrow(canvas, cx - a * 0.45f, cy, a * 0.7f, -1, 0, kInk);
        arrow(canvas, cx + a * 0.45f, cy, a * 0.7f, 1, 0, kInk);
    } else if (label == "@updown") {
        arrow(canvas, cx, cy - a * 0.45f, a * 0.7f, 0, -1, kInk);
        arrow(canvas, cx, cy + a * 0.45f, a * 0.7f, 0, 1, kInk);
    } else if (label == "@arrows") {
        const float s = a * 0.5f, o = a * 0.42f;
        arrow(canvas, cx, cy - o, s, 0, -1, kInk);
        arrow(canvas, cx, cy + o, s, 0, 1, kInk);
        arrow(canvas, cx - o, cy, s, -1, 0, kInk);
        arrow(canvas, cx + o, cy, s, 1, 0, kInk);
    } else {
        // One letter at about half the cap's height; longer names as wide as fits.
        const float text_h = label.size() == 1 ? inner_h * 0.5f : inner_h * 0.42f;
        label_text(canvas, cx, cy, inner_w, text_h, label, kInk);
    }
}

std::shared_ptr<const render::TextureReplacement> draw(const IconTexture& texture, const KeyLabels& labels, int scale) {
    Canvas canvas(texture.width * scale, texture.height * scale);
    for (const Box& box : texture.boxes)
        draw_keycap(canvas, static_cast<float>(box.x * scale), static_cast<float>(box.y * scale),
                    static_cast<float>(box.width * scale), static_cast<float>(box.height * scale),
                    labels[static_cast<size_t>(box.icon)]);
    return canvas.finish();
}

}  // namespace

size_t prepare_key_prompts(const fs::path& assets, const fs::path& fonts_dir) {
    g_ttf = read_file(fonts_dir / kLabelFont);
    g_have_font = !g_ttf.empty() && stbtt_InitFont(&g_font, g_ttf.data(), stbtt_GetFontOffsetForIndex(g_ttf.data(), 0));
    if (!g_have_font) LOG_WARN("text", "%s is missing; key prompts show no key names", (fonts_dir / kLabelFont).string().c_str());

    for (const char* pack_name : kPacks) {
        const auto pack = KapfPack::open(assets / pack_name);
        if (!pack) continue;
        const auto add = [&](const std::string& name, const std::vector<uint8_t>& dds, std::vector<Box> boxes) {
            if (dds.size() <= 128 || std::memcmp(dds.data(), "DDS ", 4) != 0 || boxes.empty()) return;
            // In memory the title holds what follows the DDS header.
            const uint64_t content = render::hash_bytes(dds.data() + 128, dds.size() - 128);
            if (g_by_content.count(content)) return;
            IconTexture texture;
            texture.name = name;
            std::memcpy(&texture.height, dds.data() + 12, 4);
            std::memcpy(&texture.width, dds.data() + 16, 4);
            if (!texture.width || !texture.height || texture.width > 1024 || texture.height > 1024) return;
            texture.boxes = std::move(boxes);
            g_textures.push_back(std::move(texture));
            g_by_content[content] = &g_textures.back();
        };
        for (const char* font : kIconFonts) {
            const KapfPack::Entry* dds = pack->find(std::string(font) + ".dds");
            const KapfPack::Entry* fdf = pack->find(std::string(font) + ".fdf");
            if (!dds || !fdf) continue;
            const auto dds_data = pack->read(*dds);
            const auto fdf_data = pack->read(*fdf);
            if (dds_data && fdf_data) add(font, *dds_data, parse_icon_table(*fdf_data));
        }
        for (const auto& [name, icon] : kFooterIcons) {
            const KapfPack::Entry* dds = pack->find(name);
            if (!dds) continue;
            if (const auto data = pack->read(*dds)) {
                uint32_t height = 0, width = 0;
                if (data->size() > 20) std::memcpy(&height, data->data() + 12, 4), std::memcpy(&width, data->data() + 16, 4);
                add(name, *data, {{icon, 0, 0, static_cast<int>(width), static_cast<int>(height)}});
            }
        }
    }
    LOG_INFO("text", "key prompts: %zu icon textures", g_textures.size());
    return g_textures.size();
}

void set_key_labels(const std::optional<KeyLabels>& labels) {
    std::lock_guard lock(g_mutex);
    if (labels == g_labels) return;
    if (labels.has_value() != g_labels.has_value())
        LOG_INFO("text", "button prompts: %s", labels ? "keyboard and mouse" : "controller");
    g_labels = labels;
    g_cache.clear();
    ++g_version;
}

std::shared_ptr<const render::TextureReplacement> key_prompt_replacement(uint64_t content, float render_scale) {
    const auto it = g_by_content.find(content);
    if (it == g_by_content.end()) return nullptr;
    std::lock_guard lock(g_mutex);
    if (!g_labels) return nullptr;
    // Drawn at the render resolution: at least 2x, at most 8x the original.
    const int scale = std::clamp(static_cast<int>(std::ceil(render_scale - 0.01f)), 2, 8);
    auto& cached = g_cache[{it->second, scale}];
    if (!cached) cached = draw(*it->second, *g_labels, scale);
    return cached;
}

uint32_t key_prompt_version(uint64_t content) { return g_by_content.count(content) ? g_version.load() : 0; }

}  // namespace text
