#pragma once

// Compiled level-script helpers over script arrays (levelscript module). The arrays are DynArray
// {data, capacity, count} blocks; by-value arrays are freed by the callee. An element read at or
// past the count first resizes the array (DynArray_Resize(array, i + 1, i + 4)). Names are ours
// (from the bodies).

#include <cstddef>
#include <cstdint>

#include "bro_scr_util.h"
#include "compiler.h"

namespace game {

// The inlined element access: &data[i], resizing the array first when i >= count. Not a title function.
uint32_t* BroScr_ArrayElement(uint32_t* array, uint32_t i);
// The inlined erase of element `index` (DynArray_Erase 0x004afb70 when it is inside the array).
void BroScr_ArrayEraseAt(uint32_t* array, uint32_t index);

// 0x00135470 (dead): args = {out, array}: *out = a random element (Scr_RandomInt(count)); returns out.
BRO_SCR_ARGS_DECL(BroScr_RandomArrayElement)
// 0x001354f0 / 0x00135590: args = {array of actors}: GScr_ActorSetFlag2000000(actor, 0 / 1) on each.
BRO_SCR_ARGS_DECL(BroScr_ActorArray_ClearFlag2000000)
BRO_SCR_ARGS_DECL(BroScr_ActorArray_SetFlag2000000)
// 0x00149ab0: removes the first `value` from the entity's array field 0xe104fd5c.
void XB_CDECL BroScr_EntArrayRemove_he104fd5c(uint32_t ent, uint32_t value);
// 0x001499a0 (dead): each entity of the array field 0xe2a39a33: notify 0x78dbe561, then deleted.
void XB_CDECL BroScr_NotifyRiders(uint32_t ent);
// 0x0013f100: appends `value` unless the array holds it already; a one-element array whose element
// is no valid entity has it replaced.
void XB_CDECL BroScr_ArrayAddUnique(uint32_t* array, uint32_t value);
// 0x0014ed10 (dead) / 0x0014ed70: a living `ent` (health > 0) added to (BroScr_ArrayAddUnique) /
// removed from (BroScr_ArrayRemoveValue on a copy) the owner's array field 0x4ced49f6.
void XB_CDECL BroScr_QueueAdd_h4ced49f6(uint32_t owner, uint32_t ent);
void XB_CDECL BroScr_QueueRemove_h4ced49f6(uint32_t owner, uint32_t ent);
// 0x0014e8c0: *out = whether the array field 0x4ced49f6 is defined; when its first element is the
// level block's +0x6ec entity (valid), BroScr_ArrayRotateFirstToBack on a copy. Returns out.
uint8_t* XB_CDECL BroScr_RotateQueue_h4ced49f6(uint8_t* out, uint32_t ent);
// 0x0013ee80: args = {list, value, array by value}: array[0] = value, then the list's elements other
// than `value` appended to the array, which is freed (the result is lost).
BRO_SCR_ARGS_DECL(BroScr_ArrayRemoveValue)
// 0x0013ef90: args = {list, unused, array by value}: the list's elements 1.. then element 0 written to
// the array from index 0 (a list of one element: copied as is); the array is freed.
BRO_SCR_ARGS_DECL(BroScr_ArrayRotateFirstToBack)

}  // namespace game
