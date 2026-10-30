#include "shader_dump.h"

#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <tuple>

#include "log.h"
#include "ngl.h"

namespace loader {
namespace {

std::filesystem::path g_dir;
std::set<std::tuple<char, uint32_t, uint32_t>> g_seen;  // kind, key, slot

const uint8_t* title_bytes(uint32_t address) { return reinterpret_cast<const uint8_t*>(uintptr_t{address}); }

void write_file(const std::string& name, const uint8_t* data, size_t size) {
    const std::filesystem::path path = g_dir / name;
    if (std::filesystem::exists(path)) return;
    if (std::FILE* file = std::fopen(path.string().c_str(), "wb")) {
        std::fwrite(data, 1, size, file);
        std::fclose(file);
    }
}

std::string hex(uint32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "%08x", value);
    return text;
}

void shader_loaded(uint32_t slot, char kind, uint32_t key, uint32_t program, uint32_t declaration) {
    if (!program || !g_seen.insert({kind, key, slot}).second) return;
    if (kind == 'v') {
        // Header: type (16 bits), instruction count (16 bits); then 16 bytes per instruction.
        const auto count = *reinterpret_cast<const uint16_t*>(title_bytes(program) + 2);
        write_file("vs_" + hex(key) + ".bin", title_bytes(program), 4 + count * 16u);
        if (declaration) write_file("decl_" + hex(declaration) + ".bin", title_bytes(declaration), 16 * 16);
    } else {
        write_file("ps_" + hex(key) + ".bin", title_bytes(program), 0xF0);
    }
    if (std::FILE* csv = std::fopen((g_dir / "shaders.csv").string().c_str(), "a")) {
        std::fprintf(csv, "%c,%08x,%08x,%08x\n", kind, key, slot, declaration);
        std::fclose(csv);
    }
}

}  // namespace

void start_shader_dump(const std::filesystem::path& dir) {
    g_dir = dir;
    std::filesystem::create_directories(dir);
    std::filesystem::remove(dir / "shaders.csv");
    game::renderer_pc_hooks().shader_loaded = &shader_loaded;
    LOG_INFO("loader", "shader programs go to %s", dir.string().c_str());
}

}  // namespace loader
