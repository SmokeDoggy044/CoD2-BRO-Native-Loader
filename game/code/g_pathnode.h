#pragma once

// Path node trees (the spatial tree over the path nodes) and the node / stance helpers next to them.

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "script_string.h"

namespace game {

// 0x002a9d70 (best in EDI, node in ESI, count in EBX): reservoir pick among the deepest nodes
// (depth +0x5e): a deeper node restarts the pick, an equal one replaces it with chance 1/count.
extern "C" PLATFORM_HIDDEN void XB_CDECL PathNodeTree_ReservoirPickDeepest(uint32_t* count, const uint8_t* node,
                                                                           uint32_t* best);
extern "C" void PathNodeTree_ReservoirPickDeepest_entry(void);
// 0x002a9db0 (thiscall; stack node base, cursor; callee pops): relinks a loaded tree from its
// cursor (0x10 per node): split nodes (+0 >= 0) get their two children (+8, +0xc), leaves their
// node list (+8, an index into the base); returns the node.
uint32_t* XB_THISCALL PathNodeTree_Link_r(uint32_t ecx, uint32_t base, uint32_t** cursor);
// 0x002a9f90 (ECX node): the stances the node allows (bits 1 stand, 2 crouch, 4 prone) from its
// disallow flags (+0x46: 4 stand, 2 crouch, 1 prone).
uint32_t XB_THISCALL Actor_GetAllowedStanceMask(const uint8_t* node);
// 0x002ad770 (blend in ESI, time in ECX; XMM0 result): the prone blend {+4 start, +8 duration,
// negative = out}: 0..1 into prone, 1..0 out; a finished blend is cleared (+4 out, +8 in).
extern "C" PLATFORM_HIDDEN float XB_CDECL Actor_GetProneBlend(uint32_t time, uint32_t* blend);
extern "C" void Actor_GetProneBlend_entry(void);
// 0x002ad810 (blend in EDX, time in ESI): 0 without a running blend (+4) or with +0 set, or when
// a blend out has ended (start cleared); else 1 (an ended blend in clears its duration).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL ProneBlend_Update(uint32_t* blend, int32_t time);
extern "C" void ProneBlend_Update_entry(void);

// Path data (world): +8 node blocks (one per zone: +4 node count, +0xc script key count, +0x10 nodes of 0x80,
// +0x18 node tree, +0x20 script keys of 0xc2, +0x28 visibility bit rows), +0x194 info (+8 sorted count,
// +0xc block count, +0x14 sorted node handles). Node handle: bits 9-15 block + 1, bits 0-8 node.

// 0x002a9e00 (thiscall: ECX path data, passed on; stack tree; callee pops): walks a node tree for the
// cylinder query at 0x00929b24 (origin, radius, radius^2, half height^2, type mask, out list of
// {node, dist^2}, max, count): splits by axis, leaves add nodes within radius/height whose type is in the mask.
void XB_THISCALL PathNode_ListInCylinder_r(uint8_t* world, const uint32_t* tree);
// 0x002b0600 (EBX claimer, stack path data; callee pops): clears every node claim (+4) held by the claimer.
extern "C" PLATFORM_HIDDEN void XB_CDECL PathNode_ReleaseClaimsBy(uint32_t claimer, uint8_t* world);
extern "C" void PathNode_ReleaseClaimsBy_entry(void);
// 0x002b0670 (EAX handle; stack path data, type mask; callee pops): the next node after the handle whose
// type (+0x28) is in the mask, or null; later blocks are scanned with the first block's node count.
uint8_t* XB_REGS1 XB_STDCALL PathNode_Next(const uint16_t* handle, uint8_t* world, uint32_t mask);
// 0x002b0760 (stdcall path data, type mask): the first node whose type is in the mask, or null.
uint8_t* XB_STDCALL PathNode_First(uint8_t* world, uint32_t mask);
// 0x002b07e0 (thiscall path data, handle): the handle's index in the sorted list, or -1.
int32_t XB_THISCALL PathNode_FindSortedIndex(uint8_t* world, const uint16_t* handle);
// 0x002b0820 (ECX origin, EDX entity or null, EBX path data): the nearest sorted node in the entity's zone
// (+0x5c; zone 0 without an entity), or null.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL Path_FindNearestNodeInList(const float* origin, uint8_t* ent,
                                                                      uint8_t* world);
