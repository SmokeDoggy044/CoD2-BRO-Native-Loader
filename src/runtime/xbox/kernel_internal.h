#pragma once

// Shared by the files that implement kernel exports. Not part of the library's API.

#include <chrono>
#include <span>

#include "kernel.h"
#include "log.h"

#define KTRACE(...)                                \
    do {                                           \
        if (::xbox::kernel_trace_enabled())        \
            LOG_TRACE("kernel", __VA_ARGS__);      \
    } while (0)

#define KIMPL(name) {#name, reinterpret_cast<void*>(&name)}
#define KIMPL_DATA(name) {#name, const_cast<void*>(static_cast<const volatile void*>(&name))}

namespace xbox {

// Each implementation file's exports; kernel_impls() joins them.
std::span<const KernelImpl> basic_kernel_impls();
std::span<const KernelImpl> object_kernel_impls();
std::span<const KernelImpl> sync_kernel_impls();
std::span<const KernelImpl> dispatcher_kernel_impls();
std::span<const KernelImpl> settings_kernel_impls();
std::span<const KernelImpl> memory_kernel_impls();
std::span<const KernelImpl> filesystem_kernel_impls();
std::span<const KernelImpl> crypto_kernel_impls();

// Kernel state shared between threads is allocated once and never destroyed: title
// and kernel threads keep using it while the process exits, and destroying a
// condition variable that has waiters hangs. One instance per type, so give each
// file its own state struct.
template <typename T>
T& immortal() {
    static T* object = new T();
    return *object;
}

// Applies the KernelConfig's console settings to the EEPROM image.
void settings_init(const KernelConfig& config);

// KeQuerySystemTime's clock: 100ns units since 1601-01-01.
int64_t current_system_time();
int64_t to_system_time(std::chrono::system_clock::time_point time);

// RtlNtStatusToDosError.
uint32_t nt_status_to_dos_error(uint32_t status);

}  // namespace xbox
