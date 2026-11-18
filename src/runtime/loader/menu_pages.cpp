// The settings pages of the game's own menus (own engine phase 4.2; fe_pc.h, src/engine/client/fe_pc_pages.cpp).
//
// The pages are lists of rows: settings from the schema (settings_schema.cpp: type, range, labels, parent switch),
// links to sub-pages, key bindings, the game's own master volume, and the actions Reset to Defaults / Apply and Exit
// / Back. A session starts at a root page with a snapshot of the live settings; live settings (platform::kLive)
// change at once, the others when the game starts next. Apply and Exit saves what changed to settings.ini (one line
// per setting, as the launcher writes it); Back on the root page undoes the live changes and drops the others.
// Runs on the title's thread (the menus' handlers): host code under HostFloatEnv.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bindings.h"
#include "devices.h"
#include "fe_pc.h"
#include "float_env.h"
#include "log.h"
#include "pc_menu.h"
#include "settings.h"
#include "settings_schema.h"
#include "settings_widgets.h"
#include "title_data.h"
#include "ui_text.h"
#include "xbox_d3d_api.h"

namespace loader {

namespace {

namespace fs = std::filesystem;
using platform::SettingDef;
using platform::SettingType;

enum class Row : uint8_t { Setting, Link, Controller, Key, GameVolume, Mod, Reset, Apply, Back };

struct RowDef {
    Row kind;
    const char* key = nullptr;    // Setting: the schema key; Link: the page id; Key: the action token
    const char* label = nullptr;  // shown name (Setting: the schema label when null)
};

struct PageDef {
    const char* id;
    const char* title;
    std::vector<RowDef> rows;
    bool in_game_only = false, front_end_only = false;
};

RowDef setting(const char* key, const char* label = nullptr) { return {Row::Setting, key, label}; }
RowDef link(const char* page, const char* label) { return {Row::Link, page, label}; }
RowDef key_row(const char* action) { return {Row::Key, action, nullptr}; }

// Root pages end in Reset to Defaults + Apply and Exit; sub-pages in Reset to Defaults + Back (added below).
const std::vector<PageDef>& pages() {
    static const std::vector<PageDef> kPages = {
        {"graphics", "GRAPHICS",
         {setting("graphics", "Enhanced Graphics"), link("display", "Display"), link("screen", "Screen"),
          link("image", "Image Quality"), link("lighting", "Lighting"), link("detail", "Detail")}},
        {"display", "DISPLAY",
         {setting("fullscreen"), setting("scale"), setting("aspect"), setting("render-scale"), setting("fps")}},
        {"screen", "SCREEN",
         {setting("brightness"), setting("fov"), setting("sharp-text"), setting("letterbox"), setting("language")}},
        {"image", "IMAGE QUALITY",
         {setting("fxaa", "Anti-aliasing"), setting("anisotropic"), setting("anisotropy"), setting("sharpen"),
          setting("sharpness"), setting("vibrance"), setting("vibrance-amount")}},
        {"lighting", "LIGHTING",
         {setting("original-shadows"), setting("sun-shadows"), setting("pixel-lighting"), setting("normal-maps"),
          setting("specular"), setting("world-dlights")}},
        {"detail", "DETAIL",
         {setting("ambient-occlusion"), setting("cavity"), setting("texture-detail"), setting("lod-bias"),
          setting("smooth-models"), setting("more-bodies"), link("mods", "Texture Packs")}},
        {"mods", "TEXTURE PACKS", {}},  // one row per installed pack (mod_rows)
        {"sound", "SOUND",
         {{Row::GameVolume, nullptr, "Master Volume"}, setting("audio", "Enhanced Sound"), link("volumes", "Volumes"),
          link("paths", "Sound Paths"), link("reverb", "Reverb"), link("quality", "Quality"),
          link("output", "Output")}},
        {"volumes", "VOLUMES",
         {setting("volume-effects", "Effects"), setting("volume-ambience", "Ambience"),
          setting("volume-dialogue", "Dialogue"), setting("volume-music", "Music"),
          setting("close-gunfire"), setting("own-gunfire"), setting("close-bullets", "Bullets Nearby")}},
        {"paths", "SOUND PATHS",
         {setting("occlusion"), setting("transmission"), setting("partial-cover"),
          setting("openings", "Through Openings"), setting("air")}},
        {"reverb", "REVERB",
         {setting("room-reverb"), setting("reverb-detail", "Room Detail"), setting("echoes"),
          setting("reverb-amount", "Amount"), setting("reverb-decay", "Decay"),
          setting("reverb-brightness", "Brightness"), setting("reverb-reflections", "Reflections")}},
        {"quality", "QUALITY",
         {setting("resampler"), setting("more-voices"), setting("stereo-scenes"), setting("reverb-clutter")}},
        {"output", "OUTPUT", {setting("speakers"), setting("silent")}},
        {"controls", "CONTROLS",
         {setting("mouse-sensitivity"), setting("aim-assist"), setting("sprint"), setting("sprint-duration"), setting("button-prompts"),
          {Row::Controller, nullptr, "Controller"}, link("keys1", "Keyboard")}},
        {"keys1", "KEYBOARD",
         {key_row("move-forward"), key_row("move-back"), key_row("move-left"), key_row("move-right"),
          key_row("jump"), key_row("crouch"), link("keys2", "More Keys")}},
        {"keys2", "KEYBOARD",
         {key_row("fire"), key_row("aim"), key_row("aim-toggle"), key_row("reload"), key_row("use"),
          key_row("sprint"), link("keys3", "More Keys")}},
        {"keys3", "KEYBOARD",
         {key_row("melee"), key_row("grenade"), key_row("next-weapon"), key_row("lean-left"),
          key_row("lean-right")}},
        {"settings", "SETTINGS",
         {link("graphics", "Graphics"), link("sound", "Sound"), link("controls", "Controls")}},
    };
    return kPages;
}

const PageDef* find_page(std::string_view id) {
    for (const PageDef& p : pages())
        if (id == p.id) return &p;
    return nullptr;
}

// Choices for settings that are free text or wide ranges in the schema: the launcher's presets.
struct Presets {
    const char* key;
    std::vector<const char*> values;
    std::vector<const char*> labels;  // empty: the values
};
const std::vector<Presets>& presets() {
    static const std::vector<Presets> kPresets = {
        {"aspect", {"4:3", "16:9", "21:9", "32:9"}, {}},  // the game takes 4:3, 16:9 or wider
        {"fps", {"display", "30", "60", "120", "144", "240"}, {"Display", "30", "60", "120", "144", "240"}},
        {"render-scale", {"", "0.5", "0.75", "1", "1.5", "2", "3", "4"}, {"Auto", "50%", "75%", "100%", "150%", "200%", "300%", "400%"}},
        {"anisotropy", {"2", "4", "8", "16"}, {"2x", "4x", "8x", "16x"}},
        {"mouse-sensitivity", {"0.03", "0.05", "0.07", "0.09", "0.11", "0.14", "0.18", "0.22", "0.3", "0.4", "0.6"}, {}},
        {"reverb-decay", {"0.25", "0.5", "0.75", "1", "1.5", "2", "3", "4"}, {}},
    };
    return kPresets;
}
// A value and a preset are the same: the same text, or the same number ("1.0" = "1"; not "16:9" = "16:10").
bool same_preset(const std::string& value, const char* preset) {
    if (value == preset) return true;
    if (value.empty() || !*preset) return false;
    char* end_a = nullptr;
    char* end_b = nullptr;
    const double a = std::strtod(value.c_str(), &end_a), b = std::strtod(preset, &end_b);
    return !*end_a && !*end_b && a == b;
}

const Presets* find_presets(std::string_view key) {
    for (const Presets& p : presets())
        if (key == p.key) return &p;
    return nullptr;
}

// The step of a number setting (left / right).
double number_step(const SettingDef& def) {
    const std::string_view key = def.key;
    if (key.starts_with("volume-")) return 5;
    if (key == "fov") return 5;
    if (key == "lod-bias") return 0.25;
    if (key == "reverb-clutter") return 0.01;
    if (def.type == SettingType::Int) return 1;
    const double range = def.max - def.min;
    if (range <= 2.5) return 0.05;
    return range >= 10 ? 1 : 0.1;
}

struct Session {
    bool active = false;
    bool in_game = false;
    platform::Settings on_open;   // live_values() when the session began (Back undoes to it)
    platform::Settings file;      // settings.ini when it began
    platform::Settings values;    // what the pages show: settings.ini, the live values over it, the changes
    int32_t game_volume_on_open = 0;
    std::vector<std::string> stack;  // page ids, root first
    std::vector<int32_t> rows;       // the selected row on each page below the top
    int32_t capture_row = -1;        // a key row waiting for its key
    int32_t select = -1;             // the row to select once (page changes)
    fs::path mods_dir;
} g_session;

fs::path g_mods_dir;

constexpr uint32_t kGameVolume = 0x008dce88;  // the Sound menu's volume, 0..50 (0x02 per step = 0..100%)

const PageDef& top_page() { return *find_page(g_session.stack.back()); }
bool on_root() { return g_session.stack.size() == 1; }

std::vector<std::string> installed_mods() {
    std::vector<std::string> mods;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(g_session.mods_dir, error))
        if (entry.is_directory(error) && fs::exists(entry.path() / "mod.ini", error))
            mods.push_back(entry.path().filename().string());
    std::sort(mods.begin(), mods.end());
    if (mods.size() > 7) mods.resize(7);
    return mods;
}

// mod-list: names in load order, '-' turns one off; mods not named come after, on.
bool mod_on(const std::string& list, const std::string& name) {
    size_t at = 0;
    while (at <= list.size()) {
        size_t comma = list.find(',', at);
        if (comma == std::string::npos) comma = list.size();
        std::string word = list.substr(at, comma - at);
        word.erase(0, word.find_first_not_of(' '));
        while (!word.empty() && word.back() == ' ') word.pop_back();
        if (word == name) return true;
        if (word == "-" + name) return false;
        at = comma + 1;
    }
    return true;
}

std::string set_mod(const std::string& list, const std::string& name, bool on) {
    std::string out;
    size_t at = 0;
    bool done = false;
    while (at <= list.size() && !list.empty()) {
        size_t comma = list.find(',', at);
        if (comma == std::string::npos) comma = list.size();
        std::string word = list.substr(at, comma - at);
        word.erase(0, word.find_first_not_of(' '));
        while (!word.empty() && word.back() == ' ') word.pop_back();
        if (word == name || word == "-" + name) {
            word = on ? name : "-" + name;
            done = true;
        }
        if (!word.empty()) out += (out.empty() ? "" : ", ") + word;
        at = comma + 1;
    }
    if (!done) out += (out.empty() ? "" : ", ") + (on ? name : "-" + name);
    return out;
}

struct ShownRow {
    RowDef def;
    std::string mod;  // Row::Mod: the pack's folder name
};

std::vector<ShownRow> page_rows(const PageDef& page) {
    std::vector<ShownRow> rows;
    for (const RowDef& r : page.rows) {
        if (r.kind == Row::Controller && g_session.in_game) continue;  // a front-end screen
        rows.push_back({r, {}});
    }
    if (std::string_view(page.id) == "mods")
        for (const std::string& mod : installed_mods()) rows.push_back({{Row::Mod, nullptr, nullptr}, mod});
    // Reset to Defaults where the page has something to reset (not on pages of links only).
    const bool settings = std::any_of(rows.begin(), rows.end(), [](const ShownRow& r) {
        return r.def.kind != Row::Link && r.def.kind != Row::Controller;
    });
    if (settings) rows.push_back({{Row::Reset, nullptr, "Reset to Defaults"}, {}});
    rows.push_back({{on_root() ? Row::Apply : Row::Back, nullptr, on_root() ? "Apply and Exit" : "Back"}, {}});
    return rows;
}

// Choice names that fit a menu row (the launcher's are longer).
const char* short_choices(std::string_view key) {
    if (key == "button-prompts") return "Automatic|Keyboard|Controller";
    if (key == "speakers") return "Automatic|Stereo|Headphones|Quad|5.1|7.1";
    return nullptr;
}

// The game's menus write rows in title case ("Reset to Defaults"); the schema's labels are sentence case.
std::string title_case(const std::string& text) {
    static constexpr std::string_view kSmall[] = {"a", "and", "or", "to", "of", "the", "in", "on", "by", "for"};
    std::string out = text;
    size_t at = 0;
    bool first = true;
    while (at < out.size()) {
        while (at < out.size() && out[at] == ' ') ++at;
        size_t end = out.find(' ', at);
        if (end == std::string::npos) end = out.size();
        const std::string_view word(out.data() + at, end - at);
        const bool small = !first && std::find(std::begin(kSmall), std::end(kSmall), word) != std::end(kSmall);
        if (!small && at < out.size()) out[at] = static_cast<char>(std::toupper(static_cast<unsigned char>(out[at])));
        first = false;
        at = end;
    }
    return out;
}

// A row's label in the game's language (lang/*.po, keyed by the label as written here); English in title case.
std::string shown_label(int language, const std::string& english) {
    if (english.empty()) return english;
    const char* shown = platform::ui_text(language, english.c_str());
    return shown != english.c_str() ? std::string(shown) : title_case(english);
}

// A value in the game's language; " *" (after a restart) stays at the end.
std::string shown_value(int language, const std::string& english) {
    if (english.empty()) return english;
    const bool restart = english.ends_with(" *");
    const std::string base = restart ? english.substr(0, english.size() - 2) : english;
    return std::string(platform::ui_text(language, base.c_str())) + (restart ? " *" : "");
}

bool is_on_text(const std::string& v) { return v == "on" || v == "true" || v == "1" || v == "enhanced"; }

std::string value_text(const SettingDef& def, const platform::SettingUi* ui, const std::string& value) {
    if (const Presets* p = find_presets(def.key)) {
        for (size_t i = 0; i < p->values.size(); ++i)
            if (same_preset(value, p->values[i]))
                return p->labels.empty() ? p->values[i] : p->labels[i];
        return value.empty() ? "Auto" : value;
    }
    switch (def.type) {
        case SettingType::Flag:
        case SettingType::Switch: return is_on_text(value) ? "on" : "off";
        case SettingType::Choice: {
            if (ui && ui->checkbox_value) return value == ui->checkbox_value ? "on" : "off";
            std::string choices = def.choices, labels = ui && ui->choice_labels ? ui->choice_labels : "";
            if (const char* short_labels = short_choices(def.key)) labels = short_labels;
            size_t at = 0, label_at = 0;
            while (true) {
                const size_t bar = choices.find('|', at), label_bar = labels.find('|', label_at);
                const std::string choice = choices.substr(at, bar - at);
                if (choice == value) {
                    if (!labels.empty()) return labels.substr(label_at, label_bar - label_at);
                    std::string shown = choice;
                    if (!shown.empty()) shown[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(shown[0])));
                    return shown;
                }
                if (bar == std::string::npos) break;
                at = bar + 1;
                label_at = label_bar == std::string::npos ? labels.size() : label_bar + 1;
            }
            return value;
        }
        case SettingType::Int:
        case SettingType::Float: {
            const double v = std::strtod(value.c_str(), nullptr);
            char text[32];
            if (ui && ui->format) {
                if (def.type == SettingType::Int)
                    std::snprintf(text, sizeof text, ui->format, static_cast<int>(std::lround(v)));
                else
                    std::snprintf(text, sizeof text, ui->format, v);
                return text;
            }
            if (std::string_view(def.key).starts_with("volume-")) {
                std::snprintf(text, sizeof text, "%.0f%%", v);
                return text;
            }
            if (std::string_view(def.arg) == "DB") {
                std::snprintf(text, sizeof text, "%+.0f dB", v);
                return text;
            }
            return ui::number_text(v);
        }
        default: return value;
    }
}

