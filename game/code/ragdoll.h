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
void XB_STDCALL RagdollCapsule_CollideWorld(uint8_t* capsule);
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

}  // namespace game
