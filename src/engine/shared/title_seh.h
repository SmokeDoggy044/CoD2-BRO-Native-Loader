#pragma once

// Structured exception handling for title code and our C (title_seh.cpp): the kernel's RtlRaiseException and
// RtlUnwind, on the FS:[0] chain of the calling title thread. The loader's kernel exports point here too.

#include <cstdint>

#include "compiler.h"

namespace game {

void XB_STDCALL title_RtlRaiseException(uint32_t record_address);
uint32_t XB_STDCALL title_RtlUnwind(uint32_t target_frame, uint32_t target_ip, uint32_t record_address,
                                    uint32_t return_value);

}  // namespace game
