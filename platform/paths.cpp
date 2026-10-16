#include "paths.h"

#include <cstdlib>
#include <system_error>

#include "log.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace platform {

namespace {

bool has_assets(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return false;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.path().filename() != "README.md") return true;
    }
    return false;
}

}  // namespace

fs::path executable_dir() {
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    return fs::path(std::wstring(buffer, length)).parent_path();
#else
    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    return ec ? fs::current_path() : exe.parent_path();
#endif
}

fs::path user_data_dir() {
#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata) return fs::path(appdata) / "cod_bro";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) return fs::path(xdg) / "cod_bro";
    if (const char* home = std::getenv("HOME"); home && *home) return fs::path(home) / ".local/share/cod_bro";
#endif
    return executable_dir() / "data";
}

fs::path default_asset_dir() { return executable_dir() / "assets"; }

bool has_game_files(const fs::path& dir) {
    std::error_code ec;
    return fs::is_regular_file(dir / "default.xbe", ec) && fs::is_directory(dir / "media", ec);
}

std::optional<fs::path> resolve_asset_root(const std::optional<fs::path>& cli_override) {
    if (cli_override) {
        if (has_assets(*cli_override)) return *cli_override;
        LOG_ERROR("paths", "--assets %s does not exist or is empty", cli_override->string().c_str());
        return std::nullopt;
    }
    if (const char* env = std::getenv("COD_BRO_ASSETS"); env && *env) {
        if (has_assets(env)) return fs::path(env);
        LOG_WARN("paths", "COD_BRO_ASSETS=%s does not exist or is empty, ignoring", env);
    }
    const fs::path bundled = default_asset_dir();
    if (has_assets(bundled)) return bundled;
    LOG_ERROR("paths", "no game assets in %s (use --assets, COD_BRO_ASSETS or put them there)",
              bundled.string().c_str());
    return std::nullopt;
}

}  // namespace platform
