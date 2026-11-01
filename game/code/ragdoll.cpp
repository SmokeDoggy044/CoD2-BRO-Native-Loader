#include "ragdoll.h"

#include <cstring>

#include "cm.h"
#include "lanes.h"
#include "phys_pool.h"
#include "title_data.h"

namespace game {

namespace {

template <typename T>
T& at(const void* base, uint32_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + offset);
}

Lanes splat(float s) { return {{s, s, s, s}}; }

Lanes div(const Lanes& a, const Lanes& b) {
    return {{a.v[0] / b.v[0], a.v[1] / b.v[1], a.v[2] / b.v[2], a.v[3] / b.v[3]}};
}

// origin (+0x30) + row0 x + row1 y + row2 z, in that order.
Lanes transform_out(const float* m, const float* v) {
    const float x = v[0], y = v[1], z = v[2];
    return add(add(add(lanes(m + 12), mul(lanes(m), x)), mul(lanes(m + 4), y)), mul(lanes(m + 8), z));
}

// Transposed rotation (SHUFPS columns, lane 3 repeats lane 2) times v: x + y, then + z.
Lanes rotate_into(const float* m, const Lanes& v) {
    Lanes c[3];
    for (int32_t i = 0; i < 3; ++i) c[i] = {{m[i], m[4 + i], m[8 + i], m[8 + i]}};
    return add(add(mul(c[0], v.v[0]), mul(c[1], v.v[1])), mul(c[2], v.v[2]));
}

// Title helpers taking their object in ESI / EDI.
void call_with_esi(uint32_t address, void* esi) {
    __asm__ volatile("call *%0" : : "r"(address), "S"(esi) : "eax", "ecx", "edx", "memory", "cc");
}
void call_with_edi(uint32_t address, void* edi) {
    __asm__ volatile("call *%0" : : "r"(address), "D"(edi) : "eax", "ecx", "edx", "memory", "cc");
}

// A ragdoll's physics: bodies to pose (thiscall), 0x0022c0f0 on the entity, the physics back to
// the pool at 0x008f0470, and the entity's ragdoll flag (+0x294) cleared.
void ragdoll_release_physics(uint8_t* ragdoll) {
    uint8_t* const entity = at<uint8_t*>(ragdoll, 0);
    title_function<void XB_THISCALL(uint8_t*)>(0x000b3690)(at<uint8_t*>(ragdoll, 0x568));  // PhysRagdoll_WriteBodiesToPose
    title_function<void XB_STDCALL(uint8_t*)>(0x0022c0f0)(entity);
    RagdollPhysPool_Free(at<uint8_t*>(ragdoll, 0x568), 0, &title_global<uint8_t>(0x008f0470));
    at<uint32_t>(ragdoll, 0x568) = 0;
    at<uint8_t>(at<uint8_t*>(ragdoll, 0), 0x294) = 0;
}

}  // namespace

// A ragdoll sphere (0x40 bytes): world position +0, previous position +0x10, local position +0x20,
// radius +0x30, flag byte +0x34.
uint8_t* XB_REGS3 XB_CDECL RagdollSphere_TransformToWorld(uint8_t* sphere, uint32_t, const float* m) {
    alignas(16) float local[4];
    std::memcpy(local, sphere + 0x20, 16);
    float rows[16];
    std::memcpy(rows, m, sizeof(rows));
    const Lanes world = transform_out(rows, local);
    std::memcpy(sphere, world.v, 16);
    std::memcpy(sphere + 0x10, sphere, 16);
    sphere[0x34] = 0;
    return sphere;  // EAX is never written
}

PLATFORM_ASM_ENTRY(PhysRagdoll_InitState_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(PhysRagdoll_InitState) "addl $8, %esp\n"
                   "ret $4")

// Resets the ragdoll's pools: 0x00029b70 (the pools at +0..+0x4a4), two self-linked list heads
// (+0xa50, +0x1000), the capsule pool at +0x1020 and the capsule pair pool at +0x1c60.
uint8_t* XB_CDECL PhysRagdoll_InitState(uint8_t* ragdoll, uint32_t value) {
    call_with_esi(0x00029b70, ragdoll);
    uint8_t* head = ragdoll + 0x4b0;
    at<uint8_t*>(head, 0x5a0) = head;
    at<uint32_t>(head, 0x5a4) = 0;
    head += 0x5b0;
    at<uint8_t*>(head, 0x5a0) = head;
    at<uint32_t>(head, 0x5a4) = 0;
    uint8_t* const capsules = ragdoll + 0x1020;
    at<uint8_t*>(capsules, 0xc30) = capsules;
    at<uint32_t>(capsules, 0xc34) = 0;
    RagdollCapsulePool_Reset(capsules);
    uint8_t* const pairs = ragdoll + 0x1c60;
    at<uint8_t*>(pairs, 0x70) = pairs;
    at<uint32_t>(pairs, 0x74) = 0;
    RagdollCapsulePairPool_Reset(pairs);
    at<uint32_t>(ragdoll, 0x1d30) = value;
    return ragdoll;
}

