#include "sv.h"

#include <cstring>

#include "cm.h"
#include "lanes.h"
#include "qcommon.h"
#include "title_data.h"
#include "tr_main.h"
#include "g_utils.h"

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

long double XB_REGS3 XB_CDECL RadiusFromBounds(const float* maxs, uint32_t, const float* mins) {
    float corner[3];
    for (int32_t i = 0; i < 3; ++i) {
        const float a = abs_bits(mins[i]);
        const float b = abs_bits(maxs[i]);
        corner[i] = platform_x87_greater(a, b) ? a : b;
    }
    const float sum = (corner[2] * corner[2] + corner[1] * corner[1]) + corner[0] * corner[0];
    long double root;
    __asm__ volatile("call *%2"
                     : "=t"(root)
                     : "0"(static_cast<long double>(sum)), "r"(uintptr_t{0x0033acf4})  // __CIsqrt
                     : "eax", "ecx", "edx", "memory");
    return static_cast<float>(root);
}

namespace {

uint8_t* const kNullSector = reinterpret_cast<uint8_t*>(uintptr_t{0x0093568c});
uint8_t*& sector_free_list() { return title_global<uint8_t*>(0x00935688); }

}  // namespace

PLATFORM_ASM_ENTRY(SV_AllocSector_entry,
                   "pushl %esi\n"
                   "pushl %edx\n" PLATFORM_ASM_CALL(SV_AllocSector) "addl $8, %esp\n"
                   "ret")

uint8_t* XB_CDECL SV_AllocSector(const float* maxs, const float* mins) {
    uint8_t* node = sector_free_list();
    if (!node) return nullptr;
    const float size[2] = {maxs[0] - mins[0], maxs[1] - mins[1]};
    const uint32_t axis = !(size[1] >= size[0]) ? 0 : 1;  // comiss; jb (unordered: 0)
    if (512.0f >= size[axis]) return nullptr;  // unordered goes on
    sector_free_list() = field<uint8_t*>(node, 0x18);
    field<uint32_t>(node, 0) = axis;
    field<float>(node, 0xc) = (mins[axis] + maxs[axis]) * 0.5f;
    field<uint8_t*>(node, 0x1c) = kNullSector;
    field<uint8_t*>(node, 0x20) = kNullSector;
    return node;
}

PLATFORM_ASM_ENTRY(SV_SplitSector_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %edi\n" PLATFORM_ASM_CALL(SV_SplitSector) "addl $12, %esp\n"
                   "ret")

void XB_CDECL SV_SplitSector(uint8_t* node, const float* mins, const float* maxs) {
    const uint32_t axis = field<uint32_t>(node, 0);
    const float dist = field<float>(node, 0xc);
    // Returns the child on `side` (0: +0x1c above dist, 1: +0x20 below), allocated when it is the null sector.
    auto child_for = [&](uint32_t side) -> uint8_t* {
        uint8_t*& slot = field<uint8_t*>(node, 0x1c + side * 4);
        if (slot != kNullSector) return slot;
        uint8_t* child = SV_AllocSector(maxs, mins);
        if (!child) return nullptr;
        slot = child;
        field<uint8_t*>(child, 0x18) = node;
        return child;
    };
    uint8_t* prev = nullptr;
    for (uint8_t* ent = field<uint8_t*>(node, 0x10); ent;) {
        uint32_t side;
        if (field<float>(ent, 0x138 + axis * 4) > dist)
            side = 0;
        else if (dist > field<float>(ent, 0x140 + axis * 4))
            side = 1;
        else {
            prev = ent;
            ent = field<uint8_t*>(ent, 0xe0);
            continue;
        }
        uint8_t* child = child_for(side);
        if (!child) return;
        uint8_t* next = field<uint8_t*>(ent, 0xe0);
        field<uint8_t*>(ent, 0xdc) = child;
        field<uint8_t*>(ent, 0xe0) = field<uint8_t*>(child, 0x10);
        field<uint8_t*>(child, 0x10) = ent;
        field<uint32_t>(child, 8) |= field<uint32_t>(ent, 0x64);
        if (prev)
            field<uint8_t*>(prev, 0xe0) = next;
        else
            field<uint8_t*>(node, 0x10) = next;
        ent = next;
    }
    prev = nullptr;
    for (uint8_t* brush = field<uint8_t*>(node, 0x14); brush;) {
        uint32_t side;
        if (field<float>(brush, 0xa0 + axis * 4) > dist)
            side = 0;
        else if (dist > field<float>(brush, 0xac + axis * 4))
            side = 1;
        else {
            prev = brush;
            brush = field<uint8_t*>(brush, 0xdc);
            continue;
        }
        uint8_t* child = child_for(side);
        if (!child) return;
        uint8_t* next = field<uint8_t*>(brush, 0xdc);
        field<uint8_t*>(brush, 0xdc) = field<uint8_t*>(child, 0x14);
        field<uint8_t*>(child, 0x14) = brush;
        field<uint32_t>(child, 4) |= field<uint32_t>(field<uint8_t*>(brush, 0x68), 0x40);
        if (prev)
            field<uint8_t*>(prev, 0xdc) = next;
        else
            field<uint8_t*>(node, 0x14) = next;
        brush = next;
    }
}

PLATFORM_ASM_ENTRY(SV_LinkToSector_entry,
                   "pushl 8(%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(SV_LinkToSector) "addl $12, %esp\n"
                   "ret")

void XB_CDECL SV_LinkToSector(uint8_t* sv, const float* absmin, const float* absmax) {
    const uint32_t contents = field<uint32_t>(sv, 0x64);
    auto store_bounds = [&] {
        field<uint32_t>(sv, 0x134) = contents;
        field<uint32_t>(sv, 0x138) = reinterpret_cast<const uint32_t*>(absmin)[0];
        field<uint32_t>(sv, 0x13c) = reinterpret_cast<const uint32_t*>(absmin)[1];
        field<uint32_t>(sv, 0x140) = reinterpret_cast<const uint32_t*>(absmax)[0];
        field<uint32_t>(sv, 0x144) = reinterpret_cast<const uint32_t*>(absmax)[1];
    };
    for (;;) {
        const float* world = reinterpret_cast<const float*>(title_global<uint8_t*>(0x0095f6b8) + 0x64);  // cm
        float mins[2] = {world[0], world[1]};
        float maxs[2] = {world[3], world[4]};
        uint8_t* node = reinterpret_cast<uint8_t*>(uintptr_t{0x00935664});
        for (;;) {
            field<uint32_t>(node, 8) |= contents;
            const uint32_t axis = field<uint32_t>(node, 0);
            const float dist = field<float>(node, 0xc);
            uint8_t* child;
            if (absmin[axis] > dist) {
                mins[axis] = dist;
                child = field<uint8_t*>(node, 0x1c);
            } else if (dist > absmax[axis]) {
                maxs[axis] = dist;
                child = field<uint8_t*>(node, 0x20);
            } else {
                if (node == field<uint8_t*>(sv, 0xdc) && !(~contents & field<uint32_t>(sv, 0x134))) {
                    store_bounds();
                    return;
                }
                break;
            }
            if (child == kNullSector) {
                break;
            }
            node = child;
        }
        uint8_t* current = field<uint8_t*>(sv, 0xdc);
        if (current) {
            if (node != current || (~contents & field<uint32_t>(sv, 0x134))) {
                SV_UnlinkEntity(sv);
                continue;
            }
        } else {
            field<uint8_t*>(sv, 0xdc) = node;
            field<uint8_t*>(sv, 0xe0) = field<uint8_t*>(node, 0x10);
            field<uint8_t*>(node, 0x10) = sv;
        }
        store_bounds();
        SV_SplitSector(node, mins, maxs);
        return;
    }
}

