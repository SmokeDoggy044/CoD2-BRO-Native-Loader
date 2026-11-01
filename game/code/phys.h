#pragma once

// Rigid-body physics: shapes, contacts, the contact allocator of the physics world.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x0001b9e0: support point of a shape placed in a frame (vtable slot); returns out.
float* XB_THISCALL PhysShape_SupportInFrame(void* shape, float* out, const float* frame, const float* dir);
// 0x0001bb60: intersection test of a shape placed in a frame (vtable slot); hit = point, normal.
uint8_t XB_THISCALL PhysShape_IntersectInFrame(void* shape, const float* frame, const float* point, uint32_t arg3,
                                               float* hit, uint32_t arg5);
// 0x0002ad30: separation bias of a contact (+0x78 / +0x7c).
void XB_THISCALL PhysContact_ComputeBias_2(uint8_t* contact, float dt, float limit);
// 0x0002ade0: allocates a contact record and appends it to the world's contact list.
uint8_t* XB_THISCALL PhysWorld_AllocContact(uint8_t* world);
// 0x0002ae40: allocates a contact record, appends it to the second list and sets it up.
uint8_t* XB_THISCALL PhysContacts_Add(uint8_t* world, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                                      uint32_t a6);

// 0x0002b150: frees a physics object (EAX) to its pool (ECX).
void XB_REGS3 XB_CDECL PhysObjPool_Free(uint8_t* obj, uint32_t unused_edx, uint8_t* pool);
// 0x0002b2c0: allocates and constructs a vehicle physics object (pool in EDX); null when full.
uint8_t* XB_REGS2 XB_CDECL VehiclePhys_AllocFromPool(uint32_t unused_eax, uint8_t* pool);
// 0x0002b300: frees a vehicle physics object (EAX) to its pool (ECX).
void XB_REGS3 XB_CDECL VehiclePhysPool_Free(uint8_t* phys, uint32_t unused_edx, uint8_t* pool);
// 0x0002b7f0: resets the physics object pool (pool in EDX).
void XB_REGS2 XB_CDECL PhysObjPool_Reset(uint32_t unused_eax, uint8_t* pool);
// 0x0002b8c0: resets the vehicle physics pool (pool in EAX).
uint8_t* XB_REGS1 XB_CDECL VehiclePhysPool_Reset(uint8_t* pool);
// 0x0002b990: v clamped by its x to [*lo, *hi] (the bound splatted); returns out.
float* XB_REGS3 XB_CDECL Vec4_ClampToScalarRange(float* out, uint32_t unused_edx, const float* v, const float* lo,
                                                 const float* hi);

// 0x000acc70, 0x000acd00, 0x000acd80, 0x000acdf0, 0x000aefa0: support points (vtable +8) of a
// segment, sphere, box, capsule and oriented box; return out.
float* XB_THISCALL PhysSegment_Support(uint8_t* shape, float* out, const float* dir);
float* XB_THISCALL PhysSphere_Support(uint8_t* shape, float* out, const float* dir);
float* XB_THISCALL PhysBox_Support(uint8_t* shape, float* out, const float* dir);
float* XB_THISCALL PhysCapsule_Support(uint8_t* shape, float* out, const float* dir);
float* XB_THISCALL PhysOrientedBox_Support(uint8_t* shape, float* out, const float* dir);

// 0x000acf70, 0x000ad040, 0x000adc90: sphere contact tests (vtable) of a point, sphere and capsule;
// AL = contact, out = surface point and normal.
uint8_t XB_THISCALL PhysPoint_SphereContact(uint8_t* shape, const float* p, float r, float* out, uint32_t unused);
uint8_t XB_THISCALL PhysSphere_SphereContact(uint8_t* shape, const float* p, float r, float* out, uint32_t unused);
uint8_t XB_THISCALL PhysCapsule_SphereContact(uint8_t* shape, const float* p, float r, float* out, uint32_t unused);

// 0x000ada90: does the segment a-b (direction dir) hit the box (vtable slot)? AL.
uint8_t XB_THISCALL PhysBox_IntersectSegment(uint8_t* shape, const float* a, const float* b, const float* dir, uint32_t unused);

