#pragma once

// Our overrides of the game's own texts (menus, HUD hints, subtitles: the localized string tables, rm_locStrings
// 0x0095f5d8, looked up by Loc_HashKey of their key). An override replaces the text of one key hash in whatever
// language the disc has: the table entry's text pointer (+8) is pointed at ours while it applies, so every reader
// of the tables (Loc_GetString, the menus, subtitles, script strings) sees it. The originals come back when an
// override is dropped. Set by the host (PC wording for the keyboard, text files); the game's tables stay untouched.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace game {

// Replaces the whole set (texts in the game's character set, Windows-1252): entries of the loaded tables follow at
// once, tables loaded later as they load. Any thread.
void loc_set_overrides(std::vector<std::pair<uint32_t, std::string>> overrides);

// StringTable_Load: a table was just read and relocated - its entries get the overrides that apply.
void loc_table_loaded(uint8_t* table);

// Every text a loaded table has (key hash, original text), for the text template. Set before the title runs; null
// stops it. Called on the title's thread as tables load.
using LocTextSeen = void (*)(uint32_t hash, const char* text);
void loc_set_text_seen(LocTextSeen seen);

}  // namespace game
