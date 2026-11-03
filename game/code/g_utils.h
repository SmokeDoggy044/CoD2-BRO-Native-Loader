#pragma once

// Game module: small entity helpers (gentity_t is reached by byte offsets; the layout is in
// tools/data/structs.txt).

#include <cstdint>

#include "compiler.h"

namespace game {

struct ScriptString;

// Calls vectoangles (0x002a0850; vector in ESI, angles out in EDI) at its title address.
extern "C" PLATFORM_HIDDEN void XB_CDECL vectoangles_call(const float* v, float* angles);

// 0x00013860 (handle pointer in EAX): the entity a handle names - index in the low 12 bits
// (below 0x540), generation above, checked against the slot table at 0x0097a968
// {entity, generation}; 0 when stale.
uint8_t* XB_REGS1 XB_CDECL G_EntHandleToEnt(const uint32_t* handle);
// 0x002b76e0 / 0x002b7730 (entity in EAX, origin in ECX): the trajectory base (+0x20),
// delta (+0x14) and +0x2c cleared, current origin +0x140. Two identical copies.
uint8_t* XB_REGS3 XB_CDECL G_SetOrigin(uint8_t* ent, uint32_t unused_edx, const float* origin);
uint8_t* XB_REGS3 XB_CDECL G_SetOrigin2(uint8_t* ent, uint32_t unused_edx, const float* origin);
// 0x002b7640 / 0x002b7690 (entity in EAX, angles in ECX): the angle trajectory (+0x44,
// +0x38, +0x50) and current angles +0x150. Two identical copies.
uint8_t* XB_REGS3 XB_CDECL G_SetAngle(uint8_t* ent, uint32_t unused_edx, const float* angles);
uint8_t* XB_REGS3 XB_CDECL G_SetAngle2(uint8_t* ent, uint32_t unused_edx, const float* angles);
// 0x002482f0 (entity in EAX): a trigger's contents (+0x134) from its spawnflags (+0x2a0).
uint8_t* XB_REGS1 XB_CDECL G_TriggerContentsFromSpawnflags(uint8_t* ent);
// 0x0024d700 (list in ESI, field offset in EDI, callee pops 4): the first entity in the list
// (count +0x2aac, pointers +0x2ab0) whose int field is set and equals value, or 0.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL G_FindEntityByIntField(const uint8_t* list, uint32_t offset,
                                                                    int32_t value);
extern "C" void G_FindEntityByIntField_entry(void);
// 0x002601e0 / 0x002601a0: SV_Trace (results, EBX = start, mins, maxs, end, pass entity, mask,
// capsule 0 / 1, 0, 0, 0, 0).
void XB_CDECL G_Trace(uint32_t results, uint32_t start, uint32_t mins, uint32_t maxs, uint32_t end, uint32_t pass,
                      uint32_t mask);
void XB_CDECL G_TraceCapsule(uint32_t results, uint32_t start, uint32_t mins, uint32_t maxs, uint32_t end,
                             uint32_t pass, uint32_t mask);
// 0x002600c0: a point trace (bounds {0, 0, 0, 1}, 16-byte aligned) - SV_Trace (results, EBX =
// start, zero, zero, end, pass, mask, 0, 1, a6, 1, a7).
extern "C" PLATFORM_HIDDEN void XB_CDECL G_LocationalTrace(uint32_t entry, uint32_t results, uint32_t start,
                                                            uint32_t end, uint32_t pass, uint32_t mask, uint32_t a6,
                                                            uint32_t a7, uint32_t edi);
