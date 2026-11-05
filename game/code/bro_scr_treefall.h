#pragma once

// Compiled level-script thread body (levelscript module): the falling tree of the
// "_treefall_callbacked" script. It suspends the script thread (Scr_WaitTill), so it is not in
// kFunctions (class thread_switch in docs/levelscript_classes.csv).

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x001288e0: with `trigger` valid: `other` (invalid: a "script_origin" spawned at the player with
// the player's angles), the entity var 0xe2a210cf "_treefall_callbacked::treefall" on trigger;
// trigger's target (none: done) is the tree, whose own target entity is deleted. A
// "script_vehicle" tree pushes from 64 above its origin; within 128 of the player a "zmaaaga" /
// "aaaa" rumble. A "script_origin" pivot at the tree (origin, angles) plays trigger's script_fxid
// (or "treefall"), faces from the tree toward `other` (yaw only), carries the tree linked through
// three pitch rotations (each waiting for its done notify 0xae6d9d1d); then the tree
// unlinked, pivot and trigger deleted.
void XB_CDECL Bro__treefall_callbacked__treefall(uint32_t trigger, uint32_t other);

}  // namespace game
