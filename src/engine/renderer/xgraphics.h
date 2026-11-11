#pragma once

// The XDK graphics helpers (XGRPH section) the renderer's C calls: resource headers, the
// texture swizzler, bytes per pixel, and the shader assembler for the four screen-space
// shaders nglInitShaders builds. Our own code at the original addresses (Compare rows).

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x003ebb20 (D3D section): stdcall, one ignored argument, returns 1. nglInit stores the result at
// 0x00902870 before D3D_SetPushBufferSize.
uint32_t XB_STDCALL D3D_ReturnOne(uint32_t unused);

// 0x003f9994: XGSetTextureHeader(Width, Height, Levels, Usage, Format, Pool, pTexture, Data, Pitch):
// Common = 0x40001 (reference count 1, texture), Data, Lock = 0, the Format and Size words; Usage
// bit 0x10000 clears the format word's border-colour bit (8). Returns the texture's byte size (the
// helpers 0x003f990a / 0x003f975f, ECX = height, are inlined).
uint32_t XB_STDCALL XGSetTextureHeader(uint32_t width, uint32_t height, uint32_t levels, uint32_t usage,
                                       uint32_t format, uint32_t pool, uint32_t* texture, uint32_t data,
                                       uint32_t pitch);

// 0x003f99be: XGSetCubeTextureHeader(EdgeLength, Levels, Usage, Format, Pool, pCubeTexture, Data, Pitch).
uint32_t XB_STDCALL XGSetCubeTextureHeader(uint32_t edge, uint32_t levels, uint32_t usage, uint32_t format,
                                           uint32_t pool, uint32_t* cube, uint32_t data, uint32_t pitch);

// 0x003f9953: XGSetSurfaceHeader(Width, Height, Format, pSurface, Data, Pitch): Common 0x50001,
// Data, Lock = 0, Format, Size, Parent (+0x14) = 0.
uint32_t XB_STDCALL XGSetSurfaceHeader(uint32_t width, uint32_t height, uint32_t format, uint32_t* surface,
                                       uint32_t data, uint32_t pitch);

// 0x003f99e6: XGSetVertexBufferHeader(Length, Usage, FVF, Pool, pBuffer, Data): Common = 1, Data,
// Lock = 0. Returns pBuffer.
uint32_t XB_STDCALL XGSetVertexBufferHeader(uint32_t length, uint32_t usage, uint32_t fvf, uint32_t pool,
                                            uint32_t* buffer, uint32_t data);

// 0x003f99fe: XGSetPaletteHeader(Size, pPalette, Data): Common = Size << 30 | 0x30001 (reference count
// 1, palette), Data, Lock = 0. Returns pPalette.
uint32_t XB_STDCALL XGSetPaletteHeader(uint32_t size, uint32_t* palette, uint32_t data);

// 0x003fa187: 1, 2 or 4 bytes for formats 0..0x41 (the byte table at 0x003fa1c1), else 0.
uint32_t XB_STDCALL XGBytesPerPixelFromFormat(uint32_t format);

// The swizzler (0x24 bytes): width, height, depth, the U/V/W bit masks, three cursors (unused here).
struct Swizzler {
    uint32_t width, height, depth;
    uint32_t mask_u, mask_v, mask_w;
    uint32_t u, v, w;
};

// 0x003f9a3e (ECX = swizzler): the dimensions and the interleaved masks (U, V, W take one bit each
// per power of two below their size, in that order). Returns the next free bit.
uint32_t XB_THISCALL Swizzler_Init(Swizzler* self, uint32_t width, uint32_t height, uint32_t depth);

// 0x003f9a9f / 0x003f9ac8 (ECX = swizzler): the coordinate spread over the U / V mask bits.
uint32_t XB_THISCALL Swizzler_SwizzleU(const Swizzler* self, uint32_t value);
uint32_t XB_THISCALL Swizzler_SwizzleV(const Swizzler* self, uint32_t value);

// 0x003fa203: XGSwizzleRect(pSource, Pitch, pRect, pDest, Width, Height, pPoint, BytesPerPixel).
// Width <= 2 or Height <= 1: a plain copy of Width * Height * BytesPerPixel bytes.
void XB_STDCALL XGSwizzleRect(const void* source, uint32_t pitch, const int32_t* rect, void* dest, uint32_t width,
                              uint32_t height, const int32_t* point, uint32_t bytes_per_pixel);

// An XGBUFFER (0x003fb895 XGBufferCreate): reference count, data, size.
struct XgBuffer {
    uint32_t references;
    uint32_t data;
    uint32_t size;
};

// 0x004020aa: XGAssembleShader(pSourceFileName, pSrcData, SrcDataLen, Flags, ppConstants,
// ppCompiledShader, ppErrorLog, ppListing, pResolver, pResolverUserData, pShaderType). The title
// assembles four constant texts (nglInitShaders: three xps.1.1 screen-space pixel shaders, one
// xvs.1.1 pass-through vertex shader); ours knows exactly those and returns the original's output
// for them (D3DPIXELSHADERDEF / vertex program, compared byte for byte with the original's on
// 2026-11-10), in a new buffer like the original's (never released by the title). Other text:
// E_FAIL and a message. ppConstants / ppErrorLog / ppListing get null; pShaderType is left alone
// (every title call passes null for all of them).
uint32_t XB_STDCALL XGAssembleShader(const char* file_name, const char* source, uint32_t length, uint32_t flags,
                                     XgBuffer** constants, XgBuffer** compiled, XgBuffer** errors,
                                     XgBuffer** listing, uint32_t resolver, uint32_t resolver_context,
                                     uint32_t* shader_type);

}  // namespace game
