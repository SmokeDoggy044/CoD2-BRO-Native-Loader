#include "text_trap.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "log.h"
#include "xbe.h"

namespace loader {
namespace {

struct Trap {
    uint32_t address;
    uint8_t original;
    std::atomic<bool> hit{false};
    std::string name;
};

// Built before the handler is installed, read-only afterwards (the handler only searches it).
Trap* g_traps = nullptr;
size_t g_trap_count = 0;
int g_out = -1;
struct sigaction g_previous {};

Trap* find_trap(uint32_t address) {
    size_t low = 0, high = g_trap_count;
    while (low < high) {
        const size_t mid = (low + high) / 2;
        if (g_traps[mid].address < address)
            low = mid + 1;
        else
            high = mid;
    }
    return low < g_trap_count && g_traps[low].address == address ? &g_traps[low] : nullptr;
}

char* put_hex(char* out, uint32_t value) {
    for (int shift = 28; shift >= 0; shift -= 4) *out++ = "0123456789abcdef"[(value >> shift) & 0xf];
    return out;
}

void on_trap(int signal, siginfo_t* info, void* context) {
    auto& gregs = static_cast<ucontext_t*>(context)->uc_mcontext.gregs;
    const uint32_t eip = static_cast<uint32_t>(gregs[REG_EIP]);
    Trap* trap = find_trap(eip - 1);
    if (!trap) {
        // Not ours (a title int3, a real fault): whoever handled it before.
        if (g_previous.sa_flags & SA_SIGINFO)
            g_previous.sa_sigaction(signal, info, context);
        else if (g_previous.sa_handler != SIG_DFL && g_previous.sa_handler != SIG_IGN)
            g_previous.sa_handler(signal);
        else
            _exit(128 + signal);
        return;
    }
    // Another thread may have put the byte back already; storing it again is harmless.
    *reinterpret_cast<volatile uint8_t*>(uintptr_t{trap->address}) = trap->original;
    gregs[REG_EIP] = static_cast<greg_t>(trap->address);
    if (trap->hit.exchange(true)) return;
    const uint32_t caller = *reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(gregs[REG_ESP]));
    char line[160];
    char* out = put_hex(line, trap->address);
    *out++ = ' ';
    const size_t length = std::min<size_t>(trap->name.size(), 100);
    std::memcpy(out, trap->name.data(), length);
    out += length;
    *out++ = ' ';
    out = put_hex(out, caller);
    *out++ = '\n';
    ssize_t written = write(g_out, line, static_cast<size_t>(out - line));
    (void)written;
}

// --text-clear: the original bytes of the code sections and which of them are INT3 now.
struct CodeRange {
    uint32_t begin = 0, end = 0;
    std::vector<uint8_t> original;
    std::vector<uint8_t> cleared;
};
std::vector<CodeRange> g_code;
std::vector<std::tuple<uint32_t, uint32_t, std::string>> g_code_names;  // read-only once the handler is in place (the bytes in `cleared` aside)
std::atomic_flag g_clear_lock = ATOMIC_FLAG_INIT;
int g_clear_out = -1;
struct sigaction g_clear_previous {};

CodeRange* find_code(uint32_t address) {
    for (CodeRange& range : g_code)
        if (address >= range.begin && address < range.end) return &range;
    return nullptr;
}

// A 16-byte boundary after the compiler's INT3 padding: two or more INT3s, or one right after a ret / ret n /
// jmp rel32 / jmp rel8 (a single 0xCC may be an operand byte, [ebp-0x34], of an instruction crossing the boundary).
bool function_end(const CodeRange& range, uint32_t address) {
    if ((address & 15) != 0 || address < range.begin + 8) return false;
    const uint8_t* o = range.original.data() + (address - range.begin);
    if (o[-1] != 0xCC) return false;
    if (o[-2] == 0xCC) return true;
    return o[-2] == 0xC3 || o[-4] == 0xC2 || o[-6] == 0xE9 || o[-3] == 0xEB;
}

void on_clear_trap(int signal, siginfo_t* info, void* context) {
    auto& gregs = static_cast<ucontext_t*>(context)->uc_mcontext.gregs;
    const uint32_t address = static_cast<uint32_t>(gregs[REG_EIP]) - 1;
    CodeRange* range = find_code(address);
    if (!range || !range->cleared[address - range->begin]) {
        if (g_clear_previous.sa_flags & SA_SIGINFO)
            g_clear_previous.sa_sigaction(signal, info, context);
        else if (g_clear_previous.sa_handler != SIG_DFL && g_clear_previous.sa_handler != SIG_IGN)
            g_clear_previous.sa_handler(signal);
        else
            _exit(128 + signal);
        return;
    }
    while (g_clear_lock.test_and_set(std::memory_order_acquire)) {
    }
    gregs[REG_EIP] = static_cast<greg_t>(address);
    // Another thread may have restored it meanwhile (an in-process restart puts the INT3s back: restore again).
    const bool restore = *reinterpret_cast<volatile uint8_t*>(uintptr_t{address}) == 0xCC;
    if (restore) {
        // One function: up to the next 16-byte boundary after INT3 padding (the compiler's), at most 64 KB. A
        // function that goes on past it (a false boundary) traps again there and is logged as one more spot.
        const uint32_t from = address;
        uint32_t to = std::min(address + 1, range->end);
        while (to < range->end && to - from < 0x10000 && !function_end(*range, to)) ++to;
        for (uint32_t a = from; a < to; ++a)
            if (range->cleared[a - range->begin])
                *reinterpret_cast<volatile uint8_t*>(uintptr_t{a}) = range->original[a - range->begin];
        const auto* stack = reinterpret_cast<const uint32_t*>(static_cast<uintptr_t>(gregs[REG_ESP]));
        char line[64];
        char* out = put_hex(line, address);
        *out++ = ' ';
        out = put_hex(out, stack[0]);
        *out++ = '\n';
        ssize_t written = write(g_clear_out, line, static_cast<size_t>(out - line));
        (void)written;
    }
    g_clear_lock.clear(std::memory_order_release);
}

// XB_CLEAR_NOACCESS=NAME: that section's whole pages also lose all access after clearing; the first access to
// each page is logged as "R address eip" (a data read or write of cleared bytes) and the page opened again.
uint32_t g_noaccess_begin = 0, g_noaccess_end = 0;
struct sigaction g_segv_previous {};

void on_noaccess(int signal, siginfo_t* info, void* context) {
    const auto address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(info->si_addr));
    if (address >= g_noaccess_begin && address < g_noaccess_end) {
        const uint32_t page = address & ~0xfffu;
        mprotect(reinterpret_cast<void*>(uintptr_t{page}), 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC);
        auto& gregs = static_cast<ucontext_t*>(context)->uc_mcontext.gregs;
        char line[64] = {'R', ' '};
        char* out = put_hex(line + 2, address);
        *out++ = ' ';
        out = put_hex(out, static_cast<uint32_t>(gregs[REG_EIP]));
        *out++ = '\n';
        ssize_t written = write(g_clear_out, line, static_cast<size_t>(out - line));
        (void)written;
        return;
    }
    if (g_segv_previous.sa_flags & SA_SIGINFO)
        g_segv_previous.sa_sigaction(signal, info, context);
    else if (g_segv_previous.sa_handler != SIG_DFL && g_segv_previous.sa_handler != SIG_IGN)
        g_segv_previous.sa_handler(signal);
    else
        _exit(128 + signal);
}

}  // namespace

