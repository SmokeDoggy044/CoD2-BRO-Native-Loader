#include "xlat_runtime.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "scr_fiber.h"

// What xl_enter_common pushes on the host stack: the guest ESP and the function's index (from the FS words), the
// XMM registers, the PUSHA block.
struct XlFrame {
    uint32_t guest_esp;
    uint32_t index;
    uint32_t xmm[8][4];
    uint32_t edi, esi, ebp, esp_unused, ebx, edx, ecx, eax;
};
static_assert(sizeof(XlFrame) == 168);

namespace {

std::vector<xl::Entry>& table() {  // sorted by address
    static std::vector<xl::Entry> entries;
    return entries;
}
std::vector<std::vector<xl::Fn>>& by_index() {  // per level, in the order its generated code numbered its entries
    static std::vector<std::vector<xl::Fn>> levels;
    return levels;
}
std::vector<game::Function>& rows() {
    static std::vector<game::Function> all;
    return all;
}

// Developers: COD_BRO_XLAT_TRACE=1 prints each translated function's first call (which ones a run reaches).
void trace_first_call(xl::Fn fn) {
    static const bool on = std::getenv("COD_BRO_XLAT_TRACE") != nullptr;
    if (!on) return;
    static xl::Fn seen[8192];
    static size_t count;
    for (size_t i = 0; i < count; ++i)
        if (seen[i] == fn) return;
    if (count < sizeof seen / sizeof *seen) seen[count++] = fn;
    uint32_t address = 0;
    for (const xl::Entry& e : table())
        if (e.fn == fn) address = e.address;
    std::fprintf(stderr, "translated level code: first call of %08x (%zu so far)\n", address, count);
}

}  // namespace

namespace xl {

void xl_divide_error() {
    std::fprintf(stderr, "translated level code: divide error\n");
    std::abort();
}

void xl_fret_missing() {
    std::fprintf(stderr, "translated level code: a call was expected to return a float (x87 st(0)) and did not\n");
    std::abort();
}
void xl_trap() {
    std::fprintf(stderr, "translated level code: int3 reached\n");
    std::abort();
}

void register_table(const Entry* entries, size_t count, uint32_t level) {
    if (by_index().size() <= level) by_index().resize(level + 1);
    for (size_t i = 0; i < count; ++i) by_index()[level].push_back(entries[i].fn);
    auto& t = table();
    t.insert(t.end(), entries, entries + count);
    std::sort(t.begin(), t.end(), [](const Entry& a, const Entry& b) { return a.address < b.address; });
}

void register_rows(const game::Function* list, size_t count) { rows().insert(rows().end(), list, list + count); }

static Fn lookup(uint32_t address) {
    const auto& t = table();
    auto it = std::lower_bound(t.begin(), t.end(), address, [](const Entry& e, uint32_t a) { return e.address < a; });
    return it != t.end() && it->address == address ? it->fn : nullptr;
}

}  // namespace xl

