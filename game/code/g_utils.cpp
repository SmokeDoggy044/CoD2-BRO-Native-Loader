#include "g_utils.h"

#include <cstring>

#include "actor.h"
#include "bg_misc.h"
#include "dobj.h"
#include "fx.h"
#include "xanim.h"
#include "cg_ents.h"
#include "g_client.h"
#include "g_main.h"
#include "g_scr_runtime.h"
#include "g_script.h"
#include "g_vehicle.h"
#include "lanes.h"
#include "nal_anim.h"
#include "qcommon.h"
#include "qcommon_core.h"
#include "qcommon_fs.h"
#include "script_string.h"
#include "snd.h"
#include "stream.h"
#include "sv.h"
#include "tr_main.h"
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

void XB_CDECL Use_Multi(uint8_t* ent, uint32_t, uint32_t activator) { multi_trigger(activator, ent); }

namespace {

// The client's (+0x23c) vehicle: flags 0x100000 set and 0x400000 clear in the client's +0xc0,
// the entity of the handle at +0x1a0.
bool client_on_vehicle(const uint8_t* ent) {
    const uint32_t client = *reinterpret_cast<const uint32_t*>(ent + 0x23c);
    if (!client) return false;
    const uint32_t flags = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(client + 0xc0));
    return (flags & 0x100000) && !(flags & 0x400000);
}

const uint8_t* vehicle_def_of(const uint8_t* vehicle) {
    const int16_t def = *reinterpret_cast<const int16_t*>(vehicle + 0x178);
    return title_global<const uint8_t*>(0x00950a28 + def * 4);  // g_vehicleDefs
}

}  // namespace

uint32_t XB_THISCALL G_ClientInOpenVehicle(const uint8_t* ent) {
    if (!client_on_vehicle(ent)) return 0;
    const uint32_t* const handle = reinterpret_cast<const uint32_t*>(ent + 0x1a0);
    if (!G_EntHandleToEnt(handle)) return 0;
    const uint32_t vehicle = *reinterpret_cast<const uint32_t*>(G_EntHandleToEnt(handle) + 0x248);
    if (!vehicle) return 0;
    const uint8_t* const def = vehicle_def_of(reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(vehicle)));
    return *reinterpret_cast<const uint32_t*>(def + 0x3c) != 0;
}

uint32_t XB_THISCALL G_ClientInVehicle(const uint8_t* ent) {
    if (!client_on_vehicle(ent)) return 0;
    const uint32_t* const handle = reinterpret_cast<const uint32_t*>(ent + 0x1a0);
    if (!*handle) return 0;
    const uint32_t vehicle = *reinterpret_cast<const uint32_t*>(G_EntHandleToEnt(handle) + 0x248);
    if (!vehicle) return 0;
    const uint8_t* const def = vehicle_def_of(reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(vehicle)));
    if (*reinterpret_cast<const int16_t*>(def + 0x20) == 5) return 0;
    if (*reinterpret_cast<const uint32_t*>(def + 0x3c)) return 0;
    return 1;
}

uint8_t* XB_THISCALL G_GetPlayerTurret(const uint8_t* ent) {
    const uint32_t client = *reinterpret_cast<const uint32_t*>(ent + 0x23c);
    if (!client) return nullptr;
    if (!(*reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(client + 0xc0)) & 0x100000)) return nullptr;
    const uint32_t* const handle = reinterpret_cast<const uint32_t*>(ent + 0x1a0);
    if (!*handle) return nullptr;
    uint8_t* const turret = G_EntHandleToEnt(handle);
    return (word_at(turret, 0x134) & 0x200000) ? turret : nullptr;
}

PLATFORM_ASM_ENTRY(G_VehicleCountRider_entry,
                   "pushl 4(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(G_VehicleCountRider) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL G_VehicleCountRider(uint8_t* ent, uint32_t entering) {
    // The rider (handle +0x1a0, inlined G_EntHandleToEnt) unless it is the player
    // (0x0095f534 +4): the vehicle's (+0x248) count at +0x2fc goes up or down.
    const uint32_t value = word_at(ent, 0x1a0);
    uint8_t* const vehicle = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(word_at(ent, 0x248)));
    const uint32_t index = value & 0xfff;
    uint32_t rider = 0;
    if (index < 0x540 && (value >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
        rider = title_global<uint32_t>(0x0097a968 + index * 8);
    const uint32_t level = title_global<uint32_t>(0x0095f534);
    if (rider == title_global<uint32_t>(level + 4)) return level;
    uint32_t count = word_at(vehicle, 0x2fc);
    if (entering)
        ++count;
    else
        --count;
    word_at(vehicle, 0x2fc) = count;
    return count;
}

PLATFORM_ASM_ENTRY(Add_AmmoClips_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Add_AmmoClips) "addl $12, %esp\n"
                   "ret")

void XB_CDECL Add_AmmoClips(uint32_t weapon, int32_t clips, uint8_t* ent) {
    if (!weapon || clips <= 0) return;
    const uint8_t* const def = title_global<const uint8_t**>(0x0095f4d8)[weapon];  // bg_weaponDefs
    if (!def) return;
    uint8_t* const ps = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(word_at(ent, 0x23c)));
    const int32_t clip_size = *reinterpret_cast<const int32_t*>(def + 0x5a8);
    int32_t amount = clip_size * clips;
    if (*reinterpret_cast<const int32_t*>(def + 0x9c) == 4) amount = clips;
    const uint32_t clip_index = *reinterpret_cast<const uint32_t*>(def + 0x598);
    const uint32_t ammo_index = *reinterpret_cast<const uint32_t*>(def + 0x5a0);
    const int32_t room = BG_AmmoRoomForWeapon(weapon, ps);
    int32_t& ammo = *reinterpret_cast<int32_t*>(ps + 0x280 + ammo_index * 4);
    if (*reinterpret_cast<const uint32_t*>(def + 0x6ec)) {
        const int32_t total = ammo + amount;
        ammo = total;
        if (total > room) ammo = room;
        if (*reinterpret_cast<const int32_t*>(def + 0x9c) == 4 && ammo > 4) ammo = 4;
        return;
    }
    const int32_t size = *reinterpret_cast<const int32_t*>(def + 0x5a8);
    int32_t added = amount + (ammo - size);
    if (added >= room) added = room;
    ammo = size;
    *reinterpret_cast<int32_t*>(ps + 0x110 + clip_index * 4) += added;
}

namespace {

uint32_t word_of(const uint8_t* base, uint32_t offset) { return *reinterpret_cast<const uint32_t*>(base + offset); }

uint32_t ent_address(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

}  // namespace

PLATFORM_ASM_ENTRY(Fill_Clip_entry,
                   "pushl %esi\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(Fill_Clip) "addl $8, %esp\n"
                   "ret")

void XB_CDECL Fill_Clip(int32_t weapon, uint8_t* ps) {
    // The def is read before the weapon is checked, as the original does.
    const uint8_t* const def = title_global<const uint8_t**>(0x0095f4d8)[weapon];  // bg_weaponDefs
    const uint32_t ammo_index = *reinterpret_cast<const uint32_t*>(def + 0x5a0);
    const uint32_t clip_index = *reinterpret_cast<const uint32_t*>(def + 0x598);
    if (weapon <= 0 || weapon > title_global<int32_t>(0x0095f4d4)) return;  // bg_lastWeaponIndex
    int32_t& ammo = *reinterpret_cast<int32_t*>(ps + 0x280 + ammo_index * 4);
    int32_t& reserve = *reinterpret_cast<int32_t*>(ps + 0x110 + clip_index * 4);
    int32_t moved = title_global<int32_t>(0x00935338 + ammo_index * 4) - ammo;
    const int32_t available = reserve;
    if (moved > available) moved = available;
    if (!moved) return;
    reserve = available - moved;
    ammo += moved;
}

PLATFORM_ASM_ENTRY(Pickup_ClipsForAllWeapons_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(Pickup_ClipsForAllWeapons) "addl $4, %esp\n"
                   "ret")

int32_t XB_CDECL Pickup_ClipsForAllWeapons(uint8_t* ent) {
    // Two clips for each of the four weapons in the client's slots +0x3f9..+0x3fc.
    const uint8_t* const client = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(word_at(ent, 0x23c)));
    for (uint32_t slot = 0x3f9; slot <= 0x3fc; ++slot)
        Add_AmmoClips(static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(client[slot]))), 2, ent);
    return -1;
}

PLATFORM_ASM_ENTRY(G_ClientFreeHeldEntity_entry,
                   "pushl %edx\n" PLATFORM_ASM_CALL(G_ClientFreeHeldEntity) "addl $4, %esp\n"
                   "ret")

void XB_CDECL G_ClientFreeHeldEntity(uint8_t* ent) {
    // The entity at the client's +0x5f0 is cleared and freed next frame (think 0xc).
    uint8_t* const held =
        reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(word_at(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(word_at(ent, 0x23c))), 0x5f0)));
    if (!held) return;
    word_at(held, 0x29c) = 0;
    word_at(held, 0x134) = 0;
    word_at(held, 0x1a0) = 0;
    word_at(held, 0x328) = title_global<uint32_t>(0x0098540c) + 1;
    word_at(held, 0x32c) = 0xc;
    word_at(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(word_at(ent, 0x23c))), 0x5f0) = 0;
}

PLATFORM_ASM_ENTRY(Player_PushAwayFromEntity_entry,
                   "pushl %edi\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(Player_PushAwayFromEntity) "addl $8, %esp\n"
                   "ret")

uint32_t XB_CDECL Player_PushAwayFromEntity(const uint8_t* other, uint8_t* player) {
    // Entities with flag 0x4000/0x8000 (+0x134) push the player's velocity (client +0x10/+0x14)
    // away within 30 units (horizontal), unless the client's +0xc0 has 0x6000, +0x3cc is set,
    // or the client's timers (+0x620 + 1000, +0x624 + 500) say it was pushed recently: then 1.
    if (!(other[0x135] & 0xc0)) return 0;
    uint8_t* const client = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(word_at(player, 0x23c)));
    if (client[0xc1] & 0x60) return 0;
    if (*reinterpret_cast<const uint32_t*>(player + 0x3cc)) return 0;
    const int32_t now = title_global<int32_t>(0x0098540c);
    if (now < *reinterpret_cast<const int32_t*>(client + 0x624) + 500 &&
        now >= *reinterpret_cast<const int32_t*>(client + 0x620) + 1000)
        return 1;
    float dir[2];
    dir[0] = *reinterpret_cast<const float*>(player + 0x140) - *reinterpret_cast<const float*>(other + 0x140);
    dir[1] = *reinterpret_cast<const float*>(player + 0x144) - *reinterpret_cast<const float*>(other + 0x144);
    const float dx2 = dir[0] * dir[0];
    const float dy2 = dir[1] * dir[1];
    const float dist2 = dy2 + dx2;
    if (dist2 >= title_global<float>(0x0083ee3c)) return 0;
    // (30 - sqrt(dist2)) * 0x008ddaa0 on the x87.
    long double push = platform_x87_sqrt_ext(static_cast<long double>(dist2));
    push = static_cast<long double>(title_global<float>(0x0083ee38)) - push;
    push = push * static_cast<long double>(title_global<float>(0x008ddaa0));
    const float speed = static_cast<float>(push);
    Vec2Normalize(dir);
    *reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(word_at(player, 0x23c))) + 0x10) =
        platform_x87_mul(speed, dir[0]);
    *reinterpret_cast<float*>(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(word_at(player, 0x23c))) + 0x14) =
        platform_x87_mul(speed, dir[1]);
    return 1;
}

