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
// 0x00225010: EAX vector to +0x100, +0x120 = the smallest +0x30 of +0xa0 and the +0x80 records.
uint32_t XB_REGS3 XB_CDECL PhysRagdoll_SetPivotAndMinBound(const float* v, uint32_t unused_edx, uint8_t* phys);
// 0x00225070: ECX a, EAX b, XMM0 radius -> stack out (a, b, radius).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Phys_SetSegment(const uint32_t* a, const uint32_t* b, uint8_t* out, uint32_t radius);
extern "C" void Phys_SetSegment_entry(void);
// 0x00225100: EAX def, XMM0, 18 stack words: a ragdoll body definition.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL RagdollBodyDef_Set(uint8_t* def, const uint32_t* args, uint32_t xmm0);
extern "C" void RagdollBodyDef_Set_entry(void);
// 0x00225250: EBX id: the body definition with that id removed.
extern "C" PLATFORM_HIDDEN int32_t XB_CDECL RagdollDef_RemoveBody(int32_t id);
extern "C" void RagdollDef_RemoveBody_entry(void);
// 0x00225a00: EDI ragdoll physics: objects and bodies destroyed.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL PhysRagdoll_DestroyBodies(uint8_t* phys);
extern "C" void PhysRagdoll_DestroyBodies_entry(void);
// 0x00225a40 / 0x00225a90: the model's bone matrices from / to a buffer.
uint32_t XB_CDECL PhysRagdoll_RestoreBoneMatrices(uint8_t* entity, uint8_t* buffer);
uint32_t XB_CDECL PhysRagdoll_SaveBoneMatrices(uint8_t* entity, uint8_t* buffer);
// 0x00225bc0: ECX body record, EAX ragdoll; result in XMM0 (dead).
extern "C" PLATFORM_HIDDEN float XB_CDECL Ragdoll_GetBodyScale(const uint8_t* record, const uint8_t* ragdoll);
extern "C" void Ragdoll_GetBodyScale_entry(void);
// 0x00225c80: EDI ragdoll physics, stack dt.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Ragdoll_UpdateSettle(uint8_t* phys, float* arg);
extern "C" void Ragdoll_UpdateSettle_entry(void);
// 0x00226120: EDX id, stack capsule set: the capsule and its pairs released (dead).
extern "C" PLATFORM_HIDDEN void XB_CDECL PhysRagdoll_RemoveCapsule(int32_t id, uint8_t* set);
extern "C" void PhysRagdoll_RemoveCapsule_entry(void);
// 0x00226200: EAX id, ESI ragdoll: a body removed (dead).
extern "C" PLATFORM_HIDDEN void XB_CDECL Ragdoll_RemoveBody(int32_t id, uint8_t* ragdoll);
extern "C" void Ragdoll_RemoveBody_entry(void);
// 0x00226220: EAX entity: the Bip01 bone indices cached.
void XB_REGS1 XB_CDECL Ragdoll_CacheBip01Bones(const uint8_t* entity);
// 0x002263c0: EDX ragdoll, EDI id: the next free capsule.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL PhysRagdoll_TakeBody(uint8_t* ragdoll, int32_t id);
extern "C" void PhysRagdoll_TakeBody_entry(void);
// 0x00226420: EAX point, ECX bone, stack (ragdoll, linear out, angular out).
extern "C" PLATFORM_HIDDEN void XB_CDECL Ragdoll_GetBoneVelocity(const float* point, int32_t bone, const uint8_t* ragdoll,
                                                                float* linear, float* angular);
extern "C" void Ragdoll_GetBoneVelocity_entry(void);
// 0x002264f0: EBX object: its frame vectors constructed.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL RagdollFrames_Construct(uint8_t* object);
extern "C" void RagdollFrames_Construct_entry(void);
// 0x002279c0: EAX frame, stack capsule: its segment into the frame.
uint8_t* XB_REGS1 XB_STDCALL RagdollCapsule_TransformSegment(const float* m, uint8_t* capsule);

