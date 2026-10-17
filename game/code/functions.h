#pragma once

// The decompiled game functions and how the route-B loader uses them: each replaces
// the original at its address, or runs next to it in the comparison harness until
// it has been shown to behave the same (loader/compare.h, docs/comparison-harness.md).

#include <cstdint>
#include <span>

namespace game {

// What a caller can see of a function's result besides memory.
enum class Result : uint8_t {
    None,    // void
    Al,      // bool or char: AL only (the rest of EAX is whatever was left there)
    Eax,     // 32-bit value or pointer
    EdxEax,  // 64-bit value
    St0,     // float or double on the x87 stack
};

enum class Status : uint8_t {
    Compare,   // the original runs; with --compare, sampled calls also run this one
    Replaced,  // this one runs instead of the original
};

struct Function {
    const char* name;
    uint32_t campaign;     // address in default.xbe
    uint32_t multiplayer;  // address in codmp_xboxf.xbe, 0 = not matched yet
    const void* code;      // same calling convention as the original
    uint16_t arg_bytes;    // stack arguments; the callee may write to them
    Result result;
    Status status;
};

std::span<const Function> functions();

}  // namespace game
