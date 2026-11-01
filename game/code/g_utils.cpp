#include "g_utils.h"

#include <cstring>

#include "actor.h"
#include "dobj.h"
#include "fx.h"
#include "lanes.h"
#include "qcommon.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "title_data.h"

namespace game {

PLATFORM_ASM_ENTRY(vectoangles_call,
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "movl 12(%esp), %esi\n"
                   "movl 16(%esp), %edi\n"
                   "movl $0x002a0850, %eax\n"
                   "call *%eax\n"
                   "popl %edi\n"
                   "popl %esi\n"
                   "ret")

namespace {

uint32_t& word_at(uint8_t* base, uint32_t offset) { return *reinterpret_cast<uint32_t*>(base + offset); }

// Copies a vector into a trajectory base (base) and the current value (current), clearing
// the trajectory's delta (delta) and the three words at clear.
void set_trajectory(uint8_t* ent, const float* value, uint32_t base, uint32_t delta, uint32_t clear,
                    uint32_t current) {
    const uint32_t* words = reinterpret_cast<const uint32_t*>(value);
    for (uint32_t i = 0; i < 3; ++i) word_at(ent, base + i * 4) = words[i];
    for (uint32_t i = 0; i < 3; ++i) word_at(ent, delta + i * 4) = 0;
    for (uint32_t i = 0; i < 3; ++i) word_at(ent, clear + i * 4) = 0;  // 0.0f
    for (uint32_t i = 0; i < 3; ++i) word_at(ent, current + i * 4) = words[i];
}

}  // namespace

uint8_t* XB_REGS1 XB_CDECL G_EntHandleToEnt(const uint32_t* handle) {
    const uint32_t value = *handle;
    const uint32_t index = value & 0xfff;
    if (index >= 0x540) return nullptr;
    if ((value >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return nullptr;
    return title_global<uint8_t*>(0x0097a968 + index * 8);
}

uint8_t* XB_REGS3 XB_CDECL G_SetOrigin(uint8_t* ent, uint32_t, const float* origin) {
    set_trajectory(ent, origin, 0x20, 0x14, 0x2c, 0x140);
    return ent;
}

uint8_t* XB_REGS3 XB_CDECL G_SetOrigin2(uint8_t* ent, uint32_t, const float* origin) {
    set_trajectory(ent, origin, 0x20, 0x14, 0x2c, 0x140);
    return ent;
}

uint8_t* XB_REGS3 XB_CDECL G_SetAngle(uint8_t* ent, uint32_t, const float* angles) {
    set_trajectory(ent, angles, 0x44, 0x38, 0x50, 0x150);
    return ent;
}

uint8_t* XB_REGS3 XB_CDECL G_SetAngle2(uint8_t* ent, uint32_t, const float* angles) {
    set_trajectory(ent, angles, 0x44, 0x38, 0x50, 0x150);
    return ent;
}

uint8_t* XB_REGS1 XB_CDECL G_TriggerContentsFromSpawnflags(uint8_t* ent) {
    const uint32_t flags = word_at(ent, 0x2a0);
    uint32_t& contents = word_at(ent, 0x134);
    contents = 0;
    if (!(flags & 8)) contents = 0x40000000;
    if (flags & 1) contents |= 0x40000;
    if (flags & 2) contents |= 0x80000;
    if (flags & 4) contents |= 0x100000;
    if (flags & 0x10) contents |= 8;
    return ent;
}

PLATFORM_ASM_ENTRY(G_FindEntityByIntField_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(G_FindEntityByIntField) "addl $12, %esp\n"
                   "ret $4")

uint8_t* XB_CDECL G_FindEntityByIntField(const uint8_t* list, uint32_t offset, int32_t value) {
    const uint8_t* const* entry = reinterpret_cast<const uint8_t* const*>(list + 0x2ab0);
    const uint32_t count = *reinterpret_cast<const uint32_t*>(list + 0x2aac);
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t* ent = const_cast<uint8_t*>(entry[i]);
        if (!ent) continue;
        const int32_t field = *reinterpret_cast<const int32_t*>(ent + offset);
        if (field && field == value) return ent;
    }
    return nullptr;
}

namespace {

// SV_Trace (0x002e9af0): eleven stack words and EBX (the start point), caller pops.
extern "C" PLATFORM_HIDDEN void XB_CDECL sv_trace_call(uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                                                       uint32_t a6, uint32_t a7, uint32_t a8, uint32_t a9,
                                                       uint32_t a10, uint32_t a11, uint32_t ebx);
PLATFORM_ASM_ENTRY(sv_trace_call,
                   "pushl %ebx\n"
                   "movl 52(%esp), %ebx\n"
                   "pushl 48(%esp)\n"
                   "pushl 48(%esp)\n"
                   "pushl 48(%esp)\n"
                   "pushl 48(%esp)\n"
                   "pushl 48(%esp)\n"
                   "pushl 48(%esp)\n"
                   "pushl 48(%esp)\n"
                   "pushl 48(%esp)\n"
                   "pushl 48(%esp)\n"
                   "pushl 48(%esp)\n"
                   "pushl 48(%esp)\n"
                   "movl $0x002e9af0, %eax\n"
                   "call *%eax\n"
                   "addl $44, %esp\n"
                   "popl %ebx\n"
                   "ret")

void list190_set(const uint8_t* ent, uint32_t key, uint32_t value) {
    const uint32_t* list = *reinterpret_cast<const uint32_t* const*>(ent + 0x190);
    if (!list) return;
    const uint32_t count = list[0];
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t* records = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(list[1]));
        if (records[(i < count ? i : 0) * 3 + 1] != key) continue;
        records[(i < list[0] ? i : 0) * 3 + 2] = value;
        return;
    }
}

}  // namespace

void XB_CDECL G_Trace(uint32_t results, uint32_t start, uint32_t mins, uint32_t maxs, uint32_t end, uint32_t pass,
                      uint32_t mask) {
    sv_trace_call(results, mins, maxs, end, pass, mask, 0, 0, 0, 0, 0, start);
}

void XB_CDECL G_TraceCapsule(uint32_t results, uint32_t start, uint32_t mins, uint32_t maxs, uint32_t end,
                             uint32_t pass, uint32_t mask) {
    sv_trace_call(results, mins, maxs, end, pass, mask, 1, 0, 0, 0, 0, start);
}

