#pragma once

// Shaders of the graphics tests (tests/shaders/*.hlsl), compiled like the title's: D3D9 tokens and SPIR-V.

#include "post_shaders.h"

namespace render {

const NamedShader* test_shader(const char* name);  // the file's name: gfx_vs, gfx_color_ps; null if none

}  // namespace render