// 0x00229930: EAX physics ragdoll: the bounding sphere of its capsules (+0xbf0, count +0xc44).
void XB_REGS1 XB_STDCALL PhysRagdoll_GetBoundingSphere(const uint8_t* ragdoll, float* center, float* radius);
// 0x00229c90: EDI capsule: its bounding sphere (+0x90, radius +0xa0) from its spheres (+0x80, count +0x84).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL PhysRagdoll_ComputeBounds(uint8_t* capsule);
extern "C" void PhysRagdoll_ComputeBounds_entry(void);
// 0x00229d20: a capsule pair (two capsules): a contact between their bodies at the closest points.
extern "C" PLATFORM_HIDDEN void XB_CDECL RagdollCapsule_CollidePair(const uint8_t* pair, uint32_t entry_esp);
extern "C" void RagdollCapsule_CollidePair_entry(void);
// 0x00229f00: EAX entity, EDI angular out, stack linear out: the velocity of the physics body or
// vehicle it rides; 0 (zero vectors) when there is none.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL PhysObj_GetInheritedVelocity(uint8_t* ent, float* angular, float* linear);
extern "C" void PhysObj_GetInheritedVelocity_entry(void);
// 0x0022c760: XMM0 time, stack ragdoll: the bone frames from the model's base pose (previous = current).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Ragdoll_StoreModelBoneFrames(float time, uint8_t* ragdoll);
extern "C" void Ragdoll_StoreModelBoneFrames_entry(void);
// 0x0022c890: XMM0 time, stack ragdoll: the bone frames from the world bone matrices.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Ragdoll_RefreshBoneMatrices(float time, uint8_t* ragdoll);
extern "C" void Ragdoll_RefreshBoneMatrices_entry(void);
// 0x0022c910: EAX entity, stack ragdoll: the ten ragdoll bones looked up and their frames set; 0 when one is missing.
uint8_t XB_REGS1 XB_STDCALL Ragdoll_BindBones(uint8_t* entity, uint8_t* ragdoll);

// 0x0022b0d0: a body definition (ECX; bones +0, +4) fitted between the entity's two bone matrices:
// ends +0x40/+0x50 pulled in by +0x30/+0x34, +0x60 the blend point (+8) in the first bone's frame,
// inertia +0xc0 (sphere of radius +0xd0, mass +0xb4), parent bone +0xe8 and its base pose +0x100.
void XB_THISCALL RagdollBodyDef_FitToBones(uint8_t* def, uint8_t* entity);
// 0x0022cec0 (bone callback): out = inverse(frame) * the bone's ({entity, bone}) world matrix.
float* XB_CDECL Ragdoll_PoseToBody(const uint32_t* bone, float* out, const float* frame);
// 0x0022ca00 (bone callback): out (+0xc) = the frame +0x70 * the matrix +8, then +0x30 = the point
// +0x20 out of it.
uint32_t XB_STDCALL PhysBone_UpdateWorldFrame(uint8_t* bone);

