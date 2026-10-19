// Comparison harness, Linux (docs/comparison-harness.md).
//
// A tested function's entry jumps to a stub (push index; jmp xbcmp_entry) that saves
// the caller's registers and calls xbcmp_dispatch. For a sampled call the dispatcher
// forks a child, which forks again at once, so that both start from the same copy of
// memory: the child runs the original, the grandchild the decompiled version. Before
// running, each makes every host mapping non-executable except the title's code, the
// harness's own section (xbcmp_text), the decompiled code and the C runtime, then
// clears the soft-dirty bits. A call into any other host code faults and ends the
// test as inconclusive. The function returns to xbcmp_landing, which saves the
// registers; the process reports them with the pages it wrote (soft-dirty bits in
// /proc/self/pagemap) over a pipe and sends the pages either side wrote. The parent
// compares and continues with the original through a trampoline.
//
// Everything a test process runs after the mprotect is in xbcmp_text and makes raw
// system calls only: no C runtime, no calls the compiler adds (memcpy, stack checks).

#include "compare.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <poll.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/ucontext.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>

#include "compiler.h"
#include "float_env.h"
#include "hooks.h"
#include "log.h"

namespace loader::compare {
namespace {

// Registers at a call, as the entry stub saved them; the test processes start from
// them and the landing saves the results the same way. The assembly uses the offsets.
struct alignas(16) Context {
    uint8_t fx[512];  // FXSAVE: x87 and SSE registers, control words, MXCSR
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t eflags;
    uint32_t target;  // where xbcmp_resume continues
    uint32_t pad[2];
};
static_assert(offsetof(Context, eax) == 512 && offsetof(Context, esp) == 528 && offsetof(Context, edi) == 540);
static_assert(offsetof(Context, eflags) == 544 && offsetof(Context, target) == 548 && sizeof(Context) == 560);

// What xbcmp_entry leaves on the stack: pushad, pushfd, the stub's index, the
// caller's return address.
struct EntryFrame {
    uint32_t edi, esi, ebp, esp_ignored, ebx, edx, ecx, eax;
    uint32_t eflags;
    uint32_t index;
    uint32_t return_address;  // the caller's ESP at the call points here
};

constexpr uint32_t kPage = 4096;
constexpr uint32_t kMaxRanges = 2048;
constexpr uint32_t kMaxDirty = 32768;  // 128 MB written by one call
constexpr uint32_t kPagemapChunk = 2048;
constexpr uint32_t kScanRegions = 512;  // runs of written pages per PAGEMAP_SCAN call

enum ReportStatus : uint32_t { kOk, kSignal, kTooManyWrites, kHarnessError };

struct Report {
    uint32_t status;
    uint32_t signal, eip, fault_address, esp, stack_word;  // kSignal
    uint32_t dirty_count;
    uint32_t scan_pages;  // pages the dirty scan read from pagemap
    // CLOCK_MONOTONIC ns: process started, maps read, bits cleared, function returned,
    // dirty pages collected (0 where not reached).
    uint64_t t_entered, t_mapped, t_cleared, t_landed, t_collected;
};

struct Range {
    uint32_t begin, end, protection;
};

// Everything a test process writes after it cleared the soft-dirty bits, in one
// block that the dirty-page scan skips. `result` must stay first (xbcmp_landing).
struct alignas(4096) ChildArea {
    Context result;
    Context start;
    Report report;
    int32_t out, in, pagemap_fd, clear_refs_fd;
    uint32_t grandchild;
    char clear_command;
    uint32_t scan_count, protect_count;
    Range scan[kMaxRanges];     // memory the function may write (private, anonymous)
    Range protect[kMaxRanges];  // host code to make non-executable
    uint32_t dirty[kMaxDirty];
    uint32_t wanted_count;
    uint32_t wanted[2 * kMaxDirty];
    uint32_t pagemap[2 * kPagemapChunk];
    pm_scan_arg scan_arg;
    page_region regions[kScanRegions];
    uint8_t signal_stack[64 * 1024];
    uint8_t stack[64 * 1024];
};

}  // namespace
}  // namespace loader::compare

using loader::compare::ChildArea;
using loader::compare::Context;
using loader::compare::EntryFrame;

extern "C" {
ChildArea xbcmp_child;
uint32_t xbcmp_child_stack_top;
// Set in test processes: a hooked function reached there (a callee of the tested one)
// runs its original straight from the stub, since the dispatcher is not executable.
uint8_t xbcmp_in_test;
extern char __start_xbcmp_text[];
extern char __stop_xbcmp_text[];
void xbcmp_entry();
void xbcmp_landing();
[[noreturn]] void xbcmp_resume(const Context* context);
[[noreturn]] void xbcmp_dispatch(EntryFrame* frame);
[[noreturn]] void xbcmp_child_finish();
}

// xbcmp_entry: [esp] = function index (pushed by its stub), [esp+4] = return address.
// xbcmp_resume(context): loads every register from `context` and jumps to
// context->target with ESP = context->esp.
// xbcmp_landing: where a test process's function returns; saves the registers into
// xbcmp_child.result, switches to the area's stack and reports.
asm(R"(
    .pushsection xbcmp_text,"ax",@progbits
    .p2align 12
    .globl xbcmp_entry
xbcmp_entry:
    pushfl
    pushal
    cld
    movl %esp, %eax
    pushl %eax
    call xbcmp_dispatch
    ud2

    .globl xbcmp_resume
xbcmp_resume:
    movl 4(%esp), %eax
    fxrstor (%eax)
    movl 528(%eax), %esp
    pushl 548(%eax)
    pushl 544(%eax)
    movl 516(%eax), %ecx
    movl 520(%eax), %edx
    movl 524(%eax), %ebx
    movl 532(%eax), %ebp
    movl 536(%eax), %esi
    movl 540(%eax), %edi
    movl 512(%eax), %eax
    popfl
    ret

    .globl xbcmp_landing
xbcmp_landing:
    movl %eax, xbcmp_child+512
    movl %ecx, xbcmp_child+516
    movl %edx, xbcmp_child+520
    movl %ebx, xbcmp_child+524
    movl %esp, xbcmp_child+528
    movl %ebp, xbcmp_child+532
    movl %esi, xbcmp_child+536
    movl %edi, xbcmp_child+540
    movl xbcmp_child_stack_top, %esp
    pushfl
    popl xbcmp_child+544
    fxsave xbcmp_child
    cld
    call xbcmp_child_finish
    ud2
    .popsection
)");

namespace loader::compare {
namespace {

// --- Test-process side after the mprotect: xbcmp_text only -----------------------

#define XBCMP_CODE PLATFORM_SECTION("xbcmp_text")

XBCMP_CODE int32_t sys3(uint32_t number, uint32_t a, uint32_t b, uint32_t c) {
    int32_t result;
    asm volatile("int $0x80" : "=a"(result) : "a"(number), "b"(a), "c"(b), "d"(c) : "memory");
    return result;
}

XBCMP_CODE int32_t sys5(uint32_t number, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e) {
    int32_t result;
    asm volatile("int $0x80" : "=a"(result) : "a"(number), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e) : "memory");
    return result;
}

XBCMP_CODE uint64_t raw_now_ns() {
    struct {
        int64_t seconds, nanoseconds;
    } t{};
    sys3(SYS_clock_gettime64, CLOCK_MONOTONIC, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&t)), 0);
    return static_cast<uint64_t>(t.seconds) * 1000000000u + static_cast<uint64_t>(t.nanoseconds);
}

