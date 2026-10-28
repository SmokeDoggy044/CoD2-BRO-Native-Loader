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

// 0x002dab20 (trace work in EAX; unnamed in Ghidra): Q3's CM_TraceCapsuleThroughCapsule against the temporary
// box model's capsule (center +- (half z - radius)): bounds check (trace bounds widened by 1), then end spheres
// (CM_TraceThroughSphere) and the cylinder (CM_TraceThroughVerticalCylinder) in the original's order.
void XB_REGS1 XB_CDECL CM_TraceCapsuleThroughCapsule(uint8_t* tw);

// Sight (any hit) versions of the temporary-model traces, under the sight trace root 0x002e4c00:
// 0x002d41f0 (model {count, brushes} in ESI, trace work on the stack): 1 at the first brush in the content mask
// that CM_TraceBoxThroughBrush hits, else 0 (also for no model).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL CM_ModelBrushesBlockSight(const uint32_t* model, uint8_t* tw);
extern "C" void CM_ModelBrushesBlockSight_entry(void);
// 0x002d3e10 (trace work in ESI): CM_TraceCapsuleThroughTempBox's setup, then always the box trace through the
// temporary box model; returns its AL zero-extended.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL CM_TempBoxBlocksSight(uint8_t* tw);
extern "C" void CM_TempBoxBlocksSight_entry(void);
// 0x002d3f40 (sphere origin in EAX, start in ECX, trace work in EDI; extra radius on the stack): starting inside
// -> 0; outside, CM_TraceThroughSphere's quadratic (nudge (b * 0.125) / length here) -> 0 when it lands below the
// current fraction, else 1. Writes nothing.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL CM_SphereBlocksSight(const float* origin, const float* start, uint8_t* tw,
                                                                float extra_radius);
extern "C" void CM_SphereBlocksSight_entry(void);
// 0x002d4050 (trace work in EDI; origin, height, extra radius on the stack): the vertical cylinder version - inside
// within the half height -> 0; outside, the 2D quadratic and the hit height checked -> 0, else 1.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL CM_VerticalCylinderBlocksSight(uint8_t* tw, const float* origin,
                                                                          float height, float extra_radius);
extern "C" void CM_VerticalCylinderBlocksSight_entry(void);
// 0x002da1d0 (trace work in EAX): CM_TraceCapsuleThroughCapsule with the sight tests: -1 as soon as a sphere or
// the cylinder blocks, else 0.
int32_t XB_REGS1 XB_CDECL CM_CapsuleBlocksSight(uint8_t* tw);

// 0x002c0d80 (b in EAX, c in EDX, a in ECX; d on the stack; unnamed in Ghidra): a slab test of the segment
// b -> c against the box a (low) .. d (high): per axis enter/leave fractions (enter from 0, leave from 1; the low
// planes enter at a - b - 0.125); 0 as soon as enter >= leave, else 1. Packed differences of whole vectors.
uint8_t XB_REGS3 XB_CDECL CM_SegmentOverlapsBox(const float* b, const float* c, const float* a, const float* d);

// 0x002dae30 (start in EDX, v0 in ECX; end (in/out), direction, radius, v1, v2, plane, startsolid flag on the stack;
// unnamed in Ghidra): clips a swept sphere's end at triangle v0 v1 v2 with its plane {normal, dist}. A sweep shorter
// than 1e-5, or a start within the radius and not moving away from the plane, tests CM_SphereTouchesTriangle at the
// start (hit: flag 1). Else, moving toward the plane: the point Q 50 units behind the start, projected onto the
// plane (along the direction once it is beyond the radius); inside all edges the end becomes that point + normal *
// (radius + 1e-4), else the ray from Q through the end against the sphere at the triangle's closest point (x87
// sqrt). AL = 1 when the end moved or the flag was set.
uint8_t XB_REGS3 XB_CDECL CM_ClipSphereSweepToTriangle(uint32_t unused, const float* start, const float* v0, float* end,
                                                     const float* dir, float radius, const float* v1, const float* v2,
                                                     const float* plane, uint8_t* startsolid);


// 0x002c6bf0 (unnamed in Ghidra): the quad tree cells (tree +4 child indexes, +0xc leaf base) the segment p1 -> p2
// crosses, in order from p1: a leaf (child index <= 0) appends {leaf record, center (+0x10, 16 bytes), half size
// (+0x20)} to the 0x30-byte list at 0x00960034 (count 0x00960030, no bound check) and returns the new count; a
// node splits the segment where it crosses the cell center's x and/or y (quadrant = 2 * (x > cx) + (cy < y);
// children at index + quadrant, centers = center + table 0x0095ff30[quadrant] * half / 2, half size halved in its
// argument slot). Returns the count after the last leaf.
int32_t XB_CDECL CM_QuadTreeTraceLeafs_r(const uint8_t* tree, int32_t node, const float* p1, const float* p2,
                                         const float* center, float half);


