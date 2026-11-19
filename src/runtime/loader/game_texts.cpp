#include "game_texts.h"

#include <algorithm>
#include <cstdio>
#include <string_view>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "bindings.h"
#include "loc_overrides.h"
#include "log.h"
#include "paths.h"
#include "title_data.h"
#include "ui_text.h"

namespace fs = std::filesystem;

namespace loader {

namespace {

// --- Windows-1252 <-> UTF-8 (the game's texts are Windows-1252; our files UTF-8) ---

constexpr char32_t kCp1252High[32] = {0x20ac, 0x81,   0x201a, 0x192,  0x201e, 0x2026, 0x2020, 0x2021,
                                      0x2c6,  0x2030, 0x160,  0x2039, 0x152,  0x8d,   0x17d,  0x8f,
                                      0x90,   0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
                                      0x2dc,  0x2122, 0x161,  0x203a, 0x153,  0x9d,   0x17e,  0x178};

std::string cp1252_to_utf8(const char* text) {
    std::string out;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p; ++p) {
        const char32_t c = *p >= 0x80 && *p < 0xa0 ? kCp1252High[*p - 0x80] : *p;
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xc0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3f));
        } else {
            out += static_cast<char>(0xe0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (c & 0x3f));
        }
    }
    return out;
}

// A .po string's body: quotes, backslashes, newlines and other control characters escaped.
std::string po_quote(const std::string& text) {
    std::string out = "\"";
    for (const char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c == '"' || c == '\\') {
            out += '\\';
            out += ch;
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c < 0x20) {
            char octal[8];
            std::snprintf(octal, sizeof(octal), "\\%03o", c);
            out += octal;
        } else {
            out += ch;
        }
    }
    return out + '"';
}

// The parts of a .po string line ("..."), escapes undone; nullopt if it is not one.
std::optional<std::string> po_unquote(std::string_view line) {
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.remove_prefix(1);
    while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) line.remove_suffix(1);
    if (line.size() < 2 || line.front() != '"' || line.back() != '"') return std::nullopt;
    line = line.substr(1, line.size() - 2);
    std::string out;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] != '\\' || i + 1 >= line.size()) {
            out += line[i];
            continue;
        }
        const char e = line[++i];
        if (e == 'n') out += '\n';
        else if (e == 't') out += '\t';
        else if (e >= '0' && e <= '7') {
            int value = 0, digits = 0;
            while (digits < 3 && i < line.size() && line[i] >= '0' && line[i] <= '7') value = value * 8 + (line[i++] - '0'), ++digits;
            --i;
            out += static_cast<char>(value);
        } else out += e;
    }
    return out;
}

// --- The template (--game-text-template) ---

std::mutex g_template_mutex;
std::ofstream g_template;
std::set<uint32_t> g_template_seen;

void template_text(uint32_t hash, const char* text) {
    std::lock_guard lock(g_template_mutex);
    if (!g_template.is_open() || !g_template_seen.insert(hash).second) return;
    char context[16];
    std::snprintf(context, sizeof(context), "0x%08x", hash);
    g_template << "msgctxt \"" << context << "\"\nmsgid " << po_quote(cp1252_to_utf8(text)) << "\nmsgstr \"\"\n\n";
    g_template.flush();
}

// --- Corrections: <user data>/text_overrides.po (msgctxt = key hash, msgstr = the text) ---

std::map<uint32_t, std::string> read_corrections(const fs::path& path) {
    std::map<uint32_t, std::string> out;
    std::ifstream file(path, std::ios::binary);
    if (!file) return out;
    std::optional<uint32_t> hash;
    std::string text, *reading = nullptr;
    std::string ignored;
    bool fuzzy = false;
    auto finish = [&] {
        if (hash && !text.empty() && !fuzzy) out[*hash] = platform::utf8_to_cp1252(text);
        hash.reset(), text.clear(), fuzzy = false, reading = nullptr;
    };
    for (std::string line; std::getline(file, line);) {
        if (line.rfind("#,", 0) == 0 && line.find("fuzzy") != std::string::npos) fuzzy = true;
        if (line.rfind("msgctxt", 0) == 0) {
            if (hash) finish();
            const auto value = po_unquote(std::string_view(line).substr(7));
            if (value) hash = static_cast<uint32_t>(std::strtoul(value->c_str(), nullptr, 0));
            reading = nullptr;
        } else if (line.rfind("msgid", 0) == 0) {
            reading = &ignored;
            ignored.clear();
        } else if (line.rfind("msgstr", 0) == 0) {
            reading = &text;
            if (const auto value = po_unquote(std::string_view(line).substr(6))) text = *value;
        } else if (reading) {
            if (const auto value = po_unquote(line)) *reading += *value;
        }
    }
    finish();
    return out;
}

std::map<uint32_t, std::string> g_corrections;

// --- PC wording ---