XBCMP_CODE uint32_t addr_of(const void* pointer) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer)); }

XBCMP_CODE bool write_all(int32_t fd, uint32_t data, uint32_t size) {
    while (size > 0) {
        const int32_t written = sys3(SYS_write, static_cast<uint32_t>(fd), data, size);
        if (written == -EINTR) continue;
        if (written <= 0) return false;
        data += static_cast<uint32_t>(written);
        size -= static_cast<uint32_t>(written);
    }
    return true;
}

XBCMP_CODE bool read_all(int32_t fd, uint32_t data, uint32_t size) {
    while (size > 0) {
        const int32_t got = sys3(SYS_read, static_cast<uint32_t>(fd), data, size);
        if (got == -EINTR) continue;
        if (got <= 0) return false;
        data += static_cast<uint32_t>(got);
        size -= static_cast<uint32_t>(got);
    }
    return true;
}

XBCMP_CODE bool in_scan_range(const ChildArea& c, uint32_t address, uint32_t size) {
    for (uint32_t i = 0; i < c.scan_count; ++i)
        if (address >= c.scan[i].begin && address - c.scan[i].begin <= c.scan[i].end - c.scan[i].begin - size)
            return true;
    return false;
}

// The same with the PAGEMAP_SCAN ioctl (Linux 6.7+), which returns only the runs of
// written pages instead of an entry per page. False if the kernel lacks it.
XBCMP_CODE bool collect_dirty_scan(ChildArea& c, uint32_t own_begin, uint32_t own_end) {
    pm_scan_arg& a = c.scan_arg;
    uint32_t count = 0;
    for (uint32_t r = 0; r < c.scan_count; ++r) {
        c.report.scan_pages += (c.scan[r].end - c.scan[r].begin) / kPage;
        for (uint32_t start = c.scan[r].begin; start < c.scan[r].end;) {
            a.size = sizeof(pm_scan_arg);
            a.flags = 0;
            a.start = start;
            a.end = c.scan[r].end;
            a.walk_end = 0;
            a.vec = addr_of(c.regions);
            a.vec_len = kScanRegions;
            a.max_pages = 0;
            a.category_inverted = 0;
            a.category_mask = PAGE_IS_SOFT_DIRTY;
            a.category_anyof_mask = 0;
            a.return_mask = PAGE_IS_SOFT_DIRTY;
            const int32_t n = sys3(SYS_ioctl, static_cast<uint32_t>(c.pagemap_fd), PAGEMAP_SCAN, addr_of(&a));
            if (n < 0) {
                if (r == 0 && start == c.scan[r].begin) return false;
                c.report.status = kHarnessError;
                return true;
            }
            for (int32_t i = 0; i < n; ++i) {
                const auto end = static_cast<uint32_t>(c.regions[i].end / kPage);
                for (auto p = static_cast<uint32_t>(c.regions[i].start / kPage); p < end; ++p) {
                    if (p >= own_begin && p < own_end) continue;
                    if (count == kMaxDirty) {
                        c.report.status = kTooManyWrites;
                        return true;
                    }
                    c.dirty[count++] = p;
                }
            }
            if (a.walk_end <= start) {
                c.report.status = kHarnessError;
                return true;
            }
            start = static_cast<uint32_t>(a.walk_end);
        }
    }
    c.report.dirty_count = count;
    return true;
}

// Pages written since the soft-dirty bits were cleared, outside the area itself.
XBCMP_CODE void collect_dirty(ChildArea& c) {
    const uint32_t own_begin = addr_of(&c) / kPage;
    const uint32_t own_end = own_begin + sizeof(ChildArea) / kPage;
    if (collect_dirty_scan(c, own_begin, own_end)) return;
    c.report.scan_pages = 0;
    uint32_t count = 0;
    for (uint32_t r = 0; r < c.scan_count; ++r) {
        const uint32_t end = c.scan[r].end / kPage;
        c.report.scan_pages += end - c.scan[r].begin / kPage;
        for (uint32_t page = c.scan[r].begin / kPage; page < end;) {
            uint32_t n = end - page;
            if (n > kPagemapChunk) n = kPagemapChunk;
            // pagemap: one 64-bit entry per page; bit 55 = soft-dirty.
            const int32_t got =
                sys5(SYS_pread64, static_cast<uint32_t>(c.pagemap_fd), addr_of(c.pagemap), n * 8, page * 8, 0);
            if (got != static_cast<int32_t>(n * 8)) {
                c.report.status = kHarnessError;
                return;
            }
            for (uint32_t i = 0; i < n; ++i) {
                if ((c.pagemap[2 * i + 1] & (1u << 23)) == 0) continue;
                const uint32_t p = page + i;
                if (p >= own_begin && p < own_end) continue;
                if (count == kMaxDirty) {
                    c.report.status = kTooManyWrites;
                    return;
                }
                c.dirty[count++] = p;
            }
            page += n;
        }
    }
    c.report.dirty_count = count;
}

// Report, results and dirty pages; then the pages the parent asks for.
[[noreturn]] XBCMP_CODE void child_send(ChildArea& c) {
    if (c.report.status != kOk) c.report.dirty_count = 0;
    if (write_all(c.out, addr_of(&c.report), sizeof(Report)) &&
        write_all(c.out, addr_of(&c.result), sizeof(Context)) &&
        write_all(c.out, addr_of(c.dirty), c.report.dirty_count * 4) &&
        read_all(c.in, addr_of(&c.wanted_count), 4) && c.wanted_count <= 2 * kMaxDirty &&
        read_all(c.in, addr_of(c.wanted), c.wanted_count * 4)) {
        for (uint32_t i = 0; i < c.wanted_count; ++i)
            if (!write_all(c.out, c.wanted[i] * kPage, kPage)) break;
    }
    if (c.grandchild) sys5(SYS_wait4, c.grandchild, 0, 0, 0, 0);
    for (;;) sys3(SYS_exit_group, 0, 0, 0);
}

extern "C" XBCMP_CODE void xbcmp_child_signal(int signal, siginfo_t* info, void* context) {
    ChildArea& c = xbcmp_child;
    const auto& gregs = static_cast<const ucontext_t*>(context)->uc_mcontext.gregs;
    c.report.status = kSignal;
    c.report.signal = static_cast<uint32_t>(signal);
    c.report.eip = static_cast<uint32_t>(gregs[REG_EIP]);
    c.report.esp = static_cast<uint32_t>(gregs[REG_ESP]);
    c.report.fault_address = addr_of(info->si_addr);
    // After a call into protected host code, the return address into its caller.
    c.report.stack_word = in_scan_range(c, c.report.esp, 4) ? *reinterpret_cast<const uint32_t*>(
                                                                    uintptr_t{c.report.esp})
                                                              : 0;
    child_send(c);
}

