// Comparison harness, Linux (docs/comparison-harness.md).
//
// A tested function's entry jumps to a stub (push index; jmp xbcmp_entry) that saves
// the caller's registers and calls xbcmp_dispatch. For a sampled call the dispatcher
// forks a child, which forks two test processes at once, so that both start from the same
// copy of memory: one runs the original, the other the decompiled version. Before
// running, each makes every host mapping non-executable except the title's code, the
// harness's own section (xbcmp_text), the decompiled code and the C runtime, then
// clears the soft-dirty bits. A call into any other host code faults and ends the
// test as inconclusive. The function returns to xbcmp_landing, which saves the
// registers; the process reports them with the pages it wrote (soft-dirty bits in
// /proc/self/pagemap) over a pipe and sends the pages either side wrote.
//
// The game does not wait for that: the dispatcher forks a coordinator and goes on with
// the original through a trampoline at once. The coordinator (the snapshot of the
// call) forks the two test processes, compares their results and sends the verdict to
// the collector thread, which records it. Several tests run at once.
//
// Everything a test process runs after the mprotect is in xbcmp_text and makes raw
// system calls only: no C runtime, no calls the compiler adds (memcpy, stack checks).

#include "compare.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/fs.h>
#include <linux/seccomp.h>

// PAGEMAP_SCAN (Linux 6.7) from older headers (the Steam Runtime's, for the player release): the
// kernel's ABI as linux/fs.h has it. A kernel without it fails the ioctl and the pagemap path runs.
#ifndef PAGEMAP_SCAN
struct page_region {
    __u64 start;
    __u64 end;
    __u64 categories;
};
struct pm_scan_arg {
    __u64 size;
    __u64 flags;
    __u64 start;
    __u64 end;
    __u64 walk_end;
    __u64 vec;
    __u64 vec_len;
    __u64 max_pages;
    __u64 category_inverted;
    __u64 category_mask;
    __u64 category_anyof_mask;
    __u64 return_mask;
};
#define PAGE_IS_SOFT_DIRTY (1 << 7)
#define PAGEMAP_SCAN _IOWR('f', 16, struct pm_scan_arg)
#endif
#include <poll.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/ucontext.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string_view>
#include <thread>

#include "compiler.h"
#include "float_env.h"
#include "hooks.h"
#include "release_map.h"
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
    uint32_t zero_below;  // bytes below ESP that xbcmp_resume clears (test processes)
    uint32_t pad;
    // force_call: stack arguments the test processes write above the return address slot.
    uint32_t stack_words;
    uint32_t stack[32];
};
static_assert(offsetof(Context, eax) == 512 && offsetof(Context, esp) == 528 && offsetof(Context, edi) == 540);
static_assert(offsetof(Context, eflags) == 544 && offsetof(Context, target) == 548 &&
              offsetof(Context, zero_below) == 552 && sizeof(Context) == 704);

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

constexpr uint32_t kSdkLogBytes = 512 * 1024;
constexpr uint32_t kMaxSdkSpecs = 256;
constexpr uint32_t kZeroedStack = 16 * 1024;  // below the caller's ESP in test processes
constexpr uint32_t kMaxSdkArgs = 10;
constexpr uint32_t kMaxSdkData = 4096;  // per pointer argument
constexpr uint32_t kPushDwords = 8192;  // the BeginPush stand-in's space
constexpr uint32_t kStandInSurfaces = 64;  // the GetSurfaceLevel2 stand-in's pool

// A D3DSurface as render's create_level_surface makes it (XbSurface: Common, Data,
// Lock, Format, Size, Parent). The pool lives in ChildArea: the same address in both
// test processes, and skipped by the dirty-page scan (the title's writes to it are
// not compared; what it reads from it is).
struct StandInSurface {
    uint32_t common, data, lock, format, size, parent;
};
constexpr uint32_t kSurfaceCommon = 0x00050000 | 0x01000000 | 1;  // surface, D3D-created, count 1

// Config::sdk_calls, parsed for the test processes. Each recorded call is logged as
// its spec index, then per argument its value, or for a pointer the byte count and
// the bytes (padded to 4); a null pointer logs kSdkNull, an unreadable one kSdkBad.
enum SdkArgWhere : uint8_t { kSdkEcx, kSdkEdx, kSdkStack };
enum SdkArgKind : uint8_t { kSdkValue, kSdkBytes, kSdkScaledBytes, kSdkProductBytes, kSdkUnlogged };
constexpr uint32_t kSdkNull = 0xFFFFFFFE;
constexpr uint32_t kSdkBad = 0xFFFFFFFF;

struct SdkArg {
    SdkArgWhere where;
    SdkArgKind kind;
    uint8_t slot;        // kSdkStack: stack slot (0 = first argument)
    uint8_t count_from;  // kSdkScaledBytes/kSdkProductBytes: the argument holding the count
    uint8_t size_from;   // kSdkProductBytes: the argument holding the bytes per count
    uint32_t bytes;      // kSdkBytes: size; kSdkScaledBytes: bytes per count
};

// What a pass-through call's "(result)" record holds: EAX, its low byte (a C++ bool: the rest
// is whatever the register held), or nothing (void, or a structure returned through memory:
// EAX is then the caller's buffer, a stack address).
enum SdkResult : uint8_t { kResultEax, kResultAl, kResultNone };

struct SdkSpec {
    uint32_t host_address;
    SdkStandIn stand_in;
    SdkResult result;  // pass-through calls
    const uint32_t* value;
    uint32_t stack_slots;  // popped on return (stdcall/fastcall)
    uint32_t arg_count;
    SdkArg args[kMaxSdkArgs];
};

// kHostLocked: a pass-through call found a host lock held at the fork (host_lock_held).
enum ReportStatus : uint32_t { kOk, kSignal, kTooManyWrites, kHarnessError, kHostLocked };

struct Report {
    uint32_t status;
    uint32_t signal, eip, fault_address, esp, stack_word;  // kSignal
    uint32_t sdk_log_bytes, sdk_calls;
    uint32_t sdk_overflow;  // the log was full: the test is inconclusive
    uint32_t pass_blocked;  // kSignal on a pass-through export it would not run: PassBlock
    uint32_t pass_spec;     // that export's spec index (also: the latest pass-through call's)
    uint32_t host_lock;     // kHostLocked: which lock (audio::TestLock)
    uint32_t pass_calls;    // pass-through calls made
    uint32_t dirty_count;
    uint32_t scan_pages;  // pages the dirty scan read from pagemap
    // CLOCK_MONOTONIC ns: process started, maps read, bits cleared, function returned,
    // dirty pages collected (0 where not reached).
    uint64_t t_entered, t_mapped, t_cleared, t_landed, t_collected;
};

struct Range {
    uint32_t begin, end, protection;
};

// File calls run for real (pass-through, SdkStandIn::Pass*): while a kernel export runs, the
// loader's code is executable and the title's is not, so that a title routine the export calls
// (an APC: ReadFileEx's completion) and the export's return are seen and switch back. Each frame
// is an export (`address` = its caller's return address) or a title callback from host code
// (`address` = where it returns into the host).
enum PassBlock : uint32_t { kPassNone, kPassWrites, kPassDepth };
constexpr uint32_t kMaxPassDepth = 8;
constexpr uint32_t kMaxPassRanges = 64;
constexpr uint32_t kMaxPassName = 512;  // path bytes logged
constexpr uint32_t kPassZeroedStack = 128 * 1024;  // below a pass-through call's ESP
struct PassFrame {
    uint32_t callback;
    uint32_t address;
    uint32_t result;  // exports: SdkResult
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
    uint32_t image_begin, image_end;  // the title, readable for recorded SDK calls
    uint32_t sdk_log[kSdkLogBytes / 4];
    uint32_t push_dwords;  // the space the last PushBegin gave (0 = none open)
    uint32_t push_buffer[kPushDwords];
    uint32_t surface_count;  // stand-in surfaces handed out
    StandInSurface surfaces[kStandInSurfaces];
    // Pass-through file calls: the loader's code ranges (made executable while an export runs)
    // and the title's (made non-executable then), with their usual protection.
    uint32_t pass_host_count, pass_title_count;
    Range pass_host[kMaxPassRanges];
    Range pass_title[kMaxPassRanges];
    uint32_t pass_depth, pass_host_mode;
    PassFrame pass_frames[kMaxPassDepth];
    uint32_t stack_limit;  // the calling thread's lowest stack address (0: unknown)
    uint8_t signal_stack[64 * 1024];
    uint8_t stack[64 * 1024];
};

SdkSpec g_sdk_specs[kMaxSdkSpecs];
uint32_t g_sdk_spec_count = 0;
uint32_t g_pass_result_spec = 0xFFFFFFFF;  // the "(result)" record after a pass-through call

}  // namespace
}  // namespace loader::compare

using loader::compare::ChildArea;
using loader::compare::Context;
using loader::compare::EntryFrame;

namespace {
uint32_t g_pokes[8][2];  // XBCMP_POKE: {address, word}
uint32_t g_poke_count;
bool g_original_twice;  // XBCMP_ORIGINAL_TWICE
}  // namespace

extern "C" {
ChildArea xbcmp_child;
uint32_t xbcmp_child_stack_top;
// Set in test processes: a hooked function reached there (a callee of the tested one)
// runs its original straight from the stub, since the dispatcher is not executable.
uint8_t xbcmp_in_test;
// Test processes: the function xbcmp_test_jump continues to (the original or the decompiled
// entry), set just before the run.
uint32_t xbcmp_test_target;
extern char __start_xbcmp_text[];
extern char __stop_xbcmp_text[];
void xbcmp_entry();
void xbcmp_landing();
void xbcmp_test_jump();
[[noreturn]] void xbcmp_resume(const Context* context);
[[noreturn]] void xbcmp_dispatch(EntryFrame* frame);
[[noreturn]] void xbcmp_child_finish();
void xbcmp_pass_return();
void xbcmp_pass_done(uint32_t* frame);
}

// xbcmp_entry: [esp] = function index (pushed by its stub), [esp+4] = return address.
// xbcmp_resume(context): loads every register from `context` and jumps to
// context->target with ESP = context->esp, after zeroing context->zero_below bytes
// below that ESP (a multiple of 4). The EFLAGS word it pops is zeroed again (MOV keeps
// the flags): code reading never-written locals there (CoD BRO 0x0008d3f0's result w)
// would see EFLAGS on the original's side only. The target word at ESP - 4 stays: in
// test processes the target is xbcmp_test_jump, which zeroes that word (MOV) and jumps
// to xbcmp_test_target, so both sides start with 0 there (it held the original's address
// on one side and the decompiled entry's on the other: NAL_Clip_FindChannelByName copies
// the word below its entry ESP into its result).
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
    movl %eax, %edx
    movl 552(%edx), %ecx
    movl %esp, %edi
    subl %ecx, %edi
    shrl $2, %ecx
    xorl %eax, %eax
    cld
    rep stosl
    movl %edx, %eax
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
    movl $0, -4(%esp)
    ret

    .globl xbcmp_test_jump
xbcmp_test_jump:
    movl $0, -4(%esp)
    jmp *xbcmp_test_target

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

    .globl xbcmp_pass_return
xbcmp_pass_return:
    pushl $0
    pushfl
    pushal
    cld
    pushl %esp
    call xbcmp_pass_done
    addl $4, %esp
    popal
    popfl
    ret
    .popsection
)");

namespace loader::compare {
namespace {

// --- Test-process side after the mprotect: xbcmp_text only -----------------------

#define XBCMP_CODE PLATFORM_SECTION("xbcmp_text")

}  // namespace
}  // namespace loader::compare

// render's texture layout, compiled into xbcmp_text for the GetSurfaceLevel2 stand-in.
#define SURFACE_LAYOUT_FN static XBCMP_CODE
#include "surface_layout.h"