namespace {

int32_t clamp_byte(int32_t value) { return value < 1 ? 1 : value > 0xff ? 0xff : value; }

int32_t leaf_index(int32_t leaf) {
    uint8_t* cm = title_global<uint8_t*>(0x0095f6b8);
    return static_cast<uint32_t>(leaf) < field<uint32_t>(cm, 8) ? leaf : 0;
}

uint8_t* leaf_row(int32_t leaf) { return field<uint8_t*>(title_global<uint8_t*>(0x0095f6b8), 0xc) + leaf_index(leaf) * 16; }

}  // namespace

PLATFORM_ASM_ENTRY(SV_LinkEntity_entry,
                   "pushl %edi\n" PLATFORM_ASM_CALL(SV_LinkEntity) "addl $4, %esp\n"
                   "ret")

void XB_CDECL SV_LinkEntity(uint8_t* ent) {
    if (!field<uint32_t>(ent, 0x224)) return;
    uint8_t* const bmodel = field<uint8_t*>(ent, 0xdc);
    if (bmodel)
        field<uint32_t>(ent, 0xac) = 0xffffff;
    else if (field<uint32_t>(ent, 0x134) & 0x200c001) {
        const int32_t i = clamp_byte(static_cast<int32_t>(field<float>(ent, 0xf0)));
        const int32_t j = clamp_byte(static_cast<int32_t>(1.0f - field<float>(ent, 0xe8)));
        const int32_t k = clamp_byte(static_cast<int32_t>(field<float>(ent, 0xf8) + 32.0f));
        field<uint32_t>(ent, 0xac) = static_cast<uint32_t>((k << 16) | (j << 8) | i);
    } else
        field<uint32_t>(ent, 0xac) = 0;

    const float* origin = reinterpret_cast<const float*>(ent + 0x140);
    float* absmin = reinterpret_cast<float*>(ent + 0x100);
    float* absmax = reinterpret_cast<float*>(ent + 0x110);
    alignas(16) float block20[4] = {};  // the original's [esp+0x20] row: stale for the bounds fallback below
    const float* angles = reinterpret_cast<const float*>(ent + 0x150);
    if (bmodel && (angles[0] != 0.0f || angles[1] != 0.0f || angles[2] != 0.0f)) {
        const long double radius = RadiusFromBounds(reinterpret_cast<const float*>(ent + 0xf0), 0,
                                                    reinterpret_cast<const float*>(ent + 0xe0));
        for (int32_t i = 0; i < 3; ++i) {
            absmin[i] = static_cast<float>(static_cast<long double>(origin[i]) - radius);
            absmax[i] = static_cast<float>(radius + static_cast<long double>(origin[i]));
        }
    } else {
        const Lanes o = lanes(origin);
        const Lanes lo = add(o, lanes(reinterpret_cast<const float*>(ent + 0xe0)));
        const Lanes hi = add(o, lanes(reinterpret_cast<const float*>(ent + 0xf0)));
        std::memcpy(absmin, lo.v, 16);
        std::memcpy(absmax, hi.v, 16);
        std::memcpy(block20, origin, 16);
    }
    for (int32_t i = 0; i < 3; ++i) absmin[i] = absmin[i] - 1.0f;
    for (int32_t i = 0; i < 3; ++i) absmax[i] = absmax[i] + 1.0f;

    field<int32_t>(ent, 0x1b4) = 0;
    field<int32_t>(ent, 0x1f8) = 0;
    field<int32_t>(ent, 0x1fc) = -1;
    field<int32_t>(ent, 0x200) = -1;
    int32_t leafs[0x80] = {};
    int32_t last_leaf = 0;
    const int32_t count = CM_BoxLeafnums(absmax, absmin, ent, leafs, 0x80, &last_leaf);
    uint8_t* const sv = ent + 0xd0;
    if (!count) {
        SV_UnlinkEntity(sv);
        return;
    }
    for (int32_t i = 0; i < count; ++i) {
        const int32_t area = *reinterpret_cast<const int16_t*>(leaf_row(leafs[i]) + 8);
        if (area == -1) continue;
        const int32_t area1 = field<int32_t>(ent, 0x1fc);
        if (area1 != -1 && area1 != area)
            field<int32_t>(ent, 0x200) = area;
        else
            field<int32_t>(ent, 0x1fc) = area;
    }
    int32_t i = 0;
    for (; i < count; ++i) {
        const int32_t cluster = field<int32_t>(leaf_row(leafs[i]), 4);
        if (cluster == -1) continue;
        field<int32_t>(ent, 0x1b8 + field<int32_t>(ent, 0x1b4) * 4) = cluster;
        if (++field<int32_t>(ent, 0x1b4) == 0x10) break;
    }
    if (i != count) field<int32_t>(ent, 0x1f8) = field<int32_t>(leaf_row(last_leaf), 4);

    alignas(16) float block10[4];
    std::memcpy(block10, origin, 16);
    field<uint32_t>(ent, 0xd0) = 1;
    const float radius = field<float>(ent, 0x12c);
    int32_t cell;
    bool cached = false;
    if (radius > 0.0f) {
        std::memcpy(block20, ent + 0x120, 16);
        const Lanes d = sub(lanes(block20), lanes(origin));
        const Lanes d2 = mul(d, d);
        const float dist2 = (d2.v[0] + d2.v[1]) + d2.v[2];
        if (radius * radius > dist2) {
            cell = *reinterpret_cast<const int16_t*>(leaf_row(field<int32_t>(ent, 0x130)) + 2);
            cached = true;
        }
    }
    if (!cached) cell = CM_CellForPoint(0, block10);
    field<int32_t>(ent, 0x458) = cell;
    if (cell < 0) {
        const float z = block10[2] + 20.0f;
        alignas(16) const float raised[4] = {block10[0], block10[1], z, z};
        field<int32_t>(ent, 0x458) = CM_CellForPoint(0, raised);
    }
    if (!field<uint32_t>(ent, 0x134)) {
        SV_UnlinkEntity(sv);
        return;
    }
    uint8_t* const obj = field<uint8_t*>(ent, 0x22c);
    const uint32_t flags = field<uint32_t>(ent, 0xd4);
    if (!obj || !(flags & 0x18)) {
        SV_LinkToSector(sv, absmin, absmax);
        return;
    }
    alignas(16) float lo[4];
    alignas(16) float hi[4];
    if (flags & 8) {
        const Lanes a = add(lanes(origin), lanes(&title_global<float>(0x00960110)));
        const Lanes b = add(lanes(origin), lanes(&title_global<float>(0x0095ff70)));
        std::memcpy(lo, a.v, 16);
        std::memcpy(hi, b.v, 16);
    } else {
        const float* bounds = field<const float*>(obj, 0x38);
        const float* bmin = bounds ? bounds : block20;
        const float* bmax = bounds ? bounds + 4 : block20;
        const float min_row[4] = {bmin[0], bmin[1], bmin[2], block10[3]};
        const float max_row[4] = {bmax[0], bmax[1], bmax[2], block20[3]};
        const Lanes a = add(lanes(min_row), lanes(origin));
        const Lanes b = add(lanes(max_row), lanes(origin));
        for (int32_t n = 0; n < 4; ++n) {
            lo[n] = a.v[n] < absmin[n] ? a.v[n] : absmin[n];  // MINPS
            hi[n] = b.v[n] > absmax[n] ? b.v[n] : absmax[n];  // MAXPS
        }
    }
    SV_LinkToSector(sv, lo, hi);
}

