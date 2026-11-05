#pragma once

// Shared compiled level scripts (levelscript module, push c, part 3): the anim scripts' weapon
// class table (anim-script object 0x00922efc +0x130, count +0x138; 24-byte entries: weapon hash,
// type hash, fire type hash, fire rate, +0x10 float, clip size), combat delay timers (script key
// 0x900ed884), look-at stops, melee pairs and radius damage. Values live in the entities' script
// key tables (ScrCall_GScr_GetEntScriptObject; the shared empty table without one) and are added
// undefined on first use. "out" results are by-value returns through a hidden pointer.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

struct ScriptString;

// 0x0019bd80: radius damage at the entity's origin: radius script_radius (128 when not a field,
// undefined float when it is undefined), damage the 0xe625b35 key (read twice) as floats.
void XB_CDECL BroScr_InitScriptRadius(uint32_t ent);
// 0x0019d3d0: whether the nearest enemy (500 when undefined) is nearer than the anim-script
// object's +0x198 range.
uint8_t* XB_CDECL BroScr_IsNearestEnemyInRange(uint8_t* out, uint32_t sentient);
// 0x0019e050 (FUN_0019e050, proposed BroScr_AnimFaceResetKeys): unless field 0x03375c37 is defined:
// it and 0x257acba4 set to 0, and the anim-script object's +0x98 string assigned to key 0xf8cb7452.
void XB_CDECL BroScr_AnimFaceResetKeys(uint32_t ent);
// 0x0019e2d0: stops looking at (rate: 0.75 x the 0x5e1a2a0c key when that field is defined, else
// 1000) and notifies 0xe099bb44.
void XB_CDECL BroScr_StopLookingAt(uint32_t actor);
// 0x0019e2d0 wrapper at 0x001a0d20 (the next 0x8a bytes are another function, 0x001a0d30).
void XB_CDECL BroScr_StopLookingAt_2(uint32_t actor);
// 0x001a0dc0: combat delay (key 0x900ed884) = now + seconds x 1000, or with undefined seconds now +
// the object's +0x18c + random(+0x8).
void XB_CDECL BroScr_SetCombatDelay(uint32_t ent, int32_t seconds);
// 0x001a0e60: BroScr_UpdateWoundedTime (0x0019fa40) with anim 0x776460f / time 0 for undefined ones.
void XB_CDECL BroScr_UpdateWoundedTimeDefaults(uint32_t ent, uint32_t anim, int32_t time);
// 0x001a0ea0: appends a weapon class entry (the names hashed; the by-value strings released).
void XB_CDECL BroScr_AddWeaponClassEntry(uint32_t unused, ScriptString weapon, ScriptString type, ScriptString fire_type,
                                         uint32_t fire_rate, uint32_t field4, uint32_t clip_size);
// 0x001a0f70: the actor's weapon type (entry +4), 0x36b1a5 for no weapon, an undefined weapon,
// hasweapon defined and 0, weapon model 0x36b1a5 or a weapon not in the table.
uint32_t* XB_CDECL BroScr_GetWeaponType(uint32_t* out, uint32_t actor);
// 0x001a1040 / 0x001a10c0 / 0x001a1140 / 0x001a11c0: entry +8 (default 0x36b1a5), +0xc (0.0f),
// +0x10 (0.0f), +0x14 (0) of the actor's weapon.
uint32_t* XB_CDECL BroScr_GetWeaponFireType(uint32_t* out, uint32_t actor);
uint32_t* XB_CDECL BroScr_GetWeaponFireRate(uint32_t* out, uint32_t actor);
uint32_t* XB_CDECL BroScr_GetWeaponTableField4(uint32_t* out, uint32_t actor);
uint32_t* XB_CDECL BroScr_GetWeaponClipSize(uint32_t* out, uint32_t actor);
// 0x001a14d0: BroScr_DelayTimerOnScriptChange (0x001a1290) for `first` and, when defined, `second`.
void XB_CDECL BroScr_DelayTimerOnScriptChangeBoth(uint32_t ent, uint32_t first, uint32_t second);
// 0x001a1510: with a valid +0x3c reference, pushes the combat delay (key 0x900ed884) out to now +
// +0x18c + random(+0x8) when that is later; whether the delay is still in the future.
uint8_t* XB_CDECL BroScr_UpdateCombatDelay(uint8_t* out, uint32_t sentient);
// 0x001a1600: false only when the hasweapon field is defined and 0.
uint8_t* XB_CDECL BroScr_HasWeapon(uint8_t* out, uint32_t actor);
// 0x001a18c0: yaw of the vector from the entity's origin to its enemy's eye (BroScr_GetEnemyEyePos).
uint32_t* XB_CDECL BroScr_GetYawToEnemy(uint32_t* out, uint32_t ent);
// 0x001a19a0: links a melee pair (b's 0x236fc839 = a, a's 0x31aaf853 = b); the failed checks only
// build and drop the "bad parameter" string (asserts compiled out).
void XB_CDECL BroScr_MeleePair_Link(uint32_t a, uint32_t b);

}  // namespace game

namespace game {

struct ScrVec3;

// 0x001b2990: whether the actor (armed, with a valid enemy or `target`) can shoot at the target's
// eye (or origin for a non-sentient; the enemy's eye without a target) or 20 below it with
// `offset`; unless `flag` is 0, first also from its "tag_flash" corrected by eye - tag.
uint8_t* XB_CDECL BroScr_GetMuzzleOrigin(uint8_t* out, uint32_t actor, ScrVec3 offset, uint32_t target, uint32_t flag);
// 0x001ba930: BroScr_GetMuzzleOrigin with a zero offset.
uint8_t* XB_CDECL BroScr_GetMuzzleOriginCall(uint8_t* out, uint32_t actor, uint32_t target, uint32_t flag);
// 0x001ba990: BroScr_GetMuzzleOrigin from `pose` (stand / crouch / prone): blocked weapons fail
// (prone with 0x371a54 succeeds), the height change from the current pose, `offset` taken from
// local to world relative to the origin unless undefined; target 0.
uint8_t* XB_CDECL BroScr_CanShootFromPose(uint8_t* out, uint32_t actor, uint32_t pose, ScrVec3 offset, uint32_t flag);
// 0x001c24e0 (dead) / 0x001c30e0: the nearest enemy is beyond anim-object +0x90 (500 undefined)
// and the actor can shoot prone (flag: the word at 0x00922ebc / 1): may it move to `point` prone.
uint32_t* XB_CDECL BroScr_ShouldGoProne(uint32_t* out, uint32_t actor, ScrVec3 point, uint32_t yaw);
uint32_t* XB_CDECL BroScr_ShouldGoProne_2(uint32_t* out, uint32_t actor, ScrVec3 point, uint32_t yaw);

// 0x001c4360: whether the actor can shoot at its enemy (BroScr_GetMuzzleOrigin, zero offset); the
// miss count 0xa31c116e is reset on success, else counted (unless `count_misses` is 0; undefined
// counts); above 4 misses: GScr_ActorFindBetterCover and reset.
uint32_t* XB_CDECL BroScr_CheckCanShoot(uint32_t* out, uint32_t actor, uint32_t count_misses);

}  // namespace game