void snapshot_code_sections(const XbeImage& image) {
    static const char* const kCode[] = {".text", "XONLINE", "XNET", "D3DX", "DSOUND", "D3D", "XGRPH", "XPP"};
    g_code.clear();
    for (const XbeSectionHeader* section : image.sections()) {
        const std::string name = image.section_name(*section);
        if (std::find(std::begin(kCode), std::end(kCode), name) == std::end(kCode)) continue;
        // XB_CLEAR_SKIP=NAME,NAME: sections to leave alone (to find which one a problem needs).
        if (const char* skip = std::getenv("XB_CLEAR_SKIP"); skip && std::strstr(skip, name.c_str())) continue;
        CodeRange range;
        range.begin = section->virtual_addr;
        range.end = section->virtual_addr + section->virtual_size;
        const auto* bytes = reinterpret_cast<const uint8_t*>(uintptr_t{range.begin});
        range.original.assign(bytes, bytes + section->virtual_size);
        g_code_names.emplace_back(range.begin, range.end, name);
        g_code.push_back(std::move(range));
    }
}

bool install_text_clear(const std::filesystem::path& out, const std::vector<std::filesystem::path>& keep_lists) {
    if (g_code.empty()) {
        LOG_ERROR("trap", "--text-clear: no code sections were saved");
        return false;
    }
    g_clear_out = open(out.string().c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0644);
    if (g_clear_out < 0) {
        LOG_ERROR("trap", "cannot write %s", out.string().c_str());
        return false;
    }
    // Data inside the code sections (merged constants the levels and our C read): kept as it is.
    std::vector<std::pair<uint32_t, uint32_t>> data;
    for (const std::filesystem::path& keep_list : keep_lists) {
        std::ifstream in(keep_list);
        if (!in) {
            LOG_ERROR("trap", "cannot read %s", keep_list.string().c_str());
            return false;
        }
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            std::istringstream fields(line);
            std::string begin, end;
            if (fields >> begin >> end)
                data.emplace_back(static_cast<uint32_t>(std::stoul(begin, nullptr, 16)),
                                  static_cast<uint32_t>(std::stoul(end, nullptr, 16)));
        }
    }
    size_t kept = 0, cleared = 0, kept_data = 0;
    for (CodeRange& range : g_code) {
        const size_t size = range.end - range.begin;
        auto* code = reinterpret_cast<uint8_t*>(uintptr_t{range.begin});
        // Bytes our hooks changed stay, with 4 bytes on each side: a jump's bytes may match the original's by chance.
        std::vector<uint8_t> keep(size, 0);
        for (size_t i = 0; i < size; ++i)
            if (code[i] != range.original[i])
                for (size_t k = i >= 4 ? i - 4 : 0; k < std::min(size, i + 5); ++k) keep[k] = 1;
        for (const auto& [begin, end] : data)
            for (uint32_t a = std::max(begin, range.begin); a < std::min(end, range.end); ++a)
                if (!keep[a - range.begin]) {
                    keep[a - range.begin] = 1;
                    ++kept_data;
                }
        range.cleared.assign(size, 0);
        for (size_t i = 0; i < size; ++i) {
            if (keep[i]) {
                ++kept;
                continue;
            }
            range.cleared[i] = 1;
            ++cleared;
        }
    }
    struct sigaction action {};
    action.sa_sigaction = on_clear_trap;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTRAP, &action, &g_clear_previous);
    for (CodeRange& range : g_code) {
        auto* code = reinterpret_cast<uint8_t*>(uintptr_t{range.begin});
        for (size_t i = 0; i < range.cleared.size(); ++i)
            if (range.cleared[i]) code[i] = 0xCC;
    }
    if (const char* name = std::getenv("XB_CLEAR_NOACCESS"); name && *name) {
        for (const auto& [begin, end, section] : g_code_names)
            if (section == name) {
                g_noaccess_begin = (begin + 0xfff) & ~0xfffu;
                g_noaccess_end = end & ~0xfffu;
            }
        struct sigaction segv {};
        segv.sa_sigaction = on_noaccess;
        segv.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
        sigemptyset(&segv.sa_mask);
        sigaction(SIGSEGV, &segv, &g_segv_previous);
    }
    LOG_INFO("trap", "text clear: %zu original code bytes are INT3 now, %zu kept (%zu of them data), log %s",
             cleared, kept, kept_data, out.string().c_str());
    return true;
}