void XB_CDECL BodySink(uint8_t* ent) {
    const int32_t now = title_global<int32_t>(0x0098540c);
    if (now - static_cast<int32_t>(word_at(ent, 0x2f0)) > 6500) {
        word_at(ent, 0xd0) = 0;
        SV_UnlinkEntity(ent + 0xd0);
        ent[0x294] = 0;
        return;
    }
    const float z = *reinterpret_cast<const float*>(ent + 0x28) - title_global<float>(0x0083eb20);
    word_at(ent, 0x328) = static_cast<uint32_t>(now + 1);
    *reinterpret_cast<float*>(ent + 0x28) = z;
}

PLATFORM_ASM_ENTRY(G_FindEntityByStringField_entry,
                   "leal 4(%esp), %eax\n"
                   "pushl %eax\n"
                   "pushl %edx\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(G_FindEntityByStringField) "addl $12, %esp\n"
                   "ret $4")

uint32_t XB_CDECL G_FindEntityByStringField(const uint8_t* list, const ScriptString* key, uint32_t* args) {
    // args[0] = the field offset; the original passes that slot as the search's result slot.
    const uint32_t count = *reinterpret_cast<const uint32_t*>(list + 0x2aac);
    const uint32_t first = ent_address(list + 0x2ab0);
    uint32_t block[4] = {ent_address(args), 0, first, ent_address(list + 0x2ab0 + count * 4)};
    ScriptString_Copy(reinterpret_cast<ScriptString*>(&block[1]), key);
    G_EntityArray_FindByStringField(args[0], block);
    const uint32_t found = args[0];
    if (found == ent_address(list + 0x2ab0 + *reinterpret_cast<const uint32_t*>(list + 0x2aac) * 4)) return 0;
    return *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(found));
}

PLATFORM_ASM_ENTRY(G_EntityArray_FindByStringField_entry,
                   "leal 4(%esp), %eax\n"
                   "pushl %eax\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(G_EntityArray_FindByStringField) "addl $8, %esp\n"
                   "ret $16")

uint32_t XB_CDECL G_EntityArray_FindByStringField(uint32_t field, uint32_t* args) {
    // args: {result slot, key (a by-value ScriptString the callee releases), begin, end}. The
    // result slot doubles as the temporary string, as in the original.
    uint32_t* const result = reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(args[0]));
    ScriptString* const temp = reinterpret_cast<ScriptString*>(&args[0]);
    ScriptString* const key = reinterpret_cast<ScriptString*>(&args[1]);
    using StringOp = void XB_THISCALL(ScriptString*);
    ScriptString_FromRep(temp, nullptr);
    uint32_t at = args[2];
    const uint32_t end = args[3];
    for (; at != end; at += 4) {
        const uint32_t ent = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(at));
        if (!ent) continue;
        title_function<void XB_THISCALL(ScriptString*, uint32_t)>(0x0011d600)(temp, ent + field);  // ScriptString_Assign
        const uint32_t rep = args[0];
        if (!rep || !(rep + 6) || !*reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(rep + 6))) continue;
        if (ScriptString_EqualsString(temp, key)) break;
    }
    *result = at;
    title_function<StringOp>(0x0011d640)(temp);  // ScriptString_Release
    title_function<StringOp>(0x0011d640)(key);   // ScriptString_Release
    return ent_address(result);
}

namespace {

const uint8_t* vehicle_def(const uint8_t* vehicle) {
    const int16_t def = *reinterpret_cast<const int16_t*>(vehicle + 0x178);
    return title_global<const uint8_t*>(0x00950a28 + def * 4);  // g_vehicleDefs
}

// A tank's entity counts while it has a vehicle (+0x248), health (+0x350 > 0) and no rider
// handle (+0x1a0).
bool tank_entity_alive(const uint8_t* ent) {
    return *reinterpret_cast<const uint32_t*>(ent + 0x248) && *reinterpret_cast<const int32_t*>(ent + 0x350) > 0 &&
           !*reinterpret_cast<const uint32_t*>(ent + 0x1a0);
}

}  // namespace

PLATFORM_ASM_ENTRY(Vehicle_GetTankEntityNum_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(Vehicle_GetTankEntityNum) "addl $8, %esp\n"
                   "ret")

uint32_t* XB_CDECL Vehicle_GetTankEntityNum(uint32_t index, uint32_t* out) {
    // Vehicle slot index (0x0098537c, 0x340 bytes each): a type-2 vehicle with an entity
    // (+0x170) and +0x190 set whose entity is alive and unridden hands out the entity number (+0x224).
    const uint8_t* const vehicle = title_global<const uint8_t*>(0x0098537c) + index * 0x340;
    const uint32_t* const handle = reinterpret_cast<const uint32_t*>(vehicle + 0x170);
    if (*handle && *reinterpret_cast<const int16_t*>(vehicle_def(vehicle) + 0x20) == 2 &&
        *reinterpret_cast<const uint32_t*>(vehicle + 0x190)) {
        const uint8_t* const ent = G_EntHandleToEnt(handle);
        if (tank_entity_alive(ent)) {
            *out = *reinterpret_cast<const uint32_t*>(ent + 0x224);
            return out;
        }
    }
    *out = 0;
    return out;
}

PLATFORM_ASM_ENTRY(Veh_GetEntTankInfo_entry,
                   "pushl 4(%esp)\n"
                   "pushl %edi\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(Veh_GetEntTankInfo) "addl $12, %esp\n"
                   "ret")

uint32_t XB_CDECL Veh_GetEntTankInfo(int32_t* slot, uint8_t* flag, uint32_t handle) {
    if (!title_global<uint32_t>(0x00986008)) return 0;  // g_compassMarkersEnabled (AL)
    // Inlined G_EntHandleToEnt; an invalid handle reads entity 0's fields (null + offset).
    const uint32_t index = handle & 0xfff;
    uint32_t ent = 0;
    if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
        ent = title_global<uint32_t>(0x0097a968 + index * 8);
    const uint8_t* const e = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(ent));
    if (!tank_entity_alive(e)) return 0;
    const uint8_t* const vehicle = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(word_of(e, 0x248)));
    if (!word_of(vehicle, 0x170)) return 0;
    const int16_t type = *reinterpret_cast<const int16_t*>(vehicle_def(vehicle) + 0x20);
    if (type != 2 && type != 1) return 0;
    if (!word_of(vehicle, 0x190)) return 0;
    if (slot) *slot = static_cast<int32_t>(vehicle - title_global<const uint8_t*>(0x0098537c)) / 0x340;
    if (flag) *flag = word_of(vehicle, 0x194) != 0;
    return 1;
}

uint32_t XB_REGS3 XB_CDECL G_PlayerCanUseTurret(uint32_t, const uint8_t* vehicle, const uint8_t* player) {
    // The player (ECX) has a client not on a turret/vehicle (+0xc0 flag 0x100000) and no rider handle;
    // the vehicle entity (EDX) has none either, +0x2fc <= 1, health, and flag 0x200000 (+0x134).
    const uint32_t client = word_of(player, 0x23c);
    if (!client) return 0;
    if (*reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(client + 0xc0)) & 0x100000) return 0;
    if (word_of(player, 0x1a0)) return 0;
    if (word_of(vehicle, 0x1a0)) return 0;
    if (*reinterpret_cast<const float*>(vehicle + 0x2fc) > title_global<float>(0x0083eb20)) return 0;
    if (*reinterpret_cast<const int32_t*>(vehicle + 0x350) <= 0) return 0;
    return (word_of(vehicle, 0x134) >> 0x15) & 1;
}

void XB_CDECL Think_BoostNearbyPlayer(uint8_t* ent) {
    // Until level.time passes +0x38c (then think 0xc frees it) it thinks every 100 ms and lifts
    // the player within 512 units: +24 upward velocity, and once (+0x30 = 50) flag 0x200.
    const float now = static_cast<float>(title_global<int32_t>(0x0098540c));
    if (now > *reinterpret_cast<const float*>(ent + 0x38c)) word_at(ent, 0x32c) = 0xc;
    word_at(ent, 0x328) = title_global<uint32_t>(0x0098540c) + 100;
    const uint8_t* const player = G_FindEntityByIntField(reinterpret_cast<const uint8_t*>(uintptr_t{0x0097a8c0}), 0x260,
                                                         title_global<int32_t>(0x0097a75c));  // scr_const_player
    if (!player) return;
    // The distance on the x87, compared unrounded.
    const float* const a = reinterpret_cast<const float*>(player + 0x140);
    const float* const b = reinterpret_cast<const float*>(ent + 0x140);
    const long double dx = static_cast<long double>(a[0]) - static_cast<long double>(b[0]);
    const long double dy = static_cast<long double>(a[1]) - static_cast<long double>(b[1]);
    const long double dz = static_cast<long double>(a[2]) - static_cast<long double>(b[2]);
    long double sum = dz * dz;
    sum = sum + dy * dy;
    sum = sum + dx * dx;
    const long double distance = platform_x87_sqrt_ext(sum);
    if (distance > static_cast<long double>(title_global<float>(0x0083ebf4))) return;
    uint8_t* client = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(word_of(player, 0x23c)));
    word_at(client, 0x10) = word_at(client, 0x10);
    word_at(client, 0x14) = word_at(client, 0x14);
    *reinterpret_cast<float*>(client + 0x18) = *reinterpret_cast<const float*>(client + 0x18) + title_global<float>(0x0083ef04);
    client = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(word_of(player, 0x23c)));
    if (word_at(client, 0x30)) return;
    word_at(client, 0x30) = 0x32;
    word_at(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(word_of(player, 0x23c))), 0x2c) |= 0x200;
}

PLATFORM_ASM_ENTRY(G_FloatOnWater_entry,
                   "pushl %esi\n" PLATFORM_ASM_CALL(G_FloatOnWater) "addl $4, %esp\n"
                   "ret")

void XB_CDECL G_FloatOnWater(uint8_t* ent) {
    // Puts the entity on the water surface (Water_GetHeight at its x/y plus the model's
    // height offset, +0x250 -> +0xf8) with a stationary trajectory.
    const float x = *reinterpret_cast<const float*>(ent + 0x140);
    const float y = *reinterpret_cast<const float*>(ent + 0x144);
    float offset = 0.0f;
    const uint32_t model = word_at(ent, 0x250);
    if (model) offset = static_cast<float>(*reinterpret_cast<const int16_t*>(static_cast<uintptr_t>(model + 0xf8)));
    const long double height = static_cast<long double>(Water_GetHeight(0, x, y)) + static_cast<long double>(offset);
    const float z = static_cast<float>(height);
    float* const base = reinterpret_cast<float*>(ent + 0x20);
    base[2] = z;
    base[0] = x;
    base[1] = y;
    word_at(ent, 0x14) = 0;
    word_at(ent, 0x18) = 0;
    word_at(ent, 0x1c) = 0;
    float* const delta = reinterpret_cast<float*>(ent + 0x2c);
    delta[2] = 0.0f;
    delta[1] = 0.0f;
    delta[0] = 0.0f;
    float* const current = reinterpret_cast<float*>(ent + 0x140);
    current[0] = x;
    current[1] = y;
    current[2] = z;
}

