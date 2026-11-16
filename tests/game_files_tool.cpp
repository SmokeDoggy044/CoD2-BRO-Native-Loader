// Developer tool (not a test): the launcher's game-file check and disc dump extraction without the window.
//   game_files_tool check SOURCE            (a disc dump or a folder)
//   game_files_tool extract DUMP TARGET     (checks first; extracts only a usable dump)
// The table: game_files.txt beside the program, or COD_BRO_GAME_FILE_TABLE.

#include <atomic>
#include <cstdio>
#include <memory>
#include <string>

#include "game_files.h"

int main(int argc, char** argv) {
    const std::string what = argc > 1 ? argv[1] : "";
    if (!((what == "check" && argc == 3) || (what == "extract" && argc == 4))) {
        std::fprintf(stderr, "usage: game_files_tool check SOURCE | extract DUMP TARGET\n");
        return 2;
    }
    const auto table = launcher::load_game_file_table();
    if (!table) {
        std::fprintf(stderr, "no game_files.txt\n");
        return 1;
    }
    std::atomic<bool> cancel{false};
    uint64_t shown = 0;
    auto progress = [&](const launcher::GameFilesProgress& p) {
        if (p.done - shown >= (512u << 20) || p.done == p.total) {
            shown = p.done;
            std::printf("%5.1f%% %s\n", p.total ? 100.0 * p.done / p.total : 0.0, p.message.c_str());
            std::fflush(stdout);
        }
    };
    std::string error;
    launcher::GameFilesCheck check;
    if (!launcher::check_game_files(argv[2], *table, check, progress, cancel, error)) {
        std::fprintf(stderr, "check: %s\n", error.c_str());
        return 1;
    }
    std::printf("%s\n", check.summary().c_str());
    for (const auto& p : check.missing) std::printf("  missing %s\n", p.c_str());
    for (const auto& p : check.damaged) std::printf("  damaged %s\n", p.c_str());
    if (what == "check") return check.usable() ? 0 : 1;
    if (!check.usable()) return 1;
    shown = 0;
    if (!launcher::extract_disc_image(argv[2], check.edition, argv[3], progress, cancel, error)) {
        std::fprintf(stderr, "extract: %s\n", error.c_str());
        return 1;
    }
    return 0;
}
