#pragma once

// Developer mode (--dev, phase 1.0b in docs/developers/own-engine-host.md): an overlay with frame time, GPU memory
// and texture pack counts, and keys that switch things while playing, for A/B looks in place:
// F1 overlay on/off, F2 mods on/off, F3 enhanced graphics on/off, F4 next enhanced feature, F5 that feature on/off,
// F7 pause (the game's cl_paused: the world stops, drawing goes on).

#include <string>
#include <vector>

#include "backend.h"

namespace render::dev {

// Each presented frame, on the presenting thread: handles the keys pressed since the last frame and returns the
// overlay's lines (empty while F1 hides it). `pause` stops (true) or resumes the game; may be null.
std::vector<std::string> frame(Backend& backend, void (*pause)(bool paused));

// What developer mode switched, in one line ("" when nothing): written with F12 marks.
std::string state_text();

}  // namespace render::dev
