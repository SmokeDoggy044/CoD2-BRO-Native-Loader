#pragma once

#include <cstdint>

namespace loader {

class XbeImage;

// Points every entry of the title's kernel thunk table at an implementation:
// the loader's own (section loading, ...), then the portable ones in src/runtime/xbox/.
// Data exports without one get a zero-filled block; functions without one get a
// stub that stops with the export's name and a backtrace (kernel_stubs.h). `host` (--host): no implementation at all - every
// function is that stub, every data export a zero block (our C under --own-main calls no kernel export).
void install_kernel_thunks(XbeImage& image, bool host = false);

}  // namespace loader
