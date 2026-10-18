// See sharp_text.h.

#include "sharp_text.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
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

// The game's fonts and their stand-ins. Every original is white with (except impact)
// a one-pixel drop shadow to the lower right, drawn into the glyph boxes of its table.
struct FontSpec {
    const char* name;  // <name>.dds and <name>.fdf in the packs
    const char* ttf;   // in the fonts folder
    bool shadow;
    uint32_t shadow_rgb;
};
constexpr FontSpec kFonts[] = {
    {"garamond", "EBGaramond-Bold.ttf", true, 0x080808},          // menus, objectives
    {"GemFontOne", "Jost-500-Medium.ttf", true, 0x520052},        // Futura-like
    {"arialblack_14", "LiberationSerif-Bold.ttf", true, 0x080808},  // a bold Times despite the name
    {"impact", "Anton-Regular.ttf", false, 0},                    // front end headings
};
// Single player and multiplayer have their own copies (the same files).
constexpr const char* kPacks[] = {"sp/global.cod", "sp/frontend.cod", "mp/global.cod", "mp/fend.cod"};

// One line of a .fdf table: " 65 x 226 y 47 w 30 gx 1 gy 7 gw 28 gh 24" - the glyph's
// box in the atlas (x, y, gw, gh, shadow included), its advance and offset in the cell.
struct Glyph {
    int code, x, y, advance, offset_x, offset_y, width, height;
};

struct Font {
    const FontSpec* spec = nullptr;
    uint32_t width = 0, height = 0;  // the original atlas
    std::vector<Glyph> glyphs;
    std::vector<uint8_t> ttf;
    std::map<int, std::shared_ptr<const render::TextureReplacement>> by_scale;
};

std::mutex g_mutex;
std::vector<std::unique_ptr<Font>> g_fonts;
std::unordered_map<uint64_t, Font*> g_by_content;

std::vector<Glyph> parse_fdf(const std::vector<uint8_t>& data) {
    std::vector<Glyph> glyphs;
    const std::string text(data.begin(), data.end());
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        const std::string line = text.substr(start, end - start);
        start = end + 1;
        Glyph g;
        if (std::sscanf(line.c_str(), " %d x %d y %d w %d gx %d gy %d gw %d gh %d", &g.code, &g.x, &g.y, &g.advance,
                        &g.offset_x, &g.offset_y, &g.width, &g.height) == 8)
            glyphs.push_back(g);
    }
    return glyphs;
}

// The tables use Windows-1252 codes (0x8C = OE, 0x9C = oe).
int unicode_of(int code) {
    static constexpr uint16_t k80[32] = {0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                         0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
                                         0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                         0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178};
    return code >= 0x80 && code < 0xA0 ? k80[code - 0x80] : code;
}

std::shared_ptr<const render::TextureReplacement> draw_atlas(const Font& font, int scale) {
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, font.ttf.data(), stbtt_GetFontOffsetForIndex(font.ttf.data(), 0))) {
        LOG_WARN("text", "%s is not a usable font", font.spec->ttf);
        return nullptr;
    }
    const uint32_t width = font.width * scale, height = font.height * scale;
    std::vector<uint8_t> coverage(static_cast<size_t>(width) * height, 0);
    std::vector<uint8_t> bitmap;
    int missing = 0;
    for (const Glyph& g : font.glyphs) {
        // The ink: the box without the shadow's extra pixel.
        const int ink_w = g.width - (font.spec->shadow ? 1 : 0), ink_h = g.height - (font.spec->shadow ? 1 : 0);
        if (ink_w <= 0 || ink_h <= 0 || g.x < 0 || g.y < 0 || static_cast<uint32_t>(g.x + g.width) > font.width ||
            static_cast<uint32_t>(g.y + g.height) > font.height)
            continue;
        const int index = stbtt_FindGlyphIndex(&info, unicode_of(g.code));
        int x0, y0, x1, y1;
        if (!index || !stbtt_GetGlyphBox(&info, index, &x0, &y0, &x1, &y1) || x1 <= x0 || y1 <= y0) {
            ++missing;
            continue;
        }
        // Scaled so the ink fills the original's box exactly: the game's spacing stays.
        const int target_w = ink_w * scale, target_h = ink_h * scale;
        const float sx = static_cast<float>(target_w) / static_cast<float>(x1 - x0);
        const float sy = static_cast<float>(target_h) / static_cast<float>(y1 - y0);
        // Shifts that put the ink's left/top edge on the bitmap's first pixel.
        const float shift_x = std::ceil(x0 * sx) - x0 * sx, shift_y = std::ceil(-y1 * sy) + y1 * sy;
        int bx0, by0, bx1, by1;
        stbtt_GetGlyphBitmapBoxSubpixel(&info, index, sx, sy, shift_x, shift_y, &bx0, &by0, &bx1, &by1);
        const int bw = bx1 - bx0, bh = by1 - by0;
        if (bw <= 0 || bh <= 0) continue;
        bitmap.assign(static_cast<size_t>(bw) * bh, 0);
        stbtt_MakeGlyphBitmapSubpixel(&info, bitmap.data(), bw, bh, bw, sx, sy, shift_x, shift_y, index);
        for (int y = 0; y < std::min(bh, target_h); ++y)
            for (int x = 0; x < std::min(bw, target_w); ++x) {
                uint8_t& out = coverage[static_cast<size_t>(g.y * scale + y) * width + g.x * scale + x];
                out = std::max(out, bitmap[static_cast<size_t>(y) * bw + x]);
            }
    }
    if (missing) LOG_INFO("text", "%s: %d glyphs not in %s (left empty)", font.spec->name, missing, font.spec->ttf);

    // White glyphs over the shadow (the glyphs again, `scale` pixels to the lower right).
    auto replacement = std::make_shared<render::TextureReplacement>();
    replacement->width = width;
    replacement->height = height;
    replacement->pixels.resize(coverage.size());
    const float shadow[3] = {static_cast<float>(font.spec->shadow_rgb >> 16 & 0xFF),
                             static_cast<float>(font.spec->shadow_rgb >> 8 & 0xFF),
                             static_cast<float>(font.spec->shadow_rgb & 0xFF)};
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x) {
            const size_t i = static_cast<size_t>(y) * width + x;
            const float g = coverage[i] / 255.0f;
            float s = 0.0f;
            if (font.spec->shadow && x >= static_cast<uint32_t>(scale) && y >= static_cast<uint32_t>(scale))
                s = coverage[i - static_cast<size_t>(scale) * width - scale] / 255.0f * (1.0f - g);
            const float alpha = g + s;
            uint32_t pixel = static_cast<uint32_t>(std::lround(alpha * 255.0f)) << 24;
            for (int c = 0; c < 3; ++c) {
                // Transparent texels white, so filtering does not darken the edges.
                const float value = alpha > 0.0f ? (255.0f * g + shadow[c] * s) / alpha : 255.0f;
                pixel |= static_cast<uint32_t>(std::lround(value)) << (16 - 8 * c);
            }
            replacement->pixels[i] = pixel;
        }
    LOG_INFO("text", "%s redrawn at %ux%u from %s", font.spec->name, width, height, font.spec->ttf);
    return replacement;
}

