#pragma once

// Start-up steps shared by the loader (cod_bro_loader) and the own engine host (cod_bro): options from settings.ini
// and the command line, the log and fatal-exit handling, the game files, and running the title with its in-process
// restarts.

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "options.h"

namespace loader {

class XbeImage;
struct ThreadEnv;

// settings.ini (or a replay's recorded text) merged with the command line. 0, or the exit code on failure.
int load_options(int argc, char** argv, std::optional<Options>& parsed);
// Log file, --diag folder, fatal-exit hook, uncaught-exception report. False: cannot write the log.
bool start_session(const Options& options, int argc, char** argv);
// The game files; none: starts the launcher (players) or tells the player, and returns nothing.
std::optional<std::filesystem::path> find_assets(const Options& options, int argc, char** argv);
// A data file next to the program (releases), else the source tree's tools/data.
std::filesystem::path data_file(const char* name);

// A message box for players without a console (not in scripted runs).
void tell_player(const std::string& what);
int startup_failed(const std::string& what);  // tells the player, returns 1
const std::optional<std::filesystem::path>& diag_dir();
std::vector<std::string>& session_args();  // this run's command line (repeated by a reboot)

// Runs the title's entry on `env` and, under --own-main, its in-process restarts; never returns.
int run_title(XbeImage& image, ThreadEnv& env, const Options& options);

}  // namespace loader
