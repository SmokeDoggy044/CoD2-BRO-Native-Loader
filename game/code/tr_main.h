#pragma once

// The Quake 3-shaped side of the renderer (tr_*.c in Q3's source): the RE_* exports
// and the view setup around NGL. Names after their Q3 counterparts where the body
// matches (docs/naming.md, Renderer).

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "dobj.h"

namespace game {

// A doubly linked list with its head and tail nodes folded into one header (the
// layout of AmigaOS's MinList): head points at the `tail` field when empty,
// tail_prev at `head`.
struct RFrameList {
    uint32_t count;
    void* head;
    void* tail;  // always null
    void* tail_prev;
};

#if UINTPTR_MAX == 0xFFFFFFFF  // the title's layout (32-bit pointers)
static_assert(sizeof(RFrameList) == 16);
#endif

// 0x002352c0: out = a * b (4x4, row vectors); EAX = a, EDX = out, ECX = b.
void XB_REGS3 myGLMultMatrix(const float* a, float* out, const float* b);

// 0x00235ad0: fills the renderer's glconfig_t with fixed values (no GL here).
void XB_CDECL GLimp_Init();
// 0x00235cc0: string length without ^0..^9 colour codes; EDX = string. Unlike Q3
// the character after a code is skipped uncounted too (and a code at the very end
// reads one byte past the terminator), as in the original.
int32_t XB_REGS2 Q_PrintStrlen(uint32_t eax_unused, const char* string);
// 0x00236230: points the world at the collision map's.
void XB_CDECL RE_LoadWorldMap();
// 0x002387e0: appends the view model's entity to the second frame list (when the
// view model is drawn).
void XB_CDECL R_AddViewModelRefEntity(uint8_t* entity);

// 0x00234cc0: the next scene starts after what the last one added (three scene
// lists; Q3: r_firstSceneDlight/Entity/Poly).
void XB_CDECL RE_ClearScene();
// 0x00234cf0: empties the two frame lists and resets every scene list count.
void XB_CDECL R_ClearFrameLists();
// 0x00235dc0: negative or zero = no far cull distance.
void XB_CDECL RE_SetFarCullDist(float distance);
// 0x00235e90: the larger of r_zfar and the far cull distance.
float XB_CDECL RE_GetFarCullDist();

// 0x00235e30: 1 when one of `count` planes (normal, dist; 20 bytes each) has every
// point of the portal's winding (points at +0x18, 16 bytes each, count at +0x1c) on
// or behind it, else 0. EAX = portal, EDX = planes, ECX = count.
int32_t XB_REGS3 R_CullPortal(const uint8_t* portal, const float* planes, int32_t count);
// 0x00236fb0: the view's projection matrix (Q3's R_SetupProjection, infinite far
// plane; tan via the x87 FPTAN) and its far distance (2048 with the flag at 0x0095e600
// bit 0, else r_zfar, 0 = 524288).
void XB_CDECL R_SetupProjection();

// Q3's cplane_t, with byte offsets into a {mins, maxs} box instead of signbits.
struct RCullPlane {
    float normal[3];
    float dist;
    uint8_t box_offset[3];  // per axis: 4 * axis, + 12 (maxs) when the normal's bits
                            // as an integer are > 0 (so -0.0 and +0.0 pick mins)
    uint8_t unknown13;
};
static_assert(sizeof(RCullPlane) == 20);
// 0x00237940: the view's four side planes (0x0095e518) into the cull planes
// (0x0095e24c), each pushed out by 0.005.
void XB_CDECL R_CopyFrustumPlanes();
// 0x0023bb70: Q3's ChopWinding-style clip of `*count` points (16 bytes apart, xyz
// used) to the front of `plane` (EAX; normal, dist), with a 0.005 epsilon. Returns
// `in` when nothing is behind, null (and *count = 0) when nothing is in front, else
// `out` with *count set (at most 32 points, the check is after clip points only).
float* XB_REGS1 XB_CDECL R_ClipPolygonToPlane(const float* plane, float* in, int32_t* count, float* out);

// 0x00238ae0: marks the entities linked into a cell (links at +0x38: {entity, mins[3],
// maxs[3], next}) that are not yet added this frame (entity +0xec) and whose box is
// on the front side of all `count` portal planes (20 bytes each: normal, dist, three
// byte offsets into mins/maxs picking the box's furthest corner) and of the near
// plane (0x0095e2c4) and the optional far plane (0x0095e2c8): clears +0xec and stores
// the cell's index at +0xf0. NaN distances count as in front, as in the original.
void XB_CDECL R_AddCellEntities(const uint8_t* cell, const uint8_t* planes, int32_t count);

// 0x002369c0: the world fog (when 0x008db100 is set) into the current NGL scene: the
// fog distances (0x0095f698, 0x0095f69c and 0x008db104/0x008db108, the last halved)
// and colour (0x008db10c).
void XB_CDECL R_SetupFog();
// 0x00234d90: two full-screen quads that write only alpha: 0, then 0x80 where the
// depth test passes at 0.9 * zfar; then 0x00045ef0's custom scene. Off when bit 7 of
// the shader disable mask (0x00901e8c) is set.
void XB_CDECL R_ClearAlpha();
// 0x002361a0: the 2D colour (0x0095e6d0) as ARGB from 0..1 floats; null = white.
void XB_CDECL RE_SetColor(const float* rgba);
// 0x002360f0: a 2D quad x, y, w, h with texture coordinates s0 t0 s1 t1 in the 2D
// colour, when a scene is current.
void XB_CDECL RE_StretchPic(float x, float y, float w, float h, float s0, float t0, float s1, float t1,
                            void* shader);
// 0x00240590: RE_StretchPic turned by `angle` degrees about the rectangle's centre
// (FastSinCos of angle * pi / 180).
void XB_CDECL RE_StretchPicRotate(float x, float y, float w, float h, float s0, float t0, float s1, float t1,
                                  float angle, void* shader);
// 0x0023ee60: Q3's RE_AddPolyToScene for quads only (vertex_count 4): a one-section strip
// mesh in frame GPU memory with a PCUV material (frame arena) for `shader`, added with
// the identity matrix.
void XB_CDECL RE_AddPolyToScene(void* shader, int32_t vertex_count, const uint8_t* verts);
// 0x00235ff0: a 2D quad from four corners (x, y; clockwise from top left) and their
// texture coordinates, in the 2D colour, when a scene is current.
void XB_CDECL RE_DrawQuadPic(const float* points, const float* coords, void* shader);
// 0x00237590: RE_StretchPic of the whole texture after the client's 640x480 adjust
// (function pointer 0x0095e6b0).
void XB_CDECL R_DrawPic(float x, float y, float w, float h, void* shader);
// 0x00237770: the height of "LltygI" in `font` at `scale` (times the global text
// scale 0x008db12c); the font's handle comes from the pointer at 0x0095e6b4.
uint32_t XB_CDECL RE_TextHeight(uint32_t font, float scale);
// 0x002372e0: Q3's RE_Shutdown: logs, removes the renderer's console commands (ri
// 0x0095e630 Printf, 0x0095e660 Cmd_RemoveCommand), frees the texture at 0x0095f6c8 and
// clears 0x0095e390.
void XB_CDECL RE_Shutdown(int32_t destroy_window);

// 0x00235890: Q3's R_CullPointAndRadius against the view's four side planes
// (0x0095e518): 0 inside, 1 crossing a plane, 2 outside; always 1 with r_nocull
// (0x0095e6c8). ECX = point, XMM2 = radius: entered through R_CullPointAndRadius_entry.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL R_CullPointAndRadius(const float* point, float radius);
extern "C" void R_CullPointAndRadius_entry(void);

// 0x0023b230: one coloured quad x0, y0 - x1, y1 at depth z; ESI = colour (four 0..1
// floats): entered through R_DrawFilledRect_entry.
extern "C" PLATFORM_HIDDEN void XB_CDECL R_DrawFilledRect(const float* colour, float x0, float y0, float x1, float y1,
                                                          float z);
extern "C" void R_DrawFilledRect_entry(void);

// 0x0023b2e0: the instance colour slot (0x00902908) of an instance parameter block (a
// 64-bit slot mask, then a word per slot): a copy of `colour` in scene memory (null
// when the frame allocator's next block would be at 0). ESI = where the block's
// pointer is, ECX = colour: entered through R_SetInstanceColor_entry.
extern "C" PLATFORM_HIDDEN void XB_CDECL R_SetInstanceColor(uint32_t* const* params, const float* colour);
extern "C" void R_SetInstanceColor_entry(void);

// The lighting an entity or static model keeps between frames: ambient, then up to
// three directional lights.
struct RCachedLighting {
    float ambient[3];
    float colors[3][4];
    float directions[3][3];
    int32_t count;
};
static_assert(sizeof(RCachedLighting) == 0x64);
// 0x0023c380: the cached lighting into the current NGL light set (ambient and each
// directional light). ESI = lighting: entered through R_ApplyCachedLighting_entry.
extern "C" PLATFORM_HIDDEN void XB_CDECL R_ApplyCachedLighting(const RCachedLighting* lighting);
extern "C" void R_ApplyCachedLighting_entry(void);

// 0x00238820: links a render entity (the record starts 0x80 bytes before the
// refEntity, which ESI points at) into the first frame list with its cell, when the
// renderer is registered; reType outside 0..15 is an ERR_DROP. Entered through
// R_AddRefEntityToScene_entry.
extern "C" PLATFORM_HIDDEN void XB_CDECL R_AddRefEntityToScene(uint8_t* ref_entity, int32_t cell);
extern "C" void R_AddRefEntityToScene_entry(void);

// A per-frame entity link of a cell (the cell's list at +0x38).
struct RCellEntityLink {
    void* entity;
    float mins[3];
    float maxs[3];
    RCellEntityLink* next;
};
#if UINTPTR_MAX == 0xFFFFFFFF
static_assert(sizeof(RCellEntityLink) == 0x20);
#endif
// 0x0023ba90: links `entity` with its bounds into a cell, or grows the bounds of its
// link there; at most 256 links a frame (0x0095e2d0, count 0x0095e2d4). EDI = cell,
// ECX = mins, EDX = maxs: entered through R_AddCellEntityLink_entry.
extern "C" PLATFORM_HIDDEN void XB_CDECL R_AddCellEntityLink(uint8_t* cell, const float* mins, const float* maxs,
                                                             void* entity);
extern "C" void R_AddCellEntityLink_entry(void);

// Q3's orientationr_t.
struct ROrientation {
    float origin[3];
    float axis[3][3];
    float view_origin[3];
    float model_matrix[16];
};
static_assert(sizeof(ROrientation) == 0x7c);
// 0x00237120: Q3's R_RotateForEntity (without the axis length): the entity's origin
// (+0xbc) and axis (+0x94), its model matrix times the world's, and the viewer's
// origin in its frame. EAX = entity, ESI = orientation, EDI = view parameters
// (viewer orientation, world orientation at +0x7c): entered through R_RotateForEntity_entry.
extern "C" PLATFORM_HIDDEN void XB_CDECL R_RotateForEntity(const uint8_t* entity, ROrientation* orientation,
                                                           const uint8_t* view_parms);
extern "C" void R_RotateForEntity_entry(void);
// 0x00238880: reType (+0x80) 0..2 get R_RotateForEntity, the rest the world's
// orientation. EAX = entity, EDX = orientation, ECX = view parameters.
void XB_REGS3 R_SetupEntityOrientation(const uint8_t* entity, ROrientation* orientation, const uint8_t* view_parms);

// 0x002375e0: lays out up to 255 characters of icon text (16-bit: high byte a kind,
// low byte the character) into the shared buffer 0x0098b5d8 (*out): colour kinds
// change with ^N codes, kinds 10-12 set the red/green/blue of `colour` (optional) to
// value / 255, kinds 13 and 16-18 are icons, where it stops. Trailing spaces are cut;
// *rest = where to continue (null at the end), *count -= characters used; returns 1
// when an icon stopped it. EAX = out, EBX = colour: entered through R_LayoutTextWithIcons_entry.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL R_LayoutTextWithIcons(char** out, float* colour, const uint16_t* text,
                                                                  int32_t* count, const uint16_t** rest);
