#include "front_picture.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <system_error>

#include "kapf.h"
#include "log.h"

namespace fs = std::filesystem;

namespace launcher {

namespace {

constexpr size_t kDdsHeader = 128;
constexpr int kConceptChapters = 13;  // the Bonus Materials' concept art: one category per chapter 1..13

uint32_t u32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

uint16_t u16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }

std::array<uint8_t, 4> rgb565(uint16_t c) {
    const int r = c >> 11 & 31, g = c >> 5 & 63, b = c & 31;
    return {static_cast<uint8_t>(r << 3 | r >> 2), static_cast<uint8_t>(g << 2 | g >> 4),
            static_cast<uint8_t>(b << 3 | b >> 2), 255};
}

// One 4x4 colour block (8 bytes) into `out` (16 RGBA texels); `three` = DXT1's 3-colour mode when c0 <= c1.
void colour_block(const uint8_t* block, bool dxt1, uint8_t out[16][4]) {
    const uint16_t c0 = u16(block), c1 = u16(block + 2);
    std::array<std::array<uint8_t, 4>, 4> colours{rgb565(c0), rgb565(c1)};
    const bool three = dxt1 && c0 <= c1;
    for (int i = 0; i < 3; ++i) {
        if (three) {
            colours[2][i] = static_cast<uint8_t>((colours[0][i] + colours[1][i]) / 2);
            colours[3][i] = 0;
        } else {
            colours[2][i] = static_cast<uint8_t>((2 * colours[0][i] + colours[1][i]) / 3);
            colours[3][i] = static_cast<uint8_t>((colours[0][i] + 2 * colours[1][i]) / 3);
        }
    }
    colours[2][3] = 255;
    colours[3][3] = three ? 0 : 255;
    const uint32_t indices = u32(block + 4);
    for (int t = 0; t < 16; ++t) std::memcpy(out[t], colours[indices >> (2 * t) & 3].data(), 4);
}

// DXT5's interpolated alpha block (8 bytes).
void alpha_block(const uint8_t* block, uint8_t out[16][4]) {
    const int a0 = block[0], a1 = block[1];
    int alphas[8] = {a0, a1};
    if (a0 > a1) {
        for (int i = 1; i < 7; ++i) alphas[i + 1] = ((7 - i) * a0 + i * a1) / 7;
    } else {
        for (int i = 1; i < 5; ++i) alphas[i + 1] = ((5 - i) * a0 + i * a1) / 5;
        alphas[6] = 0;
        alphas[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= uint64_t{block[2 + i]} << (8 * i);
    for (int t = 0; t < 16; ++t) out[t][3] = static_cast<uint8_t>(alphas[bits >> (3 * t) & 7]);
}

std::optional<std::vector<uint8_t>> read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

std::optional<std::vector<uint8_t>> pack_file(const fs::path& assets, const char* name) {
    const std::unique_ptr<text::KapfPack> pack = text::KapfPack::open(assets / "sp" / "frontend.cod");
    if (!pack) return std::nullopt;
    for (const text::KapfPack::Entry& entry : pack->entries()) {
        std::string lower = entry.name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        if (lower == name) return pack->read(entry);
    }
    return std::nullopt;
}

// Cuts dark bars off the top and bottom (the concept art is letterboxed in a square texture); the shape follows:
// the game shows the whole texture at 4:3.
void cut_letterbox(Picture& p) {
    const auto row_dark = [&](int y) {
        uint64_t sum = 0;
        for (int x = 0; x < p.width; ++x) {
            const uint8_t* t = &p.rgba[(static_cast<size_t>(y) * p.width + x) * 4];
            sum += t[0] + t[1] + t[2];
        }
        return sum < static_cast<uint64_t>(p.width) * 3 * 10;  // average below 10 of 255
    };
    int top = 0, bottom = p.height;
    while (top < bottom && row_dark(top)) ++top;
    while (bottom > top && row_dark(bottom - 1)) --bottom;
    if (bottom - top < p.height / 4) return;  // a dark picture, not bars
    const float full = p.aspect;
    p.aspect = full * static_cast<float>(p.height) / static_cast<float>(bottom - top);
    p.rgba.erase(p.rgba.begin() + static_cast<ptrdiff_t>(static_cast<size_t>(bottom) * p.width * 4), p.rgba.end());
    p.rgba.erase(p.rgba.begin(), p.rgba.begin() + static_cast<ptrdiff_t>(static_cast<size_t>(top) * p.width * 4));
    p.height = bottom - top;
}

}  // namespace

std::optional<Picture> decode_dds(const std::vector<uint8_t>& dds) {
    if (dds.size() < kDdsHeader || std::memcmp(dds.data(), "DDS ", 4) != 0) return std::nullopt;
    Picture p;
    p.height = static_cast<int>(u32(&dds[12]));
    p.width = static_cast<int>(u32(&dds[16]));
    if (p.width <= 0 || p.height <= 0 || p.width > 4096 || p.height > 4096) return std::nullopt;
    p.aspect = static_cast<float>(p.width) / static_cast<float>(p.height);
    const uint32_t flags = u32(&dds[80]);
    const uint32_t fourcc = u32(&dds[84]);
    const uint8_t* data = dds.data() + kDdsHeader;
    const size_t size = dds.size() - kDdsHeader;
    p.rgba.assign(static_cast<size_t>(p.width) * p.height * 4, 0);
    if (flags & 4) {  // DDPF_FOURCC
        int kind = 0;
        if (fourcc == u32(reinterpret_cast<const uint8_t*>("DXT1"))) kind = 1;
        if (fourcc == u32(reinterpret_cast<const uint8_t*>("DXT3"))) kind = 3;
        if (fourcc == u32(reinterpret_cast<const uint8_t*>("DXT5"))) kind = 5;
        if (!kind) return std::nullopt;
        const int bw = (p.width + 3) / 4, bh = (p.height + 3) / 4;
        const size_t block_bytes = kind == 1 ? 8 : 16;
        if (size < static_cast<size_t>(bw) * bh * block_bytes) return std::nullopt;
        for (int by = 0; by < bh; ++by)
            for (int bx = 0; bx < bw; ++bx) {
                const uint8_t* block = data + (static_cast<size_t>(by) * bw + bx) * block_bytes;
                uint8_t texels[16][4];
                colour_block(kind == 1 ? block : block + 8, kind == 1, texels);
                if (kind == 3)
                    for (int t = 0; t < 16; ++t) texels[t][3] = static_cast<uint8_t>((block[t / 2] >> (4 * (t & 1)) & 15) * 17);
                if (kind == 5) alpha_block(block, texels);
                for (int t = 0; t < 16; ++t) {
                    const int x = bx * 4 + t % 4, y = by * 4 + t / 4;
                    if (x < p.width && y < p.height)
                        std::memcpy(&p.rgba[(static_cast<size_t>(y) * p.width + x) * 4], texels[t], 4);
                }
            }
        return p;
    }
    if ((flags & 0x40) && u32(&dds[88]) == 32 && size >= p.rgba.size()) {  // DDPF_RGB, 32 bits: B G R A in memory
        const bool alpha = flags & 1;
        for (size_t i = 0; i < p.rgba.size(); i += 4) {
            p.rgba[i] = data[i + 2];
            p.rgba[i + 1] = data[i + 1];
            p.rgba[i + 2] = data[i];
            p.rgba[i + 3] = alpha ? data[i + 3] : 255;
        }
        return p;
    }
    return std::nullopt;
}

int unlocked_concept_chapters(const fs::path& udata) {
    int best = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(udata, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name != "Profiles" && name != "Profiles1" && name != "Profiles2" && name != "Profiles3") continue;
        const std::optional<std::vector<uint8_t>> file = read_file(it->path());
        if (!file || file->size() < 0x1c + 0x28 || std::memcmp(file->data(), "9-21-05", 7) != 0) continue;
        const uint8_t* profile = file->data() + 0x1c;
        if (!profile[0]) continue;  // an empty slot
        const int reached = static_cast<int32_t>(u32(profile + 0x14));
        const int chapters = profile[0x24] ? kConceptChapters : std::clamp(reached - 1, 0, kConceptChapters);
        best = std::max(best, chapters);
    }
    return best;
}

std::vector<fs::path> unlocked_concept_art(const fs::path& assets, int chapters) {
    std::vector<fs::path> art;
    std::error_code ec;
    for (int n = 1; n <= chapters; ++n) {
        const fs::path folder = assets / "bonus" / (n > 7 ? "concept2" : "concept1");
        for (int item = 1; item < 100; ++item) {
            const fs::path file = folder / ("cp" + std::to_string(n) + "_" + std::to_string(item) + "f.dds");
            if (!fs::is_regular_file(file, ec)) break;
            art.push_back(file);
        }
    }
    return art;
}

std::optional<Picture> front_picture(const fs::path& assets, const std::vector<fs::path>& concept_art,
                                     std::mt19937& random) {
    if (!concept_art.empty()) {
        const fs::path& file = concept_art[std::uniform_int_distribution<size_t>(0, concept_art.size() - 1)(random)];
        if (const auto bytes = read_file(file))
            if (std::optional<Picture> p = decode_dds(*bytes)) {
                p->aspect = 4.0f / 3.0f;
                cut_letterbox(*p);
                p->source = file.filename().string();
                return p;
            }
        LOG_WARN("launcher", "cannot read %s; the main menu picture instead", file.string().c_str());
    }
    if (const auto bytes = pack_file(assets, "single_front_bkrnd.dds"))
        if (std::optional<Picture> p = decode_dds(*bytes)) {
            p->aspect = 4.0f / 3.0f;  // the game stretches it over the 4:3 screen (bars included)
            cut_letterbox(*p);
            p->source = "the main menu";
            return p;
        }
    return std::nullopt;
}

std::optional<Picture> game_logo(const fs::path& assets) {
    if (const auto bytes = pack_file(assets, "logo.dds"))
        if (std::optional<Picture> p = decode_dds(*bytes)) {
            p->source = "logo";
            return p;
        }
    return std::nullopt;
}

}  // namespace launcher