void XB_REGS1 XB_STDCALL Ragdoll_AddForce(const float* force, void* ragdoll, uint32_t body) {
    alignas(16) float copy[4];
    std::memcpy(copy, force, 16);
    using AddBodyForce = void XB_THISCALL(void*, uint32_t, const float*);
    title_function<AddBodyForce>(0x000b38d0)(ragdoll, body, copy);  // PhysRagdoll_AddBodyForce
}

void XB_REGS3 XB_STDCALL Ragdoll_AddForceAtPoint(const float* force, uint32_t, const float* point, void* ragdoll,
                                                 uint32_t body, uint32_t arg3) {
    // The force is copied as a vec3: its w is a stack word the original never sets (zero in tests).
    alignas(16) float f[4] = {force[0], force[1], force[2], 0.0f};
    alignas(16) float p[4];
    std::memcpy(p, point, 16);
    using AddBodyForceAtPoint = void XB_THISCALL(void*, uint32_t, const float*, const float*, uint32_t);
    title_function<AddBodyForceAtPoint>(0x000b3300)(ragdoll, body, p, f, arg3);  // PhysRagdoll_AddBodyForceAtPoint
}

// A ragdoll capsule (0x130 bytes): spheres at +0x80 (count +0x84), segment +0xb0, body +0x124
// (its transform at +0x40).
void XB_STDCALL RagdollCapsule_UpdateWorldPoints(uint8_t* capsule) {
    using TransformSegment = void XB_REGS1 XB_STDCALL(const float*, uint8_t*);
    title_function<TransformSegment>(0x002279c0)(  // RagdollCapsule_TransformSegment
        reinterpret_cast<const float*>(at<uint8_t*>(capsule, 0x124) + 0x40), capsule + 0xb0);
    uint8_t* sphere = at<uint8_t*>(capsule, 0x80);
    uint8_t* const end = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(sphere) + (at<uint32_t>(capsule, 0x84) << 6));
    for (; sphere != end; sphere += 0x40)
        RagdollSphere_TransformToWorld(sphere, 0, reinterpret_cast<const float*>(at<uint8_t*>(capsule, 0x124) + 0x40));
}

// Pushes each sphere out of the world; a sphere that moved more than 1e-4 adds a contact between
// the capsule's body and the static world body.
void XB_STDCALL RagdollCapsule_CollideWorld(uint8_t* capsule) {
    uint8_t* const body = at<uint8_t*>(capsule, 0x124);
    uint8_t* const world_body = title_function<uint8_t* XB_CDECL()>(0x000b4030)();  // PhysWorld_GetStaticBody
    uint8_t* pair = nullptr;
    // The w lanes of the three vec3 arguments are stack words the original never sets (zero in tests).
    alignas(16) float normal[4] = {};
    alignas(16) float outside[4] = {};
    alignas(16) float local[4] = {};
    for (int32_t i = 0; i < at<int32_t>(capsule, 0x84); ++i) {
        uint8_t* const sphere = at<uint8_t*>(capsule, 0x80) + i * 0x40;
        float* const pos = reinterpret_cast<float*>(sphere);
        float* const pushed = reinterpret_cast<float*>(sphere + 0x10);
        if (!CM_PushSphereOutOfListedObjects(pos, pushed, at<float>(sphere, 0x30))) continue;
        const Lanes d = sub(lanes(pos), lanes(pushed));
        const Lanes squared = mul(d, d);
        platform_keep(squared);
        const float length_squared = (squared.v[0] + squared.v[1]) + squared.v[2];
        float length = length_squared;
        if (length_squared == length_squared && length_squared != 0.0f) {  // UCOMISS + JNE: NaN counts as equal
            const float r = platform_rsqrt_estimate(length_squared);
            const float scaled = length_squared * r;
            length = ((title_global<float>(0x0082e660) - scaled * r) * scaled) * title_global<float>(0x0082e670);  // 3, 0.5
        }
        if (!(length > title_global<float>(0x0083ed50))) continue;  // 1e-4
        const Lanes n = div(d, splat(length));
        if (!pair)
            pair = title_function<uint8_t* XB_CDECL(uint8_t*, uint8_t*)>(0x000b4bc0)(body, world_body);  // PhysWorld_GetBodyPair_Wrapper
        std::memcpy(normal, n.v, 12);
        const float radius = at<float>(sphere, 0x30);
        const Lanes out = add(lanes(pushed), mul(splat(radius + title_global<float>(0x0083f0c8)), n));  // 0.34
        std::memcpy(outside, out.v, 12);
        const Lanes offset = mul(splat(at<float>(sphere, 0x30)), n);
        float rows[12];
        std::memcpy(rows, body + 0x40, sizeof(rows));
        const Lanes rotated = rotate_into(rows, offset);
        const Lanes point = add(lanes(reinterpret_cast<const float*>(sphere + 0x20)), rotated);
        std::memcpy(local, point.v, 12);
        using AddContactPoint = void XB_THISCALL(uint8_t*, uint8_t*, uint8_t*, const float*, const float*, const float*,
                                                 uint32_t, float, float);
        title_function<AddContactPoint>(0x000c2de0)(pair, body, world_body, local, outside, normal,  // PhysBodyPair_AddContactPoint
                                                    at<uint32_t>(body, 0x148), 0.5f, 3600.0f);
    }
}

