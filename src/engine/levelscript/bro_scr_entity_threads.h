#pragma once

// Compiled level-script threads on entities (levelscript module, .text 0x0019b0d0-0x0019e2d0):
// goal radius restore, turret leave, magic bullet shield, exploder sounds, mover solidity and
// throws, sounds on tags, 30cal anim trees, cover shooting, anim-face waits. Every one waits
// (Scr_Wait*, Scr_WaitTill*): class thread_switch, kept out of kFunctions until script threads
// run as coroutines. Names are ours (from the bodies); "death" is hash 0x06730ae8, 0x07b4e7f7
// ends the anim script.

#include <cstdint>

#include "compiler.h"
#include "script_string.h"

namespace game {

// 0x0019b0d0: ends on death; waits for 0x0032304c; goal radius (sentient float +0x18) = the
// float key 0xe223c6aa; below 32: 400.
void XB_CDECL BroScr_RestoreGoalRadiusOnGoal(uint32_t ai);
// 0x0019b190: ends on death; the turret (key 0xaf6fd78f) is replaced by the word at 0x00922e84;
// leaves the turret, byte +0x1374 = 0, goal radius from key 0xe223c6aa; with a valid turret and a
// target: radius 8 to the target's node (by targetname), waits 2 s, radius 384.
void XB_CDECL BroScr_LeaveTurretToNode(uint32_t ai);
// 0x0019bab0: waits for `ai`'s death; the count key 0x97c82b3d of `group` - 1; at 0 or below
// notifies 0xddc0d6b6.
void XB_CDECL BroScr_DecrementAiSizeOnDeath(uint32_t group, uint32_t ai);
// 0x0019bb60: ends on death; waits for "stop magic bullet shield"; bool key 0x83ccaf2a false,
// takes damage again with `health`.
void XB_CDECL BroScr_MagicBulletShieldWait(uint32_t ent, uint32_t health);
// 0x0019bbf0: ends on "stop magic bullet shield" / death; BroScr_MagicBulletShieldWait thread
// (restore_health: the health when undefined); bool key 0x83ccaf2a true; forever: health
// `health` (default 100000000), max health health * scale (default 1), waits for damage
// (0x0039894e), the same again, int +0x30 = 1; without damage taken off already: no damage for
// `time` s (default 5), then int +0x30 = 0.
[[noreturn]] void XB_CDECL BroScr_MagicBulletShield(uint32_t ent, uint32_t health, float time,
                                                     uint32_t restore_health, uint32_t scale);
// 0x0019bec0: script_delay wait; BroScr_PlaySoundOnTempOrigin_4 on the level entity with the
// string key 0x81047339 at the entity's origin.
void XB_CDECL BroScr_ExploderPlaySound(uint32_t ent);
// 0x0019bf80 / 0x0019c2c0: script_delay (0 when undefined, min + random(max) with both
// script_delay_min / max); _2 also ends on the level's script_waittill notify. After the delay:
// the sound (string key 0xec02fc10) at the direction to the target (by targetname) when there is
// one, else on the entity. `unused` is not read.
void XB_CDECL BroScr_ExploderSoundFx(uint32_t unused, uint32_t ent);
void XB_CDECL BroScr_ExploderSoundFx_2(uint32_t unused, uint32_t ent);
// 0x0019c660: script_delay wait; connects paths (spawnflag 1); deletes the entity.
void XB_CDECL BroScr_ScriptDelayConnectPaths(uint32_t ent);
// 0x0019c6c0: script_delay wait; clears entity flag 0x400; not solid when script_noteworthy is
// "notsolid", else solid; connects paths (spawnflag 1).
void XB_CDECL BroScr_NotSolidAfterDelay(uint32_t ent);
// 0x0019c800: script_delay wait; without a valid target (by targetname): deletes itself. Else
// not solid for "notsolid_throw", clears flag 0x400, rotates and throws itself toward the target
// (speed 12), deletes the target, after 6 s itself.
void XB_CDECL BroScr_NotSolidThrow(uint32_t ent);
// 0x0019ca70 / 0x0019cc40: a script_origin linked to the entity (at tag `tag` when set, else at
// its origin and angles), deleted with it (BroScr_DeleteOnOtherDeath); waits for "sounddone" /
// "stop sound" + alias on the origin / the entity, deletes the origin. No sound call is left in
// the compiled scripts. The first does nothing for a dead sentient.
void XB_CDECL BroScr_PlaySoundOnTag(uint32_t ent, ScriptString alias, ScriptString tag);
void XB_CDECL BroScr_PlayLoopSoundOnTag(uint32_t ent, ScriptString alias, ScriptString tag);
// 0x0019ce50: *out = the entity died (before or while waiting for "finished spawning" unless the
// field finished_spawning is defined).
uint8_t* XB_CDECL BroScr_WaitFinishedSpawning(uint8_t* out, uint32_t ent);
// 0x0019cf40: ends with the anim script; BroScr_TurretSetAnimKnob_8cf684 thread on the turret;
// int +0x1370 and flagged anim 0x008cf0d8 (rate 0), waits for its flag.
void XB_CDECL BroScr_TurretPlayAnimDone(uint32_t ent, uint32_t turret);
// 0x0019cfc0 / 0x0019d180: anim tree "30cal" on the turret with a user count (int key 0xabc1df0a):
// + 1, waits for 0x07b4e7f7, - 1; the last user clears the key and the anim tree.
void XB_CDECL BroScr_Mg30calAnimTree(uint32_t turret);
void XB_CDECL BroScr_Mg30calAnimTree_2(uint32_t turret);
// 0x0019d460: ends on 0x5e12d3d2 / 0x07b4e7f7; after 3 s: int key 0xc786fa48 = 0, notifies
// 0x5e12d3d2.
void XB_CDECL BroScr_ReloadLauncherTimeout(uint32_t ent);
// 0x0019d7b0: BroScr_RelayNotifyWhileBothAlive thread; ends on either's death or 0x1e4aecac;
// waits for 0x07b4e7f7, notifies 0x1e4aecac.
void XB_CDECL BroScr_OrientToEntityKillWatch(uint32_t ent, uint32_t other);
// 0x0019da60: ends on `end`, 0x07b4e7f7, death; `count` times: waits for the 0x54e3b2b6 note
// 0x0032aa25, shoots at `target`, int key 0x0feddbea - 1. Then notifies `end`.
void XB_CDECL BroScr_CoverShootOnNotify(uint32_t ent, uint32_t end, int32_t count, uint32_t target);
// 0x0019e120: ends on death / 0xad9b8a7b; waits for 0x91935ca7; int key 0xaa35a0b5 = 1,
// notifies 0x1c87198d.
void XB_CDECL BroScr_AnimFaceWaitSetFlag(uint32_t ent);
// 0x0019e1a0 / 0x0019e260: end on death / `end`; wait for `notify` / `time` s; string key
// 0x1cc76848 = "notify" / "time"; notify `done`.
void XB_CDECL BroScr_AnimFaceWaitOnNotify(uint32_t ent, ScriptString notify, uint32_t done, uint32_t end);
void XB_CDECL BroScr_AnimFaceWaitOnTime(uint32_t ent, float time, uint32_t done, uint32_t end);

}  // namespace game
