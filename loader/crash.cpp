#include "crash.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include "log.h"
#include "symbols.h"
#include "thread_env.h"
#include "xbe.h"

namespace loader {

namespace {

const XbeImage* g_image = nullptr;
const SymbolTable* g_symbols = nullptr;
void (*g_exit_hook)(bool error) = nullptr;
std::atomic<bool> g_exiting{false};

bool on_xbox_stack(uint32_t address) {
    const ThreadEnv* env = current_thread_env();
    return env && address >= env->stack_limit && address + 8 <= env->stack_base;
}

}  // namespace

void set_crash_context(const XbeImage& image, const SymbolTable& symbols) {
    g_image = &image;
    g_symbols = &symbols;
}

void run_fatal_exit_hook(bool error) {
    if (g_exiting.exchange(true))
        for (;;) std::this_thread::sleep_for(std::chrono::hours(1));
    if (g_exit_hook) g_exit_hook(error);
}

std::string describe_address(uint32_t address) {
    if (!g_image || !g_image->contains(address)) {
        // The Ghidra symbols only cover the XBE; don't attach them to our own code.
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%08x [host]", address);
        const std::string where = describe_host_address(address);
        return where.empty() ? std::string(buffer) : std::string(buffer) + " " + where;
    }
    std::string text = g_symbols->describe(address);
    if (const XbeSectionHeader* section = g_image->find_section(address))
        text += " [" + g_image->section_name(*section) + "]";
    return text;
}

void log_backtrace(uint32_t eip, uint32_t ebp) {
    LOG_ERROR("crash", "  #0  %s", describe_address(eip).c_str());
    for (int depth = 1; depth < 48 && on_xbox_stack(ebp); ++depth) {
        const auto* frame = reinterpret_cast<const uint32_t*>(uintptr_t{ebp});
        const uint32_t return_address = frame[1];
        if (return_address == 0) break;
        LOG_ERROR("crash", "  #%-2d %s", depth, describe_address(return_address).c_str());
        if (frame[0] <= ebp) break;  // frames must move up the stack
        ebp = frame[0];
    }
}

void fatal_with_backtrace(const char* message, uint32_t eip, uint32_t ebp) {
    LOG_ERROR("fatal", "%s", message);
    log_backtrace(eip, ebp);
    run_fatal_exit_hook(true);
    std::_Exit(1);
}

void set_fatal_exit_hook(void (*hook)(bool error)) { g_exit_hook = hook; }

}  // namespace loader
