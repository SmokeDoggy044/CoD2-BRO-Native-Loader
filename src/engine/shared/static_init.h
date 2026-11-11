#pragma once

// The title's C++ static initializers (_cinit's .CRT$XC table) for the own engine host (step 0.3):
// static_init_generated.cpp (tools/gen_static_init.py) holds them in order.

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

struct StaticInitializer {
    uint32_t address;  // the original stub
    void (*code)();    // its generated C, or null: called through `address` (C behind the host's jump)
};
extern const StaticInitializer kStaticInitializers[];
extern const size_t kStaticInitializerCount;

// _atexit (0x001eed36) for the initializers: keeps the destructor; the host never runs them (the title never
// returns from its main loop, a quit ends the process). Returns 0 like _atexit's success.
uint32_t static_init_atexit(uint32_t destructor);

// Runs every initializer in table order (what _cinit does after the CRT's own .CRT$XI ones).
void XB_CDECL run_static_initializers();

// _cinit (0x001eddde) for the own engine host: the floating-point init hook (0x008d1378) and the CRT's .CRT$XI
// initializers (0x008978d0..0x008978ec) still by address (they belong to the title CRT, which our CRT replaces
// as a whole), then run_static_initializers().
void XB_CDECL title_cinit();

}  // namespace game