void XB_REGS3 XB_CDECL RagdollCapsule_GetBoundingSphere(uint32_t, uint32_t, const uint8_t* capsule, float* center,
                                                        float* radius) {
    const float* const m = reinterpret_cast<const float*>(at<uint8_t*>(capsule, 0x124) + 0x40);
    alignas(16) float end[4];
    std::memcpy(end, capsule + 0x90, 16);
    const Lanes world = transform_out(m, end);
    const Lanes other = lanes(reinterpret_cast<const float*>(capsule + 0x100));
    const float half = title_global<float>(0x0083eb24);  // 0.5
    const Lanes mid = mul(splat(half), add(world, other));
    std::memcpy(center, mid.v, 16);
    const Lanes d = sub(world, lanes(reinterpret_cast<const float*>(capsule + 0x100)));
    Lanes length = mul(d, d);
    length.v[0] = (length.v[0] + length.v[1]) + length.v[2];
    const float length_squared = length.v[0];
    if (length_squared == length_squared && length_squared != 0.0f) {  // UCOMISS + JNE: NaN counts as equal
        const float r = platform_rsqrt_estimate(length_squared);
        const float scaled = length_squared * r;
        const float three = title_global<float>(0x0082e660);
        length = {{((three - scaled * r) * scaled) * title_global<float>(0x0082e670), three, three, three}};  // 3, 0.5
    }
    const Lanes half_length = mul(splat(half), length);  // MULPS: every lane's flags
    platform_keep(half_length);
    *radius = at<float>(capsule, 0xa0) + half_length.v[0];
}

// Appends a copy of a sphere (0x40 bytes) to the capsule's two sphere slots; a third goes to
// address 0, as in the original.
uint8_t* XB_REGS2 XB_STDCALL RagdollCapsule_AddSphere(uint32_t, uint8_t* capsule, const uint8_t* sphere) {
    int32_t count = at<int32_t>(capsule, 0x84);
    uint8_t* slot = nullptr;
    if (count < 2) {
        slot = at<uint8_t*>(capsule, 0x80) + (static_cast<uint32_t>(count) << 6);
        at<int32_t>(capsule, 0x84) = count + 1;
    }
    platform_rep_movs(slot, sphere, 0x40);
    return slot;
}

PLATFORM_ASM_ENTRY(RagdollCapsulePool_Reset_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(RagdollCapsulePool_Reset) "addl $4, %esp\n"
                   "ret")

// A pool of ten 0x130-byte capsules: free list +0xbe0 (pointers), +0xc08 (indices), storage
// pointer +0xc30, used count +0xc34.
void XB_CDECL RagdollCapsulePool_Reset(uint8_t* pool) { pool_reset(pool, kCapsulePool); }

// A pool of seven 8-byte capsule pairs: pointers +0x38, indices +0x54, storage +0x70, count +0x74.
uint8_t* XB_REGS1 XB_CDECL RagdollCapsulePairPool_Reset(uint8_t* pool) {
    for (int32_t i = 0; i < 7; ++i) {
        at<int32_t>(pool, 0x54 + i * 4) = i;
        at<uint32_t>(pool, 0x38 + i * 4) = at<uint32_t>(pool, 0x70) + i * 8;
    }
    at<uint32_t>(pool, 0x74) = 0;
    return pool;  // EAX is never written
}

