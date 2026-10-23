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

}  // namespace game