// 0x002d37f0 (unnamed in Ghidra): a ray (zero-size) trace through the world quad trees for the nearest hit: out
// {hit point, normal} (32 bytes; the end and no normal when nothing is hit), the hit object's surface flags (+4)
// and contents (+8). Sweeps shorter than 0.001 (squared) return 0. Per tree whose bounds the segment meets: the
// cells CM_QuadTreeTraceLeafs_r lists, each cell's objects in the content mask (checkcount-marked): brushes with
// CM_TraceBoxThroughBrush (an exact fraction 0 returns 1), meshes through their vtable slot 5; a nearer hit
// shrinks the search box to start .. hit. Stops a tree's cells at the first whose x/y bounds hold the hit; an
// object box outside the search box ends the cell unless 0 <= (search max - object min).x. AL = hit.
uint8_t XB_CDECL CM_TraceRayThroughWorldTrees(uint8_t* tw, const float* start, const float* end, float* out,
                                              uint32_t* surface_flags, uint32_t* contents);


// 0x002e04e0 (trace work in EDI; unnamed in Ghidra): the box/capsule sweep through the world quad trees: per tree
// whose bounds meet the swept box (start/end +- trace size +0x150), the cells CM_QuadTreeTraceLeafs_r lists; per
// cell, pass 1 traces its brushes (type 9, content mask, checkcount-marked) with the capsule or box version (an
// exact fraction 0 stores the brush's surface flags/contents and returns 1); pass 2 sweeps a sphere (radius +0x164,
// or the half size x for boxes; start/end z lowered by +0x168 - radius, restored after the cell) through each
// mesh's triangles the box-tree walk finds: CM_ClipSphereSweepToTriangle with radius + 0.001 moves the end; a
// start in solid or a fraction <= 1e-5 sets fraction 0 and allsolid and returns 1, else a smaller fraction (minus
// 1e-5) stores the triangle plane, flags and contents. An object box outside ends the pass unless
// 0 <= (max - center + half).x. AL = whether a mesh was hit.
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL CM_TraceThroughWorldTrees(uint8_t* tw);
extern "C" void CM_TraceThroughWorldTrees_entry(void);

// 0x002e4980 (trace work in EAX, start in ESI, end in EBX; unnamed in Ghidra): bumps checkcount; a box/capsule
// trace (+0xfc 0) goes to CM_TraceThroughWorldTrees; a ray trace takes CM_TraceRayThroughWorldTrees's nearest
// hit: surface flags/contents, and when its distance over the trace length (rsqrt-Newton lengths) is below the
// fraction, the fraction (start/end plane distances, 0.125 nudge) and plane normal.
extern "C" PLATFORM_HIDDEN void XB_CDECL CM_TraceThroughWorld(uint8_t* tw, const float* start, const float* end);
extern "C" void CM_TraceThroughWorld_entry(void);


// 0x002e5030 (capsule block in EAX, maxs in ECX; results, start, end, mins, model, brush mask, capsule flag on the
// stack): Q3's CM_BoxTrace. The 0x170-byte trace work (Com_Memset) gets the content mask (+0xf8), the results'
// fraction (preset to 1 without a capsule block), the box centered on start/end (+0x40/+0x50 sizes, start/end
// moved by the offset, delta +0xe0, its squared length +0xf0), the capsule block (0x30 bytes to +0x140) or a
// capsule from the size (radius = min(x, z), axis offset z - radius), the swept bounds and the corner table. Equal
// start and end: the position tests (world trees, a model's brushes, the temporary box capsule), else the sweeps
// (+0xfc = zero size); endpos = start + delta * fraction, and +0x100..+0x140 copied to the results. EAX = start z.
uint32_t XB_REGS3 XB_CDECL CM_BoxTrace(const uint8_t* capsule, uint32_t unused, const float* maxs, uint8_t* results,
                                       const float* start, const float* end, const float* mins, const uint32_t* model,
                                       uint32_t brushmask, uint32_t capsule_flag);

// 0x002e70c0 (origin in EAX, maxs in ECX; results, start, end, mins, model, brush mask, angles, capsule flag on
// the stack): Q3's CM_TransformedBoxTrace - the centered box and start/end relative to the origin, rotated into a
// model with angles (AnglesToAxis rows {forward, -right, up}, Mat34_RotateVector), a capsule block (axis offset
// (0, 0, half z - radius) rotated likewise), CM_BoxTrace, the plane normal rotated back (Mat34_Transpose3) when
// the fraction dropped, endpos from the world start/end. Never-set stack words (block, row w) are zero as in
// tests. Returns end.
const float* XB_REGS3 XB_CDECL CM_TransformedBoxTrace(const float* origin, uint32_t unused, const float* maxs,
                                                      uint8_t* results, const float* start, const float* end,
                                                      const float* mins, const uint32_t* model, uint32_t brushmask,
                                                      const float* angles, uint32_t capsule_flag);


