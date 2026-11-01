#include "g_main.h"

#include <cstring>

#include "actor.h"
#include "g_script.h"
#include "g_utils.h"
#include "g_vehicle.h"
#include "qcommon.h"
#include "qcommon_core.h"
#include "ragdoll.h"
#include "script_string.h"
#include "sv.h"
#include "title_data.h"
#include "xanim.h"

// Calls a title function with all six registers (TitleRegs) and `count` stack words (args[0]
// pushed last); ESP comes back from EBP, so stdcall callees work too. Returns EAX.
PLATFORM_ASM_ENTRY(g_ent_call,
                   "pushl %ebp\n"
                   "movl %esp, %ebp\n"
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "pushl %ebx\n"
                   "movl 16(%ebp), %ecx\n"
                   "movl 20(%ebp), %edx\n"
                   "1:\n"
                   "testl %ecx, %ecx\n"
                   "jz 2f\n"
                   "decl %ecx\n"
                   "pushl (%edx,%ecx,4)\n"
                   "jmp 1b\n"
                   "2:\n"
                   "movl 12(%ebp), %eax\n"
                   "movl 4(%eax), %ebx\n"
                   "movl 8(%eax), %ecx\n"
                   "movl 12(%eax), %edx\n"
                   "movl 16(%eax), %esi\n"
                   "movl 20(%eax), %edi\n"
                   "movl 0(%eax), %eax\n"
                   "call *8(%ebp)\n"
                   "leal -12(%ebp), %esp\n"
                   "popl %ebx\n"
                   "popl %edi\n"
                   "popl %esi\n"
                   "popl %ebp\n"
                   "ret")

namespace game {

namespace {

uint32_t& word_at(uint8_t* base, uint32_t offset) { return *reinterpret_cast<uint32_t*>(base + offset); }

void zero(uint8_t* base, uint32_t begin, uint32_t end) { std::memset(base + begin, 0, end - begin); }

void copy_vector(uint8_t* base, uint32_t offset, uint32_t source) {
    for (uint32_t i = 0; i < 4; ++i) word_at(base, offset + i * 4) = title_global<uint32_t>(source + i * 4);
}

uint32_t address(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

}  // namespace

void XB_CDECL ShakeRumble_CreateManager() {
    uint8_t* manager = Heap_AllocAligned(4, 0x24);
    if (!manager) {
        title_global<uint32_t>(0x0095f5c4) = 0;
        return;
    }
    word_at(manager, 0) = 1;
    for (uint32_t list = 4; list <= 0x14; list += 0x10) {
        word_at(manager, list + 0xc) = address(manager + list + 4);
        word_at(manager, list) = 0;
        word_at(manager, list + 4) = address(manager + list + 8);
        word_at(manager, list + 8) = 0;
    }
    title_global<uint32_t>(0x0095f5c4) = address(manager);
}

uint8_t* XB_REGS1 XB_CDECL G_ClearPlayerState(uint8_t* ps) {
    copy_vector(ps, 0, 0x0082e290);
    copy_vector(ps, 0x10, 0x0082e110);
    zero(ps, 0x20, 0x8c);
    ps[0x8c] = 0;
    zero(ps, 0x90, 0x3f8);
    zero(ps, 0x400, 0x428);
    zero(ps, 0x42c, 0x488);
    copy_vector(ps, 0x490, 0x0082e290);
    copy_vector(ps, 0x4a0, 0x0082e110);
    zero(ps, 0x4b0, 0x4cc);
    ps[0x4cc] = 0;
    ps[0x4cd] = 0;
    zero(ps, 0x4d0, 0x500);
    return ps;
}

uint8_t* XB_REGS1 XB_CDECL G_ClearClientPersistant(uint8_t* pers) {
    // Five groups; the fourth byte after each byte triple is padding the original leaves alone.
    zero(pers, 0, 0xb);
    zero(pers, 0xc, 0x1b);
    zero(pers, 0x1c, 0x37);
    zero(pers, 0x38, 0x47);
    zero(pers, 0x48, 0x64);
    return pers;
}

PLATFORM_ASM_ENTRY(G_ClearClient_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(G_ClearClient) "addl $4, %esp\n"
                   "ret")

uint32_t XB_CDECL G_ClearClient(uint8_t* client) {
    G_ClearPlayerState(client);
    G_ClearClientPersistant(client + 0x520);
    zero(client, 0x584, 0x5ac);
    copy_vector(client, 0x5b0, 0x0082e290);
    zero(client, 0x5c0, 0x638);
    return 0;
}

PLATFORM_ASM_ENTRY(G_ClearLevelLocals_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(G_ClearLevelLocals) "addl $4, %esp\n"
                   "ret")

void XB_CDECL G_ClearLevelLocals(uint8_t* level) {
    using AssignCString = void XB_THISCALL(uint8_t*, const char*);
    const char* const empty = reinterpret_cast<const char*>(static_cast<uintptr_t>(0x0082dfeb));
    zero(level, 0, 0xb4);
    zero(level, 0x2b4, 0xb50);
    title_function<AssignCString>(0x0011dd20)(level + 0xb50, empty);  // ScriptString_AssignCString
    zero(level, 0xb54, 0xc90);
    level[0xc90] = 0;
    level[0xc91] = 0;
    zero(level, 0xc94, 0xca7);
    zero(level, 0xca8, 0xcb8);
    title_function<AssignCString>(0x0011dd20)(level + 0xcb8, empty);  // ScriptString_AssignCString
    zero(level, 0xcbc, 0x2708);
}

void XB_REGS1 XB_CDECL G_FreeEntityPhysics(uint8_t* ent) {
    if (word_at(ent, 0x244)) {
        const uint32_t ragdoll = word_at(ent, 0x238);
        if (ragdoll) {
            const TitleRegs regs{word_at(ent, 0x244), 0, 0, 0, ragdoll, address(ent)};
            g_ent_call(0x0022fb30, &regs, 0, nullptr);  // Ragdoll_FreePhysics (ESI)
        }
        return;
    }
    uint8_t* const vehicle = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(word_at(ent, 0x248)));
    if (vehicle) {
        const int16_t def = *reinterpret_cast<int16_t*>(vehicle + 0x178);
        const uint8_t* const vehicle_def = title_global<uint8_t*>(0x00950a28 + def * 4);  // g_vehicleDefs
        if (*reinterpret_cast<const int16_t*>(vehicle_def + 0x20) == 1) {
            const uint32_t physics = word_at(vehicle, 0x300);
            if (physics) {
                const TitleRegs regs{physics, 0, 0, 0, physics, address(ent)};
                g_ent_call(0x00225520, &regs, 0, nullptr);  // VehiclePhys_Destroy (ESI)
                word_at(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(word_at(ent, 0x248))), 0x300) = 0;
            }
            return;
        }
    }
    const TitleRegs regs{address(vehicle), 0, 0, 0, 0, address(ent)};
    g_ent_call(0x00225ae0, &regs, 0, nullptr);  // Phys_RemoveEntity (EDI)
}

