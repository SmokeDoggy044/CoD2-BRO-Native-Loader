#pragma once

// The mods folder (phase 1f, docs/developers/own-engine-host.md): which mods exist, their mod.ini facts, and the
// player's order and on/off choice - settings.ini's `mod-list` line, shared by the game (texture_pack::open) and
// the launcher's Data Files page.
//
// mod-list = ui, campaign, -oldpack
//   folder names in load order (when two mods cover the same texture the earlier one wins); a leading '-' turns a
//   mod off. Mods the list does not name come after the named ones, on, in folder-name order (a newly added mod
//   just works). Names of mods no longer there are ignored.

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace platform {

struct ModInfo {
    std::string folder;                  // folder name under the mods folder (the mod-list name)
    std::string name, version, author;   // mod.ini (name falls back to the folder)
    std::string kind, source;            // mod.ini "kind" (textures), "source" (one line about it)
    bool on = true;
};

// Every folder of `mods_dir` with a mod.ini, ordered and switched by `mod_list` (the setting's value).
std::vector<ModInfo> scan_mods(const std::filesystem::path& mods_dir, std::string_view mod_list = {});

// The setting's value for these mods in this order ("" when it says nothing: all on, in folder-name order).
std::string format_mod_list(const std::vector<ModInfo>& mods);

// .dds files in <mod>/textures (the launcher's count; the game counts while it indexes).
size_t count_mod_textures(const std::filesystem::path& mod_dir);

// mod-list's check: comma-separated folder names, each optionally with a leading '-'; rewritten to "a, b, -c".
bool check_mod_list(std::string& value, std::string* error);

}  // namespace platform
