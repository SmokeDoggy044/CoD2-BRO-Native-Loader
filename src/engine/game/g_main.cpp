#include "g_main.h"

#include <cstring>

#include "actor.h"
#include "cgame.h"
#include "dobj.h"
#include "fe.h"
#include "g_pathnode.h"
#include "g_scr_imports.h"
#include "g_script.h"
#include "g_utils.h"
#include "g_vehicle.h"
#include "qcommon.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "ragdoll.h"
#include "script_string.h"
#include "sv.h"
#include "title_crt.h"
#include "title_data.h"
#include "xanim.h"
#include "cl.h"
#include "g_scr_runtime.h"
#include "ngl.h"
#include "phys.h"
#include "snd.h"
#include "fe_menus.h"
#include "fx.h"
#include "ui_menu.h"
#include "bro_scr_brofunc.h"

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
    own_function<AssignCString>(&ScriptString_AssignCString)(level + 0xb50, empty);  // ScriptString_AssignCString
    zero(level, 0xb54, 0xc90);
    level[0xc90] = 0;
    level[0xc91] = 0;
    zero(level, 0xc94, 0xca7);
    zero(level, 0xca8, 0xcb8);
    own_function<AssignCString>(&ScriptString_AssignCString)(level + 0xcb8, empty);  // ScriptString_AssignCString
    zero(level, 0xcbc, 0x2708);
}

void XB_REGS1 XB_CDECL G_FreeEntityPhysics(uint8_t* ent) {
    if (word_at(ent, 0x244)) {
        const uint32_t ragdoll = word_at(ent, 0x238);
        if (ragdoll) {
            const TitleRegs regs{word_at(ent, 0x244), 0, 0, 0, ragdoll, address(ent)};
            g_ent_call(own_address(&Ragdoll_FreePhysics_entry), &regs, 0, nullptr);  // Ragdoll_FreePhysics (ESI)
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
                g_ent_call(own_address(&VehiclePhys_Destroy_entry), &regs, 0, nullptr);  // VehiclePhys_Destroy (ESI)
                word_at(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(word_at(ent, 0x248))), 0x300) = 0;
            }
            return;
        }
    }
    const TitleRegs regs{address(vehicle), 0, 0, 0, 0, address(ent)};
    g_ent_call(own_address(&Phys_RemoveEntity_entry), &regs, 0, nullptr);  // Phys_RemoveEntity (EDI)
}

void XB_CDECL G_VehiclePhysicsFrame(float frame_time) {
    // The scaled time goes back into the argument slot (x87 product), then to each step.
    const float scaled = platform_x87_mul(title_global<float>(0x008ff028), frame_time);
    uint32_t bits;
    std::memcpy(&bits, &scaled, sizeof bits);
    own_function<void XB_CDECL(uint32_t)>(&Ragdoll_UpdateAllFromEntities)(bits);  // Ragdoll_UpdateAllFromEntities
    own_function<void XB_CDECL(uint32_t)>(&Phys_RunFrame)(bits);  // Phys_RunFrame
    own_function<void XB_CDECL()>(&PhysObj_UpdateAll)();              // PhysObj_UpdateAll
    own_function<void XB_CDECL(uint32_t)>(&Ragdoll_UpdateAll)(bits);  // Ragdoll_UpdateAll
    // The vehicles with physics: 0x008feb70, count at 0x008feb9c (read once).
    const uint32_t end = 0x008feb70 + title_global<uint32_t>(0x008feb9c) * 4;
    for (uint32_t at = 0x008feb70; at != end; at += 4)
        own_function<void XB_STDCALL(uint32_t)>(&Vehicle_UpdateFromPhysics)(title_global<uint32_t>(at));  // Vehicle_UpdateFromPhysics
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
    own_function<AddForce>(&Phys_AddForceToEntityBodies)(dir, ent, point, scale, 0x3f800000);  // Phys_AddForceToEntityBodies
}

}  // namespace