namespace {

template <typename T>
T& ent_field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

uint32_t address_word(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// The entity of a handle (index & 0xfff below 0x540, generation >> 12), else null.
uint8_t* handle_entity(uint32_t handle) {
    const uint32_t index = handle & 0xfff;
    if (index < 0x540 && (handle >> 12) == title_global<uint32_t>(0x0097a96c + index * 8))
        return title_global<uint8_t*>(0x0097a968 + index * 8);
    return nullptr;
}

// The touch candidates: from the cache ({count, mins +0x10, maxs +0x20, handles +0x30}), else
// SV_AreaEntities in 40/40/52 around the origin with `mask`, then the entity's bounds at the
// origin. Returns the count.
int32_t touch_candidates(const uint8_t* ent, const float* origin, const uint32_t* cache, uint32_t mask, uint32_t* list,
                         float* mins, float* maxs) {
    if (cache) {
        const float* const box = reinterpret_cast<const float*>(cache);
        mins[0] = box[4];
        mins[1] = box[5];
        mins[2] = box[6];
        maxs[0] = box[8];
        maxs[1] = box[9];
        maxs[2] = box[10];
        const int32_t count = static_cast<int32_t>(cache[0]);
        for (int32_t i = 0; i < count; ++i) list[i] = cache[12 + i];
        return count;
    }
    const float side = title_global<float>(0x0083ec84);
    const float up = title_global<float>(0x0083f158);
    mins[0] = origin[0] - side;
    mins[1] = origin[1] - side;
    mins[2] = origin[2] - up;
    maxs[0] = origin[0] + side;
    maxs[1] = origin[1] + side;
    maxs[2] = origin[2] + up;
    const int32_t count = SV_AreaEntities(maxs, 0, mins, reinterpret_cast<int32_t*>(list), 0x540, mask);
    uint8_t* const e = const_cast<uint8_t*>(ent);
    for (uint32_t i = 0; i < 3; ++i) mins[i] = ent_field<float>(e, 0xe0 + i * 4) + origin[i];
    for (uint32_t i = 0; i < 3; ++i) maxs[i] = ent_field<float>(e, 0xf0 + i * 4) + origin[i];
    return count;
}

using TouchFunc = void XB_CDECL(uint8_t*, uint8_t*, uint32_t);

void run_touch(uint8_t* self, uint8_t* other) {
    title_function<TouchFunc>(title_global<uint32_t>(0x008da814 + ent_field<uint32_t>(self, 0x338) * 4))(self, other, 1);
}

}  // namespace

void XB_REGS3 XB_CDECL G_TouchEntitiesInBox(uint32_t, uint32_t, const float* origin, uint8_t* ent, const uint32_t* cache) {
    // Candidates of the touch type flag (G_GetTouchTypeFlag; none: nothing) whose contents
    // carry it, with a touch callback on either side; players (type 2, entity with a client)
    // by 0x002d6710, others by SV_ClipToEntity of the box. "touch" notifies both ways (the
    // other's handle), the candidate's touch callback, then the entity's when +0x240 is set.
    uint32_t list[0x540];
    for (uint32_t i = 0; i < 0x540; ++i) list[i] = 0;
    const uint32_t flag = G_GetTouchTypeFlag(ent);
    if (!flag) return;
    alignas(16) float mins[3];
    alignas(16) float maxs[3];
    const int32_t count = touch_candidates(ent, origin, cache, flag, list, mins, maxs);
    for (int32_t i = 0; i < count; ++i) {
        uint8_t* const other = handle_entity(list[i]);
        if (!other || !(ent_field<uint32_t>(other, 0x134) & flag)) continue;
        if (!ent_field<uint32_t>(other, 0x338) && !ent_field<uint32_t>(ent, 0x338)) continue;
        if (other[0] == 2) {
            const uint32_t client = ent_field<uint32_t>(ent, 0x23c);
            if (!client) continue;
            const TitleRegs regs{title_global<uint32_t>(0x0098540c), address_word(other), address_word(other), 0, client, 0};
            if (!g_ent_call(0x002d6710, &regs, 0, nullptr)) continue;
        } else if (!SV_ClipToEntity(other, mins, maxs, 1)) {
            continue;
        }
        uint32_t number = ent_field<uint32_t>(ent, 0x224);
        {
            const TitleRegs regs{number, address_word(other), address_word(&number), title_global<uint32_t>(0x0097a814),
                                 address_word(ent), address_word(other)};
            const uint32_t args[2] = {title_global<uint32_t>(0x0097a814), address_word(&number)};
            g_ent_call(0x000265a0, &regs, 2, args);  // G_NotifyWithEntity "touch"
        }
        number = ent_field<uint32_t>(other, 0x224);
        {
            const TitleRegs regs{number, address_word(other), address_word(&number), title_global<uint32_t>(0x0097a814),
                                 address_word(ent), address_word(ent)};
            const uint32_t args[2] = {title_global<uint32_t>(0x0097a814), address_word(&number)};
            g_ent_call(0x000265a0, &regs, 2, args);  // G_NotifyWithEntity "touch"
        }
        if (ent_field<uint32_t>(other, 0x338)) run_touch(other, ent);
        if (ent_field<uint32_t>(ent, 0x240) && ent_field<uint32_t>(ent, 0x338)) run_touch(ent, other);
    }
}

// Every title callee runs where the original's call puts it, with its registers (the C frame sits
// below a gap): frame = aligned base - 0x1530 (count +8, index +0xc, maxs +0x10, mins +0x20,
// handles +0x30, 0x540 words up to the aligned base), saved EDI/ESI at +0/+4.
PLATFORM_ASM_ENTRY(G_TouchType14EntitiesInBox_entry,
                   "movl %esp, %eax\n"
                   GAME_GAP_ESP("0x20000")
                   "pushl %ebp\n"
                   "movl %eax, %ebp\n"
                   "pushl (%esp)\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %edx\n"
                   "pushl %ecx\n"
                   "pushl %ebx\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(G_TouchType14EntitiesInBox) "addl $28, %esp\n"
                   "popl %ebp\n"
                   "popl %esp\n"
                   "ret")

uint32_t XB_CDECL G_TouchType14EntitiesInBox(uint32_t entry, uint8_t* ent, const float* origin, const uint32_t* cache,
                                             uint32_t esi, uint32_t edi, uint32_t ebp) {
    // EBX entity, ECX origin, EDX cache: as G_TouchEntitiesInBox with mask 0x800000: linked (+0xdc)
    // type-14 candidates by SV_ClipToEntity, "touch" both ways by G_NotifyWithOwnHandle, then the
    // callbacks. Returns the count (or +0x3cc when set).
    auto word = [](uint32_t address) -> uint32_t& { return *reinterpret_cast<uint32_t*>(static_cast<uintptr_t>(address)); };
    word(entry - 4) = ebp;
    const uint32_t base = (entry - 4) & ~15u;
    const uint32_t frame = base - 0x1530;
    // The callees' pushed EBP is the original's frame pointer (entry - 4), not our helper's: write it back
    // where each call's `push ebp` put it (SV_ClipToEntity/SV_AreaEntities/touch callbacks first thing,
    // G_NotifyWithEntity under G_NotifyWithOwnHandle after its locals).
    const uint32_t frame_ebp = entry - 4;
    auto pushed_ebp = [&](uint32_t function, uint32_t slot) {
        if (*reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(function)) == 0x55) word(slot) = frame_ebp;
    };
    const uint32_t maxs = frame + 0x10, mins = frame + 0x20, list = frame + 0x30;
    word(frame) = edi;
    word(frame + 4) = esi;
    for (uint32_t i = 0; i < 0x540; ++i) word(list + i * 4) = 0;
    if (ent_field<uint32_t>(ent, 0x3cc)) return ent_field<uint32_t>(ent, 0x3cc);
    const uint32_t ent_word = address_word(ent);
    esi = address_word(origin);
    edi = base;
    int32_t count;
    if (cache) {
        for (uint32_t i = 0; i < 3; ++i) word(mins + i * 4) = cache[4 + i];
        for (uint32_t i = 0; i < 3; ++i) word(maxs + i * 4) = cache[8 + i];
        count = static_cast<int32_t>(cache[0]);
        word(frame + 8) = static_cast<uint32_t>(count);
        if (count > 0) {
            for (int32_t i = 0; i < count; ++i) word(list + i * 4) = cache[12 + i];
            esi = address_word(cache + 12 + count);
            edi = list + static_cast<uint32_t>(count) * 4;
        }
    } else {
        const float side = title_global<float>(0x0083ec84);
        const float up = title_global<float>(0x0083f158);
        float* const lo = reinterpret_cast<float*>(static_cast<uintptr_t>(mins));
        float* const hi = reinterpret_cast<float*>(static_cast<uintptr_t>(maxs));
        lo[0] = origin[0] - side;
        lo[1] = origin[1] - side;
        lo[2] = origin[2] - up;
        hi[0] = origin[0] + side;
        hi[1] = origin[1] + side;
        hi[2] = origin[2] + up;
        const TitleRegs regs{maxs, ent_word, mins, 0, esi, edi};
        const uint32_t args[3] = {list, 0x540, 0x800000};
        count = static_cast<int32_t>(title_call_at_regs(frame, 0x002cc400, &regs, 3, args));  // SV_AreaEntities
        word(frame - 0x10) = 0x0025d639;
        word(frame - 0x14) = frame_ebp;
        for (uint32_t i = 0; i < 3; ++i) lo[i] = ent_field<float>(ent, 0xe0 + i * 4) + origin[i];
        for (uint32_t i = 0; i < 3; ++i) hi[i] = ent_field<float>(ent, 0xf0 + i * 4) + origin[i];
        word(frame + 8) = static_cast<uint32_t>(count);
    }
    word(frame + 0xc) = 0;
    const uint32_t touch = 0x0097a814;  // scr_const_touch
    for (int32_t i = 0; i < count; i = static_cast<int32_t>(++word(frame + 0xc))) {
        const uint32_t handle = word(list + static_cast<uint32_t>(i) * 4);
        const uint32_t index = handle & 0xfff;
        if (index >= 0x540 || (handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) continue;
        esi = title_global<uint32_t>(0x0097a968 + index * 8);
        uint8_t* const other = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(esi));
        if (!other || !(ent_field<uint32_t>(other, 0x134) & 0x800000) || other[0] != 0xe) continue;
        if (!ent_field<uint32_t>(other, 0xdc)) continue;
        uint32_t eax = ent_field<uint32_t>(other, 0x338);
        if (!eax) eax = ent_field<uint32_t>(ent, 0x338);
        if (!eax) continue;
        {
            const TitleRegs regs{eax, ent_word, maxs, mins, esi, edi};
            const uint32_t args[3] = {mins, maxs, 1};
            const uint32_t hit = title_call_at_regs(frame, 0x002e8270, &regs, 3, args);  // SV_ClipToEntity
            word(frame - 0x10) = 0x0025d743;
            word(frame - 0x14) = frame_ebp;
            if (!hit) continue;
        }
        {
            const TitleRegs regs{ent_word, ent_word, 0, 0, esi, esi};
            const uint32_t args[1] = {title_global<uint32_t>(touch)};
            title_call_at_regs(frame, 0x0024f570, &regs, 1, args);  // G_NotifyWithOwnHandle "touch"
            word(frame - 8) = 0x0025d759;
            word(frame - 0x30) = frame_ebp;
        }
        {
            const TitleRegs regs{esi, ent_word, title_global<uint32_t>(touch), 0, esi, ent_word};
            const uint32_t args[1] = {title_global<uint32_t>(touch)};
            title_call_at_regs(frame - 4, 0x0024f570, &regs, 1, args);  // G_NotifyWithOwnHandle "touch"
            word(frame - 0xc) = 0x0025d769;
            word(frame - 0x34) = frame_ebp;
        }
        edi = ent_word;
        const uint32_t own = ent_field<uint32_t>(other, 0x338);
        if (own) {
            const TitleRegs regs{own, ent_word, 0, 0, esi, edi};
            const uint32_t args[3] = {esi, ent_word, 1};
            title_call_at_regs(frame, title_global<uint32_t>(0x008da814 + own * 4), &regs, 3, args);
            word(frame - 0x10) = 0x0025d781;
            pushed_ebp(title_global<uint32_t>(0x008da814 + own * 4), frame - 0x14);
        }
        if (!ent_field<uint32_t>(ent, 0x240)) continue;
        const uint32_t mine = ent_field<uint32_t>(ent, 0x338);
        if (!mine) continue;
        const TitleRegs regs{mine, ent_word, 0, 0, esi, edi};
        const uint32_t args[3] = {ent_word, esi, 1};
        title_call_at_regs(frame, title_global<uint32_t>(0x008da814 + mine * 4), &regs, 3, args);
        word(frame - 0x10) = 0x0025d7a3;
        pushed_ebp(title_global<uint32_t>(0x008da814 + mine * 4), frame - 0x14);
    }
    return word(frame + 8);
}

