// Decompiled functions that never run in the shipped campaign (or not often enough to
// be tested there), checked offline against default.xbe's own code. The image is mapped
// at its addresses, so the original runs as it is and our C reaches the title's globals
// and functions where it expects them. tools/leafscan.py --emit picks the functions that
// need nothing but memory (leaf_functions.inc).
//
// Each input is random: registers, XMM registers, stack arguments and the globals the
// function reads hold small integers, bit masks, floats, random words or pointers into a
// 16 KB arena whose words are drawn the same way (pointer chains to any depth). Both
// versions run from the same state; compared as the in-game harness does
// (docs/comparison-harness.md): the result by kind, EBX/ESI/EDI/EBP, ESP, DF, the x87
// control word, stack and values, MXCSR, the stack arguments, the arena and every title
// data page either side wrote. Inputs the original crashes or hangs on are skipped. The
// original is single-stepped on some inputs: the share of its instructions reached says
// how well the random inputs cover it.
//
// Needs the game's default.xbe: COD_BRO_XBE, else the path CMake passes in; skipped
// (exit 77) without it. LEAF_ITERS=N inputs per function (default 4000),
// LEAF_ONLY=name,name to run only those, LEAF_VERBOSE=1 to list where the original crashed,
// LEAF_JOBS=N worker processes (default half the CPUs, at most 4),
// LEAF_DUMP=DIR to write each function's first mismatch to DIR/<name>.case (the input) and
// <name>.<arena>.original/ours.bin (the arena after each version); LEAF_REPLAY=DIR/<name>.case
// runs that input once more (LEAF_ONLY=<name>; LEAF_REPLAY_STACK=k=value changes stack word k:
// shorten a particle loop's end to find the first iteration that differs),
// LEAF_CSV=tools/data/offline.csv to record the results there (rows of other functions kept;
// tools/progress.py reads it).

#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <ucontext.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "compiler.h"
#include "functions.h"

namespace {

struct LeafFunction {
    const char* name;
    uint32_t address, size, instructions;
    std::vector<uint32_t> globals;
};

const LeafFunction kLeafFunctions[] = {
#include "leaf_functions.inc"
};

constexpr uint32_t kImageBase = 0x00010000;
constexpr uint32_t kPage = 0x1000;
constexpr uint32_t kChunk = 0x400;  // the arena is 32 chunks; a pointer points near a chunk's start
constexpr uint32_t kArenaSize = 32 * kChunk;

// The call: registers and stack in, everything a caller can see out.
struct alignas(16) Ctx {
    uint8_t fx_in[512];   // 0: FXRSTOR before the call
    uint8_t fx_out[512];  // 512: FXSAVE after it
    uint32_t in[6];       // 1024: eax ecx edx ebx esi edi
    uint32_t out[7];      // 1048: eax ecx edx ebx esi edi esp
    uint32_t target;      // 1076
    uint32_t nstack;      // 1080
    uint32_t stack[8];    // 1084
    uint32_t pad[8];      // 1116
    uint32_t host_esp;    // 1148
    uint32_t esp_before;  // 1152
    uint32_t eflags_out;  // 1156: EFLAGS (pushed after the stack arguments are dead)
    uint32_t trace;       // 1160
    uint32_t stack_top;   // 1164: the calls run on their own stack (the host's is at 0xff8xxxxx,
                          // where random words such as -inf (0xff800000) would point)
};
static_assert(offsetof(Ctx, in) == 1024 && offsetof(Ctx, out) == 1048 && offsetof(Ctx, target) == 1076);
static_assert(offsetof(Ctx, stack_top) == 1164);
static_assert(offsetof(Ctx, stack) == 1084 && offsetof(Ctx, host_esp) == 1148 && offsetof(Ctx, trace) == 1160);

}  // namespace

extern "C" void leaf_call(Ctx* ctx);

// The stack arguments are pushed below a zeroed 16 KB (like xbcmp_resume's dead stack), the
// registers loaded last; TF is set just before the call when tracing (the trap handler
// clears it once execution leaves the title image).
PLATFORM_ASM_ENTRY(leaf_call,
                   "push %ebp\n push %ebx\n push %esi\n push %edi\n"
                   "mov 20(%esp), %ebp\n"
                   "mov %esp, 1148(%ebp)\n"
                   "mov 1164(%ebp), %esp\n and $-16, %esp\n"
                   "cld\n lea -16384(%esp), %edi\n mov $4096, %ecx\n xor %eax, %eax\n rep stosl\n"
                   "mov 1080(%ebp), %ecx\n"
                   "1: test %ecx, %ecx\n jz 2f\n pushl 1080(%ebp,%ecx,4)\n dec %ecx\n jmp 1b\n"
                   "2: mov %esp, 1152(%ebp)\n"
                   "fxrstor 0(%ebp)\n"
                   "mov 1024(%ebp), %eax\n mov 1032(%ebp), %edx\n mov 1036(%ebp), %ebx\n"
                   "mov 1040(%ebp), %esi\n mov 1044(%ebp), %edi\n mov 1028(%ebp), %ecx\n"
                   "testl $1, 1160(%ebp)\n jz 3f\n pushf\n orl $0x100, (%esp)\n popf\n"
                   "3: call *1076(%ebp)\n"
                   "mov %eax, 1048(%ebp)\n mov %ecx, 1052(%ebp)\n mov %edx, 1056(%ebp)\n mov %ebx, 1060(%ebp)\n"
                   "mov %esi, 1064(%ebp)\n mov %edi, 1068(%ebp)\n mov %esp, 1072(%ebp)\n"
                   "pushf\n popl 1156(%ebp)\n"
                   "fxsave 512(%ebp)\n"
                   "fninit\n"
                   "mov 1148(%ebp), %esp\n"
                   "pop %edi\n pop %esi\n pop %ebx\n pop %ebp\n ret")

namespace {

struct Xbe {
    std::vector<uint8_t> data;
    struct Section {
        uint32_t va, vsize, raw, rsize;
        std::string name;
    };
    std::vector<Section> sections;

    bool load(const char* path) {
        FILE* f = std::fopen(path, "rb");
        if (!f) return false;
        std::fseek(f, 0, SEEK_END);
        data.resize(static_cast<size_t>(std::ftell(f)));
        std::fseek(f, 0, SEEK_SET);
        const bool ok = std::fread(data.data(), 1, data.size(), f) == data.size();
        std::fclose(f);
        if (!ok) return false;
        const uint32_t base = dword_at(0x104);
        const uint32_t count = dword_at(0x11c), headers = dword_at(0x120);
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t o = headers - base + i * 0x38;
            const uint32_t name_at = dword_at(o + 20) - base;
            sections.push_back({dword_at(o + 4), dword_at(o + 8), dword_at(o + 12), dword_at(o + 16),
                                reinterpret_cast<const char*>(data.data() + name_at)});
        }
        return true;
    }
    uint32_t dword_at(uint32_t offset) const {
        uint32_t v;
        std::memcpy(&v, data.data() + offset, 4);
        return v;
    }
};

// Title data pages (.data onwards, plus the page .rdata shares with .data): read-only while a
// function runs; the first write to each page faults, the handler makes it writable and
// records it.
uint32_t g_data_lo, g_data_hi, g_image_hi;
std::vector<uint8_t> g_data_base;  // the pages as mapped (the image's .data, zeroed .bss)
std::vector<uint8_t> g_dirty;      // per data page: written during this run
std::vector<uint32_t> g_dirty_list;
sigjmp_buf g_crash_jump;
volatile sig_atomic_t g_in_call = 0;
uint32_t g_trace_lo, g_trace_hi;
std::set<uint32_t> g_reached;
uint32_t g_crash_eip, g_crash_address;
// Arena write log (rerun of a mismatch): the arena is read-only, each store faults, is
// single-stepped, and the bytes it changed are recorded in order.
struct ArenaWrite {
    uint32_t address, store;  // store: its number in the run
    uint8_t value;
};
constexpr uint32_t kLogWindow = 64;  // bytes compared around a store (rep string steps, wide moves)
std::vector<ArenaWrite> g_log;       // reserved up front: never reallocates in the handler
bool g_logging = false, g_log_full = false;
uint32_t g_log_lo, g_log_hi, g_log_at, g_log_stores;  // arena range; the store being stepped (0: none)
uint8_t g_log_before[kLogWindow];
const char* volatile g_current = nullptr;  // the function a worker is testing
std::vector<std::pair<uint32_t, uint32_t>> g_host_ranges;  // inclusive

uint32_t g_host_lowest = 0xffffffff;