// The next value of a setting, one step left (-1) or right (+1).
std::optional<std::string> step_value(const SettingDef& def, const platform::SettingUi* ui, const std::string& value,
                                      int dir) {
    if (const Presets* p = find_presets(def.key)) {
        int index = -1;
        for (size_t i = 0; i < p->values.size(); ++i)
            if (same_preset(value, p->values[i]))
                index = static_cast<int>(i);
        const int n = static_cast<int>(p->values.size());
        index = index < 0 ? (dir > 0 ? 0 : n - 1) : (index + dir + n) % n;
        return std::string(p->values[index]);
    }
    switch (def.type) {
        case SettingType::Flag: return std::string(is_on_text(value) ? "false" : "true");
        case SettingType::Switch: return std::string(is_on_text(value) ? "off" : "on");
        case SettingType::Choice: {
            std::vector<std::string> choices;
            std::string_view all = def.choices;
            while (true) {
                const size_t bar = all.find('|');
                choices.emplace_back(all.substr(0, bar));
                if (bar == std::string_view::npos) break;
                all.remove_prefix(bar + 1);
            }
            if (ui && ui->checkbox_value) {  // two-way: on = checkbox_value
                for (const std::string& c : choices)
                    if (c != value) return c;
                return std::nullopt;
            }
            int index = 0;
            for (size_t i = 0; i < choices.size(); ++i)
                if (choices[i] == value) index = static_cast<int>(i);
            const int n = static_cast<int>(choices.size());
            return choices[(index + dir + n) % n];
        }
        case SettingType::Int:
        case SettingType::Float: {
            const double step = number_step(def);
            double v = value.empty() ? std::strtod(def.default_value, nullptr) : std::strtod(value.c_str(), nullptr);
            v = std::round((v + dir * step) / step) * step;
            v = std::clamp(v, def.min, def.max);
            return def.type == SettingType::Int ? std::to_string(static_cast<int>(std::lround(v))) : ui::number_text(v);
        }
        default: return std::nullopt;
    }
}

