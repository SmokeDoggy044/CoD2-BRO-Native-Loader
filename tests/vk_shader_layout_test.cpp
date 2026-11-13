// Every embedded title and post shader through vk/spirv_layout.h: the layout pass accepts it, every binding
// and location lands where the fixed layout says, and each pixel shader's inputs are written by the vertex
// shaders it runs with (title pixel shaders: every title vertex program; post: post_vs) with at least as many
// components. With a directory argument the results are written there as NAME.spv for spirv-val
// (vk_shader_layout_spirv_val, cmake/spirv_val_dir.cmake).

#include "test_support.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "post_shaders.h"
#include "spirv_layout.h"
#include "title_shaders.h"

namespace {

using namespace render;
using namespace render::vk;

struct Interface {
    std::map<uint32_t, uint32_t> inputs, outputs;  // location -> components
    std::vector<uint32_t> bindings;
    bool ok = true;
};

// Locations, component counts and bindings of a SPIR-V module.
Interface read_interface(const std::vector<uint32_t>& w) {
    Interface out;
    std::map<uint32_t, uint32_t> components;            // type id -> components (float 1, vector n)
    std::map<uint32_t, std::pair<uint32_t, uint32_t>> pointers;  // id -> storage class, pointee
    std::map<uint32_t, uint32_t> locations;              // variable -> location
    std::vector<std::pair<uint32_t, uint32_t>> variables;  // id, pointer type
    for (size_t i = 5; i < w.size(); i += w[i] >> 16) {
        const uint32_t count = w[i] >> 16, op = w[i] & 0xFFFF;
        if (op == 22 && count >= 3) components[w[i + 1]] = 1;                      // OpTypeFloat
        else if (op == 23) components[w[i + 1]] = w[i + 3];                        // OpTypeVector
        else if (op == 32) pointers[w[i + 1]] = {w[i + 2], w[i + 3]};              // OpTypePointer
        else if (op == 59) variables.push_back({w[i + 2], w[i + 1]});              // OpVariable
        else if (op == 71 && w[i + 2] == 30) locations[w[i + 1]] = w[i + 3];       // Location
        else if (op == 71 && w[i + 2] == 33) out.bindings.push_back(w[i + 3]);     // Binding
    }
    for (auto [id, type] : variables) {
        auto location = locations.find(id);
        if (location == locations.end()) continue;
        const auto [storage, pointee] = pointers[type];
        const uint32_t n = components.count(pointee) ? components[pointee] : 0;
        if (n == 0) out.ok = false;
        (storage == 1 ? out.inputs : out.outputs)[location->second] = n;  // Input 1, Output 3
    }
    return out;
}

std::vector<uint32_t> fixed(const unsigned char* d3d9, uint32_t d3d9_bytes, const unsigned char* spirv,
                            uint32_t spirv_bytes, const std::string& name) {
    std::string error;
    std::vector<uint32_t> words = spirv_fixed_layout(d3d9, d3d9_bytes, spirv, spirv_bytes, &error);
    CHECK(!words.empty(), "%s: %s", name.c_str(), error.c_str());
    return words;
}

void write(const std::filesystem::path& dir, const std::string& name, const std::vector<uint32_t>& words) {
    if (dir.empty() || words.empty()) return;
    std::ofstream(dir / (name + ".spv"), std::ios::binary)
        .write(reinterpret_cast<const char*>(words.data()), std::streamsize(words.size() * 4));
}

bool binding_allowed(uint32_t binding, bool vertex) {
    if (vertex) return binding == kVkVertexConstantsBinding;
    return binding == kVkPixelConstantsBinding ||
           (binding >= kVkFirstImageBinding && binding < kVkFirstImageBinding + 8) ||
           (binding >= kVkFirstSamplerBinding && binding < kVkFirstSamplerBinding + 8);
}

void check_pair(const Interface& vs, const Interface& ps, const std::string& vs_name, const std::string& ps_name) {
    for (auto [location, n] : ps.inputs) {
        auto it = vs.outputs.find(location);
        CHECK(it != vs.outputs.end() && it->second >= n, "%s reads location %u (%u components) that %s %s",
              ps_name.c_str(), location, n, vs_name.c_str(),
              it == vs.outputs.end() ? "does not write" : "writes with fewer components");
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::filesystem::path dir = argc > 1 ? argv[1] : "";
    if (!dir.empty()) std::filesystem::create_directories(dir);

    std::vector<std::pair<std::string, Interface>> vertex, pixel;
    for (unsigned i = 0; i < kTitleShaderCount; ++i) {
        const TitleShader& s = kTitleShaders[i];
        char name[40];
        std::snprintf(name, sizeof name, "%cs_%08x_%08x", s.stage, s.key, s.variant);
        const std::vector<uint32_t> words = fixed(s.d3d9, s.d3d9_bytes, s.spirv, s.spirv_bytes, name);
        if (words.empty()) continue;
        write(dir, name, words);
        Interface in = read_interface(words);
        CHECK(in.ok, "%s: interface variable of unknown type", name);
        for (uint32_t b : in.bindings) CHECK(binding_allowed(b, s.stage == 'v'), "%s: binding %u", name, b);
        if (s.stage == 'v')
            for (auto [location, n] : in.inputs) CHECK(location < 16, "%s: vertex input location %u", name, location);
        (s.stage == 'v' ? vertex : pixel).push_back({name, in});
    }
    CHECK(!vertex.empty() && !pixel.empty(), "no title shaders");
    for (const auto& [ps_name, ps] : pixel)
        for (const auto& [vs_name, vs] : vertex) check_pair(vs, ps, vs_name, ps_name);

    Interface post_vs;
    for (const char* name : {"post_vs", "fxaa", "sharpen"}) {
        const NamedShader* s = post_shader(name);
        CHECK(s != nullptr, "no post shader %s", name);
        if (!s) continue;
        const std::vector<uint32_t> words = fixed(s->d3d9, s->d3d9_bytes, s->spirv, s->spirv_bytes, name);
        write(dir, std::string("post_") + name, words);
        const Interface in = read_interface(words);
        for (uint32_t b : in.bindings)
            CHECK(binding_allowed(b, std::strcmp(name, "post_vs") == 0), "%s: binding %u", name, b);
        if (std::strcmp(name, "post_vs") == 0) post_vs = in;
        else check_pair(post_vs, in, "post_vs", name);
    }

    std::string error;
    CHECK(spirv_fixed_layout(nullptr, 0, nullptr, 0, &error).empty(), "empty input accepted");
    std::printf("%zu vertex and %zu pixel title shaders\n", vertex.size(), pixel.size());
    return test::finish("vk_shader_layout_test");
}