extern "C" void G_LocationalTrace_entry(void);
// 0x0024c5a0 (field table in EAX, object in EBX): releases the object's script-string fields
// (type 0x10 in the {offset, type} table, 0-terminated by type) and clears them.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_FreeFieldStrings(const uint32_t* fields, uint8_t* object);
extern "C" void G_FreeFieldStrings_entry(void);
// 0x002b7500 (flag in DL, entity in ECX): adds the entity number to the list at 0x00960074
// (count 0x00960070, 32 at most; flag 0x200000 at +0x2a4 marks members) or, with flag 0,
// removes it (the last member moves into its place; the mark stays).
void XB_REGS3 XB_CDECL G_SetEntityInList960070(uint32_t, uint32_t flag, uint8_t* ent);
// 0x002f5cb0 (out in EDI): the entities (list 0x0097d370, count 0x0097d36c) whose int field
// at offset equals value - their numbers into out, returning the count; without out, the
// first one's number (its vehicle, +0x248, gets +0x200 = 1).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL G_FindEntsByField(uint32_t* out, uint32_t offset, uint32_t value);
extern "C" void G_FindEntsByField_entry(void);
// 0x0024acc0 (weapon in ECX): marks the weapon and its alternate chain (+0x718) registered
// (0x00950230) once; 0x0098601c = 1, and with precache set 0x00986004 = 1.
void XB_REGS3 XB_CDECL G_RegisterWeapon(uint32_t, uint32_t, uint32_t weapon, uint32_t precache);
// 0x002511c0 (entity in EAX): frees the vehicle's effects (4 or 6 ids at +0x21c by its def's
// +0x20, and +0x234), setting them to -1.
void XB_REGS1 XB_CDECL Vehicle_StopEffects(const uint8_t* ent);
// 0x0024abb0: for every turret weapon (class 7) the anim script record (0x00987bf8, 16 bytes
// per weapon) cleared and its script function looked up by name (+0x864) in g_broFuncs.
void XB_CDECL G_InitTurretAnimScripts();
// 0x0024e620 / 0x0024e5b0: info_notnull(_big) spawns - the entity stays where it is
// (G_SetOrigin to its current origin, inlined). Two identical copies.
void XB_CDECL SP_info_notnull(uint8_t* ent);
void XB_CDECL SP_info_notnull_big(uint8_t* ent);
// 0x0024bc00 (entity in EDX): clears every live vehicle's references to the entity (+0x204,
// +0x208, +0x1cc; vehicles 0x340 bytes from g_vehicles, live with +0x170 set).
void XB_REGS2 XB_CDECL Vehicle_ClearEntityRefs(uint32_t, const uint8_t* ent);
// 0x002489f0: sound_blend spawn - type 8, no contents, static trajectories, +0xd4 flag 0x20.
void XB_CDECL SP_sound_blend(uint8_t* ent);
// 0x002485f0: script_vehicle_collmap spawn - type 0x10, no contents.
void XB_CDECL SP_script_vehicle_collmap(uint8_t* ent);
// 0x0024bc70: clears every vehicle slot's physics body (PhysBody_Clear, EDI) and use flag
// (+0x170); 0x0098537c = g_vehicles.
void XB_CDECL Vehicle_ResetPool();
// 0x0024c2c0: the script_vehicle_collmap entity whose targetname (+0x264 script string) is
// name (case-insensitive), or 0.
uint8_t* XB_CDECL Vehicle_FindCollmap(const char* name);
// 0x0024c330: the script_vehicle entity a handle names; the error texts are only formatted
// (va), never printed. 0 for a stale handle.
uint8_t* XB_CDECL Vehicle_FromEntityHandle(uint32_t handle);
// 0x002a8e40 / 0x002a8e90 (entity in EAX, key in EBX): clears / sets the enabled word of the
// keyed record in the entity's list (+0x190 -> {count, records of {?, key, enabled}}).
extern "C" PLATFORM_HIDDEN void XB_CDECL G_EntList190_Disable(const uint8_t* ent, uint32_t key);
extern "C" void G_EntList190_Disable_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL G_EntList190_Enable(const uint8_t* ent, uint32_t key);
extern "C" void G_EntList190_Enable_entry(void);
// 0x00249d40 (key hash in EDX, out in EDI): the spawn var's value through atol, else the
// default; whether it was there.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL G_SpawnInt(uint32_t key, int32_t* out, int32_t fallback);
extern "C" void G_SpawnInt_entry(void);
// 0x00249da0 (key hash in EDX): *out = the spawn var through atof (rounded to float), else
// the default; whether it was there.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL G_SpawnFloat(uint32_t key, float fallback, float* out);
extern "C" void G_SpawnFloat_entry(void);
// 0x002f5790: the anim tree record (12 bytes, name first) of that name (case-insensitive) in
// the table at 0x0095f524 -> +4 (entry 0 skipped), or 0.
uint8_t* XB_CDECL G_FindAnimTreeByName(const char* name);
// 0x002a80b0 (entity in EAX): queues an event (low bytes of event and parameter) on the
// client's player state (+0x23c; ring at +0xc8 / +0xd8) or on the entity itself (+0xb4 /
// +0xb8, sequence +0xb0); event time +0x1a8 = level time.
uint8_t* XB_REGS1 XB_CDECL G_AddEvent(uint8_t* ent, uint32_t event, uint32_t parm);
// 0x00262be0 (entity in EAX): the entity that blocks ent at origin (a trace from origin to 16
// units above it with ent's bounds and clip mask, 0x11 without one; actors (type 1) as a
// capsule; started solid), or 0. Triggers (contents 0x4000000) are never blocked.
uint8_t* XB_REGS1 XB_CDECL G_TestEntityPosition(const uint8_t* ent, const float* origin);
// 0x00252bd0 / 0x002c9780 (entity in EAX): the entity's anim tree (+0x254); for actors (type
// 0xb) the actor's entity's, created on first use (AnimTree_Create with 0x00987a90). Two
// identical copies.
uint32_t XB_REGS1 XB_CDECL G_GetEntAnimTree(const uint8_t* ent);
uint32_t XB_REGS1 XB_CDECL G_GetEntityAnimTree(const uint8_t* ent);
// The actor's entity's anim tree, created on first use (shared by the above and
// Actor_GetAnimTree).
uint32_t actor_anim_tree(const uint8_t* actor);

// 0x00262ce0 (cdecl results, start, pass; EDI = end, ECX = mask, EAX = G_LocationalTrace's sixth
// argument): G_LocationalTrace; a trace starting solid becomes no hit (fraction 1) when the surface
// flags (+0x28) have 0x800, else fraction 0 and +0x10 = start - end (16 bytes).
extern "C" PLATFORM_HIDDEN void XB_CDECL G_MissileTrace(uint32_t entry, uint8_t* results, const float* start, uint32_t pass,
                                                        uint32_t a6, uint32_t mask, const float* end);
extern "C" void G_MissileTrace_entry(void);

