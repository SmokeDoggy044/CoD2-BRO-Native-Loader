#pragma once

// The object viewer's keys and overlay lines (--viewer, phase 1.0d): render::dev's extra frame.

#include <cstdint>
#include <string>
#include <vector>

namespace loader {

void set_viewer_models(std::vector<std::string> models);
// F8: the next model in front of the player (the game pauses); F9: turning on/off.
std::vector<std::string> viewer_frame(uint32_t keys);

}  // namespace loader
