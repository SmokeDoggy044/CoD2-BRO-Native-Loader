// State the kernel shares with the host part of this library (memory, replay, cod_bro): the title clocks' start,
// the CPU time stamp counter and the --trace flag. No kernel exports here.
#include <atomic>
#include <chrono>
#include <cstdint>

#include "kernel.h"
#include "replay.h"

namespace xbox {

namespace {
std::atomic<bool> g_trace{false};
const auto g_start = std::chrono::steady_clock::now();  // at static initialisation, as before the split
}  // namespace

std::chrono::steady_clock::time_point clock_start() { return g_start; }

bool kernel_trace_enabled() { return g_trace.load(std::memory_order_relaxed); }
void set_kernel_trace(bool on) { g_trace = on; }

// 733.33 MHz = 11/15 cycles per nanosecond (integer only: title threads run with the
// title's FPU modes).
int64_t cpu_time_stamp_counter() {
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - g_start).count();
    return replay::clock(ns / 15 * 11 + ns % 15 * 11 / 15);
}

}  // namespace xbox
