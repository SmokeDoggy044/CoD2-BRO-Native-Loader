#pragma once

// Compiled level-script threads of the shared scripts (levelscript module, .text 0x00131180 -
// 0x00134e60): plane crash moves, explosion sounds, vehicle / plane mg42 fire loops, tank dead
// guys, death-physics launches, flak sound blends, engine smoke trails, shell shock, tank stun,
// debug loops. Every one but BroScr_PlayerInsideEntZone waits (Scr_Wait*, Scr_WaitTill*,
// BroScr_WaitFrame): class thread_switch, kept out of kFunctions until script threads run as
// coroutines. Names are ours (from the bodies); "death" is hash 0x06730ae8, the player handle the
// word at 0x00922eec.

#include <cstdint>

#include "compiler.h"
#include "scr_access.h"
#include "script_string.h"

namespace game {

// 0x00131180 (FUN_00131180; proposed BroScr_PlayerInsideEntZone; registered): whether the player
// is behind `ent` along its vector key 0x6ff72745 (dot(v, ent - player) > 0) and between the planes
// at `near_dist` and `far_dist` along its vector key 0xe2e8d8a5 (in front of the first, behind the
// second). The original writes the three dot products into the ent / near / far argument slots.
bool* XB_CDECL BroScr_PlayerInsideEntZone(bool* out, uint32_t ent, float near_dist, float far_dist);

// 0x001315b0: each frame reads the vector key 0xe2e8d8a5, the origin twice, the angles and their
// forward vector (results unused: a debug draw compiled out).
void XB_CDECL BroScr_DebugForwardStub(uint32_t ent);
// 0x00131720 / 0x00131d70: each frame adds the distance moved to a counter (from 0.001); while the
// vehicle speed (slot 0x00922b38) is above 1, every 128 units plays the string key 0x1cc418a4 at
// the origin. Never returns.
void XB_CDECL BroScr_StukaPlaneCrashMove(uint32_t ent);
void XB_CDECL BroScr_P47PlaneCrashMove(uint32_t ent);
// 0x001318b0 / 0x00131f00: the string key 0xe6f61fa7 at the origin three times, 0.2 s and 0.4 s
// apart.
void XB_CDECL BroScr_PlayExplodeSounds(uint32_t ent);
void XB_CDECL BroScr_PlayExplodeSounds_2(uint32_t ent);
// 0x00131c30: notifies and ends on "stop mg42s", ends on death; while alive: unless `keep_target`
// both turrets target the player; then every 0.05 s turret call 272 on the left (0xef6522f9) and
// right (0xeead8089) turret.
void XB_CDECL BroScr_VehicleMg42Fire(uint32_t ent, bool keep_target);
// 0x00132280: ends on "stop firing"; ends the thread in key 0x65637ee2 and stores its own there;
// then as BroScr_VehicleMg42Fire (targets when `keep_target` is the undefined bool).
void XB_CDECL BroScr_PlaneTurretsFire(uint32_t ent, uint8_t keep_target);
// 0x00132490: one record (clamped to the last of four) of a dead-guy table: after its delay, a
// real spawn (BroScr_TankDeadGuyPlayAnim thread) when the player is at least 3500 away and the
// record is not model-only, else a "script_origin" and a "script_model" 128 below `pos` playing
// the record's anim (anim tree "tankdeadguys"), deleted after 20 s.
struct TankDeadGuyRecord {
    uint32_t spawner;  // +0x0
    int32_t anim;      // +0x4: the spawned guy's anim (as a float to the thread)
    uint32_t model_anim;  // +0x8: set from 0x008cd1ac / 0x008cd1a0 / 0x008cd194 for records 0-2
    uint32_t model_only;  // +0xc
    float delay;          // +0x10
};
struct TankDeadGuyTable {
    TankDeadGuyRecord record[4];
};
void XB_CDECL BroScr_SpawnTankDeadGuys(uint32_t self, uint32_t unused, TankDeadGuyTable table, uint32_t index,
                                       uint32_t unused2, ScrVec3 pos, ScrVec3 angles);
// 0x00132ab0: BroScr_NotifyLevelOnDeath(self, ent) thread; ends on `ent`'s death; waits for
// 0x0032304c on `ent`; then 0x92b8c65f on the level, `self` and `ent`.
void XB_CDECL BroScr_PauseUnpauseWait(uint32_t self, uint32_t ent);
// 0x00132b40 / 0x00132bb0 / 0x00132c80 / 0x00132d50: BroScr_DeathPhysicsLaunch_2 with an undefined
// force, the tag's origin as force, or the given force; delay the word at 0x00922ea4 (the second
// from-tag form takes it as an argument).
void XB_CDECL BroScr_DeathPhysicsLaunchThread(uint32_t ent, ScrVec3 angles, uint32_t launch_arg);
void XB_CDECL BroScr_DeathPhysicsLaunch_2FromTag(uint32_t ent, ScrVec3 angles, uint32_t launch_arg, ScriptString tag);
void XB_CDECL BroScr_DeathPhysicsLaunch_2FromTag_2(uint32_t ent, ScrVec3 angles, uint32_t launch_arg,
                                                   ScriptString tag, ScrVec3 unused, float delay);
void XB_CDECL BroScr_DeathPhysicsLaunch_2Call(uint32_t ent, ScrVec3 angles, uint32_t launch_arg, ScrVec3 force);
// 0x001336e0: a "sound_blend" at the origin, ends on the level's "flak died with flaknum <n>",
// BroScr_FlakWaitDeath_2 thread on it; then forever: waits for "<name>on", blends 0 -> 1 in 0.1
// steps every 0.05 s, waits for "<name>off", blends back (the blend calls themselves are gone).
void XB_CDECL BroScr_FlakSoundBlend(uint32_t ent, ScriptString name, int32_t flaknum);
// 0x00133950: ends on death; a "script_origin" at "tag_engine" (key 0x4c9e9556) linked to the
// vehicle; for up to 10 s while faster than 100: every 48 units moved plays
// "smk_trail_thick_lg_black_a"; then until the 10 s are up every 0.1-0.4 s.
void XB_CDECL BroScr_EngineSmokeTrail(uint32_t ent);
// 0x00133db0: the player's bool key 0x2eecefb8 and sentient int +0x30 set for `seconds` - 1
// (ends on the level's "player is shell shocked"), then cleared.
void XB_CDECL BroScr_ShellShockWait(uint32_t unused, int32_t seconds);
// 0x00134150: as BroScr_StukaPlaneCrashMove, but ends when the speed drops to 1 or below.
void XB_CDECL BroScr_MoveSoundEvery128Units(uint32_t ent);
// 0x00134540: ends the thread in key 0xaee44119 when defined and stores its own; then as
// BroScr_PlaneTurretsFire on "stop mg42s" / death.
void XB_CDECL BroScr_VehicleMg42Fire_2(uint32_t ent, uint8_t keep_target);
// 0x001349d0 / 0x00134a30: ends on death; int key 0x8c05ef86 / 0xcd84c908 = 0; waits forever.
void XB_CDECL BroScr_DebugStub_h8c05ef86(uint32_t unused, uint32_t ent);
void XB_CDECL BroScr_DebugStub_hcd84c908(uint32_t unused, uint32_t ent);
// 0x00134a90: while alive: int key 0x2f66b4d8 = 0, waits for "damage", key = 1, waits
// `seconds` (0.1 s steps), then `seconds` more, notifies 0xecd3b220.
void XB_CDECL BroScr_TankStunOnDamage(uint32_t ent, int32_t seconds);
// 0x00134c40: ends on "novistarget"; waits for "turret_on_vistarget"; int key 0x4d917ba7 = 1.
void XB_CDECL BroScr_TurretVisTargetWait(uint32_t ent);
// 0x00134cb0: each frame the forward vectors of the angles turned by +/- the float key 0xbe149b8c
// (results unused: a debug draw compiled out).
void XB_CDECL BroScr_DebugForwardStub_2(uint32_t ent);

}  // namespace game
