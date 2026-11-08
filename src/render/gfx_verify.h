#pragma once

// XB_GFX_VERIFY: the renderer C's recorded state against the Xbox path's at every draw of the
// Xbox path (gfx_verify.cpp).

#include <cstdint>

#include "gfx_state.h"

namespace render {

// Where the recorded state comes from (the loader: the renderer C's); reads XB_GFX_VERIFY.
void set_gfx_state_probe(const GfxState* (*probe)());

// D3D9Draw::draw, before each draw.
void verify_gfx_state(uint32_t frame, bool has_depth);

}  // namespace render
