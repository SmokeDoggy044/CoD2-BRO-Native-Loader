#include "ragdoll.h"

#include <cstring>

#include "cm.h"
#include "dobj.h"
#include "lanes.h"
#include "phys.h"
#include "phys_pool.h"
#include "qcommon.h"
#include "qcommon_core.h"
#include "g_utils.h"
#include "g_vehicle.h"
#include "g_script.h"
#include "g_scr_runtime.h"
#include "sv.h"
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

// ECX ragdoll, EDX id; callers keep using EDX (the original never writes it).
PLATFORM_ASM_ENTRY(PhysRagdoll_FindCapsuleById_entry,
                   "pushl %edx\n"
                   "pushl %edx\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(PhysRagdoll_FindCapsuleById) "addl $8, %esp\n"
                   "popl %edx\n"
                   "ret")

// The capsule with id +0x128 == id among the ragdoll's capsules (+0xbf0, count +0xc44), or null.
uint8_t* XB_CDECL PhysRagdoll_FindCapsuleById(uint8_t* ragdoll, int32_t id) {
    const uint32_t base = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ragdoll)) + 0xbf0;
    const uint32_t end = base + static_cast<uint32_t>(at<int32_t>(ragdoll, 0xc44)) * 4;
    for (uint32_t it = base; it != end; it += 4) {
        uint8_t* const capsule = *reinterpret_cast<uint8_t**>(uintptr_t{it});
        if (at<int32_t>(capsule, 0x128) == id) return capsule;
    }
    return nullptr;
}

// EAX vector -> +0x100; +0x120 = the smallest +0x30 of +0xa0 and the 0x40-byte records at +0x80 (count +0x84).
uint32_t XB_REGS3 XB_CDECL PhysRagdoll_SetPivotAndMinBound(const float* v, uint32_t, uint8_t* phys) {
    for (uint32_t i = 0; i < 4; ++i) at<uint32_t>(phys, 0x100 + i * 4) = reinterpret_cast<const uint32_t*>(v)[i];
    at<uint32_t>(phys, 0x120) = at<uint32_t>(phys, 0xa0);
    const uint32_t first = at<uint32_t>(phys, 0x80);
    const uint32_t end = first + (at<uint32_t>(phys, 0x84) << 6);
    for (uint32_t r = first; r != end; r += 0x40)
        if (at<float>(phys, 0x120) > at<float>(reinterpret_cast<void*>(uintptr_t{r}), 0x30))
            at<uint32_t>(phys, 0x120) = at<uint32_t>(reinterpret_cast<void*>(uintptr_t{r}), 0x30);
    return end;  // EAX left at the end pointer
}

// ECX a, EAX b, XMM0 radius, stack out: out = {a, b, radius}; EAX keeps b's last word.
PLATFORM_ASM_ENTRY(Phys_SetSegment_entry,
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %eax\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(Phys_SetSegment) "addl $16, %esp\n"
                   "ret $4")

uint32_t XB_CDECL Phys_SetSegment(const uint32_t* a, const uint32_t* b, uint8_t* out, uint32_t radius) {
    for (uint32_t i = 0; i < 4; ++i) at<uint32_t>(out, i * 4) = a[i];
    for (uint32_t i = 0; i < 4; ++i) at<uint32_t>(out, 0x10 + i * 4) = b[i];
    at<uint32_t>(out, 0x20) = radius;
    return b[3];
}

// EAX def, XMM0 -> +0x30, 18 stack words (stdcall); callers keep XMM1/XMM3/XMM5 across it.
PLATFORM_ASM_ENTRY(RagdollBodyDef_Set_entry,
                   "leal 4(%esp), %edx\n"
                   "subl $52, %esp\n"
                   "movups %xmm1, 4(%esp)\n"
                   "movups %xmm3, 20(%esp)\n"
                   "movups %xmm5, 36(%esp)\n"
                   "movss %xmm0, (%esp)\n"
                   "pushl %edx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(RagdollBodyDef_Set) "addl $8, %esp\n"
                   "movups 4(%esp), %xmm1\n"
                   "movups 20(%esp), %xmm3\n"
                   "movups 36(%esp), %xmm5\n"
                   "addl $52, %esp\n"
                   "ret $0x48")

// A body definition: ids +0/+4, +8, +0x30/+0x34, the vector at +0x10, +0x38, +0xb4, +0xd0/+0xd4,
// +0xdc/+0xe0/+0xec, +0xf0/+0xf4 and four vectors at +0x120..+0x150. Returns def + 0x150.
uint8_t* XB_CDECL RagdollBodyDef_Set(uint8_t* def, const uint32_t* args, uint32_t xmm0) {
    auto vec = [&](uint32_t offset, uint32_t arg) {
        for (uint32_t i = 0; i < 4; ++i)
            at<uint32_t>(def, offset + i * 4) = reinterpret_cast<const uint32_t*>(uintptr_t{args[arg]})[i];
    };
    at<uint32_t>(def, 0x30) = xmm0;
    at<uint32_t>(def, 0x34) = args[2];
    at<uint32_t>(def, 8) = args[3];
    at<uint32_t>(def, 0) = args[0];
    at<uint32_t>(def, 4) = args[1];
    vec(0x10, 4);
    at<uint32_t>(def, 0x38) = args[5];
    at<uint32_t>(def, 0xb4) = args[6];
    at<uint32_t>(def, 0xdc) = args[9];
    at<uint32_t>(def, 0xd0) = args[7];
    at<uint32_t>(def, 0xe0) = args[10];
    at<uint32_t>(def, 0xd4) = args[8];
    at<uint32_t>(def, 0xec) = args[11];
    at<uint32_t>(def, 0xf0) = args[12];
    at<uint32_t>(def, 0xf4) = args[13];
    vec(0x120, 14);
    vec(0x130, 15);
    vec(0x140, 16);
    vec(0x150, 17);
    return def + 0x150;
}

PLATFORM_ASM_ENTRY(RagdollDef_RemoveBody_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(RagdollDef_RemoveBody) "addl $4, %esp\n"
                   "ret")

// The body definitions (0x008dfdd0, 0x60 each, count 0x008dfdd4): the first with id +0x54 == id gets -1
// and every definition whose parent (+0x58) was that id takes over the removed one's parent.
int32_t XB_CDECL RagdollDef_RemoveBody(int32_t id) {
    int32_t found = -1;
    for (int32_t i = 0; i < title_global<int32_t>(0x008dfdd4) && found == -1; ++i)
        if (at<int32_t>(title_global<uint8_t*>(0x008dfdd0), static_cast<uint32_t>(i) * 0x60 + 0x54) == id) found = i;
    if (found == -1) return -1;
    const uint32_t removed = static_cast<uint32_t>(found) * 0x60;
    at<int32_t>(title_global<uint8_t*>(0x008dfdd0), removed + 0x54) = -1;
    for (int32_t i = 0; i < title_global<int32_t>(0x008dfdd4); ++i) {
        uint8_t* const defs = title_global<uint8_t*>(0x008dfdd0);
        if (at<int32_t>(defs, static_cast<uint32_t>(i) * 0x60 + 0x58) == id)
            at<uint32_t>(defs, static_cast<uint32_t>(i) * 0x60 + 0x58) = at<uint32_t>(defs, removed + 0x58);
    }
    return static_cast<int32_t>(removed);
}

PLATFORM_ASM_ENTRY(PhysRagdoll_DestroyBodies_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(PhysRagdoll_DestroyBodies) "addl $4, %esp\n"
                   "ret")

// The ragdoll's objects, then its ten bodies (+0x28 array).
uint32_t XB_CDECL PhysRagdoll_DestroyBodies(uint8_t* phys) {
    PhysRagdoll_DestroyObjects(phys);
    uint32_t body = 0;
    for (uint32_t i = 0; i < 10; ++i) {
        body = at<uint32_t*>(phys, 0x28)[i];
        if (body) PhysWorld_DestroyBody(body);
    }
    return body;
}

namespace {

// The model's bone matrices (+0x22c: +0x20 matrices from +0x30, +0x2f count) to/from a 0x40-per-bone buffer.
void copy_bone_matrices(uint8_t* entity, uint8_t* buffer, bool restore) {
    const int32_t count = at<uint8_t*>(entity, 0x22c)[0x2f];
    for (int32_t i = 0; i < count; ++i) {
        uint32_t* const bone = reinterpret_cast<uint32_t*>(at<uint8_t*>(at<uint8_t*>(entity, 0x22c), 0x20) + i * 0x40 + 0x30);
        uint32_t* const saved = reinterpret_cast<uint32_t*>(buffer + i * 0x40);
        for (uint32_t w = 0; w < 16; ++w) {
            if (restore)
                bone[w] = saved[w];
            else
                saved[w] = bone[w];
        }
    }
}

}  // namespace

uint32_t XB_CDECL PhysRagdoll_RestoreBoneMatrices(uint8_t* entity, uint8_t* buffer) {
    copy_bone_matrices(entity, buffer, true);
    const int32_t count = at<uint8_t*>(entity, 0x22c)[0x2f];
    return count > 0 ? static_cast<uint32_t>(count) * 0x40 : addr(at<uint8_t*>(entity, 0x22c));
}

uint32_t XB_CDECL PhysRagdoll_SaveBoneMatrices(uint8_t* entity, uint8_t* buffer) {
    copy_bone_matrices(entity, buffer, false);
    const int32_t count = at<uint8_t*>(entity, 0x22c)[0x2f];
    return count > 0 ? static_cast<uint32_t>(count) * 0x40 : addr(at<uint8_t*>(entity, 0x22c));
}

// ECX body record, EAX ragdoll; the result in XMM0. No callers or stored pointers (dead).
PLATFORM_ASM_ENTRY(Ragdoll_GetBodyScale_entry,
                   "pushl %eax\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(Ragdoll_GetBodyScale) "addl $8, %esp\n"
                   "subl $4, %esp\n"
                   "fstps (%esp)\n"
                   "movss (%esp), %xmm0\n"
                   "addl $4, %esp\n"
                   "ret")

// (1 - clamp(|record +0x30| / ragdoll +0x500, 0, 1)) * 2 * (ragdoll +0x420)->+0x38 * 0x008db410.
float XB_CDECL Ragdoll_GetBodyScale(const uint8_t* record, const uint8_t* ragdoll) {
    const uint32_t bits = at<uint32_t>(record, 0x30) & 0x7fffffffu;
    float a;
    std::memcpy(&a, &bits, 4);
    const long double q = static_cast<long double>(a) / at<float>(ragdoll, 0x500);
    const float v = static_cast<float>(q);
    const float one = title_global<float>(0x0083eb20);
    float x;
    if (title_global<float>(0x0083eafc) > q)
        x = 0.0f;
    else
        x = v > one ? one : v;
    return (((one - x) * title_global<float>(0x0083eba8)) * at<float>(at<uint8_t*>(ragdoll, 0x420), 0x38)) *
           title_global<float>(0x008db410);
}

PLATFORM_ASM_ENTRY(Ragdoll_UpdateSettle_entry,
                   "leal 4(%esp), %eax\n"
                   "pushl %eax\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(Ragdoll_UpdateSettle) "addl $8, %esp\n"
                   "ret $4")

// Joints (+0x58, ten) stiffened (20, flag 0x800 of +0x30), the rest state updated; while settling
// (+0x470) the time +0x1d38 grows by dt (+0x1d3c = reached 1) and s = clamp(1 - (1 - t), 0, 1) gives the
// bodies' +0x134 = (1 - s) * 0x00989240 and the joints 20 + 50 s; else the time restarts. The original keeps
// s in its dt argument slot.
uint32_t XB_CDECL Ragdoll_UpdateSettle(uint8_t* phys, float* arg) {
    for (uint32_t i = 0; i < 10; ++i) {
        uint8_t* const joint = at<uint8_t**>(phys, 0x58)[i];
        if (!joint) continue;
        PhysJoint_SetValue1A0(joint, 20.0f);
        at<uint32_t>(joint, 0x30) |= 0x800;
    }
    PhysRagdoll_UpdateRestState(phys, *arg);
    phys[0x1d3c] = 0;
    if (!phys[0x470]) {
        at<float>(phys, 0x1d38) = 0.0f;
        return 0;
    }
    const float t = at<float>(phys, 0x1d38) + *arg;
    const float one = title_global<float>(0x0083eb20);
    at<float>(phys, 0x1d38) = t;
    if (t >= one) phys[0x1d3c] = 1;
    const float s = one - (one - t);
    if (0.0f > s)
        *arg = 0.0f;
    else
        *arg = s > one ? one : s;
    const float weight = (one - *arg) * title_global<float>(0x00989240);
    uint32_t body = 0;
    for (uint32_t i = 0; i < 10; ++i) {
        body = at<uint32_t*>(phys, 0x28)[i];
        uint8_t* const joint = at<uint8_t**>(phys, 0x58)[i];
        if (body) at<float>(reinterpret_cast<void*>(uintptr_t{body}), 0x134) = weight;
        if (joint) PhysJoint_SetValue1A0(joint, *arg * title_global<float>(0x0083ec30) + title_global<float>(0x0083eb5c));
    }
    return body;
}

