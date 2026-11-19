// Windows: hardware faults go through the SEH chain, which links the title's records
// into the thread's own. Faults in title code are fatal, as on Linux, and reported by
// a vectored handler before the title's handlers run (they never expected hardware
// faults on the Xbox). Everything else (our code, host libraries) is reported only
// when nothing handles it: drivers catch some of their own access violations.

#include "program.h"
#include "crash.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "log.h"
#include "xbe.h"

namespace loader {

namespace {

const XbeImage* g_image = nullptr;

const char* exception_name(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: return "access violation";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "illegal instruction";
        case EXCEPTION_PRIV_INSTRUCTION: return "privileged instruction";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "integer divide by zero";
        case EXCEPTION_INT_OVERFLOW: return "integer overflow";
        case EXCEPTION_STACK_OVERFLOW: return "stack overflow";
        case EXCEPTION_IN_PAGE_ERROR: return "in-page error";
        case EXCEPTION_DATATYPE_MISALIGNMENT: return "misaligned access";
        case EXCEPTION_BREAKPOINT: return "breakpoint";
        default: return nullptr;
    }
}

// "module.dll+offset" for an address in a loaded module, empty when in none.
std::string module_offset(uint32_t address) {
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(uintptr_t{address}), &module) ||
        module == GetModuleHandleA(nullptr))  // the launcher stub's image spans the Xbox memory: no code of ours
        return {};
    char path[MAX_PATH] = {};
    GetModuleFileNameA(module, path, MAX_PATH);
    const char* name = path;
    for (const char* p = path; *p; ++p)
        if (*p == '\\' || *p == '/') name = p + 1;
    char text[MAX_PATH + 32];
    std::snprintf(text, sizeof(text), "%s+%lx", name,
                  static_cast<unsigned long>(address - reinterpret_cast<uintptr_t>(module)));
    return text;
}

// A title address as describe_address gives it; anything else as module+offset (empty when in no module).
std::string module_address(uint32_t address) {
    if (g_image && g_image->contains(address)) return describe_address(address);
    return module_offset(address);
}

[[noreturn]] void report(const EXCEPTION_POINTERS& pointers) {
    const EXCEPTION_RECORD& record = *pointers.ExceptionRecord;
    const CONTEXT& c = *pointers.ContextRecord;
    const char* name = exception_name(record.ExceptionCode);
    const uint32_t fault_address =
        record.NumberParameters >= 2 ? static_cast<uint32_t>(record.ExceptionInformation[1]) : 0;
    if (name)
        LOG_ERROR("crash", "%s at %s, fault address %08x", name, describe_address(c.Eip).c_str(), fault_address);
    else
        LOG_ERROR("crash", "exception %08lx at %s", record.ExceptionCode, describe_address(c.Eip).c_str());
    LOG_ERROR("crash", "eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx", c.Eax, c.Ebx, c.Ecx, c.Edx,
              c.Esi, c.Edi);
    LOG_ERROR("crash", "esp=%08lx ebp=%08lx eflags=%08lx", c.Esp, c.Ebp, c.EFlags);
    log_backtrace(c.Eip, c.Ebp);
    // A jump to a bad address leaves no frame chain to follow: the words at the stack pointer (the return address of
    // the call that jumped there is usually the first one) show where it came from.
    if (!g_image || !g_image->contains(c.Eip)) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQuery(reinterpret_cast<const void*>(uintptr_t{c.Esp}), &info, sizeof(info)) &&
            info.State == MEM_COMMIT) {
            const uintptr_t end = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
            for (uint32_t i = 0; i < 24 && c.Esp + i * 4 + 4 <= end; ++i) {
                const uint32_t word = *reinterpret_cast<const uint32_t*>(uintptr_t{c.Esp + i * 4});
                LOG_ERROR("crash", "  [esp+%02x] %08x %s", i * 4, word, module_address(word).c_str());
            }
        }
    }
    run_fatal_exit_hook(true);
    platform::terminate_now(3);
}

LONG CALLBACK title_fault_handler(EXCEPTION_POINTERS* pointers) {
    const EXCEPTION_RECORD& record = *pointers->ExceptionRecord;
    const uint32_t eip = pointers->ContextRecord->Eip;
    // Also a jump to no code at all (a null or tiny pointer called, a smashed return address): on a title stack
    // the SEH chain cannot be walked (the stack is not the thread's own), so the process would end silently.
    const bool bad_jump = record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
                          (eip < 0x10000 || (record.NumberParameters >= 2 && record.ExceptionInformation[0] == 8));
    if (exception_name(record.ExceptionCode) && g_image && (g_image->contains(eip) || bad_jump))
        report(*pointers);
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI unhandled_filter(EXCEPTION_POINTERS* pointers) { report(*pointers); }

// The last handler before Windows' final one while Xbox code runs (thread_env_win32.cpp): what reaches it is
// unhandled. Unwinds pass through.
EXCEPTION_DISPOSITION __cdecl xbox_stack_handler(EXCEPTION_RECORD* record, void*, CONTEXT* context, void*) {
    if (record->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND)) return ExceptionContinueSearch;
    EXCEPTION_POINTERS pointers{record, context};
    report(pointers);
}

// Ctrl+C, Ctrl+Break, closing the console: runs on a thread of its own.
BOOL WINAPI console_handler(DWORD event) {
    const char* name = event == CTRL_C_EVENT       ? "Ctrl+C"
                       : event == CTRL_BREAK_EVENT ? "Ctrl+Break"
                       : event == CTRL_CLOSE_EVENT ? "closing the console"
                                                   : nullptr;
    if (!name) return FALSE;
    LOG_INFO("loader", "stopped by %s", name);
    run_fatal_exit_hook(false);
    platform::terminate_now(130);
}

}  // namespace

void install_crash_handler(const XbeImage& image, const SymbolTable& symbols) {
    set_crash_context(image, symbols);
    g_image = &image;
    AddVectoredExceptionHandler(1, title_fault_handler);
    SetUnhandledExceptionFilter(unhandled_filter);
    SetConsoleCtrlHandler(console_handler, TRUE);
}

std::string describe_host_address(uint32_t address) { return module_offset(address); }

uint32_t xbox_stack_seh_handler() {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&xbox_stack_handler));
}

}  // namespace loader
