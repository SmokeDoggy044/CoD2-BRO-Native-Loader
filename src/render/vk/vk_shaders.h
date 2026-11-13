#pragma once

// Our own Vulkan shaders (src/render/vk/shaders, GLSL), compiled to SPIR-V with glslang at build time.

#include <cstdint>

namespace render::vk {

struct SpirvShader {
    const char* name;  // the file's name: blit.vert, blit.frag
    const uint32_t* code;
    uint32_t bytes;
};

const SpirvShader* spirv_shader(const char* name);  // null if there is none

}  // namespace render::vk
