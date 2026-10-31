#pragma once

// The renderer C's GPU state in our own graphics interface's terms (render/gfx_state.h), for our
// own renderer (docs/own-renderer.md, R4). Every Xbox graphics call that sets state goes through
// the wrappers in game::xb: each still makes its Xbox call (the Xbox path draws what is not ours
// yet) and records what it means. Draws that are ours take their state from ngl_gfx_state().
// Once every draw is ours the Xbox calls go and the wrappers are what is left.
//
// XB_GFX_VERIFY=1 compares the recorded state with the Xbox path's before each of its draws
// (render/gfx_verify.cpp); the loader connects the two (game::ngl_gfx_state as the probe).

#include <cstdint>

#include "compiler.h"

namespace render {
struct GfxState;
enum class Primitive : uint8_t;
}

namespace game {

// The whole recorded state (derived fields brought up to date).
const render::GfxState* ngl_gfx_state();

// Draws that are ours: on when there is a device (game::gfx()) and XB_XBOX_DRAWS is not set (for
// screenshot diffs against the Xbox path).
// XB_DRAWS_ALTERNATE=1: ours on every other frame (screenshot diffs of one paused scene).
bool ngl_gfx_own_draws();
// After each frame is shown (nglEndFrame).
void ngl_gfx_end_frame();

// Draws `count` vertices of `stride` bytes from caller memory with the recorded state on our
// device; the layout is the selected vertex declaration's. False when it cannot (logged once per
// reason): the caller draws through the Xbox path instead.
// `indices` (may be null) index into `vertices`; `cache_key` as GfxDevice::draw_user's.
bool ngl_gfx_draw_user(render::Primitive primitive, const void* vertices, uint32_t count, uint32_t stride,
                       const uint16_t* indices = nullptr, uint32_t index_count = 0, uint64_t cache_key = 0);

// A shader's key in render/shaders: the title data that names it (tools/shader_hlsl.py). Told
// by nglVertexShader_Init / nglPixelShader_Init and the boot assemblers.
void ngl_gfx_shader_key(uint32_t program, uint32_t key);

namespace xb {

// Direct3D_CreateDevice's job (nglInit): the display opened for the D3DPRESENT_PARAMETERS at
// `present_parameters` (title memory) through our graphics interface, the Xbox device it gives
// stored at `device_out`. Without an opener (--compare test processes) the Xbox call.
void create_device(uint32_t present_parameters, uint32_t device_out);
// D3DDevice_Swap's: the frame shown through our graphics interface (or the Xbox call without it).
void swap();

// D3DDevice_SetRenderState_Simple (0x003e89f0): an NV2A method with its value.
void set_render_state_simple(uint32_t method, uint32_t value);
// A word of D3D's render-state table (0x003f8328 + state * 4) or texture-stage table
// (0x003f5c68 + stage * 0x80 + state * 4), written directly (inlined setters).
void set_d3d_state(uint32_t address, uint32_t value);
// D3DDevice_SetRenderState_<name>: the function at `function` (0x003e80f0 CullMode, ...).
void set_render_state(uint32_t function, uint32_t value);
// D3DDevice_SetTexture / SwitchTexture (the header the data and format words come from) /
// SetPalette / SetTextureState_BorderColor.
void set_texture(uint32_t stage, uint32_t texture);
void switch_texture(uint32_t stage, uint32_t command, uint32_t texture);
void set_palette(uint32_t stage, uint32_t palette);
void set_border_color(uint32_t stage, uint32_t color);
// D3DDevice_SetPixelShaderProgram / LoadVertexShaderProgram + SelectVertexShaderDirect.
void set_pixel_shader_program(uint32_t definition);
void load_vertex_program(uint32_t program);
void select_vertex_shader(uint32_t declaration);
// D3DDevice_SetVertexShader (an FVF handle: no vertex program) / SetPixelShader (a shader object;
// 0: none).
void set_vertex_shader(uint32_t handle);
void set_pixel_shader(uint32_t handle);
// D3DDevice_SetVertexShaderConstant1Fast (slot = register + 96, 4 floats) /
// ...NotInlineFast (`dwords` floats) / SetPixelShaderConstant (`count` float4s).
// D3DDevice_SetRenderTarget / SetRenderTargetFast (surface headers) / SetViewport (D3DVIEWPORT8).
void set_render_target(uint32_t color, uint32_t depth, bool fast);
void set_viewport(const void* viewport);
// D3DDevice_DrawVerticesUP (Xbox D3DPRIMITIVETYPE, vertices in caller memory): drawn by us when
// it can be (ngl_gfx_own_draws), else by the Xbox path.
void draw_vertices_up(uint32_t primitive, uint32_t count, const void* vertices, uint32_t stride);
// D3DDevice_SetVertexShaderInputDirect (declaration, stream count, STREAMINPUT {buffer header,
// stride, offset}...).
void set_vertex_input(uint32_t declaration, uint32_t stream_count, const void* streams);
// D3DDevice_DrawVertices / DrawIndexedVertices (16-bit indices in title memory) from stream 0, and
// D3DDevice_Clear (Xbox flags: 0xF0 colour, 1 depth, 2 stencil): ours when they can be.
void draw_vertices(uint32_t primitive, uint32_t start, uint32_t count);
void draw_indexed_vertices(uint32_t primitive, uint32_t index_count, const void* indices);
void clear(uint32_t rect_count, const void* rects, uint32_t flags, uint32_t color, float depth, uint32_t stencil);
void set_vertex_constant(uint32_t slot, const float* values);
void set_vertex_constants(uint32_t slot, const float* values, uint32_t dwords);
void set_pixel_constants(uint32_t reg, const float* values, uint32_t count);

}  // namespace xb
}  // namespace game