extern "C" [[noreturn]] XBCMP_CODE void xbcmp_child_finish() {
    ChildArea& c = xbcmp_child;
    c.report.t_landed = raw_now_ns();
    c.report.status = kOk;
    collect_dirty(c);
    c.report.t_collected = raw_now_ns();
    child_send(c);
}

[[noreturn]] XBCMP_CODE void child_run(ChildArea& c) {
    for (uint32_t i = 0; i < c.protect_count; ++i)
        sys3(SYS_mprotect, c.protect[i].begin, c.protect[i].end - c.protect[i].begin, c.protect[i].protection);
    // Clear the soft-dirty bits: from here on, the pages written are the function's.
    c.clear_command = '4';
    sys3(SYS_write, static_cast<uint32_t>(c.clear_refs_fd), addr_of(&c.clear_command), 1);
    c.report.t_cleared = raw_now_ns();
    xbcmp_resume(&c.start);
}

#undef XBCMP_CODE

// --- Parent side ------------------------------------------------------------------

using game::Function;
using game::Result;

uint32_t addr(const void* pointer) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer)); }

struct Hook {
    const Function* function;
    uint32_t address;
    uint32_t code;        // the decompiled version
    uint32_t trampoline;  // runs the original
    uint8_t original[5];  // the entry bytes the jump replaced
    std::atomic<uint64_t> calls{0};
    Counts total;    // since install
    Counts pending;  // since the last flush (calls: unused)
    Counts earlier;  // earlier runs (from the results file)
    std::atomic<bool> settled{false};  // enough matched tests: no more this run
    uint64_t flushed_calls = 0;
    std::string last_issue;
    uint32_t reports = 0;  // details logged so far
};

Config g_config;
std::vector<std::unique_ptr<Hook>> g_hooks;
std::vector<const Function*> g_replaced;
std::mutex g_busy;   // one test at a time
std::mutex g_stats;  // counts and the results file
std::atomic<uint64_t> g_test_ns{0};
std::atomic<uint64_t> g_tests{0};

// Test processes are reaped here: tearing down their copy of the address space takes
// milliseconds, and the game need not wait for it once it has their reports. Never
// destroyed: exit() would destroy the condition variable the thread waits on (glibc
// blocks there).
struct Reaper {
    std::mutex lock;
    std::condition_variable wake;
    std::vector<pid_t> pids;
};
Reaper* g_reaper;

void reap_later(pid_t pid) {
    {
        std::lock_guard lock(g_reaper->lock);
        g_reaper->pids.push_back(pid);
    }
    g_reaper->wake.notify_one();
}

void reaper(Reaper* r) {
    for (;;) {
        std::vector<pid_t> pids;
        {
            std::unique_lock lock(r->lock);
            r->wake.wait(lock, [r] { return !r->pids.empty(); });
            pids.swap(r->pids);
        }
        for (const pid_t pid : pids) waitpid(pid, nullptr, 0);
    }
}

uint64_t now_ns() {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<uint64_t>(t.tv_sec) * 1000000000u + static_cast<uint64_t>(t.tv_nsec);
}

// Where a test's time goes (completed tests only), logged by flush().
enum Phase { kFork, kStartOriginal, kStartNew, kMaps, kClear, kRun, kCollect, kReports, kPages, kReap, kCompare,
             kTotal, kPhases };
constexpr const char* kPhaseNames[kPhases] = {"fork", "start-orig", "start-new", "maps", "protect+clear", "run",
                                             "collect", "reports", "pages", "reap", "compare", "total"};
struct Timing {
    uint64_t tests = 0;
    uint64_t sum[kPhases] = {}, max[kPhases] = {};
    uint64_t scan_pages = 0, dirty_pages = 0;
};
Timing g_timing;  // under g_busy (one test at a time)

void add_ns(Phase phase, uint64_t ns) {
    g_timing.sum[phase] += ns;
    g_timing.max[phase] = std::max(g_timing.max[phase], ns);
}

uint64_t span(uint64_t from, uint64_t to) { return from && to > from ? to - from : 0; }

void add_phase(Phase phase, uint64_t from, uint64_t to) { add_ns(phase, span(from, to)); }

// Length of the instruction at `code` if it can run from a trampoline (no relative
// operands), else 0. Covers what MSVC puts at function entries.
uint32_t instruction_length(const uint8_t* code) {
    uint32_t prefix = 0;
    bool operand16 = false;
    for (;; ++prefix) {
        const uint8_t b = code[prefix];
        if (b == 0x66)
            operand16 = true;
        else if (b != 0x26 && b != 0x2E && b != 0x36 && b != 0x3E && b != 0x64 && b != 0x65 && b != 0xF2 && b != 0xF3)
            break;
    }
    const uint8_t* p = code + prefix;
    const uint8_t op = p[0];
    const uint32_t imm = operand16 ? 2 : 4;
    const auto modrm = [](const uint8_t* m) -> uint32_t {  // ModRM + SIB + displacement
        const uint32_t mod = m[0] >> 6, rm = m[0] & 7;
        if (mod == 3) return 1;
        uint32_t n = 1;
        if (rm == 4) {
            ++n;
            if (mod == 0 && (m[1] & 7) == 5) n += 4;
        } else if (mod == 0 && rm == 5) {
            n += 4;
        }
        if (mod == 1) n += 1;
        if (mod == 2) n += 4;
        return n;
    };
    if ((op >= 0x40 && op <= 0x5F) || op == 0x90 || op == 0x99 || op == 0x9C || op == 0x9D || op == 0xC9 ||
        op == 0xCC || op == 0xFC)
        return prefix + 1;
    if ((op & 0xF8) == 0xB0 || op == 0x6A || op == 0xA8 || (op < 0x40 && (op & 7) == 4)) return prefix + 2;
    if ((op & 0xF8) == 0xB8 || op == 0x68 || op == 0xA9 || (op < 0x40 && (op & 7) == 5)) return prefix + 1 + imm;
    if (op >= 0xA0 && op <= 0xA3) return prefix + 5;
    if (op < 0x40 && (op & 7) < 4) return prefix + 1 + modrm(p + 1);
    if ((op >= 0x84 && op <= 0x8B) || op == 0x8D || (op >= 0xD0 && op <= 0xD3) || (op >= 0xD8 && op <= 0xDF))
        return prefix + 1 + modrm(p + 1);
    if (op == 0x80 || op == 0x83 || op == 0xC0 || op == 0xC1 || op == 0xC6 || op == 0x6B)
        return prefix + 2 + modrm(p + 1);
    if (op == 0x81 || op == 0xC7 || op == 0x69) return prefix + 1 + modrm(p + 1) + imm;
    if (op == 0xF6 || op == 0xF7) {
        const uint32_t reg = (p[1] >> 3) & 7;
        return prefix + 1 + modrm(p + 1) + (reg < 2 ? (op == 0xF6 ? 1 : imm) : 0);
    }
    if (op == 0xFF) {
        const uint32_t reg = (p[1] >> 3) & 7;
        return reg == 0 || reg == 1 || reg == 6 ? prefix + 1 + modrm(p + 1) : 0;  // not call/jmp
    }
    if (op == 0x0F) {
        const uint8_t op2 = p[1];
        if (op2 == 0xB6 || op2 == 0xB7 || op2 == 0xBE || op2 == 0xBF || op2 == 0xAF || (op2 >= 0x40 && op2 <= 0x4F) ||
            op2 == 0x10 || op2 == 0x11 || op2 == 0x28 || op2 == 0x29 || op2 == 0x57)
            return prefix + 2 + modrm(p + 2);
    }
    return 0;
}

