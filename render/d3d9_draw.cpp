// D3D9 draw translation. See d3d9_draw.h.

#include "d3d9_draw.h"

#include <algorithm>
#include <cstdlib>
#include <bit>
#include <cmath>
#include <cstring>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>

#include "d3d9_shader.h"
#include "d3d_internal.h"
#include "log.h"
#include "nv2a.h"
#include "shader_state.h"
#include "texture_decode.h"
#include "texture_hash.h"
#include "xbox_d3d.h"

namespace render {

namespace {

using xbox::from_xbox_ptr;

void warn_once(std::once_flag& once, const char* message) {
    std::call_once(once, [message] { LOG_WARN("render", "%s", message); });
}

template <typename T>
void release(T*& object) {
    if (object) object->Release();
    object = nullptr;
}

// --- State translation (NV2A values are OpenGL enums) ------------------------------------

D3DCMPFUNC compare_func(uint32_t gl) {
    return gl >= 0x200 && gl <= 0x207 ? static_cast<D3DCMPFUNC>(gl - 0x200 + 1) : D3DCMP_ALWAYS;
}

D3DBLEND blend_factor(uint32_t gl) {
    switch (gl) {
        case 0x0000: return D3DBLEND_ZERO;
        case 0x0001: return D3DBLEND_ONE;
        case 0x0300: return D3DBLEND_SRCCOLOR;
        case 0x0301: return D3DBLEND_INVSRCCOLOR;
        case 0x0302: return D3DBLEND_SRCALPHA;
        case 0x0303: return D3DBLEND_INVSRCALPHA;
        case 0x0304: return D3DBLEND_DESTALPHA;
        case 0x0305: return D3DBLEND_INVDESTALPHA;
        case 0x0306: return D3DBLEND_DESTCOLOR;
        case 0x0307: return D3DBLEND_INVDESTCOLOR;
        case 0x0308: return D3DBLEND_SRCALPHASAT;
        case 0x8001: case 0x8003: return D3DBLEND_BLENDFACTOR;  // constant colour / alpha
        case 0x8002: case 0x8004: return D3DBLEND_INVBLENDFACTOR;
        default: return D3DBLEND_ONE;
    }
}

D3DBLENDOP blend_op(uint32_t gl) {
    switch (gl) {
        case 0x800A: return D3DBLENDOP_SUBTRACT;
        case 0x800B: case 0xF005: return D3DBLENDOP_REVSUBTRACT;  // 0xF005: Xbox signed variant
        case 0x8007: return D3DBLENDOP_MIN;
        case 0x8008: return D3DBLENDOP_MAX;
        default: return D3DBLENDOP_ADD;
    }
}

D3DSTENCILOP stencil_op(uint32_t gl) {
    switch (gl) {
        case 0x0000: return D3DSTENCILOP_ZERO;
        case 0x1E01: return D3DSTENCILOP_REPLACE;
        case 0x1E02: return D3DSTENCILOP_INCRSAT;
        case 0x1E03: return D3DSTENCILOP_DECRSAT;
        case 0x150A: return D3DSTENCILOP_INVERT;
        case 0x8507: return D3DSTENCILOP_INCR;
        case 0x8508: return D3DSTENCILOP_DECR;
        default: return D3DSTENCILOP_KEEP;
    }
}

D3DCULL cull_mode(uint32_t cull, uint32_t front_face) {
    if (cull == kCullNone) return D3DCULL_NONE;
    const bool cull_cw = (cull == kCullCw) != (front_face == kFrontFaceCcw);
    return cull_cw ? D3DCULL_CW : D3DCULL_CCW;
}

D3DFILLMODE fill_mode(uint32_t fill) {
    switch (fill) {
        case kFillPoint: return D3DFILL_POINT;
        case kFillWireframe: return D3DFILL_WIREFRAME;
        default: return D3DFILL_SOLID;
    }
}

DWORD texture_address(uint32_t xbox) {
    if (xbox >= 1 && xbox <= 4) return xbox;  // wrap, mirror, clamp, border
    return xbox == 5 ? DWORD{D3DTADDRESS_CLAMP} : DWORD{D3DTADDRESS_WRAP};  // 5: clamp to edge
}

DWORD texture_filter(uint32_t xbox, bool allow_none) {
    switch (xbox) {
        case 0: return allow_none ? D3DTEXF_NONE : D3DTEXF_POINT;
        case 1: return D3DTEXF_POINT;
        case 3: return D3DTEXF_ANISOTROPIC;
        default: return D3DTEXF_LINEAR;  // linear, quincunx, gaussian
    }
}

void unpack_color(uint32_t color, float* out) {
    out[0] = ((color >> 16) & 0xFF) / 255.0f;
    out[1] = ((color >> 8) & 0xFF) / 255.0f;
    out[2] = (color & 0xFF) / 255.0f;
    out[3] = (color >> 24) / 255.0f;
}

// The register combiner program, from the NV2A registers.
XbPixelShaderDef combiners_from_registers() {
    XbPixelShaderDef d{};
    for (uint32_t i = 0; i < 8; ++i) {
        d.alpha_inputs[i] = nv2a::reg(kNvCombinerAlphaInputs + i * 4);
        d.constant0[i] = nv2a::reg(kNvCombinerFactor0 + i * 4);
        d.constant1[i] = nv2a::reg(kNvCombinerFactor1 + i * 4);
        d.alpha_outputs[i] = nv2a::reg(kNvCombinerAlphaOutputs + i * 4);
        d.rgb_inputs[i] = nv2a::reg(kNvCombinerColorInputs + i * 4);
        d.rgb_outputs[i] = nv2a::reg(kNvCombinerColorOutputs + i * 4);
    }
    d.final_inputs_abcd = nv2a::reg(kNvCombinerFinal0);
    d.final_inputs_efg = nv2a::reg(kNvCombinerFinal1);
    d.final_constant0 = nv2a::reg(kNvCombinerFinalFactor);
    d.final_constant1 = nv2a::reg(kNvCombinerFinalFactor + 4);
    d.combiner_count = nv2a::reg(kNvCombinerControl);
    d.texture_modes = nv2a::reg(kNvShaderStageProgram);
    // PS_GLOBALFLAGS_TEXMODE_ADJUST: D3D fits each stage's mode to the texture bound
    // there (FUN_003f0880 in CoD BRO).
    if (shader_state().pixel_shader_def.final_constants_mapping & 0x100) {
        uint32_t modes = 0;
        for (uint32_t stage = 0; stage < 4; ++stage) {
            uint32_t mode = (d.texture_modes >> (stage * 5)) & 0x1F;
            const uint32_t format = nv2a::reg(kNvTextureFormat + stage * kNvTextureStride);
            const bool cube = format & kFormatCubemap;
            const bool volume = (format & 0x40000000) || (format & 0xF0) == 0x30;
            if (!format && mode != 4 && mode != 5 && mode != 10 && mode != 17)
                mode = 0;  // no texture: only modes that need none stay
            else if (mode >= 1 && mode <= 3)
                mode = cube ? 3 : volume ? 2 : 1;
            else if (mode == 13 || mode == 14)
                mode = cube ? 14 : 13;
            modes |= mode << (stage * 5);
        }
        d.texture_modes = modes;
    }
    d.dot_mapping = nv2a::reg(kNvDotRgbMapping);
    d.input_texture = nv2a::reg(kNvShaderOtherStageInput);
    return d;
}

d3d9::FogMode fog_mode() {
    if (!nv2a::reg(kNvFogEnable)) return d3d9::FogMode::Off;
    switch (nv2a::reg(kNvFogMode)) {
        case kNvFogModeExp: return d3d9::FogMode::Exp;
        case kNvFogModeExp2: return d3d9::FogMode::Exp2;
        default: return d3d9::FogMode::Linear;
    }
}

bool stage_has_texture(uint32_t stage) { return nv2a::reg(kNvTextureFormat + stage * kNvTextureStride) != 0; }

// No pixel shader: D3D's texture stages. Approximated as stage 0's default
// (texture * diffuse, or diffuse alone) until the stage states are translated.
XbPixelShaderDef fixed_function_combiners() {
    static std::once_flag once;
    warn_once(once, "fixed-function texture stages: approximated as texture0 * diffuse");
    XbPixelShaderDef d{};
    d.combiner_count = 1;
    if (stage_has_texture(0)) {
        d.rgb_inputs[0] = 0x08040000;    // T0 * V0
        d.alpha_inputs[0] = 0x18140000;  // T0.a * V0.a
        d.texture_modes = d3d9::kModeProject2D;
    } else {
        d.rgb_inputs[0] = 0x04200000;    // V0 * 1
        d.alpha_inputs[0] = 0x14200000;  // V0.a * 1
    }
    d.rgb_outputs[0] = 0xC0;  // AB -> R0
    d.alpha_outputs[0] = 0xC0;
    // The final combiner D3D sets: fog blends R0 (+ specular V1) towards the fog colour.
    const bool specular = render_state()[kRsSpecularEnable] != 0;
    if (nv2a::reg(kNvFogEnable)) {
        d.final_inputs_abcd = specular ? 0x130E0300 : 0x130C0300;  // lerp(FOG.a, R0|V1+R0, FOG)
        d.final_inputs_efg = 0x1C80;                                // G = R0.a, clamp V1+R0
    } else {
        d.final_inputs_abcd = specular ? 0x0E : 0x0C;
        d.final_inputs_efg = 0x1C80;
    }
    return d;
}

uint32_t primitive_count(DrawInput::Primitive primitive, uint32_t n) {
    switch (primitive) {
        case DrawInput::Primitive::Points: return n;
        case DrawInput::Primitive::Lines: return n / 2;
        case DrawInput::Primitive::LineStrip: return n > 1 ? n - 1 : 0;
        case DrawInput::Primitive::Triangles: return n / 3;
        case DrawInput::Primitive::TriangleStrip:
        case DrawInput::Primitive::TriangleFan: return n > 2 ? n - 2 : 0;
        default: return 0;
    }
}

D3DPRIMITIVETYPE primitive_type(DrawInput::Primitive primitive) {
    switch (primitive) {
        case DrawInput::Primitive::Points: return D3DPT_POINTLIST;
        case DrawInput::Primitive::Lines: return D3DPT_LINELIST;
        case DrawInput::Primitive::LineStrip: return D3DPT_LINESTRIP;
        case DrawInput::Primitive::TriangleStrip: return D3DPT_TRIANGLESTRIP;
        case DrawInput::Primitive::TriangleFan: return D3DPT_TRIANGLEFAN;
        default: return D3DPT_TRIANGLELIST;
    }
}

}  // namespace

struct D3D9Draw::CachedTexture {
    IDirect3DTexture9* texture = nullptr;
    uint64_t content = 0;
    uint32_t replacement_version = 0;  // BackendConfig::replacement_version when made
    uint32_t checked_frame = 0;
    float scale[2] = {1.0f, 1.0f};  // texel -> normalized coordinates for linear formats
    bool replaced = false;          // holds a BackendConfig::replace_texture picture
};

struct D3D9Draw::Target {
    IDirect3DTexture9* texture = nullptr;
    IDirect3DSurface9* surface = nullptr;  // level 0
    IDirect3DTexture9* copy = nullptr;     // for sampling the target while drawing into it
    uint32_t width = 0, height = 0;                  // as the title sees it
    uint32_t surface_width = 0, surface_height = 0;  // times the render scale
    D3DFORMAT format = D3DFMT_UNKNOWN;
    CachedTexture sampled;
};

struct DepthBuffer {
    IDirect3DSurface9* surface = nullptr;
    uint32_t width = 0, height = 0;  // the D3D9 surface's
};

struct D3D9Draw::Caches {
    std::unordered_map<uint32_t, IDirect3DVertexDeclaration9*> declarations;
    std::unordered_map<uint64_t, IDirect3DVertexShader9*> vertex_shaders;
    std::unordered_map<uint64_t, IDirect3DPixelShader9*> pixel_shaders;
    std::unordered_map<uint64_t, D3D9Draw::CachedTexture> textures;
    std::set<uint32_t> unsupported_formats;  // warned about once each
    std::set<uint64_t> failed_shaders;       // vertex/pixel shader keys the device refused (logged once)
    std::unordered_map<uint32_t, Target> targets;      // by Xbox address
    std::unordered_map<uint32_t, DepthBuffer> depths;  // by Xbox address