// The live setting changed in `values`: run it now (only the live ones; the rest wait for Apply and a restart).
void apply_values(const platform::Settings& before) {
    platform::Settings live_before = live_values(), live_after = live_before;
    for (const SettingDef& def : platform::setting_defs()) {
        if (!(def.flags & platform::kLive)) continue;
        const auto now = g_session.values.get(def.key);
        if (now == before.get(def.key)) continue;
        if (now)
            live_after.set(def.key, *now);
        else
            live_after.erase(def.key);
    }
    apply_live(live_before, live_after);
}

void put(const std::string& key, const std::string& value) {
    const platform::Settings before = g_session.values;
    ui::put_setting(g_session.values, key, value);
    apply_values(before);
}

int32_t game_volume() { return game::title_global<int32_t>(kGameVolume); }
void set_game_volume(int32_t v) {
    game::pc_set_game_volume(std::clamp(v, 0, 50));
}

input::Action action_of(const char* token) { return input::action_from_token(token).value_or(input::Action::Fire); }

input::Bindings session_bindings() {
    input::Bindings bindings;
    input::parse_bindings(platform::combined_setting(g_session.values, "keys").value_or(""), bindings);
    return bindings;
}

void set_session_bindings(const input::Bindings& bindings) {
    const platform::Settings before = g_session.values;
    platform::set_combined_setting(g_session.values, "keys", input::format_bindings(bindings));
    apply_values(before);
}

