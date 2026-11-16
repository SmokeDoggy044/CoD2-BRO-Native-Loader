#pragma once

// The player's game files (no UI; main.cpp's Game files section, tests/game_files_test.cpp): an Xbox disc dump
// (XISO, or a full dump with the game partition further in) or a folder of extracted files, recognised by
// default.xbe's SHA-1 and checked file by file against the known editions (game_files.txt beside the launcher,
// made by tools/game_file_tables.py: paths, sizes and SHA-1s only), and the image extracted into the game files
// folder.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "file.h"

namespace launcher {

struct GameFileTable {
    struct Row {
        std::string sha1, path;  // path with '/'
        uint64_t size = 0;
    };
    struct Edition {
        std::string id, xbe_sha1, name;
        std::vector<Row> files;
    };
    std::vector<Edition> editions;
    const Edition* by_xbe(const std::string& sha1) const;
    const Edition* by_id(const std::string& id) const;
};
std::optional<GameFileTable> parse_game_file_table(const std::string& text);
// game_files.txt beside the launcher (or COD_BRO_GAME_FILE_TABLE), or nothing.
std::optional<GameFileTable> load_game_file_table();

// An Xbox disc dump's file system (XDVDFS).
class DiscImage {
public:
    struct Entry {
        std::string path;  // '/' separated
        uint64_t size = 0, offset = 0;  // bytes; offset in the image file
    };
    static std::unique_ptr<DiscImage> open(const std::filesystem::path& path, std::string& error);
    const std::vector<Entry>& files() const { return files_; }
    bool read(uint64_t offset, void* buffer, uint32_t size);

private:
    std::unique_ptr<platform::File> file_;
    uint64_t size_ = 0;
    std::vector<Entry> files_;
};

struct GameFilesProgress {
    uint64_t done = 0, total = 0;
    std::string message;
};

struct GameFilesCheck {
    const GameFileTable::Edition* edition = nullptr;  // null: not a known edition
    std::vector<std::string> missing, damaged;          // paths (damaged: wrong size or checksum)
    size_t files = 0;
    uint64_t bytes = 0;
    bool has_xbe = false;
    // Known edition with every file intact, or an unknown one with every path of the standard release.
    bool usable() const { return has_xbe && missing.empty() && damaged.empty(); }
    std::string summary() const;  // one line for the page
};

// Checks an image or a folder (`source`) against the table. False only when it could not be read at all or was
// cancelled (`error`); a damaged dump gives true with the findings in `result`.
bool check_game_files(const std::filesystem::path& source, const GameFileTable& table, GameFilesCheck& result,
                      const std::function<void(const GameFilesProgress&)>& progress, const std::atomic<bool>& cancel,
                      std::string& error);

// Extracts image `image` into `target` (created; files of the same names are replaced, nothing else is touched),
// each file through <name>.part and checked against `edition`'s SHA-1 (when known) before it takes its name.
bool extract_disc_image(const std::filesystem::path& image, const GameFileTable::Edition* edition,
                        const std::filesystem::path& target,
                        const std::function<void(const GameFilesProgress&)>& progress,
                        const std::atomic<bool>& cancel, std::string& error);

// One check or extraction on a thread of its own (the page polls it).
class GameFilesTask {
public:
    enum class Status { Working, Done, Failed, Cancelled };
    struct Result {
        Status status = Status::Working;
        GameFilesProgress progress;
        GameFilesCheck check;
        std::string error;
    };
    static std::unique_ptr<GameFilesTask> check(std::filesystem::path source,
                                                std::shared_ptr<const GameFileTable> table);
    static std::unique_ptr<GameFilesTask> extract(std::filesystem::path image,
                                                  std::shared_ptr<const GameFileTable> table, std::string edition_id,
                                                  std::filesystem::path target);
    ~GameFilesTask();  // cancels; the thread ends on its own
    Result result() const;
    void cancel() { shared_->cancel = true; }

private:
    struct Shared {
        mutable std::mutex mutex;
        Result result;
        std::atomic<bool> cancel{false};
        std::shared_ptr<const GameFileTable> table;  // kept alive for the result's edition pointer
    };
    std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
};

}  // namespace launcher