// 0x000afde0: box shape constructor (centre, half extents); returns the box.
uint8_t* XB_THISCALL PhysBox_Construct(uint8_t* box, const float* center, const float* extent);
// 0x000affc0: squared distance from p to the box.
float XB_CDECL PhysBox_PointDistanceSq(const float* p, const uint8_t* box);
// 0x000b07a0: p in the frame strictly inside the box? AL.
uint8_t XB_CDECL PhysBox_ContainsPointInFrame(const uint8_t* box, const float* frame, const float* p);
// 0x000b08c0: sphere shape in the frame overlapping the box? AL.
uint8_t XB_CDECL PhysBox_OverlapsSphereInFrame(const uint8_t* box, const float* frame, const uint8_t* sphere);
// 0x000b1500: squared distance from p (moved into the frame) to the box.
float XB_CDECL PhysBox_PointDistanceSqInFrame(const float* p, const uint8_t* box, const float* frame);
// 0x000b25d0: GJK contact of two shapes with a 0.03 inset.
int32_t XB_CDECL Phys_GjkContact(uint32_t a, uint32_t b, uint32_t c, float* out);

// 0x000ad770: box against the ray a-b (vtable); hit = point, normal; *depth when given. AL.
uint8_t XB_THISCALL PhysBox_IntersectRay(uint8_t* box, const float* a, const float* b, float* out, uint32_t unused,
                                         float* depth);
// 0x000af800, 0x000afbd0: oriented box against a ray / a segment (vtable), in the box frame. AL.
uint8_t XB_THISCALL PhysOrientedBox_IntersectRay(uint8_t* shape, const float* a, const float* b, float* out,
                                                 uint32_t arg4, float* depth);
uint8_t XB_THISCALL PhysOrientedBox_IntersectSegment(uint8_t* shape, const float* a, const float* b, const float* dir,
                                                     uint32_t arg4);
// 0x000b26c0: contact of a sphere shape in a frame with another shape (GJK refined). AL.
uint8_t XB_CDECL Phys_SphereShapeContact(const uint8_t* sphere, const float* frame, uint8_t* shape, float* out);

// 0x000b4000: zeroes the 0x30-byte world creation parameters (Phys_Init's stack record); returns them.
uint8_t* XB_THISCALL PhysWorldParams_Init(uint8_t* params);
// 0x000b4030: the world's static body (phys_world + 0x40).
uint8_t* XB_CDECL PhysWorld_GetStaticBody();
// 0x000b4070: world +0x14, +0x18 (ints) and +0x1c (float), set once by Phys_Init.
void XB_CDECL PhysWorld_SetParams14(uint32_t a, uint32_t b, uint32_t c);
// 0x000b40d0: world +0x20, +0x24 (ints) and +0x28 (float), set once by Phys_Init.
void XB_CDECL PhysWorld_SetParams20(uint32_t a, uint32_t b, uint32_t c);
// 0x000b4130: world +0x2c (float).
void XB_CDECL PhysWorld_SetField2C(uint32_t value);
// 0x000b4160: world +0xc (a callback, 0x00233c30 from Phys_Init).
void XB_CDECL PhysWorld_SetCallbackC(uint32_t callback);
// 0x000b4170: steps the physics world by dt.
void XB_CDECL Phys_RunFrame(float dt);
// 0x000b4450: next free 0x10 link (list +0x268, used +0x274, size +0x270) holding a, b.
uint32_t* XB_CDECL PhysWorld_AllocLink10(uint32_t a, uint32_t b);
// 0x000b44a0: the world's 0x1c0 object whose owner (+0x1b0) is owner, or null.
uint8_t* XB_CDECL PhysWorld_FindObject1c0ByOwner(uint32_t owner);
// 0x000b47b0: next free body (list +0x1ec, used +0x1f8, size +0x1f4), or null.
uint8_t* XB_CDECL PhysWorld_AllocBody();
// 0x000b47e0: a 0x50 constraint between a and b from the pool at world +0x228.
uint32_t XB_CDECL PhysWorld_AllocConstraint50(uint32_t a, uint32_t b);
// 0x000b4860: next free constraint (list +0x254, used +0x260, size +0x25c) between a and b.
uint32_t* XB_CDECL PhysWorld_AllocConstraint(uint32_t a, uint32_t b);
// 0x000b48c0: a 0x28c constraint between a and b from the pool at world +0x28c.
uint32_t XB_CDECL PhysWorld_AllocConstraint28c(uint32_t a, uint32_t b);
// 0x000b49b0: frees a 0xd0 constraint to the pool at world +0x2a0.
void XB_CDECL PhysWorld_FreeConstraintD0(uint32_t constraint);