namespace loader::compare {
namespace {

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
    if (c.report.status != kOk) c.report.dirty_count = c.report.sdk_log_bytes = 0;
    if (write_all(c.out, addr_of(&c.report), sizeof(Report)) &&
        write_all(c.out, addr_of(&c.result), sizeof(Context)) &&
        write_all(c.out, addr_of(c.sdk_log), c.report.sdk_log_bytes) &&
        write_all(c.out, addr_of(c.dirty), c.report.dirty_count * 4) &&
        read_all(c.in, addr_of(&c.wanted_count), 4) && c.wanted_count <= 2 * kMaxDirty &&
        read_all(c.in, addr_of(c.wanted), c.wanted_count * 4)) {
        for (uint32_t i = 0; i < c.wanted_count; ++i)
            if (!write_all(c.out, c.wanted[i] * kPage, kPage)) break;
    }
    if (c.grandchild) sys5(SYS_wait4, c.grandchild, 0, 0, 0, 0);
    for (;;) sys3(SYS_exit_group, 0, 0, 0);
}

XBCMP_CODE bool readable(const ChildArea& c, uint32_t address, uint32_t size) {
    if (address >= c.image_begin && address - c.image_begin <= c.image_end - c.image_begin - size) return true;
    return in_scan_range(c, address, size);
}

XBCMP_CODE bool sdk_log_put(ChildArea& c, uint32_t value) {
    if (c.report.sdk_log_bytes + 4 > kSdkLogBytes) return false;
    c.sdk_log[c.report.sdk_log_bytes / 4] = value;
    c.report.sdk_log_bytes += 4;
    return true;
}

// A resource the stand-ins may touch: title memory or a pool surface. Null otherwise.
XBCMP_CODE uint32_t* stand_in_resource(const ChildArea& c, uint32_t address, uint32_t size) {
    if (!address) return nullptr;
    const uint32_t pool = addr_of(c.surfaces);
    if (address >= pool && address - pool <= sizeof(c.surfaces) - size) return reinterpret_cast<uint32_t*>(uintptr_t{address});
    return readable(c, address, size) ? reinterpret_cast<uint32_t*>(uintptr_t{address}) : nullptr;
}

constexpr uint32_t kRefcountMask = 0x0000FFFF;
constexpr uint32_t kInternalRefMask = 0x00780000;  // references held by the GPU / D3D
constexpr uint32_t kTypeMask = 0x00070000;
constexpr uint32_t kTypeSurface = 0x00050000;

// A surface's parent texture (Parent, +0x14), or 0; false if it cannot be read.
XBCMP_CODE bool stand_in_parent(const ChildArea& c, const uint32_t* resource, uint32_t& parent) {
    parent = 0;
    if ((resource[0] & kTypeMask) != kTypeSurface) return true;
    if (!stand_in_resource(c, addr_of(resource), 24)) return false;
    parent = resource[5];
    return true;
}

// render's D3DResource_AddRef: a surface's first reference adds one to its texture.
XBCMP_CODE bool stand_in_add_ref(ChildArea& c, uint32_t address, uint32_t& result) {
    uint32_t* resource = stand_in_resource(c, address, 4);
    if (!resource) return false;
    if ((resource[0] & kRefcountMask) == 0) {
        uint32_t parent = 0, ignored = 0;
        if (!stand_in_parent(c, resource, parent)) return false;
        if (parent && !stand_in_add_ref(c, parent, ignored)) return false;
    }
    result = ++resource[0] & kRefcountMask;
    return true;
}

// render's D3DResource_Release: the last reference releases a surface's texture and,
// without internal references, destroys the resource (count 0; render then frees its
// own level surfaces and host copies, nothing in title memory).
XBCMP_CODE bool stand_in_release(ChildArea& c, uint32_t address, uint32_t& result) {
    uint32_t* resource = stand_in_resource(c, address, 4);
    if (!resource) return false;
    if ((resource[0] & kRefcountMask) == 1) {
        uint32_t parent = 0, ignored = 0;
        if (!stand_in_parent(c, resource, parent)) return false;
        if (parent && !stand_in_release(c, parent, ignored)) return false;
        if ((resource[0] & kInternalRefMask) == 0) {
            resource[0] &= ~kRefcountMask;
            result = 0;
            return true;
        }
    }
    result = --resource[0] & kRefcountMask;
    return true;
}

// render's lock(): D3DLOCKED_RECT {pitch, bits} for a face/level of a pixel container,
// bits offset by the optional RECT {left, top, right, bottom}.
XBCMP_CODE bool stand_in_lock_rect(ChildArea& c, uint32_t container, uint32_t level, uint32_t locked_address,
                                   uint32_t rect_address) {
    const uint32_t* pixels = stand_in_resource(c, container, 20);  // Common, Data, Lock, Format, Size
    auto* locked = reinterpret_cast<uint32_t*>(uintptr_t{locked_address});
    if (!pixels || !readable(c, locked_address, 8) || (rect_address && !readable(c, rect_address, 16))) return false;
    namespace layout = render::surface_layout;
    const layout::SurfaceLevel surface = layout::surface_level(pixels[1], pixels[3], pixels[4], 0, level);
    uint32_t bits = surface.address;
    if (rect_address) {
        const auto* rect = reinterpret_cast<const int32_t*>(uintptr_t{rect_address});
        bits += rect[1] * surface.pitch + layout::bits_per_pixel(layout::format_of(pixels[3])) * rect[0] / 8;
    }
    locked[0] = surface.pitch;
    locked[1] = bits;
    return true;
}

// A recorded call's stand-in (SdkStandIn) on its first argument; false for a case it
// does not cover (the test then ends as inconclusive).
XBCMP_CODE bool sdk_stand_in(ChildArea& c, const SdkSpec& spec, const uint32_t* values, uint32_t& result) {
    if (spec.stand_in == SdkStandIn::PushBegin) {
        // The replacement's space is dwords + 1, zeroed; the title writes its NV2A
        // commands there (our own pages: not compared as writes).
        const uint32_t dwords = values[0];
        if (dwords >= kPushDwords) return false;
        for (uint32_t i = 0; i <= dwords; ++i) c.push_buffer[i] = 0;
        c.push_dwords = dwords + 1;
        result = addr_of(c.push_buffer);
        return true;
    }
    if (spec.stand_in == SdkStandIn::PushEnd) {
        const uint32_t begin = addr_of(c.push_buffer);
        const uint32_t end = values[0];
        if (!c.push_dwords || end < begin || (end - begin) % 4 || (end - begin) / 4 > c.push_dwords) return false;
        const uint32_t dwords = (end - begin) / 4;
        c.push_dwords = 0;
        bool ok = sdk_log_put(c, dwords);
        for (uint32_t i = 0; i < dwords && ok; ++i) ok = sdk_log_put(c, c.push_buffer[i]);
        if (!ok) c.report.sdk_overflow = 1;
        return true;
    }
    if (spec.stand_in == SdkStandIn::LevelCount) {
        if (!spec.arg_count || !readable(c, values[0], 20)) return false;
        const uint32_t format = reinterpret_cast<const uint32_t*>(uintptr_t{values[0]})[3];
        result = (format >> render::surface_layout::kFormatMipmapShift) & 0xF;
        return true;
    }
    if (spec.stand_in == SdkStandIn::SurfaceLevel) {
        if (spec.arg_count < 2 || !readable(c, values[0], 20) || c.surface_count >= kStandInSurfaces) return false;
        auto* texture = reinterpret_cast<uint32_t*>(uintptr_t{values[0]});  // Common, Data, Lock, Format, Size
        const uint32_t face = spec.arg_count == 3 ? values[1] : 0;
        const uint32_t level = values[spec.arg_count - 1];
        const render::surface_layout::SurfaceLevel layout =
            render::surface_layout::surface_level(texture[1], texture[3], texture[4], face, level);
        StandInSurface& surface = c.surfaces[c.surface_count++];
        surface.common = kSurfaceCommon;
        surface.data = layout.address & 0x0FFFFFFF;
        surface.lock = 0;
        surface.format = layout.format;
        surface.size = layout.size_field;
        surface.parent = values[0];
        ++texture[0];  // the surface's reference on its texture
        result = addr_of(&surface);
        return true;
    }
    switch (spec.stand_in) {
    case SdkStandIn::AddRef: return spec.arg_count && stand_in_add_ref(c, values[0], result);
    case SdkStandIn::Release: return spec.arg_count && stand_in_release(c, values[0], result);
    case SdkStandIn::ReturnAddRef: {
        result = *spec.value;
        if (!result) return true;
        uint32_t* resource = stand_in_resource(c, result, 4);
        if (!resource) return false;
        ++resource[0];  // render's add_ref: no parent check
        return true;
    }
    case SdkStandIn::TextureLock:  // (texture, level, locked, rect, flags)
        return spec.arg_count == 5 && stand_in_lock_rect(c, values[0], values[1], values[2], values[3]);
    case SdkStandIn::SurfaceLock:  // (surface, locked, rect, flags)
        return spec.arg_count == 4 && stand_in_lock_rect(c, values[0], 0, values[1], values[2]);
    case SdkStandIn::BufferLock: {  // (buffer, flags): its memory's CPU address
        const uint32_t* buffer = spec.arg_count ? stand_in_resource(c, values[0], 8) : nullptr;
        if (!buffer) return false;
        result = buffer[1] | 0x80000000;
        return true;
    }
    case SdkStandIn::SurfaceDesc: {  // (surface, desc): render's describe(surface, 0)
        const uint32_t* pixels = spec.arg_count == 2 ? stand_in_resource(c, values[0], 20) : nullptr;
        if (!pixels || !readable(c, values[1], 28)) return false;
        render::surface_layout::describe_level(pixels[0], pixels[3], pixels[4], 0,
                                               reinterpret_cast<uint32_t*>(uintptr_t{values[1]}));
        result = 0;
        return true;
    }
    case SdkStandIn::SystemTime: {  // (time): 0, as on both sides of every test
        if (spec.arg_count != 1 || !readable(c, values[0], 8)) return false;
        auto* time = reinterpret_cast<uint32_t*>(uintptr_t{values[0]});
        time[0] = time[1] = 0;
        result = 0;
        return true;
    }
    case SdkStandIn::PushBegin:
    case SdkStandIn::PushEnd:
    case SdkStandIn::LevelCount:
    case SdkStandIn::SurfaceLevel:
    case SdkStandIn::Clock:
    case SdkStandIn::Pass:
    case SdkStandIn::PassCreateFile:
    case SdkStandIn::PassOpenFile:
    case SdkStandIn::PassAttributes:
    case SdkStandIn::PassDirectory:
    case SdkStandIn::PassSetInformation:
    case SdkStandIn::PassSound:
    case SdkStandIn::None: break;
    }
    return false;
}

// --- Pass-through file calls -----------------------------------------------------

// Host mode: the loader's code executable, the title's not (an export runs); title mode: as
// the test started.
XBCMP_CODE void pass_mode(ChildArea& c, bool host) {
    for (uint32_t i = 0; i < c.pass_host_count; ++i) {
        const Range& r = c.pass_host[i];
        sys3(SYS_mprotect, r.begin, r.end - r.begin, r.protection | (host ? PROT_EXEC : 0));
    }
    for (uint32_t i = 0; i < c.pass_title_count; ++i) {
        const Range& r = c.pass_title[i];
        sys3(SYS_mprotect, r.begin, r.end - r.begin, host ? r.protection & ~uint32_t{PROT_EXEC} : r.protection);
    }
    c.pass_host_mode = host ? 1 : 0;
}

// Whether a pass-through call leaves every file as it is (it may run).
XBCMP_CODE bool pass_allowed(const SdkSpec& spec, const uint32_t* values) {
    // FILE_WRITE_DATA, FILE_APPEND_DATA, FILE_WRITE_EA, FILE_WRITE_ATTRIBUTES, DELETE, WRITE_DAC,
    // WRITE_OWNER, MAXIMUM_ALLOWED, GENERIC_ALL, GENERIC_WRITE.
    constexpr uint32_t kWriteAccess = 0x2 | 0x4 | 0x10 | 0x100 | 0x10000 | 0x40000 | 0x80000 | 0x02000000 |
                                      0x10000000 | 0x40000000;
    constexpr uint32_t kDeleteOnClose = 0x1000;
    switch (spec.stand_in) {
    case SdkStandIn::PassCreateFile:  // (handle*, access, attributes, iosb, size*, attributes, share, disposition, options)
        return spec.arg_count == 9 && !(values[1] & kWriteAccess) && values[7] == 1 /* FILE_OPEN */ &&
               !(values[8] & kDeleteOnClose);
    case SdkStandIn::PassOpenFile:  // (handle*, access, attributes, iosb, share, options)
        return spec.arg_count == 6 && !(values[1] & kWriteAccess) && !(values[5] & kDeleteOnClose);
    case SdkStandIn::PassSetInformation:  // (handle, iosb, information, length, class)
        return spec.arg_count == 5 && values[4] == 14;  // FilePositionInformation
    default: return true;
    }
}

// A name in the log: [root directory][length][bytes padded to words], from an ANSI_STRING
// {u16 length, u16 maximum, char* buffer}; [kSdkNull or kSdkBad][0] without one.
XBCMP_CODE bool pass_log_name(ChildArea& c, uint32_t root, uint32_t string) {
    if (!string) return sdk_log_put(c, kSdkNull) && sdk_log_put(c, 0);
    const auto* ansi = reinterpret_cast<const uint32_t*>(uintptr_t{string});
    if (!readable(c, string, 8)) return sdk_log_put(c, kSdkBad) && sdk_log_put(c, 0);
    const uint32_t length = ansi[0] & 0xFFFF;
    if (length > kMaxPassName || (length && !readable(c, ansi[1], length)))
        return sdk_log_put(c, kSdkBad) && sdk_log_put(c, 0);
    bool ok = sdk_log_put(c, root) && sdk_log_put(c, length);
    const auto* text = reinterpret_cast<const uint8_t*>(uintptr_t{ansi[1]});
    for (uint32_t at = 0; at < length && ok; at += 4) {
        uint32_t word = 0;
        for (uint32_t k = 0; k < 4 && at + k < length; ++k) word |= uint32_t{text[at + k]} << (8 * k);
        ok = sdk_log_put(c, word);
    }
    return ok;
}

// The name a pass-through call logs after its arguments (false: none).
XBCMP_CODE bool pass_log_extra(ChildArea& c, const SdkSpec& spec, const uint32_t* values) {
    uint32_t attributes = 0;  // OBJECT_ATTRIBUTES {root, ANSI_STRING* name, attributes}
    if (spec.stand_in == SdkStandIn::PassCreateFile || spec.stand_in == SdkStandIn::PassOpenFile)
        attributes = values[2];
    else if (spec.stand_in == SdkStandIn::PassAttributes)
        attributes = values[0];
    else if (spec.stand_in == SdkStandIn::PassDirectory)
        return pass_log_name(c, 0, values[8]);
    else
        return true;
    if (!attributes) return pass_log_name(c, 0, 0);
    if (!readable(c, attributes, 12)) return sdk_log_put(c, kSdkBad) && sdk_log_put(c, 0);
    const auto* a = reinterpret_cast<const uint32_t*>(uintptr_t{attributes});
    return pass_log_name(c, a[0], a[1]);
}

// A fault while a pass-through export runs: host code calling title code (an APC), or that
// title code returning into the host. False for anything else.
XBCMP_CODE bool pass_fault(ChildArea& c, greg_t* gregs) {
    if (!c.pass_depth) return false;
    const auto eip = static_cast<uint32_t>(gregs[REG_EIP]);
    const auto esp = static_cast<uint32_t>(gregs[REG_ESP]);
    const bool in_title = eip - c.image_begin < c.image_end - c.image_begin;
    if (c.pass_host_mode && in_title) {
        if (c.pass_depth == kMaxPassDepth || !in_scan_range(c, esp, 4)) return false;
        c.pass_frames[c.pass_depth++] = {1, *reinterpret_cast<const uint32_t*>(uintptr_t{esp}), kResultNone};
        pass_mode(c, false);
        return true;
    }
    const PassFrame& top = c.pass_frames[c.pass_depth - 1];
    if (!c.pass_host_mode && !in_title && top.callback && eip == top.address) {
        --c.pass_depth;
        pass_mode(c, true);
        return true;
    }
    return false;
}

// A pass-through export returned (into xbcmp_pass_return): `frame` = pushad, EFLAGS, the slot
// for the address it goes on at. Logs the result and switches back to title mode.
extern "C" XBCMP_CODE void xbcmp_pass_done(uint32_t* frame) {
    ChildArea& c = xbcmp_child;
    if (!c.pass_depth || c.pass_frames[c.pass_depth - 1].callback) {
        c.report.status = kHarnessError;
        child_send(c);
    }
    const PassFrame& done = c.pass_frames[--c.pass_depth];
    frame[9] = done.address;
    if (done.result != kResultNone &&
        (!sdk_log_put(c, g_pass_result_spec) || !sdk_log_put(c, done.result == kResultAl ? frame[7] & 0xFF : frame[7])))
        c.report.sdk_overflow = 1;
    pass_mode(c, false);
}

}  // namespace
}  // namespace loader::compare