void put_rel32(uint8_t* at, uint8_t opcode, uint32_t target) {
    at[0] = opcode;
    const uint32_t relative = target - (addr(at) + 5);
    std::memcpy(at + 1, &relative, 4);
}

// A conditional branch (short or near) at `code`: its length and target, so that
// the trampoline can re-encode it as a near branch. False for anything else.
bool conditional_branch(const uint8_t* code, uint32_t& length, uint32_t& target) {
    if (code[0] >= 0x70 && code[0] <= 0x7F) {
        length = 2;
        target = addr(code) + 2 + static_cast<uint32_t>(static_cast<int8_t>(code[1]));
        return true;
    }
    if (code[0] == 0x0F && code[1] >= 0x80 && code[1] <= 0x8F) {
        int32_t relative;
        std::memcpy(&relative, code + 2, 4);
        length = 6;
        target = addr(code) + 6 + static_cast<uint32_t>(relative);
        return true;
    }
    return false;
}

// Copies the entry instructions [entry, entry + moved) into `out`, re-encoding
// conditional branches as near branches to the same targets. Returns the bytes
// written.
uint32_t copy_entry(uint8_t* out, const uint8_t* entry, uint32_t moved) {
    uint32_t in = 0, written = 0;
    while (in < moved) {
        uint32_t length, target;
        if (conditional_branch(entry + in, length, target)) {
            const uint8_t condition = entry[in] == 0x0F ? entry[in + 1] & 0x0F : entry[in] & 0x0F;
            out[written] = 0x0F;
            put_rel32(out + written + 1, static_cast<uint8_t>(0x80 | condition), target);
            written += 6;
        } else {
            length = instruction_length(entry + in);
            std::memcpy(out + written, entry + in, length);
            written += length;
        }
        in += length;
    }
    return written;
}

std::string describe(uint32_t address) {
    if (g_config.describe) return g_config.describe(address);
    char text[16];
    std::snprintf(text, sizeof(text), "%08x", address);
    return text;
}

// --- Test process, before the mprotect (the C runtime still works) ----------------

bool contains(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

bool prepare_maps(ChildArea& c) {
    std::string maps;
    const int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    char buffer[4096];
    for (ssize_t n; (n = read(fd, buffer, sizeof(buffer))) > 0;) maps.append(buffer, static_cast<size_t>(n));
    close(fd);

    const uint32_t section_begin = addr(__start_xbcmp_text) & ~(kPage - 1);
    const uint32_t section_end = (addr(__stop_xbcmp_text) + kPage - 1) & ~(kPage - 1);
    c.scan_count = c.protect_count = 0;
    std::istringstream lines(maps);
    for (std::string line; std::getline(lines, line);) {
        unsigned long begin = 0, end = 0;
        char perms[8] = {};
        int name_at = 0;
        if (std::sscanf(line.c_str(), "%lx-%lx %7s %*s %*s %*s %n", &begin, &end, perms, &name_at) < 3) continue;
        const std::string name = name_at > 0 ? line.substr(static_cast<size_t>(name_at)) : "";
        const auto b = static_cast<uint32_t>(begin);
        const auto e = static_cast<uint32_t>(end);
        const bool writable = perms[1] == 'w';
        const bool anonymous = name.empty() || name.starts_with("[heap") || name.starts_with("[stack") ||
                               name.starts_with("[anon");
        if (writable && perms[3] == 'p' && anonymous) {
            if (c.scan_count == kMaxRanges) return false;
            c.scan[c.scan_count++] = {b, e, 0};
        }
        if (perms[2] != 'x') continue;
        if (b < g_config.image_end && e > g_config.image_begin) continue;  // the title's code
        if (name == "[vdso]" || name == "[vsyscall]") continue;
        bool allowed = false;
        for (const std::string& library : g_config.libraries) allowed |= contains(name, library.c_str());
        if (allowed) continue;
        const uint32_t protection = PROT_READ | (writable ? PROT_WRITE : 0);
        const auto add = [&](uint32_t from, uint32_t to) {
            if (from < to && c.protect_count < kMaxRanges) c.protect[c.protect_count++] = {from, to, protection};
        };
        if (section_begin < e && section_end > b) {
            add(b, section_begin);
            add(section_end, e);
        } else {
            add(b, e);
        }
        if (c.protect_count == kMaxRanges) return false;
    }
    return true;
}

// `unused`: the other pipe ends this process inherited. They must be closed: a pipe
// whose write end is still open somewhere never reports EOF, and a test process
// waiting on it outlives the game (holding its window open).
[[noreturn]] void child_main(bool run_original, int out, int in, pid_t grandchild, std::initializer_list<int> unused,
                             const Hook& hook, const Context& context) {
    ChildArea& c = xbcmp_child;
    for (int fd : unused) close(fd);
    // Gone with the process that forked it, and in any case after a while.
    const pid_t parent = getppid();
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    if (getppid() != parent) _exit(1);
    alarm(g_config.timeout_ms / 1000 * 2 + 5);
    xbcmp_in_test = 1;
    c.out = out;
    c.in = in;
    c.grandchild = static_cast<uint32_t>(grandchild > 0 ? grandchild : 0);
    c.report = {};
    c.report.t_entered = now_ns();
    const uint32_t pid = static_cast<uint32_t>(getpid());
    if (!write_all(out, addr(&pid), 4)) _exit(1);

    stack_t stack{};
    stack.ss_sp = c.signal_stack;
    stack.ss_size = sizeof(c.signal_stack);
    sigaltstack(&stack, nullptr);
    struct sigaction action {};
    action.sa_sigaction = xbcmp_child_signal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    sigset_t faults;
    sigemptyset(&faults);
    for (int signal : {SIGSEGV, SIGILL, SIGFPE, SIGBUS, SIGTRAP}) {
        sigaction(signal, &action, nullptr);
        sigaddset(&faults, signal);
    }
    sigprocmask(SIG_UNBLOCK, &faults, nullptr);

    // Calls of other tested functions (and of this one, recursively) run the originals.
    for (const auto& other : g_hooks)
        std::memcpy(reinterpret_cast<void*>(uintptr_t{other->address}), other->original, 5);

    c.pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
    c.clear_refs_fd = open("/proc/self/clear_refs", O_WRONLY);
    if (c.pagemap_fd < 0 || c.clear_refs_fd < 0 || !prepare_maps(c)) {
        c.report.status = kHarnessError;
        child_send(c);
    }
    c.report.t_mapped = now_ns();
    std::memcpy(&c.start, &context, sizeof(Context));
    c.start.target = run_original ? hook.address : hook.code;
    *reinterpret_cast<uint32_t*>(uintptr_t{c.start.esp}) = addr(reinterpret_cast<const void*>(&xbcmp_landing));
    child_run(c);
}

// --- Parent: collecting and comparing ---------------------------------------------

using Clock = std::chrono::steady_clock;

bool read_until(int fd, void* data, size_t size, Clock::time_point deadline) {
    auto* p = static_cast<uint8_t*>(data);
    while (size > 0) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (left <= 0) return false;
        pollfd poll_fd{fd, POLLIN, 0};
        const int ready = poll(&poll_fd, 1, static_cast<int>(left));
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) return false;
        const ssize_t got = read(fd, p, size);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return false;
        p += got;
        size -= static_cast<size_t>(got);
    }
    return true;
}

