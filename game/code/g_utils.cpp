#include "g_utils.h"

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

void XB_REGS3 XB_CDECL G_SetOrigin(uint8_t* ent, uint32_t, const float* origin) {
    set_trajectory(ent, origin, 0x20, 0x14, 0x2c, 0x140);
}

void XB_REGS3 XB_CDECL G_SetOrigin2(uint8_t* ent, uint32_t, const float* origin) {
    set_trajectory(ent, origin, 0x20, 0x14, 0x2c, 0x140);
}

void XB_REGS3 XB_CDECL G_SetAngle(uint8_t* ent, uint32_t, const float* angles) {
    set_trajectory(ent, angles, 0x44, 0x38, 0x50, 0x150);
}

void XB_REGS3 XB_CDECL G_SetAngle2(uint8_t* ent, uint32_t, const float* angles) {
    set_trajectory(ent, angles, 0x44, 0x38, 0x50, 0x150);
}

void XB_REGS1 XB_CDECL G_TriggerContentsFromSpawnflags(uint8_t* ent) {
    const uint32_t flags = word_at(ent, 0x2a0);
    uint32_t& contents = word_at(ent, 0x134);
    contents = 0;
    if (!(flags & 8)) contents = 0x40000000;
    if (flags & 1) contents |= 0x40000;
    if (flags & 2) contents |= 0x80000;
    if (flags & 4) contents |= 0x100000;
    if (flags & 0x10) contents |= 8;
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

void XB_CDECL G_LocationalTrace(uint32_t results, uint32_t start, uint32_t end, uint32_t pass, uint32_t mask,
                                uint32_t a6, uint32_t a7) {
    // SV_Trace reads the bounds with MOVAPS.
    alignas(16) uint32_t mins[4], maxs[4];
    for (uint32_t i = 0; i < 4; ++i) mins[i] = maxs[i] = title_global<uint32_t>(0x0082e290 + i * 4);
    const auto address = [](const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); };
    sv_trace_call(results, address(maxs), address(mins), end, pass, mask, 0, 1, a6, 1, a7, start);
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

void XB_REGS1 XB_CDECL G_AddEvent(uint8_t* ent, uint32_t event, uint32_t parm) {
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
}

}  // namespace game
