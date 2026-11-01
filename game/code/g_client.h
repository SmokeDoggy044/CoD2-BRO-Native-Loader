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

}  // namespace game