bool write_full(int fd, const void* data, size_t size) {
    const auto* p = static_cast<const uint8_t*>(data);
    while (size > 0) {
        const ssize_t written = write(fd, p, size);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        p += written;
        size -= static_cast<size_t>(written);
    }
    return true;
}

struct Side {
    const char* what;  // "original" / "new"
    int from = -1, to = -1;
    uint32_t pid = 0;
    bool answered = false;  // report received
    Report report{};
    Context result{};
    std::vector<uint32_t> dirty;
    std::vector<uint8_t> pages;
};

bool receive_report(Side& side, Clock::time_point deadline) {
    if (!read_until(side.from, &side.report, sizeof(Report), deadline) ||
        !read_until(side.from, &side.result, sizeof(Context), deadline))
        return false;
    if (side.report.status == kOk) {
        if (side.report.dirty_count > kMaxDirty) return false;
        side.dirty.resize(side.report.dirty_count);
        if (!read_until(side.from, side.dirty.data(), side.dirty.size() * 4, deadline)) return false;
    }
    side.answered = true;
    return true;
}

enum class Outcome { Matched, Drift, Mismatched, Inconclusive };

struct Verdict {
    Outcome outcome = Outcome::Matched;
    std::string details;
    void add(Outcome o, const std::string& text) {
        outcome = std::max(outcome, o);
        if (text.empty()) return;
        if (!details.empty()) details += "; ";
        details += text;
    }
};

std::string hex(uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "%08x", value);
    return text;
}

std::string signal_text(const Side& side) {
    const Report& r = side.report;
    if (r.status == kTooManyWrites) return std::string(side.what) + " wrote too much memory";
    if (r.status == kHarnessError) return std::string(side.what) + ": harness error";
    const bool host_call = r.signal == SIGSEGV && r.fault_address == r.eip &&
                           !(r.eip >= g_config.image_begin && r.eip < g_config.image_end);
    if (host_call)
        return std::string(side.what) + " called host code at " + hex(r.eip) + " from " + describe(r.stack_word);
    return std::string(side.what) + " got " + strsignal(static_cast<int>(r.signal)) + " at " + describe(r.eip) +
           ", address " + hex(r.fault_address);
}

bool is_host_call(const Report& r) {
    return r.status == kSignal && r.signal == SIGSEGV && r.fault_address == r.eip &&
           !(r.eip >= g_config.image_begin && r.eip < g_config.image_end);
}

// Two different floats that are a few bits apart (x87 vs SSE rounding, a different
// operation order). Normal numbers only: small integers read as floats are
// denormals, and pointers often look like tiny or huge ones.
bool floats_close(float a, float b) {
    if (std::fpclassify(a) != FP_NORMAL || std::fpclassify(b) != FP_NORMAL || (a < 0) != (b < 0)) return false;
    const float scale = std::max(std::fabs(a), std::fabs(b));
    return scale > 1e-20f && scale < 1e20f && std::fabs(a - b) <= scale * 1e-5f;
}

// x87 state from FXSAVE: stack depth and ST(i) (FXSAVE stores them in stack order).
uint32_t x87_depth(const Context& c) { return static_cast<uint32_t>(__builtin_popcount(c.fx[4])); }
long double x87_st(const Context& c, int i) {
    long double value = 0;
    std::memcpy(&value, c.fx + 32 + 16 * i, 10);
    return value;
}
uint16_t x87_control(const Context& c) { return static_cast<uint16_t>(c.fx[0] | c.fx[1] << 8); }
uint32_t mxcsr(const Context& c) {
    uint32_t value;
    std::memcpy(&value, c.fx + 24, 4);
    return value;
}

void compare_registers(const Hook& hook, const Context& entry, const Context& a, const Context& b, Verdict& v) {
    const auto reg = [&](const char* name, uint32_t x, uint32_t y) {
        if (x != y) v.add(Outcome::Mismatched, std::string(name) + " " + hex(x) + " vs " + hex(y));
    };
    switch (hook.function->result) {
    case Result::None:
    case Result::St0:
        break;
    case Result::Al:
        reg("al", a.eax & 0xFF, b.eax & 0xFF);
        break;
    case Result::Eax:
        reg("eax", a.eax, b.eax);
        break;
    case Result::EdxEax:
        reg("eax", a.eax, b.eax);
        reg("edx", a.edx, b.edx);
        break;
    }
    reg("ebx", a.ebx, b.ebx);
    reg("esi", a.esi, b.esi);
    reg("edi", a.edi, b.edi);
    reg("ebp", a.ebp, b.ebp);
    if (a.esp != b.esp)
        v.add(Outcome::Mismatched, "esp after return +" + std::to_string(a.esp - entry.esp) + " vs +" +
                                       std::to_string(b.esp - entry.esp) + " (calling convention)");
    reg("eflags.DF", a.eflags & 0x400, b.eflags & 0x400);
    reg("x87 control", x87_control(a), x87_control(b));
    reg("mxcsr", mxcsr(a), mxcsr(b));
    const uint32_t depth = x87_depth(a);
    if (depth != x87_depth(b)) {
        v.add(Outcome::Mismatched, "x87 stack depth " + std::to_string(depth) + " vs " + std::to_string(x87_depth(b)));
        return;
    }
    for (uint32_t i = 0; i < depth; ++i) {
        if (std::memcmp(a.fx + 32 + 16 * i, b.fx + 32 + 16 * i, 10) == 0) continue;
        const long double x = x87_st(a, static_cast<int>(i)), y = x87_st(b, static_cast<int>(i));
        char text[96];
        std::snprintf(text, sizeof(text), "st(%u) %.9Lg vs %.9Lg", i, x, y);
        const bool close = std::isfinite(x) && std::isfinite(y) &&
                           std::fabs(x - y) <= std::max(std::fabs(x), std::fabs(y)) * 1e-5L;
        v.add(close ? Outcome::Drift : Outcome::Mismatched, text);
    }
}

std::string bytes_text(const uint8_t* data, uint32_t size) {
    std::string text;
    char byte[4];
    for (uint32_t i = 0; i < std::min<uint32_t>(size, 16); ++i) {
        std::snprintf(byte, sizeof(byte), "%02x", data[i]);
        text += byte;
    }
    if (size > 16) text += "..";
    return text;
}

// Memory the comparison skips.
struct Skip {
    uint32_t begin, end;
    bool contains(uint32_t address) const { return address >= begin && address < end; }
};