extern "C" void R_LayoutTextWithIcons_entry(void);
// 0x0023fab0: the icon codes at the start of icon text (kind = high byte): 13-15 set the
// red/green/blue of `colour` (optional) to value / 255, 16/17 scale *width / *height (both
// first scale * 48) by value / 32, and a run of kind 18 is a shader name, registered into
// *shader (first 0); *consumed += 2 per code, *count -= the name's end index. XMM0 = scale,
// EBX = text, EDX = consumed: entered through R_ParseTextIcon_entry.
extern "C" PLATFORM_HIDDEN void XB_CDECL R_ParseTextIcon(float scale, const uint16_t* text, int32_t* consumed,
                                                         int32_t* count, float* width, float* height, uint32_t* shader,
                                                         float* colour);
extern "C" void R_ParseTextIcon_entry(void);

// A static model placed in a cell.
struct RStaticModel {
    RCachedLighting lighting;
    int32_t lighting_state;  // +0x64: 0 = not looked up yet, 1 = none (or default), 2 = cached
    XModel* xmodel;          // +0x68
    float axis[3][3];        // +0x6c
    float origin[3];         // +0x90
    float scale;             // +0x9c
};
#if UINTPTR_MAX == 0xFFFFFFFF
static_assert(sizeof(RStaticModel) == 0xa0);
#endif
// 0x002438b0: adds a static model's meshes (one per bone of its first LOD, at most 32
// bones) to the scene: bone matrices (0x00241ff0) times the model's, its lighting
// (looked up in the light grid once - at the origin, then raised twice; red ambient
// when there is none - then the cached copy), a scale parameter block when the scale
// is not 1, and an instance block with the light set. EAX = model.
void XB_REGS1 XB_CDECL R_AddStaticModel(RStaticModel* model);