// The test's own memory a call may write (arena, call stack, stand-in heap) sits in the middle
// of a no-access reservation: a pointer computed from random fields (base + index * stride)
// that lands within kSlack of it faults inside the call instead of corrupting ld.so, libc or
// malloc's heap next to it. `at` (a replayed case's arena): exactly there, slack one page.
constexpr uint32_t kSlack = 16u << 20;
uint8_t* map_guarded(uint32_t size, uint32_t at = 0) {
    const uint32_t slack = at ? 0x1000 : kSlack;
    void* base = mmap(reinterpret_cast<void*>(static_cast<uintptr_t>(at ? at - slack : 0)), size + 2 * slack, PROT_NONE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | (at ? MAP_FIXED_NOREPLACE : 0), -1, 0);
    if (base == MAP_FAILED) return nullptr;
    uint8_t* inside = static_cast<uint8_t*>(base) + slack;
    mprotect(inside, size, PROT_READ | PROT_WRITE);
    return inside;
}

// Stand-ins: title allocators whose state lives behind randomized globals (Z_Malloc's heap)
// or in the kernel (XPhysicalAlloc). Their entries jump here (push imm32; ret), so the
// original and our C (which calls them by title address) get the same answers: a bump heap
// reset before each run, zeroed, compared after it like the arena, and a log of the frees.
// Its own mapping between no-access guard pages (map_image): in .bss it sat just below
// malloc's heap, and a write past a block corrupted the worker's own heap.
constexpr uint32_t kHeapSize = 1 << 20;
uint8_t* g_heap;
uint32_t g_heap_used;
uint32_t g_frees[256], g_free_count;
void* heap_alloc(uint32_t size, uint32_t align) {
    align = align < 16 ? 16 : align;
    if ((align & (align - 1)) || size > kHeapSize) return nullptr;
    const uint32_t at = (g_heap_used + align - 1) & ~(align - 1);
    if (at + size > kHeapSize) return nullptr;
    g_heap_used = at + size;
    return g_heap + at;
}
void heap_free(void* pointer) {
    if (g_free_count < 256) g_frees[g_free_count] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(pointer));
    ++g_free_count;
}
__attribute__((force_align_arg_pointer)) void* XB_CDECL standin_z_malloc(uint32_t size) { return heap_alloc(size, 16); }
__attribute__((force_align_arg_pointer)) void* XB_STDCALL standin_physical_alloc(uint32_t size, uint32_t, uint32_t align, uint32_t) {
    return heap_alloc(size, align ? align : kPage);
}
__attribute__((force_align_arg_pointer)) void XB_STDCALL standin_physical_free(void* pointer) { heap_free(pointer); }
__attribute__((force_align_arg_pointer)) void* XB_CDECL standin_aligned_malloc(uint32_t size, uint32_t align) { return heap_alloc(size, align); }
__attribute__((force_align_arg_pointer)) void XB_CDECL standin_aligned_free(void* pointer) { heap_free(pointer); }
// The CRT's printf family reaches _getptd (thread data through FS): host formatting instead
// (an i386 va_list is the same pointer to the arguments). %n is refused (-1, nothing written):
// random formats would make it write anywhere.
bool writes_count(const char* format) {
    for (const char* p = format; *p; ++p) {
        if (*p != '%') continue;
        ++p;
        while (*p && std::strchr("-+ #0123456789.*hlLIqjzt", *p)) ++p;
        if (*p == 'n') return true;
        if (!*p) break;
    }
    return false;
}
__attribute__((force_align_arg_pointer)) int32_t XB_CDECL standin_vsprintf(char* out, const char* format, va_list args) {
    return writes_count(format) ? -1 : std::vsprintf(out, format, args);
}
__attribute__((force_align_arg_pointer)) int32_t XB_CDECL standin_sprintf(char* out, const char* format, ...) {
    if (writes_count(format)) return -1;
    va_list args;
    va_start(args, format);
    const int32_t n = std::vsprintf(out, format, args);
    va_end(args);
    return n;
}
// MSVC's _vsnprintf: no terminator when the text fills the buffer, -1 when it does not fit.
__attribute__((force_align_arg_pointer)) int32_t XB_CDECL standin_vsnprintf(char* out, uint32_t count, const char* format, va_list args) {
    if (writes_count(format)) return -1;
    va_list copy;
    va_copy(copy, args);
    const int n = std::vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    if (n < 0) return -1;
    std::vector<char> text(static_cast<size_t>(n) + 1);
    std::vsnprintf(text.data(), text.size(), format, args);
    if (static_cast<uint32_t>(n) > count) {
        std::memcpy(out, text.data(), count);
        return -1;
    }
    std::memcpy(out, text.data(), static_cast<uint32_t>(n) < count ? static_cast<size_t>(n) + 1 : static_cast<size_t>(n));
    return n;
}
// tools/leafscan.py's STANDINS lists the same addresses (callees it does not scan).
const std::pair<uint32_t, void*> kStandins[] = {
    {0x00042cb0, reinterpret_cast<void*>(&standin_z_malloc)},
    {0x001e763a, reinterpret_cast<void*>(&standin_physical_alloc)},
    {0x001e76ba, reinterpret_cast<void*>(&standin_physical_free)},
    {0x001f114f, reinterpret_cast<void*>(&standin_aligned_malloc)},
    {0x001f113a, reinterpret_cast<void*>(&standin_aligned_free)},
    {0x001eea95, reinterpret_cast<void*>(&standin_vsprintf)},
    {0x001ee901, reinterpret_cast<void*>(&standin_sprintf)},
    {0x001efce2, reinterpret_cast<void*>(&standin_vsnprintf)},
};

bool in_host_memory(uint32_t v) {
    if (v < g_host_lowest) return false;
    for (const auto& [lo, hi] : g_host_ranges)
        if (v >= lo && v <= hi) return true;
    return false;
}

void on_segv(int sig, siginfo_t* info, void* context) {
    const auto address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(info->si_addr));
    if (sig == SIGSEGV && g_in_call && address >= g_data_lo && address < g_data_hi) {
        const uint32_t page = (address - g_data_lo) / kPage;
        if (!g_dirty[page]) {
            g_dirty[page] = 1;
            g_dirty_list.push_back(page);
            mprotect(reinterpret_cast<void*>(static_cast<uintptr_t>(g_data_lo + page * kPage)), kPage,
                     PROT_READ | PROT_WRITE);
            return;
        }
    }
    if (sig == SIGSEGV && g_in_call && g_logging && !g_log_at && address >= g_log_lo && address < g_log_hi) {
        g_log_at = address;
        const uint32_t n = std::min(kLogWindow, g_log_hi - address);
        std::memcpy(g_log_before, reinterpret_cast<void*>(static_cast<uintptr_t>(address)), n);
        const uint32_t first = address & ~(kPage - 1), last = std::min(g_log_hi, address + n);
        mprotect(reinterpret_cast<void*>(static_cast<uintptr_t>(first)), last - first, PROT_READ | PROT_WRITE);
        static_cast<ucontext_t*>(context)->uc_mcontext.gregs[REG_EFL] |= 0x100;
        return;
    }
    if (g_in_call) {
        g_crash_eip = static_cast<uint32_t>(static_cast<ucontext_t*>(context)->uc_mcontext.gregs[REG_EIP]);
        g_crash_address = address;
        g_in_call = 0;
        siglongjmp(g_crash_jump, sig);
    }
    if (g_current) {
        char text[160];
        const int n = std::snprintf(text, sizeof text, "leaf_test: worker crashed outside a call (signal %d) after %s\n", sig,
                                    g_current);
        if (n > 0) (void)!write(2, text, static_cast<size_t>(n));
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

void on_trap(int, siginfo_t*, void* context) {
    auto* uc = static_cast<ucontext_t*>(context);
    const auto eip = static_cast<uint32_t>(uc->uc_mcontext.gregs[REG_EIP]);
    if (g_log_at) {  // the logged store has run: record what changed, protect the arena again
        const uint32_t n = std::min(kLogWindow, g_log_hi - g_log_at);
        const auto* now = reinterpret_cast<const uint8_t*>(static_cast<uintptr_t>(g_log_at));
        for (uint32_t i = 0; i < n; ++i) {
            if (now[i] == g_log_before[i]) continue;
            if (g_log.size() == g_log.capacity()) g_log_full = true;
            else g_log.push_back({g_log_at + i, g_log_stores, now[i]});
        }
        ++g_log_stores;
        mprotect(reinterpret_cast<void*>(static_cast<uintptr_t>(g_log_lo)), g_log_hi - g_log_lo, PROT_READ);
        g_log_at = 0;
        uc->uc_mcontext.gregs[REG_EFL] &= ~0x100;  // log runs are not traced
        return;
    }
    if (eip >= g_trace_lo && eip < g_trace_hi) g_reached.insert(eip);
    if (eip < kImageBase || eip >= g_image_hi) uc->uc_mcontext.gregs[REG_EFL] &= ~0x100;
}

void protect_data(int prot) {
    mprotect(reinterpret_cast<void*>(static_cast<uintptr_t>(g_data_lo)), g_data_hi - g_data_lo, prot);
}

uint8_t* data_page(uint32_t page) { return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(g_data_lo + page * kPage)); }

