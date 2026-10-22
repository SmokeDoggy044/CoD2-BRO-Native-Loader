#pragma once

// The shader classes ('cd*' and PCUV, 0x00045790-0x00055400): one singleton object per
// class, made by its Init at startup and linked onto the class list (0x00909904).
// Vtable: Draw, Register, GetName, AddToList, Load/ReleaseTexture, Relocate (naming.md).
// AddToList puts a mesh section onto the current scene's opaque or translucent draw
// list, unless the class is turned off in the disable mask (0x00901e8c..0x00901e8f,
// one bit per class; the Toggle functions flip it, Init clears it).

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "ngl.h"

namespace game {

struct NglShaderClass {
    void** vtable;
    NglShaderClass* next;  // +0x04, the class list
    uint32_t id;           // +0x08, Register's sequence number (0x009028ec); the opaque sort key
};

// The title's hashed name (0x00011060): a case-insensitive hash and the name, lowered.
struct NglHashedName {
    uint32_t hash;
    char name[28];
};

#if UINTPTR_MAX == 0xFFFFFFFF
static_assert(sizeof(NglShaderClass) == 0xc);
static_assert(sizeof(NglHashedName) == 0x20);
#endif

// Toggle: flips the class's disable bit. Init: makes the singleton (Z_Malloc) and
// enables the class. GetName: the class name (out, a hashed name). AddToList (thiscall,
// the singleton): a NglMeshNode in scene memory (vtable per class) for the section.
void XB_CDECL cdSimpleInstance_Toggle();

void XB_CDECL cdDebug_Toggle();
void XB_CDECL cdDebug_Init();
NglHashedName* XB_THISCALL cdDebug_GetName(const NglShaderClass* self, NglHashedName* out);
void XB_THISCALL cdDebug_AddToList(const NglShaderClass* self, NglMeshDrawNode* node,
                                   const NglMeshSection* section, const uint8_t* material);

void XB_CDECL ShaderPCUV_Init();
NglHashedName* XB_THISCALL ShaderPCUV_GetName(const NglShaderClass* self, NglHashedName* out);
void XB_THISCALL ShaderPCUV_AddToList(const NglShaderClass* self, NglMeshDrawNode* node,
                                      const NglMeshSection* section, const uint8_t* material);

void XB_CDECL cdGunSight_Toggle();
void XB_CDECL cdGunSight_Init();
NglHashedName* XB_THISCALL cdGunSight_GetName(const NglShaderClass* self, NglHashedName* out);
void XB_THISCALL cdGunSight_AddToList(const NglShaderClass* self, NglMeshDrawNode* node,
                                      const NglMeshSection* section, const uint8_t* material);

void XB_CDECL cdGun_Toggle();
void XB_CDECL cdGun_Init();
NglHashedName* XB_THISCALL cdGun_GetName(const NglShaderClass* self, NglHashedName* out);
void XB_THISCALL cdGun_AddToList(const NglShaderClass* self, NglMeshDrawNode* node,
                                 const NglMeshSection* section, const uint8_t* material);

void XB_CDECL cdGlass_Toggle();
void XB_CDECL cdGlass_Init();
NglHashedName* XB_THISCALL cdGlass_GetName(const NglShaderClass* self, NglHashedName* out);
void XB_THISCALL cdGlass_AddToList(const NglShaderClass* self, NglMeshDrawNode* node,
                                   const NglMeshSection* section, const uint8_t* material);

void XB_CDECL cdSimpleAlpha_Toggle();
void XB_CDECL cdSimpleAlpha_Init();
NglHashedName* XB_THISCALL cdSimpleAlpha_GetName(const NglShaderClass* self, NglHashedName* out);
void XB_THISCALL cdSimpleAlpha_AddToList(const NglShaderClass* self, NglMeshDrawNode* node,
                                         const NglMeshSection* section, const uint8_t* material);

void XB_CDECL cdPropeller_Toggle();
void XB_CDECL cdPropeller_Init();
NglHashedName* XB_THISCALL cdPropeller_GetName(const NglShaderClass* self, NglHashedName* out);
void XB_THISCALL cdPropeller_AddToList(const NglShaderClass* self, NglMeshDrawNode* node,
                                       const NglMeshSection* section, const uint8_t* material);

void XB_CDECL cdDecal_Toggle();
void XB_CDECL cdDecal_Init();
NglHashedName* XB_THISCALL cdDecal_GetName(const NglShaderClass* self, NglHashedName* out);
void XB_THISCALL cdDecal_AddToList(const NglShaderClass* self, NglMeshDrawNode* node,
                                   const NglMeshSection* section, const uint8_t* material);

void XB_CDECL cdChar_Toggle();
void XB_CDECL cdChar_Init();
NglHashedName* XB_THISCALL cdChar_GetName(const NglShaderClass* self, NglHashedName* out);
void XB_THISCALL cdChar_AddToList(const NglShaderClass* self, NglMeshDrawNode* node,
                                  const NglMeshSection* section, const uint8_t* material);

void XB_CDECL cdPrelit_Toggle();
void XB_CDECL cdPrelit_Init();
NglHashedName* XB_THISCALL cdPrelit_GetName(const NglShaderClass* self, NglHashedName* out);
void XB_THISCALL cdPrelit_AddToList(const NglShaderClass* self, NglMeshDrawNode* node,
                                    const NglMeshSection* section, const uint8_t* material);

void XB_CDECL cdAirplaneMetal_Toggle();
void XB_CDECL cdAirplaneMetal_Init();
NglHashedName* XB_THISCALL cdAirplaneMetal_GetName(const NglShaderClass* self, NglHashedName* out);
void XB_THISCALL cdAirplaneMetal_AddToList(const NglShaderClass* self, NglMeshDrawNode* node,
                                           const NglMeshSection* section, const uint8_t* material);

void XB_CDECL cdSimpleUVAnim_Toggle();
void XB_CDECL cdSimpleUVAnim_Init();
NglHashedName* XB_THISCALL cdSimpleUVAnim_GetName(const NglShaderClass* self, NglHashedName* out);
void XB_THISCALL cdSimpleUVAnim_AddToList(const NglShaderClass* self, NglMeshDrawNode* node,
                                          const NglMeshSection* section, const uint8_t* material);

void XB_CDECL cdSimplePrelit_Toggle();
void XB_CDECL cdSimplePrelit_Init();
NglHashedName* XB_THISCALL cdSimplePrelit_GetName(const NglShaderClass* self, NglHashedName* out);
void XB_THISCALL cdSimplePrelit_AddToList(const NglShaderClass* self, NglMeshDrawNode* node,
                                          const NglMeshSection* section, const uint8_t* material);

void XB_CDECL cdSimple_Toggle();
void XB_CDECL cdSimple_Init();
NglHashedName* XB_THISCALL cdSimple_GetName(const NglShaderClass* self, NglHashedName* out);
void XB_THISCALL cdSimple_AddToList(const NglShaderClass* self, NglMeshDrawNode* node,
                                    const NglMeshSection* section, const uint8_t* material);

void XB_CDECL cdRiver_Toggle();
void XB_CDECL cdRiver_Init();
NglHashedName* XB_THISCALL cdRiver_GetName(const NglShaderClass* self, NglHashedName* out);
void XB_THISCALL cdRiver_AddToList(const NglShaderClass* self, NglMeshDrawNode* node,
                                   const NglMeshSection* section, const uint8_t* material);

void XB_CDECL cdOcean_Toggle();
void XB_CDECL cdOcean_Init();
NglHashedName* XB_THISCALL cdOcean_GetName(const NglShaderClass* self, NglHashedName* out);
void XB_THISCALL cdOcean_AddToList(const NglShaderClass* self, NglMeshDrawNode* node,
                                   const NglMeshSection* section, const uint8_t* material);
// 0x0004ef90 / 0x0004f370 (index in EAX): wave set `index` (0x00901b30, 0xd0 bytes of
// float4 rows) into the ocean's tweak globals (0x00902134.., 0x0089e6dc..), and back;
// storing pads the rows with 0 or 1.0 where no global maps.
void XB_REGS1 XB_CDECL cdOcean_LoadWaveSet(uint32_t index);
void XB_REGS1 XB_CDECL cdOcean_StoreWaveSet(uint32_t index);

void XB_CDECL cdWater_Toggle();
void XB_CDECL cdWater_Init();
NglHashedName* XB_THISCALL cdWater_GetName(const NglShaderClass* self, NglHashedName* out);
void XB_THISCALL cdWater_AddToList(const NglShaderClass* self, NglMeshDrawNode* node,
                                   const NglMeshSection* section, const uint8_t* material);

void XB_CDECL cdWorldBlend_Toggle();
void XB_CDECL cdWorldBlend_Init();
NglHashedName* XB_THISCALL cdWorldBlend_GetName(const NglShaderClass* self, NglHashedName* out);
void XB_THISCALL cdWorldBlend_AddToList(const NglShaderClass* self, NglMeshDrawNode* node,
                                        const NglMeshSection* section, const uint8_t* material);

void XB_CDECL cdWorldVertexLit_Toggle();
void XB_CDECL cdWorldVertexLit_Init();
NglHashedName* XB_THISCALL cdWorldVertexLit_GetName(const NglShaderClass* self, NglHashedName* out);
void XB_THISCALL cdWorldVertexLit_AddToList(const NglShaderClass* self, NglMeshDrawNode* node,
                                            const NglMeshSection* section, const uint8_t* material);

void XB_CDECL cdWorld_Toggle();
void XB_CDECL cdWorld_Init();
NglHashedName* XB_THISCALL cdWorld_GetName(const NglShaderClass* self, NglHashedName* out);
void XB_THISCALL cdWorld_AddToList(const NglShaderClass* self, NglMeshDrawNode* node,
                                   const NglMeshSection* section, const uint8_t* material);

void XB_CDECL cdBackground_Toggle();
void XB_CDECL cdBackground_Init();
NglHashedName* XB_THISCALL cdBackground_GetName(const NglShaderClass* self, NglHashedName* out);
void XB_THISCALL cdBackground_AddToList(const NglShaderClass* self, NglMeshDrawNode* node,
                                        const NglMeshSection* section, const uint8_t* material);

void XB_CDECL cdSky_Toggle();
void XB_CDECL cdSky_Init();
NglHashedName* XB_THISCALL cdSky_GetName(const NglShaderClass* self, NglHashedName* out);
void XB_THISCALL cdSky_AddToList(const NglShaderClass* self, NglMeshDrawNode* node,
                                 const NglMeshSection* section, const uint8_t* material);

NglHashedName* XB_THISCALL nglDebugShader_GetName(const NglShaderClass* self, NglHashedName* out);

// Register's shader tables: nglVertexShader_Init(&shaders[i], declaration, programs[i]) /
// nglPixelShader_Init(&shaders[i], programs[i]) over a table of the class.
void XB_CDECL cdGlass_InitVertexShaders(uint32_t declaration);
void XB_CDECL cdGlass_InitPixelShaders();
void XB_CDECL cdSimpleAlpha_InitVertexShaders(uint32_t declaration);
void XB_CDECL cdSimpleAlpha_InitPixelShaders();
void XB_CDECL cdSimpleAlpha_InitPixelShaders2();
void XB_CDECL cdRiver_InitPixelShaders();
void XB_CDECL cdOcean_InitPixelShaders();
void XB_CDECL cdWorldBlend_InitVertexShaders(uint32_t declaration);
void XB_CDECL cdWorldBlend_InitVertexShaders2(uint32_t declaration);
void XB_CDECL cdWorldBlend_InitPixelShaders();
void XB_CDECL cdWorldBlend_InitPixelShaders2();
void XB_CDECL cdWorld_InitVertexShaders(uint32_t declaration);
void XB_CDECL cdWorld_InitVertexShaders2(uint32_t declaration);
void XB_CDECL cdWorld_InitPixelShaders();
void XB_CDECL cdWorld_InitPixelShaders2();

// Draw (thiscall, the list node; vtable slot 0 of the node): render states through
// NGL's state cache (0x00902f08), the node's world * view * projection (vertex
// constants 6..9), the material's texture on stage 0, the class's shaders, the fog
// constants and colour, then the section; the blend-mode cache is reset afterwards
// (the states were set directly).
// 0x00052c50: alpha blended, no alpha test, no culling.
void XB_THISCALL cdWorldVertexLit_Draw(const NglMeshNode* node);
// 0x000544a0: alpha tested (>= 0x80 with function 0x206), not blended, no culling.
void XB_THISCALL cdBackground_Draw(const NglMeshNode* node);
// 0x0004dd50: alpha tested (0x204, 0x80), not blended; culled unless the material's
// +0x20 is 2 (two-sided).
void XB_THISCALL cdSimplePrelit_Draw(const NglMeshNode* node);

// 0x0004c450: no alpha test, not blended, no culling; the material's second texture
// (+0x24) on stage 1 when set; fog off.
void XB_THISCALL cdPrelit_Draw(const NglMeshNode* node);
// 0x000472e0: the material's blend mode (+0x20) and texture address flags (+0x24; bit
// 6 / bit 7 pick wrap modes for stage 0), the instance's decal bias when its
// parameter mask has the bias slot, no culling, fog on.
void XB_THISCALL ShaderPCUV_Draw(const NglMeshNode* node);
// 0x000476b0: the texture animation frame (0x009026fc, nglSetTexture's frame pick):
// the instance parameter in slot 0x00902918 when its mask (64 bits) has that slot,
// else the scene's frame counter (+0x3d4).
void XB_THISCALL nglSetTextureAnimFrame(const NglMeshNode* node);

// 0x00047b10 / 0x00048320 / 0x0004e450: the lit, alpha-tested (0x204, 0x80) classes:
// culled unless two-sided, the node's four object lights (nglComputeObjectLights)
// after the matrix as constants 6..17, fog on; they differ only in their shaders.
void XB_THISCALL cdGunSight_Draw(const NglMeshNode* node);
void XB_THISCALL cdGun_Draw(const NglMeshNode* node);
void XB_THISCALL cdSimple_Draw(const NglMeshNode* node);

// 0x0004bd00: the skinned characters: no culling, alpha tested (0x204, 0x80), the
// texture animation frame, object lights as constants 6..17, then the skinning
// constants (nglSetSkinningConstants, slot -0x4c).
void XB_THISCALL cdChar_Draw(const NglMeshNode* node);
// 0x0004d560: the UV animation parameters - the vec4 at +0x30 of the instance's
// object in slot 0x00902908, else the default at 0x007b5b80.
void XB_THISCALL cdSimpleUVAnim_GetUVParams(const NglMeshNode* node, float* out);
// 0x0004d610: the lit alpha-tested template with the UV parameters as constant 18.
void XB_THISCALL cdSimpleUVAnim_Draw(const NglMeshNode* node);
// 0x000469b0: one colour (constant 12; the instance's, else white), blend mode 2.
void XB_THISCALL cdDebug_Draw(const NglMeshNode* node);
// 0x0004b560: alpha blended with decal bias 10; the matrix's depth row offset by the
// material's +0x20 over the view depth.
void XB_THISCALL cdDecal_Draw(const NglMeshNode* node);
// 0x000511c0: alpha blended, two textures, the scroll offsets as constant 10.
void XB_THISCALL cdWater_Draw(const NglMeshNode* node);
// 0x000505c0: up to four textures; shaders and the transform slot from the node
// class's vtable, the wave constants from 0x0004f810.
void XB_THISCALL cdOcean_Draw(const NglMeshNode* node);
// 0x0004ad60: alpha blended, the lit template with the blade angle as constant 18.
void XB_THISCALL cdPropeller_Draw(const NglMeshNode* node);
// 0x00049c30: clamps the debug override's pulse ranges (0x0089b020..0x0089b02c) to 0..1.
void XB_CDECL cdSimpleAlpha_ClampDebugPulse();
// 0x00049cd0: the pulsing colour: w = (sine(speed * time * 2 pi) + 1) / 2 (the title's
// packed sine polynomial), t0 = lo0..hi0 at w and t1 = lo1..hi1 at w (each clamped to
// 0..1); out = (rgb * t0 + 1 - t0, a * t1). EAX = out (returned), ECX = colour, XMM1 =
// speed: entered through cdSimpleAlpha_PulseColor_entry.
extern "C" PLATFORM_HIDDEN float* XB_CDECL cdSimpleAlpha_PulseColor(float* out, const float* colour, float speed,
                                                                    float lo0, float hi0, float lo1, float hi1,
                                                                    float time);
extern "C" void cdSimpleAlpha_PulseColor_entry(void);
// 0x00049e40: the material's colour (or the instance's alpha, or the debug override at
// 0x0089aff0), optionally pulsing, as constant 20 after the matrix and lights (8..19); a
// depth-only pre-pass for material flag 1, then the blended pass (material +0x24: 1
// add-alpha, 2 add-one, 3 alpha-tested, else alpha blend).
void XB_THISCALL cdSimpleAlpha_Draw(const NglMeshNode* node);
// 0x00053440: the world geometry: lightmap (+0x24) and the first shadow projector
// reaching the mesh, fog, then an additive pass for up to four dynamic lights.
void XB_THISCALL cdWorld_Draw(const NglMeshNode* node);
// 0x00051b10: cdWorld's draw with two blended textures, the lightmap on stage 2 and
// the projector on stage 3.
void XB_THISCALL cdWorldBlend_Draw(const NglMeshNode* node);
// 0x0004cbf0: two textures, the first object light and the camera in object space,
// scrolling UVs, a shine constant for the pixel shader.
void XB_THISCALL cdAirplaneMetal_Draw(const NglMeshNode* node);
// 0x00054ad0: the sky, turning about z with the time, scrolling UVs; no fog, depth
// test or depth writes (restored after); blended when the material's +0x20 is set.
void XB_THISCALL cdSky_Draw(const NglMeshNode* node);
// 0x00048a10: additive-over (ONE, INVSRCALPHA) glass: the world-view rotation, the
// first object light and the camera in object space; lit when the material's flags
// (+0x30) have bit 0.
void XB_THISCALL cdGlass_Draw(const NglMeshNode* node);
// 0x0007b670: NGL's own debug shader: one colour (constant 10; the instance's, else
// white), blend mode 2, no fog change.
void XB_THISCALL nglDebugShader_Draw(const NglMeshNode* node);

// 0x00049280: a mesh draw's world matrix (at its start) times the scene's view matrix.
float* XB_THISCALL nglNode_GetWorldViewRotation(const float* world, float* out);

// A skip list of reference-counted entries keyed by a hashed name (the shader cache at
// 0x00902ac0). The head is a node whose next[] start at the same offset; the list ends
// at a node whose key compares above every name.
struct ShaderCacheNode {
    NglHashedName key;
    void* value;               // +0x20
    int32_t references;        // +0x24
    ShaderCacheNode* next[1];  // +0x28, one per level up to the node's level
};

struct ShaderCache {
    uint8_t unknown00[0x4];
    ShaderCacheNode* head;     // +0x04
    int32_t bits_left;         // +0x08, 2-bit pairs left in random_bits
    int32_t random_bits;       // +0x0c, a rand() consumed two bits at a time
    int32_t level;             // +0x10, the highest level in use
};

#if UINTPTR_MAX == 0xFFFFFFFF
static_assert(offsetof(ShaderCacheNode, next) == 0x28);
static_assert(sizeof(ShaderCache) == 0x14);
#endif

// A new node's level: one more per zero 2-bit pair (chance 1/4 each), at most 15.
int32_t XB_THISCALL ShaderCache_RandomLevel(ShaderCache* self);
// The node with the key gets one more reference and is returned; otherwise a node
// {key, value, 1 reference} is added and the result is null.
ShaderCacheNode* XB_THISCALL ShaderCache_FindOrAdd(ShaderCache* self, const NglHashedName* key, void* value);

// Register (vtable slot 0, startup): the class gets its sort id and its shader cache
// entry under its name, then makes its vertex and pixel shaders.
void XB_THISCALL cdDebug_Register(NglShaderClass* self);
void XB_THISCALL ShaderPCUV_Register(NglShaderClass* self);
void XB_THISCALL cdGunSight_Register(NglShaderClass* self);
void XB_THISCALL cdGun_Register(NglShaderClass* self);
void XB_THISCALL cdGlass_Register(NglShaderClass* self);
void XB_THISCALL cdSimpleAlpha_Register(NglShaderClass* self);
void XB_THISCALL cdPropeller_Register(NglShaderClass* self);
void XB_THISCALL cdDecal_Register(NglShaderClass* self);
void XB_THISCALL cdChar_Register(NglShaderClass* self);
void XB_THISCALL cdPrelit_Register(NglShaderClass* self);
void XB_THISCALL cdAirplaneMetal_Register(NglShaderClass* self);
void XB_THISCALL cdSimpleUVAnim_Register(NglShaderClass* self);
void XB_THISCALL cdSimplePrelit_Register(NglShaderClass* self);
void XB_THISCALL cdSimple_Register(NglShaderClass* self);
void XB_THISCALL cdRiver_Register(NglShaderClass* self);
void XB_THISCALL cdOcean_Register(NglShaderClass* self);
void XB_THISCALL cdWater_Register(NglShaderClass* self);
void XB_THISCALL cdWorldBlend_Register(NglShaderClass* self);
void XB_THISCALL cdWorldVertexLit_Register(NglShaderClass* self);
void XB_THISCALL cdWorld_Register(NglShaderClass* self);
void XB_THISCALL cdBackground_Register(NglShaderClass* self);
void XB_THISCALL cdSky_Register(NglShaderClass* self);

// 0x00045610 / 0x00045620: the debug view mode (0x00901e94); console toggles set it,
// the cd* Draws pick program variants by it (== 1).
void XB_CDECL R_SetDebugViewMode(uint32_t mode);
uint32_t XB_CDECL R_GetDebugViewMode();
// 0x00047200: a PCUV material: (a, b, c) to +0x1c/+0x20 (the mode ShaderPCUV_AddToList
// reads)/+0x24, the PCUV shader class to +4; returns the material.
uint8_t* XB_THISCALL ShaderPCUV_InitMaterial(uint8_t* material, uint32_t a, uint32_t b, uint32_t c);

}  // namespace game