extern "C" void Path_FindNearestNodeInList_entry(void);
// 0x002b0b90 (stdcall path data): clears every node's claim time (+8) and sets +0x68 to -2.
void XB_STDCALL PathNode_ResetAllRuntimeState(uint8_t* world);
// 0x002b0c10 (EAX node, ECX link of 12 bytes): bumps the link's disable count (+2); the first disable
// swaps the link with the node's last active link (+0x64 links, +0x24 active count, decremented).
void XB_REGS3 XB_CDECL PathNode_DisableLink(uint8_t* node, uint32_t unused_edx, uint8_t* link);
// 0x002b0c70 (EAX handle, EDX path data, stack handle; callee pops): 1 when the visibility bits of the
// lower block (+0x28 rows, 8 bytes per block) have the pair's bit set, else 0.
uint32_t XB_REGS2 XB_STDCALL PathNode_CanSee_2(const uint16_t* from, uint8_t* world, const uint16_t* to);
// 0x002b0dd0 (EAX path data, stack block index; callee pops): every node of the block gets its active link
// count (+0x24) back from +0x62 and its links' runtime words (+4..+7) cleared.
void XB_REGS1 XB_STDCALL PathNodeBlock_ResetLinkState(uint8_t* world, uint32_t index);
// 0x002b0e80 (EAX path data, ECX block index): hands the block's script keys (script_delay, script_mg42,
// script_fallback, script_waittill/chain/door) to the game import setters (+0x824 float, +0x81c int,
// +0x814 script string) by node id and lowercase key hash; _color is skipped.
void XB_REGS3 XB_CDECL PathNode_ParseScriptKeys(uint8_t* world, uint32_t unused_edx, uint32_t index);
// 0x002b1060 (ECX origin, EDI path data; stack radius, half height, out list, max, type mask; callee pops):
// fills the cylinder query and walks every block's tree; returns the number of nodes listed.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL PathNode_ListInCylinder(const uint32_t* origin, uint8_t* world,
                                                                     float radius, float half_height, uint32_t list,
                                                                     uint32_t max, uint32_t mask);
extern "C" void PathNode_ListInCylinder_entry(void);
// 0x002b1190 (EDX object; returns it): constructs the path data (resource manager registered in the list
// at 0x009719fc, vtable 0x00832780, blocks and info cleared, flags 0x00986014..16 = 1, 0, 0).
uint8_t* XB_REGS2 XB_CDECL PathData_Construct(uint32_t unused_eax, uint8_t* data);
// 0x002b1210 (EAX node, ECX sentient): 1 when the sentient holds the claim (+4) or both the team's claim
// time (+8 + team * 4, team = sentient +4) and +8 have passed (level_time), else 0.
uint32_t XB_REGS3 XB_CDECL PathNode_IsUnclaimedFor(uint8_t* node, uint32_t unused_edx, uint8_t* sentient);
// 0x002b12a0 (cdecl handle): 1 when a sentient of team mask 4 has the node as its goal (+0x50), else 0.
uint32_t XB_CDECL PathNode_IsSentientClaimed(const uint16_t* handle);

// 0x002b9190 (EAX start index; stack path data, min depth, max depth, reference node; callee pops): from the
// sorted list, the node in the reference's zone nearest the depth range whose claim time (+8) has not passed
// and that no sentient has as its goal; stops at the zone's end or past the range.
uint8_t* XB_REGS1 XB_STDCALL PathNodeTree_ScanUnclaimedInDepthRange(int32_t start, uint8_t* world, int32_t min_depth,
                                                                    int32_t max_depth, uint8_t* ref);
// 0x002b9290 (as above, + sentient): the nearest node outside the range available for the sentient.
uint8_t* XB_REGS1 XB_STDCALL PathNodeTree_ScanAvailableInDepthRange(int32_t start, uint8_t* world, int32_t min_depth,
                                                                    int32_t max_depth, uint8_t* ref,
                                                                    const uint8_t* sentient);