PLATFORM_ASM_ENTRY(G_SetEntityAnimTree_entry,
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(G_SetEntityAnimTree) "addl $8, %esp\n"
                   "ret")

void XB_CDECL G_SetEntityAnimTree(uint8_t* anims, uint8_t* ent) {
    // The current tree (+0x254) is kept when it already runs these anims (+8); else a new one
    // (AnimTree_Create) or none, G_DObjUpdate, and the old tree queued for freeing (0x00987070
    // count, 0x00987074 list; AnimTree_FlushClearLists).
    uint8_t* const old = ent_field<uint8_t*>(ent, 0x254);
    if (anims) {
        if (old && ent_field<uint8_t*>(old, 8) == anims) return;
        ent_field<uint8_t*>(ent, 0x254) = AnimTree_Create(ent, anims);
    } else {
        if (!old) return;
        ent_field<uint32_t>(ent, 0x254) = 0;
    }
    title_function<void XB_CDECL(uint8_t*)>(0x002e8be0)(ent);  // G_DObjUpdate
    if (!old) return;
    const uint32_t count = title_global<uint32_t>(0x00987070);
    title_global<uint32_t>(0x00987074 + count * 4) = address_word(old);
    ++title_global<uint32_t>(0x00987070);
    AnimTree_FlushClearLists();
}

uint32_t XB_REGS1 XB_CDECL G_EntHasLiveDObj(const uint8_t* ent) {
    // 1 when the entity has a DObj (+0x22c) without its +0x9c bit 0.
    const uint8_t* const obj =
        reinterpret_cast<const uint8_t*>(uintptr_t{*reinterpret_cast<const uint32_t*>(ent + 0x22c)});
    if (!obj || (obj[0x9c] & 1)) return 0;
    return 1;
}

void XB_CDECL G_StartCodNote(uint8_t* note, uint32_t a, uint32_t b) {
    // The note's two words (+0x10, +0x14) set and it is marked started (+0x19); then its object
    // (+0xc) runs vtable slot 2.
    *reinterpret_cast<uint32_t*>(note + 0x10) = a;
    note[0x19] = 1;
    *reinterpret_cast<uint32_t*>(note + 0x14) = b;
    const uint32_t object = *reinterpret_cast<const uint32_t*>(note + 0xc);
    const uint32_t vtable = *reinterpret_cast<const uint32_t*>(uintptr_t{object});
    title_function<void XB_THISCALL(uint32_t)>(*reinterpret_cast<const uint32_t*>(uintptr_t{vtable + 8}))(object);
}

PLATFORM_ASM_ENTRY(Persistent_SetValue_entry,
                   "pushl 4(%esp)\n"
                   "pushl %ebx\n"
                   "pushl %edx\n"
                   "pushl %ecx\n" PLATFORM_ASM_CALL(Persistent_SetValue) "addl $16, %esp\n"
                   "ret $4")

void XB_CDECL Persistent_SetValue(uint8_t* owner, uint32_t key, uint32_t count, const uint32_t* values) {
    uint32_t item[3];
    item[1] = values[0];
    uint8_t* const array = owner + 0x560;
    uint32_t* const begin = reinterpret_cast<uint32_t*>(uintptr_t{word_at(array, 0)});
    uint32_t* const end = begin + word_at(owner, 0x568) * 3;
    item[0] = key;
    item[2] = count;
    for (uint32_t* entry = begin; entry != end; entry += 3) {
        if (entry[0] != key) continue;
        DynArray_Erase12(entry, reinterpret_cast<uint32_t*>(array), entry + entry[2] * 3);
        break;
    }
    DynArray_PushBack12(array, item);
    if (count > 1) {
        item[0] = 0;
        item[2] = 0;
        for (uint32_t i = 1; i < count; ++i) {
            item[1] = values[i];
            DynArray_PushBack12(array, item);
        }
    }
}

uint32_t XB_THISCALL G_EntityRunNotifyCallbacks(uint8_t* list, uint8_t* ent, uint32_t key, const uint32_t* args) {
    uint8_t called = 0;
    for (uint32_t i = 0; i < 6; ++i) {
        if (word_at(list, 8 + i * 8) != key) continue;
        G_EntityCallNotifyCallback(args, word_at(list, 0xc + i * 8), ent);
        called = 1;
    }
    uint8_t* const next = reinterpret_cast<uint8_t*>(uintptr_t{word_at(list, 0x38)});
    if (next) called |= static_cast<uint8_t>(G_EntityRunNotifyCallbacks(next, ent, key, nullptr));
    return called;
}

uint32_t XB_REGS2 XB_STDCALL G_EntityRemoveNotifyCallback(uint32_t, uint8_t* list, uint32_t key, uint32_t callback) {
    for (;;) {
        for (uint32_t i = 0; i < 6; ++i) {
            if (word_at(list, 8 + i * 8) != key || word_at(list, 0xc + i * 8) != callback) continue;
            word_at(list, 0xc + i * 8) = 0;
            word_at(list, 8 + i * 8) = 0;
            return 1;
        }
        list = reinterpret_cast<uint8_t*>(uintptr_t{word_at(list, 0x38)});
        if (!list) return 0;
    }
}

uint32_t XB_REGS1 XB_STDCALL G_EntityAddNotifyCallback(uint8_t* list, uint32_t key, uint32_t callback) {
    for (;;) {
        for (uint32_t i = 0; i < 6; ++i) {
            if (word_at(list, 0xc + i * 8)) continue;
            word_at(list, 8 + i * 8) = key;
            word_at(list, 0xc + i * 8) = callback;
            return 1;
        }
        if (!word_at(list, 0x38)) {
            uint8_t* const added = SmallHeap_Alloc(title_global<uint8_t*>(0x0095f568), 0x3c, 0);
            if (added)
                for (uint32_t offset = 0; offset <= 0x38; offset += 4) word_at(added, offset) = 0;
            word_at(list, 0x38) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(added));
            if (!added) return 0;
        }
        list = reinterpret_cast<uint8_t*>(uintptr_t{word_at(list, 0x38)});
    }
}

PLATFORM_ASM_ENTRY(G_EntityCallNotifyCallback_entry,
                   "pushl %ecx\n"
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(G_EntityCallNotifyCallback) "addl $12, %esp\n"
                   "ret")

void XB_CDECL G_EntityCallNotifyCallback(const uint32_t* args, uint32_t callback, uint8_t* ent) {
    using Call = void XB_CDECL(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    const uint32_t id = word_at(ent, 0x224);
    const uint32_t fn = word_at(title_global<uint8_t*>(0x0095f49c), 0x880);
    if (args) {
        title_function<Call>(fn)(callback, id, args[0], args[0], args[2], args[3]);
        return;
    }
    const uint32_t none = title_global<uint32_t>(0x00922eb8);
    title_function<Call>(fn)(callback, id, 0, 0, none, none);
}

void XB_REGS1 XB_CDECL G_EntUnlink(uint8_t* ent) {
    uint8_t* const link = reinterpret_cast<uint8_t*>(uintptr_t{word_at(ent, 0x3cc)});
    if (!link) return;
    // Position and angle trajectories stop at the current values (+0x140 origin, +0x150 angles).
    for (uint32_t i = 0; i < 3; ++i) word_at(ent, 0x20 + i * 4) = word_at(ent, 0x140 + i * 4);
    for (uint32_t i = 0; i < 3; ++i) word_at(ent, 0x14 + i * 4) = 0;
    for (uint32_t i = 3; i-- > 0;) word_at(ent, 0x2c + i * 4) = 0;  // 0.0f
    for (uint32_t i = 0; i < 3; ++i) word_at(ent, 0x140 + i * 4) = word_at(ent, 0x140 + i * 4);
    for (uint32_t i = 0; i < 3; ++i) word_at(ent, 0x44 + i * 4) = word_at(ent, 0x150 + i * 4);
    for (uint32_t i = 0; i < 3; ++i) word_at(ent, 0x38 + i * 4) = 0;
    for (uint32_t i = 3; i-- > 0;) word_at(ent, 0x50 + i * 4) = 0;
    for (uint32_t i = 0; i < 3; ++i) word_at(ent, 0x150 + i * 4) = word_at(ent, 0x150 + i * 4);
    // Unchain from the parent's child list (+0x3d0, linked through each child's link +4).
    uint8_t* const parent = reinterpret_cast<uint8_t*>(uintptr_t{word_at(link, 0)});
    const uint32_t self = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ent));
    uint32_t child = word_at(parent, 0x3d0);
    if (child == self) {
        word_at(parent, 0x3d0) = word_at(link, 4);
    } else {
        uint32_t previous;
        do {
            previous = child;
            child = word_at(reinterpret_cast<uint8_t*>(uintptr_t{word_at(reinterpret_cast<uint8_t*>(uintptr_t{child}), 0x3cc)}), 4);
        } while (child != self);
        if (!previous) {
            word_at(parent, 0x3d0) = word_at(link, 4);
        } else {
            uint8_t* const previous_link =
                reinterpret_cast<uint8_t*>(uintptr_t{word_at(reinterpret_cast<uint8_t*>(uintptr_t{previous}), 0x3cc)});
            word_at(previous_link, 4) = word_at(link, 4);
        }
    }
    if (word_at(ent, 0x23c)) {
        uint8_t* const client = reinterpret_cast<uint8_t*>(uintptr_t{word_at(parent, 0x248)});
        if (client) client[0x238] = 0;
    }
    word_at(ent, 0x3cc) = 0;
    SmallHeap_Free(title_global<uint8_t*>(0x0095f598), link);
}