    void release_target(Target& t) {
        release(t.surface);
        release(t.texture);
        release(t.copy);
    }

    ~Caches() {
        for (auto& [k, t] : targets) release_target(t);
        for (auto& [k, d] : depths) release(d.surface);
        for (auto& [k, v] : declarations) release(v);
        for (auto& [k, v] : vertex_shaders) release(v);
        for (auto& [k, v] : pixel_shaders) release(v);
        for (auto& [k, v] : textures) release(v.texture);
    }
};

D3D9Draw::D3D9Draw(IDirect3DDevice9* device, const BackendConfig& config)
    : device_(device), depth_data_(config.depth_stencil.data), anisotropy_(config.anisotropy),
      render_scale_x_(config.render_scale > 0 ? config.render_scale_x() : 1),
      render_scale_y_(config.render_scale > 0 ? config.render_scale : 1),
      narrow_factor_(config.display_aspect > 0 ? static_cast<float>(config.width) / config.height / config.display_aspect
                                               : 1.0f),
      narrow_ui_draw_(config.narrow_ui_draw), replace_texture_(config.replace_texture),
      replacement_version_(config.replacement_version), caches_(std::make_unique<Caches>()) {
    D3DCAPS9 caps{};
    if (SUCCEEDED(device_->GetDeviceCaps(&caps))) {
        anisotropy_ = std::min<uint32_t>(anisotropy_, caps.MaxAnisotropy);
        const auto major = [](DWORD version) { return static_cast<unsigned>(D3DSHADER_VERSION_MAJOR(version)); };
        const auto minor = [](DWORD version) { return static_cast<unsigned>(D3DSHADER_VERSION_MINOR(version)); };
        LOG_INFO("render", "D3D9 caps: vs %u.%u (%u constants, %d temps, %u slots), ps %u.%u (%u slots), %s vertex processing",
                 major(caps.VertexShaderVersion), minor(caps.VertexShaderVersion),
                 static_cast<unsigned>(caps.MaxVertexShaderConst), caps.VS20Caps.NumTemps,
                 static_cast<unsigned>(caps.MaxVertexShader30InstructionSlots), major(caps.PixelShaderVersion),
                 minor(caps.PixelShaderVersion), static_cast<unsigned>(caps.MaxPixelShader30InstructionSlots),
                 device_->GetSoftwareVertexProcessing() ? "software" : "hardware");
    }
    device_->SetRenderState(D3DRS_LIGHTING, FALSE);
    device_->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device_->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
    if (const char* dump = std::getenv("XB_DUMP_FRAME")) dump_frame_ = static_cast<uint32_t>(std::atoi(dump));
    if (const char* stop = std::getenv("XB_DUMP_STOP")) dump_stop_ = static_cast<uint32_t>(std::atoi(stop));
    set_render_target(config.back_buffer, depth_data_ ? &config.depth_stencil : nullptr);
}

namespace {

D3DFORMAT target_format(uint32_t xbox_format) {
    switch (xbox_format) {
        case 0x07: case 0x1E: return D3DFMT_X8R8G8B8;
        case 0x05: case 0x11: return D3DFMT_R5G6B5;
        case 0x03: case 0x1C: return D3DFMT_X1R5G5B5;
        case 0x02: case 0x10: return D3DFMT_A1R5G5B5;
        case 0x04: case 0x1D: return D3DFMT_A4R4G4B4;
        default: return D3DFMT_A8R8G8B8;  // A8R8G8B8 and anything else
    }
}

}  // namespace

bool D3D9Draw::set_render_target(const SurfaceDesc& color, const SurfaceDesc* depth) {
    Caches& caches = *caches_;
    Target& t = caches.targets[color.data];
    const D3DFORMAT format = target_format(color.format);
    if (t.texture && (t.width != color.width || t.height != color.height || t.format != format)) caches.release_target(t);
    if (!t.texture) {
        t.surface_width = std::max(1u, static_cast<uint32_t>(std::lround(color.width * render_scale_x_)));
        t.surface_height = std::max(1u, static_cast<uint32_t>(std::lround(color.height * render_scale_y_)));
        if (FAILED(device_->CreateTexture(t.surface_width, t.surface_height, 1, D3DUSAGE_RENDERTARGET, format,
                                          D3DPOOL_DEFAULT, &t.texture, nullptr)) ||
            FAILED(t.texture->GetSurfaceLevel(0, &t.surface))) {
            LOG_ERROR("render", "render target %ux%u format %02x at %08x: cannot create", color.width, color.height,
                      color.format, color.data);
            caches.release_target(t);
            caches.targets.erase(color.data);
            target_ok_ = false;
            return false;
        }
        t.width = color.width;
        t.height = color.height;
        t.format = format;
        LOG_DEBUG("render", "render target %ux%u format %02x at %08x", color.width, color.height, color.format,
                  color.data);
        device_->ColorFill(t.surface, nullptr, 0);
    }
    device_->SetRenderTarget(0, t.surface);
    target_data_ = color.data;
    target_width_ = color.width;
    target_height_ = color.height;
    target_surface_width_ = t.surface_width;
    target_surface_height_ = t.surface_height;
    target_ok_ = true;

    // Depth: by its Xbox address, at least as large as the target (D3D9 requires it).
    has_depth_ = false;
    IDirect3DSurface9* depth_surface = nullptr;
    if (depth && depth->data) {
        DepthBuffer& d = caches.depths[depth->data];
        const uint32_t width = std::max(t.surface_width, static_cast<uint32_t>(std::lround(depth->width * render_scale_x_)));
        const uint32_t height =
            std::max(t.surface_height, static_cast<uint32_t>(std::lround(depth->height * render_scale_y_)));
        if (d.surface && (d.width < width || d.height < height)) release(d.surface);
        if (!d.surface) {
            d.width = width;
            d.height = height;
            const bool d16 = depth->format == kFmtD16 || depth->format == kFmtLinD16;
            if (FAILED(device_->CreateDepthStencilSurface(d.width, d.height, d16 ? D3DFMT_D16 : D3DFMT_D24S8,
                                                          D3DMULTISAMPLE_NONE, 0, FALSE, &d.surface, nullptr)))
                LOG_ERROR("render", "depth buffer %ux%u at %08x: cannot create", d.width, d.height, depth->data);
        }
        depth_surface = d.surface;
        has_depth_ = depth_surface != nullptr;
    }
    device_->SetDepthStencilSurface(depth_surface);
    return true;
}

void D3D9Draw::forget_render_target(uint32_t data) {
    auto it = caches_->targets.find(data);
    if (it == caches_->targets.end() || data == target_data_) return;
    caches_->release_target(it->second);
    caches_->targets.erase(it);
}

IDirect3DSurface9* D3D9Draw::target_surface(uint32_t data) {
    auto it = caches_->targets.find(data);
    return it == caches_->targets.end() ? nullptr : it->second.surface;
}

IDirect3DTexture9* D3D9Draw::target_texture(uint32_t data) {
    auto it = caches_->targets.find(data);
    return it == caches_->targets.end() ? nullptr : it->second.texture;
}

D3D9Draw::~D3D9Draw() { release(magenta_); }

float D3D9Draw::narrow_anchor(const DrawInput& input, uint32_t width) const {
    // Only over a 3D scene (drawn before the UI): flat frames are pillarboxed whole.
    if (narrow_factor_ >= 1.0f || !narrow_ui_draw_ || !frame_writes_depth_ || !(input.attribute_mask & 1))
        return -1.0f;
    const float anchor = narrow_ui_draw_();
    if (anchor < 0.0f) return -1.0f;
    // Full-width quads (dimming, letterbox bars) keep covering the picture.
    const uint32_t stride = static_cast<uint32_t>(std::popcount(input.attribute_mask)) * 4;
    float left = input.vertices[0], right = left;
    for (uint32_t i = 1; i < input.vertex_count; ++i) {
        left = std::min(left, input.vertices[i * stride]);
        right = std::max(right, input.vertices[i * stride]);
    }
    return left <= 1.0f && right >= static_cast<float>(width) - 1.0f ? -1.0f : anchor;
}

void D3D9Draw::end_frame() {
    ++frame_;
    unsupported_draws_ = 0;
    flat_frames_ = frame_is_flat() ? flat_frames_ + 1 : 0;
    frame_writes_depth_ = false;
}

D3D9Draw::CachedTexture* D3D9Draw::texture_for_stage(uint32_t stage) {
    const uint32_t data = nv2a::reg(kNvTextureOffset + stage * kNvTextureStride);
    const uint32_t format = nv2a::reg(kNvTextureFormat + stage * kNvTextureStride);
    if (!format) return nullptr;
    // Linear formats keep their size only in the D3D object.
    const TextureStage& bound = texture_stage(stage);
    uint32_t size_field = 0;
    if (bound.texture) {
        const auto* pixels = from_xbox_ptr<XbPixelContainer>(bound.texture);
        if (pixels->resource.data == data) size_field = pixels->size;
    }
    TextureLayout layout;
    const bool described = describe_texture(format, size_field, layout);

    // Memory a render target uses: what was drawn there. Sampling the target being
    // drawn into goes through a copy.
    if (auto it = caches_->targets.find(data); it != caches_->targets.end()) {
        Target& t = it->second;
        t.sampled.texture = t.texture;
        if (data == target_data_) {
            if (!t.copy)
                device_->CreateTexture(t.surface_width, t.surface_height, 1, D3DUSAGE_RENDERTARGET, t.format,
                                       D3DPOOL_DEFAULT, &t.copy, nullptr);
            IDirect3DSurface9* copy_surface = nullptr;
            if (t.copy && SUCCEEDED(t.copy->GetSurfaceLevel(0, &copy_surface))) {
                device_->StretchRect(t.surface, nullptr, copy_surface, nullptr, D3DTEXF_NONE);
                copy_surface->Release();
                t.sampled.texture = t.copy;
            }
        }
        const bool linear = described ? layout.linear : true;
        t.sampled.scale[0] = linear ? 1.0f / t.width : 1.0f;
        t.sampled.scale[1] = linear ? 1.0f / t.height : 1.0f;
        return &t.sampled;
    }

    if (!described) {
        if (caches_->unsupported_formats.insert(format).second)
            LOG_WARN("render", "texture format register %08x not supported yet (stage %u)", format, stage);
        return nullptr;
    }
    const uint32_t* palette = nullptr;
    uint32_t palette_data = 0;
    if (layout.color_format == 0x0B && bound.palette) {  // P8
        palette_data = from_xbox_ptr<XbResource>(bound.palette)->data;
        palette = from_xbox_ptr<uint32_t>(palette_data | 0x80000000u);
    }

    const uint32_t key_words[4] = {data, format, size_field, palette_data};
    CachedTexture& entry = caches_->textures[hash_bytes(key_words, sizeof(key_words))];
    if (entry.texture && entry.checked_frame == frame_) return &entry;
    entry.checked_frame = frame_;
    const auto* memory = from_xbox_ptr<uint8_t>(data | 0x80000000u);
    uint64_t content = hash_bytes(memory, layout.total_size);
    if (palette) content = hash_bytes(palette, 256 * 4, content);
    const uint32_t version = replacement_version_ ? replacement_version_(content) : 0;
    if (entry.texture && entry.content == content && entry.replacement_version == version) return &entry;
    entry.content = content;
    entry.replacement_version = version;

    // A replacement has a size of its own, so the texture is made again whenever the
    // title's memory switches between a replaced picture and another.
    std::shared_ptr<const TextureReplacement> replacement;
    if (replace_texture_ && !layout.linear && !palette) replacement = replace_texture_(content, render_scale_y_);
    if (entry.texture && (replacement || entry.replaced)) {
        entry.texture->Release();
        entry.texture = nullptr;
    }
    entry.replaced = replacement && upload_replacement(entry, *replacement, layout.levels > 1);
    if (entry.replaced) return &entry;

    if (!entry.texture) {
        D3DFORMAT d3d_format = D3DFMT_A8R8G8B8;
        switch (layout.kind) {
            case TextureLayout::Kind::Dxt1: d3d_format = D3DFMT_DXT1; break;
            case TextureLayout::Kind::Dxt3: d3d_format = D3DFMT_DXT3; break;
            case TextureLayout::Kind::Dxt5: d3d_format = D3DFMT_DXT5; break;
            default: break;
        }
        if (FAILED(device_->CreateTexture(layout.width, layout.height, layout.levels, 0, d3d_format, D3DPOOL_MANAGED,
                                          &entry.texture, nullptr))) {
            LOG_ERROR("render", "CreateTexture %ux%u format %08x failed", layout.width, layout.height, format);
            entry.texture = nullptr;
            return nullptr;
        }
        if (layout.linear) {
            entry.scale[0] = 1.0f / layout.width;
            entry.scale[1] = 1.0f / layout.height;
        }
    }
    for (uint32_t level = 0; level < layout.levels; ++level) {
        D3DLOCKED_RECT locked;
        if (FAILED(entry.texture->LockRect(level, &locked, nullptr, 0))) continue;
        decode_level(layout, level, memory, palette, static_cast<uint8_t*>(locked.pBits),
                     static_cast<uint32_t>(locked.Pitch));
        entry.texture->UnlockRect(level);
    }
    return &entry;
}

bool D3D9Draw::upload_replacement(CachedTexture& entry, const TextureReplacement& replacement, bool mipmaps) {
    if (FAILED(device_->CreateTexture(replacement.width, replacement.height, mipmaps ? 0 : 1, 0, D3DFMT_A8R8G8B8,
                                      D3DPOOL_MANAGED, &entry.texture, nullptr))) {
        LOG_ERROR("render", "CreateTexture %ux%u for a replacement failed", replacement.width, replacement.height);
        entry.texture = nullptr;
        return false;
    }
    // Each level a 2x2 box filter of the one before.
    std::vector<uint32_t> level = replacement.pixels, next;
    uint32_t width = replacement.width, height = replacement.height;
    const DWORD levels = entry.texture->GetLevelCount();
    for (DWORD i = 0; i < levels; ++i) {
        if (i > 0) {
            const uint32_t w = std::max(width / 2, 1u), h = std::max(height / 2, 1u);
            next.assign(static_cast<size_t>(w) * h, 0);
            for (uint32_t y = 0; y < h; ++y)
                for (uint32_t x = 0; x < w; ++x) {
                    const uint32_t x0 = std::min(x * 2, width - 1), x1 = std::min(x * 2 + 1, width - 1);
                    const uint32_t y0 = std::min(y * 2, height - 1), y1 = std::min(y * 2 + 1, height - 1);
                    const uint32_t texels[4] = {level[y0 * width + x0], level[y0 * width + x1], level[y1 * width + x0],
                                                level[y1 * width + x1]};
                    uint32_t out = 0;
                    for (int shift = 0; shift < 32; shift += 8) {
                        uint32_t sum = 2;
                        for (uint32_t t : texels) sum += (t >> shift) & 0xFF;
                        out |= (sum / 4) << shift;
                    }
                    next[static_cast<size_t>(y) * w + x] = out;
                }
            level.swap(next);
            width = w;
            height = h;
        }
        D3DLOCKED_RECT locked;
        if (FAILED(entry.texture->LockRect(i, &locked, nullptr, 0))) continue;
        for (uint32_t y = 0; y < height; ++y)
            std::memcpy(static_cast<uint8_t*>(locked.pBits) + static_cast<size_t>(y) * locked.Pitch,
                        level.data() + static_cast<size_t>(y) * width, width * 4);
        entry.texture->UnlockRect(i);
    }
    entry.scale[0] = entry.scale[1] = 1.0f;
    return true;
}

namespace {

IDirect3DVertexDeclaration9* vertex_declaration(IDirect3DDevice9* device,
                                                std::unordered_map<uint32_t, IDirect3DVertexDeclaration9*>& cache,
                                                uint32_t mask) {
    auto& entry = cache[mask];
    if (entry) return entry;
    std::vector<D3DVERTEXELEMENT9> elements;
    WORD offset = 0;
    for (uint32_t i = 0; i < kMaxAttributes; ++i) {
        if (!(mask & (1u << i))) continue;
        elements.push_back({0, offset, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, static_cast<BYTE>(i)});
        offset += 16;
    }
    elements.push_back(D3DDECL_END());
    if (FAILED(device->CreateVertexDeclaration(elements.data(), &entry))) LOG_ERROR("render", "vertex declaration failed");
    return entry;
}

void log_tokens(const char* what, HRESULT hr, const std::vector<uint32_t>& tokens) {
    std::string text;
    for (size_t i = 0; i < tokens.size() && i < 256; ++i) {
        char buf[12];
        std::snprintf(buf, sizeof buf, "%08x ", tokens[i]);
        text += buf;
    }
    LOG_ERROR("render", "%s failed (%08x); tokens: %s", what, static_cast<unsigned>(hr), text.c_str());
}

}  // namespace

// Debugging: XB_DUMP_FRAME=n logs every draw of frame n (the screenshot of frame n-1),
// with each vertex program's microcode and c[-96..-77] once; XB_DUMP_STOP=k also drops
// that frame's draws after the k-th, to find the draw that breaks a picture.
void D3D9Draw::dump_draw(const DrawInput& input, const XbPixelShaderDef& def, uint64_t vs_key, uint32_t primitives) {
    const float* v = input.vertices;
    char textures[200] = "";
    size_t used = 0;
    for (uint32_t stage = 0; stage < 4; ++stage)
        if (d3d9::texture_mode(def, stage))
            used += std::snprintf(textures + used, sizeof(textures) - used, " t%u:mode %u fmt %08x @%08x", stage,
                                  d3d9::texture_mode(def, stage), nv2a::reg(kNvTextureFormat + stage * kNvTextureStride),
                                  nv2a::reg(kNvTextureOffset + stage * kNvTextureStride));
    static std::set<uint64_t> programs_dumped;
    if (programs_dumped.insert(vs_key).second) {
        const nv2a::State& nv = nv2a::state();
        const uint32_t start = std::min(nv2a::reg(kNvProgramStart), nv2a::kProgramSlots - 1);
        for (uint32_t i = start; i < nv2a::kProgramSlots; ++i) {
            LOG_INFO("dump", "vs %016llx %3u: %08x %08x %08x %08x", static_cast<unsigned long long>(vs_key), i - start,
                     nv.program[i][0], nv.program[i][1], nv.program[i][2], nv.program[i][3]);
            if (nv.program[i][3] & 1) break;
        }
        for (uint32_t c = 0; c < 20; ++c) {
            float f[4];
            std::memcpy(f, nv.constants[c].data(), 16);
            LOG_INFO("dump", "vs %016llx c[%d] = %g %g %g %g", static_cast<unsigned long long>(vs_key),
                     static_cast<int>(c) - 96, f[0], f[1], f[2], f[3]);
        }
    }
    const uint32_t* rs = render_state();
    LOG_INFO("dump",
             "target %08x prim %d x%u verts %u attrs %04x vs %016llx | comb %u final %08x/%08x rgb0 %08x->%08x"
             " | ztest %u zwrite %u zfunc %x zoffset %u %g/%g atest %u aref %u afunc %x blend %u %x/%x cull %x fog %u | v0 (%g %g %g %g)%s",
             target_data_, static_cast<int>(input.primitive), primitives, input.vertex_count, input.attribute_mask,
             static_cast<unsigned long long>(vs_key), def.combiner_count & 0xFF, def.final_inputs_abcd,
             def.final_inputs_efg, def.rgb_inputs[0], def.rgb_outputs[0], rs[kRsZEnable] && has_depth_,
             nv2a::reg(kNvDepthMask), nv2a::reg(kNvDepthFunc), nv2a::reg(kNvPolygonOffsetFillEnable),
             std::bit_cast<float>(nv2a::reg(kNvPolygonOffsetScaleFactor)),
             std::bit_cast<float>(nv2a::reg(kNvPolygonOffsetBias)), nv2a::reg(kNvAlphaTestEnable), nv2a::reg(kNvAlphaRef),
             nv2a::reg(kNvAlphaFunc), nv2a::reg(kNvBlendEnable), nv2a::reg(kNvBlendSrc), nv2a::reg(kNvBlendDst),
             rs[kRsCullMode], nv2a::reg(kNvFogEnable), v[0], v[1], v[2], v[3], textures);
}

bool D3D9Draw::magenta_shader() {
    if (magenta_) return true;
    const auto tokens = d3d9::solid_color_pixel_shader(1.0f, 0.0f, 1.0f, 1.0f);
    if (const HRESULT hr = device_->CreatePixelShader(reinterpret_cast<const DWORD*>(tokens.data()), &magenta_);
        FAILED(hr)) {
        static std::once_flag once;
        std::call_once(once, [&] { log_tokens("highlight CreatePixelShader", hr, tokens); });
        magenta_ = nullptr;
    }
    return magenta_ != nullptr;
}

void D3D9Draw::draw(const DrawInput& input) {
    Caches& caches = *caches_;
    const uint32_t vertex_count_or_indices = input.indices ? input.index_count : input.vertex_count;
    const uint32_t primitives = primitive_count(input.primitive, vertex_count_or_indices);
    if (!primitives || !target_ok_) return;
    const uint32_t width = target_width_, height = target_height_;

    // Vertex shader: the program from its start slot to the final instruction.
    const nv2a::State& nv = nv2a::state();
    const uint32_t start = std::min(nv2a::reg(kNvProgramStart), nv2a::kProgramSlots - 1);
    uint32_t count = 0;
    while (start + count < nv2a::kProgramSlots) {
        ++count;
        if (nv.program[start + count - 1][3] & 1) break;
    }
    const d3d9::FogMode fog = fog_mode();
    const uint64_t vs_key =
        hash_bytes(nv.program[start].data(), count * 16, input.attribute_mask | static_cast<uint64_t>(fog) << 32);
    if (caches.failed_shaders.contains(vs_key)) return;
    IDirect3DVertexShader9*& vs = caches.vertex_shaders[vs_key];
    if (!vs) {
        const auto tokens = d3d9::translate_vertex_program(
            reinterpret_cast<const uint32_t(*)[4]>(nv.program[start].data()), count, input.attribute_mask, fog);
        if (const HRESULT hr = device_->CreateVertexShader(reinterpret_cast<const DWORD*>(tokens.data()), &vs);
            FAILED(hr)) {
            log_tokens("CreateVertexShader", hr, tokens);
            vs = nullptr;
            caches.vertex_shaders.erase(vs_key);
            caches.failed_shaders.insert(vs_key);
            return;
        }
    }

    // Pixel shader: the combiners without their constants (those are shader constants).
    d3d9::PixelShaderKey ps_input{};
    ps_input.def = shader_state().pixel_shader_set ? combiners_from_registers() : fixed_function_combiners();
    const uint32_t* tss = texture_state();
    for (uint32_t stage = 0; stage < kTextureStages; ++stage)
        if (tss[stage * kTssPerStage + kTssAlphaKill]) ps_input.alpha_kill_mask |= 1u << stage;
    d3d9::PixelShaderKey ps_key = ps_input;
    std::memset(ps_key.def.constant0, 0, sizeof(ps_key.def.constant0));
    std::memset(ps_key.def.constant1, 0, sizeof(ps_key.def.constant1));
    ps_key.def.final_constant0 = ps_key.def.final_constant1 = 0;
    const uint64_t ps_hash = hash_bytes(&ps_key, sizeof(ps_key));
    if (caches.failed_shaders.contains(ps_hash)) return;
    IDirect3DPixelShader9*& ps = caches.pixel_shaders[ps_hash];
    if (!ps) {
        const auto tokens = d3d9::translate_pixel_shader(ps_key);
        if (const HRESULT hr = device_->CreatePixelShader(reinterpret_cast<const DWORD*>(tokens.data()), &ps);
            FAILED(hr)) {
            log_tokens("CreatePixelShader", hr, tokens);
            ps = nullptr;
            caches.pixel_shaders.erase(ps_hash);
            caches.failed_shaders.insert(ps_hash);
            return;
        }
    }

    // Vertex constants: the NV2A slots, then screen space -> clip space. Screen space is
    // the title's size; on a larger surface (render scale s per axis), D3D9 has its pixel centres
    // on integers, so the title's pixel x is centred at x * s + (s - 1) / 2.
    device_->SetVertexShaderConstantF(0, reinterpret_cast<const float*>(nv.constants.data()), nv2a::kConstantSlots);
    const float surface_width = static_cast<float>(target_surface_width_);
    const float surface_height = static_cast<float>(target_surface_height_);
    const float scale_x = surface_width / width, scale_y = surface_height / height;
    // UI laid out for the back buffer's shape over a wider 3D picture (in-game menus):
    // narrowed to that shape around an anchor a (a fraction of the width),
    // x' = a w + (x - a w) * k.
    const float anchor = narrow_anchor(input, width);
    const float narrow = anchor >= 0.0f ? narrow_factor_ : 1.0f;
    const float screen_to_clip[8] = {2.0f / width * narrow, -2.0f / height, 1.0f / input.depth_max, 0.0f,
                                     -1.0f + 2.0f * anchor * (1.0f - narrow) + (scale_x - 1) / surface_width,
                                     1.0f - (scale_y - 1) / surface_height, 0.0f, 0.0f};
    device_->SetVertexShaderConstantF(d3d9::kVsScreenToClip, screen_to_clip, 2);
    float fog_params[4] = {};
    std::memcpy(fog_params, &nv2a::reg(kNvFogParams), 8);
    device_->SetVertexShaderConstantF(d3d9::kVsFogParams, fog_params, 1);

    // Textures and samplers; linear textures scale their texel coordinates.
    float ps_constants[(d3d9::kPsFogColor + 1) * 4] = {};
    bool unsupported = false;  // something this draw needs is not translated yet
    for (uint32_t stage = 0; stage < kTextureStages; ++stage) {
        float* scale = ps_constants + (d3d9::kPsTexcoordScale + stage) * 4;
        scale[0] = scale[1] = scale[2] = scale[3] = 1.0f;
        const uint32_t mode = d3d9::texture_mode(ps_input.def, stage);
        IDirect3DBaseTexture9* texture = nullptr;
        if (d3d9::samples_2d(mode)) {
            if (CachedTexture* cached = texture_for_stage(stage)) {
                texture = cached->texture;
                scale[0] = cached->scale[0];
                scale[1] = cached->scale[1];
            } else if (nv2a::reg(kNvTextureFormat + stage * kNvTextureStride)) {
                unsupported = true;  // a format we cannot decode
            }
        } else if (mode == d3d9::kModeCubemap || mode == d3d9::kModeProject3D) {
            static std::once_flag once;
            warn_once(once, "cube and volume textures: not supported yet");
            unsupported = true;
        } else if (mode != d3d9::kModeNone && mode != d3d9::kModePassthru) {
            unsupported = true;  // the pixel shader reads zero for it (d3d9_shader.cpp)
        }
        device_->SetTexture(stage, texture);
        if (!texture) continue;
        const uint32_t* s = tss + stage * kTssPerStage;
        device_->SetSamplerState(stage, D3DSAMP_ADDRESSU, texture_address(s[kTssAddressU]));
        device_->SetSamplerState(stage, D3DSAMP_ADDRESSV, texture_address(s[kTssAddressV]));
        device_->SetSamplerState(stage, D3DSAMP_ADDRESSW, texture_address(s[kTssAddressW]));
        device_->SetSamplerState(stage, D3DSAMP_MAGFILTER, texture_filter(s[kTssMagFilter], false));
        DWORD min_filter = texture_filter(s[kTssMinFilter], false);
        DWORD max_anisotropy = std::max<uint32_t>(1, s[kTssMaxAnisotropy]);
        if (anisotropy_ > 1 && min_filter != D3DTEXF_POINT && texture->GetLevelCount() > 1) {
            min_filter = D3DTEXF_ANISOTROPIC;
            max_anisotropy = std::max<DWORD>(max_anisotropy, anisotropy_);
        }
        device_->SetSamplerState(stage, D3DSAMP_MINFILTER, min_filter);
        device_->SetSamplerState(stage, D3DSAMP_MIPFILTER, texture_filter(s[kTssMipFilter], true));
        device_->SetSamplerState(stage, D3DSAMP_MIPMAPLODBIAS, s[kTssMipMapLodBias]);
        device_->SetSamplerState(stage, D3DSAMP_MAXMIPLEVEL, s[kTssMaxMipLevel]);
        device_->SetSamplerState(stage, D3DSAMP_MAXANISOTROPY, max_anisotropy);
        device_->SetSamplerState(stage, D3DSAMP_BORDERCOLOR, s[kTssBorderColor]);
    }

    // Pixel constants: combiner factors per stage, final factors, texcoord scales.
    for (uint32_t i = 0; i < 8; ++i) {
        unpack_color(ps_input.def.constant0[i], ps_constants + (d3d9::kPsStageC0 + i) * 4);
        unpack_color(ps_input.def.constant1[i], ps_constants + (d3d9::kPsStageC1 + i) * 4);
    }
    unpack_color(ps_input.def.final_constant0, ps_constants + d3d9::kPsFinalC0 * 4);
    unpack_color(ps_input.def.final_constant1, ps_constants + d3d9::kPsFinalC1 * 4);
    const uint32_t fog_color = nv2a::reg(kNvFogColor);  // ABGR
    float* fog_rgba = ps_constants + d3d9::kPsFogColor * 4;
    fog_rgba[0] = (fog_color & 0xFF) / 255.0f;
    fog_rgba[1] = ((fog_color >> 8) & 0xFF) / 255.0f;
    fog_rgba[2] = ((fog_color >> 16) & 0xFF) / 255.0f;
    fog_rgba[3] = (fog_color >> 24) / 255.0f;
    device_->SetPixelShaderConstantF(0, ps_constants, d3d9::kPsFogColor + 1);

    // Raster state.
    const uint32_t* rs = render_state();
    auto reg = [](uint32_t method) { return nv2a::reg(method); };
    device_->SetRenderState(D3DRS_ZENABLE, rs[kRsZEnable] && has_depth_ ? D3DZB_TRUE : D3DZB_FALSE);
    if (rs[kRsZEnable] && has_depth_ && reg(kNvDepthMask)) frame_writes_depth_ = true;
    device_->SetRenderState(D3DRS_ZWRITEENABLE, reg(kNvDepthMask) != 0);
    device_->SetRenderState(D3DRS_ZFUNC, compare_func(reg(kNvDepthFunc)));
    device_->SetRenderState(D3DRS_ALPHATESTENABLE, reg(kNvAlphaTestEnable) != 0);
    device_->SetRenderState(D3DRS_ALPHAFUNC, compare_func(reg(kNvAlphaFunc)));
    device_->SetRenderState(D3DRS_ALPHAREF, reg(kNvAlphaRef) & 0xFF);
    device_->SetRenderState(D3DRS_ALPHABLENDENABLE, reg(kNvBlendEnable) != 0);
    device_->SetRenderState(D3DRS_SRCBLEND, blend_factor(reg(kNvBlendSrc)));
    device_->SetRenderState(D3DRS_DESTBLEND, blend_factor(reg(kNvBlendDst)));
    device_->SetRenderState(D3DRS_BLENDOP, blend_op(reg(kNvBlendEquation)));
    device_->SetRenderState(D3DRS_BLENDFACTOR, reg(kNvBlendColor));
    const uint32_t mask = reg(kNvColorMask);
    device_->SetRenderState(D3DRS_COLORWRITEENABLE, (mask & 0x00010000 ? D3DCOLORWRITEENABLE_RED : 0) |
                                                        (mask & 0x00000100 ? D3DCOLORWRITEENABLE_GREEN : 0) |
                                                        (mask & 0x00000001 ? D3DCOLORWRITEENABLE_BLUE : 0) |
                                                        (mask & 0x01000000 ? D3DCOLORWRITEENABLE_ALPHA : 0));
    device_->SetRenderState(D3DRS_CULLMODE, cull_mode(rs[kRsCullMode], rs[kRsFrontFace]));
    device_->SetRenderState(D3DRS_FILLMODE, fill_mode(rs[kRsFillMode]));
    device_->SetRenderState(D3DRS_STENCILENABLE, rs[kRsStencilEnable] != 0 && has_depth_);
    device_->SetRenderState(D3DRS_STENCILFAIL, stencil_op(rs[kRsStencilFail]));
    device_->SetRenderState(D3DRS_STENCILZFAIL, stencil_op(reg(kNvStencilOpZFail)));
    device_->SetRenderState(D3DRS_STENCILPASS, stencil_op(reg(kNvStencilOpZPass)));
    device_->SetRenderState(D3DRS_STENCILFUNC, compare_func(reg(kNvStencilFunc)));
    device_->SetRenderState(D3DRS_STENCILREF, reg(kNvStencilFuncRef));
    device_->SetRenderState(D3DRS_STENCILMASK, reg(kNvStencilFuncMask));
    device_->SetRenderState(D3DRS_STENCILWRITEMASK, reg(kNvStencilMask));
    device_->SetRenderState(D3DRS_SHADEMODE, reg(kNvShadeMode) == 0x1D00 ? D3DSHADE_FLAT : D3DSHADE_GOURAUD);
    // Polygon offset (D3D's ZBias; decals): the NV2A adds scale * depth slope + bias in
    // depth buffer units, D3D9 takes the bias as a fraction of the depth range.
    float offset_scale = 0.0f, offset_bias = 0.0f;
    if (reg(kNvPolygonOffsetFillEnable)) {
        std::memcpy(&offset_scale, &nv2a::reg(kNvPolygonOffsetScaleFactor), 4);
        std::memcpy(&offset_bias, &nv2a::reg(kNvPolygonOffsetBias), 4);
        offset_bias /= input.depth_max;
    }
    DWORD offset_bits;
    std::memcpy(&offset_bits, &offset_scale, 4);
    device_->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, offset_bits);
    std::memcpy(&offset_bits, &offset_bias, 4);
    device_->SetRenderState(D3DRS_DEPTHBIAS, offset_bits);

