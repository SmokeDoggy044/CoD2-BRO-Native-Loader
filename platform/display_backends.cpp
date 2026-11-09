// See display_backends.h.

#include "display_backends.h"

#include <SDL3/SDL.h>

#include <cstdlib>

#include "log.h"

namespace platform {

void prefer_display_backends() {
    // An explicit choice by the player (or SDL's own environment) always wins.
    if (const char* sdl = std::getenv("SDL_VIDEO_DRIVER"); sdl && *sdl) return;
    if (const char* custom = std::getenv("COD_BRO_VIDEO_DRIVER"); custom && *custom) {
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, custom);
        LOG_INFO("platform", "video driver: %s (COD_BRO_VIDEO_DRIVER)", custom);
        return;
    }
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    const char* x11 = std::getenv("DISPLAY");
    const bool have_wayland = wayland && *wayland;
    const bool have_x11 = x11 && *x11;
    // SDL3 can load the X11 and Wayland client libraries at run time (and
    // platform/CMakeLists.txt can link them in explicitly). Prefer X11 whenever
    // a DISPLAY is available: SDL3's Wayland backend has no
    // software window surface, so a 2D SDL_Renderer (the launcher's ImGui UI) can
    // fail there with "Couldn't find matching render driver" when EGL/GLES is not
    // usable. X11 works on a native X11 session *and* through XWayland on a
    // Wayland session; pure Wayland (no DISPLAY) still uses Wayland with X11 as a
    // fallback. SDL tries the list in order and falls back automatically.
    const char* drivers = have_x11 ? "x11,wayland" : have_wayland ? "wayland,x11" : "x11,wayland";
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, drivers);
    LOG_INFO("platform", "video driver preference: %s (WAYLAND_DISPLAY%s, DISPLAY%s)", drivers,
             have_wayland ? " set" : " unset", have_x11 ? " set" : " unset");
}

}  // namespace platform