// 0x002e6dc0 (origin in EAX, end in ECX; an unused word, start, mins, maxs, model, brush mask, angles, capsule flag
// on the stack; unnamed in Ghidra): the sight-trace version of CM_TransformedBoxTrace - the same centered box,
// local start/end and capsule block, handed to CM_SightTrace (0x002e4c00), whose result it returns (-1/0/1 or the
// box trace's AL; never a stack address).
uint32_t XB_REGS3 XB_CDECL CM_TransformedSightTrace(const float* origin, uint32_t unused, const float* end,
                                                    uint32_t unused_arg, const float* start, const float* mins,
                                                    const float* maxs, const uint32_t* model, uint32_t brushmask,
                                                    const float* angles, uint32_t capsule_flag);
// 0x002e4c00 (end in EAX, start in ECX; mins, maxs, model, brush mask, capsule flag, capsule block on the stack):
// CM_BoxTrace's trace-work setup (fraction 1, no results) for a sight test. A model: the temporary box model
// (0x1fe, content mask) -> CM_CapsuleBlocksSight (capsule) or CM_TempBoxBlocksSight, other models ->
// CM_ModelBrushesBlockSight; no model: the world trees (ray version for a point-sized box), 1 when blocked.
int32_t XB_REGS3 XB_CDECL CM_SightTrace(const float* end, uint32_t unused, const float* start, const float* mins,
                                        const float* maxs, const uint32_t* model, uint32_t brushmask,
                                        uint32_t capsule_flag, const uint8_t* capsule);

// Capsule traces against gathered candidate lists (CM_GatherBoxCandidates / CM_FilterBoxCandidates layout below),
// under 0x002e95b0 (actor movement traces):
// 0x002da830 (trace work in EAX; triangle list, brush list on the stack): the position test - CM_TestBoxInBrush
// per brush until startsolid, then CM_SphereTouchesTriangle (radius +0x164) per triangle; a hit sets
// startsolid/allsolid and fraction 0.
void XB_REGS1 XB_CDECL CM_TestInCandidates(uint8_t* tw, const uint8_t* triangles, const uint8_t* brushes);
// 0x002e1150 (trace work in EDI; triangle list, brush list on the stack): CM_TraceCapsuleThroughBrush per brush
// (fraction 0 ends it, a smaller fraction stores the brush's flags/contents), then the triangles as in
// CM_TraceThroughWorldTrees' pass 2 (lowered sphere, radius + 0.001, triangles with both ends beyond the plane
// skipped, CM_ClipSphereSweepToTriangle from the unchanged end each time). AL = 1 when stuck or a triangle hit.
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL CM_TraceThroughCandidates(uint8_t* tw, const uint8_t* triangles,
                                                                     const uint8_t* brushes);
extern "C" void CM_TraceThroughCandidates_entry(void);
// 0x002e20e0 (maxs in EAX, mins in ECX, start in EBX; triangle list, brush list, results, end, brush mask on the
// stack): CM_BoxTrace's setup for an always-capsule trace without corner rows or checkcount, then
// CM_TestInCandidates (start == end) or CM_TraceThroughCandidates; endpos and the 0x40-byte results as CM_BoxTrace.
// EAX = start y bits.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL CM_CandidateCapsuleTrace(const float* maxs, const float* mins,
                                                                     const float* start, const uint8_t* triangles,
                                                                     const uint8_t* brushes, uint8_t* results,
                                                                     const float* end, uint32_t brushmask);
extern "C" void CM_CandidateCapsuleTrace_entry(void);
// 0x002d3360 (trace work in ECX; triangle list, brush list, start, end, out {point, plane}, surface flags, contents on
// the stack): the ray version - each brush CM_TraceBoxThroughBrush hits writes its point/plane/ids; each triangle
// whose plane the segment crosses (start above 1e-4, end at or below 0) closer than the full length, with the
// crossing inside all three edges, writes them too (no early out, the bound stays the full length). AL = any hit.
uint8_t XB_REGS3 XB_CDECL CM_TraceRayThroughCandidates(uint32_t unused_eax, uint32_t unused_edx, uint8_t* tw,
                                                       const uint8_t* triangles, const uint8_t* brushes,
                                                       const float* start, const float* end, float* out,
                                                       uint32_t* surface_flags, uint32_t* contents);
// 0x002d4a60 (end in EAX; triangle list, brush list, results, start, brush mask on the stack): a point-sized trace
// over the candidate lists - CM_TraceThroughWorld's ray branch with CM_TraceRayThroughCandidates (sizes from
// 0x0082e290), endpos and the 0x40-byte results as CM_BoxTrace.
void XB_REGS1 XB_CDECL CM_CandidateRayTrace(const float* end, const uint8_t* triangles, const uint8_t* brushes,
                                            uint8_t* results, const float* start, uint32_t brushmask);