// SV_Trace keeps pointers derived from its stack (results by address 0x50 apart otherwise): it
// runs at the original's ESP with the two bound vectors where the original's aligned frame
// has them (push ebp; and esp,-16; sub esp,0x28: vectors at -0x20 and -0x10 from the aligned
// base; push ebx, esi).
PLATFORM_ASM_ENTRY(G_LocationalTrace_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %eax, %ebp\n"
                   "pushl %edi\n"
                   "pushl 28(%ebp)\n"
                   "pushl 24(%ebp)\n"
                   "pushl 20(%ebp)\n"
                   "pushl 16(%ebp)\n"
                   "pushl 12(%ebp)\n"
                   "pushl 8(%ebp)\n"
                   "pushl 4(%ebp)\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(G_LocationalTrace) "addl $36, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL G_LocationalTrace(uint32_t entry, uint32_t results, uint32_t start, uint32_t end, uint32_t pass,
                                uint32_t mask, uint32_t a6, uint32_t a7, uint32_t edi) {
    const uint32_t base = (entry - 4) & ~15u;  // after push ebp; and esp,-16
    const uint32_t frame = base - 0x28;
    const uint32_t maxs = frame + 8, mins = frame + 0x18;
    for (uint32_t i = 0; i < 4; ++i) {
        const uint32_t value = title_global<uint32_t>(0x0082e290 + i * 4);  // {0, 0, 0, 1}
        *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(maxs + i * 4)) = value;
        *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(mins + i * 4)) = value;
    }
    const TitleRegs regs = {mins, start, results, maxs, title_global<uint32_t>(0x0082e29c), edi};
    const uint32_t args[11] = {results, mins, maxs, end, pass, mask, 0, 1, a6, 1, a7};
    title_call_at_regs(frame - 8, 0x002e9af0, &regs, 11, args);  // SV_Trace
}

PLATFORM_ASM_ENTRY(G_FreeFieldStrings_entry,
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(G_FreeFieldStrings) "addl $8, %esp\n"
                   "ret")

void XB_CDECL G_FreeFieldStrings(const uint32_t* fields, uint8_t* object) {
    for (const uint32_t* field = fields; field[1]; field += 2) {
        if (field[1] != 0x10) continue;
        uint32_t* slot = reinterpret_cast<uint32_t*>(object + field[0]);
        if (!*slot) continue;
        title_function<void XB_THISCALL(uint32_t)>(0x0011d5b0)(*slot);  // ScriptStringRep_Release
        *slot = 0;
    }
}

void XB_REGS3 XB_CDECL G_SetEntityInList960070(uint32_t, uint32_t flag, uint8_t* ent) {
    uint32_t* list = &title_global<uint32_t>(0x00960074);
    int32_t& count = title_global<int32_t>(0x00960070);
    const bool member = word_at(ent, 0x2a4) & 0x200000;
    if (static_cast<uint8_t>(flag)) {
        if (member) return;
        word_at(ent, 0x2a4) |= 0x200000;
        if (count >= 0x20) return;
        list[count] = word_at(ent, 0x224);
        ++count;
        return;
    }
    if (!member) return;
    const uint32_t number = word_at(ent, 0x224);
    int32_t remaining = count;
    for (int32_t i = 0; i < remaining; ++i) {
        if (list[i] != number) continue;
        if (remaining) count = --remaining;
        const uint32_t last = list[remaining];
        if (i != remaining) {
            list[i] = last;
            return;
        }
    }
}

PLATFORM_ASM_ENTRY(G_FindEntsByField_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(G_FindEntsByField) "addl $12, %esp\n"
                   "ret")

uint32_t XB_CDECL G_FindEntsByField(uint32_t* out, uint32_t offset, uint32_t value) {
    uint8_t* const* ents = &title_global<uint8_t*>(0x0097d370);
    const uint32_t total = title_global<uint32_t>(0x0097d36c);
    uint32_t found = 0;
    for (uint32_t i = 0; i < total; ++i) {
        uint8_t* ent = ents[i];
        if (!ent || word_at(ent, offset) != value) continue;
        if (!out) {
            if (uint8_t* vehicle = *reinterpret_cast<uint8_t**>(ent + 0x248)) word_at(vehicle, 0x200) = 1;
            return word_at(ent, 0x224);
        }
        out[found++] = word_at(ent, 0x224);
    }
    return found;
}

void XB_REGS3 XB_CDECL G_RegisterWeapon(uint32_t, uint32_t, uint32_t weapon, uint32_t precache) {
    uint32_t* registered = &title_global<uint32_t>(0x00950230);
    if (registered[weapon]) return;
    const uint32_t chained = title_global<uint32_t>(0x0096bd34 + weapon * 0x34);
    title_global<uint32_t>(0x0098601c) = 1;
    registered[weapon] = 1;
    if (chained == 1) {
        const uint8_t* const* defs = title_global<const uint8_t* const*>(0x0095f4d8);  // bg_weaponDefs
        uint32_t current = weapon;
        do {
            registered[current] = 1;
            current = *reinterpret_cast<const uint32_t*>(defs[current] + 0x718);
        } while (current && current != weapon);
    }
    if (precache) title_global<uint32_t>(0x00986004) = 1;
}

void XB_REGS1 XB_CDECL Vehicle_StopEffects(const uint8_t* ent) {
    uint8_t* vehicle = *reinterpret_cast<uint8_t* const*>(ent + 0x248);
    const int16_t def_index = *reinterpret_cast<const int16_t*>(vehicle + 0x178);
    const uint8_t* def = title_global<const uint8_t*>(0x00950a28 + def_index * 4);  // g_vehicleDefs
    const int32_t count = *reinterpret_cast<const uint16_t*>(def + 0x20) != 1 ? 6 : 4;
    const auto free_effect = [](uint32_t& id) {
        if (id == 0xffffffff) return;
        FX_FreeEffectsById(title_global<FxIdList*>(0x0095f5b4), id);
        id = 0xffffffff;
    };
    for (int32_t i = 0; i < count; ++i) free_effect(word_at(vehicle, 0x21c + i * 4));
    free_effect(word_at(vehicle, 0x234));
}

