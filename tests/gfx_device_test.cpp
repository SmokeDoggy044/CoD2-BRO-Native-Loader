// render/gfx_d3d9: our graphics interface on the D3D9 back end, drawn on a real device (DXVK on
// Linux) in a hidden window and read back: pixel coverage of the screen -> clip mapping at render
// scales 1 and 2, vertex attributes, textures, our own render targets, blending, quads, buffers.
// Skipped (exit 77) where no device can be made.

#include "test_support.h"

#include <vector>

#include "backend.h"
#include "gfx_device.h"
#include "xbox_d3d.h"

namespace {

using namespace render;

constexpr uint32_t kWidth = 64, kHeight = 48;

// Register tokens (D3D9 shader model 3).
constexpr uint32_t reg(uint32_t type, uint32_t number) {
    return 0x80000000u | (type & 7) << 28 | (type & 0x18) << 8 | number;
}
constexpr uint32_t kTemp = 0, kInput = 1, kConst = 2, kOutput = 6, kColorOut = 8, kSampler = 10;
constexpr uint32_t dst(uint32_t type, uint32_t number, uint32_t mask = 0xF) { return reg(type, number) | mask << 16; }
constexpr uint32_t src(uint32_t type, uint32_t number, uint32_t swizzle = 0xE4) {
    return reg(type, number) | swizzle << 16;
}
constexpr uint32_t op(uint32_t code, uint32_t length) { return code | length << 24; }
constexpr uint32_t kMov = 1, kAdd = 2, kMul = 5, kDcl = 0x1F, kTexld = 0x42, kEnd = 0xFFFF;
constexpr uint32_t kUsageTexcoord = 0x80000005u;

// v0 = screen-space position (w 1), v1 = passed on as texcoord0 (colour or texture coordinates).
const std::vector<uint32_t> kVertexShader = {
    0xFFFE0300,
    op(kDcl, 2), kUsageTexcoord, dst(kInput, 0),
    op(kDcl, 2), kUsageTexcoord | 1 << 16, dst(kInput, 1),
    op(kDcl, 2), 0x80000000u, dst(kOutput, 0),  // position
    op(kDcl, 2), kUsageTexcoord, dst(kOutput, 1),
    op(kMul, 3), dst(kTemp, 0), src(kInput, 0), src(kConst, kGfxVsScreenToClip),
    op(kAdd, 3), dst(kOutput, 0, 7), src(kTemp, 0), src(kConst, kGfxVsScreenToClip + 1),
    op(kMov, 2), dst(kOutput, 0, 8), src(kInput, 0, 0xFF),
    op(kMov, 2), dst(kOutput, 1), src(kInput, 1),
    kEnd,
};

const std::vector<uint32_t> kColorShader = {
    0xFFFF0300,
    op(kDcl, 2), kUsageTexcoord, dst(kInput, 0),
    op(kMov, 2), dst(kColorOut, 0), src(kInput, 0),
    kEnd,
};

const std::vector<uint32_t> kTextureShader = {
    0xFFFF0300,
    op(kDcl, 2), kUsageTexcoord, dst(kInput, 0),
    op(kDcl, 2), 0x90000000u, dst(kSampler, 0),  // 2D
    op(kTexld, 3), dst(kTemp, 0), src(kInput, 0), src(kSampler, 0),
    op(kMov, 2), dst(kColorOut, 0), src(kTemp, 0),
    kEnd,
};

struct ColorVertex {
    float x, y, z, w;
    uint32_t color;  // A8R8G8B8
};
struct UvVertex {
    float x, y, z, w;
    float u, v;
};

const VertexAttribute kColorAttributes[] = {
    {0, 0, AttributeType::Float4, 0},
    {1, 0, AttributeType::Color, 16},
};
const VertexAttribute kUvAttributes[] = {
    {0, 0, AttributeType::Float4, 0},
    {1, 0, AttributeType::Float2, 16},
};

// A quad covering title pixels [x0, x1) x [y0, y1) (pixel centres on integers).
void color_quad(ColorVertex (&out)[4], float x0, float y0, float x1, float y1, uint32_t color) {
    out[0] = {x0 - 0.5f, y0 - 0.5f, 0, 1, color};
    out[1] = {x1 - 0.5f, y0 - 0.5f, 0, 1, color};
    out[2] = {x1 - 0.5f, y1 - 0.5f, 0, 1, color};
    out[3] = {x0 - 0.5f, y1 - 0.5f, 0, 1, color};
}

struct Fixture {
    std::unique_ptr<Backend> backend;
    GfxDevice* gfx = nullptr;
    ShaderId vs = 0, color_ps = 0, texture_ps = 0;
    LayoutId color_layout = 0, uv_layout = 0;
    std::vector<uint32_t> pixels = std::vector<uint32_t>(kWidth * kHeight);

