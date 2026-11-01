#include "g_main.h"

#include <cstring>

#include "qcommon_core.h"
#include "title_data.h"

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

}  // namespace game
