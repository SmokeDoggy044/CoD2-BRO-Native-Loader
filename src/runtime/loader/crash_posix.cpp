// Linux: faults arrive as signals, handled on an alternate stack.

#include "crash.h"

#include <dlfcn.h>
#include <pthread.h>
#include <signal.h>
#include <sys/ucontext.h>
#include <unistd.h>

#include <cstdint>
#include <cstring>
#include <thread>

#include "log.h"

namespace loader {

namespace {

void handle_signal(int signal, siginfo_t* info, void* context) {
    const auto& gregs = static_cast<ucontext_t*>(context)->uc_mcontext.gregs;
    const auto reg = [&](int index) { return static_cast<uint32_t>(gregs[index]); };
    LOG_ERROR("crash", "%s at %s, fault address %08x", strsignal(signal), describe_address(reg(REG_EIP)).c_str(),
              static_cast<uint32_t>(reinterpret_cast<uintptr_t>(info->si_addr)));
    LOG_ERROR("crash", "eax=%08x ebx=%08x ecx=%08x edx=%08x esi=%08x edi=%08x", reg(REG_EAX), reg(REG_EBX),
              reg(REG_ECX), reg(REG_EDX), reg(REG_ESI), reg(REG_EDI));
    LOG_ERROR("crash", "esp=%08x ebp=%08x eflags=%08x", reg(REG_ESP), reg(REG_EBP), reg(REG_EFL));
    log_backtrace(reg(REG_EIP), reg(REG_EBP));
    run_fatal_exit_hook(true);
    _exit(128 + signal);
}

// Ctrl+C or a kill: blocked in every thread (they inherit the mask) and taken here,
// so logging and the exit hook run outside any signal handler.
void stop_signal_thread(sigset_t signals) {
    int signal = 0;
    sigwait(&signals, &signal);
    LOG_INFO("loader", "stopped by %s", strsignal(signal));
    run_fatal_exit_hook(false);
    _exit(128 + signal);
}

}  // namespace

std::string describe_host_address(uint32_t address) {
    Dl_info info{};
    if (!dladdr(reinterpret_cast<void*>(uintptr_t{address}), &info) || !info.dli_fname) return {};
    const char* file = std::strrchr(info.dli_fname, '/');
    file = file ? file + 1 : info.dli_fname;
    char text[48];
    if (info.dli_sname)
        std::snprintf(text, sizeof(text), "+0x%x", static_cast<unsigned>(address - reinterpret_cast<uintptr_t>(info.dli_saddr)));
    else
        std::snprintf(text, sizeof(text), " +0x%x", static_cast<unsigned>(address - reinterpret_cast<uintptr_t>(info.dli_fbase)));
    return std::string(file) + (info.dli_sname ? std::string(": ") + info.dli_sname + text : text);
}

void install_crash_handler(const XbeImage& image, const SymbolTable& symbols) {
    set_crash_context(image, symbols);

    // Run the handler on its own stack in case the Xbox stack is what broke.
    static char alt_stack[64 * 1024];
    stack_t ss{};
    ss.ss_sp = alt_stack;
    ss.ss_size = sizeof(alt_stack);
    sigaltstack(&ss, nullptr);

    struct sigaction action {};
    action.sa_sigaction = handle_signal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (int signal : {SIGSEGV, SIGILL, SIGFPE, SIGBUS, SIGTRAP}) sigaction(signal, &action, nullptr);
    sigset_t stop;
    sigemptyset(&stop);
    for (int signal : {SIGINT, SIGTERM, SIGHUP}) sigaddset(&stop, signal);
    pthread_sigmask(SIG_BLOCK, &stop, nullptr);
    std::thread(stop_signal_thread, stop).detach();
}

}  // namespace loader
