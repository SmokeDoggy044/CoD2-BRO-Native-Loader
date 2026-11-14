#include "mod_list.h"

#include <algorithm>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace platform {

namespace {

std::string trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return std::string(s);
}

// "a, -b" -> {("a", on), ("b", off)}; empty items skipped.
std::vector<std::pair<std::string, bool>> parse_list(std::string_view text) {
    std::vector<std::pair<std::string, bool>> items;
    while (!text.empty()) {
        const size_t comma = text.find(',');
        std::string item = trim(text.substr(0, comma));
        text = comma == std::string_view::npos ? std::string_view{} : text.substr(comma + 1);
        bool on = true;
        if (!item.empty() && item[0] == '-') {
            on = false;
            item = trim(std::string_view(item).substr(1));
        }
        if (!item.empty()) items.emplace_back(std::move(item), on);
    }
    return items;
}

void read_ini(const fs::path& file, ModInfo& mod) {
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos || line.empty() || line[0] == '#' || line[0] == ';') continue;
        const std::string key = trim(std::string_view(line).substr(0, eq));
        const std::string value = trim(std::string_view(line).substr(eq + 1));
        if (key == "name") mod.name = value;
        else if (key == "version") mod.version = value;
        else if (key == "author") mod.author = value;
        else if (key == "kind") mod.kind = value;
        else if (key == "source") mod.source = value;
    }
    if (mod.name.empty()) mod.name = mod.folder;
}

}  // namespace

std::vector<ModInfo> scan_mods(const fs::path& mods_dir, std::string_view mod_list) {
    std::vector<ModInfo> found;
    std::error_code ec;
    if (!fs::is_directory(mods_dir, ec)) return found;
    for (const auto& entry : fs::directory_iterator(mods_dir, ec)) {
        if (!entry.is_directory(ec) || !fs::exists(entry.path() / "mod.ini", ec)) continue;
        ModInfo mod;
        mod.folder = entry.path().filename().string();
        read_ini(entry.path() / "mod.ini", mod);
        found.push_back(std::move(mod));
    }
    std::sort(found.begin(), found.end(), [](const ModInfo& a, const ModInfo& b) { return a.folder < b.folder; });
    // The listed ones first, in the list's order; the rest keep folder-name order.
    std::vector<ModInfo> mods;
    for (const auto& [folder, on] : parse_list(mod_list)) {
        auto it = std::find_if(found.begin(), found.end(), [&](const ModInfo& m) { return m.folder == folder; });
        if (it == found.end()) continue;
        it->on = on;
        mods.push_back(std::move(*it));
        found.erase(it);
    }
    for (ModInfo& mod : found) mods.push_back(std::move(mod));
    return mods;
}

std::string format_mod_list(const std::vector<ModInfo>& mods) {
    bool plain = true;  // all on, in folder-name order: nothing to say
    for (size_t i = 0; i < mods.size(); ++i)
        if (!mods[i].on || (i > 0 && mods[i].folder < mods[i - 1].folder)) plain = false;
    if (plain) return {};
    std::string text;
    for (const ModInfo& mod : mods) text += (text.empty() ? "" : ", ") + std::string(mod.on ? "" : "-") + mod.folder;
    return text;
}

size_t count_mod_textures(const fs::path& mod_dir) {
    size_t n = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(mod_dir / "textures", ec))
        if (entry.path().extension() == ".dds") ++n;
    return n;
}

bool check_mod_list(std::string& value, std::string* error) {
    std::string text;
    for (const auto& [folder, on] : parse_list(value)) {
        if (folder.find_first_of("/\\") != std::string::npos) {
            if (error) *error = "mod folder names separated by commas, '-' before a name turns it off";
            return false;
        }
        text += (text.empty() ? "" : ", ") + std::string(on ? "" : "-") + folder;
    }
    value = text;
    return true;
}

}  // namespace platform
