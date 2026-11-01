#pragma once

// Game-side client code: ammo, view angles, teleports, spawn point choice.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x0024e7a0 (entity in EAX): adds `count` to the weapon's ammo slot; clip-only weapons
// (+0x6ec) are given and their clip filled (also with fill_clip), then ammo and clip are capped
// (0x00929818 / 0x00935338) and shared-ammo groups trimmed to BG_AmmoRoomForWeapon. Returns
// how much ammo + clip changed (0 when the clip ran empty and the weapon was taken).
uint32_t XB_REGS1 XB_CDECL Add_Ammo(uint8_t* ent, uint32_t weapon, int32_t count, uint32_t fill_clip);
// 0x0024ebf0 (angles in EAX, entity in EDX): Q3's SetClientViewAngle (delta angles, entity
// and player view angles), with the clamp around +0x450/+0x458 for players with +0x2c bit 0.
void XB_REGS2 XB_CDECL SetClientViewAngle(const float* angle, uint8_t* ent);
// 0x0024eec0 (entity in EAX, origin in ECX): Q3's TeleportPlayer - origin (+1 z), toggles
// EF_TELEPORT_BIT, entity state from the player state, current origin.
void XB_REGS3 XB_CDECL TeleportPlayer(uint8_t* ent, uint32_t unused_edx, const float* origin);
// 0x0024ef50 (point in EDI): the nearest info_player_deathmatch, or 0.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL SelectNearestDeathmatchSpawnPoint(const float* from);
extern "C" void SelectNearestDeathmatchSpawnPoint_entry(void);

// 0x002500a0 (player in EAX, item entity in ECX): Q3's Pickup_Ammo - 0 when nothing was
// taken, else the respawn time (40 with spawn flag 8, else -1).
int32_t XB_REGS3 XB_CDECL Pickup_Ammo(uint8_t* other, uint32_t unused_edx, uint8_t* ent);
// 0x002507d0: Q3's ClientCommand over the dev (0x008ddae0) and cheat (0x008ddb70) tables.
void XB_CDECL ClientCommand(uint32_t handle);
// 0x00250bf0: Q3's ClientImpacts - "touch" notifies and touch callbacks per distinct handle.
void XB_CDECL ClientImpacts(uint8_t* ent, int32_t count, const uint32_t* touch);

// 0x00251730: Q3's ClientConnect - client cleared and set up, a sentient (team 2). Returns 0.
uint32_t XB_CDECL ClientConnect(uint32_t handle);
// 0x002518a0: death reload - fade out, g_reloading 1, reload time in 2 s (unless 0x0095bf9c).
void XB_CDECL ClientDeathReload();
// 0x002531b0 (player state in EAX): the player's target actor (+0x5f4) within 0x0095be70
// takes the player's node claim and gets "trigger" (delay +0x5f8). Proposed name (was FUN_002531b0).
extern "C" PLATFORM_HIDDEN void XB_CDECL G_ClientActivateTargetActor(const uint8_t* ps);
extern "C" void G_ClientActivateTargetActor_entry(void);
// 0x00253440 (attacker in EAX, inflictor in ECX, self in ESI): Q3's LookAtKiller (+0x104 of the client).
extern "C" PLATFORM_HIDDEN void XB_CDECL LookAtKiller(const uint8_t* attacker, const uint8_t* inflictor, uint8_t* self);
extern "C" void LookAtKiller_entry(void);
// 0x00253a10 (origin in EDX): Q3's SpotWouldTelefrag (live clients and actors).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL SpotWouldTelefrag(const float* origin);
extern "C" void SpotWouldTelefrag_entry(void);

// 0x00253b30: Q3's P_DamageFeedback (blend, damage angles, pain event).
void XB_CDECL P_DamageFeedback(uint8_t* player);
// 0x00254fa0: a random free info_player_deathmatch (else the first one).
uint32_t XB_CDECL SelectRandomDeathmatchSpawnPoint();

// 0x00259000 (avoid point in EAX, angles out in EBX; cdecl origin out): Q3's SelectSpawnPoint.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL SelectSpawnPoint(const float* avoid, float* angles, float* origin);
extern "C" void SelectSpawnPoint_entry(void);

// 0x0025b530 (player in EAX; tail of ClientThink): the player's head hitbox entity
// (client +0x5f0, spawned on first use) moved to the eye position with lean.
void XB_REGS1 XB_CDECL G_UpdatePlayerHeadEnt(uint8_t* ent);
// 0x0025b6b0 (origin out in EAX; cdecl angles out): Q3's SelectInitialSpawnPoint - the first
// info_player_deathmatch with spawn flag 1 (else the last one), else SelectSpawnPoint.
uint8_t* XB_REGS1 XB_CDECL SelectInitialSpawnPoint(float* origin, float* angles);

}  // namespace game
