#pragma once

// The NV2A (Kelvin) state the D3D front end keeps: the last value of every method,
// the vertex program slots and the vertex shader constants. Everything that reaches
// the GPU as methods goes through here - SetRenderState_Simple, the shader constant
// and program functions, and raw pushbuffer data from BeginPush/RunPushBuffer - so
// the draw-time translation has one place to read it from.

#include <array>
#include <cstddef>
#include <cstdint>

namespace render::nv2a {

constexpr uint32_t kProgramSlots = 136;   // vertex program instructions (4 dwords each)
constexpr uint32_t kConstantSlots = 192;  // vertex shader constants (c-96..c95 -> 0..191)

// Methods with side effects beyond storing the value.
constexpr uint32_t kTransformProgram = 0x0B00;  // ..0x0B7C: program data at the load pointer
constexpr uint32_t kTransformConstant = 0x0B80;  // ..0x0BFC: constant data at the load pointer
constexpr uint32_t kBeginEnd = 0x17FC;           // primitive type, 0 = end
constexpr uint32_t kInlineArray = 0x1818;        // vertex data between begin and end
constexpr uint32_t kTransformProgramLoad = 0x1E9C;
constexpr uint32_t kTransformProgramStart = 0x1EA0;
constexpr uint32_t kTransformConstantLoad = 0x1EA4;

struct State {
    std::array<uint32_t, 0x800> registers{};  // by method / 4
    std::array<std::array<uint32_t, 4>, kProgramSlots> program{};
    std::array<std::array<uint32_t, 4>, kConstantSlots> constants{};  // float bits
    uint32_t program_load = 0;   // in dwords
    uint32_t constant_load = 0;  // in dwords
};

State& state();

inline uint32_t& reg(uint32_t method) { return state().registers[(method & 0x1FFC) >> 2]; }

// Writes `count` values starting at `method` (incrementing the method unless
// `same_method`), as one pushbuffer command would.
void write(uint32_t method, const uint32_t* values, uint32_t count, bool same_method = false);
inline void write(uint32_t method, uint32_t value) { write(method, &value, 1); }

// Executes a pushbuffer stream (commands with their data). Returns false and stops
// at the first command it cannot handle (jumps, calls), which it logs.
bool run(const uint32_t* commands, size_t dwords);

// Called when an inline primitive (SET_BEGIN_END ... INLINE_ARRAY ... END) is complete.
using InlineDrawHandler = void (*)(uint32_t primitive, const uint32_t* vertex_data, size_t dwords);
void set_inline_draw_handler(InlineDrawHandler handler);

}  // namespace render::nv2a