bool map_image(const Xbe& xbe) {
    uint32_t hi = 0;
    for (const auto& s : xbe.sections) hi = std::max(hi, s.va + std::max(s.vsize, s.rsize));
    g_image_hi = (hi + kPage - 1) & ~(kPage - 1);
    void* at = mmap(reinterpret_cast<void*>(static_cast<uintptr_t>(kImageBase)), g_image_hi - kImageBase,
                    PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (at != reinterpret_cast<void*>(static_cast<uintptr_t>(kImageBase))) {
        std::perror("leaf_test: mmap of the image");
        return false;
    }
    std::memcpy(at, xbe.data.data(), std::min<size_t>(xbe.data.size(), 0x1000));
    uint32_t data_start = 0;
    for (const auto& s : xbe.sections) {
        std::memcpy(reinterpret_cast<void*>(static_cast<uintptr_t>(s.va)), xbe.data.data() + s.raw, s.rsize);
        if (s.name == ".data") data_start = s.va;
    }
    if (!data_start) return false;
    g_data_lo = data_start & ~(kPage - 1);
    g_data_hi = g_image_hi;
    for (const auto& [address, standin] : kStandins) {
        uint8_t* code = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(address));
        const uint32_t target = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(standin));
        code[0] = 0x68;  // push target; ret
        std::memcpy(code + 1, &target, 4);
        code[5] = 0xc3;
    }
    g_heap = map_guarded(kHeapSize);
    if (!g_heap) return false;
    // Code read-only and executable, .rdata read-only; the data pages are switched per run.
    mprotect(at, g_data_lo - kImageBase, PROT_READ | PROT_EXEC);
    g_data_base.assign(reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(g_data_lo)),
                       reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(g_data_hi)));
    g_dirty.assign((g_data_hi - g_data_lo) / kPage, 0);
    return true;
}

// Values: what title structures hold.
struct Gen {
    std::mt19937 rng;
    uint8_t* arena;
    explicit Gen(uint32_t seed, uint8_t* a) : rng(seed), arena(a) {}

    uint32_t below(uint32_t n) { return rng() % n; }
    float any_float() {
        static const float kSpecial[] = {0.0f, -0.0f, 1.0f, -1.0f, 0.5f, 2.0f, 255.0f, 1e-3f, 1e30f, -1e30f,
                                         INFINITY, -INFINITY, NAN, 1e-40f, 3.0e-39f, 0.99999994f};
        switch (below(4)) {
        case 0: return kSpecial[below(sizeof kSpecial / sizeof *kSpecial)];
        case 1: return std::uniform_real_distribution<float>(-1.5f, 1.5f)(rng);
        case 2: return std::uniform_real_distribution<float>(-2000.0f, 2000.0f)(rng);
        default: return static_cast<float>(static_cast<int32_t>(below(512)) - 256);
        }
    }
    // Pointers point near a chunk's start (structures up to 1 KB do not overlap the next
    // object partly). The arguments (registers, stack words, globals) each get a chunk of
    // their own (next_chunk): two arguments are never the same object, which callers in
    // the game never pass either (self == src); words inside the arena point anywhere.
    uint32_t chunk_order[32];
    uint32_t next_chunk = 32;
    void start_arguments() {
        for (uint32_t i = 0; i < 32; ++i) chunk_order[i] = i;
        std::shuffle(chunk_order, chunk_order + 32, rng);
        next_chunk = 0;
    }
    uint32_t pointer_in(uint32_t chunk) {
        const uint32_t offset = below(4) ? 0 : below(0x40) & (below(5) ? ~15u : ~3u);
        return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arena + chunk * kChunk + offset));
    }
    uint32_t pointer() { return pointer_in(below(32)); }
    uint32_t argument_pointer() { return pointer_in(next_chunk < 32 ? chunk_order[next_chunk++] : below(32)); }
    uint32_t pointer_percent = 30;  // per input: how often a word is a pointer into the arena
    uint32_t value(bool argument = false) {
        if (below(100) < pointer_percent) return argument ? argument_pointer() : pointer();
        for (;;) {
            const uint32_t v = any_value();
            if (!in_host_memory(v)) return v;
        }
    }
    uint32_t any_value() {
        switch (below(7) + 3) {
        case 3: case 4: return below(4) ? below(4) : below(64) - 1;
        case 5: { const float f = any_float(); uint32_t v; std::memcpy(&v, &f, 4); return v; }
        case 6: { const float f = std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); uint32_t v; std::memcpy(&v, &f, 4); return v; }
        case 7: return 1u << below(32) | (below(2) ? 1u << below(32) : 0);
        case 8: return below(2) ? 0 : 0xffffffff;
        default: return rng();
        }
    }
    // Learned per function: chunk offsets (words) the original dereferenced. A fault near
    // a word at that offset adds 1/n (n words explained the fault); from 1 on, the word is
    // mostly a pointer in every chunk (structures start near a chunk's start).
    float pointer_score[kChunk / 4] = {};
    void learn_fault(const uint8_t* arena_in, uint32_t address) {
        const uint32_t lo = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arena)), hi = lo + kArenaSize;
        uint32_t found[8], n = 0;
        for (uint32_t i = 0; i < kArenaSize; i += 4) {
            uint32_t w;
            std::memcpy(&w, arena_in + i, 4);
            if (address - w >= kPage || (w >= lo && w < hi)) continue;
            if (n == 8) return;  // too many candidates (a small fault address): no evidence
            found[n++] = i % kChunk / 4;
        }
        for (uint32_t k = 0; k < n; ++k) pointer_score[found[k]] += 1.0f / static_cast<float>(n);
    }
    void fill_arena() {
        static const uint32_t kPointerPercent[] = {15, 30, 60, 85};
        pointer_percent = kPointerPercent[below(4)];
        fill_words();
        for (uint32_t i = 0; i < kArenaSize; i += 4)
            if (pointer_score[i % kChunk / 4] >= 1.0f && below(10)) {
                const uint32_t v = pointer();
                std::memcpy(arena + i, &v, 4);
            }
    }
    void fill_words() {
        switch (below(5)) {
        case 0: {  // text: letters, digits, spaces, terminators (strings, char compares)
            static const char kText[] = "aAbBzZ_ 09^\0";
            for (uint32_t i = 0; i < kArenaSize; ++i) arena[i] = static_cast<uint8_t>(kText[below(sizeof kText - 1)]);
            break;
        }
        case 1:  // 16-bit words with small high bytes (wide text, codes)
            for (uint32_t i = 0; i < kArenaSize; i += 2) {
                const uint16_t w = static_cast<uint16_t>(below(0x14) << 8 | (below(2) ? 0x20 + below(0x60) : below(256)));
                std::memcpy(arena + i, &w, 2);
            }
            break;
        default:
            for (uint32_t i = 0; i < kArenaSize; i += 4) {
                const uint32_t v = value();
                std::memcpy(arena + i, &v, 4);
            }
        }
    }
};

struct Outcome {
    bool crashed = false;
    int signal = 0;
    Ctx ctx;
    std::vector<uint8_t> arena;
    std::vector<uint8_t> heap;     // the stand-ins' heap as far as it was used
    std::vector<uint32_t> frees;  // pointers passed to the stand-in frees, in order
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> pages;  // written data pages, contents
    std::vector<ArenaWrite> writes;  // with log_arena: every byte a store changed, in order
    uint32_t stores = 0;
    bool writes_complete = false;
};

// The x87 stack in FXSAVE order (ST0 first) and its depth.
int x87_depth(const Ctx& c) {
    const uint8_t tags = c.fx_out[4];  // abridged: 1 = valid
    int n = 0;
    for (int i = 0; i < 8; ++i) n += (tags >> i) & 1;
    return n;
}

struct Runner {
    const Xbe& xbe;
    uint8_t* arena;
    std::vector<std::pair<uint32_t, uint32_t>> global_inputs;  // address, value

    void apply_inputs() {
        for (const auto& [address, value] : global_inputs) std::memcpy(reinterpret_cast<void*>(static_cast<uintptr_t>(address)), &value, 4);
    }
    void reset_pages(const std::vector<uint32_t>& pages) {
        for (uint32_t page : pages) std::memcpy(data_page(page), g_data_base.data() + page * kPage, kPage);
    }

