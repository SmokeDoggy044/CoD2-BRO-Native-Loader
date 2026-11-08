#pragma once

// Compiled level-script level-wide helpers: flags, cvars, mission failure, precaching.

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "script_string.h"

namespace game {

// 0x0015ce00: level flag map (0x00922ee0 +0x32c, hash -> bool; a missing key is inserted as
// the undefined bool) entry for `name` = 1, then Scr_QueueNotify(level object word +4, hash of
// name). Releases `name`.
void XB_CDECL BroScr_FlagSet(ScriptString name);
// 0x001816d0 (dead): *out = GScr_GetCvarInt("g_useGear"); returns out.
uint32_t* XB_CDECL BroScr_GetUseGearCvar(uint32_t* out);
// 0x00181f70: unless cvar "ff_off" is 1: ends on "death" of the player (0x009236dc) and "mine
// death" of the level script object (0x00922ef8 word +4), notifies 0xa264707f there, and the
// first time (byte +0xf0 of the object) fails the mission with "INGAME_MISSIONFAIL_FF".
void XB_CDECL BroScr_FriendlyFireMissionFail();
// 0x00182100: spawns a "script_model" at (0, 0, 0), moves it to the player's (0x009236dc)
// origin, sets `model` and deletes it. Releases `model`.
void XB_CDECL BroScr_PrecacheModelBySpawn(ScriptString model);

// 0x00154990: a spawner trigger's threads: unless it is named "friendly_wave", the
// BroScr_TriggerSpawnerThink thread (ends on 0xe2a210cf) for each target with spawnflags bit 0;
// one thread per defined field (script_increment, script_fallback, 0xbe5f9a9f,
// script_killspawner, 0x83048760, script_hint, script_exploder, ambient; 0x7fa1fc35 / 0x0bfd0e20
// on the level entity); then by targetname: flood_spawner, grenade_spawner, grenade_spawner_once,
// friendly_wave(_off), friendly_chat, axis_touching, axis_dead, friendly_mg42 threads, "context"
// BroScr_SetupWallHopTrigger.
void XB_CDECL BroScr_SetupSpawnerTriggers(uint32_t trigger);

}  // namespace game
