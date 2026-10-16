#pragma once

#include <filesystem>
#include <optional>

namespace platform {

// Directory containing the running executable.
std::filesystem::path executable_dir();

// Finds the game's asset folder. First match wins:
//   1. `cli_override` (e.g. --assets on the command line)
//   2. the COD_BRO_ASSETS environment variable
//   3. <executable dir>/assets, if it holds anything besides README.md: the release
//      layout, which dev builds mimic with a link (see COD_BRO_GAME_FILES in CMake)
std::optional<std::filesystem::path> resolve_asset_root(
    const std::optional<std::filesystem::path>& cli_override);

// <executable dir>/assets, where a release keeps the game files.
std::filesystem::path default_asset_dir();

// Whether `dir` holds a dump of the game's disc (what the launcher checks): the
// executable and the media folder. Route C will not need default.xbe any more.
bool has_game_files(const std::filesystem::path& dir);

// Where the game writes (saves, cache): the per-user data folder, i.e.
// $XDG_DATA_HOME/cod_bro or ~/.local/share/cod_bro on Linux, %APPDATA%\cod_bro on
// Windows. Never inside the assets, which are read-only like the disc.
std::filesystem::path user_data_dir();

}  // namespace platform
