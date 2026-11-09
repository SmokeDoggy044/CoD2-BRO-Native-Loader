#pragma once

// Window-system selection for Linux. SDL3 can drive both X11 and Wayland; this
// picks the session's native one first but keeps the other as a fallback, so the
// launcher and the game start on X11, on Wayland and under XWayland alike.
// Call before SDL_Init. COD_BRO_VIDEO_DRIVER / SDL_VIDEO_DRIVER override it.

namespace platform {
void prefer_display_backends();
}
