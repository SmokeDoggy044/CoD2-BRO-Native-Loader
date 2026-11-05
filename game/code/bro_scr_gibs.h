#pragma once

// Compiled level-script gib spawners (levelscript module, .text): stone and wood debris thrown off
// a breaking entity. Names are ours (from the bodies).

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x001455e0 (dead): 1-2 (Scr_RandomInt(1) + 1) "script_model" gibs: model "xmodel/gib_stone1" (with
// `stone_only`, else "xmodel/gib_brick" when Scr_RandomInt(100) > 50), placed at `ent`'s origin plus
// Scr_RandomFloat(1) - 0.5 per axis, each with a BroScr_StoneGibLaunch and two BroScr_SpawnConcreteGibs
// threads (functors {0x007f49e0, thread, gib, a2, a3, a4}); then `ent` is deleted and the local gib
// array freed.
void XB_CDECL BroScr_SpawnStoneGibs(uint32_t ent, uint32_t a2, uint32_t a3, uint32_t a4, bool stone_only);
// Not registered (thread_switch, docs/levelscript_classes.csv): GScr_Spawn runs the new entity's script
// at once and Scr_CreateThread / Scr_StartThread start threads, which switch script threads and keep a
// block of the caller's stack in host memory. The C is kept (reviewed against the disassembly, not
// compared) until script threads are coroutines.
// 0x00145e80: queues notify 0x06730ae8 on `ent`; then Scr_RandomIntRange(3, 4) times, while the level's
// gib count (+0x314) is not above the limit at 0x008cd190: a "script_model" in `ent`'s zone (count + 1),
// model "wood_gib_a" (Scr_RandomInt(100) > 66) / "_b" (> 33) / "_c", at `ent`'s origin plus
// Scr_RandomFloat(1) - 0.5 per axis, with BroScr_WoodGibLaunch and BroScr_SpawnWoodGibsSmall threads
// (functors {0x007f49e0, thread, gib, a2, a3, a4}). With `init_target`: when `ent`'s target names a
// valid entity, BroScr_WoodGibInit(that entity, ent's origin, ent's targetname) as a started thread.
// Then `ent` is deleted and the local gib array freed.
void XB_CDECL BroScr_SpawnWoodGibs(uint32_t ent, uint32_t a2, uint32_t a3, uint32_t a4, bool init_target);

}  // namespace game
