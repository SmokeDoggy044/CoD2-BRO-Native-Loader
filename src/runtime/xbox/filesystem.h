#pragma once

// The Xbox object namespace for files: symbolic links (\??\D: -> \Device\CdRom0),
// devices backed by host folders, and file/directory handles.

#include <cstdint>
#include <memory>
#include <string_view>

#include "file.h"
#include "kernel.h"
#include "objects.h"

namespace xbox {

// Sets up the devices from `config`. Called by kernel_init.
bool filesystem_init(const KernelConfig& config);

// IoCreateSymbolicLink for host code. Returns an NTSTATUS.
uint32_t create_symbolic_link(std::string_view link_name, std::string_view target);

// What XAPI's XMountUtilityDrive does on the console - pick a cache partition,
// format it if needed, link Z: to it - without the raw disk access: links Z: to the
// cache folder, emptying it if `format` is set. Returns an NTSTATUS.
uint32_t mount_utility_drive(bool format);

// The host file behind an Xbox path (after symbolic links), e.g.
// "\\Device\\CdRom0\\default.xbe"; empty if it is no existing file.
std::filesystem::path host_file_path(std::string_view xbox_path);

// The host file behind an open file handle (null for directories and other
// handles), for SDK replacements that read files themselves.
std::shared_ptr<platform::File> host_file(Handle handle);

}  // namespace xbox
