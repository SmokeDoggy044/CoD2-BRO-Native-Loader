// Windows: hardware faults go through the SEH chain, which links the title's records
// into the thread's own. Faults in title code are fatal, as on Linux, and reported by
// a vectored handler before the title's handlers run (they never expected hardware
// faults on the Xbox). Everything else (our code, host libraries) is reported only
// when nothing handles it: drivers catch some of their own access violations.

#include "crash.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdlib>

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
    run_fatal_exit_hook();
    std::_Exit(3);
}

LONG CALLBACK title_fault_handler(EXCEPTION_POINTERS* pointers) {
    if (exception_name(pointers->ExceptionRecord->ExceptionCode) && g_image &&
        g_image->contains(pointers->ContextRecord->Eip))
        report(*pointers);
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI unhandled_filter(EXCEPTION_POINTERS* pointers) { report(*pointers); }

// Ctrl+C, Ctrl+Break, closing the console: runs on a thread of its own.
BOOL WINAPI console_handler(DWORD event) {
    const char* name = event == CTRL_C_EVENT       ? "Ctrl+C"
                       : event == CTRL_BREAK_EVENT ? "Ctrl+Break"
                       : event == CTRL_CLOSE_EVENT ? "closing the console"
                                                   : nullptr;
    if (!name) return FALSE;
    LOG_INFO("loader", "stopped by %s", name);
    run_fatal_exit_hook();
    std::_Exit(130);
}

}  // namespace

void install_crash_handler(const XbeImage& image, const SymbolTable& symbols) {
    set_crash_context(image, symbols);
    g_image = &image;
    AddVectoredExceptionHandler(1, title_fault_handler);
    SetUnhandledExceptionFilter(unhandled_filter);
    SetConsoleCtrlHandler(console_handler, TRUE);
}

}  // namespace loader