// 0x00253cb0 (fire record in EDI: forward +0, right +0xc, up +0x18, start +0x24, weapon +0x3c; end in
// ESI; cdecl spread in degrees, its slot reused as scratch): the end point range ahead (16384 for
// weapon class 5, else 8192) moved by a random point of the disc of radius tan(spread) * range.
extern "C" PLATFORM_HIDDEN void XB_CDECL Bullet_RandomSpread(const uint8_t* fire, float* end, float spread);
extern "C" void Bullet_RandomSpread_entry(void);

// 0x0024b140 (list in EAX, 0x0097a8c0): fills the null slots of the entity pointer list (+0x2ab0, count +0x2aac)
// with its last entry, storing the moved entity's index at +0x228. EAX is kept.
uint8_t* XB_REGS1 XB_CDECL G_EntityList_Compact(uint8_t* list);

// 0x00044500: the file name of path[0..size) after the last '/' or '\\' into dest (*length = its length, at most
// 0xfffe); with strip_extension and a '.', only up to the last '.' (Q_strncpySubLen).
void XB_CDECL Path_BaseName(char* dest, int32_t* length, const char* path, int32_t size, uint8_t strip_extension);

// 0x002d9cc0 (position in ESI, lean in XMM1; yaw, a, b on the stack): unless lean is 0, f = (2 - |lean|) * lean
// and the position moves by f * b along (cos r sin yaw, -cos r cos yaw, -sin r), r = f * a degrees (FastSinCos).
extern "C" PLATFORM_HIDDEN void XB_CDECL AddLeanToPosition(float* position, float lean, float yaw, float a, float b);
extern "C" void AddLeanToPosition_entry(void);

// 0x002253b0 (entity in EDX): the physics body (+8) of the record (list 0x008fccf0, count 0x008fcde4) whose +4 is
// the entity, or null.
uint8_t* XB_REGS2 XB_CDECL Phys_FindBodyForEntity(uint32_t unused_eax, const uint8_t* entity);
// 0x002482e0 (trigger think): clears the trigger's wait flag +0x328; returns the entity (EAX unchanged).
uint8_t* XB_CDECL multi_wait(uint8_t* entity);
// 0x00204db0 (entity pointer in EAX, out in ESI): out = (cos yaw, sin yaw, 0) of the entity's yaw +0x154 (degrees,
// FastSinCos); no write without out.
extern "C" PLATFORM_HIDDEN void XB_CDECL Entity_YawToForward(uint8_t* const* entity, float* out);
extern "C" void Entity_YawToForward_entry(void);

// 0x00248270 (trigger_hurt use): toggles the hurt state +0x338 (0 <-> 3); with a wait (+0x38c) the next think at
// level time + 50 (+0x328, think 0xe at +0x32c) and the next hurt at wait * 1000 + level time (+0x384). Returns the
// new state.
void XB_CDECL hurt_use(uint8_t* self);
// 0x002488d0 (trigger_use use): once the level time passes +0x384, the next use at +0x38c + time and, unless the
// activator has a client (+0x23c), toggles flag bit 0 of +0x2a0.
void XB_CDECL Use_trigger_use(uint8_t* self, const uint8_t* other);
// 0x00249c00 (token in EAX): Q3's G_AddSpawnVarToken - copies the token into the spawn var buffer (0x00985628, used
// 0x00985624, G_Error past 0x800) and returns the copy.
char* XB_REGS1 XB_CDECL G_AddSpawnVarToken(const char* token);

// 0x000142a0 (entity in EAX): the entity's zone (+0x220), or the level's default zone
// (0x0095f638 -> +0x34) when it is -1.
uint32_t XB_REGS1 XB_CDECL G_GetEntityZone(const uint8_t* ent);
// 0x00014730 (bits in EAX, iterator in ESI): starts an iterator over a 42-word entity bit set
// {bits, current word, word index, entity} and steps it to the first set bit (EntBits_IterNext);
// returns the iterator.
extern "C" PLATFORM_HIDDEN uint32_t* XB_CDECL EntBits_IterBegin(const uint32_t* bits, uint32_t* iterator);
extern "C" void EntBits_IterBegin_entry(void);
// 0x00014ca0 (out in EAX, bits in EDX): out = the complement of a 42-word entity bit set.
uint32_t* XB_REGS2 XB_CDECL EntBits_Invert(uint32_t* out, const uint32_t* bits);
// 0x000231b0 (memory context index in ESI, model in EDI): XModel_Free, then the model's memory
// back to its context (0x0095f638 -> +0x38[index], null outside 0..0x62) - Z_Free when the
// index is -1 or the context does not own it.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_FreeXModel(int32_t context_index, uint8_t* model);
extern "C" void G_FreeXModel_entry(void);

