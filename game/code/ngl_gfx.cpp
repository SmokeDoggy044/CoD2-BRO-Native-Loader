#include "ngl_gfx.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "gfx_state.h"
#include "ngl.h"
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

Recorded& recorded() {
    static Recorded r = [] {
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
    }();
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

// Fields derived from D3D's tables and the polygon offset, as D3D does it (render/draw.cpp's
// apply_fog_states for the fog).
void derive() {
    Recorded& r = recorded();
    GfxState& s = r.state;
    const uint32_t* rs = r.render_states;
    s.depth_stencil.depth_test = rs[kRsZEnable] != 0;
    s.depth_stencil.stencil = rs[kRsStencilEnable] != 0;
    s.depth_stencil.stencil_fail = render::gfx_stencil_op(rs[kRsStencilFail]);
    s.raster.cull = render::gfx_cull(rs[kRsCullMode], rs[kRsFrontFace]);
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

// What the device holds for the recorded state, made once (cleared when the device changes).
struct DeviceObjects {
    render::GfxDevice* device = nullptr;
    std::unordered_map<uint64_t, render::ShaderId> vertex_shaders, pixel_shaders;  // by variant, key
    std::unordered_map<uint32_t, std::pair<render::LayoutId, uint32_t>> layouts;  // declaration -> layout, stride
    std::unordered_map<uint64_t, render::TargetId> targets;                         // by surface data, size, format
    std::unordered_map<uint64_t, render::TextureId> textures;                       // by header, palette
};

DeviceObjects& objects(render::GfxDevice* device) {
    static DeviceObjects o;
    if (o.device != device) o = DeviceObjects{device};
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
    if (!id) id = o.device->create_target({format, width, height, data});
    return id;
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
    const render::ShaderId ps = shader_for(o, render::ShaderStage::Pixel, s.pixel_key);
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
    device.set_target(color, target_for(o, s.depth_target));
    device.set_viewport(s.viewport);
    device.set_blend(s.blend);
    device.set_depth_stencil(s.depth_stencil);
    device.set_raster(s.raster);
    device.set_fog(s.fog);
    for (uint32_t stage = 0; stage < render::kGfxStages; ++stage) {
        render::TextureId texture = 0;
        if (s.textures[stage]) {
            render::TextureId& id = o.textures[uint64_t{s.palettes[stage]} << 32 | s.textures[stage]];
            if (!id) id = device.xbox_texture(s.textures[stage], s.palettes[stage]);
            texture = id;
        }
        device.set_texture(stage, texture);
        device.set_sampler(stage, s.samplers[stage]);
    }
    device.set_shaders(vs, ps);
    device.set_layout(layout);
    device.set_constants(render::ShaderStage::Vertex, 0, &s.vs_constants[0][0], 192);
    device.set_constants(render::ShaderStage::Pixel, 0, &s.ps_constants[0][0], 18);
    return true;
}

}  // namespace

namespace {
bool env_set(const char* name) {
    const char* env = std::getenv(name);
    return env && *env && *env != '0';
}
uint32_t g_frame = 0;
}  // namespace

bool ngl_gfx_own_draws() {
    static const bool xbox_draws = env_set("XB_XBOX_DRAWS");
    static const bool alternate = env_set("XB_DRAWS_ALTERNATE");
    return !xbox_draws && (!alternate || (g_frame & 1)) && gfx() != nullptr;
}

void ngl_gfx_end_frame() { ++g_frame; }

bool ngl_gfx_draw_user(render::Primitive primitive, const void* vertices, uint32_t count, uint32_t stride,
                       const uint16_t* indices, uint32_t index_count, uint64_t cache_key) {
    render::GfxDevice* device = gfx();
    if (!device) return false;
    DeviceObjects& o = objects(device);
    if (!apply(*device, o, stride)) return false;
    device->draw_user(primitive, vertices, count, stride, indices, index_count, cache_key);
    static bool told = false;
    if (!told) {
        told = true;
        std::fprintf(stderr, "[ngl_gfx] first draw of ours (our graphics interface, render/gfx_device.h)\n");
    }
    return true;
}

const render::GfxState* ngl_gfx_state() {
    derive();
    return &recorded().state;
}

void ngl_gfx_shader_key(uint32_t program, uint32_t key) { recorded().keys[program] = key; }

namespace xb {

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
    // (clamped to 0..1, 8 bits; render/shader.cpp's apply_pixel_constant).
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
}  // namespace game
