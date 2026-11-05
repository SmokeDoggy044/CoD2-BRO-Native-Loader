#pragma once

// Shared compiled level scripts (levelscript module, push c, part 2): path-node predicates, node
// lookups at an actor's position, the _spawner_gmi death/damage callbacks (friendly-fire mission
// failure), trigger on/off by moving the trigger 10000 units down, credits names and small AI
// helpers. Engine calls go through the interface block's slots (pScr_*, 0x00922xxx). "out"
// results are the script's by-value returns through a hidden pointer; the function returns it.

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "scr_access.h"

namespace game {

// 0x0018def0: BroScr_ActorMayMoveToPointForStance with a bool result.
uint8_t* XB_CDECL BroScr_ActorMayMoveToPointForStance_2(uint8_t* out, uint32_t actor, ScrVec3 point, uint32_t yaw);
// 0x0018e050 / 0x0018e070: *value == other / != other.
uint8_t* XB_CDECL BroScr_CompareIntEqual(uint8_t* out, const int32_t* value, int32_t other);
uint8_t* XB_CDECL BroScr_CompareIntNotEqual(uint8_t* out, const int32_t* value, int32_t other);
// 0x0018e090: whether the actor's weapon (int +0xf0, defined) is in the anim-script object's weapon
// table (0x00922efc +0x130, count +0x138, 24-byte entries keyed by the first word).
uint8_t* XB_CDECL BroScr_ActorWeaponInLevelTable(uint8_t* out, uint32_t actor);
// 0x0018e100: the type of the node at the entity's position (BroScr_GetNodeAtPosition 0x00184230),
// "none" without one.
ScriptString* XB_CDECL BroScr_GetNodeTypeAtPosition(ScriptString* out, uint32_t ent);
// 0x0018e1b0: the origin of the node at the entity's position, else the entity's origin.
uint32_t* XB_CDECL BroScr_GetCoverNodeOrOrigin(uint32_t* out, uint32_t ent);
// 0x0018e220: whether anim debugging is on (cvar animDebug 1) or the entity is the anim-script
// object's valid debug target (+0x128).
uint8_t* XB_CDECL BroScr_IsAnimDebugTarget(uint8_t* out, uint32_t ent);
// 0x0018e280: value == a, b or c.
uint8_t* XB_CDECL BroScr_IsOneOf3(uint8_t* out, uint32_t value, uint32_t a, uint32_t b, uint32_t c);
// 0x0018e2b0: orient mode "face motion" when `mode` is 0xe2cb8ae5.
void XB_CDECL BroScr_SetOrientFaceMotionIf(uint32_t actor, uint32_t mode);
// 0x00198a40..0x00198bf0: path-node predicates for a valid handle (not 0 or -1; vehicle nodes:
// not -1): script_door set, script_delay defined (key 0x807116c4), script_chain set (0x001e4640),
// script_fallback (0x8bbe4822) / script_mg42 (0xec070be6) defined, script_waittill non-empty,
// vehicle node script_delay defined.
uint8_t* XB_CDECL BroScr_PathNodeHasDoor(uint8_t* out, uint32_t node);
uint8_t* XB_CDECL BroScr_PathNodeHasScriptDelay(uint8_t* out, uint32_t node);
uint8_t* XB_CDECL BroScr_IsChainNode(uint8_t* out, uint32_t node);
uint8_t* XB_CDECL BroScr_PathNodeHasScriptFallback(uint8_t* out, uint32_t node);
uint8_t* XB_CDECL BroScr_NodeHasScriptWaittill(uint8_t* out, uint32_t node);
uint8_t* XB_CDECL BroScr_PathNodeHasScriptMg42(uint8_t* out, uint32_t node);
uint8_t* XB_CDECL BroScr_VehNodeHasScriptDelay(uint8_t* out, uint32_t node);
// 0x00199040: debug circle of the entity's radius: 16 steps of 0.3925 rad through Scr_Cos/Scr_Sin
// with nothing drawn (the radius stays 16: the script_radius slot is read and dropped).
void XB_CDECL BroScr_DebugDrawRadiusCircle(uint32_t ent);
// 0x00199a20: the actor's credits name (string +0xec): its own field 0xa1763cc1 when set, else a
// random soldier name (BroScr_SetPrivateName) when it has none. Skipped with an undefined `value`
// in the "credits" level.
void XB_CDECL BroScr_SetCreditsName(uint32_t actor, uint32_t value);
// 0x00199b50: adds (key, 0, ent) to the level object's keyed list (+0x50) unless it is there.
void XB_CDECL BroScr_AddKeyedEnt(uint32_t ent, uint32_t key);
// 0x0019aef0: whether the actor may use the turret at `node` now: not busy (+0x1374), its
// turret_use_time passed, the player 100+ away from the node and (with a goal node) the goal node
// within 1100 of the player.
uint8_t* XB_CDECL BroScr_TurretUseTimeCheck(uint8_t* out, uint32_t actor, uint32_t turret, uint32_t node);
// 0x0019b050: notifies the entity's friendly_mg42 (added undefined) "friendly_finished_using_mg42".
void XB_CDECL BroScr_FriendlyFinishedMg42(uint32_t ent);
// 0x0019b390: plays "human_crunch" on the dead entity when a vehicle (vehicletype set) hit it.
void XB_CDECL Bro__spawner_gmi__tanksquish(uint32_t self, uint32_t unused, uint32_t attacker);
// 0x0019b440 / 0x0019b6b0 / 0x0019b760 / 0x0019b540: friendly fire by the player (0x009236dc):
// the friendly-fire mission failure thread (0x00181f70) on the level, unless the friend's field
// 0x7599373d is defined and 0, or its int +0x240 is 0x36b1a5. Death counts deaths (+0xdc: four
// reset both counters); damage sums the damage (+0xf4, from 600). Death callbacks re-register
// themselves first (GScr_GetEntVar 0x6730ae8).
void XB_CDECL Bro__spawner_gmi__Allies_Death(uint32_t self, uint32_t attacker);
void XB_CDECL Bro__spawner_gmi__KillFriends_Damage_Think(uint32_t self, int32_t damage, uint32_t attacker);
void XB_CDECL Bro__spawner_gmi__KillFriends_MissionFail_OnDamage(uint32_t self, uint32_t attacker);
void XB_CDECL Bro__spawner_gmi__KillFriends_MissionFail_OnDeath(uint32_t self, uint32_t attacker);
// 0x0019b830: switches a trigger off: saves its origin as realorigin (when not defined yet) and,
// while it is there, moves it 10000 down.
void XB_CDECL BroScr_TriggerOff(uint32_t trigger);
// 0x0019b940: moves the trigger back to its realorigin (when defined).
void XB_CDECL BroScr_TriggerOn(uint32_t trigger);
// 0x0019b990 / 0x0019ba00: saves the actor's float +0x82c in its script object (0x98b54d9f) and
// sets 3.0 / restores it.
void XB_CDECL BroScr_PushActorFloat82c(uint32_t actor);
void XB_CDECL BroScr_PopActorFloat82c(uint32_t actor);

}  // namespace game
