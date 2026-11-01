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

}  // namespace game