namespace {

void copy_vec4(float* dst, const float* src) { std::memcpy(dst, src, 16); }
void store_vec4(float* dst, const Lanes& v) { std::memcpy(dst, v.v, 16); }

}  // namespace

// EDX id, stack capsule set (ragdoll +0x1010): the capsule with that id freed and every capsule pair
// (pool at +0xc50: +0x38 active list, +0x54 slot per pair, +0x74 count) that uses it released by swapping
// it with the last active one. Only Ragdoll_RemoveBody calls it (dead).
PLATFORM_ASM_ENTRY(PhysRagdoll_RemoveCapsule_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(PhysRagdoll_RemoveCapsule) "addl $8, %esp\n"
                   "ret $4")

void XB_CDECL PhysRagdoll_RemoveCapsule(int32_t id, uint8_t* set) {
    uint8_t* const capsule = PhysRagdoll_FindCapsuleById(set, id);
    if (!capsule) return;
    RagdollCapsulePool_Free(capsule, 0, set + 0x10);
    uint8_t* const pool = set + 0xc50;
    for (int32_t i = 0; i < at<int32_t>(set, 0xcc4);) {
        const uint32_t pair = at<uint32_t>(set, 0xc88 + static_cast<uint32_t>(i) * 4);
        const uint8_t* const p = reinterpret_cast<const uint8_t*>(uintptr_t{pair});
        if (at<uint8_t*>(p, 0) != capsule && at<uint8_t*>(p, 4) != capsule) {
            ++i;
            continue;
        }
        const uint32_t base = at<uint32_t>(set, 0xcc0);
        const int32_t index = static_cast<int32_t>(pair - base) >> 3;
        const int32_t count = at<int32_t>(pool, 0x74);
        const int32_t slot = at<int32_t>(pool, 0x54 + static_cast<uint32_t>(index) * 4);
        if (count == 1) {
            RagdollCapsulePairPool_Reset(pool);
            continue;
        }
        at<int32_t>(pool, 0x74) = count - 1;
        const uint32_t last = at<uint32_t>(pool, 0x38 + static_cast<uint32_t>(count - 1) * 4);
        const int32_t last_index = static_cast<int32_t>(last - base) >> 3;
        const uint32_t moved = at<uint32_t>(pool, 0x38 + static_cast<uint32_t>(slot) * 4);
        at<uint32_t>(pool, 0x38 + static_cast<uint32_t>(slot) * 4) = last;
        at<uint32_t>(pool, 0x38 + static_cast<uint32_t>(at<int32_t>(pool, 0x74)) * 4) = moved;
        at<int32_t>(pool, 0x54 + static_cast<uint32_t>(index) * 4) = at<int32_t>(pool, 0x74);
        at<int32_t>(pool, 0x54 + static_cast<uint32_t>(last_index) * 4) = slot;
    }
}

// EAX id, ESI ragdoll: the body's definition, its capsule and its physics body removed. No callers or
// stored pointers (dead).
PLATFORM_ASM_ENTRY(Ragdoll_RemoveBody_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Ragdoll_RemoveBody) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Ragdoll_RemoveBody(int32_t id, uint8_t* ragdoll) {
    RagdollDef_RemoveBody(id);
    PhysRagdoll_RemoveCapsule(id, ragdoll + 0x1010);
    PhysRagdoll_RemoveBody(ragdoll, id);
}

// EAX entity: the Bip01 bone indices of its model (+0x22c) into 0x008db3d8..0x008db40c.
void XB_REGS1 XB_CDECL Ragdoll_CacheBip01Bones(const uint8_t* entity) {
    static const uint32_t kNames[14] = {0x00832a30, 0x0083c714, 0x0083c700, 0x0083c6f0, 0x0083c67c, 0x0083c6dc, 0x0083c6cc,
                                        0x0083c66c, 0x0083c6bc, 0x0083c6ac, 0x0083c65c, 0x0083c69c, 0x0083c68c, 0x0083c64c};
    const DObj* const obj = at<const DObj*>(entity, 0x22c);
    for (uint32_t i = 0; i < 14; ++i)
        title_global<int32_t>(0x008db3d8 + i * 4) = DObj_FindBoneIndex2(obj, reinterpret_cast<const char*>(uintptr_t{kNames[i]}));
}

PLATFORM_ASM_ENTRY(PhysRagdoll_TakeBody_entry,
                   "pushl %edi\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(PhysRagdoll_TakeBody) "addl $8, %esp\n"
                   "ret")

// EDX ragdoll, EDI id: the next free capsule (+0xbf0, used count +0xc44, at most 10) with its link
// (+0x80 self, +0x84 = 0), +0x124 = 0 and id +0x128. Out of capsules: Sys_DebugBreak.
uint8_t* XB_CDECL PhysRagdoll_TakeBody(uint8_t* ragdoll, int32_t id) {
    const int32_t used = at<int32_t>(ragdoll, 0xc44);
    uint8_t* capsule = nullptr;
    if (used < 10) {
        capsule = at<uint8_t*>(ragdoll, 0xbf0 + static_cast<uint32_t>(used) * 4);
        at<int32_t>(ragdoll, 0xc44) = used + 1;
        if (capsule) {
            at<uint8_t*>(capsule, 0x80) = capsule;
            at<uint32_t>(capsule, 0x84) = 0;
            at<uint32_t>(capsule, 0x124) = 0;
        }
    }
    if (!capsule) {
        title_function<void XB_CDECL()>(0x00042460)();  // Sys_DebugBreak
        return nullptr;  // the original then writes through the null capsule
    }
    at<int32_t>(capsule, 0x128) = id;
    return capsule;
}

PLATFORM_ASM_ENTRY(Ragdoll_GetBoneVelocity_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Ragdoll_GetBoneVelocity) "addl $20, %esp\n"
                   "ret $12")

// EAX local point, ECX bone: the linear and angular velocity at that point of the bone's frames
// (0x00988fb0 / 0x00988d30, 0x40 each) over the ragdoll's +0x564; both outputs take the point's w.
void XB_CDECL Ragdoll_GetBoneVelocity(const float* point, int32_t bone, const uint8_t* ragdoll, float* linear, float* angular) {
    alignas(16) float p[4];
    copy_vec4(p, point);
    alignas(16) float out_a[4];
    alignas(16) float out_b[4];
    const uint32_t offset = static_cast<uint32_t>(bone) << 6;
    Phys_GetVelocityAtLocalPoint(0x00988fb0 + offset, reinterpret_cast<const float*>(uintptr_t{0x00988d30 + offset}), p,
                                 at<uint32_t>(ragdoll, 0x564), out_a, out_b);
    out_a[3] = p[3];
    copy_vec4(linear, out_a);
    out_b[3] = p[3];
    copy_vec4(angular, out_b);
}

PLATFORM_ASM_ENTRY(RagdollFrames_Construct_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(RagdollFrames_Construct) "addl $4, %esp\n"
                   "ret")

// EBX object: two blocks of ten frames (+0x10 and +0x290, 0x40 apart) each get three 16-byte vectors
// constructed (eh_vector_constructor_iterator, constructor 0x0001b920); +0 and +0x568 cleared.
uint8_t* XB_CDECL RagdollFrames_Construct(uint8_t* object) {
    using VectorConstruct = void XB_STDCALL(void*, uint32_t, uint32_t, uint32_t);
    for (uint32_t i = 0; i < 10; ++i) title_function<VectorConstruct>(0x00011240)(object + 0x10 + i * 0x40, 0x10, 3, 0x0001b920);
    for (uint32_t i = 0; i < 10; ++i) title_function<VectorConstruct>(0x00011240)(object + 0x290 + i * 0x40, 0x10, 3, 0x0001b920);
    at<uint32_t>(object, 0) = 0;
    at<uint32_t>(object, 0x568) = 0;
    return object;
}

// EAX frame, stack capsule: its two local points (+0, +0x10) into the frame (+0x30, +0x40).
uint8_t* XB_REGS1 XB_STDCALL RagdollCapsule_TransformSegment(const float* m, uint8_t* capsule) {
    store_vec4(reinterpret_cast<float*>(capsule + 0x30), transform_out(m, reinterpret_cast<const float*>(capsule)));
    store_vec4(reinterpret_cast<float*>(capsule + 0x40), transform_out(m, reinterpret_cast<const float*>(capsule + 0x10)));
    return capsule + 0x40;
}

void XB_REGS1 XB_STDCALL PhysRagdoll_GetBoundingSphere(const uint8_t* ragdoll, float* center, float* radius) {
    RagdollCapsule_GetBoundingSphere(0, 0, at<uint8_t*>(ragdoll, 0xbf0), center, radius);
    const int32_t count = at<int32_t>(ragdoll, 0xc44);
    for (int32_t i = 1; i < count; ++i) {
        alignas(16) float other[4];
        float other_radius;
        RagdollCapsule_GetBoundingSphere(0, 0, at<uint8_t*>(ragdoll, 0xbf0 + static_cast<uint32_t>(i) * 4), other, &other_radius);
        Sphere_Merge(other, radius, center, other_radius);
    }
}

PLATFORM_ASM_ENTRY(PhysRagdoll_ComputeBounds_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(PhysRagdoll_ComputeBounds) "addl $4, %esp\n"
                   "ret")

// The first sphere (centre +0x20, radius +0x30), then each further one merged in. EAX = the count
// when there is one sphere at most, else 0.
uint32_t XB_CDECL PhysRagdoll_ComputeBounds(uint8_t* capsule) {
    const uint8_t* const first = at<uint8_t*>(capsule, 0x80);
    std::memcpy(capsule + 0x90, first + 0x20, 16);
    at<uint32_t>(capsule, 0xa0) = at<uint32_t>(first, 0x30);
    const int32_t count = at<int32_t>(capsule, 0x84);
    if (count <= 1) return static_cast<uint32_t>(count);
    for (int32_t i = 1; i < count; ++i) {
        const uint8_t* const sphere = at<uint8_t*>(capsule, 0x80) + i * 0x40;
        Sphere_Merge(reinterpret_cast<const float*>(sphere + 0x20), &at<float>(capsule, 0xa0),
                     reinterpret_cast<float*>(capsule + 0x90), at<float>(sphere, 0x30));
    }
    return 0;
}

// The callees run at the original's ESP with its registers, the points at the original's frame
// addresses: the normal's copy (+0x10) keeps a w word the original never writes.
PLATFORM_ASM_ENTRY(RagdollCapsule_CollidePair_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %edx\n"
                   "pushl %edx\n"
                   "pushl 4(%edx)\n" PLATFORM_ASM_CALL(RagdollCapsule_CollidePair) "addl $8, %esp\n"
                   "popl %esp\n"
                   "ret $4")

// Capsules a (+0) and b (+4): segments +0xe0/+0xf0, radius +0xd0, body +0x124 (transform +0x40).
// Phys_CapsuleCapsuleClosest gives the point on b (+0x20), the normal (+0x30) and the point on a
// (+0x40); both points go into their body's frame and the contact (0, 0, 100) joins the bodies' pair.
void XB_CDECL RagdollCapsule_CollidePair(const uint8_t* pair, uint32_t entry_esp) {
    const uint32_t frame = ((entry_esp - 4) & ~15u) - 0x80;  // ESP after the prologue's pushes
    uint8_t* const f = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(frame));
    const uint8_t* const a = at<uint8_t*>(pair, 0);
    const uint8_t* const b = at<uint8_t*>(pair, 4);
    TitleRegs regs{};
    regs.eax = addr(b) + 0xe0;
    regs.ebx = addr(pair);
    regs.ecx = addr(b) + 0xf0;
    regs.edx = addr(a) + 0xe0;
    regs.esi = frame + 0x40;
    regs.edi = addr(a);
    const uint32_t closest_args[7] = {addr(a) + 0xe0, addr(a) + 0xf0, at<uint32_t>(a, 0xd0), addr(b) + 0xe0,
                                      at<uint32_t>(b, 0xd0), frame + 0x20, frame + 0x30};
    if (!static_cast<uint8_t>(title_call_at_regs(frame, 0x00227290, &regs, 7, closest_args)))  // Phys_CapsuleCapsuleClosest
        return;
    uint8_t* const body_a = at<uint8_t*>(a, 0x124);
    uint8_t* const body_b = at<uint8_t*>(b, 0x124);
    regs.eax = addr(body_b);
    regs.ecx = addr(body_a);
    regs.edx = 0;
    const uint32_t pair_args[2] = {addr(body_a), addr(body_b)};
    const uint32_t body_pair = title_call_at_regs(frame, 0x000b4bc0, &regs, 2, pair_args);  // PhysWorld_GetBodyPair_Wrapper
    std::memcpy(f + 0x10, f + 0x30, 12);
    at<uint32_t>(f, 0xc) = body_pair;
    std::memcpy(f + 0x50, at<uint8_t*>(b, 0x124) + 0x40, 0x30);
    const Lanes local_b = rotate_into(reinterpret_cast<const float*>(f + 0x50),
                                      sub(lanes(reinterpret_cast<const float*>(f + 0x20)),
                                          lanes(reinterpret_cast<const float*>(at<uint8_t*>(b, 0x124) + 0x70))));
    std::memcpy(f + 0x20, local_b.v, 12);
    std::memcpy(f + 0x50, at<uint8_t*>(a, 0x124) + 0x40, 0x30);
    const Lanes local_a = rotate_into(reinterpret_cast<const float*>(f + 0x50),
                                      sub(lanes(reinterpret_cast<const float*>(f + 0x40)),
                                          lanes(reinterpret_cast<const float*>(at<uint8_t*>(a, 0x124) + 0x70))));
    std::memcpy(f + 0x30, local_a.v, 12);
    regs.eax = at<uint32_t>(a, 0x124);
    regs.ebx = addr(a);
    regs.ecx = body_pair;
    regs.edx = at<uint32_t>(b, 0x124);
    regs.esi = at<uint32_t>(a, 0x124) + 0x70;
    regs.edi = frame + 0x80;
    const uint32_t contact_args[8] = {regs.eax, regs.edx, frame + 0x30, frame + 0x20, frame + 0x10, 0, 0, 0x42c80000u};
    title_call_at_regs(frame, 0x000c2de0, &regs, 8, contact_args);  // PhysBodyPair_AddContactPoint
}

