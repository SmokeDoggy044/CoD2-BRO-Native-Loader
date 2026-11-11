#pragma once

#include <cstdint>
#include <vector>

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

// A title thread without a kernel handle (game::TitleHost::start_thread): runs the stdcall routine(context) on a
// stack of `stack_size` bytes with the title's FS block and a `tls_size` TLS block.
bool start_title_thread(uint32_t stack_size, uint32_t tls_size, uint32_t routine, uint32_t context,
                        uint32_t* thread_id);

// In-process restart (own engine host 5b, game::TitleHost::relaunch): the calling title thread hands over the next
// LaunchDataPage and ends; the loader's start thread, blocked in wait_for_title_restart, gets the page once that
// thread is gone. reset_thread_ids: the next title thread is id 1 again (the main thread, as at a fresh start).
[[noreturn]] void request_title_restart(const uint8_t* page, uint32_t size);
std::vector<uint8_t> wait_for_title_restart();
void reset_thread_ids();

}  // namespace loader
