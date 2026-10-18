#pragma once

// NGL, the title's scene library (its own 'ngl*' names: nglwhite, nglSysFont,
// nglFrontBuffer): scenes, 2D quads, meshes, textures. The function names are ours
// (docs/naming.md, Renderer). All functions are cdecl.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// Only the fields the decompiled functions use so far.
struct NglTexture {
    uint8_t unknown00[0x4];
    uint32_t flags;          // +0x04, low byte = kind (0x10: a reference to `target`),
                             // 0x400 = render target
    uint8_t unknown08[0x2c - 0x8];
    int32_t last_used_frame; // +0x2c
    uint8_t unknown30[0x4c - 0x30];
    NglTexture* target;      // +0x4c, kind 0x10
};

// Allocated from the per-frame scene memory.
struct NglRenderTargetLink {
    NglTexture* texture;
    NglRenderTargetLink* next;
};

struct NglLight;

// One list per light category (flag bits 24..31 of a light), each ending at the
// set itself; the current set is where nglLightSetAddLight adds.
struct alignas(16) NglLightSet {
    NglLight* lists[8];
    uint8_t unknown20[0x30 - 0x20];
    void* unknown30[8];      // +0x30, all start pointing at unknown30
    uint8_t unknown50[0x60 - 0x50];
    float ambient[4];        // +0x60, r g b 1, each 0..1
};

struct alignas(16) NglLight {
    NglLight* next[8];       // per list in NglLightSet::lists
    uint8_t unknown20[0x24 - 0x20];
    uint32_t flags;          // +0x24, bits 24..31: the lists it is on
    uint32_t type;           // +0x28, 0 or 1 (see nglLightVectorWorld)
    float* data;             // +0x2c
};

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
    NglRenderTargetLink* render_targets; // +0x000
    uint32_t flags;           // +0x004
    uint32_t unknown008;      // +0x008, nglSetRenderTarget stores 6
    uint8_t unknown00c[0x110 - 0xc];
    float projection[16];     // +0x110, row vectors (nglDepthToZValue uses rows 2 and 3)
    float view_matrix[16];    // +0x150
    uint8_t unknown190[0x2d0 - 0x190];
    NglScene* parent;         // +0x2d0, the scene nglPopScene returns to
    uint8_t unknown2d4[0x2e0 - 0x2d4];
    struct Callback {
        void* function;
        void* arg;
    } callbacks[3];           // +0x2e0, nglSetSceneCallback's (nglRenderSceneTree calls
                              // three: before, between and after its two draw lists)
    NglRenderTarget* render_target; // +0x2f8
    uint8_t unknown2fc[0x314 - 0x2fc];
    NglLightSet* light_set;   // +0x314
    uint8_t unknown318[0x360 - 0x318];
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

// A section of a mesh: only its bounding sphere is known.
struct NglMeshSection {
    uint8_t unknown00[0x10];
    float center[4];  // +0x10
    float radius;     // +0x20
};

struct NglMeshSectionEntry {
    uint32_t unknown0;
    NglMeshSection* section;
};

// The mesh nglMeshBuilder_* fills (one at a time, global 0x00902920).
struct NglMeshBuilder {
    uint8_t unknown00[0x08];
    uint32_t section_count;         // +0x08
    NglMeshSectionEntry* sections;  // +0x0c
    uint8_t unknown10[0x20 - 0x10];
    float center[4];                // +0x20
    float radius;                   // +0x30
};

