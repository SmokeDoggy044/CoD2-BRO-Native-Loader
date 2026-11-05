#pragma once

// Compiled level-script drone threads (levelscript module, .text 0x00169b20-0x00172400): the drone's
// anim loop and death fall, and the path spawn loops. Every one waits (Scr_Wait*, Scr_WaitTill*,
// Scr_WaitFrames): class thread_switch, kept out of kFunctions until script threads run as
// coroutines. Names are ours (from the bodies). The level's anim sets are level + 0x744 (animname
// hash -> set key -> anim array), their rates + 0x2a8 (float arrays), the anim roots + 0x7c8.

#include <cstdint>

#include "compiler.h"
#include "qcommon_fs.h"
#include "script_string.h"

namespace game {

// 0x00169b20: ends on 0xf9ed2681 / death; when valid: the anim tree of its animname (level +
// 0x238), then forever a random anim of its set 0x6e28adc5: field 0x2297b461 = the set's rate for
// it (the word at 0x00922ea4 when undefined), SetFlaggedAnimKnobAll(0x4fcdc42e, anim, the set's
// root, 1, 0.2, 1, 1) and a wait for 0x4fcdc42e (arguments 0x199a4, 0, 0).
void XB_CDECL BroScr_DroneAnimLoop(uint32_t drone);
// 0x00169e50: notifies 0xf9ed2681, unlinks the drone and takes it off the level's drone list (+0x770).
// Without a visible model or with `hide`: the gun attached to tag_weapon_right, the drone deleted
// (count +0x508 - 1). Else the anim tree of its animname; within 512 of the player the gun flies off
// (a BroScr_AttachDroneGun thread with a random direction * random distance), else it is attached
// to tag_weapon_right; a random death anim (set 0x0fc1193c when `explode`, else 0xd5cdd358)
// AnimScriptedNoNotify'd (0x0632ef00) and waited for; a body above the ground (trace 8 up to 500
// down, fraction in (0, 1), drop over 8) falls to it in drop * 0.0025 s, one frame at a time;
// then BroScr_DroneDeleteLater.
void XB_CDECL BroScr_DroneMain(uint32_t drone, uint32_t hide, uint32_t explode);
// 0x0016d3b0: ends on 0xf9ed2681 / the path's death; drone bool 0x88260b0d = true. With the path's
// bool 0x19738af8 set and points (field 0x6bd99abb, a vec3 list ending in -1 -1 -1): per point a
// move to it (time = distance / the drone's rate 0x2297b461, / the path's script_speed, or / 175;
// points at time 0 or less skipped) with BroScr_RotateToFaceDirection, then a wait of time - 0.2
// (over 0.2), time (over 0.001) or 0.001. Before each, the path's events (0x384ad510: point
// indices) at the next event index fire its type (0x095a5675): 0xdcc8dc52 / 0xa07fa5bd / 50% of
// 0x18147c6e blow up (RadiusDamage 300, Earthquake, level sound +0x6e8, BroScr_StukaStartNearbyDrones),
// 0x19d45 / 50% of 0xcea3fa90 / 0xcea3fa75 a BroScr_DroneMain(0, 0) thread, 0x0fc14a52
// BroScr_DroneMain(1, 0). At the end a BroScr_DroneMainCall(drone, `flag`) thread.
void XB_CDECL BroScr_DroneFollowSpline(uint32_t drone, uint32_t path, uint32_t flag);
// 0x001706b0: ends on 0x78b19de2; level +0x4dc / +0x2a4 = 0 when +0x4dc is undefined; the path's
// animname = `type`, its min_delay / max_delay / min_death_delay / max_death_delay = delay_min /
// delay_max / p7 / p8, just_delete = p9, using_shrimps = 0. `count` (1 when undefined) times,
// or forever when not above 0: BroScr_DronePathSpawnOne(spawner, path, type, p7, p8, p9), then a
// wait of delay_min + RandomFloat(delay_max) (both defined) or 3 s. Releases `type`.
void XB_CDECL BroScr_DronePathSpawnLoop(uint32_t spawner, uint32_t path, ScriptString type, uint32_t count,
                                        uint32_t delay_min, uint32_t delay_max, uint32_t p7, uint32_t p8, uint32_t p9);
// 0x00171970: the entities with targetname `name`; with the no_shrimps cvar above 0, nothing when
// the first is using_shrimps 0. The first's shrimp sound: freed when using_shrimps, else played on
// the level (sound = its targetname) and kept. Per entity: using_shrimps set: a
// BroScr_DronePathSpawnLoop thread (level, it, its animname, count 0, its min/max delay and death
// delays, just_delete) and a wait of (min_delay + max_delay) / count; else using_shrimps = 1,
// notify 0x78b19de2, shrimp_id = the sound. Frees the list, releases `name`.
void XB_CDECL BroScr_DroneDeathThink(uint32_t level, ScriptString name);
// 0x00171f80: per path: field 0x6bd99abb = 0, bool 0x19738af8 = false, a BroScr_DronePathSpawnLoop
// thread (spawner, path, type, p6..p11), one frame. 0x00172150: valid paths only, each followed by
// a wait of p7 + RandomFloat(p8) (both defined) or 0.5 s instead of the frame. Free `paths`,
// release `type`.
void XB_CDECL BroScr_DronePathsStartAll(uint32_t spawner, DynArray paths, ScriptString type, uint32_t p6, uint32_t p7,
                                        uint32_t p8, uint32_t p9, uint32_t p10, uint32_t p11);
void XB_CDECL BroScr_DronePathsStartAllStaggered(uint32_t spawner, DynArray paths, ScriptString type, uint32_t p6,
                                                 uint32_t p7, uint32_t p8, uint32_t p9, uint32_t p10, uint32_t p11);

}  // namespace game
