#pragma once

// Compiled level-script vehicle rider anims (levelscript module). The thread reaches script waits: class
// thread_switch, kept out of kFunctions until script threads run as coroutines. Names are ours.

#include <cstdint>

#include "compiler.h"
#include "script_string.h"

namespace game {

// 0x0016ace0 (thread of BroScr_VehicleGetPassengers / _DroneVehicleRiders): the rider's anims at the vehicle's
// `tag` (all notify 0x54e3b2b6, level map +0x19c by animname), ending with the rider's 0x0d6e32cd /
// 0x78dbe561 / death and the vehicle's death / 0x78dbe561; none for the level's +0x7f8 entity.
// Passengers (not "tag_gunner" / "tag_driver") get a BroScr_AnimScriptedRestoreAnimname thread and, while
// the vehicle's alert_state is not 0x00922ef0, anims by state (0x008cd188 / 0x008cd18c: 30% of the time,
// not twice in a row, a BroScr_DroneShoot); else the gunner idles, the others play stopped / moving and
// turn ("right" / "left" field 0x59cb9dd1) anims. Waits 0.05 s between rounds; never returns otherwise.
void XB_CDECL BroScr_VehicleRidersThink(uint32_t vehicle, uint32_t rider, ScriptString tag);

}  // namespace game
