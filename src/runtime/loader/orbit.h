#pragma once

// The lab's camera orbit (--orbit FILE, engine improvements phase 2 tools): render::dev's extra frame. The game
// pauses at the file's start frame and world scenes are drawn from each pose in turn (rendering only:
// game::RendererPcHooks::camera_override), so camera-dependent lighting bugs show up in pictures and numbers
// without a playtest. tools/lab.py orbit writes the file and takes the pictures (--replay-shots).

#include <cstdint>
#include <string>
#include <vector>

namespace loader {

// False (and why on stderr) when the file cannot be read or has no poses.
bool load_orbit(const std::string& path);
std::vector<std::string> orbit_frame(uint32_t keys);

}  // namespace loader
