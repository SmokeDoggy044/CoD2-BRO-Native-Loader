#pragma once

// Player movement and per-frame player helpers (pmove, compass markers).

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00207c60: the sentient's eye position into the two-slot history every interval.
void XB_CDECL Player_RecordEyeHistory(const uint8_t* pm);
// 0x00204640: compass marker word for the first hidden marked actor (0 without one).
uint32_t XB_CDECL Player_PackActorMarker(const float* origin);

// 0x002ac770: TODO.
// 0x002ac770: the fired weapon's screen blend (def +0x660 * 255) adds to the player state's +0x4f0 (at most 255)
// unless +0x88 is 1.
void XB_CDECL PM_AddWeaponFireBlend();

// 0x002ad430: pm_flags 0x10000 (sprint) from the button, stamina and movement.
void XB_CDECL PM_UpdateSprintFlag();
// 0x002ad520: pm_flags 0x80 from cmd button bit 0x10 (with flag 0x20, outside weapon states 5-9).
void XB_CDECL PM_UpdateButtonFlag80();
// 0x002ad580: pm_flags 0x400 for a prone player.
void XB_CDECL PM_SetProneFlag400();
// 0x002ad5e0 (time in EAX, curve in EDI, height out in EBX; value in XMM0): a view-height curve point.
extern "C" PLATFORM_HIDDEN float XB_CDECL PM_EvalLerpCurve(int32_t time, const uint8_t* curve, float* out);
extern "C" void PM_EvalLerpCurve_entry(void);
// 0x002ad680 (stance in EAX, player state in ECX; cdecl flag; ECX/EDX kept): the view-height lerp time.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL PM_ViewHeightLerpTime(uint32_t stance, const uint8_t* ps, uint32_t flag);
extern "C" void PM_ViewHeightLerpTime_entry(void);
// 0x002ad6d0 (player state in EAX): the view height as a stance (2 crouch, 1 prone, 0).
uint32_t XB_REGS1 XB_CDECL PM_ViewHeightStance(const uint8_t* ps);
// 0x002b4a30 / 0x002b4ab0 / 0x002b4b30: the end of an ADS drop / ADS raise / weapon raise (1 when it ended).
uint32_t XB_CDECL PM_FinishAdsDrop();
uint32_t XB_CDECL PM_FinishAdsRaise();
uint32_t XB_CDECL PM_FinishWeaponRaise();

// 0x002b4c20 / 0x002b4cb0: the weapon back to state 0 with weapon anim 0xa / its idle anim (0 when blocked).
uint32_t XB_CDECL PM_ResetWeaponToAnimA();
uint32_t XB_CDECL PM_ResetWeaponToIdle();

// 0x002b5390 (weapon in ECX, player state in EDX): the free slot a weapon can take (0 none).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL BG_GetFreeWeaponSlot(uint32_t weapon, const uint8_t* ps);
extern "C" void BG_GetFreeWeaponSlot_entry(void);
// 0x002b54c0 (weapon in EAX, player state in EDI, slot in ESI): puts an owned weapon in a slot it fits (1).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL PM_SetSlotWeapon(uint32_t weapon, uint8_t* ps, uint32_t slot);
extern "C" void PM_SetSlotWeapon_entry(void);

// 0x002acc10: links each weapon's alternate weapon (+0x718) by name.
void XB_CDECL BG_SetupAltWeaponIndices();
// 0x002acdb0: the weapon and ammo items of bg_itemlist from the weapon defs.
void XB_CDECL BG_SetupWeaponItems();
// 0x002acf50: per weapon the raise/drop rates (+0x868/+0x86c).
void XB_CDECL BG_SetupWeaponTransitionRates();

// 0x002ac840: the ADS fraction (+0x88) toward aiming or not, with the raise/drop/ADS weapon anims.
void XB_CDECL PM_UpdateAimDownSightFrac();

// 0x002b5ea0 (ECX out: view height; EDX: prone height unless crouched): always 1.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL PM_CheckViewHeightStance(uint32_t* ecx_edx);
extern "C" void PM_CheckViewHeightStance_entry(void);
// 0x002b5ed0 (from height in EAX, to height in ECX; fraction in XMM0): the view-height lerp's progress.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL PM_ViewHeightLerpFrac(uint32_t from, uint32_t to, float* frac);
extern "C" void PM_ViewHeightLerpFrac_entry(void);
// 0x002b5fc0 / 0x002b5fe0 / 0x002b6000 / 0x002b6020 (flags in EDX) / 0x002b6070: the land, run step, walk step,
// footstep and jump events for the ground surface.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL PM_LandEventForSurface();
extern "C" void PM_LandEventForSurface_entry(void);
uint32_t XB_CDECL PM_RunStepEventForSurface();
uint32_t XB_CDECL PM_WalkStepEventForSurface();
uint32_t XB_REGS2 XB_CDECL PM_FootstepEventForSurface(uint32_t unused_eax, uint32_t flags);
uint32_t XB_CDECL PM_JumpEventForSurface();

