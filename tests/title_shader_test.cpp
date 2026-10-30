// render/shaders (tools/shader_hlsl.py): every translated title shader draws the same picture as
// the Xbox path's translation of the same program (render/d3d9_shader.cpp, the reference checked in
// game), on a real D3D9 device with random inputs:
// - pixel shaders: a quad with random colours, texture coordinates and fog per corner, random
//   textures and combiner constants, alpha kill off and on;
// - vertex programs: triangles of random vertex inputs under random constants; each output
//   (colours, texture coordinates, fog in every fog mode) drawn by a pixel shader that shows it.
// Needs the programs as the loader recorded them: XB_SHADER_DUMP=DIR (--dump-shaders DIR);
// skipped (exit 77) without them or without a device.

#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "backend.h"
#include "d3d9_shader.h"
#include "gfx_device.h"
#include "xbox_d3d.h"

namespace {

using namespace render;

constexpr uint32_t kWidth = 64, kHeight = 48;

// --- Hand-assembled helper shaders (D3D9 shader model 3 tokens) ---
constexpr uint32_t reg(uint32_t type, uint32_t number) {
    return 0x80000000u | (type & 7) << 28 | (type & 0x18) << 8 | number;
}
constexpr uint32_t kTemp = 0, kInput = 1, kConst = 2, kOutput = 6, kColorOut = 8;
constexpr uint32_t dst(uint32_t type, uint32_t number, uint32_t mask = 0xF) { return reg(type, number) | mask << 16; }
constexpr uint32_t src(uint32_t type, uint32_t number, uint32_t swizzle = 0xE4) {
    return reg(type, number) | swizzle << 16;
}
constexpr uint32_t op(uint32_t code, uint32_t length) { return code | length << 24; }
constexpr uint32_t kMov = 1, kAdd = 2, kMul = 5, kDcl = 0x1F, kDef = 0x51, kEnd = 0xFFFF;
constexpr uint32_t usage(uint32_t kind, uint32_t index) { return 0x80000000u | kind | index << 16; }
constexpr uint32_t kPosition = 0, kTexcoord = 5, kColor = 10, kFog = 11;

// Pixel shader tests: v0 position (screen space), v1 v2 colours, v3-v6 texture coordinates, v7.x fog.
std::vector<uint32_t> passthrough_vertex_shader() {
    std::vector<uint32_t> t = {0xFFFE0300};
    for (uint32_t v = 0; v < 8; ++v) t.insert(t.end(), {op(kDcl, 2), usage(kTexcoord, v), dst(kInput, v)});
    t.insert(t.end(), {op(kDcl, 2), usage(kPosition, 0), dst(kOutput, 0)});
    t.insert(t.end(), {op(kDcl, 2), usage(kColor, 0), dst(kOutput, 1), op(kDcl, 2), usage(kColor, 1), dst(kOutput, 2)});
    for (uint32_t i = 0; i < 4; ++i) t.insert(t.end(), {op(kDcl, 2), usage(kTexcoord, i), dst(kOutput, 3 + i)});
    t.insert(t.end(), {op(kDcl, 2), usage(kFog, 0), dst(kOutput, 7, 1)});
    t.insert(t.end(), {op(kMul, 3), dst(kTemp, 0), src(kInput, 0), src(kConst, kGfxVsScreenToClip)});
    t.insert(t.end(), {op(kAdd, 3), dst(kOutput, 0, 7), src(kTemp, 0), src(kConst, kGfxVsScreenToClip + 1)});
    t.insert(t.end(), {op(kMov, 2), dst(kOutput, 0, 8), src(kInput, 0, 0xFF)});
    for (uint32_t o = 1; o <= 6; ++o) t.insert(t.end(), {op(kMov, 2), dst(kOutput, o), src(kInput, o)});
    t.insert(t.end(), {op(kMov, 2), dst(kOutput, 7, 1), src(kInput, 7, 0)});
    t.push_back(kEnd);
    return t;
}

// Vertex program tests: shows one varying as colour, value * 0.25 + 0.5 (so -2..2 fits).
std::vector<uint32_t> show_varying_shader(uint32_t kind, uint32_t index) {
    const uint32_t quarter = 0x3E800000, half = 0x3F000000;
    std::vector<uint32_t> t = {0xFFFF0300};
    t.insert(t.end(), {op(kDef, 5), dst(kConst, 0), quarter, quarter, quarter, quarter});
    t.insert(t.end(), {op(kDef, 5), dst(kConst, 1), half, half, half, half});
    t.insert(t.end(), {op(kDcl, 2), usage(kind, index), dst(kInput, 0, kind == kFog ? 1 : 0xF)});
    t.insert(t.end(), {op(kMul, 3), dst(kTemp, 0), src(kInput, 0, kind == kFog ? 0 : 0xE4), src(kConst, 0)});
    t.insert(t.end(), {op(kAdd, 3), dst(kColorOut, 0), src(kTemp, 0), src(kConst, 1)});
    t.push_back(kEnd);
    return t;
}

struct Random {
    uint32_t state = 12345;
    uint32_t next() { return state = state * 1664525u + 1013904223u; }
    float uniform(float lo, float hi) { return lo + (hi - lo) * (next() >> 8) / 16777216.0f; }
};

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}