std::string key_text(const input::Bindings& bindings, input::Action action) {
    std::string out;
    for (input::InputCode code : bindings[action]) {
        if (!code) continue;
        if (!out.empty()) out += ", ";
        out += input::input_name(code);
    }
    return out.empty() ? "none" : out;
}

void reset_page() {
    const platform::Settings before = g_session.values;
    input::Bindings bindings = session_bindings();
    const input::Bindings defaults = input::default_bindings();
    bool keys = false;
    for (const ShownRow& row : page_rows(top_page())) {
        if (row.def.kind == Row::Setting) g_session.values.erase(row.def.key);
        if (row.def.kind == Row::GameVolume) set_game_volume(25);  // the Sound menu's default (50%)
        if (row.def.kind == Row::Mod) g_session.values.erase("mod-list");
        if (row.def.kind == Row::Key) {
            const input::Action a = action_of(row.def.key);
            bindings[a] = defaults[a];
            keys = true;
        }
    }
    apply_values(before);
    if (keys) set_session_bindings(bindings);
}

void end_session(bool save) {
    platform::HostFloatEnv env;
    input::cancel_key_capture();
    if (!save) {
        apply_live(live_values(), g_session.on_open);
        set_game_volume(g_session.game_volume_on_open);
        g_session.active = false;
        LOG_INFO("loader", "settings pages: closed without saving");
        return;
    }
    platform::Settings file = platform::load_settings();
    std::string saved;
    for (const SettingDef& def : platform::setting_defs()) {
        if (def.flags & (platform::kCommandLine | platform::kLauncher)) continue;
        // Live settings: changed in this session (a value from the command line stays unsaved); the others: differ
        // from settings.ini.
        const auto now = g_session.values.get(def.key);
        const auto was = (def.flags & platform::kLive) ? g_session.on_open.get(def.key) : g_session.file.get(def.key);
        if (now == was || now == file.get(def.key)) continue;
        if (now)
            file.set(def.key, *now);
        else
            file.erase(def.key);
        saved += std::string(saved.empty() ? "" : " ") + def.key + "=" + ui::setting_value(g_session.values, def.key);
    }
    g_session.active = false;
    if (saved.empty()) return;
    if (platform::save_settings(file))
        LOG_INFO("loader", "settings pages saved: %s", saved.c_str());
    else
        LOG_WARN("loader", "cannot save the settings to %s", platform::settings_path().string().c_str());
}