PLATFORM_ASM_ENTRY(SV_ClipToEntity_entry,
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl 12(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(SV_ClipToEntity) "addl $16, %esp\n"
                   "ret")

uint32_t XB_CDECL SV_ClipToEntity(uint8_t* ent, const float* mins, const float* maxs, uint32_t capsule_flag) {
    const uint32_t* model = field<uint32_t*>(ent, 0xdc);
    if (!model)
        model = reinterpret_cast<const uint32_t*>(SV_ClipHandleForEntity(0, 0, ent));
    alignas(16) const float zero[4] = {0.0f, 0.0f, 0.0f, 1.0f};  // 0x0082e290
    alignas(16) const float start[4] = {zero[0], zero[1], zero[2], zero[3]};
    alignas(16) const float end[4] = {zero[0], zero[1], zero[2], zero[3]};
    alignas(16) uint8_t results[0x40] = {};
    field<float>(results, 0x20) = 1.0f;
    CM_TransformedBoxTrace(reinterpret_cast<const float*>(ent + 0x140), 0, maxs, results, start, end, mins, model,
                           0xffffffff, reinterpret_cast<const float*>(ent + 0x150), capsule_flag);
    return results[0x3d];
}

PLATFORM_ASM_ENTRY(SegmentMissesBounds_entry,
                   "subl $4, %esp\n"
                   "movss %xmm4, (%esp)\n"
                   "pushl 8(%esp)\n"
                   "pushl %esi\n"
                   "pushl %edi\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(SegmentMissesBounds) "addl $20, %esp\n"
                   "ret")

uint32_t XB_CDECL SegmentMissesBounds(const float* mins, const float* maxs, const float* p, const float* q,
                                      float leave) {
    float enter = 0.0f;
    float sign = -1.0f;
    const float* bound = mins;
    for (;;) {
        for (int32_t i = 0; i < 3; ++i) {
            const float d1 = (p[i] - bound[i]) * sign;
            const float d2 = (q[i] - bound[i]) * sign;
            if (d2 > 0.0f) {
                if (d1 > 0.0f) return 1;
                const float span = d2 - d1;
                const float t = d2 - 0.125f;
                if (!(t > span * enter)) continue;
                enter = t / span;
            } else {
                if (!(d1 > 0.0f)) continue;
                const float span = d2 - d1;
                if (!(d2 > span * leave)) continue;
                leave = d2 / span;
            }
            if (enter >= leave) return 1;
        }
        if (sign == 1.0f) return 0;
        bound = maxs;
        sign = 1.0f;
    }
}

void XB_CDECL SV_TraceStaticModels_r(uint8_t* clip, uint8_t* node, float p1f, float p2f, const float* p1,
                                     const float* p2) {
    if (!(p1f < field<float>(clip, 0x20))) return;  // comiss; jae (unordered goes on)
    if (!(field<uint32_t>(node, 4) & field<uint32_t>(clip, 0x40))) return;
    const uint32_t axis = field<uint32_t>(node, 0);
    const float t1 = p1[axis] - field<float>(node, 0xc);
    const float t2 = p2[axis] - field<float>(node, 0xc);
    uint8_t* const* children = reinterpret_cast<uint8_t* const*>(node + 0x1c);
    if (t1 >= 0.0f && t2 >= 0.0f) {
        SV_TraceStaticModels_r(clip, children[0], p1f, p2f, p1, p2);
    } else if (0.0f >= t1 && 0.0f >= t2) {
        SV_TraceStaticModels_r(clip, children[1], p1f, p2f, p1, p2);
    } else {
        const uint32_t side = 0.0f > t1 ? 1 : 0;
        const float frac = t1 / (t1 - t2);
        const float midf = (p2f - p1f) * frac + p1f;
        const Lanes start = lanes(p1);
        alignas(16) float mid[4];
        const Lanes m = add(start, mul(sub(lanes(p2), start), frac));
        std::memcpy(mid, m.v, 16);
        SV_TraceStaticModels_r(clip, children[side], p1f, midf, p1, mid);
        SV_TraceStaticModels_r(clip, children[side ^ 1], midf, p2f, mid, p2);
    }
    using TraceStaticModel = void XB_REGS3 XB_CDECL(uint8_t*, uint32_t, uint8_t*, uint8_t*, const float*, uint32_t);
    for (uint8_t* model = field<uint8_t*>(node, 0x14); model; model = field<uint8_t*>(model, 0xdc)) {
        if (!(field<uint32_t>(field<uint8_t*>(model, 0x68), 0x40) & field<uint32_t>(clip, 0x40))) continue;
        alignas(16) float maxs[4];
        alignas(16) float mins[4];
        std::memcpy(maxs, model + 0xac, 16);
        std::memcpy(mins, model + 0xa0, 16);
        const float* end = reinterpret_cast<const float*>(clip + 0x60);
        if (SegmentMissesBounds(mins, maxs, end, reinterpret_cast<const float*>(clip + 0x50), field<float>(clip, 0x20)))
            continue;
        title_function<TraceStaticModel>(0x002cc880)(clip + 0x50, 0, model, clip, end, field<uint32_t>(clip, 0x40));
    }
}

PLATFORM_ASM_ENTRY(SV_PointTraceToEntity_entry,
                   "pushl 4(%esp)\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(SV_PointTraceToEntity) "addl $8, %esp\n"
                   "ret")

void XB_CDECL SV_PointTraceToEntity(uint8_t* sv, uint8_t* clip) {
    uint8_t* const ent = sv - 0xd0;
    const uint32_t mask = field<uint32_t>(clip, 0x68);
    if (!(field<uint32_t>(sv, 0x64) & mask)) return;
    const uint32_t pass = field<uint32_t>(clip, 0x60);
    if (pass) {
        if (field<uint32_t>(ent, 0x224) == pass) return;
        const uint32_t owner = field<uint32_t>(ent, 0x1a0);
        if (owner == pass) return;
        if (owner == field<uint32_t>(clip, 0x64) && field<uint32_t>(clip, 0x64)) return;
    }
    const DObj* obj = field<const DObj*>(ent, 0x22c);
    const uint32_t flags = field<uint32_t>(ent, 0xd4);
    alignas(16) uint8_t result[0x40] = {};
    const float* const start = reinterpret_cast<const float*>(clip);
    const float* const end = reinterpret_cast<const float*>(clip + 0x10);
    if (field<uint32_t>(clip, 0x6c) && obj && (flags & 0x18)) {
        alignas(16) float mins[4] = {};
        alignas(16) float maxs[4] = {};
        const float origin[3] = {field<float>(ent, 0x140), field<float>(ent, 0x144), field<float>(ent, 0x148)};
        if (flags & 0x10) {
            if (!DObj_AnyModelHasFlags(obj, mask)) return;
            DObj_GetBounds(0, mins, maxs, obj);
        } else {
            for (int32_t i = 0; i < 3; ++i) mins[i] = title_global<float>(0x00960110 + i * 4) + origin[i];
            for (int32_t i = 0; i < 3; ++i) maxs[i] = title_global<float>(0x0095ff70 + i * 4) + origin[i];
            if (SegmentMissesBounds(mins, maxs, end, start, field<float>(clip, 0x40))) return;
        }
        title_function<int32_t XB_REGS2 XB_CDECL(uint32_t, uint8_t*, int32_t, int32_t)>(0x002a84e0)(  // VM_Call
            0, title_global<uint8_t*>(0x0095f50c), 0x12, field<int32_t>(ent, 0x224));
        float a[9];
        title_function<void XB_REGS1 XB_CDECL(const float*, float*)>(0x002a4610)(  // AnglesToAxisLeft
            reinterpret_cast<const float*>(ent + 0x150), a);
        const float dx = start[0] - origin[0], dy = start[1] - origin[1], dz = start[2] - origin[2];
        alignas(16) float local_start[4] = {};
        local_start[0] = (dz * a[2] + a[1] * dy) + dx * a[0];
        local_start[1] = (a[5] * dz + a[4] * dy) + a[3] * dx;
        local_start[2] = (a[8] * dz + a[7] * dy) + a[6] * dx;
        const float ex = end[0] - origin[0], ey = end[1] - origin[1], ez = end[2] - origin[2];
        alignas(16) float local_end[4] = {};
        local_end[0] = (ez * a[2] + ey * a[1]) + ex * a[0];
        local_end[2] = (ez * a[8] + ey * a[7]) + ex * a[6];
        local_end[1] = (ez * a[5] + ey * a[4]) + ex * a[3];
        float radius = 0.0f;
        const float scale = field<float>(clip, 0x70);
        if (scale > 0.0f) {
            const long double x = local_start[0], y = local_start[1], z = local_start[2];
            const float squared = static_cast<float>((z * z + y * y) + x * x);
            long double root;
            __asm__ volatile("call *%2"
                             : "=t"(root)
                             : "0"(static_cast<long double>(squared)), "r"(uintptr_t{0x0033acf4})  // __CIsqrt
                             : "eax", "ecx", "edx", "memory");
            const float length = static_cast<float>(root);
            radius = static_cast<float>(static_cast<long double>(length) * scale);
        }
        alignas(16) DObjTrace found = {};
        found.fraction = field<float>(clip, 0x40);
        if (flags & 0x10) {
            if (SegmentMissesBounds(mins, maxs, local_end, local_start, field<float>(clip, 0x40))) return;
            title_function<void XB_CDECL(const DObj*, const float*, const float*, uint32_t, DObjTrace*)>(0x0023da30)(
                obj, local_start, local_end, mask, &found);
        } else {
            DObj_TraceBoneBoxes(obj, local_start, local_end, field<const uint8_t*>(clip, 0x74), &found, radius);
        }
        if (found.fraction >= field<float>(clip, 0x40)) {  // comiss; jb (unordered: the hit branch)
            clip[0x5c] |= found.all_solid;
            clip[0x5d] |= found.start_solid;
            return;
        }
        const float nx = found.normal[0], ny = found.normal[1], nz = found.normal[2];
        float* normal = reinterpret_cast<float*>(result + 0x10);
        normal[0] = ((nx * a[0]) + (nz * a[6])) + ny * a[3];
        normal[2] = ((nz * a[8]) + (ny * a[5])) + a[2] * nx;
        normal[1] = ((nz * a[7]) + (ny * a[4])) + a[1] * nx;
        const Lanes from = lanes(start);
        const Lanes pos = add(from, mul(sub(lanes(end), from), found.fraction));
        std::memcpy(result, pos.v, 16);
        field<float>(result, 0x20) = found.fraction;
        field<uint32_t>(result, 0x24) = found.unknown04;
        field<uint32_t>(result, 0x34) = found.bone_name;
        field<uint32_t>(result, 0x38) = found.hit_class;
        result[0x3d] = found.start_solid;
    } else {
        const uint32_t* model = reinterpret_cast<const uint32_t*>(SV_ClipHandleForEntity(0, 0, ent));
        alignas(16) float angles[4];
        std::memcpy(angles, field<uint8_t*>(ent, 0xdc) ? ent + 0x150 : reinterpret_cast<const uint8_t*>(uintptr_t{0x0082e290}),
                    16);
        alignas(16) float mins[4];
        alignas(16) float maxs[4];
        std::memcpy(mins, reinterpret_cast<const void*>(uintptr_t{0x0082e290}), 16);
        std::memcpy(maxs, reinterpret_cast<const void*>(uintptr_t{0x0082e290}), 16);
        field<uint32_t>(result, 0x20) = field<uint32_t>(clip, 0x40);
        CM_TransformedBoxTrace(reinterpret_cast<const float*>(ent + 0x140), 0, maxs, result, start, end, mins, model,
                               mask, angles, 0);
        if (field<float>(result, 0x20) >= field<float>(clip, 0x40)) {  // comiss; jb (unordered: the hit branch)
            clip[0x5c] |= result[0x3c];
            clip[0x5d] |= result[0x3d];
            return;
        }
    }
    result[0x3d] |= clip[0x5d];
    result[0x3c] = clip[0x5c];
    field<uint32_t>(result, 0x28) = field<uint32_t>(ent, 0x134);
    field<uint32_t>(result, 0x30) = field<uint32_t>(ent, 0x224);
    field<uint32_t>(result, 0x2c) = 0;
    std::memcpy(clip + 0x20, result, 0x40);
}

PLATFORM_ASM_ENTRY(SV_ClipMoveToEntity_entry,
                   "pushl %ecx\n"
                   "pushl %eax\n" PLATFORM_ASM_CALL(SV_ClipMoveToEntity) "addl $8, %esp\n"
                   "ret")

void XB_CDECL SV_ClipMoveToEntity(uint8_t* clip, uint8_t* sv) {
    uint8_t* const ent = sv - 0xd0;
    if (!(field<uint32_t>(sv, 0x64) & field<uint32_t>(clip, 0x98))) return;
    const uint32_t pass = field<uint32_t>(clip, 0x90);
    if (pass) {
        if (field<uint32_t>(ent, 0x224) == pass) return;
        const uint32_t owner = field<uint32_t>(ent, 0x1a0);
        if (owner == pass) return;
        if (owner == field<uint32_t>(clip, 0x94) && field<uint32_t>(clip, 0x94)) return;
    }
    const uint32_t* model = reinterpret_cast<const uint32_t*>(SV_ClipHandleForEntity(0, 0, ent));
    alignas(16) float angles[4];
    std::memcpy(angles, ent + 0x150, 16);
    if (field<uint32_t>(ent, 0x134) & 0x800000) {
        const float limit = title_global<float>(0x008fef08);
        if (platform_x87_greater(limit, abs_bits(angles[2]))) angles[2] = 0.0f;
        if (platform_x87_greater(limit, abs_bits(angles[0]))) angles[0] = 0.0f;
    }
    if (!field<uint8_t*>(ent, 0xdc)) std::memcpy(angles, reinterpret_cast<const void*>(uintptr_t{0x0082e290}), 16);
    alignas(16) uint8_t trace[0x40] = {};
    field<uint32_t>(trace, 0x20) = field<uint32_t>(clip, 0x70);  // fraction bits
    CM_TransformedBoxTrace(reinterpret_cast<const float*>(ent + 0x140), 0, reinterpret_cast<const float*>(clip + 0x10),
                           trace, reinterpret_cast<const float*>(clip + 0x30),
                           reinterpret_cast<const float*>(clip + 0x40), reinterpret_cast<const float*>(clip), model,
                           field<uint32_t>(clip, 0x98), angles, field<uint32_t>(clip, 0x9c));
    if (field<float>(trace, 0x20) >= field<float>(clip, 0x70)) {  // comiss; jb (unordered: the hit branch)
        clip[0x8c] |= trace[0x3c];
        clip[0x8d] |= trace[0x3d];
        return;
    }
    trace[0x3c] |= clip[0x8c];
    trace[0x3d] |= clip[0x8d];
    field<uint32_t>(trace, 0x30) = field<uint32_t>(ent, 0x224);
    std::memcpy(clip + 0x50, trace, 0x40);
}

void XB_CDECL SV_ClipMoveToEntities_r(uint8_t* clip, uint8_t* node, float p1f, float p2f, const float* p1,
                                      const float* p2) {
    if (!(p1f < field<float>(clip, 0x70))) return;  // comiss; jae (unordered goes on)
    if (!(field<uint32_t>(node, 8) & field<uint32_t>(clip, 0x98))) return;
    const uint32_t axis = field<uint32_t>(node, 0);
    const float dist = field<float>(node, 0xc);
    const float t1 = p1[axis] - dist;
    const float offset = field<float>(clip, 0x20 + axis * 4);
    const float t2 = p2[axis] - dist;
    uint8_t* const* children = reinterpret_cast<uint8_t* const*>(node + 0x1c);
    if (t1 >= offset && t2 >= offset) {
        SV_ClipMoveToEntities_r(clip, children[0], p1f, p2f, p1, p2);
    } else if (0.0f - offset >= t1 && 0.0f - offset >= t2) {
        SV_ClipMoveToEntities_r(clip, children[1], p1f, p2f, p1, p2);
    } else {
        uint32_t side;
        float frac;
        float frac2;
        bool clamp = true;
        if (t2 > t1) {
            const float idist = 1.0f / (t1 - t2);
            frac2 = (offset + t1) * idist;
            side = 1;
            frac = (t1 - offset) * idist;
        } else if (t1 > t2) {
            const float idist = 1.0f / (t1 - t2);
            side = 0;
            frac2 = (t1 - offset) * idist;
            frac = (offset + t1) * idist;
        } else {
            side = 0;
            frac = 1.0f;
            frac2 = 0.0f;
            clamp = false;
        }
        if (clamp && frac > 1.0f) frac = 1.0f;
        const Lanes start = lanes(p1);
        alignas(16) float mid[4];
        Lanes m = add(start, mul(sub(lanes(p2), start), frac));
        std::memcpy(mid, m.v, 16);
        const float span = p2f - p1f;
        SV_ClipMoveToEntities_r(clip, children[side], p1f, span * frac + p1f, p1, mid);
        if (0.0f > frac2) frac2 = 0.0f;
        m = add(start, mul(sub(lanes(p2), start), frac2));
        std::memcpy(mid, m.v, 16);
        SV_ClipMoveToEntities_r(clip, children[side ^ 1], span * frac2 + p1f, p2f, mid, p2);
    }
    for (uint8_t* sv = field<uint8_t*>(node, 0x10); sv; sv = field<uint8_t*>(sv, 0xe0))
        SV_ClipMoveToEntity(clip, sv);
}

void XB_CDECL SV_PointTraceToEntities_r(uint8_t* clip, uint8_t* node, float p1f, float p2f, const float* p1,
                                        const float* p2) {
    if (!(p1f < field<float>(clip, 0x40))) return;  // comiss; jae (unordered goes on)
    if (!(field<uint32_t>(node, 8) & field<uint32_t>(clip, 0x68))) return;
    const uint32_t axis = field<uint32_t>(node, 0);
    const float t1 = p1[axis] - field<float>(node, 0xc);
    const float t2 = p2[axis] - field<float>(node, 0xc);
    uint8_t* const* children = reinterpret_cast<uint8_t* const*>(node + 0x1c);
    if (t1 >= 0.0f && t2 >= 0.0f) {
        SV_PointTraceToEntities_r(clip, children[0], p1f, p2f, p1, p2);
    } else if (0.0f >= t1 && 0.0f >= t2) {
        SV_PointTraceToEntities_r(clip, children[1], p1f, p2f, p1, p2);
    } else {
        const uint32_t side = 0.0f > t1 ? 1 : 0;
        const float frac = t1 / (t1 - t2);
        const float midf = (p2f - p1f) * frac + p1f;
        const Lanes start = lanes(p1);
        alignas(16) float mid[4];
        const Lanes m = add(start, mul(sub(lanes(p2), start), frac));
        std::memcpy(mid, m.v, 16);
        SV_PointTraceToEntities_r(clip, children[side], p1f, midf, p1, mid);
        SV_PointTraceToEntities_r(clip, children[side ^ 1], midf, p2f, mid, p2);
    }
    for (uint8_t* sv = field<uint8_t*>(node, 0x10); sv; sv = field<uint8_t*>(sv, 0xe0)) SV_PointTraceToEntity(sv, clip);
}

void XB_REGS1 XB_CDECL SV_TraceStaticModels(const float* start, uint8_t* results, const float* end,
                                            uint32_t content_mask) {
    struct alignas(16) {
        uint8_t trace[0x40];
        uint32_t mask;
        uint32_t pad[3];
        float start[4];
        float end[4];
    } clip = {};
    field<uint32_t>(clip.trace, 0x20) = field<uint32_t>(results, 0x20);
    clip.mask = content_mask;
    for (int32_t i = 0; i < 3; ++i) clip.start[i] = start[i];
    for (int32_t i = 0; i < 3; ++i) clip.end[i] = end[i];
    SV_TraceStaticModels_r(reinterpret_cast<uint8_t*>(&clip), reinterpret_cast<uint8_t*>(uintptr_t{0x00935664}), 0.0f,
                           field<float>(clip.trace, 0x20), clip.start, clip.end);
    const float frac = field<float>(clip.trace, 0x20);
    if (!(field<float>(results, 0x20) > frac)) return;
    float* endpos = reinterpret_cast<float*>(clip.trace);
    for (int32_t i = 0; i < 3; ++i) endpos[i] = (end[i] - start[i]) * frac + start[i];
    std::memcpy(results, clip.trace, 0x40);
}

PLATFORM_ASM_ENTRY(SV_Trace_entry,
                   "pushl 44(%esp)\n"
                   "pushl 44(%esp)\n"
                   "pushl 44(%esp)\n"
                   "pushl 44(%esp)\n"
                   "pushl 44(%esp)\n"
                   "pushl 44(%esp)\n"
                   "pushl 44(%esp)\n"
                   "pushl 44(%esp)\n"
                   "pushl 44(%esp)\n"
                   "pushl 44(%esp)\n"
                   "pushl 44(%esp)\n"
                   "pushl %ebx\n" PLATFORM_ASM_CALL(SV_Trace) "addl $48, %esp\n"
                   "ret")

namespace {

uint32_t pass_owner(uint32_t handle) {
    const uint32_t index = handle & 0xfff;
    if (index >= 0x540 || title_global<uint32_t>(0x0097a96c + index * 8) != (handle >> 12) ||
        !title_global<uint8_t*>(0x0097a968 + index * 8))
        return 0;
    return field<uint32_t>(G_EntHandleToEnt(&handle), 0x1a0);
}

}  // namespace

void XB_CDECL SV_Trace(const float* start, uint8_t* results, const float* mins, const float* maxs, const float* end,
                       uint32_t pass, uint32_t content_mask, uint32_t capsule_flag, uint32_t arg7, uint32_t arg8,
                       uint32_t static_models, uint32_t arg10) {
    alignas(16) uint8_t trace[0x40] = {};
    CM_BoxTrace(nullptr, 0, maxs, trace, start, end, mins, nullptr, content_mask, capsule_flag);
    const float fraction = field<float>(trace, 0x20);
    field<uint32_t>(trace, 0x30) =
        fraction != 1.0f ? field<uint32_t>(field<uint8_t*>(title_global<uint8_t*>(0x0095f534), 8), 0x224) : 0;
    if (fraction == 0.0f) {
        std::memcpy(results, trace, 0x40);
        return;
    }
    if (static_models) {
        title_function<void XB_REGS1 XB_CDECL(const float*, uint8_t*, const float*, uint32_t)>(0x002db3e0)(
            start, trace, end, content_mask);
        if (field<float>(trace, 0x20) == 0.0f) {
            std::memcpy(results, trace, 0x40);
            return;
        }
    }
    const float size = ((((maxs[0] - mins[0]) + maxs[1]) - mins[1]) + maxs[2]) - mins[2];
    if (size == 0.0f) {
        struct alignas(16) {
            float start[4];
            float end[4];
            uint8_t trace[0x40];
            uint32_t pass, owner, mask, arg7;
            uint32_t arg10, arg8;
            uint32_t pad[2];
        } clip = {};
        std::memcpy(clip.trace, trace, 0x40);
        clip.owner = 0;
        clip.mask = content_mask;
        std::memcpy(clip.start, start, 16);
        std::memcpy(clip.end, end, 16);
        clip.arg8 = arg8;
        clip.pass = pass;
        clip.arg7 = arg7;
        clip.arg10 = arg10;
        clip.owner = pass_owner(pass);
        title_function<void XB_CDECL(void*, uint32_t, uint32_t, uint32_t, const float*, const float*)>(0x002e84d0)(
            &clip, 0x00935664, 0, field<uint32_t>(clip.trace, 0x20), clip.start, clip.end);
        std::memcpy(results, clip.trace, 0x40);
        return;
    }
    struct alignas(16) {
        float mins[4];
        float maxs[4];
        float outer[4];
        float start[4];
        float end[4];
        uint8_t trace[0x40];
        uint32_t pass, owner, mask, capsule;
    } clip = {};
    std::memcpy(clip.trace, trace, 0x40);
    clip.owner = 0;
    clip.mask = content_mask;
    clip.pass = pass;
    clip.capsule = capsule_flag;
    clip.owner = pass_owner(pass);
    const Lanes half = {{0.5f, 0.5f, 0.5f, 0.5f}};
    const Lanes extents = mul(sub(lanes(maxs), lanes(mins)), half);
    std::memcpy(clip.maxs, extents.v, 16);
    const Lanes sign = lanes(reinterpret_cast<const float*>(uintptr_t{0x0082e0b0}));
    for (int32_t i = 0; i < 4; ++i) {
        uint32_t a, b;
        std::memcpy(&a, &extents.v[i], 4);
        std::memcpy(&b, &sign.v[i], 4);
        a ^= b;
        std::memcpy(&clip.mins[i], &a, 4);
    }
    const Lanes outer = add(extents, lanes(reinterpret_cast<const float*>(uintptr_t{0x0082e640})));
    std::memcpy(clip.outer, outer.v, 16);
    const Lanes center = mul(add(lanes(maxs), lanes(mins)), half);
    const Lanes s = add(lanes(start), center);
    const Lanes e = add(lanes(end), center);
    std::memcpy(clip.start, s.v, 16);
    std::memcpy(clip.end, e.v, 16);
    SV_ClipMoveToEntities_r(reinterpret_cast<uint8_t*>(&clip), reinterpret_cast<uint8_t*>(uintptr_t{0x00935664}),
                            0.0f, field<float>(clip.trace, 0x20), clip.start, clip.end);
    const float hit = field<float>(clip.trace, 0x20);
    if (field<float>(trace, 0x20) > hit) {  // the fraction after the static models
        const Lanes from = lanes(start);
        const Lanes pos = add(from, mul(sub(lanes(end), from), hit));
        std::memcpy(clip.trace, pos.v, 16);
    }
    std::memcpy(results, clip.trace, 0x40);
}

void XB_CDECL SV_SetConfigstring(int32_t index, const char* value) {
    if (index < 0 || index >= 0x400)
        title_function<void XB_CDECL(uint32_t, const char*, int32_t)>(kBreak)(1, title_text(0x00832db0), index);
    if (!value) value = title_text(0x0082dfeb);  // ""
    uint8_t* slot = reinterpret_cast<uint8_t*>(uintptr_t{0x00964ce0 + static_cast<uint32_t>(index) * 4});
    const uint8_t* current = field<uint8_t*>(slot, 0);
    if (!std::strcmp(value, reinterpret_cast<const char*>(current ? current + 6 : nullptr))) return;
    title_function<void XB_THISCALL(uint8_t*, const char*)>(0x0011dd20)(slot, value);  // ScriptString_AssignCString
    if (title_global<int32_t>(0x00964cd4) != 2) return;
    uint8_t* svs = title_global<uint8_t*>(0x00927e8c);
    if (field<int32_t>(svs, 0) != 1) return;
    const int32_t length = static_cast<int32_t>(std::strlen(value));
    const uint32_t text = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value));
    if (length < 0xe8) {
        const uint32_t args[3] = {0x00832d80, static_cast<uint32_t>(index), text};  // "cs %i %s"
        SV_SendServerCommand(svs, args);
        return;
    }
    char buffer[0x100];
    int32_t left = length;
    for (int32_t offset = 0; left > 0; offset += 0xe7, left -= 0xe7) {
        uint32_t command = 0x00832da8;  // "bcs0"
        if (offset) command = left >= 0xe8 ? 0x00832d98 : 0x00832da0;  // "bcs1" : "bcs2"
        std::strncpy(buffer, value + offset, 0xe7);
        buffer[0xe7] = 0;
        const uint32_t args[4] = {0x00832d8c, command, static_cast<uint32_t>(index),
                                  static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffer))};  // "%s %i %s"
        SV_SendServerCommand(svs, args);
    }
}