extern "C" {

#if defined(__i386__) && defined(__linux__)
// xl_native_asm(Cpu* c, uint32_t target): saves the host's callee-saved registers; the host stack below them is
// where translated code the callee reaches runs (FS host word); puts xl_native_return into the return slot at
// c->esp, loads the guest registers and jumps to the target on the guest stack. xl_native_return goes back to the
// host stack through the FS word, saves the registers the callee left into the Cpu and returns to xl_native.
PLATFORM_HIDDEN void xl_native_asm(xl::Cpu* c, uint32_t target);
PLATFORM_ASM_ENTRY(xl_native_asm,
                   "pushl %ebp\n pushl %ebx\n pushl %esi\n pushl %edi\n"
                   "movl 20(%esp), %eax\n movl 24(%esp), %edx\n"
                   "movl %esp, %fs:" XL_FS_HOST "\n movl %edx, %fs:" XL_FS_TARGET "\n"
                   "call 1f\n1: popl %esi\n leal " PLATFORM_ASM_SYMBOL(xl_native_return) "-1b(%esi), %esi\n"
                   "movl 16(%eax), %ebx\n movl %esi, (%ebx)\n"
                   "movups 40(%eax), %xmm0\n movups 56(%eax), %xmm1\n movups 72(%eax), %xmm2\n movups 88(%eax), %xmm3\n"
                   "movups 104(%eax), %xmm4\n movups 120(%eax), %xmm5\n movups 136(%eax), %xmm6\n movups 152(%eax), %xmm7\n"
                   "movl %ebx, %esp\n"
                   "movl 4(%eax), %ecx\n movl 8(%eax), %edx\n movl 12(%eax), %ebx\n movl 20(%eax), %ebp\n"
                   "movl 24(%eax), %esi\n movl 28(%eax), %edi\n movl 0(%eax), %eax\n"
                   "jmp *%fs:" XL_FS_TARGET "\n"
                   PLATFORM_ASM_SYMBOL(xl_native_return) ":\n"
                   "movl %esp, %fs:" XL_FS_ESP "\n movl %fs:" XL_FS_HOST ", %esp\n"
                   "pushal\n leal -128(%esp), %esp\n"
                   "movups %xmm0, 0(%esp)\n movups %xmm1, 16(%esp)\n movups %xmm2, 32(%esp)\n movups %xmm3, 48(%esp)\n"
                   "movups %xmm4, 64(%esp)\n movups %xmm5, 80(%esp)\n movups %xmm6, 96(%esp)\n movups %xmm7, 112(%esp)\n"
                   "movl 180(%esp), %eax\n"
                   "movl 156(%esp), %ecx\n movl %ecx, 0(%eax)\n movl 152(%esp), %ecx\n movl %ecx, 4(%eax)\n"
                   "movl 148(%esp), %ecx\n movl %ecx, 8(%eax)\n movl 144(%esp), %ecx\n movl %ecx, 12(%eax)\n"
                   "movl %fs:" XL_FS_ESP ", %ecx\n movl %ecx, 16(%eax)\n movl 136(%esp), %ecx\n movl %ecx, 20(%eax)\n"
                   "movl 132(%esp), %ecx\n movl %ecx, 24(%eax)\n movl 128(%esp), %ecx\n movl %ecx, 28(%eax)\n"
                   "movups 0(%esp), %xmm0\n movups %xmm0, 40(%eax)\n movups 16(%esp), %xmm0\n movups %xmm0, 56(%eax)\n"
                   "movups 32(%esp), %xmm0\n movups %xmm0, 72(%eax)\n movups 48(%esp), %xmm0\n movups %xmm0, 88(%eax)\n"
                   "movups 64(%esp), %xmm0\n movups %xmm0, 104(%eax)\n movups 80(%esp), %xmm0\n movups %xmm0, 120(%eax)\n"
                   "movups 96(%esp), %xmm0\n movups %xmm0, 136(%eax)\n movups 112(%esp), %xmm0\n movups %xmm0, 152(%eax)\n"
                   "leal 160(%esp), %esp\n"
                   "popl %edi\n popl %esi\n popl %ebx\n popl %ebp\n ret\n");

// Every entry stub jumps here (FS index word set). The first entry on a guest stack makes its host stack: that one
// call runs on the guest stack (once per thread or fiber).
PLATFORM_ASM_ENTRY(xl_enter_common,
                   "movl %esp, %fs:" XL_FS_ESP "\n"
                   "cmpl $0, %fs:" XL_FS_HOST "\n jne 1f\n"
                   "pushal\n" PLATFORM_ASM_CALL(xl_host_init) "popal\n"
                   "1: movl %fs:" XL_FS_HOST ", %esp\n"
                   "pushal\n leal -128(%esp), %esp\n"
                   "movups %xmm0, 0(%esp)\n movups %xmm1, 16(%esp)\n movups %xmm2, 32(%esp)\n movups %xmm3, 48(%esp)\n"
                   "movups %xmm4, 64(%esp)\n movups %xmm5, 80(%esp)\n movups %xmm6, 96(%esp)\n movups %xmm7, 112(%esp)\n"
                   "pushl %fs:" XL_FS_INDEX "\n pushl %fs:" XL_FS_ESP "\n"
                   "movl %esp, %esi\n andl $-16, %esp\n subl $12, %esp\n pushl %esi\n" PLATFORM_ASM_CALL(xl_run)
                   "leal 8(%esi), %esp\n"
                   "movups 0(%esp), %xmm0\n movups 16(%esp), %xmm1\n movups 32(%esp), %xmm2\n movups 48(%esp), %xmm3\n"
                   "movups 64(%esp), %xmm4\n movups 80(%esp), %xmm5\n movups 96(%esp), %xmm6\n movups 112(%esp), %xmm7\n"
                   "leal 128(%esp), %esp\n popal\n"
                   "movl %fs:" XL_FS_ESP ", %esp\n jmp *%fs:" XL_FS_INDEX "\n");

// `ret N` for N = 0, 4, ... 252 (3 bytes each): the way out of an entry.
extern "C" PLATFORM_HIDDEN const uint8_t xl_ret_thunks[];
PLATFORM_ASM_ENTRY(xl_ret_thunks, ".rept 64\n .byte 0xc2\n .short (.-" PLATFORM_ASM_SYMBOL(xl_ret_thunks) ")/3*4\n .endr\n");
#endif

void xl_host_init() {
    uint32_t top;
    if (uint32_t* fiber = game::scr_fiber_host_sp()) {
        top = *fiber;
    } else {
        constexpr size_t size = 1u << 20;
        static thread_local uint8_t* base;
        if (!base) base = static_cast<uint8_t*>(std::malloc(size));
        if (!base) std::abort();
        top = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(base + size));
    }
    xl::fs_w32(xl::kFsHost, top & ~15u);
}

