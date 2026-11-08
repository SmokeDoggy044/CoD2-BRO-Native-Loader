#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace platform {

// The player's settings: settings.ini in the user data folder, written by the
// launcher. Lines are "key = value"; a key is a command-line option without its
// "--" (aspect, scale, assets, ...), so the game reads the file as options placed
// before its command line, and options given there win. "true"/"false" stand for
// options without a value (silent = true -> --silent).
class Settings {
public:
    std::optional<std::string> get(const std::string& key) const;
    void set(const std::string& key, const std::string& value);
    void erase(const std::string& key);
    bool empty() const { return entries_.empty(); }

    // Parses the file's text; malformed lines are skipped.
    static Settings parse(const std::string& text);
    std::string format() const;

    // The settings as command-line options, in file order.
    std::vector<std::string> as_args() const;

private:
    std::vector<std::pair<std::string, std::string>> entries_;
};

// <user data folder>/settings.ini
std::filesystem::path settings_path();

// An empty Settings if the file does not exist.
Settings load_settings(const std::filesystem::path& path = settings_path());
bool save_settings(const Settings& settings, const std::filesystem::path& path = settings_path());

}  // namespace platform