// The world's light grid (one per level): areas of samples (one word per point: 5-bit
// r, g, b, then a count of light references in bits 15..16 and their first index from
// bit 17) and the lights they refer to.
struct RGridLight {
    float origin[3];      // the position, or the direction of a directional light
    float point;          // +0x0c, 0 = directional
    float color[3];       // +0x10
};
struct RLightGridArea {
    float origin[3];
    float cell_size[2];   // +0x0c
    uint16_t width;       // +0x14
    uint16_t height;      // +0x16
    int32_t first;        // +0x18, its first sample
    int32_t cell;         // +0x1c
};
struct RLightGrid {
    RGridLight* lights;
    uint32_t unknown04;
    const uint32_t* samples;    // +0x08
    uint32_t unknown0c;
    const uint16_t* references; // +0x10, pairs {light, weight 0..65535}
    uint32_t unknown14;
    RLightGridArea* areas;      // +0x18
    int32_t area_count;         // +0x1c
};
#if UINTPTR_MAX == 0xFFFFFFFF
static_assert(sizeof(RGridLight) == 0x1c);
static_assert(sizeof(RLightGridArea) == 0x20);
static_assert(sizeof(RLightGrid) == 0x20);
#endif
// 0x00242e90: the lighting at a point (16 bytes, 16-byte aligned) of the cell's light
// grid area: the four samples around it blended (bilinear) as the ambient, then their
// light references merged (weights blended the same way, x87 + _ftol) and up to the
// three strongest added as directional lights (a point light's direction from the
// point, normalized with RSQRTSS and a Newton step), each colour times its weight /
// 65535 also added to the ambient (times 0x008db118). Without an area for the cell:
// blue ambient. Everything goes into `out` (optional) as well. EAX = cell.
void XB_REGS1 XB_STDCALL R_SampleLightGrid(int32_t cell, const RLightGrid* grid, const float* point,
                                           RCachedLighting* out);

