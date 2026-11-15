#include "ngl_gfx.h"

#include <algorithm>
#include <atomic>
#include <cfenv>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "gfx_state.h"
#include "mesh_smooth.h"
#include "ngl.h"
#include "texture_decode.h"
#include "texture_hash.h"
#include "title_data.h"

namespace game {
namespace {

using render::GfxState;

// D3D's tables in title memory (the inlined setters write them).
constexpr uint32_t kRenderStates = 0x003f8328;  // D3DRS_*, 166 words
constexpr uint32_t kRenderStateCount = 166;
constexpr uint32_t kStageStates = 0x003f5c68;  // D3DTSS_*, 32 words a stage
constexpr uint32_t kStageStride = 0x80;

// D3D render states read here (Xbox D3DRS numbering).
constexpr uint32_t kRsFogEnable = 92, kRsFogTableMode = 93, kRsFogStart = 94, kRsFogEnd = 95, kRsFogDensity = 96;
constexpr uint32_t kRsFogColor = 138, kRsFillMode = 139, kRsZEnable = 143, kRsStencilEnable = 144;
constexpr uint32_t kRsStencilFail = 145, kRsFrontFace = 146, kRsCullMode = 147;
// D3D texture stage states.
constexpr uint32_t kTssAddressU = 0, kTssAddressV = 1, kTssAddressW = 2, kTssMagFilter = 3, kTssMinFilter = 4;
constexpr uint32_t kTssMipFilter = 5, kTssLodBias = 6, kTssMaxMipLevel = 7, kTssMaxAnisotropy = 8;
constexpr uint32_t kTssAlphaKill = 11, kTssBorderColor = 29;

struct Recorded {
    GfxState state;
    uint32_t render_states[kRenderStateCount] = {};
    uint32_t stage_states[render::kGfxStages][32] = {};
    bool offset_fill = false;  // NV2A polygon offset (fill) enable
    float offset_scale = 0.0f, offset_bias = 0.0f;
    std::unordered_map<uint32_t, uint32_t> keys;  // shader program -> key
};

Recorded fresh_recorded() {
    Recorded init;
    init.render_states[kRsFrontFace] = 0x900;  // D3D's defaults
    init.render_states[kRsCullMode] = 0x901;
    init.render_states[kRsFillMode] = 0x1B02;
    init.render_states[kRsZEnable] = 1;
    for (auto& stage : init.stage_states) {
        stage[kTssAddressU] = stage[kTssAddressV] = stage[kTssAddressW] = 1;
        stage[kTssMagFilter] = stage[kTssMinFilter] = 1;
        stage[kTssMaxAnisotropy] = 1;
    }
    return init;
}

Recorded& recorded() {
    static Recorded r = fresh_recorded();
    return r;
}

float as_float(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

void unpack_color(uint32_t color, float* out) {
    out[0] = (color >> 16 & 0xFF) / 255.0f;
    out[1] = (color >> 8 & 0xFF) / 255.0f;
    out[2] = (color & 0xFF) / 255.0f;
    out[3] = (color >> 24) / 255.0f;
}

// D3D's pixel shader definition fields used here (D3DPIXELSHADERDEF).
struct PixelDefinition {
    uint32_t alpha_inputs[8], final_abcd, final_efg, constant0[8], constant1[8], alpha_outputs[8], rgb_inputs[8];
    uint32_t compare_mode, final_constant0, final_constant1, rgb_outputs[8], combiner_count, texture_modes;
    uint32_t dot_mapping, input_texture, c0_mapping, c1_mapping, final_constants_mapping;
};
static_assert(sizeof(PixelDefinition) == 0xF0);

const PixelDefinition* definition(uint32_t address) {
    return reinterpret_cast<const PixelDefinition*>(static_cast<uintptr_t>(address));
}

// The NV2A method a simple state sends, as GfxState.
void record_method(uint32_t method, uint32_t value) {
    Recorded& r = recorded();
    render::BlendState& b = r.state.blend;
    render::DepthStencilState& d = r.state.depth_stencil;
    switch (method & 0x1FFC) {
        case 0x300: b.alpha_test = value != 0; break;
        case 0x304: b.enable = value != 0; break;
        case 0x338: r.offset_fill = value != 0; break;
        case 0x33C: b.alpha_func = render::gfx_compare(value); break;
        case 0x340: b.alpha_ref = static_cast<uint8_t>(value); break;
        case 0x344: b.src = render::gfx_blend(value); break;
        case 0x348: b.dst = render::gfx_blend(value); break;
        case 0x34C: b.constant = value; break;
        case 0x350: b.op = render::gfx_blend_op(value); break;
        case 0x354: d.depth_func = render::gfx_compare(value); break;
        case 0x358: b.write_mask = render::gfx_color_mask(value); break;
        case 0x35C: d.depth_write = value != 0; break;
        case 0x360: d.stencil_write_mask = static_cast<uint8_t>(value); break;
        case 0x364: d.stencil_func = render::gfx_compare(value); break;
        case 0x368: d.stencil_ref = static_cast<uint8_t>(value); break;
        case 0x36C: d.stencil_read_mask = static_cast<uint8_t>(value); break;
        case 0x374: d.stencil_depth_fail = render::gfx_stencil_op(value); break;
        case 0x378: d.stencil_pass = render::gfx_stencil_op(value); break;
        case 0x37C: r.state.raster.flat_shading = value == 0x1D00; break;
        case 0x384: r.offset_scale = as_float(value); break;
        case 0x388: r.offset_bias = as_float(value); break;
        default: break;  // dither and the like: nothing of ours
    }
}

// Ours (2.0 sun shadows): a draw of the shadow scene's casters (alpha only, into the shadow texture).
bool shadow_caster_draw(const GfxState& s) {
    if (s.blend.write_mask != render::kColorAlpha || !s.color_target) return false;
    const auto* words = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(s.color_target));
    return words[1] == game::renderer_pc_hooks().shadow_target_data;
}

// Fields derived from D3D's tables and the polygon offset, as D3D does it (src/render/draw.cpp's
// apply_fog_states for the fog).
void derive() {
    Recorded& r = recorded();
    GfxState& s = r.state;
    const uint32_t* rs = r.render_states;
    s.depth_stencil.depth_test = rs[kRsZEnable] != 0;
    s.depth_stencil.stencil = rs[kRsStencilEnable] != 0;
    s.depth_stencil.stencil_fail = render::gfx_stencil_op(rs[kRsStencilFail]);
    s.raster.cull = render::gfx_cull(rs[kRsCullMode], rs[kRsFrontFace]);
    if (game::renderer_pc_hooks().shadow_casters_double_sided && shadow_caster_draw(s))
        s.raster.cull = render::Cull::None;  // ours (2.0): sun shadow casters
    s.raster.fill = render::gfx_fill(rs[kRsFillMode]);
    s.raster.depth_bias = r.offset_fill ? r.offset_bias : 0.0f;
    s.raster.slope_bias = r.offset_fill ? r.offset_scale : 0.0f;
    s.fog_enable = rs[kRsFogEnable] != 0;
    s.fog.color = rs[kRsFogColor];
    switch (rs[kRsFogTableMode]) {
        case 0: s.fog.mode = render::FogMode::Linear, s.fog.bias = 1.0f, s.fog.scale = 1.0f; break;
        case 3: {
            const float start = as_float(rs[kRsFogStart]), end = as_float(rs[kRsFogEnd]);
            const float inverse = end == start ? 8192.0f : 1.0f / (end - start);
            s.fog.mode = render::FogMode::Linear, s.fog.bias = end * inverse + 1.0f, s.fog.scale = -inverse;
            break;
        }
        case 1:
            s.fog.mode = render::FogMode::Exp, s.fog.bias = 1.5f, s.fog.scale = as_float(rs[kRsFogDensity]) * -0.090168074f;
            break;
        default:
            s.fog.mode = render::FogMode::Exp2, s.fog.bias = 1.5f, s.fog.scale = as_float(rs[kRsFogDensity]) * -0.21233003f;
            break;
    }
    if (!s.fog_enable) s.fog.mode = render::FogMode::Off;
    for (uint32_t stage = 0; stage < render::kGfxStages; ++stage) {
        const uint32_t* t = r.stage_states[stage];
        render::SamplerState& sampler = s.samplers[stage];
        sampler.u = render::gfx_address(t[kTssAddressU]);
        sampler.v = render::gfx_address(t[kTssAddressV]);
        sampler.w = render::gfx_address(t[kTssAddressW]);
        sampler.mag = render::gfx_filter(t[kTssMagFilter]);
        sampler.min = render::gfx_filter(t[kTssMinFilter]);
        sampler.mip = render::gfx_filter(t[kTssMipFilter]);
        sampler.lod_bias = as_float(t[kTssLodBias]);
        sampler.max_level = t[kTssMaxMipLevel];
        sampler.anisotropy = t[kTssMaxAnisotropy];
        sampler.border = t[kTssBorderColor];
        sampler.alpha_kill = t[kTssAlphaKill] != 0;
    }
}

void record_pixel_definition(uint32_t address) {
    GfxState& s = recorded().state;
    s.pixel_definition = address;
    const auto key = recorded().keys.find(address);
    s.pixel_key = key != recorded().keys.end() ? key->second : 0;
    if (!address) return;
    // Its constants become the combiner factors (as D3D loads the definition).
    const PixelDefinition* d = definition(address);
    for (uint32_t i = 0; i < 8; ++i) {
        unpack_color(d->constant0[i], s.ps_constants[i]);
        unpack_color(d->constant1[i], s.ps_constants[8 + i]);
    }
    unpack_color(d->final_constant0, s.ps_constants[16]);
    unpack_color(d->final_constant1, s.ps_constants[17]);
}

template <typename Fn>
Fn* d3d(uint32_t address) {
    return title_function<Fn>(address);
}

// --- Draws of ours -------------------------------------------------------------------------

uint32_t g_frame = 0;  // frames shown (ngl_gfx_end_frame)

// A title texture as ours: decoded from the title memory its header names and kept current with
// that memory (what the title writes there later: movie frames, ...), or a replacement picture
// of the PC's for its contents (src/render/texture_hash.h). Change checks as the Xbox path's
// (src/render/d3d9_draw.cpp): sampled every frame it is drawn, all of it now and then.
struct TitleTexture {
    render::TextureId own = 0;    // decoded from title memory
    render::TextureId shown = 0;  // own or a replacement
    uint64_t content = 0, sample = 0;
    uint32_t checked_frame = 0, changed_frame = 0, used_frame = 0;
    uint32_t checks = 0;  // frames it was checked (all of it every kFullCheckFrames)
};
constexpr uint32_t kUnusedTextureFrames = 600;  // then ours goes (made again if drawn again)

// What the device holds for the recorded state, made once (cleared when the device changes).
struct DeviceObjects {
    render::GfxDevice* device = nullptr;
    std::unordered_map<uint64_t, render::ShaderId> vertex_shaders, pixel_shaders;  // by variant, key
    std::unordered_map<uint32_t, std::pair<render::LayoutId, uint32_t>> layouts;  // declaration -> layout, stride
    std::unordered_map<uint64_t, render::TargetId> targets;                         // by surface data, size, format
    std::unordered_map<uint32_t, render::TargetId> colour_targets;                  // by surface data
    std::unordered_map<uint64_t, TitleTexture> textures;  // by data, format, size and palette data
    std::set<uint32_t> unsupported_formats;               // warned about once each
    std::vector<uint8_t> decoded;                         // a level for update_texture
    // Ours (2.0 sun shadows): the depth buffer the shadow casters test against, by the shadow target's size,
    // and the frame it was last cleared.
    std::unordered_map<uint64_t, render::TargetId> sun_depth;
    uint32_t sun_depth_frame = ~0u;
    render::TextureId sun_mask = 0;  // RendererPcHooks::sun_mask as a texture, of sun_mask_key
    uint32_t sun_mask_key = 0;
    uint64_t stage_content[render::kGfxStages] = {};  // apply(): the bound title textures' content hashes (2.2)
};

DeviceObjects& device_objects() {
    static DeviceObjects o;
    return o;
}

DeviceObjects& objects(render::GfxDevice* device) {
    DeviceObjects& o = device_objects();
    if (o.device != device) {
        o = DeviceObjects();
        o.device = device;
    }
    return o;
}

void cannot(const std::string& why) {
    static std::set<std::string> told;
    if (told.insert(why).second) std::fprintf(stderr, "[ngl_gfx] drawn by the Xbox path: %s\n", why.c_str());
}

// An Xbox D3DVSDT vertex attribute format as ours, with its size; false if there is none.
bool attribute_type(uint32_t format, render::AttributeType& type, uint32_t& size) {
    using render::AttributeType;
    switch (format) {
        case 0x12: type = AttributeType::Float1, size = 4; return true;
        case 0x22: type = AttributeType::Float2, size = 8; return true;
        case 0x32: type = AttributeType::Float3, size = 12; return true;
        case 0x42: type = AttributeType::Float4, size = 16; return true;
        case 0x40: type = AttributeType::Color, size = 4; return true;
        case 0x25: type = AttributeType::Short2, size = 4; return true;
        case 0x45: type = AttributeType::Short4, size = 8; return true;
        case 0x21: type = AttributeType::Short2N, size = 4; return true;
        case 0x41: type = AttributeType::Short4N, size = 8; return true;
        case 0x44: type = AttributeType::UByte4N, size = 4; return true;  // PBYTE4
        case 0x16: type = AttributeType::UByte4, size = 4; return true;   // NORMPACKED3: the shader unpacks it
        default: return false;
    }
}

// The declaration's layout (VERTEXSHADERINPUT x 16: stream, offset, format, ...) and the stride of
// inline vertex data (the furthest attribute end, as D3D sends it).
bool layout_for(DeviceObjects& o, uint32_t declaration, render::LayoutId& layout, uint32_t& stride) {
    if (auto it = o.layouts.find(declaration); it != o.layouts.end()) {
        layout = it->second.first, stride = it->second.second;
        return layout != 0;
    }
    const auto* inputs = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(declaration));
    render::VertexAttribute attributes[16];
    uint32_t count = 0;
    stride = 0;
    for (uint32_t v = 0; v < 16; ++v) {
        const uint32_t* input = inputs + v * 4;
        if (input[2] == 0x02) continue;  // D3DVSDT_NONE
        render::AttributeType type;
        uint32_t size;
        if (!attribute_type(input[2], type, size) || input[0] != 0) {
            char why[96];
            std::snprintf(why, sizeof(why), "vertex declaration %08x: input v%u format %02x stream %u", declaration, v,
                          input[2], input[0]);
            cannot(why);
            o.layouts[declaration] = {0, 0};
            return false;
        }
        attributes[count++] = {static_cast<uint8_t>(v), 0, type, static_cast<uint16_t>(input[1])};
        stride = std::max(stride, input[1] + size);
    }
    stride = (stride + 3) & ~3u;
    layout = o.device->create_layout({attributes, count});
    o.layouts[declaration] = {layout, stride};
    return layout != 0;
}

// Ours (2.0): the sun shadow map's resolution over the title's 512 x 512 (times the render scale); the square it
// covers grows with it (tr_main.cpp R_RenderTestShadow's kSunRadius).
constexpr uint32_t kShadowTargetScale = 3;

// A title surface header (D3DSurface) as a target of ours at the same memory.
render::TargetId target_for(DeviceObjects& o, uint32_t surface) {
    if (!surface) return 0;
    const auto* words = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(surface));
    const uint32_t data = words[1], format_word = words[3], size = words[4];
    uint32_t width, height;
    if (size) {
        width = (size & 0xFFF) + 1;
        height = (size >> 12 & 0xFFF) + 1;
    } else {  // swizzled: log2 sizes in the format word
        width = 1u << (format_word >> 20 & 0xF);
        height = 1u << (format_word >> 24 & 0xF);
    }
    const uint32_t d3d_format = format_word >> 8 & 0xFF;
    render::Format format;
    switch (d3d_format) {
        case 0x07: case 0x1E: format = render::Format::BGRX8; break;
        case 0x05: case 0x11: format = render::Format::B5G6R5; break;
        case 0x02: case 0x10: format = render::Format::BGR5A1; break;
        case 0x03: case 0x1C: format = render::Format::BGRX5; break;
        case 0x04: case 0x1D: format = render::Format::BGRA4; break;
        case 0x2A: case 0x2B: case 0x2E: case 0x2F: format = render::Format::D24S8; break;
        case 0x2C: case 0x30: format = render::Format::D16; break;
        default: format = render::Format::BGRA8; break;
    }
    const uint64_t key = uint64_t{data} << 32 ^ uint64_t{width} << 20 ^ uint64_t{height} << 8 ^ d3d_format;
    render::TargetId& id = o.targets[key];
    if (!id) {
        // Ours (2.0 sun shadows): the shadow texture at kShadowTargetScale times the resolution.
        const uint32_t extra = data && data == game::renderer_pc_hooks().shadow_target_data ? kShadowTargetScale : 1;
        id = o.device->create_target({format, width, height, data, extra});
        if (id && format != render::Format::D24S8 && format != render::Format::D16) o.colour_targets[data] = id;
    }
    return id;
}

