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

namespace game {

struct ScriptString;

// 0x001abb00 (FUN_001abb00; proposed BroScr_GetFieldUnlessAutoOrSemi): 0 for fire types auto
// (0x315aef) / semi (0x3819c9), else the int field 0xc786fa48. Returns out.
uint32_t* XB_CDECL BroScr_GetFieldUnlessAutoOrSemi(uint32_t* out, uint32_t actor);
// 0x001abb70: whether the clip key 0x0feddbea is at most int(clip size x `fraction`) (0 for the
// undefined float; clip 0 without a weapon); false while key 0x0ccee5c1 is 0x1c6ce.
uint8_t* XB_CDECL BroScr_NeedsReload(uint8_t* out, uint32_t actor, uint32_t fraction_bits);
// 0x001abfc0: blends three aim anims by the yaw to the enemy relative to `base` (wrapped to
// -180..180) against the limits a < b < c: weights clamped to 0.01..0.99 between neighbours.
void XB_CDECL BroScr_BlendAimAnimsByYaw(uint32_t ent, uint32_t anim_a, uint32_t anim_b, uint32_t anim_c, uint32_t a_bits,
                                       uint32_t b_bits, uint32_t c_bits, uint32_t base_bits);
// 0x001ac2c0 / 0x001ac360: the cover-left / cover-right anim set (an anim-script global) for the
// weapon (0xa39d17dd, pistol) or idle set 0x61, by stand / other pose.
uint32_t XB_CDECL BroScr_GetCoverLeftAnimSet(uint32_t actor);
uint32_t XB_CDECL BroScr_GetCoverRightAnimSet(uint32_t actor);
// 0x001ac400 (functor target): end on 0x07b4e7f7, anim script 0x5f32132f, pose movement
// (0xc1165808, 0x385d90), sentient int +0x38 = 0.
void XB_CDECL BroScr_AnimScript_GrenadeRecover(uint32_t ent);
// 0x001ac470: the combat / crouch run anims for the weapon (pistol or none, LMG, other) and the
// two random run-rate floats when undefined (0.8 + rand 0.4; the second 0.9 + for non-axis).
void XB_CDECL BroScr_GetCombatRunAnim(uint32_t actor);
// 0x001acb60: clears the anim struct 0x12993a44 (+0 = 0, +0x1c/+0x24 undefined, string +0x20
// released). 0x001acc10 (inside Ghidra's 0x001acb60; proposed BroScr_SetAnimStruct_12993a44, used
// by the levels): copies a struct in (string "normal" when unset, anim g_anim_Body for 0xfefefefe).
void XB_CDECL BroScr_ResetAnimStruct_12993a44(uint32_t ent);
void XB_CDECL BroScr_SetAnimStruct_12993a44(uint32_t ent, const uint32_t* source);
// 0x001ad3f0: moves the actor's gun to `hand` (0x3798b4 left / 0x7151174 right, anything else:
// none): reattaches the current model at the held hand's tag, detaches it from the new hand's tag,
// stores the model name (0x81ea0dd3) and hand (0x1de0c645), then BroScr_GetCombatRunAnim. Nothing
// when hand and model already match.
void XB_CDECL BroScr_PutGunInHand(uint32_t actor, uint32_t hand);

// 0x001b2c80: unlinks `a` from its melee partner (field 0x31aaf853) and `b` from a partner that is
// not paired back; false for an invalid `b` or one paired both ways with another.
uint8_t* XB_CDECL BroScr_MeleePair_Relink(uint8_t* out, uint32_t a, uint32_t b);
// 0x001b4da0: detaches all, then puts the gun back in the stored hand (0x1de0c645) when defined.
void XB_CDECL BroScr_ReattachGunInHand(uint32_t actor);
// 0x001b60c0 (functor target): BroScr_ReattachGunInHand on each "allies" AI without field 0xa1763cc1
// (the allies spawners only counted), at most 100000.
void XB_CDECL BroScr_ReattachAlliesGuns();
// 0x001c2b40 (FUN_001c2b40; proposed BroScr_AnimMoveUntilObstacle): advances and applies the anim
// move for seconds x 20 frames (tracking it through 0x001c2aa0) until an obstacle: kind 0x074645e4
// turns the actor to face its yaw (true), 0x61710285 stops (false).
uint8_t* XB_CDECL BroScr_AnimMoveUntilObstacle(uint8_t* out, uint32_t actor, uint32_t seconds_bits);

// 0x001dab60: the anim object's pose-movement handlers (+0x168..+0x188: BroScr_PoseMove_StandStop
// .. BroScr_PoseMove_ProneRun).
void XB_CDECL BroScr_InitPoseMovementTable();
// 0x001a00c0 (unhookable: GScr_GetTime is called at +4, inside the first 5 bytes; not registered):
// stores the time (0x521b4bf0), the current pose (0x6f631b0e) and the previous movement / wounded
// anim / 0x175567a8 copies, then pose (0xdce273fb) and movement (0x358ad77b; undefined or 0 = the
// current one 0x0ccee5c1); pose 0 = the current pose; calls the handler for pose (stand 0x070b89a4,
// crouch 0xc1165808, prone 0x0738b1a5) x movement (stop 0x385d90, walk 0x3a09eb, run 0x1c6ce) from
// the table above (out of range: the last), then the time, the pose (0xcbffac0d) and the movement
// (0xcaeba6a6) again.
void XB_CDECL BroScr_SetPoseMovement(uint32_t ent, uint32_t pose, uint32_t movement);

}  // namespace game

