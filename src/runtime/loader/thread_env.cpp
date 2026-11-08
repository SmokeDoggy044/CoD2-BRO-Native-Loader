#include "thread_env.h"

#include <algorithm>
#include <cstring>
#include <new>

#include "log.h"
#include "vmem.h"

namespace loader {

namespace {

thread_local ThreadEnv* t_env = nullptr;

constexpr uint32_t kGuardSize = 0x1000;

// Our own code (kernel exports, logging, later the GPU driver) runs on these stacks
// too, so they are never smaller than this, whatever the title asks for.
constexpr uint32_t kMinStackSize = 1024 * 1024;

template <typename T>
uint32_t addr(T* pointer) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer));
}

void* zalloc(size_t size, size_t alignment) {
    void* p = ::operator new(size, std::align_val_t{alignment}, std::nothrow);
    if (p) std::memset(p, 0, size);
    return p;
}

}  // namespace

ThreadEnv* create_thread_env(uint32_t stack_size, uint32_t tls_size) {
    if (tls_size % 4 != 0) {
        LOG_ERROR("thread", "TLS size %u is not a multiple of 4", tls_size);
        return nullptr;
    }
    stack_size = (std::max(stack_size, kMinStackSize) + tls_size + 0xFFF) & ~0xFFFu;
    using namespace platform;
    void* stack = vmem::reserve(0, stack_size + kGuardSize);
    if (!stack || !vmem::commit(static_cast<uint8_t*>(stack) + kGuardSize, stack_size, vmem::Protection::ReadWrite)) {
        LOG_ERROR("thread", "cannot allocate a %u byte stack", stack_size);
        if (stack) vmem::release(stack, stack_size + kGuardSize);
        return nullptr;
    }

    auto* env = new ThreadEnv{};
    env->stack_limit = addr(stack) + kGuardSize;
    env->stack_base = env->stack_limit + stack_size;
    env->tls = tls_size ? reinterpret_cast<uint8_t*>(uintptr_t{env->stack_base - tls_size}) : nullptr;
    env->initial_esp = (env->stack_base - tls_size) & ~0xFu;
    env->kpcr = static_cast<Kpcr*>(zalloc(sizeof(Kpcr), 16));
    env->kthread = static_cast<uint8_t*>(zalloc(kKThreadSize, 16));
    env->seh_chain_end = 0xFFFFFFFF;

    Kpcr& kpcr = *env->kpcr;
    kpcr.tib.exception_list = 0xFFFFFFFF;
    kpcr.tib.stack_base = env->stack_base;
    kpcr.tib.stack_limit = env->stack_limit;
    kpcr.tib.self = addr(&kpcr.tib);
    kpcr.self_pcr = addr(&kpcr);
    kpcr.prcb = addr(&kpcr.prcb_data);
    kpcr.prcb_data[0] = addr(env->kthread);  // CurrentThread

    auto set_field = [&](size_t offset, uint32_t value) { std::memcpy(env->kthread + offset, &value, 4); };
    set_field(kKThreadStackBase, env->stack_base);
    set_field(kKThreadStackLimit, env->stack_limit);
    set_field(kKThreadTlsData, addr(env->tls));
    // KTHREAD starts with a DISPATCHER_HEADER (type 6 = thread), signaled at exit.
    env->kthread[0] = 6;
    env->kthread[2] = static_cast<uint8_t>(kKThreadSize / 4);
    set_field(8, addr(env->kthread + 8));  // empty wait list
    set_field(12, addr(env->kthread + 8));
    return env;
}

bool activate_thread_env(ThreadEnv* env) {
    if (!detail::activate_fs(*env)) return false;
    t_env = env;
    return true;
}

ThreadEnv* current_thread_env() { return t_env; }

namespace {

// Pushes `args` (last first) onto the stack at `stack_top`, or onto the current stack
// if it is 0, and calls `entry`. ESI holds our stack pointer across the call; MSVC
// code preserves it.
void call_with_args(uint32_t entry, uint32_t stack_top, std::span<const uint32_t> args) {
    const uint32_t* arg_data = args.data();
    uint32_t count = static_cast<uint32_t>(args.size());
    asm volatile(
        "movl %%esp, %%esi\n\t"
        "testl %%edi, %%edi\n\t"
        "jz 1f\n\t"
        "movl %%edi, %%esp\n"
        "1:\n\t"
        "testl %%ecx, %%ecx\n\t"
        "jz 3f\n"
        "2:\n\t"
        "pushl -4(%%edx,%%ecx,4)\n\t"
        "decl %%ecx\n\t"
        "jnz 2b\n"
        "3:\n\t"
        "call *%%eax\n\t"
        "movl %%esi, %%esp\n\t"
        : "+a"(entry), "+c"(count), "+d"(arg_data), "+D"(stack_top)
        :
        : "esi", "memory", "cc");
}

}  // namespace

XboxThreadEnd run_on_xbox_stack(uint32_t entry, ThreadEnv& env, std::span<const uint32_t> args) {
    if (env.depth > 0) {
        ++env.depth;
        call_with_args(entry, 0, args);
        --env.depth;
        return XboxThreadEnd::Returned;
    }
    // __builtin_setjmp only saves the frame/stack pointer and a resume address; the
    // compiler treats every register as clobbered when it returns a second time.
    // glibc's longjmp would refuse to jump from the Xbox stack to this one.
    if (__builtin_setjmp(env.exit_jump)) {
        detail::leave_xbox_stack(*t_env);
        t_env->depth = 0;
        return XboxThreadEnd::Exited;
    }
    env.depth = 1;
    detail::enter_xbox_stack(env);
    call_with_args(entry, env.initial_esp, args);
    detail::leave_xbox_stack(env);
    env.depth = 0;
    return XboxThreadEnd::Returned;
}

void exit_xbox_thread() {
    __builtin_longjmp(t_env->exit_jump, 1);
}

}  // namespace loader