// 0x002d60d0 (entity in EBX; radius, start, end on the stack; game code kept here with its collision callees): the
// entity's record (handle at +0x7c of its first pointer, table 0x0097a968) caches candidate lists (+0x450 triangles,
// +0x454 brushes) for a box (+0x430/+0x440); a segment box (2 * radius around, z +-100) outside it regathers them
// (CM_GatherBoxCandidates). The lists filtered with the entity's clip mask (+0x5c & 0xfdff3fff), then
// CM_CandidateRayTrace; AL = 1 when the fraction is below 1.
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL G_RayTraceCachedCandidates(const uint8_t* entity, float radius,
                                                                          const float* start, const float* end);
extern "C" void G_RayTraceCachedCandidates_entry(void);
// 0x002c6400 (brush in EAX, point in ESI, radius in XMM2; out on the stack): the shallowest way out of a brush for a
// sphere - per axis (half + radius) - |point - center| on the x87 (stored as float), per side plane
// -((point . n - dist) - radius); any negative depth -> 0 (outside). Else out = point pushed along the nearest plane
// (or to the nearest box face) by depth + the global skin 0x008fef1c; returns 1.
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL CM_PushPointOutOfBrush(const uint8_t* brush, const float* point,
                                                                  float radius, float* out);
extern "C" void CM_PushPointOutOfBrush_entry(void);
// 0x002d1f30 (center in EAX, out in ECX; radius, triangle a, b, c, plane normal on the stack): a sphere against a
// triangle - plane distance (a - center) . n within +-radius, then the closest feature (vertex, edge, else the face)
// by Ericson-style region tests on vectors from the center; face: out = center + (dist + radius) * n; else, for a
// squared distance above 1e-4 within the radius and dist < 0, out = center - ((r - len) / len on the x87) * v. AL.
uint8_t XB_REGS3 XB_CDECL CM_PushSphereOutOfTriangle(const float* center, uint32_t unused, float* out, float radius,
                                                     const float* a, const float* b, const float* c,
                                                     const float* normal);
// 0x002e2f50 (cdecl move state, radius; game code kept here with its collision callees): pushes the capsule
// (state +0: player state, origin at +0, handle +0x7c; clip mask +0x5c; mins/maxs z +0x108/+0x118) out of solids. The
// cached candidates as in G_RayTraceCachedCandidates (around the capsule's middle), entities from SV_AreaEntities
// (statics 0x0098e460..0x0098e690); then for three spheres (z + radius, height / 2, height - radius): brush models
// (CM_PushPointOutOfBrush in the model's frame, G_GetEntityMatrix), box entities (closest point; out by
// radius - distance + 0.01, x87), candidate brushes and triangles (CM_PushSphereOutOfTriangle); the origin is updated
// after each sphere. AL = whether anything pushed.
uint8_t XB_CDECL G_PushCapsuleOutOfSolids(uint8_t* pm, float radius);
// 0x002d5d50 (move state in ESI, radius in XMM3): pushes the origin away from nearby entities - SV_AreaEntities around
// the capsule's middle (2 * radius, z +-100; statics 0x0098c160..0x0098c390), each other entity whose origin
// (+0x140) is not above the capsule top + 2 nor (the original's x test) p.x beyond its x + 72, and within 2 * radius
// in x/y: the origin becomes entity origin + (2 * radius + 0x008fef50) * direction away (x87 sqrt; below 0.01 the
// direction is {1, 0, 0, 1}). AL = any push; the origin is written back.
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL G_PushAwayFromEntities(uint8_t* pm, float radius);
extern "C" void G_PushAwayFromEntities_entry(void);
// 0x002e6260 (move state 0x0092eff4): G_PushAwayFromEntities (clip mask 0x200c000) and G_PushCapsuleOutOfSolids
// (mask & 0xfdff3fff), each repeated up to five more times while it pushes, radius 0x008fef4c; a move longer than the
// radius is undone when G_RayTraceCachedCandidates finds the way blocked. The clip mask is restored.
void XB_CDECL G_SeparateFromSolids();
// 0x002caf70 (a in EAX, c in ECX; origin, direction, b, max fraction, out fraction on the stack): Moller-Trumbore
// segment vs triangle - |det| < 1e-4 on the x87 misses, t = fraction along the (unnormalized) direction in
// [0, max], barycentrics u, v >= 0 with u + v <= 1; a hit stores t. AL.
uint8_t XB_REGS3 XB_CDECL CM_RayHitsTriangle(const float* a, uint32_t unused, const float* c, const float* origin,
                                             const float* dir, const float* b, float max_fraction, float* fraction);
