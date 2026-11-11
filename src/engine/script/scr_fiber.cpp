#include "scr_fiber.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "g_scr_runtime.h"
#include "qcommon_core.h"
#include "stack_guard.h"
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

// 0x001fa400 Scr_ThreadEntry (context object in ECX): try { context->Invoke() } catch (...) {} - a waiting thread
// that is ended throws from its wait (Scr_Wait & co. when g_scrThreadAborting is set) and lands in the catch. The
// original's frame instruction for instruction (EH node {next, handler, state} with the saved ESP below it), with
// our own handler stub, FuncInfo (a copy of 0x00895c0c: 2 states, one try block, catch(...)) and catch funclet;
// the library is position independent, so label addresses come from call/pop (EAX is free at each of them).
PLATFORM_ASM_ENTRY(Scr_ThreadEntry,
                   "pushl %ebp\n movl %esp, %ebp\n"
                   "pushl $-1\n call 9f\n9: popl %eax\n addl $(.Lscr_te_handler - 9b), %eax\n pushl %eax\n"
                   "movl %fs:0, %eax\n pushl %eax\n movl %esp, %fs:0\n"
                   "pushl %ecx\n pushl %ebx\n pushl %esi\n pushl %edi\n"
                   "movl %esp, -0x10(%ebp)\n"
                   "movl $0, -4(%ebp)\n"
                   "movl (%ecx), %eax\n call *4(%eax)\n"
                   ".Lscr_te_continue:\n"
                   "movl $-1, -4(%ebp)\n"
                   "movl -0xc(%ebp), %ecx\n movl %ecx, %fs:0\n"
                   "popl %edi\n popl %esi\n popl %ebx\n"
                   "movl %ebp, %esp\n popl %ebp\n"
                   "ret\n"
                   ".Lscr_te_catch:\n"
                   "call 9f\n9: popl %eax\n addl $(.Lscr_te_continue - 9b), %eax\n ret\n"
                   ".Lscr_te_handler:\n"
                   "call 9f\n9: popl %eax\n addl $(.Lscr_te_funcinfo - 9b), %eax\n jmp " PLATFORM_ASM_SYMBOL(title_CxxFrameHandler_entry) "\n"
                   ".data\n .p2align 2\n"
                   ".Lscr_te_funcinfo: .long 0x19930520, 2, .Lscr_te_unwind, 1, .Lscr_te_try\n"
                   ".Lscr_te_unwind: .long -1, 0, -1, 0\n"
                   ".Lscr_te_try: .long 0, 0, 1, 1, .Lscr_te_handlers\n"
                   ".Lscr_te_handlers: .long 0, 0, 0, .Lscr_te_catch\n"
                   ".text")

// A new thread's first return lands here, ECX = its context object (from the POPA): Scr_ThreadEntry, then back to
// the scheduler the way Scr_RestoreContext would (the stack there is still the scheduler's, 1: in scr_fiber_switch).
extern "C" PLATFORM_HIDDEN void XB_CDECL scr_fiber_thread_ended();
extern "C" void scr_fiber_start(void);
PLATFORM_ASM_ENTRY(scr_fiber_start,
                   "andl $-16, %esp\n"
                   PLATFORM_ASM_CALL(Scr_ThreadEntry) PLATFORM_ASM_CALL(scr_fiber_thread_ended)
                   "movl 0x009270a0, %edi\n movl 0x009270a4, %esi\n movl 0x009270a8, %ebp\n"
                   "movl 0x009270b0, %ebx\n"
                   "movl 0x009270ac, %esp\n"
                   "movl 0x009270c0, %eax\n movl %eax, (%esp)\n"
                   "pushl 0x0092709c\n popfl\n"
                   "ret")


// The suspends (Scr_SuspendWaitTime/Frames/Till, 0x0011d950/990/9d0): the wait's frame as the original leaves it
// (16-bit FLAGS, POPA block, FS:[0]; its ESP also in 0x00922ed4/d8/dc), then scr_fiber_wait on the C stack below
// it. The scheduler resumes the thread by popping that frame (scr_fiber_switch), so the suspend returns to its
// caller. The original's 0x80-byte scratch area and EBP = ESP before Scr_SaveThreadLocals are not kept: POPA
// restores EBP and nothing reads the area.
extern "C" PLATFORM_HIDDEN void XB_CDECL scr_fiber_wait(uint32_t kind, uint32_t frame);
#define SCR_FIBER_SUSPEND(name, kind, word)                                                       \
    PLATFORM_ASM_ENTRY(name, "pushfw\n pushal\n pushl %fs:0\n movl %esp, " word "\n"              \
                             "movl %esp, %eax\n andl $-16, %esp\n subl $8, %esp\n"                \
                             "pushl %eax\n pushl $" kind "\n" PLATFORM_ASM_CALL(scr_fiber_wait) "ud2")
