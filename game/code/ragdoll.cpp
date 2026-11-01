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

uint32_t addr(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

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
// the capsule's body and the static world body. The title callees run at the original's ESP with
// its registers, and the three vec3 arguments sit at the original's frame addresses: their w words
// are never written (a title callee reads them), and the callees' stack leftovers must match.
PLATFORM_ASM_ENTRY(RagdollCapsule_CollideWorld_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %edx\n"
                   "pushl %edx\n"
                   "pushl %ebx\n"
                   "pushl 4(%edx)\n" PLATFORM_ASM_CALL(RagdollCapsule_CollideWorld) "addl $12, %esp\n"
                   "popl %esp\n"
                   "ret $4")

void XB_CDECL RagdollCapsule_CollideWorld(uint8_t* capsule, uint32_t entry_ebx, uint32_t entry_esp) {
    const uint32_t frame = ((entry_esp - 4) & ~15u) - 0xf0;  // ESP after the prologue's pushes
    float* const outside = reinterpret_cast<float*>(static_cast<uintptr_t>(frame + 0x70));
    float* const normal = reinterpret_cast<float*>(static_cast<uintptr_t>(frame + 0x80));
    float* const local = reinterpret_cast<float*>(static_cast<uintptr_t>(frame + 0x90));
    uint8_t* const body = at<uint8_t*>(capsule, 0x124);
    TitleRegs regs{};
    regs.eax = addr(body);
    regs.ebx = entry_ebx;
    regs.edi = addr(capsule);
    uint8_t* const world_body = reinterpret_cast<uint8_t*>(
        static_cast<uintptr_t>(title_call_at_regs(frame, 0x000b4030, &regs, 0, nullptr)));  // PhysWorld_GetStaticBody
    uint8_t* pair = nullptr;
    for (int32_t i = 0; i < at<int32_t>(capsule, 0x84); ++i) {
        uint8_t* const sphere = at<uint8_t*>(capsule, 0x80) + i * 0x40;
        float* const pos = reinterpret_cast<float*>(sphere);
        float* const pushed = reinterpret_cast<float*>(sphere + 0x10);
        regs.eax = addr(sphere);
        regs.ebx = addr(pushed);
        regs.ecx = at<uint32_t>(sphere, 0x30);
        regs.esi = addr(sphere);
        regs.edi = addr(capsule);
        const uint32_t radius_arg[1] = {at<uint32_t>(sphere, 0x30)};
        if (!static_cast<uint8_t>(title_call_at_regs(frame, 0x002299a0, &regs, 1, radius_arg)))  // CM_PushSphereOutOfListedObjects
            continue;
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
        if (!pair) {
            regs.eax = addr(world_body);
            regs.ecx = addr(body);
            const uint32_t args[2] = {addr(body), addr(world_body)};
            pair = reinterpret_cast<uint8_t*>(
                static_cast<uintptr_t>(title_call_at_regs(frame, 0x000b4bc0, &regs, 2, args)));  // PhysWorld_GetBodyPair_Wrapper
        }
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
        regs.eax = addr(body);
        regs.ecx = addr(pair);
        regs.edx = addr(local);
        regs.ebx = addr(pushed);
        regs.esi = addr(body + 0x70);
        regs.edi = frame + 0xf0;
        const uint32_t args[8] = {addr(body), addr(world_body), addr(local), addr(outside), addr(normal),
                                  at<uint32_t>(body, 0x148), 0x3f000000u, 0x45610000u};  // 0.5, 3600
        title_call_at_regs(frame, 0x000c2de0, &regs, 8, args);  // PhysBodyPair_AddContactPoint
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

// Physics ragdoll (0x1d50 bytes, PhysRagdoll pool): ten-slot lists of bodies (+0x28, count +0x2c),
// joints (+0x58/+0x5c), 0xd0 constraints (+0x88/+0x8c), attached objects (+0xb8/+0xbc) and their
// constraints (+0xe8/+0xec); the bone pool at +0xf0 with the used bones at +0x410 (count +0x464);
// rest state +0x470..+0x484; launch target +0x490 and launch frames left +0x4a0.

namespace {

// A list slot (ten at most): the next free entry, counted, or null when full.
uint32_t* ragdoll_list_append(uint8_t* phys, uint32_t list) {
    const int32_t count = at<int32_t>(phys, list + 4);
    if (count >= 10) return nullptr;
    uint32_t* const slot = at<uint32_t*>(phys, list) + count;
    at<int32_t>(phys, list + 4) = count + 1;
    return slot;
}

// The rest state the bodies start over with (also inlined into AddBodyForceAtPoint).
void ragdoll_reset_rest(uint8_t* phys) {
    const float limit = title_global<float>(0x0083eb9c);  // 1000
    at<uint8_t>(phys, 0x470) = 0;
    at<uint8_t>(phys, 0x478) = 0;
    at<float>(phys, 0x474) = 0.0f;
    at<float>(phys, 0x480) = limit;
    at<float>(phys, 0x484) = 0.0f;
}

// Clears every entry of a list that is a constraint on the body (its first or second body).
void ragdoll_unlink_body(uint8_t* phys, uint32_t list, uint32_t body) {
    uint32_t* it = at<uint32_t*>(phys, list);
    uint32_t* const end = it + at<int32_t>(phys, list + 4);
    for (; it != end; ++it) {
        const uint32_t constraint = *it;
        if (!constraint) continue;
        if (at<uint32_t>(reinterpret_cast<void*>(constraint), 0) == body ||
            at<uint32_t>(reinterpret_cast<void*>(constraint), 4) == body)
            *it = 0;
    }
}

using BoneCallback = void XB_CDECL(uint32_t pose, uint32_t body, uint8_t* bone);

// Each used bone (+0x40 pose, +0x44 body index) with a body: the callback.
void ragdoll_bones_call(uint8_t* phys, uint32_t callback) {
    uint8_t** it = reinterpret_cast<uint8_t**>(phys + 0x410);
    uint8_t** const end = it + at<int32_t>(phys, 0x464);
    for (; it != end; ++it) {
        uint8_t* const bone = *it;
        const uint32_t body = at<uint32_t*>(phys, 0x28)[at<int32_t>(bone, 0x44)];
        if (body) title_global<BoneCallback*>(callback)(at<uint32_t>(bone, 0x40), body, bone);
    }
}

}  // namespace

void XB_CDECL Phys_SetBoneCallbacks(const uint32_t* callbacks) {
    title_global<uint32_t>(0x0090a850) = callbacks[0];  // phys_bodyToPoseCallback
    title_global<uint32_t>(0x0090a854) = callbacks[1];  // phys_poseToBodyCallback
}

void XB_THISCALL PhysRagdoll_ResetRestState(uint8_t* phys) {
    ragdoll_reset_rest(phys);
    at<uint32_t>(phys, 0x4a0) = 0;
}

void XB_THISCALL PhysRagdoll_SetLaunchTarget(uint8_t* phys, const float* target) {
    alignas(16) float copy[4];
    std::memcpy(copy, target, 16);
    std::memcpy(phys + 0x490, copy, 16);
    at<uint32_t>(phys, 0x4a0) = 2;
}

uint32_t XB_THISCALL PhysRagdoll_AllocBody(uint8_t* phys, int32_t index) {
    const uint32_t body = title_function<uint32_t XB_CDECL()>(0x000b47b0)();  // PhysWorld_AllocBody
    at<uint32_t*>(phys, 0x28)[index] = body;
    return body;
}

// Joint between bodies a and b, stored at joint slot b.
uint32_t XB_THISCALL PhysRagdoll_CreateJoint(uint8_t* phys, int32_t a, int32_t b) {
    const uint32_t* const bodies = at<uint32_t*>(phys, 0x28);
    uint32_t* const slot = at<uint32_t*>(phys, 0x58) + b;
    using Alloc = uint32_t XB_CDECL(uint32_t, uint32_t);
    *slot = title_function<Alloc>(0x000b4b40)(bodies[a], bodies[b]);  // PhysWorld_AllocConstraint1B0
    return at<uint32_t*>(phys, 0x58)[b];
}

void XB_THISCALL PhysRagdoll_FreeConstraintD0(uint8_t* phys, int32_t index) {
    title_function<void XB_CDECL(uint32_t)>(0x000b49b0)(at<uint32_t*>(phys, 0x88)[index]);  // PhysWorld_FreeConstraintD0
    at<uint32_t*>(phys, 0x88)[index] = 0;
}

// While launch frames are left: one more push of the bodies toward the target; done once the
// remaining distance (the callee's result) is below 2.25.
void XB_THISCALL PhysRagdoll_UpdateLaunchToTarget(uint8_t* phys) {
    const int32_t frames = at<int32_t>(phys, 0x4a0);
    if (!frames) return;
    at<int32_t>(phys, 0x4a0) = frames - 1;
    float remaining;  // the `push ecx` slot: starts as the object pointer's bits
    const uint32_t bits = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(phys));
    std::memcpy(&remaining, &bits, 4);
    using Launch = void XB_CDECL(uint32_t, int32_t, uint8_t*, float*);
    title_function<Launch>(0x000bad60)(at<uint32_t>(phys, 0x28), at<int32_t>(phys, 0x2c), phys + 0x490,
                                       &remaining);  // PhysBodies_LaunchTowardTarget
    if (title_global<float>(0x007cd0a4) > remaining) at<uint32_t>(phys, 0x4a0) = 0;  // 2.25
}

void XB_THISCALL PhysRagdoll_GetCenterOfMass(uint8_t* phys, float* center, float* velocity, uint32_t arg3) {
    alignas(16) float a[4];
    alignas(16) float b[4];
    using Get = void XB_CDECL(uint32_t, int32_t, float*, float*, uint32_t);
    title_function<Get>(0x000bab40)(at<uint32_t>(phys, 0x28), at<int32_t>(phys, 0x2c), a, b, arg3);  // PhysBodies_GetCenterOfMass
    std::memcpy(center, a, 12);
    std::memcpy(velocity, b, 12);
}

// Wakes the ragdoll (rest state reset) and adds the force at the point to the body.
void XB_THISCALL PhysRagdoll_AddBodyForceAtPoint(uint8_t* phys, int32_t index, const float* point, const float* force,
                                                 uint32_t arg4) {
    const uint32_t body = at<uint32_t*>(phys, 0x28)[index];
    if (!body) return;
    ragdoll_reset_rest(phys);
    using Add = void XB_THISCALL(uint32_t, const float*, const float*, uint32_t);
    title_function<Add>(0x000bbd30)(body, force, point, arg4);  // PhysBody_AddForceAtPoint
}

// count empty slots at the end of the body, joint and 0xd0 constraint lists.
void XB_THISCALL PhysRagdoll_AddEmptySlots(uint8_t* phys, int32_t count) {
    if (count <= 0) return;
    for (int32_t i = count; i != 0; --i) {
        *ragdoll_list_append(phys, 0x28) = 0;
        *ragdoll_list_append(phys, 0x58) = 0;
        *ragdoll_list_append(phys, 0x88) = 0;
    }
}

// Snaps every joint's body to its anchor until the largest error is at most 1e-5, for fewer than
// phys_ragdollSolveIterations passes.
void XB_THISCALL PhysRagdoll_SolveJointPositions(uint8_t* phys) {
    using Snap = float XB_THISCALL(uint32_t);
    for (int32_t pass = 1; pass < title_global<int32_t>(0x008bd470); ++pass) {  // phys_ragdollSolveIterations
        uint32_t* it = at<uint32_t*>(phys, 0x58);
        uint32_t* const end = it + at<int32_t>(phys, 0x5c);
        float worst = 0.0f;
        if (end == it) return;
        for (; it != end; ++it) {
            if (!*it) continue;
            const float error = title_function<Snap>(0x000b7770)(*it);  // PhysJoint_SnapBodyToAnchor
            if (platform_x87_greater(error, worst)) worst = error;
        }
        if (!(worst > title_global<float>(0x0083effc))) return;  // 1e-5
    }
}

// Unlinks the body from every joint and constraint, frees its bones and destroys it.
void XB_THISCALL PhysRagdoll_RemoveBody(uint8_t* phys, int32_t index) {
    const uint32_t body = at<uint32_t*>(phys, 0x28)[index];
    if (!body) return;
    ragdoll_unlink_body(phys, 0x58, body);
    ragdoll_unlink_body(phys, 0x88, body);
    ragdoll_unlink_body(phys, 0xe8, body);
    int32_t i = 0;
    uint8_t** bone = reinterpret_cast<uint8_t**>(phys + 0x410);
    while (i < at<int32_t>(phys, 0x464)) {
        if (at<int32_t>(*bone, 0x44) == index) {
            // The pool moves the last bone into this slot.
            title_function<void XB_THISCALL(uint8_t*, uint8_t*)>(0x000b3ef0)(phys + 0xf0, *bone);  // PhysRagdoll_FreeBone
        } else {
            ++i;
            ++bone;
        }
    }
    title_function<void XB_CDECL(uint32_t)>(0x000b56e0)(at<uint32_t*>(phys, 0x28)[index]);  // PhysWorld_DestroyBody
    at<uint32_t*>(phys, 0x28)[index] = 0;
}

// Attaches the body to the owner's 0x1c0 object (found or made), through a 0x50 constraint.
void XB_THISCALL PhysRagdoll_AttachBodyToObject(uint8_t* phys, uint32_t owner, int32_t index, uint32_t arg3,
                                                uint32_t arg4) {
    if (!at<uint32_t*>(phys, 0x28)[index]) return;
    uint32_t object = title_function<uint32_t XB_CDECL(uint32_t)>(0x000b44a0)(owner);  // PhysWorld_FindObject1c0ByOwner
    if (!object) {
        object = title_function<uint32_t XB_CDECL()>(0x000b4b10)();  // PhysWorld_AllocObject1c0
        title_function<void XB_THISCALL(uint32_t, uint32_t)>(0x000bbde0)(object, owner);  // PhysObject1c0_Init
    }
    using Alloc = uint32_t XB_CDECL(uint32_t, uint32_t);
    const uint32_t constraint = title_function<Alloc>(0x000b47e0)(at<uint32_t*>(phys, 0x28)[index], object);  // PhysWorld_AllocConstraint50
    title_function<void XB_THISCALL(uint32_t, uint32_t, uint32_t)>(0x000b92a0)(constraint, arg4, arg3);
    title_function<void XB_THISCALL(uint8_t*, uint32_t)>(0x000b3f90)(phys, object);  // PhysRagdoll_AddAttachedObject
    *ragdoll_list_append(phys, 0xe8) = constraint;
}

void XB_THISCALL PhysRagdoll_DestroyObjects(uint8_t* phys) {
    uint32_t* it = at<uint32_t*>(phys, 0xb8);
    uint32_t* const end = it + at<int32_t>(phys, 0xbc);
    for (; it != end; ++it)
        if (*it) title_function<void XB_CDECL(uint32_t)>(0x000b5710)(*it);  // PhysWorld_DestroyObject1c0
    at<uint32_t>(phys, 0xbc) = 0;
    at<uint32_t>(phys, 0xec) = 0;
}

void XB_THISCALL PhysRagdoll_WriteBodiesToPose(uint8_t* phys) { ragdoll_bones_call(phys, 0x0090a850); }

void XB_THISCALL PhysRagdoll_PoseToBodies(uint8_t* phys) { ragdoll_bones_call(phys, 0x0090a854); }

// Rest detection over the used bodies: +0x480 = mean of body+0x168, +0x484 = share of bodies
// with body+0x15c > 0, +0x47c = bodies flagged 4 (+0x144), +0x488 = their share, +0x478 = all
// flagged. Below 80% flagged the rest timer (+0x474) restarts; otherwise it runs, and the ragdoll
// is at rest (+0x470) after 0.25 s or when every body is flagged.
void XB_THISCALL PhysRagdoll_UpdateRestState(uint8_t* phys, float dt) {
    uint32_t* const begin = at<uint32_t*>(phys, 0x28);
    uint32_t* const end = begin + at<int32_t>(phys, 0x2c);
    int32_t used = 0;
    for (uint32_t* it = begin; it != end; ++it)
        if (*it) ++used;
    const float one = title_global<float>(0x0083eb20);  // 1
    at<float>(phys, 0x480) = 0.0f;
    at<float>(phys, 0x484) = 0.0f;
    for (uint32_t* it = begin; it != end; ++it) {
        if (!*it) continue;
        const void* body = reinterpret_cast<const void*>(*it);
        at<float>(phys, 0x480) = at<float>(body, 0x168) + at<float>(phys, 0x480);
        const float moving = at<int32_t>(body, 0x15c) > 0 ? one : 0.0f;
        at<float>(phys, 0x484) = at<float>(phys, 0x484) + moving;
    }
    const float count = static_cast<float>(used);
    const float scale = one / count;
    at<float>(phys, 0x480) = at<float>(phys, 0x480) * scale;
    at<float>(phys, 0x484) = scale * at<float>(phys, 0x484);
    at<int32_t>(phys, 0x47c) = 0;
    for (uint32_t* it = begin; it != end; ++it)
        if (*it && (at<uint8_t>(reinterpret_cast<const void*>(*it), 0x144) & 4)) ++at<int32_t>(phys, 0x47c);
    const int32_t flagged = at<int32_t>(phys, 0x47c);
    const float flagged_f = static_cast<float>(flagged);
    at<float>(phys, 0x488) = flagged_f / count;
    at<uint8_t>(phys, 0x478) = flagged == used ? 1 : 0;
    if (count * title_global<float>(0x0083ebc0) > flagged_f) {  // 0.8
        at<float>(phys, 0x474) = 0.0f;
        at<uint8_t>(phys, 0x470) = 0;
        return;
    }
    if (at<uint8_t>(phys, 0x470)) return;
    const float timer = at<float>(phys, 0x474) + dt;
    at<float>(phys, 0x474) = timer;
    if (timer >= title_global<float>(0x007cd0a0) || at<uint8_t>(phys, 0x478)) at<uint8_t>(phys, 0x470) = 1;  // 0.25
}

// body+0x134 of every used body.
void XB_THISCALL PhysRagdoll_SetBodiesField134(uint8_t* phys, float value) {
    uint32_t* it = at<uint32_t*>(phys, 0x28);
    uint32_t* const end = it + at<int32_t>(phys, 0x2c);
    for (; it != end; ++it)
        if (*it) at<float>(reinterpret_cast<void*>(*it), 0x134) = value;
}

namespace {

// body+offset (aligned vec4) += delta: old + delta.
void body_accumulate(uint32_t body, uint32_t offset, const Lanes& delta) {
    float* const sum = reinterpret_cast<float*>(body + offset);
    alignas(16) float old[4];
    std::memcpy(old, sum, 16);
    std::memcpy(sum, add(lanes(old), delta).v, 16);
}

// Wakes the ragdoll and adds -(v * (1 - f[axis])) per axis of the body's velocity (+vel) to +acc;
// lane 3 comes from the x product.
void ragdoll_damp_body(uint8_t* phys, int32_t index, const float* factors, uint32_t vel, uint32_t acc) {
    const uint32_t body = at<uint32_t*>(phys, 0x28)[index];
    if (!body) return;
    ragdoll_reset_rest(phys);
    const float one = title_global<float>(0x0083eb20);  // 1
    const Lanes v = lanes(reinterpret_cast<const float*>(body + vel));
    const Lanes dz = neg(mul(v, one - factors[2]));
    const float ky = one - factors[1];
    const float kx = one - factors[0];
    const Lanes dx = neg(mul(v, kx));
    const Lanes dy = neg(mul(v, ky));
    const Lanes delta = {{dx.v[0], dy.v[1], dz.v[2], dx.v[3]}};
    body_accumulate(body, acc, delta);
}

}  // namespace

void XB_THISCALL PhysRagdoll_AddBodyForce(uint8_t* phys, int32_t index, const float* force) {
    const uint32_t body = at<uint32_t*>(phys, 0x28)[index];
    if (!body) return;
    ragdoll_reset_rest(phys);
    body_accumulate(body, 0x110, lanes(force));
}

void XB_THISCALL PhysRagdoll_DampBodyLinear(uint8_t* phys, int32_t index, const float* factors) {
    ragdoll_damp_body(phys, index, factors, 0xd0, 0x110);
}

void XB_THISCALL PhysRagdoll_DampBodyAngular(uint8_t* phys, int32_t index, const float* factors) {
    ragdoll_damp_body(phys, index, factors, 0xe0, 0x120);
}

void XB_THISCALL PhysRagdoll_DampLinear(uint8_t* phys, const float* factors) {
    for (int32_t i = 0; i < at<int32_t>(phys, 0x2c); ++i) PhysRagdoll_DampBodyLinear(phys, i, factors);
}

void XB_THISCALL PhysRagdoll_DampAngular(uint8_t* phys, const float* factors) {
    for (int32_t i = 0; i < at<int32_t>(phys, 0x2c); ++i) PhysRagdoll_DampBodyAngular(phys, i, factors);
}

// For each body whose velocity (+0xd0) falls short of v along v ((v - vel).v >= 0): the
// shortfall (v - vel).v / |v|^2 times v added to +0x110. The added w is v.w^2 (a stack lane the
// title copies along).
void XB_THISCALL PhysRagdoll_AddVelocityForce(uint8_t* phys, const float* velocity) {
    const Lanes v = lanes(velocity);
    const Lanes sq = mul(v, v);
    const float length_squared = (sq.v[0] + sq.v[1]) + sq.v[2];
    if (!(length_squared > title_global<float>(0x0083ed50))) return;  // 1e-4
    uint32_t* it = at<uint32_t*>(phys, 0x28);
    uint32_t* const end = it + at<int32_t>(phys, 0x2c);
    for (; it != end; ++it) {
        const uint32_t body = *it;
        if (!body) continue;
        const Lanes d = mul(sub(v, lanes(reinterpret_cast<const float*>(body + 0xd0))), v);
        const float along = (d.v[0] + d.v[1]) + d.v[2];
        if (!(along >= 0.0f)) continue;
        const Lanes scaled = mul(splat(along / length_squared), v);
        platform_keep(scaled);
        const Lanes delta = {{scaled.v[0], scaled.v[1], scaled.v[2], sq.v[3]}};
        body_accumulate(body, 0x110, delta);
    }
}

// The point relative to the owner's frame (rotation +0, origin +0x30) and to the body's frame,
// handed on as vec3s: their w words are never written (zero dead stack in tests).
void XB_THISCALL PhysRagdoll_AttachBodyAtPoint(uint8_t* phys, uint8_t* owner, int32_t index, const float* point) {
    const uint32_t body = at<uint32_t*>(phys, 0x28)[index];
    if (!body) return;
    const Lanes p = lanes(point);
    float rows[12];
    std::memcpy(rows, owner, sizeof(rows));
    const Lanes in_owner = rotate_into(rows, sub(p, lanes(reinterpret_cast<const float*>(owner + 0x30))));
    std::memcpy(rows, reinterpret_cast<const void*>(body), sizeof(rows));
    const Lanes in_body = rotate_into(rows, sub(p, lanes(reinterpret_cast<const float*>(body + 0x30))));
    alignas(16) float owner_point[4] = {in_owner.v[0], in_owner.v[1], in_owner.v[2], 0.0f};
    alignas(16) float body_point[4] = {in_body.v[0], in_body.v[1], in_body.v[2], 0.0f};
    platform_keep(in_owner);
    platform_keep(in_body);
    PhysRagdoll_AttachBodyToObject(phys, addr(owner), index, addr(owner_point), addr(body_point));
}

// The bone pool's free: the last used bone moves into the freed slot.
void XB_THISCALL PhysRagdoll_FreeBone(uint8_t* pool, uint8_t* bone) {
    const int32_t index = pool_index(pool, kBonePool, bone);
    const int32_t slot = at<int32_t>(pool, kBonePool.positions + static_cast<uint32_t>(index) * 4);
    if (at<int32_t>(pool, kBonePool.count) == 1) {
        RagdollBonePool_Reset(0, 0, pool);
        return;
    }
    pool_swap_out(pool, kBonePool, index, slot);
}

// A full list (ten) writes the object through null (the title faults there).
void XB_THISCALL PhysRagdoll_AddAttachedObject(uint8_t* phys, uint32_t object) {
    const uint32_t* const list = at<uint32_t*>(phys, 0xb8);
    const int32_t count = at<int32_t>(phys, 0xbc);
    for (int32_t i = 0; i < count; ++i)
        if (list[i] == object) return;
    *ragdoll_list_append(phys, 0xb8) = object;
}

// Five 0x30-byte blocks from +0 each get an empty self-linked list (+0x28 head, +0x2c count), the
// bone pool at +0xf0 its storage pointer (+0x460 = +0xf0, +0x464 = 0) and reset; +0x4a4 = 0.
PLATFORM_ASM_ENTRY(PhysRagdoll_InitLinks_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(PhysRagdoll_InitLinks) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL PhysRagdoll_InitLinks(uint8_t* ragdoll) {
    at<uint8_t*>(ragdoll, 0x28) = ragdoll;
    at<uint32_t>(ragdoll, 0x2c) = 0;
    at<uint32_t>(ragdoll, 0x5c) = 0;
    at<uint8_t*>(ragdoll, 0x58) = ragdoll + 0x30;
    at<uint32_t>(ragdoll, 0x8c) = 0;
    at<uint8_t*>(ragdoll, 0x88) = ragdoll + 0x60;
    at<uint32_t>(ragdoll, 0xbc) = 0;
    at<uint8_t*>(ragdoll, 0xb8) = ragdoll + 0x90;
    at<uint8_t*>(ragdoll, 0xe8) = ragdoll + 0xc0;
    at<uint32_t>(ragdoll, 0xec) = 0;
    uint8_t* const bones = ragdoll + 0xf0;
    at<uint8_t*>(bones, 0x370) = bones;
    at<uint32_t>(bones, 0x374) = 0;
    RagdollBonePool_Reset(0, 0, bones);
    at<uint32_t>(ragdoll, 0x4a4) = 0;
    return ragdoll;
}

}  // namespace game