    Outcome run(const Ctx& input, uint32_t target, const std::vector<uint8_t>& arena_in, uint32_t timeout_ms,
                bool log_arena = false) {
        Outcome o;
        o.ctx = input;
        o.ctx.target = target;
        std::memcpy(arena, arena_in.data(), kArenaSize);
        std::memset(g_heap, 0, g_heap_used);
        g_heap_used = 0;
        g_free_count = 0;
        g_dirty_list.clear();
        protect_data(PROT_READ);
        if (log_arena) {
            g_log.clear();
            g_log_full = false;
            g_log_at = 0;
            g_log_stores = 0;
            g_log_lo = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arena));
            g_log_hi = g_log_lo + kArenaSize;
            g_logging = true;
            mprotect(arena, kArenaSize, PROT_READ);
        }
        itimerval timer{{0, 0}, {static_cast<time_t>(timeout_ms / 1000), static_cast<suseconds_t>(timeout_ms % 1000 * 1000)}};
        setitimer(ITIMER_REAL, &timer, nullptr);
        if (const int sig = sigsetjmp(g_crash_jump, 1)) {
            o.crashed = true;
            o.signal = sig;
            asm volatile("fninit");
        } else {
            g_in_call = 1;
            leaf_call(&o.ctx);
            g_in_call = 0;
        }
        timer = {};
        setitimer(ITIMER_REAL, &timer, nullptr);
        protect_data(PROT_READ | PROT_WRITE);
        if (log_arena) {
            g_logging = false;
            g_log_at = 0;
            mprotect(arena, kArenaSize, PROT_READ | PROT_WRITE);
            o.writes = g_log;
            o.stores = g_log_stores;
            o.writes_complete = !g_log_full;
        }
        o.arena.assign(arena, arena + kArenaSize);
        o.heap.assign(g_heap, g_heap + ((g_heap_used + 3) & ~3u));
        o.frees.assign(g_frees, g_frees + std::min<uint32_t>(g_free_count, 256));
        if (g_free_count > 256) o.frees.push_back(g_free_count);
        for (uint32_t page : g_dirty_list) {
            o.pages.emplace_back(page, std::vector<uint8_t>(data_page(page), data_page(page) + kPage));
            g_dirty[page] = 0;
        }
        return o;
    }
};

std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "%08x", v);
    return b;
}

bool float_close(uint32_t a, uint32_t b) {
    float x, y;
    std::memcpy(&x, &a, 4);
    std::memcpy(&y, &b, 4);
    return std::isnormal(x) && std::isnormal(y) && std::fabs(x - y) <= 1e-5f * std::max(std::fabs(x), std::fabs(y));
}

// Differences between the two outcomes; empty when they match. `drift` is set when every
// difference is a pair of close floats, `nan` when every difference is two NaNs (payload or
// quiet bit: the random inputs hold NaNs, game data does not).
bool is_nan(uint32_t v) { return (v & 0x7f800000) == 0x7f800000 && (v & 0x007fffff); }

// A float the title stores need not be aligned (random strides overlap particle records:
// stride 35, velocity at +247), so a byte counts as a NaN difference when some 4 bytes
// around it hold a NaN on both sides.
bool nan_byte(const uint8_t* p, const uint8_t* q, uint32_t size, uint32_t at) {
    for (uint32_t s = at >= 3 ? at - 3 : 0; s <= at && s + 4 <= size; ++s) {
        uint32_t u, v;
        std::memcpy(&u, p + s, 4);
        std::memcpy(&v, q + s, 4);
        if (is_nan(u) && is_nan(v)) return true;
    }
    return false;
}

// registers_only: everything but MXCSR and memory (for the write-log check below).
std::string compare(const game::Function& f, const Outcome& a, const Outcome& b, Runner& runner, bool& drift, bool& nan,
                    bool registers_only = false) {
    std::string diff;
    bool only_floats = true, only_nans = true;
    auto reg = [&](const char* name, uint32_t x, uint32_t y, bool may_drift = false) {
        if (x == y) return;
        // Drift: every difference is a close float or two NaNs (projections give both).
        if (!(may_drift && (float_close(x, y) || (is_nan(x) && is_nan(y))))) only_floats = false;
        if (!(may_drift && is_nan(x) && is_nan(y))) only_nans = false;
        if (diff.size() < 400) diff += std::string(" ") + name + " " + hex(x) + "/" + hex(y);
    };
    if (a.crashed != b.crashed) return " decompiled version crashed (signal " + std::to_string(b.signal) + ")";
    const Ctx &x = a.ctx, &y = b.ctx;
    switch (f.result) {
    case game::Result::Al: reg("al", x.out[0] & 0xff, y.out[0] & 0xff); break;
    case game::Result::Eax: reg("eax", x.out[0], y.out[0]); break;
    case game::Result::EdxEax: reg("eax", x.out[0], y.out[0]); reg("edx", x.out[2], y.out[2]); break;
    case game::Result::Xmm0: {
        uint32_t p, q;
        std::memcpy(&p, x.fx_out + 160, 4);
        std::memcpy(&q, y.fx_out + 160, 4);
        reg("xmm0", p, q, true);
        break;
    }
    default: break;
    }
    reg("ebx", x.out[3], y.out[3]);
    reg("esi", x.out[4], y.out[4]);
    reg("edi", x.out[5], y.out[5]);
    reg("esp", x.out[6] - x.esp_before, y.out[6] - y.esp_before);
    reg("DF", x.eflags_out & 0x400, y.eflags_out & 0x400);
    uint16_t cw_a, cw_b;
    std::memcpy(&cw_a, x.fx_out, 2);
    std::memcpy(&cw_b, y.fx_out, 2);
    reg("x87 control", cw_a, cw_b);
    uint32_t mx_a, mx_b;
    std::memcpy(&mx_a, x.fx_out + 24, 4);
    std::memcpy(&mx_b, y.fx_out + 24, 4);
    if (!registers_only) reg("mxcsr", mx_a, mx_b);
    const int depth_a = x87_depth(x), depth_b = x87_depth(y);
    reg("x87 depth", depth_a, depth_b);
    if (depth_a == depth_b) {
        for (int i = 0; i < depth_a; ++i) {
            // ST(i) is at 32 + 16 * i (FXSAVE keeps them in stack order), 10 bytes.
            if (std::memcmp(x.fx_out + 32 + 16 * i, y.fx_out + 32 + 16 * i, 10) != 0) {
                long double p = 0, q = 0;
                std::memcpy(&p, x.fx_out + 32 + 16 * i, 10);
                std::memcpy(&q, y.fx_out + 32 + 16 * i, 10);
                const bool close = std::isnormal(static_cast<double>(p)) && std::isnormal(static_cast<double>(q)) &&
                                   std::fabs(static_cast<double>(p - q)) <= 1e-5 * std::fabs(static_cast<double>(p));
                if (!close) only_floats = false;
                if (!(std::isnan(p) && std::isnan(q))) only_nans = false;
                char b[96];
                std::snprintf(b, sizeof b, " st%d %.9Lg/%.9Lg", i, p, q);
                diff += b;
            }
        }
    }
    // Stack arguments are callee scratch (MSVC reuses their slots): not compared, as in the harness.
    auto memory = [&](const char* where, uint32_t base, const uint8_t* p, const uint8_t* q, uint32_t size) {
        for (uint32_t i = 0; i < size; i += 4) {
            uint32_t u, v;
            std::memcpy(&u, p + i, 4);
            std::memcpy(&v, q + i, 4);
            if (u == v) continue;
            if (!float_close(u, v) && !(is_nan(u) && is_nan(v))) only_floats = false;
            for (uint32_t k = i; k < i + 4; ++k)
                if (p[k] != q[k] && !nan_byte(p, q, size, k)) only_nans = false;
            if (diff.size() < 600) diff += std::string(" ") + where + " " + hex(base + i) + " " + hex(u) + "/" + hex(v);
        }
    };
    if (registers_only) return diff;
    memory("arena", static_cast<uint32_t>(reinterpret_cast<uintptr_t>(runner.arena)), a.arena.data(), b.arena.data(), kArenaSize);
    if (a.heap.size() != b.heap.size()) {
        diff += " heap used " + hex(static_cast<uint32_t>(a.heap.size())) + "/" + hex(static_cast<uint32_t>(b.heap.size()));
        only_floats = only_nans = false;
    } else {
        memory("heap", static_cast<uint32_t>(reinterpret_cast<uintptr_t>(g_heap)), a.heap.data(), b.heap.data(),
               static_cast<uint32_t>(a.heap.size()));
    }
    if (a.frees != b.frees) {
        diff += " frees differ";
        only_floats = only_nans = false;
    }
    // Data pages: the written content, or the page as both started (base + inputs).
    std::set<uint32_t> pages;
    for (const auto& p : a.pages) pages.insert(p.first);
    for (const auto& p : b.pages) pages.insert(p.first);
    for (uint32_t page : pages) {
        std::vector<uint8_t> start(g_data_base.begin() + page * kPage, g_data_base.begin() + (page + 1) * kPage);
        for (const auto& [address, value] : runner.global_inputs) {
            if (address >= g_data_lo + page * kPage && address + 4 <= g_data_lo + (page + 1) * kPage)
                std::memcpy(start.data() + (address - g_data_lo - page * kPage), &value, 4);
        }
        const uint8_t* pa = start.data();
        const uint8_t* pb = start.data();
        for (const auto& p : a.pages) if (p.first == page) pa = p.second.data();
        for (const auto& p : b.pages) if (p.first == page) pb = p.second.data();
        memory("data", g_data_lo + page * kPage, pa, pb, kPage);
    }
    nan = !diff.empty() && only_nans;
    drift = !diff.empty() && only_floats && !only_nans;
    return diff;
}

