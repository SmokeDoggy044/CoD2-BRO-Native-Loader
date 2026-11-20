// lang/*.po (src/platform/ui_text.h): each file reads, translates only texts of lang/template.pot, fits the menu
// rows (PcPageRow: 43-character label, 35-character value) and is Windows-1252 text; the parser and the conversion.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "ui_text.h"

namespace fs = std::filesystem;

namespace {

int g_failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::printf("FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

}  // namespace

int main() {
    // The parser: escapes, continuation lines, fuzzy and empty entries left out, errors.
    std::map<std::string, std::string> parsed;
    check(platform::parse_po("msgid \"\"\nmsgstr \"\"\n\"Language: es\\n\"\n\nmsgid \"A \\\"b\\\"\"\nmsgstr \"C\"\n"
                             "\"D\"\n\n#, fuzzy\nmsgid \"E\"\nmsgstr \"F\"\n\nmsgid \"G\"\nmsgstr \"\"\n",
                             parsed),
          "parse a small file");
    check(parsed.size() == 1 && parsed["A \"b\""] == "CD", "translations read");
    std::string error;
    check(!platform::parse_po("msgid \"A\"\nmsgid \"B\"\nmsgstr \"C\"\n", parsed, &error) && error.starts_with("line 2"),
          "msgid without msgstr refused: " + error);
    check(!platform::parse_po("msgstr \"C\"\n", parsed, &error), "msgstr without msgid refused");
    check(platform::utf8_to_cp1252("Créer à ñ €") == "Cr\xe9" "er \xe0 \xf1 \x80", "UTF-8 to Windows-1252");
    check(platform::utf8_to_cp1252("\xe4\xb8\xad") == "?", "a character Windows-1252 lacks");

    const fs::path dir = COD_BRO_LANG_DIR;
    std::map<std::string, std::string> template_texts;
    check(platform::parse_po(read(dir / "template.pot"), template_texts), "template.pot reads");
    // The template's msgstr are empty: its texts are the ids.
    std::map<std::string, bool> known;
    {
        const std::string text = read(dir / "template.pot");
        size_t at = 0;
        while ((at = text.find("\nmsgid \"", at)) != std::string::npos) {
            at += 8;
            const size_t end = text.find("\"\n", at);
            if (end != at) known[text.substr(at, end - at)] = true;
        }
    }
    check(known.size() > 100, "template.pot lists the texts");
    platform::set_ui_text_dir(dir.string());
    for (const char* code : {"de", "fr", "es", "it"}) {
        std::map<std::string, std::string> texts;
        const std::string file = std::string(code) + ".po";
        check(platform::parse_po(read(dir / file), texts, &error), file + " reads: " + error);
        for (const auto& [english, translated] : texts) {
            check(known.count(english) != 0, file + ": \"" + english + "\" is not in template.pot");
            const std::string shown = platform::utf8_to_cp1252(translated);
            check(shown.find('?') == std::string::npos || translated.find('?') != std::string::npos,
                  file + ": \"" + translated + "\" has characters Windows-1252 lacks");
            // Menu rows: 35 characters at most. Longer English texts are not rows (the one-time notice).
            check(english.size() > 35 || shown.size() <= 35, file + ": \"" + translated + "\" is longer than 35 characters");
        }
        for (const auto& [english, unused] : known)
            if (!texts.count(english)) std::printf("note: %s has no translation of \"%s\"\n", file.c_str(), english.c_str());
    }
    // A lookup in the game's language index (3 = Spanish), and English for a text without one.
    check(std::string(platform::ui_text(3, "Back")) != "Back", "Spanish Back translated");
    check(std::string(platform::ui_text(0, "Back")) == "Back", "English stays");
    const char* unknown = "No such text";
    check(platform::ui_text(2, unknown) == unknown, "an unknown text is handed back as it is");
    // Text languages: our texts in Polish, Russian and Ukrainian, on the game's codes for their letters.
    for (const char* code : {"pl", "ru", "uk"}) {
        std::map<std::string, std::string> texts;
        const std::string file = std::string(code) + ".po";
        check(platform::parse_po(read(dir / file), texts, &error), file + " reads: " + error);
        platform::set_text_language(code);
        for (const auto& [english, translated] : texts) {
            check(known.count(english) != 0, file + ": \"" + english + "\" is not in template.pot");
            const std::string shown = platform::utf8_to_game_text(translated);
            check(shown.find('?') == std::string::npos || translated.find('?') != std::string::npos,
                  file + ": \"" + translated + "\" has characters the game's text cannot show");
        }
        // A lookup while a text language is set (ui_text holds its lock while it converts: this hung once).
        check(std::string(platform::ui_text(0, "Back")) != "Back", file + ": Back translated");
    }
    platform::set_text_language("uk");
    check(platform::text_script() == platform::TextScript::Cyrillic, "Ukrainian is Cyrillic");
    check(platform::utf8_to_game_text("\u0407\u0436\u0430\u043a \u0491 \u2116 \u00ab\u2026\u00bb") ==
              "\xAF\xE6\xE0\xEA \xB4 \xB9 \xAB\x85\xBB",
          "Ukrainian letters, numero sign and shared punctuation on their Windows-1251 codes");
    platform::set_text_language("pl");
    check(platform::utf8_to_game_text("\u0141\u00f3d\u017a") == "\xA3\xF3" "d\x9F", "Polish on Windows-1250 codes");
    platform::set_text_language("");
    if (g_failures) return 1;
    std::printf("ui_text_test: all passed\n");
    return 0;
}
