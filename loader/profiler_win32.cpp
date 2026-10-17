#include "profiler.h"

#include <cstdlib>

#include "log.h"

namespace loader {

void start_profiler_from_env(const XbeImage&, const SymbolTable&) {
    if (std::getenv("XB_PROFILE")) LOG_WARN("profile", "XB_PROFILE is only supported on Linux");
}

}  // namespace loader
