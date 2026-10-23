#pragma once

// Collision model queries (Q3's cm_*.c): leafs, areas and PVS clusters of the loaded clip map.
// clipMap_t (pointer at 0x0095f6b8): +8 numLeafs, +0xc leafs/nodes (0x10 bytes each; a node has 0xffff in
// its first word, the plane at +4 and child pointers at +8/+0xc; a leaf has its cluster at +4 and its area
// as a word at +8), +0x38 numAreas, +0x3c areas {floodnum, floodvalid}, +0x40 / +0x44 area portal counts,
// +0x58 numClusters, +0x5c floodvalid. Out-of-range leaf/area indexes read entry 0.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x002aaec0 (areas in EDX, ECX): 0 for negative areas, else whether their flood numbers match.
int32_t XB_REGS3 XB_CDECL CM_AreasConnected(uint32_t unused, int32_t area1, int32_t area2);
// 0x002aaf90 (cluster in ECX): the cluster's PVS row (0x0093565c + cluster * 0x00935658), or the row
// base for invalid clusters / no vis data (0x00935660).
const uint8_t* XB_THISCALL CM_ClusterPVS(int32_t cluster);
// 0x002aafc0: Q3's CM_StoreLeafs (leafList_t: count, maxcount, overflowed, list +0xc, lastLeaf +0x30);
// returns the list.
uint8_t* XB_CDECL CM_StoreLeafs(uint8_t* list, uint32_t leafnum);
// 0x002ab2e0 / 0x002ab300 (leafnum in EAX).
int32_t XB_REGS1 XB_CDECL CM_LeafArea(uint32_t leafnum);
int32_t XB_REGS1 XB_CDECL CM_LeafCluster(uint32_t leafnum);
// 0x002c1060 (num in EAX, point in EDX): walks the nodes down to a leaf; returns its index.
int32_t XB_REGS2 XB_CDECL CM_PointLeafnum_r(uint32_t num, const float* point);
// 0x002aaf00: Q3's CM_FloodArea_r (__break 1 "reflooded").
void XB_CDECL CM_FloodArea_r(uint32_t area_num, int32_t flood_num);
// 0x002b2200: Q3's CM_FloodAreaConnections.
void XB_CDECL CM_FloodAreaConnections();

// 0x002c73a0: Q3's CM_BoxLeafnums_r on packed SSE - the box (list +0x10 mins, +0x20 maxs, 16-byte rows) against
// each node plane {normal, dist}; the smallest positive distance to a plane goes to *min_distance; leafs go
// to the list's store callback (+0x34). Side 0 (only with NaN) follows the plane pointer as the original.
void XB_CDECL CM_BoxLeafnums_r(uint8_t* list, uint32_t nodenum, float* min_distance);
// 0x002bab30 (area1 in EDX, area2 in EDI; open on the stack): Q3's CM_AdjustAreaPortalState.
extern "C" PLATFORM_HIDDEN void XB_CDECL CM_AdjustAreaPortalState(int32_t area1, int32_t area2, uint32_t open);
extern "C" void CM_AdjustAreaPortalState_entry(void);

// 0x002d5320 (maxs in EAX, mins in ECX, the caller's leaf cache in ESI; list, list size, last leaf out on
// the stack): CM_BoxLeafnums with a one-leaf cache - cache +0x120 center (16 bytes, radius in its w at
// +0x12c), +0x130 leaf, +0x140 the box center to test. Within radius - 5 of the cached center the cached
// leaf is stored; else the tree walk, and a box in one leaf caches it with its distance to the nearest
// plane. Returns the count; bumps cm +0x60 (checkcount).
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL CM_BoxLeafnums(const float* maxs, const float* mins, uint8_t* cache,
                                                          int32_t* list, int32_t list_size, int32_t* last_leaf);
extern "C" void CM_BoxLeafnums_entry(void);

// 0x002ccc50 (maxs in EAX, capsule flag in EDX; mins, contents on the stack): fills the temporary box model
// (pointer at 0x0095f504): +0x10 mins, +0x20 maxs (16-byte rows; their w lanes are never-set stack, 0 in
// tests), +0x48 0x1fe (flag set) / 0x1ff; its leaf/brush (+4) gets the center (+0x10), the half extent
// (+0x20, 16 bytes), the contents (+8) and the half extent's length (+0x1c: SSE rsqrt estimate with one
// Newton step, 0 or NaN copied as is). Returns the model.
uint8_t* XB_REGS2 XB_CDECL CM_TempBoxModel(const float* maxs, uint32_t capsule, const float* mins, uint32_t contents);
// 0x002c1ae0 (name in EAX; client load flag on the stack): registers cm_noCurves / cm_playerCurveClip and,
// unless the flag is set, remembers the map name (0x009355d8, 0x80 bytes); __break 1 for a missing name.
void XB_REGS1 XB_CDECL CM_LoadMap(const char* name, uint32_t client_load);