void XB_REGS1 XB_CDECL SV_SetBrushModel(uint8_t* ent) {
    uint32_t map = field<uint32_t>(ent, 0x220);
    if (map == 0xffffffff) map = field<uint32_t>(title_global<uint8_t*>(0x0095f638), 0x34);
    const uint32_t index = field<uint16_t>(ent, 6);
    uint32_t model = 0;
    if (index) {
        if (index == 0x1ff || index == 0x1fe) {
            model = title_global<uint32_t>(0x0095f504);
        } else {
            // 0x002afb10: the inline model `index` of clip map `map` (EBX = the table at 0x0095f500).
            uint32_t c, d;
            __asm__ volatile("pushl %%ebx\n\tmovl %%edx, %%ebx\n\tcall *%%esi\n\tpopl %%ebx"
                             : "=a"(model), "=c"(c), "=d"(d)
                             : "a"(index), "c"(map), "d"(title_global<uint32_t>(0x0095f500)), "S"(uintptr_t{0x002afb10})
                             : "memory", "cc");
        }
    }
    title_function<void XB_REGS3 XB_CDECL(float*, uint32_t, float*)>(0x002b2790)(  // the model's bounds
        reinterpret_cast<float*>(ent + 0xf0), model, reinterpret_cast<float*>(ent + 0xe0));
    field<uint32_t>(ent, 0xdc) = model;
    field<uint32_t>(ent, 0x134) = 0xffffffff;
    SV_LinkEntity(ent);
}