// Ours (2.0 sun shadows): the depth buffer for the shadow casters drawn into `surface` (the same size).
render::TargetId sun_depth_for(DeviceObjects& o, uint32_t surface) {
    const auto* words = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(surface));
    const uint32_t format_word = words[3], size = words[4];
    const uint32_t width = size ? (size & 0xFFF) + 1 : 1u << (format_word >> 20 & 0xF);
    const uint32_t height = size ? (size >> 12 & 0xFFF) + 1 : 1u << (format_word >> 24 & 0xF);
    render::TargetId& id = o.sun_depth[uint64_t{width} << 32 | height];
    if (!id) id = o.device->create_target({render::Format::D24S8, width, height, 0, kShadowTargetScale});
    return id;
}

// The texture standing for a title texture header (D3DTexture) and its palette header (P8, else
// 0) now; 0 if there is none. Memory a render target uses samples the target.
// COD_BRO_TEXTURE_LOG=FILE (developer, phase 1a): each texture content the title draws, once - the runtime hash
// texture packs are keyed by, with its layout (tools/texture_bridge.py maps them to the dump's files).
void log_texture(uint64_t content, uint32_t format, const render::TextureLayout& layout, uint32_t size, bool palette) {
    static FILE* file = [] {
        const char* path = std::getenv("COD_BRO_TEXTURE_LOG");
        FILE* f = path && *path ? std::fopen(path, "w") : nullptr;
        if (f) std::fprintf(f, "content,format,kind,width,height,levels,size,linear,palette,cube\n");
        return f;
    }();
    if (!file) return;
    static std::unordered_set<uint64_t> logged;
    if (!logged.insert(content).second) return;
    std::fprintf(file, "%016llx,%08x,%d,%u,%u,%u,%u,%d,%d,%d\n", static_cast<unsigned long long>(content), format,
                 static_cast<int>(layout.kind), layout.width, layout.height, layout.levels, size, layout.linear ? 1 : 0,
                 palette ? 1 : 0, layout.cube ? 1 : 0);
    std::fflush(file);
}

render::TextureId texture_for(DeviceObjects& o, uint32_t header, uint32_t palette_header, uint64_t* content_out = nullptr);
render::TextureId texture_for_content(DeviceObjects& o, uint32_t header, uint32_t palette_header, uint64_t& content_out);