void XB_CDECL G_InitTurretAnimScripts() {
    const int32_t last = title_global<int32_t>(0x0095f4d4);  // bg_lastWeaponIndex
    const uint8_t* const* defs = title_global<const uint8_t* const*>(0x0095f4d8);
    for (int32_t i = 0; i <= last; ++i) {
        const uint8_t* def = defs[i];
        if (*reinterpret_cast<const int32_t*>(def + 0x98) != 7) continue;
        uint32_t* record = &title_global<uint32_t>(0x00987bf8 + i * 0x10);
        record[0] = record[1] = record[2] = record[3] = 0;
        const char* name = *reinterpret_cast<const char* const*>(def + 0x864);
        if (!*name) continue;
        const uint32_t hash = Com_HashKeyNoCase(0, name);
        uint32_t function = 0;
        for (uint32_t j = 0; j < 0x34; ++j) {
            if (title_global<uint32_t>(0x00927170 + j * 8) == hash) {  // g_broFuncs {hash, function}
                function = title_global<uint32_t>(0x00927174 + j * 8);
                break;
            }
        }
        record[2] = function;
    }
}

void XB_CDECL SP_info_notnull(uint8_t* ent) {
    set_trajectory(ent, reinterpret_cast<const float*>(ent + 0x140), 0x20, 0x14, 0x2c, 0x140);
}
void XB_CDECL SP_info_notnull_big(uint8_t* ent) {
    set_trajectory(ent, reinterpret_cast<const float*>(ent + 0x140), 0x20, 0x14, 0x2c, 0x140);
}

void XB_REGS2 XB_CDECL Vehicle_ClearEntityRefs(uint32_t, const uint8_t* ent) {
    for (uint32_t i = 0; i < title_global<uint16_t>(0x00986000); ++i) {  // g_vehicleSlotCount
        uint8_t* vehicle = title_global<uint8_t*>(0x0095f694) + i * 0x340;  // g_vehicles
        if (!word_at(vehicle, 0x170)) continue;
        if (word_at(vehicle, 0x204) == *reinterpret_cast<const uint32_t*>(ent + 0x224)) word_at(vehicle, 0x204) = 0;
        if (word_at(vehicle, 0x208) == *reinterpret_cast<const uint32_t*>(ent + 0x224)) word_at(vehicle, 0x208) = 0;
        if (word_at(vehicle, 0x1cc) == *reinterpret_cast<const uint32_t*>(ent + 0x224)) word_at(vehicle, 0x1cc) = 0;
    }
}

void XB_CDECL SP_sound_blend(uint8_t* ent) {
    word_at(ent, 0x134) = 0;
    word_at(ent, 0x14) = 0;
    word_at(ent, 0x38) = 0;
    ent[4] = 0;
    ent[5] = 0;
    ent[0] = 8;
    word_at(ent, 0xbc) = 0;  // 0.0f
    word_at(ent, 0xd4) |= 0x20;
}

void XB_CDECL SP_script_vehicle_collmap(uint8_t* ent) {
    word_at(ent, 0x134) = 0;
    ent[0] = 0x10;
}

namespace {

// PhysBody_Clear (0x002492f0): body in EDI.
extern "C" PLATFORM_HIDDEN void XB_CDECL phys_body_clear_call(uint8_t* body);
PLATFORM_ASM_ENTRY(phys_body_clear_call,
                   "pushl %edi\n"
                   "movl 8(%esp), %edi\n"
                   "movl $0x002492f0, %eax\n"
                   "call *%eax\n"
                   "popl %edi\n"
                   "ret")

}  // namespace

void XB_CDECL Vehicle_ResetPool() {
    for (int16_t i = 0; i < title_global<uint16_t>(0x00986000); ++i) {  // g_vehicleSlotCount
        phys_body_clear_call(title_global<uint8_t*>(0x0095f694) + i * 0x340);
        word_at(title_global<uint8_t*>(0x0095f694) + i * 0x340, 0x170) = 0;
    }
    title_global<uint32_t>(0x0098537c) = title_global<uint32_t>(0x0095f694);
}

uint8_t* XB_CDECL Vehicle_FindCollmap(const char* name) {
    uint8_t* const* ents = &title_global<uint8_t*>(0x0097d370);
    const uint32_t total = title_global<uint32_t>(0x0097d36c);
    for (uint32_t i = 0; i < total; ++i) {
        uint8_t* ent = ents[i];
        if (!ent || word_at(ent, 0x260) != title_global<uint32_t>(0x0097a798)) continue;  // script_vehicle_collmap
        const uint8_t* rep = *reinterpret_cast<const uint8_t* const*>(ent + 0x264);
        const char* text = rep ? reinterpret_cast<const char*>(rep + 6) : nullptr;
        if (!title_function<int32_t XB_CDECL(const char*, const char*)>(0x001f1443)(text, name)) return ent;  // __stricmp
    }
    return nullptr;
}

uint8_t* XB_CDECL Vehicle_FromEntityHandle(uint32_t handle) {
    const auto va = title_function<const char* XB_CDECL(const char*)>(0x002a93d0);
    const uint32_t index = handle & 0xfff;
    uint8_t* ent = nullptr;
    if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
        ent = title_global<uint8_t*>(0x0097a968 + index * 8);
    if (!ent) {
        va(reinterpret_cast<const char*>(uintptr_t{0x0083a110}));  // "Is not a valid entity handle"
        return nullptr;
    }
    if (word_at(ent, 0x260) != title_global<uint32_t>(0x0097a790))  // script_vehicle
        va(reinterpret_cast<const char*>(uintptr_t{0x0083a158}));
    if (!word_at(ent, 0x248)) va(reinterpret_cast<const char*>(uintptr_t{0x0083a130}));
    return ent;
}

PLATFORM_ASM_ENTRY(G_EntList190_Disable_entry,
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(G_EntList190_Disable) "addl $8, %esp\n"
                   "ret")

void XB_CDECL G_EntList190_Disable(const uint8_t* ent, uint32_t key) { list190_set(ent, key, 0); }

PLATFORM_ASM_ENTRY(G_EntList190_Enable_entry,
                   "pushl %ebx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(G_EntList190_Enable) "addl $8, %esp\n"
                   "ret")

void XB_CDECL G_EntList190_Enable(const uint8_t* ent, uint32_t key) { list190_set(ent, key, 1); }

PLATFORM_ASM_ENTRY(G_SpawnInt_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(G_SpawnInt) "addl $12, %esp\n"
                   "ret")

