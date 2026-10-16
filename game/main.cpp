// The final, standalone game (route C). Until the decompiled code moves in here it
// only checks that the portable layers build and find the assets on every platform.

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>

#include "log.h"
#include "paths.h"
#include "process.h"
#include "settings.h"

int main(int argc, char** argv) {
    // settings.ini first, the command line overrides it (as in cod_bro_loader).
    std::vector<std::string> args = platform::load_settings().as_args();
    args.insert(args.end(), argv + 1, argv + argc);
    std::optional<std::filesystem::path> assets_override;
    for (size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == "--assets") assets_override = args[i + 1];

    const auto assets = platform::resolve_asset_root(assets_override);
    if (!assets) {
        // The player gets the launcher; not when a developer named the folder.
        bool named = std::getenv("COD_BRO_ASSETS") && *std::getenv("COD_BRO_ASSETS");
        for (int i = 1; i < argc; ++i) named |= std::string(argv[i]) == "--assets";
        const auto launcher = platform::sibling_executable("cod_bro_launcher");
        if (!named && std::filesystem::exists(launcher)) platform::start_program(launcher);
        return 1;
    }
    LOG_INFO("game", "assets: %s", assets->string().c_str());
    LOG_INFO("game", "no decompiled game code yet - run cod_bro_loader for now");
    return 0;
}