// The title texture as ours; *content_out = its content hash (texture_hash.h; 0 for render targets / unknown).
render::TextureId texture_for(DeviceObjects& o, uint32_t header, uint32_t palette_header, uint64_t* content_out) {
    uint64_t content = 0;
    const render::TextureId id = texture_for_content(o, header, palette_header, content);
    if (content_out) *content_out = content;
    return id;
}

render::TextureId texture_for_content(DeviceObjects& o, uint32_t header, uint32_t palette_header,
                                      uint64_t& content_out) {
    const auto* words = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(header));
    const uint32_t data = words[1], format = words[3], size_field = words[4];
    if (!format) return 0;
    render::TextureLayout layout;
    const bool described = render::describe_texture(format, size_field, layout);
    if (auto it = o.colour_targets.find(data); it != o.colour_targets.end())
        return o.device->target_texture(it->second, described ? layout.linear : true);
    if (!described) {
        if (o.unsupported_formats.insert(format).second)
            std::fprintf(stderr, "[ngl_gfx] texture format register %08x not supported yet\n", format);
        return 0;
    }
    const uint32_t* palette = nullptr;
    uint32_t palette_data = 0;
    if (layout.color_format == 0x0B && palette_header) {  // P8
        palette_data = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(palette_header))[1];
        palette = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(palette_data | 0x80000000u));
    }
    const uint32_t key_words[4] = {data, format, size_field, palette_data};
    const uint64_t key = render::hash_bytes(key_words, sizeof(key_words));
    TitleTexture& t = o.textures[key];
    t.used_frame = g_frame;
    content_out = t.content;
    if (t.shown && t.checked_frame == g_frame) return t.shown;
    t.checked_frame = g_frame;

    const auto* memory = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(data | 0x80000000u));
    const uint32_t size = static_cast<uint32_t>(layout.total_size);
    uint64_t sample = render::sample_texture(memory, size);
    if (palette) sample = render::hash_bytes(palette, 256 * 4, sample);
    const bool full_check = !t.shown || sample != t.sample || g_frame - t.changed_frame < render::kHotFrames ||
                            ++t.checks % render::kFullCheckFrames == 0;
    t.sample = sample;
    if (!full_check) return t.shown;
    uint64_t content = render::hash_bytes(memory, size);
    if (palette) content = render::hash_bytes(palette, 256 * 4, content);
    log_texture(content, format, layout, size, palette != nullptr);
    // Replacements are for swizzled pictures without a palette (fonts, button prompts).
    const render::TextureId replacement =
        layout.linear || palette ? 0 : o.device->replacement_texture(content, layout.levels > 1);
    if (t.shown && content == t.content && t.shown == (replacement ? replacement : t.own)) return t.shown;
    if (t.shown) t.changed_frame = g_frame;
    t.content = content;
    content_out = content;
    if (replacement) return t.shown = replacement;

    if (!t.own) {
        render::TextureDesc desc;
        switch (layout.kind) {
            case render::TextureLayout::Kind::Dxt1: desc.format = render::Format::DXT1; break;
            case render::TextureLayout::Kind::Dxt3: desc.format = render::Format::DXT3; break;
            case render::TextureLayout::Kind::Dxt5: desc.format = render::Format::DXT5; break;
            default: desc.format = render::Format::BGRA8; break;
        }
        desc.width = layout.width;
        desc.height = layout.height;
        desc.levels = layout.levels;
        desc.texel_coordinates = layout.linear;
        t.own = o.device->create_texture(desc);
        if (!t.own) return t.shown = 0;
    }
    const bool blocks = layout.kind != render::TextureLayout::Kind::Argb;
    const uint32_t block_bytes = layout.kind == render::TextureLayout::Kind::Dxt1 ? 8 : 16;
    for (uint32_t level = 0; level < layout.levels; ++level) {
        const uint32_t w = render::level_width(layout, level), h = render::level_height(layout, level);
        const uint32_t pitch = blocks ? (w + 3) / 4 * block_bytes : w * 4;
        const uint32_t rows = blocks ? (h + 3) / 4 : h;
        o.decoded.resize(size_t{pitch} * rows);
        render::decode_level(layout, level, memory, palette, o.decoded.data(), pitch);
        o.device->update_texture(t.own, level, 0, o.decoded.data(), pitch, 0);
    }
    o.device->label_texture(t.own, content);
    return t.shown = t.own;
}

// Ours of the title textures not drawn for a while.
void drop_unused_textures(DeviceObjects& o) {
    for (auto it = o.textures.begin(); it != o.textures.end();) {
        if (g_frame - it->second.used_frame > kUnusedTextureFrames) {
            if (it->second.own) o.device->destroy_texture(it->second.own);
            it = o.textures.erase(it);
        } else {
            ++it;
        }
    }
}

// Whether a vertex declaration has packed normals (NORMPACKED3): its vertex programs then take the
// variant made for it.
bool has_packed_normals(uint32_t declaration) {
    const auto* inputs = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(declaration));
    for (uint32_t v = 0; v < 16; ++v)
        if (inputs[v * 4 + 2] == 0x16) return true;
    return false;
}

render::ShaderId shader_for(DeviceObjects& o, render::ShaderStage stage, uint32_t key, uint32_t variant = 0) {
    auto& cache = stage == render::ShaderStage::Vertex ? o.vertex_shaders : o.pixel_shaders;
    const uint64_t id = uint64_t{variant} << 32 | key;
    auto it = cache.find(id);
    if (it == cache.end()) it = cache.emplace(id, key ? o.device->create_title_shader(stage, key, variant) : 0).first;
    return it->second;
}

// --diag marks (GfxDevice::dumping): every draw of the frame after a mark to <mark>_draws.txt, then pictures of
// the colour targets it drew into besides the frame (<mark>_target_<memory>.bmp; the sun shadow map also decoded
// as depth, <mark>_shadow_depth.bmp, and the sun mask, <mark>_sun_mask.bmp).
struct DrawDump {
    std::FILE* file = nullptr;
    std::string done_for;  // the prefix already written (dumping() stays true until the device's frame moves)
    uint32_t index = 0;
    std::set<uint32_t> targets;  // title surface headers drawn into
};
DrawDump& draw_dump() {
    static DrawDump d;
    return d;
}

bool write_bmp(const std::string& path, const uint32_t* pixels, uint32_t width, uint32_t height) {
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const uint32_t bytes = width * height * 4;
    uint8_t header[54] = {'B', 'M'};
    const auto put = [&](size_t at, uint32_t v) { std::memcpy(header + at, &v, 4); };
    put(2, 54 + bytes);
    put(10, 54);
    put(14, 40);
    put(18, width);
    put(22, static_cast<uint32_t>(-static_cast<int32_t>(height)));  // top-down
    header[26] = 1;
    header[28] = 32;
    put(34, bytes);
    std::fwrite(header, 1, sizeof header, f);
    std::fwrite(pixels, 1, bytes, f);
    std::fclose(f);
    return true;
}

uint32_t surface_data(uint32_t surface) {
    return surface ? reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(surface))[1] : 0;
}

void dump_draw(render::GfxDevice& device, const GfxState& s, uint32_t pixel_key, bool sun_caster,
               const game::RendererPcHooks& hooks) {
    DrawDump& d = draw_dump();
    const std::string prefix = device.dump_file("");
    if (prefix == d.done_for) return;
    if (!d.file) {
        d.file = std::fopen(device.dump_file("draws.txt").c_str(), "w");
        if (!d.file) {
            d.done_for = prefix;
            return;
        }
        d.index = 0;
        d.targets.clear();
        std::fprintf(d.file, "frame %u; sun shadows: depth %d target %08x reach %.0f centre (%.0f %.0f) mask %ux%u key %u\n",
                     g_frame, hooks.shadow_depth ? 1 : 0, hooks.shadow_target_data, hooks.shadow_reach,
                     hooks.shadow_reach_center[0], hooks.shadow_reach_center[1], hooks.sun_mask_width,
                     hooks.sun_mask_height, hooks.sun_mask_key);
        const float* c = hooks.pixel_override_constants;
        std::fprintf(d.file, "  receiver c0 (%g %g %g %g) c1 (%g %g %g %g) c2 (%g %g %g %g)\n", c[0], c[1], c[2], c[3],
                     c[4], c[5], c[6], c[7], c[8], c[9], c[10], c[11]);
        std::fprintf(d.file, "#n target(memory) vs ps [ours] mask blend depth(test write func) cull stencil textures(memory)\n");
    }
    d.targets.insert(s.color_target);
    std::fprintf(d.file, "%u %08x vs %08x ps %08x%s m%x b%d d%d%d%d c%d s%d t", d.index++, surface_data(s.color_target),
                 s.vertex_key, pixel_key, sun_caster ? " [caster]" : pixel_key != s.pixel_key ? " [receiver]" : "",
                 s.blend.write_mask, s.blend.enable ? 1 : 0, s.depth_stencil.depth_test ? 1 : 0,
                 s.depth_stencil.depth_write ? 1 : 0, static_cast<int>(s.depth_stencil.depth_func),
                 static_cast<int>(s.raster.cull), s.depth_stencil.stencil ? 1 : 0);
    for (uint32_t stage = 0; stage < render::kGfxStages; ++stage)
        std::fprintf(d.file, " %08x", s.textures[stage] ? surface_data(s.textures[stage]) : 0);
    std::fprintf(d.file, "\n");
}