uint32_t XB_CDECL G_SpawnInt(uint32_t key, int32_t* out, int32_t fallback) {
    const int32_t count = title_global<int32_t>(0x00985420);  // level_numSpawnVars
    for (int32_t i = 0; i < count; ++i) {
        if (title_global<uint32_t>(0x00985424 + i * 8) != key) continue;  // level_spawnVars {key, value}
        *out = title_function<int32_t XB_CDECL(const char*)>(0x001eeb81)(  // _atol
            title_global<const char*>(0x00985428 + i * 8));
        return 1;
    }
    *out = fallback;
    return 0;
}

PLATFORM_ASM_ENTRY(G_SpawnFloat_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(G_SpawnFloat) "addl $12, %esp\n"
                   "ret")

uint32_t XB_CDECL G_SpawnFloat(uint32_t key, float fallback, float* out) {
    const int32_t count = title_global<int32_t>(0x00985420);  // level_numSpawnVars
    for (int32_t i = 0; i < count; ++i) {
        if (title_global<uint32_t>(0x00985424 + i * 8) != key) continue;
        // atof (0x001ef056) leaves a double in ST0; declared float, so the store rounds as the
        // original's FSTP DWORD does.
        *out = title_function<float XB_CDECL(const char*)>(0x001ef056)(title_global<const char*>(0x00985428 + i * 8));
        return 1;
    }
    *out = fallback;
    return 0;
}

uint8_t* XB_CDECL G_FindAnimTreeByName(const char* name) {
    if (!name) return nullptr;
    const uint32_t* list = *reinterpret_cast<const uint32_t* const*>(title_global<const uint8_t*>(0x0095f524) + 4);
    for (uint32_t i = 1; i < list[0]; ++i) {
        uint8_t* record = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(list[1])) + (i < list[0] ? i : 0) * 12;
        if (!title_function<int32_t XB_CDECL(const char*, const char*)>(0x001f1443)(  // __stricmp
                *reinterpret_cast<const char* const*>(record), name))
            return record;
    }
    return nullptr;
}

uint8_t* XB_REGS1 XB_CDECL G_TestEntityPosition(const uint8_t* ent, const float* origin) {
    uint32_t mask = *reinterpret_cast<const uint32_t*>(ent + 0x2a8);
    if (!mask)
        mask = 0x11;
    else if (*reinterpret_cast<const uint32_t*>(ent + 0x134) == 0x4000000)
        return nullptr;
    alignas(16) uint8_t trace[0x40] = {};  // SV_Trace uses MOVAPS on the trace and the end point
    *reinterpret_cast<uint32_t*>(trace + 0x30) = 0;  // hit entity handle
    alignas(16) float end[4] = {origin[0], origin[1], origin[2] + title_global<float>(0x0083ebb8)};  // 16
    const auto address = [](const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); };
    const uint8_t type = ent[0];
    const uint32_t pass = *reinterpret_cast<const uint32_t*>(ent + (type == 3 ? 0x1a0 : 0x224));
    sv_trace_call(address(trace), address(ent + 0xe0), address(ent + 0xf0), address(end), pass, mask,
                  type == 1 ? 1 : 0, 0, 0, 0, 0, address(origin));
    if (!trace[0x3d]) return nullptr;  // start solid
    return G_EntHandleToEnt(reinterpret_cast<const uint32_t*>(trace + 0x30));
}

uint32_t actor_anim_tree(const uint8_t* actor) {
    uint8_t* ent = *reinterpret_cast<uint8_t* const*>(actor);
    if (!word_at(ent, 0x254)) {
        const uint32_t tree = title_function<uint32_t XB_CDECL(uint8_t*, uint32_t)>(0x002c4a00)(  // AnimTree_Create
            ent, title_global<uint32_t>(0x00987a90));
        word_at(*reinterpret_cast<uint8_t* const*>(actor), 0x254) = tree;
    }
    return word_at(*reinterpret_cast<uint8_t* const*>(actor), 0x254);
}

namespace {

uint32_t ent_anim_tree(const uint8_t* ent) {
    if (ent[0] != 0xb) return *reinterpret_cast<const uint32_t*>(ent + 0x254);
    return actor_anim_tree(*reinterpret_cast<const uint8_t* const*>(ent + 0x240));
}

}  // namespace

uint32_t XB_REGS1 XB_CDECL G_GetEntAnimTree(const uint8_t* ent) { return ent_anim_tree(ent); }
uint32_t XB_REGS1 XB_CDECL G_GetEntityAnimTree(const uint8_t* ent) { return ent_anim_tree(ent); }

uint8_t* XB_REGS1 XB_CDECL G_AddEvent(uint8_t* ent, uint32_t event, uint32_t parm) {
    if (uint8_t* ps = *reinterpret_cast<uint8_t**>(ent + 0x23c)) {
        word_at(ps, 0xc8 + (word_at(ps, 0xc4) & 3) * 4) = event & 0xff;
        word_at(ps, 0xd8 + (word_at(ps, 0xc4) & 3) * 4) = parm & 0xff;
        ++word_at(ps, 0xc4);
    } else {
        ent[0xb4 + (word_at(ent, 0xb0) & 3)] = static_cast<uint8_t>(event);
        ent[0xb8 + (word_at(ent, 0xb0) & 3)] = static_cast<uint8_t>(parm);
        ++word_at(ent, 0xb0);
    }
    word_at(ent, 0x1a8) = title_global<uint32_t>(0x0098540c);  // level time
    return ent;
}