// 0x002d27c0 (start, delta, plane out, fraction in/out on the stack): the nearest mesh triangle the segment crosses
// front-facing - per world quad tree whose bounds meet the segment box, only the leaf at the segment's middle
// (CM_QuadTreeFindLeaf), its non-brush objects, triangles from the box-tree walk with plane . delta < 0
// (CM_RayHitsTriangle with the current fraction as the limit). Fraction starts at 1; AL = any hit.
uint8_t XB_CDECL CM_RayHitsWorldMeshes(const float* start, const float* delta, float* plane_out, float* fraction);
// 0x002cc0e0 (start, end, brush, fraction in/out): Q3-style segment vs brush clip without a work block - the six box
// faces (mins with normal -1, then maxs with +1), then the side planes (+0x34, count +0x30); enter/leave fractions
// with the 0.125 epsilon, leave starting at *fraction. 0 when it misses; else 1 with *fraction = enter after a
// facing plane, 0 when the start is inside, untouched otherwise.
uint8_t XB_CDECL CM_ClipSegmentToBrush(const float* start, const float* end, const uint8_t* brush, float* fraction);
// 0x002a11f0 (vector in ESI; result in XMM0): the pitch of a vector in degrees [0, 360) with the title's own arcsine
// series (t < 0.5: t + t^3/6 + 0.075 t^5 + 0.054 t^7; else the sqrt((1 - t) / 2) form, __CIsqrt everywhere): 270 / 90
// straight up / down, 0 for a zero vector; atan2-style quadrant fix-ups, * -180 / pi, + 360 below 0.
extern "C" PLATFORM_HIDDEN float XB_CDECL Vec3_PitchDegrees(const float* v);
extern "C" void Vec3_PitchDegrees_entry(void);
// 0x002d2b50 (triangle list, brush list, start, end, fraction in/out, surface flags, contents; Vehicle_UpdatePhysics):
// CM_TraceRayThroughCandidates with fractions - brushes through CM_ClipSegmentToBrush (ids, 0x009507e0 = -1; an exact
// 0 ends it), crossed triangles inside their edges give ((start - crossing) . plane - 0.125) / (ds - de) when below
// the current fraction (ids, mesh to 0x0094cb9c, triangle to 0x009507e0).
void XB_CDECL CM_TraceSegmentThroughCandidates(const uint8_t* triangles, const uint8_t* brushes, const float* start,
                                               const float* end, float* fraction, uint32_t* surface_flags,
                                               uint32_t* contents);
// 0x00227160 (fraction out in EDI; start, delta on the stack): *fraction = 1, then every brush of the list 0x008fcee0
// (count 0x008fcee4) through CM_ClipSegmentToBrush and every front-facing triangle of the list 0x008fd3f0 ({a, c,
// b, plane} pointers, count 0x008fd3f4) through CM_RayHitsTriangle. AL = any hit.
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL CM_RayHitsListedObjects(float* fraction, const float* start,
                                                                   const float* delta);
extern "C" void CM_RayHitsListedObjects_entry(void);
// 0x002299a0 (point in EAX, out in EBX; radius on the stack): out = point pushed out of the listed brushes
// (CM_PushPointOutOfBrush), triangles (CM_PushSphereOutOfTriangle) and the brush models of the entity records at
// 0x008fd680 (count 0x008fd684; in each entity's frame, as G_PushCapsuleOutOfSolids). AL = any push.
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL CM_PushSphereOutOfListedObjects(const float* point, float* out, float radius);
extern "C" void CM_PushSphereOutOfListedObjects_entry(void);
// 0x0002a340 (object in EAX): moves a point attached to its owner (+0x124; rows +0x40.., origin +0x70) - world =
// owner frame * local point (+0x90); when farther than half the length (+0x120) from the position (+0x100), the
// segment scaled by (half + len) / len (x87) is tested with CM_RayHitsListedObjects and the push (+0x110) set to
// (fraction - 1) * that segment; the owner origin moves by the push and the position becomes world + push.
void XB_REGS1 XB_CDECL Phys_MoveAttachedPoint(uint8_t* obj);
// 0x0002ab10 (object in EAX): for each attached point (+0xbf0, count +0xc44) the owner origin (+0x124 -> +0x70) moves back
// by the point's push (+0x110); the w lane added is a never-written stack word in the original (0 here, as in tests).
void XB_REGS1 XB_CDECL Phys_UndoAttachedPushes(uint8_t* obj);
// 0x00203f60 (entity record in EAX; start, end, radius, content mask, output triangle and brush lists on the stack):
// 0 without a record or cached lists (+0x450 / +0x454) or when the segment box (radius around, z up to 2 * radius)
// is not inside the cached box (+0x430 / +0x440); else CM_FilterBoxCandidates into the output lists and 1.
uint8_t XB_REGS1 XB_CDECL G_FilterCachedCandidates(const uint8_t* record, const float* start, const float* end,
                                                   float radius, uint32_t content_mask, uint8_t* out_triangles,
                                                   uint8_t* out_brushes);


