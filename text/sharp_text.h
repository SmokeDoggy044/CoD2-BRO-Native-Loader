#pragma once

// Sharper game text (PC option, --sharp-text): the game's text comes from small
// pre-rendered bitmap fonts (an atlas texture plus a .fdf glyph table each, in the
// .cod packs). This draws each atlas again at the render resolution from free
// look-alike fonts (fonts/ next to the executable, all SIL OFL), glyph by glyph into
// the boxes the game's table gives, with the drop shadow the originals have. The
// renderer shows the new atlas in place of the original (render::BackendConfig::
// replace_texture); the game's glyph table and code stay as they are.

#include <cstdint>
#include <filesystem>
#include <memory>

#include "backend.h"

namespace text {

// Reads the game's font tables and atlases from the packs under `assets` and the
// replacement fonts from `fonts_dir`. Returns how many of the game's fonts can be
// replaced (logged).
size_t prepare_sharp_text(const std::filesystem::path& assets, const std::filesystem::path& fonts_dir);

// For render::BackendConfig::replace_texture: the redrawn atlas if `content` is one of
// the prepared fonts' (made on first use for each scale), else null.
std::shared_ptr<const render::TextureReplacement> sharp_text_replacement(uint64_t content, float render_scale);

}  // namespace text