// G_LocationalTrace runs where the original's call puts it (aligned frame, 0x18 locals, two saved
// registers, seven arguments); the C frame sits below a gap.
PLATFORM_ASM_ENTRY(G_MissileTrace_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %edx, %ebp\n"
                   "pushl %edi\n"
                   "pushl %ecx\n"
                   "pushl %eax\n"
                   "pushl 12(%ebp)\n"
                   "pushl 8(%ebp)\n"
                   "pushl 4(%ebp)\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(G_MissileTrace) "addl $28, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret")

void XB_CDECL G_MissileTrace(uint32_t entry, uint8_t* results, const float* start, uint32_t pass, uint32_t a6, uint32_t mask,
                             const float* end) {
    const uint32_t top = ((entry - 4) & ~15u) - 0x20;
    const uint32_t args[] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(results)),
                             static_cast<uint32_t>(reinterpret_cast<uintptr_t>(start)),
                             static_cast<uint32_t>(reinterpret_cast<uintptr_t>(end)), pass, mask, a6, 0};
    const TitleRegs regs = {a6, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(start)), mask, pass,
                            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(results)),
                            static_cast<uint32_t>(reinterpret_cast<uintptr_t>(end))};
    title_call_at_regs(top, 0x002600c0, &regs, 7, args);  // G_LocationalTrace
    if (!results[0x3d]) return;
    if (*reinterpret_cast<const uint32_t*>(results + 0x28) & 0x800) {
        // Started inside something that does not block missiles: no hit.
        results[0x3d] = 0;
        *reinterpret_cast<float*>(results + 0x20) = title_global<float>(0x0083eb20);
        return;
    }
    *reinterpret_cast<float*>(results + 0x20) = 0.0f;
    float* normal = reinterpret_cast<float*>(results + 0x10);
    for (int32_t i = 0; i < 4; ++i) normal[i] = start[i] - end[i];  // all four lanes, as the title's SUBPS
}

namespace {

// tan(degrees * pi / 180) on the x87 through the title CRT's __CItan, stored as float.
float title_tan_degrees(float degrees, float pi, float half_turn) {
    float result;
    __asm__ volatile("flds %1\n\tfmuls %2\n\tfdivs %3\n\tcall *%4\n\tfstps %0"
                     : "=m"(result)
                     : "m"(degrees), "m"(pi), "m"(half_turn), "r"(uintptr_t{0x0033af10})
                     : "eax", "ecx", "edx", "st", "memory");
    return result;
}

int32_t title_rand() { return title_function<int32_t XB_CDECL()>(0x001efcc0)(); }

}  // namespace

PLATFORM_ASM_ENTRY(Bullet_RandomSpread_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(Bullet_RandomSpread) "addl $12, %esp\n"
                   "ret")

void XB_CDECL Bullet_RandomSpread(const uint8_t* fire, float* end, float spread) {
    const auto f = [fire](uint32_t offset) { return *reinterpret_cast<const float*>(fire + offset); };
    const uint8_t* weapon = *reinterpret_cast<uint8_t* const*>(fire + 0x3c);
    const float range = *reinterpret_cast<const int32_t*>(weapon + 0xa8) == 5 ? title_global<float>(0x0083ee50)
                                                                              : title_global<float>(0x00830bd8);
    const float pi = title_global<float>(0x0083ed00), half_turn = title_global<float>(0x0083eb60);
    const float radius = title_tan_degrees(spread, pi, half_turn) * range;
    const float scale = title_global<float>(0x0083ebc8);
    const float angle = static_cast<float>(title_rand()) / scale * title_global<float>(0x0083ebcc);
    const float distance = static_cast<float>(title_rand()) / scale;
    float sine, cosine;
    FastSinCos(&sine, 0, &cosine, angle * pi / half_turn);
    const float u = cosine * distance * radius;  // along the right axis (+0xc)
    const float v = sine * distance * radius;    // along the up axis (+0x18)
    for (int32_t i = 0; i < 3; ++i) end[i] = f(4 * i) * range + f(0x24 + 4 * i);
    for (int32_t i = 0; i < 3; ++i) end[i] = f(0xc + 4 * i) * u + end[i];
    for (int32_t i = 0; i < 3; ++i) end[i] = f(0x18 + 4 * i) * v + end[i];
}

uint8_t* XB_REGS1 XB_CDECL G_EntityList_Compact(uint8_t* list) {
    int32_t& count = *reinterpret_cast<int32_t*>(list + 0x2aac);
    uint8_t** const slots = reinterpret_cast<uint8_t**>(list + 0x2ab0);
    for (int32_t i = 0; i < count; ++i) {
        if (slots[i]) continue;
        if (count) --count;
        uint8_t* const last = slots[count];
        if (last) {
            *reinterpret_cast<int32_t*>(last + 0x228) = i;
            slots[i] = last;
        } else {
            --i;  // look at this slot again
        }
    }
    return list;
}

void XB_CDECL Path_BaseName(char* dest, int32_t* length, const char* path, int32_t size, uint8_t strip_extension) {
    int32_t slash = -1;
    int32_t dot = size;
    for (int32_t i = 0; i < size; ++i) {
        const char c = path[i];
        if (c == '\\' || c == '/')
            slash = i;
        else if (c == '.')
            dot = i;
    }
    if (strip_extension && dot) {
        Q_strncpySubLen(dest, length, path, slash + 1, dot - slash - 1, 0xffff);
        return;
    }
    const int32_t start = slash + 1;
    if (start >= 0xffff) return;
    int32_t end = (start - slash) + size - 1;
    if (end > 0xfffe) end = 0xfffe;
    int32_t copied = 0;
    for (int32_t i = start; i < end && path[i]; ++i, ++copied) dest[i - start] = path[i];
    *length = copied;
    dest[copied] = 0;
}

PLATFORM_ASM_ENTRY(AddLeanToPosition_entry,
                   "subl $4, %esp\n"
                   "movss %xmm1, (%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(AddLeanToPosition) "addl $24, %esp\n"
                   "ret")

void XB_CDECL AddLeanToPosition(float* position, float lean, float yaw, float a, float b) {
    if (lean == 0.0f) return;
    uint32_t bits;
    std::memcpy(&bits, &lean, 4);
    bits &= 0x7fffffff;
    float magnitude;
    std::memcpy(&magnitude, &bits, 4);
    const float f = (2.0f - magnitude) * lean;
    const float roll = f * a;
    const float deg = title_global<float>(0x0083edd8);
    float sin_yaw, cos_yaw, sin_roll, cos_roll;
    FastSinCos(&sin_yaw, 0, &cos_yaw, yaw * deg);
    FastSinCos(&sin_roll, 0, &cos_roll, roll * deg);
    const float k = f * b;
    const float x = (cos_roll * sin_yaw) * k + position[0];
    const float y = (0.0f - cos_roll * cos_yaw) * k + position[1];
    const float z = (0.0f - sin_roll) * k + position[2];
    position[0] = x;
    position[1] = y;
    position[2] = z;
}

