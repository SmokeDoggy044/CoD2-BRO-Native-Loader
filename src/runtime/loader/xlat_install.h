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
    size_t count = 0;
    for (const game::Function& f : game::translated_functions())
        if (f.code) install_jump(f.campaign, f.code), ++count;
    LOG_INFO("loader", "translated level code: %zu functions", count);
}

}  // namespace loader
