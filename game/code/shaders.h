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

}  // namespace game
