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

}  // namespace game
