#pragma once

// NGL's debug drawing: the frame statistics overlay (nglstats / nglfpsdisplay), the
// debug meshes (box, wire box, sphere, pyramid) and the lines drawn with them, the
// world-space debug line batch, and the game's debug line/string queues (DebugDraw_*).

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// The vertex writer nglDebugMesh_AddBox fills (a stack object of nglInitDebugMeshes,
// vtable 0x007be7d8): the material whose section (+4) gets the vertices, and the
// number of vertices written so far.
struct NglDebugMeshWriter {
    uint32_t vtable;
    uint32_t* material;
    uint32_t vertex_count;
};

// 0x00064780: the statistics text in the top right corner on a translucent black
// quad - with nglstats (0x00902c00 == 1) the NGL version, frame times, counts and
// memory, else only the frame rate and time.
void XB_CDECL nglDrawFpsCounter();
// 0x0006bb50: the frame time bar along the top of the screen (nglfpsdisplay): two
// times (0x00902bd4 / 0x00902bd8) as a share of the frame (20 ms PAL50, else 16.7 ms)
// of a bar with one tick per frame interval (0x008a2f00 of them). Never reached: nothing
// writes its switch 0x00902c01 (only 0x0006497e and nglEndFrame read it).
void XB_CDECL nglDrawFrameTimeBar();
// 0x0006c2b0 (size in EAX, writer in ESI, centre in EDI): a box of `size` (four
// lanes) around `center` as three strips of two quads each (24 vertices of 12 bytes).
extern "C" PLATFORM_HIDDEN void XB_CDECL nglDebugMesh_AddBox(const float* size, NglDebugMeshWriter* writer,
                                                         const float* center);
extern "C" void nglDebugMesh_AddBox_entry(void);
// 0x0006ce00: a line from `from` to `to` as the solid debug box stretched along it (the
// width is 0x008a2f04); with `arrow` the pyramid mesh at `to`, twice as wide. Lines
// shorter than 0.001 are skipped. Dead code: only the uncalled wrappers 0x0006d200 (no
// arrow) / 0x0006d220 (arrow) and nglDebugDrawAxes call it.
void XB_CDECL nglDebugDrawLine(const float* from, const float* to, uint32_t color, uint8_t arrow);
// 0x0006d240: the three axes of `matrix` (rows 0-2) from its origin (row 3), `scale` long. No callers.
void XB_CDECL nglDebugDrawAxes(const float* matrix, float scale, uint32_t color_x, uint32_t color_y,
                               uint32_t color_z);
// 0x0006d390 (vtable 0x007be7b4): a debug line batch node - vertices of {x, y, z,
// colour} (+0x10, count +0x14) as a line strip (+0x1c == 1) or a line list; in screen
// space (+0x18: depths through nglDepthToZValue) or through the matrix at +0xc (or the
// scene's view-projection, scene +0x190) in vertex shader constants 6-9. Never reached: the
// batch's creator and line adders (gap code 0x0006c050, 0x0006c270, 0x0006c290) have no callers.
void XB_THISCALL nglDebugLineBatch_Draw(uint8_t* self);
// 0x0006da20: builds the four debug meshes: the solid box (0x00902c90), the wire box
// of 12 edge boxes as wide as a debug line (0x00902c94), a sphere of 20-degree bands
// (0x00902c98) and a pyramid with its tip at +z (0x00902c9c).
void XB_CDECL nglInitDebugMeshes();
// 0x00241b40 (axes and origin in EAX): for each model of the object a call of
// 0x00240930 with its first LOD's skeleton, its bone matrices and the transform; the
// axis end points it computes first are never used.
void XB_REGS1 XB_CDECL R_DrawDObjSkeleton(const float* transform, const uint8_t* obj);
// 0x002b2f20 (end in EAX): queues a debug line (0x2c-byte records at 0x0092bf78, at
// most 1024) when debug drawing is on (0x0092be80): start, end, colour (four floats),
// `width`, how many frames it stays, its group (the current one at 0x0092bf88 when
// `grouped`, else 2) and with `fade` a per-frame fade of 1 / frames.
void XB_REGS1 XB_CDECL DebugDraw_AddLine(const float* end, const float* start, const float* color, uint32_t width,
                                         int32_t frames, uint32_t grouped, uint32_t fade);
