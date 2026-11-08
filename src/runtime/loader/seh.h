#pragma once

// Structured exception handling for title code (RtlRaiseException, RtlUnwind).

#include <span>

#include "kernel.h"

namespace loader {

std::span<const xbox::KernelImpl> seh_impls();

}  // namespace loader
