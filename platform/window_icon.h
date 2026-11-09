#pragma once

// Shared window icon for the launcher and the game's SDL window. The pixels are
// baked into platform/window_icon.cpp; no image file is needed at run time.
struct SDL_Window;

namespace platform {
void set_window_icon(SDL_Window* window);
}  // namespace platform