void XB_CDECL G_VehiclePhysicsFrame(float frame_time) {
    // The scaled time goes back into the argument slot (x87 product), then to each step.
    const float scaled = platform_x87_mul(title_global<float>(0x008ff028), frame_time);
    uint32_t bits;
    std::memcpy(&bits, &scaled, sizeof bits);
    title_function<void XB_CDECL(uint32_t)>(0x00230390)(bits);  // Ragdoll_UpdateAllFromEntities
    title_function<void XB_CDECL(uint32_t)>(0x000b4170)(bits);  // Phys_RunFrame
    title_function<void XB_CDECL()>(0x00230930)();              // PhysObj_UpdateAll
    title_function<void XB_CDECL(uint32_t)>(0x00230350)(bits);  // Ragdoll_UpdateAll
    // The vehicles with physics: 0x008feb70, count at 0x008feb9c (read once).
    const uint32_t end = 0x008feb70 + title_global<uint32_t>(0x008feb9c) * 4;
    for (uint32_t at = 0x008feb70; at != end; at += 4)
        title_function<void XB_STDCALL(uint32_t)>(0x0022d230)(title_global<uint32_t>(at));  // Vehicle_UpdateFromPhysics
}

namespace {

// 0x0083ed50 (0.0001) > |v| on the x87 (FABS + FCOMIP: NaN fails).
bool tiny(const float& v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    bits &= 0x7fffffff;
    float magnitude;
    std::memcpy(&magnitude, &bits, sizeof magnitude);
    return platform_x87_greater(title_global<float>(0x0083ed50), magnitude);
}

bool tiny3(const float* v) { return tiny(v[0]) && tiny(v[1]) && tiny(v[2]); }

void scale4(float* out, const float* v, float scale) {
    for (uint32_t i = 0; i < 4; ++i) out[i] = v[i] * scale;  // MULPS: all four lanes
}

void add_force_to_bodies(const float* dir, uint8_t* ent, const float* point, float scale) {
    using AddForce = void XB_REGS1 XB_CDECL(const float*, uint8_t*, const float*, float, uint32_t);
    title_function<AddForce>(0x00226890)(dir, ent, point, scale, 0x3f800000);  // Phys_AddForceToEntityBodies
}

}  // namespace

