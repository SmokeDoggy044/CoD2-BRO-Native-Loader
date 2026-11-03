#pragma once

// Path node trees (the spatial tree over the path nodes) and the node / stance helpers next to them.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

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

}  // namespace game
