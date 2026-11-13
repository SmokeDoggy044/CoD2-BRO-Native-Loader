#pragma once

// Title threads without the kernel (own engine host): the thread ids, game::TitleHost::start_thread and the
// in-process restart. Shared by cod_bro and the loader; the loader's kernel threads (PsCreateSystemThreadEx in
// kernel_thunks.cpp) take their ids from here too, so ids stay in one sequence.

#include <cstdint>
#include <vector>

namespace loader {

// The next title thread id (1 = the title's main thread).
uint32_t next_title_thread_id();
// The next title thread is id 1 again (the main thread, as at a fresh start).
void reset_thread_ids();

// A title thread for our C (game::TitleHost::start_thread): runs the stdcall routine(context) on a stack of
// `stack_size` bytes with the title's FS block and a `tls_size` TLS block. No kernel handle: our own object table
// keeps the thread's handle.
bool start_title_thread(uint32_t stack_size, uint32_t tls_size, uint32_t routine, uint32_t context,
                        uint32_t* thread_id);

// In-process restart (own engine host 5b, game::TitleHost::relaunch): the calling title thread hands over the next
// LaunchDataPage and ends; the start thread, blocked in wait_for_title_restart, gets the page once that thread is
// gone (its host-side thread main calls title_thread_left after leaving the title).
[[noreturn]] void request_title_restart(const uint8_t* page, uint32_t size);
std::vector<uint8_t> wait_for_title_restart();
void title_thread_left();

}  // namespace loader
