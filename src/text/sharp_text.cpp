// See sharp_text.h.

#include "sharp_text.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <cctype>
#include <span>
#include <sstream>
#include <set>
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
    const char* fallback;  // a second stand-in for letters `ttf` lacks (Cyrillic), or null
    bool shadow;
    uint32_t shadow_rgb;
};
constexpr FontSpec kFonts[] = {
    {"garamond", "EBGaramond-Bold.ttf", nullptr, true, 0x080808},            // menus, objectives
    {"GemFontOne", "Jost-500-Medium.ttf", "Montserrat-Medium.ttf", true, 0x520052},  // Futura-like
    {"arialblack_14", "LiberationSerif-Bold.ttf", nullptr, true, 0x080808},  // a bold Times despite the name
    {"impact", "Anton-Regular.ttf", "Oswald-Bold.ttf", false, 0},          // front end headings (no Cyrillic)
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
    std::set<int> borrowed;      // codes whose atlas box a script letter took (not drawn)
    std::string script_table;    // the rewritten .fdf the title reads (a text script's letters)
    std::vector<uint8_t> ttf;
    std::vector<uint8_t> fallback_ttf;  // FontSpec::fallback's, when there is one
    std::set<int> from_fallback;        // codes drawn from it
    std::map<int, std::shared_ptr<const render::TextureReplacement>> by_scale;
};

std::mutex g_mutex;
platform::TextScript g_script = platform::TextScript::Latin;
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

// Letters a script's text does not use, whose atlas boxes one of its letters may take. Polish: Western letters;
// Cyrillic: the Windows-1252 letters on codes Windows-1251 gives Serbian/Macedonian/Belarusian letters or that
// Russian/Ukrainian text does not use (the Western accented letters on 0xC0-0xFF are the Cyrillic letters' own).
constexpr int kPolishDonors[] = {0xC3, 0xD5, 0xD0, 0xDE, 0xD8, 0xC5, 0xCC, 0xCD, 0xCE, 0xCF, 0xD2, 0xD4, 0xD9, 0xDB,
                                 0xDD, 0xCB, 0xC2, 0xE3, 0xF5, 0xF0, 0xFE, 0xF8, 0xE5, 0xEC, 0xED, 0xEE, 0xEF, 0xF2,
                                 0xF4, 0xF9, 0xFB, 0xFD, 0xFF, 0xEB, 0xE2, 0xB6, 0xA7, 0xA4, 0xA9, 0xAE};
constexpr int kCyrillicDonors[] = {0x80, 0x81, 0x83, 0x8A, 0x8C, 0x8D, 0x8E, 0x8F, 0x90, 0x9A, 0x9C,
                                   0x9D, 0x9E, 0x9F, 0xA1, 0xA2, 0xA3, 0xA4, 0xA6, 0xBC, 0xBD, 0xBE};

std::span<const int> script_donors(platform::TextScript script) {
    if (script == platform::TextScript::Polish) return kPolishDonors;
    if (script == platform::TextScript::Cyrillic) return kCyrillicDonors;
    return {};
}

const char* script_name(platform::TextScript script) {
    return script == platform::TextScript::Polish ? "Polish" : script == platform::TextScript::Cyrillic ? "Cyrillic" : "";
}

// The script letter on `code` (its Unicode letter), or 0.
int script_unicode(int code) {
    for (const platform::ScriptLetter& l : platform::script_letters(g_script))
        if (l.code == code) return l.unicode;
    return 0;
}

void mark_used(const Font& font, std::vector<uint8_t>& used, int x, int y, int w, int h) {
    for (int yy = std::max(0, y); yy < std::min<int>(font.height, y + h); ++yy)
        for (int xx = std::max(0, x); xx < std::min<int>(font.width, x + w); ++xx)
            used[static_cast<size_t>(yy) * font.width + xx] = 1;
}

