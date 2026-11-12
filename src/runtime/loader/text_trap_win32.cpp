#include "text_trap.h"

#include "log.h"

namespace loader {

bool install_text_trap(const std::filesystem::path&, const std::filesystem::path&) {
    LOG_ERROR("trap", "--text-trap is Linux only");
    return false;
}

void snapshot_code_sections(const XbeImage&) {}

bool install_text_clear(const std::filesystem::path&, const std::vector<std::filesystem::path>&) {
    LOG_ERROR("trap", "--text-clear is Linux only");
    return false;
}

void text_clear_protect() {}

}  // namespace loader
