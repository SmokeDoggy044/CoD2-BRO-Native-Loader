// See spirv_layout.h.

#include "spirv_layout.h"

#include <cstdio>
#include <cstring>
#include <map>

namespace render::vk {

namespace {

// d3dbc (D3D9 shader model 3 tokens).
constexpr uint32_t kD3dDcl = 0x1F, kD3dComment = 0xFFFE, kD3dEnd = 0xFFFF;
constexpr uint32_t kRegInput = 1, kRegOutput = 6, kRegSampler = 10;
constexpr uint32_t kUsagePosition = 0, kUsageTexcoord = 5, kUsageColor = 10, kUsageFog = 11;

// SPIR-V.
constexpr uint32_t kSpvMagic = 0x07230203;
constexpr uint32_t kOpName = 5, kOpDecorate = 71;
constexpr uint32_t kDecorationLocation = 30, kDecorationBinding = 33,
                   kDecorationDescriptorSet = 34;

struct Declaration {
    uint32_t usage, index;
};

std::string format(const char* text, uint32_t a, uint32_t b = 0) {
    char buffer[160];
    std::snprintf(buffer, sizeof buffer, text, a, b);
    return buffer;
}

// The declarations of v# (key 'v'), o# ('o') and s# ('s', usage = texture type) by register number.
bool read_declarations(const uint32_t* tokens, uint32_t count, std::map<std::pair<char, uint32_t>, Declaration>& out,
                       bool& vertex, std::string* error) {
    const uint32_t kind = count ? tokens[0] >> 16 : 0;
    if (count < 2 || (kind != 0xFFFE && kind != 0xFFFF) || (tokens[0] & 0xFF00u) != 0x0300u) {
        *error = format("not a shader model 3 shader (version token %08x)", count ? tokens[0] : 0);
        return false;
    }
    vertex = kind == 0xFFFE;
    for (uint32_t i = 1; i < count;) {
        const uint32_t t = tokens[i];
        const uint32_t opcode = t & 0xFFFF;
        if (t == kD3dEnd) return true;
        if (opcode == kD3dComment) {
            i += 1 + ((t >> 16) & 0x7FFF);
            continue;
        }
        const uint32_t length = (t >> 24) & 0xF;
        if (opcode == kD3dDcl && length == 2 && i + 2 < count) {
            const uint32_t usage = tokens[i + 1], reg = tokens[i + 2];
            const uint32_t type = (reg >> 28 & 7) | (reg >> 8 & 0x18), number = reg & 0x7FF;
            if (type == kRegInput || type == kRegOutput)
                out[{type == kRegInput ? 'v' : 'o', number}] = {usage & 0x1F, usage >> 16 & 0xF};
            else if (type == kRegSampler)
                out[{'s', number}] = {usage >> 27 & 0xF, 0};
        }
        i += 1 + length;
    }
    *error = "no end token";
    return false;
}

}  // namespace

std::vector<uint32_t> spirv_fixed_layout(const void* d3dbc, uint32_t d3dbc_bytes, const void* spirv,
                                         uint32_t spirv_bytes, std::string* error) {
    std::string ignored;
    if (!error) error = &ignored;
    if (!d3dbc || !spirv || d3dbc_bytes % 4 || spirv_bytes % 4 || spirv_bytes < 20) {
        *error = "missing or odd-sized code";
        return {};
    }
    std::vector<uint32_t> tokens(d3dbc_bytes / 4);
    std::memcpy(tokens.data(), d3dbc, d3dbc_bytes);
    std::vector<uint32_t> words(spirv_bytes / 4);
    std::memcpy(words.data(), spirv, spirv_bytes);
    if (words[0] != kSpvMagic) {
        *error = "not SPIR-V";
        return {};
    }

    std::map<std::pair<char, uint32_t>, Declaration> declarations;
    bool vertex = false;
    if (!read_declarations(tokens.data(), uint32_t(tokens.size()), declarations, vertex, error)) return {};

    // Names of the ids vkd3d made: v0, o3, s0, t0, cb0_0.
    std::map<uint32_t, std::string> names;
    for (size_t i = 5; i < words.size();) {
        const uint32_t count = words[i] >> 16, opcode = words[i] & 0xFFFF;
        if (count == 0 || i + count > words.size()) {
            *error = format("bad instruction at word %u", uint32_t(i));
            return {};
        }
        if (opcode == kOpName && count >= 3)
            names[words[i + 1]] = std::string(reinterpret_cast<const char*>(&words[i + 2]),
                                              strnlen(reinterpret_cast<const char*>(&words[i + 2]), (count - 2) * 4));
        i += count;
    }

    auto location_of = [&](char kind, uint32_t reg, uint32_t& location) {
        auto it = declarations.find({kind, reg});
        if (it == declarations.end()) {
            *error = format("%c%u has no declaration", uint32_t(kind), reg);
            return false;
        }
        const Declaration d = it->second;
        if (vertex && kind == 'v') {
            if (d.usage != kUsageTexcoord) {
                *error = format("vertex input v%u: usage %u (not TEXCOORD)", reg, d.usage);
                return false;
            }
            location = d.index;
            return true;
        }
        if (d.usage == kUsageColor && d.index < 2) location = kVkLocationColor0 + d.index;
        else if (d.usage == kUsageTexcoord && d.index < 8) location = kVkLocationTexcoord0 + d.index;
        else if (d.usage == kUsageFog && d.index == 0) location = kVkLocationFog;
        else {
            *error = std::string(1, kind) + std::to_string(reg) + format(": usage %u index %u has no location", d.usage, d.index);
            return false;
        }
        return true;
    };

    for (size_t i = 5; i < words.size(); i += words[i] >> 16) {
        const uint32_t count = words[i] >> 16, opcode = words[i] & 0xFFFF;
        if (opcode != kOpDecorate || count < 4) continue;
        const uint32_t decoration = words[i + 2];
        if (decoration != kDecorationLocation && decoration != kDecorationBinding &&
            decoration != kDecorationDescriptorSet)
            continue;
        auto named = names.find(words[i + 1]);
        if (named == names.end()) {
            *error = format("decorated id %u has no name", words[i + 1]);
            return {};
        }
        const std::string& name = named->second;
        uint32_t& value = words[i + 3];
        if (decoration == kDecorationDescriptorSet) {
            value = 0;
            continue;
        }
        const char kind = name.empty() ? 0 : name[0];
        unsigned reg = 0;
        int used = 0;
        if (decoration == kDecorationBinding) {
            if (name == "cb0_0") value = vertex ? kVkVertexConstantsBinding : kVkPixelConstantsBinding;
            else if ((kind == 't' || kind == 's') && std::sscanf(name.c_str() + 1, "%u%n", &reg, &used) == 1 &&
                     used == int(name.size() - 1) && reg < 8)
                value = (kind == 't' ? kVkFirstImageBinding : kVkFirstSamplerBinding) + reg;
            else {
                *error = "unexpected resource " + name;
                return {};
            }
            continue;
        }
        // Location.
        if ((kind != 'v' && kind != 'o') || std::sscanf(name.c_str() + 1, "%u%n", &reg, &used) != 1 ||
            used != int(name.size() - 1)) {
            *error = "unexpected interface variable " + name;
            return {};
        }
        if (!vertex && kind == 'o') continue;  // colour outputs oC# keep location # (render target #)
        if (!location_of(kind, reg, value)) return {};
    }
    // The position output is a built-in already; any other output declared POSITION would be lost.
    for (const auto& [key, d] : declarations)
        if (key.first == 'o' && d.usage == kUsagePosition && key.second != 0) {
            *error = format("position in o%u", key.second);
            return {};
        }
    return words;
}

uint32_t spirv_input_mask(const std::vector<uint32_t>& words) {
    constexpr uint32_t kOpTypePointer = 32, kOpVariable = 59, kStorageInput = 1;
    std::map<uint32_t, uint32_t> pointer_storage, variable_storage, locations;
    for (size_t i = 5; i < words.size() && (words[i] >> 16); i += words[i] >> 16) {
        const uint32_t count = words[i] >> 16, opcode = words[i] & 0xFFFF;
        if (opcode == kOpTypePointer && count >= 4) pointer_storage[words[i + 1]] = words[i + 2];
        else if (opcode == kOpVariable && count >= 4) variable_storage[words[i + 2]] = words[i + 3];
        else if (opcode == kOpDecorate && count >= 4 && words[i + 2] == kDecorationLocation)
            locations[words[i + 1]] = words[i + 3];
    }
    uint32_t mask = 0;
    for (const auto& [id, location] : locations) {
        auto v = variable_storage.find(id);
        if (v != variable_storage.end() && v->second == kStorageInput && location < 32) mask |= 1u << location;
    }
    return mask;
}

}  // namespace render::vk