// The calling thread's C runtime block: the thread descriptor at the thread pointer,
// to the end of its mapping (it records the thread id, which differs between the
// test processes), and the rseq area below it (the kernel writes the current CPU
// there). Title code never owns this memory.
std::vector<Skip> thread_runtime_block() {
    uint32_t tp;
    asm("movl %%gs:0, %0" : "=r"(tp));
    std::vector<Skip> skips;
    if (std::ifstream maps("/proc/self/maps"); maps) {
        for (std::string line; std::getline(maps, line);) {
            unsigned long begin = 0, end = 0;
            if (std::sscanf(line.c_str(), "%lx-%lx", &begin, &end) == 2 && tp >= begin && tp < end) {
                skips.push_back({tp, static_cast<uint32_t>(end)});
                break;
            }
        }
    }
    if (skips.empty()) skips.push_back({tp, tp + 0x1000});
    // glibc 2.35+ (sys/rseq.h); looked up so that older C runtimes work too.
    if (const auto* offset = static_cast<const ptrdiff_t*>(dlsym(RTLD_DEFAULT, "__rseq_offset"))) {
        const auto* size = static_cast<const unsigned int*>(dlsym(RTLD_DEFAULT, "__rseq_size"));
        const uint32_t rseq = tp + static_cast<uint32_t>(*offset);
        skips.push_back({rseq, rseq + std::max(32u, size ? *size : 0u)});
    }
    return skips;
}

thread_local std::vector<Skip> t_runtime_skips;

void compare_memory(const Hook& hook, const Context& entry, const std::vector<uint32_t>& pages, const Side& a,
                    const Side& b, Verdict& v) {
    // Below the caller's stack pointer is the callee's scratch space; the return
    // address and the arguments belong to the callee too.
    const uint32_t limit = g_config.stack_limit ? g_config.stack_limit() : 0;
    std::vector<Skip> skips = t_runtime_skips;
    skips.push_back({limit ? limit : entry.esp - std::min<uint32_t>(entry.esp, 0x10000),
                     entry.esp + 4 + hook.function->arg_bytes});
    const auto skipped = [&](uint32_t address) {
        for (const Skip& skip : skips)
            if (skip.contains(address)) return true;
        return false;
    };
    int shown = 0, hidden = 0;
    for (size_t i = 0; i < pages.size(); ++i) {
        const uint8_t* x = a.pages.data() + i * kPage;
        const uint8_t* y = b.pages.data() + i * kPage;
        if (std::memcmp(x, y, kPage) == 0) continue;
        const uint32_t base = pages[i] * kPage;
        for (uint32_t offset = 0; offset < kPage;) {
            const uint32_t address = base + offset;
            if (x[offset] == y[offset] || skipped(address)) {
                ++offset;
                continue;
            }
            // A run of differences (gaps under 4 bytes joined).
            uint32_t end = offset + 1;
            for (uint32_t gap = 0; end < kPage && gap < 4; ++end) {
                if (x[end] != y[end] && !skipped(base + end))
                    gap = 0;
                else
                    ++gap;
            }
            while (end > offset + 1 && x[end - 1] == y[end - 1]) --end;
            // Only aligned floats a few bits apart: drift.
            bool drift = true;
            for (uint32_t word = offset & ~3u; word < end && drift; word += 4) {
                if (word + 4 > kPage) {
                    drift = false;
                    break;
                }
                float f, g;
                std::memcpy(&f, x + word, 4);
                std::memcpy(&g, y + word, 4);
                drift = std::memcmp(&f, &g, 4) == 0 || floats_close(f, g);
            }
            if (shown < 6) {
                v.add(drift ? Outcome::Drift : Outcome::Mismatched,
                      describe(address) + " [" + std::to_string(end - offset) + "] " + bytes_text(x + offset, end - offset) +
                          " vs " + bytes_text(y + offset, end - offset));
                ++shown;
            } else {
                v.add(drift ? Outcome::Drift : Outcome::Mismatched, "");
                ++hidden;
            }
            offset = end;
        }
    }
    if (hidden) v.details += " (+" + std::to_string(hidden) + " more)";
}

Verdict run_test(Hook& hook, const Context& entry) {
    Verdict verdict;
    if (t_runtime_skips.empty()) t_runtime_skips = thread_runtime_block();
    int a_out[2], a_in[2], b_out[2], b_in[2];
    if (pipe2(a_out, O_CLOEXEC) || pipe2(a_in, O_CLOEXEC) || pipe2(b_out, O_CLOEXEC) || pipe2(b_in, O_CLOEXEC)) {
        verdict.add(Outcome::Inconclusive, "no pipes");
        return verdict;
    }
    const uint64_t t_fork = now_ns();
    const pid_t child = fork();
    if (child == 0) {
        // Same memory for both: fork the second test process before anything runs.
        const pid_t grandchild = fork();
        if (grandchild == 0) child_main(false, b_out[1], b_in[0], 0, {a_out[0], a_out[1], a_in[0], a_in[1], b_out[0], b_in[1]}, hook, entry);
        child_main(true, a_out[1], a_in[0], grandchild, {a_out[0], a_in[1], b_out[0], b_out[1], b_in[0], b_in[1]}, hook,
                   entry);
    }
    const uint64_t t_forked = now_ns();
    for (int fd : {a_out[1], a_in[0], b_out[1], b_in[0]}) close(fd);
    Side sides[2];
    sides[0].what = "original";
    sides[0].from = a_out[0];
    sides[0].to = a_in[1];
    sides[1].what = "new";
    sides[1].from = b_out[0];
    sides[1].to = b_in[1];
    const auto finish = [&] {
        for (Side& side : sides) {
            close(side.from);
            close(side.to);
        }
        if (child > 0) reap_later(child);
    };
    if (child < 0) {
        finish();
        verdict.add(Outcome::Inconclusive, "fork failed");
        return verdict;
    }

    const auto deadline = Clock::now() + std::chrono::milliseconds(g_config.timeout_ms);
    bool complete = true;
    for (Side& side : sides) complete &= read_until(side.from, &side.pid, 4, deadline);
    for (Side& side : sides) complete &= complete && receive_report(side, deadline);
    const uint64_t t_reports = now_ns();
    if (!complete) {
        // A test process that hangs (a wait for another thread, which does not exist
        // there) or died without a report.
        for (Side& side : sides) {
            if (side.pid) kill(static_cast<pid_t>(side.pid), SIGKILL);
            if (!side.answered) verdict.add(Outcome::Inconclusive, std::string(side.what) + " timed out");
        }
        finish();
        if (verdict.details.empty()) verdict.add(Outcome::Inconclusive, "test process failed");
        return verdict;
    }

    const Side& a = sides[0];
    const Side& b = sides[1];
    std::vector<uint32_t> pages;
    if (a.report.status == kOk && b.report.status == kOk) {
        pages = a.dirty;
        pages.insert(pages.end(), b.dirty.begin(), b.dirty.end());
        std::sort(pages.begin(), pages.end());
        pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
    }
    const auto count = static_cast<uint32_t>(pages.size());
    for (Side& side : sides) {
        complete &= write_full(side.to, &count, 4) && write_full(side.to, pages.data(), pages.size() * 4);
        close(side.to);
        side.to = -1;
    }
    for (Side& side : sides) {
        side.pages.resize(pages.size() * kPage);
        complete &= complete && read_until(side.from, side.pages.data(), side.pages.size(), deadline);
    }
    const uint64_t t_pages = now_ns();
    finish();
    const uint64_t t_reaped = now_ns();

    if (a.report.status != kOk) {
        verdict.add(Outcome::Inconclusive, signal_text(a));
        return verdict;
    }
    if (b.report.status != kOk) {
        // The original ran through; the new version reaching host code the original
        // did not need is inconclusive too (e.g. a C runtime call we do not allow).
        verdict.add(is_host_call(b.report) || b.report.status == kHarnessError ? Outcome::Inconclusive
                                                                                : Outcome::Mismatched,
                    signal_text(b));
        return verdict;
    }
    if (!complete) {
        verdict.add(Outcome::Inconclusive, "pages not received");
        return verdict;
    }
    compare_registers(hook, entry, a.result, b.result, verdict);
    compare_memory(hook, entry, pages, a, b, verdict);
    const uint64_t t_compared = now_ns();
    const Report& ra = a.report;
    const Report& rb = b.report;
    ++g_timing.tests;
    add_phase(kFork, t_fork, t_forked);
    add_phase(kStartOriginal, t_fork, ra.t_entered);
    add_phase(kStartNew, t_fork, rb.t_entered);  // includes the second fork
    // The two sides run side by side: the slower one counts.
    add_ns(kMaps, std::max(span(ra.t_entered, ra.t_mapped), span(rb.t_entered, rb.t_mapped)));
    add_ns(kClear, std::max(span(ra.t_mapped, ra.t_cleared), span(rb.t_mapped, rb.t_cleared)));
    add_ns(kRun, std::max(span(ra.t_cleared, ra.t_landed), span(rb.t_cleared, rb.t_landed)));
    add_ns(kCollect, std::max(span(ra.t_landed, ra.t_collected), span(rb.t_landed, rb.t_collected)));
    add_phase(kReports, std::max(ra.t_collected, rb.t_collected), t_reports);
    add_phase(kPages, t_reports, t_pages);
    add_phase(kReap, t_pages, t_reaped);
    add_phase(kCompare, t_reaped, t_compared);
    add_phase(kTotal, t_fork, t_compared);
    g_timing.scan_pages += ra.scan_pages;
    g_timing.dirty_pages += pages.size();
    return verdict;
}

