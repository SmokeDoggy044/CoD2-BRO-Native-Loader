#include "text_trap.h"

#include <fcntl.h>
#include <signal.h>
#include <ucontext.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "log.h"

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

}  // namespace

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
