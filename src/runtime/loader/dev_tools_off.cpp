// Player builds (COD_BRO_DEV_TOOLS off): the developer tools' entry points, doing nothing. Their options are refused
// before any of this is reached (options.cpp); the profiler's spec can still come from XB_PROFILE, and is ignored.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "log.h"
#include "menu_walk.h"
#include "orbit.h"
#include "profiler.h"
#include "shader_dump.h"
#include "text_trap.h"
#include "viewer.h"

namespace loader {

namespace {

bool not_in_this_build(const char* tool) {
    LOG_ERROR("loader", "%s is a developer tool; this build is for players and leaves it out", tool);
    return false;
}

}  // namespace

void start_profiler(const char* spec, uint32_t, const XbeImage&, const SymbolTable&) {
    if (spec && *spec) LOG_INFO("loader", "profiling (%s) is a developer tool; not in this build", spec);
}

bool install_text_trap(const std::filesystem::path&, const std::filesystem::path&) {
    return not_in_this_build("--text-trap");
}
void snapshot_code_sections(const XbeImage&) {}
bool install_text_clear(const std::filesystem::path&, const std::vector<std::filesystem::path>&) {
    return not_in_this_build("--text-clear");
}
void text_clear_protect() {}

void start_shader_dump(const std::filesystem::path&) { not_in_this_build("--dump-shaders"); }

bool load_menu_walk(const std::string&, const std::string&) { return not_in_this_build("--menu-walk"); }
void menu_walk_frame() {}
std::string menu_walk_screenshot() { return {}; }

bool load_orbit(const std::string&) { return not_in_this_build("--orbit"); }
std::vector<std::string> orbit_frame(uint32_t) { return {}; }

void set_viewer_models(std::vector<std::string>) {}
std::vector<std::string> viewer_frame(uint32_t) { return {}; }

}  // namespace loader
