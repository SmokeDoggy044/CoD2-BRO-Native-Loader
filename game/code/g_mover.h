#pragma once

// Game module: movers (script movers, doors) and falling entities.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x0024c3b0 (entity in ESI): sets a script mover up: the "light"/"color" spawn variables
// packed into an RGBA (+0xa8) when either is set, entity type 7, the trajectory and angles from
// the current origin/angles, flag 0x8000.
extern "C" PLATFORM_HIDDEN void XB_CDECL InitScriptMover(uint8_t* ent);
extern "C" void InitScriptMover_entry(void);
// 0x0024c6f0: a door's first think: thinks again in 100 ms; a team master picks its think
// (0x19, 0x1b rotating, 0x1a with spawn flag 8) and hands its +0x3b4 to the team.
void XB_CDECL Think_InitDoor(uint8_t* ent);
// 0x0024c800: think: drops the entity from 1000 units above (gravity trajectory from
// level.time - 50) and frees it after 30 s (think 8).
void XB_CDECL Artillery_Drop(uint8_t* ent);

// 0x0024fb10 (aiming in EAX, turret in ECX): the turret info's (+0x24c) aim state (+1: 0 idle,
// 2 turning, 1 on target; "turretstatechange" when leaving or entering 0) from the angle
// deltas to `angles` (pitch, yaw); then +2 bit 0x40 and the aim point into +0x10.
void XB_REGS3 XB_CDECL Turret_UpdateAimState(uint32_t aiming, uint32_t unused_edx, uint8_t* ent,
                                             const float* aim_point, const float* angles);
// 0x0024fcc0 (turret in EDI): a new random aim offset (+0x68) every 4.5..7.5 s, then turns
// toward it (Turret_UpdateTargetAngles with AI rates).
extern "C" PLATFORM_HIDDEN void XB_CDECL Turret_UpdateAimWithJitter(uint8_t* ent);
extern "C" void Turret_UpdateAimWithJitter_entry(void);
// 0x0024fd90 (turret in EAX): clears info +2 bits 0x48 and +0xc, state idle.
void XB_REGS1 XB_CDECL Turret_SetIdle(uint8_t* ent);

}  // namespace game
