#include "sv.h"

#include <cstring>

#include "cm.h"
#include "lanes.h"
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

void XB_CDECL SV_AreaEntities_r(const uint8_t* node, uint8_t* parms) {
    const uint32_t mask = field<uint32_t>(parms, 0x3c);
    const float* mins = reinterpret_cast<const float*>(parms + 0x10);
    const float* maxs = reinterpret_cast<const float*>(parms + 0x20);
    while (mask & *reinterpret_cast<const uint32_t*>(node + 8)) {
        for (uint8_t* ent = *reinterpret_cast<uint8_t* const*>(node + 0x10); ent; ent = field<uint8_t*>(ent, 0xe0)) {
            if (!(field<uint32_t>(parms, 0x3c) & field<uint32_t>(ent, 0x64))) continue;
            const Lanes a = sub(lanes(mins), lanes(reinterpret_cast<const float*>(ent + 0x40)));
            const Lanes b = sub(lanes(reinterpret_cast<const float*>(ent + 0x30)), lanes(maxs));
            int32_t inside = 0;
            for (int32_t i = 0; i < 4; ++i) {
                const float m = a.v[i] > b.v[i] ? a.v[i] : b.v[i];  // MAXPS(a, b)
                if (m < 0.0f && i < 3) inside |= 1 << i;
            }
            if (inside != 7) continue;
            int32_t& count = field<int32_t>(parms, 0x34);
            if (count == field<int32_t>(parms, 0x38)) return;
            field<int32_t*>(parms, 0x30)[count] = field<int32_t>(ent, 0x154);
            ++count;
        }
        const int32_t axis = *reinterpret_cast<const int32_t*>(node);
        const float dist = *reinterpret_cast<const float*>(node + 0xc);
        if (maxs[axis] > dist) SV_AreaEntities_r(*reinterpret_cast<const uint8_t* const*>(node + 0x1c), parms);
        if (!(dist > mins[axis])) return;
        node = *reinterpret_cast<const uint8_t* const*>(node + 0x20);
    }
}

int32_t XB_REGS3 XB_CDECL SV_AreaEntities(const float* maxs, uint32_t, const float* mins, int32_t* list,
                                         int32_t max_count, uint32_t content_mask) {
    alignas(16) uint8_t parms[0x40] = {};
    std::memcpy(parms + 0x10, mins, 16);
    std::memcpy(parms + 0x20, maxs, 16);
    field<int32_t*>(parms, 0x30) = list;
    field<int32_t>(parms, 0x34) = 0;
    field<int32_t>(parms, 0x38) = max_count;
    field<uint32_t>(parms, 0x3c) = content_mask;
    SV_AreaEntities_r(reinterpret_cast<const uint8_t*>(uintptr_t{0x00935664}), parms);
    return field<int32_t>(parms, 0x34);
}

uint32_t XB_CDECL SV_PointContents(const float* point, int32_t pass_entity, uint32_t content_mask) {
    int32_t touch[0x540] = {};
    uint32_t contents = CM_PointContents(point, 0, nullptr);
    const int32_t count = SV_AreaEntities(point, 0, point, touch, 0x540, content_mask);
    for (int32_t i = 0; i < count; ++i) {
        const uint32_t handle = static_cast<uint32_t>(touch[i]);
        if (handle == static_cast<uint32_t>(pass_entity)) continue;
        const uint32_t index = handle & 0xfff;
        uint8_t* ent = nullptr;
        if (index < 0x540 && title_global<uint32_t>(0x0097a96c + index * 8) == (handle >> 12))
            ent = title_global<uint8_t*>(0x0097a968 + index * 8);
        const uint8_t* clip = field<uint8_t*>(ent, 0xdc);
        if (!clip)
            clip = CM_TempBoxModel(reinterpret_cast<const float*>(ent + 0xf0), (field<uint32_t>(ent, 0xd4) & 0x200) ? 1 : 0,
                                   reinterpret_cast<const float*>(ent + 0xe0), field<uint32_t>(ent, 0x134));
        contents |= CM_TransformedPointContents(reinterpret_cast<const float*>(ent + 0x140),
                                                reinterpret_cast<const float*>(ent + 0x150),
                                                reinterpret_cast<const uint32_t*>(clip), point);
    }
    return contents & content_mask;
}

// EBP is saved below the gap: G_RunFrame's argument lands at the entry ESP - 4.
PLATFORM_ASM_ENTRY(SV_RunGameFrame_entry,
                   "movl %esp, %eax\n"
                   "subl $0x20000, %esp\n"
                   "pushl %ebp\n"
                   "movl %eax, %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl 4(%ebp)\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(SV_RunGameFrame) "addl $20, %esp\n"
                   "popl %ebp\n"
                   "addl $0x20000, %esp\n"
                   "ret")

void XB_CDECL SV_RunGameFrame(uint32_t top, int32_t time, uint32_t ebx, uint32_t esi, uint32_t edi) {
    if (!++title_global<int32_t>(0x0093fc28)) title_global<int32_t>(0x0093fc28) = 1;
    title_global<uint32_t>(0x0095f510) = 1;
    const TitleRegs regs = {1, ebx, 0, 0, esi, edi};
    const uint32_t arg = static_cast<uint32_t>(time);
    title_call_at_regs(top, 0x00274660, &regs, 1, &arg);  // G_RunFrame
    title_global<uint32_t>(0x0095f510) = 0;
}