// 0x002b4fd0 (weapon in EAX; cdecl player state): the reserve ammo, summed over a shared ammo cap.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL BG_GetTotalReserveAmmo(int32_t weapon, const uint8_t* ps);
extern "C" void BG_GetTotalReserveAmmo_entry(void);

// 0x002b5920 / 0x002b5a70 / 0x002b5c40: the clip, shared ammo cap and ammo indices by name.
void XB_CDECL BG_SetupClipIndices();
void XB_CDECL BG_SetupSharedAmmoCaps();
void XB_CDECL BG_SetupAmmoIndices();

// 0x002b6a70 (name in ESI): the bg_itemlist entry by weapon, pickup or class name (0 none).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL BG_FindItem_body(const char* name);
extern "C" void BG_FindItem_entry(void);
// 0x002bb9e0: the melee hit (event 0xaa, weapon state 0xb).
void XB_CDECL PM_WeaponMeleeStrike();
// 0x002bbd30: an empty weapon flagged to go when empty leaves its slot (event 0x97).
void XB_CDECL PM_RemoveEmptyWeapon();
// 0x002bbdc0: 1 when the weapon can reload.
uint32_t XB_CDECL PM_WeaponCanReload();
// 0x002bc310 / 0x002bc3d0: the start of an ADS drop / raise (weapon states 0xd / 0xc).
void XB_CDECL PM_Weapon_BeginAdsDrop();
void XB_CDECL PM_Weapon_BeginAdsRaise();
// 0x002bc570: weapon state 0 with the idle anim.
void XB_CDECL PM_SetWeaponReady();
// 0x002bc860: the stamina breath event (0x8c) every 1700 ms while pm_flags 0x20000.
void XB_CDECL PM_UpdateStaminaBreathEvent();
// 0x002bc8e0: events 0x92 / 0x93 when pm +0x121 got set / cleared.
void XB_CDECL PM_AddFlagChangeEvents();
// 0x002bccc0: leaves the ground with the jump speed for a height.
void XB_CDECL PM_StartJump(float height);
// 0x002c20e0: the melee states' end (1 when handled).
uint32_t XB_CDECL PM_FinishWeaponMelee();
// 0x002c2170: the fire anim by ADS fraction and clip.
void XB_CDECL PM_StartFireAnim();
// 0x002c25e0: starts an ADS raise or drop for scoped weapons.
void XB_CDECL PM_Weapon_CheckAdsTransition();
// 0x002c80b0: the weapons' hint string indices.
void XB_CDECL BG_SetupWeaponHintStrings();
// 0x002d6710 (time in EAX, entity in ECX, origin in ESI): 1 when the origin is in the box at the entity's
// trajectory position.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL BG_PlayerInTrajectoryBox_body(int32_t time, const uint8_t* ent,
                                                                          const float* origin);
extern "C" void BG_PlayerInTrajectoryBox_entry(void);
// 0x002e2a30 (saved origin in EAX, saved velocity in ESI): a prone move whose feet no longer fit is undone (0).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL PM_ProneUndoBlockedMove_body(const float* origin, const float* velocity);
extern "C" void PM_ProneUndoBlockedMove_entry(void);
// 0x002eabd0 (start state in ECX): the reload state with its looped sound.
void XB_REGS3 XB_CDECL PM_EnterReloadState(uint32_t, uint32_t, uint32_t start);
// 0x002ec2e0: PM_EnterReloadState from the clip.
void XB_CDECL PM_StartReload();