// 0x002b30d0 (colour in EDI): queues a debug string (0x84-byte records at 0x0092bf68, at
// most 256): position, colour, scale and up to 95 characters.
extern "C" PLATFORM_HIDDEN void XB_CDECL DebugDraw_AddString(const float* color, const float* origin, float scale,
                                                         const char* text, uint32_t grouped);
extern "C" void DebugDraw_AddString_entry(void);

// 0x00068f70 (unreferenced: no calls, no pointers to it; kept here, not in ngl.cpp): a mesh
// section's bone matrix palette - per bone of the section (+8 count, +0xc 16-bit bone
// indices) one 4x4 matrix into `out`, by the node's instance flags (node +0x90 -> word 0):
// 0x10 the instance matrices (+8) as they are; 0x8 bind matrix (node +0x88 -> +0x14) times
// instance matrix; 0x4 that times the node's world inverse; otherwise identities.
void XB_CDECL nglBuildBoneMatrixPalette(float* out, uint8_t* node, const uint8_t* section);

// The debug-draw queues' growable arrays {data (Mem_AlignedAlloc), capacity, count} with
// 0x30-byte slots: push_back of one element (EAX = array). Full arrays grow by 4 (by 1
// while count <= 3), copying the old elements.
struct DebugDrawQueue {
    uint32_t data;
    int32_t capacity;
    int32_t count;
};
// 0x00028160: a 0x24-byte line (two points, colour); new slots up to the old count + 1
// get +0x10 = 0 first.
void XB_REGS1 XB_STDCALL DebugDraw_QueueLine(DebugDrawQueue* queue, const uint32_t* line);
// 0x000282b0: a 0x30-byte triangle.
void XB_REGS1 XB_STDCALL DebugDraw_QueueTriangle(DebugDrawQueue* queue, const uint32_t* triangle);

// 0x00027d60 (was FUN_00027d60; EDX = b, ECX = c): a triangle record {a, b, c} (four words
// each) into `out`; returns out. Only DebugDraw_AddTriangleIfEnabled calls it.
uint32_t* XB_REGS3 XB_STDCALL DebugDraw_MakeTriangle(uint32_t unused_eax, const uint32_t* b, const uint32_t* c,
                                                     uint32_t* out, const uint32_t* a);
// 0x0024f790 (EDX = b; no callers by CALL): when the debug-geometry switch (0x0095f688) is on,
// the triangle {a, b, c} onto the triangle queue 0x0097a8b0.
void XB_REGS2 XB_CDECL DebugDraw_AddTriangleIfEnabled(uint32_t unused_eax, const uint32_t* b, const uint32_t* a,
                                                      const uint32_t* c);
// 0x0024f7d0 (EAX = second, ECX = first, XMM0 = value; no callers by CALL): when 0x0095f688 is
// on, the line {first (four words), value, second (four words)} onto the line queue 0x00984ce0.
// (Ghidra's body runs on into 0x0024f840, a separate uncalled function: configstrings
// 0x10-0x1f set to empty.)
extern "C" PLATFORM_HIDDEN void XB_CDECL DebugDraw_AddLineIfEnabled(const uint32_t* second, const uint32_t* first,
                                                                    float value);
extern "C" void DebugDraw_AddLineIfEnabled_entry(void);
// 0x002ab560: with debug drawing on (0x0092be80), the queued strings and lines of one group
// expire (`toggle`: the group at 0x0092bf88 flips first and is the one; else group 2) and both
// lists go to the draw callbacks at 0x00928568 / 0x0092856c.
void XB_CDECL DebugDraw_Flush(uint32_t toggle);
// 0x002b2e80: frees the queues' arrays (Z_Free) and zeroes their state block 0x0092bf60. Returns 0.
uint32_t XB_CDECL DebugDraw_Shutdown();

}  // namespace game