uint8_t* XB_REGS2 XB_CDECL Phys_FindBodyForEntity(uint32_t, const uint8_t* entity) {
    // Walks to records + count * 4 (32-bit wrap): count mod 2^30 records.
    const uint32_t end = 0x008fccf0 + title_global<uint32_t>(0x008fcde4) * 4;
    for (uint32_t at = 0x008fccf0; at != end; at += 4) {
        uint8_t* const record = title_global<uint8_t*>(at);
        if (*reinterpret_cast<const uint8_t* const*>(record + 4) == entity)
            return *reinterpret_cast<uint8_t* const*>(record + 8);
    }
    return nullptr;
}

uint8_t* XB_CDECL multi_wait(uint8_t* entity) {
    *reinterpret_cast<uint32_t*>(entity + 0x328) = 0;
    return entity;
}

PLATFORM_ASM_ENTRY(Entity_YawToForward_entry, "pushl %esi\n"
                                              "pushl %eax\n" PLATFORM_ASM_CALL(Entity_YawToForward) "addl $8, %esp\n"
                                              "ret")

void XB_CDECL Entity_YawToForward(uint8_t* const* entity, float* out) {
    const float yaw = *reinterpret_cast<const float*>(*entity + 0x154) * title_global<float>(0x0083edd8);
    float cosine;
    float sine;
    FastSinCos(&sine, 0, &cosine, yaw);
    if (!out) return;
    out[0] = cosine;
    out[1] = sine;
    out[2] = 0.0f;
}

void XB_CDECL hurt_use(uint8_t* self) {
    uint32_t& state = *reinterpret_cast<uint32_t*>(self + 0x338);
    state = state ? 0 : 3;
    const float wait = *reinterpret_cast<const float*>(self + 0x38c);
    if (!(wait == title_global<float>(0x0083eafc))) {
        const int32_t time = title_global<int32_t>(0x0098540c);  // level time
        *reinterpret_cast<int32_t*>(self + 0x328) = time + 50;
        *reinterpret_cast<uint32_t*>(self + 0x32c) = 0xe;
        *reinterpret_cast<float*>(self + 0x384) = wait * title_global<float>(0x0083eb9c) + float(time);
    }
}

void XB_CDECL Use_trigger_use(uint8_t* self, const uint8_t* other) {
    const float time = float(title_global<int32_t>(0x0098540c));
    float& next = *reinterpret_cast<float*>(self + 0x384);
    if (!(time > next)) return;
    next = *reinterpret_cast<const float*>(self + 0x38c) + time;
    if (*reinterpret_cast<const uint32_t*>(other + 0x23c)) return;
    *reinterpret_cast<uint32_t*>(self + 0x2a0) ^= 1;
}

char* XB_REGS1 XB_CDECL G_AddSpawnVarToken(const char* token) {
    const uint32_t length = uint32_t(std::strlen(token));
    if (int32_t(title_global<uint32_t>(0x00985624) + length + 1) > 0x800)
        title_function<void XB_CDECL(const char*)>(0x00248af0)(&title_global<const char>(0x00839d74));  // G_Error
    char* const copy = &title_global<char>(0x00985628 + title_global<uint32_t>(0x00985624));
    std::memcpy(copy, token, length + 1);
    title_global<uint32_t>(0x00985624) += length + 1;
    return copy;
}

uint32_t XB_REGS1 XB_CDECL G_GetEntityZone(const uint8_t* ent) {
    const uint32_t zone = *reinterpret_cast<const uint32_t*>(ent + 0x220);
    if (zone != 0xffffffff) return zone;
    return *reinterpret_cast<const uint32_t*>(title_global<const uint8_t*>(0x0095f638) + 0x34);
}

PLATFORM_ASM_ENTRY(EntBits_IterBegin_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(EntBits_IterBegin) "addl $8, %esp\n"
                   "ret")

uint32_t* XB_CDECL EntBits_IterBegin(const uint32_t* bits, uint32_t* iterator) {
    iterator[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(bits));
    iterator[1] = bits[0];
    iterator[2] = 0;
    iterator[3] = 0xffffffff;
    EntBits_IterNext(iterator);
    return iterator;
}

uint32_t* XB_REGS2 XB_CDECL EntBits_Invert(uint32_t* out, const uint32_t* bits) {
    // All 42 words are read (last first) into a stack copy before the copy out.
    uint32_t inverted[42];
    for (int32_t i = 41; i >= 0; --i) inverted[i] = ~bits[i];
    platform_rep_movs(out, inverted, sizeof(inverted));
    return out;
}

PLATFORM_ASM_ENTRY(G_FreeXModel_entry,
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(G_FreeXModel) "addl $8, %esp\n"
                   "ret")

void XB_CDECL G_FreeXModel(int32_t context_index, uint8_t* model) {
    if (!model) return;
    XModel_Free(model);
    if (context_index != -1) {
        uint8_t* context = nullptr;
        if (context_index >= 0 && context_index < 0x63)
            context = *reinterpret_cast<uint8_t* const*>(title_global<const uint8_t*>(0x0095f638) + 0x38 +
                                                          context_index * 4);
        if (MemContext_Free_r(context, model)) return;
    }
    Z_Free(model);
}

uint32_t* XB_REGS1 XB_CDECL G_EntityList_Init(uint32_t* list) {
    for (int32_t i = 0x28; i >= -1; --i) list[i + 1] = 0;
    for (uint32_t i = 0; i < 0x540; ++i) {
        list[0x2a + i * 2] = 0;
        list[0x2a + i * 2 + 1] = 1;
    }
    list[0xaaa] = 0;
    for (int32_t i = 0; i < 0x540; ++i) list[i >> 5] |= 1u << (i & 0x1f);
    return list;
}

namespace {

// The level's entity pointer list (0x0097d370, count 0x0097d36c), walked up to base + count * 4
// (the pointer wraps like the original's).
template <typename Match>
void gather_entities(uint32_t* out, Match match) {
    const uint32_t base = 0x0097d370;
    const uint32_t end = base + title_global<uint32_t>(0x0097d36c) * 4;
    for (uint32_t slot = base; slot != end; slot += 4) {
        uint8_t* const ent = title_global<uint8_t*>(slot);
        if (!ent || !match(ent)) continue;
        const int32_t count = static_cast<int32_t>(out[0]);
        if (count >= 0x1000) continue;
        out[1 + count] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ent));
        ++out[0];
    }
}

}  // namespace

PLATFORM_ASM_ENTRY(G_GatherEntsByIntField_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(G_GatherEntsByIntField) "addl $12, %esp\n"
                   "ret")