struct Harness {
    std::unique_ptr<Backend> backend;
    GfxDevice* gfx = nullptr;
    TargetId target = 0;
    std::vector<uint32_t> pixels = std::vector<uint32_t>(kWidth * kHeight);

    bool init() {
        BackendConfig config;
        config.width = kWidth;
        config.height = kHeight;
        config.back_buffer = {0x01000000, kWidth, kHeight, kFmtLinX8R8G8B8};
        config.render_scale = 1;
        config.hidden_window = true;
        backend = create_d3d9_backend(config);
        if (!backend || !(gfx = backend->gfx_device())) return false;
        target = gfx->create_target({Format::BGRA8, kWidth, kHeight, 0});
        return target != 0;
    }

    ShaderId shader(ShaderStage stage, const std::vector<uint32_t>& tokens) {
        return gfx->create_shader(stage, {tokens.data(), static_cast<uint32_t>(tokens.size() * 4)});
    }

    std::vector<uint32_t> draw(ShaderId vs, ShaderId ps, LayoutId layout, const std::vector<float>& vertices,
                               uint32_t stride, Primitive primitive) {
        gfx->set_target(target, 0);
        gfx->clear(kGfxClearColor, 0xFF000000, 1.0f, 0);
        gfx->set_shaders(vs, ps);
        gfx->set_layout(layout);
        gfx->draw_user(primitive, vertices.data(), static_cast<uint32_t>(vertices.size() * 4 / stride), stride,
                       nullptr, 0);
        gfx->read_target(target, pixels.data(), kWidth, kHeight);
        return pixels;
    }
};

// Pixels whose channels differ by more than `tolerance`.
uint32_t differences(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b, uint32_t tolerance) {
    uint32_t count = 0;
    for (size_t i = 0; i < a.size(); ++i)
        for (uint32_t shift = 0; shift < 32; shift += 8) {
            const int d = static_cast<int>(a[i] >> shift & 0xFF) - static_cast<int>(b[i] >> shift & 0xFF);
            if (static_cast<uint32_t>(std::abs(d)) > tolerance) {
                ++count;
                break;
            }
        }
    return count;
}

uint32_t covered(const std::vector<uint32_t>& a) {
    uint32_t count = 0;
    for (uint32_t p : a) count += p != 0xFF000000u;
    return count;
}