PLATFORM_ASM_ENTRY(PhysObj_GetInheritedVelocity_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(PhysObj_GetInheritedVelocity) "addl $12, %esp\n"
                   "ret")

// Up the chain of riders (+0x3cc -> entity): an entity with a physics body (+0x2a4 flag 0x400000)
// hands over the body's velocities (+0xd0 linear, +0xe0 angular; 1 even without a body); a vehicle
// (+0x248) is first stepped along its path and gives +0x100 (w = x) and its turn rates (+0x110,
// degrees) as radians * 0.3.
uint32_t XB_CDECL PhysObj_GetInheritedVelocity(uint8_t* ent, float* angular, float* linear) {
    for (;;) {
        if (at<uint32_t>(ent, 0x2a4) & 0x400000) {
            const uint8_t* const body = Phys_FindBodyForEntity(0, ent);
            if (body) {
                std::memcpy(linear, body + 0xd0, 16);
                std::memcpy(angular, body + 0xe0, 16);
            }
            return 1;
        }
        if (at<uint32_t>(ent, 0x248)) {
            title_function<void XB_THISCALL(uint8_t*)>(0x0024bec0)(ent);                // Vehicle_LoadPhysicsState
            title_function<void XB_REGS1 XB_CDECL(uint32_t, uint8_t*)>(0x00252530)(0x32, ent);  // Vehicle_MoveAlongPath
            const uint8_t* const vehicle = at<uint8_t*>(ent, 0x248);
            const float x = at<float>(vehicle, 0x100);
            const float v[4] = {x, at<float>(vehicle, 0x104), at<float>(vehicle, 0x108), x};
            std::memcpy(linear, v, 16);
            const float pi = title_global<float>(0x0083ed00);
            const float k = title_global<float>(0x0083ecfc);  // 1/180
            const float z = (at<float>(vehicle, 0x118) * pi) * k;
            const float ax = (at<float>(vehicle, 0x110) * pi) * k;
            const float ay = (at<float>(vehicle, 0x114) * pi) * k;
            const Lanes w = mul(Lanes{{ax, ay, z, ax}}, title_global<float>(0x0083066c));  // 0.3
            std::memcpy(angular, w.v, 16);
            return 1;
        }
        uint8_t* const* const link = at<uint8_t**>(ent, 0x3cc);
        if (!link || !*link) break;
        ent = *link;
    }
    std::memcpy(linear, &title_global<uint8_t>(0x0082e110), 16);
    std::memcpy(angular, &title_global<uint8_t>(0x0082e110), 16);
    return 0;
}

namespace {

using WorldBoneMatrix = const uint8_t* XB_REGS3 XB_STDCALL(uint8_t*, uint32_t, int32_t, float*);

// G_DObjGetWorldBoneMatrix: EAX entity, ECX bone, stack buffer (EDX = the buffer as well).
const uint8_t* world_bone_matrix(uint8_t* entity, int32_t bone, float* buffer) {
    return title_function<WorldBoneMatrix>(0x002d6800)(entity, addr(buffer), bone, buffer);
}

}  // namespace

PLATFORM_ASM_ENTRY(Ragdoll_StoreModelBoneFrames_entry,
                   "pushl 4(%esp)\n"
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n" PLATFORM_ASM_CALL(Ragdoll_StoreModelBoneFrames) "addl $8, %esp\n"
                   "ret $4")

// The ragdoll (entity +0): time +0x564; per bone (indices +0x550, int16) the current frame
// (+0x10 + 0x40 i) goes to the previous one (+0x290 + 0x40 i), the new one is the model's (+0x22c)
// base matrix (+0x20, +0x30 + 0x40 bone) or identity without a model. Then +0x540 = +0x520,
// +0x530 = +0x510 and the entity's +0x140 / +0x150 into +0x520 / +0x510.
uint32_t XB_CDECL Ragdoll_StoreModelBoneFrames(float time, uint8_t* ragdoll) {
    at<float>(ragdoll, 0x564) = time;
    for (uint32_t i = 0; i < 10; ++i) {
        const int32_t bone = at<int16_t>(ragdoll, 0x550 + i * 2);
        uint8_t* const current = ragdoll + 0x10 + i * 0x40;
        std::memcpy(current + 0x280, current, 0x40);
        const uint8_t* const obj = at<uint8_t*>(at<uint8_t*>(ragdoll, 0), 0x22c);
        float m[16];
        alignas(16) float identity[16];
        const uint8_t* source;
        if (!obj) {
            Matrix_Identity(identity);
            source = reinterpret_cast<const uint8_t*>(identity);
        } else {
            source = at<uint8_t*>(obj, 0x20) + static_cast<uint32_t>(bone << 6) + 0x30;
        }
        std::memcpy(m, source, 0x40);
        std::memcpy(current, m, 0x40);
    }
    std::memcpy(ragdoll + 0x540, ragdoll + 0x520, 16);
    std::memcpy(ragdoll + 0x530, ragdoll + 0x510, 16);
    const uint8_t* const entity = at<uint8_t*>(ragdoll, 0);
    std::memcpy(ragdoll + 0x520, entity + 0x140, 16);
    std::memcpy(ragdoll + 0x510, entity + 0x150, 16);
    return at<uint32_t>(entity, 0x15c);
}

PLATFORM_ASM_ENTRY(Ragdoll_RefreshBoneMatrices_entry,
                   "pushl 4(%esp)\n"
                   "subl $4, %esp\n"
                   "movss %xmm0, (%esp)\n" PLATFORM_ASM_CALL(Ragdoll_RefreshBoneMatrices) "addl $8, %esp\n"
                   "ret $4")

// Time +0x564; each bone's world matrix into both its previous and current frame.
uint32_t XB_CDECL Ragdoll_RefreshBoneMatrices(float time, uint8_t* ragdoll) {
    at<float>(ragdoll, 0x564) = time;
    for (uint32_t i = 0; i < 10; ++i) {
        alignas(16) float buffer[16];
        const uint8_t* const m = world_bone_matrix(at<uint8_t*>(ragdoll, 0), at<int16_t>(ragdoll, 0x550 + i * 2), buffer);
        uint8_t* const current = ragdoll + 0x10 + i * 0x40;
        std::memcpy(current + 0x280, m, 0x40);
        std::memcpy(current, m, 0x40);
    }
    return 0;
}

// The bone names (0x008db388, name +0 and index +4 per 8 bytes) looked up in the model; a missing
// bone gets identity frames and the result 0, a found one its world matrix in both frames.
uint8_t XB_REGS1 XB_STDCALL Ragdoll_BindBones(uint8_t* entity, uint8_t* ragdoll) {
    at<uint8_t*>(ragdoll, 0) = entity;
    at<float>(ragdoll, 0x564) = title_global<float>(0x0083f0cc);  // 1/30
    uint8_t ok = 1;
    for (uint32_t i = 0; i < 10; ++i) {
        const uint32_t entry = 0x008db388 + i * 8;
        const DObj* const obj = at<const DObj*>(at<uint8_t*>(ragdoll, 0), 0x22c);
        const int16_t bone = static_cast<int16_t>(
            DObj_FindBoneIndex2(obj, reinterpret_cast<const char*>(uintptr_t{title_global<uint32_t>(entry)})));
        at<int16_t>(ragdoll, 0x550 + i * 2) = bone;
        float* const current = reinterpret_cast<float*>(ragdoll + 0x10 + i * 0x40);
        if (bone < 0) {
            ok = 0;
            Matrix_Identity(current + 0xa0);
            Matrix_Identity(current);
            continue;
        }
        title_global<int32_t>(entry + 4) = bone;
        alignas(16) float buffer[16];
        const uint8_t* const m = world_bone_matrix(at<uint8_t*>(ragdoll, 0), at<int16_t>(ragdoll, 0x550 + i * 2), buffer);
        std::memcpy(current + 0xa0, m, 0x40);
        std::memcpy(current, m, 0x40);
    }
    return ok;
}

namespace {

// Rotation only: row0 x + row1 y + row2 z.
Lanes rotate_out(const float* m, const float* v) {
    const float x = v[0], y = v[1], z = v[2];
    return add(add(mul(lanes(m), x), mul(lanes(m + 4), y)), mul(lanes(m + 8), z));
}

// The same with each sum spelt b + a (GCC -O0 then keeps MSVC's destination operand: NaN payloads).
Lanes rotate_out_ba(const float* m, const float* v) {
    const float x = v[0], y = v[1], z = v[2];
    return add(mul(lanes(m + 8), z), add(mul(lanes(m + 4), y), mul(lanes(m), x)));
}
Lanes transform_out_ba(const float* m, const float* v) {
    const float x = v[0], y = v[1], z = v[2];
    return add(mul(lanes(m + 8), z), add(mul(lanes(m + 4), y), add(mul(lanes(m), x), lanes(m + 12))));
}

}  // namespace

void XB_THISCALL RagdollBodyDef_FitToBones(uint8_t* def, uint8_t* entity) {
    alignas(16) float a[16];
    alignas(16) float b[16];
    world_bone_matrix(entity, at<int32_t>(def, 0), a);
    world_bone_matrix(entity, at<int32_t>(def, 4), b);
    std::memcpy(def + 0x40, a + 12, 16);
    const Lanes end = add(lanes(b + 12), rotate_out(a, reinterpret_cast<const float*>(def + 0x10)));
    std::memcpy(def + 0x50, end.v, 16);
    const float t = at<float>(def, 8);
    const Lanes far_end = lanes(reinterpret_cast<const float*>(def + 0x50));
    const Lanes scaled_end = mul(far_end, t);
    const Lanes blend = add(mul(lanes(reinterpret_cast<const float*>(def + 0x40)), title_global<float>(0x0083eb20) - at<float>(def, 8)),
                            scaled_end);
    float rows[12];
    std::memcpy(rows, a, sizeof(rows));
    const Lanes local = rotate_into(rows, sub(blend, lanes(a + 12)));
    std::memcpy(def + 0x60, local.v, 16);
    std::memcpy(def + 0x70, a + 12, 16);
    const Lanes v = add(lanes(b + 12), rotate_out(a, reinterpret_cast<const float*>(def + 0xa0)));
    std::memcpy(def + 0x80, v.v, 16);
    // Direction between the ends (from the values read before the +0x80 store), normalised.
    const Lanes d = sub(far_end, lanes(reinterpret_cast<const float*>(def + 0x40)));
    const Lanes squared = mul(d, d);
    platform_keep(squared);
    const float length_squared = (squared.v[0] + squared.v[1]) + squared.v[2];
    float length = length_squared;
    if (length_squared == length_squared && length_squared != 0.0f) {  // UCOMISS + JNE: NaN counts as equal
        const float r = platform_rsqrt_estimate(length_squared);
        const float scaled = length_squared * r;
        length = ((title_global<float>(0x0082e660) - scaled * r) * scaled) * title_global<float>(0x0082e670);  // 3, 0.5
    }
    const Lanes n = mul(d, title_global<float>(0x0083eb20) / length);
    const Lanes start = add(lanes(reinterpret_cast<const float*>(def + 0x40)), mul(n, at<float>(def, 0x30)));
    std::memcpy(def + 0x40, start.v, 16);
    const Lanes pulled = sub(lanes(reinterpret_cast<const float*>(def + 0x50)), mul(n, at<float>(def, 0x34)));
    std::memcpy(def + 0x50, pulled.v, 16);
    alignas(16) float inertia[4] = {};  // w: a stack word the callee never writes (zero in tests)
    float mass;
    Phys_SphereMassProperties(at<float>(def, 0xd0), inertia, &mass);
    const Lanes scaled_inertia = mul(lanes(inertia), at<float>(def, 0xb4) / mass);
    std::memcpy(def + 0xc0, scaled_inertia.v, 16);
    const int32_t bone = at<int32_t>(def, 0);
    at<int32_t>(def, 0xe4) = bone;
    const int32_t parent = DObj_GetBoneParent(at<const DObj*>(entity, 0x22c), bone);
    at<int32_t>(def, 0xe8) = parent;
    if (parent < 0) return;
    const uint8_t* const pose = DObj_GetBoneBasePose(at<const DObj*>(entity, 0x22c), bone);
    uint8_t rotation[16];
    std::memcpy(rotation, pose + 0x24, 16);
    std::memcpy(def + 0x100, rotation, 16);
    std::memset(def + 0x110, 0, 16);
}