    // The whole target is the D3D9 viewport (the vertex program did the Xbox one);
    // the Xbox viewport rectangle clips.
    D3DVIEWPORT9 viewport{0, 0, target_surface_width_, target_surface_height_, 0.0f, 1.0f};
    device_->SetViewport(&viewport);
    const XbViewport& v = view_state().viewport;
    auto to_surface = [](uint32_t value, uint32_t limit, float scale) {
        return static_cast<LONG>(std::lround(std::min(value, limit) * scale));
    };
    RECT clip{to_surface(v.x, width, scale_x), to_surface(v.y, height, scale_y),
              to_surface(v.x + v.width, width, scale_x), to_surface(v.y + v.height, height, scale_y)};
    if (narrow != 1.0f) {
        const float at = surface_width * anchor;
        clip.left = static_cast<LONG>(std::lround(at + (clip.left - at) * narrow));
        clip.right = static_cast<LONG>(std::lround(at + (clip.right - at) * narrow));
    }
    device_->SetScissorRect(&clip);

    if (dump_frame_ && frame_ == dump_frame_) {
        dump_draw(input, ps_input.def, vs_key, primitives);
        if (++dump_index_ > dump_stop_) return;  // bisecting which draw breaks the frame
    }

    device_->SetVertexDeclaration(vertex_declaration(device_, caches.declarations, input.attribute_mask));
    device_->SetVertexShader(vs);
    device_->SetPixelShader(ps);
    if (unsupported) ++unsupported_draws_;
    if (unsupported && highlight_ && magenta_shader()) {
        device_->SetPixelShader(magenta_);
        device_->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        device_->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        device_->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    }
    const UINT stride = static_cast<UINT>(std::popcount(input.attribute_mask)) * 16;
    if (input.indices)
        device_->DrawIndexedPrimitiveUP(primitive_type(input.primitive), 0, input.vertex_count, primitives,
                                        input.indices, D3DFMT_INDEX32, input.vertices, stride);
    else
        device_->DrawPrimitiveUP(primitive_type(input.primitive), primitives, input.vertices, stride);
}

}  // namespace render