// 0x00028040 (list in EAX; StaticInit_G_EntityList): an entity handle list {bits[42], slots[0x540]
// {entity, generation 1}, count}: bits cleared, then all 0x540 set (free slots). Returns the list.
uint32_t* XB_REGS1 XB_CDECL G_EntityList_Init(uint32_t* list);
// 0x00028f60 / 0x00028fb0 (out list {count, entities[0x1000]} in ESI; value in EBX for the short
// one): appends every level entity (0x0097d370, count 0x0097d36c) whose int / short field at
// `offset` equals value.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_GatherEntsByIntField(uint32_t* out, uint32_t offset, uint32_t value);
extern "C" void G_GatherEntsByIntField_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL G_GatherEntsByShortField(uint32_t value, uint32_t* out, uint32_t offset);
extern "C" void G_GatherEntsByShortField_entry(void);
// 0x0002bf00 (saved block in EAX): for the 0x540 saved {entity handle, value} pairs at +4 (last
// first), entity +0x134 = value; null handles skipped.
void XB_REGS1 XB_CDECL G_RestoreSavedEntityField134(const uint8_t* saved);
// 0x00044ee0: the water surface height at (x, y) of ocean wave set `index` (0x00901b30, 0xd0
// bytes): base height (+0) plus four packed sine waves (angle +0x70, falloff +0x80, length +0x90,
// amplitude +0xa0, offset +0xb0, phase speed +0xc0, origins +0x50; time 0x00901e90).
float XB_CDECL Water_GetHeight(int32_t index, float x, float y);
// 0x000b05a0 (no references: dead): the smallest squared distance of p from the lines through
// the triangle's edges ab, bc, ca, at least 0.0001. 16-byte aligned vectors (MOVAPS).
float XB_CDECL Tri_MinEdgeLineDistSq(const float* a, const float* b, const float* c, const float* p);

// 0x00248ee0 (entity in ECX): the content flag an entity touches triggers with: 0x8 vehicle
// (+0x248), 0x400000 no ragdoll (+0x244), 0x40000000 with +0x23c, else by the ragdoll's kind
// (1/2/3 -> 0x40000/0x80000/0x100000, other 0).
uint32_t XB_THISCALL G_GetTouchTypeFlag(const uint8_t* ent);
// 0x00249a40 (activator in EAX, trigger in ESI): fires a multiple trigger: stores the activator
// (+0x378); unless waiting or blocked (+0x32c 0x1c, +0x328 set) schedules the next think: after
// wait (+0x384) + random spread (+0x388) seconds, or next frame with think 0xc when wait <= 0.
extern "C" PLATFORM_HIDDEN void XB_CDECL multi_trigger(uint32_t activator, uint8_t* ent);
extern "C" void multi_trigger_entry(void);
// 0x00248a30: empty entity callback (a bare RET; unhookable).
void XB_CDECL Ent_Callback348Stub2();
// 0x0024afe0 (actor in EDI, duration in EAX): starts leaving prone (+0x198 {state, start,
// blend}): an expired blend is cleared; a new duration starts at level.time, or earlier by the
// current blend fraction (Actor_GetProneBlend) when one with another duration runs.
extern "C" PLATFORM_HIDDEN void XB_CDECL Actor_ExitProne(uint8_t* actor, int32_t duration);
extern "C" void Actor_ExitProne_entry(void);
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL actor_get_prone_blend_call(uint8_t* prone, int32_t time);

// 0x0024b610: use callback of trigger_multiple: multi_trigger with the activator (third argument).
void XB_CDECL Use_Multi(uint8_t* ent, uint32_t other, uint32_t activator);
// 0x0024ba60 (entity in ECX): 1 when the entity's client rides a vehicle (client +0xc0 flag
// 0x100000 without 0x400000; vehicle entity by the handle at +0x1a0) whose def has +0x3c set.
uint32_t XB_THISCALL G_ClientInOpenVehicle(const uint8_t* ent);
// 0x0024bad0 (entity in ECX): 1 when the entity's client rides a vehicle whose def is not
// type 5 (+0x20) and has +0x3c clear.
uint32_t XB_THISCALL G_ClientInVehicle(const uint8_t* ent);
// 0x0024bb30 (entity in ECX): the turret the client uses (client +0xc0 flag 0x100000, handle
// +0x1a0, the entity's +0x134 flag 0x200000), else 0.
uint8_t* XB_THISCALL G_GetPlayerTurret(const uint8_t* ent);
// 0x0024bce0 (entity in EAX, entering on the stack): unless the rider (handle +0x1a0) is the
// player, counts the vehicle's (+0x248) riders (+0x2fc) up or down. Called from CG_EntityEvent.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL G_VehicleCountRider(uint8_t* ent, uint32_t entering);
extern "C" void G_VehicleCountRider_entry(void);
// 0x0024ce90 (weapon in EAX, clips in ECX, entity on the stack): gives clips of a weapon's
// ammo (clip-only weapons: into the ammo, capped at the room and at 4 for type 4; else the clip
// is filled and the rest goes to the reserve +0x110), BG_AmmoRoomForWeapon as the cap.
extern "C" PLATFORM_HIDDEN void XB_CDECL Add_AmmoClips(uint32_t weapon, int32_t clips, uint8_t* ent);
extern "C" void Add_AmmoClips_entry(void);

