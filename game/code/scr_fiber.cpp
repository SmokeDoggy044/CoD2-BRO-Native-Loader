#include "scr_fiber.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "g_scr_runtime.h"
#include "title_data.h"
#include "vmem.h"

// The switch itself (AT&T, both 32-bit builds). scr_fiber_switch(target, fresh) keeps the scheduler's EBP, EBX,
// ESI, EDI and FS:[0] on its own stack, writes the context Scr_RestoreContext (0x002f5660) returns through at
// 0x0092709c - Scr_SaveContext's layout {EFLAGS, EDI, ESI, EBP, ESP, EBX, EDX, ECX, EAX, return address}, with
// ESP the slot holding the return address 1: - and loads `target`, a frame as a wait leaves it: FS:[0], POPA,
// 16-bit FLAGS, return address. A fresh frame gets the scheduler's FS:[0] (the original's new threads link
// their first SEH record to the chain they start under). The thread comes back to 1: when it waits (the wait
// record jumps to Scr_RestoreContext) or ends (scr_fiber_start below), with EBP, EBX, ESI, EDI restored.
extern "C" PLATFORM_HIDDEN void XB_CDECL scr_fiber_switch(uint32_t target, uint32_t fresh);
PLATFORM_ASM_ENTRY(scr_fiber_switch,
                   "pushl %ebp\n pushl %ebx\n pushl %esi\n pushl %edi\n"
                   "pushl %fs:0\n"
                   "movl 24(%esp), %eax\n"
                   "movl 28(%esp), %edx\n"
                   "call 2f\n"
                   "1:\n"
                   "popl %eax\n movl %eax, %fs:0\n"
                   "popl %edi\n popl %esi\n popl %ebx\n popl %ebp\n"
                   "ret\n"
                   "2:\n"
                   "movl (%esp), %ecx\n movl %ecx, 0x009270c0\n"
                   "movl %esp, 0x009270ac\n"
                   "pushfl\n popl %ecx\n movl %ecx, 0x0092709c\n"
                   "movl %edi, 0x009270a0\n movl %esi, 0x009270a4\n movl %ebp, 0x009270a8\n"
                   "movl %ebx, 0x009270b0\n movl $0, 0x009270b4\n movl $0, 0x009270b8\n movl $0, 0x009270bc\n"
                   "testl %edx, %edx\n jz 3f\n movl %fs:0, %ecx\n movl %ecx, (%eax)\n"
                   "3:\n"
                   "movl %eax, %esp\n"
                   "popl %eax\n movl %eax, %fs:0\n"
                   "popal\n"
                   "popfw\n"
                   "ret")

// A new thread's first return lands here, ECX = its context object (from the POPA): Scr_ThreadEntry
// (0x001fa400: try { context->Invoke() } catch (...) {}), then back to the scheduler the way
// Scr_RestoreContext would (the stack there is still the scheduler's, 1: in scr_fiber_switch).
extern "C" PLATFORM_HIDDEN void XB_CDECL scr_fiber_thread_ended();
extern "C" void scr_fiber_start(void);
PLATFORM_ASM_ENTRY(scr_fiber_start,
                   "andl $-16, %esp\n"
                   "movl $0x001fa400, %eax\n call *%eax\n" PLATFORM_ASM_CALL(scr_fiber_thread_ended)
                   "movl 0x009270a0, %edi\n movl 0x009270a4, %esi\n movl 0x009270a8, %ebp\n"
                   "movl 0x009270b0, %ebx\n"
                   "movl 0x009270ac, %esp\n"
                   "movl 0x009270c0, %eax\n movl %eax, (%esp)\n"
                   "pushl 0x0092709c\n popfl\n"
                   "ret")