PLATFORM_ASM_ENTRY(G_EntGetTagIndex_entry,
                   "pushl %esi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(G_EntGetTagIndex) "addl $8, %esp\n"
                   "ret")

int32_t XB_CDECL G_EntGetTagIndex(const uint8_t* ent, uint32_t name) {
    const DObj* const obj = *reinterpret_cast<const DObj* const*>(ent + 0x22c);
    if (!obj) return -1;
    return DObj_GetTagIndex(obj, name);
}

void XB_STDCALL ShakeRumble_StartByName(uint32_t id_slot, float second) {
    // Dead code (no callers): a descriptor (0x40 bytes, two channels of 0x20) with both channels'
    // +8 / +0x10 set from the two arguments, started at strength 1; the id lands in the first argument's slot.
    alignas(4) uint8_t desc[0x48];
    ShakeRumbleDesc_Init(desc);
    uint8_t* const manager = title_global<uint8_t*>(0x0095f5c4);  // shakeRumbleMgr
    const uint32_t first = id_slot;
    *reinterpret_cast<uint32_t*>(desc + 8) = first;
    *reinterpret_cast<float*>(desc + 0x10) = second;
    *reinterpret_cast<uint32_t*>(desc + 0x28) = first;
    *reinterpret_cast<float*>(desc + 0x30) = second;
    if (manager) ShakeRumble_Start(desc, manager, &id_slot, 1.0f);
    for (uint32_t i = 2; i-- > 0;)
        title_function<void XB_THISCALL(uint8_t*)>(0x0011d640)(desc + 0x18 + i * 0x20);  // ScriptString_Release
}

uint8_t* XB_REGS1 XB_CDECL EntityState_Clear(uint8_t* state) {
    for (uint32_t i = 0; i < 6; ++i) state[i] = 0;
    for (uint32_t offset = 0x8; offset < 0x5c; offset += 4) word_at(state, offset) = 0;
    for (uint32_t offset = 0x60; offset < 0xb4; offset += 4) word_at(state, offset) = 0;
    word_at(state, 0xbc) = 0;  // 0.0f
    for (uint32_t offset = 0xc0; offset < 0xcc; offset += 4) word_at(state, offset) = 0;
    *reinterpret_cast<uint16_t*>(state + 6) = 0;
    for (uint32_t i = 0; i < 4; ++i) {
        state[0xb4 + i] = 0;
        state[0xb8 + i] = 0;
    }
    return state;
}

void XB_CDECL G_FreeEntity(uint8_t* ent) {
    if (ent == title_global<uint8_t*>(0x0095f52c)) {
        title_function<void XB_CDECL(uint8_t*)>(0x00042460)(ent);  // Sys_DebugBreak (tail jump)
        return;
    }
    if (title_global<uint8_t>(0x008d50d4)) {
        uint8_t* const info = reinterpret_cast<uint8_t*>(uintptr_t{word_at(title_global<uint8_t*>(0x0095f4f8), 0x194)});
        if (info) {
            uint32_t index = word_at(ent, 0x290);
            if (static_cast<int32_t>(index) > -1) {
                if (index >= word_at(info, 0)) index = 0;
                reinterpret_cast<uint8_t*>(uintptr_t{word_at(info, 4)})[index] = 1;
            }
        }
    }
    int32_t context = static_cast<int32_t>(word_at(ent, 0x220));
    title_global<uint8_t>(0x008d50d4) = 1;
    if (context == -1) context = static_cast<int32_t>(word_at(title_global<uint8_t*>(0x0095f638), 0x34));
    title_function<void XB_THISCALL(uint8_t*)>(0x002eda30)(ent);  // G_FreeEntityContents
    word_at(ent, 0x224) = 0;
    if (context != -1) {
        uint8_t* const heap = context >= 0 && context < 0x63
                                  ? reinterpret_cast<uint8_t*>(uintptr_t{word_at(title_global<uint8_t*>(0x0095f638), 0x38 + context * 4)})
                                  : nullptr;
        if (MemContext_Free_r(heap, ent)) return;
    }
    Z_Free(ent);
}

void XB_CDECL CodNote_Free(uint8_t* note) {
    const uint32_t context = word_at(note, 0x24);
    uint8_t saved = 0;
    if (context != 0xffffffff) {
        saved = title_global<uint8_t>(0x0095f5d0);
        uint8_t* const memory = title_global<uint8_t*>(0x0095f638);
        title_global<uint8_t>(0x0095f5d1) = 0;
        title_global<uint8_t>(0x0095f5d0) = 1;
        MemContext_Push(memory, context);
    }
    const uint32_t object = word_at(note, 0xc);
    if (object) {
        const uint32_t vtable = word_at(reinterpret_cast<uint8_t*>(uintptr_t{object}), 0);
        title_function<void XB_THISCALL(uint32_t, uint32_t)>(word_at(reinterpret_cast<uint8_t*>(uintptr_t{vtable}), 0))(object, 1);
    }
    File_Close(static_cast<int32_t>(word_at(note, 8)));
    SmallHeap_Free(title_global<uint8_t*>(0x0095f520), note);
    if (context == 0xffffffff) return;
    uint8_t* const memory = title_global<uint8_t*>(0x0095f638);
    title_global<uint8_t>(0x0095f5d0) = saved;
    title_global<uint8_t>(0x0095f5d1) = 0;
    MemContext_Pop(memory);
}

namespace {

// The entity behind the handle game import +0x8ac returns (the note's owner), or null.
uint8_t* cod_note_entity() {
    const uint32_t handle =
        title_function<uint32_t XB_CDECL()>(word_at(title_global<uint8_t*>(0x0095f49c), 0x8ac))();
    const uint32_t index = handle & 0xfff;
    if (static_cast<int32_t>(index) >= 0x540) return nullptr;
    if ((handle >> 12) != title_global<uint32_t>(0x0097a96c + index * 8)) return nullptr;
    return title_global<uint8_t*>(0x0097a968 + index * 8);  // g_entityHandleTable
}

}  // namespace

void XB_CDECL G_ProcessCodNotes(uint32_t time) {
    using Method = uint32_t XB_THISCALL(uint8_t*);
    using Method1 = void XB_THISCALL(uint8_t*, uint32_t);
    auto method = [](uint8_t* object, uint32_t slot) {
        return word_at(reinterpret_cast<uint8_t*>(uintptr_t{word_at(object, 0)}), slot);
    };
    uint32_t first = title_global<uint32_t>(0x008dda50);
    uint8_t* note = reinterpret_cast<uint8_t*>(uintptr_t{first});
    uint32_t following = first ? word_at(note, 0) : 0;
    uint32_t pending = first;
    if (first == 0x008dda54 || !following) return;  // the list ends at the sentinel 0x008dda54
    for (;;) {
        if (following) {
            pending = following;
            following = word_at(reinterpret_cast<uint8_t*>(uintptr_t{following}), 0);
        }
        const uint32_t context = word_at(note, 0x24);
        uint8_t* const object = reinterpret_cast<uint8_t*>(uintptr_t{word_at(note, 0xc)});
        uint8_t saved = 0;
        if (context != 0xffffffff) {
            saved = title_global<uint8_t>(0x0095f5d0);
            title_global<uint8_t>(0x0095f5d1) = 0;
            title_global<uint8_t>(0x0095f5d0) = 1;
            MemContext_Push(title_global<uint8_t*>(0x0095f638), context);
        }
        title_function<Method1>(method(object, 0xc))(object, time);
        if (!note[0x18] && (title_function<Method>(method(object, 4))(object) & 0xff)) {
            Scr_Notify(cod_note_entity(), word_at(note, 0x10));
            note[0x18] = 1;
        }
        const uint8_t started = note[0x18];
        if (started) {
            uint8_t* const anim = reinterpret_cast<uint8_t*>(uintptr_t{word_at(object, 4)});
            if (!((word_at(anim, 4) >> 1) & 1) &&
                *reinterpret_cast<const float*>(object + 0x10) >= *reinterpret_cast<const float*>(anim + 0x3c)) {
                uint8_t* const next = reinterpret_cast<uint8_t*>(uintptr_t{word_at(note, 0)});
                word_at(next, 4) = word_at(note, 4);
                uint8_t* const previous = reinterpret_cast<uint8_t*>(uintptr_t{word_at(note, 4)});
                word_at(previous, 0) = word_at(note, 0);
                --title_global<int32_t>(0x008dda4c);
                Scr_Notify(cod_note_entity(), word_at(note, 0x10));
                title_function<Method1>(method(object, 0))(object, 1);
                File_Close(static_cast<int32_t>(word_at(note, 8)));
                SmallHeap_Free(title_global<uint8_t*>(0x0095f520), note);
            } else {
                uint8_t* const current = reinterpret_cast<uint8_t*>(uintptr_t{word_at(object, 4)});
                const float end = *reinterpret_cast<const float*>(current + 0x3c) - *reinterpret_cast<const float*>(note + 0x20);
                if (*reinterpret_cast<const float*>(object + 0x10) > end && word_at(note, 0x14))
                    Scr_Notify(cod_note_entity(), word_at(note, 0x14));
            }
        }
        uint8_t* const memory = title_global<uint8_t*>(0x0095f638);
        if (context != 0xffffffff) {
            title_global<uint8_t>(0x0095f5d1) = 0;
            title_global<uint8_t>(0x0095f5d0) = saved;
            MemContext_Pop(memory);
        }
        if (!following) return;
        note = reinterpret_cast<uint8_t*>(uintptr_t{pending});
    }
}

void XB_STDCALL G_FreeAllEntities(uint8_t* state) {
    // The entity list 0x0097d370 (count 0x0097d36c).
    uint32_t* const end = &title_global<uint32_t>(0x0097d370 + title_global<uint32_t>(0x0097d36c) * 4);
    for (uint32_t* slot = &title_global<uint32_t>(0x0097d370); slot != end; ++slot) {
        if (!*slot) continue;
        title_global<uint8_t>(0x008d50d4) = 0;
        G_FreeEntity(reinterpret_cast<uint8_t*>(uintptr_t{*slot}));
    }
    word_at(state, 4) = 0;
    word_at(state, 8) = 0;
}