// A free w x h rectangle (plus a gap of one texel) in the atlas: its top-left in x, y.
bool find_free(const Font& font, const std::vector<uint8_t>& used, int w, int h, int& x, int& y) {
    const int W = static_cast<int>(font.width), H = static_cast<int>(font.height);
    std::vector<int> sum(static_cast<size_t>(W + 1) * (H + 1), 0);  // prefix sums of `used`
    for (int yy = 0; yy < H; ++yy)
        for (int xx = 0; xx < W; ++xx)
            sum[(yy + 1) * (W + 1) + xx + 1] = used[static_cast<size_t>(yy) * W + xx] + sum[yy * (W + 1) + xx + 1] +
                                               sum[(yy + 1) * (W + 1) + xx] - sum[yy * (W + 1) + xx];
    const int ww = w + 1, hh = h + 1;
    for (int yy = 0; yy + hh <= H; ++yy)
        for (int xx = 0; xx + ww <= W; ++xx) {
            const int taken = sum[(yy + hh) * (W + 1) + xx + ww] - sum[yy * (W + 1) + xx + ww] -
                              sum[(yy + hh) * (W + 1) + xx] + sum[yy * (W + 1) + xx];
            if (!taken) {
                x = xx;
                y = yy;
                return true;
            }
        }
    return false;
}