    bool init(float render_scale) {
        BackendConfig config;
        config.width = kWidth;
        config.height = kHeight;
        config.back_buffer = {0x01000000, kWidth, kHeight, kFmtLinX8R8G8B8};
        config.depth_stencil = {0x02000000, kWidth, kHeight, kFmtD24S8};
        config.render_scale = render_scale;
        config.hidden_window = true;
        backend = create_d3d9_backend(config);
        if (!backend || !(gfx = backend->gfx_device())) return false;
        vs = gfx->create_shader(ShaderStage::Vertex, {kVertexShader.data(), uint32_t(kVertexShader.size() * 4)});
        color_ps = gfx->create_shader(ShaderStage::Pixel, {kColorShader.data(), uint32_t(kColorShader.size() * 4)});
        texture_ps =
            gfx->create_shader(ShaderStage::Pixel, {kTextureShader.data(), uint32_t(kTextureShader.size() * 4)});
        color_layout = gfx->create_layout({kColorAttributes, 2});
        uv_layout = gfx->create_layout({kUvAttributes, 2});
        CHECK(vs && color_ps && texture_ps && color_layout && uv_layout);
        return vs && color_ps && texture_ps && color_layout && uv_layout;
    }

    uint32_t at(uint32_t x, uint32_t y) const { return pixels[y * kWidth + x] & 0xFFFFFF; }
    bool read(TargetId target) { return gfx->read_target(target, pixels.data(), kWidth, kHeight); }
};

// A quad over pixels [10, 20) x [5, 15) fills exactly those, at either render scale.
void test_coverage(Fixture& f, float scale) {
    GfxDevice& g = *f.gfx;
    g.set_target(g.back_buffer(), g.depth_buffer());
    g.clear(kGfxClearColor | kGfxClearDepth, 0xFF000000, 1.0f, 0);
    g.set_shaders(f.vs, f.color_ps);
    g.set_layout(f.color_layout);
    ColorVertex quad[4];
    color_quad(quad, 10, 5, 20, 15, 0xFF20C040);
    g.draw_user(Primitive::Quads, quad, 4, sizeof(ColorVertex), nullptr, 0);
    CHECK(f.read(g.back_buffer()));
    uint32_t wrong = 0;
    for (uint32_t y = 0; y < kHeight; ++y)
        for (uint32_t x = 0; x < kWidth; ++x) {
            const bool inside = x >= 10 && x < 20 && y >= 5 && y < 15;
            if (f.at(x, y) != (inside ? 0x20C040u : 0u)) {
                if (!wrong) std::printf("scale %.0f: pixel %u,%u = %06x\n", double(scale), x, y, f.at(x, y));
                ++wrong;
            }
        }
    CHECK(wrong == 0, "scale %.0f: %u pixels wrong", double(scale), wrong);

    // A viewport moves the same screen-space positions' clip mapping with it: the quad stays put.
    g.clear(kGfxClearColor, 0xFF000000, 1.0f, 0);
    g.set_viewport({8, 4, 32, 24, 0.0f, 1.0f});
    g.draw_user(Primitive::Quads, quad, 4, sizeof(ColorVertex), nullptr, 0);
    g.set_viewport({});
    CHECK(f.read(g.back_buffer()));
    CHECK(f.at(10, 5) == 0x20C040 && f.at(19, 14) == 0x20C040 && f.at(9, 5) == 0 && f.at(20, 14) == 0,
          "viewport: %06x %06x %06x %06x", f.at(10, 5), f.at(19, 14), f.at(9, 5), f.at(20, 14));

    // The scissor clips.
    g.clear(kGfxClearColor, 0xFF000000, 1.0f, 0);
    const Rect clip{0, 0, 15, kHeight};
    g.set_scissor(&clip);
    g.draw_user(Primitive::Quads, quad, 4, sizeof(ColorVertex), nullptr, 0);
    g.set_scissor(nullptr);
    CHECK(f.read(g.back_buffer()));
    CHECK(f.at(14, 8) == 0x20C040 && f.at(15, 8) == 0, "scissor: %06x %06x", f.at(14, 8), f.at(15, 8));
}

// A 2x2 texture, point sampled over a quad; then the same through a render target of ours.
void test_texture_and_target(Fixture& f) {
    GfxDevice& g = *f.gfx;
    const TextureId texture = g.create_texture({TextureType::Tex2D, Format::BGRA8, 2, 2, 1, 1});
    CHECK(texture != 0);
    const uint32_t texels[4] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFFFF};
    CHECK(g.update_texture(texture, 0, 0, texels, 8, 0));
    SamplerState point;
    point.min = point.mag = Filter::Point;
    point.u = point.v = Address::Clamp;
    g.set_sampler(0, point);
    g.set_texture(0, texture);

