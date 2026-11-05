#pragma once

// Compiled level-script threads of the shared scripts (levelscript module, .text 0x0015b6c0 -
// 0x00160d40), the second half of bro_scr_level_threads: the tank shell HUD, looping / random
// sound bursts, blood impact fx, minefields, incoming shells, respawners, stone-break triggers,
// tree bursts, fx creation sounds, spawner groups and the plant-bomb objective. All but the dead
// ones wait (Scr_Wait*, Scr_WaitTill*): class thread_switch, kept out of kFunctions until script
// threads run as coroutines. Names are ours (from the bodies).

#include <cstdint>

#include "compiler.h"
#include "qcommon_fs.h"
#include "scr_access.h"
#include "script_string.h"

namespace game {

// 0x0015b6c0 (dead: no references): GScr_UseAnimTree on each entity of `ents` with the level's
// anim tree under its animname (level map +0x238), then frees the array. Ghidra's range also holds
// 0x0015b7e0 (BroScr_TankShellHudFireWait).
void XB_CDECL BroScr_ArrayUseAnimTreeByAnimname(DynArray ents);

// 0x0015b7e0 (no Ghidra function: inside BroScr_ArrayUseAnimTreeByAnimname's range; proposed
// BroScr_TankShellHudFireWait; a thread body of BroScr_TankShellReadyHud(_2)): ends on the level's
// "player not on turret"; sets the level flag 0xa26502a4, then while the level word +0x3b0 is 1
// shows the ready-shell hud (+0x80c), waits for `ent`'s "turret_fire", hides it for 4 s.
void XB_CDECL BroScr_TankShellHudFireWait(uint32_t ent);

// 0x0015b8d0: waits for `ent`'s "death", hides the ready-shell hud, notifies the level "player not
// on turret" and clears the level flag 0xa26502a4.
void XB_CDECL BroScr_TankShellHudHideOnDeath(uint32_t ent);

// 0x0015b990: the tank's ready-shell hud: ends on death; sets health, starts the flak88 death and
// fire/reload threads, builds the hud (a new hud elem at 590,420, "gfx/hud/hud@fire_ready_shell.tga" 64x64, hidden)
// and the hide-on-death thread, then every 0.15 s: while the tank has an owner, level +0x3b0 = 1
// and (unless level byte +0x204 bit 0) a BroScr_TankShellHudFireWait thread; without one the hud is
// hidden, the level notified "player not on turret" and the flag cleared.
void XB_CDECL BroScr_TankShellReadyHud(uint32_t ent);

// 0x0015bd20: until `ent`'s bool key 0x75c85f71 is set, plays the level sound alias (map +0x458)
// `alias` on it and waits for its end notify 0x10b1bdae; ends on the level's `ender` when defined.
void XB_CDECL BroScr_LoopSoundUntilFlag(uint32_t ent, uint32_t alias, uint32_t ender);

// 0x0015bde0: forever: RandomIntRange(min, max) bursts of the level sound (map +0x57c) `alias`,
// each waited for (0x2c5b1495) plus burst_base + RandomFloat(burst_random) s, then wait_base +
// RandomFloat(wait_random) s. Waits a frame first; ends on the level's `ender` when defined.
void XB_CDECL BroScr_RandomSoundBursts(uint32_t ent, uint32_t alias, int32_t min, int32_t max, float wait_base,
                                       float wait_random, float burst_base, float burst_random, uint32_t ender);

// 0x0015bee0: the blood impact fx: the level fx map (+0x57c) keys 0x06579a08 and "flesh small" set
// to "impact_flesh" when cg_blood is "1", else "impact_smg_generic"; then every 2 s switched to the
// other one while cg_blood is NOT "0" (flesh) / NOT "1" (generic) - the original's tests are the
// wrong way round, so with an unchanged cvar the two fx swap every 2 s.
void XB_CDECL BroScr_PlayBloodImpactFx();

// 0x0015c120 (a thread of BroScr_MinefieldTriggerThink): marks `ent` in the minefield (bool key
// 0x8903ef3c), waits 0.5 + RandomFloat(0.5) s; still touching `trigger`: the player notifies the
// level "mine death", anyone else gets an "explo_mine" temp-origin sound; then the mine sound
// (0x7857af8e), RadiusDamage(origin, 300, 2000, 50) and for the player the minefield mission
// failure. Clears the mark.
void XB_CDECL BroScr_MinefieldKill(uint32_t ent, uint32_t trigger);

// 0x0015c390: unless the level's shell type `type` count (map +0x430) is defined, plays
// "shell_incoming" (or "shell_incoming_muffled" when the level ambient is "interior") on `ent` and
// waits 1.5 s.
void XB_CDECL BroScr_PlayShellIncoming(uint32_t ent, uint32_t type);

// 0x0015eed0: BroScr_TankShellReadyHud for a two-tank level: the hud as there (level word +0x204
// bit 0 cleared), then with both `tank_a` and `tank_b` valid every 0.35 s their owners (either: the
// fire-wait threads on both unless the flag is set; neither: hud hidden, level notified, flag
// cleared); otherwise the single-tank loop on `ent` every 0.15 s.
void XB_CDECL BroScr_TankShellReadyHud_2(uint32_t ent, uint32_t tank_a, uint32_t tank_b);

// 0x0015ff60: forever: a sentient touching `trigger` that is not already in the minefield gets a
// BroScr_MinefieldKill thread.
void XB_CDECL BroScr_MinefieldTriggerThink(uint32_t trigger);

// 0x00160010: BroScr_PlayShellIncoming(ent, the word at 0x00922e88 (the default type)).
void XB_CDECL BroScr_PlayShellIncomingCall(uint32_t ent);

}  // namespace game
