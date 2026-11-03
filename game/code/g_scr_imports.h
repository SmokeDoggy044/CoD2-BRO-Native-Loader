#pragma once

// The game import block (0x0095f49c) setup: script builtins, entity/sentient/hud field accessors and engine
// callbacks the level script module calls through it.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00301df0: fills the import block with the objective builtins (+0x15c..+0x194).
void XB_CDECL G_SetupObjectiveScriptImports();
// 0x00302360: fills the import block with the player builtins (+0x684..+0x738).
void XB_CDECL G_SetupPlayerScriptImports();
// 0x00303bb0: fills the import block with the script mover builtins (move/rotate, solid).
void XB_CDECL G_SetupMoverScriptImports();
// 0x003088b0: fills the import block with the vehicle builtins.
void XB_CDECL G_SetupVehicleScriptImports();
// 0x00309030: fills the import block with the actor builtins.
void XB_CDECL G_SetupActorScriptImports();
// 0x003094a0: fills the import block with the entity/hud/sentient field accessors and game builtins; the actor, mover and player sets in between.
void XB_CDECL G_SetupGameScriptImports();
// 0x0030adb0: fills the import block with the level builtins and engine callbacks, the level script imports (+0x800, 0x27 words), the game, objective and vehicle sets, then the ocean wave setters (+0xcf0..).
void XB_CDECL G_SetupLevelScriptImports();

}  // namespace game
