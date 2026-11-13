#pragma once

// What the loader and the own engine host set up alike before the title starts: network port, sound output and
// tuning, graphics back end, our C's TitleHost (threads, replay, memory, time, folders), native platform objects
// under --own-main, and input.

#include <filesystem>

#include "options.h"

namespace loader {

struct TitleInfo;

// device_failed: called when no graphics device can be created (does not return).
void configure_title_services(const Options& options, const std::filesystem::path& assets, const TitleInfo& title,
                              float aspect, void (*device_failed)());

}  // namespace loader
