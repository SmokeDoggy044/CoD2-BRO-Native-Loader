// Xbox Direct3D 8 replacement: shader state (vertex programs, constants, vertex input
// layout, pixel shader programs). See xbox_d3d_api.h.
//
// Vertex programs and their constants are NV2A state (nv2a.h): titles load microcode
// into program slots and select a start address, and constants are written through
// the same methods raw pushbuffer data uses. The vertex input layout and the pixel
// shader (register combiner) program are kept here in their Xbox form.

#include <algorithm>
#include <cstring>
#include <mutex>

#include "compiler.h"
#include "d3d_internal.h"
#include "log.h"
#include "nv2a.h"
#include "shader_state.h"
#include "xbox_d3d.h"

namespace render {

namespace {

using xbox::from_xbox_ptr;

ShaderState g_shaders;

constexpr uint32_t kVertexShaderHandleIsObject = 1;  // else an FVF (fixed function)

void warn_once(std::once_flag& once, const char* message) {
    std::call_once(once, [message] { LOG_WARN("render", "%s", message); });
}

// --- Vertex shader constants ---------------------------------------------------------

// D3DSCM_* flags: 0x01 192 constants, 0x10 no constant shadowing; nothing to do here.
void XB_STDCALL D3DDevice_SetShaderConstantMode(uint32_t mode) { g_shaders.constant_mode = mode; }

void XB_STDCALL D3DDevice_GetShaderConstantMode(XbPtr mode) {
    *from_xbox_ptr<uint32_t>(mode) = g_shaders.constant_mode;
}

void load_constants(uint32_t slot, const uint32_t* data, uint32_t dwords) {
    nv2a::write(nv2a::kTransformConstantLoad, slot);
    nv2a::write(nv2a::kTransformConstant, data, dwords, true);
}

// ECX = constant slot (register + 96), EDX = 4 dwords.
void XB_FASTCALL D3DDevice_SetVertexShaderConstant1Fast(uint32_t slot, XbPtr data) {
    load_constants(slot, from_xbox_ptr<uint32_t>(data), 4);
}

// ECX = constant slot, EDX = data, stack: dword count.
void XB_FASTCALL D3DDevice_SetVertexShaderConstantNotInlineFast(uint32_t slot, XbPtr data, uint32_t dwords) {
    load_constants(slot, from_xbox_ptr<uint32_t>(data), dwords);
}

void XB_FASTCALL D3DDevice_SetVertexShaderConstantNotInline(uint32_t slot, XbPtr data, uint32_t dwords) {
    load_constants(slot, from_xbox_ptr<uint32_t>(data), dwords);
}

// Register is the D3D register (-96..95); count in 4-dword constants.
void XB_STDCALL D3DDevice_GetVertexShaderConstant(int32_t reg, XbPtr data, uint32_t count) {
    auto* out = from_xbox_ptr<uint32_t>(data);
    for (uint32_t i = 0; i < count; ++i) {
        const int32_t slot = reg + 96 + static_cast<int32_t>(i);
        if (slot >= 0 && slot < static_cast<int32_t>(nv2a::kConstantSlots))
            std::memcpy(out + i * 4, nv2a::state().constants[slot].data(), 16);
    }
}

// --- Vertex programs -------------------------------------------------------------------

// function: VSH_SHADER_HEADER {u16 version, u16 instruction count} + instructions.
void XB_STDCALL D3DDevice_LoadVertexShaderProgram(XbPtr function, uint32_t address) {
    const auto* header = from_xbox_ptr<uint16_t>(function);
    nv2a::write(nv2a::kTransformProgramLoad, address);
    nv2a::write(nv2a::kTransformProgram, from_xbox_ptr<uint32_t>(function + 4), header[1] * 4u, true);
}

void set_attributes(XbPtr format) {
    if (format) {
        std::memcpy(g_shaders.attributes.data(), from_xbox_ptr<void>(format), sizeof(g_shaders.attributes));
        g_shaders.attributes_set = true;
    }
}

// Selects the program at `address` with an explicit vertex input layout.
void XB_STDCALL D3DDevice_SelectVertexShaderDirect(XbPtr format, uint32_t address) {
    set_attributes(format);
    nv2a::write(nv2a::kTransformProgramStart, address);
    g_shaders.vertex_shader = 0;
    g_shaders.programmable = true;
}

void set_streams(uint32_t stream_count, XbPtr streams) {
    g_shaders.stream_count = stream_count < kMaxStreams ? stream_count : kMaxStreams;
    if (streams)
        std::memcpy(g_shaders.streams.data(), from_xbox_ptr<void>(streams),
                    g_shaders.stream_count * sizeof(XbStreamInput));
}

// Vertex streams for the current shader, bypassing SetStreamSource. A null format
// with no streams resets the inputs.
void XB_STDCALL D3DDevice_SetVertexShaderInputDirect(XbPtr format, uint32_t stream_count, XbPtr streams) {
    set_attributes(format);
    set_streams(stream_count, streams);
}

void XB_STDCALL D3DDevice_SetVertexShaderInput(XbPtr shader, uint32_t stream_count, XbPtr streams) {
    if (shader) {
        static std::once_flag once;
        warn_once(once, "SetVertexShaderInput with a shader object: layout not taken from it yet");
    }
    set_streams(stream_count, streams);
}

// handle: a vertex shader object (address | 1) or an FVF code (fixed function).
void XB_STDCALL D3DDevice_SetVertexShader(uint32_t handle) {
    g_shaders.vertex_shader = handle;
    g_shaders.programmable = (handle & kVertexShaderHandleIsObject) != 0;
    if (g_shaders.programmable) {
        static std::once_flag once;
        warn_once(once, "SetVertexShader with a shader object: not supported yet");
    }
}

void XB_STDCALL D3DDevice_GetVertexShader(XbPtr handle) { *from_xbox_ptr<uint32_t>(handle) = g_shaders.vertex_shader; }

// --- Pixel shaders ----------------------------------------------------------------------
//
// Like D3D, setting a pixel shader writes its definition into the NV2A combiner
// registers; the draw path reads the combiners from there, so render states that set
// single combiner registers (D3DRS_PSRGBINPUTS0 & co) work too.

void load_pixel_shader_def(const XbPixelShaderDef& d) {
    nv2a::write(kNvCombinerAlphaInputs, d.alpha_inputs, 8);
    nv2a::write(kNvCombinerFinal0, d.final_inputs_abcd);
    nv2a::write(kNvCombinerFinal1, d.final_inputs_efg);
    nv2a::write(kNvCombinerFactor0, d.constant0, 8);
    nv2a::write(kNvCombinerFactor1, d.constant1, 8);
    nv2a::write(kNvCombinerAlphaOutputs, d.alpha_outputs, 8);
    nv2a::write(kNvCombinerColorInputs, d.rgb_inputs, 8);
    nv2a::write(kNvCombinerFinalFactor, d.final_constant0);
    nv2a::write(kNvCombinerFinalFactor + 4, d.final_constant1);
    nv2a::write(kNvCombinerColorOutputs, d.rgb_outputs, 8);
    nv2a::write(kNvCombinerControl, d.combiner_count);
    nv2a::write(kNvShaderStageProgram, d.texture_modes);
    nv2a::write(kNvDotRgbMapping, d.dot_mapping);
    nv2a::write(kNvShaderOtherStageInput, d.input_texture);
}

// float4 -> D3DCOLOR (each component clamped to 0..1).
uint32_t to_color(const float* v) {
    auto byte = [](float f) { return static_cast<uint32_t>(std::clamp(f, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return byte(v[3]) << 24 | byte(v[0]) << 16 | byte(v[1]) << 8 | byte(v[2]);
}

// Sends constant `reg` to the combiner factors the current shader maps it to: one
// nibble per stage for C0 and C1, and two for the final combiner.
void apply_pixel_constant(uint32_t reg) {
    const XbPixelShaderDef& d = g_shaders.pixel_shader_def;
    const uint32_t color = to_color(g_shaders.pixel_constants[reg].data());
    for (uint32_t stage = 0; stage < 8; ++stage) {
        if (((d.c0_mapping >> (stage * 4)) & 0xF) == reg) nv2a::write(kNvCombinerFactor0 + stage * 4, color);
        if (((d.c1_mapping >> (stage * 4)) & 0xF) == reg) nv2a::write(kNvCombinerFactor1 + stage * 4, color);
    }
    if ((d.final_constants_mapping & 0xF) == reg) nv2a::write(kNvCombinerFinalFactor, color);
    if (((d.final_constants_mapping >> 4) & 0xF) == reg) nv2a::write(kNvCombinerFinalFactor + 4, color);
}

void set_pixel_shader_def(XbPtr definition) {
    std::memcpy(&g_shaders.pixel_shader_def, from_xbox_ptr<void>(definition), sizeof(XbPixelShaderDef));
    load_pixel_shader_def(g_shaders.pixel_shader_def);
}

// handle: a PixelShader object {refcount, D3D owned, D3DPIXELSHADERDEF*}, or 0 for the
// fixed-function texture stages.
void XB_STDCALL D3DDevice_SetPixelShader(XbPtr handle) {
    g_shaders.pixel_shader = handle;
    g_shaders.pixel_shader_set = handle != 0;
    if (handle) set_pixel_shader_def(from_xbox_ptr<uint32_t>(handle)[2]);
}

// A D3DPIXELSHADERDEF without a shader object (D3D uses one inside the device).
void XB_STDCALL D3DDevice_SetPixelShaderProgram(XbPtr definition) {
    g_shaders.pixel_shader = 0;
    g_shaders.pixel_shader_set = definition != 0;
    if (definition) set_pixel_shader_def(definition);
}

void XB_STDCALL D3DDevice_GetPixelShader(XbPtr handle) { *from_xbox_ptr<uint32_t>(handle) = g_shaders.pixel_shader; }

// Constants as float4 (D3D clamps them to 0..1 for the combiners).
void XB_STDCALL D3DDevice_SetPixelShaderConstant(uint32_t reg, XbPtr data, uint32_t count) {
    const auto* values = from_xbox_ptr<float>(data);
    for (uint32_t i = 0; i < count && reg + i < kPixelShaderConstants; ++i) {
        std::memcpy(g_shaders.pixel_constants[reg + i].data(), values + i * 4, 16);
        apply_pixel_constant(reg + i);
    }
}

}  // namespace

const xbox::KernelImpl kShaderFunctions[] = {
    D3D_IMPL(D3DDevice_GetPixelShader),
    D3D_IMPL(D3DDevice_GetShaderConstantMode),
    D3D_IMPL(D3DDevice_GetVertexShader),
    D3D_IMPL(D3DDevice_GetVertexShaderConstant),
    D3D_IMPL(D3DDevice_LoadVertexShaderProgram),
    D3D_IMPL(D3DDevice_SelectVertexShaderDirect),
    D3D_IMPL(D3DDevice_SetPixelShader),
    D3D_IMPL(D3DDevice_SetPixelShaderConstant),
    D3D_IMPL(D3DDevice_SetPixelShaderProgram),
    D3D_IMPL(D3DDevice_SetShaderConstantMode),
    D3D_IMPL(D3DDevice_SetVertexShader),
    D3D_IMPL(D3DDevice_SetVertexShaderConstant1Fast),
    D3D_IMPL(D3DDevice_SetVertexShaderConstantNotInline),
    D3D_IMPL(D3DDevice_SetVertexShaderConstantNotInlineFast),
    D3D_IMPL(D3DDevice_SetVertexShaderInput),
    D3D_IMPL(D3DDevice_SetVertexShaderInputDirect),
};
const size_t kShaderFunctionCount = std::size(kShaderFunctions);

ShaderState& shader_state() { return g_shaders; }

}  // namespace render
