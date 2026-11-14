#include "settings.h"

#include <fstream>
#include <sstream>
#include <system_error>

#include "log.h"
#include "paths.h"
#include "settings_schema.h"

namespace fs = std::filesystem;

namespace platform {

namespace {

std::string trim(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t\r");
    if (begin == std::string::npos) return {};
    const auto end = text.find_last_not_of(" \t\r");
    return text.substr(begin, end - begin + 1);
}

}  // namespace

std::optional<std::string> Settings::get(const std::string& key) const {
    for (const auto& [k, v] : entries_)
        if (k == key) return v;
    return std::nullopt;
}

void Settings::set(const std::string& key, const std::string& value) {
    for (auto& [k, v] : entries_) {
        if (k == key) {
            v = value;
            return;
        }
    }
    entries_.emplace_back(key, value);
}

void Settings::erase(const std::string& key) {
    std::erase_if(entries_, [&](const auto& entry) { return entry.first == key; });
}

Settings Settings::parse(const std::string& text) {
    Settings settings;
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = trim(line.substr(0, equals));
        if (!key.empty()) settings.set(key, trim(line.substr(equals + 1)));
    }
    return settings;
}

std::string Settings::format() const {
    std::string text =
        "# Call of Duty 2: Big Red One settings, written by cod_bro_launcher.\n"
        "# One line per setting; each is also a command-line option (cod_bro_loader --help), which wins.\n";
    for (const auto& [key, value] : entries_) text += key + " = " + value + "\n";
    return text;
}

std::vector<std::string> Settings::as_args() const {
    std::vector<std::string> args;
    for (const auto& [key, value] : entries_) {
        if (value == "false") continue;
        if (const SettingDef* def = find_setting(key); def && (def->flags & kLauncher)) continue;  // not the game's
        args.push_back("--" + key);
        if (value != "true") args.push_back(value);
    }
    return args;
}

fs::path settings_path() { return user_data_dir() / "settings.ini"; }

Settings load_settings(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    std::ostringstream text;
    text << file.rdbuf();
    // Older combined lines (graphics-features = ...) read as one line per setting; written so on the next save.
    Settings settings = Settings::parse(text.str());
    upgrade_settings(settings);
    return settings;
}

bool save_settings(const Settings& settings, const fs::path& path) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!(file << settings.format())) {
        LOG_ERROR("settings", "cannot write %s", path.string().c_str());
        return false;
    }
    return true;
}

}  // namespace platform