void XB_CDECL G_GatherEntsByIntField(uint32_t* out, uint32_t offset, uint32_t value) {
    gather_entities(out, [&](uint8_t* ent) { return word_at(ent, offset) == value; });
}

PLATFORM_ASM_ENTRY(G_GatherEntsByShortField_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(G_GatherEntsByShortField) "addl $12, %esp\n"
                   "ret")

void XB_CDECL G_GatherEntsByShortField(uint32_t value, uint32_t* out, uint32_t offset) {
    gather_entities(out, [&](uint8_t* ent) { return *reinterpret_cast<const uint16_t*>(ent + offset) == uint16_t(value); });
}

void XB_REGS1 XB_CDECL G_RestoreSavedEntityField134(const uint8_t* saved) {
    for (int32_t i = 0x53f; i >= 0; --i) {
        const uint32_t* const pair = reinterpret_cast<const uint32_t*>(saved + 4 + i * 8);
        const uint32_t handle = pair[0];
        if (!handle) continue;
        // A stale handle gives entity 0: the original then writes to address 0x134 too.
        uintptr_t ent = 0;
        const uint32_t index = handle & 0xfff;
        if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
            ent = title_global<uint32_t>(0x0097a968 + index * 8);
        *reinterpret_cast<uint32_t*>(ent + 0x134) = pair[1];
    }
}

namespace {

Lanes splat(float f) { return {{f, f, f, f}}; }

Lanes title_lanes(uint32_t address) {
    const float* const p = &title_global<const float>(address);
    return {{p[0], p[1], p[2], p[3]}};
}

Lanes div_lanes(const Lanes& a, const Lanes& b) {
    return {{a.v[0] / b.v[0], a.v[1] / b.v[1], a.v[2] / b.v[2], a.v[3] / b.v[3]}};
}

// MAXPS(a, b) / MINPS(a, b): b when the comparison fails (also on NaN).
Lanes max_ps(const Lanes& a, const Lanes& b) {
    Lanes r;
    for (int i = 0; i < 4; ++i) r.v[i] = a.v[i] > b.v[i] ? a.v[i] : b.v[i];
    return r;
}

Lanes min_ps(const Lanes& a, const Lanes& b) {
    Lanes r;
    for (int i = 0; i < 4; ++i) r.v[i] = a.v[i] < b.v[i] ? a.v[i] : b.v[i];
    return r;
}

Lanes abs_ps(const Lanes& a) { return {{abs_bits(a.v[0]), abs_bits(a.v[1]), abs_bits(a.v[2]), abs_bits(a.v[3])}}; }

Lanes rcp_ps(const Lanes& a) {
    return {{platform_rcp_estimate(a.v[0]), platform_rcp_estimate(a.v[1]), platform_rcp_estimate(a.v[2]),
             platform_rcp_estimate(a.v[3])}};
}

Lanes sine_ps(const Lanes& a) {
    return {{scene_sine_poly(a.v[0]), scene_sine_poly(a.v[1]), scene_sine_poly(a.v[2]), scene_sine_poly(a.v[3])}};
}

// One Newton step on a reciprocal estimate: (2 - r * x) * r.
Lanes refine_rcp(const Lanes& r, const Lanes& x) { return mul(sub(splat(2.0f), mul(r, x)), r); }

}  // namespace

float XB_CDECL Water_GetHeight(int32_t index, float x, float y) {
    const uint32_t set = 0x00901b30 + static_cast<uint32_t>(index) * 0xd0;
    const Lanes pi = splat(from_bits(0x40490fdb));
    const Lanes one = splat(1.0f);
    const Lanes zero = splat(0.0f);
    const Lanes magic = splat(from_bits(0x4b400000));  // 1.5 * 2^23: (v + magic) - magic rounds
    // Each wave's direction from its angle in degrees: sin(a + 3 pi / 2) and sin(a) per wave.
    const Lanes angle = div_lanes(mul(title_lanes(set + 0x70), pi), splat(180.0f));
    const float q = from_bits(0x4096cbe2);
    const Lanes quarter = {{q, 0.0f, q, 0.0f}};
    const Lanes dir01 = sine_ps(add({{angle.v[0], angle.v[0], angle.v[1], angle.v[1]}}, quarter));
    const Lanes dir23 = sine_ps(add({{angle.v[2], angle.v[2], angle.v[3], angle.v[3]}}, quarter));
    const Lanes falloff = max_ps(one, title_lanes(set + 0x80));
    const Lanes falloff_rcp = rcp_ps(falloff);
    const Lanes length = max_ps(one, title_lanes(set + 0x90));
    const Lanes length_rcp = rcp_ps(length);
    // The amplitude counts only when it rounds to at least 1 (clamped to 0..1).
    const Lanes amplitude = title_lanes(set + 0xa0);
    const Lanes amplitude_on = min_ps(one, max_ps(zero, sub(add(amplitude, magic), magic)));
    const Lanes scaled_amplitude = mul(amplitude, amplitude_on);
    // Distance along each wave's direction from its origin (+0x50, two waves per vector).
    const Lanes point = {{x, y, x, y}};
    const Lanes d01 = mul(sub(point, title_lanes(set + 0x50)), dir01);
    const Lanes d23 = mul(sub(point, title_lanes(set + 0x60)), dir23);
    const Lanes e23 = add(d23, splat(d23.v[3]));
    const Lanes f23 = add(d23, splat(d23.v[1]));
    const Lanes e01 = add(d01, splat(d01.v[3]));
    const Lanes f01 = add(d01, splat(d01.v[1]));
    platform_keep(e23);
    platform_keep(f23);
    platform_keep(e01);
    platform_keep(f01);
    const Lanes distance = abs_ps({{f01.v[0], e01.v[2], f23.v[0], e23.v[2]}});
    const Lanes phase = title_lanes(set + 0xc0);
    const Lanes phase_whole = sub(add(phase, magic), magic);
    const Lanes time = splat(title_global<float>(0x00901e90));
    const Lanes offset = sub(mul(phase, time), phase_whole);
    const Lanes cycles = add(sub(title_lanes(set + 0xb0), offset), mul(distance, refine_rcp(length_rcp, length)));
    const Lanes wave = sine_ps(mul(mul(cycles, splat(2.0f)), pi));
    const Lanes fade = sub(one, max_ps(zero, min_ps(one, mul(distance, refine_rcp(falloff_rcp, falloff)))));
    const Lanes height = mul(wave, mul(fade, scaled_amplitude));
    const Lanes pairs = add(height, {{height.v[2], height.v[3], height.v[0], height.v[0]}});
    platform_keep(pairs);
    return (pairs.v[0] + pairs.v[1]) + title_global<float>(set);
}

