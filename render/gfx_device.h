#pragma once

// Our own graphics interface: what the game's renderer needs from the graphics hardware, in
// its own terms (textures, buffers, shaders, render targets, state, draws), with nothing of
// the Xbox's Direct3D 8 or NV2A. The decompiled renderer (game/code: ngl*, R_*, FX) is moved
// onto it one draw path at a time (docs/own-renderer.md, step R4). One back end so far:
// Direct3D 9 (gfx_d3d9.cpp), sharing the device and render targets of the Xbox D3D path, so
// converted and unconverted draws mix in one frame. D3D11, Vulkan and D3D12 follow.
//
// This header is only declarations: the game library (game/code) includes it without
// linking the render library; the loader hands it the device (game::set_gfx_device).
//
// Conventions:
// - Ids are 0 for none. Every create_* returns 0 on failure (logged); destroy(0) is allowed.
// - Sizes, viewports and scissors are in the title's pixels (640x480 back buffer); the back end
//   scales them to the render scale and the display shape (PC settings) on its own.
// - Colours packed in a uint32_t are A8R8G8B8 (D3DCOLOR).
// - Shader constants are float4 registers. Vertex: c0..c191 the shader's own, then the
//   reserved ones below, which the back end fills before every draw. Pixel: c0..c17 the
//   shader's own, then the reserved ones.
// - Vertex inputs are numbered 0..15 (HLSL semantic TEXCOORDn), as the NV2A's v0..v15.

#include <cstdint>

