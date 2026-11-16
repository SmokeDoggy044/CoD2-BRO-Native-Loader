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

// Ends this process now with `code`: the tasks registered with at_end_process (newest first), every C stream
// flushed, then the process ends (Windows: TerminateProcess) - no static destructors, atexit handlers or DLL detach
// code. On Windows those run after the other threads (game, render, sound, texture loading) are gone, and one of
// them kept the process alive after its window closed. Any thread may call it.
[[noreturn]] void end_process(int code);
// The same without the tasks (crash handlers, which may run with any lock held): streams flushed, process ended.
[[noreturn]] void terminate_now(int code);
// Work that must happen before end_process ends the process (writing results out). Not for joining threads.
void at_end_process(void (*task)());

// A program that comes with the operating system (System32 on Windows, /usr/bin or
// /bin elsewhere), by full path so that no other folder can supply it.
std::filesystem::path system_program(const std::string& name);

}  // namespace platform
