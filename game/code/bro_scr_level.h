#pragma once

// Compiled level-script helpers over the level block's entity lists and lookups by targetname
// (levelscript module). The level block is the word at 0x00922ee0; its DynArray {data, capacity,
// count} lists sit at +0x784 (vehicles) and +0x790. Names are ours (from the bodies).

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "script_string.h"

namespace game {

// 0x001366b0 / 0x0013a560: *out = the first entity of the level list at +0x784 / +0x790 whose
// targetname equals `name`, else the word at 0x00922e84. Releases `name`; returns out.
uint32_t* XB_CDECL BroScr_FindLevelVehicleByTargetname(uint32_t* out, ScriptString name);
uint32_t* XB_CDECL BroScr_FindInLevelList798ByTargetname(uint32_t* out, ScriptString name);
// 0x001367d0 (dead): the vehicle named `name` gets a copy of `group` in its targets group field
// (ScrField_targets_groupname_2). Releases both.
void XB_CDECL BroScr_SetVehicleTargetsGroupByName(ScriptString name, ScriptString group);
// 0x00136870 (dead): BroScr_SetTargetsGroupAndEnt(the vehicle named `name`, target).
void XB_CDECL BroScr_SetVehicleTargetsByName(ScriptString name, uint32_t target);
// 0x00136900: BroScr_SetTargetsGroupAndEnt(ent, the entity named `name` (GScr_GetEntArray)).
void XB_CDECL BroScr_SetTargetsByTargetname(uint32_t ent, ScriptString name);
// 0x00136990: the vehicle named `name` made unusable (low byte of `unusable` set) or usable.
void XB_CDECL BroScr_SetVehicleUsableByName(ScriptString name, uint32_t unusable);
// 0x00136a20 (dead): the vehicle's can_shoot_low byte = the low byte of `value`.
void XB_CDECL BroScr_SetVehicleCanShootLowByName(ScriptString name, uint32_t value);
// 0x00136c00: 0x0012ff00(out, the vehicle named `name`); returns out.
uint32_t* XB_CDECL BroScr_GetVehicleSecondsSince_h05903940(uint32_t* out, ScriptString name);
// 0x00135ed0: notify 0x78b19de2 to every entity whose targetname is `name` (Scr_GetEntArray).
void XB_CDECL BroScr_NotifyEntsByTargetname(ScriptString name);
// 0x0013be00: when `suffix` is set: notify 0x0c34930d to the entity whose key 0x2d36ed4e is `name`
// + 0x00824304, then to every entity whose key is `name`, then deletes the first one; else only
// the notifies to every entity whose key is `name`. Releases both.
void XB_CDECL BroScr_StopManagerEnts(ScriptString name, ScriptString suffix);
// 0x0013bfc0 / 0x0013c0d0 (dead): BroScr_StopManagerEnts(name, the empty string). 0x0013c040
// (dead): BroScr_StopManagerEnts(name, suffix). The first argument of the last two is unused.
void XB_CDECL BroScr_StopManagerEntsByName(ScriptString name);
void XB_CDECL BroScr_StopManagerEntsByName_2(uint32_t unused, ScriptString name, ScriptString suffix);
void XB_CDECL BroScr_StopManagerEntsByName_3(uint32_t unused, ScriptString name);
// 0x00150c10 (dead): removes `value` from the array field 0xe104fd5c of the list-0x790 entity
// named `name`. 0x00150b70 (dead): appends it.
void XB_CDECL BroScr_InList798ByNameCall(ScriptString name, uint32_t value);
void XB_CDECL BroScr_AddToLevelList798EntArray(ScriptString name, uint32_t value);
// 0x001362b0 (dead): deletes the first valid entity of the list at +0x784 named `name` and erases
// it from the list.
void XB_CDECL BroScr_DeleteFromLevelList784ByTargetname(ScriptString name);

}  // namespace game
