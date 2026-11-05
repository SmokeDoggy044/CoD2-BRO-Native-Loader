#pragma once

// The GScr_ builtins from 0x00303600 on (the rest are in g_scr_builtins.h): plain cdecl, script values as raw
// words (entity handles: index in the low 12 bits, generation above; vectors by pointer; floats by value). An
// anim value names a tree of rm_animTrees (0x0095f524 -> +4: {count, 12-byte records}) in its high 16 bits (0
// when out of range) and the node in its low 16.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

struct ScriptString;

// 0x00303600: a world trace (CM_BoxTrace, point size, contents 0x2820011) from start to end into out: fraction
// +0, endpos +4, the hit entity's handle +0x10 (0 for none or the world), the normal +0x14 and the surface name
// +0x20 (the surface table 0x008d4d08 by bits 20-24 of the surface flags, "default" outside 1..0x16). No hit:
// the normalized direction (1 / __CIsqrt of the squared length, unchanged for length 0) and "default". Returns
// ScriptString_AssignCString's result.
uint32_t XB_CDECL GScr_TraceSurfaceType(uint8_t* out, const float* start, const float* end);
// 0x00303880: BadPlace_Delete of the name unless it is empty.
void XB_CDECL GScr_BadPlace_Delete(const ScriptString* name);
// 0x003038a0: a box entity (G_Spawn with the memory context of `owner` +0x28, -1 without one): mins/maxs sorted
// per axis in place (+0xe0 / +0xf0), classname 0x00984c64 and its hash (+0x25c / +0x260), BoxEntity_SetSolidity,
// G_UpdateEntityStringHashes. Returns its handle (+0x224).
uint32_t XB_CDECL GScr_SpawnBoxEntity(float* mins, float* maxs, uint32_t solid, const uint8_t* owner);
// 0x00303980 / 0x003039b0 / 0x003039e0: GScr_RotateAxis (ECX = the handle) for axis 2 / 1 / 0.
void XB_CDECL GScr_RotateRoll(uint32_t handle, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
void XB_CDECL GScr_RotateYaw(uint32_t handle, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
void XB_CDECL GScr_RotatePitch(uint32_t handle, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
// 0x00303a10 / 0x00303a40 / 0x00303a70: GScr_MoveAxis (ECX = the handle) for axis 2 / 1 / 0.
void XB_CDECL GScr_MoveZ(uint32_t handle, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
void XB_CDECL GScr_MoveY(uint32_t handle, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
void XB_CDECL GScr_MoveX(uint32_t handle, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
// 0x00303b70: Actor_LeaveTurret of the handle's actor.
void XB_CDECL GScr_ActorLeaveTurret(uint32_t handle);
// 0x00303c70 / 0x00303cc0: the angles (MatrixToAngles, tail jump) / origin (+0x30) of the entity's cached tag
// matrix (G_GetCachedTagMatrix with the entity's own handle +0x224); nothing when stale or not found.
void XB_CDECL GScr_GetTagAngles(uint32_t handle, float* out, const ScriptString* tag);
void XB_CDECL GScr_GetTagOrigin(uint32_t handle, uint32_t* out, const ScriptString* tag);
// 0x00303d20: an arc bad place: ceil(seconds * 1000) ms (x87, rounded to float, _ftol), {origin z, y, z (sic),
// radius, height / 2, yaw - left, yaw + right} with the yaws wrapped to [0, 360) ({0, 360} for 360 or more),
// the team mask of `team`. A temporary "badplace_arc" string is made and dropped around Scr_TeamFromString.
void XB_CDECL GScr_BadPlace_Arc(const ScriptString* name, float seconds, const float* origin, float radius,
                                float height, const float* direction, float left, float right, const ScriptString* team);
// 0x00303e70: the same for a cylinder: ceil (no float rounding), {z, y, z (sic), radius, height / 2, 0, 360},
// and the team mask of the name itself (Scr_TeamFromString gets the first argument; no sixth is read).
void XB_CDECL GScr_BadPlace_Cylinder(const ScriptString* name, float seconds, const float* origin, float radius,
                                     float height);
// The anim and path builtins run their callees at the original's ESP with the locals the callees get at the
// original's addresses (AnimTree_GetRootDelta* and Path_DisconnectPathsForEntity leave addresses of them in
// memory): ESP entries, the C version reads its arguments from the entry ESP.
// 0x00303f30: the yaw (vectoyaw_2) of the anim's root direction between two times (AnimTree_GetRootDeltaBetween);
// ST0.
// (uint32_t anim, uint32_t from, uint32_t to)
extern "C" PLATFORM_HIDDEN float XB_CDECL GScr_GetAngleDelta_c(uint32_t entry_esp);
extern "C" void GScr_GetAngleDelta_entry(void);
// 0x00303f90: the anim's root position delta between two times into out.
// (float* out, uint32_t anim, uint32_t from, uint32_t to)
extern "C" PLATFORM_HIDDEN void XB_CDECL GScr_GetMoveDelta_c(uint32_t entry_esp);
extern "C" void GScr_GetMoveDelta_entry(void);
// 0x00303fe0: the anim's whole root move (position at 1 minus at 0) through the axis of `angles` (forward,
// -right, up; AnglesToAxis), transposed: out[i] = up[i] * dz + forward[i] * dx + -right[i] * dy.
// (float* out, const float* angles, uint32_t anim)
extern "C" PLATFORM_HIDDEN void XB_CDECL GScr_GetAnimWorldMoveDelta_c(uint32_t entry_esp);
extern "C" void GScr_GetAnimWorldMoveDelta_entry(void);
// 0x00304150: the angles (AxisToAngles, 12-byte rows) of the axis of `angles` turned back by the anim's start yaw
// (vectoyaw_2 of its root direction at 0, FastSinCos). The second argument is not read.
// (float* out, uint32_t unused, const float* angles, uint32_t anim)
extern "C" PLATFORM_HIDDEN void XB_CDECL GScr_GetAnimStartAngles_c(uint32_t entry_esp);
extern "C" void GScr_GetAnimStartAngles_entry(void);
// 0x003043c0: origin plus the anim's root position at 0 through the axis of `angles` (as
// GScr_GetAnimWorldMoveDelta).
// (float* out, const float* origin, const float* angles, uint32_t anim)
extern "C" PLATFORM_HIDDEN void XB_CDECL GScr_TransformLocalOffset_c(uint32_t entry_esp);
extern "C" void GScr_TransformLocalOffset_entry(void);
// 0x00304500: Path_DisconnectPathsForEntity (g_pathNodeBlocks 0x0095f4f4) for an entity with flag 0x1000 of
// +0x2a4, else only the message (va, with its classname +0x25c).
// (uint32_t handle)
extern "C" PLATFORM_HIDDEN void XB_CDECL GScr_DisconnectPaths_c(uint32_t entry_esp);
extern "C" void GScr_DisconnectPaths_entry(void);

// 0x00304570: G_SetModel (memory context: `owner` +0x28, else the entity's +0x220, or the level's 0x0095f638
// +0x34 for -1), G_DObjUpdate, SV_LinkEntity; nothing for an empty name or a stale handle.
void XB_CDECL GScr_SetModel(uint32_t handle, const ScriptString* name, const uint8_t* owner);
// 0x003045f0: whether two entities touch: both linked (+0xdc) - their absolute boxes overlap on every axis
// (packed: max(b.max - a.max... ) < 0, see the body); else SV_ClipToEntity of one's box against the other
// (the linked one is clipped against; capsule = flag 0x200 of the box owner's +0xd4). AL.
// (handle, other; SV_ClipToEntity at the original's ESP: ESP entry)
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL GScr_IsTouching_c(uint32_t entry_esp);
extern "C" void GScr_IsTouching_entry(void);
// 0x00304880: the actor's aim blend towards a point: the normalized height difference from its eye
// (Sentient_EyeOrigin; __CIsqrt length, unnormalized for length 0) split into down / straight / up weights (s > 0:
// up s, straight 1 - s; else down -s, straight 1 - down), AnimTree_SetGoalWeight of the three aim nodes (+0x160,
// +0x164, +0x168; blend 0.1, rate 1) of the shared info's tree (+0x254 of *actor, created on demand), then the
// three nodes at +0x16c..+0x174 through an inlined goal setter. Returns 0.1 (0 without an actor) in ST0.
float XB_CDECL GScr_ActorBlendAimToPoint(uint32_t handle, const float* point);
// 0x00304dc0: GScr_SetModel without an owner.
void XB_CDECL GScr_SetModelNoFlag(uint32_t handle, const ScriptString* name);
// 0x00304e00: fire_grenade of the actor's grenade weapon (+0x131c) from origin with velocity, fuse seconds *
// 1000 (CVTTSS2SI).
void XB_CDECL GScr_MagicGrenadeManual(uint32_t handle, const float* origin, const float* velocity, float seconds);
// 0x00304e60: drops the entity's anim tree (+0x254: queued for freeing at 0x00987070/74, G_DObjUpdate,
// AnimTree_FlushClearLists). Actors (type 0xb) first get their shared info's tree (+0x254 of *actor, created
// on demand); a message (va) when the entity's own tree is another.
void XB_CDECL GScr_ClearAnimTree(uint32_t handle);
// 0x00304f50: G_SetEntityAnimTree with the tree record of that name (G_FindAnimTreeByName; nothing for an empty
// name, no record or a record without anims), a message when G_GetEntityAnimTree is not the entity's +0x254.
void XB_CDECL GScr_UseAnimTree(uint32_t handle, const ScriptString* name);
// 0x00304ff0 / 0x00305050: a door's (func_door / func_door_rotating) locked flag +0x3b4 = 0 / 1, then
// Door_UpdatePaths (tail jump).
void XB_CDECL GScr_DoorUnlock(uint32_t handle);
void XB_CDECL GScr_DoorLock(uint32_t handle);
// 0x003050b0: Sentient_EyeOrigin of the entity's sentient (+0x244) into out.
void XB_CDECL GScr_GetEye(uint32_t handle, uint32_t* out);
// 0x00305110: G_EntDetachAll (tail jump).
void XB_CDECL GScr_DetachAll(uint32_t handle);
// 0x00305150: G_EntDetach (tail jump) with the model's and tag's texts (null when empty).
void XB_CDECL GScr_Detach(uint32_t handle, const ScriptString* model, const ScriptString* tag);
// 0x003051c0: G_EntDetach of the model and tag, then G_EntAttach (memory context as GScr_SetModel) with the
// ignore-collision byte; nothing unless both names are set.
void XB_CDECL GScr_Attach(uint32_t handle, const ScriptString* model, const ScriptString* tag, uint32_t ignore,
                          const uint8_t* owner);
// 0x00305280 / 0x00305330: a turret entity (G_Spawn as GScr_SpawnBoxEntity): classname and its hash, origin
// +0x140 (and angles +0x150), Turret_ParseInfo with the weapon name, G_UpdateEntityStringHashes. Returns the
// handle.
uint32_t XB_CDECL GScr_SpawnTurretWithAngles(const ScriptString* classname, const uint32_t* origin,
                                             const ScriptString* weapon, const uint32_t* angles, const uint8_t* owner);
uint32_t XB_CDECL GScr_SpawnTurret(const ScriptString* classname, const uint32_t* origin, const ScriptString* weapon,
                                   const uint8_t* owner);
// 0x003053c0: Vehicle_ExitPlayer of the vehicle's driver (owner handle +0x1a0) when that entity is a client
// (+0x23c); "No driver to eject" (va, tail jump) when the owner handle is stale.
void XB_CDECL GScr_Vehicle_EjectDriver(uint32_t handle);
// 0x00305470 (handle, point, yaw, skip start check): whether the actor fits prone at the point (BG_CheckProne:
// g_actorMaxs radius, height 24, ground check, the zero bounds 0x0082e110, G_TraceCapsule / G_Trace, flag 1,
// length 60), at the original's ESP with origin and bounds at its frame addresses. AL.
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL GScr_ActorMayMoveToPoint_c(uint32_t entry_esp);
extern "C" void GScr_ActorMayMoveToPoint_entry(void);
// 0x00305560: Actor_Grenade_Grab when the actor has a grenade (+0x1310).
void XB_CDECL GScr_ActorGrabGrenade(uint32_t handle);
// 0x003055b0: Actor_EnterProne with seconds * 1000 (CVTTSS2SI).
void XB_CDECL GScr_ActorEnterProne(uint32_t handle, float seconds);
// 0x00305610: GScr_MagicGrenadeManual with a 5 second fuse.
void XB_CDECL GScr_MagicGrenadeManualDefaultFuse(uint32_t handle, const float* origin, const float* velocity);


// 0x00305630 (named AttachNoTag; proposed GScr_DetachNoTag): GScr_Detach with an empty tag (a temporary string
// from "" around the call).
void XB_CDECL GScr_AttachNoTag(uint32_t handle, const ScriptString* model);
// 0x00305660 (named DetachNoTag; proposed GScr_AttachNoTag): GScr_Attach with an empty tag, no ignore flag, no
// owner.
void XB_CDECL GScr_DetachNoTag(uint32_t handle, const ScriptString* model);
// 0x003056a0 (proposed GScr_AttachWithOwner): GScr_Attach(handle, model, tag, 0, owner).
void XB_CDECL GScr_DetachEx(uint32_t handle, const ScriptString* model, const ScriptString* tag, const uint8_t* owner);
// The sound builtins return FXCmd_*'s id, -1 for an empty alias or a stale handle; the FXCmd builders run at the
// original's ESP (ESP entries).
// 0x00305d80 (handle, alias, value): FXCmd_PlaySoundAliasMode7.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL GScr_PlaySoundMode7_c(uint32_t entry_esp);
extern "C" void GScr_PlaySoundMode7_entry(void);
// 0x00305de0 (handle, alias, notify, unused, flag): G_SetSoundDoneNotify when a notify is given, then
// FXCmd_PlaySoundAlias looped.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL GScr_PlayLoopSound_c(uint32_t entry_esp);
extern "C" void GScr_PlayLoopSound_entry(void);
// 0x00305e70 (handle, alias, point, notify): FXCmd_PlaySoundAliasAtPoint (not looped); -1 while a sound-done
// notify is pending (+0x3c4).
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL GScr_PlaySoundAtPosition_c(uint32_t entry_esp);
extern "C" void GScr_PlaySoundAtPosition_entry(void);
// 0x00305f00 (handle, alias, notify, unused, flag): as GScr_PlayLoopSound, not looped, -1 while a notify is
// pending.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL GScr_PlaySound_c(uint32_t entry_esp);
extern "C" void GScr_PlaySound_entry(void);
// 0x00305f90 (handle, kind 0-2): FXCmd_PlayWeaponSound (tail jump, EAX = the entity) of the client's weapon's
// (+0x80; records of 0x34 bytes at 0x0096bd10) name +4 with value 0x17 + kind; -1 for other kinds or no client.
// The C version writes the jump's argument slots and returns {target, EAX} (tail-jump entry).
extern "C" PLATFORM_HIDDEN uint64_t XB_CDECL GScr_PlayerWeaponSound_c(uint32_t entry_esp);
extern "C" void GScr_PlayerWeaponSound_entry(void);
// 0x00306030: a vehicle entity (G_Spawn as GScr_SpawnBoxEntity; classname 0x00984c00 and its hash): G_SetModel
// (context -1) - freed again (G_FreeEntity) and 0 without a model -, targetname +0x264, origin +0x140, angles
// +0x150, Vehicle_Init with the def name, G_UpdateEntityStringHashes. Returns the handle.
uint32_t XB_CDECL GScr_SpawnVehicle(const ScriptString* model, const ScriptString* targetname, const ScriptString* def,
                                    const uint32_t* origin, const uint32_t* angles, const uint8_t* owner);
// 0x00306120 / 0x003061e0: an entity by classname (G_Spawn by the owner's context, origin +0x140, spawn flags
// +0x2a0 / 0) through G_CallSpawn: its handle (GScr_SpawnWithFlags also G_UpdateEntityStringHashes), else the
// message (va) and 0.
uint32_t XB_CDECL GScr_SpawnWithFlags(const ScriptString* classname, const uint32_t* origin, uint32_t flags,
                                      const uint8_t* owner);
uint32_t XB_CDECL GScr_Spawn(const ScriptString* classname, const uint32_t* origin, const uint8_t* owner);
// 0x003062a0 (handle): Sentient_CachedNearestNode (radius 192) into the actor's +0x7e0; the node's number or 0.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL GScr_ActorUpdateNearestNode_c(uint32_t entry_esp);
extern "C" void GScr_ActorUpdateNearestNode_entry(void);
// 0x00306310 (handle): 1 when the actor's and its enemy's (sentient +0x3c) cached nearest nodes are the same or
// PathNode_CanSee, else 0.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL GScr_ActorNodeCanSeeEnemyNode_c(uint32_t entry_esp);
extern "C" void GScr_ActorNodeCanSeeEnemyNode_entry(void);
// 0x00306b60: Drop_Weapon of a named weapon (BG_FindWeaponIndexForHash; message for unknown names) at the left
// or right weapon tag (scr_const_left / right, else none), thrown at `speed` from the actor's centre (+12 up),
// pickup time +0x18 = now. Returns the item's handle; 0 for "none", a stale handle or no actor.
uint32_t XB_CDECL GScr_DropWeapon(uint32_t handle, const ScriptString* weapon, uint32_t side, float speed);
// 0x00306d50 (handle, a, b): Actor_FindNearestNode's node number, 0 for none.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL GScr_ActorGetNearestNode_c(uint32_t entry_esp);
extern "C" void GScr_ActorGetNearestNode_entry(void);
// 0x00306db0 (handle, end, offset): Actor_IsShotClear from the muzzle (Actor_GetMuzzleInfo) plus offset to end,
// with 0x0095f704 = the actor's entity meanwhile; 0 without a muzzle.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL GScr_ActorCanShootWithOffset_c(uint32_t entry_esp);
extern "C" void GScr_ActorCanShootWithOffset_entry(void);
// 0x00306e90 (handle, anim a, anim b, goal, blend, rate): unless a prone blend runs (+0x19c, +0x198, +0x1a0
// expired: cleared), both anims of the actor's tree to the same goal (AnimTree_SetGoalWeight), then
// Actor_UpdateAimBlend (tail jump, ECX = actor, EAX = 0; tail-jump entry).
extern "C" PLATFORM_HIDDEN uint64_t XB_CDECL GScr_ActorSetAimAnimWeights_c(uint32_t entry_esp);
extern "C" void GScr_ActorSetAimAnimWeights_entry(void);
// 0x00306f60 (handle, origin, radius, max, min) / 0x00306fd0 (origin, radius, max, min): G_RadiusDamage from the
// entity (0 when stale) / the world entity, means of death 0x17, with 0x0098600c = 0x00986010 meanwhile.
extern "C" PLATFORM_HIDDEN void XB_CDECL GScr_RadiusDamageFromEntity_c(uint32_t entry_esp);
extern "C" void GScr_RadiusDamageFromEntity_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL GScr_RadiusDamage_c(uint32_t entry_esp);
extern "C" void GScr_RadiusDamage_entry(void);

// 0x00307010 (handle): a vehicle's kill - Vehicle_FreePhysics, entity type 15, classname 0x00984c04 (hash +0x260),
// G_UpdateEntityStringHashes, Scr_Notify "death".
extern "C" PLATFORM_HIDDEN void XB_CDECL GScr_Vehicle_Kill_c(uint32_t entry_esp);
extern "C" void GScr_Vehicle_Kill_entry(void);
// 0x00307090 (handle, &node): the vehicle starts its path at the node (VehPath_InitAtNode; a message for a used
// path, +0x297 == 2): origin/angles placed twice around the wheel heights (6 tags 0x009886f8 -> +0x138) and, for
// def types 1/2, Vehicle_UpdatePhysics.
extern "C" PLATFORM_HIDDEN void XB_CDECL GScr_Vehicle_AttachPath_c(uint32_t entry_esp);
extern "C" void GScr_Vehicle_AttachPath_entry(void);
// 0x00307250 (handle, turret handle): Actor_CanUseTurret != 0 (AL).
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL GScr_ActorCanUseTurret_c(uint32_t entry_esp);
extern "C" void GScr_ActorCanUseTurret_entry(void);
// 0x003072d0 (handle, end): GScr_ActorCanShootWithOffset with a zero offset.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL GScr_ActorCanShoot_c(uint32_t entry_esp);
extern "C" void GScr_ActorCanShoot_entry(void);
// 0x00307300 (handle): Actor_MeleeAttack with 0x0095f704 = the actor's entity meanwhile.
extern "C" PLATFORM_HIDDEN void XB_CDECL GScr_ActorMelee_c(uint32_t entry_esp);
extern "C" void GScr_ActorMelee_entry(void);
// 0x00307350 (handle, start, end, fuse seconds) / 0x00307ca0 (5 s): a grenade thrown by the actor from start to
// end on the first arc that works (min energy, low / high by time, fallback).
extern "C" PLATFORM_HIDDEN void XB_CDECL GScr_MagicGrenade_c(uint32_t entry_esp);
extern "C" void GScr_MagicGrenade_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL GScr_MagicGrenadeDefaultFuse_c(uint32_t entry_esp);
extern "C" void GScr_MagicGrenadeDefaultFuse_entry(void);
// 0x00307460 (handle, rate, origin, angles, anim, notify, root anim): G_AnimScriptedStart (an actor aborts its
// anim script and pushes state 10 first). 0x00307cc0: the same with an unread eighth word 0; 0x00307cf0 (handle,
// rate, origin, angles, anim): empty notify, the anim's tree as root, nothing for node 0xffff.
extern "C" PLATFORM_HIDDEN void XB_CDECL GScr_AnimScripted_c(uint32_t entry_esp);
extern "C" void GScr_AnimScripted_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL GScr_AnimScriptedFlag0_c(uint32_t entry_esp);
extern "C" void GScr_AnimScriptedFlag0_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL GScr_AnimScriptedNoNotify_c(uint32_t entry_esp);
extern "C" void GScr_AnimScriptedNoNotify_entry(void);
// 0x00307690 (handle, turret handle): the actor takes the turret (+0x136c) when Actor_CanUseTurret (AL 1).
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL GScr_ActorUseTurret_c(uint32_t entry_esp);
extern "C" void GScr_ActorUseTurret_entry(void);
// 0x00307720 (handle): the actor's grenade throw (Actor_CheckGrenadeThrow, held grenade or fire_grenade).
extern "C" PLATFORM_HIDDEN void XB_CDECL GScr_ActorThrowGrenade_c(uint32_t entry_esp);
extern "C" void GScr_ActorThrowGrenade_entry(void);
// 0x00307950 (handle, out velocity, hand offset, method): whether and how the actor can throw at its enemy.
extern "C" PLATFORM_HIDDEN void XB_CDECL GScr_CheckGrenadeThrow_c(uint32_t entry_esp);
extern "C" void GScr_CheckGrenadeThrow_entry(void);
// 0x00307c10 (handle, goal handle): Sentient_SetGoalEntity, the sentient's +0x20 = -1.
extern "C" PLATFORM_HIDDEN void XB_CDECL GScr_SetGoalEntity_c(uint32_t entry_esp);
extern "C" void GScr_SetGoalEntity_entry(void);
// 0x00307d40 (value, key hash, out, unused): the entities whose key field equals the value (targetname, target,
// classname, groupname, script_noteworthy; else any spawn field) into out; returns the count (no out: the first
// one's handle).
uint32_t XB_CDECL GScr_GetEntArray(const ScriptString* name, uint32_t key, uint32_t* out, uint32_t unused);
// 0x00307ed0 (handle, other, max age seconds): the actor's visibility of the other entity (ST0).
extern "C" PLATFORM_HIDDEN float XB_CDECL GScr_ActorCanSee_c(uint32_t entry_esp);
extern "C" void GScr_ActorCanSee_entry(void);
// 0x00307fa0 (zone): Stream_Update until the zone has loaded.
void XB_CDECL GScr_LoadZoneSync(uint8_t* zone);

// 0x00308010 (handle) / 0x00308070 (handle, max age seconds): the actor's visibility of its enemy
// (Actor_UpdateVisibilityIfStale; default age +0x830 ms), 0 without one (ST0).
extern "C" PLATFORM_HIDDEN float XB_CDECL GScr_ActorCanSeeEnemyDefaultAge_c(uint32_t entry_esp);
extern "C" void GScr_ActorCanSeeEnemyDefaultAge_entry(void);
extern "C" PLATFORM_HIDDEN float XB_CDECL GScr_ActorCanSeeEnemy_c(uint32_t entry_esp);
extern "C" void GScr_ActorCanSeeEnemy_entry(void);
// 0x003080e0 (handle, other): GScr_ActorCanSee with the default age (ST0).
extern "C" PLATFORM_HIDDEN float XB_CDECL GScr_ActorCanSeeDefaultAge_c(uint32_t entry_esp);
extern "C" void GScr_ActorCanSeeDefaultAge_entry(void);
// 0x00308150 (handle): the vehicle's turret fires once per flash tag.
extern "C" PLATFORM_HIDDEN void XB_CDECL GScr_Vehicle_FireWeapon_c(uint32_t entry_esp);
extern "C" void GScr_Vehicle_FireWeapon_entry(void);
// 0x00308720 (weapon name, start, end): a bullet fired by the world entity.
extern "C" PLATFORM_HIDDEN void XB_CDECL GScr_MagicBullet_c(uint32_t entry_esp);
extern "C" void GScr_MagicBullet_entry(void);
// 0x00308a30 (handle, scale) / 0x00308a90 (handle, unused, point): Actor_ShootWeapon.
extern "C" PLATFORM_HIDDEN void XB_CDECL GScr_ActorShoot_c(uint32_t entry_esp);
extern "C" void GScr_ActorShoot_entry(void);
extern "C" PLATFORM_HIDDEN void XB_CDECL GScr_ActorShootAt_c(uint32_t entry_esp);
extern "C" void GScr_ActorShootAt_entry(void);
// 0x00308af0 (handle, flag 28, flag 27, clear enemy): entity flags +0x2a4 bits 28/27 set or cleared; the third
// makes its sentient forget the enemy.
void XB_CDECL GScr_SetAIFlags(uint32_t handle, uint32_t flag28, uint32_t flag27, uint32_t clear_enemy);
// 0x00308bb0 (turret handle, user handle): Turret_FireForUserOrDriver, or the not-a-turret message (tail jumps;
// the C version returns the target and the EAX/ECX words).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL GScr_TurretCall272_c(uint32_t entry_esp, uint32_t* regs);
extern "C" void GScr_TurretCall272_entry(void);
// 0x00308c50 (handle): Actor_Cover_FindBetterCover's result, 0 unless the actor may look for cover.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL GScr_ActorFindBetterCover_c(uint32_t entry_esp);
extern "C" void GScr_ActorFindBetterCover_entry(void);
// 0x00308cc0 / 0x00308df0 (spawner handle, targetname, owner): Actor_SpawnFromSpawner (forced / not); the new
// actor's handle or 0. 0x00308fb0 / 0x00308ff0 (spawner handle, owner): the same with an empty targetname.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL GScr_ForceSpawnNamed_c(uint32_t entry_esp);
extern "C" void GScr_ForceSpawnNamed_entry(void);
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL GScr_DoSpawnNamed_c(uint32_t entry_esp);
extern "C" void GScr_DoSpawnNamed_entry(void);
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL GScr_ForceSpawn_c(uint32_t entry_esp);
extern "C" void GScr_ForceSpawn_entry(void);
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL GScr_DoSpawn_c(uint32_t entry_esp);
extern "C" void GScr_DoSpawn_entry(void);
// 0x00308f20 (handle): the actor's anim move for this frame (Actor_MoveByAnimMode, Actor_ApplyMove, blend).
extern "C" PLATFORM_HIDDEN void XB_CDECL GScr_ActorApplyAnimMove_c(uint32_t entry_esp);
extern "C" void GScr_ActorApplyAnimMove_entry(void);

// 0x003056c0 (out, start, end, hit-box contents, pass handle): GScr_TraceSurfaceType's record from a locational
// trace (SV_LocationalTrace over an entity's cached candidates for near-vertical traces, else
// G_LocationalTrace). Returns ScriptString_AssignCString's result.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL GScr_LocationalTraceSurface_c(uint32_t entry_esp);
extern "C" void GScr_LocationalTraceSurface_entry(void);
// 0x003063c0 (handle, point, angles or null, ignore sight): the actor teleports to the point unless the
// player could see it happen (AL 1 when it did).
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL GScr_ActorTryTeleport_c(uint32_t entry_esp);
extern "C" void GScr_ActorTryTeleport_entry(void);
// 0x00306740 (handle, point): whether the actor may move to the point (AL).
extern "C" PLATFORM_HIDDEN uint8_t XB_CDECL GScr_ActorIsSafeMovePoint_c(uint32_t entry_esp);
extern "C" void GScr_ActorIsSafeMovePoint_entry(void);

}  // namespace game
