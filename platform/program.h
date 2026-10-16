#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace platform {

// Starts `program` with `args` (without the program name) in the background; the
// caller may exit right after. Returns false if it could not be started.
bool start_program(const std::filesystem::path& program, const std::vector<std::string>& args = {});

// The executable named `name` (".exe" added on Windows) next to the running one.
std::filesystem::path sibling_executable(const std::string& name);

}  // namespace platform
