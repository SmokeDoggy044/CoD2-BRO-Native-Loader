#pragma once

// Sharper game text (PC option, --sharp-text): the game's text comes from small
// pre-rendered bitmap fonts (an atlas texture plus a .fdf glyph table each, in the
// .cod packs). This draws each atlas again at the render resolution from free
// look-alike fonts (src/text/fonts/ next to the executable, all SIL OFL), glyph by glyph into
// the boxes the game's table gives, with the drop shadow the originals have. The
// renderer shows the new atlas in place of the original (render::BackendConfig::
// replace_texture); the game's glyph table and code stay as they are.

#include <cstdint>
#include <filesystem>
#include <memory>

#include "backend.h"

#include "ui_text.h"

namespace text {

// Reads the game's font tables and atlases from the packs under `assets` and the
// replacement fonts from `fonts_dir`. Returns how many of the game's fonts can be
// replaced (logged).
size_t prepare_sharp_text(const std::filesystem::path& assets, const std::filesystem::path& fonts_dir);

// For render::BackendConfig::replace_texture: the redrawn atlas if `content` is one of
// the prepared fonts' (made on first use for each scale), else null.
std::shared_ptr<const render::TextureReplacement> sharp_text_replacement(uint64_t content, float render_scale);

// The text language's letters (the text-language setting; call before prepare_sharp_text). The game's fonts have
// Windows-1252 letters only; Polish text comes in Windows-1250 and Russian/Ukrainian in Windows-1251, whose letters
// sit on codes the fonts give other letters (platform::script_letters). Each font's table is rewritten for them: a
// letter gets metrics measured against its base letter (Polish: a, c, e, l, ...; Cyrillic: a Latin look-alike, else H
// or n) as the stand-in font shapes both, and an atlas box big enough - its own code's, one of a letter the script
// does not use, free atlas space, or a shrunk fit. A letter the stand-in lacks comes from the font's second stand-in
// (impact: Oswald, GemFontOne: Montserrat). The atlas is then drawn with the letters there (sharp text must be on).
void set_sharp_text_script(platform::TextScript script);
// The rewritten table of the game's font `name` (as the title loads it, e.g. "garamond" or "garamond.fdf") for the
// title to read instead of its own; null when it keeps its own. A fresh copy each call (the title parses in place).
char* script_font_table(const char* name);

}  // namespace text
