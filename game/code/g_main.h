#pragma once

// Game module: level and client setup (G_InitGame's helpers: clearing the level locals, the
// clients' player state and persistent data) and the shake/rumble manager Com_Init creates.

#include <cstddef>
#include <cstdint>

#include "compiler.h"
#include "title_data.h"

namespace game {

// 0x00024260: allocates the shake/rumble manager (0x24 bytes: a word set to 1, then two empty
// lists {head, sentinel {first, last}}) into shakeRumbleMgr (0x0095f5c4; 0 when out of memory).
void XB_CDECL ShakeRumble_CreateManager();
// 0x00026750 (player state in EAX): clears a player state (0x500 bytes): two vectors
// {0, 0, 0, 1} and {0, 0, 0, 0} (0x0082e290 / 0x0082e110) at +0 and +0x490, the rest zero
// except the padding the original skips (+0x8d..+0x8f, +0x3f8, +0x428, +0x488, +0x4ce).
uint8_t* XB_REGS1 XB_CDECL G_ClearPlayerState(uint8_t* ps);
// 0x00026ae0 (persistent data in EAX): clears a client's persistent block (0x64 bytes).
uint8_t* XB_REGS1 XB_CDECL G_ClearClientPersistant(uint8_t* pers);
// 0x00026b60 (client in ESI): G_ClearPlayerState, G_ClearClientPersistant (+0x520), the rest of
// the client (+0x584..+0x638) zero except the vector {0, 0, 0, 1} at +0x5b0. Returns 0 (EAX).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL G_ClearClient(uint8_t* client);
extern "C" void G_ClearClient_entry(void);
// 0x00026cc0 (level locals in ESI, 0x00985370): clears the level locals (0x2708 bytes) except
// +0xb4..+0x2b4 and the padding; the script strings +0xb50 and +0xcb8 become "".
extern "C" PLATFORM_HIDDEN void XB_CDECL G_ClearLevelLocals(uint8_t* level);
extern "C" void G_ClearLevelLocals_entry(void);

// 0x00231670 (entity in EAX): frees an entity's physics: its ragdoll (+0x238, when +0x244 is
// set; Ragdoll_FreePhysics), a type-1 vehicle's physics object (+0x248 -> +0x300;
// VehiclePhys_Destroy, then cleared), else Phys_RemoveEntity.
void XB_REGS1 XB_CDECL G_FreeEntityPhysics(uint8_t* ent);
// 0x00233bd0: the physics part of a game frame: the frame time times 0x008ff028 (x87) to the
// ragdolls, Phys_RunFrame and the physics objects, then each physics vehicle (0x008feb70).
void XB_CDECL G_VehiclePhysicsFrame(float frame_time);
// 0x002345a0 (entity in EAX): pushes an entity with dir * scale at a point (world space, or body
// space when local_point; all components under 0.0001 = at the body's origin): a ragdoll (started
// first, the actor killed), a type-1 vehicle (gets its physics object, no force), else a physics
// object (created when the entity has none, flag 0x400000 of +0x2a4).
void XB_REGS1 XB_CDECL G_PhysicsLaunch(uint8_t* ent, const float* point, const float* dir, float scale,
                                       uint32_t local_point, uint32_t hit_location);
// 0x0024a070: looks up the 44 animation-script functions (g_animScriptInit 0x00987b70 ..
// 0x00987bf4) by name hash in g_broFuncs; a missing one is 0.
void XB_CDECL G_InitAnimScripts();

// 0x0024c9f0: applies the queued entity-list changes (removals 0x00987a78, additions
// 0x0097a640) to the entity list 0x00984cec and empties both queues.
void XB_CDECL G_FlushEntityListChanges();
// 0x0024cab0: queues an entity for removal from the entity list; when it is queued already,
// takes it out again (the original counts the addition queue down for that).
void XB_CDECL G_EntityList_QueueRemove(uint32_t ent);
// 0x0024cb10: cancels a queued removal of the entity, else queues it for addition (0x0097a640).
void XB_CDECL G_EntityList_QueueAdd(uint32_t ent);
// 0x0024cbd0: links entities with equal team strings (+0x2f8) into team chains.
void XB_CDECL G_FindTeams();
// 0x0024cd70: the 32 entities of the handles at 0x00985e30 think next frame with think 0xc.
// No references (dead).
void XB_CDECL G_FreeDroppedItems();

// 0x0024d250 (first argument in ECX): the command arguments from there on, joined by spaces
// into 0x0098b858.
char* XB_THISCALL ConcatArgs(int32_t start);
// 0x0024d8b0: the game's console commands: "entitylist" and "listvehicles" are accepted (1),
// with no effect.
uint32_t XB_CDECL ConsoleCommand();
// 0x0024d940: resizes the vehicle pool to `slots`, keeping the used slots.
void XB_CDECL G_SetupVehiclePool(uint32_t slots);

// 0x0024f060 (entity in ESI, list in EDI): gives the entity a handle (+0x224) if it has none
// and appends it to the list (count +0x2aac, entries +0x2ab0, max 0x1000; index in +0x228).
extern "C" PLATFORM_HIDDEN void XB_CDECL G_EntityList_Add(uint8_t* ent, uint8_t* list);
extern "C" void G_EntityList_Add_entry(void);
// 0x0024f860: the hint-string configstring (0x273 + 0..31) holding `text`, set into the first
// empty one if none does; 1 and *out = the index, else 0 and -1.
uint32_t XB_CDECL G_FindHintStringIndex(int32_t* out, const char* text);
// 0x0024fe10: frees the anim trees' notetracks (1..g_numAnimTrees) and the game's string table.
void XB_CDECL G_FreeAnimTrees();
// 0x0024fe60: frees the actor corpses past the corpse limit (0x00950c5c, 1..16).
void XB_CDECL G_TrimCorpseQueue();
// 0x0024b620 (no references): when the game owns the string table (0x0095f5a0 == 1), frees it
// (0x0095f59c) unless `keep` and clears the owner.
void XB_CDECL G_FreeStringTable(uint32_t keep);

}  // namespace game

// Calls a title function with all six registers and `count` stack words (args[0] pushed last);
// returns its EAX.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL g_ent_call(uint32_t function, const game::TitleRegs* regs,
                                                     uint32_t count, const uint32_t* args);
