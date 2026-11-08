#pragma once

#include <cstdint>

namespace loader {

class XbeImage;

// Points every entry of the title's kernel thunk table at an implementation:
// the loader's own (section loading, ...), then the portable ones in src/runtime/xbox/.
// Data exports without one get a zero-filled block; functions without one get a
// stub that stops with the export's name and a backtrace.
void install_kernel_thunks(XbeImage& image);

// Pages of the image that are in memory before the title starts (headers and
// preloaded sections), for the Xbox memory budget.
int32_t initial_image_pages(const XbeImage& image);

}  // namespace loader