void XB_REGS1 XB_CDECL G_PhysicsLaunch(uint8_t* ent, const float* point, const float* dir, float scale,
                                       uint32_t local_point, uint32_t hit_location) {
    if (!ent || ent == title_global<uint8_t*>(title_global<uint32_t>(0x0095f534) + 8)) return;
    const uint32_t ragdoll = word_at(ent, 0x238);
    if (ragdoll) {
        // FUN_00225480: hit location (EAX) -> ragdoll body index.
        const uint32_t body = title_function<uint32_t XB_REGS1 XB_CDECL(uint32_t)>(0x00225480)(hit_location);
        uint8_t* const doll = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(ragdoll));
        if (!word_at(doll, 0x568)) {
            if (title_global<int32_t>(0x008fb484) >= 6) return;
            const TitleRegs regs{0, 0, ragdoll, 0, address(ent), body};
            const uint32_t args[1] = {ragdoll};
            g_ent_call(0x00234380, &regs, 1, args);  // Ragdoll_StartPhysics (EBX = 0, stdcall)
            if (word_at(ent, 0x240)) G_KillEntityOutright(word_at(ent, 0x240), 0, ent);
        }
        alignas(16) float force[4];
        if (tiny3(point)) {
            void* const physics = reinterpret_cast<void*>(static_cast<uintptr_t>(
                word_at(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(word_at(ent, 0x238))), 0x568)));
            scale4(force, dir, scale);
            Ragdoll_AddForce(force, physics, body);
            return;
        }
        void* const physics = reinterpret_cast<void*>(static_cast<uintptr_t>(
            word_at(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(word_at(ent, 0x238))), 0x568)));
        scale4(force, dir, scale);
        Ragdoll_AddForceAtPoint(force, 0, point, physics, body, 0x3f800000);
        return;
    }
    // FUN_00248850: the entity is a vehicle whose def has type 1.
    if (title_function<uint32_t XB_REGS1 XB_CDECL(uint8_t*)>(0x00248850)(ent) & 0xff) {
        const uint32_t vehicle = word_at(ent, 0x248);
        if (word_at(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(vehicle)), 0x300)) return;
        const TitleRegs regs{vehicle, 0, 0, 0, address(ent), 0};
        g_ent_call(0x00234560, &regs, 0, nullptr);  // VehiclePhys_GetOrCreate (ESI)
        return;
    }
    if (!(word_at(ent, 0x2a4) & 0x400000)) {
        title_function<void XB_CDECL(uint8_t*, uint32_t, uint32_t, uint32_t)>(0x002303f0)(ent, 0, 0x3f800000,
                                                                                        0xbf800000);  // PhysObj_Create
        if (!(word_at(ent, 0x2a4) & 0x400000)) return;
    }
    if (tiny3(point)) {
        // At the body's origin (matrix row 3).
        const float* const body = reinterpret_cast<const float*>(Phys_FindBodyForEntity(0, ent));
        alignas(16) float origin[4];
        std::memcpy(origin, body + 12, sizeof origin);
        add_force_to_bodies(dir, ent, origin, scale);
        return;
    }
    if (local_point & 0xff) {
        // A body-space point: origin + rows 0..2 times the point, all four lanes.
        const float* const body = reinterpret_cast<const float*>(Phys_FindBodyForEntity(0, ent));
        alignas(16) float m[16];
        std::memcpy(m, body, sizeof m);
        const float px = point[0];
        const float py = point[1];
        const float pz = point[2];
        alignas(16) float world[4];
        for (uint32_t i = 0; i < 4; ++i) {
            const float x = m[i] * px;
            const float a = m[12 + i] + x;
            const float y = m[4 + i] * py;
            const float b = a + y;
            const float z = m[8 + i] * pz;
            world[i] = b + z;
        }
        add_force_to_bodies(dir, ent, world, scale);
        return;
    }
    add_force_to_bodies(dir, ent, point, scale);
}

namespace {

struct BroFuncSlot {
    uint32_t name;    // string address
    uint32_t target;  // global the function pointer goes to
    bool warn;        // the original formats "BroFunc: %s, NOT FOUND" (and drops it)
};

// From the original's 44 inlined lookups, in order.
const BroFuncSlot kAnimScriptSlots[] = {
    {0x0083a884, 0x00987b70, true},
    {0x0083a870, 0x00987aa0, true},
    {0x0083a850, 0x00987ab0, true},
    {0x0083a834, 0x00987ac0, true},
    {0x0083a818, 0x00987ad0, true},
    {0x0083a800, 0x00987ae0, true},
    {0x0083a7e8, 0x00987af0, true},
    {0x0083a7d0, 0x00987b00, true},
    {0x0083a7b8, 0x00987b10, true},
    {0x0083a7a0, 0x00987b20, true},
    {0x0083a784, 0x00987b30, true},
    {0x0083a768, 0x00987b40, true},
    {0x0083a758, 0x00987b50, true},
    {0x0083a738, 0x00987b60, true},
    {0x0083a728, 0x00987b80, true},
    {0x0083a714, 0x00987b90, true},
    {0x0083a704, 0x00987bc0, true},
    {0x0083a6f4, 0x00987ba0, true},
    {0x0083a6e0, 0x00987bb0, true},
    {0x0083a6c8, 0x00987bd0, true},
    {0x0083a6ac, 0x00987be0, true},
    {0x0083a69c, 0x00987bf0, true},
    {0x0083a68c, 0x00987b74, false},
    {0x0083a678, 0x00987aa4, false},
    {0x0083a658, 0x00987ab4, false},
    {0x0083a63c, 0x00987ac4, false},
    {0x0083a620, 0x00987ad4, false},
    {0x0083a608, 0x00987ae4, false},
    {0x0083a5f0, 0x00987af4, false},
    {0x0083a5d8, 0x00987b04, false},
    {0x0083a5c0, 0x00987b14, false},
    {0x0083a5a8, 0x00987b24, false},
    {0x0083a58c, 0x00987b34, false},
    {0x0083a570, 0x00987b44, false},
    {0x0083a560, 0x00987b54, false},
    {0x0083a540, 0x00987b64, false},
    {0x0083a530, 0x00987b84, false},
    {0x0083a51c, 0x00987b94, false},
    {0x0083a50c, 0x00987bc4, false},
    {0x0083a4fc, 0x00987ba4, false},
    {0x0083a4e8, 0x00987bb4, false},
    {0x0083a4d0, 0x00987bd4, false},
    {0x0083a4b4, 0x00987be4, false},
    {0x0083a4a4, 0x00987bf4, false},
};

}  // namespace