namespace render {

class GfxDevice;

using TextureId = uint32_t;
using BufferId = uint32_t;
using ShaderId = uint32_t;
using LayoutId = uint32_t;
using TargetId = uint32_t;

constexpr uint32_t kGfxTextureSlots = 8;
constexpr uint32_t kGfxVertexStreams = 4;
constexpr uint32_t kGfxVertexInputs = 16;

// Reserved vertex constants.
// Screen to clip: the NV2A's vertex programs output screen-space positions (title pixels and
// depth buffer units; the title's viewport transform is part of them). clip.xyz = screen.xyz *
// c192.xyz + c193.xyz maps them into the current viewport (render scale and pixel centres
// included); shaders that output clip space ignore it.
constexpr uint32_t kGfxVsScreenToClip = 192;  // two registers
constexpr uint32_t kGfxVsFog = 194;           // Fog::bias, Fog::scale, Fog::mode, 0
// Reserved pixel constants.
constexpr uint32_t kGfxPsTexcoordScale = 18;  // c18..c25 per texture slot: (sx, sy, 1, 1), texel ->
                                              // normalized for the title's linear textures, else 1
constexpr uint32_t kGfxPsFogColor = 26;       // Fog::color as rgba
constexpr uint32_t kGfxPsAlphaKill = 27;      // x..w: SamplerState::alpha_kill of slots 0-3 (1 or 0)

// --- Resources ---

enum class Format : uint8_t {
    Unknown,
    BGRA8,     // A8R8G8B8
    BGRX8,     // X8R8G8B8
    B5G6R5,    // R5G6B5
    BGR5A1,    // A1R5G5B5
    BGRX5,     // X1R5G5B5
    BGRA4,     // A4R4G4B4
    L8,
    A8,
    A8L8,
    DXT1,
    DXT3,
    DXT5,
    D16,       // depth targets only
    D24S8,
};

enum class TextureType : uint8_t { Tex2D, Cube, Volume };

struct TextureDesc {
    TextureType type = TextureType::Tex2D;
    Format format = Format::BGRA8;
    uint32_t width = 1, height = 1, depth = 1;
    uint32_t levels = 1;  // mip levels; 0: the full chain
};

enum class BufferKind : uint8_t { Vertex, Index16, Index32 };
enum class BufferUsage : uint8_t {
    Static,   // written once (at load), drawn many times
    Dynamic,  // rewritten often (map with Discard / NoOverwrite)
};

struct BufferDesc {
    BufferKind kind = BufferKind::Vertex;
    BufferUsage usage = BufferUsage::Static;
    uint32_t bytes = 0;
};

enum class MapMode : uint8_t {
    Write,        // the GPU may still read the old contents: waits if it does
    Discard,      // the whole buffer's old contents are dropped
    NoOverwrite,  // the caller writes only where no queued draw reads
};

enum class ShaderStage : uint8_t { Vertex, Pixel };

// A compiled shader for each back end that exists; the back end picks its own (R3 builds
// these from committed HLSL files).
struct ShaderCode {
    const void* d3d9 = nullptr;  // vs_3_0 / ps_3_0 tokens
    uint32_t d3d9_bytes = 0;
};

enum class AttributeType : uint8_t {
    Float1, Float2, Float3, Float4,
    Color,    // 4 x 8-bit unsigned normalized, B G R A in memory (D3DCOLOR), read as rgba
    UByte4,   // 4 x 8-bit unsigned integers, read as floats
    UByte4N,  // 4 x 8-bit unsigned normalized, in order (x, y, z, w)
    Short2, Short4,      // signed integers, read as floats
    Short2N, Short4N,    // signed normalized
    Half2, Half4,
};

struct VertexAttribute {
    uint8_t input = 0;   // 0..15
    uint8_t stream = 0;  // 0..kGfxVertexStreams-1
    AttributeType type = AttributeType::Float4;
    uint16_t offset = 0;  // bytes into the stream's vertex
};

struct VertexLayout {
    const VertexAttribute* attributes = nullptr;
    uint32_t count = 0;
};

// A render target (or depth buffer). xbox_data: the title memory it stands for while the
// Xbox D3D path still draws (0 for ours): the same memory means the same target on both
// paths, and Xbox textures at that address show what was drawn.
struct TargetDesc {
    Format format = Format::BGRA8;
    uint32_t width = 0, height = 0;
    uint32_t xbox_data = 0;
};

// --- State ---

enum class Compare : uint8_t { Never, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };

enum class Blend : uint8_t {
    Zero, One, SrcColor, InvSrcColor, SrcAlpha, InvSrcAlpha, DestAlpha, InvDestAlpha, DestColor, InvDestColor,
    SrcAlphaSat, ConstantColor, InvConstantColor, ConstantAlpha, InvConstantAlpha,
};
enum class BlendOp : uint8_t { Add, Subtract, RevSubtract, Min, Max };

enum ColorMask : uint8_t { kColorRed = 1, kColorGreen = 2, kColorBlue = 4, kColorAlpha = 8, kColorAll = 15 };

struct BlendState {
    bool enable = false;
    Blend src = Blend::One, dst = Blend::Zero;
    BlendOp op = BlendOp::Add;
    uint8_t write_mask = kColorAll;
    uint32_t constant = 0;  // the constant colour
    // Fixed alpha test (the NV2A's; back ends without one discard in the shader).
    bool alpha_test = false;
    Compare alpha_func = Compare::Always;
    uint8_t alpha_ref = 0;
};

enum class StencilOp : uint8_t { Keep, Zero, Replace, IncrSat, DecrSat, Invert, Incr, Decr };

struct DepthStencilState {
    bool depth_test = false;
    bool depth_write = false;
    Compare depth_func = Compare::LessEqual;
    bool stencil = false;
    Compare stencil_func = Compare::Always;
    uint8_t stencil_ref = 0, stencil_read_mask = 0xFF, stencil_write_mask = 0xFF;
    StencilOp stencil_fail = StencilOp::Keep, stencil_depth_fail = StencilOp::Keep, stencil_pass = StencilOp::Keep;
};

enum class Cull : uint8_t { None, Clockwise, CounterClockwise };  // which winding is dropped
enum class Fill : uint8_t { Solid, Wireframe, Point };

struct RasterState {
    Cull cull = Cull::None;
    Fill fill = Fill::Solid;
    bool flat_shading = false;
    // Polygon offset (decals): depth += slope_bias * depth slope + depth_bias, in units of the
    // depth buffer's smallest step.
    float depth_bias = 0.0f, slope_bias = 0.0f;
};

enum class Address : uint8_t { Wrap, Mirror, Clamp, Border };
enum class Filter : uint8_t { None, Point, Linear };  // None: mipmaps off (mip filter only)

struct SamplerState {
    Address u = Address::Wrap, v = Address::Wrap, w = Address::Wrap;
    Filter min = Filter::Linear, mag = Filter::Linear, mip = Filter::None;
    float lod_bias = 0.0f;
    uint32_t max_level = 0;  // the largest mip level used (0 = the full texture)
    uint32_t anisotropy = 1;  // the title's own; the PC setting raises it for mipmapped textures
    uint32_t border = 0;
    // The NV2A's alpha kill: pixels where this slot's texture alpha is 0 are discarded (the
    // shader does it: kGfxPsAlphaKill; slots 0-3).
    bool alpha_kill = false;
};

// Fog is the shaders' work: the back end hands them these (kGfxVsFog, kGfxPsFogColor). The
// factor from the distance d the vertex program writes (the NV2A's formulas): linear
// bias + d * scale - 1, exp bias + 2^(16 d scale) - 1.5, exp2 bias + 2^(-32 (d scale)^2) - 1.5.
enum class FogMode : uint8_t { Off, Linear, Exp, Exp2 };
struct Fog {
    FogMode mode = FogMode::Off;
    float bias = 0.0f, scale = 0.0f;
    uint32_t color = 0;
};

struct Viewport {
    uint32_t x = 0, y = 0, width = 0, height = 0;  // 0 width: the whole target
    float min_z = 0.0f, max_z = 1.0f;
};

struct Rect {
    int32_t left = 0, top = 0, right = 0, bottom = 0;
};

enum class Primitive : uint8_t { Points, Lines, LineStrip, Triangles, TriangleStrip, TriangleFan, Quads };

enum GfxClear : uint32_t { kGfxClearColor = 1, kGfxClearDepth = 2, kGfxClearStencil = 4 };

class GfxDevice {
public:
    // Whether a device is there to draw with (the Xbox D3D path creates it for now).
    virtual bool open() = 0;

