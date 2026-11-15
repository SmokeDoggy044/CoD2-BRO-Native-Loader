#pragma once

// Developer mode (--dev, phase 1.0b in docs/developers/own-engine-host.md): an overlay with frame time, GPU memory
// and texture pack counts, and keys that switch things while playing, for A/B looks in place:
// F1 overlay on/off, F2 mods on/off, F3 enhanced graphics on/off, F4 next enhanced feature, F5 that feature on/off,
// F6 texture picker (the draws under the crosshair: content hash, size, mod, dump name with
// COD_BRO_TEXTURE_NAMES=bridge.csv; Vulkan only), F7 pause (the game's cl_paused: the world stops, drawing goes on), F10 next debug view (DebugView).

#include <cstdint>
#include <string>
#include <vector>

#include "backend.h"

namespace render::dev {

// Each presented frame, on the presenting thread: handles the keys pressed since the last frame and returns the
// overlay's lines (empty while F1 hides it). `pause` stops (true) or resumes the game; may be null.
std::vector<std::string> frame(Backend& backend, void (*pause)(bool paused));

// Pauses or resumes the game as F7 does (the object viewer pauses while it shows a model).
void set_paused(bool paused);
// F7 holds the game paused.
bool paused();

// More of developer mode from outside the render library (the object viewer): called each frame with the keys
// pressed (bit n-1 = Fn); returns lines for the overlay.
using ExtraFrame = std::vector<std::string> (*)(uint32_t keys);
void set_extra(ExtraFrame extra);

// Debug views of our own shaders (engine improvements phase 2 tools): what our pixel shaders output instead of
// the picture. 0 = off. Our shaders read it as c3.x (game::RendererPcHooks::pixel_override_constants); a view a
// shader has no data for leaves that draw as it is. F10 in developer mode steps through them; --debug-view NAME
// sets one (lab runs, no --dev needed).
enum DebugView : uint32_t {
    kViewOff = 0,
    kViewLighting,   // the light only: textures white (lightmap x shadow on the world, the summed lights on models)
    kViewShadow,     // the sun shadow's factor: white = full sun, black = all lost; red = no sun there (baked)
    kViewSunMask,    // the baked-sun weight (light grid's sun reference) as grey
    kViewNormals,    // the shading normal (world space, 0.5 + 0.5 n)
    kViewRoughness,  // the roughness map as grey
    kViewLights,     // lights per draw: 0 black, 1 blue, 2 green, 3 yellow, 4+ red
    kViewAo,         // ambient occlusion only (white = open)
    kViewCount
};
const char* debug_view_name(uint32_t view);
uint32_t debug_view();
// By name ("off", "lighting", ...); false: no such view (the names go to stderr).
bool set_debug_view(const char* name);
// Called with the new view whenever it changes (the loader passes it on to the game's renderer).
void on_debug_view(void (*changed)(uint32_t view));

// What developer mode switched, in one line ("" when nothing): written with F12 marks.
std::string state_text();

}  // namespace render::dev
