#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace platform {

// Starts `program` with `args` (without the program name) in the background; the
// caller may exit right after. Returns false if it could not be started.
bool start_program(const std::filesystem::path& program, const std::vector<std::string>& args = {});

// Runs `program` with `args` (paths, so that non-ASCII folder names survive on
// Windows) without a console window and waits for it. Its exit code, or -1 if it could
// not be started.
int run_program(const std::filesystem::path& program, const std::vector<std::filesystem::path>& args);

// The executable named `name` (".exe" added on Windows) next to the running one.
std::filesystem::path sibling_executable(const std::string& name);

// A program that comes with the operating system (System32 on Windows, /usr/bin or
// /bin elsewhere), by full path so that no other folder can supply it.
std::filesystem::path system_program(const std::string& name);

}  // namespace platform
