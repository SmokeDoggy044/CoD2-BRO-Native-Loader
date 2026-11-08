#pragma once

// Compiled level-script helpers for vehicles on paths: riders and passengers, bombs, blockers, the
// end of a path (levelscript module, .text). Names are ours (from the bodies).

#include <cstdint>

#include "compiler.h"
#include "qcommon_fs.h"
#include "script_string.h"

namespace game {

// 0x0014b780: deletes each valid bomb (array field 0x0030b1c2) whose "dropped" field is set and 0.
void XB_CDECL BroScr_DeleteUndroppedBombs(uint32_t ent);
// 0x0014e7a0: *out = the first non-zero BroScr_VehicleCheckBlocker(ent, ent, other, range) over the
// valid entities of `list` (range = `ent`'s float field 0xa22fa6ba), else 0. Returns out.
uint32_t* XB_CDECL BroScr_VehicleFindBlocker(uint32_t* out, uint32_t ent, DynArray* list);
// 0x0014e020: whether `other` blocks `vehicle` (z-flattened origins; `vehicle`'s position is the
// entity in its handle field 0xcc043125; `range_bits` is a float). Out of range: `self`'s flags
// 0xcd84c908 and 0x8c05ef86 cleared, *out = 0. In range and other != vehicle: *out = 2 (flag
// 0xcd84c908 set) when a point of the level entity's (+0x6ec) array 0xb0996bdc is nearer the vehicle's
// first 0xb0996bdc point than that entity's float field 0x8166d37c; else *out = 1 (flag 0x8c05ef86 set)
// when the direction from a point of `other`'s array 0x9ecce58a to the vehicle, dotted with the
// vehicle's yaw forward, is below `self`'s float field 0x515816fa. Otherwise both flags cleared,
// *out = 0. Returns out.
uint32_t* XB_CDECL BroScr_VehicleCheckBlocker(uint32_t* out, uint32_t self, uint32_t vehicle, uint32_t other,
                                              uint32_t range_bits);
// 0x00136ab0: clears the root anim, then the flak panzer fire anim of the rider's tag ("tag_gunner",
// "tag_passenger1", "tag_passenger2") through GScr_SetFlaggedAnimKnobAll. Releases `tag`.
void XB_CDECL BroScr_SetRiderAnimForTag(uint32_t ent, ScriptString tag);
// 0x00149b90: the first valid rider (array field 0xe2a39a33) whose string field 0xd91e5c5b equals
// `name`: deleted when it is in the level's list at +0x4ec (and erased there), and erased from the
// riders unless it is the first one. Releases `name`.
void XB_CDECL BroScr_DeleteRiders(uint32_t ent, ScriptString name);
// 0x001508d0: every rider at "tag_passenger<number>" gets notify 0x0d6e32cd and leaves `ent`'s
// riders (and the 0xe104fd5c list, 0x00149ab0).
void XB_CDECL BroScr_HalftrackAttachPassengers(uint32_t ent, int32_t number);
// 0x0014cbf0: the end of a vehicle path: turrets (unless script_noteworthy "noturrets") and undropped
// bombs deleted, bomb list emptied; a dead plane (field 0x4af51227) crashes and is deleted, a live
// vehicle is deleted; else "loop" restarts the path, "no_delete" keeps it, anything else kills it.
void XB_CDECL BroScr_VehicleFollowPath(uint32_t ent);

// 0x0014a430: *out = the follow path index (24-byte array field 0xaeae4e0b) from the current one
// (int field 0x9aec9c1a, 0 when undefined) on: the index of the point nearest the followed entity
// (handle field 0xd952b5cf), once the lag distance (int field 0x96f9883f) exceeds it - the point
// before it (the current index itself, or 0 at the start). Returns out.
uint32_t* XB_CDECL BroScr_FollowUpdateCurrentPos(uint32_t* out, uint32_t ent);
// 0x0014a6d0 (proposed name; was FUN_0014a6d0): when the bool field 0x217d7784 is set and `index` is
// an inner point of the follow path, the point's second vector (+0xc) = the point moved sideways
// (perpendicular to the neighbours' segment in x/y, normalized) by Scr_RandomFloatRange(-32, 32).
void XB_CDECL BroScr_FollowPathOffsetPoint(uint32_t ent, int32_t index);

}  // namespace game
