#pragma once

// The launcher's update check: the newest release on GitHub (pre-releases included,
// as every build so far is one) against this build's version. Installing it is
// updater.h's job.

#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace launcher {

// "v0.1.12" or "0.1.12" -> {0, 1, 12}; nothing if it isn't numbers and dots.
std::optional<std::array<int, 3>> parse_version(const std::string& text);
// The first "tag_name" in a GitHub releases list (newest first).
std::optional<std::string> newest_tag(const std::string& releases_json);

class UpdateCheck {
public:
    enum class Status { Checking, UpToDate, Available, Failed };
    struct Result {
        Status status = Status::Checking;
        std::string version;  // the newest release's ("0.1.6"), when Available or UpToDate
        std::string tag;      // its tag ("v0.1.6")
        std::string url;      // its release page
    };

    // Starts the check on a thread of its own (left running if the launcher closes first).
    UpdateCheck(std::string repository, std::string current_version);
    Result result() const;

private:
    struct Shared {
        mutable std::mutex mutex;
        Result result;
    };
    std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
};

}  // namespace launcher