#if UINTPTR_MAX == 0xFFFFFFFF  // the title's layout (32-bit pointers)
static_assert(offsetof(NglMeshBuilder, center) == 0x20);
static_assert(offsetof(NglMeshBuilder, radius) == 0x30);
static_assert(sizeof(NglMeshSectionEntry) == 8);
static_assert(sizeof(NglQuadVertex) == 0x14);
static_assert(sizeof(NglQuad) == 0x64);
static_assert(offsetof(NglQuad, texture) == 0x60);
static_assert(offsetof(NglScene, view_matrix) == 0x150);
static_assert(offsetof(NglScene, parent) == 0x2d0);
static_assert(offsetof(NglScene, callbacks) == 0x2e0);
static_assert(offsetof(NglScene, render_target) == 0x2f8);
static_assert(offsetof(NglScene, light_set) == 0x314);
static_assert(offsetof(NglTexture, target) == 0x4c);
static_assert(sizeof(NglLightSet) == 0x70);
static_assert(offsetof(NglLight, data) == 0x2c);
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
// 0x0005dfb0: rotates the vertex positions by `angle` (radians) around (cx, cy).
void XB_CDECL nglRotateQuadVerts(NglQuad* quad, float cx, float cy, float angle);
// 0x0005e040: scales the vertex positions by (sx, sy) around (cx, cy).
void XB_CDECL nglScaleQuadVerts(NglQuad* quad, float cx, float cy, float sx, float sy);
// 0x0005e110: rotates the texture coordinates by `angle` around (cu, cv).
void XB_CDECL nglRotateQuad(NglQuad* quad, float cu, float cv, float angle);

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
// 0x0005e3f0: render targets the scene draws into (flag 0x400) get a link in the
// scene's list, from the frame's scene memory.
void XB_CDECL nglSceneAddRenderTarget(NglTexture* texture);
// 0x0005e5c0: which = 0..2 (others are ignored).
void XB_CDECL nglSetSceneCallback(uint32_t which, void* function, void* arg);

// Textures.
// 0x000633b0 / 0x000633c0
void XB_CDECL nglSetTextureManager(void* manager);
void* XB_CDECL nglGetTextureManager();
// 0x00063520
void XB_CDECL nglAddRef(uint32_t* refs);
// 0x00063600: not used in the last frame (references are followed first).
bool XB_CDECL nglTextureIsStale(const NglTexture* texture);
// 0x00064560: 1 when the video mode is PAL 50 Hz, else 0.
uint32_t XB_CDECL nglIsPAL50();

// 0x000645c0: vertical blanks per frame (mode 0..2 = 0, 1, 2; 3 and 4 = 1 and 2 plus a
// flag at 0x00902878); other modes are ignored. The loader's 60 fps pacing replaces
// it (--pc-patches).
void XB_CDECL nglSetVBlankMode(uint32_t mode);

// Per-frame bump allocators (no null check; `align` a power of two).
// 0x00066ad0: GPU memory.
void* XB_CDECL nglAllocFrameGpu(uint32_t size, uint32_t align);
// 0x00077b30: CPU scratch memory.
void* XB_CDECL nglAllocFrameScratch(uint32_t size, uint32_t align);

// Lights.
// 0x0006f550
void XB_CDECL nglSetSceneLightSet(NglLightSet* set);
// 0x0006f580: a light on the current light set's lists named by flags bits 24..31;
// null when the frame's scene memory is exhausted.
NglLight* XB_CDECL nglLightSetAddLight(uint32_t type, float* data, uint32_t flags);
// 0x0006f7c0: an empty light set (white ambient), made current; null when the frame's
// scene memory is exhausted.
NglLightSet* XB_CDECL nglCreateLightSet();
// 0x0006f850: type 0 returns the light's own data; type 1 (directional) writes a
// far-away point light into `out` (position - direction * 1e12, range 1e24) and
// returns it; other types null.
float* XB_CDECL nglLightVectorWorld(float* out, const NglLight* light, const float* position);
// 0x0006f8d0: each clamped to 0..1.
void XB_CDECL nglSetAmbientColor(float r, float g, float b);

// 0x0007b1b0: entries[index] = color; `self` points at the entry array pointer.
void XB_THISCALL nglPaletteSetEntry(uint32_t** self, uint32_t index, uint32_t color);

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
// 0x00066a40: the bounding sphere of the mesh being built, copied to each of its
// sections.
void XB_CDECL nglMeshBuilder_SetBounds(const float* center, float radius);
void XB_CDECL nglSwapRedBlue(uint8_t* pixels, uint32_t count);

}  // namespace game
