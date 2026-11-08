// NV2A method interpreter. See nv2a.h.

#include "nv2a.h"

#include <vector>

#include "log.h"

namespace render::nv2a {

namespace {

State g_state;
std::vector<uint32_t> g_inline_vertices;
uint32_t g_primitive = 0;
InlineDrawHandler g_inline_draw = nullptr;

void write_one(uint32_t method, uint32_t value) {
    State& s = g_state;
    if (method >= kTransformProgram && method < kTransformProgram + 0x80) {
        const uint32_t slot = s.program_load / 4;
        if (slot < kProgramSlots) s.program[slot][s.program_load % 4] = value;
        ++s.program_load;
    } else if (method >= kTransformConstant && method < kTransformConstant + 0x80) {
        const uint32_t slot = s.constant_load / 4;
        if (slot < kConstantSlots) s.constants[slot][s.constant_load % 4] = value;
        ++s.constant_load;
    } else if (method == kTransformProgramLoad) {
        s.program_load = value * 4;
    } else if (method == kTransformConstantLoad) {
        s.constant_load = value * 4;
    } else if (method == kBeginEnd) {
        if (value != 0) {
            g_primitive = value;
            g_inline_vertices.clear();
        } else if (g_primitive != 0) {
            if (!g_inline_vertices.empty() && g_inline_draw)
                g_inline_draw(g_primitive, g_inline_vertices.data(), g_inline_vertices.size());
            g_primitive = 0;
        }
    } else if (method == kInlineArray) {
        g_inline_vertices.push_back(value);
    }
    s.registers[(method & 0x1FFC) >> 2] = value;
}

}  // namespace

State& state() { return g_state; }

void write(uint32_t method, const uint32_t* values, uint32_t count, bool same_method) {
    for (uint32_t i = 0; i < count; ++i) write_one(same_method ? method : method + i * 4, values[i]);
}

bool run(const uint32_t* commands, size_t dwords) {
    size_t i = 0;
    while (i < dwords) {
        const uint32_t command = commands[i++];
        if (command == 0) continue;  // NOP
        const uint32_t method = command & 0x1FFC;
        const uint32_t count = (command >> 18) & 0x7FF;
        bool same_method;
        if ((command & 0xE0030003) == 0)
            same_method = false;
        else if ((command & 0xE0030003) == 0x40000000)
            same_method = true;
        else {
            LOG_ERROR("render", "pushbuffer command %08x not supported (jump/call/return)", command);
            return false;
        }
        if (i + count > dwords) {
            LOG_ERROR("render", "pushbuffer command %08x runs past the end", command);
            return false;
        }
        write(method, commands + i, count, same_method);
        i += count;
    }
    return true;
}

void set_inline_draw_handler(InlineDrawHandler handler) { g_inline_draw = handler; }

}  // namespace render::nv2a
