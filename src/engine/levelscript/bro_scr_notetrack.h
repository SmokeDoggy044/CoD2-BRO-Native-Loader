#pragma once

// Compiled level-script anim notetrack commands (BroScr_AnimNotetrackCommands): create / delete
// a script model at a tag, drop the gun. `params` is the notetrack's parameter hash map.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x0015ad70: spawns a script_model and appends it to the actor's model array (field 0xef260d98);
// GScr_SetModel(actor, params["create model"], 0); the model goes to the origin and angles of the
// tag params[0x0cf2dc5b] on the actor. The original leaves index * 4 in the actor's argument slot.
void XB_CDECL BroScr_CreateModel(uint32_t actor, uint32_t params);
// 0x0015afe0: prints "Tried to remove a model with delete model before it was create model'd on
// guy: " + animname when the array is not defined; then for each model: params[0xdec84f52] set:
// GScr_RadiusDamage(its origin, 350, 700, 50) (also an unused AnglesToForward and GetOrigin);
// else params[0xe6f61fe4] set: the BroScr_ExploderIfSet thread on the level entity with it; then
// GScr_Delete(model).
void XB_CDECL BroScr_DeleteModel(uint32_t actor, uint32_t params);
// 0x0015b3b0: unless the actor's dropped gun (handle field 0x4225d5c5) is set: spawns the actor's
// weapon (classname of ScrActor_GetInt_f0) as the field's entity, at the origin and angles of tag
// params[0x1dc47] on `target`; linked there with zero offsets when target is another entity, else
// a script_origin at the gun and thread 0x0011fbf0 (level entity, gun, origin). Then
// BroScr_PutGunInHand(actor, 0x0036b1a5) and ScrActor_SetInt_1378(actor, 0).
void XB_CDECL BroScr_NotetrackDropGun(uint32_t target, uint32_t actor, uint32_t params);

}  // namespace game
