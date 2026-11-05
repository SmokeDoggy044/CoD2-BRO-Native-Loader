#pragma once

// Levelscript (push c3): exploders and level-wide helpers (targetnames, health drops, ambient,
// the BroFunc registry users). The compiled scripts' C++; SEH registration left out.

#include <cstdint>

#include "compiler.h"

namespace game {

struct ScriptString;

// 0x001a9bf0: fires exploder `number`: marks it in the int map (GScr_SetIntMapValue(number, 1)),
// then for each valid entity of the level's exploder list (level +0x78) whose key 0xf3b8dce5 is
// `number`: sound-fx threads for fields 0x0aedcb08 / script_fxid (on the level), BroScr_InitScriptRadius
// for script_damage, BroScr_ExploderPlaySound for script_sound; then by targetname: "exploder" ->
// BroScr_NotSolidAfterDelay, "exploderchunk"/"exploderchunk visible" -> BroScr_NotSolidThrow, other
// names and none without script_fxid -> BroScr_ScriptDelayConnectPaths. `self` is unused.
void XB_CDECL BroScr_Exploder(uint32_t self, int32_t number);
// 0x001b6020: BroScr_Exploder(self, atol(number)); releases `number`.
void XB_CDECL BroScr_ExploderIfSet(uint32_t self, ScriptString number);

struct DynArray;

// 0x001b3250: appends `start` and each entity it targets in turn (GScr_GetEntArray by target) to
// the owner's array key 0x0605894e until an invalid entity.
void XB_CDECL BroScr_BuildTargetChain(uint32_t owner, uint32_t start);
// 0x001b5120: a BroScr_SetTargetnameFromOrigin thread on each entity, then a level
// BroScr_TriggeredSpawnerThink thread for each (with the undefined entity); frees `ents`.
void XB_CDECL BroScr_SetTargetnamesFromOrigins(DynArray ents);
// 0x001b6090 (functor target): BroScr_SetTargetnamesFromOrigins on a copy of *ents.
void XB_CDECL BroScr_SetTargetnamesFromOriginsCall(const DynArray* ents);
// 0x001b5880 (functor target): the trigger's entity variable 0xe2a210cf = the hash of
// "_spawner_gmi::friendly_wave_callback"; the first call starts BroScr_FriendlyWaveSpawn on the level.
void XB_CDECL BroScr_SetFriendlyWaveTrigger(uint32_t unused, uint32_t trigger);

// 0x001bea50 (functor target): unless already in fallback `index` (array key 0x9ef08b23), joins it
// (entry = 1, key 0x8bbe4822 = index), counts it in the level's +0x6c array when `name` is empty,
// starts BroScr_FallbackGoalThink at `node` (valid, level +0xe4 entry set; through the wrapper at
// 0x00660450 in the PIANLUPO section) and a level BroScr_NotifyFallbackerDied thread. Releases `name`.
void XB_CDECL BroScr_FallbackerJoinFallback(uint32_t ent, uint32_t index, uint32_t node, ScriptString name);

// 0x001ba0d0 (FUN_001ba0d0; proposed BroScr_PickWeightedChoice): keeps `current` with chance
// `keep` when it is one of the non-zero choices, else picks a choice by its weight (weights
// normalised to sum 1); 0 when nothing is picked. Returns out.
uint32_t* XB_CDECL BroScr_PickWeightedChoice(uint32_t* out, uint32_t current, uint32_t keep_bits, uint32_t choice1,
                                             uint32_t weight1, uint32_t choice2, uint32_t weight2, uint32_t choice3,
                                             uint32_t weight3);

// 0x001e0880: adds the AI to its drop group (level +0xa4 map, key `group`) and marks one random
// valid member of the group (key 0xd66dcf8c = 1, the others 0).
void XB_CDECL BroScr_AiMaybeDropHealth(uint32_t ent, uint32_t group);
// 0x001e09e0 (functor target): unless g_gameskill is "3" or the AI is unmarked: an
// "item_health_medium" 10 above its origin at a random yaw; its script_health group is emptied and
// the drop goes in the level's ring (+0xd0), deleting the one it replaces.
void XB_CDECL BroScr_DropHealth(uint32_t ent);

// 0x001e1b80 (dead): the level's key 0x9c1519ac = `name`; with level +0x1c set: a "script_origin" at
// the player (level +0x1d4) named "global_ambient". Releases `name`.
void XB_CDECL BroScr_SpawnGlobalAmbient(ScriptString name);
// 0x001e2a60 (dead): cvar mg42 "off" when unset; BroScr_Mg42CoverSpotThink on each "mg42", a
// BroScr_TrackTriggerTouch thread on each "mg42cover" ("crater") and "mg42cover_cow" ("cow");
// level +0x14c = 24.
void XB_CDECL BroScr_InitMg42CoverSpots();

}  // namespace game