// 0x0024cf70 (weapon in ECX, player state in ESI): refills a weapon's clip (+0x280, up to the
// ammo type's size at 0x00935338) from its reserve (+0x110).
extern "C" PLATFORM_HIDDEN void XB_CDECL Fill_Clip(int32_t weapon, uint8_t* ps);
extern "C" void Fill_Clip_entry(void);
// 0x0024d320 (entity in EDX): frees the entity the client holds (+0x5f0; cleared, think 0xc).
extern "C" PLATFORM_HIDDEN void XB_CDECL G_ClientFreeHeldEntity(uint8_t* ent);
extern "C" void G_ClientFreeHeldEntity_entry(void);
// 0x0024d3c0 (other in EDX, player in EDI): pushes the player's velocity away from a touching
// entity with flag 0x4000/0x8000 within 30 units; 1 when handled.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Player_PushAwayFromEntity(const uint8_t* other, uint8_t* player);
extern "C" void Player_PushAwayFromEntity_entry(void);
// 0x0024d4d0: think of a dead body: sinks 1 unit per frame, unlinked after 6.5 s (+0x2f0).
void XB_CDECL BodySink(uint8_t* ent);
// 0x0024d630 (field offset in EBX; args {result, key by value, begin, end}, callee pops 16):
// the first entity pointer in [begin, end) whose script-string field equals key; returns the
// result pointer.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL G_EntityArray_FindByStringField(uint32_t field, uint32_t* args);
extern "C" void G_EntityArray_FindByStringField_entry(void);
// 0x0024d6b0 (list in ESI, key ScriptString* in EDX, field offset on the stack, callee pops 4):
// the first entity of the list (+0x2aac count, +0x2ab0 pointers) whose field equals key, or 0.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL G_FindEntityByStringField(const uint8_t* list, const ScriptString* key,
                                                                       uint32_t* args);
extern "C" void G_FindEntityByStringField_entry(void);
// 0x0024dad0 (slot in EAX, out in ESI): the entity number of a live type-2 vehicle's entity, else 0.
extern "C" PLATFORM_HIDDEN uint32_t* XB_CDECL Vehicle_GetTankEntityNum(uint32_t index, uint32_t* out);
extern "C" void Vehicle_GetTankEntityNum_entry(void);
// 0x0024db50 (slot out in ESI, flag out in EDI, entity handle on the stack): for compass markers:
// 1 (AL) with the vehicle slot and +0x194 flag of a live type 1/2 vehicle entity.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Veh_GetEntTankInfo(int32_t* slot, uint8_t* flag, uint32_t handle);
extern "C" void Veh_GetEntTankInfo_entry(void);
// 0x0024dc10 (turret entity in EDX, player in ECX): 1 when the player may use the turret.
uint32_t XB_REGS3 XB_CDECL G_PlayerCanUseTurret(uint32_t unused_eax, const uint8_t* vehicle, const uint8_t* player);
// 0x0024e1d0: think that lifts the player within 512 units until +0x38c.
void XB_CDECL Think_BoostNearbyPlayer(uint8_t* ent);
// 0x0024e690 (entity in ESI): puts the entity on the water surface, stationary.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_FloatOnWater(uint8_t* ent);
extern "C" void G_FloatOnWater_entry(void);
// 0x0024e740 (entity in ESI): two clips for each of the client's four weapon slots; returns -1.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL Pickup_ClipsForAllWeapons(uint8_t* ent);
extern "C" void Pickup_ClipsForAllWeapons_entry(void);

// 0x0025d230 (origin in ECX; cdecl entity, cache): the entities touching the entity's box at
// the origin (from the cache {count, mins +0x10, maxs +0x20, handles +0x30}, else
// SV_AreaEntities 40/40/52 around it with its touch flag): "touch" both ways and both touch
// callbacks.
void XB_REGS3 XB_CDECL G_TouchEntitiesInBox(uint32_t unused_eax, uint32_t unused_edx, const float* origin, uint8_t* ent,
                                           const uint32_t* cache);
// 0x0025d520 (entity in EBX, origin in ECX, cache in EDX): the same for linked type-14 entities
// with contents 0x800000 (G_NotifyWithOwnHandle); nothing with +0x3cc set (returned). Returns
// the candidate count. Proposed name (was FUN_0025d520).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL G_TouchType14EntitiesInBox(uint32_t entry, uint8_t* ent,
                                                                       const float* origin, const uint32_t* cache,
                                                                       uint32_t esi, uint32_t edi, uint32_t ebp);
extern "C" void G_TouchType14EntitiesInBox_entry(void);
// 0x0025df60 (anims in EAX, entity in EDI): a new anim tree (+0x254) for other anims (none:
// cleared), the DObj updated, the old tree queued for freeing.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_SetEntityAnimTree(uint8_t* anims, uint8_t* ent);
extern "C" void G_SetEntityAnimTree_entry(void);

// 0x002a7ff0 (entity in EAX): whether its DObj (+0x22c) is live (+0x9c bit 0 clear).
uint32_t XB_REGS1 XB_CDECL G_EntHasLiveDObj(const uint8_t* ent);
// 0x002a8260: starts a cod note (two words, started flag, its object's slot 2).
void XB_CDECL G_StartCodNote(uint8_t* note, uint32_t a, uint32_t b);

// 0x002b49a0 (ECX owner, EDX key, EBX count; stack values; callee pops): replaces the owner's persistent value
// (12-byte records at +0x560: {key, value, count} then count - 1 records {0, value, 0}) with the new values.
extern "C" PLATFORM_HIDDEN void XB_CDECL Persistent_SetValue(uint8_t* owner, uint32_t key, uint32_t count,
                                                            const uint32_t* values);
extern "C" void Persistent_SetValue_entry(void);
// 0x002b6de0 (thiscall callback list; stack entity, key, args; callee pops): calls every callback (6 {key,
// callback} pairs at +8, next list +0x38) registered for the key; the chained lists get no args. AL = any called.
uint32_t XB_THISCALL G_EntityRunNotifyCallbacks(uint8_t* list, uint8_t* ent, uint32_t key, const uint32_t* args);
// 0x002b6e50 (EDX list; stack key, callback; callee pops): clears the first matching pair; AL = found.
uint32_t XB_REGS2 XB_STDCALL G_EntityRemoveNotifyCallback(uint32_t unused_eax, uint8_t* list, uint32_t key,
                                                          uint32_t callback);