void XB_CDECL G_InitAnimScripts() {
    // Each animation-script function is looked up by name hash in g_broFuncs (0x00927170, 52 pairs
    // {hash, function}); a missing one leaves 0.
    char message[0x80];
    for (const BroFuncSlot& slot : kAnimScriptSlots) {
        const char* const name = reinterpret_cast<const char*>(static_cast<uintptr_t>(slot.name));
        const uint32_t hash = Com_HashKeyNoCase(0, name);
        uint32_t function = 0;
        bool found = false;
        for (uint32_t i = 0; i < 0x34; ++i) {
            if (hash == title_global<uint32_t>(0x00927170 + i * 8)) {
                function = title_global<uint32_t>(0x00927174 + i * 8);
                found = true;
                break;
            }
        }
        if (!found && slot.warn)
            title_function<int XB_CDECL(char*, const char*, ...)>(0x001ee901)(
                message, reinterpret_cast<const char*>(static_cast<uintptr_t>(0x00832320)), name);  // _sprintf
        title_global<uint32_t>(slot.target) = function;
    }
}

void XB_CDECL G_FlushEntityListChanges() {
    // The entity list (0x00984cec, count 0x00984cf4) loses the queued removals (0x00987a78) by
    // swapping in its last element, then gains the queued additions (0x0097a640); both queues end empty.
    uint8_t* const list = reinterpret_cast<uint8_t*>(uintptr_t{0x00984cec});
    for (int32_t r = 0; r < title_global<int32_t>(0x00987a80); ++r) {
        uint32_t* items = title_global<uint32_t*>(0x00984cec);
        int32_t count = title_global<int32_t>(0x00984cf4);
        for (int32_t i = 0; i < count; ++i) {
            if (items[i] != title_global<uint32_t*>(0x00987a78)[r]) continue;
            if (i + 1 < count) {
                items[i] = items[count - 1];
                items = title_global<uint32_t*>(0x00984cec);
                --i;
            }
            --count;
            if (title_global<int32_t>(0x00984cf4)) --title_global<int32_t>(0x00984cf4);
        }
    }
    for (int32_t i = 0; i < title_global<int32_t>(0x0097a648); ++i)
        DynArray_PushBackAligned(list, title_global<uint32_t*>(0x0097a640) + i);
    DynArray_ResizeAligned(reinterpret_cast<uint8_t*>(uintptr_t{0x0097a640}), 0);
    DynArray_ResizeAligned(reinterpret_cast<uint8_t*>(uintptr_t{0x00987a78}), 0);
}

namespace {

// The index of ent in the remove queue (0x00987a78, count 0x00987a80), or -1.
int32_t find_queued_removal(uint32_t ent) {
    const int32_t count = title_global<int32_t>(0x00987a80);
    for (int32_t i = 0; i < count; ++i)
        if (title_global<uint32_t*>(0x00987a78)[i] == ent) return i;
    return -1;
}

}  // namespace

void XB_CDECL G_EntityList_QueueRemove(uint32_t ent) {
    const int32_t at = find_queued_removal(ent);
    if (at < 0) {
        DynArray_PushBackAligned(reinterpret_cast<uint8_t*>(uintptr_t{0x00987a78}), &ent);
        return;
    }
    // Already queued: the original swaps the remove queue's last entry in but counts the
    // addition queue (0x0097a648) down.
    const int32_t count = title_global<int32_t>(0x00987a80);
    uint32_t* const items = title_global<uint32_t*>(0x00987a78);
    if (at + 1 < count) items[at] = items[count - 1];
    if (title_global<int32_t>(0x0097a648)) --title_global<int32_t>(0x0097a648);
}

void XB_CDECL G_EntityList_QueueAdd(uint32_t ent) {
    // Despite the name: a queued removal of ent is cancelled, else ent is queued for addition
    // (0x0097a640).
    const int32_t at = find_queued_removal(ent);
    if (at < 0) {
        DynArray_PushBackAligned(reinterpret_cast<uint8_t*>(uintptr_t{0x0097a640}), &ent);
        return;
    }
    int32_t count = title_global<int32_t>(0x00987a80);
    uint32_t* const items = title_global<uint32_t*>(0x00987a78);
    if (at + 1 < count) {
        items[at] = items[count - 1];
        count = title_global<int32_t>(0x00987a80);
    }
    if (count) title_global<int32_t>(0x00987a80) = count - 1;
}

