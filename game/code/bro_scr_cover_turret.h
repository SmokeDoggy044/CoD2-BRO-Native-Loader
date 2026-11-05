#pragma once

// Shared compiled level scripts (levelscript module, push c, part 1): the mg42 anim-handle table,
// the cover-left/right anim sets, turret gunner anim knobs and small AI script helpers. They call
// the engine through the interface block's slots (pScr_*, 0x00922xxx). "out" results are the
// script's by-value returns through a hidden pointer (bool, float, int words); the function
// returns that pointer.

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "scr_access.h"

namespace game {



// 0x0018b010 (EAX out, EDX value, ECX hash): the mg42 anim handles (0x88333b05 -> 0x008cf7b0,
// 0x7eb70b0f -> 0x008cf7a4, 0x19effa78 -> 0x008cf7bc). With `out` the handle is copied there and
// returned, else `value` is stored and returned; 0 for other hashes.
uint32_t XB_REGS3 XB_CDECL BroScr_BAnimHandles_mg42(uint32_t* out, uint32_t value, uint32_t hash);
// 0x0018b080 / 0x0018c180 / 0x0018d9a0: empty script functions (ret).
void XB_CDECL BroScr_EmptyFunc_77();
void XB_CDECL BroScr_EmptyFunc_78();
void XB_CDECL BroScr_EmptyFunc_79();
// 0x0018b090: get/set in an anim-handle table by its hash: mg42 (0x00371ab2) or generic_human
// (0x3b8fe918, 0x00184c60 with EAX hash, EDX value, ECX out); tail calls, 0 for other tables.
uint32_t XB_CDECL BroScr_BAnimHandle_GetSet(uint32_t table, uint32_t hash, uint32_t* out, uint32_t value);
// 0x0018b0d0 / 0x0018b0e0: return false (EAX 0) / true (AL 1).
uint32_t XB_CDECL BroScr_ReturnFalse();
uint8_t XB_CDECL BroScr_ReturnTrue();
// 0x0018b0f0: the handle under `hash` in the table, through the table's own argument slot (the
// table writes it there and returns it).
uint32_t XB_CDECL BroScr_BAnimHandle_Lookup(uint32_t table, uint32_t hash);
// 0x0018b1e0: the path node's radius (pScr_ScrPathNode_GetRadius) into *out.
float* XB_CDECL BroScr_GetNodeRadius(float* out, uint32_t node);
// 0x0018b200: whether `handle` is neither 0 nor -1 and valid (pScr_GScr_IsValidHandle).
uint8_t* XB_CDECL BroScr_IsValidHandle(uint8_t* out, uint32_t handle);
// 0x0018bab0: whether the mg42 turret may target the player's stance: prone, or crouch when the
// level object's +0x1c8 string is "cow" (false while the level object's +0x190 entity is invalid).
uint8_t* XB_CDECL BroScr_MgTurretCanTargetStance(uint8_t* out);
// 0x0018bba0: a random soldier name for the actor: rank (localized; "Pvt. " for the plain rank
// in the first table) + " " + a name from 0x008cd510 (101 names, counter +0x60) or 0x008cd6a8
// (49, +0x154); the counters start random and step on. Actor int +0xf0 == 0xd8e9c234 picks the
// other rank string.
ScriptString* XB_CDECL BroScr_SetPrivateName(ScriptString* out, uint32_t actor);
// 0x0018be00: deletes every entity of the level object's (key, ?, entity) list (+0x50, count
// +0x58, 12-byte entries) with `key` other than `keep`, restarting the search after each delete.
void XB_CDECL BroScr_DeleteOthersWithKey(uint32_t keep, uint32_t key);
// 0x0018be70: deletes the entity when its targetname is "flood_spawner".
void XB_CDECL BroScr_DeleteFloodSpawner(uint32_t ent);
// 0x0018c0b0: death callback of the axis spawner: re-registers itself (GScr_GetEntVar 0x6730ae8),
// and when the player (0x009236dc) killed it, notifies the level (0xb1ada748) and counts +0xdc.
void XB_CDECL Bro__spawner_gmi__Axis_Death(uint32_t self, uint32_t attacker);
// 0x0018c430: aim blend times 0x009230ec / 0x0092379c (undefined: 0.1, and 2.0 or the first when
// that is larger).
void XB_CDECL BroScr_SetAimBlendTimes(uint32_t first, uint32_t second);
// 0x0018c540: ends on 0x7b4e7f7, knobs the turret's 0x008cf684 anim and the gunner's idle.
void XB_CDECL BroScr_TurretSetIdleKnobs(uint32_t gunner, uint32_t turret);
// 0x0018c5f0: knobs the turret's 0x008cf684 anim and the 30cal gunner's fire anim.
void XB_CDECL BroScr_Turret30calFire_2(uint32_t gunner, uint32_t turret);
// 0x0018c650: the nearest enemy distance, 500 when undefined.
uint32_t* XB_CDECL BroScr_GetFloatOrDefault500(uint32_t* out, uint32_t sentient);
// 0x0018c710..0x0018d840 (bro_scr_cover_animsets.inc, tools/gen_cover_animsets.py): fill an
// anim-set record (yaw words +0x0/+0x24..+0x2c, a vector +0x30, anims +0x10.., hash +0x20).
uint8_t* XB_CDECL BroScr_CoverLeftAnimSet_1(uint8_t* set);
uint8_t* XB_CDECL BroScr_CoverLeftAnimSet_2(uint8_t* set);
uint8_t* XB_CDECL BroScr_CoverLeftAnimSet_3(uint8_t* set);
uint8_t* XB_CDECL BroScr_CoverLeftAnimSet_4(uint8_t* set);
uint8_t* XB_CDECL BroScr_CoverLeftAnimSet_5(uint8_t* set);
uint8_t* XB_CDECL BroScr_CoverLeftAnimSet_6(uint8_t* set);
uint8_t* XB_CDECL BroScr_CoverLeftAnimSet_7(uint8_t* set);
uint8_t* XB_CDECL BroScr_CoverRightAnimSet_1(uint8_t* set);
uint8_t* XB_CDECL BroScr_CoverRightAnimSet_2(uint8_t* set);
uint8_t* XB_CDECL BroScr_CoverRightAnimSet_3(uint8_t* set);
uint8_t* XB_CDECL BroScr_CoverRightAnimSet_4(uint8_t* set);
uint8_t* XB_CDECL BroScr_CoverRightAnimSet_5(uint8_t* set);
uint8_t* XB_CDECL BroScr_CoverRightAnimSet_6(uint8_t* set);
uint8_t* XB_CDECL BroScr_CoverRightAnimSet_7(uint8_t* set);
// 0x0018cfe0 / 0x0018d930: fill the seven cover-left / cover-right anim sets once (flag 0x009233e4
// / 0x00922f00, also when it equals the undefined bool).
void XB_CDECL BroScr_CoverLeftInitAnimSets();
void XB_CDECL BroScr_CoverRightInitAnimSets();
// 0x0018d9b0: whether the actor may move to `point` (by value) facing `yaw`; prone check when its
// +0x184 pose is 0x738b1a5. A 0/1 word.
uint32_t* XB_CDECL BroScr_ActorMayMoveToPointForStance(uint32_t* out, uint32_t actor, ScrVec3 point, uint32_t yaw);
// 0x0018da00: runs 0x009226a0 on the entity for the note 0x6e33cc5.
void XB_CDECL BroScr_MeleeNoteTrackHandler(uint32_t ent, uint32_t note);
// 0x0018da70 / 0x0018db30 / 0x0018db80: mg42 gunner aim knobs (prone with the turret's 0x008cf7a4
// anim; prone loop after an end-on 0x7b4e7f7; stand).
void XB_CDECL BroScr_TurretProneMG42Aim(uint32_t gunner, uint32_t turret);
void XB_CDECL BroScr_TurretProneMG42AimLoop(uint32_t gunner);
void XB_CDECL BroScr_TurretStandMG42Aim(uint32_t gunner);

}  // namespace game
