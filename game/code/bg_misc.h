#pragma once

// Game module, shared player code: weapon lookups and the pmove helpers (pm 0x0092eff4 -> {ps,
// ...}; playerState_t by byte offsets).

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x002acb00 (hash in ESI): the index of the weapon whose hash (+4) matches among
// bg_weaponDefs[0..bg_lastWeaponIndex]; not found leaves AL 0 over the loop counter.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL BG_FindWeaponIndexForHash(uint32_t hash);
extern "C" void BG_FindWeaponIndexForHash_entry(void);
// 0x002bc610 (weapon in ECX): the player's ammo in the weapon's clip slot (+0x5a0 -> ps
// +0x280).
int32_t XB_REGS3 XB_CDECL PM_GetClipAmmo(uint32_t, uint32_t, uint32_t weapon);
// 0x002bc5e0 (weapon in ECX): that clip is empty.
uint32_t XB_REGS3 XB_CDECL PM_ClipIsEmpty(uint32_t, uint32_t, uint32_t weapon);
// 0x002bc630 (weapon in ECX): takes amount from that clip; returns the clip's address.
int32_t* XB_REGS3 XB_CDECL PM_WeaponUseAmmo(uint32_t, uint32_t, uint32_t weapon, int32_t amount);
// 0x002ad5a0: counts the pm_time (+0x30) down by pml_msec; when it runs out, clears it and
// the time-knockback flags (+0x2c).
void XB_CDECL PM_DropTimers();
// 0x002ad700 (ps in EAX, event in ECX): queues the event (low byte) and its parameter (low
// byte) in the four-entry ring (+0xc8 / +0xd8, sequence +0xc4); event 0 does nothing.
void XB_REGS3 XB_CDECL BG_AddPredictableEventToPlayerstate(uint8_t* ps, uint32_t, uint32_t event, uint32_t parm);
// 0x002b63f0 (event in ECX): the same for pm's player with parameter 0.
void XB_REGS3 XB_CDECL PM_AddEvent(uint32_t, uint32_t, uint32_t event);
// 0x002bcda0: adds an entity number to pm's touch list (+0x7c count, +0x80, 32 at most)
// unless it is 0 or already there.
void XB_CDECL PM_AddTouchEnt(int32_t entity);
// 0x002ac7e0 (anim in ECX): sets the legs anim (+0x4ec, toggle bit 0x200 flipped) unless the
// player is dead (+0x24 >= 4) or pm +0xa is clear; anim 0 above 0.9 of +0x88 becomes 0x17; in
// mode 5 (0x00960020 -> +0x94) the anim is stored as is.
void XB_REGS3 XB_CDECL PM_StartLegsAnim(uint32_t, uint32_t, uint32_t anim);
// 0x002bcf90 (entity state in EAX, player state in ECX): copies the player into its entity
// state - origin, angles, the lean (+0x78, signed byte as float), the newest predictable
// event's parameter, and the external events since the last call that are not in the
// predictable-only list (0x008d2188, ended by a value <= 0).
void XB_REGS3 XB_CDECL BG_PlayerStateToEntityState(uint8_t* es, uint32_t, uint8_t* ps);
// 0x002b62f0 (usercmd in EAX, result in XMM0): the move scale - speed (+0x50) * the largest
// of |forward|, |right|, |up| / (127 * |move|) on the x87, times the stance speed (+0x430;
// +0x434 / +0x438 when not moving vertically (+0x4c = 0) and not sprinting (+0x2c bit 7)),
// x3 / x6 for pm types 2 / 3.
extern "C" PLATFORM_HIDDEN float XB_CDECL PM_CmdScale(const int8_t* cmd);
extern "C" void PM_CmdScale_entry(void);
// 0x002b5120 (weapon in EAX, player state in EBX): how much more ammo the player can carry
// for the weapon - its ammo (+0x598, max 0x00929818) or clip (+0x5a0 with +0x6ec set, max
// 0x00935338) slot, or for shared-ammo weapons (+0x5b0 >= 0, max 0x00934a68) the group's
// maximum less every distinct slot of the group's weapons the player has (bits +0x3f0).
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL BG_AmmoRoomForWeapon(uint32_t weapon, const uint8_t* ps);
extern "C" void BG_AmmoRoomForWeapon_entry(void);
// 0x002b5400 (player state in EDI): the inventory slot (1-6, bytes +0x3f8 + slot) holding the
// weapon (or, with follow set, one of its alternate-weapon chain +0x718), by the weapon's slot
// type (+0x9c: types 1 and 2 share slots 1 and 2); 0 when not held.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL BG_GetWeaponSlot(const uint8_t* ps, int32_t weapon, uint32_t follow);
extern "C" void BG_GetWeaponSlot_entry(void);
// 0x002b52b0 (preferred slot in EAX, weapon in ECX, player state in ESI): the slot a picked-up
// weapon goes to (the two primary slots for types 1-2, else its type's slot), 0 when none
// fits; only weapons with +0xa0 set take slots.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL BG_ChooseWeaponSlot(uint32_t preferred, uint32_t weapon, const uint8_t* ps);
extern "C" void BG_ChooseWeaponSlot_entry(void);

// 0x002b6710 (time in EDX, result in ECX, cdecl trajectory): the trajectory's velocity at `time`
// by type (+0): 2 delta, 3 delta until start + duration, 4 sine (x87 cos), 5/6/7 gravity (1, 0.3,
// 0.2 of 0x0094d450) on z, 9/10 delta * t^2 / t until the end; others (and past the end) zero.
void XB_REGS3 XB_CDECL BG_EvaluateTrajectoryDelta(uint32_t, int32_t time, float* result, const uint8_t* tr);

}  // namespace game