void XB_CDECL G_FindTeams() {
    // Links the entities with the same team string (+0x2f8) into team chains (+0x37c) under
    // the first one (master +0x380); the others get flag 0x10 (+0x2a4). Tram cars only lead
    // with spawn flag 8 and are unlinked as members.
    uint32_t bits[42];
    EntBits_Invert(bits, reinterpret_cast<const uint32_t*>(uintptr_t{0x0097a8c0}));
    uint32_t outer[4] = {address(bits), bits[0], 0, 0xffffffff};
    EntBits_IterNext(outer);
    const uint32_t tramcar = 0x0097a6e8;  // scr_const_func_tramcar
    for (;;) {
        if (outer[3] == 0xffffffff && outer[2] == 0xffffffff) return;
        uint8_t* const master = title_global<uint8_t*>(0x0097a968 + outer[3] * 8);
        const uint32_t team = word_at(master, 0x2f8);
        if (team && team + 6 && *reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(team + 6)) &&
            !(master[0x2a4] & 0x10) &&
            (word_at(master, 0x260) != title_global<uint32_t>(tramcar) || (master[0x2a0] & 8))) {
            uint32_t inner[4] = {outer[0], outer[1], outer[2], outer[3]};
            word_at(master, 0x380) = address(master);
            for (;;) {
                EntBits_IterNext(inner);
                if (inner[3] == 0xffffffff && inner[2] == 0xffffffff) break;
                uint8_t* const member = title_global<uint8_t*>(0x0097a968 + inner[3] * 8);
                const uint32_t other = word_at(member, 0x2f8);
                if (!other || !(other + 6) || !*reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(other + 6)))
                    continue;
                if (member[0x2a4] & 0x10) continue;
                if (!ScriptString_EqualsString(reinterpret_cast<const ScriptString*>(master + 0x2f8),
                                               reinterpret_cast<const ScriptString*>(member + 0x2f8)))
                    continue;
                word_at(member, 0x37c) = word_at(master, 0x37c);
                word_at(master, 0x37c) = address(member);
                const uint32_t flags = word_at(member, 0x2a4);
                word_at(member, 0x380) = address(master);
                word_at(member, 0x2a4) = flags | 0x10;
                if (word_at(member, 0x260) == title_global<uint32_t>(tramcar)) {
                    word_at(member, 0xd0) = 0;
                    SV_UnlinkEntity(member + 0xd0);
                }
            }
        }
        EntBits_IterNext(outer);
    }
}

