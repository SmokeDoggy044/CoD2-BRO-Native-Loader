#pragma once

// The post passes' shaders (render/post/*.hlsl), compiled at build time like the title's.

#include <cstdint>

namespace render {

struct NamedShader {
    const char* name;            // the file's name: post_vs, fxaa, sharpen
    const unsigned char* d3d9;   // vs_3_0 / ps_3_0
    uint32_t d3d9_bytes;
};

const NamedShader* post_shader(const char* name);  // null if there is none

}  // namespace render