// 0x002b6ea0 (EAX list; stack key, callback; callee pops): stores the pair in the first free slot, chaining a
// zeroed 0x3c-byte list from the small heap at 0x0095f568 when all are full; AL = stored.
uint32_t XB_REGS1 XB_STDCALL G_EntityAddNotifyCallback(uint8_t* list, uint32_t key, uint32_t callback);
// 0x002f5d60 (EAX args or null, ESI callback, ECX entity): runs the callback through game import +0x880 with the
// entity number (+0x224) and the args {+0, +0, +8, +0xc} (none: 0, 0 and the value at 0x00922eb8 twice).
extern "C" PLATFORM_HIDDEN void XB_CDECL G_EntityCallNotifyCallback(const uint32_t* args, uint32_t callback,
                                                                   uint8_t* ent);
extern "C" void G_EntityCallNotifyCallback_entry(void);
// 0x002b7900 (EAX entity): detaches a linked entity (+0x3cc link {parent, next}): stops its trajectories at the
// current origin/angles, unchains it from the parent's child list (+0x3d0), clears the parent client's +0x238
// for a player (+0x23c) and frees the link (small heap at 0x0095f598).
void XB_REGS1 XB_CDECL G_EntUnlink(uint8_t* ent);
// 0x002b8420 (EAX entity, ESI tag name): DObj_GetTagIndex on the entity's DObj (+0x22c), -1 without one.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL G_EntGetTagIndex(const uint8_t* ent, uint32_t name);
extern "C" void G_EntGetTagIndex_entry(void);

// 0x002bde90 (stdcall; dead: no callers): a shake/rumble descriptor with both channels' +4/+0xc from the arguments,
// started on shakeRumbleMgr at strength 1 (the id goes to the first argument's slot); its script strings released.
void XB_STDCALL ShakeRumble_StartByName(uint32_t id_slot, float second);
// 0x002be2e0 (EAX state; returns it): zeroes an entity state (0xcc bytes except +0x5c).
uint8_t* XB_REGS1 XB_CDECL EntityState_Clear(uint8_t* state);
// 0x002be420 (cdecl entity): marks its scene slot (+0x290) changed, G_FreeEntityContents, clears +0x224 and frees
// it in its memory context (+0x220, -1 = the default +0x34) or with Z_Free. The world entity (0x0095f52c) breaks.
void XB_CDECL G_FreeEntity(uint8_t* ent);
// 0x002be860 (cdecl note): deletes the note's anim object (+0xc), closes its file (+8) and frees it (small heap
// 0x0095f520), inside its memory context (+0x24) when it has one.
void XB_CDECL CodNote_Free(uint8_t* note);
// 0x002be8f0 (cdecl time): advances every cod note on the list at 0x008dda50: its object's +0xc(time); notifies the
// owner (game import +0x8ac) with +0x10 once started (+0x18), frees a finished one (notify +0x10 again) and sends
// +0x14 within +0x20 of the end.
void XB_CDECL G_ProcessCodNotes(uint32_t time);

// 0x002c3870 (stdcall state): G_FreeEntity of every entity in the list 0x0097d370 (0x008d50d4 cleared before each),
// then clears the state's +4 and +8.
void XB_STDCALL G_FreeAllEntities(uint8_t* state);
// 0x002c3960 (was FUN_002c3960; EAX entity, EDI check flag): a vehicle's tags (Vehicle_CacheTags), then each linked
// child's link tag index refreshed (0x002be510: ECX check, EBX child; unlinks a child whose tag is gone).
extern "C" PLATFORM_HIDDEN void XB_CDECL G_EntUpdateLinkedTags(uint8_t* ent, uint32_t check);
extern "C" void G_EntUpdateLinkedTags_entry(void);
// 0x002c39a0 (EBX slot count; cdecl name, base index, create, error label): the configstring slot (1..count-1)
// after base whose text equals the name (stricmp); the first empty slot gets it with create (SV_SetConfigstring;
// bases 0x20 and 0xe0 also set 0x0098601c). 0 for no name, no free slot, or not found without create.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL G_FindConfigstringIndex(int32_t max, const char* name, int32_t base,
                                                                   uint32_t create, const char* error);
extern "C" void G_FindConfigstringIndex_entry(void);
// 0x002c4070: unlinks and frees (CodNote_Free) every cod note from the list's tail.
void XB_CDECL G_FreeAllCodNotes();
// 0x002c40c0: unlinks the note (Sys_DebugBreak when it is not in the list) and CodNote_Free.
void XB_CDECL G_FreeCodNote(uint8_t* note);
// 0x002c4110 (cdecl name, notify, start/end times as float bits, use the zone's memory context): a cod note (0x28
// bytes) playing the zone raw file `name` as a streamed anim (NAL_StreamedAnim_Create), appended to the list.
uint8_t* XB_CDECL G_CreateCodNote(const char* name, uint32_t notify, uint32_t start_bits, uint32_t end_bits,
                                  uint32_t use_zone_context);