void XB_CDECL G_FreeDroppedItems() {
    // 32 entity handles from 0x00985e30: each live one thinks next frame with think 0xc.
    for (uint32_t i = 0; i < 32; ++i) {
        const uint32_t value = title_global<uint32_t>(0x00985e30 + i * 4);
        const uint32_t index = value & 0xfff;
        if (index >= 0x540 || (value >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) continue;
        uint8_t* const ent = title_global<uint8_t*>(0x0097a968 + index * 8);
        if (!ent) continue;
        word_at(ent, 0x32c) = 0xc;
        word_at(ent, 0x328) = title_global<uint32_t>(0x0098540c) + 1;
    }
}

namespace {

using StrncpyFn = char* XB_CDECL(char*, const char*, uint32_t);

const char* command_arg(uint32_t index) {
    if (index < title_global<uint32_t>(0x00934248))  // cmd_argc
        return title_global<const char*>(0x0092df90 + index * 4);  // cmd_argv
    return reinterpret_cast<const char*>(uintptr_t{0x0082dfeb});  // ""
}

}  // namespace

char* XB_THISCALL ConcatArgs(int32_t start) {
    // The arguments from start on, joined by spaces into 0x0098b858 (256 bytes; an argument
    // that would not fit ends it).
    char* const out = reinterpret_cast<char*>(uintptr_t{0x0098b858});
    const int32_t count = title_global<int32_t>(0x00934248);
    int32_t length = 0;
    for (int32_t i = start; i < count; ++i) {
        char arg[0x100];
        title_function<StrncpyFn>(0x001eed80)(arg, command_arg(static_cast<uint32_t>(i)), 0xff);  // _strncpy
        arg[0xff] = 0;
        const int32_t size = static_cast<int32_t>(std::strlen(arg));
        if (size + length >= 0xff) break;
        std::memcpy(out + length, arg, static_cast<size_t>(size));
        length += size;
        if (i != count - 1) out[length++] = ' ';
    }
    out[length] = 0;
    return out;
}

uint32_t XB_CDECL ConsoleCommand() {
    // The game's console commands ("entitylist", "listvehicles") are accepted and do nothing.
    char cmd[0x80];
    title_function<StrncpyFn>(0x001eed80)(cmd, command_arg(0), 0x7f);  // _strncpy
    cmd[0x7f] = 0;
    if (!Q_stricmpn(cmd, reinterpret_cast<const char*>(uintptr_t{0x00839d08}), 99999)) return 1;
    return Q_stricmpn(cmd, reinterpret_cast<const char*>(uintptr_t{0x00839cf8}), 99999) == 0;
}

void XB_CDECL G_SetupVehiclePool(uint32_t slots) {
    // (Re)allocates the vehicle slots (g_vehicles 0x0095f694, 0x340 bytes each, count
    // g_vehicleSlotCount 0x00986000) and 0x0095f690 (0xe0 each), keeping the used old slots
    // (+0x170 set) packed at the front.
    uint8_t* const old = title_global<uint8_t*>(0x0095f694);
    uint8_t* saved = nullptr;
    int32_t saved_count = 0;
    if (old) {
        const int32_t count = title_global<uint16_t>(0x00986000);
        if (count > 0) {
            int32_t used = 0;
            for (int32_t i = 0; i < count; ++i)
                if (*reinterpret_cast<const uint32_t*>(old + 0x170 + i * 0x340)) ++used;
            if (used) {
                saved_count = count;
                saved = Heap_AllocAligned(0x10, static_cast<uint32_t>(count) * 0x340);
                platform_rep_movs(saved, title_global<uint8_t*>(0x0095f694),
                                  static_cast<uint32_t>(title_global<uint16_t>(0x00986000)) * 0x340);
            }
        }
        // The pointer freed is the one read last (reloaded after the copy).
        uint8_t* const current = saved ? title_global<uint8_t*>(0x0095f694) : old;
        if (current) {
            Z_Free(current);
            title_global<uint32_t>(0x0095f694) = 0;
            title_global<uint16_t>(0x00986000) = 0;
        }
    }
    if (title_global<uint32_t>(0x0095f690)) {
        Z_Free(title_global<uint8_t*>(0x0095f690));
        title_global<uint32_t>(0x0095f690) = 0;
    }
    title_global<uint16_t>(0x00986000) = static_cast<uint16_t>(slots);
    if (static_cast<uint16_t>(slots)) {
        uint8_t* const vehicles =
            Heap_AllocAligned(0x10, static_cast<uint32_t>(title_global<uint16_t>(0x00986000)) * 0x340);
        title_global<uint8_t*>(0x0095f694) = vehicles;
        std::memset(vehicles, 0, static_cast<uint32_t>(title_global<uint16_t>(0x00986000)) * 0x340);
        uint8_t* const extra = Heap_AllocAligned(0x10, static_cast<uint32_t>(title_global<uint16_t>(0x00986000)) * 0xe0);
        title_global<uint8_t*>(0x0095f690) = extra;
        std::memset(extra, 0, static_cast<uint32_t>(title_global<uint16_t>(0x00986000)) * 0xe0);
    }
    Vehicle_ResetPool();
    if (!saved) return;
    uint8_t* to = title_global<uint8_t*>(0x0095f694);
    for (int32_t i = 0; i < saved_count; ++i) {
        const uint8_t* const from = saved + i * 0x340;
        if (!*reinterpret_cast<const uint32_t*>(from + 0x170)) continue;
        platform_rep_movs(to, from, 0x340);
        to += 0x340;
    }
    Z_Free(saved);
}

PLATFORM_ASM_ENTRY(G_EntityList_Add_entry,
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(G_EntityList_Add) "addl $8, %esp\n"
                   "ret")

void XB_CDECL G_EntityList_Add(uint8_t* ent, uint8_t* list) {
    // An entity without a handle (+0x224) gets one from 0x0097a8c0 (the handle table's object
    // slot set when its index is below 0x540); then it is appended to the list (count +0x2aac,
    // at most 0x1000 entries from +0x2ab0) and remembers its index there (+0x228).
    if (word_at(ent, 0x224)) return;
    uint32_t handle = 0;
    const uint32_t value = *EntHandleTable_Alloc(&title_global<uint32_t>(0x0097a8c0), &handle);
    word_at(ent, 0x224) = value;
    const uint32_t index = value & 0xfff;
    if (index < 0x540) title_global<uint32_t>(0x0097a968 + index * 8) = address(ent);
    const int32_t count = static_cast<int32_t>(word_at(list, 0x2aac));
    if (count < 0x1000) {
        word_at(list, 0x2ab0 + count * 4) = address(ent);
        ++word_at(list, 0x2aac);
    }
    word_at(ent, 0x228) = word_at(list, 0x2aac) - 1;
}

uint32_t XB_CDECL G_FindHintStringIndex(int32_t* out, const char* text) {
    // The 32 hint-string configstrings (0x273..): the first equal one, else the first empty
    // one gets the text. *out = its index (-1 and 0 when all are taken by others).
    char buffer[0x100];
    for (int32_t i = 0; i < 0x20; ++i) {
        if (i < -0x273 || i >= 0x18d)
            title_function<void XB_CDECL(int32_t, const char*, int32_t)>(0x002938f0)(
                1, reinterpret_cast<const char*>(uintptr_t{0x00832d34}), i + 0x273);  // __break
        const uint32_t cs = title_global<uint32_t>(0x009656ac + i * 4);
        title_function<char* XB_CDECL(char*, const char*, uint32_t)>(0x001eed80)(
            buffer, cs ? reinterpret_cast<const char*>(static_cast<uintptr_t>(cs + 6)) : nullptr, 0xff);  // _strncpy
        buffer[0xff] = 0;
        if (!buffer[0]) {
            SV_SetConfigstring(i + 0x273, text);
            *out = i;
            return 1;
        }
        if (!std::strcmp(text, buffer)) {
            *out = i;
            return 1;
        }
    }
    *out = -1;
    return 0;
}

void XB_CDECL G_FreeAnimTrees() {
    // Each anim tree's notetracks (from 1), then the string table when the game loaded it.
    for (int32_t i = 1; i < title_global<int32_t>(0x00927098); ++i)
        title_function<void XB_REGS1 XB_CDECL(uint32_t)>(0x002f6840)(static_cast<uint32_t>(i));  // AnimTree_FreeNotetracks
    if (title_global<int32_t>(0x0095f5a0) == 1) {
        title_function<void XB_CDECL(uint32_t)>(0x000428d0)(title_global<uint32_t>(0x0095f59c));  // Z_Free
        title_global<uint32_t>(0x0095f59c) = 0;
        title_global<uint32_t>(0x0095f5a0) = 0;
    }
}

void XB_CDECL G_TrimCorpseQueue() {
    // The corpse limit (0x00950c5c, clamped to 1..16) shrank below the count (0x00985418):
    // the corpses past it (g_actorCorpses, 0x1c each, handle first) are freed.
    int32_t limit = title_global<int32_t>(0x00950c5c);
    if (limit < 1)
        limit = 1;
    else if (limit > 0x10)
        limit = 0x10;
    if (limit == title_global<int32_t>(0x00985418)) return;
    for (int32_t i = limit; i < title_global<int32_t>(0x00985418); ++i) {
        uint32_t& handle = title_global<uint32_t>(0x009881c8 + i * 0x1c);
        if (!handle) continue;
        const uint32_t index = handle & 0xfff;
        uint32_t ent = 0;
        if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
            ent = title_global<uint32_t>(0x0097a968 + index * 8);
        title_function<void XB_CDECL(uint32_t, uint32_t)>(0x002be420)(ent, 0);  // G_FreeEntity
        handle = 0;
    }
    title_global<int32_t>(0x00985418) = limit;
}

void XB_CDECL G_FreeStringTable(uint32_t keep) {
    // The string table (0x0095f59c) when the game loaded it (0x0095f5a0 == 1): freed unless
    // `keep`; the owner is cleared either way.
    if (title_global<int32_t>(0x0095f5a0) != 1) return;
    if (!keep) {
        title_function<void XB_CDECL(uint32_t)>(0x000428d0)(title_global<uint32_t>(0x0095f59c));  // Z_Free
        title_global<uint32_t>(0x0095f59c) = 0;
    }
    title_global<uint32_t>(0x0095f5a0) = 0;
}

void XB_CDECL SaveRegisteredItems() {
    // The 0x88 item-registered flags (0x00950230) as hex digits, four flags each (lowest
    // first), into configstring 8; the dirty flag (0x00986004) cleared.
    char text[0x104];
    title_global<uint32_t>(0x00986004) = 0;
    uint32_t length = 0;
    uint32_t bits = 0;
    uint32_t count = 0;
    for (uint32_t i = 0; i < 0x88; ++i) {
        if (title_global<uint32_t>(0x00950230 + i * 4)) bits += 1u << count;
        if (++count == 4) {
            text[length++] = static_cast<char>(bits >= 10 ? 'a' + bits - 10 : '0' + bits);
            bits = 0;
            count = 0;
        }
    }
    if (count) text[length++] = static_cast<char>(bits >= 10 ? 'a' + bits - 10 : '0' + bits);
    text[length] = 0;
    SV_SetConfigstring(8, text);
}

uint32_t XB_CDECL G_GetAnimFlag0(uint32_t anim) {
    // Anim handle = tree (high word) and anim (low word) of rm_animTrees (0x0095f524), each
    // index past its count reading entry 0. Bit 0 of the anim's flags (+0x34) for parent nodes
    // (+0x20 set), else of its loaded clip's (+0x34; 0 without a clip).
    const uint32_t* const list = *reinterpret_cast<const uint32_t* const*>(title_global<const uint8_t*>(0x0095f524) + 4);
    uint32_t tree = anim >> 16;
    if (tree >= list[0]) tree = 0;
    const uint32_t* const record = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(list[1] + tree * 12 + 4));
    uint32_t index = anim & 0xffff;
    if (index >= record[0]) index = 0;
    uint8_t* const node = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(record[1] + index * 0x38));
    if (*reinterpret_cast<const uint16_t*>(node + 0x20)) return node[0x34] & 1;
    if (!word_at(node, 0x24)) AnimData_EnsureLoaded(node);
    const uint8_t* const clip = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(word_at(node, 0x24)));
    if (!clip) return 0;
    return clip[0x34] & 1;
}