// 0x000b4b10: next free 0x1c0 object (pool at world +0x1d4), or null.
uint8_t* XB_CDECL PhysWorld_AllocObject1c0();
// 0x000b4b40: next free 0x1b0 constraint (pool at world +0x278) between a and b, set up.
uint32_t* XB_CDECL PhysWorld_AllocConstraint1B0(uint32_t a, uint32_t b);
// 0x000b4bc0: PhysWorld_GetBodyPair on the pool at world +0x1fc.
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL PhysWorld_GetBodyPair_Wrapper(uint32_t a, uint32_t b, uint32_t entry_ebx, uint32_t entry_esi,
                                                uint32_t entry_edi, uint32_t entry_esp);
extern "C" void PhysWorld_GetBodyPair_Wrapper_entry(void);
// 0x000b5010: frees every constraint of the world that links the body.
void XB_CDECL PhysWorld_RemoveBodyConstraints(uint32_t body);
// 0x000b56e0: frees the body's constraints, then the body.
void XB_CDECL PhysWorld_DestroyBody(uint32_t body);
// 0x000b5710: frees the object's constraints, then the 0x1c0 object.
void XB_CDECL PhysWorld_DestroyObject1c0(uint32_t object);
// 0x000b57a0..0x000b59c0: world pool resets (ECX = pool); EAX = the last index + 1 (0 when empty).
int32_t XB_THISCALL PhysPool1c0_Reset(uint8_t* pool);
int32_t XB_THISCALL PhysBodyPool_Reset(uint8_t* pool);
int32_t XB_THISCALL PhysConstraintPool30_Reset(uint8_t* pool);
int32_t XB_THISCALL PhysConstraintPool50_Reset(uint8_t* pool);
int32_t XB_THISCALL PhysConstraintPoolF0_Reset(uint8_t* pool);
int32_t XB_THISCALL PhysConstraintPool60_Reset(uint8_t* pool);
int32_t XB_THISCALL PhysPool10_Reset(uint8_t* pool);
int32_t XB_THISCALL PhysConstraintPoolE0_Reset(uint8_t* pool);
int32_t XB_THISCALL PhysConstraintPoolD0_Reset(uint8_t* pool);
// 0x000b5a00 / 0x000b5a50: AVL rotations of the pair tree (node: left +0, right +4, balance +8);
// *root becomes the old root's left / right child, which is returned.
uint8_t* XB_STDCALL PhysPairTree_RotateRight(uint8_t** root);
uint8_t* XB_STDCALL PhysPairTree_RotateLeft(uint8_t** root);
// 0x000b5aa0: sets up a 0x1b0 constraint: twelve 8-byte slots from +0x34 with -1, an empty
// self-linked list at +0x110 (+0x190/+0x194), +0x30 = 0.
uint8_t* XB_THISCALL PhysConstraint1B0_Init(uint8_t* constraint);

// 0x000af120: oriented box (frame +0x30, half extents +0x70) against a sphere: the centre moved
// into the box frame, the box's sphere contact (0x000ae970), point and normal moved back out (the
// normal also on a miss). Unreferenced (no vtable holds it). AL.
uint8_t XB_THISCALL PhysOrientedBox_SphereContact(uint8_t* shape, const float* centre, uint32_t radius, float* out,
                                                  uint32_t arg4);
