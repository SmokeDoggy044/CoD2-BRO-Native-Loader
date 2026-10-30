#pragma once

// --dump-shaders DIR: every shader program the renderer's shader slots get, for the shader tool
// of our own renderer (tools/shader_hlsl.py, docs/own-renderer.md R3).

#include <filesystem>

namespace loader {

// DIR/vs_KEY.bin (the vertex program's function blob), DIR/ps_KEY.bin (D3DPIXELSHADERDEF),
// DIR/decl_ADDRESS.bin (the 16 vertex inputs) and DIR/shaders.csv (kind, key, slot, declaration).
void start_shader_dump(const std::filesystem::path& dir);

}  // namespace loader