// The title callees run at the original's ESP (entry - 0x40): 0x002c4fe0 leaves a stack address in .data.
PLATFORM_ASM_ENTRY(SV_PacketEvent_entry,
                   "movl %esp, %ecx\n"
                   "subl $0x20000, %esp\n"
                   "pushl %ebp\n"
                   "movl %ecx, %ebp\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl 20(%ebp)\n"
                   "pushl 16(%ebp)\n"
                   "pushl 12(%ebp)\n"
                   "pushl 8(%ebp)\n"
                   "pushl 4(%ebp)\n"
                   "pushl %eax\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(SV_PacketEvent) "addl $40, %esp\n"
                   "popl %ebp\n"
                   "addl $0x20000, %esp\n"
                   "ret")

void XB_CDECL SV_PacketEvent(uint32_t entry_esp, uint8_t* msg, uint32_t type, uint32_t ip, uint32_t ipx1,
                             uint32_t ipx2, uint32_t port_word, uint32_t ebx, uint32_t esi, uint32_t edi) {
    const uint32_t top = entry_esp - 0x40;
    const uint32_t msg_word = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(msg));
    const uint32_t from[6] = {type, ip, ipx1, ipx2, port_word, 0x008341e4};  // "disconnect"
    if (field<int32_t>(msg, 0xc) >= 4 && *field<int32_t*>(msg, 4) == -1) {
        const TitleRegs regs = {msg_word, ebx, 0, 0, esi, edi};
        title_call_at_regs(top, 0x002b8200, &regs, 5, from);  // SV_ConnectionlessPacket
        return;
    }
    if (!++title_global<int32_t>(0x0093fc28)) title_global<int32_t>(0x0093fc28) = 1;
    title_global<uint32_t>(0x0095f510) = 1;
    field<int32_t>(msg, 0x10) = 0;
    MSG_ReadLong(msg);
    int32_t count = field<int32_t>(msg, 0x10);
    const uint8_t* data = field<uint8_t*>(msg, 4);
    const uint8_t low = data[count];
    field<int32_t>(msg, 0x10) = ++count;
    const uint8_t high = data[count];
    field<int32_t>(msg, 0x10) = ++count;
    uint32_t qport = static_cast<uint32_t>(low) | static_cast<uint32_t>(high) << 8;
    if (count > field<int32_t>(msg, 0xc)) qport = 0xffff;
    uint8_t* client = title_global<uint8_t*>(0x00927e8c);
    for (int32_t i = 0; i < 1; ++i, client += 0xec0) {
        if (!field<uint32_t>(client, 0)) continue;
        const uint32_t* remote = reinterpret_cast<const uint32_t*>(client + 0x698);
        bool same;
        if (type != remote[0])
            same = false;
        else if (type == 2)
            same = true;
        else if (type == 4)
            same = !std::memcmp(&from[1], &remote[1], 4);
        else if (type == 5)
            same = !std::memcmp(&from[2], &remote[2], 10);
        else
            same = true;
        if (!same) continue;
        if (field<uint32_t>(client, 0x6ac) != qport) continue;
        const uint16_t port = static_cast<uint16_t>(port_word >> 16);
        if (field<uint16_t>(client, 0x6aa) != port) field<uint16_t>(client, 0x6aa) = port;
        const uint32_t client_word = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(client));
        const TitleRegs chan_regs = {client_word + 0x690, msg_word, 0, 0, ipx2, edi};
        if (title_call_at_regs(top, 0x002b13b0, &chan_regs, 0, nullptr)) {  // Netchan_Process
            const TitleRegs exec_regs = {0, msg_word, 0, 0, ipx2, edi};
            title_call_at_regs(top, 0x002c4fe0, &exec_regs, 1, &client_word);
        }
        title_global<uint32_t>(0x0095f510) = 0;
        return;
    }
    const TitleRegs regs = {1, ebx, 0, port_word, esi, edi};
    title_call_at_regs(top, 0x002ba530, &regs, 6, from);  // NET_OutOfBandPrint
    title_global<uint32_t>(0x0095f510) = 0;
}

