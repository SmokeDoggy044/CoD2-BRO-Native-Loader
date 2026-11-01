#pragma once

// Ragdolls: capsules of spheres on physics bodies, their pools, forces and world collision.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00029bd0: sphere (EAX) local position to world through the matrix in ECX; previous = current.
uint8_t* XB_REGS3 XB_CDECL RagdollSphere_TransformToWorld(uint8_t* sphere, uint32_t unused_edx, const float* m);
// 0x00029ce0: resets a ragdoll's pools (ragdoll in EDI); stores value at +0x1d30.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL PhysRagdoll_InitState(uint8_t* ragdoll, uint32_t value);
extern "C" void PhysRagdoll_InitState_entry(void);
// 0x00029d50: force (EAX) on one body of a ragdoll.
void XB_REGS1 XB_STDCALL Ragdoll_AddForce(const float* force, void* ragdoll, uint32_t body);
// 0x00029da0: force (EAX) at a point (ECX) on one body of a ragdoll.
void XB_REGS3 XB_STDCALL Ragdoll_AddForceAtPoint(const float* force, uint32_t unused_edx, const float* point, void* ragdoll,
                                                 uint32_t body, uint32_t arg3);
// 0x0002a5d0: moves a capsule's segment and spheres to world space.
void XB_STDCALL RagdollCapsule_UpdateWorldPoints(uint8_t* capsule);
// 0x0002a620: pushes a capsule's spheres out of the world and adds contacts with the static body.
extern "C" PLATFORM_HIDDEN void XB_CDECL RagdollCapsule_CollideWorld(uint8_t* capsule, uint32_t entry_ebx, uint32_t entry_esp);
extern "C" void RagdollCapsule_CollideWorld_entry(void);
// 0x0002a910: bounding sphere of a capsule (capsule in ECX).
void XB_REGS3 XB_CDECL RagdollCapsule_GetBoundingSphere(uint32_t unused_eax, uint32_t unused_edx, const uint8_t* capsule,
                                                        float* center, float* radius);
// 0x0002aec0: appends a sphere to a capsule (capsule in EDX); returns the slot.
uint8_t* XB_REGS2 XB_STDCALL RagdollCapsule_AddSphere(uint32_t unused_eax, uint8_t* capsule, const uint8_t* sphere);
// 0x0002b4c0: resets the capsule pool (pool in ESI).
extern "C" PLATFORM_HIDDEN void XB_CDECL RagdollCapsulePool_Reset(uint8_t* pool);
extern "C" void RagdollCapsulePool_Reset_entry(void);
// 0x0002b500: resets the capsule pair pool (pool in EAX).
uint8_t* XB_REGS1 XB_CDECL RagdollCapsulePairPool_Reset(uint8_t* pool);

// 0x0002af10: frees a capsule (EAX) to its pool (ECX).
void XB_REGS3 XB_CDECL RagdollCapsulePool_Free(uint8_t* capsule, uint32_t unused_edx, uint8_t* pool);
// 0x0002afb0: frees a ragdoll (EAX) to its pool (ECX), releasing its physics.
void XB_REGS3 XB_CDECL RagdollPool_Free(uint8_t* ragdoll, uint32_t unused_edx, uint8_t* pool);
// 0x0002b0a0: frees a physics ragdoll (EAX) to its pool (ECX), destroying its bodies.
void XB_REGS3 XB_CDECL RagdollPhysPool_Free(uint8_t* phys, uint32_t unused_edx, uint8_t* pool);
// 0x0002b3a0: resets a bone pool (ten 0x50-byte bones; pool in ECX).
void XB_REGS3 XB_CDECL RagdollBonePool_Reset(uint32_t unused_eax, uint32_t unused_edx, uint8_t* pool);
// 0x0002b570: releases the physics of every used ragdoll.
void XB_STDCALL RagdollPool_ReleaseAllPhysics(uint8_t* pool);
// 0x0002b5f0: releases all physics and resets the ragdoll pool (pool in ESI).
extern "C" PLATFORM_HIDDEN void XB_CDECL RagdollPool_Clear(uint8_t* pool);
extern "C" void RagdollPool_Clear_entry(void);
// 0x0002b6d0: destroys the objects and bodies of every used physics ragdoll.
void XB_STDCALL RagdollPhysPool_DestroyAll(uint8_t* pool);
// 0x0002b740: destroys all and resets the physics ragdoll pool (pool in ESI).
extern "C" PLATFORM_HIDDEN void XB_CDECL RagdollPhysPool_Clear(uint8_t* pool);
extern "C" void RagdollPhysPool_Clear_entry(void);

