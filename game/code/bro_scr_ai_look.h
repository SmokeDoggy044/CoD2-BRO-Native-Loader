#pragma once

// Shared compiled level scripts (levelscript module, push c 2, part 1): look-at targets and rates,
// per-team cached values, MG burst settings, wounded timing, script-change delays and the enemy eye
// position. Values live in the entities' script key tables (added undefined on first use); "out"
// results are by-value returns through a hidden pointer.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x0019d4e0: looks at a position: the assert strings built and dropped (IsAI called, key
// 0xfa9a8384 touched); unless `interrupt` is 0x00825de8 (or undefined), nothing while the look
// timer (0x644536d0) is still running. Look type 0x42f377f3 = 0xd915114e, the 0xb217897f vector =
// pos, timer = now + time x 1000 (as a script int), rate 0x5e1a2a0c = 800 for speed 0xc028584c
// else 1600, then notify 0x758409ea for notify type 0xa3c65328, else 0x189e36ec.
void XB_CDECL BroScr_LookAtPosition(uint32_t ent, uint32_t x, uint32_t y, uint32_t z, float time, uint32_t speed,
                                    uint32_t notify_type, uint32_t interrupt);
// 0x0019d820 (dead): caches the 0xad8f34a0 key in 0x00922fa0 and, by the entity's team hash, four
// anim-script object words (+0x1c / +0xf8 / +0xd0 / +0x74) into 0x00922fa4..0x00922fbc.
void XB_CDECL BroScr_CachePerTeamValues(uint32_t ent);
// 0x0019e360: the actor's look rates (GScr_ActorSetLookRates): 90/90 for anim script 0x22a9df44
// with field 0x81f1fdcb set and for some move/pose/speed combinations, else the 0x9a44df1e rate and
// 0; written back to key 0x9a44df1e.
void XB_CDECL BroScr_UpdateLookRates(uint32_t ent, uint32_t move, uint32_t pose, uint32_t speed, uint32_t stance,
                                     uint32_t arg5, uint32_t notify_type);
// 0x0019f550: MG burst settings: delay min / range and burst min / range from the script_delay_* /
// script_burst_* fields (range = max - min), else BroScr_MgBurstModeFactor of "delay",
// "delay_range", "burst", "burst_range".
void XB_CDECL BroScr_MgBurstSettings(uint32_t unused, uint32_t ent, uint32_t* delay_min, uint32_t* delay_range,
                                     uint32_t* burst_min, uint32_t* burst_range);
// 0x0019fa40: after pain: the wounded timers (0x32292cf8 = now + 5000; 0x8ef2ad7c from the normal
// health, 0 above 0.99), the wounded anim set (0xfa79d674) by pain anim and the 0x32222d6e value
// blended toward time / health x 5 (capped at 1).
void XB_CDECL BroScr_UpdateWoundedTime(uint32_t ent, uint32_t anim, int32_t time);
// 0x001a0940: looks at an entity (type 0x9b751a35 for sentients, else 0xccdbaaf9; handle field
// 0xeeadf3f2 = target); the timer, rate and notify as BroScr_LookAtPosition (interrupt 0xcd18b019).
void XB_CDECL BroScr_LookAtEntity(uint32_t ent, uint32_t target, float time, uint32_t speed, uint32_t notify_type,
                                  uint32_t interrupt);
// 0x001a0b80 (FUN_001a0b80, dead; proposed BroScr_LookAtPositionNoType): BroScr_LookAtPosition
// without the asserts and the look type; timer = now + int(time x 1000); interrupt 0x71b01aea.
void XB_CDECL BroScr_LookAtPositionNoType(uint32_t ent, uint32_t x, uint32_t y, uint32_t z, float time,
                                          uint32_t speed, uint32_t notify_type, uint32_t interrupt);
// 0x001a0d30 (no function in Ghidra, dead; proposed BroScr_SetLookOverrideFields): keys 0x81f1fdcb
// = a and 0x3fd9be84 = b (added as undefined handles).
void XB_CDECL BroScr_SetLookOverrideFields(uint32_t ent, uint32_t a, uint32_t b);
// 0x001a1290: on a script change pushes the combat delay (0x900ed884) out to now + +0x18c +
// random(+0x8): for 15 scripts always, for 10 others never, else when the +0x3c reference is valid.
void XB_CDECL BroScr_DelayTimerOnScriptChange(uint32_t ent, uint32_t script);
// 0x001a1680: the enemy's eye (stored in 0x8b05303f, time in 0x60a41c54) while the enemy is
// valid; without one the stored eye for 3 s, then the entity's own eye + 0.? x its +0x108 velocity.
uint32_t* XB_CDECL BroScr_GetEnemyEyePos(uint32_t* out, uint32_t ent);

}  // namespace game
