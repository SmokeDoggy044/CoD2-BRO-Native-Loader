#pragma once

#include <cstdint>
#include <string>

namespace loader {

class SymbolTable;
class XbeImage;

// Reports faults (SIGSEGV, SIGILL, ...; Windows: exceptions nothing handles, and
// hardware faults in title code) with registers and an Xbox-side backtrace, then
// exits. SIGINT/SIGTERM/SIGHUP (Windows: Ctrl+C, closing the console) exit quietly
// after the exit hook. Call before any other thread starts. Per platform.
void install_crash_handler(const XbeImage& image, const SymbolTable& symbols);

// Logs a backtrace by following the EBP chain, starting at `eip`.
void log_backtrace(uint32_t eip, uint32_t ebp);

// Logs `message` plus a backtrace and exits. For fatal conditions found in loader
// code, e.g. a call to an unimplemented kernel export.
[[noreturn]] void fatal_with_backtrace(const char* message, uint32_t eip, uint32_t ebp);

// Runs once before a crash, a fatal error or a stop signal ends the process
// (diagnostics summary, telling the player). `error`: a crash or fatal error, not a
// stop (Ctrl+C, kill).
void set_fatal_exit_hook(void (*hook)(bool error));

// For the platform parts: runs the exit hook on the first call; later callers (other
// threads crashing meanwhile) wait forever for the first to end the process.
void run_fatal_exit_hook(bool error);

// `address` with its Ghidra symbol and section if it is in the XBE, else "[host]" and,
// where the platform part can tell, the library and symbol (or offset) there.
std::string describe_address(uint32_t address);

// For the platform parts: "libfoo.so: symbol" or "libfoo.so +0xoffset"; empty if unknown.
std::string describe_host_address(uint32_t address);

// Remembers what describe_address uses (install_crash_handler).
void set_crash_context(const XbeImage& image, const SymbolTable& symbols);

}  // namespace loader