void test_pixel_shaders(Harness& h, const std::string& dir, const std::vector<uint32_t>& keys) {
    GfxDevice& g = *h.gfx;
    Random random;
    const ShaderId vs = h.shader(ShaderStage::Vertex, passthrough_vertex_shader());
    VertexAttribute attributes[8];
    for (uint8_t i = 0; i < 8; ++i) attributes[i] = {i, 0, AttributeType::Float4, static_cast<uint16_t>(i * 16)};
    const LayoutId layout = g.create_layout({attributes, 8});
    CHECK(vs && layout);

    // Random textures, point sampled; the fog colour white (the reference reads it from c24, where
    // the back end puts slot 6's texel scale: 1).
    for (uint32_t slot = 0; slot < 4; ++slot) {
        std::vector<uint32_t> texels(8 * 8);
        for (auto& t : texels) t = random.next();
        const TextureId texture = g.create_texture({TextureType::Tex2D, Format::BGRA8, 8, 8, 1, 1});
        g.update_texture(texture, 0, 0, texels.data(), 32, 0);
        g.set_texture(slot, texture);
    }
    Fog fog;
    fog.color = 0xFFFFFFFF;
    g.set_fog(fog);

    uint32_t failed = 0;
    for (const uint32_t key : keys) {
        char name[32];
        std::snprintf(name, sizeof(name), "/ps_%08x.bin", key);
        const std::vector<uint8_t> blob = read_file(dir + name);
        if (blob.size() < sizeof(XbPixelShaderDef)) continue;
        d3d9::PixelShaderKey reference{};
        std::memcpy(&reference.def, blob.data(), sizeof(reference.def));
        const ShaderId ours = g.create_title_shader(ShaderStage::Pixel, key);
        CHECK(ours != 0, "ps %08x: not built", key);

        // A quad over the whole target with random interpolants per corner.
        const float corners[4][2] = {{-0.5f, -0.5f}, {kWidth - 0.5f, -0.5f}, {kWidth - 0.5f, kHeight - 0.5f},
                                     {-0.5f, kHeight - 0.5f}};
        std::vector<float> vertices;
        for (const auto& corner : corners) {
            vertices.insert(vertices.end(), {corner[0], corner[1], 0.0f, 1.0f});
            for (uint32_t i = 1; i < 8; ++i)
                for (uint32_t k = 0; k < 4; ++k)
                    vertices.push_back(i >= 3 && i <= 6 ? (k == 3 ? 1.0f : random.uniform(0, 1))
                                                        : random.uniform(-0.2f, 1.2f));
        }
        float constants[18 * 4];
        for (float& c : constants) c = random.uniform(0, 1);
        g.set_constants(ShaderStage::Pixel, 0, constants, 18);

        for (const bool kill : {false, true}) {
            reference.alpha_kill_mask = kill ? 0xF : 0;
            for (uint32_t slot = 0; slot < 4; ++slot) {
                SamplerState sampler;
                sampler.min = sampler.mag = Filter::Point;
                sampler.alpha_kill = kill;
                g.set_sampler(slot, sampler);
            }
            const ShaderId theirs = h.shader(ShaderStage::Pixel, d3d9::translate_pixel_shader(reference));
            const auto expected = h.draw(vs, theirs, layout, vertices, 8 * 16, Primitive::Quads);
            const auto actual = h.draw(vs, ours, layout, vertices, 8 * 16, Primitive::Quads);
            g.destroy_shader(theirs);
            const uint32_t wrong = differences(expected, actual, 2);
            if (wrong) {
                ++failed;
                const uint32_t at = [&] {
                    for (uint32_t i = 0; i < expected.size(); ++i)
                        if (differences({expected[i]}, {actual[i]}, 2)) return i;
                    return 0u;
                }();
                std::printf("ps %08x (alpha kill %s): %u of %u pixels differ, first at %u,%u: %08x, ours %08x\n", key,
                            kill ? "on" : "off", wrong, kWidth * kHeight, at % kWidth, at / kWidth, expected[at],
                            actual[at]);
            }
        }
        g.destroy_shader(ours);
    }
    CHECK(failed == 0, "%u pixel shader runs differ", failed);
    std::printf("pixel shaders: %zu compared, %u runs differ\n", keys.size(), failed);
    for (uint32_t slot = 0; slot < 4; ++slot) g.set_texture(slot, 0);
}

