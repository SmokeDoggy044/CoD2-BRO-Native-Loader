#pragma once

// Minimal test support for the kernel layer: a CHECK macro, export lookup by name,
// and a fake host (what the loader provides to src/runtime/xbox/ in the real program).

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <thread>

#include "compiler.h"
#include "dispatcher.h"
#include "kernel.h"
#include "title_calls.h"

namespace test {

inline int g_failures = 0;

#define CHECK(condition, ...)                                                          \
    do {                                                                               \
        if (!(condition)) {                                                            \
            ++::test::g_failures;                                                      \
            std::printf("FAIL %s:%d: %s", __FILE__, __LINE__, #condition);             \
            __VA_OPT__(std::printf(" - "); std::printf(__VA_ARGS__);)                  \
            std::printf("\n");                                                         \
        }                                                                              \
    } while (0)

// A kernel export by name, as the title would call it through its thunk table.
template <typename Fn>
Fn find_export(const char* name) {
    for (const auto& impl : xbox::kernel_impls())
        if (std::strcmp(impl.name, name) == 0) return reinterpret_cast<Fn>(impl.address);
    std::printf("missing kernel export %s\n", name);
    std::exit(2);
}

// Polls `condition` until it holds or `timeout` passes; returns whether it held.
template <typename Condition>
bool eventually(Condition condition, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

// --- Fake host --------------------------------------------------------------------
//
// Each thread gets a made-up KTHREAD address and its own IRQL byte. "Title"
// functions are host functions registered under small ids, so the tests do not
// depend on host code addresses fitting in 32 bits.

using GuestFunction = void (*)(const uint32_t* args, uint32_t count);

inline thread_local uint32_t t_kthread = 0;
inline thread_local uint8_t t_irql = 0;
inline std::atomic<uint32_t> g_next_kthread{0x10000};
inline GuestFunction g_guest_functions[16] = {};

inline void attach_thread() { t_kthread = g_next_kthread += 0x200; }

inline uint32_t register_guest(uint32_t id, GuestFunction function) {
    g_guest_functions[id] = function;
    return id;
}

// `cdrom_root`/`data_root`: host folders behind the Xbox devices (none if empty).
inline void init_kernel(const std::filesystem::path& cdrom_root = {}, const std::filesystem::path& data_root = {}) {
    xbox::KernelConfig config{};
    config.image_file_name = "\\Device\\CdRom0\\default.xbe";
    config.cdrom_root = cdrom_root;
    config.data_root = data_root;
    config.current_thread = [] { return t_kthread; };
    config.current_irql = [] { return &t_irql; };
    config.attach_kernel_thread = attach_thread;
    config.call_guest = [](uint32_t function, const uint32_t* args, uint32_t count) {
        if (function >= std::size(g_guest_functions) || !g_guest_functions[function]) {
            std::printf("call_guest: unknown function %u\n", function);
            std::exit(2);
        }
        g_guest_functions[function](args, count);
    };
    if (!xbox::kernel_init(config)) {
        std::printf("kernel_init failed\n");
        std::exit(2);
    }
    // As the loader's main: host code signals title events and raises IRQL through the kernel.
    xbox::set_event_signal([](uint32_t handle) { xbox::set_event_handle(handle); });
    xbox::set_irql_calls({&xbox::raise_irql_to, &xbox::lower_irql_to});
    attach_thread();
}

inline int finish(const char* name) {
    if (g_failures)
        std::printf("%s: %d failure(s)\n", name, g_failures);
    else
        std::printf("%s: all passed\n", name);
    return g_failures ? 1 : 0;
}

}  // namespace test