float* XB_CDECL Ragdoll_PoseToBody(const uint32_t* bone, float* out, const float* frame) {
    alignas(16) float inverse[16];
    Mat44_InverseRigid_2(inverse, frame);
    alignas(16) float buffer[16];
    const float* const m = reinterpret_cast<const float*>(
        world_bone_matrix(reinterpret_cast<uint8_t*>(uintptr_t{bone[0]}), static_cast<int32_t>(bone[1]), buffer));
    Lanes rows[3];
    for (int k = 0; k < 3; ++k) rows[k] = rotate_out_ba(m, inverse + k * 4);
    std::memcpy(out, rows, 0x30);
    const Lanes origin = transform_out_ba(m, inverse + 12);  // the matrix re-read after the rows' stores
    std::memcpy(out + 12, origin.v, 16);
    return out + 12;
}

uint32_t XB_STDCALL PhysBone_UpdateWorldFrame(uint8_t* bone) {
    const float* const m = at<float*>(bone, 8);
    float* const out = at<float*>(bone, 0xc);
    const float* const frame = reinterpret_cast<const float*>(bone + 0x70);
    Lanes rows[3];
    for (int k = 0; k < 3; ++k) rows[k] = rotate_out(m, frame + k * 4);
    std::memcpy(out, rows, 0x30);
    const Lanes origin = transform_out(m, frame + 12);  // the matrix re-read after the rows' stores
    std::memcpy(out + 12, origin.v, 16);
    const Lanes point = transform_out(at<float*>(bone, 0xc), reinterpret_cast<const float*>(bone + 0x20));
    std::memcpy(bone + 0x30, point.v, 16);
    return at<uint32_t>(at<uint8_t*>(bone, 0xc), 0x3c);
}

PLATFORM_ASM_ENTRY(Ragdoll_FreePhysics_entry,
                   "pushl %eax\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Ragdoll_FreePhysics) "addl $8, %esp\n"
                   "ret")

// EAX: the entity after a release, else as it came.
uint32_t XB_CDECL Ragdoll_FreePhysics(uint8_t* ragdoll, uint32_t eax) {
    if (!at<uint32_t>(ragdoll, 0x568)) return eax;
    ragdoll_release_physics(ragdoll);
    return at<uint32_t>(ragdoll, 0);
}

void XB_STDCALL Ragdoll_Update(uint8_t* ragdoll, float dt) {
    uint8_t* const phys = at<uint8_t*>(ragdoll, 0x568);
    if (!phys) return;
    uint8_t* const entity = at<uint8_t*>(ragdoll, 0);
    float slot = dt;  // the callee keeps a value in its argument slot
    Ragdoll_UpdateSettle(phys, &slot);
    PhysRagdoll_WriteBodiesToPose(phys);
    const float t = at<float>(phys, 0x1d40) + dt;
    const float duration = at<float>(phys, 0x1d44);
    if (duration > t) {
        at<float>(phys, 0x1d40) = t;
        title_function<void XB_STDCALL(uint8_t*, uint8_t*, float)>(0x0022b500)(phys + 0x4b0, entity, t / duration);  // Ragdoll_BlendBonePoses
    } else {
        title_function<void XB_STDCALL(uint8_t*)>(0x0022c0f0)(entity);
    }
    if (!at<uint8_t*>(ragdoll, 0x568)[0x1d3c] &&
        !(title_global<float>(0x0083f10c) > at<float>(at<uint8_t*>(ragdoll, 0), 0x148)))  // -60000
        return;
    Scr_Notify(at<uint8_t*>(ragdoll, 0), title_global<uint32_t>(0x0097a754));  // "physicsdone"
    if (at<uint32_t>(ragdoll, 0x568)) ragdoll_release_physics(ragdoll);
}

PLATFORM_ASM_ENTRY(Ragdoll_UpdateFromEntity_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Ragdoll_UpdateFromEntity) "addl $8, %esp\n"
                   "ret $4")

// The entity's matrix is fetched and not used.
void XB_CDECL Ragdoll_UpdateFromEntity(uint8_t* ragdoll, float time) {
    alignas(16) float m[16];
    G_GetEntityMatrix(at<uint8_t*>(ragdoll, 0), m);
    Ragdoll_StoreModelBoneFrames(time, ragdoll);
    if (at<uint8_t*>(ragdoll, 0x568)) PhysRagdoll_UpdateLaunchToTarget(at<uint8_t*>(ragdoll, 0x568));
}

void XB_CDECL Ragdoll_UpdateAll(float dt) {
    uint8_t* const* const end = reinterpret_cast<uint8_t* const*>(uintptr_t{0x008f02e0 + title_global<uint32_t>(0x008f0464) * 4});
    for (uint8_t* const* item = reinterpret_cast<uint8_t* const*>(uintptr_t{0x008f02e0}); item != end; ++item)
        Ragdoll_Update(*item, dt);
}

void XB_CDECL Ragdoll_UpdateAllFromEntities(float time) {
    uint8_t* const* const end = reinterpret_cast<uint8_t* const*>(uintptr_t{0x008f02e0 + title_global<uint32_t>(0x008f0464) * 4});
    for (uint8_t* const* item = reinterpret_cast<uint8_t* const*>(uintptr_t{0x008f02e0}); item != end; ++item) {
        uint8_t* const ragdoll = *item;
        alignas(16) float m[16];
        G_GetEntityMatrix(at<uint8_t*>(ragdoll, 0), m);
        Ragdoll_StoreModelBoneFrames(time, ragdoll);
        if (at<uint8_t*>(ragdoll, 0x568)) PhysRagdoll_UpdateLaunchToTarget(at<uint8_t*>(ragdoll, 0x568));
    }
}

// The callees run at the original's ESP with its registers (title callees below read stale words).
PLATFORM_ASM_ENTRY(PhysRagdoll_Collide_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %edx\n"
                   "pushl %edx\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(PhysRagdoll_Collide) "addl $16, %esp\n"
                   "popl %esp\n"
                   "ret")

// The set's owner (+0) is the current one (0x0095f6f0) meanwhile. The world geometry within the
// bounding sphere is gathered, each capsule (+0xbf0, count +0xc44) moves its attached point, goes to
// world space and collides with the world, then each capsule pair (+0xc88, count +0xcc4); the
// attached pushes are undone.
uint32_t XB_CDECL PhysRagdoll_Collide(uint8_t* set, uint32_t esi, uint32_t edi, uint32_t entry_esp) {
    const uint32_t frame = ((entry_esp - 4) & ~15u) - 0x20;  // ESP after the prologue's pushes
    uint8_t* const f = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(frame));
    title_global<uint32_t>(0x0095f6f0) = at<uint32_t>(set, 0);
    TitleRegs regs{addr(set), addr(set), frame + 0xc, frame + 0x10, esi, edi};
    const uint32_t sphere_args[2] = {frame + 0x10, frame + 0xc};
    title_call_at_regs(frame, 0x00229930, &regs, 2, sphere_args);  // PhysRagdoll_GetBoundingSphere
    regs.eax = at<uint32_t>(f, 0xc);
    regs.ecx = frame + 0x10;
    const uint32_t gather_args[2] = {frame + 0x10, at<uint32_t>(f, 0xc)};
    title_call_at_regs(frame, 0x0022d7d0, &regs, 2, gather_args);  // Ragdoll_GatherCollisionGeometry
    const uint32_t end = addr(set) + 0xbf0 + at<uint32_t>(set, 0xc44) * 4;
    at<uint32_t>(f, 0xc) = end;
    regs.edx = addr(set);
    for (uint32_t cursor = addr(set) + 0xbf0; cursor != end; cursor += 4) {
        const uint32_t capsule = *reinterpret_cast<const uint32_t*>(uintptr_t{cursor});
        regs.eax = capsule;
        regs.esi = capsule;
        regs.edi = cursor;
        regs.eax = title_call_at_regs(frame, 0x0002a340, &regs, 0, nullptr);  // Phys_MoveAttachedPoint
        const uint32_t capsule_arg[1] = {capsule};
        regs.eax = title_call_at_regs(frame, 0x0002a5d0, &regs, 1, capsule_arg);  // RagdollCapsule_UpdateWorldPoints
        regs.eax = title_call_at_regs(frame, 0x0002a620, &regs, 1, capsule_arg);  // RagdollCapsule_CollideWorld
    }
    const uint32_t pairs_end = addr(set) + 0xc88 + at<uint32_t>(set, 0xcc4) * 4;
    regs.edi = pairs_end;
    for (uint32_t cursor = addr(set) + 0xc88; cursor != pairs_end; cursor += 4) {
        const uint32_t pair_arg[1] = {*reinterpret_cast<const uint32_t*>(uintptr_t{cursor})};
        regs.ecx = pair_arg[0];
        regs.esi = cursor;
        regs.eax = title_call_at_regs(frame, 0x00229d20, &regs, 1, pair_arg);  // RagdollCapsule_CollidePair
    }
    regs.eax = addr(set);
    const uint32_t result = title_call_at_regs(frame, 0x0002ab10, &regs, 0, nullptr);  // Phys_UndoAttachedPushes
    title_global<uint32_t>(0x0095f6f0) = 0;
    return result;
}

PLATFORM_ASM_ENTRY(Ragdoll_CollideAll_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %edx\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(Ragdoll_CollideAll) "addl $4, %esp\n"
                   "popl %esp\n"
                   "ret")

uint32_t XB_CDECL Ragdoll_CollideAll(uint32_t entry_esp) {
    const uint32_t top = entry_esp - 12;  // ESP after the pushes of ESI, EDI and EBX
    const uint32_t end = 0x008f02e0 + title_global<uint32_t>(0x008f0464) * 4;
    uint32_t eax = 0;
    for (uint32_t cursor = 0x008f02e0; cursor != end; cursor += 4) {
        eax = at<uint32_t>(*reinterpret_cast<uint8_t* const*>(uintptr_t{cursor}), 0x568);
        if (!eax) continue;
        const TitleRegs regs{eax, eax + 0x1010, 0, 0, cursor, end};
        eax = title_call_at_regs(top, 0x00231770, &regs, 0, nullptr);  // PhysRagdoll_Collide
    }
    return eax;
}

PLATFORM_ASM_ENTRY(Ragdoll_StartPhysics_entry,
                   "pushl %eax\n"
                   "pushl 8(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(Ragdoll_StartPhysics) "addl $12, %esp\n"
                   "ret $4")

uint32_t XB_CDECL Ragdoll_StartPhysics(uint32_t value, uint8_t* ragdoll, uint32_t entry_eax) {
    uint8_t* phys = at<uint8_t*>(ragdoll, 0x568);
    if (phys) {
        at<uint32_t>(phys, 0x1d34) = value;
        PhysRagdoll_DestroyObjects(phys);
        PhysRagdoll_ResetRestState(phys);
        phys[0x1d3c] = 0;
        at<float>(phys, 0x1d38) = 0.0f;
        return entry_eax;
    }
    const int32_t used = title_global<int32_t>(0x008fb484);
    if (used < 6) {
        uint8_t* const block = title_global<uint8_t*>(0x008fb450 + static_cast<uint32_t>(used) * 4);
        title_global<int32_t>(0x008fb484) = used + 1;
        if (block) phys = PhysRagdoll_InitState(block, addr(ragdoll));
    }
    at<uint8_t*>(ragdoll, 0x568) = phys;
    at<uint32_t>(phys, 0x1d34) = value;
    title_function<void XB_STDCALL(uint8_t*, uint8_t*)>(0x00231860)(phys, at<uint8_t*>(ragdoll, 0));  // PhysRagdoll_Build
    at<float>(phys, 0x1d40) = 0.0f;
    at<float>(phys, 0x1d44) = title_global<float>(0x0083eb24);  // 0.5
    PhysRagdoll_ResetRestState(phys);
    phys[0x1d3c] = 0;
    at<float>(phys, 0x1d38) = 0.0f;
    uint8_t* const entity = at<uint8_t*>(ragdoll, 0);
    const uint32_t flags = at<uint32_t>(entity, 0x2a4) | 0x410000;
    at<uint32_t>(entity, 0x2a4) = flags;
    return flags;
}