void XB_CDECL G_FindSpawnFunc() {
    // Fills the spawn map (0x00983e1c): each g_spawns entry's name hash (8-byte entries, until
    // a zero hash) mapped to its index (node +0xc).
    uint32_t index = 0;
    for (uint32_t entry = 0x009885e0; title_global<uint32_t>(entry); entry += 8, ++index) {
        uint8_t* const map = &title_global<uint8_t>(0x00983e1c);
        uint32_t node = 0;
        UintHashMap_Find(&node, &title_global<uint32_t>(entry), map);
        if (node == title_global<uint32_t>(0x00983e24)) {
            const uint32_t pair[2] = {title_global<uint32_t>(entry), 0};
            uint32_t result[2] = {0, 0};
            node = *HashMap_Insert_10(map, result, pair);
        }
        *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(node + 0xc)) = index;
    }
}

void XB_CDECL G_InitAnimTrees(uint32_t arg) {
    // The interface's anim-tree load (slot 0x868 of 0x0095f49c), the tree count, the anim
    // scripts, the tree setup (callback 0x0024a050) and the "generic_human" tree (0x00987a90).
    title_function<void XB_CDECL()>(*reinterpret_cast<const uint32_t*>(
        static_cast<uintptr_t>(title_global<uint32_t>(0x0095f49c) + 0x868)))();
    const uint32_t* const list = *reinterpret_cast<const uint32_t* const*>(title_global<const uint8_t*>(0x0095f524) + 4);
    title_global<uint32_t>(0x00927098) = list[0];  // g_numAnimTrees
    title_function<void XB_CDECL()>(0x0024a070)();  // G_InitAnimScripts
    title_function<void XB_CDECL()>(0x0024abb0)();  // G_InitTurretAnimScripts
    title_function<void XB_CDECL(uint32_t, uint32_t)>(0x002fe200)(0x0024a050, arg);  // G_SetupAnimTrees
    const char* const name = reinterpret_cast<const char*>(uintptr_t{0x0083a894});  // "generic_human"
    uint8_t* const tree = G_FindAnimTreeByName(name);
    if (!tree)
        title_function<void XB_CDECL(const char*, ...)>(0x00248af0)(
            reinterpret_cast<const char*>(uintptr_t{0x0083a8a4}), name);  // G_Error
    title_global<uint32_t>(0x00987a90) = address(tree);
}

