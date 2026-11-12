// Structured exception handling for title code: the kernel exports RtlRaiseException and RtlUnwind are our engine
// C's (src/engine/shared/title_seh.cpp), which the own engine host uses without the kernel table.

#include "seh.h"

#include "title_seh.h"

namespace loader {

namespace {

const xbox::KernelImpl kSehImpls[] = {
    {"RtlRaiseException", reinterpret_cast<void*>(&game::title_RtlRaiseException)},
    {"RtlUnwind", reinterpret_cast<void*>(&game::title_RtlUnwind)},
};

}  // namespace

std::span<const xbox::KernelImpl> seh_impls() { return kSehImpls; }

}  // namespace loader
