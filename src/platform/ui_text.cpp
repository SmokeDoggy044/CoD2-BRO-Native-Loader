#include "ui_text.h"

#include <algorithm>

#include <array>
#include <atomic>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <unordered_map>

#include "log.h"
#include "paths.h"

namespace platform {

namespace fs = std::filesystem;

namespace {

// One language: its translations (UTF-8) and the Windows-1252 copies handed out (node-based: pointers stay valid).
struct Language {
    std::map<std::string, std::string> utf8;
    std::map<std::string, std::string> cp1252;
};

std::mutex g_mutex;
std::string g_dir;
std::string g_text_language;  // set_text_language
std::unordered_map<std::string, std::unique_ptr<Language>> g_languages;

fs::path text_dir() {
    if (!g_dir.empty()) return g_dir;
    const fs::path next_to_program = executable_dir() / "lang";
    std::error_code error;
    if (fs::is_directory(next_to_program, error)) return next_to_program;
#ifdef COD_BRO_LANG_DIR
    return COD_BRO_LANG_DIR;
#else
    return next_to_program;
#endif
}

// The language's translations, read on first use (an empty set when there is no file). Under g_mutex.
Language* language(const char* code) {
    auto& slot = g_languages[code];
    if (slot) return slot.get();
    slot = std::make_unique<Language>();
    if (std::string_view(code) == "en") return slot.get();
    const fs::path path = text_dir() / (std::string(code) + ".po");
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        LOG_INFO("text", "no %s: our own texts stay English", path.string().c_str());
        return slot.get();
    }
    std::ostringstream text;
    text << in.rdbuf();
    std::string error;
    if (!parse_po(text.str(), slot->utf8, &error)) {
        LOG_WARN("text", "%s: %s; our own texts stay English", path.string().c_str(), error.c_str());
        slot->utf8.clear();
    } else {
        LOG_INFO("text", "%zu of our own texts in %s from %s", slot->utf8.size(), code, path.string().c_str());
    }
    return slot.get();
}

// A quoted .po string's contents after `at` (the opening quote), escapes resolved; false when malformed.
bool quoted(std::string_view line, std::string& out) {
    const size_t open = line.find('"');
    if (open == std::string_view::npos || line.size() < open + 2 || line.back() != '"') return false;
    for (size_t i = open + 1; i + 1 < line.size(); ++i) {
        char c = line[i];
        if (c == '"') return false;
        if (c == '\\') {
            if (++i + 1 >= line.size()) return false;
            switch (line[i]) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                default: return false;
            }
        }
        out += c;
    }
    return true;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

// Windows-1252's codes 0x80-0x9f (0: none).
constexpr char16_t k1252[32] = {0x20ac, 0,      0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
                                0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017d, 0,
                                0,      0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
                                0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0,      0x017e, 0x0178};

}  // namespace

const char* ui_language_code(int index) {
    static constexpr const char* kCodes[] = {"en", "de", "fr", "es", "it"};
    return index >= 0 && index < 5 ? kCodes[index] : "en";
}