void text_clear_protect() {
    if (g_noaccess_end <= g_noaccess_begin) return;
    mprotect(reinterpret_cast<void*>(uintptr_t{g_noaccess_begin}), g_noaccess_end - g_noaccess_begin, PROT_NONE);
    LOG_INFO("trap", "text clear: %08x-%08x no access", g_noaccess_begin, g_noaccess_end);
}

bool install_text_trap(const std::filesystem::path& list, const std::filesystem::path& out) {
    std::ifstream in(list);
    if (!in) {
        LOG_ERROR("trap", "cannot read %s", list.string().c_str());
        return false;
    }
    std::vector<std::pair<uint32_t, std::string>> entries;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        std::string address, name;
        if (!(fields >> address >> name)) continue;
        entries.emplace_back(static_cast<uint32_t>(std::stoul(address, nullptr, 16)), name);
    }
    std::sort(entries.begin(), entries.end());
    entries.erase(std::unique(entries.begin(), entries.end(),
                              [](const auto& a, const auto& b) { return a.first == b.first; }),
                  entries.end());
    g_out = open(out.string().c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0644);
    if (g_out < 0) {
        LOG_ERROR("trap", "cannot write %s", out.string().c_str());
        return false;
    }
    g_traps = new Trap[entries.size()];
    size_t skipped = 0;
    for (const auto& [address, name] : entries) {
        auto* code = reinterpret_cast<uint8_t*>(uintptr_t{address});
        // A hook's jump (to host code, outside the image) means the original never runs from here.
        if (code[0] == 0xE9) {
            int32_t relative;
            std::memcpy(&relative, code + 1, 4);
            const uint32_t target = address + 5 + static_cast<uint32_t>(relative);
            if (target >= 0x02000000u) {  // the image ends far below
                ++skipped;
                continue;
            }
        }
        Trap& trap = g_traps[g_trap_count++];
        trap.address = address;
        trap.original = code[0];
        trap.name = name;
    }
    struct sigaction action {};
    action.sa_sigaction = on_trap;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_NODEFER;
    sigemptyset(&action.sa_mask);
    sigaction(SIGTRAP, &action, &g_previous);
    for (size_t i = 0; i < g_trap_count; ++i) *reinterpret_cast<uint8_t*>(uintptr_t{g_traps[i].address}) = 0xCC;
    LOG_INFO("trap", "%zu original functions trapped (%zu hooked, skipped), log %s", g_trap_count, skipped,
             out.string().c_str());
    return true;
}

}  // namespace loader
