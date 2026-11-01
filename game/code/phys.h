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

}  // namespace game