namespace {

// 0x002be510 (was FUN_002be510; ECX check, EBX entity): refreshes the entity's link tag index; unlinks it when the
// tag is gone.
void ent_update_link_tag(uint32_t check, uint8_t* ent) {
    uint32_t ebx = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ent));
    uint32_t function = 0x002be510;
    __asm__ volatile("pushl %%ebx\n\tmovl %%esi, %%ebx\n\tcall *%%edx\n\tpopl %%ebx"
                     : "+c"(check), "+d"(function), "+S"(ebx)
                     :
                     : "eax", "edi", "memory", "cc");
}

}  // namespace

PLATFORM_ASM_ENTRY(G_EntUpdateLinkedTags_entry,
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(G_EntUpdateLinkedTags) "addl $8, %esp\n"
                   "ret")

void XB_CDECL G_EntUpdateLinkedTags(uint8_t* ent, uint32_t check) {
    if (word_at(ent, 0x248)) Vehicle_CacheTags(ent);
    uint8_t* child = reinterpret_cast<uint8_t*>(uintptr_t{word_at(ent, 0x3d0)});
    while (child) {
        uint8_t* const next =
            reinterpret_cast<uint8_t*>(uintptr_t{word_at(reinterpret_cast<uint8_t*>(uintptr_t{word_at(child, 0x3cc)}), 4)});
        ent_update_link_tag(check, child);
        child = next;
    }
}

PLATFORM_ASM_ENTRY(G_FindConfigstringIndex_entry,
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(G_FindConfigstringIndex) "addl $20, %esp\n"
                   "ret")

int32_t XB_CDECL G_FindConfigstringIndex(int32_t max, const char* name, int32_t base, uint32_t create,
                                         const char* error) {
    if (!name || !*name) return 0;
    int32_t i = 1;
    for (; i < max; ++i) {
        const uint32_t string = title_global<uint32_t>(0x00964ce0 + (base + i) * 4);  // configstrings
        const char* const text = string ? reinterpret_cast<const char*>(uintptr_t{string + 6}) : nullptr;
        if (!*text) break;
        if (!title_function<int32_t XB_CDECL(const char*, const char*)>(0x001f1443)(text, name)) return i;  // __stricmp
    }
    if (!create) {
        if (error)
            title_function<const char* XB_CDECL(const char*, ...)>(0x002a93d0)(  // va (the error call is gone)
                reinterpret_cast<const char*>(uintptr_t{0x0083289c}), error, name);  // "%s \"%s\" not precached"
        return 0;
    }
    if (i == max) return 0;
    SV_SetConfigstring(base + i, name);
    if (base == 0x20 || base == 0xe0) title_global<uint32_t>(0x0098601c) = 1;
    return i;
}

// The cod note list: count 0x008dda4c, head node 0x008dda50 {first, sentinel 0x008dda54 = 0, last}.
void XB_CDECL G_FreeAllCodNotes() {
    while (title_global<uint32_t>(0x008dda50) != 0x008dda54) {
        uint8_t* note = title_global<uint8_t*>(0x008dda58);
        if (note == reinterpret_cast<uint8_t*>(uintptr_t{0x008dda50})) {
            note = nullptr;
        } else {
            uint8_t* const previous = reinterpret_cast<uint8_t*>(uintptr_t{word_at(note, 4)});
            title_global<uint8_t*>(0x008dda58) = previous;
            word_at(previous, 0) = word_at(note, 0);
            --title_global<int32_t>(0x008dda4c);
        }
        CodNote_Free(note);
    }
}

void XB_CDECL G_FreeCodNote(uint8_t* note) {
    const auto debug_break = title_function<void XB_CDECL(uint8_t*)>(0x00042460);  // Sys_DebugBreak (tail jump)
    uint8_t* current = title_global<uint8_t*>(0x008dda50);
    uint8_t* next = reinterpret_cast<uint8_t*>(uintptr_t{word_at(current, 0)});
    bool found = false;
    while (next) {
        if (current == note) {
            found = true;
            break;
        }
        current = next;
        next = reinterpret_cast<uint8_t*>(uintptr_t{word_at(next, 0)});
    }
    if (!found || !word_at(note, 0)) {
        debug_break(note);
        return;
    }
    word_at(reinterpret_cast<uint8_t*>(uintptr_t{word_at(note, 0)}), 4) = word_at(note, 4);
    word_at(reinterpret_cast<uint8_t*>(uintptr_t{word_at(note, 4)}), 0) = word_at(note, 0);
    word_at(note, 0) = 0;
    word_at(note, 4) = 0;
    --title_global<int32_t>(0x008dda4c);
    CodNote_Free(note);
}

uint8_t* XB_CDECL G_CreateCodNote(const char* name, uint32_t notify, uint32_t start_bits, uint32_t end_bits,
                                  uint32_t use_zone_context) {
    uint8_t* const memory_global = &title_global<uint8_t>(0x0095f638);
    uint8_t saved = 0;
    int32_t context = -1;
    if (static_cast<uint8_t>(use_zone_context)) {
        // The memory context id of the pak's current zone.
        uint8_t* const pak = title_global<uint8_t*>(0x0095f640);
        uint32_t index = word_at(pak, 0x1c4);
        uint8_t* const record = reinterpret_cast<uint8_t*>(uintptr_t{word_at(pak, 4 + word_at(pak, 0x1cc) * 4)});
        const uint32_t count = word_at(record, 0x1c);
        uint8_t* const zones = reinterpret_cast<uint8_t*>(uintptr_t{word_at(record, 0x20)});
        if (index >= count) index = 0;
        uint8_t* const zone = reinterpret_cast<uint8_t*>(uintptr_t{word_at(zones, index * 4)});
        uint8_t* const a = reinterpret_cast<uint8_t*>(uintptr_t{word_at(zone, 0x2c)});
        uint8_t* const b = reinterpret_cast<uint8_t*>(uintptr_t{word_at(a, 0x24)});
        context = static_cast<int32_t>(word_at(b, 0x28));
        if (context != -1) {
            saved = title_global<uint8_t>(0x0095f5d0);
            title_global<uint8_t>(0x0095f5d1) = 0;
            title_global<uint8_t>(0x0095f5d0) = 1;
            MemContext_Push(*reinterpret_cast<uint8_t**>(memory_global), static_cast<uint32_t>(context));
        }
    }
    uint8_t* const memory = *reinterpret_cast<uint8_t**>(memory_global);
    const int32_t current = static_cast<int32_t>(word_at(memory, 0x34));
    uint32_t data = 0;
    uint32_t size = 0;
    uint8_t* const heap = current >= 0 && current < 0x63
                              ? reinterpret_cast<uint8_t*>(uintptr_t{word_at(memory, 0x38 + current * 4)})
                              : nullptr;
    Stream_FindZoneRawFile(name, title_global<uint8_t*>(0x0095f630), static_cast<uint32_t>(current), &data, &size);
    uint8_t* const note = SmallHeap_Alloc(title_global<uint8_t*>(0x0095f520), 0x28, 0);
    if (note) {
        word_at(note, 0) = 0;
        word_at(note, 4) = 0;
        word_at(note, 0xc) = 0;
        note[0x18] = 0;
        note[0x19] = 0;
        word_at(note, 0x1c) = start_bits;
        word_at(note, 0x20) = end_bits;
        word_at(note, 0x24) = 0xffffffff;
    }
    const int32_t file = File_OpenRead(title_global<uint32_t>(0x0095f6f4), reinterpret_cast<const char*>(heap + 8));
    word_at(note, 8) = static_cast<uint32_t>(file);
    word_at(note, 0xc) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(NAL_StreamedAnim_Create(
        static_cast<uint32_t>(file), data, size, 0, 0x002beb20,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(note + 0x1c)))));
    word_at(note, 0x10) = notify;
    word_at(note, 0x24) = static_cast<uint32_t>(context);
    word_at(note, 0) = 0x008dda54;
    const uint32_t last = title_global<uint32_t>(0x008dda58);
    word_at(note, 4) = last;
    word_at(reinterpret_cast<uint8_t*>(uintptr_t{last}), 0) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(note));
    ++title_global<int32_t>(0x008dda4c);
    title_global<uint8_t*>(0x008dda58) = note;
    if (context != -1) {
        title_global<uint8_t>(0x0095f5d1) = 0;
        title_global<uint8_t>(0x0095f5d0) = saved;
        MemContext_Pop(*reinterpret_cast<uint8_t**>(memory_global));
    }
    return note;
}

uint32_t XB_CDECL PM_ActivateTargetActor() {
    uint8_t* const pm = title_global<uint8_t*>(0x0092eff4);  // pm
    uint8_t* const ps = reinterpret_cast<uint8_t*>(uintptr_t{word_at(pm, 0)});
    if (!(word_at(ps, 0x2c) & 0x1000000)) return 0;
    if (word_at(ps, 0x3c)) {
        const uint8_t* const weapon =
            reinterpret_cast<const uint8_t*>(uintptr_t{word_at(title_global<uint8_t*>(0x0095f4d8), word_at(ps, 0x80) * 4)});
        if (*reinterpret_cast<const uint32_t*>(weapon + 0x94) == 1) return 0;  // bg_weaponDefs
    }
    word_at(ps, 0x3c) = 0;
    G_ClientActivateTargetActor(reinterpret_cast<const uint8_t*>(uintptr_t{word_at(pm, 0)}));
    return 1;
}

float* XB_REGS3 XB_STDCALL G_DObjGetBoneMatrix(const uint8_t* ent, uint32_t, uint32_t bone, float* out) {
    const uint8_t* const obj = reinterpret_cast<const uint8_t*>(uintptr_t{*reinterpret_cast<const uint32_t*>(ent + 0x22c)});
    if (!obj) {
        alignas(16) float identity[16];
        Matrix_Identity(identity);
        std::memcpy(out, identity, sizeof identity);
        return out;
    }
    const uint8_t* const bones = reinterpret_cast<const uint8_t*>(uintptr_t{*reinterpret_cast<const uint32_t*>(obj + 0x20)});
    std::memcpy(out, bones + 0x30 + bone * 0x40, 0x40);
    return out;
}

