#pragma once

// One-click update from the launcher: the release's archive for this platform from
// GitHub, checked against the release's SHA256SUMS.txt, unpacked with the system's tar
// (Windows has one since 10 1803) and moved into the install folder file by file. A
// replaced file moves to cod_bro_update/old/ first (Windows can rename a running
// executable but not overwrite it); the next start deletes that folder. assets/ (the
// player's game files) is never touched, and saves and settings live elsewhere.

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace launcher {

// Where an update is unpacked and the replaced files are kept, in the install folder.
inline constexpr const char* kUpdateDir = "cod_bro_update";

// The checksum `sums` (sha256sum's format: "<hex>  <name>" lines) lists for `file`.
std::optional<std::string> checksum_for(const std::string& sums, const std::string& file);

// Whether `dir` is an installed release (dev builds are not updated in place).
bool is_release_install(const std::filesystem::path& dir);

// Deletes what an earlier update left in `dir` (the files it replaced).
void remove_update_leftovers(const std::filesystem::path& dir);

// Moves every file under `from` to the same place under `to`, except assets/; a file
// already there moves to the same place under `backup` first. On a failure, everything
// moved so far goes back and `error` says what failed.
bool install_files(const std::filesystem::path& from, const std::filesystem::path& to,
                   const std::filesystem::path& backup, std::string& error);

class Updater {
public:
    enum class Status { Working, Installed, Failed };
    struct Result {
        Status status = Status::Working;
        std::string message;  // what it is doing, or why it failed
    };

    // Starts installing release `tag` (version `version`) of `repository` into
    // `install_dir` on a thread of its own. `package` is this platform's archive suffix
    // ("windows.zip": cod_bro-<version>-windows.zip).
    Updater(std::string repository, std::string tag, std::string version, std::string package,
            std::filesystem::path install_dir);
    Result result() const;

private:
    struct Shared {
        mutable std::mutex mutex;
        Result result;
    };
    std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
};

}  // namespace launcher