// After the dumped frame: the pictures, then the file closed.
void finish_draw_dump(render::GfxDevice& device, DeviceObjects& o) {
    DrawDump& d = draw_dump();
    if (!d.file) return;
    std::fprintf(d.file, "%u draws\n", d.index);
    std::fclose(d.file);
    d.file = nullptr;
    d.done_for = device.dump_file("");
    const game::RendererPcHooks& hooks = game::renderer_pc_hooks();
    for (uint32_t surface : d.targets) {
        const auto* words = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(surface));
        const uint32_t format_word = words[3], size = words[4], data = words[1];
        const uint32_t width = size ? (size & 0xFFF) + 1 : 1u << (format_word >> 20 & 0xF);
        const uint32_t height = size ? (size >> 12 & 0xFFF) + 1 : 1u << (format_word >> 24 & 0xF);
        if (width >= 640) continue;  // the frame: the mark's own picture
        const render::TargetId target = target_for(o, surface);
        const uint32_t w = width * 2, h = height * 2;
        std::vector<uint32_t> pixels(size_t{w} * h);
        if (!target || !device.read_target(target, pixels.data(), w, h)) continue;
        char name[48];
        std::snprintf(name, sizeof name, "target_%08x.bmp", data);
        write_bmp(device.dump_file(name), pixels.data(), w, h);
        if (data != hooks.shadow_target_data || !hooks.shadow_depth) continue;
        // Packed 1 - depth (sun_common.glsl) as grey: white = nearest to the sun, black = no caster.
        for (uint32_t& p : pixels) {
            const float e = ((p >> 16 & 0xFF) + (p >> 8 & 0xFF) / 255.0f + (p & 0xFF) / 65025.0f) / 255.0f;
            const uint32_t g = e > 0.0f ? 64 + static_cast<uint32_t>(std::min(1.0f, e) * 191.0f) : 0;
            p = 0xFF000000u | g << 16 | g << 8 | g;
        }
        write_bmp(device.dump_file("shadow_depth.bmp"), pixels.data(), w, h);
    }
    if (hooks.sun_mask && hooks.sun_mask_width) {
        std::vector<uint32_t> mask(size_t{hooks.sun_mask_width} * hooks.sun_mask_height);
        for (size_t i = 0; i < mask.size(); ++i) {
            const uint32_t g = hooks.sun_mask[i];
            mask[i] = 0xFF000000u | g << 16 | g << 8 | g;
        }
        write_bmp(device.dump_file("sun_mask.bmp"), mask.data(), hooks.sun_mask_width, hooks.sun_mask_height);
    }
}

// Developers (phase 2 tools, 2.1a shader ports): COD_BRO_SHADER_AB=own|self with a dumped frame (F12 mark,
// COD_BRO_DUMP_FRAME): every draw whose shaders are ours ("own"; "self": every draw, title against title, to
// check the harness) is drawn twice more before its real draw, into a cleared scratch target of its target's
// size: once with the title's shaders and constants, once with ours. Both read back and compared:
// <prefix>_shader_ab.csv (draw index as in _draws.txt), <prefix>_shader_ab_worst_{title,ours}.bmp.
struct ShaderAb {
    // apply()'s choice for the draw being made
    bool ours = false, reapplying = false, model = false;
    render::ShaderId vs = 0, ps = 0, title_vs = 0, title_ps = 0;
    uint32_t vertex_key = 0, title_pixel_key = 0, pixel_key = 0, color_surface = 0;
    // the frame's results
    std::FILE* file = nullptr;
    std::string done_for;
    uint32_t draws = 0, differing = 0, worst = 0;
    std::vector<uint32_t> worst_a, worst_b;
    uint32_t worst_width = 0, worst_height = 0;
    std::unordered_map<uint64_t, std::pair<render::TargetId, render::TargetId>> scratch;  // by size
};
ShaderAb& shader_ab() {
    static ShaderAb ab;
    return ab;
}
// 0: off, 1: draws with our shaders, 2: every draw (title against title).
int shader_ab_mode() {
    static const int mode = [] {
        const char* env = std::getenv("COD_BRO_SHADER_AB");
        if (!env || !*env || *env == '0') return 0;
        return std::strcmp(env, "self") == 0 ? 2 : 1;
    }();
    return mode;
}
constexpr uint32_t kAbSentinel = 0x01FE02FDu;  // the scratch's clear colour: "not drawn"

// Ours (engine improvements 2.1): the lit model classes' programs with GLSL ports of our own
// (tools/shader_glsl.py; vk/shaders/model_shaders.csv lists the same keys).
bool own_model_vertex(uint32_t key) {
    switch (key) {
    case 0x00899a00: case 0x00899d70: case 0x0089b030: case 0x0089b7c0: case 0x0089c4d0: case 0x0089ca40:
        return true;
    default:
        return false;
    }
}
bool own_model_pixel(uint32_t key) {
    switch (key) {
    case 0x00899b48: case 0x00899c40: case 0x00899eb8: case 0x00899fb0: case 0x0089b2a8: case 0x0089b3a0:
    case 0x0089baa8: case 0x0089bba0: case 0x0089c628: case 0x0089c720: case 0x0089cb88: case 0x0089cc80:
        return true;
    default:
        return false;
    }
}
uint32_t g_lighting_features = 0;  // RendererPcHooks::lighting_features of this frame (model_shader_mode)
// Per frame: 0 = the title's shaders, else bit 0 pixel lighting, bit 1 sun shadow on models, bit 2 forced
// (COD_BRO_MODEL_SHADERS=1: ours with both off - the 2.1a check that they equal the title's).
uint32_t model_shader_mode(const game::RendererPcHooks& hooks) {
    static uint32_t frame = ~0u, mode = 0;
    uint32_t& features = g_lighting_features;
    if (frame != g_frame) {
        frame = g_frame;
        static const bool forced = [] {
            const char* env = std::getenv("COD_BRO_MODEL_SHADERS");
            return env && *env == '1';
        }();
        mode = 0;
        features = 0;
        if (hooks.lighting_features) {  // set on Vulkan only
            features = hooks.lighting_features();
            if (features & game::kLightingPixel) mode |= 1;
            if (forced) mode |= 4;
        }
    }
    uint32_t m = mode;
    if (hooks.shadow_depth && hooks.shadow_texture) m |= 2;
    return m;
}

void bind_sun_mask(render::GfxDevice& device, DeviceObjects& o, const game::RendererPcHooks& hooks) {
    if (hooks.sun_mask && hooks.sun_mask_key != o.sun_mask_key) {
        device.destroy_texture(o.sun_mask);
        o.sun_mask = device.create_texture({render::TextureType::Tex2D, render::Format::L8, hooks.sun_mask_width,
                                            hooks.sun_mask_height, 1, 1, false});
        if (o.sun_mask) device.update_texture(o.sun_mask, 0, 0, hooks.sun_mask, hooks.sun_mask_width, 0);
        o.sun_mask_key = hooks.sun_mask_key;
    }
    render::SamplerState clamp;
    clamp.u = clamp.v = clamp.w = render::Address::Clamp;
    device.set_texture(4, hooks.sun_mask ? o.sun_mask : 0);
    device.set_sampler(4, clamp);
}

// Surface maps (2.2) for the texture on stage 0: t6 the normal map, t7 roughness (R) + height (G), sampled as
// that texture (addressing), trilinear; `present` = 1 when both are bound.
void bind_surface_maps(render::GfxDevice& device, DeviceObjects& o, float& present) {
    const uint64_t content = o.stage_content[0];
    const render::TextureId normal = content ? device.surface_map_texture(content, 0) : 0;
    const render::TextureId surface = normal ? device.surface_map_texture(content, 1) : 0;
    present = normal && surface ? 1.0f : 0.0f;
    render::SamplerState wrap = ngl_gfx_state()->samplers[0];  // as the texture they belong to
    wrap.alpha_kill = false;
    wrap.min = wrap.mag = wrap.mip = render::Filter::Linear;
    wrap.max_level = 0;
    wrap.lod_bias = 0.0f;
    device.set_texture(6, present != 0.0f ? normal : 0);
    device.set_texture(7, present != 0.0f ? surface : 0);
    device.set_sampler(6, wrap);
    device.set_sampler(7, wrap);
}

// Our model shaders' constants (own_model.glsl) and textures: t4 the sun mask, t5 the shadow map.
// World units the receiver moves off its surface against the model's own coarse caster copy (acne): lab orbit m2
// 11-15, speckle per pose 0 / 8 / 16 units: turn 135 0.22 / 0.08 / 0.07 (reference without model receivers 0.07).
constexpr float kModelNormalOffset = 8.0f;
// 2.3: normal maps at full strength; specular "on at low strength" (user 11-15): GGX x this.
constexpr float kModelNormalMapStrength = 1.0f;
constexpr float kModelSpecularStrength = 0.35f;
void set_model_constants(render::GfxDevice& device, DeviceObjects& o, const game::RendererPcHooks& hooks,
                         uint32_t mode) {
    alignas(16) float own[render::kGfxVsOwnCount][4] = {};
    const game::RendererPcHooks::ModelLights& m = hooks.model;
    const bool shadow = (mode & 2) && m.sun >= 0;
    own[0][0] = (mode & 1) ? 1.0f : 0.0f;
    own[0][1] = static_cast<float>(shadow ? m.sun : -1);
    own[0][2] = shadow ? 1.0f : 0.0f;
    own[0][3] = static_cast<float>(hooks.debug_view);
    std::memcpy(own[1], m.to_world, sizeof m.to_world);
    if (shadow) {
        const float* p = hooks.shadow_projector;  // u v z w = x p[0..3] + y p[4..7] + z p[8..11] + p[12..15]
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k) own[4 + row][k] = p[k * 4 + row];
        own[7][0] = own[7][1] = own[7][2] = 0.0f;  // w = 1 (the world receivers' c14)
        own[7][3] = 1.0f;
        std::memcpy(own[8], hooks.pixel_override_constants, 12 * sizeof(float));  // c0 c1 c2 (sun_world.glsl)
    }
    own[11][0] = static_cast<float>(m.count);
    own[11][1] = kModelNormalOffset;
    std::memcpy(own[12], m.lights, sizeof m.lights);
    // Surface maps of the model's texture (stage 0): t6 normal, t7 roughness + height (2.2); own[0].w's views and
    // own[11].w = 1 when both are there.
    bind_surface_maps(device, o, own[11][3]);
    // own[28]: (normal map strength, specular strength, cavity strength, 0) - per pixel only, with maps (2.3).
    if ((mode & 1) && own[11][3] != 0.0f) {
        own[28][0] = (g_lighting_features & game::kLightingNormalMaps) ? kModelNormalMapStrength : 0.0f;
        own[28][1] = (g_lighting_features & game::kLightingSpecular) ? kModelSpecularStrength : 0.0f;
    }
    device.set_constants(render::ShaderStage::Vertex, render::kGfxVsOwn, &own[0][0], render::kGfxVsOwnCount);
    if (!shadow) return;
    bind_sun_mask(device, o, hooks);
    render::SamplerState border;
    border.u = border.v = border.w = render::Address::Clamp;
    device.set_texture(5, texture_for(o, hooks.shadow_texture, 0));
    device.set_sampler(5, border);
}