// A pass-through export found a host lock that another thread held at the fork (it stays held
// in the copy): ends the test as inconclusive instead of waiting forever. Host code calls it.
[[noreturn]] XBCMP_CODE void loader::compare::host_lock_held(uint32_t which) {
    ChildArea& c = xbcmp_child;
    c.report.status = kHostLocked;
    c.report.host_lock = which;
    c.report.stack_word = 0;  // the title's call: the innermost export frame's return address
    for (uint32_t i = c.pass_depth; i > 0; --i)
        if (!c.pass_frames[i - 1].callback) {
            c.report.stack_word = c.pass_frames[i - 1].address;
            break;
        }
    child_send(c);
}

namespace loader::compare {
namespace {

// A fault on the entry of a recorded SDK replacement: log the call and return to the
// caller as the replacement would (EAX = 0, stack arguments popped). False if `eip`
// is not one (or the caller's stack cannot be read): the test then ends as usual.
// A pass-through export (SdkStandIn::Pass*) is logged and then runs (pass_mode).
XBCMP_CODE bool record_sdk_call(ChildArea& c, greg_t* gregs) {
    const auto eip = static_cast<uint32_t>(gregs[REG_EIP]);
    uint32_t index = 0;
    while (index < g_sdk_spec_count && g_sdk_specs[index].host_address != eip) ++index;
    if (index == g_sdk_spec_count) return false;
    const SdkSpec& spec = g_sdk_specs[index];
    const auto esp = static_cast<uint32_t>(gregs[REG_ESP]);
    if (!in_scan_range(c, esp, 4 + spec.stack_slots * 4)) return false;
    const auto* stack = reinterpret_cast<const uint32_t*>(uintptr_t{esp});
    uint32_t values[kMaxSdkArgs];
    for (uint32_t i = 0; i < spec.arg_count; ++i) {
        const SdkArg& arg = spec.args[i];
        values[i] = arg.where == kSdkEcx   ? static_cast<uint32_t>(gregs[REG_ECX])
                    : arg.where == kSdkEdx ? static_cast<uint32_t>(gregs[REG_EDX])
                                           : stack[1 + arg.slot];
    }
    const bool pass = spec.stand_in >= SdkStandIn::Pass;  // not is_pass(): an out-of-line call at -O0
    if (pass) {
        if (c.pass_depth == kMaxPassDepth || !c.pass_host_count || !pass_allowed(spec, values)) {
            c.report.pass_blocked = c.pass_depth == kMaxPassDepth || !c.pass_host_count ? kPassDepth : kPassWrites;
            c.report.pass_spec = index;
            return false;
        }
    }
    bool ok = sdk_log_put(c, index);
    for (uint32_t i = 0; i < spec.arg_count && ok; ++i) {
        const SdkArg& arg = spec.args[i];
        if (arg.kind == kSdkUnlogged) continue;
        if (arg.kind == kSdkValue) {
            ok = sdk_log_put(c, values[i]);
            continue;
        }
        const uint32_t bytes = arg.kind == kSdkBytes          ? arg.bytes
                               : arg.kind == kSdkScaledBytes ? values[arg.count_from] * arg.bytes
                                                             : values[arg.count_from] * values[arg.size_from];
        if (values[i] == 0) {
            ok = sdk_log_put(c, kSdkNull);
        } else if (bytes > kMaxSdkData || !readable(c, values[i], bytes)) {
            ok = sdk_log_put(c, kSdkBad);
        } else {
            ok = sdk_log_put(c, bytes);
            const auto* data = reinterpret_cast<const uint8_t*>(uintptr_t{values[i]});
            for (uint32_t at = 0; at < bytes && ok; at += 4) {
                uint32_t word = 0;
                for (uint32_t k = 0; k < 4 && at + k < bytes; ++k) word |= uint32_t{data[at + k]} << (8 * k);
                ok = sdk_log_put(c, word);
            }
        }
    }
    if (pass) {
        if (!ok || !pass_log_extra(c, spec, values)) c.report.sdk_overflow = 1;
        // The dead stack below reads as zeros, as at a test's start: the export's frames go
        // there, and host code that copies bytes it never set (struct padding) would otherwise
        // copy what the two sides' different frames left there into host memory.
        const uint32_t room = c.stack_limit && esp > c.stack_limit ? esp - c.stack_limit : 0;  // no std::min here
        const uint32_t zeroed = spec.stand_in == SdkStandIn::PassSound ? kZeroedStack : kPassZeroedStack;
        const uint32_t floor = esp - (room < zeroed ? room : zeroed);
        for (auto* p = reinterpret_cast<uint32_t*>(uintptr_t{floor}); p < stack; ++p) *p = 0;
        ++c.report.sdk_calls;
        ++c.report.pass_calls;
        c.report.pass_spec = index;
        c.pass_frames[c.pass_depth++] = {0, stack[0], spec.result};
        *const_cast<uint32_t*>(stack) = addr_of(reinterpret_cast<const void*>(&xbcmp_pass_return));
        pass_mode(c, true);
        return true;  // the export runs from its entry now
    }
    if (!ok) c.report.sdk_overflow = 1;
    uint32_t result = 0;
    if (spec.stand_in == SdkStandIn::Clock)
        gregs[REG_EDX] = 0;
    else if (spec.stand_in != SdkStandIn::None && !sdk_stand_in(c, spec, values, result))
        return false;
    ++c.report.sdk_calls;
    gregs[REG_EIP] = static_cast<greg_t>(stack[0]);
    gregs[REG_ESP] = static_cast<greg_t>(esp + 4 + spec.stack_slots * 4);
    gregs[REG_EAX] = static_cast<greg_t>(result);
    return true;
}

extern "C" XBCMP_CODE void xbcmp_child_signal(int signal, siginfo_t* info, void* context) {
    ChildArea& c = xbcmp_child;
    auto& gregs = static_cast<ucontext_t*>(context)->uc_mcontext.gregs;
    if (signal == SIGSEGV && addr_of(info->si_addr) == static_cast<uint32_t>(gregs[REG_EIP]) &&
        (pass_fault(c, gregs) || record_sdk_call(c, gregs)))
        return;
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
    uint32_t reports = 0;          // inconclusive details logged so far
    uint32_t issue_reports = 0;    // mismatch / drift details logged so far
};

Config g_config;
std::vector<std::unique_ptr<Hook>> g_hooks;
std::vector<const Function*> g_replaced;
std::mutex g_stats;  // counts and the results file
std::atomic<uint64_t> g_test_ns{0};  // the game's waits (stall)
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

// Where a test's time goes (completed tests only), logged by flush(). "stall" is the
// game's wait (its fork of the coordinator); "total" the coordinator's work; "latency"
// from the call to the collector having the verdict.
enum Phase { kStall, kFork, kStartOriginal, kStartNew, kMaps, kClear, kRun, kCollect, kReports, kPages, kCompare,
             kTotal, kLatency, kPhases };
constexpr const char* kPhaseNames[kPhases] = {"stall", "fork", "start-orig", "start-new", "maps", "protect+clear",
                                             "run", "collect", "reports", "pages", "compare", "total", "latency"};
// One test's phases (ns), measured in the coordinator.
struct TestTiming {
    uint64_t ns[kPhases] = {};
    uint64_t scan_pages = 0, dirty_pages = 0;
    uint32_t complete = 0;  // all phases measured
};
struct Timing {
    uint64_t tests = 0;
    uint64_t sum[kPhases] = {}, max[kPhases] = {};
    uint64_t scan_pages = 0, dirty_pages = 0;
};
Timing g_timing;  // under g_stats
std::atomic<uint64_t> g_skipped_busy{0};  // sampled calls not tested: all slots busy

uint64_t span(uint64_t from, uint64_t to) { return from && to > from ? to - from : 0; }

// In a coordinator: its verdict pipe (test processes close it) and the read end of the
// pipe that ends when the game does (-1 in the game).
int g_verdict_fd = -1;
int g_game_alive_fd = -1;

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
            op2 == 0x10 || op2 == 0x11 || op2 == 0x28 || op2 == 0x29 || (op2 >= 0x12 && op2 <= 0x17) ||
            (op2 >= 0x2A && op2 <= 0x2F) || (op2 >= 0x51 && op2 <= 0x5F) || op2 == 0x6E || op2 == 0x6F ||
            op2 == 0x7E || op2 == 0x7F)  // SSE moves/arithmetic/compares: no immediates
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

// A relative CALL (E8) or JMP (E9, EB) at `code`: its length and target. The trampoline
// re-encodes a JMP as a near JMP and a CALL as `push <the original's return address>; jmp
// target`, so the callee returns into the original code right after the call (the hook's
// 5 bytes always end at or before that address) with the same return address on the
// stack as the original (CoD BRO `push 0xc; call Z_Malloc` constructors, SV_Init).
bool relative_transfer(const uint8_t* code, uint32_t& length, uint32_t& target) {
    if (code[0] == 0xE8 || code[0] == 0xE9) {
        int32_t relative;
        std::memcpy(&relative, code + 1, 4);
        length = 5;
        target = addr(code) + 5 + static_cast<uint32_t>(relative);
        return true;
    }
    if (code[0] == 0xEB) {
        length = 2;
        target = addr(code) + 2 + static_cast<uint32_t>(static_cast<int8_t>(code[1]));
        return true;
    }
    return false;
}

// True when the function's own code at `address` branches back into the hook's bytes
// (address, address + moved): the hook's JMP overwrites them, so such a branch lands in the
// middle of it (CoD BRO S_WaitForWaveBanks loops back to 0x002ce9c1, its `push edi` - every
// run crashed at boot). A linear sweep from the end of the moved bytes over what the
// trampoline decoder knows plus RET/indirect calls and jumps/string ops; it stops at a
// RET/JMP no earlier branch jumps past (the function's end) or at an instruction it cannot
// size (then it found nothing - best effort, not a proof).
bool branches_into_entry(uint32_t address, uint32_t moved) {
    const auto* code = reinterpret_cast<const uint8_t*>(uintptr_t{address});
    uint32_t at = moved, furthest = 0;
    for (uint32_t steps = 0; steps < 0x4000; ++steps) {
        const uint8_t* p = code + at;
        uint32_t length = 0, target = 0;
        bool end = false;
        if (conditional_branch(p, length, target) || relative_transfer(p, length, target)) {
            if (target > address && target < address + moved) return true;
            if (target > address + at && target - address > furthest) furthest = target - address;
            end = p[0] == 0xE9 || p[0] == 0xEB;
        } else if (p[0] == 0xC3 || p[0] == 0xC2) {
            length = p[0] == 0xC3 ? 1 : 3;
            end = true;
        } else if (p[0] == 0xFF) {
            const uint32_t reg = (p[1] >> 3) & 7;
            uint8_t probe[16];  // sized as FF /0 (inc): the same operand bytes
            std::memcpy(probe, p, sizeof(probe));
            probe[1] = static_cast<uint8_t>(p[1] & ~0x38);
            length = instruction_length(probe);
            end = reg == 4 || reg == 5;
        } else if (const uint32_t rep = p[0] == 0xF3 || p[0] == 0xF2 ? 1 : 0;
                   (p[rep] >= 0xA4 && p[rep] <= 0xA7) || (p[rep] >= 0xAA && p[rep] <= 0xAF) ||
                   (!rep && (p[0] == 0x98 || p[0] == 0x9E || p[0] == 0x9F || p[0] == 0x9B))) {
            length = rep + 1;
        } else {
            length = instruction_length(p);
        }
        if (!length) return false;
        at += length;
        if (end && at > furthest) return false;
    }
    return false;
}

// Copies the entry instructions [entry, entry + moved) into `out`, re-encoding
// conditional branches as near branches to the same targets and relative CALL/JMP as
// above. Returns the bytes written.
uint32_t copy_entry(uint8_t* out, const uint8_t* entry, uint32_t moved) {
    uint32_t in = 0, written = 0;
    while (in < moved) {
        uint32_t length, target;
        if (conditional_branch(entry + in, length, target)) {
            const uint8_t condition = entry[in] == 0x0F ? entry[in + 1] & 0x0F : entry[in] & 0x0F;
            out[written] = 0x0F;
            put_rel32(out + written + 1, static_cast<uint8_t>(0x80 | condition), target);
            written += 6;
        } else if (relative_transfer(entry + in, length, target)) {
            if (entry[in] == 0xE8) {
                const uint32_t return_address = addr(entry + in) + length;
                out[written] = 0x68;  // push imm32
                std::memcpy(out + written + 1, &return_address, 4);
                written += 5;
            }
            put_rel32(out + written, 0xE9, target);
            written += 5;
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

bool contains(std::string_view text, const char* part) { return text.find(part) != std::string_view::npos; }

// /proc/self/maps, read without the heap: the two test processes must leave the host heap
// as they found it before a test (pass-through exports allocate there, and the pages they
// write are compared - with whatever else is on them).
char g_maps_text[1024 * 1024];

// The lines of `text` (each ends in a NUL, prepare_maps), one at a time.
bool next_line(std::string_view& text, std::string_view& line) {
    if (text.empty()) return false;
    const size_t end = std::min(text.find('\0'), text.size());
    line = text.substr(0, end);
    text.remove_prefix(std::min(end + 1, text.size()));
    return true;
}

bool prepare_maps(ChildArea& c) {
    const int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    size_t size = 0;
    for (ssize_t n; size < sizeof(g_maps_text) - 1 &&
                    (n = read(fd, g_maps_text + size, sizeof(g_maps_text) - 1 - size)) > 0;)
        size += static_cast<size_t>(n);
    close(fd);
    if (size == sizeof(g_maps_text) - 1) return false;
    // Each line ends in a NUL for sscanf (the newlines become NULs).
    for (size_t i = 0; i < size; ++i)
        if (g_maps_text[i] == '\n') g_maps_text[i] = 0;
    g_maps_text[size] = 0;
    const std::string_view maps(g_maps_text, size);

    const uint32_t section_begin = addr(__start_xbcmp_text) & ~(kPage - 1);
    const uint32_t section_end = (addr(__stop_xbcmp_text) + kPage - 1) & ~(kPage - 1);
    c.scan_count = c.protect_count = 0;
    c.pass_host_count = c.pass_title_count = 0;
    // Pass-through exports run in the mapping that holds them (the loader's code).
    uint32_t pass_address = 0;
    for (uint32_t i = 0; i < g_sdk_spec_count && !pass_address; ++i)
        if (is_pass(g_sdk_specs[i].stand_in)) pass_address = g_sdk_specs[i].host_address;
    std::string_view pass_mapping;
    {
        std::string_view lines = maps, line;
        while (pass_address && next_line(lines, line)) {
            unsigned long begin = 0, end = 0;
            int name_at = 0;
            if (std::sscanf(line.data(), "%lx-%lx %*s %*s %*s %*s %n", &begin, &end, &name_at) < 2) continue;
            if (pass_address >= begin && pass_address < end && name_at > 0)
                pass_mapping = line.substr(static_cast<size_t>(name_at));
        }
    }
    std::string_view lines = maps, line;
    while (next_line(lines, line)) {
        unsigned long begin = 0, end = 0;
        char perms[8] = {};
        int name_at = 0;
        if (std::sscanf(line.data(), "%lx-%lx %7s %*s %*s %*s %n", &begin, &end, perms, &name_at) < 3) continue;
        const std::string_view name =
            name_at > 0 && static_cast<size_t>(name_at) <= line.size() ? line.substr(static_cast<size_t>(name_at)) : "";
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
        if (b < g_config.image_end && e > g_config.image_begin) {  // the title's code
            if (c.pass_title_count == kMaxPassRanges) return false;
            const uint32_t protection = PROT_READ | PROT_EXEC | (writable ? PROT_WRITE : 0);
            c.pass_title[c.pass_title_count++] = {std::max(b, g_config.image_begin & ~(kPage - 1)),
                                                  std::min(e, (g_config.image_end + kPage - 1) & ~(kPage - 1)),
                                                  protection};
            continue;
        }
        if (name == "[vdso]" || name == "[vsyscall]") continue;
        bool allowed = false;
        for (const std::string& library : g_config.libraries) allowed |= contains(name, library.c_str());
        if (allowed) continue;
        const uint32_t protection = PROT_READ | (writable ? PROT_WRITE : 0);
        const auto protect = [&](uint32_t from, uint32_t to) {
            if (from < to && c.protect_count < kMaxRanges) c.protect[c.protect_count++] = {from, to, protection};
        };
        // Allowed host code pages (Config::host_code) inside the mapping stay executable.
        const auto add = [&](uint32_t from, uint32_t to) {
            for (const auto& [begin, end] : g_config.host_code) {
                if (begin >= end || begin >= to || end <= from) continue;
                protect(from, std::max(from, begin));
                from = std::min(to, end);
            }
            protect(from, to);
        };
        const uint32_t first = c.protect_count;
        if (section_begin < e && section_end > b) {
            add(b, section_begin);
            add(section_end, e);
        } else {
            add(b, e);
        }
        if (c.protect_count == kMaxRanges) return false;
        if (!pass_mapping.empty() && name == pass_mapping) {
            for (uint32_t i = first; i < c.protect_count; ++i) {
                if (c.pass_host_count == kMaxPassRanges) return false;
                c.pass_host[c.pass_host_count++] = c.protect[i];
            }
        }
    }
    // There is always host code to protect (the loader's): none means the maps were misread,
    // and a test would run host code freely.
    return c.protect_count > 0 && c.scan_count > 0;
}

// A seccomp filter for a test process: no system call that changes a file, sends data or
// starts a process works there (EPERM), and writes and ioctls go only to the harness's own
// files (its report pipe, clear_refs, pagemap). Pass-through file calls run the loader's own
// export code in test processes; this keeps "a test never touches the game's files" true for
// whatever that code (or its logging) would do.
bool install_file_guard(const ChildArea& c) {
    // No padding: the vectors' heap blocks must hold the same bytes on both sides (they are
    // freed, and host memory pages a pass-through call writes later are compared whole).
    struct Insn {
        uint32_t code;
        uint32_t k;
        int jt, jf;  // label numbers, -1 = the next instruction
    };
    enum Label { kAllow, kDeny, kCheckFd, kCheckIoctl, kCheckOpen1, kCheckOpen2, kCheckMmap, kLabels };
    std::vector<Insn> code;
    int label_at[kLabels] = {};
    const auto stmt = [&](uint32_t op, uint32_t k) { code.push_back({op, k, -1, -1}); };
    const auto jump = [&](uint32_t k, int to) { code.push_back({BPF_JMP | BPF_JEQ | BPF_K, k, to, -1}); };
    const auto label = [&](Label l) { label_at[l] = static_cast<int>(code.size()); };
    const auto arg = [](int i) { return static_cast<uint32_t>(offsetof(seccomp_data, args) + 8 * i); };
    stmt(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, arch));
    jump(AUDIT_ARCH_I386, -1);
    code.back().jf = kDeny;
    stmt(BPF_LD | BPF_W | BPF_ABS, offsetof(seccomp_data, nr));
    static const int kDenied[] = {
        SYS_unlink, SYS_unlinkat, SYS_rename, SYS_renameat, SYS_renameat2, SYS_mkdir, SYS_mkdirat, SYS_rmdir,
        SYS_truncate, SYS_truncate64, SYS_ftruncate, SYS_ftruncate64, SYS_fallocate, SYS_link, SYS_linkat,
        SYS_symlink, SYS_symlinkat, SYS_chmod, SYS_fchmod, SYS_fchmodat, SYS_chown, SYS_lchown, SYS_fchown,
        SYS_chown32, SYS_lchown32, SYS_fchown32, SYS_fchownat, SYS_utime, SYS_utimes, SYS_utimensat,
        SYS_futimesat, SYS_fsync, SYS_fdatasync, SYS_sync, SYS_syncfs, SYS_sync_file_range, SYS_msync,
        SYS_creat, SYS_mknod, SYS_mknodat, SYS_setxattr, SYS_lsetxattr, SYS_fsetxattr, SYS_removexattr,
        SYS_lremovexattr, SYS_fremovexattr, SYS_sendfile, SYS_sendfile64, SYS_splice, SYS_copy_file_range,
        SYS_socketcall, SYS_socket, SYS_connect, SYS_sendto, SYS_sendmsg, SYS_sendmmsg, SYS_execve,
        SYS_execveat, SYS_fork, SYS_vfork, SYS_clone, SYS_clone3, SYS_ptrace, SYS_process_vm_writev,
        SYS_io_uring_setup, SYS_io_uring_enter, SYS_io_submit, SYS_openat2, SYS_name_to_handle_at,
        SYS_open_by_handle_at,
    };
    for (int nr : kDenied) jump(static_cast<uint32_t>(nr), kDeny);
    for (int nr : {SYS_write, SYS_writev, SYS_pwrite64, SYS_pwritev, SYS_pwritev2}) jump(static_cast<uint32_t>(nr), kCheckFd);
    jump(SYS_ioctl, kCheckIoctl);
    jump(SYS_open, kCheckOpen1);
    jump(SYS_openat, kCheckOpen2);
    jump(SYS_mmap2, kCheckMmap);
    stmt(BPF_JMP | BPF_JA, 0);
    code.back().jt = kAllow;  // JA: resolved through jt below
    constexpr uint32_t kWriting = O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND;
    label(kCheckFd);
    stmt(BPF_LD | BPF_W | BPF_ABS, arg(0));
    jump(static_cast<uint32_t>(c.out), kAllow);
    jump(static_cast<uint32_t>(c.clear_refs_fd), kAllow);
    stmt(BPF_JMP | BPF_JA, 0);
    code.back().jt = kDeny;
    label(kCheckIoctl);
    stmt(BPF_LD | BPF_W | BPF_ABS, arg(0));
    jump(static_cast<uint32_t>(c.pagemap_fd), kAllow);
    stmt(BPF_JMP | BPF_JA, 0);
    code.back().jt = kDeny;
    label(kCheckOpen1);
    stmt(BPF_LD | BPF_W | BPF_ABS, arg(1));
    code.push_back({BPF_JMP | BPF_JSET | BPF_K, kWriting, kDeny, kAllow});
    label(kCheckOpen2);
    stmt(BPF_LD | BPF_W | BPF_ABS, arg(2));
    code.push_back({BPF_JMP | BPF_JSET | BPF_K, kWriting, kDeny, kAllow});
    label(kCheckMmap);  // a writable shared mapping writes its file
    stmt(BPF_LD | BPF_W | BPF_ABS, arg(3));
    code.push_back({BPF_JMP | BPF_JSET | BPF_K, MAP_SHARED, -1, kAllow});
    stmt(BPF_LD | BPF_W | BPF_ABS, arg(2));
    code.push_back({BPF_JMP | BPF_JSET | BPF_K, PROT_WRITE, kDeny, kAllow});
    label(kAllow);
    stmt(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
    label(kDeny);
    stmt(BPF_RET | BPF_K, SECCOMP_RET_ERRNO | EPERM);

    std::vector<sock_filter> program;
    for (size_t i = 0; i < code.size(); ++i) {
        const Insn& insn = code[i];
        const auto offset = [&](int to) -> int { return to < 0 ? 0 : label_at[to] - static_cast<int>(i) - 1; };
        const int jt = offset(insn.jt), jf = offset(insn.jf);
        if (jt < 0 || jt > 255 || jf < 0 || jf > 255) return false;
        if (insn.code == (BPF_JMP | BPF_JA))
            program.push_back(BPF_STMT(static_cast<uint16_t>(insn.code), static_cast<uint32_t>(jt)));
        else if (BPF_CLASS(insn.code) == BPF_JMP)
            program.push_back(BPF_JUMP(static_cast<uint16_t>(insn.code), insn.k, static_cast<uint8_t>(jt),
                                       static_cast<uint8_t>(jf)));
        else
            program.push_back(BPF_STMT(static_cast<uint16_t>(insn.code), insn.k));
    }
    sock_fprog fprog{static_cast<unsigned short>(program.size()), program.data()};
    return prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0 &&
           syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0, &fprog) == 0;
}

// `unused`: the other pipe ends this process inherited. They must be closed: a pipe
// whose write end is still open somewhere never reports EOF, and a test process
// waiting on it outlives the game (holding its window open).
[[noreturn]] void child_main(bool run_original, int out, int in, pid_t grandchild, std::initializer_list<int> unused,
                             const Hook& hook, const Context& context, uint32_t stack_limit) {
    ChildArea& c = xbcmp_child;
    for (int fd : unused) close(fd);
    if (g_verdict_fd >= 0) close(g_verdict_fd);
    // The same descriptor table on both sides: the pipe ends move above all eight pipe
    // descriptors (the same numbers in both processes), so files the pass-through exports
    // open get the same numbers on both sides (they are stored in host memory).
    {
        int top = std::max(out, in);
        for (int fd : unused) top = std::max(top, fd);
        const int moved_out = fcntl(out, F_DUPFD_CLOEXEC, top + 1);
        const int moved_in = fcntl(in, F_DUPFD_CLOEXEC, top + 1);
        if (moved_out < 0 || moved_in < 0) _exit(1);
        close(out);
        close(in);
        out = moved_out;
        in = moved_in;
    }
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
    c.push_dwords = 0;
    c.surface_count = 0;
    c.pass_depth = c.pass_host_mode = 0;
    c.stack_limit = stack_limit;
    c.report.t_entered = now_ns();
    c.image_begin = g_config.image_begin;
    c.image_end = g_config.image_end;
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
    // XBCMP_POKE: title words both sides get before the test (not counted as written).
    for (uint32_t i = 0; i < g_poke_count; ++i)
        *reinterpret_cast<uint32_t*>(uintptr_t{g_pokes[i][0]}) = g_pokes[i][1];
    std::memcpy(&c.start, &context, sizeof(Context));
    xbcmp_test_target = run_original || g_original_twice ? hook.address : hook.code;
    c.start.target = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&xbcmp_test_jump));
    // The dead stack below the caller's ESP reads as zeros on both sides: code that
    // sends uninitialized locals (unused lanes of a shader constant block) sends the
    // same bytes then, and the decompiled version writes zeros there.
    c.start.zero_below = 0;
    if (stack_limit && c.start.esp > stack_limit)
        c.start.zero_below = std::min<uint32_t>(kZeroedStack, (c.start.esp - stack_limit) & ~3u);
    auto* frame = reinterpret_cast<uint32_t*>(uintptr_t{c.start.esp});
    for (uint32_t i = 0; i < c.start.stack_words; ++i) frame[1 + i] = c.start.stack[i];
    frame[0] = addr(reinterpret_cast<const void*>(&xbcmp_landing));
    if (!install_file_guard(c)) {
        c.report.status = kHarnessError;
        child_send(c);
    }
    child_run(c);
}

// --- Parent: collecting and comparing ---------------------------------------------

using Clock = std::chrono::steady_clock;

bool read_until(int fd, void* data, size_t size, Clock::time_point deadline) {
    auto* p = static_cast<uint8_t*>(data);
    while (size > 0) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (left <= 0) return false;
        pollfd poll_fds[2] = {{fd, POLLIN, 0}, {g_game_alive_fd, POLLIN, 0}};
        const int ready = poll(poll_fds, g_game_alive_fd >= 0 ? 2 : 1, static_cast<int>(left));
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) return false;
        if (poll_fds[1].revents) _exit(0);  // the game is gone: so is this coordinator
        if (!poll_fds[0].revents) continue;
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
    std::vector<uint32_t> sdk_log;
};