// Candidate lists (CM_GatherBoxCandidates / CM_FilterBoxCandidates): brushes {pointer +0x800, count +0x804} of 4-byte
// entries, triangles {pointer +0x1000, count +0x1004} of {mesh, triangle} pairs; at most 0x200 each (the count
// still grows when the slot address is 0).
// 0x002c6840 (unnamed in Ghidra): the quad tree walk for a box: a leaf's objects (checkcount-marked before the
// test, no content mask) whose bounds meet the box - brushes (type 9) appended, meshes' triangles (box-tree walk,
// triangle bounds) appended; a node recurses into each quadrant whose cell (center + tables 0x00963870 /
// 0x00960120 * half) meets the box in x and y, with the child center (table 0x0095ff30 * half / 2).
void XB_CDECL CM_GatherQuadTreeCandidates_r(uint8_t* tree, uint32_t tree_index, const int16_t* child, const float* center,
                                            float half, const float* mins, const float* maxs, uint8_t* triangles,
                                            uint8_t* brushes);
// 0x002cb0f0 (maxs in EDI; mins, triangle list, brush list on the stack): clears both lists, bumps checkcount and
// walks each world quad tree whose bounds meet the box. EAX = the tree count (the clip map without trees).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL CM_GatherBoxCandidates(const float* mins, uint8_t* triangles,
                                                                    uint8_t* brushes, const float* maxs);
extern "C" void CM_GatherBoxCandidates_entry(void);
// 0x002c6630 (output brush list in EAX, input brush list in ECX; mins, maxs, content mask, input and output
// triangle lists on the stack): the gathered candidates in the content mask whose bounds (and, for triangles,
// the triangle's bounds) meet the box. Returns the input triangle count.
int32_t XB_REGS3 XB_CDECL CM_FilterBoxCandidates(uint8_t* out_brushes, uint32_t unused, const uint8_t* in_brushes,
                                                 const float* mins, const float* maxs, uint32_t contents,
                                                 const uint8_t* in_triangles, uint8_t* out_triangles);


// 0x002d1980 (mesh in ESI, maxs in EDI; start, end, delta, mins on the stack; unnamed in Ghidra): 1 when the segment
// crosses a mesh triangle the box-tree walk finds: facing planes (delta.normal < 0) with start and end on opposite
// sides, triangle bounds meeting the box, the crossing point inside all three edges.
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL CM_SegmentCrossesMesh(const uint8_t* mesh, const float* maxs, const float* start,
                                                                  const float* end, const float* delta, const float* mins);
extern "C" void CM_SegmentCrossesMesh_entry(void);
// 0x002d2f90 (unnamed in Ghidra): the sight (any hit) version of CM_TraceRayThroughWorldTrees: brushes hit when the
// box trace drops the fraction below 1, meshes through CM_SegmentCrossesMesh; the object hit goes to 0x0094cba0.
uint8_t XB_CDECL CM_SightTraceRayThroughWorldTrees(uint8_t* tw, const float* start, const float* end);
// 0x002e1740 (trace work in EDI; unnamed in Ghidra): the sight version of CM_TraceThroughWorldTrees: any brush
// fraction below 1, or any triangle CM_ClipSphereSweepToTriangle clips (end copy per triangle), returns 1.
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL CM_SightTraceThroughWorldTrees(uint8_t* tw);
extern "C" void CM_SightTraceThroughWorldTrees_entry(void);

// World trees (.cg data, relocated in place): a tree has +0x00 node count / +0x04 nodes (2 bytes each), +0x08
// leaf count / +0x0c leaves {count, pointer to count 2-byte entries}, +0x24 count / +0x28 16-byte records,
// +0x30 mesh count / +0x34 meshes (0x50 bytes: CM_Mesh), +0x38 brush count / +0x3c brushes (0x40 bytes: CM_Brush).
// The fixups turn the data offsets into pointers: each array starts at the cursor rounded up to 16, the
// pointer is cursor + base, the cursor then moves past the array; the cursor ends rounded up to 4.

