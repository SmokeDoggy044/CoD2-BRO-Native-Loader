#include "g_main.h"

#include <cstring>

#include "actor.h"
#include "cgame.h"
#include "dobj.h"
#include "g_script.h"
#include "g_utils.h"
#include "g_vehicle.h"
#include "qcommon.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
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

template <typename T>
T& field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

uint8_t* entity_from_handle(uint32_t handle) {
    const uint32_t index = handle & 0xfff;
    if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
        return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x0097a968 + index * 8)));
    return nullptr;
}

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

void XB_CDECL G_InitSpawnFuncMap() {
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

void XB_CDECL G_ProcessCheckpointTriggers() {
    // This frame's trigger pairs (0x0098606c, {self, other handle, self/other spawn ids +0xc4};
    // count 0x0098706c), always the first, replaced by the last: still valid pairs notify
    // "trigger" (other entity number), checkpoints (classname "checkpoint") save first
    // (G_SaveCheckpoint with the target name, 0x0095f4e0). Count 0 after.
    int32_t& count = title_global<int32_t>(0x0098706c);
    uint32_t* const pair = &title_global<uint32_t>(0x0098606c);
    for (int32_t i = 0; i < count;) {
        uint8_t* const self = entity_from_handle(pair[0]);
        if (self && field<uint32_t>(self, 0xc4) == pair[2]) {
            uint8_t* const other = entity_from_handle(pair[1]);
            if (other && field<uint32_t>(other, 0xc4) == pair[3]) {
                if (ScriptString_Equals(reinterpret_cast<const ScriptString*>(self + 0x264),
                                        reinterpret_cast<const char*>(uintptr_t{0x008353b8}))) {
                    const uint32_t name = field<uint32_t>(self, 0x26c);
                    const TitleRegs regs{name ? name + 6 : 0, title_global<uint32_t>(0x0095f4e0), 0, 0, 0, 0};
                    const uint32_t args[1] = {0};
                    g_ent_call(0x002cd750, &regs, 1, args);  // G_SaveCheckpoint (stdcall)
                }
                uint32_t number = field<uint32_t>(other, 0x224);
                const TitleRegs regs{0, address(other), 0, static_cast<uint32_t>(i), address(pair), address(self)};
                const uint32_t args[2] = {title_global<uint32_t>(0x0097a818), address(&number)};
                g_ent_call(0x000265a0, &regs, 2, args);  // G_NotifyWithEntity "trigger"
            }
        }
        --count;
        const uint32_t* const last = pair + count * 4;
        pair[0] = last[0];
        pair[1] = last[1];
        pair[2] = last[2];
        pair[3] = last[3];
    }
    count = 0;
}

void XB_CDECL G_RegisterCvars() {
    // Q3's G_RegisterCvars over gameCvarTable (0x008da8e8, 0x18 bytes: vmCvar, name, default,
    // flags, modification count; count 0x008dae40), then the dead/victory screen cvars cleared.
    for (int32_t i = 0; i < title_global<int32_t>(0x008dae40); ++i) {
        const uint32_t entry = 0x008da8e8 + i * 0x18;
        using CvarGet = uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t);
        const uint32_t cvar = title_function<CvarGet>(0x0029c2d0)(title_global<uint32_t>(entry + 4),
                                                                  title_global<uint32_t>(entry + 8),
                                                                  title_global<uint32_t>(entry + 12));  // Cvar_Get
        const uint32_t vm = title_global<uint32_t>(entry);
        if (vm) {
            uint32_t* const v = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(vm));
            v[0] = static_cast<uint32_t>(static_cast<int32_t>(cvar - 0x00940a10) / 0x2c);
            v[1] = 0xffffffffu;
            const TitleRegs regs{0, vm, 0, 0, 0, 0};
            g_ent_call(0x00296ec0, &regs, 0, nullptr);  // Cvar_Update (EBX)
        }
        const uint32_t again = title_global<uint32_t>(entry);
        if (again) title_global<uint32_t>(entry + 16) = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(again + 4));
    }
    static const uint32_t kCleared[] = {0x00948898, 0x009482f8, 0x0094b780, 0x00948538};
    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t vm = kCleared[i];
        title_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x0029c030)(
            title_global<uint32_t>(0x00940a10 + title_global<uint32_t>(vm) * 0x2c), 0x0082dfeb, 1);  // Cvar_Set2 ""
        const TitleRegs regs{0, vm, 0, 0, 0, 0};
        g_ent_call(0x00296ec0, &regs, 0, nullptr);  // Cvar_Update
    }
}