namespace game {

namespace {

constexpr uint32_t kCurrentThread = 0x008dda2c;   // the running script thread, 0 in the scheduler
constexpr uint32_t kImportBlock = 0x0095f49c;     // -> the level import block (0x00922140)
// A fiber's usable stack and the no-access page below it. The title's frames on a waiting thread stay under
// 1 KB (measured 11-07: 986 bytes, missions 4 and 10); the rest is our C below builtins the thread calls and
// their gap entries' gaps (up to 0x40000 each, title_data.h GAME_GAP_ESP).
constexpr uint32_t kStackSize = 1024 * 1024;
constexpr uint32_t kGuardSize = 4096;

struct Fiber {
    uint8_t* reservation = nullptr;  // guard page + stack
    uint32_t limit = 0;              // lowest usable address
    uint32_t top = 0;                // first frame goes below this
    uint32_t esp = 0;                // the wait's frame while the thread waits
    uint32_t floor = 0;              // the thread's gap-entry floor while it waits (title_data.h)
};

std::unordered_map<uint8_t*, Fiber*> g_fibers;  // thread record -> its fiber
std::vector<Fiber*> g_free;
Fiber* g_running = nullptr;
bool g_ended = false;
uint32_t g_live = 0, g_peak = 0, g_created = 0;

Fiber* new_fiber() {
    auto* fiber = new Fiber;
    fiber->reservation = static_cast<uint8_t*>(platform::vmem::reserve(0, kStackSize + kGuardSize));
    if (!fiber->reservation ||
        !platform::vmem::commit(fiber->reservation + kGuardSize, kStackSize, platform::vmem::Protection::ReadWrite)) {
        std::fprintf(stderr, "[scr_fiber] out of address space for script thread stacks (%u live)\n", g_live);
        std::abort();
    }
    fiber->limit = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fiber->reservation)) + kGuardSize;
    fiber->top = fiber->limit + kStackSize - 16;
    ++g_created;
    return fiber;
}

Fiber* take_fiber(uint8_t* thread) {
    if (auto it = g_fibers.find(thread); it != g_fibers.end()) return it->second;  // a reused record
    Fiber* fiber;
    if (g_free.empty()) {
        fiber = new_fiber();
    } else {
        fiber = g_free.back();
        g_free.pop_back();
    }
    fiber->esp = 0;
    fiber->floor = 0;
    g_fibers[thread] = fiber;
    if (++g_live > g_peak) {
        g_peak = g_live;
        if (g_peak % 64 == 0 && std::getenv("COD_FIBER_STATS"))
            std::fprintf(stderr, "[scr_fiber] peak %u live threads (%u stacks)\n", g_peak, g_created);
    }
    return fiber;
}

uint32_t g_deepest = 0;

// COD_FIBER_STATS: how deep the fiber's stack was used (the lowest nonzero word; zeroed again for its next use).
void measure(Fiber* fiber) {
    static const bool stats = std::getenv("COD_FIBER_STATS") != nullptr;
    if (!stats) return;
    auto* words = reinterpret_cast<uint32_t*>(uintptr_t{fiber->limit});
    const uint32_t count = (fiber->top + 16 - fiber->limit) / 4;
    uint32_t i = 0;
    while (i < count && !words[i]) ++i;
    const uint32_t used = (count - i) * 4;
    if (used > g_deepest) {
        g_deepest = used;
        std::fprintf(stderr, "[scr_fiber] deepest stack use %u bytes\n", used);
    }
    std::memset(words + i, 0, (count - i) * 4);
}

void give_back(uint8_t* thread) {
    const auto it = g_fibers.find(thread);
    if (it == g_fibers.end()) return;
    if (it->second == g_running) return;  // still on it (cannot happen: records are freed by the scheduler)
    measure(it->second);
    g_free.push_back(it->second);
    g_fibers.erase(it);
    --g_live;
}

// A new thread's frame, as a wait leaves one (scr_fiber_switch pops it): FS:[0] (filled by the switch),
// EDI ESI EBP ESP EBX EDX ECX EAX (ECX = the context object), FLAGS (16-bit, 0x0202 = IF as a wait sees it),
// the return address scr_fiber_start.
uint32_t fresh_frame(Fiber* fiber, uint32_t context) {
    uint32_t esp = fiber->top - 42;
    uint8_t* frame = reinterpret_cast<uint8_t*>(uintptr_t{esp});
    std::memset(frame, 0, 42);
    *reinterpret_cast<uint32_t*>(frame + 4 + 6 * 4) = context;  // ECX in the POPA block
    const uint16_t flags = 0x0202;
    std::memcpy(frame + 36, &flags, 2);
    const uint32_t start = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&scr_fiber_start));
    std::memcpy(frame + 38, &start, 4);
    return esp;
}