// 0x002cb420 (brush in ECX; point on the stack, 16-byte aligned; unnamed in Ghidra): 1 when the point is
// outside the brush bounds (+0x10 center, +0x20 half size: max(center - half - p, p - center - half) < 0 per
// axis) or behind every side plane (+0x30 count, +0x34 sides {normal, dist}); 0 at the first side with
// p.normal - dist >= 0.
uint8_t XB_REGS3 XB_CDECL CM_BrushPointTest(uint32_t unused_eax, uint32_t unused_edx, const uint8_t* brush,
                                            const float* point);

// 0x000b15c0 (tree in ECX; point, out center, out half size on the stack, callee-popped; unnamed in
// Ghidra): descends a 2D quad tree - +0x4 child indexes (shorts; > 0 = node, children at index +0..3 by
// x >= / y >= the cell center), +0xc leaf base, +0x10 root center (16 bytes), +0x20 root half size. Each
// step halves the size (packed, every lane) and moves the center by +-half with the sign of point - center.
// Returns the leaf record (base - child * 8).
const uint8_t* XB_THISCALL CM_QuadTreeFindLeaf(const uint8_t* tree, const float* point, float* center,
                                               float* half_size);

// 0x002cc480 (point in EAX, model in ECX): the contents at a point - with a model, every brush (count +0,
// 0x40-byte brushes at +4) CM_BrushPointTest accepts; without, each world quad tree (count at +0xc of the
// record at 0x0095f4fc, trees from +0x10) whose bounds (+0x40 mins, +0x50 maxs) hold the point: the leaf's
// objects (index < +0x30: 0x50-byte records at +0x34, else 0x40-byte brushes at +0x3c), of which those whose
// vtable slot 8 returns 9 count when the point is inside their bounds and not in front of a side
// (dot > dist). The packed point is {x, y, z, x} for the world, {x, y, z, never set} for models.
uint32_t XB_REGS3 XB_CDECL CM_PointContents(const float* point, uint32_t unused, const uint32_t* model);

// 0x002db500 (origin in EAX, angles in EDX, model in EBX; point on the stack): CM_PointContents of the point
// moved into the model's frame (rotated by AnglesToAxis unless the model is the temporary box (+0x48 0x1ff)
// or all angles are 0: x' = p.forward, y' = 0 - p.right, z' = p.up, sums in the original's order).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL CM_TransformedPointContents(const float* origin, const float* angles,
                                                                       const uint32_t* model, const float* point);
extern "C" void CM_TransformedPointContents_entry(void);

// 0x002ba8e0 (out in EAX, rows in ECX; unnamed in Ghidra): the transpose of a 3x3 held in 16-byte rows
// (out rows 16 bytes apart, w words untouched).
void XB_REGS3 XB_CDECL Mat34_Transpose3(float* out, uint32_t unused, const float* rows);
// 0x002c7220 (vector in EAX, in place and returned; rows in ECX; unnamed in Ghidra): v = {row0.v, row1.v, row2.v} with
// packed products (every lane, the input's w included).
float* XB_REGS3 XB_CDECL Mat34_RotateVector(float* v, uint32_t unused, const float* rows);

// Trace work (ECX in the trace helpers): +0 start, +0x20 / +0x30 bounds (16-byte rows), +0x60 the eight box
// corner offsets (by sign of x/y/z), +0x120 fraction, +0x128 contents, +0x13c startsolid, +0x13d allsolid,
// +0x140 capsule axis offset, +0x160 capsule flag, +0x164 capsule radius.
// 0x002cb1a0 (trace work in ECX, kept; brush on the stack; unnamed in Ghidra): Q3's CM_TestBoxInBrush - the
// brush bounds (+0x10 center, +0x20 half size) against the trace bounds, then each side plane against the
// nearest box corner (or the capsule's nearer end + radius); inside all: startsolid/allsolid, fraction 0,
// the brush contents (+8).
extern "C" PLATFORM_HIDDEN void XB_CDECL CM_TestBoxInBrush(uint8_t* tw, const uint8_t* brush);
extern "C" void CM_TestBoxInBrush_entry(void);

