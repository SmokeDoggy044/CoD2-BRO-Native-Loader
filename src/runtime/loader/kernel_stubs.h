#pragma once

// The kernel thunk table without a kernel (cod_bro, --host): every function import becomes a stub that stops with
// the export's name and a backtrace, every data import a zero-filled block. Our C calls no kernel export, so any
// call is a bug. The loader's install_kernel_thunks uses the same stubs for exports it does not implement.

#include <cstdint>

namespace loader {

class XbeImage;

void* kernel_stub(uint32_t ordinal);  // ordinal <= xbox::kMaxKernelOrdinal
void* kernel_zero_block();            // a fresh zero-filled 4 KiB block

// Fills the whole table with kernel_stub / kernel_zero_block.
void install_fatal_kernel_thunks(XbeImage& image);

}  // namespace loader
