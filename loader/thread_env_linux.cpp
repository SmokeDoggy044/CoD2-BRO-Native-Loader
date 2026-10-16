// Linux: FS points at the thread's own KPCR, through a per-thread GDT TLS entry.

#include "thread_env.h"

#include <asm/ldt.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>

#include "log.h"

namespace loader {

namespace {

// glibc on i386 uses GS for its own TLS and leaves FS alone. We take one of the
// per-thread GDT TLS slots: every thread uses the same selector, but the kernel
// switches its base on each context switch.
std::atomic<int> g_gdt_entry{-1};

}  // namespace

bool detail::activate_fs(ThreadEnv& env) {
    user_desc desc{};
    desc.entry_number = static_cast<unsigned>(g_gdt_entry.load());
    desc.base_addr = static_cast<unsigned>(reinterpret_cast<uintptr_t>(env.kpcr));
    desc.limit = sizeof(Kpcr) - 1;
    desc.seg_32bit = 1;
    desc.contents = 0;  // data
    desc.useable = 1;
    if (syscall(SYS_set_thread_area, &desc) != 0) {
        LOG_ERROR("thread", "set_thread_area failed: %s", std::strerror(errno));
        return false;
    }
    g_gdt_entry.store(static_cast<int>(desc.entry_number));
    const uint16_t selector = static_cast<uint16_t>((desc.entry_number << 3) | 3);
    asm volatile("movw %0, %%fs" : : "r"(selector));
    return true;
}

void detail::enter_xbox_stack(ThreadEnv&) {}
void detail::leave_xbox_stack(ThreadEnv&) {}

NtTib& xbox_tib() { return current_thread_env()->kpcr->tib; }

uint8_t* current_irql_location() {
    ThreadEnv* env = current_thread_env();
    return env ? &env->kpcr->irql : nullptr;
}

bool prepare_fs_access(const XbeImage&) { return true; }

}  // namespace loader
