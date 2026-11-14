#pragma once

// Every setting of the game, described once (own engine 0.6a, docs/developers/own-engine-host.md "0.6 plan").
//
// A setting is a command-line option (--key) and, unless it is command-line only, a settings.ini line
// (key = value). From this table come: the option parser's checks and error messages (src/runtime/loader/
// options.cpp applies the checked values to its Options), --help, settings.ini's one line per setting, and the
// conversion of the older combined lines ("graphics-features = fxaa=0 sharpness=0.3", "volume = ...",
// "reverb = ...", "enhanced = ...") into those lines. The launcher and the PC menu draw from it in 0.6b.
//
// Values are text in the setting's normal form: switches "on"/"off", flags "true"/"false", numbers as given.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace platform {

class Settings;

enum class SettingType : uint8_t {
    Flag,    // no value on the command line; "true"/"false" in settings.ini
    Switch,  // on|off (also accepted: 1/0, true/false)
    Int,     // a whole number in min..max
    Float,   // a number in min..max
    Choice,  // one of `choices` ("a|b|c")
    Text,    // any text (checked by `check` when set)
    Path,    // a file or folder
    Custom,  // more than one value or an optional one: the option parser reads it itself (command line only)
};

enum class SettingGroup : uint8_t { Files, Display, Graphics, Sound, Controls, Network, Testing, Developer };

enum SettingFlags : uint8_t {
    kLive = 1,         // changes while playing (the PC menu)
    kCommandLine = 2,  // command line only: never read from or written to settings.ini
    kLauncher = 4,     // launcher only: in settings.ini, not an option of the game
    kHidden = 8,       // not in --help (internal, or an older form still read)
};

struct SettingDef {
    const char* key;
    const char* arg;  // --help's placeholder for the value ("N", "on|off"); "" for a flag
    SettingType type;
    SettingGroup group;
    uint8_t flags;
    double min, max;            // Int, Float
    const char* choices;        // Choice: "a|b|c"
    const char* default_value;  // normal form; "" = none (unset)
    const char* help;           // --help, one paragraph (wrapped when printed)
    // One part of an older combined setting ("graphics-features", "volume", "reverb", "enhanced"): that setting
    // and the part's key in it. The game still reads the combined form (recordings, the command line).
    const char* part_of = nullptr;
    const char* part_key = nullptr;
    // Text/Custom: checks the value, may rewrite it to its normal form; false with `error` = what it needs.
    bool (*check)(std::string& value, std::string* error) = nullptr;
};

// How a setting shows in the launcher and the PC menu (src/ui/settings_widgets.h draws them in this table's order).
struct SettingUi {
    const char* key;
    const char* section;  // separator title the setting starts (nullptr: under the one before)
    const char* label;
    const char* tip;                      // tooltip (nullptr: none)
    const char* choice_labels = nullptr;  // Choice: shown names, in `choices` order ("Vulkan|Direct3D 9")
    const char* format = nullptr;         // slider format ("%.0f%%", "%+.1f dB")
    // Shown under (indented) and enabled only while another setting is on: "sharpen" (a switch or flag) or
    // "graphics=enhanced" (a value).
    const char* parent = nullptr;
    const char* checkbox_value = nullptr;  // a two-way Choice drawn as a checkbox: its value when ticked
    bool logarithmic = false;
};

std::span<const SettingDef> setting_defs();
std::span<const SettingUi> setting_uis();
const SettingUi* setting_ui(std::string_view key);
// By key (without "--"); nullptr when there is none.
const SettingDef* find_setting(std::string_view key);
const char* setting_group_name(SettingGroup group);

// The value in its normal form, or nullopt with `error` saying what the setting needs ("a number (1-8)").
std::optional<std::string> check_setting(const SettingDef& def, std::string_view value, std::string* error = nullptr);

// The usage text (--help), from the table.
std::string settings_help(const char* program);

// Older combined lines (graphics-features, volume, reverb, enhanced) become one line per part; parts with their
// default value are left out. True when something changed.
bool upgrade_settings(Settings& settings);
// A combined setting's parts in settings.ini as its "key=value ..." text (nullopt when none is set): for code that
// still keeps them in one struct (render::GraphicsFeatures, audio::CategoryVolumes, ...).
std::optional<std::string> combined_setting(const Settings& settings, std::string_view combined);
// Writes a combined setting's "key=value ..." text as its parts (those not named are removed: back to default).
void set_combined_setting(Settings& settings, std::string_view combined, const std::string& text);

}  // namespace platform
