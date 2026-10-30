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
}

namespace game {

// The whole recorded state (derived fields brought up to date).
const render::GfxState* ngl_gfx_state();

// A shader's key in render/shaders: the title data that names it (tools/shader_hlsl.py). Told
// by nglVertexShader_Init / nglPixelShader_Init and the boot assemblers.
void ngl_gfx_shader_key(uint32_t program, uint32_t key);

namespace xb {

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
void set_vertex_constant(uint32_t slot, const float* values);
void set_vertex_constants(uint32_t slot, const float* values, uint32_t dwords);
void set_pixel_constants(uint32_t reg, const float* values, uint32_t count);

}  // namespace xb
}  // namespace game