// An entity's lighting cache (the refEntity's +8).
struct REntityLighting {
    int32_t valid;
    float timer;          // +0x04, seconds until the next look-up (counted down by 0x008ddaa0)
    float origin[3];      // +0x08, where it was looked up
    RCachedLighting lighting; // +0x14
};
static_assert(sizeof(REntityLighting) == 0x78);
// 0x00243770: an entity's lighting: the cached copy while its timer runs or it has not
// moved, else the light grid at origin (cached again for 0.14..0.22 s, picked with
// the title's LCG at 0x008d5a70), else the default lighting. EAX = cache, EDI = origin
// (16 bytes): entered through R_SetupEntityLighting_entry.
extern "C" PLATFORM_HIDDEN void XB_CDECL R_SetupEntityLighting(REntityLighting* cache, const float* origin);
extern "C" void R_SetupEntityLighting_entry(void);

// 0x0023cc80: the cell containing a point (16-byte aligned, 4 lanes: the w lane is
// multiplied too), down the world's cell tree (0x0095f6b8 +0xc); -1 outside. EDX = point.
int32_t XB_REGS2 CM_CellForPoint(uint32_t unused, const float* point);
// 0x0023cd30: the cell of the view origin (0x0095e240); ERR_DROP without a world.
int32_t XB_CDECL R_CellForViewOrigin();
// 0x0023cda0: a fixed directional light (function statics: direction (0, a, a) and a
// colour, times 0x0083ef60) and the colour times 0x008db118 as ambient.
void XB_CDECL R_SetDefaultLighting();
// 0x0023e510: the light grid of the cell at position + 0x0083eb94 up (then once more
// higher), or null (no cell, or the cell has none; blue ambient when its grid is not
// loaded). *cell_out (optional) = the cell. EAX = position, ECX = cell_out.
void* XB_REGS3 R_GetCellLighting(const float* position, uint32_t unused, int32_t* cell_out);
// 0x00236d30: removes every cell model link whose model's +0xe0 is `owner`.
void XB_CDECL R_UnlinkEntityFromCells(const void* owner);
// 0x00236dc0: links a model into a cell of the owner's world (+0x100) unless it is
// already the cell's first link. EAX = owner, ECX = cell, EDI = model: entered through
// R_LinkEntityToCell_entry.
extern "C" PLATFORM_HIDDEN void XB_CDECL R_LinkEntityToCell(const uint8_t* owner, uint32_t cell, uint8_t* model);
extern "C" void R_LinkEntityToCell_entry(void);
// 0x0023d690: links a model into every cell its box touches, from a node of the cell tree.
void XB_CDECL R_LinkEntityToCells_r(const uint8_t* owner, const uint8_t* node, uint8_t* model, const float* mins,
                                    const float* maxs);