// The font's table rewritten for the text script's letters (font.glyphs changed in place, the borrowed codes and
// the letters drawn from the second stand-in noted) and as .fdf text from `original`.
void make_script_letters(Font& font, const std::string& original) {
    stbtt_fontinfo primary, fallback;
    if (!stbtt_InitFont(&primary, font.ttf.data(), stbtt_GetFontOffsetForIndex(font.ttf.data(), 0))) return;
    const bool has_fallback =
        !font.fallback_ttf.empty() &&
        stbtt_InitFont(&fallback, font.fallback_ttf.data(), stbtt_GetFontOffsetForIndex(font.fallback_ttf.data(), 0));
    const std::span<const platform::ScriptLetter> letters = platform::script_letters(g_script);
    const int shadow = font.spec->shadow ? 1 : 0;
    auto entry = [&](int code) -> Glyph* {
        for (Glyph& g : font.glyphs)
            if (g.code == code) return &g;
        return nullptr;
    };
    struct Box {
        int code, x, y, w, h;
        bool free = true;
    };
    std::vector<Box> pool;
    for (const platform::ScriptLetter& l : letters)
        if (const Glyph* g = entry(l.code); g && g->width > 1 && g->height > 1)
            pool.push_back({l.code, g->x, g->y, g->width, g->height});
    for (const int code : script_donors(g_script))
        if (const Glyph* g = entry(code); g && g->width > 1 && g->height > 1)
            pool.push_back({code, g->x, g->y, g->width, g->height});
    struct Need {
        const platform::ScriptLetter* letter;
        Glyph glyph;
        bool from_fallback;
    };
    std::vector<Need> needs;
    int missing = 0;
    for (const platform::ScriptLetter& l : letters) {
        const Glyph* base = entry(static_cast<unsigned char>(l.base));
        if (!base || !entry(l.code)) continue;
        // The stand-in that has the letter (and its base letter: both are measured in the same font's units).
        const stbtt_fontinfo* info = &primary;
        bool second = false;
        if (!stbtt_FindGlyphIndex(&primary, l.unicode) && has_fallback && stbtt_FindGlyphIndex(&fallback, l.unicode))
            info = &fallback, second = true;
        const int pi = stbtt_FindGlyphIndex(info, l.unicode), bi = stbtt_FindGlyphIndex(info, l.base);
        int px0, py0, px1, py1, bx0, by0, bx1, by1;
        if (!pi || !bi || !stbtt_GetGlyphBox(info, pi, &px0, &py0, &px1, &py1) ||
            !stbtt_GetGlyphBox(info, bi, &bx0, &by0, &bx1, &by1) || bx1 <= bx0 || by1 <= by0) {
            ++missing;
            continue;
        }
        int p_advance, b_advance, lsb;
        stbtt_GetGlyphHMetrics(info, pi, &p_advance, &lsb);
        stbtt_GetGlyphHMetrics(info, bi, &b_advance, &lsb);
        // Font units to the game's pixels, from the base letter's ink box.
        const float sx = static_cast<float>(base->width - shadow) / static_cast<float>(bx1 - bx0);
        const float sy = static_cast<float>(base->height - shadow) / static_cast<float>(by1 - by0);
        Glyph g = *base;
        g.code = l.code;
        g.width = std::max(1, static_cast<int>(std::lround((px1 - px0) * sx))) + shadow;
        g.height = std::max(1, static_cast<int>(std::lround((py1 - py0) * sy))) + shadow;
        g.offset_x = base->offset_x + static_cast<int>(std::lround((px0 - bx0) * sx));
        g.offset_y = base->offset_y + static_cast<int>(std::lround((by1 - py1) * sy));  // y down; the tops differ
        g.advance = base->advance + static_cast<int>(std::lround((p_advance - b_advance) * sx));
        needs.push_back({&l, g, second});
    }
    std::sort(needs.begin(), needs.end(), [](const Need& a, const Need& b) {
        return a.glyph.width * a.glyph.height > b.glyph.width * b.glyph.height;
    });
    // The atlas's texels in use by any box (for free rectangles).
    std::vector<uint8_t> used(static_cast<size_t>(font.width) * font.height, 0);
    for (const Glyph& g : font.glyphs) mark_used(font, used, g.x, g.y, g.width + 1, g.height + 1);  // a gap of 1
    const char* const script = script_name(g_script);
    int placed = 0, shrunk = 0;
    float smallest = 1.0f;
    for (Need& need : needs) {
        Box* best = nullptr;
        for (Box& box : pool) {
            if (!box.free || box.w < need.glyph.width || box.h < need.glyph.height) continue;
            const bool own = box.code == need.letter->code;
            if (!best || own || (best->code != need.letter->code && box.w * box.h < best->w * best->h)) best = &box;
            if (own) break;
        }
        if (!best) {
            // Unused atlas space (a free rectangle among all the boxes), else the largest box with the letter
            // shrunk to fit (its offsets scaled alike, so it sits as before).
            int fx = -1, fy = -1;
            if (find_free(font, used, need.glyph.width, need.glyph.height, fx, fy)) {
                mark_used(font, used, fx, fy, need.glyph.width, need.glyph.height);
                need.glyph.x = fx;
                need.glyph.y = fy;
                *entry(need.letter->code) = need.glyph;
                if (need.from_fallback) font.from_fallback.insert(need.letter->code);
                ++placed;
                continue;
            }
            float best_fit = 0.0f;
            for (Box& box : pool) {
                if (!box.free) continue;
                const float fit = std::min(static_cast<float>(box.w - shadow) / (need.glyph.width - shadow),
                                           static_cast<float>(box.h - shadow) / (need.glyph.height - shadow));
                if (fit > best_fit) best_fit = fit, best = &box;
            }
            if (!best || best_fit < 0.6f) {
                LOG_WARN("text", "%s: no atlas room for %s letter 0x%02X (drawn in its own box)", font.spec->name,
                         script, need.letter->code);
                continue;
            }
            Glyph& g = need.glyph;
            const int w = std::max(1, static_cast<int>((g.width - shadow) * best_fit)) + shadow;
            const int h = std::max(1, static_cast<int>((g.height - shadow) * best_fit)) + shadow;
            g.offset_x += (g.width - w) / 2;  // centred where the full-size letter would be, bottom kept
            g.offset_y += g.height - h;
            g.width = w;
            g.height = h;
            ++shrunk;
            smallest = std::min(smallest, best_fit);
            if (letters.size() <= 16)
                LOG_INFO("text", "%s: %s letter 0x%02X at %.0f%% of its size", font.spec->name, script,
                         need.letter->code, best_fit * 100.0f);
        }
        best->free = false;
        need.glyph.x = best->x;
        need.glyph.y = best->y;
        *entry(need.letter->code) = need.glyph;
        if (need.from_fallback) font.from_fallback.insert(need.letter->code);
        if (best->code != need.letter->code && !script_unicode(best->code)) font.borrowed.insert(best->code);
        ++placed;
    }
    // The .fdf text: the letters' lines replaced (a letter whose own box another letter took is drawn where its
    // entry points now).
    std::istringstream in(original);
    std::string out;
    for (std::string line; std::getline(in, line);) {
        int code = -1;
        if (std::sscanf(line.c_str(), " %d x", &code) == 1 && script_unicode(code)) {
            if (const Glyph* g = entry(code)) {
                char text[128];
                std::snprintf(text, sizeof(text), " %d x %d y %d w %d gx %d gy %d gw %d gh %d", g->code, g->x, g->y,
                              g->advance, g->offset_x, g->offset_y, g->width, g->height);
                line = text;
            }
        }
        out += line;
        out += '\n';
    }
    font.script_table = std::move(out);
    LOG_INFO("text", "%s: %d of %zu %s letters placed%s%s", font.spec->name, placed, letters.size(), script,
             font.from_fallback.empty() ? "" : (", " + std::to_string(font.from_fallback.size()) + " from " +
                                                font.spec->fallback).c_str(),
             shrunk && letters.size() > 16 ? (", " + std::to_string(shrunk) + " shrunk to fit (smallest " +
                                              std::to_string(static_cast<int>(smallest * 100)) + "%)").c_str()
                                           : "");
    if (missing) LOG_WARN("text", "%s: %d %s letters in neither stand-in", font.spec->name, missing, script);
}