uint32_t XB_REGS3 XB_CDECL G_FreeFarthestDroppedItem(int32_t index, uint32_t, uint32_t slot) {
    // Dropped item handles of a player slot (0x00985e30, 32 per slot): `index` < 0 picks the one
    // farthest (squared, packed) from the slot's player entity (0x0095f534 + 4 + slot * 4); any
    // stale handle returns early. The chosen item frees itself next frame (think 0xc).
    uint32_t chosen = 0;
    if (index < 0) {
        float best = title_global<float>(0x0083eb34);  // -1
        for (uint32_t i = 0; i < 0x20; ++i) {
            const uint8_t* const item = entity_from_handle(title_global<uint32_t>(0x00985e30 + (slot * 0x20 + i) * 4));
            if (!item) return i;
            const uint8_t* const player = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(
                *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(title_global<uint32_t>(0x0095f534) + slot * 4 + 4))));
            const float* const a = reinterpret_cast<const float*>(player + 0x140);
            const float* const b = reinterpret_cast<const float*>(item + 0x140);
            float d[4];
            for (uint32_t k = 0; k < 4; ++k) d[k] = a[k] - b[k];
            float m[4];
            for (uint32_t k = 0; k < 4; ++k) m[k] = d[k] * d[k];
            platform_keep(m[3]);
            const float dist = m[0] + m[1] + m[2];
            if (dist > best) {
                best = dist;
                chosen = i;
            }
        }
    } else {
        if (!entity_from_handle(title_global<uint32_t>(0x00985e30 + (slot * 0x20 + index) * 4))) return static_cast<uint32_t>(index);
        chosen = static_cast<uint32_t>(index);
    }
    uint8_t* const item = entity_from_handle(title_global<uint32_t>(0x00985e30 + (slot * 0x20 + chosen) * 4));
    field<uint32_t>(item, 0x32c) = 0xc;
    field<int32_t>(item, 0x328) = title_global<int32_t>(0x0098540c) + 1;
    return chosen;
}