void XB_REGS1 XB_CDECL G_PhysicsLaunch(uint8_t* ent, const float* point, const float* dir, float scale,
                                       uint32_t local_point, uint32_t hit_location) {
    if (!ent || ent == title_global<uint8_t*>(title_global<uint32_t>(0x0095f534) + 8)) return;
    const uint32_t ragdoll = word_at(ent, 0x238);
    if (ragdoll) {
        // FUN_00225480: hit location (EAX) -> ragdoll body index.
        const uint32_t body = own_function<uint32_t XB_REGS1 XB_CDECL(uint32_t)>(&Phys_MapLaunchType_entry)(hit_location);
        uint8_t* const doll = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(ragdoll));
        if (!word_at(doll, 0x568)) {
            if (title_global<int32_t>(0x008fb484) >= 6) return;
            const TitleRegs regs{0, 0, ragdoll, 0, address(ent), body};
            const uint32_t args[1] = {ragdoll};
            g_ent_call(own_address(&Ragdoll_StartPhysics_entry), &regs, 1, args);  // Ragdoll_StartPhysics (EBX = 0, stdcall)
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
    if (own_function<uint32_t XB_REGS1 XB_CDECL(uint8_t*)>(&Vehicle_HasDefType1)(ent) & 0xff) {
        const uint32_t vehicle = word_at(ent, 0x248);
        if (word_at(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(vehicle)), 0x300)) return;
        const TitleRegs regs{vehicle, 0, 0, 0, address(ent), 0};
        g_ent_call(own_address(&VehiclePhys_GetOrCreate_entry), &regs, 0, nullptr);  // VehiclePhys_GetOrCreate (ESI)
        return;
    }
    if (!(word_at(ent, 0x2a4) & 0x400000)) {
        own_function<void XB_CDECL(uint8_t*, uint32_t, uint32_t, uint32_t)>(&PhysObj_Create)(ent, 0, 0x3f800000,
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
            title_sprintf(
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
        title_strncpy(arg, command_arg(static_cast<uint32_t>(i)), 0xff);  // _strncpy
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
    title_strncpy(cmd, command_arg(0), 0x7f);  // _strncpy
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
        title_strncpy(
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
        own_function<void XB_REGS1 XB_CDECL(uint32_t)>(&AnimTree_FreeNotetracks)(static_cast<uint32_t>(i));  // AnimTree_FreeNotetracks
    if (title_global<int32_t>(0x0095f5a0) == 1) {
        own_function<void XB_CDECL(uint32_t)>(&Z_Free)(title_global<uint32_t>(0x0095f59c));  // Z_Free
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
        own_function<void XB_CDECL(uint32_t, uint32_t)>(&G_FreeEntity)(ent, 0);  // G_FreeEntity
        handle = 0;
    }
    title_global<int32_t>(0x00985418) = limit;
}

void XB_CDECL G_FreeStringTable(uint32_t keep) {
    // The string table (0x0095f59c) when the game loaded it (0x0095f5a0 == 1): freed unless
    // `keep`; the owner is cleared either way.
    if (title_global<int32_t>(0x0095f5a0) != 1) return;
    if (!keep) {
        own_function<void XB_CDECL(uint32_t)>(&Z_Free)(title_global<uint32_t>(0x0095f59c));  // Z_Free
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
    own_function<void XB_CDECL()>(&G_InitAnimScripts)();  // G_InitAnimScripts
    own_function<void XB_CDECL()>(&G_InitTurretAnimScripts)();  // G_InitTurretAnimScripts
    own_function<void XB_CDECL(uint32_t, uint32_t)>(&G_SetupAnimTrees)(0x0024a050, arg);  // G_SetupAnimTrees
    const char* const name = reinterpret_cast<const char*>(uintptr_t{0x0083a894});  // "generic_human"
    uint8_t* const tree = G_FindAnimTreeByName(name);
    if (!tree)
        own_function<void XB_CDECL(const char*, ...)>(&G_Error_entry)(
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
    const char* text = own_function<Va>(&va_entry)(reinterpret_cast<const char*>(uintptr_t{0x0083b37c}),
                                                      title_global<int32_t>(0x0098540c) + 250,
                                                      title_global<int32_t>(0x00985ec8) + 750);  // "scr_fade 1 %i %i"
    SV_GameSendServerCommand(text, 0);
    text = own_function<Va>(&va_entry)(reinterpret_cast<const char*>(uintptr_t{0x0083bdb4}),
                                          title_global<int32_t>(0x00985ec8) + 1000);  // "snd_fade 0 %d"
    SV_GameSendServerCommand(text, 0);
    title_global<int32_t>(0x00985e28) = title_global<int32_t>(0x0098540c) + title_global<int32_t>(0x00985ec8) + 1000;
    using CvarSet = uint32_t XB_CDECL(const char*, const char*, uint32_t);
    const char* const name = reinterpret_cast<const char*>(uintptr_t{0x0082c89c});  // "g_reloading"
    text = own_function<Va>(&va_entry)(reinterpret_cast<const char*>(uintptr_t{0x008325b8}), 4);  // "%d"
    own_function<CvarSet>(&Cvar_Set2)(name, text, 1);  // Cvar_Set2
    if (title_global<uint32_t>(0x00985eb4)) {
        text = own_function<Va>(&va_entry)(reinterpret_cast<const char*>(uintptr_t{0x008325b8}), 16);
        own_function<CvarSet>(&Cvar_Set2)(name, text, 1);
    }
}

void XB_CDECL G_ShutdownGame(uint32_t restart) {
    // Q3's G_ShutdownGame: every entity, the vehicle pool and the second vehicle block
    // (0x0095f690), the vehicle defs; without a restart also the corpse queue and anim trees.
    own_function<void XB_STDCALL(uint32_t)>(&G_FreeAllEntities)(title_global<uint32_t>(0x0095f534));  // G_FreeAllEntities
    if (title_global<uint32_t>(0x0095f694)) {
        own_function<void XB_CDECL(uint32_t)>(&Z_Free)(title_global<uint32_t>(0x0095f694));  // Z_Free
        title_global<uint32_t>(0x0095f694) = 0;
        title_global<uint16_t>(0x00986000) = 0;
    }
    if (title_global<uint32_t>(0x0095f690)) {
        own_function<void XB_CDECL(uint32_t)>(&Z_Free)(title_global<uint32_t>(0x0095f690));  // Z_Free
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
    SV_GameSendServerCommand(own_function<Va>(&va_entry)(reinterpret_cast<const char*>(uintptr_t{0x0083bd50}), fog), 0);
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
                    g_ent_call(own_address(&G_SaveCheckpoint_entry), &regs, 1, args);  // G_SaveCheckpoint (stdcall)
                }
                uint32_t number = field<uint32_t>(other, 0x224);
                const TitleRegs regs{0, address(other), 0, static_cast<uint32_t>(i), address(pair), address(self)};
                const uint32_t args[2] = {title_global<uint32_t>(0x0097a818), address(&number)};
                g_ent_call(own_address(&G_NotifyWithEntity_entry), &regs, 2, args);  // G_NotifyWithEntity "trigger"
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
        const uint32_t cvar = own_function<CvarGet>(&Cvar_Get)(title_global<uint32_t>(entry + 4),
                                                                  title_global<uint32_t>(entry + 8),
                                                                  title_global<uint32_t>(entry + 12));  // Cvar_Get
        const uint32_t vm = title_global<uint32_t>(entry);
        if (vm) {
            uint32_t* const v = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(vm));
            v[0] = static_cast<uint32_t>(static_cast<int32_t>(cvar - 0x00940a10) / 0x2c);
            v[1] = 0xffffffffu;
            const TitleRegs regs{0, vm, 0, 0, 0, 0};
            g_ent_call(own_address(&Cvar_Update_entry), &regs, 0, nullptr);  // Cvar_Update (EBX)
        }
        const uint32_t again = title_global<uint32_t>(entry);
        if (again) title_global<uint32_t>(entry + 16) = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(again + 4));
    }
    static const uint32_t kCleared[] = {0x00948898, 0x009482f8, 0x0094b780, 0x00948538};
    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t vm = kCleared[i];
        own_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t)>(&Cvar_Set2)(
            title_global<uint32_t>(0x00940a10 + title_global<uint32_t>(vm) * 0x2c), 0x0082dfeb, 1);  // Cvar_Set2 ""
        const TitleRegs regs{0, vm, 0, 0, 0, 0};
        g_ent_call(own_address(&Cvar_Update_entry), &regs, 0, nullptr);  // Cvar_Update
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
        call(own_address(&Cvar_Set2), TitleRegs{}, 3, args);  // Cvar_Set2("cl_paused", value, 1)
    };
    const auto open_menu = [&at, &call]() {
        uint8_t* const root = title_global<uint8_t*>(0x0095f8fc);
        const uint32_t six = 6;
        call(word_at(at(word_at(root, 0)), 0xc), TitleRegs{0, 0, address(root), 0, 0, 0}, 1, &six);  // thiscall
    };
    const auto stop_sounds = [&call]() {
        call(own_address(&S_StopAllSounds_entry), TitleRegs{0, title_global<uint32_t>(0x0095f4c4), 0, 0, 0, 0}, 0, nullptr);  // S_StopAllSounds (EBX)
        call(own_address(&RumbleQueue_ClearAll), TitleRegs{title_global<uint32_t>(0x0095f5c4), 0, 0, 0, 0, 0}, 0, nullptr);  // rumble stop (EAX)
    };
    if (title_global<uint32_t>(0x00985eb0)) {
        title_global<uint32_t>(0x00985eb0) = 0;
        call(own_address(&G_StartMissionEndFade), TitleRegs{}, 0, nullptr);  // G_StartMissionEndFade
    }
    const uint32_t kind = title_global<uint32_t>(0x0095bf9c);
    if (!kind) return;
    const int32_t end_time = title_global<int32_t>(0x00985e28);
    if (!end_time || end_time >= title_global<int32_t>(0x0098540c)) return;
    if (!title_global<uint8_t>(0x0095f67c)) {
        const uint32_t list = 0x008dd1d8;
        call(own_address(&Scr_AbortAllThreads), TitleRegs{}, 1, &list);  // stdcall
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
        call(own_address(&MissionSuccessFailMenu_ClearTexts_entry), TitleRegs{0, 0, 0, 0, menu(), 0}, 0, nullptr);  // MissionSuccessFailMenu_ClearTexts (ESI)
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
        const char* const path = own_function<Va>(&va_entry)(text(format), text(language));
        const int32_t file = own_function<int32_t XB_CDECL(uint32_t, const char*)>(&File_OpenRead)(
            title_global<uint32_t>(0x0095f6f4), path);  // File_OpenRead
        const uint32_t size = own_function<uint32_t XB_CDECL(int32_t)>(&File_GetSize)(file);  // File_GetSize
        const uint32_t padded = (size + 0x7fff) & 0xffff8000u;
        const uint32_t data =
            own_function<uint32_t XB_CDECL(uint32_t, uint32_t, uint32_t)>(&Mem_AlignedAlloc)(padded, 0x80, 0x30000);  // Mem_AlignedAlloc
        own_function<int32_t XB_CDECL(int32_t, uint32_t, uint32_t, uint32_t)>(&File_ReadAt)(file, 0, data, padded);  // File_ReadAt (file, offset, buffer, size)
        uint32_t name = 0;
        own_function<uint32_t* XB_THISCALL(uint32_t*, const char*)>(&HashStr_Init)(&name, text(0x0083b32c));  // HashStr_Init "texSuc"
        const uint32_t texture = own_function<uint32_t XB_CDECL(uint32_t*, uint32_t, uint32_t, uint32_t)>(&nglLoadTexture)(
            &name, 0, data, size);  // nglLoadTexture
        uint32_t quad[0x19] = {};
        own_function<void XB_CDECL(uint32_t*)>(&nglInitQuad)(quad);  // nglInitQuad
        own_function<void XB_CDECL(uint32_t*, float, float, float, float)>(&nglSetQuadRect)(quad, 0.0f, 0.0f, 640.0f,
                                                                                         480.0f);  // nglSetQuadRect
        own_function<void XB_CDECL(uint32_t*, uint32_t)>(&nglSetQuadTexture)(quad, texture);  // nglSetQuadTexture
        own_function<void XB_CDECL(uint32_t*, float)>(&nglSetQuadZ)(quad, 300.0f);    // nglSetQuadZ
        using PadCreate = uint32_t XB_CDECL();
        for (;;) {
            own_function<uint32_t XB_CDECL()>(&File_PumpRequests)();  // File_PumpRequests
            {
                const TitleRegs pad{0, 0, own_function<PadCreate>(&IN_PadCreate)(), 0, 0, 0};
                g_ent_call(own_address(&IN_PadUpdate), &pad, 0, nullptr);  // IN_PadUpdate (ECX)
            }
            const uint32_t controller = title_global<uint32_t>(0x008d50d8);
            const uint32_t pad = own_function<PadCreate>(&IN_PadCreate)();
            if (!(own_function<uint8_t XB_THISCALL(uint32_t, uint32_t)>(&IN_PadIsConnected)(pad, controller))) {  // IN_PadIsConnected
                title_global<uint8_t>(0x008d50dc) = 0;
                if (!title_global<uint32_t>(0x0095f8f4) || title_global<uint8_t>(0x0095f538)) continue;
                const TitleRegs pause{title_global<uint32_t>(0x0095f4c4), 0, 0, 0, 0, 0};
                g_ent_call(own_address(&S_PauseAllChannels), &pause, 0, nullptr);  // S_PauseAllChannels (EAX)
                own_function<void XB_STDCALL(uint32_t)>(&S_CheckWaveBankNotifies)(title_global<uint32_t>(0x0095f4c0));  // S_CheckWaveBankNotifies
                const uint32_t paused = word_at(title_global<uint8_t*>(0x0093fcd8), 0x20);
                title_global<uint8_t>(0x0095f539) = paused != 0;
                own_function<CvarSet>(&Cvar_Set2)(text(0x0082ca00), text(0x0082e058), 1);  // cl_paused "1"
                const TitleRegs draw{0, 0, 0, 0, 0x0095f8d0, paused};
                g_ent_call(0x0030f190, &draw, 0, nullptr);  // a paused frame (ESI = g_uiRoot)
                title_global<uint8_t>(0x0095f538) = 1;
                continue;
            }
            title_global<uint8_t>(0x008d50dc) = 1;
            if (title_global<uint8_t>(0x0095f538)) {
                const uint32_t pressed_controller = title_global<uint32_t>(0x008d50d8);
                const uint32_t pressed_pad = own_function<PadCreate>(&IN_PadCreate)();
                if (!(own_function<uint8_t XB_THISCALL(uint32_t, uint32_t, uint32_t)>(&IN_PadButtonPressed)(
                        pressed_pad, pressed_controller, 0xe)))  // IN_PadButtonPressed
                    continue;
                const TitleRegs resume{title_global<uint32_t>(0x0095f4c4), 0, 0, 0, 0, 0};
                g_ent_call(own_address(&S_ResumeAllChannels), &resume, 0, nullptr);  // S_ResumeAllChannels (EAX)
                own_function<CvarSet>(&Cvar_Set2)(
                    text(0x0082ca00), text(title_global<uint8_t>(0x0095f539) ? 0x0082e058 : 0x0082e098), 1);  // cl_paused
                title_global<uint8_t>(0x0095f538) = 0;
                const uint32_t consume_pad = own_function<PadCreate>(&IN_PadCreate)();
                const TitleRegs consume{0, consume_pad, 0, 0, 0, title_global<uint32_t>(0x008d50d8)};
                g_ent_call(own_address(&IN_PadConsumeAllPressed_entry), &consume, 0, nullptr);  // IN_PadConsumeAllPressed (EDI)
            }
            using Set = void XB_CDECL(uint32_t);
            own_function<void XB_CDECL()>(&nglBeginFrame)();  // nglBeginFrame
            own_function<Set>(&nglPushScene)(0);  // nglPushScene
            own_function<Set>(&nglSetClearFlags)(0);  // nglSetClearFlags
            own_function<Set>(&nglSetSceneDepthTest)(0);  // nglSetSceneDepthTest
            own_function<Set>(&nglSetSceneDepthEnable)(0);  // nglSetSceneDepthEnable
            own_function<void XB_CDECL(const uint32_t*)>(&nglListAddQuad)(quad);  // nglListAddQuad
            own_function<void XB_CDECL()>(&nglPopScene)();                     // nglPopScene
            own_function<Set>(&nglPushScene)(0);
            own_function<Set>(&nglSetClearFlags)(0);
            own_function<Set>(&nglSetSceneDepthTest)(0);
            own_function<Set>(&nglSetSceneDepthEnable)(0);
            const TitleRegs ui{0, 0, 0, 0, 0x0095f8d0, 0};
            g_ent_call(own_address(&UIRoot_DrawInGameScreens_entry), &ui, 0, nullptr);         // UIRoot_DrawInGameScreens (ESI)
            own_function<void XB_CDECL()>(&nglPopScene)();  // nglPopScene
            own_function<void XB_CDECL()>(&nglEndFrame)();  // nglEndFrame
            const uint32_t dt = title_global<uint32_t>(0x008ddaa0);
            g_ent_call(own_address(&UIRoot_UpdateInGameScreens_entry), &ui, 1, &dt);  // UIRoot_UpdateInGameScreens (ESI, stdcall)
        }
    }
    if (title_global<uint32_t>(0x00985ebc)) {
        call(own_address(&FX_FreeAllEffects_entry), TitleRegs{0, 0, 0, 0, title_global<uint32_t>(0x0095f5b4), 0}, 0, nullptr);  // FX_FreeAllEffects (ESI)
        stop_sounds();
        cvar_paused(0x0082e058);  // "1"
        title_global<uint8_t>(0x0095f919) = 1;
        word_at(at(menu()), 0x58) = 1;
        const uint32_t reason = title_global<uint32_t>(0x00985ec0);
        const uint32_t failed = reason ? reason + 6 : 0;
        call(own_address(&MissionSuccessFailMenu_SetFailedText_entry), TitleRegs{0, 0, 0, 0, menu(), 0}, 1, &failed);  // MissionSuccessFailMenu_SetFailedText (ESI, stdcall)
        open_menu();
        return;
    }
    if (kind == 4) {
        call(own_address(&Cmd_ExecuteRegisteredCommand), TitleRegs{0x0082c894, 0, 0, 0, 0, 0}, 0, nullptr);  // Cmd_ExecuteRegisteredCommand "reboot"
    } else if (kind == 0x10) {
        call(own_address(&G_EndGame), TitleRegs{}, 0, nullptr);  // G_EndGame
    } else {
        const uint32_t zero = 0;
        call(own_address(&SV_GameSendServerCommand), TitleRegs{0x00838c14, 0, 0, 0, 0, 0}, 1, &zero);  // SV_GameSendServerCommand "opendeadscreen"
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
        own_function<CvarSet>(&Cvar_Set2)(text(title_global<uint32_t>(0x00940a10 + index * 0x2c)), value, 1);  // cvar_indexes
        Cvar_Update(&title_global<uint8_t>(vm_cvar));
    };
    reset(0x00947f98, own_function<Va>(&va_entry)(text(0x0082ded4), 0));  // "%i"
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
    if (world) own_function<FreeEntity>(&G_FreeEntity)(world, 0);  // G_FreeEntity
    uint32_t* const first = &title_global<uint32_t>(0x0097d370);
    const uint32_t* const last = first + title_global<uint32_t>(0x0097d36c);
    for (uint32_t* it = first; it != last; ++it) {
        if (*it) own_function<FreeEntity>(&G_FreeEntity)(*it, 0);  // G_FreeEntity
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
                own_function<void XB_STDCALL(uint8_t*)>(&Ragdoll_PropagateBoneMatrices)(owner);
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
        g_ent_call(own_address(&G_FreeAllDObjs_entry), &regs, 0, nullptr);  // G_FreeAllDObjs
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
        const long double max_health = platform_x87_ld(*reinterpret_cast<const int32_t*>(client + 0x108));
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

void XB_REGS3 XB_CDECL G_ApplyCheckpointStart(uint8_t* ent, uint32_t, uint8_t* checkpoint) {
    const auto stricmp = title_function<int32_t XB_CDECL(const char*, const char*)>(0x001f1443);  // __stricmp
    auto text_of = [](uint32_t string) { return string ? reinterpret_cast<const char*>(uintptr_t{string + 6}) : nullptr; };
    // Only for the level the checkpoint was saved in (+0x55c against the map name 0x00988c58).
    if (stricmp(text_of(word_at(checkpoint, 0x55c)), reinterpret_cast<const char*>(uintptr_t{0x00988c58}))) return;
    const uint32_t name = word_at(checkpoint, 0x558);
    if (!name || !*text_of(name)) return;
    Cvar_Set2(reinterpret_cast<const char*>(uintptr_t{0x008353b8}), const_cast<char*>(text_of(word_at(checkpoint, 0x558))),
              1);  // "checkpoint"
    if (ScriptString_Equals(reinterpret_cast<const ScriptString*>(ent + 0x25c),
                            reinterpret_cast<const char*>(uintptr_t{0x008353a4}))) {  // "info_player_start"
        word_at(ent, 0x140) = word_at(checkpoint, 0x384);
        word_at(ent, 0x144) = word_at(checkpoint, 0x388);
        word_at(ent, 0x148) = word_at(checkpoint, 0x38c);
        word_at(ent, 0x150) = word_at(checkpoint, 0x378);
        word_at(ent, 0x154) = word_at(checkpoint, 0x37c);
        word_at(ent, 0x158) = word_at(checkpoint, 0x380);
    }
    // The saved entity spots (+0x394, 0x1c bytes: targetname, angles, origin) by targetname (+0x264).
    for (int32_t i = 0; i < static_cast<int32_t>(word_at(checkpoint, 0x554)); ++i) {
        uint8_t* const spot = checkpoint + 0x394 + i * 0x1c;
        if (!ScriptString_EqualsString(reinterpret_cast<const ScriptString*>(ent + 0x264),
                                       reinterpret_cast<const ScriptString*>(spot)))
            continue;
        word_at(ent, 0x140) = word_at(spot, 0x10);
        word_at(ent, 0x144) = word_at(spot, 0x14);
        word_at(ent, 0x148) = word_at(spot, 0x18);
        word_at(ent, 0x150) = word_at(spot, 4);
        word_at(ent, 0x154) = word_at(spot, 8);
        word_at(ent, 0x158) = word_at(spot, 0xc);
        break;
    }
    uint8_t* const sentient = reinterpret_cast<uint8_t*>(uintptr_t{word_at(ent, 0x244)});
    if (!sentient) return;
    const uint16_t* const goal = reinterpret_cast<const uint16_t*>(sentient + 0x48);
    if (PathNode_HandleIsValid(goal) & 0xff) {
        Sentient_ClearNodeClaim(0, PathNode_FromHandle2(goal));
        *reinterpret_cast<uint16_t*>(sentient + 0x48) = 0;
    }
    PathNode_ReleaseClaimsBy(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(sentient)),
                             title_global<uint8_t*>(0x0095f4f4));  // g_pathNodeBlocks
}

namespace {

// The checkpoint record's state reset (G_ClearCheckpoint, Checkpoint_Init): flags, counts, the checkpoint cvar to
// "0", +4 and the two 6-word blocks at +0x20 and +0x38.
void checkpoint_reset(uint8_t* checkpoint, bool clear_name) {
    checkpoint[0x390] = 0;
    checkpoint[0] = 0;
    word_at(checkpoint, 0x554) = 0;
    *reinterpret_cast<float*>(checkpoint + 0x34c) = 0.0f;
    word_at(checkpoint, 0x594) = 0;
    if (clear_name) own_function<void XB_THISCALL(uint8_t*)>(&ScriptString_Clear)(checkpoint + 0x558);  // ScriptString_Clear
    Cvar_Set2(reinterpret_cast<const char*>(uintptr_t{0x008353b8}), reinterpret_cast<char*>(uintptr_t{0x0082e098}),
              1);  // checkpoint "0"
    word_at(checkpoint, 4) = 0;
    zero(checkpoint, 0x20, 0x38);
    zero(checkpoint, 0x38, 0x50);
}

// A hash map of the checkpoint (list at +list, its head +4; bucket vector at +list+0xc; +0x1c/+0x20 = 1) emptied.
void checkpoint_map_clear(uint8_t* checkpoint, uint32_t list) {
    std_list_Clear(checkpoint + list);
    uint32_t head = word_at(checkpoint, list + 4);
    std_vector_Assign4(&head, reinterpret_cast<Vector4*>(checkpoint + list + 0xc), 9);
    word_at(checkpoint, list + 0x1c) = 1;
    word_at(checkpoint, list + 0x20) = 1;
}

// The same map constructed: allocator byte (an uninitialised local in the original; 0 under tests), a new head and
// 9 buckets pointing at it.
void checkpoint_map_init(uint8_t* checkpoint, uint32_t list) {
    checkpoint[list - 4] = 0;
    const uint32_t head = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(std_list_BuyHeadZ()));
    word_at(checkpoint, list + 4) = head;
    word_at(checkpoint, list + 8) = 0;
    std_vector_ConstructN4Z(9, checkpoint + list + 0xc, &head);
    word_at(checkpoint, list + 0x1c) = 1;
    word_at(checkpoint, list + 0x20) = 1;
}

}  // namespace

PLATFORM_ASM_ENTRY(G_ClearCheckpoint_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(G_ClearCheckpoint) "addl $4, %esp\n"
                   "ret")

void XB_CDECL G_ClearCheckpoint(uint8_t* checkpoint) {
    checkpoint_reset(checkpoint, true);
    checkpoint_map_clear(checkpoint, 0x570);
    checkpoint_map_clear(checkpoint, 0x354);
}

PLATFORM_ASM_ENTRY(Checkpoint_Init_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(Checkpoint_Init) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL Checkpoint_Init(uint8_t* checkpoint) {
    using FromRep = void XB_THISCALL(uint8_t*, uint32_t);
    const auto from_rep = own_function<FromRep>(&ScriptString_FromRep);  // ScriptString_FromRep
    for (uint32_t i = 0; i < 6; ++i) from_rep(checkpoint + 8 + i * 4, 0);
    checkpoint_map_init(checkpoint, 0x354);
    for (uint32_t i = 0; i < 16; ++i) {
        uint8_t* const spot = checkpoint + 0x394 + i * 0x1c;
        from_rep(spot, 0);
        for (uint32_t f = 4; f < 0x1c; f += 4) *reinterpret_cast<float*>(spot + f) = 0.0f;
    }
    from_rep(checkpoint + 0x558, 0);
    from_rep(checkpoint + 0x55c, 0);
    word_at(checkpoint, 0x560) = 0;
    word_at(checkpoint, 0x564) = 0;
    word_at(checkpoint, 0x568) = 0;
    checkpoint_map_init(checkpoint, 0x570);
    checkpoint_reset(checkpoint, false);
    return checkpoint;
}

void XB_REGS1 XB_CDECL G_RestorePlayerLoadout(const uint8_t* saved) {
    if (!saved[0x390] || !saved[0]) return;
    uint8_t* const player = *reinterpret_cast<uint8_t**>(title_global<uint8_t*>(0x0095f534) + 4);
    auto ps = [&] { return reinterpret_cast<uint8_t*>(uintptr_t{word_at(player, 0x23c)}); };
    std::memcpy(ps() + 0x110, saved + 0x50, 0x170);   // weapons
    std::memcpy(ps() + 0x280, saved + 0x1c0, 0x170);  // ammo
    for (uint32_t f = 0; f < 0x18; f += 4)
        word_at(ps(), 0x3f0 + f) = *reinterpret_cast<const uint32_t*>(saved + 0x330 + f);
    CG_SelectWeapon(*reinterpret_cast<const int32_t*>(saved + 0x348));
    G_RestorePlayerHealth(saved);
}

PLATFORM_ASM_ENTRY(G_SaveCheckpoint_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(G_SaveCheckpoint) "addl $12, %esp\n"
                   "ret $4")

void XB_CDECL G_SaveCheckpoint(const char* name, uint8_t* checkpoint, uint32_t at_player) {
    auto text = [](uint32_t address) { return reinterpret_cast<const char*>(uintptr_t{address}); };
    auto ent_at = [](uint32_t word) { return reinterpret_cast<uint8_t*>(uintptr_t{word}); };
    uint8_t* const letterbox = Cvar_Get(text(0x00835390), text(0x0082e098), 0);  // letterbox_enabled "0"
    if (word_at(letterbox, 0x20)) {
        title_function<void XB_CDECL()>(0x00042460)();  // Sys_DebugBreak
        return;
    }
    if (!name || !*name) return;
    ScriptString* const current = reinterpret_cast<ScriptString*>(checkpoint + 0x558);
    const uint32_t current_word = word_at(checkpoint, 0x558);
    if (current_word && *text(current_word + 6) && ScriptString_Equals(current, name)) return;
    using AssignCString = void XB_THISCALL(uint8_t*, const char*);
    const auto assign_cstring = own_function<AssignCString>(&ScriptString_AssignCString);  // ScriptString_AssignCString
    uint8_t* source;
    if (!static_cast<uint8_t>(at_player)) {
        // The entity whose targetname is the name: its +0x27c string names the checkpoint.
        uint32_t local = 0;
        own_function<void XB_THISCALL(uint32_t*, const char*)>(&ScriptString_FromCString)(&local, name);  // ScriptString_FromCString
        const uint32_t key = Com_HashKeyNoCase(0, text(0x0082dda8));  // "targetname"
        using Find = uint32_t XB_CDECL(uint32_t*, uint32_t, uint32_t, uint32_t, uint32_t);
        const uint32_t handle =
            title_function<Find>(word_at(title_global<uint8_t*>(0x0095f49c), 0x84))(&local, key, 0, 0, 0);
        own_function<void XB_THISCALL(uint32_t*)>(&ScriptString_Release_entry)(&local);  // ScriptString_Release
        local = handle;
        source = G_EntHandleToEnt(&local);
        if (!source) return;
        const uint32_t label = word_at(source, 0x27c);
        if (!label) return;
        if (own_function<uint8_t XB_THISCALL(uint8_t*)>(&ScriptString_IsEmpty)(source + 0x27c)) return;  // ScriptString_IsEmpty
        if (ScriptString_EqualsString(current, reinterpret_cast<const ScriptString*>(source + 0x27c))) return;
        own_function<void XB_THISCALL(uint8_t*, const uint8_t*)>(&ScriptString_Assign_entry)(checkpoint + 0x558,
                                                                             source + 0x27c);  // ScriptString_Assign
        assign_cstring(checkpoint + 0x55c, text(0x00988c58));  // the map name
        word_at(checkpoint, 0x378) = 0;  // 0.0f
        word_at(checkpoint, 0x37c) = word_at(source, 0x154);
        word_at(checkpoint, 0x380) = 0;
    } else {
        source = ent_at(word_at(title_global<uint8_t*>(0x0095f534), 4));  // the player
        if (!source) return;
        assign_cstring(checkpoint + 0x55c, text(0x00988c58));
        assign_cstring(checkpoint + 0x558, name);
        word_at(checkpoint, 0x378) = word_at(source, 0x150);
        word_at(checkpoint, 0x37c) = word_at(source, 0x154);
        word_at(checkpoint, 0x380) = word_at(source, 0x158);
    }
    word_at(checkpoint, 0x384) = word_at(source, 0x140);
    word_at(checkpoint, 0x388) = word_at(source, 0x144);
    word_at(checkpoint, 0x38c) = word_at(source, 0x148);
    // The player's health (capped at the max, 0 -> max), weapons, ammo and selected weapon.
    uint8_t* const level = title_global<uint8_t*>(0x0095f534);
    auto ps = [&] { return ent_at(word_at(ent_at(word_at(level, 4)), 0x23c)); };
    int32_t health = static_cast<int32_t>(word_at(ps(), 0x100));
    if (health > static_cast<int32_t>(word_at(ps(), 0x108)) || !word_at(ps(), 0x100))
        health = static_cast<int32_t>(word_at(ps(), 0x108));
    word_at(checkpoint, 4) = static_cast<uint32_t>(health);
    word_at(checkpoint, 0x34c) = title_global<uint32_t>(0x0083ece8);  // 5.0f
    if (!health) word_at(checkpoint, 4) = word_at(ps(), 0x108);
    std::memcpy(checkpoint + 0x50, ps() + 0x110, 0x170);
    std::memcpy(checkpoint + 0x1c0, ps() + 0x280, 0x170);
    for (uint32_t f = 0; f < 0x18; f += 4) word_at(checkpoint, 0x330 + f) = word_at(ps(), 0x3f0 + f);
    uint8_t* const defs = title_global<uint8_t*>(0x0095f4d8);  // bg_weaponDefs
    uint32_t weapon = word_at(ps(), 0x80);
    if (word_at(ent_at(word_at(defs, weapon * 4)), 0x98) == 5) {
        // A weapon of class 5 is saved as the alternate (+0x94) unless that is class 5 too.
        const uint32_t alternate = word_at(ps(), 0x94);
        weapon = word_at(ent_at(word_at(defs, alternate * 4)), 0x98) == 5 ? 0 : alternate;
    }
    word_at(checkpoint, 0x348) = weapon;
    checkpoint[0x390] = 1;
    ++word_at(checkpoint, 0x594);
    // Scene zones in state 1 become 2.
    uint8_t* const scenes = ent_at(word_at(title_global<uint8_t*>(0x0095f4f8), 0x194));  // scn_zoneScenes
    for (uint32_t i = 0; i < word_at(scenes, 0); ++i) {
        uint8_t* const states = ent_at(word_at(scenes, 4));
        const uint32_t count = word_at(scenes, 0);
        if (states[i < count ? i : 0] == 1) states[i < count ? i : 0] = 2;
    }
    if (checkpoint + 0x56c != checkpoint + 0x350)
        HashMap_CopyFrom_7(reinterpret_cast<const HashMap*>(checkpoint + 0x350), reinterpret_cast<HashMap*>(checkpoint + 0x56c));
    // Squad actors' spots (up to 16).
    word_at(checkpoint, 0x554) = 0;
    uint32_t* const end = &title_global<uint32_t>(0x0097d370 + title_global<uint32_t>(0x0097d36c) * 4);
    for (uint32_t* slot = &title_global<uint32_t>(0x0097d370); slot != end; ++slot) {
        uint8_t* const actor = ent_at(*slot);
        if (!actor || actor[0] != 0xb || Actor_GetSquadSlot(actor) < 0) continue;
        if (static_cast<int32_t>(word_at(checkpoint, 0x554)) >= 0x10) return;
        auto spot = [&] { return checkpoint + 0x394 + word_at(checkpoint, 0x554) * 0x1c; };
        word_at(spot(), 0x10) = word_at(actor, 0x140);
        word_at(spot(), 0x14) = word_at(actor, 0x144);
        word_at(spot(), 0x18) = word_at(actor, 0x148);
        word_at(spot(), 4) = word_at(actor, 0x150);
        word_at(spot(), 8) = word_at(actor, 0x154);
        word_at(spot(), 0xc) = word_at(actor, 0x158);
        own_function<void XB_THISCALL(uint8_t*, const uint8_t*)>(&ScriptString_Assign_entry)(spot(), actor + 0x264);  // Assign
        ++word_at(checkpoint, 0x554);
    }
}

// The module's entry, main and post-init run where the original's calls are (entry - 0xc / - 4, the post-init as a
// tail call at the entry ESP): level script code leaves its stack below them. The C frame sits below a gap.
PLATFORM_ASM_ENTRY(G_LoadLevelScriptModule_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %eax\n" PLATFORM_ASM_CALL(G_LoadLevelScriptModule) "addl $4, %esp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL G_LoadLevelScriptModule(uint32_t entry) {
    // The level script module of the current zone's context: its entry (G_FindLevelScriptModule by the context's
    // name) gets the import block pointer and g_levelScriptImports and returns the level main (0x00927090); the
    // imports are filled, main runs, then the module's post-init (0x00927144, tail jump).
    uint8_t* const memory = title_global<uint8_t*>(0x0095f638);
    const int32_t id = static_cast<int32_t>(*reinterpret_cast<uint32_t*>(memory + 0x34));
    if (id < 0 || id >= 0x63) return;
    if (!*reinterpret_cast<uint32_t*>(memory + 0x38 + id * 4)) return;
    uint8_t* const context = MemContext_FindById(memory, id);
    const uint32_t name = *reinterpret_cast<uint32_t*>(context + 4);
    const uint32_t module = G_FindLevelScriptModule(reinterpret_cast<const char*>(uintptr_t{name}));
    if (!module) {
        title_function<void XB_CDECL()>(0x00042460)();  // Sys_DebugBreak (tail jump)
        return;
    }
    auto stack = [](uint32_t address) -> uint32_t& { return *reinterpret_cast<uint32_t*>(uintptr_t{address}); };
    title_global<uint32_t>(0x00927160) = own_address(&BroFunc_Clear);  // BroFunc_Clear
    title_global<uint32_t>(0x00927164) = own_address(&BroFunc_Register);  // BroFunc_Register
    title_global<uint32_t>(0x009270d0) = 0x002f5130;
    const TitleRegs entry_regs{module, 0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(memory)), 0, name, 0};
    const uint32_t args[2] = {0x0095f49c, 0x009270d0};
    const uint32_t main = title_call_at_regs(entry - 4, module, &entry_regs, 2, args);
    stack(entry - 0x10) = 0x0030b9c0;
    title_global<uint32_t>(0x00927090) = main;  // g_levelScriptMain
    const TitleRegs main_regs{main, 0, 0, 0, main, 0};
    title_call_at_regs(entry - 4, own_address(&G_SetupLevelScriptImports), &main_regs, 0, nullptr);  // G_SetupLevelScriptImports
    stack(entry - 8) = 0x0030b9d0;
    title_call_at_regs(entry - 4, main, &main_regs, 0, nullptr);
    stack(entry - 8) = 0x0030b9d2;
    // The tail call returns through the caller's return address at the entry ESP: saved around it.
    const uint32_t ret = stack(entry);
    title_call_at_regs(entry + 4, title_global<uint32_t>(0x00927144), &main_regs, 0, nullptr);
    stack(entry) = ret;
}

void XB_REGS1 XB_STDCALL Hud_LoadWeaponIcons(uint8_t* icons, uint8_t* pictures) {
    // Once (slot +0x18 still empty): each weapon's icon (def +0x584, '$' -> '_', + ".tga") looked up in the
    // panel's pictures into +0x18 + i * 4 (the default picture 0x0095f904 -> 0); then every one of the 0x84
    // pictures from +8 gets colour 0xffffffff (vtable +0x70).
    if (*reinterpret_cast<uint32_t*>(icons + 0x18)) return;
    char name[0x84] = {};
    for (int32_t i = 1; i <= title_global<int32_t>(0x0095f4d4); ++i) {  // bg_lastWeaponIndex
        const uint8_t* const def = reinterpret_cast<const uint8_t*>(
            uintptr_t{reinterpret_cast<const uint32_t*>(title_global<uint8_t*>(0x0095f4d8))[i]});  // bg_weaponDefs
        const char* const icon = reinterpret_cast<const char*>(uintptr_t{*reinterpret_cast<const uint32_t*>(def + 0x584)});
        if (!icon[0]) continue;
        for (int32_t c = 0; c < 0x7c; ++c) {
            if (!icon[c]) {
                name[c] = 0;
                break;
            }
            name[c] = icon[c] == '$' ? '_' : icon[c];
        }
        char* const tail = name + std::strlen(name);
        std::memcpy(tail, reinterpret_cast<const void*>(uintptr_t{0x0082dc0c}), 4);  // ".tga"
        tail[4] = title_global<char>(0x0082dc10);
        uint8_t* picture = Panel_FindPicture(pictures, name);
        reinterpret_cast<uint8_t**>(icons + 0x18)[i] = picture;
        if (picture == title_global<uint8_t*>(0x0095f904)) reinterpret_cast<uint8_t**>(icons + 0x18)[i] = nullptr;
    }
    uint8_t** const slots = reinterpret_cast<uint8_t**>(icons + 8);
    for (uint32_t i = 0; i < 0x84; ++i) {
        uint8_t* const picture = slots[i];
        if (!picture) continue;
        const uint32_t vtable = *reinterpret_cast<uint32_t*>(picture);
        title_function<void XB_THISCALL(uint8_t*, uint32_t)>(*reinterpret_cast<uint32_t*>(uintptr_t{vtable + 0x70}))(
            picture, 0xffffffff);
    }
}

PLATFORM_ASM_ENTRY(Scn_ResetZoneFlags_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(Scn_ResetZoneFlags) "addl $4, %esp\n"
                   "ret")

void XB_CDECL Scn_ResetZoneFlags(uint8_t* scene) {
    // The flag array (+0x194: count, bytes; an index past the count reads byte 0) re-read every step.
    uint32_t* flags = *reinterpret_cast<uint32_t**>(scene + 0x194);
    if (flags == nullptr) return;
    const uint8_t* const state = title_global<uint8_t*>(0x0095f4e0);
    const bool restoring = state[0] != 0 && state[0x390] != 0;
    for (uint32_t i = 0; i < flags[0]; ++i) {
        const uint32_t index = i < flags[0] ? i : 0;
        uint8_t* const bytes = reinterpret_cast<uint8_t*>(flags[1]);
        if (!restoring || bytes[index] == 1) bytes[index] = 0;
        flags = *reinterpret_cast<uint32_t**>(scene + 0x194);
    }
}

}  // namespace game
