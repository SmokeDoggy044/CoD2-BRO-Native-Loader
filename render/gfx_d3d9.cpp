#include "gfx_d3d9.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "d3d9_draw.h"
#include "d3d_internal.h"
#include "float_env.h"
#include "enhanced_graphics.h"
#include "log.h"
#include "texture_hash.h"
#include "title_shaders.h"
#include "xbox_d3d.h"

namespace render {
namespace {

template <typename T>
void release(T*& object) {
    if (object) object->Release();
    object = nullptr;
}

void warn_once(std::once_flag& once, const char* message) {
    std::call_once(once, [message] { LOG_WARN("render", "gfx: %s", message); });
}

// --- Translations ---

D3DFORMAT d3d_format(Format format) {
    switch (format) {
        case Format::BGRA8: return D3DFMT_A8R8G8B8;
        case Format::BGRX8: return D3DFMT_X8R8G8B8;
        case Format::B5G6R5: return D3DFMT_R5G6B5;
        case Format::BGR5A1: return D3DFMT_A1R5G5B5;
        case Format::BGRX5: return D3DFMT_X1R5G5B5;
        case Format::BGRA4: return D3DFMT_A4R4G4B4;
        case Format::L8: return D3DFMT_L8;
        case Format::A8: return D3DFMT_A8;
        case Format::A8L8: return D3DFMT_A8L8;
        case Format::DXT1: return D3DFMT_DXT1;
        case Format::DXT3: return D3DFMT_DXT3;
        case Format::DXT5: return D3DFMT_DXT5;
        case Format::D16: return D3DFMT_D16;
        case Format::D24S8: return D3DFMT_D24S8;
        case Format::Unknown: break;
    }
    return D3DFMT_UNKNOWN;
}

// The Xbox D3DFORMAT D3D9Draw keys a target's format on.
uint32_t xbox_format(Format format) {
    switch (format) {
        case Format::BGRX8: return kFmtX8R8G8B8;
        case Format::B5G6R5: return 0x05;
        case Format::BGR5A1: return 0x02;
        case Format::BGRX5: return 0x03;
        case Format::BGRA4: return 0x04;
        case Format::D16: return kFmtD16;
        case Format::D24S8: return kFmtD24S8;
        default: return kFmtA8R8G8B8;
    }
}

bool is_depth(Format format) { return format == Format::D16 || format == Format::D24S8; }
bool is_block(Format format) { return format == Format::DXT1 || format == Format::DXT3 || format == Format::DXT5; }

// Bytes per row of `width` pixels (per row of 4x4 blocks for DXT).
uint32_t row_bytes(Format format, uint32_t width) {
    switch (format) {
        case Format::BGRA8: case Format::BGRX8: return width * 4;
        case Format::B5G6R5: case Format::BGR5A1: case Format::BGRX5: case Format::BGRA4: case Format::A8L8:
            return width * 2;
        case Format::L8: case Format::A8: return width;
        case Format::DXT1: return (width + 3) / 4 * 8;
        case Format::DXT3: case Format::DXT5: return (width + 3) / 4 * 16;
        default: return 0;
    }
}

D3DCMPFUNC compare_func(Compare c) { return static_cast<D3DCMPFUNC>(static_cast<uint32_t>(c) + 1); }

D3DBLEND blend_factor(Blend b) {
    switch (b) {
        case Blend::Zero: return D3DBLEND_ZERO;
        case Blend::One: return D3DBLEND_ONE;
        case Blend::SrcColor: return D3DBLEND_SRCCOLOR;
        case Blend::InvSrcColor: return D3DBLEND_INVSRCCOLOR;
        case Blend::SrcAlpha: return D3DBLEND_SRCALPHA;
        case Blend::InvSrcAlpha: return D3DBLEND_INVSRCALPHA;
        case Blend::DestAlpha: return D3DBLEND_DESTALPHA;
        case Blend::InvDestAlpha: return D3DBLEND_INVDESTALPHA;
        case Blend::DestColor: return D3DBLEND_DESTCOLOR;
        case Blend::InvDestColor: return D3DBLEND_INVDESTCOLOR;
        case Blend::SrcAlphaSat: return D3DBLEND_SRCALPHASAT;
        // D3D9 has one blend factor colour: the alpha forms use it as well (the NV2A's
        // constant alpha is the factor's alpha, which callers replicate).
        case Blend::ConstantColor: case Blend::ConstantAlpha: return D3DBLEND_BLENDFACTOR;
        case Blend::InvConstantColor: case Blend::InvConstantAlpha: return D3DBLEND_INVBLENDFACTOR;
    }
    return D3DBLEND_ONE;
}

D3DBLENDOP blend_op(BlendOp op) {
    switch (op) {
        case BlendOp::Add: return D3DBLENDOP_ADD;
        case BlendOp::Subtract: return D3DBLENDOP_SUBTRACT;
        case BlendOp::RevSubtract: return D3DBLENDOP_REVSUBTRACT;
        case BlendOp::Min: return D3DBLENDOP_MIN;
        case BlendOp::Max: return D3DBLENDOP_MAX;
    }
    return D3DBLENDOP_ADD;
}

D3DSTENCILOP stencil_op(StencilOp op) { return static_cast<D3DSTENCILOP>(static_cast<uint32_t>(op) + 1); }

DWORD address(Address a) {
    switch (a) {
        case Address::Wrap: return D3DTADDRESS_WRAP;
        case Address::Mirror: return D3DTADDRESS_MIRROR;
        case Address::Clamp: return D3DTADDRESS_CLAMP;
        case Address::Border: return D3DTADDRESS_BORDER;
    }
    return D3DTADDRESS_WRAP;
}

DWORD filter(Filter f) {
    switch (f) {
        case Filter::None: return D3DTEXF_NONE;
        case Filter::Point: return D3DTEXF_POINT;
        case Filter::Linear: return D3DTEXF_LINEAR;
    }
    return D3DTEXF_LINEAR;
}

BYTE decl_type(AttributeType t) {
    switch (t) {
        case AttributeType::Float1: return D3DDECLTYPE_FLOAT1;
        case AttributeType::Float2: return D3DDECLTYPE_FLOAT2;
        case AttributeType::Float3: return D3DDECLTYPE_FLOAT3;
        case AttributeType::Float4: return D3DDECLTYPE_FLOAT4;
        case AttributeType::Color: return D3DDECLTYPE_D3DCOLOR;
        case AttributeType::UByte4: return D3DDECLTYPE_UBYTE4;
        case AttributeType::UByte4N: return D3DDECLTYPE_UBYTE4N;
        case AttributeType::Short2: return D3DDECLTYPE_SHORT2;
        case AttributeType::Short4: return D3DDECLTYPE_SHORT4;
        case AttributeType::Short2N: return D3DDECLTYPE_SHORT2N;
        case AttributeType::Short4N: return D3DDECLTYPE_SHORT4N;
        case AttributeType::Half2: return D3DDECLTYPE_FLOAT16_2;
        case AttributeType::Half4: return D3DDECLTYPE_FLOAT16_4;
    }
    return D3DDECLTYPE_FLOAT4;
}

// D3D9 primitive and primitive count for `n` vertices (or indices). Quads are drawn as
// indexed triangle lists from quad_indices_.
D3DPRIMITIVETYPE primitive_type(Primitive p) {
    switch (p) {
        case Primitive::Points: return D3DPT_POINTLIST;
        case Primitive::Lines: return D3DPT_LINELIST;
        case Primitive::LineStrip: return D3DPT_LINESTRIP;
        case Primitive::Triangles: case Primitive::Quads: return D3DPT_TRIANGLELIST;
        case Primitive::TriangleStrip: return D3DPT_TRIANGLESTRIP;
        case Primitive::TriangleFan: return D3DPT_TRIANGLEFAN;
    }
    return D3DPT_TRIANGLELIST;
}

uint32_t primitive_count(Primitive p, uint32_t n) {
    switch (p) {
        case Primitive::Points: return n;
        case Primitive::Lines: return n / 2;
        case Primitive::LineStrip: return n > 1 ? n - 1 : 0;
        case Primitive::Triangles: return n / 3;
        case Primitive::TriangleStrip: case Primitive::TriangleFan: return n > 2 ? n - 2 : 0;
        case Primitive::Quads: return n / 4 * 2;
    }
    return 0;
}

void unpack_color(uint32_t color, float* out) {
    out[0] = ((color >> 16) & 0xFF) / 255.0f;
    out[1] = ((color >> 8) & 0xFF) / 255.0f;
    out[2] = (color & 0xFF) / 255.0f;
    out[3] = (color >> 24) / 255.0f;
}

constexpr uint32_t kQuadLimit = 16384;          // quads per draw from quad_indices_ (16-bit indices)
constexpr uint32_t kUserVertexBytes = 4u << 20;  // draw_user's ring buffers
constexpr uint32_t kUserIndexBytes = 1u << 20;
// Keys of our own render targets in D3D9Draw (by Xbox address there): above anything the
// title's memory (128 MB) uses.
constexpr uint32_t kOwnTargetBase = 0xC0000000u;

class Impl final : public D3D9GfxDevice {
public:
    Impl(IDirect3DDevice9* device, D3D9Draw& draw, const BackendConfig& config)
        : device_(device), draw_(draw), narrow_ui_draw_(config.narrow_ui_draw), back_width_(config.width),
          back_height_(config.height) {
        device_->AddRef();
        Target back;
        back.desc = config.back_buffer;
        back_buffer_ = add_target(back);
        if (config.depth_stencil.data) {
            Target depth;
            depth.desc = config.depth_stencil;
            depth.depth = true;
            depth_buffer_ = add_target(depth);
        }
    }