// The first arena store where the two logged runs differ: store by store when both made as
// many, else each byte's sequence of changed values is compared and, of the bytes that differ,
// the one whose divergent store came first in the original decides (approximate: a store that
// rewrites the old value records nothing). True when that store leaves a NaN on both sides: everything after it may follow
// from the NaN's payload (SSE keeps the first operand's NaN, GCC swaps commutative operands),
// e.g. later records overlapping it read those bytes back as other numbers.
bool first_divergence_nan(const Outcome& a, const Outcome& b, const std::vector<uint8_t>& arena_in, uint32_t base) {
    if (!a.writes_complete || !b.writes_complete) return false;
    if (a.stores == b.stores) {
        // The same stores in the same order (the usual case): the arenas agree up to the first
        // store whose changes differ - exact, also when one side's store rewrote the old value.
        std::vector<uint8_t> ma = arena_in, mb = arena_in;
        size_t i = 0, j = 0;
        for (uint32_t store = 0; store < a.stores; ++store) {
            const size_t i0 = i, j0 = j;
            while (i < a.writes.size() && a.writes[i].store == store) ++i;
            while (j < b.writes.size() && b.writes[j].store == store) ++j;
            bool same = i - i0 == j - j0;
            for (size_t k = 0; same && k < i - i0; ++k)
                same = a.writes[i0 + k].address == b.writes[j0 + k].address && a.writes[i0 + k].value == b.writes[j0 + k].value;
            for (size_t k = i0; k < i; ++k) ma[a.writes[k].address - base] = a.writes[k].value;
            for (size_t k = j0; k < j; ++k) mb[b.writes[k].address - base] = b.writes[k].value;
            if (same) continue;
            bool all_nan = true;
            for (size_t k = i0; k < i; ++k) {
                const uint32_t at = a.writes[k].address - base;
                if (ma[at] != mb[at] && !nan_byte(ma.data(), mb.data(), kArenaSize, at)) all_nan = false;
            }
            for (size_t k = j0; k < j; ++k) {
                const uint32_t at = b.writes[k].address - base;
                if (ma[at] != mb[at] && !nan_byte(ma.data(), mb.data(), kArenaSize, at)) all_nan = false;
            }
            return all_nan;
        }
        return false;
    }
    // Otherwise (e.g. a 16-byte copy against four dword stores): per byte.
    std::map<uint32_t, std::vector<uint32_t>> by_address_a, by_address_b;  // byte -> indices into writes
    for (uint32_t i = 0; i < a.writes.size(); ++i) by_address_a[a.writes[i].address].push_back(i);
    for (uint32_t i = 0; i < b.writes.size(); ++i) by_address_b[b.writes[i].address].push_back(i);
    constexpr uint32_t kNone = 0xffffffff;
    uint32_t first_a = kNone, first_b = kNone, first_address = 0;
    auto check = [&](uint32_t address) {
        static const std::vector<uint32_t> kEmpty;
        const auto ita = by_address_a.find(address), itb = by_address_b.find(address);
        const auto& va = ita == by_address_a.end() ? kEmpty : ita->second;
        const auto& vb = itb == by_address_b.end() ? kEmpty : itb->second;
        size_t k = 0;
        while (k < va.size() && k < vb.size() && a.writes[va[k]].value == b.writes[vb[k]].value) ++k;
        if (k == va.size() && k == vb.size()) return;
        const uint32_t ta = k < va.size() ? va[k] : kNone, tb = k < vb.size() ? vb[k] : kNone;
        if (ta < first_a || (ta == kNone && first_a == kNone && tb < first_b)) {
            first_a = ta;
            first_b = tb;
            first_address = address;
        }
    };
    for (const auto& [address, unused] : by_address_a) check(address);
    for (const auto& [address, unused] : by_address_b)
        if (!by_address_a.count(address)) check(address);
    if (first_a == kNone || first_b == kNone) return false;  // one side never wrote it
    // The arena on each side just after that store (all its bytes).
    std::vector<uint8_t> ma = arena_in, mb = arena_in;
    for (const ArenaWrite& w : a.writes) {
        if (w.store > a.writes[first_a].store) break;
        ma[w.address - base] = w.value;
    }
    for (const ArenaWrite& w : b.writes) {
        if (w.store > b.writes[first_b].store) break;
        mb[w.address - base] = w.value;
    }
    return nan_byte(ma.data(), mb.data(), kArenaSize, first_address - base);
}

void appendf(std::string& out, const char* format, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof buffer, format, args);
    va_end(args);
    out += buffer;
}

// One function's result line for the parent: index, failed, address, name, counts, coverage,
// then the report with its newlines as 0x1f.
void emit(FILE* file, size_t index, bool failed, const LeafFunction& leaf, int tested, int matched, int nans,
          int mismatched, double coverage, std::string report) {
    for (char& c : report)
        if (c == '\n') c = '\x1f';
    std::fprintf(file, "%zu %d %x %s %d %d %d %d %f %s\n", index, failed ? 1 : 0, leaf.address, leaf.name, tested, matched,
                 nans, mismatched, coverage, report.c_str());
    std::fflush(file);
}

const game::Function* find_function(const char* name) {
    for (const game::Function& f : game::functions())
        if (std::strcmp(f.name, name) == 0) return &f;
    return nullptr;
}

}  // namespace

