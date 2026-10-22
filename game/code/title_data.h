#pragma once

// The title's own globals, reached at their default.xbe addresses while the route-B
// loader maps the original image there. Decompiled functions that use one stay
// campaign-only (multiplayer address 0) until the globals are ours (route C).

#include <cstdint>

#include "compiler.h"

namespace game {

template <typename T>
T& title_global(uint32_t address) {
    return *reinterpret_cast<T*>(static_cast<uintptr_t>(address));
}

// A title function not decompiled yet, called at its default.xbe address. F carries
// the calling convention (XB_CDECL etc.).
template <typename F>
F* title_function(uint32_t address) {
    return reinterpret_cast<F*>(static_cast<uintptr_t>(address));
}

// Register inputs for title_call_at_regs.
struct TitleRegs {
    uint32_t eax, ebx, ecx, edx, esi, edi;
};

}  // namespace game

// Calls `function` with ESP at `top` minus its stack arguments (args[0] pushed last) and
// the registers in `regs`; returns its EAX. For callers that run each callee where the
// original's call puts it (an asm entry moves the C frame below a gap): title code stores
// addresses of its stack locals in globals (CoD BRO FX_AddParticleComponents: 0x00971800).
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL title_call_at_regs(uint32_t top, uint32_t function,
                                                            const game::TitleRegs* regs, uint32_t count,
                                                            const uint32_t* args);
