#include "text_trap.h"

#include "log.h"

namespace loader {

bool install_text_trap(const std::filesystem::path&, const std::filesystem::path&) {
    LOG_ERROR("trap", "--text-trap is Linux only");
    return false;
}

}  // namespace loader
