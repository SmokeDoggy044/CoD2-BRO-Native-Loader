#pragma once

// The translated level code (tools/translator, src/levels/translated, game::translated_functions()) in place of the
// original x86, where a build has it: cod_bro uses it by default (1.0.11; COD_BRO_XLAT=0 runs the original x86),
// cod_bro_loader only with COD_BRO_XLAT=1. Inline: compare_test builds hooks.cpp without the game code.

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <vector>

#include "functions.h"
#include "hooks.h"
#include "log.h"

namespace loader {

inline void install_translated_levels(bool by_default) {
    if (game::translated_functions().empty()) return;  // a build without it
    const char* xlat = std::getenv("COD_BRO_XLAT");
    if (xlat ? *xlat != '1' : !by_default) {
        if (xlat) LOG_INFO("loader", "translated level code: off (COD_BRO_XLAT=%s); the original x86 runs", xlat);
        return;
    }
    const char* why = nullptr;
    if (!game::translated_functions().empty() && !game::translated_levels_usable(&why)) {
        LOG_WARN("loader", "translated level code: not used (%s); the original x86 runs", why);
        return;
    }
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
    // Only where our engine has no C of its own: our own engine installs every C row (compare::install, all_c),
    // and the translation of the original x86 must not replace it. The two differ where the original reads words
    // nobody wrote: AeThreadFunctor5 Invoke (0x00459970) passes 7 words to BroScr_DeathPhysicsLaunch_2FromTag_2,
    // which reads 10 - its delay is the word above them, the functor's return address when translated, a host code
    // address; on Windows a float of 1e34, so the prologue's thrown soldiers never took their killing damage.
    std::vector<uint32_t> ours;
    for (const game::Function& f : game::functions())
        if (f.code && f.campaign) ours.push_back(f.campaign);
    std::sort(ours.begin(), ours.end());
    size_t count = 0, kept = 0;
    for (const game::Function& f : game::translated_functions()) {
        if (!f.code || !wanted(f.campaign)) continue;
        if (std::binary_search(ours.begin(), ours.end(), f.campaign)) {
            ++kept;
            continue;
        }
        install_jump(f.campaign, f.code), ++count;
    }
    LOG_INFO("loader", "translated level code: %zu functions (%zu more have our own C)", count, kept);
}

}  // namespace loader