// 0x0023d8f0: links an entity's bounds (16-byte aligned) into every cell they touch,
// from a node of the world's cell tree (R_AddCellEntityLink).
void XB_CDECL R_AddEntityToCells_r(const uint8_t* node, void* entity, const float* mins, const float* maxs);
// 0x0023ffb0: clears the cells' entity counts (+0x38) and links each model entity of
// the list at 0x008db128 (with the cvar at 0x0095e234 set) into the cells its box
// touches - or its light's cell when within the light's range - unless the frustum
// `planes`, the near or the far plane cull it (+0xec = 2).
void XB_CDECL R_AddEntitiesToCells(const RCullPlane* planes, int32_t count);
// 0x00243fd0: the cell's linked static models not added this frame (stamp +0xe4, a
// float). EAX = cell.
void XB_REGS1 XB_CDECL R_AddCellModels(const uint8_t* cell);
// 0x0023fee0: once a frame, the cell's world meshes (+0x44, count +0x40) and, with the
// cvar at 0x0095e36c set, its entities. ESI = cell: entered through R_AddCellSurfaces_entry.
extern "C" PLATFORM_HIDDEN void XB_CDECL R_AddCellSurfaces(uint8_t* cell, const uint8_t* planes, int32_t count);
extern "C" void R_AddCellSurfaces_entry(void);
// A clip plane of the portal walk: normal, dist, then the byte offsets (0/4/8, +0xc for
// a positive component) of the box corner furthest along the normal.
struct RPlane {
    float normal[3];
    float dist;
    uint8_t corner[3];
    uint8_t pad;
};
static_assert(sizeof(RPlane) == 20);
// 0x00238c50: the planes from the view origin through the edges of a winding of `n`
// points (16 bytes apart), Z_Malloc'd, pushed 0.005 back; *count = how many. With
// r_portalbevels (0x0095e6e0, default 0.7) above 0 also the edges of its projected bounds
// (bevels), unless within that much of an edge plane. EAX = n.
RPlane* XB_REGS1 XB_CDECL R_ClipPortalWinding(int32_t n, int32_t* count, const float* points);
// 0x0023cb30: the frustum (planes, Z_Malloc'd) through a portal's winding clipped to
// `clip_plane`, the near plane (0x0095e2c8) and the current frustum; null with *count =
// 0 when nothing is left, a copy of the frustum when the view is within 1 of the portal.
// EAX = count, EDX = portal, ECX = planes.
RPlane* XB_REGS3 R_ClipFrustumToPortal(int32_t* count, const uint8_t* portal, const RPlane* planes,
                                       const float* clip_plane, int32_t plane_count);
