#pragma once

// The OS side of the sampling profiler (profiler.h): profiler_linux.cpp, profiler_win32.cpp.

#include <cstdint>
#include <string>
#include <vector>

namespace loader::profiler_os {

struct Sample {
    uint32_t eip;
    uint32_t tid;
};

// Samples every other thread that is running about once a millisecond for `seconds`.
// Returns the number of ticks (one look at every thread each).
size_t sample(double seconds, std::vector<Sample>& samples);

std::string thread_name(uint32_t tid);

// The share of the sampling time the thread ran (1 = a whole core), measured from its
// CPU time where the OS keeps it per thread; negative where it does not (Linux: only
// running threads are sampled, so the share of ticks with a sample is that).
double busy(uint32_t tid);

// Where a host address is: library and symbol, or the file and its offset there
// (for addr2line).
std::string host_place(uint32_t eip);

}  // namespace loader::profiler_os