// 0x000af460: oriented box against a sphere swept from a to b (direction dir): the box's sweep
// (0x000ad140) in the box frame; a hit writes point and normal over b. Unreferenced. AL.
uint8_t XB_THISCALL PhysOrientedBox_SweepSphere(uint8_t* shape, const float* a, float* b, uint32_t radius,
                                                const float* dir);
// 0x000b16b0: GJK sub-determinants for the simplex with the new point (globals gjk_*).
void XB_CDECL Gjk_ComputeDeterminants();

// 0x000b5ae0..0x000b5f10, 0x000b6250: world pool frees (ECX = pool, element on the stack): the last
// used element takes the freed one's list slot; freeing the only one resets the pool. The 0x30 pool
// first clears the word the constraint's back-pointer (+0x2c) names.
void XB_THISCALL PhysPool1c0_Free(uint8_t* pool, uint32_t element);
void XB_THISCALL PhysBodyPool_Free(uint8_t* pool, uint32_t element);
void XB_THISCALL PhysConstraintPool30_Free(uint8_t* pool, uint32_t element);
void XB_THISCALL PhysConstraintPool50_Free(uint8_t* pool, uint32_t element);
void XB_THISCALL PhysConstraintPoolF0_Free(uint8_t* pool, uint32_t element);
void XB_THISCALL PhysConstraintPool60_Free(uint8_t* pool, uint32_t element);
void XB_THISCALL PhysConstraintPoolE0_Free(uint8_t* pool, uint32_t element);
void XB_THISCALL PhysConstraintPoolD0_Free(uint8_t* pool, uint32_t element);
void XB_THISCALL PhysConstraintPool1B0_Free(uint8_t* pool, uint32_t element);
// 0x000b61b0: world pool reset for 0x1b0 constraints.
int32_t XB_THISCALL PhysConstraintPool1B0_Reset(uint8_t* pool);
// 0x000b5f90: next 0x18-byte node of the pair tree (storage +0, capacity +4, used +8): children
// and balance 0, the key pair at +0xc, payload +0x14 = 0. A full tree writes through null.
uint8_t* XB_THISCALL PhysPairTree_AllocNode(uint8_t* tree, const uint32_t* key);
// 0x000b5fe0 / 0x000b6040: next entry of a world pool (cdecl, second argument unused) with the
// two bodies at +0 and +4; the 0x50 constraint gets -1 at +0x30/+0x38/+0x40 and 0 at +0x48, the
// 0x28c one -1 at +0xa8/+0xb0/+0xb8/+0xc0. A full pool writes through null.
uint32_t* XB_CDECL PhysConstraintPool_Alloc(uint8_t* pool, uint32_t unused, uint32_t a, uint32_t b);
uint32_t* XB_CDECL PhysConstraintPool28c_Alloc(uint8_t* pool, uint32_t unused, uint32_t a, uint32_t b);
// 0x000b60b0: AVL insert of the tree's key (tree +0x10 points at the pair) below *slot; the
// node found or made goes to tree +0x14. EAX: nonzero when the subtree grew.
int32_t XB_THISCALL PhysPairTree_Insert(uint8_t* tree, uint8_t** slot);
// 0x000b62d0: the pair record (0x30 pool) of two bodies, made on first use (cdecl; arguments 3
// and 4 are read). The sorted key pair is a local whose address stays in world +0x1c0 (gap entry).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL PhysWorld_GetBodyPair(uint32_t a, uint32_t b, uint32_t entry_ebx,
                                                                  uint32_t entry_ebp, uint32_t entry_esi,
                                                                  uint32_t entry_edi, uint32_t entry_esp);
extern "C" void PhysWorld_GetBodyPair_entry(void);
// 0x000b63f0: sets up a 0x28c constraint: three vectors to +0x30/+0x40/+0x50, seven floats to
// +0x60..+0x78, +0x7c = 1, +0x88..+0x98 = 0, +0xa0 = 1, +0xa4 = 0.
void XB_THISCALL PhysConstraint28c_Setup(uint8_t* constraint, const float* v0, const float* v1, const float* v2,
                                         uint32_t f0, uint32_t f1, uint32_t f2, uint32_t f3, uint32_t f4,
                                         uint32_t f5, uint32_t f6);