void page_begin(int32_t root) {
    platform::HostFloatEnv env;
    static constexpr const char* kRoots[] = {"graphics", "sound", "controls", "settings"};
    if (root < 0 || root > 3) root = 0;
    Session& s = g_session;
    if (s.active && s.capture_row >= 0) input::cancel_key_capture();
    s = Session{};
    s.active = true;
    s.in_game = root == game::kPcRootInGame;
    s.mods_dir = g_mods_dir;
    s.on_open = live_values();
    s.file = platform::load_settings();
    s.values = s.file;
    for (const SettingDef& def : platform::setting_defs()) {
        if (!(def.flags & platform::kLive)) continue;
        if (const auto v = s.on_open.get(def.key))
            s.values.set(def.key, *v);
        else
            s.values.erase(def.key);
    }
    s.game_volume_on_open = game_volume();
    s.stack = {kRoots[root]};
    s.select = 0;
}

void page_view(game::PcPageView* view) {
    platform::HostFloatEnv env;
    Session& s = g_session;
    const int language = view->language;  // the game's (an input), before the view is cleared
    *view = {};
    view->language = language;
    if (!s.active) return;
    // A key capture finished: bind it.
    if (s.capture_row >= 0) {
        uint16_t code = 0;
        const input::Capture state = input::key_capture(code);
        if (state == input::Capture::Done || state == input::Capture::Cancelled) {
            const std::vector<ShownRow> rows = page_rows(top_page());
            if (state == input::Capture::Done && s.capture_row < static_cast<int32_t>(rows.size())) {
                input::Bindings bindings = session_bindings();
                input::bind(bindings, action_of(rows[s.capture_row].def.key), 0, code);
                set_session_bindings(bindings);
            }
            s.capture_row = -1;
        } else if (state == input::Capture::None) {
            s.capture_row = -1;
        }
    }
    const PageDef& page = top_page();
    std::snprintf(view->title, sizeof view->title, "%s", platform::ui_text(language, page.title));
    const std::vector<ShownRow> rows = page_rows(page);
    const input::Bindings bindings = session_bindings();
    const std::string mod_list = ui::setting_value(s.values, "mod-list");
    view->rows = std::min<int32_t>(static_cast<int32_t>(rows.size()), game::kPcPageMaxRows);
    for (int32_t i = 0; i < view->rows; ++i) {
        game::PcPageRow& out = view->row[i];
        const ShownRow& row = rows[i];
        out.enabled = 1;
        std::string label = row.def.label ? row.def.label : "", value;
        switch (row.def.kind) {
            case Row::Setting: {
                const SettingDef* def = platform::find_setting(row.def.key);
                const platform::SettingUi* ui = platform::setting_ui(row.def.key);
                if (label.empty() && ui) label = ui->label;
                if (def) value = value_text(*def, ui, ui::setting_value(s.values, row.def.key));
                if (def && !(def->flags & platform::kLive) && s.values.get(def->key) != s.file.get(def->key))
                    value += " *";  // after a restart
                out.enabled = ui::setting_enabled(s.values, row.def.key);
                out.kind = game::kPcRowValue;
                break;
            }
            case Row::GameVolume: {
                char text[16];
                std::snprintf(text, sizeof text, "%d%%", game_volume() * 2);
                value = text;
                out.kind = game::kPcRowValue;
                break;
            }
            case Row::Key: {
                const input::Action a = action_of(row.def.key);
                label = input::action_label(a);
                value = i == s.capture_row ? "Press a key" : key_text(bindings, a);
                out.kind = game::kPcRowValue;
                break;
            }
            case Row::Mod:
                label = row.mod;
                value = mod_on(mod_list, row.mod) ? "on" : "off";
                if (s.values.get("mod-list") != s.file.get("mod-list")) value += " *";
                out.kind = game::kPcRowValue;
                break;
            case Row::Link:
            case Row::Controller: out.kind = game::kPcRowLink; break;
            default: out.kind = game::kPcRowAction; break;
        }
        std::snprintf(out.label, sizeof out.label, "%s", shown_label(language, label).c_str());
        std::snprintf(out.value, sizeof out.value, "%s", shown_value(language, value).c_str());
    }
    view->selected = std::exchange(s.select, -1);
}

