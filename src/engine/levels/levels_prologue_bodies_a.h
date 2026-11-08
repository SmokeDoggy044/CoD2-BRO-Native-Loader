#pragma once

// Prologue level script bodies (level section 0x0041a000-0x00458000, agent a of the prologue push).
// Rows marked "thread switch" reach a script wait: their C is kept out of kFunctions (docs/research/levels_classes.csv)
// until script threads are coroutines; the rest are registered Compare.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

struct ScriptString;
struct ScrVec3;

// 0x0041a270 (thread switch): waits `count` times for the level notify 0xb1ada748.
void XB_CDECL BroLvl_prologue_WaitLevelNotifyCount(uint32_t, int32_t count);
// 0x0041a2f0 (thread switch): BroLvl_prologue_SpawnVehicleDrone with undefined handles and an empty name for the
// optional arguments; *out = its result.
uint32_t* XB_CDECL BroLvl_prologue_SpawnVehicleDroneDefault(uint32_t* out, uint32_t a2, uint32_t a3, ScriptString name, uint32_t a5, uint32_t a6);
// 0x0041a600: deletes the entity found by script_chain `chain`, else by target `target` (GScr_Delete(0) with
// neither).
void XB_CDECL BroLvl_prologue_DeleteByChainOrTarget(uint32_t, ScriptString chain, ScriptString target);
// 0x0041a700: GScr_SaveCheckpoint(name's text).
void XB_CDECL BroLvl_prologue_SaveCheckpointNamed(uint32_t, ScriptString name);
// 0x0041aa50: level +0x894 / +0x864 = the zones "prologue_Vehicles01" / "prologue_Vehicles02".
void XB_CDECL BroLvl_prologue_FindVehicleZones();
// 0x0041aba0 (thread switch): aims the tank's turret at a script_origin 500 ahead (+104 up) linked to it until
// turret_on_target, then deletes it.
void XB_CDECL BroLvl_prologue_TurretAimAhead(uint32_t tank);
// 0x0041b910: pScr_strncpy(a2, the prologue anim-tree name `index` (12), a3) != 0; false for index >= 12.
uint8_t XB_CDECL BroLvl_prologue_AnimTreeNameOp(int32_t index, uint32_t a2, uint32_t a3);
// 0x0041bc10 (thread switch): level music by stage 1-4 after an optional delay (ends on the level notify
// 0xce4339fd).
void XB_CDECL BroLvl_prologue_InitMusic(uint32_t, int32_t stage, float delay);
// 0x0041be80: the objectives by stage (1-15 with sub-stages x.1-x.3): marks the previous one done, adds the next
// "active" with its position, or moves the current position; then makes it current.
void XB_CDECL BroLvl_prologue_FriendlyChainThink(uint32_t, float stage);
// 0x0041cd00 (thread switch): on goal, damages the entity by its health + 50 at its origin.
void XB_CDECL BroLvl_prologue_DamageOnGoal(uint32_t ent);
// 0x0041ce90 (thread switch): after 3.3 s plays "PROL_Event1_Tank_Scrape" on the tank.
void XB_CDECL BroLvl_prologue_TankScrapeSound(uint32_t tank);
// 0x0041cf10: ScrEnt_SetAnimname(guy, "event1_driver").
void XB_CDECL BroLvl_prologue_SetEvent1DriverAnimname(uint32_t guy);
// 0x0041d290 (thread switch): waits (0.25 s steps) until the entity's z is at most -192, then deletes it.
void XB_CDECL BroLvl_prologue_DeleteBelowFloor(uint32_t ent);
// 0x0041d360 (thread switch): on trigger: earthquake at the target's origin and its script_fxid sound along its
// forward vector.
void XB_CDECL BroLvl_prologue_QuakeOnTargetTrigger(uint32_t trigger);
// 0x0041d6b0 (thread switch): script_offtime of "event3_panzer1" = 1 for 13 s.
void XB_CDECL BroLvl_prologue_Event3Panzer1OffTime();
// 0x0041d850: *out = the player's stance rank (stand 3, crouch 2, prone 1) <= most (true for most >= 3).
uint8_t* XB_CDECL BroLvl_prologue_PlayerStanceAtMost(uint8_t* out, uint32_t, int32_t most);
// 0x0041d950 (thread switch): moves the entity forever to random points of a box (a wider one once level +0x860 is
// set).
void XB_CDECL BroLvl_prologue_DriftAroundCourtyard(uint32_t ent);
// 0x0041dcf0 (thread switch): the wall explosion sounds on the left, then the right wall.
void XB_CDECL BroLvl_prologue_Event4TigerThroughWall();
// 0x0041de70 (thread switch): "FLAK_METAL_HIT" at 100, then after 3 s at 75.
void XB_CDECL BroLvl_prologue_FlakMetalHits(uint32_t ent);
// 0x0041df30 (thread switch): sends the actor to the tag's origin on `ent` (goal radius 64 meanwhile), then counts
// level +0x870 up.
void XB_CDECL BroLvl_prologue_ActorGoToTag(uint32_t actor, uint32_t ent, ScriptString tag);
// 0x0041e060 (thread switch): the four flak fire effect entities: loop sound, metal hits, then deleted.
void XB_CDECL BroLvl_prologue_Event4FlakFireFx();
// 0x0041e4a0 (thread switch): "collision_for_flak" is on only while the player uses the turret "jl830".
void XB_CDECL BroLvl_prologue_FlakCollisionForPlayer();
// 0x0041e6a0 (thread switch): when `other` dies, 10000 damage to `ent`.
void XB_CDECL BroLvl_prologue_KillOnDeathOf(uint32_t ent, uint32_t other);
// 0x0041e8b0 (thread switch): red smoke at the grenade: a spinning script_origin with the smoke sound for 30 s,
// then both deleted.
void XB_CDECL BroLvl_prologue_Event5RedSmoke(uint32_t, uint32_t grenade);
// 0x0041eac0: pitch field (from the angles when undefined) += 5 x step while below 45, clamped at 45.
void XB_CDECL BroLvl_prologue_PitchUpStep(uint32_t ent, float step);
// 0x0041ec30 (thread switch): once the entity is at most `height` high: "artillery_generic" at its origin, level
// notify 0x7ad8c8f5, entity deleted.
void XB_CDECL BroLvl_prologue_ArtilleryWhenBelow(uint32_t, uint32_t ent, float height);
// 0x0041edb0 (thread switch): "building_fall" from a script_origin at pos until done.
void XB_CDECL BroLvl_prologue_BuildingFallSound(uint32_t, ScrVec3 pos);
// 0x0041ee80 (thread switch): BroScr_ShellIncomingThenImpact (512, 50, 1) at a script_origin at pos, deleted on
// 0xbd1fe198.
void XB_CDECL BroLvl_prologue_ShellImpactAt(uint32_t, ScrVec3 pos, uint32_t type);
// 0x0041ef60 (thread switch): fog pass, three player view lerps (time / 2 each), an earthquake and "Body_Land".
void XB_CDECL BroLvl_prologue_PlayerKnockdown(uint32_t, float time);
// 0x0041f1a0: allows all player stances, then disallows the two others than `stance` (stand / crouch / prone;
// 0xc0d4ba61: not 0x200000).
void XB_CDECL BroLvl_prologue_PlayerAllowOnlyStance(uint32_t, uint32_t stance);

}  // namespace game