// The recorded state onto the device; false if something cannot be ours yet.
bool apply(render::GfxDevice& device, DeviceObjects& o, uint32_t stride) {
    const GfxState& s = *ngl_gfx_state();
    if (!s.vertex_key || !s.pixel_key) {
        cannot(!s.vertex_key ? "no vertex program of ours (fixed function or unknown)"
                             : "no pixel shader of ours (fixed function or unknown)");
        return false;
    }
    const uint32_t variant =
        s.vertex_declaration && has_packed_normals(s.vertex_declaration) ? s.vertex_declaration : 0;
    const render::ShaderId vs = shader_for(o, render::ShaderStage::Vertex, s.vertex_key, variant);
    // Ours (2.0 sun shadows): casters write depth (their draws write alpha only), the receivers compare it.
    const game::RendererPcHooks& hooks = game::renderer_pc_hooks();
    const bool sun_caster = hooks.shadow_depth && shadow_caster_draw(s);
    uint32_t pixel_key = sun_caster ? render::kOwnPsSunCaster : hooks.pixel_override ? hooks.pixel_override : s.pixel_key;
    render::ShaderId ps = shader_for(o, render::ShaderStage::Pixel, pixel_key);
    // Ours (2.1): the lit model classes on our GLSL ports (per-pixel lights, the sun's shadow).
    render::ShaderId model_vs = 0;
    const uint32_t model_mode = !sun_caster && !hooks.pixel_override && own_model_vertex(s.vertex_key) &&
                                own_model_pixel(s.pixel_key) ? model_shader_mode(hooks) : 0;
    if (model_mode) {
        model_vs = shader_for(o, render::ShaderStage::Vertex, s.vertex_key | render::kOwnModelShader, variant);
        const render::ShaderId model_ps =
            model_vs ? shader_for(o, render::ShaderStage::Pixel, s.pixel_key | render::kOwnModelShader) : 0;
        if (model_ps) {
            pixel_key = s.pixel_key | render::kOwnModelShader;
            ps = model_ps;
        } else {
            model_vs = 0;
        }
    }
    ShaderAb& ab = shader_ab();
    ab.ours = !sun_caster && pixel_key != s.pixel_key;
    ab.model = model_vs != 0;
    ab.vs = model_vs ? model_vs : vs;
    ab.ps = ps;
    ab.title_vs = vs;
    ab.title_ps = ab.ours ? shader_for(o, render::ShaderStage::Pixel, s.pixel_key) : ps;
    ab.vertex_key = s.vertex_key;
    ab.title_pixel_key = s.pixel_key;
    ab.pixel_key = pixel_key;
    ab.color_surface = s.color_target;
    render::LayoutId layout;
    uint32_t layout_stride;
    if (!vs || !ps || !layout_for(o, s.vertex_declaration, layout, layout_stride)) return false;
    if (layout_stride != stride) {
        char why[96];
        std::snprintf(why, sizeof(why), "vertex declaration %08x: stride %u, the draw's %u", s.vertex_declaration,
                      layout_stride, stride);
        cannot(why);
        return false;
    }
    const render::TargetId color = target_for(o, s.color_target);
    if (!color) return false;
    if (sun_caster && !shader_ab().reapplying) ++game::renderer_pc_hooks().casters_this_frame;
    if (sun_caster) {
        const render::TargetId depth = sun_depth_for(o, s.color_target);
        if (!depth) return false;
        device.set_target(color, depth);
        if (o.sun_depth_frame != g_frame) {  // the first caster of the frame
            o.sun_depth_frame = g_frame;
            device.clear(render::kGfxClearDepth | render::kGfxClearStencil, 0, 1.0f, 0);
        }
        render::BlendState blend = s.blend;
        blend.enable = false;
        blend.write_mask = render::kColorAll;
        render::DepthStencilState depth_state;
        depth_state.depth_test = depth_state.depth_write = true;
        depth_state.depth_func = render::Compare::LessEqual;
        device.set_viewport(s.viewport);
        device.set_blend(blend);
        device.set_depth_stencil(depth_state);
    } else {
        device.set_target(color, target_for(o, s.depth_target));
        device.set_viewport(s.viewport);
        device.set_blend(s.blend);
        device.set_depth_stencil(s.depth_stencil);
    }
    device.set_raster(s.raster);
    device.set_fog(s.fog);
    for (uint32_t stage = 0; stage < render::kGfxStages; ++stage) {
        o.stage_content[stage] = 0;
        device.set_texture(stage, s.textures[stage] ? texture_for(o, s.textures[stage], s.palettes[stage],
                                                                   &o.stage_content[stage])
                                                    : 0);
        device.set_sampler(stage, s.samplers[stage]);
    }
    device.set_shaders(model_vs ? model_vs : vs, ps);
    device.set_layout(layout);
    device.set_constants(render::ShaderStage::Vertex, 0, &s.vs_constants[0][0], 192);
    device.set_constants(render::ShaderStage::Pixel, 0, &s.ps_constants[0][0], 18);
    if (device.dumping() && !ab.reapplying) dump_draw(device, s, pixel_key, sun_caster, hooks);
    if (model_vs) set_model_constants(device, o, hooks, model_mode);
    if (!sun_caster && hooks.pixel_override) {
        // c3.x: the debug view (RendererPcHooks::debug_view; tools for phase 2).
        alignas(16) float constants[16];
        std::memcpy(constants, hooks.pixel_override_constants, 12 * sizeof(float));
        constants[12] = static_cast<float>(hooks.debug_view);
        constants[13] = constants[14] = constants[15] = 0.0f;
        device.set_constants(render::ShaderStage::Pixel, 0, constants, 4);
        bind_sun_mask(device, o, hooks);  // slot 4 (no world shader uses it)
    }
    return true;
}

}  // namespace

namespace {

template <typename Draw>
void run_shader_ab(render::GfxDevice& device, DeviceObjects& o, uint32_t stride, const Draw& draw) {
    ShaderAb& ab = shader_ab();
    const std::string prefix = device.dump_file("");
    if (prefix == ab.done_for) return;
    if (!ab.file) {
        ab.file = std::fopen(device.dump_file("shader_ab.csv").c_str(), "w");
        if (!ab.file) {
            ab.done_for = prefix;
            return;
        }
        std::fprintf(ab.file, "draw,vertex,title_pixel,pixel,covered_title,covered_ours,differ,max,mean\n");
        ab.draws = ab.differing = ab.worst = 0;
        ab.worst_a.clear();
        ab.worst_b.clear();
    }
    const render::TargetId color = target_for(o, ab.color_surface);
    uint32_t width = 0, height = 0;
    if (!color || !device.target_size(color, width, height)) return;
    // The scratch pair, made at the title target's size (the back end scales it as the real one).
    const auto* words = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(ab.color_surface));
    const uint32_t size = words[4];
    const uint32_t title_w = size ? (size & 0xFFF) + 1 : 1u << (words[3] >> 20 & 0xF);
    const uint32_t title_h = size ? (size >> 12 & 0xFFF) + 1 : 1u << (words[3] >> 24 & 0xF);
    auto& pair = ab.scratch[uint64_t{title_w} << 32 | title_h];
    if (!pair.first) {
        pair.first = device.create_target({render::Format::BGRA8, title_w, title_h, 0, 1});
        pair.second = device.create_target({render::Format::D24S8, title_w, title_h, 0, 1});
    }
    uint32_t scratch_w = 0, scratch_h = 0;
    if (!pair.first || !pair.second || !device.target_size(pair.first, scratch_w, scratch_h)) return;
    const GfxState& s = *ngl_gfx_state();
    const game::RendererPcHooks& hooks = game::renderer_pc_hooks();
    std::vector<uint32_t> a(size_t{scratch_w} * scratch_h), b(a.size());
    const auto pass = [&](bool title, std::vector<uint32_t>& out) {
        device.set_target(pair.first, pair.second);
        device.clear(render::kGfxClearColor | render::kGfxClearDepth | render::kGfxClearStencil, kAbSentinel, 1.0f, 0);
        device.set_shaders(title ? ab.title_vs : ab.vs, title ? ab.title_ps : ab.ps);
        if (title) {
            device.set_constants(render::ShaderStage::Pixel, 0, &s.ps_constants[0][0], 18);
        } else if (ab.ours && !ab.model) {
            alignas(16) float constants[16];
            std::memcpy(constants, hooks.pixel_override_constants, 12 * sizeof(float));
            constants[12] = static_cast<float>(hooks.debug_view);
            constants[13] = constants[14] = constants[15] = 0.0f;
            device.set_constants(render::ShaderStage::Pixel, 0, constants, 4);
        }
        draw();
        device.read_target(pair.first, out.data(), scratch_w, scratch_h);
    };
    const bool self = shader_ab_mode() == 2;  // "self": the title's shaders on both sides
    pass(true, a);
    if (self) {
        pass(true, b);
    } else {
        pass(false, b);
    }
    // Back to the draw's own state for its real draw.
    ab.reapplying = true;
    apply(device, o, stride);
    ab.reapplying = false;

