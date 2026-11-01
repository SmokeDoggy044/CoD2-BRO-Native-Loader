#pragma once

// Game module: spawning entities from the level's spawn variables (key/value pairs).

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00249b90 (list in ECX: {count, pairs {key, value}}): copies the pairs into level.spawnVars
// (0x00985424; count 0x00985420) and clears 0x00985624.
void XB_THISCALL G_SetSpawnVarsFromList(const uint32_t* list);
// 0x00249cd0 (key in EAX, out in ECX, default in EDI): reads a spawn variable as "%f %f %f" into
// out (returns 1), or copies the default (returns 0).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL G_SpawnVector(uint32_t key, float* out, const uint32_t* fallback);
extern "C" void G_SpawnVector_entry(void);
// 0x00248460 (spawner in EDI, actor on the stack): copies the spawner's actor fields (table
// 0x0082ff38 by type: int/float, short, byte, vector, script string) and +0x260..+0x280.
extern "C" PLATFORM_HIDDEN void XB_CDECL Actor_CopySpawnerFields(uint8_t* spawner, uint8_t* actor);
extern "C" void Actor_CopySpawnerFields_entry(void);

// 0x0024f000: Q3's SP_info_player_start - renamed info_player_deathmatch, think 5 in 200 ms.
void XB_CDECL SP_info_player_start(uint8_t* ent);
// 0x0024f9a0: the sky portal configstring (10): origin, fov, fog colour/near/far.
void XB_CDECL SP_skyportal(uint8_t* ent);

}  // namespace game