namespace {

// Sums and products with the title's destination operand first (it decides which NaN wins).
float sum_xyz(const Lanes& a) {
    float s = a.v[0];
    s += a.v[1];
    s += a.v[2];
    return s;
}

Lanes mul_ordered(const Lanes& a, const Lanes& b) {
    Lanes r = a;
    for (int i = 0; i < 4; ++i) r.v[i] *= b.v[i];
    return r;
}

// Squared distance of p from the line through a and b: |ap|^2 - (ab . ap)^2 / |ab|^2 (packed,
// every lane computed).
float line_dist_sq(const Lanes& a, const Lanes& b, const Lanes& p) {
    const Lanes ab = sub(b, a);
    const Lanes ap = sub(p, a);
    const Lanes ab_ab = mul(ab, ab);
    const Lanes ap_ap = mul(ap, ap);
    const Lanes ab_ap = mul_ordered(ab, ap);
    platform_keep(ab_ab);
    platform_keep(ap_ap);
    platform_keep(ab_ap);
    float along = sum_xyz(ab_ap);
    along *= along;
    along /= sum_xyz(ab_ab);
    float result = sum_xyz(ap_ap);
    result -= along;
    return result;
}

}  // namespace

float XB_CDECL Tri_MinEdgeLineDistSq(const float* a, const float* b, const float* c, const float* p) {
    const Lanes la = lanes(a), lb = lanes(b), lc = lanes(c), lp = lanes(p);
    float best = line_dist_sq(la, lb, lp);
    const float bc = line_dist_sq(lb, lc, lp);
    if (best > bc) best = bc;
    const float ca = line_dist_sq(la, lc, lp);
    if (best > ca) best = ca;
    const float minimum = from_bits(0x38d1b717);  // 0.0001
    if (minimum > best) best = minimum;
    return best;
}

uint32_t XB_THISCALL G_GetTouchTypeFlag(const uint8_t* ent) {
    if (*reinterpret_cast<const uint32_t*>(ent + 0x248)) return 0x8;
    const uint32_t ragdoll = *reinterpret_cast<const uint32_t*>(ent + 0x244);
    if (!ragdoll) return 0x400000;
    if (*reinterpret_cast<const uint32_t*>(ent + 0x23c)) return 0x40000000;
    const uint32_t kind = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(ragdoll + 4));
    if (kind == 1) return 0x40000;
    if (kind == 2) return 0x80000;
    if (kind == 3) return 0x100000;
    return 0;
}

PLATFORM_ASM_ENTRY(multi_trigger_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(multi_trigger) "addl $8, %esp\n"
                   "ret")

void XB_CDECL multi_trigger(uint32_t activator, uint8_t* ent) {
    word_at(ent, 0x378) = activator;
    if (word_at(ent, 0x32c) == 0x1c || word_at(ent, 0x328)) return;
    const float wait = *reinterpret_cast<const float*>(ent + 0x384);
    if (wait > title_global<float>(0x0083eafc)) {
        // Wait again: next think in (wait + random * spread) seconds.
        word_at(ent, 0x32c) = 0x12;
        const int32_t r = title_function<int32_t XB_CDECL()>(0x001efcc0)();  // _rand
        const int32_t now = title_global<int32_t>(0x0098540c);                 // level.time
        float f = static_cast<float>(r) * title_global<float>(0x0083ecf8);
        f = f - title_global<float>(0x0083eb20);
        f = f * *reinterpret_cast<const float*>(ent + 0x388);
        f = f + *reinterpret_cast<const float*>(ent + 0x384);
        f = f * title_global<float>(0x0083ec74);
        word_at(ent, 0x328) = static_cast<uint32_t>(now - static_cast<int32_t>(f));
        return;
    }
    word_at(ent, 0x338) = 0;
    word_at(ent, 0x328) = title_global<uint32_t>(0x0098540c) + 1;
    word_at(ent, 0x32c) = 0xc;
}

void XB_CDECL Ent_Callback348Stub2() {}

// Actor_GetProneBlend (0x002ad770): prone state in ESI, time in ECX; the XMM0 result's bits.
PLATFORM_ASM_ENTRY(actor_get_prone_blend_call,
                   "pushl %esi\n"
                   "movl 8(%esp), %esi\n"
                   "movl 12(%esp), %ecx\n"
                   "movl $0x002ad770, %eax\n"
                   "call *%eax\n"
                   "movd %xmm0, %eax\n"
                   "popl %esi\n"
                   "ret")

PLATFORM_ASM_ENTRY(Actor_ExitProne_entry,
                   "pushl %eax\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(Actor_ExitProne) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Actor_ExitProne(uint8_t* actor, int32_t duration) {
    uint8_t* const prone = actor + 0x198;
    const int32_t start = static_cast<int32_t>(word_at(prone, 4));
    if (!start || word_at(prone, 0)) return;
    const int32_t blend = static_cast<int32_t>(word_at(prone, 8));
    if (blend) {
        const int32_t now = title_global<int32_t>(0x0098540c);  // level.time
        if (blend < 0) {
            if (start - blend < now) {
                word_at(prone, 4) = 0;
                return;
            }
        } else if (blend + start < now) {
            word_at(prone, 8) = 0;
        }
    }
    const int32_t current = static_cast<int32_t>(word_at(actor, 0x1a0));
    if (current && duration != current) {
        const uint32_t bits = actor_get_prone_blend_call(prone, title_global<int32_t>(0x0098540c));
        const int32_t now = title_global<int32_t>(0x0098540c);
        float fraction;
        std::memcpy(&fraction, &bits, sizeof fraction);
        const float elapsed = static_cast<float>(duration) * fraction;
        word_at(actor, 0x1a0) = static_cast<uint32_t>(-duration);
        word_at(actor, 0x19c) = static_cast<uint32_t>(now - static_cast<int32_t>(elapsed));
        return;
    }
    word_at(actor, 0x19c) = title_global<uint32_t>(0x0098540c);
    word_at(actor, 0x1a0) = static_cast<uint32_t>(-duration);
}

}  // namespace game
