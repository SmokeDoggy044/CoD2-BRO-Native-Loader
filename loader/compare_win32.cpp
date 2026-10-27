// Comparison harness, Windows: decompiled functions replace the originals; testing
// them against the originals needs fork() and soft-dirty pages (compare_linux.cpp).

#include "compare.h"

#include "hooks.h"
#include "log.h"

namespace loader::compare {

void install(std::span<const game::Function> functions, const Config& config) {
    int replaced = 0;
    for (const game::Function& function : functions) {
        const uint32_t address = config.multiplayer ? function.multiplayer : function.campaign;
        if (!address || function.status != game::Status::Replaced) continue;
        install_jump(address, function.code);
        ++replaced;
    }
    if (config.enabled) LOG_WARN("compare", "--compare works on Linux only; the originals run");
    if (replaced) LOG_INFO("compare", "%d decompiled functions replace the originals", replaced);
}

Counts counts(const char*) { return {}; }

const uint8_t* in_test_flag() {
    static const uint8_t never = 0;
    return &never;
}

ForceResult force_call(const char*, const char*, const char*, const TitleRegisters&, uint32_t, std::string& error) {
    error = "--compare works on Linux only";
    return ForceResult::Failed;
}

void flush() {}

}  // namespace loader::compare
