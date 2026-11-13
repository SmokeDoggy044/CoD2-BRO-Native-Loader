#pragma once

// What the loader and the own engine host set up alike before the title starts: network port, sound output and
// tuning, graphics back end, our C's TitleHost (threads, replay, memory, time, folders), native platform objects
// under --own-main, and input.

#include <cstdint>
#include <filesystem>

#include "options.h"

namespace loader {

struct TitleInfo;

// The title thread plumbing both mains give the kernel / host code (xbox::TitleCalls): the calling thread's KTHREAD
// address, a title environment for a host thread, and a title call on the calling thread's stack.
uint32_t current_kthread();
void attach_kernel_thread();
void call_guest(uint32_t function, const uint32_t* args, uint32_t count);

// Also sets xbox::TitleCalls. device_failed: called when no graphics device can be created (does not return).
void configure_title_services(const Options& options, const std::filesystem::path& assets, const TitleInfo& title,
                              float aspect, void (*device_failed)());

}  // namespace loader
