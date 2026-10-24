#pragma once

// Game module: vehicles, turrets and the weapon-slot helpers they use (gentity_t and the
// vehicle record (+0x248) are reached by byte offsets).

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x002fd650: how many of the g_vehicleSlotCount (0x00986000) vehicle slots (0x0095f694,
// 0x340 bytes each) are in use (+0x170).
int32_t XB_CDECL Vehicle_CountActive();
// 0x002487d0 (name in EDI): the vehicle def named so (_stricmp) in AX, -1 when none; the
// rest of EAX as the last _stricmp (or the caller) left it.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Vehicle_FindDefIndex(uint32_t eax, const char* name);
extern "C" void Vehicle_FindDefIndex_entry(void);
// 0x00249e70: frees the vehicle defs (Z_Free) and clears their count. EAX is left as
// Z_Free left it; the caller (G_ShutdownGame) overwrites it.
extern "C" PLATFORM_HIDDEN void XB_CDECL Vehicle_FreeDefs();
// 0x002b5df0 (index in EBX; cdecl fields, count): a zeroed 0x870-byte weapon def in
// bg_weaponDefs[index], name "" hashed, string fields (type 0) set to "".
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL BG_AllocWeaponDef(uint32_t index, const uint32_t* fields, int32_t count);
extern "C" void BG_AllocWeaponDef_entry(void);
// 0x00226720 (stdcall vehicle physics record): the eight wheel bones (names 0x008db414) in
// +0..+0x1c, each found one's base pose as a 4x4 at +0x220 + 0x40 * i. Returns 8.
int32_t XB_STDCALL Vehicle_InitWheelTags(int32_t* vehicle);
// 0x002b5690 (weapon in EAX, playerState in ESI): gives the weapon (and its alternates,
// +0x718) unless owned or of kind 7/9, puts it in a free slot by its slot kind (+0x9c).
// Returns 1 when given.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL BG_GivePlayerWeapon(uint32_t weapon, uint8_t* ps);
extern "C" void BG_GivePlayerWeapon_entry(void);
// 0x002b5530 (weapon in EAX, playerState in ECX): takes the weapon and its alternates; its
// slot gets the first other owned weapon that has none (when +0xa0), else 0. Returns 1
// when the weapon was owned.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL BG_RemoveWeaponFromSlot(uint32_t weapon, uint8_t* ps);
extern "C" void BG_RemoveWeaponFromSlot_entry(void);
// 0x0025a400: a push on the vehicle: the direction against its forward/up axes (+0x1e8,
// +0x1ec, normalized, times the def's +0x54/+0x58 and scale clamped to 0..1), spin
// (+0x1f8 = ent +0x2fc * spin) and +0x1fc. EAX = the def (the entity when no vehicle).
uint32_t XB_CDECL Vehicle_ApplyPush(uint8_t* ent, const float* direction, float scale, float spin, uint32_t extra);
// 0x0024e300 (AI flag in EDX; cdecl ent, target angles): turns the turret's pitch and yaw
// (+0x90) toward the target at the weapon's turn speeds (+0x828/+0x82c; 200 without AI)
// this frame; Scr_Notify "turret_on_target" and 1 when both arrive. The entry hands the C
// version the argument block (the original keeps a float in the first slot).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Turret_UpdateTargetAngles(uint32_t* args, uint32_t ai, uint8_t* ent,
                                                                      const float* target);
extern "C" void Turret_UpdateTargetAngles_entry(void);
// 0x0024dc70: the vehicle's tag indices (tag_player .. tag_coax, +0x2b0..+0x2c8) and the
// two bone tables (0x009886e8 -> +0x2cc, 0x009886f8 -> +0x2dc; "tag_origin" = bone 0),
// the name hashes computed once. EAX = the last value stored.
uint32_t XB_CDECL Vehicle_CacheTags(uint8_t* ent);