// 0x002b9380 (EAX node, EDX max depth, EDI min depth, ESI path data; stack sentient; callee pops): climbs the
// parents (+0x60) to the max depth (through g_pathNodeBlocks), then on to the first one available for the
// sentient (through ESI) not above the min depth; null when none.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL PathNodeTree_FindAvailableAncestor(uint8_t* node, int32_t max_depth,
                                                                              int32_t min_depth, uint8_t* world,
                                                                              const uint8_t* sentient);
extern "C" void PathNodeTree_FindAvailableAncestor_entry(void);
// 0x002b9470 (EAX root; stack min depth, max depth, sentient; callee pops): among the nodes stored after the root
// in its zone, the deepest available one in the range (ties picked at random); null when a direct child is met.
uint8_t* XB_REGS1 XB_STDCALL PathNodeTree_PickAvailableInSubtree(uint8_t* root, int32_t min_depth, int32_t max_depth,
                                                                 const uint8_t* sentient);
// 0x002b9530 (thiscall path data; stack min depth, max depth, sorted index, sentient; callee pops): reservoir pick
// of the deepest available descendant of the sorted node, recursing into children above the max depth.
uint8_t* XB_THISCALL PathNodeTree_PickAvailableDescendant_r(uint8_t* world, int32_t min_depth, int32_t max_depth,
                                                            int32_t start, const uint8_t* sentient);
// 0x002b9740 (EAX path data, ECX link; stack entity, node; callee pops): takes a record from the free list, fills it
// {node handle, link target} and rings it into the entity's list (+0x410), then PathNode_DisableLink.
void XB_REGS3 XB_STDCALL Path_RecordDisconnectedLink(uint8_t* world, uint32_t unused_edx, uint8_t* link, uint8_t* ent,
                                                     uint8_t* node);

// 0x002b9870 (thiscall path data): resets the 2048 disconnected-link records to one free ring (+4 prev, +6 next).
void XB_THISCALL Path_ResetLinkList2048(uint8_t* world);
// 0x002b99f0 (EDI path data): PathNode_ParseScriptKeys for every present block.
extern "C" PLATFORM_HIDDEN void XB_CDECL PathNode_ParseAllScriptKeys(uint8_t* world);
extern "C" void PathNode_ParseAllScriptKeys_entry(void);
// 0x002b9c00 (EAX key, stack path data, value, out list or null; callee pops): the nodes whose field named by the key
// (targetname +0x30, target +0x38, animscript +0x3c, script_noteworthy +0x34, origin +0x44, angles +0x50,
// radius +0x54, spawnflags +0x2c, type +0x28) equals the value: the count listed (out), else the first handle;
// -1 for an unknown key or none found. An empty key or value with an out list lists every node.
int32_t XB_REGS1 XB_STDCALL PathNode_FindByKey(const ScriptString* key, uint8_t* world, const ScriptString* value,
                                               uint32_t* out);
// 0x002ba050 (EAX actor, EDX stance limits): copies the node's (actor +0x18) no-stand/no-crouch/no-prone
// flags (+0x2c bits 0x10/8/4) into the limits' +0x46 bits 1/2/4.
void XB_REGS2 XB_CDECL Actor_SetStanceLimitsFromNode(const uint8_t* actor, uint8_t* limits);
// 0x002ba0d0 (ESI node, cdecl team): drops the team's claim (+4 claimer of that team), blocks the team for
// 5 s (+8 + team * 4); an enabled node (+0x26 = 0) also clears +8 and the claims of its two linked nodes (+0x58, +0x5a).
extern "C" PLATFORM_HIDDEN void XB_CDECL PathNode_RevokeClaim(uint8_t* node, uint32_t team);
extern "C" void PathNode_RevokeClaim_entry(void);
// 0x002c0480 (thiscall entity; stack path data, chain name; callee pops): with friendly chains on (0x009550f4),
// sets the entity's chain node (+0x4a) to the sorted node of that targetname; G_Error when the name is only on an
// unsorted node or on none.
void XB_THISCALL G_FriendlyChainNode(uint8_t* ent, uint8_t* world, const ScriptString* name);

}  // namespace game