PLATFORM_ASM_ENTRY(Ragdoll_Free_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(Ragdoll_Free) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Ragdoll_Free(uint8_t* ragdoll) {
    if (at<uint32_t>(ragdoll, 0x568)) ragdoll_release_physics(ragdoll);
    RagdollPool_Free(ragdoll, 0, reinterpret_cast<uint8_t*>(uintptr_t{0x008dfde0}));
}

PLATFORM_ASM_ENTRY(Ragdoll_Alloc_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(Ragdoll_Alloc) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL Ragdoll_Alloc(uint8_t* entity) {
    uint8_t* ragdoll = at<uint8_t*>(entity, 0x238);
    if (!ragdoll) {
        const int32_t used = title_global<int32_t>(0x008f0464);
        if (used < 0x30) {
            ragdoll = title_global<uint8_t*>(0x008f02e0 + static_cast<uint32_t>(used) * 4);
            title_global<int32_t>(0x008f0464) = used + 1;
            if (ragdoll) RagdollFrames_Construct(ragdoll);
        }
    }
    if (Ragdoll_BindBones(entity, ragdoll)) return ragdoll;
    RagdollPool_Free(ragdoll, 0, reinterpret_cast<uint8_t*>(uintptr_t{0x008dfde0}));
    return nullptr;
}

namespace {
// Quaternion of a rotation given as three rows (w lanes 0), as the title's packed code does it:
// the trace path, else the largest diagonal; every lane of each packed step is computed (MXCSR).
// 0.5 / sqrt(s) on the x87.
Lanes rows_to_quat(const Lanes& a, const Lanes& b, const Lanes& c) {
    const float m0 = a.v[0], m4 = b.v[1], m8 = c.v[2];
    const float one = title_global<float>(0x0083eb20);
    const float half = title_global<float>(0x0083eb24);
    const float trace = (m8 + m4) + m0;
    Lanes q;
    if (trace >= title_global<float>(0x0083efb8)) {  // -1/3
        const float s = trace + one;
        const float f = platform_x87_half_over_sqrt(s, half);
        const Lanes t7 = sub(b, splat(a.v[1]));
        const Lanes t6 = sub(splat(a.v[2]), c);
        const Lanes t1 = sub(c, splat(b.v[2]));
        platform_keep(t7);
        platform_keep(t6);
        platform_keep(t1);
        q = mul(Lanes{{t1.v[1], t6.v[0], t7.v[0], s}}, splat(f));
    } else if (m4 > m0 && m4 > m8) {
        const float s = ((m4 - m0) - m8) + one;
        const float f = platform_x87_half_over_sqrt(s, half);
        const Lanes t6 = sub(splat(a.v[2]), c);
        const Lanes t3 = add(c, splat(b.v[2]));
        const Lanes t0 = add(b, splat(a.v[1]));
        platform_keep(t6);
        platform_keep(t3);
        platform_keep(t0);
        q = mul(Lanes{{t0.v[0], s, t3.v[1], t6.v[0]}}, splat(f));
    } else if (!(m4 > m0) && m0 > m8) {
        const float s = ((m0 - m4) - m8) + one;
        const float f = platform_x87_half_over_sqrt(s, half);
        const Lanes t2 = sub(c, splat(b.v[2]));
        const Lanes t5 = add(c, splat(a.v[2]));
        const Lanes t1 = add(b, splat(a.v[1]));
        platform_keep(t2);
        platform_keep(t5);
        platform_keep(t1);
        q = mul(Lanes{{s, t1.v[0], t5.v[0], t2.v[1]}}, splat(f));
    } else {
        const float s = ((m8 - m0) - m4) + one;
        const float f = platform_x87_half_over_sqrt(s, half);
        const Lanes t7 = sub(b, splat(a.v[1]));
        const Lanes t7b = add(c, splat(b.v[2]));
        const Lanes t0 = add(c, splat(a.v[2]));
        platform_keep(t7);
        platform_keep(t7b);
        platform_keep(t0);
        q = mul(Lanes{{t0.v[0], t7b.v[1], s, t7.v[0]}}, splat(f));
    }
    return q;
}

}  // namespace

// Records of 0x60 bytes from the pool at 0x008dfdd0 (count 0x008dfdd4, 90): rows 0-2 of the base
// rotation (w 0), the base origin (w 1), the rotation's quaternion (+0x40), the bone (+0x50), its
// slot in the ragdoll bone table 0x008db38c (+0x54, -1) and that slot again at +0x58 when the bone
// has a parent (else -1). Built once per model: kept while the pool holds at least numBones.
void XB_STDCALL Ragdoll_CacheBaseBoneFrames(uint8_t* entity) {
    int32_t& count = title_global<int32_t>(0x008dfdd4);
    if (count > 0 && at<uint8_t>(at<uint8_t*>(entity, 0x22c), 0x2f) <= static_cast<uint32_t>(count)) return;
    count = 0;
    const int32_t bones = at<uint8_t>(at<uint8_t*>(entity, 0x22c), 0x2f);
    for (int32_t bone = 0; bone < bones; ++bone) {
        uint8_t* rec = nullptr;
        if (count < 0x5a) {
            rec = title_global<uint8_t*>(0x008dfdd0) + count * 0x60;
            ++count;
        }
        at<int32_t>(rec, 0x54) = -1;
        at<int32_t>(rec, 0x58) = -1;
        at<int32_t>(rec, 0x50) = bone;
        for (int32_t i = 0; i < 10; ++i) {
            if (bone == title_global<int32_t>(0x008db38c + i * 8)) {
                at<int32_t>(rec, 0x54) = i;
                break;
            }
        }
        if (DObj_GetBoneParent(at<const DObj*>(entity, 0x22c), bone) >= 0) at<int32_t>(rec, 0x58) = at<int32_t>(rec, 0x54);
        const uint8_t* const pose = DObj_GetBoneBasePose(at<const DObj*>(entity, 0x22c), bone);
        uint32_t m[9];
        std::memcpy(m, pose, 0x24);
        const uint32_t rows[12] = {m[0], m[1], m[2], 0, m[3], m[4], m[5], 0, m[6], m[7], m[8], 0};
        std::memcpy(rec, rows, 0x30);
        uint8_t origin[16];
        std::memcpy(origin, pose + 0x24, 16);
        std::memcpy(rec + 0x30, origin, 16);
        at<float>(rec, 0x3c) = title_global<float>(0x0083eb20);
        const float* const r = reinterpret_cast<const float*>(rec);
        const Lanes q = rows_to_quat(lanes(r), lanes(r + 4), lanes(r + 8));
        std::memcpy(rec + 0x40, q.v, 16);
    }
}

// Per bone of the entity's model, a quaternion (pool +0x5a0, count +0x5a4) and an origin (pool
// +0xb50, count +0xb54; 16-byte records, 90 each) of its base pose local to its parent: the bone's
// model matrix (obj +0x20, 0x40 apart, from +0x30) taken into the parent's transposed rotation;
// root bones take DObj_GetBoneBasePose's rotation and origin.
void XB_STDCALL PhysRagdoll_StoreLocalBasePose(uint8_t* phys, uint8_t* entity) {
    const int32_t bones = at<uint8_t>(at<uint8_t*>(entity, 0x22c), 0x2f);
    uint32_t stale = 0;  // the stack word after the root path's 9-word copy: the parent path's first word
    for (int32_t bone = 0; bone < bones; ++bone) {
        uint8_t* quat = nullptr;
        int32_t& quats = at<int32_t>(phys, 0x5a4);
        if (quats < 0x5a) {
            quat = at<uint8_t*>(phys, 0x5a0) + quats * 16;
            ++quats;
        }
        uint8_t* origin = nullptr;
        int32_t& origins = at<int32_t>(phys, 0xb54);
        if (origins < 0x5a) {
            origin = at<uint8_t*>(phys, 0xb50) + origins * 16;
            ++origins;
        }
        const DObj* const obj = at<const DObj*>(entity, 0x22c);
        const int32_t parent = DObj_GetBoneParent(obj, bone);
        Lanes q;
        if (parent >= 0) {
            const uint8_t* const base = at<uint8_t*>(obj, 0x20);
            const float* const p = reinterpret_cast<const float*>(base + parent * 0x40 + 0x30);
            const float* const b = reinterpret_cast<const float*>(base + bone * 0x40 + 0x30);
            Lanes brow[4];
            for (int32_t k = 0; k < 4; ++k) brow[k] = lanes(b + k * 4);
            float prot[12];
            std::memcpy(prot, p, 0x30);
            const Lanes d = sub(brow[3], lanes(p + 12));
            const Lanes t3 = rotate_into(prot, d);
            const Lanes t2 = rotate_into(prot, brow[2]);
            const Lanes t1 = rotate_into(prot, brow[1]);
            const Lanes t0 = rotate_into(prot, brow[0]);
            std::memcpy(&stale, &t0.v[0], 4);
            q = rows_to_quat(t0, t1, t2);
            std::memcpy(quat, q.v, 16);
            std::memcpy(origin, t3.v, 16);
        } else {
            const uint8_t* const pose = DObj_GetBoneBasePose(obj, bone);
            float m[10];
            std::memcpy(m, pose, 0x24);
            std::memcpy(&m[9], &stale, 4);
            q = rows_to_quat(lanes(m), lanes(m + 3), lanes(m + 6));
            std::memcpy(quat, q.v, 16);
            const uint8_t* const again = DObj_GetBoneBasePose(at<const DObj*>(entity, 0x22c), bone);
            uint8_t o[16];
            std::memcpy(o, again + 0x24, 16);
            std::memcpy(origin, o, 16);
        }
    }
}

PLATFORM_ASM_ENTRY(VehiclePhys_TraceWheels_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(VehiclePhys_TraceWheels) "addl $4, %esp\n"
                   "ret")

// ESI vehicle physics: with a body (+0x34), each of the four wheel points (+0x50, local) in the
// body frame (+0x40) is traced down the frame's version of +0x90 against the world meshes, from
// 2 * radius (+0x120) above over 4 * radius; a hit sets the wheel's flag (+0x130), the contact
// point (+0xa0) and the plane (+0xe0). +0x134 (per wheel) is cleared.
void XB_CDECL VehiclePhys_TraceWheels(uint8_t* phys) {
    const uint8_t* const body = at<uint8_t*>(phys, 0x34);
    if (!body) return;
    const float* const m = reinterpret_cast<const float*>(body + 0x40);
    float up[4];
    std::memcpy(up, phys + 0x90, 16);
    const Lanes dir = add(add(mul(lanes(m), up[0]), mul(lanes(m + 4), up[1])), mul(lanes(m + 8), up[2]));
    for (int32_t i = 0; i < 4; ++i) {
        phys[0x130 + i] = 0;
        at<float>(phys, 0x134 + i * 4) = 0.0f;
        float p[4];
        std::memcpy(p, phys + 0x50 + i * 16, 16);
        const Lanes world = transform_out(m, p);
        const float radius = at<float>(phys, 0x120);
        alignas(16) float start[4];
        const Lanes s = sub(world, mul(splat(radius * title_global<float>(0x0083eba8)), dir));  // 2
        std::memcpy(start, s.v, 16);
        alignas(16) float delta[4];
        const Lanes d = mul(splat(at<float>(phys, 0x120) * title_global<float>(0x0083100c)), dir);  // 4
        std::memcpy(delta, d.v, 16);
        alignas(16) float plane[4];
        float fraction;
        if (!CM_RayHitsWorldMeshes(start, delta, plane, &fraction)) continue;
        phys[0x130 + i] = 1;
        std::memcpy(phys + 0xe0 + i * 16, plane, 16);
        const Lanes hit = add(lanes(start), mul(splat(fraction), lanes(delta)));
        std::memcpy(phys + 0xa0 + i * 16, hit.v, 16);
    }
}

namespace {

// The world body (+8) of the entity's first physics object (0x008fccf0, count 0x008fcde4), or 0.
uint8_t* entity_world_body(const void* entity) {
    const uint32_t end = 0x008fccf0 + title_global<uint32_t>(0x008fcde4) * 4;
    for (uint32_t it = 0x008fccf0; it != end; it += 4) {
        const uint8_t* const object = *reinterpret_cast<uint8_t* const*>(uintptr_t{it});
        if (at<const void*>(object, 4) == entity) return at<uint8_t*>(object, 8);
    }
    return nullptr;
}

// A world point in a body's frame: the transposed rotation (+0) times point - origin (+0x30).
Lanes body_local_point(const uint8_t* body, const Lanes& world) {
    float rot[12];
    std::memcpy(rot, body, 0x30);
    return rotate_into(rot, sub(world, lanes(reinterpret_cast<const float*>(body + 0x30))));
}

}  // namespace

// EAX point on a, stack (a, b, point on b): both entities' world bodies joined by a new attach
// constraint (PhysWorld_AllocConstraint50) anchored at the points (entity matrices +0x160) in each
// body's frame. The anchors are vec3 stores: a's w is the stack word left from b's matrix row 2
// (+0x18c), b's the w of its point. No caller (no CALL, pointer or immediate): dead.
void XB_REGS1 XB_CDECL Phys_AttachEntitiesAtPoints(const float* point_a, uint8_t* a, uint8_t* b, const float* point_b) {
    uint8_t* const body_a = entity_world_body(a);
    uint8_t* const body_b = entity_world_body(b);
    if (!body_a || !body_b) return;
    float pa[4];
    std::memcpy(pa, point_a, 16);
    const Lanes local_a = body_local_point(body_a, transform_out(reinterpret_cast<const float*>(a + 0x160), pa));
    float pb[4];
    std::memcpy(pb, point_b, 16);
    const Lanes local_b = body_local_point(body_b, transform_out(reinterpret_cast<const float*>(b + 0x160), pb));
    float anchor_a[4] = {local_a.v[0], local_a.v[1], local_a.v[2], 0.0f};
    std::memcpy(&anchor_a[3], b + 0x18c, 4);
    float anchor_b[4] = {local_b.v[0], local_b.v[1], local_b.v[2], pb[3]};
    const uint32_t c = PhysWorld_AllocConstraint50(addr(body_a), addr(body_b));
    PhysAttach_SetAnchors(reinterpret_cast<uint8_t*>(uintptr_t{c}), anchor_a, anchor_b);
}

// EAX vehicle physics: the debug view of its body (+0x34) with the drawing compiled out - the eight
// box corners (half extents +0x40 in the body frame +0) and per wheel (+0x50) the suspension line
// +-radius (+0x120) minus the wheel's compression (+0x134) along the frame's +0x90 are computed into
// one dummy stack slot. Only the float math (MXCSR) is left. Called only from 0x00228bc0 (no
// caller, pointer or immediate): dead.
void XB_REGS1 XB_CDECL VehiclePhys_DebugDrawBody(const uint8_t* phys) {
    const uint8_t* const body = at<uint8_t*>(phys, 0x34);
    if (!body) return;
    const float* const m = reinterpret_cast<const float*>(body);
    const float half = title_global<float>(0x0083eb24);
    for (int32_t i = 0; i < 2; ++i) {
        for (int32_t j = 0; j < 2; ++j) {
            for (int32_t k = 0; k < 2; ++k) {
                const float x = i ? 0.0f - at<float>(phys, 0x40) : at<float>(phys, 0x40);
                const float y = j ? 0.0f - at<float>(phys, 0x44) : at<float>(phys, 0x44);
                const float z = k ? 0.0f - at<float>(phys, 0x48) : at<float>(phys, 0x48);
                const Lanes h = mul(splat(half), Lanes{{x, y, z, m[7]}});  // w: row 1's w left in the slot
                const Lanes corner = add(add(add(lanes(m + 12), mul(lanes(m), h.v[0])), mul(lanes(m + 4), h.v[1])),
                                         mul(lanes(m + 8), h.v[2]));
                platform_keep(corner);
            }
        }
    }
    float up[4];
    std::memcpy(up, phys + 0x90, 16);
    const Lanes dir = add(add(mul(lanes(m), up[0]), mul(lanes(m + 4), up[1])), mul(lanes(m + 8), up[2]));
    const float radius = at<float>(phys, 0x120);
    const float below = 0.0f - radius;
    for (int32_t w = 0; w < 4; ++w) {
        float p[4];
        std::memcpy(p, phys + 0x50 + w * 16, 16);
        const Lanes world = transform_out(m, p);
        const float compression = at<float>(phys, 0x134 + w * 4);
        const Lanes top = add(world, mul(splat(radius - compression), dir));
        const Lanes bottom = add(world, mul(splat(below - compression), dir));
        platform_keep(top);
        platform_keep(bottom);
    }
}

// Every physics object (0x008fccf0; the end taken once, before removals): the entity's matrix
// (+0x160) = the body frame times the object's local frame (+0x70 rows, +0xa0 origin), the pose
// from physics; "physicsdone" and the object removed once its body is at rest (+0x144 flag 4) or
// below z -60000.
void XB_CDECL PhysObj_UpdateAll() {
    const uint32_t end = 0x008fccf0 + title_global<uint32_t>(0x008fcde4) * 4;
    for (uint32_t it = 0x008fccf0; it != end; it += 4) {
        uint8_t* const object = *reinterpret_cast<uint8_t* const*>(uintptr_t{it});
        const float* const body = at<float*>(object, 8);
        uint8_t* const entity = at<uint8_t*>(object, 0);
        const float* const local = reinterpret_cast<const float*>(object + 0x70);
        Lanes rows[3];
        for (int32_t k = 0; k < 3; ++k) {
            const float* const l = local + k * 4;
            const float x = l[0], y = l[1], z = l[2];
            rows[k] = add(add(mul(lanes(body), x), mul(lanes(body + 4), y)), mul(lanes(body + 8), z));
        }
        std::memcpy(entity + 0x160, rows, 0x30);
        const Lanes origin = transform_out(body, local + 12);
        std::memcpy(entity + 0x190, origin.v, 16);
        G_SetPoseFromPhysics(at<uint8_t*>(object, 0));
        const uint8_t* const b = at<uint8_t*>(object, 8);
        if ((b[0x144] & 4) || title_global<float>(0x0083f10c) > at<float>(b, 0x38)) {  // -60000
            Scr_Notify(at<uint8_t*>(object, 0), title_global<uint32_t>(0x0097a754));  // "physicsdone"
            Phys_RemoveEntity(at<uint8_t*>(object, 0));
        }
    }
}

// A physics object (pool 0x008fccf0, 30, count 0x008fcde4) for the entity: a box body over its
// model's bounds (+0x22c -> +0x38) at the entity matrix, mass `mass`, the description (identity
// frame offset by -centre), inertia scaled to the mass (the smallest lane at least 0.01), the
// inherited velocities, bounce 0.5 (0.2 and +0x138 = 12 for boxes up to 5 across, else 16; `bounce`
// within [0, 1] wins); the entity unlinked and flagged 0x400000. 0 when the pool or world is full.
// The velocity copies are vec3 stores: their w words are what the frame slots held (the inertia
// minimum's lane 3 for the angular one, also stored at body +0xcc; the length's lane 3 for the
// linear one). The inertia's w is a stack word Phys_BoxMassProperties never writes (zero in tests).
uint8_t* XB_CDECL PhysObj_Create(uint8_t* ent, uint32_t arg, float mass, float bounce) {
    int32_t& count = title_global<int32_t>(0x008fcde4);
    if (count >= 0x1e) return nullptr;
    uint8_t* const object = title_global<uint8_t*>(0x008fccf0 + static_cast<uint32_t>(count) * 4);
    ++count;
    if (!object) return nullptr;
    const uint8_t* const model = at<uint8_t*>(ent, 0x22c);
    if (!model) {
        PhysObjPool_Free(object, 0, reinterpret_cast<uint8_t*>(uintptr_t{0x008fb490}));
        return nullptr;
    }
    const float* const bounds = at<float*>(model, 0x38);
    const Lanes size = sub(lanes(bounds + 4), lanes(bounds));
    const Lanes centre = mul(add(lanes(bounds + 4), lanes(bounds)), splat(title_global<float>(0x0083eb24)));
    alignas(16) float inertia[4] = {};
    float box_mass;
    Phys_BoxMassProperties(size.v, inertia, &box_mass);
    const Lanes sq = mul(size, size);
    const float len2 = (sq.v[0] + sq.v[1]) + sq.v[2];
    Lanes length;
    if (len2 == len2 && len2 != title_global<float>(0x0082e680)) {  // UCOMISS + JNE: NaN counts as equal
        const float r = platform_rsqrt_estimate(len2);
        const float scaled = len2 * r;
        const float three = title_global<float>(0x0082e660);
        length = Lanes{{((three - scaled * r) * scaled) * title_global<float>(0x0082e670), three, three, three}};
    } else {
        length = Lanes{{len2, sq.v[1], sq.v[2], sq.v[3]}};
    }
    alignas(16) uint32_t frame[16];
    Matrix_Identity(reinterpret_cast<float*>(frame));
    uint32_t centre_bits[4];
    std::memcpy(centre_bits, centre.v, 16);
    for (int32_t i = 0; i < 4; ++i) frame[12 + i] = centre_bits[i] ^ title_global<uint32_t>(0x0082e0b0 + i * 4);
    uint8_t* const body = PhysWorld_AllocBody();
    if (!body) {
        PhysObjPool_Free(object, 0, reinterpret_cast<uint8_t*>(uintptr_t{0x008fb490}));
        return nullptr;
    }
    uint32_t length_bits;
    std::memcpy(&length_bits, &length.v[0], 4);
    const uint32_t desc_args[5] = {addr(object), addr(ent), addr(body), arg, addr(frame)};
    PhysObjDesc_Init(addr(ent), centre_bits, desc_args, length_bits);
    alignas(16) float m[16];
    {
        alignas(16) float buffer[16];
        std::memcpy(m, G_GetEntityMatrix(ent, buffer), 64);
    }
    const Lanes origin = transform_out(m, centre.v);
    std::memcpy(m + 12, origin.v, 16);
    const float ratio = mass / box_mass;
    Lanes scaled_inertia = mul(splat(ratio), lanes(inertia));
    const float x = scaled_inertia.v[0], y = scaled_inertia.v[1], z = scaled_inertia.v[2];
    float smallest;
    float lane3;  // what the frame slot's fourth word holds afterwards
    if (z <= ((y <= x) ? y : x)) {
        smallest = z;
        lane3 = z;
    } else if (y <= x) {
        smallest = y;
        lane3 = y;
    } else {
        smallest = x;
        lane3 = scaled_inertia.v[3];
    }
    const float floor = title_global<float>(0x0083eb94);  // 0.01
    if (floor > smallest) scaled_inertia = mul(scaled_inertia, splat(floor / smallest));
    alignas(16) float inertia_out[4];
    std::memcpy(inertia_out, scaled_inertia.v, 16);
    float friction = title_global<float>(0x0083eb24);  // 0.5
    if (title_global<float>(0x0083ece8) >= length.v[0]) {  // 5
        friction = title_global<float>(0x0083eca0);  // 0.2
        at<float>(body, 0x138) = title_global<float>(0x0083eccc);  // 12
    } else {
        at<float>(body, 0x138) = title_global<float>(0x0083ebb8);  // 16
    }
    if (bounce >= title_global<float>(0x0083eafc) && title_global<float>(0x0083eb20) >= bounce) friction = bounce;
    alignas(16) float angular[4];
    alignas(16) float linear[4];
    std::memcpy(angular, &title_global<uint8_t>(0x0082e110), 16);
    std::memcpy(linear, &title_global<uint8_t>(0x0082e110), 16);
    PhysObj_GetInheritedVelocity(ent, angular, linear);
    G_EntUnlink(ent);
    if (uint8_t* const vehicle = at<uint8_t*>(ent, 0x248)) PhysBody_Clear(vehicle);
    alignas(16) float angular_copy[4] = {angular[0], angular[1], angular[2], lane3};
    alignas(16) float linear_copy[4] = {linear[0], linear[1], linear[2], length.v[3]};
    PhysBody_Init(body, mass, inertia_out, m, linear_copy, angular_copy, friction, 3);
    float c0[4];
    std::memcpy(c0, &title_global<uint8_t>(0x00989230), 16);
    c0[3] = lane3;
    std::memcpy(body + 0xc0, c0, 16);
    at<float>(body, 0x134) = title_global<float>(0x00989240);
    at<uint32_t>(ent, 0x2a4) |= 0x400000;
    return body;
}

// The model's bone matrices (obj +0x20, 0x40 apart from +0x30) after a ragdoll step, for the first
// min(numBones, cached) bones of Ragdoll_CacheBaseBoneFrames' records: a bone with no ragdoll slot
// (-1) takes its parent's matrix times its base frame; slot 0 is left alone; other ragdoll bones
// keep their rotation and get their origin from the parent's matrix and their base origin.
void XB_STDCALL Ragdoll_PropagateBoneMatrices(uint8_t* entity) {
    int32_t count = at<uint8_t>(at<uint8_t*>(entity, 0x22c), 0x2f);
    if (count > title_global<int32_t>(0x008dfdd4)) count = title_global<int32_t>(0x008dfdd4);
    for (int32_t bone = 0; bone < count; ++bone) {
        const uint8_t* const obj = at<uint8_t*>(entity, 0x22c);
        const int32_t parent = DObj_GetBoneParent(reinterpret_cast<const DObj*>(obj), bone);
        const uint8_t* const rec = title_global<uint8_t*>(0x008dfdd0) + bone * 0x60;
        const int32_t slot = at<int32_t>(rec, 0x54);
        uint8_t* const base = at<uint8_t*>(at<uint8_t*>(entity, 0x22c), 0x20);
        const float* const p = reinterpret_cast<const float*>(base + parent * 0x40 + 0x30);
        float* const out = reinterpret_cast<float*>(base + bone * 0x40 + 0x30);
        const float* const frame = reinterpret_cast<const float*>(rec);
        if (slot == -1) {
            Lanes rows[3];
            for (int32_t k = 0; k < 3; ++k) {
                const float x = frame[k * 4], y = frame[k * 4 + 1], z = frame[k * 4 + 2];
                rows[k] = add(add(mul(lanes(p), x), mul(lanes(p + 4), y)), mul(lanes(p + 8), z));
            }
            std::memcpy(out, rows, 0x30);
        } else if (slot == 0) {
            continue;
        }
        const Lanes origin = transform_out(p, frame + 12);
        std::memcpy(out + 12, origin.v, 16);
    }
}

// Bone callback (Phys_SetBoneCallbacks; bone = {entity, index}): the physics frame times the bone's
// local frame, taken into the entity's frame (+0x160, transposed rotation) as the model's bone
// matrix (obj +0x20, 0x40 apart from +0x30). The root bone (index 0) first sets the entity
// matrix to identity rotation (row 0's w 1) at the bone's world origin and its pose from physics.
void XB_CDECL Ragdoll_BodyToPose(const uint32_t* bone, const float* frame, const float* local) {
    uint8_t* const entity = reinterpret_cast<uint8_t*>(uintptr_t{bone[0]});
    const int32_t index = static_cast<int32_t>(bone[1]);
    Lanes r[3];
    for (int32_t k = 0; k < 3; ++k) {
        const float* const l = local + k * 4;
        const float x = l[0], y = l[1], z = l[2];
        r[k] = add(add(mul(lanes(frame), x), mul(lanes(frame + 4), y)), mul(lanes(frame + 8), z));
    }
    const Lanes origin = transform_out(frame, local + 12);
    if (index == 0) {
        const float one = title_global<float>(0x0083eb20);
        const float rows[16] = {one,  0.0f, 0.0f, one,  0.0f,        one,         0.0f,        0.0f,
                                0.0f, 0.0f, one,  0.0f, origin.v[0], origin.v[1], origin.v[2], origin.v[3]};
        std::memcpy(entity + 0x160, rows, 64);
        G_SetPoseFromPhysics(entity);
    }
    float e[12];
    std::memcpy(e, entity + 0x160, 0x30);
    const Lanes d = sub(origin, lanes(reinterpret_cast<const float*>(entity + 0x190)));
    const Lanes t3 = rotate_into(e, d);
    const Lanes t2 = rotate_into(e, r[2]);
    const Lanes t1 = rotate_into(e, r[1]);
    const Lanes t0 = rotate_into(e, r[0]);
    const Lanes out[4] = {t0, t1, t2, t3};
    uint8_t* const base = at<uint8_t*>(at<uint8_t*>(entity, 0x22c), 0x20);
    std::memcpy(base + index * 0x40 + 0x30, out, 64);
}

namespace {

// A frame from angles and an origin as G_GetEntityMatrix builds it: rows forward, -left (0 - x),
// up, origin; each row's w repeats its x.
void angles_frame(const float* angles, const float* origin, Lanes* rows) {
    float forward[3];
    float left[3];
    float up[3];
    uint32_t args[2] = {addr(angles), addr(forward)};
    AnglesToAxis(args, left, up);
    const float l0 = 0.0f - left[0], l1 = 0.0f - left[1], l2 = 0.0f - left[2];
    rows[0] = Lanes{{forward[0], forward[1], forward[2], forward[0]}};
    rows[1] = Lanes{{l0, l1, l2, l0}};
    rows[2] = Lanes{{up[0], up[1], up[2], up[0]}};
    rows[3] = Lanes{{origin[0], origin[1], origin[2], origin[0]}};
}

// frame (rows f) times a 4x4 matrix m (rows 0-2 rotate, row 3 also offset by f[3]).
void frame_times(const Lanes* f, const float* m, float* out) {
    Lanes rows[4];
    for (int32_t k = 0; k < 3; ++k) {
        const float* const r = m + k * 4;
        const float x = r[0], y = r[1], z = r[2];
        rows[k] = add(add(mul(f[0], x), mul(f[1], y)), mul(f[2], z));
    }
    const float x = m[12], y = m[13], z = m[14];
    rows[3] = add(add(add(f[3], mul(f[0], x)), mul(f[1], y)), mul(f[2], z));
    std::memcpy(out, rows, 64);
}

}  // namespace

// The ragdoll's ten bone frames in world space for both of its sets: frames +0x290 (0x40 apart)
// in the frame of angles +0x530 / origin +0x540 into 0x00988fb0, frames +0x10 in the frame of
// angles +0x510 / origin +0x520 into 0x00988d30.
void XB_STDCALL Ragdoll_BuildWorldBoneFrames(uint8_t* ragdoll) {
    Lanes first[4];
    angles_frame(reinterpret_cast<const float*>(ragdoll + 0x510), reinterpret_cast<const float*>(ragdoll + 0x520), first);
    Lanes second[4];
    angles_frame(reinterpret_cast<const float*>(ragdoll + 0x530), reinterpret_cast<const float*>(ragdoll + 0x540), second);
    for (int32_t i = 0; i < 10; ++i) {
        frame_times(second, reinterpret_cast<const float*>(ragdoll + 0x290 + i * 0x40),
                    &title_global<float>(0x00988fb0 + i * 0x40));
        frame_times(first, reinterpret_cast<const float*>(ragdoll + 0x10 + i * 0x40),
                    &title_global<float>(0x00988d30 + i * 0x40));
    }
}

namespace {

float dot3_lanes(const Lanes& a, const Lanes& b) {
    const Lanes p = mul(a, b);
    platform_keep(p);
    return (p.v[0] + p.v[1]) + p.v[2];
}

// The clamp parameter as the title packs it: lane 0 = dot / divisor, lanes 1-3 the products.
float clamp_param(const Lanes& u, const Lanes& v, float divisor) {
    const Lanes p = mul(u, v);
    alignas(16) float value[4] = {((p.v[0] + p.v[1]) + p.v[2]) / divisor, p.v[1], p.v[2], p.v[3]};
    const float lo = 0.0f;
    const float hi = title_global<float>(0x0083eb20);
    alignas(16) float out[4];
    return *Vec4_ClampToScalarRange(out, 0, value, &lo, &hi);
}

float clamp01(float x, float zero, float one) {
    if (zero > x) return zero;
    if (x > one) return one;
    return x;
}

// (c * a) - (b * b) on the x87 (FLD/FMUL/FSUBP, the caller's precision control), stored as float.
float x87_cross_term(float c, float a, float b) {
    const long double t = static_cast<long double>(c) * a - static_cast<long double>(b) * b;
    return static_cast<float>(t);
}

}  // namespace

PLATFORM_ASM_ENTRY(Phys_CapsuleCapsuleClosest_entry,
                   "leal 4(%esp), %eax\n"
                   "pushl %eax\n"
                   "pushl %esi\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(Phys_CapsuleCapsuleClosest) "addl $12, %esp\n"
                   "ret")

// ECX b1, ESI out point on a; stack (a0, a1, radius a, b0, radius b, out point on b, out normal):
// the closest points of segments a0-a1 and b0-b1 (the interior solution, else the best of four
// clamped endpoint tests); 1 when they are closer than the radii (and not touching), the points
// then moved to the capsule surfaces along the normal (b - a, normalised).
uint32_t XB_CDECL Phys_CapsuleCapsuleClosest(const float* b1, float* out_a, const uint32_t* args) {
    const float* const a0 = reinterpret_cast<const float*>(uintptr_t{args[0]});
    const float* const a1 = reinterpret_cast<const float*>(uintptr_t{args[1]});
    float ra, rb;
    std::memcpy(&ra, &args[2], 4);
    const float* const b0 = reinterpret_cast<const float*>(uintptr_t{args[3]});
    std::memcpy(&rb, &args[4], 4);
    float* const out_b = reinterpret_cast<float*>(uintptr_t{args[5]});
    float* const out_n = reinterpret_cast<float*>(uintptr_t{args[6]});
    const Lanes d1 = sub(lanes(a1), lanes(a0));
    const Lanes r = sub(lanes(a0), lanes(b0));
    const Lanes e = sub(lanes(b0), lanes(b1));
    const float a = dot3_lanes(d1, d1);
    const float b = dot3_lanes(d1, e);
    const float c = dot3_lanes(e, e);
    const float den = x87_cross_term(c, a, b);
    const float d = dot3_lanes(d1, r);
    const float f = dot3_lanes(e, r);
    const float zero = 0.0f;
    const float one = title_global<float>(0x0083eb20);
    float best;
    bool done = false;
    if (platform_x87_greater(abs_bits(den), title_global<float>(0x0083effc))) {  // 1e-5
        const float inv = one / den;
        const float s = 0.0f - ((c * d - b * f) * inv);
        if (s >= zero && one >= s) {
            const float t = 0.0f - ((a * f - b * d) * inv);
            if (t >= zero && one >= t) {
                const Lanes pa = add(lanes(a0), mul(splat(s), d1));
                std::memcpy(out_a, pa.v, 16);
                const Lanes pb = sub(lanes(b0), mul(splat(t), e));
                std::memcpy(out_b, pb.v, 16);
                const Lanes n = sub(pb, lanes(out_a));
                std::memcpy(out_n, n.v, 16);
                best = dot3_lanes(n, n);
                done = true;
            }
        }
    }
    if (!done) {
        const float s1 = clamp01(0.0f - d / a, zero, one);
        const Lanes pa1 = add(lanes(a0), mul(splat(s1), d1));
        std::memcpy(out_a, pa1.v, 16);
        std::memcpy(out_b, b0, 16);
        const Lanes n1 = sub(lanes(out_b), lanes(out_a));
        std::memcpy(out_n, n1.v, 16);
        best = dot3_lanes(n1, n1);
        // a0 against segment b
        const float t1 = clamp01(0.0f - f / c, zero, one);
        const Lanes pb1 = sub(lanes(b0), mul(splat(t1), e));
        const Lanes start_a = lanes(a0);
        const Lanes n2 = sub(pb1, start_a);
        const float dist2 = dot3_lanes(n2, n2);
        if (best > dist2) {
            std::memcpy(out_a, start_a.v, 16);
            std::memcpy(out_b, pb1.v, 16);
            std::memcpy(out_n, n2.v, 16);
            best = dist2;
        }
        // a1 against segment b
        const Lanes v = sub(lanes(a1), lanes(b1));
        const float t3 = clamp_param(v, e, c);
        const Lanes pb3 = add(lanes(b1), mul(splat(t3), e));
        const Lanes end_a = lanes(a1);
        const Lanes n3 = sub(pb3, end_a);
        const float dist3 = dot3_lanes(n3, n3);
        if (best > dist3) {
            std::memcpy(out_a, end_a.v, 16);
            std::memcpy(out_b, pb3.v, 16);
            std::memcpy(out_n, n3.v, 16);
            best = dist3;
        }
        // b1 against segment a
        const float s4 = clamp_param(v, d1, a);
        const Lanes pa4 = sub(lanes(a1), mul(splat(s4), d1));
        const Lanes end_b = lanes(b1);
        const Lanes n4 = sub(end_b, pa4);
        const float dist4 = dot3_lanes(n4, n4);
        if (best > dist4) {
            std::memcpy(out_a, pa4.v, 16);
            std::memcpy(out_b, end_b.v, 16);
            std::memcpy(out_n, n4.v, 16);
            best = dist4;
        }
    }
    if (!(best > title_global<float>(0x0083effc))) return 0;
    const float sum = ra + rb;
    if (!(sum * sum >= best)) return 0;
    const float length = platform_x87_sqrt(best);
    const Lanes n = div(lanes(out_n), splat(length));
    std::memcpy(out_n, n.v, 16);
    const Lanes pa = add(lanes(out_a), mul(splat(ra), n));
    std::memcpy(out_a, pa.v, 16);
    const Lanes pb = sub(lanes(out_b), mul(splat(rb), lanes(out_n)));
    std::memcpy(out_b, pb.v, 16);
    return 1;
}

namespace {

// CMPLTPS a < b over four lanes (every lane compared: MXCSR), as a lane mask.
uint32_t lt_mask(const Lanes& a, const Lanes& b) {
    uint32_t mask = 0;
    for (int32_t i = 0; i < 4; ++i)
        if (a.v[i] < b.v[i]) mask |= 1u << i;
    return mask;
}

void sys_debug_break() { title_function<void XB_CDECL()>(0x00042460)(); }

}  // namespace

// The world geometry within `radius` of `center` for the ragdoll's collision this step: per world
// tree whose box overlaps, the objects of the leaf at the center (stamped with cm +0x60 so each
// is taken once; contents 0x22091, box +0x10 centre / +0x20 half size overlapping): type 9 objects
// into 0x008fcee0 (60), else their triangles (+0x3c packed 6-bit vertex indices into +0x34, plane
// +0x44, normal length^2 at least 0.5) that touch the sphere into 0x008fd3f0 (80 records of
// {a, b, c, plane}); then up to 8 entities in the box (SV_AreaEntities_r over 0x00935664; not the
// ragdoll's own, with +0xdc) as {matrix, entity} into 0x008fd680. EAX: the area entity count.
uint32_t XB_CDECL Ragdoll_GatherCollisionGeometry(const float* center, float radius) {
    uint8_t* const cm = title_global<uint8_t*>(0x0095f6b8);
    ++at<int32_t>(cm, 0x60);
    const Lanes r = splat(radius);
    const Lanes lo = sub(lanes(center), r);
    const Lanes hi = add(lanes(center), r);
    title_global<int32_t>(0x008fcee4) = 0;
    title_global<int32_t>(0x008fd3f4) = 0;
    title_global<int32_t>(0x008fd684) = 0;
    for (int32_t i = 0; i < at<int32_t>(title_global<uint8_t*>(0x0095f4fc), 0xc); ++i) {
        const uint8_t* const tree = at<uint8_t*>(title_global<uint8_t*>(0x0095f4fc), 0x10 + i * 4);
        const uint32_t above = lt_mask(lanes(reinterpret_cast<const float*>(tree + 0x40)), hi) & 7;
        if (above != 7) continue;
        if ((lt_mask(lo, lanes(reinterpret_cast<const float*>(tree + 0x50))) & 7) != above) continue;
        alignas(16) float leaf_center[4];
        alignas(16) float leaf_half[4];
        const uint8_t* const leaf = CM_QuadTreeFindLeaf(tree, center, leaf_center, leaf_half);
        const int32_t count = at<int32_t>(leaf, 0);
        for (int32_t j = 0; j < count; ++j) {
            const uint32_t index = at<uint16_t*>(leaf, 4)[j];
            uint8_t* object;
            const uint32_t split = at<uint32_t>(tree, 0x30);
            if (static_cast<int32_t>(index) < static_cast<int32_t>(split))
                object = at<uint8_t*>(tree, 0x34) + index * 0x50;
            else
                object = at<uint8_t*>(tree, 0x3c) + (index - split) * 0x40;
            const int32_t stamp = at<int32_t>(cm, 0x60);
            if (at<int32_t>(object, 0xc) == stamp) continue;
            if (!(at<uint32_t>(object, 8) & 0x22091)) continue;
            at<int32_t>(object, 0xc) = stamp;
            const Lanes c = lanes(reinterpret_cast<const float*>(object + 0x10));
            const Lanes h = lanes(reinterpret_cast<const float*>(object + 0x20));
            const uint32_t in = lt_mask(sub(c, h), hi) & 7;
            if (in != 7) continue;
            if ((lt_mask(lo, add(c, h)) & 7) != in) continue;
            const uint32_t type = title_function<uint32_t XB_THISCALL(uint8_t*)>(at<uint32_t>(at<uint8_t*>(object, 0), 0x20))(object);
            if (type == 9) {
                int32_t& n = title_global<int32_t>(0x008fcee4);
                if (n == 0x3c) continue;
                uint32_t* slot = nullptr;
                if (n >= 0x3c) {
                    sys_debug_break();
                } else {
                    slot = title_global<uint32_t*>(0x008fcee0) + n;
                    ++n;
                    if (!slot) sys_debug_break();
                }
                *static_cast<uint32_t volatile*>(slot) = addr(object);
                continue;
            }
            const int32_t triangles = at<int32_t>(object, 0x40);
            for (int32_t k = 0; k < triangles; ++k) {
                const uint32_t packed = at<uint32_t*>(object, 0x3c)[k];
                const uint8_t* const verts = at<uint8_t*>(object, 0x34);
                const float* const plane = reinterpret_cast<const float*>(at<uint8_t*>(object, 0x44) + k * 16);
                const float* const va = reinterpret_cast<const float*>(verts + (packed & 0x3f) * 16);
                const float* const vb = reinterpret_cast<const float*>(verts + ((packed >> 6) & 0x3f) * 16);
                const float* const vc = reinterpret_cast<const float*>(verts + ((packed >> 12) & 0x3f) * 16);
                const Lanes nn = mul(lanes(plane), lanes(plane));
                platform_keep(nn);
                if (title_global<float>(0x0083eb24) > (nn.v[0] + nn.v[1]) + nn.v[2]) continue;  // 0.5
                if (!Phys_SphereTouchesTriangle(center, plane, va, radius, vb, vc)) continue;
                int32_t& m = title_global<int32_t>(0x008fd3f4);
                if (m == 0x50) continue;
                uint32_t* rec = nullptr;
                if (m >= 0x50) {
                    sys_debug_break();
                } else {
                    rec = reinterpret_cast<uint32_t*>(title_global<uint8_t*>(0x008fd3f0) + m * 16);
                    ++m;
                    if (!rec) sys_debug_break();
                }
                uint32_t volatile* const out = rec;
                out[0] = addr(va);
                out[1] = addr(vb);
                out[2] = addr(vc);
                out[3] = addr(plane);
            }
        }
    }
    uint8_t& guard = title_global<uint8_t>(0x0098e8d0);
    if (!(guard & 1)) {
        title_global<uint32_t>(0x0098e8d0) |= 1;
        std::memset(&title_global<uint8_t>(0x0098e6d0), 0, 0x200);
    }
    std::memcpy(&title_global<uint8_t>(0x0098e6b0), lo.v, 16);
    std::memcpy(&title_global<uint8_t>(0x0098e6c0), hi.v, 16);
    alignas(16) uint8_t parms[0x40] = {};  // +0 never written (SV_AreaEntities_r reads from +0x10)
    std::memcpy(parms + 0x10, lo.v, 16);
    std::memcpy(parms + 0x20, hi.v, 16);
    at<uint32_t>(parms, 0x30) = 0x0098e6d0;
    at<uint32_t>(parms, 0x34) = 0;
    at<uint32_t>(parms, 0x38) = 0x80;
    at<uint32_t>(parms, 0x3c) = 0x22091;
    SV_AreaEntities_r(reinterpret_cast<const uint8_t*>(uintptr_t{0x00935664}), parms);
    const int32_t found = at<int32_t>(parms, 0x34);
    title_global<int32_t>(0x0098e6a0) = found;
    for (int32_t e = 0; e < title_global<int32_t>(0x0098e6a0); ++e) {
        const uint32_t handle = title_global<uint32_t>(0x0098e6d0 + e * 4);
        const uint32_t index = handle & 0xfff;
        if (index >= 0x540) continue;
        if ((handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) continue;
        uint8_t* const ent = title_global<uint8_t*>(0x0097a968 + index * 8);
        if (!ent || addr(ent) == title_global<uint32_t>(0x0095f6f0) || !at<uint32_t>(ent, 0xdc)) continue;
        int32_t& n = title_global<int32_t>(0x008fd684);
        if (n >= 8) continue;
        uint8_t* const rec = title_global<uint8_t*>(0x008fd680) + n * 0x50;
        ++n;
        if (!rec) sys_debug_break();
        alignas(16) float matrix[16];
        std::memcpy(rec, G_GetEntityMatrix(ent, matrix), 64);
        at<uint32_t>(rec, 0x40) = addr(ent);
    }
    return static_cast<uint32_t>(title_global<int32_t>(0x0098e6a0));
}

namespace {

// sign ^ v over four lanes (XORPS with 0x0082e0b0).
Lanes negate_bits(const Lanes& v) {
    Lanes out;
    for (int32_t i = 0; i < 4; ++i) {
        uint32_t bits;
        std::memcpy(&bits, &v.v[i], 4);
        bits ^= title_global<uint32_t>(0x0082e0b0 + i * 4);
        std::memcpy(&out.v[i], &bits, 4);
    }
    return out;
}

// The blended local rotation of one bone: q0 (physics) and q1 (base) mixed by t (the shorter way;
// slerp weights from the title's acos and sine polynomials unless nearly equal), the vector part
// normalised (unit x when it vanishes) and turned into rows with w and the vector's length.
void blend_rotation(const Lanes& q0, const Lanes& q1, float t, float one_minus_t, Lanes* rows) {
    const float zero = 0.0f;
    const float one = title_global<float>(0x0083eb20);
    const Lanes p = mul(q0, q1);
    const Lanes h = add(p, Lanes{{p.v[2], p.v[3], p.v[0], p.v[0]}});
    platform_keep(h);
    float dot = h.v[0] + h.v[1];
    Lanes w;
    if (zero > dot) {
        w = Lanes{{one_minus_t, zero - t, one, zero}};
        dot = zero - dot;
    } else {
        w = Lanes{{one_minus_t, t, one, zero}};
    }
    if (title_global<float>(0x0083efd8) > dot) {  // 0.999999
        float angle;
        if (title_global<float>(0x0083eb24) > dot) {  // acos(d) = pi/2 - d - d^3/6 - 0.075 d^5 + c d^7
            const float d2 = dot * dot;
            const float d3 = dot * d2;
            const float d5 = d2 * d3;
            const float a3 = d3 * title_global<float>(0x0083ee9c);
            const float a5 = d5 * title_global<float>(0x0083ee98);
            const float a7 = (d5 * d2) * title_global<float>(0x0083efd4);
            angle = (((a7 - a5) - a3) - dot) + title_global<float>(0x0083ee88);
        } else {  // acos(d) = 2 asin(sqrt((1 - d) / 2)) by its series
            const float s = platform_x87_sqrt_sub_mul(one, dot, title_global<float>(0x0083eb24));
            const float s2 = s * s;
            const float s3 = s2 * s;
            const float s1 = s * title_global<float>(0x0083eba8);
            const float b3 = s3 * title_global<float>(0x0083ee90);
            const float s5 = s3 * s2;
            const float b5 = s5 * title_global<float>(0x0083ed2c);
            const float b7 = (s5 * s2) * title_global<float>(0x0083efd0);
            angle = s1 + (b3 + (b5 + b7));
        }
        const float* const c = &title_global<float>(0x0082e850);
        const Lanes a = mul(w, splat(angle));
        const Lanes a2 = mul(a, a);
        const Lanes a3 = mul(a2, a);
        const Lanes a5 = mul(a2, a3);
        const Lanes t3 = mul(a3, splat(c[0]));
        const Lanes t5 = mul(a5, splat(c[1]));
        const Lanes t7 = mul(mul(a2, a5), splat(c[2]));
        const Lanes sine = add(add(add(a, t7), t5), t3);
        w = div(sine, splat(sine.v[2]));
    }
    const Lanes q = add(mul(q0, splat(w.v[0])), mul(q1, splat(w.v[1])));
    const Lanes sq = mul(q, q);
    const float n2 = (sq.v[0] + sq.v[1]) + sq.v[2];
    Lanes length;
    if (n2 == n2 && n2 != title_global<float>(0x0082e680)) {  // UCOMISS + JNE: NaN counts as equal
        const float r = platform_rsqrt_estimate(n2);
        const float scaled = n2 * r;
        const float three = title_global<float>(0x0082e660);
        length = Lanes{{((three - scaled * r) * scaled) * title_global<float>(0x0082e670), three, three, three}};
    } else {
        length = Lanes{{n2, sq.v[1], sq.v[2], sq.v[3]}};
    }
    Lanes v;
    if (length.v[0] == zero) {  // UCOMISS + LAHF/TEST 0x44/JNP: equal and ordered
        v = lanes(&title_global<float>(0x00830640));
    } else {
        v = div(q, splat(length.v[0]));
    }
    const float qw = q.v[3];
    const Lanes twice = mul(splat(qw * title_global<float>(0x0083eba8)), length);
    platform_keep(twice);
    const float a = qw * qw - length.v[0] * length.v[0];
    const float b = twice.v[0];
    const Lanes nv = negate_bits(v);
    const Lanes nva = mul(nv, splat(a));
    const Lanes outer = sub(nv, nva);
    const Lanes nvb = mul(nv, splat(b));
    const Lanes vb = negate_bits(nvb);
    rows[0] = add(Lanes{{a, nvb.v[2], vb.v[1], vb.v[1]}}, mul(outer, splat(nv.v[0])));
    rows[1] = add(Lanes{{vb.v[2], a, nvb.v[0], nvb.v[0]}}, mul(outer, splat(nv.v[1])));
    rows[2] = add(Lanes{{nvb.v[1], vb.v[0], a, a}}, mul(outer, splat(nv.v[2])));
}

}  // namespace

// The model's bone matrices blended from the ragdoll (local quaternions +0x5a0, origins +0xb50 of
// `phys`) toward the base pose (Ragdoll_CacheBaseBoneFrames' records) by t, for the first
// min(numBones, +0x5a4) bones: bones without a ragdoll slot get the blended rotation and origin
// under their parent's matrix, other ragdoll bones (not slot 0) only the blended origin.
void XB_STDCALL Ragdoll_BlendBonePoses(uint8_t* phys, uint8_t* entity, float t) {
    int32_t count = at<uint8_t>(at<uint8_t*>(entity, 0x22c), 0x2f);
    if (count > at<int32_t>(phys, 0x5a4)) count = at<int32_t>(phys, 0x5a4);
    const float one_minus_t = title_global<float>(0x0083eb20) - t;
    for (int32_t bone = 0; bone < count; ++bone) {
        const uint8_t* const obj = at<uint8_t*>(entity, 0x22c);
        const int32_t parent = DObj_GetBoneParent(reinterpret_cast<const DObj*>(obj), bone);
        const Lanes phys_origin = lanes(reinterpret_cast<const float*>(at<uint8_t*>(phys, 0xb50) + bone * 16));
        const uint8_t* const rec = title_global<uint8_t*>(0x008dfdd0) + bone * 0x60;
        const Lanes origin = add(mul(splat(one_minus_t), phys_origin),
                                 mul(splat(t), lanes(reinterpret_cast<const float*>(rec + 0x30))));
        const int32_t slot = at<int32_t>(rec, 0x54);
        if (slot == 0) continue;
        uint8_t* const base = at<uint8_t*>(at<uint8_t*>(entity, 0x22c), 0x20);
        const float* const p = reinterpret_cast<const float*>(base + parent * 0x40 + 0x30);
        float* const out = reinterpret_cast<float*>(base + bone * 0x40 + 0x30);
        if (slot == -1) {
            Lanes local[3];
            blend_rotation(lanes(reinterpret_cast<const float*>(at<uint8_t*>(phys, 0x5a0) + bone * 16)),
                           lanes(reinterpret_cast<const float*>(rec + 0x40)), t, one_minus_t, local);
            Lanes rows[3];
            for (int32_t k = 0; k < 3; ++k) {
                const float x = local[k].v[0], y = local[k].v[1], z = local[k].v[2];
                rows[k] = add(add(mul(lanes(p), x), mul(lanes(p + 4), y)), mul(lanes(p + 8), z));
            }
            std::memcpy(out, rows, 0x30);
        }
        const float o[4] = {origin.v[0], origin.v[1], origin.v[2], origin.v[3]};
        const Lanes moved = transform_out(p, o);
        std::memcpy(out + 12, moved.v, 16);
    }
}

}  // namespace game
