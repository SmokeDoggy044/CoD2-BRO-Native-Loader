#pragma once

// Anim-handle dispatchers of the compiled level scripts: a hash -> global table per anim tree,
// compiled as a binary compare tree. The tables are generated (tools/gen_anim_handles.py).

#include <cstddef>
#include <cstdint>

#include "compiler.h"

namespace game {

// 0x00184c60 (EAX hash, EDX value, ECX out; 25520 bytes, 686 keys): the generic_human anim
// handles. out set: *out = the handle's global, returned; out null: the global = value,
// returned. Unknown hashes return 0. Half the get leaves leave the global in EDX (EDX:EAX
// result, so the harness checks it too).
uint64_t XB_REGS3 XB_CDECL BroScr_BAnimHandles_generic_human(uint32_t hash, uint32_t value, uint32_t* out);

}  // namespace game