// 0x00245930: adds a cell, then recurses through each portal (0x24 bytes: plane,
// target cell +0x14, visiting flag +0x20) facing the viewer and inside the frustum,
// with the frustum clipped to it (R_ClipFrustumToPortal, 0x0023cb30).
void XB_CDECL R_VisitCell_r(uint8_t* cell, const uint8_t* from_portal, const uint8_t* planes, int32_t count,
                            uint32_t pass);

// 0x0023f130: Q3's R_SetupFrustum: the view's four side planes (0x0095e518) from its
// axes and field of view (FastSinCos of fov / 180 * pi / 2 on the x87).
void XB_CDECL R_SetupFrustum();
// 0x0023f380: Q3's R_RotateForViewer: tr.or and viewParms.world from the view's origin
// and axes (the axes adjusted by 0x002c9270 first).
void XB_CDECL R_RotateForViewer();
// 0x00294610: the inverse of a 4x4 matrix (cofactors over the determinant). EAX = m,
// ECX = dst.
void XB_REGS3 MatrixInverse4x4(const float* m, uint32_t unused, float* dst);
// 0x002377d0: the world's model matrix times the projection, transposed into 0x0095e2d8
// (R_ClipPortalWinding projects with it) and inverted into 0x0095e318.
void XB_CDECL R_SetupPortalBevels();
// 0x002388c0: (unless r_lockpvs, 0x008ff018 overriding it once) the cull planes: the
// frustum sides, the view origin (0x0095e240), a near plane 0.1 ahead along the view
// axis, a far plane (0x0095e2c8, null for 0) at max(r_zfar, the far cull distance), then
// R_SetupPortalBevels with r_portalbevels above 0.
void XB_CDECL R_SetupCullPlanes();
// 0x00246fa0: the world's surfaces and models for the frame (skipped with 0x0095e600
// bit 0): the portal walk from the view's cell (just that cell with r_singlecell), or
// every cell outside the map. The cell entity link buffer (0x2000 bytes) is on its stack:
// entered through R_AddWorldSurfaces_entry.
extern "C" PLATFORM_HIDDEN void XB_CDECL R_AddWorldSurfaces(void* link_buffer);
extern "C" void R_AddWorldSurfaces_entry(void);

// The instance blocks R_AddDObjSurfaces hands nglListAddMesh, at +0xf0 and +0x130 of
// its frame. nglListAddMesh copies all 0x30 bytes, and the original leaves some of them
// unset (whatever earlier calls left on the stack), so they stay at those addresses.
struct RDObjRigidParams {
    uint32_t flags;      // 0x80, | 2 = scaled, | 0x80 again for the shadow copy
    uint32_t unknown04;  // 0
    uint32_t unknown08;  // 0
    uint32_t unset0c;
    float scale[4];      // +0x10, written only when scaled
    float distance;      // +0x20, 0x008da6d8's value; 100000 for the shadow copy
    uint32_t unset24[3];
};
struct RDObjSkinnedParams {
    uint32_t flags;         // 8
    uint32_t skin;          // +0x04, the mesh's +0x10
    const uint8_t* bones;   // +0x08, the model's first bone matrix
    uint32_t unset0c[9];
};
#if UINTPTR_MAX == 0xFFFFFFFF
static_assert(sizeof(RDObjRigidParams) == 0x30);
static_assert(sizeof(RDObjSkinnedParams) == 0x30);
#endif