// 0x002d5ba0 (was FUN_002d5ba0): with the use flag (ps +0x2c bit 0x1000000) and no blocking weapon (+0x3c set and
// the weapon's +0x94 = 1 blocks), clears +0x3c and G_ClientActivateTargetActor; 1 when activated.
uint32_t XB_CDECL PM_ActivateTargetActor();
// 0x002d67a0 (EAX entity, ECX bone; stack out; callee pops): copies the bone's matrix (DObj +0x20 + 0x30, 0x40 each)
// or the identity without a DObj (+0x22c). Returns out.
float* XB_REGS3 XB_STDCALL G_DObjGetBoneMatrix(const uint8_t* ent, uint32_t unused_edx, uint32_t bone, float* out);
// 0x002d6ba0 (EAX entity; stack memory context; callee pops): clears the entity (state, physics +0xd0, fields,
// script strings; bounds +0x430/+0x440 = +-FLT_MAX; +0x2fc = -1), classname 0x00984ba4, adds it to the entity list
// 0x0097a8c0. Returns it.
uint8_t* XB_REGS1 XB_STDCALL G_InitGentity(uint8_t* ent, int32_t context);
// 0x002d7030 (EAX memory context, -1 = the current one): a 0x460-byte entity from the context's heap (else
// Heap_AllocAligned), G_InitGentity; counts the attempt (0x00985374). Null when out of memory.
uint8_t* XB_REGS1 XB_CDECL G_Spawn(int32_t context);
// 0x002d7c60 (EAX memory context, EDX entity, ECX model name): registers the model (+0x258); a "cdChar" model with
// 15 skins gets a random skin variant in the entity's render record (+0xee).
void XB_REGS3 XB_CDECL G_SetModel(int32_t context, uint8_t* ent, const char* name);

// 0x002d6800 (EAX entity, ECX bone; stack out; callee pops): the bone's matrix times the entity's world matrix
// (+0x160, 4 rows), or the identity without a DObj. Returns out.
float* XB_REGS3 XB_STDCALL G_DObjGetWorldBoneMatrix(const uint8_t* ent, uint32_t unused_edx, uint32_t bone, float* out);

