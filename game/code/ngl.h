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
                             // 0x400 = render target, 0x2000 = a plain surface,
                             // 0x10000000 = cube map
    uint32_t format;         // +0x08, D3D format
    uint8_t unknown0c[0x4];
    uint32_t width;          // +0x10 (cube maps: the edge length)
    uint32_t height;         // +0x14
    uint32_t levels;         // +0x18
    uint8_t unknown1c[0x2c - 0x1c];
    int32_t last_used_frame; // +0x2c
    uint8_t* d3d_texture;    // +0x30, the D3D texture (its +4 and +0xc feed SwitchTexture)
    uint32_t* surfaces;      // +0x34, the D3D surface per level (cube maps: per face a
                             // list; flag 0x2000: the surface itself)
    uint8_t unknown38[0x3c - 0x38];
    uint8_t* palette;        // +0x3c, an NGL palette (its D3D palette at +0xc) or null
    uint8_t unknown40[0x4c - 0x40];
    NglTexture* target;      // +0x4c, kind 0x10; kind 0x0f (animated): the frame count
    NglTexture** frames;     // +0x50, kind 0x0f
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
    NglLight* object_lights; // +0x20, ends at the set itself (nglEvalFirstObjectLight)
    uint8_t unknown24[0x30 - 0x24];
    NglLight* projectors[8]; // +0x30, shadow projectors (same layout as lights), per
                             // flag bit 24..31; empty lists end at `projectors`
    uint8_t unknown50[0x60 - 0x50];
    float ambient[4];        // +0x60, r g b 1, each 0..1
};

struct alignas(16) NglLight {
    NglLight* next[8];       // per list in NglLightSet::lists
    NglLight* next_object;   // +0x20, NglLightSet::object_lights' link
    uint32_t flags;          // +0x24, bits 24..31: the lists it is on
    uint32_t type;           // +0x28, 0 or 1 (see nglLightVectorWorld)
    float* data;             // +0x2c
};

// A skip list keyed by 32-byte names (nglSkipList_Find); nodes are {key, next[]}.
struct NglSkipListNode {
    const uint32_t* key;          // the object, whose first 32 bytes are its name
    NglSkipListNode* next[1];     // one per level up to the node's height
};

struct NglSkipList {
    uint32_t seed;                // rand() at creation
    uint32_t max_level;           // 7 for the font manager
    NglSkipListNode* head;        // +0x08
    int32_t level;                // +0x0c, highest level in use
};

// A video mode (nglSelectVideoMode builds one and copies it to the current mode).
struct NglVideoMode {
    uint32_t width;
    uint32_t height;
    uint8_t progressive;          // +0x08
    uint8_t widescreen;           // +0x09
    uint8_t unknown0a;
    uint8_t unknown0b;            // always 0
    uint32_t unknown0c[9];        // +0x0c..+0x2c
    uint8_t unknown30;            // always 0
    float unknown34;              // 1.0
    float unknown38;              // 1.0
};