// 0x00244020: adds the meshes of a DObj's models (current LODs; the first model alone
// is only sphere-culled from LOD 5 on) to the current NGL scene, lit by the entity's
// cached lighting, the cell's light grid or the default, plus the scene's lights of
// category 6. Rigid meshes take their bone's matrix times `matrix`; with limit_depth
// they are pulled in and shrunk to end at the far distance 0x0095e514. With an entity:
// its animation time (+0x248/+0xa0) as an extra matrix, its scale. alpha (0xff =
// none) goes into the slot 0x00902908. cast_shadow (with r_testshadow) adds each drawn
// mesh again, coarsest LOD, to the shadow scene 0x0095f6cc. Returns whether anything
// was drawn. Entered through R_AddDObjSurfaces_entry, which rebuilds the original's
// frame (the instance blocks above live in it).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL R_AddDObjSurfaces(DObj* obj, uint8_t* entity, const float* matrix,
                                                              uint8_t limit_depth, uint8_t alpha, uint8_t cast_shadow,
                                                              uint8_t* frame);
extern "C" void R_AddDObjSurfaces_entry(void);
// 0x00245a20: a model entity (EAX; the record starts 0x80 before its refEntity): its
// DObj (+0xd4) with the axis (+0x94) and origin (+0xbc) unless its owner (+0xd8) is
// flagged 0x200000; bit 0 of +0xed = drawn. With r_testshadow and 0x0095f6b4, a red
// 32-pixel quad at its projected origin in the shadow scene. Entered through
// R_AddEntityModel_entry (the original's frame, R_AddDObjSurfaces at its position).
extern "C" PLATFORM_HIDDEN void XB_CDECL R_AddEntityModel(uint8_t* entity, uint8_t* frame);
extern "C" void R_AddEntityModel_entry(void);

// 0x00246d60: with r_drawentities, the model entities of the list at 0x008db128
// (unless too far away horizontally while 0x0095f550 is set), then the live animated
// objects of the handle table at 0x00960070 (R_AddDObjSurfaces).
void XB_CDECL R_AddEntitySurfaces();
// 0x0023bd90: once the renderer is registered (0x0095e390): the frame count
// (0x0095e398) up, nglBeginFrame, the shader clock advanced by the frame time with the
// colour at 0x0095e41c (r g b r), colour and depth clears with the fog colour, and the
// full-screen viewport.
void XB_CDECL RE_BeginFrame();
// 0x00247350: Q3's R_RenderView: the view parameters (EBX, 0x1c8 bytes) into
// tr.viewParms, orientation and frustum, then the NGL scenes: the world view (sky mesh,
// effects, local/packet entities, world surfaces, entity surfaces), then the view
// model scene. Entered through R_RenderView_entry, which rebuilds the original's frame:
// every call runs at the original's stack position (title_call_at).
// 0x00296350 (stdcall, out buffer in EBX): the text before the first "{name}" whose
// name Key_TranslateButtonName (0x00292ee0) knows into `out`, *icon = that button's
// name and *next past the '}' (returns 1); otherwise out empty, *next = text, *icon
// null (returns 0).
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL RE_ParseButtonToken(char* out, uint32_t table, const char* text,
                                                                const char** next, const char** icon);
extern "C" void RE_ParseButtonToken_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL R_RenderView(const uint8_t* parms, uint8_t* frame);
extern "C" void R_RenderView_entry(void);

// 0x002476b0: Q3's RE_RenderScene: the refdef into tr.refdef (0x0095e5e4: rect, fov,
// time +0x54, rdflags +0x58), the dynamic light count, then the view parameters (fov,
// LOD scale <= 4 and bias <= 0 scaled by tan(fov/2) / tan(c), zfar, origin and axis) for
// R_RenderView. Entered through RE_RenderScene_entry (refdef on the stack, cdecl).
extern "C" PLATFORM_HIDDEN void XB_CDECL RE_RenderScene(uint32_t entry, const uint8_t* refdef);
extern "C" void RE_RenderScene_entry(void);

// 0x002374a0 (integral flag in EAX, cvar in ESI): warns and resets a cvar that is not
// integral (to its integer) or outside [minimum, maximum] (to the bound).
extern "C" PLATFORM_HIDDEN void XB_CDECL R_CheckCvarRange(uint32_t integral, const uint8_t* cvar, float minimum,
                                                          float maximum);
