// render/d3d9_shader: translated shaders keep the read-port limits that Windows'
// Direct3D 9 runtime enforces in CreateVertexShader (DXVK does not check them):
// vs_3_0 reads at most one constant (c#) and one input (v#) register per instruction.

#include "test_support.h"

#include <set>
#include <vector>

#include "d3d9_shader.h"

namespace {

constexpr uint32_t kTypeInput = 1, kTypeConst = 2;
constexpr uint32_t kOpDcl = 0x1F, kOpDef = 0x51, kOpComment = 0xFFFE, kOpEnd = 0xFFFF;

uint32_t register_type(uint32_t token) { return (token >> 28 & 7) | (token >> 8 & 0x18); }

// The first instruction that reads two different c# or v# registers (-1 = none).
int read_port_violation(const std::vector<uint32_t>& tokens) {
    for (size_t i = 1; i < tokens.size();) {
        const uint32_t op = tokens[i] & 0xFFFF;
        if (op == kOpEnd) break;
        const uint32_t length = op == kOpComment ? tokens[i] >> 16 : tokens[i] >> 24 & 0xF;
        if (op != kOpDcl && op != kOpDef && op != kOpComment) {
            std::set<uint32_t> constants, inputs;
            for (uint32_t s = 2; s <= length; ++s) {  // sources follow the destination
                const uint32_t token = tokens[i + s];
                if (register_type(token) == kTypeConst) constants.insert(token & 0x7FF);
                if (register_type(token) == kTypeInput) inputs.insert(token & 0x7FF);
            }
            if (constants.size() > 1 || inputs.size() > 1) return static_cast<int>(i);
        }
        i += 1 + length;
    }
    return -1;
}

}  // namespace

int main() {
    // The shortest program: one final MAC mov o[pos] <- v0 (a menu quad's shape).
    const uint32_t program[1][4] = {{0x00000000, 0x0020001B, 0x0836106C, 0x2070F819}};
    for (const auto fog : {render::d3d9::FogMode::Off, render::d3d9::FogMode::Linear, render::d3d9::FogMode::Exp,
                           render::d3d9::FogMode::Exp2}) {
        const auto tokens = render::d3d9::translate_vertex_program(program, 1, 1, fog);
        CHECK(!tokens.empty() && tokens[0] == 0xFFFE0300);
        const int at = read_port_violation(tokens);
        CHECK(at < 0, "fog mode %u: token %d (%08x)", static_cast<unsigned>(fog), at,
              at < 0 ? 0u : tokens[static_cast<size_t>(at)]);
    }
    return test::g_failures ? 1 : 0;
}