SCR_FIBER_SUSPEND(Scr_SuspendWaitTime, "0", "0x00922ed4")
SCR_FIBER_SUSPEND(Scr_SuspendWaitFrames, "1", "0x00922ed8")
SCR_FIBER_SUSPEND(Scr_SuspendWaitTill, "2", "0x00922edc")
#undef SCR_FIBER_SUSPEND

// 0x002f5660: back to the context `context` describes (Scr_SaveContext's layout, see scr_fiber_switch): its
// return address goes into the slot its ESP names, then FLAGS and the POPA block are pushed there and popped.
// Kept instruction for instruction: the compare harness lands a suspended test through it.
PLATFORM_ASM_ENTRY(Scr_RestoreContext,
                   "movl 4(%esp), %eax\n"
                   "movl 0x10(%eax), %ebx\n movl 0x24(%eax), %ecx\n movl %ecx, (%ebx)\n"
                   "movl 0x10(%eax), %esp\n"
                   "pushl 0x20(%eax)\n pushl 0x1c(%eax)\n pushl 0x18(%eax)\n pushl 0x14(%eax)\n pushl 0x10(%eax)\n"
                   "pushl 0xc(%eax)\n pushl 0x8(%eax)\n pushl 0x4(%eax)\n pushl (%eax)\n"
                   "popfl\n popal\n"
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

void scr_fiber_switch_ranges(std::vector<std::pair<uint32_t, uint32_t>>& out) {
    const auto word = [&](uint32_t at) { out.push_back({at, at + 4}); };
    if (g_running) word(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_running->esp)));
    word(game_floor_word());
    uint32_t tib = 0;
#if defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
    __asm__ volatile("movl %%fs:0x18, %0" : "=r"(tib));  // the TIB's self pointer
#endif
    if (tib) {
        word(tib);         // FS:[0], the SEH chain
        word(tib + 0x14);  // the frame limit (platform_set_title_frame_limit)
    }
    uint32_t begin = 0, end = 0;
    stack_guard_range(&begin, &end);
    if (begin < end) out.push_back({begin, end});
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

namespace game {

namespace {

constexpr uint32_t kSchedulerContext = 0x0092709c;

uint8_t* alloc_record(uint32_t size) {
    return SmallHeap_Alloc(title_global<uint8_t*>(0x0095f494), size, 0);  // com_smallHeap
}

// A plain wait record: {vtable, 0, 0, done 0, kind 2, value} (+0x14), as Scr_WaitRecord15/16 fill it.
uint8_t* simple_record(uint32_t size, uint32_t vtable) {
    uint8_t* const record = alloc_record(size);
    if (record) {
        word(record, 4) = 0;
        word(record, 8) = 0;
        record[0xc] = 0;
        word(record, 0x10) = 2;
        word(record, 0) = vtable;
    }
    return record;
}

// The record joins the current thread's wait list (+0x20 sentinel, +0x24 last, +0x18 count) and the thread is
// marked waiting (+0x10 |= 2, 0x10, 0x200: three stores, as the original), then back to the scheduler.
[[noreturn]] void wait_and_switch(uint8_t* record) {
    uint8_t* const thread = title_global<uint8_t*>(kCurrentThread);
    uint32_t& flags = word(thread, 0x10);
    uint32_t f = flags | 2;
    flags = f;
    f |= 0x10;
    flags = f;
    flags = f | 0x200;
    // A failed allocation links at address 4 (the original does the same; SmallHeap_Alloc does not fail).
    uint32_t* const node = reinterpret_cast<uint32_t*>(record + 4);
    node[0] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(thread + 0x20));
    const uint32_t last = word(thread, 0x24);
    node[1] = last;
    *reinterpret_cast<uint32_t*>(uintptr_t{last}) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node));
    word(thread, 0x24) = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(node));
    ++word(thread, 0x18);
    Scr_RestoreContext(kSchedulerContext);
    __builtin_unreachable();
}

}  // namespace

