#pragma once

// Shared bits of the compiled level-script decomp (bro_scr_*.cpp): engine calls go through the
// interface block at 0x00922xxx (one pointer per pScr_* slot).

#include <cstdint>

#include "compiler.h"
#include "title_data.h"

namespace game {

// The function behind an interface slot (`call [slot]`), read at the call.
template <typename F>
F* scr_slot(uint32_t slot) {
    return title_function<F>(title_global<uint32_t>(slot));
}

// ScriptString_Release (0x0011d640, thiscall on the string); EAX = what it returns.
inline uint32_t scr_release(void* string) {
    return title_function<uint32_t XB_THISCALL(void*)>(0x0011d640)(string);
}

// ScriptString_FromCString (0x0011eb50) on a title string.
inline void scr_string_from(void* string, uint32_t text) {
    title_function<void XB_THISCALL(void*, uint32_t)>(0x0011eb50)(string, text);
}

}  // namespace game
