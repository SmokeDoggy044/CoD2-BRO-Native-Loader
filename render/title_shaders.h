#pragma once

// The title's shader programs as HLSL of our own (render/shaders, made by tools/shader_hlsl.py),
// compiled at build time; GfxDevice::create_title_shader finds them by key.

#include <cstdint>

namespace render {

struct TitleShader {
    char stage;    // 'v' vertex, 'p' pixel
    uint32_t key;  // the title data that names it (see tools/shader_hlsl.py)
    uint32_t variant;  // vertex programs: the declaration giving packed normals it is made for, or 0
    const unsigned char* d3d9;  // vs_3_0 / ps_3_0
    uint32_t d3d9_bytes;
};

extern const TitleShader kTitleShaders[];
extern const unsigned kTitleShaderCount;

}  // namespace render
