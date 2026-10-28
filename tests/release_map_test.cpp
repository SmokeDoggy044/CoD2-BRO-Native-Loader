// ReleaseMap (loader/release_map.h): finding the supported build's functions and data
// in a build where code grew in the middle, later sections moved and data moved
// within its section (as the European multiplayer XBE of issue #9), and variables
// after the code in its section changed (as D3D's in that XBE).

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <random>
#include <vector>

#include "release_map.h"

namespace {

int g_failures = 0;

#define CHECK(condition, ...)                                              \
    do {                                                                   \
        if (!(condition)) {                                                \
            ++g_failures;                                                  \
            std::printf("FAIL %s:%d: %s", __FILE__, __LINE__, #condition); \
            __VA_OPT__(std::printf(" - "); std::printf(__VA_ARGS__);)      \
            std::printf("\n");                                             \
        }                                                                  \
    } while (0)

constexpr uint32_t kBase = 0x10000;
constexpr int kFunctions = 60;
constexpr int kGrown = 30;           // this function gets 0x30 more bytes in its middle
constexpr uint32_t kGrowth = 0x30;
constexpr int kVariables = 40;
constexpr uint32_t kTail = 0x40;  // variables after the code, in .text (as an SDK library's)
constexpr int kTailVariables[] = {38, 39};

struct Build {
    std::vector<uint8_t> bytes;
    std::vector<loader::ImageSection> sections;
    std::vector<uint32_t> functions;
    loader::ImageBytes view() const { return {kBase, bytes, sections}; }
};

// Instructions without addresses in them (imm8/disp8 random), for filling functions.
// C1 E8 (shr eax,imm8) holds an E8 byte that is not a CALL.
const std::vector<std::vector<uint8_t>> kFillers = {
    {0x8B, 0xC1}, {0x03, 0xC2}, {0x85, 0xC9}, {0x8B, 0x4E, 0}, {0x89, 0x46, 0}, {0x83, 0xC0, 0},
    {0x74, 0},    {0x75, 0},    {0x90},       {0x6A, 0},       {0x56},          {0x5E},
    {0xC1, 0xE8, 0}, {0x8D, 0x49, 0x00}, {0x0F, 0xAF, 0xC1}, {0xD9, 0x46, 0}, {0xF6, 0x44, 0x24, 0x08, 0},
};

// Functions of random instructions that each refer to two variables, at about 8 and
// 24 bytes in: test ecx,ecx; mov [esi],variable (its bytes C9 C7 06 00 read as an
// address in the image when the variable's low byte is 0, which hid the variable from
// 0.1.12's hash), and mov eax,[variable]. `grow` adds bytes in the
// middle of function kGrown. `variable_at` places each variable.
Build make_build(bool grow, const std::vector<uint32_t>& variable_at) {
    std::mt19937 random(1234);
    std::vector<std::vector<std::vector<uint8_t>>> bodies;  // per function, its fillers
    for (int f = 0; f < kFunctions; ++f) {
        const int length = 40 + static_cast<int>(random() % 160);
        std::vector<std::vector<uint8_t>> body;
        for (int bytes = 0; bytes < length;) {
            std::vector<uint8_t> filler = kFillers[random() % kFillers.size()];
            if (filler.size() > 1 && filler.back() == 0) filler.back() = static_cast<uint8_t>(random() % 0x7F + 1);
            bytes += static_cast<int>(filler.size());
            body.push_back(filler);
        }
        bodies.push_back(body);
    }
    Build build;
    const uint32_t text = 0x11000;
    uint32_t at = text;
    std::vector<uint8_t> code;
    for (int f = 0; f < kFunctions; ++f) {
        build.functions.push_back(at);
        std::vector<uint8_t> body;
        bool grown = false, placed[2] = {};
        for (const std::vector<uint8_t>& filler : bodies[f]) {
            for (int k = 0; k < 2; ++k) {
                if (placed[k] || body.size() < static_cast<size_t>(8 + 16 * k)) continue;
                placed[k] = true;
                const uint32_t variable = variable_at[(f * 2 + k) % kVariables];
                const std::vector<uint8_t> load = k == 0 ? std::vector<uint8_t>{0x85, 0xC9, 0xC7, 0x06}
                                                         : std::vector<uint8_t>{0xA1};
                body.insert(body.end(), load.begin(), load.end());
                body.insert(body.end(), reinterpret_cast<const uint8_t*>(&variable),
                            reinterpret_cast<const uint8_t*>(&variable) + 4);
            }
            if (grow && f == kGrown && !grown && body.size() >= 36) {
                body.insert(body.end(), kGrowth, 0x90);
                grown = true;
            }
            body.insert(body.end(), filler.begin(), filler.end());
        }
        body.push_back(0xC3);
        code.insert(code.end(), body.begin(), body.end());
        at += static_cast<uint32_t>(body.size());
        while (at % 16) code.push_back(0xCC), ++at;
    }
    // The tail: a symbol like a function's, with other contents in the other build.
    while (at % 0x20) code.push_back(0xCC), ++at;
    build.functions.push_back(at);
    code.resize(code.size() + kTail, 0);
    code[code.size() - kTail + 0x10] = grow ? 1 : 2;
    at += kTail;
    const uint32_t text_size = at - text;
    const uint32_t data = (at + 0x1F) & ~0x1Fu;  // XBE sections: 0x20 apart
    build.sections = {{".text", text, text_size}, {".data", data, 0x400}};
    build.bytes.assign(data + 0x400 - kBase, 0);
    std::memcpy(&build.bytes[text - kBase], code.data(), code.size());
    return build;
}

std::vector<uint32_t> variables(uint32_t data, bool shuffled) {
    std::vector<uint32_t> at;
    for (int v = 0; v < kVariables; ++v) at.push_back(data + 0x10 * v);
    // Another build: variables 10-19 sit 0x80 later (something before them grew), and
    // variable 5 went to the end.
    if (shuffled) {
        for (int v = 10; v < 20; ++v) at[v] += 0x80;
        at[5] = data + 0x3F0;
    }
    for (int v : kTailVariables) at[v] = data - kTail + 0x20 + 0x10 * (v - kTailVariables[0]);
    return at;
}

}  // namespace

int main() {
    // The supported build: its data section's address depends on its code size.
    const uint32_t supported_data = make_build(false, variables(0, false)).sections[1].address;
    const Build supported = make_build(false, variables(supported_data, false));
    const Build other_probe = make_build(true, variables(0, false));
    const uint32_t other_data = other_probe.sections[1].address;
    const Build other = make_build(true, variables(other_data, true));
    CHECK(other_data > supported_data, "the data section did not move");

    loader::ReleaseSignatures signatures = loader::make_signatures(supported.view(), supported.functions);
    CHECK(signatures.functions.size() == kFunctions + 1);
    CHECK(signatures.data.size() == kVariables, "%zu data references", signatures.data.size());

    const auto path = std::filesystem::temp_directory_path() / "release_map_test_signatures.txt";
    CHECK(signatures.save(path));
    loader::ReleaseSignatures loaded;
    CHECK(loaded.load(path));
    std::filesystem::remove(path);
    CHECK(loaded.functions.size() == signatures.functions.size() && loaded.data.size() == signatures.data.size());
    CHECK(loaded.image_end == signatures.image_end && loaded.sections.size() == signatures.sections.size());

    // Not built: the identity.
    loader::ReleaseMap map;
    CHECK(map.identity() && map.find(0x12345) == 0x12345u);

    // The supported build itself maps onto itself.
    map.build(loaded, supported.view());
    CHECK(map.unchanged() == kFunctions + 1 && map.missing() == 0);
    for (uint32_t f : supported.functions) CHECK(map.find(f + 3) == f + 3);

    map.build(loaded, other.view());
    CHECK(map.unchanged() == kFunctions - 1, "%zu unchanged", map.unchanged());
    CHECK(map.entry_only() == 1, "%zu entry only", map.entry_only());
    CHECK(map.missing() == 1, "%zu missing (the tail)", map.missing());
    for (int f = 0; f < kFunctions; ++f) {
        const uint32_t ours = supported.functions[f], theirs = other.functions[f];
        CHECK(map.find(ours) == theirs, "function %d", f);
        if (f == kGrown)
            CHECK(!map.find(ours + 40), "inside the grown function");  // changed code
        else
            CHECK(map.find(ours + 20) == theirs + 20, "function %d + 20", f);
    }
    const std::vector<uint32_t> ours = variables(supported_data, false), theirs = variables(other_data, true);
    bool window_case = false;  // a mov [esi],variable whose variable's low byte is 0 here only
    for (int v = 0; v < kVariables; v += 2) window_case |= (ours[v] & 0xFF) == 0 && (theirs[v] & 0xFF) != 0;
    CHECK(window_case, "no variable for the issue #9 window case");
    for (int v = 0; v < kVariables; ++v) CHECK(map.find(ours[v]) == theirs[v], "variable %d", v);
    // Between variables that moved alike: alike; between ones that moved differently: unknown.
    CHECK(map.find(ours[2] + 4) == theirs[2] + 4);
    CHECK(map.find(ours[12] + 4) == theirs[12] + 4);
    CHECK(!map.find(ours[9] + 8), "between variables 9 and 10");
    // In the changed tail: where code refers to; between its variables, as they moved.
    CHECK(map.find(ours[38] + 4) == theirs[38] + 4);
    CHECK(!map.find(supported.functions[kFunctions]), "the tail's start");
    // A guess in changed code moves like the function it is in.
    CHECK(map.guess(supported.functions[kGrown] + 40) == other.functions[kGrown] + 40);

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("release_map_test: all passed\n");
    return 0;
}