bool should_test(uint64_t call) {
    return call < g_config.first_calls || (g_config.every && call % g_config.every == 0);
}

void record(Hook& hook, uint64_t call, const Verdict& verdict) {
    std::lock_guard lock(g_stats);
    for (Counts* counts : {&hook.total, &hook.pending}) {
        ++counts->tested;
        switch (verdict.outcome) {
        case Outcome::Matched: ++counts->matched; break;
        case Outcome::Drift: ++counts->drift; break;
        case Outcome::Mismatched: ++counts->mismatched; break;
        case Outcome::Inconclusive: ++counts->inconclusive; break;
        }
    }
    const Counts& e = hook.earlier;
    const Counts& t = hook.total;
    if (g_config.enough_matches && e.matched + t.matched >= g_config.enough_matches &&
        e.mismatched + t.mismatched + e.drift + t.drift == 0 && !hook.settled) {
        hook.settled = true;
        LOG_INFO("compare", "%s: %llu matched tests, none mismatched; not tested any more", hook.function->name,
                 static_cast<unsigned long long>(e.matched + t.matched));
    }
    if (verdict.outcome == Outcome::Matched) return;
    hook.last_issue = verdict.details;
    if (hook.reports >= 5) return;
    ++hook.reports;
    static const char* const kWhat[] = {"matched", "float drift", "MISMATCH", "inconclusive"};
    const auto level = verdict.outcome == Outcome::Mismatched ? platform::LogLevel::Warn : platform::LogLevel::Info;
    PLATFORM_LOG(level, "compare", "%s call %llu: %s: %s", hook.function->name, static_cast<unsigned long long>(call),
                 kWhat[static_cast<int>(verdict.outcome)], verdict.details.c_str());
}

thread_local Context* t_context;

}  // namespace
}  // namespace loader::compare

extern "C" [[noreturn]] void xbcmp_dispatch(EntryFrame* frame) {
    using namespace loader::compare;
    if (!t_context) t_context = new Context{};
    Context& context = *t_context;
    asm volatile("fxsave %0" : "=m"(context.fx));
    context.eax = frame->eax;
    context.ecx = frame->ecx;
    context.edx = frame->edx;
    context.ebx = frame->ebx;
    context.ebp = frame->ebp;
    context.esi = frame->esi;
    context.edi = frame->edi;
    context.eflags = frame->eflags;
    context.esp = addr(&frame->return_address);
    Hook& hook = *g_hooks[frame->index];
    context.target = hook.trampoline;

    const uint64_t call = hook.calls.fetch_add(1, std::memory_order_relaxed);
    if (!hook.settled.load(std::memory_order_relaxed) && should_test(call) && g_busy.try_lock()) {
        {
            platform::HostFloatEnv host_float;
            const auto start = Clock::now();
            const Verdict verdict = run_test(hook, context);
            g_test_ns += static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
            const uint64_t tests = ++g_tests;
            record(hook, call, verdict);
            if (tests % 64 == 0) flush();
        }
        g_busy.unlock();
    }
    xbcmp_resume(&context);
}

namespace loader::compare {

namespace {

struct Row {
    std::string name, status;
    uint64_t calls = 0, tested = 0, matched = 0, drift = 0, mismatched = 0, inconclusive = 0;
    std::string last_issue;
};

constexpr const char* kHeader = "address,name,status,calls,tested,matched,drift,mismatched,inconclusive,last_issue";

std::string csv_field(const std::string& text) {
    std::string out = "\"";
    for (char ch : text) out += ch == '"' ? std::string("\"\"") : std::string(1, ch);
    return out + "\"";
}

// Fields of one CSV line (quoted fields may contain commas and doubled quotes).
std::vector<std::string> csv_split(const std::string& line) {
    std::vector<std::string> fields(1);
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (quoted) {
            if (ch == '"' && i + 1 < line.size() && line[i + 1] == '"') {
                fields.back() += '"';
                ++i;
            } else if (ch == '"') {
                quoted = false;
            } else {
                fields.back() += ch;
            }
        } else if (ch == '"') {
            quoted = true;
        } else if (ch == ',') {
            fields.emplace_back();
        } else {
            fields.back() += ch;
        }
    }
    return fields;
}

std::map<uint32_t, Row> read_rows(const std::filesystem::path& path) {
    std::map<uint32_t, Row> rows;
    if (path.empty()) return rows;
    std::ifstream in(path);
    std::string line;
    std::getline(in, line);  // header
    while (std::getline(in, line)) {
        const std::vector<std::string> f = csv_split(line);
        if (f.size() < 10) continue;
        Row row{f[1], f[2], std::stoull(f[3]), std::stoull(f[4]), std::stoull(f[5]),
                std::stoull(f[6]), std::stoull(f[7]), std::stoull(f[8]), f[9]};
        rows[static_cast<uint32_t>(std::stoul(f[0], nullptr, 16))] = row;
    }
    return rows;
}

}  // namespace

