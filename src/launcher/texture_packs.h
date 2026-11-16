#pragma once

// The launcher's texture packs (docs/developers/texture-packs.md): a release of the pack repository (its
// cod_bro_textures.index + data parts), from the web or from a folder of downloaded parts, installed per pack as
// <mods>/<pack>/mod.ini + pack.idx + pack.dat. The stored files stay encrypted: the game unpacks them with keys
// from the player's own textures. No UI here (main.cpp's Data Files page); the tests use it too.

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "texture_index.h"

namespace launcher {

inline constexpr const char* kIndexName = "cod_bro_textures.index";

// Where the release files come from: a folder holding them, or a web address they sit under (ends with '/').
struct PackSource {
    std::filesystem::path folder;
    std::string base_url;
    std::string user_agent;
    bool web() const { return !base_url.empty(); }
};

// The newest release of `repository` ("owner/name"; pre-releases too) as a web source, or nothing (`error` says
// why). COD_BRO_TEXTURE_SOURCE=<base address> replaces the lookup (developer test servers). Blocks.
std::optional<PackSource> newest_pack_release(const std::string& repository, const std::string& user_agent,
                                              std::string& error);

// The source's index, checked against its SHA256SUMS.txt when the source has one (a web source must). Blocks.
std::optional<platform::TextureIndex> read_pack_index(const PackSource& source, std::string& error);

struct InstalledPack {
    std::string id, release, name;
    size_t files = 0;
    uint64_t bytes = 0;
    platform::TextureIndex index;  // its pack.idx
};
// Installed release packs: folders of `mods_dir` with a pack.idx.
std::vector<InstalledPack> installed_packs(const std::filesystem::path& mods_dir);

struct PackProgress {
    uint64_t done = 0, total = 0;  // bytes of the pack's stored files
    std::string message;
};

// Installs (or updates) pack `pack_id` of `index` from `source` into mods_dir/<pack_id>. Files an installed older
// version already holds (same id, kind and checksum) are copied from it; the rest are read from the source, each
// checked against its SHA-256. Works in mods_dir/<pack_id>.download/ (an interrupted install resumes there,
// re-checking what it has) and swaps the finished folder in. `cancel` stops it between chunks. Blocks.
bool install_pack(const PackSource& source, const platform::TextureIndex& index, const std::string& pack_id,
                  const std::filesystem::path& mods_dir, const std::function<void(const PackProgress&)>& progress,
                  const std::atomic<bool>& cancel, std::string& error);

// Deletes mods_dir/<pack_id> and an unfinished download of it.
bool remove_pack(const std::filesystem::path& mods_dir, const std::string& pack_id, std::string& error);

// One job on a thread of its own: looking up the release, or installing a pack (then the page re-reads the mods).
class PackTask {
public:
    enum class Status { Working, Done, Failed, Cancelled };
    struct Result {
        Status status = Status::Working;
        PackProgress progress;
        std::string error;
        std::optional<PackSource> source;           // lookup: the source found
        std::optional<platform::TextureIndex> index;  // lookup: its index
    };

    // Looks up the newest release (`folder` empty) or reads the folder's index.
    static std::unique_ptr<PackTask> lookup(std::string repository, std::filesystem::path folder,
                                            std::string user_agent);
    static std::unique_ptr<PackTask> install(PackSource source, platform::TextureIndex index, std::string pack_id,
                                             std::filesystem::path mods_dir);
    ~PackTask();  // cancels; the thread finishes on its own

    Result result() const;
    void cancel() { shared_->cancel = true; }

private:
    struct Shared {
        mutable std::mutex mutex;
        Result result;
        std::atomic<bool> cancel{false};
    };
    std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
};

}  // namespace launcher
