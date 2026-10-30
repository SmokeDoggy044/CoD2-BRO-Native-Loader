#pragma once

// The whole state a draw uses, in our graphics interface's terms (gfx_device.h). The renderer C
// keeps one (game/code/ngl_gfx.cpp) wherever it sets state, and draws that are ours take their
// state from it (docs/own-renderer.md, R4). Declarations and inline conversions only: the game
// library includes it without linking the render library.
//
// The conversions take the values the title's renderer works in: OpenGL enums for what it sends
// as NV2A methods (its own state cache keeps those), D3D8 enums for its D3D calls.

#include <cstdint>

#include "gfx_device.h"

namespace render {

constexpr uint32_t kGfxStages = 4;  // the NV2A's texture stages

struct GfxState {
    BlendState blend;
    DepthStencilState depth_stencil;
    RasterState raster;
    Fog fog;
    bool fog_enable = false;  // D3DRS_FOGENABLE; the mode applies only with it on
    SamplerState samplers[kGfxStages];
    // Transition: the title's texture and palette headers bound per stage (0: none).
    uint32_t textures[kGfxStages] = {};
    uint32_t palettes[kGfxStages] = {};
    // The vertex program and pixel shader definition the title selected (its data, as
    // nglVertexShader_Init / nglPixelShader_Init got them), and their keys in render/shaders.
    uint32_t vertex_program = 0, vertex_declaration = 0, vertex_key = 0;
    uint32_t pixel_definition = 0, pixel_key = 0;  // 0: none (D3D's fixed-function stages)
    // Transition: the title's render target and depth surface headers (D3DSurface), and its
    // viewport (title pixels of the target; min_z / max_z as D3D's).
    uint32_t color_target = 0, depth_target = 0;
    // Vertex stream 0 (SetVertexShaderInputDirect): the title's vertex buffer header, stride, offset.
    uint32_t stream_buffer = 0, stream_stride = 0, stream_offset = 0;
    Viewport viewport;
    float vs_constants[192][4] = {};
    float ps_constants[18][4] = {};  // c0-c7 stage C0, c8-c15 C1, c16 c17 the final C0 C1
};

// --- From OpenGL enums (NV2A methods) ---

inline Compare gfx_compare(uint32_t gl) {
    return gl >= 0x200 && gl <= 0x207 ? static_cast<Compare>(gl - 0x200) : Compare::Always;
}

inline Blend gfx_blend(uint32_t gl) {
    switch (gl) {
        case 0x0000: return Blend::Zero;
        case 0x0300: return Blend::SrcColor;
        case 0x0301: return Blend::InvSrcColor;
        case 0x0302: return Blend::SrcAlpha;
        case 0x0303: return Blend::InvSrcAlpha;
        case 0x0304: return Blend::DestAlpha;
        case 0x0305: return Blend::InvDestAlpha;
        case 0x0306: return Blend::DestColor;
        case 0x0307: return Blend::InvDestColor;
        case 0x0308: return Blend::SrcAlphaSat;
        case 0x8001: return Blend::ConstantColor;
        case 0x8002: return Blend::InvConstantColor;
        case 0x8003: return Blend::ConstantAlpha;
        case 0x8004: return Blend::InvConstantAlpha;
        default: return Blend::One;
    }
}

inline BlendOp gfx_blend_op(uint32_t gl) {
    switch (gl) {
        case 0x800A: return BlendOp::Subtract;
        case 0x800B: case 0xF005: return BlendOp::RevSubtract;  // 0xF005: the Xbox's signed variant
        case 0x8007: return BlendOp::Min;
        case 0x8008: return BlendOp::Max;
        default: return BlendOp::Add;
    }
}

inline StencilOp gfx_stencil_op(uint32_t gl) {
    switch (gl) {
        case 0x0000: return StencilOp::Zero;
        case 0x1E01: return StencilOp::Replace;
        case 0x1E02: return StencilOp::IncrSat;
        case 0x1E03: return StencilOp::DecrSat;
        case 0x150A: return StencilOp::Invert;
        case 0x8507: return StencilOp::Incr;
        case 0x8508: return StencilOp::Decr;
        default: return StencilOp::Keep;
    }
}

// The NV2A colour mask (A << 24 | R << 16 | G << 8 | B, one bit each).
inline uint8_t gfx_color_mask(uint32_t nv) {
    return static_cast<uint8_t>((nv & 0x00010000 ? kColorRed : 0) | (nv & 0x00000100 ? kColorGreen : 0) |
                                (nv & 0x00000001 ? kColorBlue : 0) | (nv & 0x01000000 ? kColorAlpha : 0));
}

// --- From the Xbox's D3D8 enums (GL values for cull and fill) ---

// D3DRS_CULLMODE (0 none, 0x900 clockwise, 0x901 counter-clockwise) with D3DRS_FRONTFACE (0x900,
// 0x901): which winding is dropped (as D3D9Draw's cull_mode).
inline Cull gfx_cull(uint32_t cull, uint32_t front_face) {
    if (cull != 0x900 && cull != 0x901) return Cull::None;
    return (cull == 0x900) != (front_face == 0x901) ? Cull::Clockwise : Cull::CounterClockwise;
}

inline Fill gfx_fill(uint32_t fill) {
    return fill == 0x1B00 ? Fill::Point : fill == 0x1B01 ? Fill::Wireframe : Fill::Solid;
}

// D3DTEXTUREADDRESS (1 wrap, 2 mirror, 3 clamp, 4 border, 5 clamp to edge).
inline Address gfx_address(uint32_t d3d) {
    switch (d3d) {
        case 2: return Address::Mirror;
        case 3: case 5: return Address::Clamp;
        case 4: return Address::Border;
        default: return Address::Wrap;
    }
}

// D3DTEXTUREFILTERTYPE (0 none, 1 point, 2 linear, 3 anisotropic, 4 quincunx, 5 gaussian).
inline Filter gfx_filter(uint32_t d3d) {
    return d3d == 0 ? Filter::None : d3d == 1 ? Filter::Point : Filter::Linear;
}

}  // namespace render