// The menu text setters keep a stack address in heap word 0x100006a8: every call outside the
// success loop runs at the original's ESP (entry - 0x94), the C frame below a gap.
PLATFORM_ASM_ENTRY(G_CheckMissionEnd_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x30000")
                   "pushl %edx\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(G_CheckMissionEnd) "addl $4, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL G_CheckMissionEnd(uint32_t entry_esp) {
    // Once the mission-end time (0x00985e28) has passed: the first frame stops sounds and rumble
    // and arms 0x0095f67c; the next one shows the outcome. Success (0x00985eb8): the success
    // menu (6) over a full-screen texture of the campaign (suc_afr / suc_sic / suc_eur in the
    // language folder) in a frame loop of its own that never returns (pauses with the pad
    // pulled, A resumes); failure (0x00985ebc): effects freed and the failed menu with its text;
    // else by 0x0095bf9c: "reboot" (4), G_EndGame (16) or "opendeadscreen".
    using CvarSet = uint32_t XB_CDECL(const char*, const char*, uint32_t);
    const auto text = [](uint32_t address) { return reinterpret_cast<const char*>(static_cast<uintptr_t>(address)); };
    const auto at = [](uint32_t value) { return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(value)); };
    // The front end's menu stack (0x0095f8fc): +4 -> +0x18 = the success/fail menu; slot 3 opens a menu.
    const auto menu = []() { return title_global<uint32_t>(word_at(title_global<uint8_t*>(0x0095f8fc), 4) + 0x18); };
    const uint32_t top = entry_esp - 0x94;  // sub esp,0x84; four pushes
    const auto call = [top](uint32_t function, const TitleRegs& regs, uint32_t count, const uint32_t* args) {
        return title_call_at_regs(top, function, &regs, count, args);
    };
    const auto cvar_paused = [&call](uint32_t value) {
        const uint32_t args[3] = {0x0082ca00, value, 1};
        call(0x0029c030, TitleRegs{}, 3, args);  // Cvar_Set2("cl_paused", value, 1)
    };
    const auto open_menu = [&at, &call]() {
        uint8_t* const root = title_global<uint8_t*>(0x0095f8fc);
        const uint32_t six = 6;
        call(word_at(at(word_at(root, 0)), 0xc), TitleRegs{0, 0, address(root), 0, 0, 0}, 1, &six);  // thiscall
    };
    const auto stop_sounds = [&call]() {
        call(0x002c8d60, TitleRegs{0, title_global<uint32_t>(0x0095f4c4), 0, 0, 0, 0}, 0, nullptr);  // S_StopAllSounds (EBX)
        call(0x00298b20, TitleRegs{title_global<uint32_t>(0x0095f5c4), 0, 0, 0, 0, 0}, 0, nullptr);  // rumble stop (EAX)
    };
    if (title_global<uint32_t>(0x00985eb0)) {
        title_global<uint32_t>(0x00985eb0) = 0;
        call(0x002512d0, TitleRegs{}, 0, nullptr);  // G_StartMissionEndFade
    }
    const uint32_t kind = title_global<uint32_t>(0x0095bf9c);
    if (!kind) return;
    const int32_t end_time = title_global<int32_t>(0x00985e28);
    if (!end_time || end_time >= title_global<int32_t>(0x0098540c)) return;
    if (!title_global<uint8_t>(0x0095f67c)) {
        const uint32_t list = 0x008dd1d8;
        call(0x002fc2a0, TitleRegs{}, 1, &list);  // stdcall
        stop_sounds();
        title_global<uint8_t>(0x0095f509) = 1;
        title_global<uint8_t>(0x0095f67c) = 1;
        return;
    }
    title_global<uint8_t>(0x0095f67c) = 0;
    if (title_global<uint32_t>(0x00985eb8)) {
        stop_sounds();
        cvar_paused(0x0082e058);  // "1"
        title_global<uint8_t>(0x0095f919) = 1;
        word_at(at(menu()), 0x58) = 0;
        call(0x0030cfb0, TitleRegs{0, 0, 0, 0, menu(), 0}, 0, nullptr);  // MissionSuccessFailMenu_ClearTexts (ESI)
        open_menu();
        uint32_t language = 0x0082d014;  // "en"
        switch (title_global<int32_t>(0x0095f5dc)) {  // g_languageIndex, case table 0x0025308c
        case 1: language = 0x0082d010; break;  // "de"
        case 2: language = 0x0082d008; break;  // "fr"
        case 3: language = 0x0082d004; break;  // "es"
        case 4: language = 0x0082d00c; break;  // "it"
        default: break;
        }
        using Va = char* XB_CDECL(const char*, ...);
        const int32_t chapter = title_global<int32_t>(0x008dce48);  // g_currentChapter
        uint32_t format = 0x0083b364;  // "textures\%s\suc_eur.dds"
        if (chapter > 1 && chapter < 10) format = chapter <= 6 ? 0x0083b34c : 0x0083b334;  // suc_afr / suc_sic
        const char* const path = title_function<Va>(0x002a93d0)(text(format), text(language));
        const int32_t file = title_function<int32_t XB_CDECL(uint32_t, const char*)>(0x000c9d40)(
            title_global<uint32_t>(0x0095f6f4), path);  // File_OpenRead
        const uint32_t size = title_function<uint32_t XB_CDECL(int32_t)>(0x000c9d10)(file);  // File_GetSize
        const uint32_t padded = (size + 0x7fff) & 0xffff8000u;
        const uint32_t data =
            title_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t)>(0x00093950)(padded, 0x80, 0x30000);  // Mem_AlignedAlloc
        title_function<int32_t XB_CDECL(int32_t, uint32_t, uint32_t, uint32_t)>(0x000caff0)(file, data, 0, padded);  // File_ReadAt
        uint32_t name = 0;
        title_function<uint32_t* XB_THISCALL(uint32_t*, const char*)>(0x00011060)(&name, text(0x0083b32c));  // HashStr_Init "texSuc"
        const uint32_t texture = title_function<uint32_t XB_CDECL(uint32_t*, uint32_t, uint32_t, uint32_t)>(0x000635a0)(
            &name, 0, data, size);  // nglLoadTexture
        uint32_t quad[0x19] = {};
        title_function<void XB_CDECL(uint32_t*)>(0x0005ddb0)(quad);  // nglInitQuad
        title_function<void XB_CDECL(uint32_t*, float, float, float, float)>(0x0005ded0)(quad, 0.0f, 0.0f, 640.0f,
                                                                                         480.0f);  // nglSetQuadRect
        title_function<void XB_CDECL(uint32_t*, uint32_t)>(0x0005de20)(quad, texture);  // nglSetQuadTexture
        title_function<void XB_CDECL(uint32_t*, float)>(0x0005df20)(quad, 300.0f);    // nglSetQuadZ
        using PadCreate = uint32_t XB_CDECL();
        for (;;) {
            title_function<uint32_t XB_CDECL()>(0x000ca690)();  // File_PumpRequests
            {
                const TitleRegs pad{0, 0, title_function<PadCreate>(0x00055e40)(), 0, 0, 0};
                g_ent_call(0x00055e70, &pad, 0, nullptr);  // IN_PadUpdate (ECX)
            }
            const uint32_t controller = title_global<uint32_t>(0x008d50d8);
            const uint32_t pad = title_function<PadCreate>(0x00055e40)();
            if (!(title_function<uint8_t XB_THISCALL(uint32_t, uint32_t)>(0x000560e0)(pad, controller))) {  // IN_PadIsConnected
                title_global<uint8_t>(0x008d50dc) = 0;
                if (!title_global<uint32_t>(0x0095f8f4) || title_global<uint8_t>(0x0095f538)) continue;
                const TitleRegs pause{title_global<uint32_t>(0x0095f4c4), 0, 0, 0, 0, 0};
                g_ent_call(0x002b6be0, &pause, 0, nullptr);  // S_PauseAllChannels (EAX)
                title_function<void XB_STDCALL(uint32_t)>(0x002c8f60)(title_global<uint32_t>(0x0095f4c0));  // S_CheckWaveBankNotifies
                const uint32_t paused = word_at(title_global<uint8_t*>(0x0093fcd8), 0x20);
                title_global<uint8_t>(0x0095f539) = paused != 0;
                title_function<CvarSet>(0x0029c030)(text(0x0082ca00), text(0x0082e058), 1);  // cl_paused "1"
                const TitleRegs draw{0, 0, 0, 0, 0x0095f8d0, paused};
                g_ent_call(0x0030f190, &draw, 0, nullptr);  // a paused frame (ESI = g_uiRoot)
                title_global<uint8_t>(0x0095f538) = 1;
                continue;
            }
            title_global<uint8_t>(0x008d50dc) = 1;
            if (title_global<uint8_t>(0x0095f538)) {
                const uint32_t pressed_controller = title_global<uint32_t>(0x008d50d8);
                const uint32_t pressed_pad = title_function<PadCreate>(0x00055e40)();
                if (!(title_function<uint8_t XB_THISCALL(uint32_t, uint32_t, uint32_t)>(0x00056580)(
                        pressed_pad, pressed_controller, 0xe)))  // IN_PadButtonPressed
                    continue;
                const TitleRegs resume{title_global<uint32_t>(0x0095f4c4), 0, 0, 0, 0, 0};
                g_ent_call(0x002b6ba0, &resume, 0, nullptr);  // S_ResumeAllChannels (EAX)
                title_function<CvarSet>(0x0029c030)(
                    text(0x0082ca00), text(title_global<uint8_t>(0x0095f539) ? 0x0082e058 : 0x0082e098), 1);  // cl_paused
                title_global<uint8_t>(0x0095f538) = 0;
                const uint32_t consume_pad = title_function<PadCreate>(0x00055e40)();
                const TitleRegs consume{0, consume_pad, 0, 0, 0, title_global<uint32_t>(0x008d50d8)};
                g_ent_call(0x00011b80, &consume, 0, nullptr);  // IN_PadConsumeAllPressed (EDI)
            }
            using Set = void XB_CDECL(uint32_t);
            title_function<void XB_CDECL()>(0x00064690)();  // nglBeginFrame
            title_function<Set>(0x00062fc0)(0);  // nglPushScene
            title_function<Set>(0x0005e430)(0);  // nglSetClearFlags
            title_function<Set>(0x0005e4b0)(0);  // nglSetSceneDepthTest
            title_function<Set>(0x0005e490)(0);  // nglSetSceneDepthEnable
            title_function<void XB_CDECL(const uint32_t*)>(0x0005e270)(quad);  // nglListAddQuad
            title_function<void XB_CDECL()>(0x0005e730)();                     // nglPopScene
            title_function<Set>(0x00062fc0)(0);
            title_function<Set>(0x0005e430)(0);
            title_function<Set>(0x0005e4b0)(0);
            title_function<Set>(0x0005e490)(0);
            const TitleRegs ui{0, 0, 0, 0, 0x0095f8d0, 0};
            g_ent_call(0x003153d0, &ui, 0, nullptr);         // UIRoot_DrawInGameScreens (ESI)
            title_function<void XB_CDECL()>(0x0005e730)();  // nglPopScene
            title_function<void XB_CDECL()>(0x00064ec0)();  // nglEndFrame
            const uint32_t dt = title_global<uint32_t>(0x008ddaa0);
            g_ent_call(0x00315520, &ui, 1, &dt);  // UIRoot_UpdateInGameScreens (ESI, stdcall)
        }
    }
    if (title_global<uint32_t>(0x00985ebc)) {
        call(0x0029b860, TitleRegs{0, 0, 0, 0, title_global<uint32_t>(0x0095f5b4), 0}, 0, nullptr);  // FX_FreeAllEffects (ESI)
        stop_sounds();
        cvar_paused(0x0082e058);  // "1"
        title_global<uint8_t>(0x0095f919) = 1;
        word_at(at(menu()), 0x58) = 1;
        const uint32_t reason = title_global<uint32_t>(0x00985ec0);
        const uint32_t failed = reason ? reason + 6 : 0;
        call(0x0030cfd0, TitleRegs{0, 0, 0, 0, menu(), 0}, 1, &failed);  // MissionSuccessFailMenu_SetFailedText (ESI, stdcall)
        open_menu();
        return;
    }
    if (kind == 4) {
        call(0x002b1eb0, TitleRegs{0x0082c894, 0, 0, 0, 0, 0}, 0, nullptr);  // Cmd_ExecuteRegisteredCommand "reboot"
    } else if (kind == 0x10) {
        call(0x0024acb0, TitleRegs{}, 0, nullptr);  // G_EndGame
    } else {
        const uint32_t zero = 0;
        call(0x002bffd0, TitleRegs{0x00838c14, 0, 0, 0, 0, 0}, 1, &zero);  // SV_GameSendServerCommand "opendeadscreen"
    }
    title_global<int32_t>(0x00985e28) = 0;
}

