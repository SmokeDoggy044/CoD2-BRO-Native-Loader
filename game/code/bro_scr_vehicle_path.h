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

}  // namespace game