// 0x0001bdc0 (thiscall): the triangle mesh destructor - CM_TriMesh vtable, then Mem_AlignedFree of the arrays at
// +0x4c, +0x44, +0x3c, +0x34 (each pointer and its count before it cleared).
void XB_THISCALL CM_TriMesh_Dtor(uint8_t* mesh);
// 0x0001bec0 / 0x0001c0c0 (vtable slot 9): CM_TriMesh_Dtor (the CM_Mesh one sets its vtable first), then Z_Free
// with flags bit 0. Return the mesh.
uint8_t* XB_THISCALL CM_TriMesh_DeletingDtor(uint8_t* mesh, uint32_t flags);
uint8_t* XB_THISCALL CM_Mesh_DeletingDtor(uint8_t* mesh, uint32_t flags);
// 0x0001c100 (vtable slot 9): Mem_AlignedFree of the side array (+0x34, count +0x30, both cleared), then Z_Free
// with flags bit 0. Returns the brush.
uint8_t* XB_THISCALL CM_Brush_DeletingDtor(uint8_t* brush, uint32_t flags);
// 0x0001d8b0 (vtable slot 0): CM_BrushArray_Free of the box hull (+0x190), ResourceManager_Destruct, then
// Z_Free with flags bit 0. Returns the manager.
uint8_t* XB_THISCALL CM_WorldTreeManager_DeletingDtor(uint8_t* manager, uint32_t flags);
// 0x0001fda0 (array {count, brushes} in ESI): each brush's vtable slot 9 with flags 0, Mem_AlignedFree, both
// words cleared (only when the array pointer is set).
extern "C" PLATFORM_HIDDEN void XB_CDECL CM_BrushArray_Free(uint32_t* array);
extern "C" void CM_BrushArray_Free_entry(void);
// 0x0001fa90 (array in EAX; count on the stack): CM_BrushArray_Free, then count 0x40-byte brushes
// (Mem_AlignedAlloc, 16-byte aligned): vtable CM_Brush, +4..+0xc -1, +0x10 the zero vector of 0x0082e290 with w 0,
// +0x30/+0x34 0. The count is stored when the allocation succeeded.
void XB_REGS1 XB_STDCALL CM_BrushArray_Alloc(uint32_t* array, uint32_t count);
// 0x002cab00 (hull in ESI): one brush (CM_BrushArray_Alloc), +0x10/+0x20/+0x30 the vector 0x0082e290 {0,0,0,1},
// +0x40/+0x44 0.0f, +0x48 0. Returns the hull.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL CM_InitBoxHull(uint8_t* hull);
extern "C" void CM_InitBoxHull_entry(void);
// 0x002d0d60 (manager in EBX): the inlined ResourceManager registration (0x009719f8 list of 20), the
// CM_WorldTreeManager vtable, CM_InitBoxHull(+0x190), +4..+0x190 zeroed, 0x0095f504 = the hull. Returns the manager.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL CM_WorldTreeManager_Construct(uint8_t* manager);
extern "C" void CM_WorldTreeManager_Construct_entry(void);
// 0x0001be40 (cursor in EAX, base in EDX, mesh in ECX): the mesh's arrays +0x34 (count +0x30, 16 bytes each),
// +0x3c (+0x38, 4 bytes), +0x44 (+0x40, 16 bytes), +0x4c (+0x48 bytes).
void XB_REGS3 XB_CDECL CM_WorldMesh_Fixup(uint32_t* cursor, uint32_t base, uint8_t* mesh);
// 0x0001ce30 (cursor in EAX, tree in ECX, base in EDI): the tree's nodes (+4), leaves (+0xc), records (+0x28),
// then each leaf's entries (2 bytes each).
extern "C" PLATFORM_HIDDEN void XB_CDECL CM_WorldTreeNodes_Fixup(uint32_t* cursor, uint8_t* tree, uint32_t base);
extern "C" void CM_WorldTreeNodes_Fixup_entry(void);
// 0x0001cff0 (cursor in ESI; tree, base on the stack, stdcall): the meshes (CM_WorldMesh_Fixup each), the brushes
// (each one's sides +0x34, count +0x30, 16 bytes each), CM_WorldTreeNodes_Fixup; the cursor ends 4-aligned.
extern "C" PLATFORM_HIDDEN void XB_CDECL CM_WorldTree_Fixup(uint32_t* cursor, uint8_t* tree, uint32_t base);
extern "C" void CM_WorldTree_Fixup_entry(void);
// 0x002d0c60 (manager in EBX, tree in EDI; id on the stack, stdcall): CM_WorldTree_Fixup from offset 0x80 with the
// tree as base, the tree added to the manager (+0xc count, +0x10 trees, +0x19c ids); each brush and mesh gets its
// vtable and flag 0x4000 (+8) moved to 0x1000000 (an old 0x1000000 cleared first).
extern "C" PLATFORM_HIDDEN void XB_CDECL CM_LoadWorldTree(uint8_t* manager, uint8_t* tree, uint32_t id);
extern "C" void CM_LoadWorldTree_entry(void);
// 0x0001ced0 (cursor in EAX, model in EDX, base in EBX): an inline model's brushes (+4, count +0, 0x40 bytes) and
// each brush's sides (+0x34, count +0x30); the cursor ends 4-aligned.
extern "C" PLATFORM_HIDDEN void XB_CDECL Dcg_RelocateModel(uint32_t* cursor, uint8_t* model, uint32_t base);
extern "C" void Dcg_RelocateModel_entry(void);
// 0x0001cf50 (cursor in ESI, list {count, models} in EDI; base on the stack, stdcall): the 0x50-byte models, then
// Dcg_RelocateModel each; the cursor ends 4-aligned.
extern "C" PLATFORM_HIDDEN void XB_CDECL Dcg_RelocateModels(uint32_t* cursor, uint32_t* list, uint32_t base);
extern "C" void Dcg_RelocateModels_entry(void);
// 0x0001cfc0 (list {count, models} in ECX, key in EDI): the 0x50-byte model whose +0x48 is the key, or null.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL CM_FindInlineModelInList(const uint32_t* list, uint32_t key);
extern "C" void CM_FindInlineModelInList_entry(void);
// 0x002afb10 (key in EAX, index in ECX, model lists in EBX): the key in list `index` (lists + 4 + index * 4),
// else in the list indexed by +0x34 of 0x0095f638, else null.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL CM_FindInlineModel(uint32_t key, uint32_t index, const uint8_t* lists);
extern "C" void CM_FindInlineModel_entry(void);
// 0x002ab010 (unnamed in Ghidra): unlinks every entry whose owner (+0xe0) is `owner` from the sector's list (+0x14,
// next at +0xdc) and its children's (+0x1c, +0x20), rebuilding the contents (+4) from what stays (entry +0x68 ->
// +0x40). Returns the sector's contents. The tree is the server's sector tree (0x00935664).
uint32_t XB_CDECL CM_SectorTree_UnlinkOwner_r(uint32_t owner, uint8_t* sector);
// 0x002cc750 (dead: only its own recursion calls it): CM_BoxLeafnums_r without the nearest-plane distance.
void XB_CDECL CM_BoxLeafnums_r_2(uint8_t* list, uint32_t nodenum);
// 0x00376480 / 0x00376530 / 0x003765d0 (static initializers): four 16-byte vectors each.
void XB_CDECL StaticInit_Vec4x4_95ff30();
void XB_CDECL StaticInit_Vec4x4_963870();
void XB_CDECL StaticInit_Vec4x4_960120();
// 0x00379920 (atexit): Mem_AlignedFree of 0x00960034, it and its count 0x00960030 cleared.
void XB_CDECL AtExit_FreeWorldTrees();

