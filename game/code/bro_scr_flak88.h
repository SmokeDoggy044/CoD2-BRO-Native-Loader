#pragma once

// Compiled level-script flak 88 thinker (levelscript module). The thread reaches script waits: class
// thread_switch, kept out of kFunctions until script threads run as coroutines. Names are ours.

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00162960 (dead: no callers, pointers or immediates): the flak 88 sibling of BroScr_FlakPanzerThink.
// The type is string field 0xd1c52a4f (no model test); a BroScr_FlakDeathThink thread (with true);
// the targets (only when the flak has a target): start triggers (BroScr_FlakWaitStartTrigger threads,
// then a wait for "start_turret"), script models (BroScr_PlantBombObjective threads), the rest the crew
// spawners. With every spawned guy alive: model "xmodel/turret_flak88" for type "controlled", the level
// flags reset, the flak's setup function, per guy the crew think / BroScr_FlakStopMovingOnDeath /
// BroScr_FlakWaitDeath threads; then while manned: with the level flag clear, guns in hand, "preidle" /
// "turn" (first round) / else a BroScr_FlakGunnerFireAnim thread, a wait for "fire_turret", "fire"
// ("flak88_fire" sound for script_noteworthy "fake", else GScr_Vehicle_FireWeapon), the
// BroScr_FlakCrewAnim threads and a wait for "flak_anim_done<n>"; else guns on the back, 1 s.
void XB_CDECL BroScr_Flak88Think(uint32_t ent, uint32_t flaknum);

}  // namespace game
