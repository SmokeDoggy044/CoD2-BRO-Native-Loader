#include "sv.h"

#include <cstring>

#include "cm.h"
#include "qcommon.h"
#include "title_data.h"

namespace game {

namespace {

template <typename T>
T& field(uint8_t* base, uint32_t offset) {
    return *reinterpret_cast<T*>(base + offset);
}

const char* title_text(uint32_t address) { return reinterpret_cast<const char*>(uintptr_t{address}); }

using Break = void XB_CDECL(uint32_t, const char*);
constexpr uint32_t kBreak = 0x002938f0;

}  // namespace

int32_t XB_CDECL SV_GameSystemCalls(const int32_t* args) {
    title_function<void XB_CDECL(uint32_t, const char*, int32_t)>(kBreak)(1, title_text(0x00832df8), args[0]);
    return -1;
}

int32_t XB_REGS1 XB_CDECL SV_UnlinkEntity(uint8_t* ent) {
    uint8_t* sector = field<uint8_t*>(ent, 0xdc);
    if (!sector) return 0;
    field<uint8_t*>(ent, 0xdc) = nullptr;
    uint8_t*& first = field<uint8_t*>(sector, 0x10);
    if (first == ent) {
        first = field<uint8_t*>(ent, 0xe0);
    } else {
        for (uint8_t* scan = first; scan; scan = field<uint8_t*>(scan, 0xe0)) {
            if (field<uint8_t*>(scan, 0xe0) == ent) {
                field<uint8_t*>(scan, 0xe0) = field<uint8_t*>(ent, 0xe0);
                break;
            }
        }
    }
    uint8_t* const null_sector = reinterpret_cast<uint8_t*>(uintptr_t{0x0093568c});
    if (!field<uint8_t*>(sector, 0x10)) {
        for (;;) {
            if (field<uint32_t>(sector, 0x14)) break;
            if (field<uint8_t*>(sector, 0x1c) != null_sector || field<uint8_t*>(sector, 0x20) != null_sector) break;
            field<uint32_t>(sector, 8) = 0;
            uint8_t* parent = field<uint8_t*>(sector, 0x18);
            if (!parent) break;
            field<uint8_t*>(sector, 0x18) = title_global<uint8_t*>(0x00935688);
            title_global<uint8_t*>(0x00935688) = sector;
            if (field<uint8_t*>(parent, 0x1c) == sector)
                field<uint8_t*>(parent, 0x1c) = null_sector;
            else
                field<uint8_t*>(parent, 0x20) = null_sector;
            sector = parent;
            if (field<uint8_t*>(parent, 0x10)) break;
        }
    }
    for (; sector; sector = field<uint8_t*>(sector, 0x18)) {
        uint32_t contents = 0;
        if (uint8_t* child = field<uint8_t*>(sector, 0x1c)) contents = field<uint32_t>(child, 8);
        if (uint8_t* child = field<uint8_t*>(sector, 0x20)) contents |= field<uint32_t>(child, 8);
        for (uint8_t* e = field<uint8_t*>(sector, 0x10); e; e = field<uint8_t*>(e, 0xe0)) contents |= field<uint32_t>(e, 0x64);
        field<uint32_t>(sector, 8) = contents;
    }
    return 0;
}

void XB_REGS3 XB_CDECL SV_AdjustAreaPortalState(const uint8_t* ent, uint32_t, uint32_t open) {
    const int32_t area2 = *reinterpret_cast<const int32_t*>(ent + 0x200);
    if (area2 == -1) return;
    CM_AdjustAreaPortalState(*reinterpret_cast<const int32_t*>(ent + 0x1fc), area2, open);
}

void XB_CDECL SV_AllocServerStatic() {
    if (title_global<uint32_t>(0x00927e84))
        title_function<Break>(kBreak)(0, title_text(0x00832cf0));  // "SV_AllocServerStatic: called while running"
    void* svs = title_function<void* XB_CDECL(uint32_t)>(0x00042c10)(0xec0);  // Z_Malloc
    Com_Memset(static_cast<uint8_t*>(svs), 0, 0xec0);
    title_global<void*>(0x00927e8c) = svs;
    if (!svs) title_function<Break>(kBreak)(0, title_text(0x00832cc4));  // "SV_AllocServerStatic: out of memory"
    title_global<uint32_t>(0x00927e90) = 0x540;
    title_global<uint32_t>(0x00927e84) = 1;
    Cvar_Set2(title_text(0x00832cb8), const_cast<char*>(title_text(0x0082e058)), 1);  // "sv_running", "1"
}

namespace {

void set_skill(uint32_t max_health, int32_t skill) {
    Cvar_Set2(title_text(0x00832ff8), const_cast<char*>(title_text(max_health)), 1);  // g_player_maxhealth
    const uint32_t skill_args[2] = {0x008325b8, static_cast<uint32_t>(skill)};  // "%d"
    Cvar_Set2(title_text(0x00832fec), va(skill_args), 1);  // g_gameskill
    if (*reinterpret_cast<const int32_t*>(title_global<uint8_t*>(0x0093f800) + 0x20)) {
        const uint32_t reload_args[2] = {0x008325b8, 2};
        Cvar_Set2(title_text(0x0082c89c), va(reload_args), 1);  // g_reloading
    }
}

}  // namespace

void XB_CDECL SV_SetSkillEasy() { set_skill(0x00832fe0, 2); }      // "500"
void XB_CDECL SV_SetSkillMedium() { set_skill(0x00832fe4, 1); }    // "1100"
void XB_CDECL SV_SetSkillHard() { set_skill(0x0083300c, 0); }      // "2000"

uint8_t* XB_REGS3 XB_CDECL SV_ClipHandleForEntity(uint32_t, uint32_t, uint8_t* ent) {
    if (uint8_t* model = field<uint8_t*>(ent, 0xdc)) return model;
    const uint32_t capsule = (field<uint32_t>(ent, 0xd4) & 0x200) ? 1 : 0;
    return CM_TempBoxModel(reinterpret_cast<const float*>(ent + 0xf0), capsule,
                           reinterpret_cast<const float*>(ent + 0xe0), field<uint32_t>(ent, 0x134));
}

void XB_REGS1 XB_CDECL SV_AddServerCommand(uint8_t* buffer, int32_t slot, const char* text) {
    const char* overflow = title_text(0x00832d10);  // "Reliable command buffer overflow"
    if (!field<int32_t>(buffer, 4)) title_function<Break>(kBreak)(1, overflow);
    const int32_t length = static_cast<int32_t>(std::strlen(text));
    char* base = field<char*>(buffer, 8);
    char*& cursor = field<char*>(buffer, 0x14);
    if (length - static_cast<int32_t>(reinterpret_cast<uintptr_t>(base)) +
            static_cast<int32_t>(reinterpret_cast<uintptr_t>(cursor)) + 1 >=
        field<int32_t>(buffer, 4))
        cursor = base;
    if (length) {
        int32_t left = length;
        for (const char* p = cursor; left && !*p; ++p) --left;
        if (left) {
            const int32_t size = field<int32_t>(buffer, 4);
            int32_t i = 0;
            char* q = base;
            while (i < field<int32_t>(buffer, 4)) {
                if (!*q && (!i || !q[-1])) {
                    const int32_t last = field<int32_t>(buffer, 4) - 1;
                    int32_t j = 0;
                    while (i < field<int32_t>(buffer, 4) - 1 && j < length + 1 && !q[j]) {
                        ++i;
                        ++j;
                    }
                    if (j == length + 1) {
                        cursor = q;
                        break;
                    }
                    if (i == last) title_function<Break>(kBreak)(1, overflow);
                    q = field<char*>(buffer, 8) + i;
                }
                ++i;
                ++q;
            }
            (void)size;
        }
    }
    field<char*>(field<uint8_t*>(buffer, 0xc), static_cast<uint32_t>(slot) * 4) = cursor;
    field<int32_t>(field<uint8_t*>(buffer, 0x10), static_cast<uint32_t>(slot) * 4) = length;
    for (int32_t i = 0; i < length; ++i) {
        char* dst = field<char*>(field<uint8_t*>(buffer, 0xc), static_cast<uint32_t>(slot) * 4) + i;
        *dst = text[i];
        if (text[i] == '%') *dst = '.';
    }
    field<char*>(field<uint8_t*>(buffer, 0xc), static_cast<uint32_t>(slot) * 4)[length] = 0;
    cursor = field<char*>(buffer, 0x14) + length + 1;
}

PLATFORM_ASM_ENTRY(SV_SendServerCommand_entry,
                   "leal 4(%esp), %eax\n"
                   "pushl %eax\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(SV_SendServerCommand) "addl $8, %esp\n"
                   "ret")

void XB_CDECL SV_SendServerCommand(uint8_t* client, const uint32_t* args) {
    char message[0x800];
    title_function<int32_t XB_CDECL(char*, const char*, const uint32_t*)>(0x001eea95)(  // _vsprintf
        message, reinterpret_cast<const char*>(uintptr_t{args[0]}), args + 1);
    if (!client) {
        client = title_global<uint8_t*>(0x00927e8c);
        if (field<int32_t>(client, 0) != 1) return;
    }
    const int32_t sequence = ++field<int32_t>(client, 0x18);
    SV_AddServerCommand(client, sequence & 0x3f, message);
}

void XB_REGS1 XB_CDECL SV_GameSendServerCommand(const char* text, uint32_t handle) {
    const uint32_t args[2] = {0x0082ddd4, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(text))};  // "%s"
    if (!handle) {
        SV_SendServerCommand(nullptr, args);
        return;
    }
    const uint32_t index = handle & 0xfff;
    uint8_t* ent = nullptr;
    if (index < 0x540 && title_global<uint32_t>(0x0097a96c + index * 8) == (handle >> 12))
        ent = title_global<uint8_t*>(0x0097a968 + index * 8);
    if (field<uint32_t>(ent, 0x23c)) SV_SendServerCommand(title_global<uint8_t*>(0x00927e8c), args);
}

}  // namespace game