// NORMPACKED3 as the Xbox path decodes it (render/draw.cpp): 11:11:10 signed normalized, w 1.
void unpack_normal(uint32_t v, float* out) {
    auto extend = [](uint32_t value, int bits) {
        return static_cast<float>(static_cast<int32_t>(value << (32 - bits)) >> (32 - bits));
    };
    out[0] = std::max(extend(v & 0x7FF, 11) / 1023.0f, -1.0f);
    out[1] = std::max(extend(v >> 11 & 0x7FF, 11) / 1023.0f, -1.0f);
    out[2] = std::max(extend(v >> 22, 10) / 511.0f, -1.0f);
    out[3] = 1.0f;
}

// One vertex program with every input given; `packed`: the inputs given as packed normals
// (ours: the variant for `declaration`, fed the packed bytes; the reference: the decoded floats).
struct VertexCase {
    uint32_t key, declaration;
    uint32_t packed;  // input mask
};

void test_vertex_programs(Harness& h, const std::string& dir, const std::vector<VertexCase>& cases) {
    GfxDevice& g = *h.gfx;
    Random random;
    VertexAttribute attributes[16];
    for (uint8_t i = 0; i < 16; ++i) attributes[i] = {i, 0, AttributeType::Float4, static_cast<uint16_t>(i * 16)};
    const LayoutId layout = g.create_layout({attributes, 16});
    std::map<uint32_t, LayoutId> packed_layouts;  // by packed input mask
    struct Varying {
        const char* name;
        uint32_t kind, index;
    };
    const Varying varyings[] = {{"d0", kColor, 0}, {"d1", kColor, 1}, {"t0", kTexcoord, 0}, {"t1", kTexcoord, 1},
                                {"t2", kTexcoord, 2}, {"t3", kTexcoord, 3}, {"fog", kFog, 0}};
    ShaderId shows[7];
    for (int i = 0; i < 7; ++i) shows[i] = h.shader(ShaderStage::Pixel, show_varying_shader(varyings[i].kind, varyings[i].index));

    uint32_t failed = 0, total_covered = 0, runs = 0;
    for (const VertexCase& c : cases) {
        const uint32_t key = c.key;
        char name[32];
        std::snprintf(name, sizeof(name), "/vs_%08x.bin", key);
        const std::vector<uint8_t> blob = read_file(dir + name);
        if (blob.size() < 4) continue;
        const uint32_t count = blob[2] | blob[3] << 8;
        const auto* instructions = reinterpret_cast<const uint32_t(*)[4]>(blob.data() + 4);
        const ShaderId ours = g.create_title_shader(ShaderStage::Vertex, key, c.packed ? c.declaration : 0);
        CHECK(ours != 0, "vs %08x (%08x): not built", key, c.declaration);
        LayoutId ours_layout = layout;
        if (c.packed) {
            LayoutId& l = packed_layouts[c.packed];
            if (!l) {
                VertexAttribute a[16];
                for (uint8_t i = 0; i < 16; ++i)
                    a[i] = {i, 0, (c.packed >> i & 1) ? AttributeType::UByte4 : AttributeType::Float4,
                            static_cast<uint16_t>(i * 16)};
                l = g.create_layout({a, 16});
            }
            ours_layout = l;
        }

        // Random constants and inputs; enough triangles that about a tenth of the target is drawn.
        std::vector<float> constants(192 * 4);
        for (float& c : constants) c = random.uniform(-16, 16);
        g.set_constants(ShaderStage::Vertex, 0, constants.data(), 192);
        std::vector<float> vertices;
        for (uint32_t v = 0; v < 3 * 256; ++v)
            for (uint32_t k = 0; k < 64; ++k) vertices.push_back(k == 0 ? random.uniform(0, kWidth)
                                                                 : k == 1 ? random.uniform(0, kHeight)
                                                                 : k == 3 ? 1.0f
                                                                          : random.uniform(-1, 1));
        // Packed inputs: random packed words for ours, their decoded floats for the reference.
        std::vector<float> packed_vertices = vertices;
        for (uint32_t v = 0; v < 3 * 256; ++v)
            for (uint32_t i = 0; i < 16; ++i) {
                if (!(c.packed >> i & 1)) continue;
                const uint32_t word = random.next();
                std::memcpy(&packed_vertices[v * 64 + i * 4], &word, 4);
                unpack_normal(word, &vertices[v * 64 + i * 4]);
            }
        for (int fog_mode = 0; fog_mode < 4; ++fog_mode) {
            Fog fog;
            fog.mode = static_cast<FogMode>(fog_mode);
            fog.bias = 0.5f;
            fog.scale = 0.01f;
            g.set_fog(fog);
            const ShaderId theirs = h.shader(
                ShaderStage::Vertex,
                d3d9::translate_vertex_program(instructions, count, 0xFFFF, static_cast<d3d9::FogMode>(fog_mode)));
            for (int i = fog_mode ? 6 : 0; i < 7; ++i) {  // fog modes only matter to the fog output
                const auto expected = h.draw(theirs, shows[i], layout, vertices, 256, Primitive::Triangles);
                const auto actual =
                    h.draw(ours, shows[i], ours_layout, c.packed ? packed_vertices : vertices, 256, Primitive::Triangles);
                ++runs;
                total_covered += covered(expected);
                const uint32_t wrong = differences(expected, actual, 2);
                // Edges may round differently; more than a few pixels is a translation error.
                if (wrong > 4) {
                    ++failed;
                    std::printf("vs %08x (%08x) %s (fog mode %d): %u pixels differ of %u covered\n", key,
                                c.declaration, varyings[i].name, fog_mode, wrong, covered(expected));
                }
            }
            g.destroy_shader(theirs);
        }
        g.destroy_shader(ours);
    }
    CHECK(failed == 0, "%u vertex program runs differ", failed);
    std::printf("vertex programs: %zu compared, %u runs differ, %u pixels covered per run on average\n", cases.size(),
                failed, runs ? total_covered / runs : 0);
}

}  // namespace