    // --- Textures ---
    virtual TextureId create_texture(const TextureDesc& desc) = 0;
    // One level (and cube face 0..5 / volume) from linear memory in the texture's format:
    // `pitch` bytes per row of pixels (DXT: per row of 4x4 blocks), `slice_pitch` per volume slice.
    virtual bool update_texture(TextureId texture, uint32_t level, uint32_t face, const void* data, uint32_t pitch,
                                uint32_t slice_pitch) = 0;
    virtual void destroy_texture(TextureId texture) = 0;

    // --- Buffers ---
    virtual BufferId create_buffer(const BufferDesc& desc, const void* initial) = 0;  // initial may be null
    // Memory to write `bytes` at `offset`; null on failure. unmap_buffer before drawing from it.
    virtual void* map_buffer(BufferId buffer, uint32_t offset, uint32_t bytes, MapMode mode) = 0;
    virtual void unmap_buffer(BufferId buffer) = 0;
    virtual void destroy_buffer(BufferId buffer) = 0;

    // --- Shaders and vertex layouts ---
    virtual ShaderId create_shader(ShaderStage stage, const ShaderCode& code) = 0;
    // One of the title's shaders translated to HLSL (render/shaders/vs_KEY.hlsl, ps_KEY.hlsl:
    // KEY is the title data naming it, tools/shader_hlsl.py); 0 if there is none by that key.
    // `variant`: a vertex program's form for a vertex declaration with packed normals (given as
    // AttributeType::UByte4, unpacked by the shader), named by the declaration; 0 = none.
    virtual ShaderId create_title_shader(ShaderStage stage, uint32_t key, uint32_t variant = 0) = 0;
    virtual void destroy_shader(ShaderId shader) = 0;
    virtual LayoutId create_layout(const VertexLayout& layout) = 0;  // kept until the device goes

    // --- Render targets ---
    virtual TargetId create_target(const TargetDesc& desc) = 0;
    virtual void destroy_target(TargetId target) = 0;
    // The target as a texture to sample (the same id while the target lives).
    virtual TextureId target_texture(TargetId target) = 0;
    // The back buffer (what present shows) and the device's own depth buffer.
    virtual TargetId back_buffer() = 0;
    virtual TargetId depth_buffer() = 0;
    // Where draws and clears go from now on; depth 0: none. Resets viewport and scissor to the
    // whole target.
    virtual void set_target(TargetId color, TargetId depth) = 0;
    virtual void clear(uint32_t flags, uint32_t color, float depth, uint32_t stencil) = 0;

    // --- State (kept until changed) ---
    virtual void set_viewport(const Viewport& viewport) = 0;
    virtual void set_scissor(const Rect* rect) = 0;  // null: the whole target
    virtual void set_blend(const BlendState& state) = 0;
    virtual void set_depth_stencil(const DepthStencilState& state) = 0;
    virtual void set_raster(const RasterState& state) = 0;
    virtual void set_fog(const Fog& fog) = 0;
    virtual void set_texture(uint32_t slot, TextureId texture) = 0;
    virtual void set_sampler(uint32_t slot, const SamplerState& state) = 0;
    virtual void set_shaders(ShaderId vertex, ShaderId pixel) = 0;
    virtual void set_constants(ShaderStage stage, uint32_t first, const float* values, uint32_t count) = 0;
    virtual void set_layout(LayoutId layout) = 0;
    virtual void set_vertex_buffer(uint32_t stream, BufferId buffer, uint32_t offset, uint32_t stride) = 0;
    virtual void set_index_buffer(BufferId buffer) = 0;

    // --- Draws ---
    virtual void draw(Primitive primitive, uint32_t first_vertex, uint32_t vertex_count) = 0;
    virtual void draw_indexed(Primitive primitive, uint32_t first_index, uint32_t index_count,
                              int32_t base_vertex) = 0;
    // Vertices (and 16-bit indices into them, may be null) from caller memory, copied into the
    // back end's own stream buffers: stream 0 with `stride`, other streams unbound. `cache_key`
    // (non-zero) names vertex data that often stays the same between frames (a title vertex
    // buffer's range): while its contents do, the back end draws from a copy it keeps.
    virtual void draw_user(Primitive primitive, const void* vertices, uint32_t vertex_count, uint32_t stride,
                           const uint16_t* indices, uint32_t index_count, uint64_t cache_key = 0) = 0;

    // --- Diagnostics ---
    // The target's pixels at the title's size (A8R8G8B8, width * height); false on failure.
    virtual bool read_target(TargetId target, uint32_t* pixels, uint32_t width, uint32_t height) = 0;

    // --- Transition (until step R4 is done): the title's own Xbox resources ---
    // A texture standing for the title's Xbox texture header (D3DTexture in title memory) and its
    // palette header (P8, else 0), decoded and kept current with the memory each frame like the
    // Xbox path does (texture replacements and PC filtering included). Valid until the title's
    // header changes; asking again for the same header gives the same id.
    virtual TextureId xbox_texture(uint32_t texture_header, uint32_t palette_header) = 0;

protected:
    ~GfxDevice() = default;
};

// The current back end's device, null before the title created its Direct3D device. In the
// render library (the loader's); the game library asks game::gfx() (ngl.h).
GfxDevice* gfx_device();

}  // namespace render
