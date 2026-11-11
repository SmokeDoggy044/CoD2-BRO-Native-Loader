#pragma once

// Original-code coverage for the own engine host (step 0.3): an INT3 on the first byte of every original
// function that still has its own bytes (not hooked). The first time one runs, the trap logs it with its
// caller, puts the byte back and lets it run on. What the log lists is the original code our host still needs.

#include <cstdint>
#include <filesystem>

namespace loader {

// `list`: lines "hexaddress name" (tools/text_trap_list.py). `out`: the log, one line per first entry
// ("address name caller"). Call after every hook is in place. False when the platform has no support or a
// file cannot be opened.
bool install_text_trap(const std::filesystem::path& list, const std::filesystem::path& out);

}  // namespace loader