    const TargetId target = g.create_target({Format::BGRA8, 32, 32, 0});
    CHECK(target != 0);
    g.set_target(target, 0);
    g.clear(kGfxClearColor, 0xFF000000, 1.0f, 0);
    g.set_shaders(f.vs, f.texture_ps);
    g.set_layout(f.uv_layout);
    const UvVertex quad[4] = {
        {-0.5f, -0.5f, 0, 1, 0, 0}, {31.5f, -0.5f, 0, 1, 1, 0}, {31.5f, 31.5f, 0, 1, 1, 1}, {-0.5f, 31.5f, 0, 1, 0, 1}};
    g.draw_user(Primitive::Quads, quad, 4, sizeof(UvVertex), nullptr, 0);

    // The target onto the back buffer at 0,0.
    g.set_target(g.back_buffer(), g.depth_buffer());
    g.clear(kGfxClearColor, 0xFF000000, 1.0f, 0);
    g.set_texture(0, g.target_texture(target));
    g.draw_user(Primitive::Quads, quad, 4, sizeof(UvVertex), nullptr, 0);
    CHECK(f.read(g.back_buffer()));
    CHECK(f.at(4, 4) == 0xFF0000 && f.at(28, 4) == 0x00FF00 && f.at(4, 28) == 0x0000FF && f.at(28, 28) == 0xFFFFFF,
          "texture via target: %06x %06x %06x %06x", f.at(4, 4), f.at(28, 4), f.at(4, 28), f.at(28, 28));
    g.set_texture(0, 0);
    g.destroy_target(target);
    g.destroy_texture(texture);
}

// Alpha blending, and static vertex/index buffers drawn indexed.
void test_blend_and_buffers(Fixture& f) {
    GfxDevice& g = *f.gfx;
    g.set_target(g.back_buffer(), g.depth_buffer());
    g.clear(kGfxClearColor, 0xFF0000FF, 1.0f, 0);
    ColorVertex quad[4];
    color_quad(quad, 0, 0, 16, 16, 0x80FF0000);  // red at half alpha over blue
    const uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
    const BufferId vb = g.create_buffer({BufferKind::Vertex, BufferUsage::Static, sizeof(quad)}, quad);
    const BufferId ib = g.create_buffer({BufferKind::Index16, BufferUsage::Static, sizeof(indices)}, indices);
    CHECK(vb && ib);
    BlendState blend;
    blend.enable = true;
    blend.src = Blend::SrcAlpha;
    blend.dst = Blend::InvSrcAlpha;
    g.set_blend(blend);
    g.set_shaders(f.vs, f.color_ps);
    g.set_layout(f.color_layout);
    g.set_vertex_buffer(0, vb, 0, sizeof(ColorVertex));
    g.set_index_buffer(ib);
    g.draw_indexed(Primitive::Triangles, 0, 6, 0);
    g.set_blend({});
    CHECK(f.read(g.back_buffer()));
    const uint32_t p = f.at(8, 8);
    const auto near = [](uint32_t a, uint32_t b) { return a > b ? a - b <= 2 : b - a <= 2; };
    CHECK(near(p >> 16, 0x80) && (p >> 8 & 0xFF) == 0 && near(p & 0xFF, 0x7F), "blend: %06x", p);
    CHECK(f.at(20, 8) == 0x0000FF, "outside: %06x", f.at(20, 8));

    // Depth: a nearer quad first, then a farther one behind it does not show.
    g.clear(kGfxClearColor | kGfxClearDepth, 0xFF000000, 1.0f, 0);
    DepthStencilState depth;
    depth.depth_test = depth.depth_write = true;
    depth.depth_func = Compare::LessEqual;
    g.set_depth_stencil(depth);
    color_quad(quad, 0, 0, 16, 16, 0xFF00FF00);
    for (auto& v : quad) v.z = 0.25f * 16777215.0f;  // depth buffer units
    g.draw_user(Primitive::Quads, quad, 4, sizeof(ColorVertex), nullptr, 0);
    color_quad(quad, 8, 0, 24, 16, 0xFFFF0000);
    for (auto& v : quad) v.z = 0.75f * 16777215.0f;
    g.draw_user(Primitive::Quads, quad, 4, sizeof(ColorVertex), nullptr, 0);
    g.set_depth_stencil({});
    CHECK(f.read(g.back_buffer()));
    CHECK(f.at(10, 8) == 0x00FF00 && f.at(20, 8) == 0xFF0000, "depth: %06x %06x", f.at(10, 8), f.at(20, 8));
    g.destroy_buffer(vb);
    g.destroy_buffer(ib);
}

}  // namespace

int main() {
    for (const float scale : {1.0f, 2.0f}) {
        Fixture f;
        if (!f.init(scale)) {
            if (!f.backend) {
                std::printf("SKIP: no D3D9 device\n");
                return 77;
            }
            return 1;
        }
        test_coverage(f, scale);
        test_texture_and_target(f);
        test_blend_and_buffers(f);
    }
    if (test::g_failures) return 1;
    std::printf("gfx_device_test: ok\n");
    return 0;
}