// Physics ragdoll methods (ragdoll in ECX).
// 0x000b2ff0: sets the bone <-> body callbacks (two function pointers).
void XB_CDECL Phys_SetBoneCallbacks(const uint32_t* callbacks);
// 0x000b3050: resets the rest state and the launch frames.
void XB_THISCALL PhysRagdoll_ResetRestState(uint8_t* phys);
// 0x000b3090: launch target; two launch frames.
void XB_THISCALL PhysRagdoll_SetLaunchTarget(uint8_t* phys, const float* target);
// 0x000b3110: allocates body `index`; returns it.
uint32_t XB_THISCALL PhysRagdoll_AllocBody(uint8_t* phys, int32_t index);
// 0x000b3130: joint between bodies a and b at joint slot b; returns it.
uint32_t XB_THISCALL PhysRagdoll_CreateJoint(uint8_t* phys, int32_t a, int32_t b);
// 0x000b31c0: frees 0xd0 constraint `index`.
void XB_THISCALL PhysRagdoll_FreeConstraintD0(uint8_t* phys, int32_t index);
// 0x000b31f0: one launch step toward the target while frames are left.
void XB_THISCALL PhysRagdoll_UpdateLaunchToTarget(uint8_t* phys);
// 0x000b3240: centre of mass and its velocity (vec3s).
void XB_THISCALL PhysRagdoll_GetCenterOfMass(uint8_t* phys, float* center, float* velocity, uint32_t arg3);
// 0x000b3300: wakes the ragdoll and adds a force at a point to one body.
void XB_THISCALL PhysRagdoll_AddBodyForceAtPoint(uint8_t* phys, int32_t index, const float* point, const float* force,
                                                 uint32_t arg4);
// 0x000b3360: appends count empty body/joint/constraint slots.
void XB_THISCALL PhysRagdoll_AddEmptySlots(uint8_t* phys, int32_t count);
// 0x000b33e0: iterates the joints until the bodies sit on their anchors.
void XB_THISCALL PhysRagdoll_SolveJointPositions(uint8_t* phys);
// 0x000b34a0: unlinks and destroys body `index` and its bones.
void XB_THISCALL PhysRagdoll_RemoveBody(uint8_t* phys, int32_t index);
// 0x000b35b0: attaches body `index` to the owner's object.
void XB_THISCALL PhysRagdoll_AttachBodyToObject(uint8_t* phys, uint32_t owner, int32_t index, uint32_t arg3,
                                                uint32_t arg4);
// 0x000b3640: destroys the attached objects.
void XB_THISCALL PhysRagdoll_DestroyObjects(uint8_t* phys);
// 0x000b3690, 0x000b36e0: bodies -> bone poses, bone poses -> bodies (the bone callbacks).
void XB_THISCALL PhysRagdoll_WriteBodiesToPose(uint8_t* phys);
void XB_THISCALL PhysRagdoll_PoseToBodies(uint8_t* phys);

// 0x000b3730: rest detection (dt on the stack).
void XB_THISCALL PhysRagdoll_UpdateRestState(uint8_t* phys, float dt);
// 0x000b38a0: body+0x134 of every body.
void XB_THISCALL PhysRagdoll_SetBodiesField134(uint8_t* phys, float value);
// 0x000b38d0: wakes the ragdoll and adds a force to one body.
void XB_THISCALL PhysRagdoll_AddBodyForce(uint8_t* phys, int32_t index, const float* force);
// 0x000b3960, 0x000b3a80: per-axis damping of one body's linear / angular velocity.
void XB_THISCALL PhysRagdoll_DampBodyLinear(uint8_t* phys, int32_t index, const float* factors);
void XB_THISCALL PhysRagdoll_DampBodyAngular(uint8_t* phys, int32_t index, const float* factors);
// 0x000b3ba0, 0x000b3bd0: the same for every body.
void XB_THISCALL PhysRagdoll_DampLinear(uint8_t* phys, const float* factors);
void XB_THISCALL PhysRagdoll_DampAngular(uint8_t* phys, const float* factors);
// 0x000b3c00: pushes every body toward a velocity.
void XB_THISCALL PhysRagdoll_AddVelocityForce(uint8_t* phys, const float* velocity);

// 0x000b3dc0: attaches the body to the owner at a world point (the point in both frames).
void XB_THISCALL PhysRagdoll_AttachBodyAtPoint(uint8_t* phys, uint8_t* owner, int32_t index, const float* point);
// 0x000b3ef0: frees a bone to the bone pool (ECX = phys + 0xf0).
void XB_THISCALL PhysRagdoll_FreeBone(uint8_t* pool, uint8_t* bone);
// 0x000b3f90: adds the 0x1c0 object to the attached-object list unless it is there.
void XB_THISCALL PhysRagdoll_AddAttachedObject(uint8_t* phys, uint32_t object);

// 0x00029b70: empty link lists in the ragdoll's five 0x30 blocks, the bone pool at +0xf0 reset,
// +0x4a4 = 0 (ESI = ragdoll; returns it).
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL PhysRagdoll_InitLinks(uint8_t* ragdoll);
extern "C" void PhysRagdoll_InitLinks_entry(void);
// 0x00224fc0: ECX ragdoll, EDX id: the capsule with that id (+0x128), or null; EDX kept.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL PhysRagdoll_FindCapsuleById(uint8_t* ragdoll, int32_t id);
extern "C" void PhysRagdoll_FindCapsuleById_entry(void);

}  // namespace game