    ~Impl() override {
        for (auto& [id, t] : textures_) release(t.texture);
        for (auto& [id, b] : buffers_) release(b.buffer);
        for (auto& [id, s] : shaders_) release(s.object);
        for (auto* d : layouts_) release(d);
        for (auto& [key, k] : kept_) release(k.buffer);
        release(user_vertices_);
        release(user_indices_);
        release(quad_indices_);
        release(staging_);
        release(device_);
    }

    bool open() override { return true; }

    void end_frame() override {
        // Kept vertices unused for a while go.
        if (draw_.frame() % 60) return;
        for (auto it = kept_.begin(); it != kept_.end();) {
            if (draw_.frame() - it->second.used_frame > 300) {
                if (it->second.buffer) kept_bytes_ -= it->second.bytes;
                release(it->second.buffer);
                it = kept_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // --- Textures ---

    TextureId create_texture(const TextureDesc& desc) override {
        platform::HostFloatEnv float_env;
        const D3DFORMAT format = d3d_format(desc.format);
        Texture t;
        t.desc = desc;
        HRESULT hr = E_FAIL;
        switch (desc.type) {
            case TextureType::Tex2D: {
                IDirect3DTexture9* texture = nullptr;
                hr = device_->CreateTexture(desc.width, desc.height, desc.levels, 0, format, D3DPOOL_MANAGED, &texture,
                                            nullptr);
                t.texture = texture;
                break;
            }
            case TextureType::Cube: {
                IDirect3DCubeTexture9* texture = nullptr;
                hr = device_->CreateCubeTexture(desc.width, desc.levels, 0, format, D3DPOOL_MANAGED, &texture, nullptr);
                t.texture = texture;
                break;
            }
            case TextureType::Volume: {
                IDirect3DVolumeTexture9* texture = nullptr;
                hr = device_->CreateVolumeTexture(desc.width, desc.height, desc.depth, desc.levels, 0, format,
                                                  D3DPOOL_MANAGED, &texture, nullptr);
                t.texture = texture;
                break;
            }
        }
        if (FAILED(hr) || !t.texture) {
            LOG_ERROR("render", "gfx: texture %ux%ux%u format %u: cannot create (%08x)", desc.width, desc.height,
                      desc.depth, static_cast<unsigned>(desc.format), static_cast<unsigned>(hr));
            return 0;
        }
        const TextureId id = next_id_++;
        textures_[id] = t;
        return id;
    }

    bool update_texture(TextureId id, uint32_t level, uint32_t face, const void* data, uint32_t pitch,
                        uint32_t slice_pitch) override {
        auto it = textures_.find(id);
        if (it == textures_.end() || !it->second.texture || !data) return false;
        platform::HostFloatEnv float_env;
        const Texture& t = it->second;
        const uint32_t width = std::max(1u, t.desc.width >> level);
        const uint32_t height = std::max(1u, t.desc.height >> level);
        const uint32_t rows = is_block(t.desc.format) ? (height + 3) / 4 : height;
        const uint32_t bytes = row_bytes(t.desc.format, width);
        const auto* source = static_cast<const uint8_t*>(data);
        if (t.desc.type == TextureType::Volume) {
            auto* texture = static_cast<IDirect3DVolumeTexture9*>(t.texture);
            const uint32_t depth = std::max(1u, t.desc.depth >> level);
            D3DLOCKED_BOX box;
            if (FAILED(texture->LockBox(level, &box, nullptr, 0))) return false;
            for (uint32_t z = 0; z < depth; ++z)
                for (uint32_t y = 0; y < rows; ++y)
                    std::memcpy(static_cast<uint8_t*>(box.pBits) + z * box.SlicePitch + y * box.RowPitch,
                                source + z * slice_pitch + y * pitch, bytes);
            texture->UnlockBox(level);
            return true;
        }
        D3DLOCKED_RECT locked;
        HRESULT hr;
        if (t.desc.type == TextureType::Cube)
            hr = static_cast<IDirect3DCubeTexture9*>(t.texture)
                     ->LockRect(static_cast<D3DCUBEMAP_FACES>(face), level, &locked, nullptr, 0);
        else
            hr = static_cast<IDirect3DTexture9*>(t.texture)->LockRect(level, &locked, nullptr, 0);
        if (FAILED(hr)) return false;
        for (uint32_t y = 0; y < rows; ++y)
            std::memcpy(static_cast<uint8_t*>(locked.pBits) + y * locked.Pitch, source + y * pitch, bytes);
        if (t.desc.type == TextureType::Cube)
            static_cast<IDirect3DCubeTexture9*>(t.texture)->UnlockRect(static_cast<D3DCUBEMAP_FACES>(face), level);
        else
            static_cast<IDirect3DTexture9*>(t.texture)->UnlockRect(level);
        return true;
    }

    void destroy_texture(TextureId id) override {
        auto it = textures_.find(id);
        if (it == textures_.end()) return;
        unbind_texture(id);
        release(it->second.texture);
        if (it->second.xbox_header) xbox_textures_.erase(xbox_key(it->second.xbox_header, it->second.xbox_palette));
        textures_.erase(it);
    }

    // --- Buffers ---

    BufferId create_buffer(const BufferDesc& desc, const void* initial) override {
        if (!desc.bytes) return 0;
        platform::HostFloatEnv float_env;
        Buffer b;
        b.desc = desc;
        const DWORD usage = D3DUSAGE_WRITEONLY | (desc.usage == BufferUsage::Dynamic ? D3DUSAGE_DYNAMIC : 0) |
                            software_usage();
        const D3DPOOL pool = desc.usage == BufferUsage::Dynamic ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED;
        HRESULT hr;
        if (desc.kind == BufferKind::Vertex) {
            IDirect3DVertexBuffer9* buffer = nullptr;
            hr = device_->CreateVertexBuffer(desc.bytes, usage, 0, pool, &buffer, nullptr);
            b.buffer = buffer;
        } else {
            IDirect3DIndexBuffer9* buffer = nullptr;
            hr = device_->CreateIndexBuffer(desc.bytes, usage,
                                            desc.kind == BufferKind::Index16 ? D3DFMT_INDEX16 : D3DFMT_INDEX32, pool,
                                            &buffer, nullptr);
            b.buffer = buffer;
        }
        if (FAILED(hr) || !b.buffer) {
            LOG_ERROR("render", "gfx: buffer of %u bytes: cannot create (%08x)", desc.bytes, static_cast<unsigned>(hr));
            return 0;
        }
        const BufferId id = next_id_++;
        buffers_[id] = b;
        if (initial) {
            if (void* memory = map_buffer(id, 0, desc.bytes, MapMode::Write)) {
                std::memcpy(memory, initial, desc.bytes);
                unmap_buffer(id);
            }
        }
        return id;
    }

    void* map_buffer(BufferId id, uint32_t offset, uint32_t bytes, MapMode mode) override {
        auto it = buffers_.find(id);
        if (it == buffers_.end() || offset + bytes > it->second.desc.bytes || it->second.mapped) return nullptr;
        platform::HostFloatEnv float_env;
        Buffer& b = it->second;
        DWORD flags = 0;
        if (b.desc.usage == BufferUsage::Dynamic) {
            if (mode == MapMode::Discard) flags = D3DLOCK_DISCARD;
            if (mode == MapMode::NoOverwrite) flags = D3DLOCK_NOOVERWRITE;
        }
        void* memory = nullptr;
        const HRESULT hr =
            b.desc.kind == BufferKind::Vertex
                ? static_cast<IDirect3DVertexBuffer9*>(b.buffer)->Lock(offset, bytes, &memory, flags)
                : static_cast<IDirect3DIndexBuffer9*>(b.buffer)->Lock(offset, bytes, &memory, flags);
        if (FAILED(hr)) return nullptr;
        b.mapped = true;
        return memory;
    }

    void unmap_buffer(BufferId id) override {
        auto it = buffers_.find(id);
        if (it == buffers_.end() || !it->second.mapped) return;
        platform::HostFloatEnv float_env;
        Buffer& b = it->second;
        if (b.desc.kind == BufferKind::Vertex)
            static_cast<IDirect3DVertexBuffer9*>(b.buffer)->Unlock();
        else
            static_cast<IDirect3DIndexBuffer9*>(b.buffer)->Unlock();
        b.mapped = false;
    }

    void destroy_buffer(BufferId id) override {
        auto it = buffers_.find(id);
        if (it == buffers_.end()) return;
        for (auto& s : streams_)
            if (s.buffer == id) s = {};
        if (index_buffer_ == id) index_buffer_ = 0;
        unmap_buffer(id);
        release(it->second.buffer);
        buffers_.erase(it);
    }

    // --- Shaders and layouts ---

    ShaderId create_shader(ShaderStage stage, const ShaderCode& code) override {
        if (!code.d3d9) {
            LOG_ERROR("render", "gfx: shader without Direct3D 9 code");
            return 0;
        }
        platform::HostFloatEnv float_env;
        Shader s;
        s.stage = stage;
        HRESULT hr;
        if (stage == ShaderStage::Vertex) {
            IDirect3DVertexShader9* shader = nullptr;
            hr = device_->CreateVertexShader(static_cast<const DWORD*>(code.d3d9), &shader);
            s.object = shader;
        } else {
            IDirect3DPixelShader9* shader = nullptr;
            hr = device_->CreatePixelShader(static_cast<const DWORD*>(code.d3d9), &shader);
            s.object = shader;
        }
        if (FAILED(hr) || !s.object) {
            LOG_ERROR("render", "gfx: %s shader: cannot create (%08x)", stage == ShaderStage::Vertex ? "vertex" : "pixel",
                      static_cast<unsigned>(hr));
            return 0;
        }
        const ShaderId id = next_id_++;
        shaders_[id] = s;
        return id;
    }

    ShaderId create_title_shader(ShaderStage stage, uint32_t key, uint32_t variant) override {
        const char kind = stage == ShaderStage::Vertex ? 'v' : 'p';
        for (unsigned i = 0; i < kTitleShaderCount; ++i)
            if (kTitleShaders[i].stage == kind && kTitleShaders[i].key == key && kTitleShaders[i].variant == variant)
                return create_shader(stage, {kTitleShaders[i].d3d9, kTitleShaders[i].d3d9_bytes});
        LOG_ERROR("render", "gfx: no title %s shader %08x (variant %08x)", kind == 'v' ? "vertex" : "pixel", key, variant);
        return 0;
    }

    void destroy_shader(ShaderId id) override {
        auto it = shaders_.find(id);
        if (it == shaders_.end()) return;
        if (vertex_shader_ == id) vertex_shader_ = 0;
        if (pixel_shader_ == id) pixel_shader_ = 0;
        release(it->second.object);
        shaders_.erase(it);
    }

    LayoutId create_layout(const VertexLayout& layout) override {
        if (layout.count > kGfxVertexInputs) return 0;
        platform::HostFloatEnv float_env;
        D3DVERTEXELEMENT9 elements[kGfxVertexInputs + 1];
        for (uint32_t i = 0; i < layout.count; ++i) {
            const VertexAttribute& a = layout.attributes[i];
            if (a.input >= kGfxVertexInputs || a.stream >= kGfxVertexStreams) return 0;
            elements[i] = {a.stream, a.offset, decl_type(a.type), D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, a.input};
        }
        elements[layout.count] = D3DDECL_END();
        IDirect3DVertexDeclaration9* declaration = nullptr;
        if (FAILED(device_->CreateVertexDeclaration(elements, &declaration))) {
            LOG_ERROR("render", "gfx: vertex layout of %u attributes: cannot create", layout.count);
            return 0;
        }
        layouts_.push_back(declaration);
        layout_attributes_.emplace_back(layout.attributes, layout.attributes + layout.count);
        return static_cast<LayoutId>(layouts_.size());
    }

    // --- Render targets ---

    TargetId create_target(const TargetDesc& desc) override {
        if (!desc.width || !desc.height || d3d_format(desc.format) == D3DFMT_UNKNOWN) return 0;
        Target t;
        t.depth = is_depth(desc.format);
        t.desc.width = desc.width;
        t.desc.height = desc.height;
        t.desc.format = xbox_format(desc.format);
        t.desc.data = desc.xbox_data ? desc.xbox_data : kOwnTargetBase + (next_id_ << 4);
        return add_target(t);
    }

    void destroy_target(TargetId id) override {
        auto it = targets_.find(id);
        if (it == targets_.end() || id == back_buffer_ || id == depth_buffer_) return;
        if (it->second.texture) destroy_texture(it->second.texture);
        if (color_target_ == id || depth_target_ == id) set_target(back_buffer_, depth_buffer_);
        if (!it->second.depth) draw_.forget_render_target(it->second.desc.data);
        targets_.erase(it);
    }

    TextureId target_texture(TargetId id) override {
        auto it = targets_.find(id);
        if (it == targets_.end() || it->second.depth) return 0;
        Target& t = it->second;
        if (!t.texture) {
            t.texture = next_id_++;
            Texture texture;
            texture.target = id;
            textures_[t.texture] = texture;
        }
        return t.texture;
    }

    TargetId back_buffer() override { return back_buffer_; }
    TargetId depth_buffer() override { return depth_buffer_; }

    void set_target(TargetId color, TargetId depth) override {
        auto c = targets_.find(color);
        if (c == targets_.end() || c->second.depth) return;
        auto d = targets_.find(depth);
        const SurfaceDesc* depth_desc = d != targets_.end() && d->second.depth ? &d->second.desc : nullptr;
        platform::HostFloatEnv float_env;
        draw_.set_render_target(c->second.desc, depth_desc);
        color_target_ = color;
        depth_target_ = depth_desc ? depth : 0;
        viewport_ = {};
        scissor_on_ = false;
    }

    void clear(uint32_t flags, uint32_t color, float depth, uint32_t stencil) override {
        begin_draw();  // the Xbox path's CPU writes to the back buffer first (when it has a device)
        if (!draw_.target_ok()) return;
        platform::HostFloatEnv float_env;
        DWORD d3d_flags = 0;
        if (flags & kGfxClearColor) d3d_flags |= D3DCLEAR_TARGET;
        if ((flags & kGfxClearDepth) && draw_.has_depth()) d3d_flags |= D3DCLEAR_ZBUFFER;
        if ((flags & kGfxClearStencil) && draw_.has_depth()) d3d_flags |= D3DCLEAR_STENCIL;
        if (!d3d_flags) return;
        // The whole target: the scissor test (always on, D3D9Draw) limits Clear too.
        const RECT whole{0, 0, static_cast<LONG>(draw_.target_surface_width()),
                         static_cast<LONG>(draw_.target_surface_height())};
        device_->SetScissorRect(&whole);
        device_->Clear(0, nullptr, d3d_flags, color, depth, stencil);
    }

    // --- State ---

    void set_viewport(const Viewport& viewport) override { viewport_ = viewport; }
    void set_scissor(const Rect* rect) override {
        scissor_on_ = rect != nullptr;
        if (rect) scissor_ = *rect;
    }
    void set_blend(const BlendState& state) override { blend_ = state; }
    void set_depth_stencil(const DepthStencilState& state) override { depth_stencil_ = state; }
    void set_raster(const RasterState& state) override { raster_ = state; }
    void set_fog(const Fog& fog) override { fog_ = fog; }
    void set_texture(uint32_t slot, TextureId texture) override {
        if (slot < kGfxTextureSlots) bound_textures_[slot] = texture;
    }
    void set_sampler(uint32_t slot, const SamplerState& state) override {
        if (slot < kGfxTextureSlots) samplers_[slot] = state;
    }
    void set_shaders(ShaderId vertex, ShaderId pixel) override {
        vertex_shader_ = vertex;
        pixel_shader_ = pixel;
    }
    void set_constants(ShaderStage stage, uint32_t first, const float* values, uint32_t count) override {
        auto& constants = stage == ShaderStage::Vertex ? vs_constants_ : ps_constants_;
        const uint32_t limit = stage == ShaderStage::Vertex ? kGfxVsScreenToClip : kGfxPsTexcoordScale;
        if (first >= limit) return;
        count = std::min(count, limit - first);
        std::memcpy(constants.data() + first * 4, values, count * 16);
    }
    void set_layout(LayoutId layout) override { layout_ = layout; }
    void set_vertex_buffer(uint32_t stream, BufferId buffer, uint32_t offset, uint32_t stride) override {
        if (stream < kGfxVertexStreams) streams_[stream] = {buffer, offset, stride};
    }
    void set_index_buffer(BufferId buffer) override { index_buffer_ = buffer; }

    // --- Draws ---

    void draw(Primitive primitive, uint32_t first_vertex, uint32_t vertex_count) override {
        platform::HostFloatEnv float_env;
        const uint32_t primitives = primitive_count(primitive, vertex_count);
        if (!primitives || !prepare(primitive)) return;
        if (!bind_streams()) return;
        if (primitive == Primitive::Quads) {
            draw_quads(first_vertex, vertex_count);
            return;
        }
        device_->DrawPrimitive(primitive_type(primitive), first_vertex, primitives);
    }

    void draw_indexed(Primitive primitive, uint32_t first_index, uint32_t index_count, int32_t base_vertex) override {
        platform::HostFloatEnv float_env;
        if (primitive == Primitive::Quads) {
            static std::once_flag once;
            warn_once(once, "indexed quads are not supported (draw them as triangles)");
            return;
        }
        const uint32_t primitives = primitive_count(primitive, index_count);
        auto ib = buffers_.find(index_buffer_);
        if (!primitives || ib == buffers_.end() || ib->second.desc.kind == BufferKind::Vertex || !prepare(primitive))
            return;
        if (!bind_streams()) return;
        device_->SetIndices(static_cast<IDirect3DIndexBuffer9*>(ib->second.buffer));
        // Any vertex may be referenced: the whole of stream 0's buffer.
        const uint32_t vertices = stream_vertex_count(0);
        device_->DrawIndexedPrimitive(primitive_type(primitive), base_vertex, 0, vertices, first_index, primitives);
    }

    void draw_user(Primitive primitive, const void* vertices, uint32_t vertex_count, uint32_t stride,
                   const uint16_t* indices, uint32_t index_count, uint64_t cache_key) override {
        platform::HostFloatEnv float_env;
        const uint32_t primitives = primitive_count(primitive, indices ? index_count : vertex_count);
        if (!primitives || !vertices || !stride || (indices && primitive == Primitive::Quads)) return;
        float x_range[2];
        const bool has_range = position_range(vertices, vertex_count, stride, x_range);
        if (!prepare(primitive, has_range ? x_range : nullptr) || !create_user_buffers()) return;
        const uint32_t bytes = vertex_count * stride;
        const uint32_t index_bytes = indices ? index_count * 2 : 0;
        if (bytes > kUserVertexBytes || index_bytes > kUserIndexBytes) {
            static std::once_flag once;
            warn_once(once, "draw_user: more vertices than its stream buffer holds");
            return;
        }
        uint32_t first_vertex = 0;
        if (IDirect3DVertexBuffer9* kept = kept_vertices(cache_key, vertices, bytes)) {
            device_->SetStreamSource(0, kept, 0, stride);
        } else {
            // Vertices at a multiple of the stride, so they are addressed by vertex index.
            uint32_t offset = (user_vertex_offset_ + stride - 1) / stride * stride;
            DWORD flags = D3DLOCK_NOOVERWRITE;
            if (offset + bytes > kUserVertexBytes) {
                offset = 0;
                flags = D3DLOCK_DISCARD;
            }
            void* memory = nullptr;
            if (FAILED(user_vertices_->Lock(offset, bytes, &memory, flags))) return;
            std::memcpy(memory, vertices, bytes);
            user_vertices_->Unlock();
            user_vertex_offset_ = offset + bytes;
            device_->SetStreamSource(0, user_vertices_, 0, stride);
            first_vertex = offset / stride;
        }
        for (uint32_t s = 1; s < kGfxVertexStreams; ++s) device_->SetStreamSource(s, nullptr, 0, 0);
        if (primitive == Primitive::Quads) {
            draw_quads(first_vertex, vertex_count);
            return;
        }
        if (!indices) {
            device_->DrawPrimitive(primitive_type(primitive), first_vertex, primitives);
            return;
        }
        uint32_t index_offset = user_index_offset_;
        DWORD flags = D3DLOCK_NOOVERWRITE;
        void* memory = nullptr;
        if (index_offset + index_bytes > kUserIndexBytes) {
            index_offset = 0;
            flags = D3DLOCK_DISCARD;
        }
        if (FAILED(user_indices_->Lock(index_offset, index_bytes, &memory, flags))) return;
        std::memcpy(memory, indices, index_bytes);
        user_indices_->Unlock();
        user_index_offset_ = index_offset + index_bytes;
        device_->SetIndices(user_indices_);
        device_->DrawIndexedPrimitive(primitive_type(primitive), static_cast<INT>(first_vertex), 0, vertex_count,
                                      index_offset / 2, primitives);
    }

    // --- Diagnostics ---

    bool read_target(TargetId id, uint32_t* pixels, uint32_t width, uint32_t height) override {
        auto it = targets_.find(id);
        if (it == targets_.end() || it->second.depth || !pixels || !width || !height) return false;
        platform::HostFloatEnv float_env;
        IDirect3DSurface9* surface = draw_.target_surface(it->second.desc.data);
        if (!surface) return false;
        D3DSURFACE_DESC desc;
        surface->GetDesc(&desc);
        if (staging_) {
            D3DSURFACE_DESC staged;
            staging_->GetDesc(&staged);
            if (staged.Width != desc.Width || staged.Height != desc.Height || staged.Format != desc.Format)
                release(staging_);
        }
        if (!staging_ && FAILED(device_->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format,
                                                                     D3DPOOL_SYSTEMMEM, &staging_, nullptr)))
            return false;
        if (FAILED(device_->GetRenderTargetData(surface, staging_))) return false;
        D3DLOCKED_RECT locked;
        if (FAILED(staging_->LockRect(&locked, nullptr, D3DLOCK_READONLY))) return false;
        const bool wide = desc.Format == D3DFMT_A8R8G8B8 || desc.Format == D3DFMT_X8R8G8B8;
        for (uint32_t y = 0; y < height; ++y) {
            // The pixel centre's surface pixel (render scale).
            const uint32_t sy = std::min(desc.Height - 1, static_cast<uint32_t>((y + 0.5) * desc.Height / height));
            const auto* row = static_cast<const uint8_t*>(locked.pBits) + sy * locked.Pitch;
            for (uint32_t x = 0; x < width; ++x) {
                const uint32_t sx = std::min(desc.Width - 1, static_cast<uint32_t>((x + 0.5) * desc.Width / width));
                uint32_t value = 0;
                if (wide) {
                    std::memcpy(&value, row + sx * 4, 4);
                    if (desc.Format == D3DFMT_X8R8G8B8) value |= 0xFF000000u;
                }
                pixels[y * width + x] = value;
            }
        }
        staging_->UnlockRect();
        if (!wide) {
            static std::once_flag once;
            warn_once(once, "read_target: only 32-bit targets are read");
        }
        return wide;
    }

    // --- Transition ---

    TextureId xbox_texture(uint32_t texture_header, uint32_t palette_header) override {
        if (!texture_header) return 0;
        TextureId& id = xbox_textures_[xbox_key(texture_header, palette_header)];
        if (!id) {
            id = next_id_++;
            Texture t;
            t.xbox_header = texture_header;
            t.xbox_palette = palette_header;
            textures_[id] = t;
        }
        return id;
    }

private:
    struct Texture {
        IDirect3DBaseTexture9* texture = nullptr;  // ours
        TextureDesc desc;
        TargetId target = 0;        // a target's texture
        uint32_t xbox_header = 0;   // a title texture (D3D9Draw's cache)
        uint32_t xbox_palette = 0;
    };
    struct Buffer {
        IUnknown* buffer = nullptr;  // IDirect3DVertexBuffer9 / IDirect3DIndexBuffer9
        BufferDesc desc;
        bool mapped = false;
    };
    struct Shader {
        IUnknown* object = nullptr;  // IDirect3DVertexShader9 / IDirect3DPixelShader9
        ShaderStage stage = ShaderStage::Vertex;
    };
    struct Target {
        SurfaceDesc desc;  // D3D9Draw's key: memory address, title size, Xbox format
        bool depth = false;
        TextureId texture = 0;  // target_texture
    };
    struct Stream {
        BufferId buffer = 0;
        uint32_t offset = 0, stride = 0;
    };

    static uint64_t xbox_key(uint32_t header, uint32_t palette) { return uint64_t{palette} << 32 | header; }

    TargetId add_target(const Target& t) {
        const TargetId id = next_id_++;
        targets_[id] = t;
        return id;
    }

    DWORD software_usage() const { return device_->GetSoftwareVertexProcessing() ? D3DUSAGE_SOFTWAREPROCESSING : 0; }

    void unbind_texture(TextureId id) {
        for (auto& bound : bound_textures_)
            if (bound == id) bound = 0;
    }

    uint32_t stream_vertex_count(uint32_t stream) {
        const Stream& s = streams_[stream];
        auto it = buffers_.find(s.buffer);
        if (it == buffers_.end() || !s.stride || s.offset >= it->second.desc.bytes) return 0;
        return (it->second.desc.bytes - s.offset) / s.stride;
    }

    bool bind_streams() {
        for (uint32_t s = 0; s < kGfxVertexStreams; ++s) {
            auto it = buffers_.find(streams_[s].buffer);
            if (it == buffers_.end() || it->second.desc.kind != BufferKind::Vertex) {
                device_->SetStreamSource(s, nullptr, 0, 0);
                if (s == 0) return false;
                continue;
            }
            device_->SetStreamSource(s, static_cast<IDirect3DVertexBuffer9*>(it->second.buffer), streams_[s].offset,
                                     streams_[s].stride);
        }
        return true;
    }

    bool create_user_buffers() {
        if (user_vertices_) return true;
        if (user_buffers_failed_) return false;
        const DWORD usage = D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY | software_usage();
        if (FAILED(device_->CreateVertexBuffer(kUserVertexBytes, usage, 0, D3DPOOL_DEFAULT, &user_vertices_, nullptr)) ||
            FAILED(device_->CreateIndexBuffer(kUserIndexBytes, usage, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &user_indices_,
                                              nullptr))) {
            LOG_ERROR("render", "gfx: draw_user's stream buffers: cannot create");
            release(user_vertices_);
            release(user_indices_);
            user_buffers_failed_ = true;
            return false;
        }
        return true;
    }

    // draw_user's cache: the vertices of `key` from a kept copy while they stay the same (made
    // the second time the same contents come), else null (streamed).
    IDirect3DVertexBuffer9* kept_vertices(uint64_t key, const void* vertices, uint32_t bytes) {
        if (!key) return nullptr;
        const uint64_t content = hash_bytes(vertices, bytes);
        Kept& k = kept_[key];
        k.used_frame = draw_.frame();
        if (k.buffer && k.content == content && k.bytes == bytes) return k.buffer;
        if (k.content != content || k.bytes != bytes) {
            k.content = content;
            k.bytes = bytes;
            release(k.buffer);  // changed: streamed until it repeats
            return nullptr;
        }
        void* memory = nullptr;
        if (FAILED(device_->CreateVertexBuffer(bytes, D3DUSAGE_WRITEONLY | software_usage(), 0, D3DPOOL_MANAGED,
                                               &k.buffer, nullptr)) ||
            FAILED(k.buffer->Lock(0, bytes, &memory, 0))) {
            release(k.buffer);
            return nullptr;
        }
        std::memcpy(memory, vertices, bytes);
        k.buffer->Unlock();
        kept_bytes_ += bytes;
        return k.buffer;
    }

    // Quads from `first_vertex` with the shared quad index buffer (v0 v1 v2, v0 v2 v3 each).
    void draw_quads(uint32_t first_vertex, uint32_t vertex_count) {
        if (!quad_indices_) {
            std::vector<uint16_t> indices(kQuadLimit * 6);
            for (uint32_t q = 0; q < kQuadLimit; ++q) {
                const auto v = static_cast<uint16_t>(q * 4);
                const uint16_t quad[6] = {v, static_cast<uint16_t>(v + 1), static_cast<uint16_t>(v + 2), v,
                                          static_cast<uint16_t>(v + 2), static_cast<uint16_t>(v + 3)};
                std::memcpy(&indices[q * 6], quad, sizeof(quad));
            }
            void* memory = nullptr;
            if (FAILED(device_->CreateIndexBuffer(static_cast<UINT>(indices.size() * 2), D3DUSAGE_WRITEONLY | software_usage(),
                                                  D3DFMT_INDEX16, D3DPOOL_MANAGED, &quad_indices_, nullptr)) ||
                FAILED(quad_indices_->Lock(0, 0, &memory, 0))) {
                LOG_ERROR("render", "gfx: quad index buffer: cannot create");
                release(quad_indices_);
                return;
            }
            std::memcpy(memory, indices.data(), indices.size() * 2);
            quad_indices_->Unlock();
        }
        device_->SetIndices(quad_indices_);
        for (uint32_t done = 0; done + 4 <= vertex_count; done += kQuadLimit * 4) {
            const uint32_t quads = std::min(kQuadLimit, (vertex_count - done) / 4);
            device_->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, static_cast<INT>(first_vertex + done), 0, quads * 4, 0,
                                          quads * 2);
        }
    }

    // The D3D9 texture behind `id` now and its texel scale; null if there is none.
    IDirect3DBaseTexture9* resolve_texture(TextureId id, float (&scale)[2]) {
        scale[0] = scale[1] = 1.0f;
        auto it = textures_.find(id);
        if (it == textures_.end()) return nullptr;
        const Texture& t = it->second;
        if (t.texture) return t.texture;
        if (t.xbox_header) return draw_.xbox_texture(t.xbox_header, t.xbox_palette, scale);
        if (t.target) {
            auto target = targets_.find(t.target);
            return target == targets_.end() ? nullptr : draw_.sample_target(target->second.desc.data);
        }
        return nullptr;
    }

    // Everything but the streams, for a draw now; false if it cannot draw.
    // The x range of input 0 (screen-space positions) of caller vertices; false if the layout has
    // no float position there.
    bool position_range(const void* vertices, uint32_t count, uint32_t stride, float (&range)[2]) const {
        if (layout_ == 0 || layout_ > layout_attributes_.size() || !count) return false;
        for (const VertexAttribute& a : layout_attributes_[layout_ - 1]) {
            if (a.input != 0 || a.stream != 0 || a.type < AttributeType::Float1 || a.type > AttributeType::Float4)
                continue;
            const auto* bytes = static_cast<const uint8_t*>(vertices) + a.offset;
            std::memcpy(&range[0], bytes, 4);
            range[1] = range[0];
            for (uint32_t i = 1; i < count; ++i) {
                float x;
                std::memcpy(&x, bytes + i * stride, 4);
                range[0] = std::min(range[0], x);
                range[1] = std::max(range[1], x);
            }
            return true;
        }
        return false;
    }

    // UI laid out for a narrower shape than the picture's, drawn into the back buffer over a 3D
    // scene: how it is narrowed (BackendConfig::narrow_ui_draw, as D3D9Draw::narrow_ui).
    UiNarrowing narrowing(const float* x_range) const {
        if (!x_range || !narrow_ui_draw_ || draw_.frame_is_flat() || draw_.target_width() != back_width_ ||
            draw_.target_height() != back_height_)
            return {};
        const UiNarrowing n = narrow_ui_draw_();
        if (n.anchor < 0.0f || n.factor >= 1.0f) return {};
        // Full-width quads (dimming, letterbox bars) keep covering the picture.
        return x_range[0] <= 1.0f && x_range[1] >= static_cast<float>(back_width_) - 1.0f ? UiNarrowing{} : n;
    }

    bool prepare(Primitive primitive, const float* x_range = nullptr) {
        (void)primitive;
        begin_draw();
        if (!draw_.target_ok()) return false;
        auto vs = shaders_.find(vertex_shader_);
        auto ps = shaders_.find(pixel_shader_);
        if (vs == shaders_.end() || ps == shaders_.end() || layout_ == 0 || layout_ > layouts_.size()) {
            static std::once_flag once;
            warn_once(once, "draw without shaders or vertex layout (skipped)");
            return false;
        }

        // Viewport: in title pixels, scaled to the surface.
        const float width = static_cast<float>(draw_.target_width());
        const float height = static_cast<float>(draw_.target_height());
        const float surface_width = static_cast<float>(draw_.target_surface_width());
        const float surface_height = static_cast<float>(draw_.target_surface_height());
        const float sx = surface_width / width, sy = surface_height / height;
        Viewport v = viewport_;
        if (!v.width || !v.height) {
            v.x = v.y = 0;
            v.width = draw_.target_width();
            v.height = draw_.target_height();
        }
        auto to_surface = [](float value, float scale, float limit) {
            return static_cast<DWORD>(std::clamp(std::lround(value * scale), 0l, static_cast<long>(limit)));
        };
        D3DVIEWPORT9 viewport;
        viewport.X = to_surface(static_cast<float>(v.x), sx, surface_width);
        viewport.Y = to_surface(static_cast<float>(v.y), sy, surface_height);
        viewport.Width = std::max<DWORD>(1, to_surface(static_cast<float>(v.x + v.width), sx, surface_width) - viewport.X);
        viewport.Height =
            std::max<DWORD>(1, to_surface(static_cast<float>(v.y + v.height), sy, surface_height) - viewport.Y);
        viewport.MinZ = v.min_z;
        viewport.MaxZ = v.max_z;
        device_->SetViewport(&viewport);
        RECT clip{0, 0, static_cast<LONG>(surface_width), static_cast<LONG>(surface_height)};
        if (scissor_on_)
            clip = {static_cast<LONG>(to_surface(static_cast<float>(scissor_.left), sx, surface_width)),
                    static_cast<LONG>(to_surface(static_cast<float>(scissor_.top), sy, surface_height)),
                    static_cast<LONG>(to_surface(static_cast<float>(scissor_.right), sx, surface_width)),
                    static_cast<LONG>(to_surface(static_cast<float>(scissor_.bottom), sy, surface_height))};
        const UiNarrowing narrow = narrowing(x_range);
        if (narrow.anchor >= 0.0f) {
            const float at = surface_width * narrow.anchor;
            clip.left = static_cast<LONG>(std::lround(at + (clip.left - at) * narrow.factor));
            clip.right = static_cast<LONG>(std::lround(at + (clip.right - at) * narrow.factor));
        }
        device_->SetScissorRect(&clip);

        // Screen space (title pixels, centres on integers) -> clip space of the viewport: the
        // title's pixel x is centred at x * s + (s - 1) / 2 on the surface (see D3D9Draw::draw).
        const float depth_max = draw_.depth_is_d16() ? 65535.0f : 16777215.0f;
        const float vx = static_cast<float>(viewport.X), vy = static_cast<float>(viewport.Y);
        const float vw = static_cast<float>(viewport.Width), vh = static_cast<float>(viewport.Height);
        const float z_range = v.max_z > v.min_z ? v.max_z - v.min_z : 1.0f;
        float* reserved = vs_constants_.data() + kGfxVsScreenToClip * 4;
        reserved[0] = 2.0f * sx / vw;
        reserved[1] = -2.0f * sy / vh;
        reserved[2] = 1.0f / (depth_max * z_range);
        reserved[3] = 0.0f;
        reserved[4] = (sx - 1.0f - 2.0f * vx) / vw - 1.0f;
        reserved[5] = 1.0f - (sy - 1.0f - 2.0f * vy) / vh;
        reserved[6] = -v.min_z / z_range;
        reserved[7] = 0.0f;
        if (narrow.anchor >= 0.0f) {  // x' = a + (x - a) * k, a in title pixels
            const float a = narrow.anchor * width;
            reserved[4] += a * (1.0f - narrow.factor) * reserved[0];
            reserved[0] *= narrow.factor;
        }
        reserved[8] = fog_.bias;
        reserved[9] = fog_.scale;
        reserved[10] = static_cast<float>(fog_.mode);
        reserved[11] = 0.0f;
        device_->SetVertexShaderConstantF(0, vs_constants_.data(), kGfxVsFog + 1);

        // Textures and samplers.
        const uint32_t anisotropy = draw_.anisotropy();
        const GraphicsFeatures features = active_graphics_features();
        for (uint32_t slot = 0; slot < kGfxTextureSlots; ++slot) {
            float scale[2];
            IDirect3DBaseTexture9* texture = resolve_texture(bound_textures_[slot], scale);
            float* c = ps_constants_.data() + (kGfxPsTexcoordScale + slot) * 4;
            c[0] = scale[0];
            c[1] = scale[1];
            c[2] = c[3] = 1.0f;
            device_->SetTexture(slot, texture);
            if (!texture) continue;
            const SamplerState& s = samplers_[slot];
            device_->SetSamplerState(slot, D3DSAMP_ADDRESSU, address(s.u));
            device_->SetSamplerState(slot, D3DSAMP_ADDRESSV, address(s.v));
            device_->SetSamplerState(slot, D3DSAMP_ADDRESSW, address(s.w));
            device_->SetSamplerState(slot, D3DSAMP_MAGFILTER, filter(s.mag == Filter::None ? Filter::Point : s.mag));
            DWORD min_filter = filter(s.min == Filter::None ? Filter::Point : s.min);
            DWORD max_anisotropy = std::max<uint32_t>(1, s.anisotropy);
            if (anisotropy > 1 && min_filter != D3DTEXF_POINT && s.mip != Filter::None && texture->GetLevelCount() > 1) {
                min_filter = D3DTEXF_ANISOTROPIC;
                max_anisotropy = std::max<DWORD>(max_anisotropy, anisotropy);
            }
            // Enhanced graphics' texture detail: mipmaps chosen sharper.
            float bias = s.lod_bias;
            if (texture->GetLevelCount() > 1 && features.texture_detail) bias += features.lod_bias;
            DWORD lod_bias;
            std::memcpy(&lod_bias, &bias, 4);
            device_->SetSamplerState(slot, D3DSAMP_MINFILTER, min_filter);
            device_->SetSamplerState(slot, D3DSAMP_MIPFILTER, filter(s.mip));
            device_->SetSamplerState(slot, D3DSAMP_MIPMAPLODBIAS, lod_bias);
            device_->SetSamplerState(slot, D3DSAMP_MAXMIPLEVEL, s.max_level);
            device_->SetSamplerState(slot, D3DSAMP_MAXANISOTROPY, max_anisotropy);
            device_->SetSamplerState(slot, D3DSAMP_BORDERCOLOR, s.border);
        }
        unpack_color(fog_.color, ps_constants_.data() + kGfxPsFogColor * 4);
        for (uint32_t slot = 0; slot < 4; ++slot)
            ps_constants_[kGfxPsAlphaKill * 4 + slot] = samplers_[slot].alpha_kill ? 1.0f : 0.0f;
        device_->SetPixelShaderConstantF(0, ps_constants_.data(), kGfxPsAlphaKill + 1);

        // Blend, depth, stencil, raster.
        const bool has_depth = draw_.has_depth();
        const DepthStencilState& ds = depth_stencil_;
        device_->SetRenderState(D3DRS_ZENABLE, ds.depth_test && has_depth ? D3DZB_TRUE : D3DZB_FALSE);
        device_->SetRenderState(D3DRS_ZWRITEENABLE, ds.depth_write);
        if (ds.depth_test && ds.depth_write && has_depth) draw_.note_depth_write();
        device_->SetRenderState(D3DRS_ZFUNC, compare_func(ds.depth_func));
        device_->SetRenderState(D3DRS_STENCILENABLE, ds.stencil && has_depth);
        device_->SetRenderState(D3DRS_STENCILFAIL, stencil_op(ds.stencil_fail));
        device_->SetRenderState(D3DRS_STENCILZFAIL, stencil_op(ds.stencil_depth_fail));
        device_->SetRenderState(D3DRS_STENCILPASS, stencil_op(ds.stencil_pass));
        device_->SetRenderState(D3DRS_STENCILFUNC, compare_func(ds.stencil_func));
        device_->SetRenderState(D3DRS_STENCILREF, ds.stencil_ref);
        device_->SetRenderState(D3DRS_STENCILMASK, ds.stencil_read_mask);
        device_->SetRenderState(D3DRS_STENCILWRITEMASK, ds.stencil_write_mask);
        const BlendState& b = blend_;
        device_->SetRenderState(D3DRS_ALPHABLENDENABLE, b.enable);
        device_->SetRenderState(D3DRS_SRCBLEND, blend_factor(b.src));
        device_->SetRenderState(D3DRS_DESTBLEND, blend_factor(b.dst));
        device_->SetRenderState(D3DRS_BLENDOP, blend_op(b.op));
        device_->SetRenderState(D3DRS_BLENDFACTOR, b.constant);
        device_->SetRenderState(D3DRS_COLORWRITEENABLE, b.write_mask & kColorAll);
        device_->SetRenderState(D3DRS_ALPHATESTENABLE, b.alpha_test);
        device_->SetRenderState(D3DRS_ALPHAFUNC, compare_func(b.alpha_func));
        device_->SetRenderState(D3DRS_ALPHAREF, b.alpha_ref);
        const RasterState& r = raster_;
        device_->SetRenderState(D3DRS_CULLMODE, r.cull == Cull::Clockwise          ? D3DCULL_CW
                                                : r.cull == Cull::CounterClockwise ? D3DCULL_CCW
                                                                                   : D3DCULL_NONE);
        device_->SetRenderState(D3DRS_FILLMODE, r.fill == Fill::Wireframe ? D3DFILL_WIREFRAME
                                                : r.fill == Fill::Point   ? D3DFILL_POINT
                                                                          : D3DFILL_SOLID);
        device_->SetRenderState(D3DRS_SHADEMODE, r.flat_shading ? D3DSHADE_FLAT : D3DSHADE_GOURAUD);
        // D3D9 takes the bias as a fraction of the depth range.
        const float bias = r.depth_bias / depth_max;
        DWORD bits;
        std::memcpy(&bits, &r.slope_bias, 4);
        device_->SetRenderState(D3DRS_SLOPESCALEDEPTHBIAS, bits);
        std::memcpy(&bits, &bias, 4);
        device_->SetRenderState(D3DRS_DEPTHBIAS, bits);

        device_->SetVertexDeclaration(layouts_[layout_ - 1]);
        device_->SetVertexShader(static_cast<IDirect3DVertexShader9*>(vs->second.object));
        device_->SetPixelShader(static_cast<IDirect3DPixelShader9*>(ps->second.object));
        return true;
    }

    IDirect3DDevice9* device_;
    D3D9Draw& draw_;
    uint32_t next_id_ = 1;  // one sequence for every kind of id
    std::unordered_map<TextureId, Texture> textures_;
    std::unordered_map<uint64_t, TextureId> xbox_textures_;  // by xbox_key
    std::unordered_map<BufferId, Buffer> buffers_;
    std::unordered_map<ShaderId, Shader> shaders_;
    std::vector<IDirect3DVertexDeclaration9*> layouts_;  // LayoutId - 1
    std::vector<std::vector<VertexAttribute>> layout_attributes_;
    UiNarrowing (*narrow_ui_draw_)();  // BackendConfig::narrow_ui_draw
    uint32_t back_width_, back_height_;
    std::unordered_map<TargetId, Target> targets_;
    TargetId back_buffer_ = 0, depth_buffer_ = 0;
    TargetId color_target_ = 0, depth_target_ = 0;

    // The state draws use.
    Viewport viewport_;
    bool scissor_on_ = false;
    Rect scissor_;
    BlendState blend_;
    DepthStencilState depth_stencil_;
    RasterState raster_;
    Fog fog_;
    TextureId bound_textures_[kGfxTextureSlots] = {};
    SamplerState samplers_[kGfxTextureSlots];
    ShaderId vertex_shader_ = 0, pixel_shader_ = 0;
    LayoutId layout_ = 0;
    Stream streams_[kGfxVertexStreams];
    BufferId index_buffer_ = 0;
    std::vector<float> vs_constants_ = std::vector<float>((kGfxVsFog + 1) * 4);
    std::vector<float> ps_constants_ = std::vector<float>((kGfxPsAlphaKill + 1) * 4);

    IDirect3DVertexBuffer9* user_vertices_ = nullptr;  // draw_user
    IDirect3DIndexBuffer9* user_indices_ = nullptr;
    uint32_t user_vertex_offset_ = 0, user_index_offset_ = 0;
    bool user_buffers_failed_ = false;
    IDirect3DIndexBuffer9* quad_indices_ = nullptr;
    struct Kept {
        IDirect3DVertexBuffer9* buffer = nullptr;
        uint64_t content = 0;
        uint32_t bytes = 0, used_frame = 0;
    };
    std::unordered_map<uint64_t, Kept> kept_;  // draw_user's cache_key
    uint64_t kept_bytes_ = 0;
    IDirect3DSurface9* staging_ = nullptr;  // read_target
};

}  // namespace

std::unique_ptr<D3D9GfxDevice> create_d3d9_gfx_device(IDirect3DDevice9* device, D3D9Draw& draw,
                                                      const BackendConfig& config) {
    return std::make_unique<Impl>(device, draw, config);
}

GfxDevice* gfx_device() {
    Backend* current = backend();
    return current ? current->gfx_device() : nullptr;
}

}  // namespace render