bool parse_po(std::string_view text, std::map<std::string, std::string>& out, std::string* error) {
    enum class Part { None, Id, Str };
    Part part = Part::None;
    std::string id, str;
    bool fuzzy = false, have_entry = false;
    int line_number = 0;
    auto fail = [&](const char* what) {
        if (error) *error = "line " + std::to_string(line_number) + ": " + what;
        return false;
    };
    auto finish = [&] {
        if (have_entry && !fuzzy && !id.empty() && !str.empty()) out[id] = str;
        id.clear();
        str.clear();
        fuzzy = have_entry = false;
        part = Part::None;
    };
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);  // a UTF-8 byte order mark
    while (!text.empty()) {
        const size_t end = text.find('\n');
        const std::string_view line = trim(text.substr(0, end));
        text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
        ++line_number;
        if (line.empty()) {
            finish();
            continue;
        }
        if (line[0] == '#') {
            if (part == Part::Str) finish();
            if (line.starts_with("#,") && line.find("fuzzy") != std::string_view::npos) fuzzy = true;
            continue;
        }
        if (line.starts_with("msgid ")) {
            if (part == Part::Str) {
                const bool was_fuzzy = fuzzy;
                finish();
                fuzzy = was_fuzzy;
            } else if (part == Part::Id) {
                return fail("msgid without msgstr");
            }
            part = Part::Id;
            have_entry = true;
            if (!quoted(line, id)) return fail("expected msgid \"...\"");
        } else if (line.starts_with("msgstr ")) {
            if (part != Part::Id) return fail("msgstr without msgid");
            part = Part::Str;
            if (!quoted(line, str)) return fail("expected msgstr \"...\"");
        } else if (line[0] == '"') {
            std::string more;
            if (part == Part::None || !quoted(line, more)) return fail("a stray line");
            (part == Part::Id ? id : str) += more;
        } else if (line.starts_with("msgctxt") || line.starts_with("msgid_plural") || line.starts_with("msgstr[")) {
            return fail("contexts and plural forms are not used here");
        } else {
            return fail("not a .po line");
        }
    }
    if (part == Part::Id) return fail("msgid without msgstr");
    finish();
    return true;
}

std::string utf8_to_cp1252(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        uint32_t code = '?';
        size_t length = 1;
        if (c < 0x80) {
            code = c;
        } else if ((c & 0xe0) == 0xc0 && i + 1 < text.size()) {
            code = ((c & 0x1fu) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3fu);
            length = 2;
        } else if ((c & 0xf0) == 0xe0 && i + 2 < text.size()) {
            code = ((c & 0x0fu) << 12) | ((static_cast<unsigned char>(text[i + 1]) & 0x3fu) << 6) |
                   (static_cast<unsigned char>(text[i + 2]) & 0x3fu);
            length = 3;
        } else if ((c & 0xf8) == 0xf0) {
            length = 4;
        }
        i += length;
        if (code < 0x80 || (code >= 0xa0 && code <= 0xff)) {
            out += static_cast<char>(code);
            continue;
        }
        char mapped = '?';
        for (int k = 0; k < 32; ++k)
            if (k1252[k] && k1252[k] == code) mapped = static_cast<char>(0x80 + k);
        out += mapped;
    }
    return out;
}

namespace {
// The text language's script, readable without g_mutex: utf8_to_game_text runs inside ui_text, which holds it.
std::atomic<TextScript> g_text_script{TextScript::Latin};
}  // namespace

void set_text_language(const std::string& code) {
    std::lock_guard lock(g_mutex);
    g_text_language = code;
    g_text_script = code == "pl"                   ? TextScript::Polish
                    : code == "ru" || code == "uk" ? TextScript::Cyrillic
                                                   : TextScript::Latin;
}

std::string text_language() {
    std::lock_guard lock(g_mutex);
    return g_text_language;
}

TextScript text_script() { return g_text_script.load(); }

