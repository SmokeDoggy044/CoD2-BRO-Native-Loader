// Xbox Direct3D 8 replacement: render states and texture stages. See d3d.h and xbox_d3d.h.
//
// Each state has exactly one source the draw-time translation reads:
// - simple states: the NV2A registers that SetRenderState_Simple (and raw pushbuffer
//   data) writes; inlined code sends them with the method already encoded.
// - deferred and complex states: their D3D__RenderState slot. The complex states'
//   functions store it here; the deferred ones are written by inlined code.

#include <cstring>

#include "compiler.h"
#include "d3d_internal.h"
#include "nv2a.h"
#include "xbox_d3d.h"

namespace render {

namespace {

uint32_t g_fallback_render_state[kRsCount]{};
uint32_t* g_render_state = g_fallback_render_state;
uint32_t g_fallback_texture_state[kTextureStages * kTssPerStage]{};
uint32_t* g_texture_state = g_fallback_texture_state;
TextureStage g_stages[kTextureStages]{};

// ECX = method | count << 18 (count is 1), EDX = value.
void XB_FASTCALL D3DDevice_SetRenderState_Simple(uint32_t method, uint32_t value) {
    nv2a::write(method & kNvMethodMask, value);
}

// A complex state whose function only needs to record its value.
template <uint32_t Index>
void XB_STDCALL set_complex_state(uint32_t value) {
    g_render_state[Index] = value;
}

// The polygon offset registers, as D3D sets them: slope = -bias / 4, offset = -bias.
void XB_STDCALL D3DDevice_SetRenderState_ZBias(int32_t bias) {
    const float offset = -static_cast<float>(static_cast<uint32_t>(bias));
    const float slope = offset * 0.25f;
    const uint32_t enable = bias != 0;
    uint32_t slope_bits, offset_bits;
    static_assert(sizeof(float) == sizeof(uint32_t));
    std::memcpy(&slope_bits, &slope, 4);
    std::memcpy(&offset_bits, &offset, 4);
    nv2a::write(kNvPolygonOffsetScaleFactor, slope_bits);
    nv2a::write(kNvPolygonOffsetBias, offset_bits);
    nv2a::write(kNvPolygonOffsetPointEnable, enable);
    nv2a::write(kNvPolygonOffsetLineEnable, enable);
    nv2a::write(kNvPolygonOffsetFillEnable, enable);
    g_render_state[kRsZSlopeScale] = slope_bits;
    g_render_state[kRsZOffset] = offset_bits;
    g_render_state[kRsPointOffsetEnable] = enable;
    g_render_state[kRsWireframeOffsetEnable] = enable;
    g_render_state[kRsSolidOffsetEnable] = enable;
    g_render_state[kRsZBias] = static_cast<uint32_t>(bias);
}

// ARGB -> the fog colour register's ABGR, as D3D sends it.
void XB_STDCALL D3DDevice_SetRenderState_FogColor(uint32_t color) {
    nv2a::write(kNvFogColor, (color >> 16 & 0xFF) | (color & 0xFF) << 16 | (color & 0xFF00FF00));
    g_render_state[kRsFogColor] = color;
}

// Overrides the current pixel shader's texture modes (D3D keeps one value that
// SetPixelShader sets from the definition and this replaces).
void XB_STDCALL D3DDevice_SetRenderState_PSTextureModes(uint32_t modes) {
    nv2a::write(kNvShaderStageProgram, modes);
    g_render_state[kRsPSTextureModes] = modes;
}

// --- Textures ------------------------------------------------------------------------

void XB_STDCALL D3DDevice_SetTexture(uint32_t stage, XbPtr texture) {
    if (stage >= kTextureStages) return;
    g_stages[stage].texture = texture;
    const uint32_t method = kNvTextureOffset + stage * kNvTextureStride;
    if (texture) {
        const auto* pixels = xbox::from_xbox_ptr<XbPixelContainer>(texture);
        nv2a::write(method, pixels->resource.data);
        nv2a::write(method + 4, pixels->format);
    } else {
        nv2a::write(method + 4, 0);
    }
}

// D3DDevice_GetTexture2 (stage): adds a reference, like GetBackBuffer2.
XbPtr XB_STDCALL D3DDevice_GetTexture2(uint32_t stage) {
    if (stage >= kTextureStages || !g_stages[stage].texture) return 0;
    auto* resource = xbox::from_xbox_ptr<XbResource>(g_stages[stage].texture);
    ++resource->common;
    return g_stages[stage].texture;
}

// Swaps the texture of a stage for one of the same kind: ECX = the stage's
// SET_TEXTURE_OFFSET command (count 2), EDX = Data, stack: Format.
void XB_FASTCALL D3DDevice_SwitchTexture(uint32_t command, uint32_t data, uint32_t format) {
    const uint32_t method = command & kNvMethodMask;
    nv2a::write(method, data);
    nv2a::write(method + 4, format);
}

void XB_STDCALL D3DDevice_SetPalette(uint32_t stage, XbPtr palette) {
    if (stage < kTextureStages) g_stages[stage].palette = palette;
}

void XB_STDCALL D3DDevice_SetTextureState_TexCoordIndex(uint32_t stage, uint32_t value) {
    if (stage < kTextureStages) g_texture_state[stage * kTssPerStage + kTssTexCoordIndex] = value;
}

void XB_STDCALL D3DDevice_SetTextureState_BorderColor(uint32_t stage, uint32_t value) {
    if (stage < kTextureStages) g_texture_state[stage * kTssPerStage + kTssBorderColor] = value;
}

void XB_STDCALL D3DDevice_SetTextureState_ColorKeyColor(uint32_t stage, uint32_t value) {
    if (stage < kTextureStages) g_texture_state[stage * kTssPerStage + kTssColorKeyColor] = value;
}

// type: D3DTSS_BUMPENVMAT00..D3DTSS_BUMPENVLOFFSET (their own indexes).
void XB_STDCALL D3DDevice_SetTextureState_BumpEnv(uint32_t stage, uint32_t type, uint32_t value) {
    if (stage < kTextureStages && type < kTssPerStage) g_texture_state[stage * kTssPerStage + type] = value;
}

#define COMPLEX_STATE(name, index) {"D3DDevice_SetRenderState_" #name, reinterpret_cast<void*>(&set_complex_state<index>)}

}  // namespace

const xbox::KernelImpl kStateFunctions[] = {
    D3D_IMPL(D3DDevice_SetRenderState_FogColor),
    D3D_IMPL(D3DDevice_SetRenderState_PSTextureModes),
    D3D_IMPL(D3DDevice_SetRenderState_Simple),
    D3D_IMPL(D3DDevice_SetRenderState_ZBias),
    D3D_IMPL(D3DDevice_GetTexture2),
    D3D_IMPL(D3DDevice_SetPalette),
    D3D_IMPL(D3DDevice_SetTexture),
    D3D_IMPL(D3DDevice_SetTextureState_BorderColor),
    D3D_IMPL(D3DDevice_SetTextureState_BumpEnv),
    D3D_IMPL(D3DDevice_SetTextureState_ColorKeyColor),
    D3D_IMPL(D3DDevice_SetTextureState_TexCoordIndex),
    D3D_IMPL(D3DDevice_SwitchTexture),
    COMPLEX_STATE(BackFillMode, kRsBackFillMode),
    COMPLEX_STATE(CullMode, kRsCullMode),
    COMPLEX_STATE(DoNotCullUncompressed, kRsDoNotCullUncompressed),
    COMPLEX_STATE(Dxt1NoiseEnable, kRsDxt1NoiseEnable),
    COMPLEX_STATE(EdgeAntiAlias, kRsEdgeAntiAlias),
    COMPLEX_STATE(FillMode, kRsFillMode),
    COMPLEX_STATE(FrontFace, kRsFrontFace),
    COMPLEX_STATE(LineWidth, kRsLineWidth),
    COMPLEX_STATE(LogicOp, kRsLogicOp),
    COMPLEX_STATE(MultiSampleAntiAlias, kRsMultiSampleAntiAlias),
    COMPLEX_STATE(MultiSampleMask, kRsMultiSampleMask),
    COMPLEX_STATE(MultiSampleMode, kRsMultiSampleMode),
    COMPLEX_STATE(MultiSampleRenderTargetMode, kRsMultiSampleRenderTargetMode),
    COMPLEX_STATE(NormalizeNormals, kRsNormalizeNormals),
    COMPLEX_STATE(OcclusionCullEnable, kRsOcclusionCullEnable),
    COMPLEX_STATE(RopZCmpAlwaysRead, kRsRopZCmpAlwaysRead),
    COMPLEX_STATE(RopZRead, kRsRopZRead),
    COMPLEX_STATE(SampleAlpha, kRsSampleAlpha),
    COMPLEX_STATE(ShadowFunc, kRsShadowFunc),
    COMPLEX_STATE(StencilCullEnable, kRsStencilCullEnable),
    COMPLEX_STATE(StencilEnable, kRsStencilEnable),
    COMPLEX_STATE(StencilFail, kRsStencilFail),
    COMPLEX_STATE(TextureFactor, kRsTextureFactor),
    COMPLEX_STATE(TwoSidedLighting, kRsTwoSidedLighting),
    COMPLEX_STATE(VertexBlend, kRsVertexBlend),
    COMPLEX_STATE(YuvEnable, kRsYuvEnable),
    COMPLEX_STATE(ZEnable, kRsZEnable),
};
const size_t kStateFunctionCount = std::size(kStateFunctions);

uint32_t* render_state() { return g_render_state; }

void set_render_state_address(XbPtr address) {
    if (address) g_render_state = xbox::from_xbox_ptr<uint32_t>(address);
}

uint32_t* texture_state() { return g_texture_state; }

void set_texture_state_address(XbPtr address) {
    if (address) g_texture_state = xbox::from_xbox_ptr<uint32_t>(address);
}

TextureStage& texture_stage(uint32_t stage) { return g_stages[stage % kTextureStages]; }

}  // namespace render