std::vector<uint8_t> read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return in ? std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {}) : std::vector<uint8_t>();
}

}  // namespace

size_t prepare_sharp_text(const fs::path& assets, const fs::path& fonts_dir) {
    std::lock_guard lock(g_mutex);
    std::vector<std::unique_ptr<KapfPack>> packs;
    for (const char* name : kPacks)
        if (auto pack = KapfPack::open(assets / name)) packs.push_back(std::move(pack));

    size_t prepared = 0;
    for (const FontSpec& spec : kFonts) {
        std::vector<uint8_t> ttf = read_file(fonts_dir / spec.ttf);
        if (ttf.empty()) {
            LOG_WARN("text", "%s is missing; %s keeps its original letters", (fonts_dir / spec.ttf).string().c_str(),
                     spec.name);
            continue;
        }
        bool found = false;
        for (const auto& pack : packs) {
            const KapfPack::Entry* dds_entry = pack->find(std::string(spec.name) + ".dds");
            const KapfPack::Entry* fdf_entry = pack->find(std::string(spec.name) + ".fdf");
            if (!dds_entry || !fdf_entry) continue;
            const auto dds = pack->read(*dds_entry);
            const auto fdf = pack->read(*fdf_entry);
            if (!dds || !fdf || dds->size() <= 128 || std::memcmp(dds->data(), "DDS ", 4) != 0) continue;
            // In memory the title holds what follows the DDS header, every level.
            const uint64_t content = render::hash_bytes(dds->data() + 128, dds->size() - 128);
            if (g_by_content.count(content)) {
                found = true;
                continue;
            }
            auto font = std::make_unique<Font>();
            font->spec = &spec;
            std::memcpy(&font->height, dds->data() + 12, 4);
            std::memcpy(&font->width, dds->data() + 16, 4);
            font->glyphs = parse_fdf(*fdf);
            if (font->glyphs.empty() || !font->width || !font->height || font->width > 1024 || font->height > 1024)
                continue;
            font->ttf = ttf;
            g_by_content[content] = font.get();
            g_fonts.push_back(std::move(font));
            found = true;
        }
        if (found) ++prepared;
    }
    LOG_INFO("text", "sharper text: %zu of %zu fonts", prepared, std::size(kFonts));
    return prepared;
}

std::shared_ptr<const render::TextureReplacement> sharp_text_replacement(uint64_t content, float render_scale) {
    std::lock_guard lock(g_mutex);
    const auto it = g_by_content.find(content);
    if (it == g_by_content.end()) return nullptr;
    // At least twice the original, so the letters stay smooth at every render scale;
    // at most 8x (4096 texels for the largest atlas).
    const int scale = std::clamp(static_cast<int>(std::ceil(render_scale - 0.01f)), 2, 8);
    auto& cached = it->second->by_scale[scale];
    if (!cached) cached = draw_atlas(*it->second, scale);
    return cached;
}

}  // namespace text
