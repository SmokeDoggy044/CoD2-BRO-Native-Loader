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

// 0x00255150 (entity in EBX): Q3's InitTrigger without the link.
extern "C" PLATFORM_HIDDEN void XB_CDECL InitTrigger(uint8_t* ent);
extern "C" void InitTrigger_entry(void);

// 0x002591e0: spawn for trigger_mount.
void XB_CDECL SP_trigger_mount(uint8_t* ent);
// 0x00259230: spawn for trigger_lookat.
void XB_CDECL SP_trigger_lookat(uint8_t* ent);
// 0x00259260: spawn for trigger_damage.
void XB_CDECL SP_trigger_damage(uint8_t* ent);
// 0x002593b0: spawn for trigger_once.
void XB_CDECL SP_trigger_once(uint8_t* ent);
// 0x002593f0: spawn for trigger_hurt.
void XB_CDECL SP_trigger_hurt(uint8_t* ent);
// 0x002594e0: spawn for trigger_friendlychain.
void XB_CDECL SP_trigger_friendlychain(uint8_t* ent);
// 0x00259530: spawn for trigger_multiple.
void XB_CDECL SP_trigger_multiple(uint8_t* ent);

// 0x00257de0: spawn for trigger_use (hint string / cursor hint / arc).
void XB_CDECL SP_trigger_use(uint8_t* ent);

// 0x0025f7c0 (entity in EAX, item in ESI): Q3's G_SpawnItem.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_SpawnItem(uint8_t* ent, const uint8_t* item);
extern "C" void G_SpawnItem_entry(void);
// 0x00260220 (entity in EDI): Q3's G_CallSpawn (spawn map, else items); 1 when spawned.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL G_CallSpawn(uint8_t* ent);
extern "C" void G_CallSpawn_entry(void);

// 0x0025fb20 (slot in EAX, origin in EBX): Q3's LaunchItem - a dropped item entity thrown with the velocity.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL LaunchItem(uint32_t slot, const float* origin, int32_t model_index,
                                                     const uint8_t* item, const float* velocity, int32_t index);
extern "C" void LaunchItem_entry(void);

// 0x00264f60 (entity in ESI, "no throw" in EAX; item, yaw offset): Q3's Drop_Item.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL Drop_Item(uint8_t* ent, uint32_t still, const uint8_t* item, float angle);
extern "C" void Drop_Item_entry(void);
// 0x00265500 (entity in EAX): Q3's TossClientItems (the held weapon).
void XB_REGS1 XB_CDECL TossClientItems(uint8_t* ent);
// 0x002655b0 (entity handle): Q3's ClientDisconnect.
void XB_CDECL ClientDisconnect(uint32_t handle);

// 0x002b8d40 (EDX zone, ECX scene): points the scene at the zone's +0x38 list when set and spreads the zone's
// update groups (0x60 bytes at +0x34, count +0x30) over frames: +0x58 = frame (+0x19c), 22 groups per frame (+0x198).
void XB_REGS3 XB_CDECL Scn_InitUpdateGroups(uint32_t unused_eax, uint8_t* zone, uint8_t* scene);

}  // namespace game