bool receive_report(Side& side, Clock::time_point deadline) {
    if (!read_until(side.from, &side.report, sizeof(Report), deadline) ||
        !read_until(side.from, &side.result, sizeof(Context), deadline))
        return false;
    if (side.report.status == kOk) {
        if (side.report.sdk_log_bytes > kSdkLogBytes || side.report.sdk_log_bytes % 4) return false;
        side.sdk_log.resize(side.report.sdk_log_bytes / 4);
        if (!read_until(side.from, side.sdk_log.data(), side.report.sdk_log_bytes, deadline)) return false;
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
    TestTiming timing;
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

std::vector<std::string> g_sdk_names;  // per g_sdk_specs entry

std::string signal_text(const Side& side) {
    const Report& r = side.report;
    if (r.status == kSignal && r.pass_blocked && r.pass_spec < g_sdk_names.size())
        return std::string(side.what) + " made a file call the harness does not run (" +
               (r.pass_blocked == kPassWrites ? "it would change a file" : "nested too deep") + "): " +
               g_sdk_names[r.pass_spec] + " from " + describe(r.stack_word);
    if (r.status == kTooManyWrites) return std::string(side.what) + " wrote too much memory";
    if (r.status == kHarnessError) return std::string(side.what) + ": harness error";
    if (r.status == kHostLocked) {
        static const char* const kLocks[] = {"?", "DirectSound state", "a voice", "the stream callbacks"};
        const char* lock = kLocks[r.host_lock < std::size(kLocks) ? r.host_lock : 0];
        return std::string(side.what) + " found " + lock + " locked at the fork (another thread held it): " +
               (r.pass_spec < g_sdk_names.size() ? g_sdk_names[r.pass_spec] : std::string("?")) + " from " +
               describe(r.stack_word);
    }
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
uint32_t xmm0_low(const Context& c) {
    uint32_t value;
    std::memcpy(&value, c.fx + 160, 4);
    return value;
}
uint32_t mxcsr(const Context& c) {
    uint32_t value;
    std::memcpy(&value, c.fx + 24, 4);
    return value;
}


// One recorded call's length in log words at `at`, or 0 if the log is damaged.
size_t sdk_record_words(const std::vector<uint32_t>& log, size_t at) {
    if (at >= log.size() || log[at] >= g_sdk_spec_count) return 0;
    const SdkSpec& spec = g_sdk_specs[log[at]];
    size_t p = at + 1;
    for (uint32_t i = 0; i < spec.arg_count; ++i) {
        if (spec.args[i].kind == kSdkUnlogged) continue;  // `x`: nothing in the log
        if (p >= log.size()) return 0;
        const uint32_t word = log[p++];
        if (spec.args[i].kind != kSdkValue && word != kSdkNull && word != kSdkBad) p += (word + 3) / 4;
    }
    // PushEnd's stand-in appends the dword count and the dwords written.
    if (spec.stand_in == SdkStandIn::PushEnd) {
        if (p >= log.size()) return 0;
        p += 1 + size_t{log[p]};
    }
    // Pass-through calls with a name: root directory, length, the bytes (pass_log_name).
    if (spec.stand_in == SdkStandIn::PassCreateFile || spec.stand_in == SdkStandIn::PassOpenFile ||
        spec.stand_in == SdkStandIn::PassAttributes || spec.stand_in == SdkStandIn::PassDirectory) {
        if (p + 1 >= log.size()) return 0;
        p += 2 + (size_t{log[p + 1]} + 3) / 4;
    }
    return p <= log.size() ? p - at : 0;
}

// The two sides' recorded SDK calls must be the same calls with the same arguments.
void compare_sdk_logs(const Side& a, const Side& b, Verdict& v) {
    if (a.report.sdk_overflow || b.report.sdk_overflow) {
        v.add(Outcome::Inconclusive, "SDK call log full");
        return;
    }
    if (a.sdk_log == b.sdk_log) return;
    size_t pa = 0, pb = 0;
    for (uint32_t call = 0;; ++call) {
        const bool a_end = pa >= a.sdk_log.size(), b_end = pb >= b.sdk_log.size();
        if (a_end || b_end) {
            v.add(Outcome::Mismatched, "SDK calls: original made " + std::to_string(a.report.sdk_calls) + ", new " +
                                           std::to_string(b.report.sdk_calls));
            return;
        }
        const size_t na = sdk_record_words(a.sdk_log, pa), nb = sdk_record_words(b.sdk_log, pb);
        if (!na || !nb) {
            v.add(Outcome::Mismatched, "SDK call log damaged");
            return;
        }
        if (a.sdk_log[pa] != b.sdk_log[pb]) {
            v.add(Outcome::Mismatched, "SDK call " + std::to_string(call) + ": original " + g_sdk_names[a.sdk_log[pa]] +
                                           ", new " + g_sdk_names[b.sdk_log[pb]]);
            return;
        }
        if (na != nb || !std::equal(a.sdk_log.begin() + pa, a.sdk_log.begin() + pa + na, b.sdk_log.begin() + pb)) {
            size_t k = 1;
            while (k < na && k < nb && a.sdk_log[pa + k] == b.sdk_log[pb + k]) ++k;
            v.add(Outcome::Mismatched, "SDK call " + std::to_string(call) + " " + g_sdk_names[a.sdk_log[pa]] +
                                           ": arguments differ at word " + std::to_string(k) + " (" +
                                           hex(k < na ? a.sdk_log[pa + k] : 0) + " / " +
                                           hex(k < nb ? b.sdk_log[pb + k] : 0) + ")");
            return;
        }
        pa += na;
        pb += nb;
    }
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
    case Result::Xmm0:
        reg("xmm0", xmm0_low(a), xmm0_low(b));
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

// `limit`: the calling thread's lowest stack address, or 0.
void compare_memory(const Hook& hook, const Context& entry, uint32_t limit, const std::vector<uint32_t>& pages,
                    const Side& a, const Side& b, Verdict& v) {
    // Below the caller's stack pointer is the callee's scratch space; the return
    // address and the arguments belong to the callee too.
    std::vector<Skip> skips = t_runtime_skips;
    skips.push_back({limit ? limit : entry.esp - std::min<uint32_t>(entry.esp, 0x10000),
                     entry.esp + 4 + hook.function->arg_bytes});
    const auto skipped = [&](uint32_t address) {
        for (const Skip& skip : skips)
            if (skip.contains(address)) return true;
        return false;
    };
    // XBCMP_SHOW=N lists N runs of differences per report (default 6).
    static const int show = [] {
        const char* value = std::getenv("XBCMP_SHOW");
        return value ? std::atoi(value) : 6;
    }();
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
            if (shown < show) {
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
    if (shown && show > 6) {  // XBCMP_SHOW: where the caller's stack was
        char where[64];
        std::snprintf(where, sizeof where, " [esp %08x, stack limit %08x]", entry.esp, limit);
        v.details += where;
    }
}

// What a test process that timed out is doing, from /proc/PID/syscall (the coordinator is its
// ancestor, so it may read it): " (running)", " (waiting: futex)" - a lock or a wait for a
// thread the test process does not have -, another system call by number, or "".
std::string timeout_state(uint32_t pid) {
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/%u/syscall", pid);
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return "";
    char text[128] = {};
    const ssize_t n = read(fd, text, sizeof(text) - 1);
    close(fd);
    if (n <= 0) return "";
    if (std::strncmp(text, "running", 7) == 0) return " (running)";
    const long number = std::strtol(text, nullptr, 10);
    if (number == SYS_futex || number == SYS_futex_time64) return " (waiting: futex)";
    if (number == SYS_nanosleep || number == SYS_clock_nanosleep || number == SYS_clock_nanosleep_time64)
        return " (sleeping)";
    return " (in system call " + std::to_string(number) + ")";
}

// In the coordinator: forks the two test processes and compares their results.
Verdict run_test(const Hook& hook, const Context& entry, uint32_t stack_limit) {
    Verdict verdict;
    int a_out[2], a_in[2], b_out[2], b_in[2];
    if (pipe2(a_out, O_CLOEXEC) || pipe2(a_in, O_CLOEXEC) || pipe2(b_out, O_CLOEXEC) || pipe2(b_in, O_CLOEXEC)) {
        verdict.add(Outcome::Inconclusive, "no pipes");
        return verdict;
    }
    const uint64_t t_fork = now_ns();
    const pid_t child = fork();
    if (child == 0) {
        // Same memory for both: this process forks both test processes before anything
        // runs and waits for them. Both are forks of a fork, so glibc's fork handlers ran as
        // often in each (the child's handler rewrites the malloc arenas' bookkeeping): the
        // host heap is the same on both sides, which pass-through file calls rely on.
        const pid_t original = fork();
        if (original == 0)
            child_main(true, a_out[1], a_in[0], 0, {a_out[0], a_in[1], b_out[0], b_out[1], b_in[0], b_in[1]}, hook,
                       entry, stack_limit);
        const pid_t decompiled = original > 0 ? fork() : -1;
        if (decompiled == 0)
            child_main(false, b_out[1], b_in[0], 0, {a_out[0], a_out[1], a_in[0], a_in[1], b_out[0], b_in[1]}, hook,
                       entry, stack_limit);
        for (int fd : {a_out[0], a_out[1], a_in[0], a_in[1], b_out[0], b_out[1], b_in[0], b_in[1]}) close(fd);
        prctl(PR_SET_PDEATHSIG, SIGKILL);  // gone with the coordinator; the test processes with it
        for (pid_t pid : {original, decompiled})
            if (pid > 0) waitpid(pid, nullptr, 0);
        _exit(0);
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
        // The coordinator exits right after; the test processes go with it (PDEATHSIG).
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
            const std::string state = side.pid && !side.answered ? timeout_state(side.pid) : "";
            if (side.pid) kill(static_cast<pid_t>(side.pid), SIGKILL);
            if (!side.answered) verdict.add(Outcome::Inconclusive, std::string(side.what) + " timed out" + state);
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

    if (a.report.status != kOk) {
        verdict.add(Outcome::Inconclusive, signal_text(a));
        return verdict;
    }
    if (b.report.status != kOk) {
        // The original ran through; the new version reaching host code the original
        // did not need is inconclusive too (e.g. a C runtime call we do not allow).
        verdict.add(is_host_call(b.report) || b.report.status == kHarnessError || b.report.status == kHostLocked
                        ? Outcome::Inconclusive
                                                                                : Outcome::Mismatched,
                    signal_text(b));
        return verdict;
    }
    if (!complete) {
        verdict.add(Outcome::Inconclusive, "pages not received");
        return verdict;
    }
    compare_registers(hook, entry, a.result, b.result, verdict);
    // With file calls a different call explains the rest (the host heap the exports use differs
    // then): it comes first.
    const bool files = a.report.pass_calls || b.report.pass_calls;
    if (files) compare_sdk_logs(a, b, verdict);
    compare_memory(hook, entry, stack_limit, pages, a, b, verdict);
    if (!files) compare_sdk_logs(a, b, verdict);
    const uint64_t t_compared = now_ns();
    const Report& ra = a.report;
    const Report& rb = b.report;
    TestTiming& t = verdict.timing;
    t.complete = 1;
    t.ns[kFork] = span(t_fork, t_forked);
    t.ns[kStartOriginal] = span(t_fork, ra.t_entered);
    t.ns[kStartNew] = span(t_fork, rb.t_entered);  // includes the second fork
    // The two sides run side by side: the slower one counts.
    t.ns[kMaps] = std::max(span(ra.t_entered, ra.t_mapped), span(rb.t_entered, rb.t_mapped));
    t.ns[kClear] = std::max(span(ra.t_mapped, ra.t_cleared), span(rb.t_mapped, rb.t_cleared));
    t.ns[kRun] = std::max(span(ra.t_cleared, ra.t_landed), span(rb.t_cleared, rb.t_landed));
    t.ns[kCollect] = std::max(span(ra.t_landed, ra.t_collected), span(rb.t_landed, rb.t_collected));
    t.ns[kReports] = span(std::max(ra.t_collected, rb.t_collected), t_reports);
    t.ns[kPages] = span(t_reports, t_pages);
    t.ns[kCompare] = span(t_pages, t_compared);
    t.ns[kTotal] = span(t_fork, t_compared);
    t.scan_pages = ra.scan_pages;
    t.dirty_pages = pages.size();
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
    static const char* const only = std::getenv("XBCMP_ONLY");  // listed ones keep being tested
    if (g_config.enough_matches && !only && e.matched + t.matched >= g_config.enough_matches &&
        e.mismatched + t.mismatched + e.drift + t.drift == 0 && !hook.settled) {
        hook.settled = true;
        LOG_INFO("compare", "%s: %llu matched tests, none mismatched; not tested any more", hook.function->name,
                 static_cast<unsigned long long>(e.matched + t.matched));
    }
    if (verdict.outcome == Outcome::Matched) return;
    hook.last_issue = verdict.details;
    // Separate caps, so front-end inconclusive tests do not hide the first mismatches.
    uint32_t& reports = verdict.outcome == Outcome::Inconclusive ? hook.reports : hook.issue_reports;
    if (reports >= 5) return;
    ++reports;
    static const char* const kWhat[] = {"matched", "float drift", "MISMATCH", "inconclusive"};
    const auto level = verdict.outcome == Outcome::Mismatched ? platform::LogLevel::Warn : platform::LogLevel::Info;
    PLATFORM_LOG(level, "compare", "%s call %llu: %s: %s", hook.function->name, static_cast<unsigned long long>(call),
                 kWhat[static_cast<int>(verdict.outcome)], verdict.details.c_str());
}

void flush_rows();

// --- Tests beside the game: the coordinator and the collector ----------------------

// What a coordinator sends back; one write under PIPE_BUF, so it arrives whole.
struct VerdictMessage {
    uint32_t outcome;
    uint32_t details_size;
    TestTiming timing;
};
constexpr size_t kMaxDetails = 3072;
static_assert(sizeof(VerdictMessage) + kMaxDetails <= PIPE_BUF);

struct InFlight {
    int fd;  // the coordinator's verdict pipe
    pid_t pid;
    Hook* hook;
    uint64_t call;
    uint64_t t_call;
    uint64_t stall;  // the game's fork
    uint64_t deadline;
};

// Per process (a fork of the game that tests needs its own). Never destroyed: the
// collector thread waits on it until the process ends.
struct Collector {
    pid_t owner;
    int game_alive[2];  // the coordinators watch the read end: EOF = the game is gone
    int wake[2];        // new tests for the collector
    std::mutex lock;
    std::condition_variable idle;
    std::vector<InFlight> added;
    uint32_t in_flight = 0;  // under lock
};
Collector* g_collector;
std::mutex g_collector_setup;

uint32_t parallel_limit() {
    if (g_config.parallel) return g_config.parallel;
    return std::max(1u, std::thread::hardware_concurrency() / 2);
}

void finish_test(Collector& collector, const InFlight& test, const Verdict& verdict) {
    close(test.fd);
    reap_later(test.pid);
    {
        std::lock_guard lock(g_stats);
        const TestTiming& t = verdict.timing;
        if (t.complete) {
            Timing& total = g_timing;
            ++total.tests;
            for (int p = 0; p < kPhases; ++p) {
                const uint64_t ns = p == kLatency ? span(test.t_call, now_ns()) : p == kStall ? test.stall : t.ns[p];
                total.sum[p] += ns;
                total.max[p] = std::max(total.max[p], ns);
            }
            total.scan_pages += t.scan_pages;
            total.dirty_pages += t.dirty_pages;
        }
    }
    record(*test.hook, test.call, verdict);
    if (++g_tests % 64 == 0) flush_rows();
    std::lock_guard lock(collector.lock);
    --collector.in_flight;
    collector.idle.notify_all();
}

Verdict read_verdict(int fd) {
    Verdict verdict;
    alignas(8) char buffer[PIPE_BUF];
    ssize_t got;
    do got = read(fd, buffer, sizeof(buffer));
    while (got < 0 && errno == EINTR);
    VerdictMessage m;
    if (got < static_cast<ssize_t>(sizeof(m))) {
        verdict.add(Outcome::Inconclusive, "coordinator failed");
        return verdict;
    }
    std::memcpy(&m, buffer, sizeof(m));
    verdict.outcome = static_cast<Outcome>(std::min<uint32_t>(m.outcome, static_cast<uint32_t>(Outcome::Inconclusive)));
    verdict.details.assign(buffer + sizeof(m), std::min<size_t>(m.details_size, static_cast<size_t>(got) - sizeof(m)));
    verdict.timing = m.timing;
    return verdict;
}

[[noreturn]] void collect(Collector* collector) {
    std::vector<InFlight> tests;
    std::vector<pollfd> fds;
    for (;;) {
        fds.assign(1, {collector->wake[0], POLLIN, 0});
        uint64_t next = UINT64_MAX;
        for (const InFlight& test : tests) {
            fds.push_back({test.fd, POLLIN, 0});
            next = std::min(next, test.deadline);
        }
        const uint64_t now = now_ns();
        const int wait_ms = next == UINT64_MAX ? -1 : next <= now ? 0 : static_cast<int>((next - now) / 1000000 + 1);
        if (poll(fds.data(), fds.size(), wait_ms) < 0 && errno != EINTR) continue;
        if (fds[0].revents) {
            char drop[64];
            while (read(collector->wake[0], drop, sizeof(drop)) > 0) {
            }
            std::lock_guard lock(collector->lock);
            tests.insert(tests.end(), collector->added.begin(), collector->added.end());
            collector->added.clear();
        }
        const uint64_t after = now_ns();
        size_t kept = 0;
        for (size_t i = 0; i < tests.size(); ++i) {
            const InFlight& test = tests[i];
            const bool ready = i + 1 < fds.size() && fds[i + 1].fd == test.fd && fds[i + 1].revents;
            if (ready) {
                finish_test(*collector, test, read_verdict(test.fd));
            } else if (after >= test.deadline) {
                kill(test.pid, SIGKILL);  // its test processes follow (PDEATHSIG)
                Verdict verdict;
                verdict.add(Outcome::Inconclusive, "coordinator timed out");
                finish_test(*collector, test, verdict);
            } else {
                tests[kept++] = test;
            }
        }
        tests.resize(kept);
    }
}

Collector& collector() {
    std::lock_guard lock(g_collector_setup);
    if (!g_collector || g_collector->owner != getpid()) {
        auto* c = new Collector;
        c->owner = getpid();
        if (pipe2(c->game_alive, O_CLOEXEC) || pipe2(c->wake, O_CLOEXEC | O_NONBLOCK)) {
            LOG_ERROR("compare", "cannot create the collector's pipes");
            std::abort();
        }
        std::thread(collect, c).detach();
        g_collector = c;
    }
    return *g_collector;
}

bool take_slot(Collector& collector) {
    std::lock_guard lock(collector.lock);
    if (collector.in_flight >= parallel_limit()) return false;
    ++collector.in_flight;
    return true;
}

void release_slot(Collector& collector) {
    std::lock_guard lock(collector.lock);
    --collector.in_flight;
    collector.idle.notify_all();
}

[[noreturn]] void coordinate(const Hook& hook, const Context& entry, uint32_t stack_limit, int verdict_fd) {
    // The game's crash handlers must not run here (they log and flush the results).
    for (int number : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP}) signal(number, SIG_DFL);
    alarm(g_config.timeout_ms / 1000 * 2 + 5);  // in case the collector never kills it
    const Verdict verdict = run_test(hook, entry, stack_limit);
    alignas(8) char buffer[PIPE_BUF];
    VerdictMessage m{};
    m.outcome = static_cast<uint32_t>(verdict.outcome);
    m.details_size = static_cast<uint32_t>(std::min(verdict.details.size(), kMaxDetails));
    m.timing = verdict.timing;
    std::memcpy(buffer, &m, sizeof(m));
    std::memcpy(buffer + sizeof(m), verdict.details.data(), m.details_size);
    write_full(verdict_fd, buffer, sizeof(m) + m.details_size);
    _exit(0);
}

// In the game: forks the coordinator for a test of this call. False if none started.
bool start_test(Collector& collector, Hook& hook, uint64_t call, const Context& entry) {
    if (t_runtime_skips.empty()) t_runtime_skips = thread_runtime_block();
    const uint32_t stack_limit = g_config.stack_limit ? g_config.stack_limit() : 0;
    int verdict[2];
    if (pipe2(verdict, O_CLOEXEC)) return false;
    const uint64_t t_call = now_ns();
    if (g_config.before_fork) g_config.before_fork();
    const pid_t pid = fork();
    if (g_config.after_fork) g_config.after_fork();
    if (pid == 0) {
        close(verdict[0]);
        close(collector.game_alive[1]);
        g_game_alive_fd = collector.game_alive[0];
        g_verdict_fd = verdict[1];
        coordinate(hook, entry, stack_limit, verdict[1]);
    }
    close(verdict[1]);
    if (pid < 0) {
        close(verdict[0]);
        return false;
    }
    const uint64_t stall = now_ns() - t_call;
    g_test_ns += stall;
    {
        std::lock_guard lock(collector.lock);
        collector.added.push_back({verdict[0], pid, &hook, call, t_call, stall,
                                   t_call + (g_config.timeout_ms + 3000ull) * 1000000ull});
    }
    const char wake = 1;
    (void)!write(collector.wake[1], &wake, 1);
    return true;
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
    if (!hook.settled.load(std::memory_order_relaxed) && should_test(call)) {
        platform::HostFloatEnv host_float;
        Collector& c = collector();
        if (!take_slot(c))
            ++g_skipped_busy;
        else if (!start_test(c, hook, call, context))
            release_slot(c);
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

// SdkCall::args -> SdkSpec; false if malformed.
bool parse_sdk_args(const char* text, SdkSpec& spec) {
    std::istringstream words(text);
    spec.arg_count = spec.stack_slots = 0;
    spec.result = kResultEax;
    for (std::string word; words >> word;) {
        if (word == "ret:al" || word == "ret:void") {  // pass-through calls: the result record
            spec.result = word == "ret:al" ? kResultAl : kResultNone;
            continue;
        }
        if (spec.arg_count == kMaxSdkArgs) return false;
        SdkArg& arg = spec.args[spec.arg_count];
        arg = {};
        const std::string where = word.substr(0, word.find(':'));
        if (where == "ecx") {
            arg.where = kSdkEcx;
        } else if (where == "edx") {
            arg.where = kSdkEdx;
        } else if (where == "s" || where == "x") {
            arg.where = kSdkStack;
            arg.slot = static_cast<uint8_t>(spec.stack_slots++);
            if (where == "x") arg.kind = kSdkUnlogged;
        } else {
            return false;
        }
        if (const size_t colon = word.find(':'); colon != std::string::npos) {
            const std::string size = word.substr(colon + 1);
            unsigned from = 0, bytes = 0, size_from = 0;
            if (std::sscanf(size.c_str(), "a%u*a%u", &from, &size_from) == 2) {
                arg.kind = kSdkProductBytes;
                arg.count_from = static_cast<uint8_t>(from);
                arg.size_from = static_cast<uint8_t>(size_from);
            } else if (std::sscanf(size.c_str(), "a%u*%u", &from, &bytes) == 2) {
                arg.kind = kSdkScaledBytes;
                arg.count_from = static_cast<uint8_t>(from);
            } else if (std::sscanf(size.c_str(), "%u", &bytes) == 1) {
                arg.kind = kSdkBytes;
            } else {
                return false;
            }
            arg.bytes = bytes;
        }
        ++spec.arg_count;
    }
    for (uint32_t i = 0; i < spec.arg_count; ++i)
        if ((spec.args[i].kind == kSdkScaledBytes || spec.args[i].kind == kSdkProductBytes) &&
            (spec.args[i].count_from >= spec.arg_count || spec.args[i].size_from >= spec.arg_count))
            return false;
    return true;
}

void install(std::span<const game::Function> functions, const Config& config) {
    g_config = config;
    // The protection pass below walks these in address order.
    std::erase_if(g_config.host_code, [](const auto& range) { return range.first >= range.second; });
    std::sort(g_config.host_code.begin(), g_config.host_code.end());
    // XBCMP_EVERY=N: test every N-th call after the first ones (with XBCMP_ONLY, to
    // reach calls the default sampling skips).
    if (const char* every = std::getenv("XBCMP_EVERY")) g_config.every = static_cast<uint32_t>(std::atoi(every));
    // XBCMP_TIMEOUT_MS=N: a longer limit per test (long synchronous loads: SV_SpawnServer).
    if (const char* timeout = std::getenv("XBCMP_TIMEOUT_MS")) {
        const int ms = std::atoi(timeout);
        if (ms > 0) g_config.timeout_ms = static_cast<uint32_t>(ms);
    }
    // XBCMP_ORIGINAL_TWICE=1: both test processes run the original (a determinism check of the
    // harness and its pass-through calls: every test must match). The counts go into
    // functions_twice.csv beside the results file instead (not tracked; delete it after).
    if (const char* twice = std::getenv("XBCMP_ORIGINAL_TWICE"); twice && *twice == '1') {
        g_original_twice = true;
        if (!g_config.results.empty()) g_config.results.replace_filename("functions_twice.csv");
        LOG_INFO("compare", "XBCMP_ORIGINAL_TWICE: the original runs on both sides; counts go to %s",
                 g_config.results.string().c_str());
    }
    // XBCMP_POKE=ADDR=VALUE[,ADDR=VALUE]: words written in every test process before the
    // test, on both sides (Stream_LoadSync: 0x008bfd80=0, the file system's single-thread
    // mode, so the caller pumps the devices - a test process has no file request thread).
    g_poke_count = 0;
    if (const char* poke = std::getenv("XBCMP_POKE")) {
        for (const char* at = poke; *at && g_poke_count < std::size(g_pokes);) {
            char* end = nullptr;
            const auto where = static_cast<uint32_t>(std::strtoul(at, &end, 0));
            if (*end != '=') break;
            const auto value = static_cast<uint32_t>(std::strtoul(end + 1, &end, 0));
            g_pokes[g_poke_count][0] = where;
            g_pokes[g_poke_count++][1] = value;
            LOG_INFO("compare", "XBCMP_POKE: [%08x] = %08x in test processes", where, value);
            if (*end != ',') break;
            at = end + 1;
        }
    }
    xbcmp_child_stack_top = addr(xbcmp_child.stack + sizeof(xbcmp_child.stack));
    g_sdk_spec_count = 0;
    g_sdk_names.clear();
    for (const SdkCall& call : config.sdk_calls) {
        SdkSpec spec{};
        spec.host_address = call.host_address;
        spec.stand_in = call.stand_in;
        spec.value = call.value;
        if (!call.host_address || g_sdk_spec_count == kMaxSdkSpecs || !parse_sdk_args(call.args, spec) ||
            (call.stand_in == SdkStandIn::ReturnAddRef && !call.value)) {
            LOG_WARN("compare", "%s: bad SDK call spec \"%s\"; not recorded", call.name, call.args);
            continue;
        }
        g_sdk_specs[g_sdk_spec_count++] = spec;
        g_sdk_names.push_back(call.name);
    }
    // A pass-through call's result (EAX) is a record of its own after the call: calls made
    // inside it (APCs) come in between. Its address is never a fault address (xbcmp_text).
    g_pass_result_spec = 0xFFFFFFFF;
    if (g_sdk_spec_count < kMaxSdkSpecs) {
        SdkSpec spec{};
        spec.host_address = addr(reinterpret_cast<const void*>(&xbcmp_pass_return));
        if (parse_sdk_args("s", spec)) {
            g_pass_result_spec = g_sdk_spec_count;
            g_sdk_specs[g_sdk_spec_count++] = spec;
            g_sdk_names.push_back("(result)");
        }
    }
    int replaced = 0, settled = 0, kept_original = 0;
    const std::map<uint32_t, Row> earlier =
        config.enabled && config.enough_matches ? read_rows(config.results) : std::map<uint32_t, Row>{};
    for (const game::Function& function : functions) {
        const uint32_t address = config.multiplayer ? function.multiplayer : function.campaign;
        if (!address) continue;
        if (function.status == game::Status::Replaced) {
            if (keeps_original(function)) {
                ++kept_original;
                continue;
            }
            install_jump(address, function.code);
            g_replaced.push_back(&function);
            ++replaced;
            continue;
        }
        if (!config.enabled) continue;
        // XBCMP_ONLY=name,name: hook just these, settled or not (a closer look at one).
        static const char* const only = std::getenv("XBCMP_ONLY");
        const bool listed = only && listed_in(only, function.name);
        if (only && !listed) continue;
        Counts before;
        if (const auto row = earlier.find(address); row != earlier.end() && row->second.name == function.name) {
            const Row& r = row->second;
            before = {r.calls, r.tested, r.matched, r.drift, r.mismatched, r.inconclusive};
            if (!listed && r.matched >= config.enough_matches && r.mismatched == 0 && r.drift == 0) {
                ++settled;
                continue;
            }
        }

        auto* entry = reinterpret_cast<uint8_t*>(uintptr_t{address});
        uint32_t moved = 0;
        bool movable = true;
        while (moved < 5) {
            uint32_t length = instruction_length(entry + moved), target = 0;
            // A conditional branch or relative CALL/JMP moves too, unless it jumps back
            // into the bytes the hook overwrites.
            if (!length && (!(conditional_branch(entry + moved, length, target) ||
                              relative_transfer(entry + moved, length, target)) ||
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
        if (branches_into_entry(address, moved)) {
            LOG_WARN("compare", "%s: its code branches into the hooked bytes %08x..%08x; not tested", function.name,
                     address + 1, address + moved - 1);
            continue;
        }
        uint8_t* code = alloc_code(moved * 3 + 5 + 32);  // a 2-byte branch becomes 6, a CALL 10
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
    if (kept_original)
        LOG_INFO("compare", "%s: the originals run (%d decompiled functions not installed)",
                 release_map().identity() ? "XBCMP_ORIGINALS" : "another release of the game", kept_original);
}

namespace {

// The return address slot of the forced call being parsed ("&N" values).
uint32_t g_force_esp;

// One force_call argument value: a number, "[E]" (the title word at E, read without
// faulting), "&N" (the address of stack word N in the test processes: a stack word can
// stand in for a field of an object the call needs), then any "+N" / "-N" / "*N" (right to left:
// "A+B*C" is A + B * C, "A*B+C" is A * (B + C)).
bool parse_value(std::string_view& text, uint32_t& value, std::string& error) {
    if (!text.empty() && text[0] == '&') {
        text.remove_prefix(1);
        const std::string digits(text.substr(0, text.find_first_not_of("0123456789")));
        if (digits.empty()) {
            error = "& needs a stack word index";
            return false;
        }
        value = g_force_esp + 4 + 4 * static_cast<uint32_t>(std::stoul(digits));
        text.remove_prefix(digits.size());
    } else if (!text.empty() && text[0] == '[') {
        text.remove_prefix(1);
        uint32_t at = 0;
        if (!parse_value(text, at, error)) return false;
        if (text.empty() || text[0] != ']') {
            error = "missing ]";
            return false;
        }
        text.remove_prefix(1);
        iovec local{&value, 4}, remote{reinterpret_cast<void*>(uintptr_t{at}), 4};
        if (process_vm_readv(getpid(), &local, 1, &remote, 1, 0) != 4) {
            error = "cannot read " + hex(at);
            return false;
        }
    } else {
        const std::string digits(text.substr(0, text.find_first_of(",]+-*=", 1)));
        char* end = nullptr;
        value = static_cast<uint32_t>(std::strtoul(digits.c_str(), &end, 0));
        if (digits.empty() || *end) {
            error = "not a number: " + digits;
            return false;
        }
        text.remove_prefix(digits.size());
    }
    while (!text.empty() && (text[0] == '+' || text[0] == '-' || text[0] == '*')) {
        const char op = text[0];
        text.remove_prefix(1);
        uint32_t offset = 0;
        if (!parse_value(text, offset, error)) return false;
        value = op == '-' ? value - offset : op == '*' ? value * offset : value + offset;
    }
    return true;
}

}  // namespace

const uint8_t* in_test_flag() { return &xbcmp_in_test; }

ForceResult force_call(const char* name, const char* args, const char* when, const TitleRegisters& registers,
                       uint32_t esp, std::string& error) {
    Context context{};
    asm volatile("fxsave %0" : "=m"(context.fx));  // the title's x87/SSE modes: no host float code ran yet
    Hook* hook = nullptr;
    for (const auto& h : g_hooks)
        if (std::strcmp(h->function->name, name) == 0) hook = h.get();
    if (!g_config.enabled || !hook) {
        error = "not hooked (settled, not Compare, or no --compare)";
        return ForceResult::Failed;
    }
    if (when && *when) {
        std::string_view text = when;
        uint32_t value = 0;
        bool parsed = parse_value(text, value, error);
        // "?A=B": the call is made on frames where A equals B.
        if (parsed && !text.empty() && text[0] == '=') {
            text.remove_prefix(1);
            uint32_t other = 0;
            parsed = parse_value(text, other, error);
            value = value == other;
        }
        if (!parsed || !text.empty()) {
            // A guard that cannot be read yet (the objects it walks do not exist): later.
            if (error.starts_with("cannot read")) return ForceResult::Busy;
            if (error.empty()) error = "unexpected " + std::string(text);
            return ForceResult::Failed;
        }
        if (!value) return ForceResult::Busy;
    }
    context.eax = registers.eax;
    context.ecx = registers.ecx;
    context.edx = registers.edx;
    context.ebx = registers.ebx;
    context.ebp = registers.ebp;
    context.esi = registers.esi;
    context.edi = registers.edi;
    context.eflags = registers.eflags;
    context.esp = esp;
    context.target = hook->trampoline;
    g_force_esp = esp;
    std::string_view text = args ? args : "";
    while (!text.empty()) {
        uint32_t* to = nullptr;
        for (auto [prefix, reg] : {std::pair{"eax=", &context.eax}, {"ecx=", &context.ecx}, {"edx=", &context.edx},
                                   {"ebx=", &context.ebx}, {"esi=", &context.esi}, {"edi=", &context.edi},
                                   {"ebp=", &context.ebp}})
            if (text.starts_with(prefix)) {
                text.remove_prefix(4);
                to = reg;
            }
        if (!to) {
            if (context.stack_words == std::size(context.stack)) {
                error = "more than 32 stack words";
                return ForceResult::Failed;
            }
            to = &context.stack[context.stack_words++];
        }
        if (!parse_value(text, *to, error)) return ForceResult::Failed;
        if (!text.empty() && text[0] != ',') {
            error = "unexpected " + std::string(text);
            return ForceResult::Failed;
        }
        if (!text.empty()) text.remove_prefix(1);
    }
    platform::HostFloatEnv host_float;
    Collector& c = collector();
    if (!take_slot(c)) return ForceResult::Busy;
    if (!start_test(c, *hook, hook->calls.load(std::memory_order_relaxed), context)) {
        release_slot(c);
        return ForceResult::Busy;
    }
    error = "eax=" + hex(context.eax) + " ecx=" + hex(context.ecx) + " edx=" + hex(context.edx) + " esp=" + hex(esp);
    for (uint32_t i = 0; i < context.stack_words; ++i) error += " " + hex(context.stack[i]);
    return ForceResult::Started;
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

void drain() {
    Collector* c = g_collector;
    if (!c || c->owner != getpid()) return;
    std::unique_lock lock(c->lock);
    c->idle.wait_for(lock, std::chrono::milliseconds(g_config.timeout_ms + 2000),
                     [c] { return c->in_flight == 0; });
}

void flush() {
    drain();
    flush_rows();
}

namespace {

void flush_rows() {
    std::lock_guard lock(g_stats);
    if (g_config.results.empty() || (g_hooks.empty() && g_replaced.empty())) return;
    // Several runs may share the file (missions in parallel): read, merge and replace it
    // under an exclusive lock, through a temporary file of this process's own.
    const std::string lock_path = g_config.results.string() + ".lock";
    const int lock_fd = ::open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (lock_fd >= 0) ::flock(lock_fd, LOCK_EX);
    struct Unlock {
        int fd;
        ~Unlock() {
            if (fd >= 0) ::close(fd);  // releases the lock
        }
    } unlock{lock_fd};
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
    const std::filesystem::path temporary = g_config.results.string() + "." + std::to_string(::getpid()) + ".tmp";
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
        LOG_INFO("compare", "%llu tests, the game waited %.1f ms per test, up to %u at once (%llu sampled calls "
                 "not tested: all busy); counts in %s",
                 static_cast<unsigned long long>(tests),
                 static_cast<double>(g_test_ns.load()) / 1e6 / static_cast<double>(tests), parallel_limit(),
                 static_cast<unsigned long long>(g_skipped_busy.load()), g_config.results.string().c_str());
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

}  // namespace

}  // namespace loader::compare