int main() {
    // A replayed case needs its arena at the same address: reserved before anything else maps.
    const char* replay = std::getenv("LEAF_REPLAY");
    FILE* replay_file = replay ? std::fopen(replay, "rb") : nullptr;
    uint32_t replay_header[3] = {};
    if (replay && (!replay_file || std::fread(replay_header, sizeof replay_header, 1, replay_file) != 1 ||
                   replay_header[1] != sizeof(Ctx) || !std::getenv("LEAF_ONLY"))) {
        std::printf("leaf_test: LEAF_REPLAY needs a case file from this build and LEAF_ONLY=<its function>\n");
        return 1;
    }
    // Guarded (map_guarded): mapped first, right below the libraries, an unguarded arena let a
    // write past its end (a long _vsprintf, a computed index) corrupt ld.so and libc.
    auto* arena = map_guarded(kArenaSize, replay_header[0]);
    if (!arena) {
        std::printf("leaf_test: the case's arena address %08x is taken\n", replay_header[0]);
        return 1;
    }
    const char* path = std::getenv("COD_BRO_XBE");
#ifdef COD_BRO_DEFAULT_XBE
    if (!path) path = COD_BRO_DEFAULT_XBE;
#endif
    Xbe xbe;
    if (!path || !xbe.load(path)) {
        std::printf("leaf_test: no default.xbe (set COD_BRO_XBE) - skipped\n");
        return 77;
    }
    if (!map_image(xbe)) return 1;
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    const int iterations = std::getenv("LEAF_ITERS") ? std::atoi(std::getenv("LEAF_ITERS")) : 4000;
    const bool verbose = std::getenv("LEAF_VERBOSE") != nullptr;
    const std::string only = std::getenv("LEAF_ONLY") ? std::string(",") + std::getenv("LEAF_ONLY") + "," : "";

    static uint8_t alt_stack[65536];
    stack_t ss{alt_stack, 0, sizeof alt_stack};
    sigaltstack(&ss, nullptr);
    struct sigaction sa{};
    sa.sa_sigaction = on_segv;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    for (int sig : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGALRM}) sigaction(sig, &sa, nullptr);
    sa.sa_sigaction = on_trap;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGTRAP, &sa, nullptr);

    Runner runner{xbe, arena, {}};
    constexpr uint32_t kCallStack = 1 << 20;
    auto* call_stack = map_guarded(kCallStack);
    const auto call_stack_top = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(call_stack + kCallStack - 64));

    // Host memory: random words must not point there (a library's data, or the gap below the
    // main stack, which the kernel grows into on any access - -inf = 0xff800000 read fresh
    // pages): the two sides would read host state instead of faulting.
    if (FILE* maps = std::fopen("/proc/self/maps", "r")) {
        char line[256];
        while (std::fgets(line, sizeof line, maps)) {
            unsigned long lo = 0, hi = 0;
            if (std::sscanf(line, "%lx-%lx", &lo, &hi) != 2) continue;
            if (lo >= kImageBase && hi <= g_image_hi) continue;
            if (lo == reinterpret_cast<uintptr_t>(arena)) continue;
            if (std::strstr(line, "[stack]")) lo = lo > (64ul << 20) ? lo - (64ul << 20) : 0;
            g_host_ranges.emplace_back(static_cast<uint32_t>(lo), static_cast<uint32_t>(hi - 1));
            g_host_lowest = std::min(g_host_lowest, static_cast<uint32_t>(lo));
        }
        std::fclose(maps);
    }

    g_log.reserve(2u << 20);
    // A clean FPU/SSE image to start each call from.
    alignas(16) uint8_t clean_fx[512];
    asm volatile("fninit\n fxsave %0" : "=m"(clean_fx));

    if (replay) {
        Ctx input{};
        std::fread(&input, sizeof input, 1, replay_file);
        runner.global_inputs.resize(replay_header[2]);
        for (auto& g : runner.global_inputs) std::fread(&g, sizeof g, 1, replay_file);
        std::vector<uint8_t> arena_in(kArenaSize);
        std::fread(arena_in.data(), 1, kArenaSize, replay_file);
        std::fclose(replay_file);
        input.stack_top = call_stack_top;
        input.trace = 0;
        if (const char* set = std::getenv("LEAF_REPLAY_STACK")) {
            const unsigned k = static_cast<unsigned>(std::atoi(set));
            if (const char* eq = std::strchr(set, '='); eq && k < 8) input.stack[k] = static_cast<uint32_t>(std::strtoul(eq + 1, nullptr, 0));
        }
        const std::string name = only.substr(1, only.find(',', 1) - 1);
        const game::Function* f = find_function(name.c_str());
        const LeafFunction* leaf = nullptr;
        for (const LeafFunction& l : kLeafFunctions)
            if (name == l.name) leaf = &l;
        if (!f || !leaf) {
            std::printf("leaf_test: %s is not in the table\n", name.c_str());
            return 1;
        }
        runner.apply_inputs();
        const Outcome a = runner.run(input, leaf->address, arena_in, 2000);
        for (const auto& p : a.pages) std::memcpy(data_page(p.first), g_data_base.data() + p.first * kPage, kPage);
        runner.apply_inputs();
        const Outcome b = runner.run(input, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(f->code)), arena_in, 2000);
        bool drift = false, nan = false, d2 = false, n2 = false;
        const std::string diff = compare(*f, a, b, runner, drift, nan);
        for (const auto& p : b.pages) std::memcpy(data_page(p.first), g_data_base.data() + p.first * kPage, kPage);
        if (!diff.empty() && !drift && !nan && a.arena != b.arena && compare(*f, a, b, runner, d2, n2, true).empty()) {
            runner.apply_inputs();
            const Outcome la = runner.run(input, leaf->address, arena_in, 5000, true);
            for (const auto& p : la.pages) std::memcpy(data_page(p.first), g_data_base.data() + p.first * kPage, kPage);
            runner.apply_inputs();
            const Outcome lb = runner.run(input, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(f->code)), arena_in, 5000, true);
            nan = !la.crashed && !lb.crashed && first_divergence_nan(la, lb, arena_in, replay_header[0]);
            std::printf("write log: %zu/%zu bytes stored%s%s, first divergent store %s\n", la.writes.size(), lb.writes.size(),
                        la.crashed ? ("; original crashed, signal " + std::to_string(la.signal) + " at " + hex(g_crash_eip)).c_str() : "",
                        lb.crashed ? ("; ours crashed, signal " + std::to_string(lb.signal) + " at " + hex(g_crash_eip) + " address " + hex(g_crash_address)).c_str() : "",
                        nan ? "NaN on both sides" : "not a NaN pair");
        }
        std::printf("%s: %s%s\n", name.c_str(), a.crashed ? "original crashed; " : "",
                    diff.empty() ? "same" : ((drift ? "drift:" : nan ? "nan:" : "mismatch:") + diff).c_str());
        const std::string base = std::string(replay).substr(0, std::string(replay).size() - 5);
        for (const auto& [what, o] : {std::pair{"original", &a}, std::pair{"ours", &b}}) {
            if (FILE* file = std::fopen((base + ".replay." + what + ".bin").c_str(), "wb")) {
                std::fwrite(o->arena.data(), 1, kArenaSize, file);
                std::fclose(file);
            }
        }
        return diff.empty() ? 0 : 1;
    }

    int failures = 0;
    struct Record {
        uint32_t address;
        std::string name;
        int tested, matched, nans, mismatched;
        double coverage;
    };
    std::vector<Record> records;
    std::printf("%-34s %7s %7s %6s %6s %6s %8s %9s\n", "function", "tested", "matched", "drift", "nan", "mism", "skipped", "coverage");
    // Workers (forked after the image is mapped) take functions from a shared queue and write
    // one line per function to their own temporary file: index, failed, record, report.
    const char* jobs_env = std::getenv("LEAF_JOBS");
    const long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    const int jobs = jobs_env ? std::max(1, std::atoi(jobs_env)) : static_cast<int>(std::clamp(cpus / 2, 1L, 4L));
    std::vector<size_t> todo;
    for (size_t index = 0; index < sizeof kLeafFunctions / sizeof *kLeafFunctions; ++index)
        if (only.empty() || only.find(std::string(",") + kLeafFunctions[index].name + ",") != std::string::npos)
            todo.push_back(index);
    // A worker can die outside a call (a pointer computed from random fields wrote into its own
    // heap, often noticed by malloc during a later function): a fresh worker takes its place at
    // once and runs the function it was on first, on a clean heap (until that function has killed
    // 3 workers), so one death neither leaves the rest to fewer workers nor fails a bystander.
    struct Queue {
        std::atomic<uint32_t> next, count;
        int32_t current[64];  // per worker slot: the function's index, -1 between functions
        int32_t items[1];     // todo
    };
    // Between no-access guard pages: the kernel maps it next to the arena, and a function
    // writing past the arena's end filled the counters with 0xffffffff.
    const size_t queue_bytes = (sizeof(Queue) + todo.size() * 4 + kPage - 1) & ~size_t{kPage - 1};
    auto* queue_map = static_cast<uint8_t*>(mmap(nullptr, queue_bytes + 2 * kPage, PROT_NONE, MAP_SHARED | MAP_ANONYMOUS, -1, 0));
    if (queue_map == MAP_FAILED) return 1;
    mprotect(queue_map + kPage, queue_bytes, PROT_READ | PROT_WRITE);
    auto* queue = reinterpret_cast<Queue*>(queue_map + kPage);
    for (size_t k = 0; k < todo.size(); ++k) queue->items[k] = static_cast<int32_t>(todo[k]);
    queue->next = 0;
    queue->count = static_cast<uint32_t>(todo.size());
    std::vector<FILE*> outputs;
    FILE* output = nullptr;
    int worker = -1;
    int32_t first_job = -1;  // a replacement worker's first function
    std::vector<std::pair<int, std::string>> reports;
    {
        const int slots = std::min(jobs, 64);
        std::vector<pid_t> slot_pid(static_cast<size_t>(slots), 0);
        std::map<int32_t, int> deaths;
        std::vector<std::pair<int, int32_t>> spawn;  // slot, first function (-1: none)
        for (int k = slots - 1; k >= 0; --k) spawn.emplace_back(k, -1);
        int live = 0;
        for (;;) {
            while (!spawn.empty()) {
                const auto [k, first] = spawn.back();
                spawn.pop_back();
                queue->current[k] = -1;
                outputs.push_back(std::tmpfile());
                const pid_t pid = fork();
                if (pid == 0) {
                    // No core dump for a death outside a call (handled here; systemd-coredump
                    // would notify the desktop).
                    prctl(PR_SET_DUMPABLE, 0);
                    worker = k;
                    first_job = first;
                    output = outputs.back();
                    break;
                }
                slot_pid[static_cast<size_t>(k)] = pid;
                ++live;
            }
            if (worker >= 0 || live == 0) break;
            int status = 0;
            const pid_t pid = waitpid(-1, &status, 0);
            if (pid < 0) {
                if (errno == EINTR) continue;
                std::printf("leaf_test: waitpid: %s\n", std::strerror(errno));
                break;
            }
            const auto at = std::find(slot_pid.begin(), slot_pid.end(), pid);
            if (at == slot_pid.end()) continue;
            const int k = static_cast<int>(at - slot_pid.begin());
            *at = 0;
            --live;
            if (WIFEXITED(status)) continue;
            std::printf("leaf_test: a worker died (status %d)\n", status);
            const int32_t index = queue->current[k];
            spawn.emplace_back(k, index >= 0 && ++deaths[index] < 3 ? index : -1);
        }
    }
    if (worker < 0) {
        std::set<size_t> done;
        for (FILE* file : outputs) {
            std::rewind(file);
            char line[8192];
            while (std::fgets(line, sizeof line, file)) {
                int index = 0, failed = 0, tested = 0, matched = 0, nans = 0, mismatched = 0, consumed = 0;
                unsigned address = 0;
                char name[128];
                double coverage = 0;
                if (std::sscanf(line, "%d %d %x %127s %d %d %d %d %lf%n", &index, &failed, &address, name, &tested, &matched,
                                &nans, &mismatched, &coverage, &consumed) != 9)
                    continue;
                if (!done.insert(static_cast<size_t>(index)).second) continue;  // requeued after it had finished
                std::string text = line + consumed + 1;
                for (size_t at; (at = text.find('\x1f')) != std::string::npos;) text[at] = '\n';
                if (!text.empty() && text.back() == '\n') text.pop_back();
                reports.emplace_back(index, text);
                failures += failed;
                if (tested) records.push_back({address, name, tested, matched, nans, mismatched, coverage});
            }
            std::fclose(file);
        }
        std::vector<size_t> missing;
        for (size_t index : todo)
            if (!done.count(index)) missing.push_back(index);
        todo = missing;
    }
    if (worker < 0) {
        std::sort(reports.begin(), reports.end());
        for (const auto& [index, text] : reports) std::fputs(text.c_str(), stdout);
        for (size_t index : todo) {
            std::printf("%-34s never finished (its workers died)\n", kLeafFunctions[index].name);
            ++failures;
        }
    }
    while (worker >= 0) {
        queue->current[worker] = -1;
        size_t index;
        if (first_job >= 0) {
            index = static_cast<size_t>(first_job);
            first_job = -1;
        } else {
            uint32_t q = queue->next.load();
            if (q >= queue->count.load()) break;
            if (!queue->next.compare_exchange_weak(q, q + 1)) continue;
            index = static_cast<size_t>(queue->items[q]);
        }
        queue->current[worker] = static_cast<int32_t>(index);
        const LeafFunction& leaf = kLeafFunctions[index];
        g_current = leaf.name;
        std::string out;
        const game::Function* f = find_function(leaf.name);
        if (!f) {
            appendf(out, "%-34s not in kFunctions\n", leaf.name);
            emit(output, index, true, leaf, 0, 0, 0, 0, 0, out);
            continue;
        }
        Gen gen(0x1eaf0000u ^ leaf.address, arena);
        const auto function_started = std::chrono::steady_clock::now();
        g_trace_lo = leaf.address;
        g_trace_hi = leaf.address + leaf.size;
        g_reached.clear();
        int tested = 0, matched = 0, drifted = 0, nans = 0, mismatched = 0, skipped = 0, unstable = 0;
        std::string first_issue;
        bool first_failing = false;
        std::map<uint32_t, int> crash_sites;  // where the original crashed (0 = timeout)
        std::vector<uint8_t> pointer_slot(14 + leaf.globals.size(), 0);
        // Inputs the original ran without crashing; half of the inputs mutate one of them
        // (a few arena words or input slots), so the rare input that gets through a chain
        // of pointers and small indexes leads to more like it.
        struct Saved {
            std::vector<uint8_t> arena;
            Ctx input;
            std::vector<std::pair<uint32_t, uint32_t>> globals;
            std::vector<uint32_t> slots;
        };
        std::vector<Saved> corpus;
        for (int it = 0; it < iterations; ++it) {
            // A budget: after 15 s, stop once 1000 inputs ran (the slowest loops; far more than
            // the 100 an offline function needs, and coverage has stopped growing by then).
            if (it % 64 == 0 && tested >= 1000 &&
                std::chrono::steady_clock::now() - function_started > std::chrono::seconds(15))
                break;
            gen.fill_arena();
            std::vector<uint8_t> arena_in(arena, arena + kArenaSize);
            runner.global_inputs.clear();
            Ctx input{};
            gen.start_arguments();
            // Input slots: 6 registers, 8 stack words, then the globals. A slot the original
            // faulted near (address - value < 4 KB) holds a pointer from then on, mostly.
            std::vector<uint32_t> slot_values;
            auto slot_value = [&](size_t slot) {
                const uint32_t v = pointer_slot[slot] && gen.below(10) ? gen.argument_pointer() : gen.value(true);
                slot_values.push_back(v);
                return v;
            };
            for (uint32_t& r : input.in) r = slot_value(slot_values.size());
            for (uint32_t& st : input.stack) st = slot_value(slot_values.size());
            for (uint32_t g : leaf.globals) {
                const uint32_t v = slot_value(slot_values.size());
                if (!pointer_slot[slot_values.size() - 1] && gen.below(4) == 0) continue;  // keep the image's value
                runner.global_inputs.emplace_back(g, v);
            }
            std::memcpy(input.fx_in, clean_fx, 512);
            static const uint16_t kControl[] = {0x027f, 0x007f, 0x037f, 0x0c7f};
            static const uint32_t kMxcsr[] = {0x1f80, 0x1f80, 0x9f80, 0x7f80};
            const uint16_t cw = kControl[gen.below(4)];
            const uint32_t mxcsr = kMxcsr[gen.below(4)];
            std::memcpy(input.fx_in, &cw, 2);
            std::memcpy(input.fx_in + 24, &mxcsr, 4);
            for (int r = 0; r < 8; ++r) {
                for (int lane = 0; lane < 4; ++lane) {
                    const uint32_t v = gen.below(3) ? [&] { const float x = gen.any_float(); uint32_t u; std::memcpy(&u, &x, 4); return u; }()
                                                    : gen.value();
                    std::memcpy(input.fx_in + 160 + 16 * r + 4 * lane, &v, 4);
                }
            }
            input.stack_top = call_stack_top;
            input.nstack = 8;  // a caller's stack has words above the arguments (and arg_bytes can be short)
            if (!corpus.empty() && gen.below(2)) {
                const Saved& s = corpus[gen.below(static_cast<uint32_t>(corpus.size()))];
                std::memcpy(arena, s.arena.data(), kArenaSize);
                input = s.input;
                runner.global_inputs = s.globals;
                slot_values = s.slots;
                for (uint32_t n = 1 + gen.below(4); n; --n) {
                    const uint32_t kind = gen.below(4);
                    if (kind < 2) {  // an arena word, mostly a structure's first fields
                        const uint32_t at = gen.below(32) * kChunk + 4 * (gen.below(4) ? gen.below(0x30) : gen.below(kChunk / 4));
                        const uint32_t v = kind ? gen.value() : gen.below(2) ? gen.below(8) : gen.below(256);
                        std::memcpy(arena + at, &v, 4);
                    } else if (kind == 2) {  // an input slot: register, stack word or global
                        const size_t k = gen.below(static_cast<uint32_t>(slot_values.size()));
                        // Arguments stay distinct objects: a pointer into another slot's chunk is redrawn.
                        const uint32_t lo = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arena));
                        const auto chunk_taken = [&](uint32_t v) {
                            if (v - lo >= kArenaSize) return false;
                            for (size_t j = 0; j < slot_values.size(); ++j)
                                if (j != k && slot_values[j] - lo < kArenaSize && (slot_values[j] - lo) / kChunk == (v - lo) / kChunk)
                                    return true;
                            return false;
                        };
                        uint32_t v = gen.below(2) ? gen.value() : gen.below(8);
                        for (int tries = 0; tries < 8 && chunk_taken(v); ++tries) v = gen.value();
                        if (chunk_taken(v)) v = gen.below(8);
                        slot_values[k] = v;
                        if (k < 6)
                            input.in[k] = v;
                        else if (k < 14)
                            input.stack[k - 6] = v;
                        else {
                            const uint32_t g = leaf.globals[k - 14];
                            auto at = std::find_if(runner.global_inputs.begin(), runner.global_inputs.end(),
                                                   [&](const auto& e) { return e.first == g; });
                            if (at != runner.global_inputs.end())
                                at->second = v;
                            else
                                runner.global_inputs.emplace_back(g, v);
                        }
                    } else {  // the x87 control word and MXCSR
                        std::memcpy(input.fx_in, &kControl[gen.below(4)], 2);
                        std::memcpy(input.fx_in + 24, &kMxcsr[gen.below(4)], 4);
                    }
                }
                arena_in.assign(arena, arena + kArenaSize);
            }
            input.trace = it % 4 == 0;

            runner.apply_inputs();
            const auto started = std::chrono::steady_clock::now();
            // 10 ms for the original (50 single-stepped): random counts make long loops, and
            // every timed-out input costs the whole limit (NAL_Float3_Fixup: 1 input in 5).
            const Outcome a = runner.run(input, leaf.address, arena_in, input.trace ? 50 : 10);
            const auto took = std::chrono::steady_clock::now() - started;
            // Back to the starting state: the original's written pages, then the inputs.
            for (const auto& p : a.pages) std::memcpy(data_page(p.first), g_data_base.data() + p.first * kPage, kPage);
            std::vector<uint32_t> input_pages;
            for (const auto& [address, value] : runner.global_inputs) input_pages.push_back((address - g_data_lo) / kPage);
            if (a.crashed) {
                ++skipped;
                ++crash_sites[a.signal == SIGALRM ? 0 : g_crash_eip];
                if (a.signal == SIGSEGV || a.signal == SIGBUS) {
                    for (size_t k = 0; k < slot_values.size(); ++k)
                        if (g_crash_address - slot_values[k] < kPage) pointer_slot[k] = 1;
                    gen.learn_fault(arena_in.data(), g_crash_address);
                }
                runner.reset_pages(input_pages);
                continue;
            }
            // An x87 result function that returns nothing on the x87 stack (FPTAN out of range
            // pushes nothing) leaves its caller to underflow: treated like a crash.
            if (f->result == game::Result::St0 && x87_depth(a.ctx) == 0) {
                ++skipped;
                runner.reset_pages(input_pages);
                continue;
            }
            // Only quick inputs breed: a long loop's input would fill the corpus with slow ones.
            if (took > std::chrono::milliseconds(2)) {
            } else if (corpus.size() < 256)
                corpus.push_back({arena_in, input, runner.global_inputs, slot_values});
            else
                corpus[gen.below(256)] = {arena_in, input, runner.global_inputs, slot_values};
            runner.apply_inputs();
            input.trace = 0;
            const uint32_t ours = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(f->code));
            Outcome b = runner.run(input, ours, arena_in, 50);
            if (b.crashed && b.signal == SIGALRM) {
                // Our C is slower on long loops (the -O0 preset: NAL_Float3_SkipFixedBlock
                // over millions of channels): once more with a longer limit.
                for (const auto& p : b.pages) std::memcpy(data_page(p.first), g_data_base.data() + p.first * kPage, kPage);
                runner.apply_inputs();
                b = runner.run(input, ours, arena_in, 2000);
            }
            bool drift = false, nan = false;
            const std::string diff = compare(*f, a, b, runner, drift, nan);
            if (!diff.empty() && !drift && !nan) {
                // A pointer computed from random fields can land on the call stack, where the
                // two versions' frames differ. Such an input changes the original's own result
                // when the stack moves: unstable, skipped.
                for (const auto& p : b.pages) std::memcpy(data_page(p.first), g_data_base.data() + p.first * kPage, kPage);
                runner.apply_inputs();
                Ctx moved = input;
                moved.stack_top -= 0x2040;
                const Outcome again = runner.run(moved, leaf.address, arena_in, 50);
                for (const auto& p : again.pages) std::memcpy(data_page(p.first), g_data_base.data() + p.first * kPage, kPage);
                runner.reset_pages(input_pages);
                bool d2 = false, n2 = false;
                if (again.crashed || !compare(*f, a, again, runner, d2, n2).empty()) {
                    ++unstable;
                    continue;
                }
                runner.apply_inputs();
                // Registers agree and the arena differs: rerun both with the arena write log; a
                // first divergent store that is NaN on both sides makes this a NaN difference.
                bool d3 = false, n3 = false;
                if (a.arena != b.arena && compare(*f, a, b, runner, d3, n3, true).empty()) {
                    const Outcome la = runner.run(input, leaf.address, arena_in, 5000, true);
                    for (const auto& p : la.pages) std::memcpy(data_page(p.first), g_data_base.data() + p.first * kPage, kPage);
                    runner.apply_inputs();
                    const Outcome lb = runner.run(input, ours, arena_in, 5000, true);
                    for (const auto& p : lb.pages) std::memcpy(data_page(p.first), g_data_base.data() + p.first * kPage, kPage);
                    runner.apply_inputs();
                    if (!la.crashed && !lb.crashed &&
                        first_divergence_nan(la, lb, arena_in, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arena))))
                        nan = true;
                }
            }
            ++tested;
            if (diff.empty())
                ++matched;
            else if (drift)
                ++drifted;
            else if (nan)
                ++nans;
            else
                ++mismatched;
            // The report shows the first mismatch, else the first drift or NaN difference.
            const bool failing = !diff.empty() && !drift && !nan;
            if (!diff.empty() && (first_issue.empty() || (failing && !first_failing))) {
                first_failing = failing;
                first_issue = (drift ? "drift:" : nan ? "nan:" : "mismatch:") + diff + "\n    input: eax ecx edx ebx esi edi";
                for (uint32_t r : input.in) first_issue += " " + hex(r);
                first_issue += ", stack";
                for (uint32_t k = 0; k < input.nstack; ++k) first_issue += " " + hex(input.stack[k]);
                if (!runner.global_inputs.empty()) first_issue += ", globals";
                for (const auto& [address, value] : runner.global_inputs) first_issue += " " + hex(address) + "=" + hex(value);
                // LEAF_DUMP=DIR: the first mismatch's input (replayable) and the arena after each version.
                if (const char* dir = std::getenv("LEAF_DUMP"); dir && failing) {
                    const auto dump = [&](const char* what, const uint8_t* bytes) {
                        const std::string path = std::string(dir) + "/" + leaf.name + "." + hex(static_cast<uint32_t>(
                            reinterpret_cast<uintptr_t>(arena))) + "." + what + ".bin";
                        if (FILE* file = std::fopen(path.c_str(), "wb")) {
                            std::fwrite(bytes, 1, kArenaSize, file);
                            std::fclose(file);
                        }
                    };
                    dump("original", a.arena.data());
                    dump("ours", b.arena.data());
                    // The whole input, for LEAF_REPLAY: arena address, Ctx, global inputs, arena.
                    const std::string path = std::string(dir) + "/" + leaf.name + ".case";
                    if (FILE* file = std::fopen(path.c_str(), "wb")) {
                        const uint32_t header[3] = {static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arena)),
                                                    static_cast<uint32_t>(sizeof input),
                                                    static_cast<uint32_t>(runner.global_inputs.size())};
                        std::fwrite(header, sizeof header, 1, file);
                        std::fwrite(&input, sizeof input, 1, file);
                        for (const auto& g : runner.global_inputs) std::fwrite(&g, sizeof g, 1, file);
                        std::fwrite(arena_in.data(), 1, kArenaSize, file);
                        std::fclose(file);
                    }
                }
            }
            // Restore: pages either side wrote and the inputs' pages.
            for (const auto& p : b.pages) std::memcpy(data_page(p.first), g_data_base.data() + p.first * kPage, kPage);
            runner.reset_pages(input_pages);
        }
        const double coverage = leaf.instructions ? 100.0 * static_cast<double>(g_reached.size()) / leaf.instructions : 0;
        appendf(out, "%-34s %7d %7d %6d %6d %6d %8d %8.0f%%\n", leaf.name, tested, matched, drifted, nans, mismatched, skipped + unstable,
                    coverage);
        if (!first_issue.empty()) appendf(out, "    %s\n", first_issue.c_str());
        if (verbose) {
            std::vector<std::pair<int, uint32_t>> sites;
            for (const auto& [eip, n] : crash_sites) sites.emplace_back(n, eip);
            std::sort(sites.rbegin(), sites.rend());
            std::string text;
            for (size_t i = 0; i < sites.size() && i < 4; ++i)
                text += " " + (sites[i].second ? hex(sites[i].second) : std::string("timeout")) + " x" + std::to_string(sites[i].first);
            if (!text.empty()) appendf(out, "    original crashed at:%s\n", text.c_str());
        }
        emit(output, index, mismatched != 0, leaf, tested, matched, nans, mismatched + drifted,
             coverage, out);
        // Progress as each function finishes (the table above is printed sorted at the end).
        std::fprintf(stderr, "[%zu/%zu] %s: %d of %d matched%s (%.0f s)\n", index + 1, sizeof kLeafFunctions / sizeof *kLeafFunctions,
                     leaf.name, matched, tested, mismatched ? " - MISMATCH" : "",
                     std::chrono::duration<double>(std::chrono::steady_clock::now() - function_started).count());
    }
    if (worker >= 0) {
        std::fflush(output);
        _exit(0);
    }
    if (const char* csv = std::getenv("LEAF_CSV")) {
        std::map<uint32_t, std::string> rows;  // address -> line, the file's own rows first
        if (FILE* in = std::fopen(csv, "r")) {
            char line[512];
            while (std::fgets(line, sizeof line, in)) {
                unsigned address = 0;
                if (std::strncmp(line, "address,", 8) == 0) continue;  // the header ("add" reads as hex)
                if (std::sscanf(line, "%x,", &address) == 1) rows[address] = line;
            }
            std::fclose(in);
        }
        for (const Record& r : records) {
            char line[512];
            std::snprintf(line, sizeof line, "%08x,%s,%d,%d,%d,%d,%.1f\n", r.address, r.name.c_str(), r.tested,
                          r.matched, r.nans, r.mismatched, r.coverage);
            rows[r.address] = line;
        }
        if (FILE* out = std::fopen(csv, "w")) {
            std::fputs("address,name,tested,matched,nan,mismatched,coverage\n", out);
            for (const auto& [address, line] : rows) std::fputs(line.c_str(), out);
            std::fclose(out);
        }
    }
    return failures ? 1 : 0;
}