// 0x002cb560 (model {count, brushes} in ESI, trace work in ECX, kept; unnamed in Ghidra): CM_TestBoxInBrush on
// each 0x40-byte brush until startsolid.
extern "C" PLATFORM_HIDDEN void XB_CDECL CM_TestInBrushes(const uint32_t* model, uint8_t* tw);
extern "C" void CM_TestInBrushes_entry(void);
// 0x002d4250 (trace work in ESI; unnamed in Ghidra): the start test of a capsule trace against the temporary
// box model: start/end (+0, +0x10) moved to the box center, radius = min(half x, half z) (half x on NaN),
// half height in +0x168, axis offset {0, 0, half z - radius}; then the box model of the trace bounds
// (+0x40, +0x50) tested with CM_TestBoxInBrush. Leaves ECX = the trace work like the original.
extern "C" PLATFORM_HIDDEN void XB_CDECL CM_TestCapsuleInTempBox(uint8_t* tw);
extern "C" void CM_TestCapsuleInTempBox_entry(void);

// 0x002cb5a0 (trace work in EAX; unnamed in Ghidra): Q3's CM_TestCapsuleInCapsule against the temporary box
// model - the trace capsule's end spheres (start +- axis offset) vs the box capsule's (center +- half z -
// radius, radius = min(half x, half z)) within the summed radius, then the cylinder overlap; a hit sets
// startsolid/allsolid and fraction 0. Packed differences of whole 16-byte vectors (w lanes included).
void XB_REGS1 XB_CDECL CM_TestCapsuleInCapsule(uint8_t* tw);

// 0x002da9d0 (trace work in EDI; unnamed in Ghidra): for each world quad tree whose bounds meet the start
// box (start + trace bounds, widened by the {1,1,1,1} at 0x0082e640), the leaf at the start point goes to
// 0x002da4d0 (trace work, tree, leaf); stops at startsolid.
extern "C" PLATFORM_HIDDEN void XB_CDECL CM_TestInWorldTrees(uint8_t* tw);
extern "C" void CM_TestInWorldTrees_entry(void);

// Bounding-box tree walk stack: 16-bit words at 0x0090a020, depth in 0x0090a620; entries are triples
// {node, count, axis}.
// 0x0001bef0 (unnamed in Ghidra): pushes one triple.
void XB_CDECL CM_BoxTreeStack_Push(uint16_t node, uint16_t count, uint16_t axis);
// 0x0001bf40 (tree in ECX; mins, maxs on the stack, callee-popped; unnamed in Ghidra): pops triples until a
// node whose split plane (tree +0x34 rows indexed by tree +0x3c flags & 0x3f, component `axis`) the box
// straddles; pushes the remaining siblings (counts from tree +0x4c byte pairs) and the side(s) the box
// reaches, cycling the axis. Returns the node in AX, 0xffff when the stack runs empty (registered without a
// result: the upper half of EAX is caller garbage).
uint16_t XB_THISCALL CM_BoxTreeStack_Next(const uint8_t* tree, const float* mins, const float* maxs);

// 0x000b0190 (unnamed in Ghidra): the point of triangle a, b, c closest to p, Ericson's region tests with
// packed SSE (16-byte vectors, every lane; the ac terms use a - c). Vertex regions copy the vertex, edge
// regions store a + t * edge (all four lanes, unaligned), the face region copies p. Returns out.
float* XB_CDECL ClosestPointOnTriangle(float* out, const float* a, const float* b, const float* c, const float* p);

// 0x002d4f80 (v2 in EAX, v1 in ECX, sphere center in ESI, v0 in EDI; radius and plane on the stack; unnamed
// in Ghidra): 1 when the sphere touches the triangle - not when the plane distance's magnitude exceeds the
// radius (x87 compare); inside all three edges (packed cross products with yzx shuffles) of the center's
// projection: 1; else the closest point on the triangle within the radius.
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL CM_SphereTouchesTriangle(const float* v2, const float* v1,
                                                                    const float* center, const float* v0,
                                                                    float radius, const float* plane);
extern "C" void CM_SphereTouchesTriangle_entry(void);

// 0x002da4d0 (unnamed in Ghidra): the trace position test against a quad tree leaf's objects (cm checkcount
// +0x60 bumped; objects marked with it at +0xc). Pass 1: type 9 brushes (vtable slot 8) in the content mask
// (trace work +0xf8) -> CM_TestBoxInBrush, until startsolid. Then, unless trace work +0xfc is set, pass 2:
// type 8 triangle meshes - inside their bounds (+0x10 center, +0x20 half) against the start sphere box, each
// triangle the box-tree walk finds (vertex indexes in +0x3c flags, rows +0x34, planes +0x44) tested with
// CM_SphereTouchesTriangle; a hit sets startsolid/allsolid/fraction 0. A mesh outside the box ends the whole
// test unless 0 <= (box max - center + half).x (the original's scalar CMPLESS).
void XB_CDECL CM_TestInLeafObjects(uint8_t* tw, uint8_t* tree, const uint8_t* leaf);