// 0x002314c0: for each physics vehicle (list 0x008feb70, count 0x008feb9c): its pose
// (+0x170, 64 bytes) into its physics body (+0x154) +0x160, then G_SetPoseFromPhysics.
void XB_CDECL Vehicle_PhysicsPreFrame();
// 0x00258a10 (player in EAX, turret in ECX): the turret is usable (+0x24c flag 0x10, not
// in use, a weapon, the player in its arc (0x00254bd0)) by a client that is not busy.
uint32_t XB_REGS3 XB_CDECL Turret_IsUsableBy(uint8_t* player, uint32_t, uint8_t* turret);
// 0x0025b810 (pain callback): bullet and grenade-class weapons (+0x94 1/2) push the
// vehicle along the hit direction (Vehicle_ApplyPush, scale 1).
void XB_CDECL Vehicle_Pain(uint8_t* self, const uint8_t* attacker, uint32_t, uint32_t, uint32_t, const float* direction);
// 0x0024bb70: runs the think of every vehicle slot's entity with +0x297 == 2 (G_RunThink,
// entity in ESI; the original pushes its caller's EBX as the argument); 0x0095f684 set meanwhile.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_RunPlayerVehicleThinks(uint32_t ebx);
extern "C" void G_RunPlayerVehicleThinks_entry(void);
// 0x00259d60: each vehicle's collision brush model (Vehicle_FindCollmap of its def name)
// set (SV_SetBrushModel), contents 0x800000 (0xa00000 with spawnflag 1). EAX = the slot
// count, else EAX as it came.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Vehicle_InitBrushModels(uint32_t eax);
extern "C" void Vehicle_InitBrushModels_entry(void);
// 0x00266470 (use callback): a client in a vehicle (+0xc0 flag 0x100000) gets out, else in.
uint32_t XB_CDECL Vehicle_Use(uint8_t* self, uint8_t* other);

// 0x0025b950 (controller callback): the body (+0x280/+0x288), turret yaw (+0x294) and
// barrel pitch (+0x290) bone angles through 0x002de420 for the tags the vehicle has.
void XB_CDECL Vehicle_Controller(uint8_t* ent);
// 0x00266390 (die callback): stops the effects, puts the driver out, and pushes the wreck
// along the hit direction for bullet and grenade-class weapons.
void XB_CDECL Vehicle_Die(uint8_t* self, uint32_t, const uint8_t* attacker, uint32_t, uint32_t, uint32_t, uint32_t,
                          const float* direction);
// 0x002c2aa0: weapon 0 ("none"), bg_lastWeaponIndex = 0, then the weapon files (0x00024820,
// "WEAPONFILE"). Registered Result::None: EAX is what 0x00024820 leaves (a stack address
// that differs with the frame depth); the caller does not read it.
uint32_t XB_CDECL BG_InitDefaultWeapon();
// 0x0025cc60 (turret in EDI): its user stops using it (client: G_ClientStopUsingTurret;
// actor: Actor_LeaveTurret); turret info byte 0, +0x297 and +0x24c cleared. EAX = the info.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL G_ReleaseTurretUser(uint8_t* turret);
extern "C" void G_ReleaseTurretUser_entry(void);

// 0x00267020 (move in EAX; cdecl ent, pusher, amove): moves an entity with a pusher: by
// the move plus the pusher's rotation (amove) about it; when blocked, tries offsets in
// shells of 4 units up to half its size (+0xf0). Clients/actors turn with the yaw;
// pushed_p (0x0095bef8) advances. Returns 1 when moved (or still free where it was).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL G_TryPushingEntity(const float* move, uint8_t* ent, uint8_t* pusher,
                                                               const float* amove);
extern "C" void G_TryPushingEntity_entry(void);
// 0x00270f90: the vehicle turret's trigger (+0x2fc: the player driver's attack button) and
// refire delay (+0x184 -= elapsed); fires (Vehicle_FireTurretWeapon) when due.
uint32_t XB_CDECL Vehicle_UpdateTurret(uint8_t* ent, int32_t elapsed);
// 0x00272360 (player in EAX): keeps using the turret (+0x1a0) while it is in use with
// +0x350 > 0 (0x00271b60, player in EBX), else G_ClientStopUsingTurret.
extern "C" PLATFORM_HIDDEN void XB_CDECL Player_UpdateTurretUse(uint8_t* player);
extern "C" void Player_UpdateTurretUse_entry(void);

}  // namespace game
