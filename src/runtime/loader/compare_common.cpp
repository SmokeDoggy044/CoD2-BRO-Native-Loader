// Comparison harness, both platforms: which Replaced functions run instead of the originals.

#include "compare.h"

#include <cstdlib>
#include <cstring>
#include <string_view>

#include "release_map.h"

namespace loader::compare {

bool listed_in(const char* list, const char* name) {
    const std::string_view names(list), wanted(name);
    for (size_t at = 0; at <= names.size();) {
        const size_t comma = std::min(names.find(',', at), names.size());
        if (names.substr(at, comma - at) == wanted) return true;
        at = comma + 1;
    }
    return false;
}

bool keeps_original(const game::Function& function) {
    // The C uses the supported build's addresses throughout: another release keeps the original.
    // XBCMP_ORIGINALS=1 keeps every original, XBCMP_ORIGINALS=name,name those (finding which
    // replaced function a problem comes from).
    static const char* const originals = std::getenv("XBCMP_ORIGINALS");
    return !release_map().identity() ||
           (originals && (std::strcmp(originals, "1") == 0 || listed_in(originals, function.name)));
}

bool replaced(std::span<const game::Function> functions, uint32_t campaign_address) {
    for (const game::Function& function : functions)
        if (function.campaign == campaign_address)
            return function.status == game::Status::Replaced && !keeps_original(function);
    return false;
}

}  // namespace loader::compare
