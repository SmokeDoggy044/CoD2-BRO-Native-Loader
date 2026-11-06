#pragma once

// Prologue level script bodies (level section 0x0041a000-0x00458000), body list b of the prologue push:
// the event threads, their setup tables and the small wrappers around them. Same script compiler output
// as levels_prologue_{a,b}.cpp; the level block is the word at 0x00921cbc (+4 = the level's own entity).
// Rows marked "thread switch" reach a script wait: their C is kept out of kFunctions (docs/
// levels_classes.csv) until script threads are coroutines. Names of the form BroLvl_prologue_* are ours.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

struct DynArray;
struct ScriptString;
struct ScrVec3;

// 0x0043fb40 (thread switch): FUN_00437bb0(self, name, the empty string); releases `name`.
void XB_CDECL BroLvl_prologue_StartEntArrayDefault(uint32_t self, ScriptString name);
// 0x0043fbd0: each entity of `targets` appended to the tank's array field 0xfe9bb316 (added empty
// first); frees the array.
void XB_CDECL BroLvl_prologue_AddTankTargets(uint32_t tank, DynArray targets);
// 0x0043fcd0 (no function in Ghidra yet; stored to g_scrValueTypeLookup 0x00922e70 by the level's
// init at 0x004588fb): the prologue's value-type lookup - per field hash the four value functions
// (parse, copy, ..., release) into the given out words; unknown keys write nothing.
void XB_CDECL LevelScript_prologue_ValueTypeLookup(uint32_t key, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d);
// 0x00442b60: the three hilltop Shermans of event 3: each spawned on its path node
// ("event3_sheman<n>_top_path<k>", v_sherman, vehicletype "ShermanTank", health 3000 / 3000 / 10000),
// BroScr_TankInit_2 "no_turret" thread, path attached, its targets ("event3_sheman<n>_top_targets1")
// handed to FUN_0043fbd0 and a FUN_0043d4f0 thread (9, 15) on it.
void XB_CDECL BroLvl_prologue_Event3HilltopShermans();
// 0x00444270: the entities named "<name>_targets" handed to FUN_0043fbd0 with `tank`, then a
// FUN_0043d4f0 thread (2, 5) on it. Releases `name`.
void XB_CDECL BroLvl_prologue_TankStartTargets(uint32_t tank, ScriptString name);
// 0x00447b10 / 0x00447c80 (thread switch): a script_origin moved to a random point of the box
// [min, max] (_InBoxXZ: y fixed at `y`), waiting base + Scr_RandomFloat(spread) and playing the level
// sound `sound` (level +0x57c) there, forever; with `loop` 0 the origin is deleted at once.
void XB_CDECL BroLvl_prologue_LoopSoundInBox(uint32_t self, uint32_t sound, ScrVec3 min, ScrVec3 max, uint32_t loop,
                                             float base, float spread);
void XB_CDECL BroLvl_prologue_LoopSoundInBoxXZ(uint32_t self, uint32_t sound, ScrVec3 min, ScrVec3 max, int32_t y,
                                               uint32_t loop, float base, float spread);
// 0x00447de0 (thread switch): the same over the points of `points`, shuffled before each pass.
void XB_CDECL BroLvl_prologue_LoopSoundAtPoints(uint32_t self, uint32_t sound, DynArray* points, uint32_t loop,
                                                float base, float spread);
// 0x0044cfb0: six drones (script_model at the truck, animname "event1_drones", a random model and gun
// from the level's +0x814 / +0x274 tables, forced LOD 4) linked to the truck's "tag_passenger<n>"
// (n = 2.., skipping 3 and 8), listed on level +0x770, each with a BroLvl_prologue_AttachPassengerDrones
// thread; the level's drone count +0x508 counts them.
void XB_CDECL BroLvl_prologue_TruckDrones(uint32_t self, uint32_t truck);
// 0x0044edf0: Bloomfield's dialogue texts into the level's line table (+0x124, speaker 0x1c593687).
void XB_CDECL BroLvl_prologue_InitDialogueBloomfield();
// 0x0044f4b0: the jeep passenger intro scene: speaker 0x8d39895a's anims in the level maps +0x19c /
// +0x1d8 (scene 0x06a6de2f) and its line 0x452b8e59 "PRO_E01L09b_JEE_ItsHellOu".
void XB_CDECL BroLvl_prologue_InitJeepPassIntroScene();
// 0x0044f5d0: the read-map guys' anims into the level's anim lists (+0x744, 0xa5c25861/2).
void XB_CDECL BroLvl_prologue_InitReadMapAnims();
// 0x0044f690: the drone tables: "prologue_drones", model and gun names, a death anim and the anim
// lists (+0x744, class 0x658f814a).
void XB_CDECL BroLvl_prologue_SetupDrones();
// 0x0044fa80 (no caller): the five anims of the drone list 0x0fc1193c (as BroLvl_prologue_SetupDrones).
void XB_CDECL BroLvl_prologue_SetDroneRunAnims();
// 0x00452f10 / 0x00452fd0 / 0x00453090 (thread switch): *out = BroLvl_prologue_SpawnVehicleDrone with
// the defaults for the arguments not given; releases `name`. Returns out.
uint32_t* XB_CDECL BroLvl_prologue_SpawnVehicleDrone3(uint32_t* out, uint32_t a2, uint32_t a3, ScriptString name);
uint32_t* XB_CDECL BroLvl_prologue_SpawnVehicleDrone4(uint32_t* out, uint32_t a2, uint32_t a3, ScriptString name,
                                                      uint32_t a5);
uint32_t* XB_CDECL BroLvl_prologue_SpawnVehicleDrone5(uint32_t* out, uint32_t a2, uint32_t a3, ScriptString name,
                                                      uint32_t a5, uint32_t a6);
// 0x004557e0: the plane-crash-distance step of event 4: entity var 0xe2a210cf, notify 0xc9fe433d on the
// level entity, then threads BroLvl_prologue_FriendlyChainThink (11.1), FUN_00424a00,
// BroLvl_prologue_Event4Flak and 0x00452b90 on it.
void XB_CDECL Bro_prologue__Event4_Plane_Crash_Distance(uint32_t self);
// 0x00457fd0: a set "checkpoint" cvar (not "" or "0") marks the level (+0x260), starts the matching
// event thread (+0x844 = the checkpoint hash) and restores the player's loadout.
void XB_CDECL BroLvl_prologue_StartFromCheckpoint();

}  // namespace game