// 0x000b6510: a constraint's second body (+4), its anchor (+0x10) and axis (+0x20); flag 1 of
// +0xa4. Unreferenced.
void XB_THISCALL PhysConstraint_AttachSecondBody(uint8_t* constraint, uint32_t body, const float* anchor,
                                                 const float* axis);
// 0x000b6590: the constraint's segment (centre +0x30, axis +0x40, half length +0x60) moved out of
// the frame: lo = centre - axis * len, hi = centre + axis * len (xyz). Unreferenced.
void XB_THISCALL PhysConstraint_GetSegmentEnds(const uint8_t* constraint, const float* frame, float* lo, float* hi);
// 0x000b6780: flag 2 of +0xa4 while the bodies' separation along the axis reaches the limit
// +0x74 (constraints with flag 1). The stack word is unused.
void XB_THISCALL PhysConstraint_UpdateLimitFlag(uint8_t* constraint, uint32_t unused);
// 0x000b7020: the clamped separation (+0x98), +0x9c, the contact impulse into the speed +0x8c and
// the position +0x94 += speed * dt.
void XB_THISCALL PhysConstraint_UpdateDrive(uint8_t* constraint, float dt);
// 0x000b71d0 / 0x000b7260: out = v + the body's +0x30 / +0x70 vector (v alone when flag 0x10 of
// +0x144 is set). Returns out.
float* XB_CDECL PhysBody_OffsetByPosition(float* out, const uint8_t* body, const float* v);
float* XB_CDECL PhysBody_OffsetByField70(float* out, const uint8_t* body, const float* v);
// 0x000b72f0 / 0x000b7330: a 0xa0-byte block of the world's frame buffer into +0xb0 / +0xb4.
uint32_t XB_THISCALL PhysContact_AllocBufferB0(uint8_t* contact, uint8_t* world);
uint32_t XB_THISCALL PhysContact_AllocBufferB4(uint8_t* contact, uint8_t* world);
// 0x000b61f0: next 0xd0 constraint (-1 at +0xac/+0xb4/+0xbc), bodies a and b at +0 and +4.
uint32_t* XB_CDECL PhysConstraintPoolD0_Alloc(uint8_t* pool, uint32_t unused, uint32_t a, uint32_t b);
// 0x000b4b90: the world's 0xd0 constraint pool (+0x2a0) alloc.
uint32_t* XB_CDECL PhysWorld_AllocConstraintD0(uint32_t a, uint32_t b);
// 0x000b3d00: flag 1 of +0x144 on each body of the list at +0x28 (count +0x2c). Unreferenced.
uint32_t XB_THISCALL PhysRagdoll_SetBodiesFlag1(uint8_t* ragdoll);

// 0x000ad140: box (centre +0x10, half extents +0x20) against a sphere of the radius swept from a
// along dir to b: the first face hit (b = the hit pushed out by radius + 0.002, normal +-axis at
// b + 4), else the first touch of a clamped edge/corner point (b = a + t (a - b), normal -dir).
// AL = hit.
uint8_t XB_THISCALL PhysBox_SweepSphere(uint8_t* box, const float* a, float* b, float radius, const float* dir);
// 0x000ae970: box against a sphere: out = the contact point, out + 4 the normal (-+axis when
// separated, the face/edge/corner normal on contact, the nearest face's when the centre is inside).
// AL = contact.
uint8_t XB_THISCALL PhysBox_SphereContact(uint8_t* box, const float* p, float radius, float* out, uint32_t unused);
// 0x000b1b40: GJK closest points of shape A (in `frame`, rows +0..+0x20, origin +0x30) and shape B:
// out = point on A, out + 4 on B, out + 8 = separation (also the start direction). 0 = separated
// beyond `limit` (squared), 1 = closest points found, 2 = touching/degenerate. Unreferenced chain.
int32_t XB_CDECL Gjk_ClosestPoints(void* shape_a, const float* frame, void* shape_b, float* out, float limit,
                                   uint8_t keep_going);