// The title callees run at the original's ESP (entry - 4, below its saved EDI): the C frame sits below a gap.
PLATFORM_ASM_ENTRY(SV_Frame_entry,
                   "movl %esp, %eax\n"
                   "subl $0x20000, %esp\n"
                   "pushl %ebp\n"
                   "movl %eax, %ebp\n"
                   "pushl %edx\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl %ebp\n" PLATFORM_ASM_CALL(SV_Frame) "addl $16, %esp\n"
                   "popl %ebp\n"
                   "addl $0x20000, %esp\n"
                   "ret")

void XB_CDECL SV_Frame(uint32_t entry_esp, uint32_t ebx, uint32_t msec, uint32_t edx) {
    const uint32_t top = entry_esp - 4;
    uint8_t* const killserver = title_global<uint8_t*>(0x00929b5c);
    if (field<uint32_t>(killserver, 0x20)) {
        const TitleRegs regs = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(killserver)), ebx,
                                field<uint32_t>(killserver, 0x20), edx, msec, 0};
        title_call_at_regs(top, 0x002a6e00, &regs, 0, nullptr);  // SV_Shutdown
        Cvar_Set2(title_text(0x00832bf0), const_cast<char*>(title_text(0x0082e098)), 1);  // "sv_killserver", "0"
        return;
    }
    if (field<uint32_t>(title_global<uint8_t*>(0x0093fcf0), 0x20)) {  // com_sv_running
        if (!field<uint32_t>(title_global<uint8_t*>(0x0093fcd8), 0x20) && title_global<uint32_t>(0x0092bdf0) != 2)
            Cvar_Set2(title_text(0x0082ca00), const_cast<char*>(title_text(0x0082e058)), 1);  // "cl_paused", "1"
        if (!field<uint32_t>(title_global<uint8_t*>(0x0093fcd8), 0x20) || title_global<uint32_t>(0x0092f548)) {
            const uint32_t paused = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(title_global<uint8_t*>(0x0093fcd8)));
            const TitleRegs regs = {paused, ebx, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(
                                                     title_global<uint8_t*>(0x0093fcf0))),
                                    paused, msec, 0};
            if (title_call_at_regs(top, 0x002f3de0, &regs, 0, nullptr)) {  // CheckReloadStatus
                title_global<uint32_t>(0x0092be90) = 0;
                title_global<uint32_t>(0x0092be94) = 0;
                return;
            }
            if (!title_global<uint32_t>(0x0092f548)) {
                const TitleRegs info_regs = {0, ebx, 0, 0, msec, 0};
                title_call_at_regs(top, 0x002c4e50, &info_regs, 1, &msec);  // SV_UpdateInfoConfigstrings
                title_global<uint32_t>(0x0092f548) = 1;
            }
            const uint32_t command = 0x13;
            const TitleRegs vm_regs = {0, ebx, 0, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(
                                                      title_global<uint8_t*>(0x0095f50c))),
                                       msec, 0};
            const int32_t limit = static_cast<int32_t>(title_call_at_regs(top, 0x002a84e0, &vm_regs, 1, &command));
            title_global<int32_t>(0x0092f094) = limit;
            if (limit > 0x70000000 ||
                title_global<int32_t>(0x00927e94) >= 0x7ffffffe - title_global<int32_t>(0x00927e90)) {
                const TitleRegs regs2 = {0, ebx, 0, 0, msec, 0};
                title_call_at_regs(top, 0x002a6e00, &regs2, 0, nullptr);  // SV_Shutdown
                Cbuf_AddText(title_text(0x00832b88));  // "vstr nextmap\n"
                return;
            }
            const TitleRegs run_regs = {0, ebx, 0, 0, msec, 0};
            title_call_at_regs(top, 0x002f2e40, &run_regs, 1, &msec);  // SV_RunGameFrame
            title_call_at_regs(top, 0x002e6d80, &run_regs, 0, nullptr);
        }
    }
    title_global<uint32_t>(0x0092be94) = 0;
    title_global<uint32_t>(0x0092be90) = msec;
}

void XB_CDECL SV_UpdateInfoConfigstrings(int32_t time) {
    title_function<void XB_CDECL(uint32_t)>(0x002ab560)(1);
    uint32_t& modified = title_global<uint32_t>(0x009403ec);  // cvar_modifiedFlags
    if (modified & 4) {
        SV_SetConfigstring(0, Cvar_InfoString(4));
        modified &= ~4u;
    }
    if (modified & 8) {
        SV_SetConfigstring(1, Cvar_InfoString_Big(8));
        modified &= ~8u;
    }
    title_function<int32_t XB_REGS2 XB_CDECL(uint32_t, uint8_t*, int32_t, int32_t)>(0x002a84e0)(  // VM_Call
        0, title_global<uint8_t*>(0x0095f50c), 0xa, time);
}

}  // namespace game