namespace {

constexpr ScriptLetter kPolishLetters[] = {
    {0xB9, 0x0105, 'a'}, {0xE6, 0x0107, 'c'}, {0xEA, 0x0119, 'e'}, {0xB3, 0x0142, 'l'},
    {0xF1, 0x0144, 'n'}, {0x9C, 0x015B, 's'}, {0x9F, 0x017A, 'z'}, {0xBF, 0x017C, 'z'},
    {0xA5, 0x0104, 'A'}, {0xC6, 0x0106, 'C'}, {0xCA, 0x0118, 'E'}, {0xA3, 0x0141, 'L'},
    {0xD1, 0x0143, 'N'}, {0x8C, 0x015A, 'S'}, {0x8F, 0x0179, 'Z'}, {0xAF, 0x017B, 'Z'},
};

// Windows-1251: А-я on 0xC0-0xFF, Ё ё, the Ukrainian Є є І і Ї ї Ґ ґ and №. Each is measured against its Latin
// look-alike where there is one, else H (capitals) or n (small letters).
std::array<ScriptLetter, 64 + 11> make_cyrillic_letters() {
    // In alphabet order: А Б В Г Д Е Ж З И Й К Л М Н О П Р С Т У Ф Х Ц Ч Ш Щ Ъ Ы Ь Э Ю Я.
    constexpr const char* kUpper = "AHBHHEHHHHKHMHOHPCTYHXHHHHHHHHHH";
    constexpr const char* kLower = "annnnennnnnnnnonpcnynxnnnnnnnnnn";
    static_assert(std::char_traits<char>::length(kUpper) == 32 && std::char_traits<char>::length(kLower) == 32);
    std::array<ScriptLetter, 64 + 11> letters{};
    for (int i = 0; i < 32; ++i) {
        letters[i] = {0xC0 + i, 0x0410 + i, kUpper[i]};
        letters[32 + i] = {0xE0 + i, 0x0430 + i, kLower[i]};
    }
    constexpr ScriptLetter kMore[] = {{0xA8, 0x0401, 'E'}, {0xB8, 0x0451, 'e'}, {0xAA, 0x0404, 'E'}, {0xBA, 0x0454, 'e'},
                                      {0xB2, 0x0406, 'I'}, {0xB3, 0x0456, 'i'}, {0xAF, 0x0407, 'I'}, {0xBF, 0x0457, 'i'},
                                      {0xA5, 0x0490, 'H'}, {0xB4, 0x0491, 'n'}, {0xB9, 0x2116, 'N'}};
    for (size_t i = 0; i < std::size(kMore); ++i) letters[64 + i] = kMore[i];
    return letters;
}
const auto kCyrillicLetters = make_cyrillic_letters();

}  // namespace

std::span<const ScriptLetter> script_letters(TextScript script) {
    switch (script) {
        case TextScript::Polish: return kPolishLetters;
        case TextScript::Cyrillic: return kCyrillicLetters;
        default: return {};
    }
}

std::string ui_text_dir() {
    std::lock_guard lock(g_mutex);
    return text_dir().string();
}

std::string utf8_to_game_text(std::string_view text) {
    const std::span<const ScriptLetter> letters = script_letters(text_script());
    if (letters.empty()) return utf8_to_cp1252(text);
    // The script's letters on their codes; everything else as Windows-1252 (the punctuation the scripts share).
    std::string out;
    for (size_t i = 0; i < text.size();) {
        size_t length = 1;
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if ((c & 0xe0) == 0xc0) length = 2;
        else if ((c & 0xf0) == 0xe0) length = 3;
        else if ((c & 0xf8) == 0xf0) length = 4;
        length = std::min(length, text.size() - i);
        uint32_t cp = length == 1 ? c : c & (0xff >> (length + 1));
        for (size_t k = 1; k < length; ++k) cp = cp << 6 | (static_cast<unsigned char>(text[i + k]) & 0x3f);
        const ScriptLetter* letter = nullptr;
        if (cp >= 0x80)
            for (const ScriptLetter& l : letters)
                if (static_cast<uint32_t>(l.unicode) == cp) letter = &l;
        if (letter)
            out += static_cast<char>(letter->code);
        else
            out += utf8_to_cp1252(text.substr(i, length));
        i += length;
    }
    return out;
}

const char* ui_text(int index, const char* english) {
    std::lock_guard lock(g_mutex);
    Language* lang = language(g_text_language.empty() ? ui_language_code(index) : g_text_language.c_str());
    const auto found = lang->utf8.find(english);
    if (found == lang->utf8.end()) return english;
    auto [it, added] = lang->cp1252.try_emplace(found->first);
    if (added) it->second = utf8_to_game_text(found->second);
    return it->second.c_str();
}

std::optional<std::string> ui_text_utf8(int index, std::string_view english) {
    std::lock_guard lock(g_mutex);
    Language* lang = language(g_text_language.empty() ? ui_language_code(index) : g_text_language.c_str());
    const auto found = lang->utf8.find(std::string(english));
    if (found == lang->utf8.end()) return std::nullopt;
    return found->second;
}

void set_ui_text_dir(const std::string& dir) {
    std::lock_guard lock(g_mutex);
    g_dir = dir;
    g_languages.clear();
}

}  // namespace platform