// 0x002ddd70 (dead: no callers, no stored pointer): the sound listener at the player's origin (+0x140) with axes
// {0 - right, up} of its angles (+0x150).
void XB_CDECL G_UpdateSoundListenerFromPlayer();
// 0x002ddea0 (was FUN_002ddea0; EDI level): a new entity in the current memory context (G_Spawn's allocation, counted
// in 0x00985374) stored as the level's world entity (+8). Returns it.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL G_AllocWorldEntity(uint8_t* level);
extern "C" void G_AllocWorldEntity_entry(void);
// 0x002ddf10 (EDI level): the same for the player entity (+4), unless one exists.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL G_AllocPlayerEntity(uint8_t* level);
extern "C" void G_AllocPlayerEntity_entry(void);
// 0x002ddf80 (EAX entity): stops the trajectories and takes angles (MatrixToAngles of +0x160) and origin (+0x190)
// from the physics matrix into the entity, its state and a client's origin (old one to +0x5b0); SV_LinkEntity.
void XB_REGS1 XB_CDECL G_SetPoseFromPhysics(uint8_t* ent);
// 0x002de2d0 (cdecl origin, event): a "tempEntity" of type event + 0x11 at the origin, freed after the event time
// (+0x1a4 = 1, +0x1a8 = level_time); linked.
uint8_t* XB_CDECL G_TempEntity(const uint32_t* origin, uint32_t event);
// 0x002df740 (EAX parent; cdecl entity, tag name or null, flags byte): unlinks the entity, then links it to the
// parent's tag (0 for a missing tag/DObj or a link cycle): a 0x70-byte link {parent, next child, tag hash, tag index,
// flags, offset +0x40 = inverse of the entity's tag-parent matrix (zero without flags and +0x3d4)}; a client linked to
// a vehicle sets the vehicle's +0x238. 1 when linked.
uint32_t XB_REGS1 XB_CDECL G_EntLinkTo(uint8_t* parent, uint8_t* ent, const char* tag, uint32_t flags);
// 0x002e3670 (EDI entity; stack 4 DObj_Create words; callee pops): the entity's DObj (+0x22c; 0xa0 bytes in its memory
// context, DObj_Ctor, queued for the entity list), then DObj_Create(EAX = 3rd, EDX = 1st, ECX = 4th; 2nd, DObj) and
// the DObj's owner (+0x88).
extern "C" PLATFORM_HIDDEN void XB_CDECL G_DObjCreateForEnt(uint8_t* ent, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
extern "C" void G_DObjCreateForEnt_entry(void);
// 0x002e3720 (EAX entity, ECX tag name hash): the tag's bone matrix in the skeleton (SV_DObjCreateSkeleton first), null
// for no DObj or tag.
const uint8_t* XB_REGS3 XB_CDECL G_DObjGetTagMatrixPtr(uint8_t* ent, uint32_t unused_edx, uint32_t name);
// 0x002e3780 (cdecl entity, bone, out 4x4): the bone matrix times the entity's frame (angles +0x150, origin +0x140).
void XB_CDECL G_DObjGetWorldTagMatrix(uint8_t* ent, uint32_t bone, float* out);

// 0x002e38a0 (entity in EBX; ClientEndFrame): a player linked to a tag - the link offset times the parent's tag
// frame (inlined product), the view angles moved by the change (AnglesNormalize180, scaled by client +0x614.. into
// +0x46c..), the origin from the frame's up row at the view height (client +0xac); SV_LinkEntity.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_ClientUpdateLinkTo(uint8_t* ent);
extern "C" void G_ClientUpdateLinkTo_entry(void);
// 0x002e3f10: the link's offset (+0x3cc record, +0x10) from the entity's pose in the parent tag's frame
// (G_GetTagParentMatrix): rotation only (rows dotted with the parent's) when angles_only, else frame * inverse.
void XB_CDECL G_CalcTagLinkOffset(uint8_t* ent, uint32_t angles_only);
// 0x002e4280 (entity in EAX, out in EDI; no callers): the link's stored offset (+0x40) times the parent tag frame.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_CalcLinkedEntityMatrix(uint8_t* ent, float* out);
extern "C" void G_CalcLinkedEntityMatrix_entry(void);
// 0x002e44b0 (flags in EAX, entity in EDX, tag in ECX; parent, origin, angles on the stack): G_EntLinkTo, then the
// link offset set from the angles' axis and the origin. 1 when linked.
uint32_t XB_REGS3 XB_CDECL G_EntLinkToWithOffset(uint32_t flags, uint8_t* ent, const char* tag, uint8_t* parent,
                                                 const float* origin, const float* angles);
// 0x002e6420 (entity in EDI, tag name hash in ESI; out on the stack): the tag's world matrix when the DObj has it.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL G_DObjTryGetTagMatrix(uint8_t* ent, uint32_t name, float* out);
extern "C" void G_DObjTryGetTagMatrix_entry(void);
// 0x002e6490: a linked entity's origin and angles from its link offset and the parent's tag frame (stationary
// trajectories); SV_LinkEntity.
void XB_REGS1 XB_CDECL G_GeneralLink(uint8_t* ent);
// 0x002e74f0: think for script-linked entities: next think level_time + 1, G_GeneralLink while linked.
void XB_CDECL Think_ScriptLinked(uint8_t* ent);
// 0x002e8b40: DObj_Free of the entity's DObj; with free_model (low byte) also the ragdoll, the model memory
// (G_FreeXModel) and the entity-list slot.
void XB_STDCALL G_FreeEntityDObj(uint8_t* ent, uint32_t free_model);
// 0x002e8bb0 (G_ReloadLevel): G_FreeEntityDObj for every entity; the flag is the caller's EBX.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_FreeAllDObjs(uint32_t ebx);
extern "C" void G_FreeAllDObjs_entry(void);
// 0x002e8be0: rebuilds the entity's DObj from its model (+0x258) and attached models (+0x3d8/+0x3dc, bits +0x299):
// the anim tree (actors: the shared info's, created on demand; dropped for another skeleton), G_DObjCreateForEnt,
// vehicle tags, then the children's link tag indices (unlinking those whose tag is gone).
void XB_CDECL G_DObjUpdate(uint8_t* ent);
// 0x002e91f0: clears all seven attached models and their bits; G_DObjUpdate.
void XB_CDECL G_EntDetachAll(uint8_t* ent);
// 0x002e9230 (was G_EntAttach): removes the attached model by name ("xmodel/" stripped) and tag, the later slots
// moving down; G_DObjUpdate. 0 when not attached or no tag.
uint32_t XB_CDECL G_EntDetach(uint8_t* ent, const char* model, const char* tag);
// 0x002e9370 (was G_EntDetach; entity in EDI, model context in ECX, -1 = the entity's/default): attaches a model on
// a tag in the first free slot (R_RegisterModel, lowercased tag name, ignore-collision bit); G_DObjUpdate.
// 0 when all seven slots are used.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL G_EntAttach(uint8_t* ent, int32_t context, const char* model,
                                                        const char* tag, uint32_t ignore_collision);
extern "C" void G_EntAttach_entry(void);

// 0x002e9840 (end in EAX, start in EDI, maxs in EBX; result, mins, two pass entity handles, content mask, capsule
// flag on the stack): CM_SightTrace, then the entities of the server's sector tree (point or box clip, the pass
// entities and their owners skipped). *result != 0 when blocked.
// Entry: the original's frame rebuilt at its position (entry = its ESP at entry).
extern "C" PLATFORM_HIDDEN void XB_CDECL G_SightTrace(uint32_t entry, const float* end, const float* start,
                                                     const float* maxs, int32_t* result, const float* mins, uint32_t pass1,
                                                     uint32_t pass2, uint32_t content_mask, uint32_t capsule);
extern "C" void G_SightTrace_entry(void);

// 0x002fc920 (state in ESI; objective index on the stack): at most every 100 ms (0x00985e2c) the objective
// "completed" / "failed" / "updated" server command for the state (none while the HUD shows the objective). 1 when
// sent or due.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL G_SendObjectiveUpdate(const ScriptString* state, uint32_t index);
extern "C" void G_SendObjectiveUpdate_entry(void);
// 0x002fe3b0 (index in EBX; text, send flag on the stack): the objective's configstring (index + 0x10) "str" key
// set to the text (no ", ; or \, under 256 characters), its "state" key (0-5) to one of the six state names;
// G_SendObjectiveUpdate with the flag.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_SetObjectiveString(int32_t index, const ScriptString* text,
                                                             uint32_t send_update);
extern "C" void G_SetObjectiveString_entry(void);

}  // namespace game