// A text that names the Xbox controller in words, by key hash (Loc_HashKey of the key), and what it says with a
// keyboard: English, translated by lang/*.po; {action} is that action's first bound input in brackets (bindings.h
// tokens), {move} the four move keys. The stick clicks mean what the default layout gives them (L3 sprint, R3 aim
// toggle). [AIM] is the game's own token (its prompts show the key). The PlayStation 2 texts ("analog stick", "Press
// START button") never show on the Xbox build and stay.
struct PcWording {
    uint32_t hash;
    const char* english;
};
constexpr PcWording kPcWording[] = {
    {0x9bf60578, "Use {move}"},      // INGAME_XBOX_USE_LEFT_STICK "Move the left thumbstick" ([USE_LEFT_STICK])
    {0xe6c78792, "Move the mouse"},  // INGAME_XBOX_USE_RIGHT_STICK "Move the right thumbstick" ([USE_RIGHT_STICK])
    {0xb1c442af, "{sprint}"},        // INGAME_L3_XBOX "the left thumbstick" (~l3)
    {0x31c442a8, "{aim-toggle}"},    // INGAME_R3_XBOX "the right thumbstick" (~r3)
    {0x04e39bda, "{sprint}"},        // INGAME_L3 "L3 Button"
    {0x04e3981a, "{aim-toggle}"},    // INGAME_R3 "R3 Button"
    {0x5aee56eb, "Press and hold {sprint}"},  // INGAME_LEFT_THUMB "Click and hold the left thumbstick"
    {0xca7dbd03, "Press {aim-toggle}"},       // INGAME_RIGHT_THUMB "Click and hold the right thumbstick"
    // INGAME_XBOX_LMG_MOUNTPOINT_CLICK_L / _CLICK_R / _PULL ("Click and hold the left thumbstick to setup your
    // weapon", ...): as the game's own INGAME_PS2_LMG_MOUNTPOINT.
    {0xd50e4fc0, "Press and hold [AIM] to setup your weapon"},
    {0xd50e4fde, "Press and hold [AIM] to setup your weapon"},
    {0xd0904eb0, "Press and hold [AIM] to setup your weapon"},
};

std::string with_keys(std::string text, const input::Bindings& bindings) {
    for (size_t at = text.find('{'); at != std::string::npos; at = text.find('{', at)) {
        const size_t end = text.find('}', at);
        if (end == std::string::npos) break;
        const std::string token = text.substr(at + 1, end - at - 1);
        std::string keys;
        if (token == "move") {
            for (input::Action a : {input::Action::MoveForward, input::Action::MoveLeft, input::Action::MoveBack,
                                    input::Action::MoveRight})
                keys += (keys.empty() ? "" : " ") + input::input_name(bindings[a][0]);
            keys = "[" + keys + "]";
        } else if (const auto action = input::action_from_token(token)) {
            const std::string name = input::input_name(bindings[*action][0]);
            keys = "[" + (name.empty() ? std::string("-") : name) + "]";
        } else {
            keys = token;
        }
        text.replace(at, end - at + 1, keys);
        at += keys.size();
    }
    return text;
}

std::mutex g_mutex;
std::optional<bool> g_keyboard;
input::Bindings g_bindings;
int32_t g_language = -1;

void apply(bool keyboard) {
    std::vector<std::pair<uint32_t, std::string>> overrides;
    const int32_t language = game::title_global<int32_t>(0x0095f5dc);  // g_languageIndex
    if (keyboard) {
        for (const PcWording& w : kPcWording) {
            if (!w.english || g_corrections.count(w.hash)) continue;
            overrides.emplace_back(w.hash, with_keys(platform::ui_text(language, w.english), g_bindings));
        }
    }
    for (const auto& [hash, text] : g_corrections) overrides.emplace_back(hash, text);
    game::loc_set_overrides(std::move(overrides));
}

}  // namespace

std::vector<const char*> game_text_texts() {
    std::vector<const char*> texts;
    for (const PcWording& w : kPcWording)
        if (std::string_view(w.english).find_first_not_of("{}-abcdefghijklmnopqrstuvwxyz") != std::string_view::npos &&
            std::find(texts.begin(), texts.end(), std::string_view(w.english)) == texts.end())
            texts.push_back(w.english);
    return texts;
}

void start_game_texts(const fs::path& template_path) {
    if (!template_path.empty()) {
        std::lock_guard lock(g_template_mutex);
        g_template.open(template_path, std::ios::binary | std::ios::trunc);
        if (g_template) {
            g_template << "# The game's own texts (this disc's language) by key hash, as the string tables loaded them.\n"
                          "# Copy entries to text_overrides.po in the data folder and fill in msgstr to replace them.\n"
                          "msgid \"\"\nmsgstr \"\"\n\"Content-Type: text/plain; charset=UTF-8\\n\"\n\n";
            game::loc_set_text_seen(&template_text);
            LOG_INFO("text", "game text template: %s", template_path.string().c_str());
        } else {
            LOG_WARN("text", "cannot write the game text template %s", template_path.string().c_str());
        }
    }
    const fs::path corrections = platform::user_data_dir() / "text_overrides.po";
    g_corrections = read_corrections(corrections);
    if (!g_corrections.empty())
        LOG_INFO("text", "%zu text corrections from %s", g_corrections.size(), corrections.string().c_str());
    std::lock_guard lock(g_mutex);
    apply(false);
}

void update_game_texts(bool keyboard) {
    const input::Bindings bindings = input::bindings();
    const int32_t language = game::title_global<int32_t>(0x0095f5dc);
    std::lock_guard lock(g_mutex);
    if (g_keyboard == keyboard && (!keyboard || (bindings == g_bindings && language == g_language))) return;
    g_keyboard = keyboard;
    g_bindings = bindings;
    g_language = language;
    apply(keyboard);
}

}  // namespace loader
