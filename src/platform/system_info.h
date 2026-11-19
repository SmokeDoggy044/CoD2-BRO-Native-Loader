#pragma once

#include <string>

namespace platform {

// "AMD Ryzen 7 7800X3D 8-Core Processor" (CPUID brand string), empty if unknown.
std::string cpu_name();

// One log line for bug reports: our version, the CPU (with its thread count), the RAM and the operating system
// (Windows build, or Wine and its version when running under it; Linux distribution and kernel). The GPU and its
// driver come from the renderer (Vulkan / Direct3D 9), which knows them only once it starts.
void log_system_info();

}  // namespace platform