void XB_CDECL G_InitSkillCvars(uint32_t restore) {
    // G_RegisterCvars; with `restore` the skill level and g_player_maxhealth from before come
    // back (copies in 0x00950cec / 0x00950ce8). Then four vmCvars are reset: 0x00947f98 to "0"
    // (through "%i"), 0x0094c230 and vm_cl_stance to "0", 0x0094a3b8 to "1".
    using CvarSet = uint32_t XB_CDECL(const char*, const char*, uint32_t);
    using Va = char* XB_CDECL(const char*, ...);
    const auto text = [](uint32_t address) { return reinterpret_cast<const char*>(static_cast<uintptr_t>(address)); };
    int32_t skill = 0;
    int32_t health = 0;
    if (restore) {
        skill = title_global<int32_t>(0x00950b3c);  // g_skillLevel
        health = static_cast<int32_t>(word_at(Cvar_Get(text(0x00832ff8), text(0x0082e058), 0), 0x20));  // g_player_maxhealth
    }
    G_RegisterCvars();
    if (restore) {
        Cvar_SetValue(text(0x00832fec), static_cast<float>(skill));  // g_gameskill
        const float value = static_cast<float>(health);
        title_global<int32_t>(0x00950b3c) = skill;
        Cvar_SetValue(text(0x00832ff8), value);
        title_global<int32_t>(0x00950cec) = health;
        title_global<float>(0x00950ce8) = value;
    }
    const auto reset = [&text](uint32_t vm_cvar, const char* value) {
        const uint32_t index = title_global<uint32_t>(vm_cvar);
        title_function<CvarSet>(0x0029c030)(text(title_global<uint32_t>(0x00940a10 + index * 0x2c)), value, 1);  // cvar_indexes
        Cvar_Update(&title_global<uint8_t>(vm_cvar));
    };
    reset(0x00947f98, title_function<Va>(0x002a93d0)(text(0x0082ded4), 0));  // "%i"
    reset(0x0094c230, text(0x0082e098));
    reset(0x00948778, text(0x0082e098));  // vm_cl_stance
    reset(0x0094a3b8, text(0x0082e058));
}

