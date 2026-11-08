#pragma once

#include <filesystem>

namespace loader {

// --diag: writes DIR/summary.txt from DIR/log.txt - how the session ended, its length
// and reboots, the marks, and every distinct warning and error with its count, first
// time and the screenshot nearest to it. A session starts at the last launch that was
// not a title reboot. Called when the process exits (also after a crash).
void write_diag_summary(const std::filesystem::path& dir);

}  // namespace loader
