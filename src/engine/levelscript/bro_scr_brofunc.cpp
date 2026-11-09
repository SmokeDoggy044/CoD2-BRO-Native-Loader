#include "bro_scr_brofunc.h"

#include "qcommon.h"
#include "title_crt.h"
#include "title_data.h"

namespace game {

namespace {

constexpr uint32_t kBroFuncs = 0x00927170;  // g_broFuncs
constexpr uint32_t kCount = 0x34;

}  // namespace

extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL BroFunc_Find_c(const char* name, uint32_t report) {
    const uint32_t hash = Com_HashKeyNoCase(0, name);
    for (uint32_t i = 0; i < kCount; ++i) {
        if (hash == title_global<uint32_t>(kBroFuncs + i * 8)) return title_global<uint32_t>(kBroFuncs + 4 + i * 8);
    }
    if (static_cast<uint8_t>(report)) {
        char message[0x80];
        title_sprintf(
            message, reinterpret_cast<const char*>(static_cast<uintptr_t>(0x00832320)), name);  // _sprintf, dropped
    }
    return 0;
}

void XB_CDECL BroFunc_Register(const char* name, uint32_t function) {
    uint32_t i = 0;
    while (title_global<uint32_t>(kBroFuncs + i * 8) != 0) {
        if (++i >= kCount) {
            title_function<void XB_CDECL()>(0x00042460)();  // Sys_DebugBreak (tail jump)
            return;
        }
    }
    uint32_t hash = 0;
    if (name) {
        for (const char* c = name; *c; ++c) {
            int8_t ch = static_cast<int8_t>(*c);
            if (ch >= 0x41 && ch <= 0x5a) ch = static_cast<int8_t>(ch + 0x20);
            hash = ((hash >> 27) | (hash << 5)) ^ static_cast<uint32_t>(static_cast<int32_t>(ch));
        }
    }
    title_global<uint32_t>(kBroFuncs + i * 8) = hash;
    title_global<uint32_t>(kBroFuncs + 4 + i * 8) = function;
}

void XB_CDECL BroFunc_Clear() {
    for (uint32_t i = 0; i < kCount * 2; ++i) title_global<uint32_t>(kBroFuncs + i * 4) = 0;
    title_global<uint32_t>(0x0092716c) = 0;
}

}  // namespace game

PLATFORM_ASM_ENTRY(BroFunc_Find_entry,
                   "pushl 4(%esp)\n"
                   "pushl %esi\n" PLATFORM_ASM_CALL(BroFunc_Find_c) "addl $8, %esp\n"
                   "ret")