std::shared_ptr<const render::TextureReplacement> draw_atlas(const Font& font, int scale) {
    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, font.ttf.data(), stbtt_GetFontOffsetForIndex(font.ttf.data(), 0))) {
        LOG_WARN("text", "%s is not a usable font", font.spec->ttf);
        return nullptr;
    }
    stbtt_fontinfo fallback;
    const bool has_fallback =
        !font.fallback_ttf.empty() &&
        stbtt_InitFont(&fallback, font.fallback_ttf.data(), stbtt_GetFontOffsetForIndex(font.fallback_ttf.data(), 0));
    const uint32_t width = font.width * scale, height = font.height * scale;
    std::vector<uint8_t> coverage(static_cast<size_t>(width) * height, 0);
    std::vector<uint8_t> bitmap;
    int missing = 0;
    for (const Glyph& g : font.glyphs) {
        if (font.borrowed.count(g.code)) continue;  // its box holds a script letter now
        // The ink: the box without the shadow's extra pixel.
        const int ink_w = g.width - (font.spec->shadow ? 1 : 0), ink_h = g.height - (font.spec->shadow ? 1 : 0);
        if (ink_w <= 0 || ink_h <= 0 || g.x < 0 || g.y < 0 || static_cast<uint32_t>(g.x + g.width) > font.width ||
            static_cast<uint32_t>(g.y + g.height) > font.height)
            continue;
        const int letter = script_unicode(g.code);
        const int unicode = letter ? letter : unicode_of(g.code);
        const stbtt_fontinfo& from = has_fallback && font.from_fallback.count(g.code) ? fallback : info;
        const int index = stbtt_FindGlyphIndex(&from, unicode);
        int x0, y0, x1, y1;
        if (!index || !stbtt_GetGlyphBox(&from, index, &x0, &y0, &x1, &y1) || x1 <= x0 || y1 <= y0) {
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
        stbtt_GetGlyphBitmapBoxSubpixel(&from, index, sx, sy, shift_x, shift_y, &bx0, &by0, &bx1, &by1);
        const int bw = bx1 - bx0, bh = by1 - by0;
        if (bw <= 0 || bh <= 0) continue;
        bitmap.assign(static_cast<size_t>(bw) * bh, 0);
        stbtt_MakeGlyphBitmapSubpixel(&from, bitmap.data(), bw, bh, bw, sx, sy, shift_x, shift_y, index);
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
            if (spec.fallback && g_script == platform::TextScript::Cyrillic) {
                font->fallback_ttf = read_file(fonts_dir / spec.fallback);
                if (font->fallback_ttf.empty())
                    LOG_WARN("text", "%s is missing; %s lacks some letters", (fonts_dir / spec.fallback).string().c_str(),
                             spec.name);
            }
            if (g_script != platform::TextScript::Latin) make_script_letters(*font, std::string(fdf->begin(), fdf->end()));
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

void set_sharp_text_script(platform::TextScript script) {
    std::lock_guard lock(g_mutex);
    g_script = script;
}

char* script_font_table(const char* name) {
    std::lock_guard lock(g_mutex);
    if (g_script == platform::TextScript::Latin || !name) return nullptr;
    std::string stem = fs::path(name).stem().string();
    for (auto& c : stem) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const auto& font : g_fonts) {
        std::string spec = font->spec->name;
        for (auto& c : spec) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (spec != stem || font->script_table.empty()) continue;
        static std::vector<std::unique_ptr<char[]>> copies;  // the title keeps its fonts for the program's life
        auto copy = std::make_unique<char[]>(font->script_table.size() + 1);
        std::memcpy(copy.get(), font->script_table.c_str(), font->script_table.size() + 1);
        copies.push_back(std::move(copy));
        return copies.back().get();
    }
    return nullptr;
}

}  // namespace text
