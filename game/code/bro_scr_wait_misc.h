#pragma once

// Compiled level-script thread bodies (levelscript module), more of the waiting kind: death
// launches with an angle or a tag, player-angle lerp calls, notify-or-timeout waits, anim-reach and
// vehicle-rider counters. They suspend the script thread (directly or through a callee), so they
// are not in kFunctions (class thread_switch in docs/levelscript_classes.csv).

#include <cstdint>

#include "compiler.h"
#include "scr_access.h"
#include "script_string.h"

namespace game {

// 0x00128430: BroScr_DeathPhysicsLaunch with the launch direction from `angles` (pitch negated,
// Scr_AnglesToForward) and the damage after `delay` (0.1 when undefined).
void XB_CDECL BroScr_DeathPhysicsLaunch_2(uint32_t ent, ScrVec3 angles, uint32_t launch_arg, ScrVec3 force, float delay);
// 0x001285e0: BroScr_DeathPhysicsLaunch with an undefined force.
void XB_CDECL BroScr_DeathPhysicsLaunchNoForce(uint32_t ent, uint32_t attacker, uint32_t launch_arg);
// 0x00128640: BroScr_DeathPhysicsLaunch with the force at ent's `tag` (GScr_GetTagOrigin). Releases tag.
void XB_CDECL BroScr_DeathPhysicsLaunchFromTag(uint32_t ent, uint32_t attacker, uint32_t launch_arg, ScriptString tag);
// 0x00128eb0 / 0x00128ef0: BroScr_LerpPlayerAngles / _2 with `release` set (a byte 1 in a stack
// word whose upper bytes are stale).
void XB_CDECL BroScr_LerpPlayerAnglesCall(uint32_t player, float pitch, float yaw, float roll, float time);
void XB_CDECL BroScr_LerpPlayerAnglesCall_2(uint32_t player, float pitch, float yaw, float roll, float time);
// 0x001281c0: ends on the level's `endon` (when set); queues `notify` on the level and ends on it
// (when set); then waits `timeout` seconds of game time, or forever when it is undefined. Releases
// the three strings.
void XB_CDECL BroScr_WaitForNotifyOrTimeout(uint32_t unused0, ScrVec3 unused1, ScriptString unused_string,
                                            ScrVec3 unused2, ScriptString notify, ScriptString endon, float timeout);
// 0x0012f2e0: starts BroScr_AnimReachNotifyOnDeath(level, ent, other), keeps ent's goal radius in
// field 0x25bf26c6 and zeroes it; on 0x0032304c queues 0x2253e8ee on `other`.
void XB_CDECL BroScr_AnimReachGuyThread(uint32_t ent, uint32_t other);
// 0x0012f3b0: after 0.05 s counts ent's field 0x06f63273 up; over 1, a debug print "Too many of same
// animation playing" (a stub that only releases its string).
void XB_CDECL BroScr_LimitSameAnimation(uint32_t ent);
// 0x0012f460: ends on ent's "death"; on `notify` from `object` counts ent's field 0x06f63273 down.
void XB_CDECL BroScr_DecrementCounterOnNotify(uint32_t object, uint32_t ent, uint32_t notify);
// 0x0012fdb0: ends on the rider's 0x9966dcdf; on its death counts the vehicle's field 0xb330b301
// down and stops the rider's scripted anim.
void XB_CDECL BroScr_VehicleRiderDeathWatch(uint32_t vehicle, uint32_t rider);
// 0x0012fe30: starts BroScr_KillWhenHealthLow on the rider; on its death queues 0xa05165ad on
// `vehicle` and unlinks the rider.
void XB_CDECL BroScr_VehicleRiderUnlinkOnDeath(uint32_t vehicle, uint32_t rider);
// 0x0012fe80: ends on the vehicle's 0x996a3647; on the rider's death counts the vehicle's field
// 0xb330b301 down, stops the rider's scripted anim and queues 0x996a3647 on the vehicle.
void XB_CDECL BroScr_VehicleRiderDeathWatch_2(uint32_t vehicle, uint32_t rider);

}  // namespace game