int32_t page_input(int32_t index, int32_t action) {
    platform::HostFloatEnv env;
    Session& s = g_session;
    if (!s.active) return game::kPcResultClose;
    if (s.capture_row >= 0) return game::kPcResultStay;  // waiting for a key
    if (action == game::kPcInputBack) {
        if (on_root()) {
            end_session(false);
            return game::kPcResultClose;
        }
        s.stack.pop_back();
        s.select = s.rows.back();
        s.rows.pop_back();
        return game::kPcResultPage;
    }
    const std::vector<ShownRow> rows = page_rows(top_page());
    if (index < 0 || index >= static_cast<int32_t>(rows.size())) return game::kPcResultStay;
    const ShownRow& row = rows[index];
    const int dir = action == game::kPcInputLeft ? -1 : 1;
    switch (row.def.kind) {
        case Row::Setting: {
            const SettingDef* def = platform::find_setting(row.def.key);
            if (!def || !ui::setting_enabled(s.values, row.def.key)) return game::kPcResultStay;
            if (action == game::kPcInputAccept && def->type != SettingType::Switch && def->type != SettingType::Flag &&
                !(def->type == SettingType::Choice && platform::setting_ui(row.def.key) &&
                  platform::setting_ui(row.def.key)->checkbox_value))
                return game::kPcResultStay;  // accept toggles on/off rows only
            // The next value the schema accepts (a preset the game would refuse is skipped).
            std::string value = ui::setting_value(s.values, row.def.key);
            for (int tries = 0; tries < 16; ++tries) {
                const auto next = step_value(*def, platform::setting_ui(row.def.key), value, dir);
                if (!next || *next == value) break;
                value = *next;
                if (value.empty() || platform::check_setting(*def, value)) {
                    put(row.def.key, value);
                    break;
                }
            }
            return game::kPcResultStay;
        }
        case Row::GameVolume:
            if (action != game::kPcInputAccept) set_game_volume(game_volume() + dir);
            return game::kPcResultStay;
        case Row::Key: {
            const input::Action a = action_of(row.def.key);
            if (action == game::kPcInputAccept || action == game::kPcInputRight) {
                s.capture_row = index;
                input::begin_key_capture();
            } else {  // left: the action's second input goes
                input::Bindings bindings = session_bindings();
                bindings[a][1] = 0;
                set_session_bindings(bindings);
            }
            return game::kPcResultStay;
        }
        case Row::Mod: {
            const std::string list = ui::setting_value(s.values, "mod-list");
            ui::put_setting(s.values, "mod-list", set_mod(list, row.mod, !mod_on(list, row.mod)));
            return game::kPcResultStay;
        }
        case Row::Link:
            if (action != game::kPcInputAccept || !find_page(row.def.key)) return game::kPcResultStay;
            s.rows.push_back(index);
            s.stack.emplace_back(row.def.key);
            s.select = 0;
            return game::kPcResultPage;
        case Row::Controller: return action == game::kPcInputAccept ? game::kPcResultController : game::kPcResultStay;
        case Row::Reset:
            if (action == game::kPcInputAccept) reset_page();
            return game::kPcResultStay;
        case Row::Apply:
            if (action != game::kPcInputAccept) return game::kPcResultStay;
            end_session(true);
            return game::kPcResultClose;
        case Row::Back:
            if (action != game::kPcInputAccept) return game::kPcResultStay;
            return page_input(index, game::kPcInputBack);
    }
    return game::kPcResultStay;
}

}  // namespace