void XB_CDECL G_FreeLevelEntities() {
    // The world's entity (+8 of 0x0095f534) and every listed entity (0x0097d36c count, 0x0097d370
    // list) by G_FreeEntity; then for each listed entity with a model object (+0x22c): DObj_Free,
    // its ragdoll (+0x238) released, the object freed to its memory context (+0x220, 0..0x62)
    // or the zone, and the entity number queued for removal.
    using FreeEntity = void XB_CDECL(uint32_t, uint32_t);
    const uint32_t world = word_at(title_global<uint8_t*>(0x0095f534), 8);
    if (world) title_function<FreeEntity>(0x002be420)(world, 0);  // G_FreeEntity
    uint32_t* const first = &title_global<uint32_t>(0x0097d370);
    const uint32_t* const last = first + title_global<uint32_t>(0x0097d36c);
    for (uint32_t* it = first; it != last; ++it) {
        if (*it) title_function<FreeEntity>(0x002be420)(*it, 0);  // G_FreeEntity
    }
    title_function<void XB_CDECL()>(0x00042460)();  // Sys_DebugBreak
    const uint32_t* const end = first + title_global<uint32_t>(0x0097d36c);
    for (uint32_t* it = first; it != end; ++it) {
        uint8_t* const ent = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(*it));
        if (!ent) continue;
        uint8_t* const model = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(word_at(ent, 0x22c)));
        if (!model) continue;
        DObj_Free(reinterpret_cast<DObj*>(model), 0);
        uint8_t* const ragdoll = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(word_at(ent, 0x238)));
        if (ragdoll) {
            if (word_at(ragdoll, 0x568)) {
                uint8_t* const owner = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(word_at(ragdoll, 0)));
                PhysRagdoll_WriteBodiesToPose(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(word_at(ragdoll, 0x568))));
                title_function<void XB_STDCALL(uint8_t*)>(0x0022c0f0)(owner);
                RagdollPhysPool_Free(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(word_at(ragdoll, 0x568))), 0,
                                     &title_global<uint8_t>(0x008f0470));
                word_at(ragdoll, 0x568) = 0;
                reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(word_at(ragdoll, 0)))[0x294] = 0;
            }
            RagdollPool_Free(ragdoll, 0, &title_global<uint8_t>(0x008dfde0));
            word_at(ent, 0x238) = 0;
        }
        uint8_t* const object = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(word_at(ent, 0x22c)));
        const int32_t context = static_cast<int32_t>(word_at(ent, 0x220));
        if (object) {
            XModel_Free(object);
            bool freed = false;
            if (context != -1) {
                uint8_t* pool = nullptr;
                if (context >= 0 && context < 0x63)
                    pool = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(
                        word_at(title_global<uint8_t*>(0x0095f638), 0x38 + static_cast<uint32_t>(context) * 4)));
                freed = MemContext_Free_r(pool, object) != 0;
            }
            if (!freed) Z_Free(object);
        }
        word_at(ent, 0x22c) = 0;
        G_EntityList_QueueRemove(word_at(ent, 0x224));
    }
    AnimTree_FlushClearLists();
}

