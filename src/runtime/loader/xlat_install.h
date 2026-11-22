#pragma once

// Developers: COD_BRO_XLAT=1 runs the translated level code (tools/translator, game::translated_functions())
// instead of the original x86. Inline: compare_test builds hooks.cpp without the game code.

#include <cstddef>
#include <cstdlib>

#include "functions.h"
#include "hooks.h"
#include "log.h"

namespace loader {

inline void install_translated_levels() {
    const char* xlat = std::getenv("COD_BRO_XLAT");
    if (!xlat || *xlat != '1') return;
    // COD_BRO_XLAT_ONLY=addr,addr (hex): only those entries (bisecting; translated callers still call translated
    // callees of their own level directly).
    const char* only = std::getenv("COD_BRO_XLAT_ONLY");
    auto wanted = [only](uint32_t address) {
        if (!only) return true;
        for (const char* p = only; *p;) {
            char* end = nullptr;
            if (std::strtoul(p, &end, 16) == address) return true;
            if (end == p) break;
            p = *end == ',' ? end + 1 : end;
        }
        return false;
    };
    size_t count = 0;
    for (const game::Function& f : game::translated_functions())
        if (f.code && wanted(f.campaign)) install_jump(f.campaign, f.code), ++count;
    LOG_INFO("loader", "translated level code: %zu functions", count);
}

}  // namespace loader