    uint32_t covered_a = 0, covered_b = 0, differ = 0, max = 0;
    uint64_t sum = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        const bool in_a = a[i] != kAbSentinel, in_b = b[i] != kAbSentinel;
        covered_a += in_a;
        covered_b += in_b;
        if (!in_a && !in_b) continue;
        uint32_t d = 255;
        if (in_a && in_b) {
            d = 0;
            for (int shift = 0; shift < 32; shift += 8)
                d = std::max(d, static_cast<uint32_t>(std::abs(static_cast<int>(a[i] >> shift & 0xFF) -
                                                               static_cast<int>(b[i] >> shift & 0xFF))));
        }
        differ += d > 2;
        max = std::max(max, d);
        sum += d;
    }
    const uint32_t covered = std::max(covered_a, covered_b);
    std::fprintf(ab.file, "%u,%08x,%08x,%08x,%u,%u,%u,%u,%.3f\n", draw_dump().index ? draw_dump().index - 1 : 0,
                 ab.vertex_key, ab.title_pixel_key, self ? ab.title_pixel_key : ab.pixel_key, covered_a, covered_b,
                 differ, max, covered ? static_cast<double>(sum) / covered : 0.0);
    ++ab.draws;
    if (differ) ++ab.differing;
    if (differ > ab.worst) {
        ab.worst = differ;
        ab.worst_a = std::move(a);
        ab.worst_b = std::move(b);
        ab.worst_width = scratch_w;
        ab.worst_height = scratch_h;
    }
}

// After the dumped frame: the summary line and the worst draw's two pictures.
void finish_shader_ab(render::GfxDevice& device) {
    ShaderAb& ab = shader_ab();
    if (!ab.file) return;
    std::fprintf(ab.file, "# %u draws compared, %u differ (> 2 levels in a channel)\n", ab.draws, ab.differing);
    std::fclose(ab.file);
    ab.file = nullptr;
    ab.done_for = device.dump_file("");
    std::fprintf(stderr, "[shader_ab] %u draws compared, %u differ -> %sshader_ab.csv\n", ab.draws, ab.differing,
                 ab.done_for.c_str());
    if (ab.worst) {
        for (auto* pixels : {&ab.worst_a, &ab.worst_b})
            for (uint32_t& p : *pixels)
                if (p == kAbSentinel) p = 0xFF000000u;
        write_bmp(device.dump_file("shader_ab_worst_title.bmp"), ab.worst_a.data(), ab.worst_width, ab.worst_height);
        write_bmp(device.dump_file("shader_ab_worst_ours.bmp"), ab.worst_b.data(), ab.worst_width, ab.worst_height);
    }
}

bool env_set(const char* name) {
    const char* env = std::getenv(name);
    return env && *env && *env != '0';
}
}  // namespace

bool ngl_gfx_own_draws() {
    static const bool xbox_draws = env_set("XB_XBOX_DRAWS");
    static const bool alternate = env_set("XB_DRAWS_ALTERNATE");
    return !xbox_draws && (!alternate || (g_frame & 1)) && gfx() != nullptr;
}

void ngl_gfx_end_frame() {
    game::RendererPcHooks& hooks = game::renderer_pc_hooks();
    hooks.casters_last_frame = std::exchange(hooks.casters_this_frame, 0u);
    if (render::GfxDevice* device = gfx(); device && shader_ab().file) finish_shader_ab(*device);
    if (render::GfxDevice* device = gfx(); device && draw_dump().file) finish_draw_dump(*device, objects(device));
    ++g_frame;
    if (g_frame % 60 == 0)
        if (render::GfxDevice* device = gfx()) drop_unused_textures(objects(device));
}

bool ngl_gfx_draw_user(render::Primitive primitive, const void* vertices, uint32_t count, uint32_t stride,
                       const uint16_t* indices, uint32_t index_count, uint64_t cache_key) {
    render::GfxDevice* device = gfx();
    if (!device) return false;
    DeviceObjects& o = objects(device);
    if (!apply(*device, o, stride)) return false;
    if (shader_ab_mode() && device->dumping() && (shader_ab().ours || shader_ab_mode() == 2))
        run_shader_ab(*device, o, stride, [&] { device->draw_user(primitive, vertices, count, stride, indices,
                                                                    index_count, cache_key); });
    device->draw_user(primitive, vertices, count, stride, indices, index_count, cache_key);
    static bool told = false;
    if (!told) {
        told = true;
        std::fprintf(stderr, "[ngl_gfx] first draw of ours (our graphics interface, src/render/gfx_device.h)\n");
    }
    return true;
}

const render::GfxState* ngl_gfx_state() {
    derive();
    return &recorded().state;
}

void ngl_gfx_shader_key(uint32_t program, uint32_t key) { recorded().keys[program] = key; }

namespace xb {

void create_device(uint32_t present_parameters, uint32_t device_out) {
    const GfxDeviceOpener open = gfx_device_opener();
    if (!open) {
        d3d<uint32_t XB_STDCALL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)>(0x003e9cc0)(
            0, 1, 0, 0x40, present_parameters, device_out);
        return;
    }
    // D3DPRESENT_PARAMETERS: width, height, back buffer format and count, multisample type, swap
    // effect, window, auto depth stencil, its format, flags (+40), ...
    const auto* p = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(present_parameters));
    render::DisplayDesc desc;
    desc.width = p[0];
    desc.height = p[1];
    desc.depth = !p[8] ? render::Format::Unknown : p[9] == 0x2c || p[9] == 0x30 ? render::Format::D16 : render::Format::D24S8;
    desc.widescreen = (p[10] & 0x10) != 0;
    desc.tall_pixels = (p[10] & 0x100) != 0;
    uint32_t device = 0;
    open(desc, &device);
    title_global<uint32_t>(device_out) = device;
}

void swap() {
    if (render::GfxDevice* device = gfx())
        device->present();
    else
        d3d<uint32_t XB_STDCALL(uint32_t)>(0x003eabe0)(0);
}

void set_render_state_simple(uint32_t method, uint32_t value) {
    d3d<void XB_FASTCALL(uint32_t, uint32_t)>(0x003e89f0)(method, value);
    record_method(method, value);
}

void set_d3d_state(uint32_t address, uint32_t value) {
    title_global<uint32_t>(address) = value;
    Recorded& r = recorded();
    if (address >= kRenderStates && address < kRenderStates + kRenderStateCount * 4) {
        r.render_states[(address - kRenderStates) / 4] = value;
    } else if (address >= kStageStates && address < kStageStates + render::kGfxStages * kStageStride) {
        const uint32_t offset = address - kStageStates;
        r.stage_states[offset / kStageStride][offset % kStageStride / 4] = value;
    }
}

void set_render_state(uint32_t function, uint32_t value) {
    d3d<void XB_STDCALL(uint32_t)>(function)(value);
    uint32_t* rs = recorded().render_states;
    switch (function) {
        case 0x003e80f0: rs[kRsCullMode] = value; break;
        case 0x003e88a0: rs[kRsFillMode] = value; break;
        case 0x003e8210: rs[kRsZEnable] = value; break;
        case 0x003e82b0: rs[kRsStencilEnable] = value; break;
        case 0x003e8360: rs[kRsStencilFail] = value; break;
        case 0x003e8090: rs[kRsFogColor] = value; break;
        default: break;  // back fill, edge and multisample antialiasing, logic op, YUV, z read: not ours
    }
}

void set_texture(uint32_t stage, uint32_t texture) {
    d3d<void XB_STDCALL(uint32_t, uint32_t)>(0x003e5f70)(stage, texture);
    if (stage < render::kGfxStages) recorded().state.textures[stage] = texture;
}

void switch_texture(uint32_t stage, uint32_t command, uint32_t texture) {
    const auto* words = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(texture));
    d3d<void XB_FASTCALL(uint32_t, uint32_t, uint32_t)>(0x003e6130)(command, words[1], words[3]);
    if (stage < render::kGfxStages) recorded().state.textures[stage] = texture;
}

void set_palette(uint32_t stage, uint32_t palette) {
    d3d<void XB_STDCALL(uint32_t, uint32_t)>(0x003e6160)(stage, palette);
    if (stage < render::kGfxStages) recorded().state.palettes[stage] = palette;
}

void set_border_color(uint32_t stage, uint32_t color) {
    d3d<void XB_STDCALL(uint32_t, uint32_t)>(0x003e8ba0)(stage, color);
    if (stage < render::kGfxStages) recorded().stage_states[stage][kTssBorderColor] = color;
}

void set_pixel_shader_program(uint32_t address) {
    d3d<void XB_STDCALL(uint32_t)>(0x003e77e0)(address);
    record_pixel_definition(address);
}

void load_vertex_program(uint32_t program) {
    d3d<void XB_STDCALL(uint32_t, uint32_t)>(0x003e6ea0)(program, 0);
    GfxState& s = recorded().state;
    s.vertex_program = program;
    const auto key = recorded().keys.find(program);
    s.vertex_key = key != recorded().keys.end() ? key->second : 0;
}

void select_vertex_shader(uint32_t declaration) {
    d3d<void XB_STDCALL(uint32_t, uint32_t)>(0x003e6f40)(declaration, 0);
    if (declaration) recorded().state.vertex_declaration = declaration;
}

void set_vertex_shader(uint32_t handle) {
    d3d<void XB_STDCALL(uint32_t)>(0x003e72b0)(handle);
    GfxState& s = recorded().state;
    s.vertex_program = s.vertex_key = 0;
}

