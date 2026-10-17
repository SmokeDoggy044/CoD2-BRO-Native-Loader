#pragma once

// Xbox shaders -> Direct3D 9 shader model 3 bytecode (dxvk-native has no HLSL
// compiler, so the tokens are assembled here).
//
// Vertex: NV2A vertex program microcode (MAC + ILU pairs) -> vs_3_0. Input v<n> is
// declared as TEXCOORD<n>. The program writes screen-space positions (the Xbox
// viewport transform is part of it); the epilogue maps them back to clip space with
// kVsScreenToClip.
// Pixel: register combiners (D3DPIXELSHADERDEF) -> ps_3_0.

#include <cstdint>
#include <vector>

#include "shader_state.h"

namespace render::d3d9 {

// vs_3_0 constants: c0..c191 are the NV2A constant slots, then ours.
constexpr uint32_t kVsScreenToClip = 192;  // (2/width, -2/height, 1/zmax, 0), then (-1, 1, 0, 0)
constexpr uint32_t kVsFogParams = 194;     // the NV2A fog params (bias, scale, 0, 0)

// How the epilogue turns oFog.x (the fog distance) into the fog factor (NV2A fog
// modes; the factor is 1 with fog off).
enum class FogMode : uint32_t { Off, Linear, Exp, Exp2 };

// ps_3_0 constants.
constexpr uint32_t kPsStageC0 = 0;      // c0..c7: each combiner stage's C0
constexpr uint32_t kPsStageC1 = 8;      // c8..c15: C1
constexpr uint32_t kPsFinalC0 = 16;
constexpr uint32_t kPsFinalC1 = 17;
constexpr uint32_t kPsTexcoordScale = 18;  // c18..c21: per stage (sx, sy, 1, 1)
constexpr uint32_t kPsFogColor = 24;       // the FOG register's rgb

// Vertex program: `instructions` from the start slot to the final one.
std::vector<uint32_t> translate_vertex_program(const uint32_t (*instructions)[4], uint32_t count,
                                               uint32_t attribute_mask, FogMode fog);

// PS_TEXTUREMODE values.
enum TextureMode : uint32_t {
    kModeNone = 0, kModeProject2D = 1, kModeProject3D = 2, kModeCubemap = 3, kModePassthru = 4,
    kModeDependentAR = 15, kModeDependentGB = 16,
};

// Modes that sample a 2D texture.
inline bool samples_2d(uint32_t mode) {
    return mode == kModeProject2D || mode == kModeDependentAR || mode == kModeDependentGB;
}

// The earlier stage a dependent stage reads its coordinates from (PS_INPUTTEXTURE).
inline uint32_t input_stage(const XbPixelShaderDef& def, uint32_t stage) {
    switch (stage) {
        case 2: return (def.input_texture >> 16) & 1;
        case 3: return (def.input_texture >> 20) & 3;
        default: return 0;
    }
}

struct PixelShaderKey {
    XbPixelShaderDef def;
    uint32_t alpha_kill_mask;  // D3DTSS_ALPHAKILL per stage
};

std::vector<uint32_t> translate_pixel_shader(const PixelShaderKey& key);

// A pixel shader that outputs one colour (diagnostics: highlighting draws).
std::vector<uint32_t> solid_color_pixel_shader(float r, float g, float b, float a);

// ps_2_0 (usable with pre-transformed vertices): s0 sampled at t0, each channel
// looked up in s1, a 256x1 table with the red ramp in its red channel, green in
// green, blue in blue (point sampling).
std::vector<uint32_t> gamma_ramp_pixel_shader();

// Texture modes (PS_TEXTUREMODES) of stage `stage`.
inline uint32_t texture_mode(const XbPixelShaderDef& def, uint32_t stage) {
    return (def.texture_modes >> (stage * 5)) & 0x1F;
}

}  // namespace render::d3d9