void XB_CDECL Scr_WaitRecord15(uint32_t seconds_bits) {
    uint8_t* const record = simple_record(0x18, 0x00831824);
    if (record) word(record, 0x14) = seconds_bits;
    wait_and_switch(record);
}

void XB_CDECL Scr_WaitRecord16(uint32_t frames) {
    uint8_t* const record = simple_record(0x18, 0x00831808);
    if (record) word(record, 0x14) = frames;
    wait_and_switch(record);
}

void XB_CDECL Scr_WaitRecord17(uint32_t object, uint32_t wait_object, uint32_t notify, uint32_t a, uint32_t b,
                               uint32_t c, uint32_t flag, uint32_t timeout_bits) {
    uint8_t* record;
    if (wait_object) {
        record = simple_record(0x1c, 0x0083181c);
        if (record) {
            word(record, 0x14) = wait_object;
            word(record, 0x18) = notify;
        }
    } else {
        // The object is an entity handle (low 12 bits the slot, the rest its generation): a stale or empty one
        // ends the thread (+0x10 |= 8, 0x40, 4) instead of waiting.
        const uint32_t slot = object & 0xfff;
        const uint32_t* const table = &title_global<uint32_t>(0x0097a968);  // g_entityHandleTable {entity, gen}
        if (slot >= 0x540 || table[slot * 2 + 1] != (object >> 12) || !table[slot * 2]) {
            uint32_t& flags = word(title_global<uint8_t*>(kCurrentThread), 0x10);
            const uint32_t f = flags | 8;
            flags = f;
            flags = f | 0x40;
            flags = f | 0x44;
            Scr_RestoreContext(kSchedulerContext);
            __builtin_unreachable();
        }
        if (!(a | b | c)) {
            float timeout;
            std::memcpy(&timeout, &timeout_bits, 4);
            if (timeout > 0.0f) {  // comiss/jbe: NaN takes the plain record
                record = alloc_record(0x20);
                if (record) record = ScrWaitTimeoutRecord_Init(record, object, notify, 2, timeout_bits);
            } else {
                record = alloc_record(0x20);
                if (record) {
                    word(record, 4) = 0;
                    word(record, 8) = 0;
                    record[0xc] = 0;
                    word(record, 0x10) = 2;
                    word(record, 0) = 0x00831800;  // ScrWaitRecord_vtable
                    word(record, 0x14) = object;
                    word(record, 0x18) = notify;
                    word(record, 0x1c) = 0;
                }
            }
        } else {
            record = alloc_record(0x30);
            if (record) record = Scr_InitWaitRecord4(record, 0, 2, object, notify, a, b, c, flag);
        }
    }
    wait_and_switch(record);
}

void scr_fiber_reset() {
    auto drop = [](Fiber* fiber) {
        platform::vmem::release(fiber->reservation, kStackSize + kGuardSize);
        delete fiber;
    };
    for (auto& [thread, fiber] : g_fibers) drop(fiber);
    for (Fiber* fiber : g_free) drop(fiber);
    g_fibers.clear();
    g_free.clear();
    g_running = nullptr;
    g_ended = false;
    g_live = g_peak = g_created = 0;
}

}  // namespace game

// Scr_SaveThreadLocals (import slot 13) is not called: the suspend keeps the frame with the fiber itself.
extern "C" PLATFORM_HIDDEN void XB_CDECL scr_fiber_wait(uint32_t kind, uint32_t frame) {
    using namespace game;
    scr_fiber_note_wait(title_global<uint8_t*>(kCurrentThread), frame);
    switch (kind) {
        case 0:
            Scr_WaitRecord15(title_global<uint32_t>(0x00922124));  // g_scrWaitTime
            break;
        case 1:
            Scr_WaitRecord16(title_global<uint32_t>(0x00922128));
            break;
        default:
            Scr_WaitRecord17(title_global<uint32_t>(0x00922134), title_global<uint32_t>(0x00922e6c),
                             title_global<uint32_t>(0x00922130), title_global<uint32_t>(0x00922120),
                             title_global<uint32_t>(0x0092212c), title_global<uint32_t>(0x00922e60),
                             title_global<uint8_t>(0x00922e5c), title_global<uint32_t>(0x00922138));
            break;
    }
}
