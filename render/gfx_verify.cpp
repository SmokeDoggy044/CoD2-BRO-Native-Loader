// XB_GFX_VERIFY=1: before every draw of the Xbox path, the state the renderer C recorded in our
// own terms (gfx_state.h, through the probe the loader sets) is compared with the state the Xbox
// path draws with (the NV2A registers and D3D's tables, as D3D9Draw reads them). Each field that
// differs is logged the first times with both values, and counted (summary every 600 frames).
// What R4 needs before a draw path can be ours: every difference gone (docs/own-renderer.md).

#include "gfx_verify.h"

#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "d3d_internal.h"
#include "log.h"
#include "nv2a.h"
#include "shader_state.h"
#include "xbox_d3d.h"

namespace render {
namespace {

using xbox::from_xbox_ptr;

const GfxState* (*g_probe)() = nullptr;
bool g_enabled = false;
std::map<std::string, uint32_t> g_counts;  // field -> draws it differed in
uint32_t g_draws = 0, g_last_summary_frame = 0;
// How many draws had each feature on (the Xbox path's state): what the comparison covered.
struct Coverage {
    uint32_t blend = 0, alpha_test = 0, fog = 0, textured = 0, stencil = 0, depth_bias = 0, culled = 0, pixel_shader = 0;
} g_coverage;

GfxState xbox_state() {
    auto reg = [](uint32_t method) { return nv2a::reg(method); };
    auto reg_float = [](uint32_t method) { return std::bit_cast<float>(nv2a::reg(method)); };
    const uint32_t* rs = render_state();
    const uint32_t* tss = texture_state();
    GfxState s;
    s.blend.enable = reg(kNvBlendEnable) != 0;
    s.blend.src = gfx_blend(reg(kNvBlendSrc));
    s.blend.dst = gfx_blend(reg(kNvBlendDst));
    s.blend.op = gfx_blend_op(reg(kNvBlendEquation));
    s.blend.write_mask = gfx_color_mask(reg(kNvColorMask));
    s.blend.constant = reg(kNvBlendColor);
    s.blend.alpha_test = reg(kNvAlphaTestEnable) != 0;
    s.blend.alpha_func = gfx_compare(reg(kNvAlphaFunc));
    s.blend.alpha_ref = static_cast<uint8_t>(reg(kNvAlphaRef));
    DepthStencilState& d = s.depth_stencil;
    d.depth_test = rs[kRsZEnable] != 0;
    d.depth_write = reg(kNvDepthMask) != 0;
    d.depth_func = gfx_compare(reg(kNvDepthFunc));
    d.stencil = rs[kRsStencilEnable] != 0;
    d.stencil_func = gfx_compare(reg(kNvStencilFunc));
    d.stencil_ref = static_cast<uint8_t>(reg(kNvStencilFuncRef));
    d.stencil_read_mask = static_cast<uint8_t>(reg(kNvStencilFuncMask));
    d.stencil_write_mask = static_cast<uint8_t>(reg(kNvStencilMask));
    d.stencil_fail = gfx_stencil_op(rs[kRsStencilFail]);
    d.stencil_depth_fail = gfx_stencil_op(reg(kNvStencilOpZFail));
    d.stencil_pass = gfx_stencil_op(reg(kNvStencilOpZPass));
    s.raster.cull = gfx_cull(rs[kRsCullMode], rs[kRsFrontFace]);
    s.raster.fill = gfx_fill(rs[kRsFillMode]);
    s.raster.flat_shading = reg(kNvShadeMode) == 0x1D00;
    if (reg(kNvPolygonOffsetFillEnable)) {
        s.raster.slope_bias = reg_float(kNvPolygonOffsetScaleFactor);
        s.raster.depth_bias = reg_float(kNvPolygonOffsetBias);
    }
    s.fog_enable = reg(kNvFogEnable) != 0;
    const uint32_t fog_mode = reg(kNvFogMode);
    s.fog.mode = !s.fog_enable                   ? FogMode::Off
                 : fog_mode == kNvFogModeExp     ? FogMode::Exp
                 : fog_mode == kNvFogModeExp2    ? FogMode::Exp2
                                                 : FogMode::Linear;
    s.fog.bias = reg_float(kNvFogParams);
    s.fog.scale = reg_float(kNvFogParams + 4);
    const uint32_t fog = reg(kNvFogColor);  // ABGR
    s.fog.color = (fog & 0xFF00FF00) | (fog & 0xFF) << 16 | (fog >> 16 & 0xFF);
    for (uint32_t stage = 0; stage < kGfxStages; ++stage) {
        const uint32_t* t = tss + stage * kTssPerStage;
        SamplerState& sampler = s.samplers[stage];
        sampler.u = gfx_address(t[kTssAddressU]);
        sampler.v = gfx_address(t[kTssAddressV]);
        sampler.w = gfx_address(t[kTssAddressW]);
        sampler.mag = gfx_filter(t[kTssMagFilter]);
        sampler.min = gfx_filter(t[kTssMinFilter]);
        sampler.mip = gfx_filter(t[kTssMipFilter]);
        sampler.lod_bias = std::bit_cast<float>(t[kTssMipMapLodBias]);
        sampler.max_level = t[kTssMaxMipLevel];
        sampler.anisotropy = t[kTssMaxAnisotropy];
        sampler.border = t[kTssBorderColor];
        sampler.alpha_kill = t[kTssAlphaKill] != 0;
        // Textures by their memory (the title's headers are not in the registers).
        s.textures[stage] = reg(kNvTextureFormat + stage * kNvTextureStride) ? reg(kNvTextureOffset + stage * kNvTextureStride) : 0;
        s.palettes[stage] = texture_stage(stage).palette;
    }
    s.color_target = current_render_target();
    s.depth_target = current_depth_stencil();
    const XbViewport& v = view_state().viewport;
    s.viewport = {v.x, v.y, v.width, v.height, v.min_z, v.max_z};
    const auto& nv = nv2a::state();
    for (uint32_t i = 0; i < 192; ++i)
        for (uint32_t k = 0; k < 4; ++k) s.vs_constants[i][k] = std::bit_cast<float>(nv.constants[i][k]);
    auto unpack = [](uint32_t c, float* out) {
        out[0] = (c >> 16 & 0xFF) / 255.0f;
        out[1] = (c >> 8 & 0xFF) / 255.0f;
        out[2] = (c & 0xFF) / 255.0f;
        out[3] = (c >> 24) / 255.0f;
    };
    for (uint32_t i = 0; i < 8; ++i) {
        unpack(reg(kNvCombinerFactor0 + i * 4), s.ps_constants[i]);
        unpack(reg(kNvCombinerFactor1 + i * 4), s.ps_constants[8 + i]);
    }
    unpack(reg(kNvCombinerFinalFactor), s.ps_constants[16]);
    unpack(reg(kNvCombinerFinalFactor + 4), s.ps_constants[17]);
    return s;
}

void differs(const std::string& field, const std::string& xbox, const std::string& ours, uint32_t frame) {
    if (g_counts[field]++ < 3)
        LOG_WARN("gfx-verify", "frame %u draw %u: %s: Xbox path %s, recorded %s", frame, g_draws, field.c_str(),
                 xbox.c_str(), ours.c_str());
}

std::string num(double v) {
    char text[32];
    std::snprintf(text, sizeof(text), "%g", v);
    return text;
}

template <typename T>
void check(const char* field, T xbox, T ours, uint32_t frame) {
    if (xbox != ours) differs(field, num(static_cast<double>(xbox)), num(static_cast<double>(ours)), frame);
}

void check_float(const std::string& field, float xbox, float ours, float tolerance, uint32_t frame) {
    if (!(std::fabs(xbox - ours) <= tolerance) && !(std::isnan(xbox) && std::isnan(ours)))
        differs(field, num(xbox), num(ours), frame);
}

// The vertex program at `program` (title memory: header, then 16 bytes an instruction) against
// the NV2A program memory from the start slot.
bool same_program(uint32_t program) {
    if (!program) return false;
    const auto* header = from_xbox_ptr<uint16_t>(program);
    const auto* words = from_xbox_ptr<uint32_t>(program + 4);
    const uint32_t start = nv2a::reg(nv2a::kTransformProgramStart);
    const auto& slots = nv2a::state().program;
    for (uint32_t i = 0; i < header[1] && start + i < slots.size(); ++i) {
        if (std::memcmp(slots[start + i].data(), words + i * 4, 16) != 0) return false;
        if (words[i * 4 + 3] & 1) break;  // the final instruction
    }
    return true;
}

// The pixel shader definition at `definition` against D3D's copy, constants aside.
bool same_definition(uint32_t definition) {
    const ShaderState& shaders = shader_state();
    if (!definition || !shaders.pixel_shader_set) return !definition && !shaders.pixel_shader_set;
    XbPixelShaderDef a, b = shaders.pixel_shader_def;
    std::memcpy(&a, from_xbox_ptr<void>(definition), sizeof(a));
    for (XbPixelShaderDef* d : {&a, &b}) {
        std::memset(d->constant0, 0, sizeof(d->constant0));
        std::memset(d->constant1, 0, sizeof(d->constant1));
        d->final_constant0 = d->final_constant1 = 0;
    }
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}

}  // namespace

void set_gfx_state_probe(const GfxState* (*probe)()) {
    g_probe = probe;
    const char* env = std::getenv("XB_GFX_VERIFY");
    g_enabled = env && *env && *env != '0';
    if (g_enabled) LOG_INFO("gfx-verify", "comparing the renderer C's recorded state with the Xbox path's at every draw");
}

void verify_gfx_state(uint32_t frame, bool has_depth) {
    if (!g_enabled || !g_probe) return;
    const GfxState* recorded = g_probe();
    if (!recorded) return;
    ++g_draws;
    const GfxState x = xbox_state();
    const GfxState& r = *recorded;
    g_coverage.blend += x.blend.enable;
    g_coverage.alpha_test += x.blend.alpha_test;
    g_coverage.fog += x.fog_enable;
    g_coverage.textured += x.textures[0] != 0;
    g_coverage.stencil += x.depth_stencil.stencil;
    g_coverage.depth_bias += x.raster.depth_bias != 0;
    g_coverage.culled += x.raster.cull != Cull::None;
    g_coverage.pixel_shader += shader_state().pixel_shader_set;
    check("blend.enable", x.blend.enable, r.blend.enable, frame);
    if (x.blend.enable) {
        check("blend.src", static_cast<int>(x.blend.src), static_cast<int>(r.blend.src), frame);
        check("blend.dst", static_cast<int>(x.blend.dst), static_cast<int>(r.blend.dst), frame);
        check("blend.op", static_cast<int>(x.blend.op), static_cast<int>(r.blend.op), frame);
        check("blend.constant", x.blend.constant, r.blend.constant, frame);
    }
    check("blend.write_mask", x.blend.write_mask, r.blend.write_mask, frame);
    check("blend.alpha_test", x.blend.alpha_test, r.blend.alpha_test, frame);
    if (x.blend.alpha_test) {
        check("blend.alpha_func", static_cast<int>(x.blend.alpha_func), static_cast<int>(r.blend.alpha_func), frame);
        check("blend.alpha_ref", x.blend.alpha_ref, r.blend.alpha_ref, frame);
    }
    const DepthStencilState &xd = x.depth_stencil, &rd = r.depth_stencil;
    if (has_depth) {
        check("depth.test", xd.depth_test, rd.depth_test, frame);
        check("depth.write", xd.depth_write, rd.depth_write, frame);
        if (xd.depth_test) check("depth.func", static_cast<int>(xd.depth_func), static_cast<int>(rd.depth_func), frame);
        check("stencil.enable", xd.stencil, rd.stencil, frame);
        if (xd.stencil) {
            check("stencil.func", static_cast<int>(xd.stencil_func), static_cast<int>(rd.stencil_func), frame);
            check("stencil.ref", xd.stencil_ref, rd.stencil_ref, frame);
            check("stencil.read_mask", xd.stencil_read_mask, rd.stencil_read_mask, frame);
            check("stencil.write_mask", xd.stencil_write_mask, rd.stencil_write_mask, frame);
            check("stencil.fail", static_cast<int>(xd.stencil_fail), static_cast<int>(rd.stencil_fail), frame);
            check("stencil.depth_fail", static_cast<int>(xd.stencil_depth_fail), static_cast<int>(rd.stencil_depth_fail),
                  frame);
            check("stencil.pass", static_cast<int>(xd.stencil_pass), static_cast<int>(rd.stencil_pass), frame);
        }
    }
    check("raster.cull", static_cast<int>(x.raster.cull), static_cast<int>(r.raster.cull), frame);
    check("raster.fill", static_cast<int>(x.raster.fill), static_cast<int>(r.raster.fill), frame);
    check("raster.flat_shading", x.raster.flat_shading, r.raster.flat_shading, frame);
    check_float("raster.depth_bias", x.raster.depth_bias, r.raster.depth_bias, 0, frame);
    check_float("raster.slope_bias", x.raster.slope_bias, r.raster.slope_bias, 0, frame);
    check("color_target", x.color_target, r.color_target, frame);
    check("depth_target", x.depth_target, r.depth_target, frame);
    check("viewport.x", x.viewport.x, r.viewport.x, frame);
    check("viewport.y", x.viewport.y, r.viewport.y, frame);
    check("viewport.width", x.viewport.width, r.viewport.width, frame);
    check("viewport.height", x.viewport.height, r.viewport.height, frame);
    check_float("viewport.min_z", x.viewport.min_z, r.viewport.min_z, 0, frame);
    check_float("viewport.max_z", x.viewport.max_z, r.viewport.max_z, 0, frame);
    check("fog.enable", x.fog_enable, r.fog_enable, frame);
    if (x.fog_enable) {
        check("fog.mode", static_cast<int>(x.fog.mode), static_cast<int>(r.fog.mode), frame);
        check_float("fog.bias", x.fog.bias, r.fog.bias, 1e-6f, frame);
        check_float("fog.scale", x.fog.scale, r.fog.scale, 1e-9f, frame);
        check("fog.color", x.fog.color & 0xFFFFFF, r.fog.color & 0xFFFFFF, frame);
    }

    // Shaders: which program / definition, by content.
    const bool programmable = shader_state().programmable;
    if (!programmable)
        differs("vertex_program", "fixed function", num(r.vertex_program), frame);
    else if (!same_program(r.vertex_program))
        differs("vertex_program", "other", num(r.vertex_program), frame);
    if (!same_definition(r.pixel_definition))
        differs("pixel_definition", shader_state().pixel_shader_set ? "other" : "none", num(r.pixel_definition), frame);

    // Textures and samplers of the stages that have one.
    for (uint32_t stage = 0; stage < kGfxStages; ++stage) {
        const std::string at = "stage" + std::to_string(stage) + ".";
        const uint32_t ours = r.textures[stage] ? from_xbox_ptr<XbResource>(r.textures[stage])->data : 0;
        check((at + "texture").c_str(), x.textures[stage], ours, frame);
        if (!x.textures[stage]) continue;
        const SamplerState &xs = x.samplers[stage], &rs = r.samplers[stage];
        check((at + "address_u").c_str(), static_cast<int>(xs.u), static_cast<int>(rs.u), frame);
        check((at + "address_v").c_str(), static_cast<int>(xs.v), static_cast<int>(rs.v), frame);
        check((at + "min").c_str(), static_cast<int>(xs.min), static_cast<int>(rs.min), frame);
        check((at + "mag").c_str(), static_cast<int>(xs.mag), static_cast<int>(rs.mag), frame);
        check((at + "mip").c_str(), static_cast<int>(xs.mip), static_cast<int>(rs.mip), frame);
        check_float(at + "lod_bias", xs.lod_bias, rs.lod_bias, 0, frame);
        check((at + "max_level").c_str(), xs.max_level, rs.max_level, frame);
        check((at + "border").c_str(), xs.border, rs.border, frame);
        check((at + "alpha_kill").c_str(), xs.alpha_kill, rs.alpha_kill, frame);
        check((at + "palette").c_str(), x.palettes[stage], r.palettes[stage], frame);
    }

    // Constants: every vertex slot (which ones a program reads is not known here) and the
    // combiner factors.
    uint32_t vs_wrong = 0, first_wrong = 0;
    for (uint32_t i = 0; i < 192; ++i)
        for (uint32_t k = 0; k < 4; ++k) {
            const float a = x.vs_constants[i][k], b = r.vs_constants[i][k];
            if (a != b && !(std::isnan(a) && std::isnan(b))) {
                if (!vs_wrong++) first_wrong = i;
                break;
            }
        }
    if (vs_wrong) differs("vs_constants", num(vs_wrong) + " slots, first c" + num(first_wrong), "", frame);
    for (uint32_t i = 0; i < 18; ++i)
        for (uint32_t k = 0; k < 4; ++k)
            check_float("ps_constant c" + std::to_string(i), x.ps_constants[i][k], r.ps_constants[i][k], 0.5f / 255, frame);

    if (frame - g_last_summary_frame >= 600) {
        g_last_summary_frame = frame;
        std::string summary;
        for (const auto& [field, count] : g_counts) summary += " " + field + "=" + std::to_string(count);
        const Coverage& c = g_coverage;
        LOG_INFO("gfx-verify",
                 "%u draws so far (blend %u, alpha test %u, fog %u, textured %u, stencil %u, depth bias %u, culled %u, "
                 "pixel shader %u); differences per field:%s",
                 g_draws, c.blend, c.alpha_test, c.fog, c.textured, c.stencil, c.depth_bias, c.culled, c.pixel_shader,
                 summary.empty() ? " none" : summary.c_str());
    }
}

}  // namespace render