// 0x002cb890 (trace work in ECX; brush on the stack; unnamed in Ghidra): a capsule trace (start +0, end +0x10,
// axis offset +0x140, per-axis size +0x150, radius +0x164) through a brush: the six bound planes (center -
// half with normal -1 per axis, then center + half with +1), then the side planes against the capsule end
// nearer each plane, Q3 CM_TraceThroughBrush style (SURFACE_CLIP_EPSILON 0.125 at 0x0083ec68). A hit stores
// the enter fraction (+0x120) and the leading plane (+0x110, only the components the bound planes set); a
// start inside sets allsolid (+0x13d), and startsolid with fraction 0 when it does not get out.
extern "C" PLATFORM_HIDDEN void XB_CDECL CM_TraceCapsuleThroughBrush(uint8_t* tw, const uint8_t* brush);
extern "C" void CM_TraceCapsuleThroughBrush_entry(void);

// 0x002cbca0 (trace work in EAX; brush on the stack; unnamed in Ghidra): the box version of
// CM_TraceCapsuleThroughBrush - bound planes against the trace half size (+0x50), side planes moved by the box
// corner (+0x60 table by the normal's signs; the packed corner is {x, y, z, x}). AL = 0 when it stops early
// (no hit possible), else 1.
uint8_t XB_REGS1 XB_CDECL CM_TraceBoxThroughBrush(uint8_t* tw, const uint8_t* brush);

// 0x002d4f10 (model {count, brushes} in EDI, trace work in ESI; unnamed in Ghidra): each brush in the content
// mask traced with the capsule or box version (trace work +0x160); stops when the fraction becomes 0.
extern "C" PLATFORM_HIDDEN void XB_CDECL CM_TraceThroughBrushes(const uint32_t* model, uint8_t* tw);
extern "C" void CM_TraceThroughBrushes_entry(void);
// 0x002d4360 (trace work in ESI; unnamed in Ghidra): CM_TestCapsuleInTempBox's setup (also the per-axis size
// +0x150 = {radius, radius, half height}), the box model of the trace bounds, then the capsule trace through it.
extern "C" PLATFORM_HIDDEN void XB_CDECL CM_TraceCapsuleThroughTempBox(uint8_t* tw);
extern "C" void CM_TraceCapsuleThroughTempBox_entry(void);

// 0x002a3240 (vector in EAX, out in ESI): normalizes the 16-byte vector (all four lanes scaled by 1 / length;
// length through the title's __CIsqrt, stored as float); a zero length clears out's xyz. Returns the length on
// the x87 (0 for zero).
extern "C" PLATFORM_HIDDEN long double XB_CDECL Vec4Normalize(const float* v, float* out);
extern "C" void Vec4Normalize_entry(void);

// 0x002d47d0 (start in EAX, sphere origin in ECX, trace work in EDI; end and extra radius on the stack;
// unnamed in Ghidra): Q3's CM_TraceThroughSphere with radius trace +0x164 + extra. Starting inside: fraction 0,
// allsolid, the plane normal = start - origin over its rsqrt-Newton length, startsolid when the end is inside
// too; returns 0. Else, with the trace delta (+0xe0, its squared length at +0xf0): no hit -> 1; a hit stores
// the normalized normal (Vec4Normalize), the fraction from the x87 quadratic minus the 0.125 nudge (when
// below the current one; clamped at 0) and the temporary box model's contents; returns 0.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL CM_TraceThroughSphere(const float* start, const float* origin, uint8_t* tw,
                                                                 const float* end, float extra_radius);
extern "C" void CM_TraceThroughSphere_entry(void);

// 0x002d44b0 (trace work in EDI; origin, height and extra radius on the stack; unnamed in Ghidra): Q3's
// CM_TraceThroughVerticalCylinder - the 2D (x, y) version of CM_TraceThroughSphere with the half height
// (+0x168 - +0x164 + height) checked at the start (inside) or at the hit point (outside).
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL CM_TraceThroughVerticalCylinder(uint8_t* tw, const float* origin,
                                                                           float height, float extra_radius);
extern "C" void CM_TraceThroughVerticalCylinder_entry(void);

}  // namespace game