int main() {
    const char* dir = std::getenv("XB_SHADER_DUMP");
    if (!dir) {
        std::printf("SKIP: set XB_SHADER_DUMP to a --dump-shaders directory\n");
        return 77;
    }
    std::ifstream csv(std::string(dir) + "/shaders.csv");
    std::map<char, std::vector<uint32_t>> keys;
    for (std::string line; std::getline(csv, line);) {
        if (line.size() < 10) continue;
        const uint32_t key = static_cast<uint32_t>(std::stoul(line.substr(2, 8), nullptr, 16));
        auto& list = keys[line[0]];
        if (std::find(list.begin(), list.end(), key) == list.end()) list.push_back(key);
    }
    if (keys.empty()) {
        std::printf("SKIP: no shaders in %s\n", dir);
        return 77;
    }
    Harness h;
    if (!h.init()) {
        std::printf("SKIP: no D3D9 device\n");
        return 77;
    }
    test_pixel_shaders(h, dir, keys['p']);
    // Each vertex program, and its variant for every declaration giving it packed normals.
    std::vector<VertexCase> cases;
    for (const uint32_t key : keys['v']) cases.push_back({key, 0, 0});
    std::ifstream again(std::string(dir) + "/shaders.csv");
    for (std::string line; std::getline(again, line);) {
        if (line.size() < 28 || line[0] != 'v') continue;
        const uint32_t key = static_cast<uint32_t>(std::stoul(line.substr(2, 8), nullptr, 16));
        const uint32_t declaration = static_cast<uint32_t>(std::stoul(line.substr(20, 8), nullptr, 16));
        char name[32];
        std::snprintf(name, sizeof(name), "/decl_%08x.bin", declaration);
        const std::vector<uint8_t> inputs = read_file(std::string(dir) + name);
        uint32_t packed = 0;
        for (uint32_t v = 0; v < 16 && inputs.size() >= 256; ++v) {
            uint32_t format;
            std::memcpy(&format, inputs.data() + v * 16 + 8, 4);
            if (format == 0x16) packed |= 1u << v;
        }
        bool seen = false;
        for (const VertexCase& c : cases) seen |= c.key == key && c.declaration == declaration;
        if (packed && !seen) cases.push_back({key, declaration, packed});
    }
    test_vertex_programs(h, dir, cases);
    if (test::g_failures) return 1;
    std::printf("title_shader_test: ok\n");
    return 0;
}