extern "C" void R_CheckCvarRange_entry(void);
// 0x00243fb0: R_RegisterModel(name, 0x0095f6ac, handle, flags); returns `handle`.
uint32_t XB_CDECL RE_RegisterModel(uint32_t handle, const char* name, uint32_t flags);
// 0x0023f570: text at (x, y - its height) in the font found by name at size
// 0x008db12c * scale, coloured rgba or "^1".."^9" (a trailing "^7" dropped); "{name}"
// tokens become button icons (R_DrawPic, as high as a space) or the button's name.
// The last five arguments are not read.
void XB_CDECL RE_DrawTextExt(float x, float y, uint32_t font_name, float scale, const float* rgba, const char* text,
                             int32_t, uint32_t, uint32_t, uint32_t, uint32_t);
// 0x0030f2b0: the index of the named font in the 7-entry font table (0x0095f7e8),
// compared without case after the first '.' and what follows when strip_extension;
// 8 when absent. EAX = name.
int32_t XB_REGS1 XB_STDCALL R_FindFont(const char* name, uint32_t strip_extension);
// 0x0023fc30: RE_DrawTextExt with the arguments 6 and 7 = -1, 0 inserted after the sixth.
void XB_CDECL RE_DrawText(float x, float y, uint32_t font_name, float scale, const float* rgba, const char* text,
                          uint32_t a6, uint32_t a7, uint32_t a8);
// 0x0023fc70: the width of `text` in the font at `scale` (times 0x008db12c); with a
// "{...}" in it, the runs between button tokens plus each icon's shader width scaled
// to the last run's height (or the token's name as text without a shader).
int32_t XB_CDECL RE_TextWidth(const char* text, uint32_t font_name, float scale);
// 0x00241a30: the width of icon text (count 16-bit characters): each laid-out run by
// RE_TextWidth, or by its printable length * char_width when that is not 0, plus each
// icon's width (R_ParseTextIcon at size `scale`); truncated to an integer.
// 0x00241860: icon text (count 16-bit characters) at x, y: each laid-out run with
// RE_DrawTextExt in the running colour (starting as rgba), each icon (R_ParseTextIcon) as a
// picture centred on the line, through the 640x480 adjust (0x0095e6b0); arg7 and arg9
// go to RE_DrawTextExt's ninth and eleventh arguments.
void XB_CDECL RE_DrawTextWithIcons(float x, float y, uint32_t font_name, float scale, const float* rgba,
                                   const uint16_t* text, uint32_t arg7, int32_t count, uint32_t arg9);
int32_t XB_CDECL RE_TextWidthWithIcons(const uint16_t* text, uint32_t font_name, float scale, float char_width,
                                       int32_t count);
// 0x0023ec30 (name in EDX): the texture for a shader name: the name up to its first '.',
// after its last '@'/'$' and last '/', hashed and looked up with Stream_FindTexture -
// first in the "FrontEnd" zone's bank (when the zone table 0x0095f638 +0x18 has that
// zone and its bank +0x28 is not -1; otherwise the default bank +0x2c, only if the zone
// exists), then in the bank at +0x34; the white texture (0x00902700) when neither has it.
uint8_t* XB_REGS2 XB_CDECL RE_RegisterShader(uint32_t unused, const char* name);
// 0x00240750: a cell's (0x0095f6b8 +0x1c, 0x50 bytes each) meshes. `release`: the
// mesh file (+0x3c) and every mesh slot (+0x44, count +0x40) cleared. Otherwise the
// mesh file "<map>_<cell:03d>" (map name 0x00988c58) or up to 32 split files
// "<map>_<cell:03d>_SPLIT<n>" (+0x3c = 1 then) through Stream_FindMeshFile, and every
// mesh of them into the slots in order.
void XB_CDECL R_LoadCellMeshes(uint32_t cell, uint32_t release);

// 0x00045ef0: the end of R_ClearAlpha: a scene (no clear, no depth test or write) with
// one custom node that calls R_DrawFogPass (0x00045850) with {1, 0.0}.
void XB_CDECL R_AddFogPass();

}  // namespace game