uint8_t* XB_REGS1 XB_STDCALL G_InitGentity(uint8_t* ent, int32_t context) {
    using FromRep = void XB_THISCALL(uint8_t*, uint32_t);
    const auto from_rep = title_function<FromRep>(0x0011d5d0);  // ScriptString_FromRep
    const auto clear = title_function<void XB_THISCALL(uint8_t*)>(0x0011d660);  // ScriptString_Clear
    auto zero_words = [&](uint32_t begin, uint32_t end) {
        for (uint32_t f = begin; f < end; f += 4) word_at(ent, f) = 0;
    };
    EntityState_Clear(ent);
    title_function<void XB_REGS1 XB_CDECL(uint8_t*)>(0x002ceb50)(ent + 0xd0);  // FUN_002ceb50 (physics state)
    word_at(ent, 0x220) = static_cast<uint32_t>(context);
    word_at(ent, 0x224) = 0;
    word_at(ent, 0x228) = 0xffffffff;
    zero_words(0x22c, 0x258);
    word_at(ent, 0x258) = 0;
    from_rep(ent + 0x25c, 0);  // classname
    word_at(ent, 0x260) = 0;
    from_rep(ent + 0x264, 0);  // targetname
    word_at(ent, 0x268) = 0;
    from_rep(ent + 0x26c, 0);
    word_at(ent, 0x270) = 0;
    from_rep(ent + 0x274, 0);
    word_at(ent, 0x278) = 0;
    from_rep(ent + 0x27c, 0);
    from_rep(ent + 0x284, 0);
    word_at(ent, 0x28c) = 0;
    word_at(ent, 0x290) = 0xffffffff;
    ent[0x294] = 0;
    ent[0x295] = 0;
    ent[0x297] = 0;
    ent[0x298] = 0;
    ent[0x299] = 0;
    zero_words(0x29c, 0x2b4);
    word_at(ent, 0x2f0) = 0;
    word_at(ent, 0x2f4) = 0;  // 0.0f
    from_rep(ent + 0x2f8, 0);
    word_at(ent, 0x2fc) = 0;
    word_at(ent, 0x300) = 0;
    zero_words(0x320, 0x384);
    zero_words(0x384, 0x390);  // 0.0f
    word_at(ent, 0x3b0) = 0;
    word_at(ent, 0x3b4) = 0;
    from_rep(ent + 0x3b8, 0);
    zero_words(0x3bc, 0x3d8);
    for (uint32_t i = 0; i < 7; ++i) {
        word_at(ent, 0x3d8 + i * 8) = 0;
        from_rep(ent + 0x3dc + i * 8, 0);
    }
    *reinterpret_cast<uint16_t*>(ent + 0x410) = 0;
    zero_words(0x414, 0x42c);
    word_at(ent, 0x450) = 0;
    word_at(ent, 0x454) = 0;
    word_at(ent, 0x458) = 0xffffffff;
    word_at(ent, 0x260) = 0;
    word_at(ent, 0x3c4) = 0;
    word_at(ent, 0x3c8) = 0;
    // Bounds: +0x430 = FLT_MAX x4 (0x0082eed0), +0x440 = its negation (XORPS 0x0082e0b0).
    const uint32_t big = title_global<uint32_t>(0x0082eed0);
    for (uint32_t f = 0; f < 0x10; f += 4) word_at(ent, 0x430 + f) = big;
    for (uint32_t f = 0; f < 0x10; f += 4) word_at(ent, 0x440 + f) = word_at(ent, 0x430 + f) ^ 0x80000000u;
    static const uint32_t kZeroVectors[] = {0x2c0, 0x2d0, 0x2e0, 0x310, 0x390, 0x3a0};
    for (const uint32_t f : kZeroVectors) {
        word_at(ent, f) = 0;  // 0.0f
        word_at(ent, f + 4) = 0;
        word_at(ent, f + 8) = 0;
    }
    G_UpdateEntityStringHashes(ent);
    uint32_t* const classname = &word_at(ent, 0x25c);
    title_function<void XB_THISCALL(uint32_t*, const uint32_t*)>(0x0011d600)(  // ScriptString_Assign
        classname, &title_global<uint32_t>(0x00984ba4));
    const uint32_t name = *classname;
    word_at(ent, 0x260) = Com_HashKeyNoCase(0, name ? reinterpret_cast<const char*>(uintptr_t{name + 6}) : nullptr);
    word_at(ent, 0x1a0) = 0;
    word_at(ent, 0x2b0) = 0;
    word_at(ent, 0x1a4) = 0;
    word_at(ent, 0x1a8) = 0;
    word_at(ent, 0x2a0) = 0;
    word_at(ent, 0x12c) = 0;  // 0.0f
    clear(ent + 0x27c);
    clear(ent + 0x264);
    word_at(ent, 0x424) = 0;
    word_at(ent, 0x420) = 0;
    word_at(ent, 0x2fc) = title_global<uint32_t>(0x0083eb34);  // -1.0f
    G_UpdateEntityStringHashes(ent);
    G_EntityList_Add(ent, &title_global<uint8_t>(0x0097a8c0));
    return ent;
}

uint8_t* XB_REGS1 XB_CDECL G_Spawn(int32_t context) {
    uint8_t* const memory = title_global<uint8_t*>(0x0095f638);
    uint8_t* ent = nullptr;
    if (context == -1) context = static_cast<int32_t>(word_at(memory, 0x34));
    if (context != -1) {
        uint8_t* const heap = context >= 0 && context < 0x63
                                  ? reinterpret_cast<uint8_t*>(uintptr_t{word_at(memory, 0x38 + context * 4)})
                                  : nullptr;
        ent = MemContext_Alloc_r(heap, 0x10, 0x460);
    }
    if (!ent) ent = Heap_AllocAligned(0x10, 0x460);
    if (!ent) {
        ++title_global<int32_t>(0x00985374);
        return nullptr;
    }
    uint8_t* const result = G_InitGentity(ent, context);
    ++title_global<int32_t>(0x00985374);
    return result;
}

void XB_REGS3 XB_CDECL G_SetModel(int32_t context, uint8_t* ent, const char* name) {
    uint32_t* const char_hash = &title_global<uint32_t>(0x0098c394);
    if (!(title_global<uint8_t>(0x0098c3b4) & 1)) {
        title_global<uint32_t>(0x0098c3b4) |= 1;
        HashStr_Init(char_hash, reinterpret_cast<const char*>(uintptr_t{0x008329a0}));  // "cdChar"
    }
    if (!name) return;
    if (context == -1) {
        context = static_cast<int32_t>(word_at(ent, 0x220));
        if (context == -1) context = static_cast<int32_t>(word_at(title_global<uint8_t*>(0x0095f638), 0x34));
    }
    uint32_t model = 0;
    R_RegisterModel(name, title_global<uint32_t>(0x0095f6ac), &model, static_cast<uint32_t>(context));  // rm_models
    if (!model) return;
    word_at(ent, 0x258) = model;
    uint8_t* const info = reinterpret_cast<uint8_t*>(uintptr_t{word_at(reinterpret_cast<uint8_t*>(uintptr_t{model}), 0x20)});
    uint8_t* const lods = reinterpret_cast<uint8_t*>(uintptr_t{word_at(info, 0x2c)});
    uint8_t* const mesh = reinterpret_cast<uint8_t*>(uintptr_t{word_at(lods, 0)});
    if (!mesh) return;
    auto follow = [](uint8_t* p, uint32_t offset) { return reinterpret_cast<uint8_t*>(uintptr_t{word_at(p, offset)}); };
    uint8_t* const node = follow(follow(follow(mesh, 0xc), 4), 4);
    uint8_t* const object = follow(node, 4);
    uint32_t name_hash[5];
    using NameOf = const uint32_t* XB_THISCALL(uint8_t*, uint32_t*);
    const uint32_t* const hashed =
        title_function<NameOf>(word_at(follow(object, 0), 4))(object, name_hash);  // vtable slot 1: the name hash
    if (!HashStr_Equals(char_hash, hashed)) return;
    // A cdChar skin with 15 variants: a random one (1..count-1) in the entity's render record (+0xee).
    uint8_t* const skin = follow(node, 0x1c);
    if (word_at(skin, 4) != 0xf) return;
    const int32_t r = title_function<int32_t XB_CDECL()>(0x001efcc0)();  // _rand
    const uint8_t variant =
        static_cast<uint8_t>(static_cast<uint32_t>(r >> 2) % (word_at(skin, 0x4c) - 1) + 1);
    CG_GetEntityRefEntity(ent)[0xee] = variant;
}

float* XB_REGS3 XB_STDCALL G_DObjGetWorldBoneMatrix(const uint8_t* ent, uint32_t, uint32_t bone, float* out) {
    const uint8_t* const obj = reinterpret_cast<const uint8_t*>(uintptr_t{*reinterpret_cast<const uint32_t*>(ent + 0x22c)});
    if (!obj) {
        alignas(16) float identity[16];
        Matrix_Identity(identity);
        std::memcpy(out, identity, sizeof identity);
        return out;
    }
    const float* const m = reinterpret_cast<const float*>(
        uintptr_t{*reinterpret_cast<const uint32_t*>(obj + 0x20)} + 0x30 + bone * 0x40);
    const float* const w = reinterpret_cast<const float*>(ent + 0x160);  // the entity's 4x4 world matrix
    // Rows 0-2: (w0 * x + w1 * y) + w2 * z over all four lanes; row 3 adds them to w3.
    auto row = [&](const Lanes& r) {
        return add(add(mul(lanes(w), r.v[0]), mul(lanes(w + 4), r.v[1])), mul(lanes(w + 8), r.v[2]));
    };
    const Lanes r0 = row(lanes(m)), r1 = row(lanes(m + 4)), r2 = row(lanes(m + 8));
    std::memcpy(out, r0.v, 16);
    std::memcpy(out + 4, r1.v, 16);
    std::memcpy(out + 8, r2.v, 16);
    const Lanes t = lanes(m + 12);
    const Lanes x = mul(lanes(w), t.v[0]), y = mul(lanes(w + 4), t.v[1]), z = mul(lanes(w + 8), t.v[2]);
    const Lanes r3 = add(add(add(lanes(w + 12), x), y), z);
    std::memcpy(out + 12, r3.v, 16);
    return out;
}

void XB_CDECL G_UpdateSoundListenerFromPlayer() {
    uint8_t* const player = *reinterpret_cast<uint8_t**>(title_global<uint8_t*>(0x0095f534) + 4);
    uint32_t origin[4];
    std::memcpy(origin, player + 0x140, sizeof origin);
    uint32_t args[2] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(player + 0x150)), 0};
    alignas(16) float forward[4], right[4], up[4];
    args[1] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(forward));
    AnglesToAxis(args, right, up);
    // The listener axes: 0 - right, then up.
    float left[3];
    left[0] = 0.0f - right[0];
    left[2] = 0.0f - right[2];
    left[1] = 0.0f - right[1];
    uint32_t a[3], b[3];
    std::memcpy(a, left, sizeof a);
    std::memcpy(b, up, sizeof b);
    Snd_SetListenerOrigin(origin);
    Snd_SetListenerOrientation(a, b);
}

namespace {

// A 0x460-byte entity in the current memory context (else Heap_AllocAligned), G_InitGentity; counts the attempt.
uint8_t* alloc_entity_in_current_context() {
    uint8_t* const memory = title_global<uint8_t*>(0x0095f638);
    const int32_t context = static_cast<int32_t>(word_at(memory, 0x34));
    uint8_t* ent = nullptr;
    if (context != -1) {
        uint8_t* const heap = context >= 0 && context < 0x63
                                  ? reinterpret_cast<uint8_t*>(uintptr_t{word_at(memory, 0x38 + context * 4)})
                                  : nullptr;
        ent = MemContext_Alloc_r(heap, 0x10, 0x460);
    }
    if (!ent) ent = Heap_AllocAligned(0x10, 0x460);
    if (!ent) {
        ++title_global<int32_t>(0x00985374);
        return nullptr;
    }
    uint8_t* const result = G_InitGentity(ent, context);
    ++title_global<int32_t>(0x00985374);
    return result;
}

}  // namespace

