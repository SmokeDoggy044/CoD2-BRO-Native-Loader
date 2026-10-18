#pragma once

// NGL, the title's scene library (its own 'ngl*' names: nglwhite, nglSysFont,
// nglFrontBuffer): scenes, 2D quads, meshes, textures. The function names are ours
// (docs/naming.md, Renderer). All functions are cdecl.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

struct NglTexture;

// Only the fields the decompiled functions use so far.
struct NglRenderTarget {
    uint8_t unknown00[0x10];
    uint32_t width;   // +0x10
    uint32_t height;  // +0x14
    uint8_t unknown18[0x28 - 0x18];
    uint32_t flags;   // +0x28, 4 = the 640x480 screen whatever the size fields say
};

struct NglQuadVertex {
    float x, y;
    float u, v;
    uint32_t color;  // ARGB
};

// A screen-space quad; vertices in the order top-left, top-right, bottom-left,
// bottom-right.
struct NglQuad {
    NglQuadVertex verts[4];
    float z;
    uint32_t flags;      // nglInitQuad: 0xc2
    uint32_t blend_mode; // nglInitQuad: 2
    uint32_t blend_arg;
    NglTexture* texture;
};

// Only the fields the decompiled functions use so far.
struct NglScene {
    uint8_t unknown000[0x4];
    uint32_t flags;           // +0x004
    uint32_t unknown008;      // +0x008, nglSetRenderTarget stores 6
    uint8_t unknown00c[0x110 - 0xc];
    float projection[16];     // +0x110, row vectors (nglDepthToZValue uses rows 2 and 3)
    float view_matrix[16];    // +0x150
    uint8_t unknown190[0x2d0 - 0x190];
    NglScene* parent;         // +0x2d0, the scene nglPopScene returns to
    uint8_t unknown2d4[0x2f8 - 0x2d4];
    NglRenderTarget* render_target; // +0x2f8
    uint8_t unknown2fc[0x360 - 0x2fc];
    uint32_t clear_flags;     // +0x360
    float clear_depth;        // +0x364
    uint8_t unknown368[0x370 - 0x368];
    float clear_color[4];     // +0x370, r g b a
    uint32_t color_write_mask; // +0x380, 0x01000000 alpha .. 0x00010101 rgb
    uint8_t unknown384[0x388 - 0x384];
    float fog[4];             // +0x388, nglSetFog's arguments (fog[0] = start)
    uint8_t unknown398[0x3a0 - 0x398];
    float fog_color[4];       // +0x3a0, r g b, 1
    uint8_t unknown3b0[0x3bc - 0x3b0];
    float aspect;             // +0x3bc
    uint8_t unknown3c0[0x3c4 - 0x3c0];
    float near_z;             // +0x3c4
    float far_z;              // +0x3c8
};

#if UINTPTR_MAX == 0xFFFFFFFF  // the title's layout (32-bit pointers)
static_assert(sizeof(NglQuadVertex) == 0x14);
static_assert(sizeof(NglQuad) == 0x64);
static_assert(offsetof(NglQuad, texture) == 0x60);
static_assert(offsetof(NglScene, view_matrix) == 0x150);
static_assert(offsetof(NglScene, parent) == 0x2d0);
static_assert(offsetof(NglScene, render_target) == 0x2f8);
static_assert(offsetof(NglScene, clear_color) == 0x370);
static_assert(offsetof(NglScene, fog) == 0x388);
static_assert(offsetof(NglScene, fog_color) == 0x3a0);
static_assert(offsetof(NglScene, projection) == 0x110);
static_assert(offsetof(NglScene, near_z) == 0x3c4);
static_assert(offsetof(NglRenderTarget, flags) == 0x28);
#endif