PLATFORM_ASM_ENTRY(G_ReloadLevel_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(G_ReloadLevel) "addl $8, %esp\n"
                   "ret")

uint8_t* XB_CDECL G_ReloadLevel(uint32_t ebx, uint32_t full) {
    // "g_reloading" 1 while the level restarts; a full reload frees the level's entities, hud
    // elements, bad places, DObjs (G_FreeAllDObjs with BL = 1; the pushed EBX keeps the
    // caller's upper bytes) and queued anim trees; then the sound and screen fade-in commands
    // to every client and "g_reloading" 0 (its Cvar_Set2 result).
    Cvar_Set2(reinterpret_cast<const char*>(uintptr_t{0x0082c89c}), reinterpret_cast<char*>(uintptr_t{0x0082e058}), 1);
    if (full) {
        G_FreeLevelEntities();
        HudElem_ClearAll();
        BadPlace_ClearAll();
        const TitleRegs regs{0, (ebx & ~0xffu) | 1, 0, 0, 0, 0};
        g_ent_call(0x002e8bb0, &regs, 0, nullptr);  // G_FreeAllDObjs
        AnimTree_FlushClearLists();
    }
    SV_GameSendServerCommand(reinterpret_cast<const char*>(uintptr_t{0x0083b2f0}), 0);  // "snd_fade 1 0"
    SV_GameSendServerCommand(reinterpret_cast<const char*>(uintptr_t{0x0083b2e0}), 0);  // "scr_fade 0 0 0"
    return Cvar_Set2(reinterpret_cast<const char*>(uintptr_t{0x0082c89c}), reinterpret_cast<char*>(uintptr_t{0x0082e098}),
                     1);
}

