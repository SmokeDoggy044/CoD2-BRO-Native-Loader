#pragma once

// The title's own globals, reached at their default.xbe addresses while the route-B
// loader maps the original image there. Decompiled functions that use one stay
// campaign-only (multiplayer address 0) until the globals are ours (route C).

#include <cstdint>

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

}  // namespace game
