#pragma once

// Game module: small entity helpers (gentity_t is reached by byte offsets; the layout is in
// tools/data/structs.txt).

#include <cstdint>

#include "compiler.h"

namespace game {

// Calls vectoangles (0x002a0850; vector in ESI, angles out in EDI) at its title address.
extern "C" PLATFORM_HIDDEN void XB_CDECL vectoangles_call(const float* v, float* angles);

// 0x00013860 (handle pointer in EAX): the entity a handle names - index in the low 12 bits
// (below 0x540), generation above, checked against the slot table at 0x0097a968
// {entity, generation}; 0 when stale.
uint8_t* XB_REGS1 XB_CDECL G_EntHandleToEnt(const uint32_t* handle);
// 0x002b76e0 / 0x002b7730 (entity in EAX, origin in ECX): the trajectory base (+0x20),
// delta (+0x14) and +0x2c cleared, current origin +0x140. Two identical copies.
void XB_REGS3 XB_CDECL G_SetOrigin(uint8_t* ent, uint32_t unused_edx, const float* origin);
void XB_REGS3 XB_CDECL G_SetOrigin2(uint8_t* ent, uint32_t unused_edx, const float* origin);
// 0x002b7640 / 0x002b7690 (entity in EAX, angles in ECX): the angle trajectory (+0x44,
// +0x38, +0x50) and current angles +0x150. Two identical copies.
void XB_REGS3 XB_CDECL G_SetAngle(uint8_t* ent, uint32_t unused_edx, const float* angles);
void XB_REGS3 XB_CDECL G_SetAngle2(uint8_t* ent, uint32_t unused_edx, const float* angles);
// 0x002482f0 (entity in EAX): a trigger's contents (+0x134) from its spawnflags (+0x2a0).
void XB_REGS1 XB_CDECL G_TriggerContentsFromSpawnflags(uint8_t* ent);
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
void XB_CDECL G_LocationalTrace(uint32_t results, uint32_t start, uint32_t end, uint32_t pass, uint32_t mask,
                                uint32_t a6, uint32_t a7);
// 0x0024c5a0 (field table in EAX, object in EBX): releases the object's script-string fields
// (type 0x10 in the {offset, type} table, 0-terminated by type) and clears them.
extern "C" PLATFORM_HIDDEN void XB_CDECL G_FreeFieldStrings(const uint32_t* fields, uint8_t* object);
extern "C" void G_FreeFieldStrings_entry(void);
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
void XB_REGS1 XB_CDECL G_AddEvent(uint8_t* ent, uint32_t event, uint32_t parm);
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

}  // namespace game
