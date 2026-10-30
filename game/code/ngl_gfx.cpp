#include "ngl_gfx.h"

#include <algorithm>
#include <cstring>
#include <unordered_map>

#include "gfx_state.h"
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

}  // namespace

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