// Runs the fiber from `esp` until its thread waits or ends; true when it ended. The gap-entry floor and the
// on-a-fiber mark (FS:[0x14] bit 0) go with the stack they describe.
bool run(Fiber* fiber, uint32_t esp, bool fresh) {
    g_running = fiber;
    g_ended = false;
    const uint32_t scheduler_floor = game_floor_swap(fiber->floor);
    platform_set_title_on_fiber(true);
    scr_fiber_switch(esp, fresh ? 1u : 0u);
    platform_set_title_on_fiber(false);
    fiber->floor = game_floor_swap(scheduler_floor);
    g_running = nullptr;
    return g_ended;
}

uint32_t& word(uint8_t* base, uint32_t offset) { return *reinterpret_cast<uint32_t*>(base + offset); }

struct Stats {
    ~Stats() {
        if (std::getenv("COD_FIBER_STATS"))
            std::fprintf(stderr, "[scr_fiber] %u stacks made, peak %u live threads\n", g_created, g_peak);
    }
} g_stats;

}  // namespace

bool scr_fiber_stack(uint32_t* limit, uint32_t* base) {
    if (!g_running) return false;
    *limit = g_running->limit;
    *base = g_running->top + 16;
    return true;
}

void scr_fiber_note_wait(uint8_t* thread, uint32_t frame) {
    const auto it = g_fibers.find(thread);
    if (it == g_fibers.end() || it->second != g_running) {
        std::fprintf(stderr, "[scr_fiber] thread %p waits off its fiber\n", static_cast<void*>(thread));
        std::abort();
    }
    it->second->esp = frame;
}

void scr_fiber_release(uint8_t* thread) { give_back(thread); }

}  // namespace game

extern "C" PLATFORM_HIDDEN void XB_CDECL scr_fiber_thread_ended() { game::g_ended = true; }

extern "C" PLATFORM_HIDDEN void XB_CDECL Scr_ResumeThread(uint8_t* thread) {
    using namespace game;
    title_global<uint8_t*>(kCurrentThread) = thread;
    Scr_UpdateThreadWaits(thread);
    uint32_t& flags = word(thread, 0x10);
    title_global<uint8_t*>(kImportBlock)[0x24] = static_cast<uint8_t>((flags >> 6) & 1);  // g_scrThreadAborting
    // The original re-tests the flags each time the thread comes back from a wait (its Scr_SaveContext returns
    // again); an end returns for good.
    for (;;) {
        const uint32_t f = flags;
        if ((f & 1) && !((f >> 4) & 1)) {  // new, not waiting
            flags = f & ~1u;
            flags = flags & ~4u;
            if ((flags >> 6) & 1) {  // ended before it ran
                flags |= 8;
                break;
            }
            Fiber* const fiber = take_fiber(thread);
            word(thread, 0x28) = fiber->top;  // the original's stack base for the save; kept for its readers
            if (run(fiber, fresh_frame(fiber, word(thread, 0xc)), true)) {
                flags |= 8;
                give_back(thread);
                break;
            }
            continue;
        }
        if (((f >> 1) & 1) && ((f >> 2) & 1)) {  // waited, now runnable
            flags = f & ~2u;
            flags = flags & ~4u;
            const auto it = g_fibers.find(thread);
            if (it == g_fibers.end() || !it->second->esp) {
                std::fprintf(stderr, "[scr_fiber] thread %p resumed without a saved wait\n", static_cast<void*>(thread));
                std::abort();
            }
            Fiber* const fiber = it->second;
            const uint32_t esp = fiber->esp;
            fiber->esp = 0;
            if (run(fiber, esp, false)) {
                flags |= 8;
                give_back(thread);
                break;
            }
            continue;
        }
        break;
    }
    title_global<uint32_t>(kCurrentThread) = 0;
}

PLATFORM_ASM_ENTRY(Scr_ResumeThread_entry,
                   "pushl %eax\n" PLATFORM_ASM_CALL(Scr_ResumeThread) "addl $4, %esp\n"
                   "ret")