void set_pixel_shader(uint32_t handle) {
    d3d<void XB_STDCALL(uint32_t)>(0x003e7820)(handle);
    GfxState& s = recorded().state;
    s.pixel_definition = s.pixel_key = 0;
    if (handle) {  // {refcount, D3D owned, definition}: as SetPixelShaderProgram
        const uint32_t definition = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(handle))[2];
        record_pixel_definition(definition);
    }
}

void set_render_target(uint32_t color, uint32_t depth, bool fast) {
    if (fast)
        d3d<void XB_STDCALL(uint32_t, uint32_t, uint32_t)>(0x003e55b0)(color, depth, 0);
    else
        d3d<void XB_STDCALL(uint32_t, uint32_t)>(0x003e52b0)(color, depth);
    GfxState& s = recorded().state;
    if (color) s.color_target = color;  // 0: keep the current one (D3D's)
    s.depth_target = depth;
}

void set_viewport(const void* viewport) {
    d3d<void XB_STDCALL(const void*)>(0x003e5cc0)(viewport);
    struct D3dViewport {
        uint32_t x, y, width, height;
        float min_z, max_z;
    } v;
    std::memcpy(&v, viewport, sizeof(v));
    recorded().state.viewport = {v.x, v.y, v.width, v.height, v.min_z, v.max_z};
}

namespace {

// An Xbox D3DPRIMITIVETYPE (= the NV2A's begin values) as ours; false for line loops.
bool primitive_of(uint32_t xbox, render::Primitive& ours) {
    switch (xbox) {
        case 1: ours = render::Primitive::Points; return true;
        case 2: ours = render::Primitive::Lines; return true;
        case 4: ours = render::Primitive::LineStrip; return true;
        case 5: ours = render::Primitive::Triangles; return true;
        case 6: case 9: ours = render::Primitive::TriangleStrip; return true;  // a quad strip is a strip
        case 7: case 10: ours = render::Primitive::TriangleFan; return true;   // a (convex) polygon is a fan
        case 8: ours = render::Primitive::Quads; return true;
        default: return false;
    }
}

// Stream 0's vertex memory (the title's buffer header: data at +4, physical).
const uint8_t* stream_memory() {
    const GfxState& s = recorded().state;
    if (!s.stream_buffer) return nullptr;
    const uint32_t data = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(s.stream_buffer))[1];
    return reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(data | 0x80000000u)) + s.stream_offset;
}

}  // namespace

void draw_vertices_up(uint32_t primitive, uint32_t count, const void* vertices, uint32_t stride) {
    render::Primitive ours;
    if (ngl_gfx_own_draws() && primitive_of(primitive, ours) && ngl_gfx_draw_user(ours, vertices, count, stride))
        return;
    d3d<void XB_STDCALL(uint32_t, uint32_t, const void*, uint32_t)>(0x003e9710)(primitive, count, vertices, stride);
}

void set_vertex_input(uint32_t declaration, uint32_t stream_count, const void* streams) {
    d3d<void XB_STDCALL(uint32_t, uint32_t, const void*)>(0x003e75e0)(declaration, stream_count, streams);
    GfxState& s = recorded().state;
    if (declaration) s.vertex_declaration = declaration;
    if (stream_count && streams) {
        const auto* stream = static_cast<const uint32_t*>(streams);
        s.stream_buffer = stream[0], s.stream_stride = stream[1], s.stream_offset = stream[2];
    } else {
        s.stream_buffer = s.stream_stride = s.stream_offset = 0;
    }
}

void draw_vertices(uint32_t primitive, uint32_t start, uint32_t count) {
    render::Primitive ours;
    const GfxState& s = recorded().state;
    if (ngl_gfx_own_draws() && primitive_of(primitive, ours) && s.stream_stride) {
        if (const uint8_t* memory = stream_memory()) {
            const uint8_t* vertices = memory + start * s.stream_stride;
            const uint64_t key = uint64_t{count} << 32 ^ static_cast<uint32_t>(reinterpret_cast<uintptr_t>(vertices));
            if (ngl_gfx_draw_user(ours, vertices, count, s.stream_stride, nullptr, 0, key)) return;
        }
    }
    d3d<void XB_STDCALL(uint32_t, uint32_t, uint32_t)>(0x003e98c0)(primitive, start, count);
}

void draw_indexed_vertices(uint32_t primitive, uint32_t index_count, const void* indices) {
    render::Primitive ours;
    const GfxState& s = recorded().state;
    if (ngl_gfx_own_draws() && primitive_of(primitive, ours) && s.stream_stride && index_count) {
        if (const uint8_t* memory = stream_memory()) {
            // The vertices the indices use, and the indices from the first of them; quads as
            // triangles (v0 v1 v2, v0 v2 v3).
            const auto* source = static_cast<const uint16_t*>(indices);
            uint16_t low = 0xFFFF, high = 0;
            for (uint32_t i = 0; i < index_count; ++i) {
                low = std::min(low, source[i]);
                high = std::max(high, source[i]);
            }
            static std::vector<uint16_t> rebased;
            rebased.clear();
            if (ours == render::Primitive::Quads) {
                for (uint32_t q = 0; q + 4 <= index_count; q += 4)
                    for (const uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u})
                        rebased.push_back(static_cast<uint16_t>(source[q + i] - low));
                ours = render::Primitive::Triangles;
            } else {
                for (uint32_t i = 0; i < index_count; ++i) rebased.push_back(static_cast<uint16_t>(source[i] - low));
            }
            const uint8_t* vertices = memory + uint32_t{low} * s.stream_stride;
            const uint32_t count = uint32_t{high} - low + 1;
            const uint64_t key = uint64_t{count} << 32 ^ static_cast<uint32_t>(reinterpret_cast<uintptr_t>(vertices));
            if (ngl_gfx_draw_user(ours, vertices, count, s.stream_stride, rebased.data(),
                                  static_cast<uint32_t>(rebased.size()), key))
                return;
        }
    }
    d3d<void XB_STDCALL(uint32_t, uint32_t, const void*)>(0x003e9990)(primitive, index_count, indices);
}

namespace {

// Smoothed sections (draw_smoothed), by where their data is. A sample of the data checks that it is still
// the same (a new level's meshes can load at the same addresses). Smoothing runs once per section on worker
// threads (SmoothWorkers); until a section's result is ready it is drawn as it is.
struct Smoothed {
    uint64_t sample = 0;
    uint64_t id = 0;              // the back end's cache key
    std::atomic<int> state{0};    // kPending, kReady, kRefused
    render::smooth::Result result;  // the workers write it before state becomes kReady
};
constexpr int kPending = 0, kReady = 1, kRefused = 2;

struct SmoothCache {
    std::unordered_map<uint64_t, std::shared_ptr<Smoothed>> sections;  // the drawing thread only
    uint64_t next_id = 1;
};
SmoothCache& smooth_cache() {
    static SmoothCache cache;
    return cache;
}

// What a worker needs: copies of the section's data (title memory is not the workers' to read).
struct SmoothJob {
    std::shared_ptr<Smoothed> entry;
    std::vector<uint8_t> vertices;
    std::vector<uint16_t> indices;  // empty: not indexed
    uint32_t vertex_count = 0, count = 0, stride = 0, attribute_count = 0;
    render::smooth::Attribute attributes[16];
    render::smooth::Topology topology;
    render::smooth::Options options;
};

// The worker threads: half the CPU's threads (at least 1, at most 8), started on first use and never stopped
// (they wait on an empty queue). Statistics for the log.
class SmoothWorkers {
public:
    void add(SmoothJob job) {
        {
            std::lock_guard lock(mutex_);
            if (threads_ == 0) start();
            queue_.push_back(std::move(job));
        }
        wake_.notify_one();
    }

    static SmoothWorkers& get() {
        static SmoothWorkers* workers = new SmoothWorkers;  // never destroyed: detached threads use it
        return *workers;
    }

private:
    void start() {
        const unsigned cpus = std::max(2u, std::thread::hardware_concurrency());
        threads_ = std::clamp(cpus / 2, 1u, 8u);
        for (unsigned i = 0; i < threads_; ++i) std::thread([this] { run(); }).detach();
        std::fprintf(stderr, "[ngl_gfx] model smoothing: %u worker threads\n", threads_);
    }

    void run() {
        std::fesetenv(FE_DFL_ENV);  // the C defaults, not the title's float modes
        for (;;) {
            SmoothJob job;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [this] { return !queue_.empty(); });
                job = std::move(queue_.front());
                queue_.pop_front();
            }
            work(job);
        }
    }

    void work(SmoothJob& job) {
        using namespace render::smooth;
        const auto started = std::chrono::steady_clock::now();
        const Layout layout{job.stride, job.attributes, job.attribute_count};
        const uint16_t* indices = job.indices.empty() ? nullptr : job.indices.data();
        Result& result = job.entry->result;
        bool ok = smooth(layout, job.vertices.data(), job.vertex_count, job.topology, indices, job.count, job.options,
                         result);
        if (!ok && job.options.segments > 2) {  // too many vertices: try fewer
            job.options.segments = 2;
            ok = smooth(layout, job.vertices.data(), job.vertex_count, job.topology, indices, job.count, job.options,
                        result);
        }
        if (!ok) result = Result();
        const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        job.entry->state.store(ok ? kReady : kRefused, std::memory_order_release);
        std::lock_guard lock(mutex_);
        (ok ? done_ : refused_) += 1;
        curved_ += result.curved_edges;
        straight_ += result.straight_edges;
        rounded_ += result.rounded_edges;
        bytes_ += result.vertices.size() + result.indices.size() * 2;
        seconds_ += took;
        worst_ = std::max(worst_, took);
        const uint32_t total = done_ + refused_;
        if ((total & (total - 1)) == 0 && total >= 16)  // now and then
            std::fprintf(stderr,
                         "[ngl_gfx] model smoothing: %u sections smoothed, %u kept as they are, %zu KB, %.1f ms of "
                         "worker time (worst section %.2f ms), %llu edges curved, %llu straight (%llu of them rounded outlines)\n",
                         done_, refused_, bytes_ >> 10, seconds_ * 1e3, worst_ * 1e3,
                         static_cast<unsigned long long>(curved_), static_cast<unsigned long long>(straight_),
                         static_cast<unsigned long long>(rounded_));
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<SmoothJob> queue_;
    unsigned threads_ = 0;
    uint32_t done_ = 0, refused_ = 0;
    size_t bytes_ = 0;
    double seconds_ = 0, worst_ = 0;
    uint64_t curved_ = 0, straight_ = 0, rounded_ = 0;
};

