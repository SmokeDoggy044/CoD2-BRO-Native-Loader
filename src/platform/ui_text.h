#pragma once

// Our own texts (the PC settings pages, our menu rows) in the game's language: lang/<code>.po next to the program
// (a release) or in the source tree (builds), one file per language in the gettext format, keyed by the English
// text. A text without a translation, or a language without a file, stays English. lang/template.pot lists every
// text (tools/lang_template.py writes it); lang/README.md says how to add or correct a language.

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace platform {

// The game's language index (g_languageIndex: 0 English, 1 German, 2 French, 3 Spanish, 4 Italian) as a file code
// ("en", "de", "fr", "es", "it"); "en" for any other.
const char* ui_language_code(int index);

// The translations of one language: English text -> translated text (UTF-8). False with `error` (line number and
// what is wrong) when the file is not a .po file we read; entries marked fuzzy and empty translations are left out.
bool parse_po(std::string_view text, std::map<std::string, std::string>& out, std::string* error = nullptr);

// `english` in language `index` as the game's text functions take it (Windows-1252, the game's and the sharp
// fonts' character set); `english` itself when there is no translation. The pointer stays valid for the program's
// life. The language's file is read on first use. Thread-safe.
const char* ui_text(int index, const char* english);
// The same, UTF-8 (the launcher, logs). nullopt when there is no translation.
std::optional<std::string> ui_text_utf8(int index, std::string_view english);

// UTF-8 to Windows-1252; characters it has no code for become '?'.
std::string utf8_to_cp1252(std::string_view text);

// The text language (the text-language setting): our texts in `code` ("pl") whatever the game's language, and the
// game's character set with Polish letters on their Windows-1250 codes (the fonts get them: text::
// set_sharp_text_polish). Empty: the game's language and Windows-1252. Set before any text is asked for.
void set_text_language(const std::string& code);
bool polish_text();
// Where the language files are (lang/ next to the program, else the source tree's).
std::string ui_text_dir();
// UTF-8 to the game's character set as it stands (utf8_to_cp1252, plus the Polish letters with Polish text).
std::string utf8_to_game_text(std::string_view text);

// Where the language files are (tests): `dir`/<code>.po. Default: lang/ next to the program, else the source tree's.
void set_ui_text_dir(const std::string& dir);

}  // namespace platform