// 0x000b0a00: do two shapes (bounding sphere centre +0x10, radius +0x1c) in their frames overlap
// within limit: sphere test, then GJK in B's frame. Unreferenced.
uint8_t XB_CDECL Phys_ShapesOverlapGjk(void* shape_a, const float* frame_a, void* shape_b, const float* frame_b,
                                       float limit);


// Push 5: joints, contact records, constraint setters.
uint8_t* XB_THISCALL PhysWorld_AllocContactC0(uint8_t* world);
float* XB_CDECL PhysBody_RotateVector2(float* out, const uint8_t* body, const float* v);
void XB_THISCALL PhysJoint_SetAnchors(uint8_t* joint, const float* a, const float* b);
void XB_THISCALL PhysJoint_SetValue1A0(uint8_t* joint, float value);
void XB_THISCALL PhysAngleLimit_Set(uint8_t* limit, const float* axis, float angle);
void XB_THISCALL PhysVec_SetNormalized(float* out, const float* v);
void XB_THISCALL PhysJoint_SetAxes(uint8_t* joint, const float* a, const float* b);
void XB_THISCALL PhysJoint_AddAngleLimit(uint8_t* joint, const float* axis, float angle);
float XB_THISCALL PhysJoint_SnapBodyToAnchor(uint8_t* joint);
void XB_THISCALL PhysContact_ComputeBias(uint8_t* contact, float dt, float limit);
void XB_CDECL PhysBody_Translate(uint8_t* body, const float* delta);
void XB_THISCALL PhysContact140_SetBias(uint8_t* contact, float dt);
void XB_THISCALL PhysWorld_AddContact140(uint8_t* world, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                         uint32_t a5, float dt);
void XB_THISCALL PhysAttach_SetAnchors(uint8_t* attach, const float* a, const float* b);
void XB_THISCALL PhysConstraint_UpdateSumSquares48(uint8_t* c, uint32_t);
void XB_THISCALL PhysConstraint60_Init(uint8_t* c, const float* a, const float* b, float value30, float target);
void XB_THISCALL PhysConstraint60_ComputeTargetRate(uint8_t* c, const float* dt);
void XB_THISCALL PhysConstraint60_AdvanceTarget(uint8_t* c, float dt);
void XB_THISCALL PhysConstraint60_SnapToTarget(uint8_t* c, uint32_t);
void XB_THISCALL PhysConstraintFrame_Restore(uint8_t* c, uint32_t);
uint8_t* XB_THISCALL PhysConstraintFrame_Init(uint8_t* c, float value, const float* frame);
void XB_THISCALL PhysConstraintFrame_Update(uint8_t* c, const float* dt);
void XB_THISCALL PhysAttach_AddWorldContact(uint8_t* attach, uint8_t* world, float dt);

void XB_THISCALL PhysJoint_AddAxisContacts(uint8_t* joint, uint8_t* world, const float* p, const float* n, float dt);
void XB_THISCALL PhysJoint_SetLimitAxes(uint8_t* joint, const float* v, float angle_a, float angle_b);
void XB_THISCALL PhysJoint_SetupAxes10(uint8_t* joint, const float* a, const float* b, const float* c, const float* v,
                                       float angle_a, float angle_b);
void XB_THISCALL PhysJoint_SetupAxes20(uint8_t* joint, const float* a, const float* b, const float* c, const float* v,
                                       float angle_a, float angle_b);
void XB_THISCALL PhysJoint_UpdateLimitFlags(uint8_t* joint, uint32_t);
void XB_THISCALL PhysContacts_AddPair(uint8_t* world, uint32_t a0, const float* v1, uint32_t a2, const float* v3,
                                      uint32_t a4, uint32_t a5, uint32_t a6, float dt);

void XB_THISCALL PhysJoint_AddContacts(uint8_t* joint, uint8_t* world, float dt);

void XB_THISCALL PhysConstraint_AddContacts(uint8_t* constraint, uint8_t* world, float dt);

uint8_t XB_CDECL PhysBox_IntersectSegmentInFrame(const uint8_t* box, const float* frame, const float* a, const float* b);