void XB_REGS3 XB_CDECL RagdollCapsulePool_Free(uint8_t* capsule, uint32_t, uint8_t* pool) {
    const int32_t index = pool_index(pool, kCapsulePool, capsule);
    const int32_t slot = at<int32_t>(pool, kCapsulePool.positions + static_cast<uint32_t>(index) * 4);
    if (at<int32_t>(pool, kCapsulePool.count) == 1) {
        RagdollCapsulePool_Reset(pool);
        return;
    }
    pool_swap_out(pool, kCapsulePool, index, slot);
}

// The ragdoll pool (0x570-byte ragdolls); a ragdoll with physics (+0x568) releases it first.
void XB_REGS3 XB_CDECL RagdollPool_Free(uint8_t* ragdoll, uint32_t, uint8_t* pool) {
    const int32_t index = pool_index(pool, kRagdollPool, ragdoll);
    const int32_t slot = at<int32_t>(pool, kRagdollPool.positions + static_cast<uint32_t>(index) * 4);
    if (at<int32_t>(pool, kRagdollPool.count) == 1) {
        RagdollPool_Clear(pool);
        return;
    }
    if (at<uint32_t>(ragdoll, 0x568)) ragdoll_release_physics(ragdoll);
    pool_swap_out(pool, kRagdollPool, index, slot);
}

// The ragdoll physics pool (0x1d50 bytes each); a freed one destroys its bodies (0x00225a00, EDI).
void XB_REGS3 XB_CDECL RagdollPhysPool_Free(uint8_t* phys, uint32_t, uint8_t* pool) {
    const int32_t index = pool_index(pool, kRagdollPhysPool, phys);
    const int32_t slot = at<int32_t>(pool, kRagdollPhysPool.positions + static_cast<uint32_t>(index) * 4);
    if (at<int32_t>(pool, kRagdollPhysPool.count) == 1) {
        RagdollPhysPool_Clear(pool);
        return;
    }
    call_with_edi(0x00225a00, phys);  // PhysRagdoll_DestroyBodies
    pool_swap_out(pool, kRagdollPhysPool, index, slot);
}

void XB_REGS3 XB_CDECL RagdollBonePool_Reset(uint32_t, uint32_t, uint8_t* pool) { pool_reset(pool, kBonePool); }

void XB_STDCALL RagdollPool_ReleaseAllPhysics(uint8_t* pool) {
    for (int32_t i = 0; i < at<int32_t>(pool, kRagdollPool.count); ++i) {
        uint8_t* const ragdoll = at<uint8_t*>(pool, kRagdollPool.pointers + i * 4);
        if (at<uint32_t>(ragdoll, 0x568)) ragdoll_release_physics(ragdoll);
    }
}

PLATFORM_ASM_ENTRY(RagdollPool_Clear_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(RagdollPool_Clear) "addl $4, %esp\n"
                   "ret")

void XB_CDECL RagdollPool_Clear(uint8_t* pool) {
    RagdollPool_ReleaseAllPhysics(pool);
    pool_reset(pool, kRagdollPool);
}

// Destroys every used physics ragdoll's objects (0x000b3640) and its ten bodies (+0x28 list).
void XB_STDCALL RagdollPhysPool_DestroyAll(uint8_t* pool) {
    for (int32_t i = 0; i < at<int32_t>(pool, kRagdollPhysPool.count); ++i) {
        uint8_t* const phys = at<uint8_t*>(pool, kRagdollPhysPool.pointers + i * 4);
        title_function<void XB_THISCALL(uint8_t*)>(0x000b3640)(phys);  // PhysRagdoll_DestroyObjects
        for (uint32_t offset = 0; offset < 0x28; offset += 4) {
            const uint32_t body = at<uint32_t>(at<uint8_t*>(phys, 0x28), offset);
            if (body) title_function<void XB_CDECL(uint32_t)>(0x000b56e0)(body);  // PhysWorld_DestroyBody
        }
    }
}

PLATFORM_ASM_ENTRY(RagdollPhysPool_Clear_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(RagdollPhysPool_Clear) "addl $4, %esp\n"
                   "ret")

void XB_CDECL RagdollPhysPool_Clear(uint8_t* pool) {
    RagdollPhysPool_DestroyAll(pool);
    pool_reset(pool, kRagdollPhysPool);
}

}  // namespace game