// Only the fields the decompiled functions use so far.
struct NglRenderTarget {
    uint8_t unknown00[0x10];
    uint32_t width;   // +0x10
    uint32_t height;  // +0x14
    uint32_t levels;  // +0x18, mip levels (nglRenderSceneTree makes the rest after drawing)
    uint8_t unknown1c[0x28 - 0x1c];
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

// A draw-list node (meshes, text, custom nodes): a title C++ object.
struct NglNode {
    void** vtable;      // slot 1: (out[2]) -> list (1 = translucent, else opaque), sort key
    NglNode* next;      // +0x04
    uint32_t sort_key;  // +0x08
};

struct NglMeshSection;

// A mesh instance's node.
struct NglMeshNode : NglNode {
    const float* world_matrix;      // +0x0c
    const NglMeshSection* section;  // +0x10
    const uint8_t* material;        // +0x14, the shader classes' AddToList store it
};

// Only the fields the decompiled functions use so far.
struct NglScene {
    NglRenderTargetLink* render_targets; // +0x000
    uint32_t flags;           // +0x004, also the stencil clear value
    uint32_t unknown008;      // +0x008, nglSetRenderTarget stores 6
    uint8_t unknown00c[0x10 - 0xc];
    float base_projection[16]; // +0x010, nglMatrixPerspective/Ortho's (kind at +0x300)
    float viewport[16];       // +0x050, nglMatrixViewport's
    float target_scale[16];   // +0x090, the render target's sample scale (0x00073e90)
    float matrix0d0[16];      // +0x0d0, the camera: the view matrix's inverse (rotation
                              // rows with lane 3 = lane 2, then the position); the last
                              // factor of nglMatrixSceneClipToScreen
    float projection[16];     // +0x110, base projection * viewport * target scale; row
                              // vectors (nglDepthToZValue uses rows 2 and 3)
    float view_matrix[16];    // +0x150
    float view_projection[16]; // +0x190, view * projection
    float clip_to_screen[16]; // +0x1d0, nglMatrixSceneClipToScreen's
    float screen_to_target[16]; // +0x210, nglMatrixScreenToClip's * target scale
    float camera_position[4]; // +0x250, camera row 3
    float camera_forward[4];  // +0x260, camera row 2
    float frustum[6][4];      // +0x270, world-space planes (normal, distance; inside when
                              // dot(p, n) - d >= 0): left, right, two in y, near, far
    NglScene* parent;         // +0x2d0, the scene nglPopScene returns to
    NglScene* next_sibling;   // +0x2d4, the scene tree nglPushScene builds
    NglScene* first_child;    // +0x2d8
    NglScene* last_child;     // +0x2dc
    struct Callback {
        void* function;
        void* arg;
    } callbacks[3];           // +0x2e0, nglSetSceneCallback's (nglRenderSceneTree calls
                              // three: before, between and after its two draw lists)
    NglRenderTarget* render_target; // +0x2f8
    uint32_t render_target_level; // +0x2fc
    uint32_t projection_kind; // +0x300, 1 = perspective
    NglNode* opaque_nodes;    // +0x304, nglListAddNode's lists (newest first)
    NglNode* translucent_nodes; // +0x308
    uint32_t opaque_count;    // +0x30c
    uint32_t translucent_count; // +0x310
    NglLightSet* light_set;   // +0x314
    int32_t viewport_pixels[4]; // +0x318, x0 y0 x1 y1 (nglSetViewportNormalized)
    float viewport_rect[4];   // +0x328, x0 y0 x1 y1 in -1..1 (nglSetViewportRect)
    float viewport_clamped[4]; // +0x338, nglSetViewportNormalized's, clamped to -1..1
    float viewport_ndc[4];    // +0x348, viewport_clamped mapped into viewport_rect's -1..1
    float frustum_scale[2];   // +0x358, perspective: the field of view's x and y extents
    uint32_t clear_flags;     // +0x360
    float clear_depth;        // +0x364
    uint8_t unknown368[0x370 - 0x368];
    float clear_color[4];     // +0x370, r g b a
    uint32_t color_write_mask; // +0x380, 0x01000000 alpha .. 0x00010101 rgb
    uint8_t depth_enable;     // +0x384, depth writes (nglRenderSceneTree's opaque list);
                              // nglBeginSceneRender: Z enable when this or
    uint8_t depth_test;       // +0x385  depth_test is set; depth_test picks the depth function
    uint8_t unknown386[0x388 - 0x386];
    float fog[4];             // +0x388, nglSetFog's arguments (fog[0] = start)
    uint8_t unknown398[0x3a0 - 0x398];
    float fog_color[4];       // +0x3a0, r g b, 1
    uint8_t unknown3b0[0x3b8 - 0x3b0];
    uint8_t stale;            // +0x3b8, matrices need nglUpdateScene
    uint8_t unknown3b9[0x3bc - 0x3b9];
    float aspect;             // +0x3bc
    float fov;                // +0x3c0, 0 for orthographic
    float near_z;             // +0x3c4
    float far_z;              // +0x3c8
    float time;               // +0x3cc, nglBeginSceneRender derives +0x3d0 from it
    float frame_time;         // +0x3d0, seconds: time, or when 0 the frame interval
    uint32_t frame_counter;   // +0x3d4, the default texture animation frame
    uint32_t* slots;          // +0x3d8, 2 + scene slot count (0x00902900) words of
                              // frame memory, copied from the source scene
    uint8_t unknown3dc[0x3e0 - 0x3dc];
};

// A section of a mesh: only its bounding sphere is known.
struct NglMeshSection {
    uint8_t unknown00[0x8];
    uint32_t bone_count;      // +0x08, skinned sections: the bones their vertices use
    const uint16_t* bones;    // +0x0c, indices into the mesh's and instance's bone tables
    float center[4];          // +0x10
    float radius;             // +0x20
    uint8_t unknown24[0x28 - 0x24];
    uint32_t primitive;       // +0x28, D3DPRIMITIVETYPE
    uint32_t index_count;     // +0x2c, 0: not indexed
    uint32_t indices;         // +0x30, Xbox address of the index data
    uint32_t push_buffer;     // +0x34, a recorded push buffer, run instead when +0x38 is set
    uint32_t has_push_buffer; // +0x38
    uint8_t unknown3c[0x40 - 0x3c];
    uint32_t vertex_count;    // +0x40
    uint8_t vertex_buffer[0xc]; // +0x44, an NGL vertex buffer (the D3D buffer at its +8)
    uint32_t stride;          // +0x50
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
static_assert(offsetof(NglMeshSection, primitive) == 0x28);
static_assert(offsetof(NglMeshSection, vertex_count) == 0x40);
static_assert(offsetof(NglMeshSection, stride) == 0x50);
static_assert(sizeof(NglQuadVertex) == 0x14);
static_assert(sizeof(NglQuad) == 0x64);
static_assert(offsetof(NglQuad, texture) == 0x60);
static_assert(offsetof(NglScene, base_projection) == 0x10);
static_assert(offsetof(NglScene, matrix0d0) == 0xd0);
static_assert(offsetof(NglScene, view_matrix) == 0x150);
static_assert(offsetof(NglScene, projection_kind) == 0x300);
static_assert(offsetof(NglScene, viewport_pixels) == 0x318);
static_assert(offsetof(NglScene, frustum) == 0x270);
static_assert(offsetof(NglScene, viewport_ndc) == 0x348);
static_assert(offsetof(NglScene, clear_flags) == 0x360);
static_assert(offsetof(NglScene, last_child) == 0x2dc);
static_assert(offsetof(NglScene, slots) == 0x3d8);
static_assert(sizeof(NglScene) == 0x3e0);
static_assert(offsetof(NglScene, viewport_clamped) == 0x338);
static_assert(offsetof(NglScene, parent) == 0x2d0);
static_assert(offsetof(NglScene, callbacks) == 0x2e0);
static_assert(offsetof(NglScene, render_target) == 0x2f8);
static_assert(offsetof(NglScene, light_set) == 0x314);
static_assert(offsetof(NglScene, viewport_rect) == 0x328);
static_assert(offsetof(NglScene, stale) == 0x3b8);
static_assert(offsetof(NglScene, fov) == 0x3c0);
static_assert(offsetof(NglTexture, target) == 0x4c);
static_assert(sizeof(NglLightSet) == 0x70);
static_assert(sizeof(NglSkipList) == 0x10);
static_assert(sizeof(NglVideoMode) == 0x3c);
static_assert(offsetof(NglVideoMode, unknown30) == 0x30);
static_assert(offsetof(NglLight, data) == 0x2c);
static_assert(offsetof(NglScene, clear_color) == 0x370);
static_assert(offsetof(NglScene, fog) == 0x388);
static_assert(offsetof(NglScene, fog_color) == 0x3a0);
static_assert(offsetof(NglScene, frame_counter) == 0x3d4);
static_assert(offsetof(NglScene, projection) == 0x110);
static_assert(offsetof(NglScene, near_z) == 0x3c4);
static_assert(offsetof(NglScene, depth_enable) == 0x384);
static_assert(offsetof(NglScene, time) == 0x3cc);
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
// 0x00062df0: makes `scene` current, initialised by `mode` (0 cleared + defaults,
// 1 a copy of the current scene, 2 of the root scene, else left as it is) with its
// own slot block (frame memory, copied for 1 and 2); the tree links, render
// targets, callbacks and draw lists are cleared and the parent is the previous
// current scene, which is returned.
NglScene* XB_CDECL nglInitScene(NglScene* scene, int32_t mode);
// 0x00062fc0: a new scene from frame memory, appended to the current scene's
// children (the root when there is none), then nglInitScene; null when the frame
// memory is exhausted.
NglScene* XB_CDECL nglPushScene(int32_t mode);
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
// 0x0005f310: the inverse of the current scene's viewport times the inverse of its
// projection (both affine products: the w columns of the left factor's rows are
// skipped), times its matrix at +0xd0; each output row keeps the left product's w.
void XB_CDECL nglMatrixSceneClipToScreen(float* out);
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
// out: a 0x28-byte light record (10 floats), filled for directional lights.
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
// 0x00066a40: the bounding sphere of the mesh being built, copied to each of its
// sections.
void XB_CDECL nglMeshBuilder_SetBounds(const float* center, float radius);
// 0x0007b220: swaps bytes 0 and 2 of `count` 32-bit pixels.
void XB_CDECL nglSwapRedBlue(uint8_t* pixels, uint32_t count);

// 0x0005e490 / 0x0005e4b0 / 0x0005e550: current scene's depth_enable, depth_test, time.
void XB_CDECL nglSetSceneDepthEnable(uint8_t enable);
void XB_CDECL nglSetSceneDepthTest(uint8_t test);
void XB_CDECL nglSetSceneTime(float time);
// 0x0005e650: called by nglEndFrame before the swap; 0x0005e670: last in nglEndFrame.
void XB_CDECL nglSetPreSwapCallback(void* function, void* arg);
void XB_CDECL nglSetEndFrameCallback(void* function, void* arg);

// Mesh managers (objects with a vtable; the game chains its own in front, like
// nglSetTextureManager). 0x000673c0 / 0x000673d0: the mesh file manager
// (nglLoadMeshFile); 0x000673e0 / 0x000673f0: the mesh manager (nglFindMesh).
void XB_CDECL nglSetMeshFileManager(void* manager);
void* XB_CDECL nglGetMeshFileManager();
void XB_CDECL nglSetMeshManager(void* manager);
void* XB_CDECL nglGetMeshManager();
// 0x00067470: a mesh file's first mesh (null file: null); 0x000674b0: the next mesh.
uint8_t* XB_CDECL nglMeshFileFirstMesh(const uint8_t* file);
uint8_t* XB_CDECL nglMeshNext(const uint8_t* mesh);
// 0x000677b0 / 0x0007a450: a mesh image moved from `old_base` to `new_base`: every
// non-null pointer of the mesh, its tables and its sections moves along.
void XB_CDECL nglRelocateMesh(uint8_t* new_base, uint8_t* old_base, uint8_t* mesh);
void XB_CDECL nglRelocateMeshSection(uint8_t* new_base, uint8_t* old_base, uint8_t* section);

// Shader handles in the shader classes' Register functions.
// 0x000761f0: shader[0] = program, shader[1] = declaration.
void XB_CDECL nglVertexShader_Init(uint32_t* shader, uint32_t declaration, uint32_t program);
// 0x00076210
void XB_CDECL nglPixelShader_Init(uint32_t* shader, uint32_t definition);

// A buffer of 16-bit indices that is just a pointer (`self`). Offsets are halved
// as in the original. 0x0007a3c0: returns 1; the size is unused.
uint8_t XB_THISCALL nglIndexBuffer_Init(uint8_t** self, uint8_t* data, uint32_t size);
// 0x0007a3f0
void XB_THISCALL nglIndexBuffer_Clear(uint8_t** self);
// 0x0007a400
uint8_t* XB_THISCALL nglIndexBuffer_Lock(uint8_t** self, uint32_t offset, uint32_t flags);
// 0x0007a420
void XB_THISCALL nglIndexBuffer_Write(uint8_t** self, const void* source, uint32_t size, uint32_t offset);

// 0x0006ffc0: a point light (position +0x00, colour +0x10, falloff start +0x20 and
// end +0x24) at `position`: out[0..3] = the direction to the point (unit, from rsqrt
// and one Newton step), out[4..7] = colour * linear falloff (clamped to 0..1).
// Returns 0 when the falloff clamped at 0 or the point is at the light (then
// direction 0,1,0,0 and the full colour), else 1.
uint8_t XB_CDECL nglEvalPointLight(float* out, const float* light, const float* position);

// The draw lists' std::sort helpers; elements are {value, key}, opaque lists sort
// by unsigned key ascending, translucent ones by float key descending, then value.
struct NglOpaqueNode {
    uint32_t value;
    uint32_t key;
};
struct NglTranslucentNode {
    uint32_t value;
    float key;
};
// 0x000744f0 / 0x00074600: std::_Push_heap.
void XB_CDECL std_PushHeap_OpaqueNodes(NglOpaqueNode* first, int32_t hole, int32_t top, NglOpaqueNode value);
void XB_CDECL std_PushHeap_TranslucentNodes(NglTranslucentNode* first, int32_t hole, int32_t top,
                                            NglTranslucentNode value);
// 0x00074540: std::_Rotate (random access, gcd cycles) of 8-byte elements.
void XB_CDECL std_Rotate_NodeKeys(NglOpaqueNode* first, NglOpaqueNode* mid, NglOpaqueNode* last);
// 0x00074970 / 0x000749e0: std::_Med3.
void XB_CDECL std_Med3_OpaqueNodes(NglOpaqueNode* first, NglOpaqueNode* mid, NglOpaqueNode* last);
void XB_CDECL std_Med3_TranslucentNodes(NglTranslucentNode* first, NglTranslucentNode* mid,
                                        NglTranslucentNode* last);
// std::_Unguarded_partition's result: the range equal to the pivot.
template <typename Node>
struct NglNodeRange {
    Node* first;
    Node* second;
};
// The rest of std::sort, per node type; `pred` is the (empty) comparison object.
// 0x00074670 / 0x000746f0: std::_Adjust_heap.
void XB_CDECL std_AdjustHeap_OpaqueNodes(NglOpaqueNode* first, int32_t hole, int32_t bottom, NglOpaqueNode value,
                                         uint32_t pred);
void XB_CDECL std_AdjustHeap_TranslucentNodes(NglTranslucentNode* first, int32_t hole, int32_t bottom,
                                              NglTranslucentNode value, uint32_t pred);
// 0x00074790 / 0x000747e0: std::_Make_heap (two unused tag arguments follow).
void XB_CDECL std_MakeHeap_OpaqueNodes(NglOpaqueNode* first, NglOpaqueNode* last, uint32_t pred);
void XB_CDECL std_MakeHeap_TranslucentNodes(NglTranslucentNode* first, NglTranslucentNode* last, uint32_t pred);
// 0x00074a90 / 0x00074af0: std::sort_heap.
void XB_CDECL std_SortHeap_OpaqueNodes(NglOpaqueNode* first, NglOpaqueNode* last, uint32_t pred);
void XB_CDECL std_SortHeap_TranslucentNodes(NglTranslucentNode* first, NglTranslucentNode* last, uint32_t pred);
// 0x00074830 / 0x000748b0: std::_Insertion_sort (moves with std::_Rotate).
void XB_CDECL std_InsertionSort_OpaqueNodes(NglOpaqueNode* first, NglOpaqueNode* last, uint32_t pred);
void XB_CDECL std_InsertionSort_TranslucentNodes(NglTranslucentNode* first, NglTranslucentNode* last, uint32_t pred);
// 0x00074b50 / 0x00074bf0: std::_Median (ninther above 40 elements).
void XB_CDECL std_Median_OpaqueNodes(NglOpaqueNode* first, NglOpaqueNode* mid, NglOpaqueNode* last, uint32_t pred);
void XB_CDECL std_Median_TranslucentNodes(NglTranslucentNode* first, NglTranslucentNode* mid, NglTranslucentNode* last,
                                          uint32_t pred);
// 0x00074c90 / 0x00074e50: std::_Unguarded_partition (result by hidden pointer).
NglNodeRange<NglOpaqueNode>* XB_CDECL std_UnguardedPartition_OpaqueNodes(NglNodeRange<NglOpaqueNode>* result,
                                                                         NglOpaqueNode* first, NglOpaqueNode* last,
                                                                         uint32_t pred);
NglNodeRange<NglTranslucentNode>* XB_CDECL std_UnguardedPartition_TranslucentNodes(
    NglNodeRange<NglTranslucentNode>* result, NglTranslucentNode* first, NglTranslucentNode* last, uint32_t pred);
// 0x00075280 / 0x00075370: std::_Sort (quicksort to 32 elements, heap sort when
// `ideal` runs out, then insertion sort).
void XB_CDECL nglSortOpaqueNodes_r(NglOpaqueNode* first, NglOpaqueNode* last, int32_t ideal, uint32_t pred);
void XB_CDECL nglSortTranslucentNodes_r(NglTranslucentNode* first, NglTranslucentNode* last, int32_t ideal,
                                        uint32_t pred);


// 0x00064080: the object whose 32-byte name equals `key` (compared as 8 unsigned
// words, the first different word deciding), null if none.
const uint32_t* XB_THISCALL nglSkipList_Find(const NglSkipList* self, const uint32_t* key);
// 0x00067510: the mesh of a mesh file (list at +0x13c, next at +0x0c) whose 32-byte
// name equals `key`, null if none.
uint8_t* XB_CDECL nglMeshFileFindMesh(const uint32_t* key, const uint8_t* file);
// 0x00065380: returns self.
NglVideoMode* XB_THISCALL nglVideoMode_Init(NglVideoMode* self, uint32_t width, uint32_t height,
                                            uint8_t progressive, uint8_t widescreen,
                                            uint8_t unknown0a, uint32_t u0, uint32_t u1,
                                            uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5,
                                            uint32_t u6, uint32_t u7, uint32_t u8);
// 0x00065420: field-wise copy (padding stays); returns self.
NglVideoMode* XB_THISCALL nglVideoMode_Copy(NglVideoMode* self, const NglVideoMode* other);
// 0x0006bad0: clears the frame statistics nglBeginFrame/nglEndFrame and the fps
// counter keep (0x00902b60..0x00902c90).
void XB_CDECL nglResetFrameStats();
// 0x0006a900: writes one byte of an 8-bit swizzled texture (pixels at +0x20): x and
// y are spread over the current swizzle masks (0x00902b44 / 0x00902b48); the two
// offsets are left in 0x00902b50 / 0x00902b54 as in the original.
void XB_CDECL nglTextureSetPixel(const uint8_t* texture, uint32_t x, uint32_t y, uint8_t value);
// 0x0006f6a0: a shadow projector on the current light set's projector lists named by
// flags bits 24..31; null when the frame's scene memory is exhausted.
NglLight* XB_CDECL nglLightSetAddProjector(uint32_t type, float* data, uint32_t flags);
// 0x00073c20: 4 floats for the target: the multisample scale (0x00903c70, set by
// nglInitMultisample) for the screen (flags & 4), else a constant 1, 1, 0.53125, 0
// (a function-local static in title memory). Returns out.
float* XB_CDECL nglGetTargetSampleScale(float* out, const NglRenderTarget* target);

// 0x0006ad00: a texture stage's address mode from texture flags, through a cache of
// the last mode per stage (`self` + 0x20): 2 -> U/V mirror, W wrap; 4 -> all mirror;
// 8 -> U/V clamp, W mirror, plus the border colour (set every time); else all wrap.
// Writes D3D's texture stage state (0x003f5c74 + stage * 0x80) and dirty mask.
void XB_THISCALL nglSetTextureAddressMode(uint8_t* self, uint32_t stage, uint8_t flags, uint32_t border);
// 0x00076360: points the shader constant table (0x00903c9c..0x00903d08) at the
// title's constant blocks.
void XB_CDECL nglInitShaderConstantTable();

// 0x00068560: the LOD for a mesh at `position` (16-byte aligned): its view depth
// (current scene's view matrix, column z), times `params` +0x20 when `flags` bit 7 is
// set, is compared from the last entry down with the table's {mesh, min depth}
// entries (count +0x18, array +0x1c); the first one it is beyond gives its mesh.
// None (or NaN): returns `table` itself, as the original leaves EAX. EAX = table.
uint8_t* XB_REGS1 XB_CDECL nglSelectMeshLod(uint8_t* table, int8_t flags, const uint8_t* params,
                                            const float* position);
// 0x00068680: `matrix` with rows 0..2 scaled by the instance scale (`instance` + 0x10),
// in a static matrix (0x00902af0) that is returned; *max_scale = the largest of the
// scale's |x|, |y|, |z| (MAXPS order).
float* XB_CDECL nglScaleMeshMatrix(const float* matrix, const uint8_t* instance, float* max_scale);

// A mesh instance in the frame's scene memory (0xa0 bytes, 64-byte aligned), made by
// nglListAddMesh and handed to each section's shader.
struct NglMeshDrawNode {
    float world[16];                 // +0x00, the instance matrix (scaled rows when flag 2)
    float world_view_projection[16]; // +0x40, world * the scene's view_projection, transposed
    float* world_inverse;            // +0x80, nglNode_GetWorldInverse's cache (scene memory)
    uint32_t unknown84;              // +0x84, zeroed
    uint8_t* mesh;                   // +0x88, after the LOD pick
    uint32_t extra;                  // +0x8c, *extra or the address 0x009028f8
    const uint32_t* params;          // +0x90, the instance block (0x30 bytes; copied unless
                                     // flag bit 31) or the default at 0x00902a90
    float scale;                     // +0x94, nglScaleMeshMatrix's max scale, else 1
    uint8_t unknown98[0xa0 - 0x98];
};
#if UINTPTR_MAX == 0xFFFFFFFF
static_assert(sizeof(NglMeshDrawNode) == 0xa0);
static_assert(offsetof(NglMeshDrawNode, params) == 0x90);
#endif

// 0x00068850: adds a mesh instance to the current scene: params (flags first; 2 =
// scaled, 0x40 = no frustum cull, bit 31 = keep the caller's block) may be null. Makes
// an NglMeshDrawNode, picks the LOD (mesh +0x18), culls the bounding sphere (+0x20,
// radius +0x30; culled: the scene memory is given back, returns 0) and calls each
// section's shader (section +4 = material, material +4 = shader; vtable slot 2,
// thiscall (node, section, material)), adding to the frame statistics. Null mesh: -1.
int32_t XB_CDECL nglListAddMesh(uint8_t* mesh, const float* matrix, const uint32_t* params, const uint32_t* extra);
// 0x00049750: the inverse of the node's world matrix (nglNode_CalcWorldMatrix, a rigid
// one: rotation transposed with lane 3 repeating lane 2, then -t * R^T), made once per
// frame into scene memory (+0x80) and copied to out. Returns out.
float* XB_THISCALL nglNode_GetWorldInverse(NglMeshDrawNode* self, float* out);
// 0x00070200: up to four object lights of the current light set for the node, at its
// bounding sphere's centre: directions[i] = -light direction in object space (lane 3
// 0), colors = r[4], g[4], b[4] (one lane per light), then the set's ambient; unused
// slots zero.
void XB_CDECL nglComputeObjectLights(NglMeshDrawNode* node, float* directions, float* colors);
// 0x0006fe00: makes the instance's light set (its parameter slot 0x00902914, else the
// scene's) current and rebuilds its object-light list from the mesh's category list
// (mesh +7, minus one; 0 = none): directional lights (type 1) always, point lights
// (type 0) whose radius (data +0x24) plus the mesh's (+0x30) reaches the mesh centre
// (+0x20, in world space); each one goes in front.
void XB_CDECL nglSelectMeshLightSet(NglMeshDrawNode* node);

// 0x00070b00: six planes (n, d as 4 floats; 0x60 bytes) of the box spanned by
// points[0..2] (edge vectors) at points[3] (a corner): normals cross(p0, p1),
// cross(p1, p2), cross(p2, p0) through the corner, each with its opposite plane
// through the matching edge; nglAddShadowProjector's projector box.
void XB_CDECL nglBuildProjectorPlanes(float* out, const float* points);

// The current scene's camera and viewport setters; each ends in nglUpdateScene(1).
// 0x0005fc90: rebuilds the current scene's matrices and world-space frustum planes
// when it is stale; with `mark_stale` only marks it stale (what every setter does).
void XB_CDECL nglUpdateScene(uint8_t mark_stale);
// 0x00061940: the viewport rectangle in -1..1 (x0 y0 x1 y1).
void XB_CDECL nglSetSceneViewportRect(float x0, float y0, float x1, float y1);
// 0x00061b70: the viewport rectangle in pixels of the scene's render target (the
// screen's 640x480 for flags & 4), inclusive x1/y1, then nglSetViewportNormalized
// (0x000619a0) with the -1..1 values.
void XB_CDECL nglSetViewportRect(float x0, float y0, float x1, float y1);
// 0x000619a0: the viewport rectangle in -1..1, kept clamped and as rounded pixels of
// the render target (the screen's size for flags & 4).
void XB_CDECL nglSetViewportNormalized(float x0, float y0, float x1, float y1);
// 0x00061cc0
void XB_CDECL nglSetAspect(float aspect);
// 0x00061ce0
void XB_CDECL nglSetPerspective(float fov, float near_z, float far_z);
// 0x00061d40
void XB_CDECL nglSetOrthographic(float near_z, float far_z);
// 0x00061d90
void XB_CDECL nglSetViewMatrix(const float* matrix);
// 0x00061dc0: the view matrix as the inverse of a camera matrix (rotation rows 0..2,
// position in row 3; 16-byte aligned).
void XB_CDECL nglSetCameraMatrix(const float* camera);
// 0x00062250: the scene's matrix at +0xd0, updated first when stale.
const float* XB_CDECL nglGetViewMatrix();

// 0x0005e270: a copy of `quad` on the current scene's draw lists (frame scene memory,
// 0x70 bytes: vtable 0x007bc410, next, sort key, the quad): blend modes 0 and 1 go on
// the opaque list (+0x304, count +0x30c) keyed by texture, the others on the
// translucent list (+0x308, count +0x310) keyed by z. Updates a stale scene first.
void XB_CDECL nglListAddQuad(const NglQuad* quad);
// 0x00061f30: one of the current scene's matrices into `out` (returned): 0 the one
// at +0xd0 and 2 the view matrix, as 4x3 with the fourth column 0 0 0 1; 1 the
// projection (+0x110), 3 the one at +0x190; others the identity. Updates a stale
// scene first.
float* XB_CDECL nglGetMatrix(float* out, int32_t which);
// 0x00062310: `point` (16-byte aligned) through the view, projection and viewport
// matrices into `out` (returned): screen x y, view-space z, and w's own screen x.
float* XB_CDECL nglProjectPoint(float* out, const float* point);
// 0x00062d80: nglGetMatrix (0x00061f30) through a local copy.
void XB_CDECL nglGetMatrixCopy(float* out, int32_t which);
// 0x00062db0: nglProjectPoint (0x00062310) through a local copy.
void XB_CDECL nglProjectPointCopy(float* out, const float* point);
// 0x0006f960: a directional light (direction, colour r g b with 1) on the current
// light set; nothing when the frame's scene memory is exhausted.
// 0x00029200: a sphere against the current scene's six frustum planes (+0x270,
// inward normals and distance); false as soon as it lies behind one.
bool XB_CDECL nglSphereInFrustum(const float* center, float radius);
// 0x0006f9f0: a point light (position, colour r g b with 1, inner and outer radius)
// on the current light set when its sphere is in the frustum.
void XB_CDECL nglAddPointLight(uint32_t flags, const float* position, float inner, float radius,
                               const float* color);
void XB_CDECL nglAddDirectionalLight(uint32_t flags, const float* direction, const float* color);
// 0x00070120: a light's contribution at `position`: type 0 (point) evaluated into
// `out`, type 1 its own data, else null.
const float* XB_CDECL nglEvalLight(float* out, const NglLight* light, const float* position);
// 0x00070160: nglEvalLight for the current light set's first object light; with none,
// `out` = {0, 1, 0, 0, 0, 0, 0, 0}.
const float* XB_CDECL nglEvalFirstObjectLight(float* out, const float* position);

// Resource managers (title objects; vtable slot 2 finds, 7 loads, 8 releases).
// 0x00063530 / 0x00067450: find by name, null if not loaded.
void* XB_CDECL nglFindTexture(const char* name);
void* XB_CDECL nglFindMesh(const char* name);
// 0x00063570 / 0x000658c0: find and add a reference (texture +0, font +0x20), or load.
void* XB_CDECL nglFindTextureAddRef(const char* name);
void* XB_CDECL nglFindFontAddRef(const char* name);
// 0x00063630 / 0x00067720 / 0x000659f0
void XB_CDECL nglReleaseTexture(void* texture);
// 0x000456a0 / 0x000456c0: nglFindTextureAddRef for a material's texture name, null
// for an empty one (first word 0); OrDefault also gives null for the default texture
// (0x009026f8).
void* XB_CDECL nglMaterialFindTexture(const char* name);
void* XB_CDECL nglMaterialFindTextureOrDefault(const char* name);
// The shaders' material load/release slots (vtable slots 3 and 4): textures named at
// +0x18 / +0x20 into +0x1c / +0x24.
// 0x00047670: +0x1c = nglFindTextureAddRef(+0x18) (no empty-name check).
void XB_STDCALL nglMaterial_LoadTextureRef(uint8_t* material);
// 0x000485e0: releases +0x1c and clears it.
void XB_STDCALL nglMaterial_ReleaseTexture(uint8_t* material);
// 0x00049130: releases +0x1c and +0x24 (left set).
void XB_STDCALL nglMaterial_ReleaseTwoTextures(uint8_t* material);
// 0x0004c6e0: +0x1c, and +0x24 unless it is only the default texture.
void XB_STDCALL nglMaterial_LoadBaseAndOptional(uint8_t* material);
// 0x0004c720: releases +0x1c, and +0x24 if set.
void XB_STDCALL nglMaterial_ReleaseBaseAndOptional(uint8_t* material);
// 0x0004e710: +0x1c only.
void XB_STDCALL nglMaterial_LoadTexture(uint8_t* material);
// 0x00051510: +0x1c and +0x24.
void XB_STDCALL nglMaterial_LoadTwoTextures(uint8_t* material);
// 0x00051540: releases and clears +0x1c and +0x24.
void XB_STDCALL nglMaterial_ReleaseTwoTexturesClear(uint8_t* material);
// 0x00047590: the decal depth bias through a render-state cache (self +0x80 / +0x84):
// polygon offset bias -bias and factor bias * -0.25, each sent (SetRenderState_Simple)
// only when it changed (NaN always).
void XB_THISCALL nglSetDecalZBias(uint8_t* self, uint32_t bias);

// NGL's render-state cache (0x00902f08; the shader classes' Draw slots also test its
// fields inline): each setter sends its NV2A state with SetRenderState_Simple only when
// it changed.
// 0x00047dc0 +0x50 alpha test enable (0x300), 0x00047df0 +0x54 alpha function (0x33c),
// 0x00047e20 +0x58 alpha reference (0x340), 0x00047e50 +0x5c blend enable (0x304),
// 0x00048fd0 +0x60 source blend (0x344), 0x00049000 +0x64 destination blend (0x348),
// 0x00049030 +0x70 blend equation (0x350), 0x00076050 +0x6c blend colour (0x34c; also
// D3D's render state at 0x003f8454).
void XB_THISCALL nglStateCache_SetAlphaTestEnable(uint8_t* self, uint8_t enable);
void XB_THISCALL nglStateCache_SetAlphaFunc(uint8_t* self, uint32_t func);
void XB_THISCALL nglStateCache_SetAlphaRef(uint8_t* self, uint32_t ref);
void XB_THISCALL nglStateCache_SetBlendEnable(uint8_t* self, uint8_t enable);
void XB_THISCALL nglStateCache_SetSrcBlend(uint8_t* self, uint32_t factor);
void XB_THISCALL nglStateCache_SetDestBlend(uint8_t* self, uint32_t factor);
void XB_THISCALL nglStateCache_SetBlendOp(uint8_t* self, uint32_t op);
void XB_THISCALL nglStateCache_SetBlendColor(uint8_t* self, uint32_t color);
// 0x00046d50: fog on or off (+0x8b): D3D's D3DRS_FOGENABLE (0x003f8498) and its dirty
// bit (0x003f8320 bit 13), no NV2A command of its own.
void XB_THISCALL nglStateCache_SetFogEnable(uint8_t* self, uint8_t enable);
// 0x00075ac0: blend mode 0..8 (+0xe8) with its alpha (+0xec, modes 5..7: the blend
// colour's alpha) and alpha reference (+0xf0, mode 1: alpha test only); unchanged
// modes return at once except mode 1. 0 opaque, 1 alpha test, 2 alpha blend, 3
// additive, 4 reverse subtract, 5..7 constant-alpha variants, 8 modulate.
void XB_THISCALL nglSetBlendMode(uint8_t* self, uint32_t mode, uint32_t alpha, uint32_t alpha_ref);

// A shader program reference: the vertex or pixel program, then (vertex) its input
// format (null: the cached one stays).
struct NglShaderRef {
    uint32_t program;
    uint32_t format;
};
// 0x000760a0: SetPixelShaderProgram when it is not the cached one (0x00903c10).
void XB_CDECL nglSetPixelShaderProgramCached(const NglShaderRef* shader);
// 0x000760c0: LoadVertexShaderProgram + SelectVertexShaderDirect unless program and
// format are the cached ones (0x00903c08 / 0x00903c0c); a cached format is not sent again.
void XB_CDECL nglSetVertexShaderCached(const NglShaderRef* shader);
// 0x0007b1d0: SetPalette for a stage when the NGL palette changed (cache 0x00903d48).
void XB_CDECL nglSetPaletteCached(uint32_t stage, uint8_t* palette);
// 0x0006a6c0: no palettes and textures on stages 0..3; the caches reset.
void XB_CDECL nglUnbindTextures();
// 0x0006a760: a texture on a stage: an animated texture's current frame, a reference's
// target; marks it used this frame, SetTexture (or SwitchTexture when only the data
// changed within the same format) when the stage's texture changed, its palette, then
// the address mode (nglSetTextureAddressMode).
void XB_CDECL nglSetTexture(uint32_t stage, NglTexture* texture, uint8_t address_flags, uint32_t border);
// 0x00078790: vertex stream 0 (SetVertexShaderInputDirect with the cached format) when
// buffer, stride or offset (start * stride) changed.
void XB_CDECL nglSetVertexStreams(const uint8_t* vertex_buffer, uint32_t stride, uint32_t start);
// 0x00078890: draws a mesh section: indexed, as a list, or its push buffer.
void XB_CDECL nglDrawMeshSection(const NglMeshSection* section);
// 0x000776f0: two vertex constants at slot + 0x60: (fog[2], fog[0], 1 / (fog[1] -
// fog[0]), fog[3] - fog[2]) and the camera position in the node's object space, w 1.
void XB_CDECL nglSetTransformConstants(int32_t slot, NglMeshDrawNode* node, float fog0, float fog1, float fog2,
                                       float fog3);
void XB_CDECL nglReleaseMeshFile(void* file);
void XB_CDECL nglReleaseFont(void* font);

// 0x0004dfb0 / 0x0004d0b0: a loaded material's texture offsets (+0x18, and +0x20)
// made pointers by adding the file's base; zero stays zero.
void XB_STDCALL nglMaterial_Relocate(uint8_t* material, uint32_t base);
void XB_STDCALL nglMaterial_Relocate2(uint8_t* material, uint32_t base);

// 0x00052730: nglMaterial_LoadBaseAndOptional plus a third texture: +0x2c from the name
// at +0x28 (null for the default texture).
void XB_STDCALL cdWorldBlend_LoadTextures(uint8_t* material);
// 0x00052780: releases +0x1c, +0x24 and (when set) +0x2c.
void XB_STDCALL cdWorldBlend_ReleaseTextures(uint8_t* material);
// 0x000490f0: nglMaterial_LoadTwoTextures; a second texture other than the default one
// sets material flag 2 (+0x30).
void XB_STDCALL cdGlass_LoadTextures(uint8_t* material);

// 0x00046cd0: onto the current scene's opaque or translucent list, by the node's
// own sort key (vtable slot 1).
void XB_CDECL nglListAddNode(NglNode* node);
// 0x00046df0: the view-space depth of the section's centre, plus its radius.
float XB_THISCALL nglNode_ViewDepth(const NglMeshNode* node);
// 0x000495d0: the node's matrix (+0); when its instance (+0x90) has flag 2, rows 0..2
// divided by the instance's scale (+0x10, RCPPS + one Newton step). Returns out.
float* XB_THISCALL nglNode_CalcWorldMatrix(const uint8_t* self, float* out);

// 0x00073ce0: the render target's pixel mapping: x, y scaled by size * scale / 2 and
// offset by scale[2] (+ that half size), z by 2^24 - 1. Returns out.
float* XB_CDECL nglMatrixTargetScale(float* out, const NglRenderTarget* target, const float* scale);
// 0x00073e90: nglMatrixTargetScale for the current scene's target and its sample scale.
float* XB_CDECL nglGetSceneTargetScale(float* out);

// 0x000704f0: a box shadow projector (0x140 bytes of scene memory) for `matrix` (rows
// 0..2 the box axes, row 3 its centre) and `scale` (its size per axis): +0x00 world to
// projector space (0..1 over the box), +0x40 the box (edges, corner), +0x80 the three
// words given (the third only when nonzero), +0x90 scale, +0xa0 centre, +0xb0 the
// matrix, +0xe0 the box planes (nglBuildProjectorPlanes); added with
// nglLightSetAddProjector(2, ..., flags).
void XB_CDECL nglAddShadowProjector(uint32_t flags, const float* matrix, const float* scale, uint32_t param_a,
                                    uint32_t param_b, uint32_t param_c);

// 0x000765c0: nglComputeObjectLights' directions and colours as 8 vertex shader
// constants from slot + 0x60.
void XB_CDECL nglSetObjectLightConstants(int32_t slot, NglMeshDrawNode* node);
// 0x00076620: the skinning matrices of a section (3 transposed rows each, at 0x00903000,
// sent from slot + 0x60) and constant 0 = (765.003, slot, 0x00903c70, 0x00903c74). Per
// bone index b: instance flag 4 = mesh bone b (mesh +0x14) * instance bone b (params[2])
// * the node's world inverse, 8 = mesh bone * instance bone, 0x10 = the instance bone,
// else identity.
void XB_CDECL nglSetSkinningConstants(int32_t slot, NglMeshDrawNode* node, const NglMeshSection* section);
// 0x00045640: the shaders' clock (0x00901e90) advanced by `seconds` (x87) and made the
// scene time; `color` (4 floats) kept at 0x00901ea0.
void XB_CDECL nglAdvanceShaderTime(const float* color, float seconds);
// 0x000456f0: the current scene's fog colour (+0x3a0), kept at 0x00901eb0 and copied
// to out.
void XB_CDECL nglGetSceneFogColor(float* out);
// 0x0001b930: `size` bytes of the frame's scene memory (0x009025a8) at an `align`
// (power of two) boundary.
void* XB_CDECL nglAllocSceneMemory(uint32_t size, uint32_t align);
// 0x00046ca0: onto the current scene's translucent list, whatever its sort key.
void XB_CDECL nglListAddTranslucent(NglNode* node);
// 0x000661c0: a text node (0x2c bytes of scene memory, vtable 0x007bd0cc, drawn by
// nglRenderTextNode): a copy of the text, the font, position x/y (+0x14/+0x18), depth
// (+0x1c, also the sort key), colour word (+0x28) and the two scales (+0x20/+0x24),
// onto the translucent list. Nothing for empty text or a font without glyphs (+0x24).
void XB_CDECL nglListAddText(const uint8_t* font, const char* text, float x, float y, float z, uint32_t color,
                             float scale_x, float scale_y);
// 0x00062a10: a custom draw node (0x20 bytes of scene memory, vtable 0x007bc6a0: +0xc
// = 1, +0x10 and +0x14 the two words, +0x18 the two words at `data`) onto the current
// scene's lists, after bringing its matrices up to date.
void XB_CDECL nglListAddCustomNode(uint32_t a, uint32_t b, const uint32_t* data);
// 0x00075460 / 0x000754f0: a draw list sorted (std::sort of {node, key} pairs in scene
// memory) and relinked in that order, then each node drawn (vtable slot 0) with
// 0x009025b4 = the last one drawn.
void XB_CDECL nglRenderOpaqueList(NglNode* nodes, uint32_t count, uint32_t pred);
void XB_CDECL nglRenderTranslucentList(NglNode* nodes, uint32_t count, uint32_t pred);
// 0x00074330: draws the current scene after its child scenes (each made current in
// turn): render targets, matrices, nglBeginSceneRender, the three callbacks around
// the opaque list (depth writes as depth_enable) and the translucent list (depth
// writes off), then the render target's mips. The frame allocator (0x00903c34) is
// given back afterwards.
void XB_CDECL nglRenderSceneTree();

// 0x000666f0: the manager's skip list (+4) looked up by name (tail jump, not hookable).
const uint32_t* XB_THISCALL nglResourceManager_Find(const uint8_t* manager, const uint32_t* name);
// 0x00066940: locks the D3D buffer (+8) no-overwrite, returns the pointer at offset.
uint8_t* XB_THISCALL nglLockVertexBuffer(const uint8_t* buffer, uint32_t offset, uint32_t unused);
// 0x0007b180: locks the D3D palette (+0xc), the entries pointer to +0.
void XB_THISCALL nglLockPalette(uint8_t* palette);
// 0x000939f0: GlobalMemoryStatus' available physical memory.
uint32_t XB_CDECL nglGetFreeMemory();
// 0x0006aab0: the front-buffer texture (0x00902cd8) with a fresh GetBackBuffer2(-1) surface.
NglTexture* XB_CDECL nglGetFrontBufferTexture();
// 0x0006aae0: the back-buffer texture (0x00902d50) with a fresh GetBackBuffer2(0) surface.
NglTexture* XB_CDECL nglGetBackBufferTexture();
// 0x00064660: clears the vertex input and texture/palette bindings; frame counter += 2.
void XB_CDECL nglResetDeviceBindings();
// 0x000669e0: {memory, size, D3D buffer}: waits for the GPU, frees both, clears all three.
void XB_THISCALL nglVertexBuffer_Destroy(uint32_t* buffer);
// 0x0006a820: LockRect of level 0 into +0x1c; swizzled textures (flag 0x100) set up the swizzler.
void XB_CDECL nglTexture_Lock(NglTexture* texture, uint32_t flags);
// 0x000669a0: frees the D3D object, optionally re-protects the memory (kept).
void XB_THISCALL nglVertexBuffer_Free(uint32_t* buffer, uint32_t protect);

// 0x00093b90: frees a loaded file's buffer {memory} through the installed hooks or by kind.
void XB_CDECL nglFreeFileBuffer(uint32_t* buffer);
// 0x0007b130: waits for the GPU, unbinds, frees the entries (unless borrowed), the D3D palette and the record.
void XB_CDECL nglFreePalette(uint32_t* palette);
// 0x0006a560: attaches pixel memory to the texture's D3D resource.
void XB_THISCALL nglRegisterTexture(NglTexture* texture, uint32_t data);
// 0x0006ab70: frees/detaches the pixel memory (+0x24).
void XB_THISCALL nglReleaseTextureMemory(NglTexture* texture);
// 0x00078810: SetRenderTarget with the last colour/depth pair cached (SetRenderTargetFast when unchanged).
void XB_CDECL nglSetRenderTargetCached(NglTexture* target, uint32_t level, NglTexture* depth, uint32_t face);
// 0x0006a9a0: a locked 32-bit texture's pixel.
uint32_t XB_CDECL nglTextureGetPixel(const NglTexture* texture, uint32_t x, uint32_t y);

// 0x00072ba0: points the depth-view texture (0x00902e88) at the depth buffer as linear 32-bit pixels.
void XB_CDECL nglInitDepthTextureView();
// 0x00066490: nglListAddText with printf formatting (1 KB buffer).
void XB_CDECL nglListAddTextf(const uint8_t* font, float x, float y, float z, uint32_t color, float scale_x,
                              float scale_y, const char* format, ...);
// 0x0009d240: draws the batched quads and advances the batch.
void XB_CDECL nglQuadBatch_Draw();
// 0x0009d5b0: a particle quad batch's texture and render states (blend 0-3).
void XB_CDECL nglQuadBatch_SetMaterial(NglTexture* texture, uint32_t blend, uint8_t fog, uint32_t alpha_ref);

// 0x00062a70: TODO.
void XB_CDECL nglSetDefaultSceneParams();
// 0x00075d90: TODO.
void XB_THISCALL nglRestoreRenderStates(uint8_t* self);
// 0x00076470: TODO.
void XB_CDECL nglInitShaderConstants(uint8_t init_table);
// 0x0006abe0: TODO.
void XB_THISCALL nglReleaseTextureSurfaces(NglTexture* texture);
// 0x00063870: TODO.
void XB_CDECL nglGenerateRenderTargetMips(NglTexture* texture);
// 0x00075780: TODO.
void XB_THISCALL nglSetDefaultRenderStates(uint8_t* self);
// A run of glyphs with one position, scale and colour (frame memory, nglBuildTextGlyphs).
struct NglTextRun {
    NglTextRun* next;
    const char* text;
    uint32_t length;      // characters
    uint32_t glyph_count;
    float x, y;
    float scale_x, scale_y;
    uint32_t color;       // ARGB
};
// 0x00065b60: splits text into runs (the list at *runs, their count in *run_count);
// returns the glyph count.
uint32_t XB_CDECL nglBuildTextGlyphs(const uint8_t* font, NglTextRun** runs, float x, float y, float scale_x,
                                     float scale_y, uint32_t color, const char* text, uint32_t* run_count);

// Index buffers need no unlock on the Xbox: an empty function.
void XB_CDECL nglIndexBuffer_Unlock();
// The shader classes' empty vtable slot (one argument).
void XB_STDCALL nglShader_Nop(uint32_t unused);
// A vtable slot that returns 1.
uint32_t XB_CDECL WaitTilOutputInst1_str_VFunc2();
// Frees the two fog textures (0x00902e64/68) once made (0x00902e6c).
void XB_CDECL nglFreeFogTextures();
// Frees the current scene's render targets (and a depth texture at +0x38 of targets
// with flags 0x5000).
void XB_CDECL nglFreeSceneRenderTargets();
// Sets every field of the render state cache to its default, then sends them all to
// D3D (nglSetDefaultRenderStates).
void XB_THISCALL nglInitRenderStates(uint8_t* self);
// The size in pixels of a text with control codes (nglBuildTextGlyphs'): the widest
// line, and the lines' heights (font height x the line's largest y scale) summed.
void XB_CDECL nglGetTextExtent(const uint8_t* font, const char* text, int32_t* width, int32_t* height,
                               float scale_x, float scale_y);
// Before a scene's draw lists: its frame time, depth and stencil states, render
// target, viewport and clear.
void XB_CDECL nglBeginSceneRender(NglScene* scene);

// printf-style nglGetTextExtent at scale 1.
void XB_CDECL nglGetTextExtentf(const uint8_t* font, int32_t* width, int32_t* height, const char* format, ...);
// printf-style nglGetTextExtent.
void XB_CDECL nglGetTextSizef(const uint8_t* font, int32_t* width, int32_t* height, float scale_x, float scale_y,
                              const char* format, ...);
// An NGL vertex buffer {data, size, D3D vertex buffer}: `size` bytes of write-combined
// memory. 0 when an allocation fails. The second argument is unused.
uint8_t XB_THISCALL nglCreateVertexBuffer(uint32_t* self, uint32_t size, uint32_t unused);
// The same over memory the caller owns; `protect` != 0 is applied with XPhysicalProtect.
uint8_t XB_THISCALL nglCreateVertexBufferPhysical(uint32_t* self, uint32_t data, uint32_t size, uint32_t protect);
// Allocates the texture's D3D header (surface, cube or texture by its flags) and fills it.
void XB_THISCALL nglInitTextureHeader(NglTexture* self);
// Reads the level count and the surface of each level (each face's for cube maps).
void XB_THISCALL nglTextureGetSurfaces(NglTexture* self);
// Pixel memory for every level (and face) of the texture; render targets take it from
// the per-frame scratch memory.
void* XB_THISCALL nglAllocTextureMemory(const NglTexture* self);
// An NGL palette {entries, entry count, borrowed flag (byte), D3D palette} over `entries`
// (or new memory when null). The first argument is unused.
uint32_t* XB_CDECL nglCreatePalette(uint32_t unused, uint32_t count, uint32_t entries);
// Starts building a mesh (0x00902920) with `section_count` empty sections and a copy of
// the 0x40-byte material records; flag 0x40000 allocates from the frame's GPU memory.
void XB_CDECL nglMeshBuilder_Begin(uint32_t flags, uint32_t section_count, uint32_t material_count,
                                   const void* materials);
// Fills the builder's next section (0x00902924 of the mesh at 0x00902920; last one at
// 0x00902930): material (which gets the section at +4), primitive type, a vertex buffer
// of `vertex_count` x `stride` (GPU-frame meshes: a slice of the shared buffer at
// 0x00902970, offset 0x0090295c aligned to the stride) and a copy of the 16-bit indices.
// Returns the new section count.
uint32_t XB_CDECL nglMeshBuilder_AddSection(uint32_t* material, uint32_t primitive_type, uint32_t vertex_count,
                                            uint32_t unused, uint32_t index_count, const uint16_t* indices,
                                            uint32_t stride);
// No-op: vertex memory needs no unlocking on the Xbox.
void XB_THISCALL nglVertexBuffer_Unlock(uint32_t* self);
// The D3D vertex buffer (+8) of an NGL vertex buffer.
uint32_t XB_THISCALL nglVertexBuffer_GetD3D(const uint32_t* self);
// Double-buffered dynamic vertex memory: two NGL vertex buffers (+0, +0xc) with their
// capacities (+0x18), per-frame counters +0x20..+0x2c, the buffer index +0x30, the
// current capacity +0x34 and a copy of the current buffer, locked, at +0x38.
void XB_THISCALL nglDynamicVB_BeginFrame(uint32_t* self);
// Clears the counters, flips to the other buffer and unlocks the current one.
void XB_THISCALL nglDynamicVB_EndFrame(uint32_t* self);
// Room for `quads` more quads this frame (limit 0x00909f34)? Yes: sets the pending
// count and returns the write pointer. No: flags the overflow (0x00909f44), returns null.
uint8_t* XB_CDECL nglQuadBatch_Reserve(int32_t quads);
// *self = `size` bytes of index memory; false when that fails.
uint8_t XB_THISCALL nglIndexBuffer_Alloc(uint8_t** self, uint32_t size);
// *out = an NGL palette over `entries` with the DDS header's entry count (word +0xc).
uint32_t* XB_CDECL nglCreateDDSPalette(const uint8_t* header, uint32_t** out, uint32_t entries);
// Finds `key` from *cursor on, moves the cursor past it and parses the number after it
// (strtol, the cursor ends behind it). 0 when the key is not found.
int32_t XB_REGS2 XB_CDECL nglFontParseKeyInt(const char* key, char** cursor, int32_t base);
// No-op: texture memory needs no unlocking on the Xbox.
void XB_CDECL nglTexture_Unlock();
// No-op called from nglBeginFrame.
void XB_CDECL nglBeginFrame_Nop();
// Pixel memory and surfaces for the two fog textures (0x00902e64/68) that have none;
// sets 0x00902e6c.
void XB_CDECL nglAllocFogTextures();
// Pixel memory and surfaces for the current scene's render targets (and their depth
// textures, flags 0x1000 | 0x4000).
void XB_CDECL nglAllocSceneRenderTargets();
// Finishes the builder's mesh: unless it lives in frame GPU memory, each section's
// indices (+0x30, count +0x2c) move into an index buffer of their own, rebased to the
// first section's first index. Returns the mesh.
uint32_t* XB_CDECL nglMeshBuilder_End();

struct NglHashedName;

// One glyph of an NGL font (.fdf): the cell in the atlas and its placement.
struct NglFontGlyph {
    int32_t x;         // +0x00
    int32_t y;         // +0x04
    int32_t width;     // +0x08, gw
    int32_t height;    // +0x0c, gh
    int32_t offset_x;  // +0x10, gx
    int32_t offset_y;  // +0x14, gy
    int32_t advance;   // +0x18, w
};
static_assert(sizeof(NglFontGlyph) == 0x1c);

struct NglFont {
    uint8_t name[0x20];     // an NglHashedName
    uint32_t refs;          // +0x20
    NglTexture* texture;    // +0x24, the atlas (same name)
    NglFontGlyph* glyphs;   // +0x28, indexed by code - first_glyph
    int32_t version;        // +0x2c
    int32_t cell_height;    // +0x30
    int32_t ascent;         // +0x34
    int32_t first_glyph;    // +0x38
    int32_t glyph_count;    // +0x3c
    uint32_t unknown40;     // +0x40, 2 when loaded
    uint32_t unknown44;     // +0x44, 2 when loaded
    uint8_t unknown48[0x4];
    float (*uvs)[4];        // +0x4c, per glyph in file order: u0 v0 u1 v1
    uint8_t unknown50[0x4];
};
static_assert(sizeof(NglFont) == 0x54);

// 0x00065590 (font in EDI): the .fdf text's header keys (version in hex), then per glyph
// "<code> x y w gx gy gh gw"; texture coordinates from the atlas size. Returns the count.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL nglParseFontFile(NglFont* font, char* text);
extern "C" void nglParseFontFile_entry(void);
// 0x000657b0: a new font (refs 1) over the texture of the same name, parsed from `text`.
NglFont* XB_CDECL nglLoadFontFile(const NglHashedName* name, char* text);
// 0x000658f0: the font manager's font (one more reference), else a loaded and added
// one; the system font when loading fails.
NglFont* XB_CDECL nglLoadFont(const NglHashedName* name, char* text);
// 0x00079020: a DDSP palette file (texture +0x128, version 1): per palette (count word
// +8, stride word +0xa) an NGL palette into the array +0x13c; the entries (64-byte
// aligned after the 16-byte header) are swapped to BGR once (byte +0xf).
uint8_t XB_CDECL nglParseDDSPalette(uint32_t unused, uint8_t* texture);
// 0x000728d0: when a fog value changed (only the first changed one is taken over, cache
// 0x008a30fc), refills the fog table texture 0x00902e70: per texel the depth along the
// near..far range, density fog[2] before fog[0], fog[3] past fog[1], linear between,
// stored as 255 - density * 255.
void XB_CDECL nglUpdateFogTable();

}  // namespace game