void install_menu_pages(const fs::path& mods_dir, bool quit_row) {
    g_mods_dir = mods_dir;
    game::PcSettingsHooks& hooks = game::pc_settings_hooks();
    hooks.page_begin = &page_begin;
    hooks.page_view = &page_view;
    hooks.page_input = &page_input;
    hooks.page_active = [] { return g_session.active; };
    hooks.movie_playing = [] { input::set_movie_playing(); };
    hooks.text_entry = [] { input::set_text_entry(); };
    hooks.take_typed = [](uint32_t* code) { return input::take_typed(*code); };
    hooks.text = [](int32_t language, const char* english) {
        platform::HostFloatEnv env;
        return platform::ui_text(language, english);
    };
    // Quit Game adds title allocations at boot (a sixth main-menu row): not while recording or replaying, so that
    // recordings made before it and after it replay alike.
    if (quit_row) hooks.quit_game = [] {
        LOG_INFO("loader", "Quit Game (main menu)");
        render::request_quit();
    };
}

}  // namespace loader

namespace loader {

bool write_lang_template(const fs::path& path) {
    std::vector<std::string> texts;
    auto add = [&](const std::string& text) {
        // Numbers ("50%", "16x", "5.1") read the same in every language.
        if (text.find_first_not_of("0123456789.%x") == std::string::npos) return;
        if (std::find(texts.begin(), texts.end(), text) == texts.end()) texts.push_back(text);
    };
    auto add_list = [&](const std::string& list) {  // "A|B|C"
        size_t at = 0;
        while (true) {
            const size_t bar = list.find('|', at);
            add(list.substr(at, bar - at));
            if (bar == std::string::npos) break;
            at = bar + 1;
        }
    };
    for (const char* text : {game::kPcTextGraphics, game::kPcTextControls, game::kPcTextSettings,
                             game::kPcTextEnhancedGraphics, game::kPcTextQuitGame, game::kPcTextPressStart})
        add(text);
    for (const PageDef& page : pages()) {
        add(page.title);
        for (const RowDef& row : page.rows) {
            if (row.label) add(row.label);
            if (row.kind == Row::Key) add(input::action_label(action_of(row.key)));
            if (row.kind != Row::Setting) continue;
            const SettingDef* def = platform::find_setting(row.key);
            const platform::SettingUi* ui = platform::setting_ui(row.key);
            if (!row.label && ui) add(ui->label);
            if (!def) continue;
            if (const Presets* p = find_presets(def->key)) {
                for (const char* label : p->labels) add(label);
                add("Auto");
                continue;
            }
            if (def->type == SettingType::Switch || def->type == SettingType::Flag || (ui && ui->checkbox_value)) {
                add("on");
                add("off");
            } else if (def->type == SettingType::Choice) {
                if (const char* labels = short_choices(def->key)) {
                    add_list(labels);
                } else if (ui && ui->choice_labels) {
                    add_list(ui->choice_labels);
                } else {
                    std::string choices = def->choices;
                    size_t at = 0;
                    while (true) {
                        const size_t bar = choices.find('|', at);
                        std::string shown = choices.substr(at, bar - at);
                        if (!shown.empty())
                            shown[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(shown[0])));
                        add(shown);
                        if (bar == std::string::npos) break;
                        at = bar + 1;
                    }
                }
            }
        }
    }
    for (const char* text : {"Master Volume", "Reset to Defaults", "Apply and Exit", "Back", "Press a key", "none",
                             "on", "off"})
        add(text);
    FILE* out = std::fopen(path.string().c_str(), "wb");
    if (!out) {
        std::fprintf(stderr, "cannot write %s\n", path.string().c_str());
        return false;
    }
    std::fprintf(out,
                 "# Our own texts in the game's menus (the PC settings pages and our menu rows), in English.\n"
                 "# Written by cod_bro --lang-template lang/template.pot; translations: lang/README.md.\n"
                 "msgid \"\"\nmsgstr \"\"\n\"Content-Type: text/plain; charset=UTF-8\\n\"\n");
    for (const std::string& text : texts) {
        std::string escaped;
        for (char c : text) {
            if (c == '"' || c == '\\') escaped += '\\';
            escaped += c;
        }
        std::fprintf(out, "\nmsgid \"%s\"\nmsgstr \"\"\n", escaped.c_str());
    }
    const bool ok = std::fclose(out) == 0;
    std::printf("%zu texts written to %s\n", texts.size(), path.string().c_str());
    return ok;
}

}  // namespace loader