// 0x0022fb30: ESI ragdoll: its physics released (bodies to pose, physics freed, entity +0x294 = 0).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Ragdoll_FreePhysics(uint8_t* ragdoll, uint32_t eax);
extern "C" void Ragdoll_FreePhysics_entry(void);
// 0x002301f0: one physics step of a ragdoll: settle, bodies to pose, the bone blend in (+0x1d40 /
// +0x1d44), "physicsdone" and the physics released once settled or fallen below -60000.
void XB_STDCALL Ragdoll_Update(uint8_t* ragdoll, float dt);
// 0x002302d0: ESI ragdoll, stack time: the bone frames from the model, the launch step.
extern "C" PLATFORM_HIDDEN void XB_CDECL Ragdoll_UpdateFromEntity(uint8_t* ragdoll, float time);
extern "C" void Ragdoll_UpdateFromEntity_entry(void);
// 0x00230350: Ragdoll_Update for every used ragdoll (0x008f02e0, count 0x008f0464).
void XB_CDECL Ragdoll_UpdateAll(float dt);
// 0x00230390: the bone frames from the model and the launch step for every used ragdoll.
void XB_CDECL Ragdoll_UpdateAllFromEntities(float time);
// 0x00231770: EBX capsule set (physics ragdoll +0x1010): the capsules collide with the world
// geometry around the ragdoll and with each other.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL PhysRagdoll_Collide(uint8_t* set, uint32_t esi, uint32_t edi, uint32_t entry_esp);
extern "C" void PhysRagdoll_Collide_entry(void);
// 0x00233b80: PhysRagdoll_Collide for every used ragdoll with physics.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Ragdoll_CollideAll(uint32_t entry_esp);
extern "C" void Ragdoll_CollideAll_entry(void);
// 0x00234380: EBX launch value, stack ragdoll: its physics (re)started; a new one from the pool
// (0x008fb450, six) is built (PhysRagdoll_Build) and the entity gets flags 0x410000.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Ragdoll_StartPhysics(uint32_t value, uint8_t* ragdoll, uint32_t entry_eax);
extern "C" void Ragdoll_StartPhysics_entry(void);
// 0x002348e0: ESI ragdoll: its physics released, then the ragdoll back to its pool (0x008dfde0).
extern "C" PLATFORM_HIDDEN void XB_CDECL Ragdoll_Free(uint8_t* ragdoll);
extern "C" void Ragdoll_Free_entry(void);
// 0x00234930: EDI entity: its ragdoll (+0x238) or a new one from the pool, bound to the bones; 0
// (the ragdoll freed) when a bone is missing.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL Ragdoll_Alloc(uint8_t* entity);
extern "C" void Ragdoll_Alloc_entry(void);

// 0x00228460: per bone of the entity's model a 0x60-byte record (pool 0x008dfdd0, count 0x008dfdd4)
// of its base frame, quaternion, bone, ragdoll slot; built once per model.
void XB_STDCALL Ragdoll_CacheBaseBoneFrames(uint8_t* entity);
// 0x00227bd0: per bone the base pose local to its parent as a quaternion (+0x5a0) and origin (+0xb50).
void XB_STDCALL PhysRagdoll_StoreLocalBasePose(uint8_t* phys, uint8_t* entity);
// 0x00228bf0: ESI vehicle physics: the four wheels traced down against the world meshes.
extern "C" PLATFORM_HIDDEN void XB_CDECL VehiclePhys_TraceWheels(uint8_t* phys);
extern "C" void VehiclePhys_TraceWheels_entry(void);
// 0x00228870: EAX point on a, stack (a, b, point on b): an attach constraint between the bodies (dead).
void XB_REGS1 XB_CDECL Phys_AttachEntitiesAtPoints(const float* point_a, uint8_t* a, uint8_t* b, const float* point_b);
// 0x00226950: EAX vehicle physics: its debug view with the drawing compiled out (dead).
void XB_REGS1 XB_CDECL VehiclePhys_DebugDrawBody(const uint8_t* phys);
// 0x00230930: every physics object's entity matrix and pose from its body; "physicsdone" at rest.
void XB_CDECL PhysObj_UpdateAll();
// 0x002303f0: a physics object with a box body for the entity, or 0.
uint8_t* XB_CDECL PhysObj_Create(uint8_t* ent, uint32_t arg, float mass, float bounce);

// 0x0022c0f0: the model's bone matrices after a ragdoll step (bones without a ragdoll body follow
// their parent with the base frame).
void XB_STDCALL Ragdoll_PropagateBoneMatrices(uint8_t* entity);
// 0x00230e40: bone callback: the physics frame into the model's bone matrix.
void XB_CDECL Ragdoll_BodyToPose(const uint32_t* bone, const float* frame, const float* local);
// 0x0022fb70: the ragdoll's two sets of ten bone frames in world space (0x00988fb0, 0x00988d30).
void XB_STDCALL Ragdoll_BuildWorldBoneFrames(uint8_t* ragdoll);
// 0x00227290: ECX b1, ESI out point on a, 7 stack words: closest points of two capsules; AL 1 on contact.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL Phys_CapsuleCapsuleClosest(const float* b1, float* out_a, const uint32_t* args);
extern "C" void Phys_CapsuleCapsuleClosest_entry(void);

