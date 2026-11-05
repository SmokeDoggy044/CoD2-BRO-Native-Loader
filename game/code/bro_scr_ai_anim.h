#pragma once

// Shared compiled level scripts (levelscript module, push c 2, part 2): melee pair unlinking, anim
// script start and desired pose, allowed move poses, yaw to target and weapon fire-type predicates
// / shot intervals. "out" results are by-value returns through a hidden pointer.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x001a1aa0: unlinks a melee pair: a's partner key 0x31aaf853 (removed when b is invalid or b's
// 0x236fc839 is a) and b's 0x236fc839 (removed when it is a); the failed consistency checks only
// build and drop the "bad parameter" string.
void XB_CDECL BroScr_MeleePair_Unlink(uint32_t a, uint32_t b);
// 0x001a1ce0: |entity yaw - yaw to enemy| as int(d) % 360, folded above 180 (minus 360), as a float.
uint32_t* XB_CDECL BroScr_GetAbsYawToTarget(uint32_t* out, uint32_t ent);
// 0x001a1dd0: the wanted pose (the actor's current one when undefined/0) or the nearest allowed
// one: stand 0x070b89a4 (also with an enemy in range), crouch 0xc1165808, prone 0x0738b1a5.
uint32_t* XB_CDECL BroScr_GetAllowedMovePose(uint32_t* out, uint32_t actor, uint32_t pose);
// 0x001a1f50: starts anim script `script`: unlinks a live melee partner, resets the body / shoot
// anims and state keys, the combat delays for the old and new script, sets the script (0x2181000b)
// and calls the stored start callback (0xe057503b) when set.
void XB_CDECL BroScr_InitAnimScript(uint32_t ent, uint32_t script);
// 0x001a2260: applies the desired pose (key 0x8cb5047a; prone for actors in state 0x30880b outside
// scripts 0x39894e / 0x6730ae8, else the current one): prone pitch-blend anims, sets it and removes
// the key.
void XB_CDECL BroScr_ApplyDesiredAnimPose(uint32_t ent);
// 0x001a2420 (inside Ghidra's BroScr_ApplyDesiredAnimPose; proposed BroScr_InitAnimScriptAndPose):
// BroScr_InitAnimScript then BroScr_ApplyDesiredAnimPose.
void XB_CDECL BroScr_InitAnimScriptAndPose(uint32_t ent, uint32_t script);
// 0x001a2710: weapon type 0xe5aa1ee7. 0x001a2740 (dead) / 0x001a2770 / 0x001a27a0: fire type
// 0x1bdc7 / 0x315aef / 0x3819c9.
uint8_t* XB_CDECL BroScr_IsFlamethrower(uint8_t* out, uint32_t actor);
uint8_t* XB_CDECL BroScr_IsWeaponFireTypeLmg(uint8_t* out, uint32_t actor);
uint8_t* XB_CDECL BroScr_IsWeaponFireTypeAuto(uint8_t* out, uint32_t actor);
uint8_t* XB_CDECL BroScr_IsWeaponFireTypeSemi(uint8_t* out, uint32_t actor);
// 0x001a27d0: fire rate x 0.1 for fire type 0x315aef, else 0.2.
uint32_t* XB_CDECL BroScr_GetShotInterval_2(uint32_t* out, uint32_t actor);
// 0x001a2840: 1 / fire rate for fire types 0x315aef / 0x1bdc7 with a non-zero rate, else
// (0.8 + RandomFloat(0.4)) x the weapon table's +0x10 float.
uint32_t* XB_CDECL BroScr_GetShotInterval(uint32_t* out, uint32_t actor);

}  // namespace game

namespace game {

struct ScriptString;

// 0x001a2930 / 0x001a2af0: the stand / crouch aim and shoot anims (anim-script object +0x3c..+0x50)
// for the actor's weapon: pistol 0xe6a85d8c, flamethrower 0xe5aa1ee7 (stand) / panzerfausts (crouch),
// else by fire type (auto 0x315aef; crouch also LMG 0x1bdc7).
void XB_CDECL BroScr_SetAimAnimsForWeapon(uint32_t actor);
void XB_CDECL BroScr_SetAimAnimsForWeapon_2(uint32_t actor);
// 0x001a2cd0: the clip key 0x0feddbea = the weapon's clip size; the asserts only build and drop
// their strings (their checks still run).
void XB_CDECL BroScr_RefillClip(uint32_t actor);
// 0x001a2e30 (dead): 0 for weapon 0x36b1a5, else BroScr_GetWeaponClipSize.
uint32_t* XB_CDECL BroScr_GetClipSizeOrZero(uint32_t* out, uint32_t actor);
// 0x001a3c90 (dead): pose from the wounded anim (0x06088a8c prone, 0x068698d3 crouch, stand anim
// 0x070b89a4 stand, else crouch); releases the unused string argument.
void XB_CDECL BroScr_SetWoundedPose(uint32_t ent, ScriptString unused);

}  // namespace game

namespace game {

// 0x001a6730: an actor takes a node turret: aim anim (`aim_anim`, else g_anim_standmg42gunner_aim),
// use + mode 0xad76f051; when the turret targets a "script_origin" other than itself: that entity
// as its target (key 0xcf1e52a5), mode 0x6c271e5a and a BroScr_TurretAutoAiOnNotify thread; then a
// BroScr_TurretManualTargetThink thread, notify 0xcb6a4b2a and use again.
void XB_CDECL BroScr_UseNodeTurret(uint32_t actor, uint32_t turret, uint32_t aim_anim);

}  // namespace game