PLATFORM_ASM_ENTRY(G_RestorePlayerHealth_entry,
                   "pushl %ebx\n" PLATFORM_ASM_CALL(G_RestorePlayerHealth) "addl $4, %esp\n"
                   "ret")

void XB_CDECL G_RestorePlayerHealth(const uint8_t* saved) {
    uint8_t* const player = reinterpret_cast<uint8_t*>(uintptr_t{*reinterpret_cast<const uint32_t*>(title_global<uint8_t*>(0x0095f534) + 4)});
    if (!player) return;
    uint8_t* const client = *reinterpret_cast<uint8_t**>(player + 0x23c);
    if (!client) return;
    int32_t health;
    if (saved[0x390] && saved[0]) {
        // The minimum health per skill level: max health (+0x108) times three factors, truncated (_ftol).
        const long double max_health = static_cast<long double>(*reinterpret_cast<const int32_t*>(client + 0x108));
        int32_t minimum[3];
        minimum[0] = static_cast<int32_t>(static_cast<int64_t>(title_global<double>(0x0083ee70) * max_health));
        minimum[1] = static_cast<int32_t>(static_cast<int64_t>(title_global<double>(0x0083ecb0) * max_health));
        minimum[2] = static_cast<int32_t>(static_cast<int64_t>(max_health * title_global<double>(0x0083ee68)));
        const int32_t limit = minimum[title_global<int32_t>(0x00950b3c)];  // g_skillLevel
        health = *reinterpret_cast<const int32_t*>(saved + 4);
        if (health < limit) health = limit;
    } else {
        health = *reinterpret_cast<const int32_t*>(client + 0x108);
    }
    *reinterpret_cast<int32_t*>(client + 0x100) = health;
    *reinterpret_cast<int32_t*>(player + 0x350) = health;
}

}  // namespace game
