#pragma once

// Renderer start-up and shutdown: the resource manager objects registered at Com_Init,
// the refexport table, the shader assembler helpers.

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00023e00: removes the "PakRender" command and frees the pack render state (0x0095f638)
// and its list (0x0095f608, count 0x0095f60c).
void XB_CDECL PakRender_Shutdown();

// The registry of resource manager objects (count 0x009719f8, up to 20 at 0x009719fc):
// every constructor adds its object, the base destructor removes it.

// 0x0028d7d0 (ECX = object): the base destructor - base vtable 0x00838220, the object taken
// out of the registry (the last entry moves into its slot).
void XB_THISCALL ResourceManager_Destruct(uint8_t* self);

// 0x0028ec30 (ESI = object): registers the object, installs itself as the texture, mesh file
// and mesh manager (each sub-object keeps the previous one) and links four more sub-objects
// into the lists at 0x00909828/2c/30/38; the rest of the object (0x5c-0x1e7) is zeroed.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL ResourceManager_Construct(uint8_t* self);
extern "C" void ResourceManager_Construct_entry(void);

// 0x002c03e0 (ESI = object): the scene effects object - registered, its update callback
// 0x0001b9a0 added to the list at 0x00988780 (up to 32), the "SceneFX" and "RenderEnts"
// commands added.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL SceneFX_Init(uint8_t* self);
extern "C" void SceneFX_Init_entry(void);

// 0x00247980: Q3's GetRefAPI - the import table (0x22 words, EAX) copied into ri (0x0095e630),
// the export table (0x0098f220, 0x28 words) cleared, then filled when the API version (EDX) is
// 14; otherwise ri.Printf reports the mismatch and the result is null.
uint32_t XB_REGS2 GetRefAPI(const uint32_t* imports, uint32_t version);

// 0x00076120: "xps.1.1\n" + the formatted text, assembled with XGAssembleShader (flags
// 0x20000); *code = the compiled buffer's data. Returns the buffer (never released).
// cdecl (code, format, ...); the entry runs the formatting and the assembler at the original's ESP.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL nglAssemblePixelShader_body(uint32_t entry, uint32_t ebx, uint32_t esi,
                                                                    uint32_t edi);
extern "C" void nglAssemblePixelShader_entry(void);

// 0x00076220: "xvs.1.1\n#pragma screenspace\n" + the formatted text, followed (unless
// `no_fixup`) by the screen-space position fixup; out[0] = the compiled data, out[1] = `tag`.
// Returns `out`.
// cdecl (tag, out, no_fixup, format, ...); entry as for the pixel shader.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL nglAssembleVertexShader_body(uint32_t entry, uint32_t ebx, uint32_t esi,
                                                                     uint32_t edi);
extern "C" void nglAssembleVertexShader_entry(void);

// 0x000795c0: "CAMERA", the matrix's rows (w printed as 0 0 0 1), "ENDCAMERA" through nglDumpPrintf
// to the dump file (0x00903d5c). No callers in the title.
void XB_CDECL nglDumpCamera(const float* matrix);

// 0x00079950: a mesh instance for the scene dump - mesh file, model, scale (skin flag 2),
// matrix rows, bones (skin flags 0x3c: count +4, 0x40-byte matrices at +8); nothing for
// instances with flag 0x20000. No callers in the title.
void XB_CDECL nglDumpSceneMeshes(const uint8_t* instance, const float* matrix, const uint8_t* skin);

// 0x0006f080: the normalization cube map (0x00902cc8, edge 0x00902ccc), made again for a new edge
// length: per face and texel the unit direction as an 0xffRRGGBB colour, swizzled into the face.
// No callers in the title.
void XB_CDECL nglCreateNormalCubeMap(uint32_t size);

// 0x00077b50: the frame memory sizes not set yet - scene 0x40000, block 1 0x2000, vertex buffers
// 0x20000, index buffers 0x10000, block 2 0x180000 (nglSetFrameMemorySize, reallocating).
void XB_CDECL nglAllocFrameMemory();

// 0x00064bc0: the renderer's start-up (Sys_Main) - FPU modes (24-bit x87, SSE round toward zero), push
// buffer size, video mode, the device (present parameters at 0x00902750), frame memory, the
// 640x480 scale factors (0x009028c4/c8; 0x009028c0 set unless both are 1), render states, texture
// stage defaults, shader constants, managers, frame buffers (each back buffer cleared and shown),
// the vertical blank callback, multisampling, fog textures and the debug meshes.
void XB_CDECL nglInit();

}  // namespace game