void XB_CDECL SV_ShutdownGameProgs() {
    uint8_t* vm = title_global<uint8_t*>(0x0095f50c);  // gvm
    if (!vm) return;
    title_function<int32_t XB_REGS2 XB_CDECL(uint32_t, uint8_t*, int32_t, int32_t)>(0x002a84e0)(0, vm, 1, 0);  // VM_Call
    Com_Memset(title_global<uint8_t*>(0x0095f50c), 0, 0x8c);
    title_global<uint32_t>(0x0095f514) = 0;  // currentVM
    title_global<uint32_t>(0x0095f50c) = 0;
}

namespace {

// Shared body; `line` is the source line the original hands Z_MallocDebug, `fill` the part mask words.
void dobj_skeleton(uint8_t* ent, uint32_t line, uint32_t fill) {
    uint8_t* obj = field<uint8_t*>(ent, 0x22c);
    if (field<uint32_t>(obj, 0x20)) return;
    const uint32_t size = static_cast<uint32_t>(obj[0x2f]) * 0x40 + 0x30;
    void* memory = title_function<void* XB_CDECL(uint32_t, const char*, const char*, uint32_t)>(0x00042c20)(  // Z_MallocDebug
        size, title_text(0x0083534c), title_text(0x00832e14), line);  // "hunk", "c:\cod\code\game\sv_game.cpp"
    title_function<void XB_CDECL(uint8_t*, void*)>(0x00240c00)(obj, memory);  // DObj_CreateSkeleton
    uint32_t parts[4] = {fill, fill, fill, fill};
    if (uint8_t* current = field<uint8_t*>(ent, 0x22c))
        title_function<void XB_CDECL(uint8_t*)>(0x002df880)(current);  // DObj_UpdateSkeletons (title: stale-stack lanes)
    if (const uint32_t controller = field<uint32_t>(ent, 0x34c))
        title_function<void XB_CDECL(uint8_t*, uint32_t*)>(title_global<uint32_t>(0x008da8b0 + controller * 4))(ent, parts);
}

}  // namespace

PLATFORM_ASM_ENTRY(SV_DObjCreateSkeleton_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(SV_DObjCreateSkeleton) "addl $4, %esp\n"
                   "ret")

void XB_CDECL SV_DObjCreateSkeleton(uint8_t* ent) { dobj_skeleton(ent, 0x2ad, 0); }

PLATFORM_ASM_ENTRY(SV_DObjEnsureSkeleton_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(SV_DObjEnsureSkeleton) "addl $4, %esp\n"
                   "ret")

void XB_CDECL SV_DObjEnsureSkeleton(uint8_t* ent) { dobj_skeleton(ent, 0x2c7, 0xffffffff); }

void XB_CDECL SV_AddOperatorCommands() {
    // {name, handler} in the original's order: map_restart_internal, map_restart, map_restart_check, ...,
    // reboot, load_mp_exe, the three map commands (SV_Map_f), loadgame, loadlastsave, the skill commands ...
    static const uint32_t kCommands[][2] = {
        {0x0082abdc, 0x002cfc20}, {0x0082c874, 0x002cfba0}, {0x0082abf4, 0x002c00b0}, {0x00832f1c, 0x002c0100},
        {0x00832f10, 0x002a8870}, {0x00832f08, 0x002a8840}, {0x0082c894, 0x002cfa40}, {0x0082c51c, 0x002cf920},
        {0x00832f00, 0x002f3f40}, {0x00832efc, 0x002f3f40}, {0x00832fa8, 0x002f3f40}, {0x00832ef0, 0x002f4550},
        {0x00832edc, 0x002f4510}, {0x00832ec8, 0x002c5130}, {0x00832eb8, 0x002c5200}, {0x00832ea4, 0x002c51a0},
        {0x00832e94, 0x002c5140}, {0x00832e88, 0x002a8880}, {0x00832e7c, 0x002af760},
    };
    if (title_global<uint32_t>(0x0098f2cc)) return;
    title_global<uint32_t>(0x0098f2cc) = 1;
    for (const auto& command : kCommands) Cmd_AddServerCommand(title_text(command[0]), command[1]);
}

void XB_CDECL SV_Init() {
    // {name, value, flags, global (0: not kept)}.
    static const uint32_t kCvars[][4] = {
        {0x00832fec, 0x0082e058, 0x24, 0x00929bf0},  {0x00835230, 0x00832c10, 0x44, 0x00929c08},
        {0x00832c00, 0x0082e098, 0, 0x00929ccc},     {0x008340d0, 0x0082e098, 0x48, 0},
        {0x008340dc, 0x0082e098, 0x48, 0x0093e6bc},  {0x008340c8, 0x0082dfeb, 0x48, 0},
        {0x008340bc, 0x0082dfeb, 0x48, 0},           {0x008340a8, 0x0082dfeb, 0x48, 0},
        {0x00834090, 0x0082dfeb, 0x48, 0},           {0x008344c8, 0x0082dfeb, 0x100, 0},
        {0x00832bf0, 0x0082e098, 0, 0x00929b5c},     {0x0082c89c, 0x0082e098, 0x40, 0x0093f800},
        {0x00832bd8, 0x0082e058, 0, 0x00933ee0},
    };
    SV_AddOperatorCommands();
    for (const auto& cvar : kCvars) {
        uint8_t* var = Cvar_Get(title_text(cvar[0]), title_text(cvar[1]), cvar[2]);
        if (cvar[3]) title_global<uint8_t*>(cvar[3]) = var;
    }
}

}  // namespace game