PLATFORM_ASM_ENTRY(G_AllocWorldEntity_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(G_AllocWorldEntity) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL G_AllocWorldEntity(uint8_t* level) {
    uint8_t* const ent = alloc_entity_in_current_context();
    word_at(level, 8) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ent));
    return ent;
}

PLATFORM_ASM_ENTRY(G_AllocPlayerEntity_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(G_AllocPlayerEntity) "addl $4, %esp\n"
                   "ret")

uint8_t* XB_CDECL G_AllocPlayerEntity(uint8_t* level) {
    if (word_at(level, 4)) return reinterpret_cast<uint8_t*>(uintptr_t{word_at(level, 4)});
    uint8_t* const ent = alloc_entity_in_current_context();
    word_at(level, 4) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ent));
    return ent;
}

void XB_REGS1 XB_CDECL G_SetPoseFromPhysics(uint8_t* ent) {
    // Trajectories stopped: deltas 0, types/times 0.
    static const uint32_t kStopped[] = {0x34, 0x30, 0x2c, 0x58, 0x54, 0x50};
    for (const uint32_t f : kStopped) word_at(ent, f) = 0;  // 0.0f
    word_at(ent, 0x18) = 0;
    word_at(ent, 0x1c) = 0;
    word_at(ent, 0x3c) = 0;
    word_at(ent, 0x40) = 0;
    if (word_at(ent, 0x23c)) {
        word_at(ent, 0x44) = word_at(ent, 0x150);
        word_at(ent, 0x48) = word_at(ent, 0x154);
        word_at(ent, 0x4c) = word_at(ent, 0x158);
    }
    // Angles (+0x150) from the physics matrix (+0x160), origin (+0x140) from its translation row (+0x190).
    alignas(16) uint32_t angles[3] = {word_at(ent, 0x150), word_at(ent, 0x154), word_at(ent, 0x158)};
    MatrixToAngles(reinterpret_cast<float*>(angles), 0, reinterpret_cast<const float*>(ent + 0x160));
    word_at(ent, 0x150) = angles[0];
    word_at(ent, 0x154) = angles[1];
    word_at(ent, 0x158) = angles[2];
    for (uint32_t f = 0; f < 0x10; f += 4) word_at(ent, 0x140 + f) = word_at(ent, 0x190 + f);
    word_at(ent, 0x20) = word_at(ent, 0x140);
    word_at(ent, 0x24) = word_at(ent, 0x144);
    word_at(ent, 0x28) = word_at(ent, 0x148);
    uint8_t* const client = reinterpret_cast<uint8_t*>(uintptr_t{word_at(ent, 0x23c)});
    const uint32_t base = client ? 0x50 : 0x44;
    word_at(ent, base) = word_at(ent, 0x150);
    word_at(ent, base + 4) = word_at(ent, 0x154);
    word_at(ent, base + 8) = word_at(ent, 0x158);
    word_at(ent, 0x14) = 0;
    word_at(ent, 0x38) = 0;
    if (client) {
        // The client's old origin (+0x5b0) and origin.
        for (uint32_t f = 0; f < 0xc; f += 4) {
            uint8_t* const ps = reinterpret_cast<uint8_t*>(uintptr_t{word_at(ent, 0x23c)});
            word_at(ps, 0x5b0 + f) = word_at(ps, f);
        }
        for (uint32_t f = 0; f < 0xc; f += 4)
            word_at(reinterpret_cast<uint8_t*>(uintptr_t{word_at(ent, 0x23c)}), f) = word_at(ent, 0x140 + f);
    }
    SV_LinkEntity(ent);
}

uint8_t* XB_CDECL G_TempEntity(const uint32_t* origin, uint32_t event) {
    uint8_t* const ent = alloc_entity_in_current_context();
    uint32_t* const classname = &word_at(ent, 0x25c);
    ent[0] = static_cast<uint8_t>(static_cast<uint8_t>(event) + 0x11);  // eType = event + 0x11
    title_function<void XB_THISCALL(uint32_t*, const uint32_t*)>(0x0011d600)(  // ScriptString_Assign
        classname, &title_global<uint32_t>(0x00984c50));
    const uint32_t name = *classname;
    word_at(ent, 0x260) = Com_HashKeyNoCase(0, name ? reinterpret_cast<const char*>(uintptr_t{name + 6}) : nullptr);
    word_at(ent, 0x1a4) = 1;
    word_at(ent, 0x1a8) = title_global<uint32_t>(0x0098540c);  // level_time
    const uint32_t x = origin[0], y = origin[1], z = origin[2];
    word_at(ent, 0x20) = x;
    word_at(ent, 0x24) = y;
    word_at(ent, 0x28) = z;
    word_at(ent, 0x14) = 0;
    word_at(ent, 0x18) = 0;
    word_at(ent, 0x1c) = 0;
    word_at(ent, 0x34) = 0;  // 0.0f
    word_at(ent, 0x30) = 0;
    word_at(ent, 0x2c) = 0;
    word_at(ent, 0x140) = x;
    word_at(ent, 0x144) = y;
    word_at(ent, 0x148) = z;
    SV_LinkEntity(ent);
    G_UpdateEntityStringHashes(ent);
    return ent;
}

uint32_t XB_REGS1 XB_CDECL G_EntLinkTo(uint8_t* parent, uint8_t* ent, const char* tag, uint32_t flags) {
    G_EntUnlink(ent);
    int16_t tag_index = -1;
    if (tag && *tag) {
        const DObj* const obj = *reinterpret_cast<const DObj* const*>(parent + 0x22c);
        if (!obj) return 0;
        const int32_t index = DObj_GetTagIndex(obj, Com_HashKeyNoCase(0, tag));
        if (index < 0) return 0;
        tag_index = static_cast<int16_t>(index);
    }
    // No link cycles: the parent and its own parents (+0x3cc link, +0 parent) must not be the entity.
    for (uint8_t* p = parent;;) {
        if (p == ent) return 0;
        uint8_t* const link = reinterpret_cast<uint8_t*>(uintptr_t{word_at(p, 0x3cc)});
        if (!link) break;
        p = reinterpret_cast<uint8_t*>(uintptr_t{word_at(link, 0)});
    }
    uint8_t* link = SmallHeap_Alloc(title_global<uint8_t*>(0x0095f598), 0x70, 0);
    if (link) word_at(link, 8) = 0;
    word_at(link, 0) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(parent));
    word_at(link, 8) = tag ? Com_HashKeyNoCase(0, tag) : 0;
    word_at(link, 4) = word_at(parent, 0x3d0);  // the parent's child list
    *reinterpret_cast<int16_t*>(link + 0xc) = tag_index;
    *reinterpret_cast<uint16_t*>(link + 0xe) = static_cast<uint8_t>(flags);
    std::memset(link + 0x10, 0, 0x30);
    word_at(parent, 0x3d0) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ent));
    word_at(ent, 0x3cc) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(link));
    if (!word_at(ent, 0x3d4) && !*reinterpret_cast<uint16_t*>(link + 0xe)) {
        std::memset(link + 0x40, 0, 0x30);
    } else {
        // The offset: the inverse of the entity's matrix in the tag's parent frame.
        float parent_matrix[12];
        title_function<void XB_REGS1 XB_CDECL(uint8_t*, float*)>(0x002df3e0)(ent, parent_matrix);  // G_GetTagParentMatrix
        MatrixInverse43(reinterpret_cast<float*>(link + 0x40), 0, parent_matrix);
    }
    if (word_at(ent, 0x23c)) {
        uint8_t* const vehicle = reinterpret_cast<uint8_t*>(uintptr_t{word_at(parent, 0x248)});
        if (vehicle) vehicle[0x238] = 1;
    }
    return 1;
}

PLATFORM_ASM_ENTRY(G_DObjCreateForEnt_entry,
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl 16(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(G_DObjCreateForEnt) "addl $20, %esp\n"
                   "ret $16")

void XB_CDECL G_DObjCreateForEnt(uint8_t* ent, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    if (!word_at(ent, 0x22c)) {
        const int32_t context = static_cast<int32_t>(word_at(ent, 0x220));
        uint8_t* block = nullptr;
        if (context != -1) {
            uint8_t* const heap = context >= 0 && context < 0x63
                                      ? reinterpret_cast<uint8_t*>(uintptr_t{word_at(title_global<uint8_t*>(0x0095f638), 0x38 + context * 4)})
                                      : nullptr;
            block = MemContext_Alloc_r(heap, 0x10, 0xa0);
        }
        if (!block) block = Heap_AllocAligned(0x10, 0xa0);
        DObj* const obj = block ? DObj_Ctor(static_cast<int32_t>(word_at(ent, 0x220)), reinterpret_cast<DObj*>(block)) : nullptr;
        word_at(ent, 0x22c) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(obj));
        G_EntityList_QueueAdd(word_at(ent, 0x224));
    }
    // DObj_Create (EAX = c, EDX = a, ECX = d; stack b, the DObj).
    TitleRegs regs{c, 0, d, a, 0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ent))};
    const uint32_t args[2] = {b, word_at(ent, 0x22c)};
    g_ent_call(0x00245ff0, &regs, 2, args);
    word_at(reinterpret_cast<uint8_t*>(uintptr_t{word_at(ent, 0x22c)}), 0x88) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ent));
}

const uint8_t* XB_REGS3 XB_CDECL G_DObjGetTagMatrixPtr(uint8_t* ent, uint32_t, uint32_t name) {
    const DObj* const obj = *reinterpret_cast<const DObj* const*>(ent + 0x22c);
    if (!obj) return nullptr;
    const int32_t index = DObj_GetTagIndex(obj, name);
    if (index < 0) return nullptr;
    SV_DObjCreateSkeleton(ent);
    const uint8_t* const dobj = reinterpret_cast<const uint8_t*>(uintptr_t{word_at(ent, 0x22c)});
    const uint32_t matrices = *reinterpret_cast<const uint32_t*>(dobj + 0x20);
    const uint32_t base = matrices ? dobj[0x60] * 0x40 + matrices + 0x30 : 0;
    return reinterpret_cast<const uint8_t*>(uintptr_t{base + static_cast<uint32_t>(index) * 0x40});
}

void XB_CDECL G_DObjGetWorldTagMatrix(uint8_t* ent, uint32_t bone, float* out) {
    SV_DObjCreateSkeleton(ent);
    const uint8_t* const dobj = reinterpret_cast<const uint8_t*>(uintptr_t{word_at(ent, 0x22c)});
    const uint32_t matrices = *reinterpret_cast<const uint32_t*>(dobj + 0x20);
    const uint32_t base = matrices ? dobj[0x60] * 0x40 + matrices + 0x30 : 0;
    // The entity's 4x3 frame: forward, 0 - right, up, origin.
    float frame[12];
    alignas(16) float right[4];
    uint32_t args[2] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ent + 0x150)),
                        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(frame))};
    AnglesToAxis(args, right, frame + 6);
    frame[3] = 0.0f - right[0];
    frame[5] = 0.0f - right[2];
    frame[4] = 0.0f - right[1];
    std::memcpy(frame + 9, ent + 0x140, 12);
    MatrixMultiply43To44(frame, out, reinterpret_cast<const float*>(uintptr_t{base + bone * 0x40}));
}

}  // namespace game
