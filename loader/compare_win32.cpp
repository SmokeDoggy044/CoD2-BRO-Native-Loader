// Comparison harness, Windows: decompiled functions replace the originals; testing
// them against the originals needs fork() and soft-dirty pages (compare_linux.cpp).

#include "compare.h"

#include <cstdlib>

#include "hooks.h"
#include "release_map.h"
#include "log.h"

namespace loader::compare {

void install(std::span<const game::Function> functions, const Config& config) {
    int replaced = 0, kept_original = 0;
    for (const game::Function& function : functions) {
        const uint32_t address = config.multiplayer ? function.multiplayer : function.campaign;
        if (!address || function.status != game::Status::Replaced) continue;
        if (keeps_original(function)) {
            ++kept_original;
            continue;
        }
        install_jump(address, function.code);
        ++replaced;
    }
    if (kept_original)
        LOG_INFO("compare", "another release of the game: its own code runs (%d decompiled functions are for the supported one)",
                 kept_original);
    if (config.enabled) LOG_WARN("compare", "--compare works on Linux only; the originals run");
    if (replaced) LOG_INFO("compare", "%d decompiled functions replace the originals", replaced);
}

Counts counts(const char*) { return {}; }

const uint8_t* in_test_flag() {
    static const uint8_t never = 0;
    return &never;
}

void host_lock_held(uint32_t) { std::abort(); }

ForceResult force_call(const char*, const char*, const char*, const TitleRegisters&, uint32_t, std::string& error) {
    error = "--compare works on Linux only";
    return ForceResult::Failed;
}

void flush() {}

}  // namespace loader::compare