// 0x002bc660: reserve ammo into the clip (returns the amount).
int32_t XB_CDECL PM_ReloadClip();
// 0x002bc490: the reload's second timer (+0x38; returns it).
int32_t XB_CDECL PM_SetReloadDelay();
// 0x002ee940: a staged reload start or PM_StartReload.
void XB_CDECL PM_BeginWeaponReload();
// 0x002f0450: 1 with ammo in the clip, else a reload or the empty-weapon handling (0).
uint32_t XB_CDECL PM_WeaponCheckAmmo();
// 0x002c2690: a staged reload's next stage or PM_ReloadClip.
void XB_CDECL PM_ReloadAmmoAtTime();
// 0x002c24d0: the weapon timers (1 when the second ran out).
uint32_t XB_CDECL PM_UpdateWeaponTimers();
// 0x002c2380: starts a weapon change for the command's weapon or a forced drop.
void XB_CDECL PM_CheckWeaponChange();
// 0x002c2e80 (wishdir in ECX; cdecl wishspeed, accel): Q3's PM_Accelerate.
void XB_REGS3 XB_CDECL PM_Accelerate(uint32_t, uint32_t, const float* wishdir, float wishspeed, float accel);
// 0x002c2f60: Q3's PM_Friction.
void XB_CDECL PM_Friction();
// 0x002c2b40: sprint stamina drain/refill.
void XB_CDECL PM_UpdateStaminaMeter();
// 0x002e2e40 (yaw in XMM0, player state in EAX; one cdecl word): BG_CheckProneFeet for a prone turn.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL PM_CheckProneYaw_body(uint8_t* ps, float yaw, uint32_t arg);
extern "C" void PM_CheckProneYaw_entry(void);
// 0x002b68f0 (touch flag in EDX, entity state in ECX; cdecl player state): 1 when the item can be picked up.
uint32_t XB_REGS3 XB_CDECL BG_CanItemBeGrabbed(uint32_t, uint32_t touch, const uint8_t* es, const uint8_t* ps);
// 0x002cdf00: the weapon def table and the per-weapon setup (once).
void XB_CDECL BG_InitWeapons();

// 0x002e27d0: pm_flags 0x20 (ADS) and the type-3 weapons' prone entry from the ADS button.
void XB_CDECL PM_UpdateAdsState();
// 0x002c21f0 (EAX: skip when set): the fire timers after a shot, weapon state 3.
uint32_t XB_REGS1 XB_CDECL PM_SetWeaponFireTimers(uint32_t skip);
// 0x002bc120: puts the old weapon away toward the new one (weapon state 2).
void XB_CDECL PM_BeginWeaponChange(int32_t old_weapon, int32_t new_weapon);

// 0x002c28f0: the bolt weapons' rechamber after a shot (1 while it waits).
uint32_t XB_CDECL PM_UpdateWeaponRechamber(uint32_t fired);
// 0x002f0d40: a shot of the current weapon.
void XB_CDECL PM_WeaponFire(uint32_t skip_timers);
// 0x002f0ef0: 1 when the fire button's release ends the shot (grenades: the throw).
uint32_t XB_CDECL PM_CheckFireButtonRelease(uint32_t held);
// 0x002c2c80: Q3's PM_CheckJump (1 when jumping).
uint32_t XB_CDECL PM_CheckJump();

// 0x002cdff0: while walking the velocity loses 20 units of speed (stops at 0).
void XB_CDECL PM_ReduceGroundSpeed();

// 0x002bcdf0 (entity state in EAX, player state in ECX; cdecl time): the entity state with an extrapolated
// trajectory.
uint8_t* XB_REGS3 XB_CDECL BG_PlayerStateToEntityStateExtraPolate(uint8_t* es, uint32_t, uint8_t* ps, uint32_t time);

// 0x002bbe40: the end of a weapon change to the commanded weapon (raise state 1, events, anims); 0 when not
// changing.
uint32_t XB_CDECL PM_FinishWeaponChange();
// 0x002bc990: Q3's PM_CrashLand - fall damage, slowdown and the land / step events.
void XB_CDECL PM_CrashLand();
// 0x002c84e0: Q3's PM_NoclipMove.
void XB_CDECL PM_NoclipMove();
// 0x002ce160: the movement direction (+0x78) from the body yaw, the ladder normal or the frame's move.
void XB_CDECL PM_SetMovementDir();
// 0x002c8110 (was FUN_002c8110): a noclip move with forward kept horizontal and up/down from buttons.
void XB_CDECL PM_NoclipFlatMove();
// 0x002e2ac0 (was FUN_002e2ac0): prone feet / steep ground events and the prone pitch (+0x454) and roll (+0x458)
// toward the ground plane.
void XB_CDECL PM_UpdateProneTilt();
// 0x002eb610 (was FUN_002eb610): the ladder check (pm_flags 0x10, ladder normal +0x64).
void XB_CDECL PM_DetectLadder();
// 0x002ebaf0 (was FUN_002ebaf0): event 0x8b at an interval set by the xy speed (pm +0x124) while the player moves
// through contents 2.
void XB_CDECL PM_PeriodicContentsEvent();

}  // namespace game
