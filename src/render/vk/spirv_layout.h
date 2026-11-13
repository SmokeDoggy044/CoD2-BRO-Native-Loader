#pragma once

// The title's and the post passes' shaders on Vulkan: vkd3d turns our vs_3_0 / ps_3_0 (d3dbc) into SPIR-V at
// build time, numbering bindings and locations its own way (bindings in order of use, locations = register
// numbers). spirv_fixed_layout rewrites them into one layout every pipeline shares, reading the semantics
// from the d3dbc's declarations:
// - descriptor set 0: binding 0 = vertex constants (c0..c194 as float4), 1 = pixel constants (c0..c28),
//   2..9 = sampled images t0..t7, 10..17 = samplers s0..s7 (kGfxTextureSlots);
// - vertex inputs: location n for TEXCOORDn (the NV2A's v0..v15, as the D3D9 path's declarations);
// - vertex outputs = pixel inputs, by semantic: COLOR0 0, COLOR1 1, TEXCOORDn 2+n, FOG 10.
// - pixel outputs: location n for oCn, as vkd3d gives them.

#include <cstdint>
#include <string>
#include <vector>

namespace render::vk {

constexpr uint32_t kVkVertexConstantsBinding = 0;
constexpr uint32_t kVkPixelConstantsBinding = 1;
constexpr uint32_t kVkFirstImageBinding = 2;
constexpr uint32_t kVkFirstSamplerBinding = 10;
constexpr uint32_t kVkBindingCount = 18;
constexpr uint32_t kVkVertexConstants = 195;  // float4 registers (kGfxVsFog + 1)
constexpr uint32_t kVkPixelConstants = 29;    // kGfxPsReserved

constexpr uint32_t kVkLocationColor0 = 0;
constexpr uint32_t kVkLocationColor1 = 1;
constexpr uint32_t kVkLocationTexcoord0 = 2;  // + n, n < 8
constexpr uint32_t kVkLocationFog = 10;

// d3dbc: the shader's tokens; spirv: vkd3d's translation of the same tokens. Returns the SPIR-V with the
// fixed layout, or an empty vector with the reason in *error.
std::vector<uint32_t> spirv_fixed_layout(const void* d3dbc, uint32_t d3dbc_bytes, const void* spirv,
                                         uint32_t spirv_bytes, std::string* error);

}  // namespace render::vk