// Push 6.
// 0x000b2a90: contact points of two convex shapes via GJK, pushing touching shapes apart. Dead.
uint8_t XB_CDECL Phys_ShapeContact(void* shape_a, const float* frame, void* shape_b, float* out, uint32_t a4, uint32_t a5,
                                   uint32_t a6, uint32_t a7);
// 0x000b9620 / 0x000b9750: hinge limit flags and contacts (island list +0x8).
void XB_THISCALL PhysHinge_UpdateLimitFlags(uint8_t* hinge, uint32_t unused);
void XB_THISCALL PhysHinge_AddContacts(uint8_t* hinge, uint8_t* world, float dt);
// 0x000b9a40: distance joint contacts (island list +0xc).
void XB_THISCALL PhysDistanceJoint_AddContacts(uint8_t* joint, uint8_t* world, float dt);
float* XB_CDECL PhysBody_WorldToRelative(float* out, const uint8_t* body, const float* p);
void XB_THISCALL PhysContact_ComputeBiasWithOffset(uint8_t* contact, float dt, float offset, float limit);
float XB_THISCALL PhysContact_GetSeparation(const uint8_t* contact);

// 0x000ba020: hinge setup (anchors, axes, limit axes, friction). Dead.
void XB_THISCALL PhysHinge_Setup(uint8_t* hinge, const float* anchor_a, const float* anchor_b, const float* axis_a,
                                 const float* axis_b, const float* reference, const float* v, float angle_lo,
                                 float angle_hi, float friction);
// 0x000ba2c0: angular motor contacts (island list +0x1c).
void XB_THISCALL PhysAngularMotor_AddContacts(uint8_t* motor, uint8_t* world, float dt);
// 0x000ba860: rows +0x10..+0x30 turned by the +0x90 rotation vector times dt (world list +0x2a4).
void XB_THISCALL PhysFrame_RotateByVelocity(uint8_t* object, float dt);
void XB_CDECL Phys_SphereMassProperties(float radius, float* inertia, float* mass);
void XB_CDECL PhysBodies_GetCenterOfMass(uint8_t* const* bodies, int32_t count, float* com, float* momentum,
                                         float* total);
void XB_CDECL Phys_PointsBoundingSphere(const float* points, int32_t count, float* radius, float* centre);
void XB_CDECL Phys_BoxMassProperties(const float* size, float* inertia, float* mass);
void XB_CDECL Phys_PointsBoxCenterSize(const float* points, int32_t count, float* size, float* centre);

void XB_CDECL PhysBodies_LaunchTowardTarget(uint8_t* const* bodies, int32_t count, const float* target,
                                            float* dist_sq);
float* XB_CDECL Phys_GetVelocityAtLocalPoint(uint32_t a0, const float* frame, const float* local, uint32_t a3,
                                             float* out, float* angular);
void XB_THISCALL PhysBody_SetMass(uint8_t* body, float mass);
void XB_THISCALL PhysBody_SetInertia(uint8_t* body, const float* inertia);
void XB_THISCALL PhysBody_AddForceAtPoint(uint8_t* body, const float* force, const float* point, float scale);
uint8_t* XB_THISCALL PhysObject1c0_Init(uint8_t* object, const float* frame);
uint8_t* XB_THISCALL PhysBody_Init(uint8_t* body, float mass, const float* inertia, const float* frame,
                                   const float* velocity, const float* angular, float value148, uint32_t value160);
// 0x000bc0e0: a fixed body (flag 0x10), identity frame.
uint32_t XB_THISCALL PhysBody_InitFixed(uint8_t* body);
void XB_CDECL PhysBody_PredictTransform(uint8_t* body, const float* dt);
// 0x000bc620: a 0x140 contact's separation vector.
float* XB_THISCALL PhysContact140_GetSeparationVector(const uint8_t* contact, float* out);

// 0x000bb4f0: linear and angular velocity that take frame `from` to `to` in dt.
void XB_CDECL Phys_AngularVelocityBetweenFrames(const float* from, const float* to, float dt, float* linear,
                                                float* angular);

}  // namespace game