void install(std::span<const game::Function> functions, const Config& config) {
    g_config = config;
    xbcmp_child_stack_top = addr(xbcmp_child.stack + sizeof(xbcmp_child.stack));
    int replaced = 0, settled = 0;
    const std::map<uint32_t, Row> earlier =
        config.enabled && config.enough_matches ? read_rows(config.results) : std::map<uint32_t, Row>{};
    for (const game::Function& function : functions) {
        const uint32_t address = config.multiplayer ? function.multiplayer : function.campaign;
        if (!address) continue;
        if (function.status == game::Status::Replaced) {
            install_jump(address, function.code);
            g_replaced.push_back(&function);
            ++replaced;
            continue;
        }
        if (!config.enabled) continue;
        Counts before;
        if (const auto row = earlier.find(address); row != earlier.end() && row->second.name == function.name) {
            const Row& r = row->second;
            before = {r.calls, r.tested, r.matched, r.drift, r.mismatched, r.inconclusive};
            if (r.matched >= config.enough_matches && r.mismatched == 0 && r.drift == 0) {
                ++settled;
                continue;
            }
        }

        auto* entry = reinterpret_cast<uint8_t*>(uintptr_t{address});
        uint32_t moved = 0;
        bool movable = true;
        while (moved < 5) {
            uint32_t length = instruction_length(entry + moved), target = 0;
            // A conditional branch moves too, unless it jumps back into the bytes
            // the hook overwrites.
            if (!length && (!conditional_branch(entry + moved, length, target) ||
                            (target >= address && target < address + 5))) {
                movable = false;
                break;
            }
            moved += length;
        }
        if (!movable) {
            LOG_WARN("compare", "%s: cannot move the instruction at %08x; not tested", function.name, address + moved);
            continue;
        }
        uint8_t* code = alloc_code(moved * 3 + 5 + 32);  // a 2-byte branch becomes 6
        if (!code) continue;
        auto hook = std::make_unique<Hook>();
        hook->function = &function;
        hook->earlier = before;
        hook->address = address;
        hook->code = addr(function.code);
        hook->trampoline = addr(code);
        std::memcpy(hook->original, entry, 5);
        const uint32_t copied = copy_entry(code, entry, moved);
        put_rel32(code + copied, 0xE9, address + moved);
        uint8_t* stub = code + copied + 5;
        stub[0] = 0x80;  // cmp byte [xbcmp_in_test], 0
        stub[1] = 0x3D;
        const uint32_t flag = addr(&xbcmp_in_test);
        std::memcpy(stub + 2, &flag, 4);
        stub[6] = 0;
        stub[7] = 0x0F;  // jne trampoline
        stub[8] = 0x85;
        const int32_t back = static_cast<int32_t>(addr(code) - (addr(stub) + 13));
        std::memcpy(stub + 9, &back, 4);
        stub[13] = 0x68;  // push index
        const auto index = static_cast<uint32_t>(g_hooks.size());
        std::memcpy(stub + 14, &index, 4);
        put_rel32(stub + 18, 0xE9, addr(reinterpret_cast<const void*>(&xbcmp_entry)));
        install_jump(address, stub);
        g_hooks.push_back(std::move(hook));
    }
    if (config.enabled) {
        // A test process that dies while we write to it must not take the game along.
        signal(SIGPIPE, SIG_IGN);
        if (!g_reaper) {
            g_reaper = new Reaper;
            std::thread(reaper, g_reaper).detach();
        }
        LOG_INFO("compare",
                 "testing %zu decompiled functions against the originals; %d settled (%u+ matched tests, "
                 "--compare-all tests them too); %d replaced",
                 g_hooks.size(), settled, config.enough_matches, replaced);
    } else if (replaced) {
        LOG_INFO("compare", "%d decompiled functions replace the originals", replaced);
    }
}

Counts counts(const char* name) {
    std::lock_guard lock(g_stats);
    for (const auto& hook : g_hooks)
        if (std::strcmp(hook->function->name, name) == 0) {
            Counts c = hook->total;
            c.calls = hook->calls.load();
            return c;
        }
    return {};
}


void flush() {
    std::lock_guard lock(g_stats);
    if (g_config.results.empty() || (g_hooks.empty() && g_replaced.empty())) return;
    std::map<uint32_t, Row> rows = read_rows(g_config.results);
    for (const auto& hook : g_hooks) {
        Row& row = rows[hook->address];
        row.name = hook->function->name;
        row.status = "compare";
        const uint64_t calls = hook->calls.load();
        row.calls += calls - hook->flushed_calls;
        hook->flushed_calls = calls;
        row.tested += hook->pending.tested;
        row.matched += hook->pending.matched;
        row.drift += hook->pending.drift;
        row.mismatched += hook->pending.mismatched;
        row.inconclusive += hook->pending.inconclusive;
        if (!hook->last_issue.empty()) row.last_issue = hook->last_issue;
        hook->pending = {};
    }
    for (const game::Function* function : g_replaced) {
        Row& row = rows[g_config.multiplayer ? function->multiplayer : function->campaign];
        row.name = function->name;
        row.status = "replaced";
    }
    const std::filesystem::path temporary = g_config.results.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::trunc);
        out << kHeader << '\n';
        for (const auto& [address, row] : rows) {
            char text[16];
            std::snprintf(text, sizeof(text), "%08x", address);
            out << text << ',' << row.name << ',' << row.status << ',' << row.calls << ',' << row.tested << ','
                << row.matched << ',' << row.drift << ',' << row.mismatched << ',' << row.inconclusive << ','
                << csv_field(row.last_issue) << '\n';
        }
        if (!out) {
            LOG_WARN("compare", "cannot write %s", temporary.string().c_str());
            return;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temporary, g_config.results, ec);
    const uint64_t tests = g_tests.load();
    if (tests)
        LOG_INFO("compare", "%llu tests, %.1f ms each; counts in %s", static_cast<unsigned long long>(tests),
                 static_cast<double>(g_test_ns.load()) / 1e6 / static_cast<double>(tests),
                 g_config.results.string().c_str());
    const Timing& t = g_timing;
    if (t.tests) {
        std::string line;
        char part[96];
        for (int p = 0; p < kPhases; ++p) {
            std::snprintf(part, sizeof(part), " %s %.2f", kPhaseNames[p],
                          static_cast<double>(t.sum[p]) / 1e6 / static_cast<double>(t.tests));
            line += part;
            if (t.max[p]) {
                std::snprintf(part, sizeof(part), " (max %.1f)", static_cast<double>(t.max[p]) / 1e6);
                line += part;
            }
            line += ',';
        }
        std::string vm;
        if (std::ifstream status("/proc/self/status"); status)
            for (std::string l; std::getline(status, l);)
                if (l.starts_with("VmSize") || l.starts_with("VmRSS") || l.starts_with("VmPTE"))
                    vm += " " + l.substr(0, l.find(':')) + l.substr(l.find_first_not_of(" \t", l.find(':') + 1) - 1);
        LOG_INFO("compare", "timing over %llu completed tests, ms mean:%s scanned %.0f pages, dirty %.1f pages;%s",
                 static_cast<unsigned long long>(t.tests), line.c_str(),
                 static_cast<double>(t.scan_pages) / static_cast<double>(t.tests),
                 static_cast<double>(t.dirty_pages) / static_cast<double>(t.tests), vm.c_str());
    }
}

}  // namespace loader::compare