void XB_CDECL G_SyncRegisteredItems() {
    // 0x00985414 = level.time; the registered items' configstring when they changed.
    title_global<uint32_t>(0x00985414) = title_global<uint32_t>(0x0098540c);
    if (title_global<uint32_t>(0x00986004)) SaveRegisteredItems();
}

void XB_CDECL G_StartMissionEndFade() {
    // With the player alive (and 0x0095bf9c clear): screen and sound fade-out commands timed
    // by 0x00985ec8, the end time in 0x00985e28, g_reloading 4 (16 more with 0x00985eb4).
    using Va = char* XB_CDECL(const char*, ...);
    const uint8_t* const player = G_FindEntityByIntField(&title_global<uint8_t>(0x0097a8c0), 0x260,
                                                         title_global<uint32_t>(0x0097a75c));
    if (*reinterpret_cast<const int32_t*>(player + 0x350) <= 0) return;
    if (title_global<uint32_t>(0x0095bf9c)) return;
    const char* text = title_function<Va>(0x002a93d0)(reinterpret_cast<const char*>(uintptr_t{0x0083b37c}),
                                                      title_global<int32_t>(0x0098540c) + 250,
                                                      title_global<int32_t>(0x00985ec8) + 750);  // "scr_fade 1 %i %i"
    SV_GameSendServerCommand(text, 0);
    text = title_function<Va>(0x002a93d0)(reinterpret_cast<const char*>(uintptr_t{0x0083bdb4}),
                                          title_global<int32_t>(0x00985ec8) + 1000);  // "snd_fade 0 %d"
    SV_GameSendServerCommand(text, 0);
    title_global<int32_t>(0x00985e28) = title_global<int32_t>(0x0098540c) + title_global<int32_t>(0x00985ec8) + 1000;
    using CvarSet = uint32_t XB_CDECL(const char*, const char*, uint32_t);
    const char* const name = reinterpret_cast<const char*>(uintptr_t{0x0082c89c});  // "g_reloading"
    text = title_function<Va>(0x002a93d0)(reinterpret_cast<const char*>(uintptr_t{0x008325b8}), 4);  // "%d"
    title_function<CvarSet>(0x0029c030)(name, text, 1);  // Cvar_Set2
    if (title_global<uint32_t>(0x00985eb4)) {
        text = title_function<Va>(0x002a93d0)(reinterpret_cast<const char*>(uintptr_t{0x008325b8}), 16);
        title_function<CvarSet>(0x0029c030)(name, text, 1);
    }
}

void XB_CDECL G_ShutdownGame(uint32_t restart) {
    // Q3's G_ShutdownGame: every entity, the vehicle pool and the second vehicle block
    // (0x0095f690), the vehicle defs; without a restart also the corpse queue and anim trees.
    title_function<void XB_STDCALL(uint32_t)>(0x002c3870)(title_global<uint32_t>(0x0095f534));  // G_FreeAllEntities
    if (title_global<uint32_t>(0x0095f694)) {
        title_function<void XB_CDECL(uint32_t)>(0x000428d0)(title_global<uint32_t>(0x0095f694));  // Z_Free
        title_global<uint32_t>(0x0095f694) = 0;
        title_global<uint16_t>(0x00986000) = 0;
    }
    if (title_global<uint32_t>(0x0095f690)) {
        title_function<void XB_CDECL(uint32_t)>(0x000428d0)(title_global<uint32_t>(0x0095f690));  // Z_Free
        title_global<uint32_t>(0x0095f690) = 0;
    }
    Vehicle_FreeDefs();
    if (restart) return;
    for (uint32_t corpse = 0x009881c8; corpse < 0x00988388; corpse += 0x1c) title_global<uint32_t>(corpse) = 0;
    G_FreeAnimTrees();
}

PLATFORM_ASM_ENTRY(G_SetFog_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(G_SetFog) "addl $4, %esp\n"
                   "ret")

void XB_CDECL G_SetFog(const char* fog) {
    // "fog <text>" to every client; then the fog range (0x00985fcc, its square 0x00985fd0;
    // FLT_MAX without) from seven floats: when the third is >= 1, start + (end - start) * 0.828.
    using Va = char* XB_CDECL(const char*, ...);
    SV_GameSendServerCommand(title_function<Va>(0x002a93d0)(reinterpret_cast<const char*>(uintptr_t{0x0083bd50}), fog), 0);
    float v[7];  // as the original's frame: v[0] first, v[1] second, v[2] third
    title_global<float>(0x00985fcc) = title_global<float>(0x0082eed0);
    title_global<float>(0x00985fd0) = title_global<float>(0x0082eed0);
    const int32_t n = title_function<int32_t XB_CDECL(const char*, const char*, ...)>(0x001f07e1)(
        fog, reinterpret_cast<const char*>(uintptr_t{0x0083bd38}), &v[0], &v[1], &v[2], &v[3], &v[4], &v[5],
        &v[6]);  // sscanf "%f %f %f %f %f %f %f"
    if (n != 7) return;
    if (!(v[2] >= title_global<float>(0x0083eb20))) return;
    const float start = v[0];
    const float range = (v[1] - start) * title_global<float>(0x0083ef80) + start;
    title_global<float>(0x00985fcc) = range;
    title_global<float>(0x00985fd0) = range * range;
}

}  // namespace game