// 0x0005ddb0: white, untextured, UVs 0..1, flags 0xc2, blend mode 2.
void XB_CDECL nglInitQuad(NglQuad* quad);
// 0x0005de20
void XB_CDECL nglSetQuadTexture(NglQuad* quad, NglTexture* texture);
// 0x0005de30
void XB_CDECL nglSetQuadFlags(NglQuad* quad, uint32_t flags);
// 0x0005de40
void XB_CDECL nglSetQuadBlend(NglQuad* quad, uint32_t mode, uint32_t arg);
// 0x0005de60
void XB_CDECL nglSetQuadUV(NglQuad* quad, float u0, float v0, float u1, float v1);
// 0x0005deb0: all four vertices.
void XB_CDECL nglSetQuadColor(NglQuad* quad, uint32_t color);
// 0x0005ded0
void XB_CDECL nglSetQuadRect(NglQuad* quad, float x0, float y0, float x1, float y1);
// 0x0005df20
void XB_CDECL nglSetQuadZ(NglQuad* quad, float z);
// 0x0005df30
void XB_CDECL nglSetQuadVertexPos(NglQuad* quad, int32_t vertex, float x, float y);
// 0x0005df60
void XB_CDECL nglSetQuadVertexUV(NglQuad* quad, int32_t vertex, float u, float v);
// 0x0005df90
void XB_CDECL nglSetQuadVertexColor(NglQuad* quad, int32_t vertex, uint32_t color);

// Setters on the current scene (nglPushScene's).
// 0x0005e430
void XB_CDECL nglSetClearFlags(uint32_t flags);
// 0x0005e450
void XB_CDECL nglSetClearDepth(float depth);
// 0x0005e470
void XB_CDECL nglSetColorWriteMask(uint32_t mask);
// 0x0005e4d0: the fog start no nearer than the near plane.
void XB_CDECL nglSetFog(float start, float value1, float value2, float value3);
// 0x0005e730: makes the parent current; returns the scene left.
NglScene* XB_CDECL nglPopScene();
// 0x0005e750: returns the previous current scene.
NglScene* XB_CDECL nglSetCurrentScene(NglScene* scene);
// 0x0005eaa0
void XB_CDECL nglSetClearColor(float r, float g, float b, float a);
// 0x0005eb60: alpha 1.
void XB_CDECL nglSetFogColor(float r, float g, float b);
// Matrices are 4x4 row-vector matrices (16 floats, row by row). The title's quirks
// are kept (see nglMatrixViewport).
// 0x0005ec20: x_scale, y_scale on the diagonal, depth mapped to 0..1 over near..far.
void XB_CDECL nglMatrixPerspective(float* out, float x_scale, float y_scale, float near_z, float far_z);
// 0x0005ed60: the same with a linear depth.
void XB_CDECL nglMatrixOrtho(float* out, float x_scale, float y_scale, float near_z, float far_z);
// 0x0005f010: screen pixels of the current scene's render target to -1..1.
void XB_CDECL nglMatrixScreenToClip(float* out);
// 0x0005f1b0: -1..1 to the rectangle x0, y0 .. x1, y1.
void XB_CDECL nglMatrixViewport(float* out, float x0, float y0, float x1, float y1);
// 0x0006e860: a view depth (clamped to near..far) to the 24-bit depth buffer value.
float XB_CDECL nglDepthToZValue(float depth);
// 0x00064640 / 0x00064650: the title's fixed 640x480 screen.
int32_t XB_CDECL nglGetScreenWidth();
int32_t XB_CDECL nglGetScreenHeight();
// 0x0006e780: ARGB bytes to r, g, b, a floats in 0..1.
void XB_CDECL nglColorToVec4(float* out, uint32_t color);
// 0x00073a70
void XB_CDECL nglSetRenderTarget(NglRenderTarget* target);
// 0x00073ae0
void XB_CDECL nglSetSceneFlags(uint32_t flags);
// 0x0007b220: swaps bytes 0 and 2 of `count` 32-bit pixels.
void XB_CDECL nglSwapRedBlue(uint8_t* pixels, uint32_t count);

}  // namespace game