// 0x0022d7d0: the world geometry and entities around a sphere for the ragdoll's collision step;
// EAX the area entity count.
uint32_t XB_CDECL Ragdoll_GatherCollisionGeometry(const float* center, float radius);
// 0x0022b500: the model's bone matrices blended from the ragdoll toward the base pose by t.
void XB_STDCALL Ragdoll_BlendBonePoses(uint8_t* phys, uint8_t* entity, float t);

// 0x0022ebe0: the ten ragdoll body definitions (0x00989250) for the entity's Bip01 bones.
void XB_CDECL Ragdoll_BuildBodyDefs(uint8_t* entity);

// 0x00233c40: EAX entity, EBX vehicle physics: its box body, physics object and wheels; EAX +0x90.
extern "C" PLATFORM_HIDDEN uint8_t* XB_CDECL VehiclePhys_Init(uint8_t* ent, uint8_t* phys);
extern "C" void VehiclePhys_Init_entry(void);

// 0x00228ee0 (vtable 0x0083c5f0): the vehicle physics' constraint rows between bodies a and b.
void XB_THISCALL VehiclePhys_AddContacts(uint8_t* phys, uint8_t* a, uint8_t* b, uint8_t* world, float dt);

// 0x00228bc0: the debug view of every vehicle physics (dead).
void XB_CDECL VehiclePhys_DebugDrawAll();
// 0x00231820 (EAX index, EBX ragdoll, 1 stack): one joint's debug text, or all ten (dead).
extern "C" PLATFORM_HIDDEN void XB_CDECL Ragdoll_DrawJointsDebug(int32_t index, uint8_t* ragdoll, uint32_t mode);
extern "C" void Ragdoll_DrawJointsDebug_entry(void);
// 0x002341f0 (EAX vehicle, 1 unused stack slot): the drive inputs to wheel modes and motor forces.
void XB_REGS1 XB_STDCALL VehiclePhys_ApplyDriveInputs(uint8_t* vehicle, uint32_t);
// 0x0022dd40: a ragdoll joint's debug text (dead).
void XB_THISCALL Ragdoll_DrawJointDebug(uint8_t* joint, uint32_t mode);

// 0x0022a590: the vehicle physics body, wheel constraints and drive values from its def.
void XB_STDCALL VehiclePhys_SetupFromDef(uint8_t* vehicle, uint8_t* def, uint32_t build);

// 0x00231860 (stdcall phys, entity): the ragdoll physics from the body definitions (gap entry: the
// frame stays at the original's ESP).
extern "C" PLATFORM_HIDDEN void XB_CDECL PhysRagdoll_Build(uint8_t* phys, uint8_t* ent, uint32_t ebx, uint32_t esi,
                                                       uint32_t edi, uint32_t ebp, uint32_t entry_esp);
extern "C" void PhysRagdoll_Build_entry(void);

// 0x002348a0: the drive inputs of every vehicle physics in the list 0x008fe0b0 (dead).
void XB_CDECL VehiclePhys_ApplyDriveInputsAll(uint32_t unused);
// 0x00234330: EAX value, ESI phys, ECX entity: builds the ragdoll and resets its rest state (dead).
extern "C" PLATFORM_HIDDEN void XB_CDECL PhysRagdoll_BuildAndReset(uint32_t value, uint8_t* phys, uint8_t* ent);
extern "C" void PhysRagdoll_BuildAndReset_entry(void);
// 0x002255d0: ESI vehicle: motor or brake per wheel constraint from the wheel mode bits +0x504.
extern "C" PLATFORM_HIDDEN void XB_CDECL VehiclePhys_ApplyWheelMotors(uint8_t* vehicle);
extern "C" void VehiclePhys_ApplyWheelMotors_entry(void);
// 0x002256b0: EAX vehicle: the four wheel constraints' friction +0x64/+0x68 from the def.
void XB_REGS1 XB_CDECL VehiclePhys_ApplyWheelFriction(uint8_t* vehicle);

// 0x0022a0b0 (gap function, 11-11): VehiclePhys_TraceWheels on each vehicle of the list 0x008feb70 (count
// 0x008feb9c).
void XB_CDECL VehiclePhys_TraceAllWheels();

}  // namespace game
