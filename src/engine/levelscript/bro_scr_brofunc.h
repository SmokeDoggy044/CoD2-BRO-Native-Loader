#pragma once

// Levelscript (push c3): the BroFunc registry g_broFuncs (0x00927170, 52 {name hash, function}
// pairs) the script runtime fills through its interface table.

#include <cstdint>

#include "compiler.h"

namespace game {

// 0x002f4f30 (dead: no callers, no stored pointer): the function registered under the name in
// ESI (Com_HashKeyNoCase), 0 when missing (the "NOT FOUND" text is built and dropped when `report`).
extern "C" void BroFunc_Find_entry(void);
extern "C" PLATFORM_HIDDEN uint32_t XB_CDECL BroFunc_Find_c(const char* name, uint32_t report);
// 0x002f4f90: stores {hash of `name` (rotate left 5, xor each lowercased char), function} in the
// first free pair; Sys_DebugBreak when all 52 are used.
void XB_CDECL BroFunc_Register(const char* name, uint32_t function);
// 0x002f4ff0: clears the 52 pairs and the word at 0x0092716c.
void XB_CDECL BroFunc_Clear();

}  // namespace game
