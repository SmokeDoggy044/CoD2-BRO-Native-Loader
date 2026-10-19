#pragma once

// The Quake 3-shaped side of the renderer (tr_*.c in Q3's source): the RE_* exports
// and the view setup around NGL. Names after their Q3 counterparts where the body
// matches (docs/naming.md, Renderer).

#include <cstddef>
#include <cstdint>

#include "compiler.h"

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
// 0x00235ff0: a 2D quad from four corners (x, y; clockwise from top left) and their
// texture coordinates, in the 2D colour, when a scene is current.
void XB_CDECL RE_DrawQuadPic(const float* points, const float* coords, void* shader);
// 0x00237590: RE_StretchPic of the whole texture after the client's 640x480 adjust
// (function pointer 0x0095e6b0).
void XB_CDECL R_DrawPic(float x, float y, float w, float h, void* shader);
// 0x00237770: the height of "LltygI" in `font` at `scale` (times the global text
// scale 0x008db12c); the font's handle comes from the pointer at 0x0095e6b4.
uint32_t XB_CDECL RE_TextHeight(uint32_t font, float scale);

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

}  // namespace game
