#pragma once

// Process-level things the route-B loader does differently on Linux and Windows
// (os_linux.cpp, os_win32.cpp). Each platform also has the entry point that calls
// loader_main.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace loader::os {

// Maps `size` bytes at exactly `base`, zeroed, readable, writable and executable
// (the XBE image). Null if the range is taken.
void* map_image(uint32_t base, uint32_t size);

// Windows: frees the ranges the stub executable held for the kernel's memory, just
// before kernel_init reserves them for itself. Nothing on Linux.
void release_reserved_ranges();

// Puts the launch data page where a restarted loader finds it (Linux: a memory
// file; Windows: a temporary file), inherited by the new process. Returns the
// number to pass with --launch-data (descriptor or handle), or -1.
int pass_launch_data(std::span<const uint8_t> page);

// Reads and closes what pass_launch_data passed; empty on failure.
std::vector<uint8_t> read_launch_data(int id);

// Starts the loader again with `args` (args[0] is replaced by the loader itself) in
// place of this process: exec on Linux; on Windows a new process, and this one ends.
[[noreturn]] void restart(const std::vector<std::string>& args);

}  // namespace loader::os

// The loader's main (main.cpp), called by the platform's entry point.
int loader_main(int argc, char** argv);
