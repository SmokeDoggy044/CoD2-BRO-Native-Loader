#pragma once

// Comparison harness: puts the decompiled functions (game::functions()) in place of
// the originals, or tests them against the originals while the game runs. A tested
// call runs both versions, each in its own copy of the process, and compares their
// registers and every byte of memory they wrote; the game then goes on with the
// original. Design: docs/comparison-harness.md. Testing is Linux-only.

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "functions.h"

namespace loader::compare {

struct Config {
    bool enabled = false;      // test Compare functions (otherwise their originals run untouched)
    bool multiplayer = false;  // use the functions' codmp_xboxf.xbe addresses
    // The title's code, which stays executable in the test processes.
    uint32_t image_begin = 0;
    uint32_t image_end = 0;
    // Host libraries the test processes may run (substrings of their file names): the
    // decompiled code's and the C runtime's. Any other host code a test reaches -
    // kernel exports, SDK replacements, the window, GPU, sound - ends the test as
    // inconclusive, so that a test process never touches the parent's devices or files.
    std::vector<std::string> libraries = {"libcod_bro_game", "libc.so", "libm.so", "libstdc++", "libgcc_s",
                                          "ld-linux"};
    // Lowest address of the calling thread's stack, or 0 if unknown. The stack below
    // the caller's frame is scratch space and is not compared.
    uint32_t (*stack_limit)() = nullptr;
    // A title address with its symbol, for reports (null: hex only).
    std::string (*describe)(uint32_t address) = nullptr;
    // CSV with per-function counts, merged into on flush(); empty = none.
    std::filesystem::path results;
    uint32_t timeout_ms = 2000;  // per test; a test still running then is inconclusive
    // Which calls are tested: the first `first_calls`, then every `every`-th.
    uint32_t first_calls = 16;
    uint32_t every = 64;
};

// Hooks `functions`: Replaced ones always, Compare ones only when testing is enabled.
// Call once, before the title runs. `functions` must outlive the process.
void install(std::span<const game::Function> functions, const Config& config);

struct Counts {
    uint64_t calls = 0;
    uint64_t tested = 0;
    uint64_t matched = 0;       // identical registers and memory
    uint64_t drift = 0;         // only floats differ, and only in their last bits
    uint64_t mismatched = 0;    // anything else differs, or only the new version crashed
    uint64_t inconclusive = 0;  // host code reached, timeout, the original crashed, ...
};

// Counts of the tested function named `name` since install (zero if not tested).
Counts counts(const char* name);

// Adds the counts since the last flush to config.results. Any thread, any time;
// call it before the process exits or re-execs.
void flush();

}  // namespace loader::compare
