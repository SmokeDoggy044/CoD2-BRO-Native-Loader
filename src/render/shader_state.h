#pragma once

// Shader-related device state in its Xbox form (shader.cpp keeps it; the draw-time
// translation reads it).

#include <array>
#include <cstdint>

#include "types.h"

namespace render {

using xbox::XbPtr;

constexpr uint32_t kMaxAttributes = 16;
constexpr uint32_t kMaxStreams = 16;
constexpr uint32_t kPixelShaderConstants = 16;

// VERTEXSHADERINPUT: where one vertex attribute (v0..v15) comes from.
struct XbVertexShaderInput {
    uint32_t stream;
    uint32_t offset;
    uint32_t format;  // D3DVSDT_*; 0x02 = unused (D3DVSDT_NONE)
    uint8_t tessellation_type;
    uint8_t tessellation_source;
    uint8_t pad[2];
};
static_assert(sizeof(XbVertexShaderInput) == 16);

// STREAMINPUT
struct XbStreamInput {
    XbPtr vertex_buffer;  // D3DVertexBuffer*
    uint32_t stride;
    uint32_t offset;
};
static_assert(sizeof(XbStreamInput) == 12);

// D3DPIXELSHADERDEF: register combiner and texture stage setup.
struct XbPixelShaderDef {
    uint32_t alpha_inputs[8];
    uint32_t final_inputs_abcd;
    uint32_t final_inputs_efg;
    uint32_t constant0[8];
    uint32_t constant1[8];
    uint32_t alpha_outputs[8];
    uint32_t rgb_inputs[8];
    uint32_t compare_mode;
    uint32_t final_constant0;
    uint32_t final_constant1;
    uint32_t rgb_outputs[8];
    uint32_t combiner_count;
    uint32_t texture_modes;
    uint32_t dot_mapping;
    uint32_t input_texture;
    uint32_t c0_mapping;
    uint32_t c1_mapping;
    uint32_t final_constants_mapping;
};
static_assert(sizeof(XbPixelShaderDef) == 0xF0);

struct ShaderState {
    uint32_t constant_mode = 0;
    uint32_t vertex_shader = 0;  // SetVertexShader handle (object | 1, or FVF)
    bool programmable = false;   // a vertex program (SelectVertexShaderDirect) is active
    bool attributes_set = false;
    std::array<XbVertexShaderInput, kMaxAttributes> attributes{};
    uint32_t stream_count = 0;
    std::array<XbStreamInput, kMaxStreams> streams{};
    XbPtr pixel_shader = 0;
    bool pixel_shader_set = false;
    XbPixelShaderDef pixel_shader_def{};
    std::array<std::array<float, 4>, kPixelShaderConstants> pixel_constants{};
};

ShaderState& shader_state();

}  // namespace render