uint64_t sample_hash(const uint8_t* data, size_t bytes, uint64_t h) {
    auto mix = [&h](uint32_t w) { h = (h ^ w) * 0x100000001B3ull; };
    mix(static_cast<uint32_t>(bytes));
    if (bytes < 4) return h;
    const size_t words = bytes / 4, step = std::max<size_t>(1, words / 61);
    for (size_t i = 0; i < words; i += step) {
        uint32_t w;
        std::memcpy(&w, data + i * 4, 4);
        mix(w);
    }
    uint32_t last;
    std::memcpy(&last, data + bytes - 4, 4);
    mix(last);
    return h;
}

// The declaration's attributes for smoothing: v0 the position, v1 the (packed) normal; skinned: v3 the bone
// indices, v4 the weights (cdChar's vertex shader).
bool smooth_layout(uint32_t declaration, bool skinned, render::smooth::Attribute* out, uint32_t& count) {
    using render::smooth::Format;
    using render::smooth::Role;
    const auto* inputs = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(declaration));
    count = 0;
    for (uint32_t v = 0; v < 16; ++v) {
        const uint32_t* input = inputs + v * 4;
        if (input[2] == 0x02) continue;
        if (input[0] != 0) return false;
        Format f;
        switch (input[2]) {
            case 0x12: f = Format::Float1; break;
            case 0x22: f = Format::Float2; break;
            case 0x32: f = Format::Float3; break;
            case 0x42: f = Format::Float4; break;
            case 0x40: f = Format::Color; break;
            case 0x44: f = Format::UByte4N; break;
            case 0x16: f = Format::PackedNormal; break;
            case 0x25: f = Format::Short2; break;
            case 0x45: f = Format::Short4; break;
            case 0x21: f = Format::Short2N; break;
            case 0x41: f = Format::Short4N; break;
            default: return false;
        }
        Role role = Role::Other;
        if (v == 0) role = Role::Position;
        else if (v == 1) role = Role::Normal;
        else if (skinned && v == 3) role = Role::BlendIndices;
        else if (skinned && v == 4) role = Role::BlendWeights;
        out[count++] = {static_cast<uint16_t>(input[1]), f, role};
    }
    return true;
}

}  // namespace

bool draw_smoothed(uint32_t primitive, uint32_t vertex_count, uint32_t index_count, const void* indices, bool skinned) {
    using namespace render::smooth;
    const GfxState& s = recorded().state;
    if (!ngl_gfx_own_draws() || !s.stream_stride || !s.vertex_declaration || !vertex_count) return false;
    Topology topology;
    switch (primitive) {
        case 5: topology = Topology::Triangles; break;
        case 6: topology = Topology::Strip; break;
        case 8: topology = Topology::Quads; break;
        default: return false;
    }
    const uint8_t* memory = stream_memory();
    if (!memory) return false;
    const auto* index_data = static_cast<const uint16_t*>(indices);
    const uint32_t count = index_data ? index_count : vertex_count;
    const uint64_t key = (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(memory)) << 32 ^
                          static_cast<uint32_t>(reinterpret_cast<uintptr_t>(index_data))) ^
                         (uint64_t{count} << 20) ^ (uint64_t{s.vertex_declaration} << 1) ^ primitive;
    uint64_t sample = sample_hash(memory, size_t{vertex_count} * s.stream_stride, 0xCBF29CE484222325ull);
    if (index_data) sample = sample_hash(reinterpret_cast<const uint8_t*>(index_data), size_t{count} * 2, sample);
    SmoothCache& cache = smooth_cache();
    std::shared_ptr<Smoothed>& slot = cache.sections[key];
    if (!slot || slot->sample != sample) {
        if (cache.sections.size() > 20000) {  // a long session: start over (jobs in flight keep their entries)
            cache.sections.clear();
            return false;
        }
        slot = std::make_shared<Smoothed>();
        slot->sample = sample;
        slot->id = cache.next_id++;
        SmoothJob job;
        if (!smooth_layout(s.vertex_declaration, skinned, job.attributes, job.attribute_count)) {
            slot->state.store(kRefused, std::memory_order_relaxed);
            return false;
        }
        static const uint32_t segments = [] {
            const char* text = std::getenv("COD_BRO_SMOOTH_SEGMENTS");  // developers: 1-8 (default 3)
            return text ? static_cast<uint32_t>(std::clamp(std::atoi(text), 1, 8)) : 3u;
        }();
        job.entry = slot;
        job.vertices.assign(memory, memory + size_t{vertex_count} * s.stream_stride);
        if (index_data) job.indices.assign(index_data, index_data + count);
        job.vertex_count = vertex_count;
        job.count = count;
        job.stride = s.stream_stride;
        job.topology = topology;
        job.options.segments = segments;
        SmoothWorkers::get().add(std::move(job));
        return false;  // as it is until the result is ready
    }
    if (slot->state.load(std::memory_order_acquire) != kReady) return false;
    const Result& result = slot->result;
    return ngl_gfx_draw_user(render::Primitive::Triangles, result.vertices.data(), result.vertex_count, s.stream_stride,
                             result.indices.data(), static_cast<uint32_t>(result.indices.size()),
                             slot->id | render::kGfxFixedContent);
}

void clear(uint32_t rect_count, const void* rects, uint32_t flags, uint32_t color, float depth, uint32_t stencil) {
    render::GfxDevice* device = gfx();
    if (ngl_gfx_own_draws() && device && !rect_count) {
        DeviceObjects& o = objects(device);
        const GfxState& s = recorded().state;
        if (const render::TargetId target = target_for(o, s.color_target)) {
            device->set_target(target, target_for(o, s.depth_target));
            uint32_t ours = 0;
            if (flags & 0xF0) ours |= render::kGfxClearColor;
            if (flags & 1) ours |= render::kGfxClearDepth;
            if (flags & 2) ours |= render::kGfxClearStencil;
            device->clear(ours, color, depth, stencil);
            return;
        }
    }
    d3d<void XB_STDCALL(uint32_t, const void*, uint32_t, uint32_t, float, uint32_t)>(0x003ead90)(
        rect_count, rects, flags, color, depth, stencil);
}

void set_vertex_constant(uint32_t slot, const float* values) {
    d3d<void XB_FASTCALL(uint32_t, const float*)>(0x003e7440)(slot, values);
    if (slot < 192) std::memcpy(recorded().state.vs_constants[slot], values, 16);
}

void set_vertex_constants(uint32_t slot, const float* values, uint32_t dwords) {
    d3d<void XB_FASTCALL(uint32_t, const float*, uint32_t)>(0x003e7490)(slot, values, dwords);
    float* constants = &recorded().state.vs_constants[0][0];
    const uint32_t first = slot * 4;
    if (first < 192 * 4) std::memcpy(constants + first, values, std::min(dwords, 192 * 4 - first) * 4);
}

void set_pixel_constants(uint32_t reg, const float* values, uint32_t count) {
    d3d<void XB_STDCALL(uint32_t, const float*, uint32_t)>(0x003e7a50)(reg, values, count);
    // To the combiner factors the current definition maps each register to, as a colour
    // (clamped to 0..1, 8 bits; src/render/shader.cpp's apply_pixel_constant).
    GfxState& s = recorded().state;
    if (!s.pixel_definition) return;
    const PixelDefinition* d = definition(s.pixel_definition);
    auto byte = [](float f) { return static_cast<uint32_t>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
    for (uint32_t i = 0; i < count && reg + i < 16; ++i) {
        const float* v = values + i * 4;
        float color[4];
        unpack_color(byte(v[3]) << 24 | byte(v[0]) << 16 | byte(v[1]) << 8 | byte(v[2]), color);
        const uint32_t r = reg + i;
        for (uint32_t stage = 0; stage < 8; ++stage) {
            if ((d->c0_mapping >> (stage * 4) & 0xF) == r) std::memcpy(s.ps_constants[stage], color, 16);
            if ((d->c1_mapping >> (stage * 4) & 0xF) == r) std::memcpy(s.ps_constants[8 + stage], color, 16);
        }
        if ((d->final_constants_mapping & 0xF) == r) std::memcpy(s.ps_constants[16], color, 16);
        if ((d->final_constants_mapping >> 4 & 0xF) == r) std::memcpy(s.ps_constants[17], color, 16);
    }
}

}  // namespace xb
void ngl_gfx_restart_title() {
    // Everything here came from title memory, which the restart replaces: our device objects go, the recorded
    // state is D3D's defaults again (the window and its device stay).
    DeviceObjects& o = device_objects();
    if (o.device) {
        for (auto& [key, texture] : o.textures)
            if (texture.own) o.device->destroy_texture(texture.own);
        for (auto& [key, target] : o.targets) o.device->destroy_target(target);  // colour_targets: the same ids
        for (auto& [key, shader] : o.vertex_shaders) o.device->destroy_shader(shader);
        for (auto& [key, shader] : o.pixel_shaders) o.device->destroy_shader(shader);
    }
    o = DeviceObjects();
    recorded() = fresh_recorded();
    xb::smooth_cache() = xb::SmoothCache();
    g_frame = 0;
}

}  // namespace game