// 0x000aceb0 (vtable slot 2 of CM_Mesh / CM_TriMesh; thiscall): the support point - the vertex (+0x34, count
// +0x30, 16-byte rows) with the largest dot product with `dir` (the first one on ties) copied to out; returns out.
float* XB_THISCALL CM_TriMesh_Support(const uint8_t* mesh, float* out, const float* dir);
// 0x000adf20 (vtable slot 5 of CM_Mesh; thiscall, 5 stack words, the last unused): the nearest triangle the
// segment start -> end crosses. A start outside the mesh's bounding sphere (+0x10 center, +0x1c radius) needs the
// segment to head towards it and to pass within the radius. Then the box-tree walk over the segment's bounds:
// facing planes (delta.normal < 0) with the ends on opposite sides, triangle bounds meeting the segment's,
// the crossing point inside all three edges and nearer than the best so far (from 1). A hit writes the
// point (out +0) and the plane (out +0x10) and the triangle to *triangle. Returns 1 when anything was hit.
uint8_t XB_THISCALL CM_Mesh_IntersectRay(uint8_t* mesh, const float* start, const float* end, float* out,
                                         uint32_t* triangle, uint32_t unused);
// 0x002d1ce0 (mesh pointer's address in EAX, out in EBX; start, end, triangle on the stack; unnamed in Ghidra):
// re-tests the cached triangle *triangle of the mesh: start at least 0.0001 in front of its plane, end not in front,
// the crossing point ((start de - end ds) / (de - ds), as a reciprocal product) inside all three edges -> out
// gets the point (+0) and the plane (+0x10), returns 1; else *triangle = -1, returns 0.
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL CM_RayHitsMeshTriangle(uint8_t* const* mesh_ref, float* out, const float* start,
                                                                  const float* end, int32_t* triangle);
extern "C" void CM_RayHitsMeshTriangle_entry(void);
// 0x002d0680 (world tree manager in ECX; a per-frame debug draw callback through the thunk at 0x0001d0d0, only with
// manager +4 bit 0 set): for each world tree (only 0x008fef14 unless -1) holding the view position of
// 0x0095f534 +4 (+0x140), every mesh whose box is within 0x008fef10 of it is drawn once per checkcount as a
// frame-memory position mesh (its triangles as 3-vertex strips) tinted from its own address bytes (* 0.004).
// Brushes (vtable slot 8 = 9) are skipped.
void XB_THISCALL CM_DrawWorldTreeDebug(uint8_t* manager);
// 0x002e4940 (dead: no caller; trace work in EAX; start, end on the stack): checkcount bumped, then
// CM_SightTraceRayThroughWorldTrees for ray work (+0xfc set), else CM_SightTraceThroughWorldTrees.
uint8_t XB_REGS1 XB_CDECL CM_SightTraceThroughWorld(uint8_t* tw, const float* start, const float* end);
// 0x002da080 (dead: no caller; start in EDI, end in ESI): a point sight trace - a zeroed 0x170-byte trace work
// with start (+0), end (+0x10), their bounds (+0x20/+0x30), a zero box (+0x40/+0x50, the w lanes = checkcount),
// delta (+0xe0) and its squared length (+0xf0), contents 0x280d003 (+0xf8), fraction 1 (+0x120), through
// CM_SightTraceRayThroughWorldTrees. Returns 1 when blocked.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL CM_SightTraceRay(const float* start, const float* end);
extern "C" void CM_SightTraceRay_entry(void);

}  // namespace game