void xl_run(XlFrame* f) {
    const uint32_t level = f->index >> 16, index = f->index & 0xffffu;
    const xl::Fn fn =
        level < by_index().size() && index < by_index()[level].size() ? by_index()[level][index] : nullptr;
    if (!fn) std::abort();
    trace_first_call(fn);
    xl::Cpu c{f->eax, f->ecx, f->edx, f->ebx, f->guest_esp, f->ebp, f->esi, f->edi, 0, 0, {}};
    std::memcpy(c.xmm, f->xmm, sizeof c.xmm);
    const uint32_t entry_esp = f->guest_esp, ret = xl::mem_r32(entry_esp);
    fn(&c);
    if (c.ret != ret || c.esp != entry_esp + 4 + c.ret_pop || c.ret_pop > 252 || (c.ret_pop & 3)) {
        std::fprintf(stderr, "translated level code: unexpected return (esp %08x, expected %08x; ret %08x/%08x)\n",
                     c.esp, entry_esp + 4 + c.ret_pop, c.ret, ret);
        std::abort();
    }
    f->eax = c.eax, f->ecx = c.ecx, f->edx = c.edx, f->ebx = c.ebx, f->ebp = c.ebp, f->esi = c.esi, f->edi = c.edi;
    std::memcpy(f->xmm, c.xmm, sizeof f->xmm);
    // The way out: ESP at the return slot, then `ret N` (the FS words are free again: nothing runs in between).
#if defined(__i386__) && defined(__linux__)
    xl::fs_w32(xl::kFsEsp, entry_esp);
    xl::fs_w32(xl::kFsIndex, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(xl_ret_thunks)) + c.ret_pop / 4 * 3);
#endif
    // A float return goes to the native caller in st(0), as the original's did (last: nothing below uses the FPU).
    if (c.fret) {
        const long double v = xl::ld_f80(c.f80);
        __asm__ volatile("fldt %0" : : "m"(v));
    }
}

}  // extern "C"

namespace xl {

void xl_native(Cpu* c, uint32_t target) {
    if (target < 0x10000) {  // a null or wild pointer: name the call site (the return address the caller pushed)
        std::fprintf(stderr, "translated level code: call to %08x from the call returning to %08x (esp %08x)\n",
                     target, mem_r32(c->esp), c->esp);
        std::abort();
    }
#if defined(__i386__) && defined(__linux__)
    const uint32_t host = fs_r32(kFsHost);
    // The return slot holds our way back while the callee runs; afterwards what a `call` leaves there.
    const uint32_t ret_slot = c->esp, ret = mem_r32(ret_slot);
    uint16_t before, after;
    __asm__ volatile("fnstsw %0" : "=m"(before));
    xl_native_asm(c, target);
    // A float return: the callee pushed one value on the FPU (its stack top moved down by one) - into the Cpu.
    __asm__ volatile("fnstsw %0" : "=m"(after));
    if (((after >> 11) & 7) == ((((before >> 11) & 7) + 7) & 7)) {
        long double v;
        __asm__ volatile("fstpt %0" : "=m"(v));
        st_f80(c->f80, v);
        c->fret = 1;
    }
    mem_w32(ret_slot, ret);
    fs_w32(kFsHost, host);
#else
    std::fprintf(stderr, "translated level code: no native code to call (%08x)\n", target);
    std::abort();
#endif
}

void xl_native_tail(Cpu* c, uint32_t target) {
    const uint32_t slot = c->esp, ret = mem_r32(slot);
    xl_native(c, target);
    c->ret = ret;
    c->ret_pop = c->esp - slot - 4;
}

void xl_call(Cpu* c, uint32_t target) {
    if (Fn fn = lookup(target)) return fn(c);
    xl_native(c, target);
}

void xl_tail(Cpu* c, uint32_t target) {
    if (Fn fn = lookup(target)) return fn(c);
    xl_native_tail(c, target);
}

uint32_t host_sp_swap(uint32_t sp) {
#if defined(__i386__) && defined(__linux__)
    const uint32_t old = fs_r32(kFsHost);
    fs_w32(kFsHost, sp);
    return old;
#else
    return sp;
#endif
}

}  // namespace xl

namespace game {
// Every level built in (each generated file registers its rows at start-up).
std::span<const Function> translated_functions() { return {rows().data(), rows().size()}; }
}  // namespace game
