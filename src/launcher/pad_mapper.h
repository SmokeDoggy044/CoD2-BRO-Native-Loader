#pragma once

// The launcher's controller mapper (1.0.6): SDL's testcontroller picture of a gamepad (front, and the back for the
// paddles; third_party/sdl_gamepad_art/) with the player's own pad bindings (src/input/bindings.h `pad`). Pressing a
// button on the controller - or clicking it in the picture - selects it, the list under the picture gives it an
// action; held buttons light up. The preset choice (Modern, Original A-D) sits above.

#include <SDL3/SDL.h>

#include <string>

#include "bindings.h"

namespace launcher {

class PadMapper {
public:
    explicit PadMapper(SDL_Renderer* renderer);
    ~PadMapper();
    PadMapper(const PadMapper&) = delete;
    PadMapper& operator=(const PadMapper&) = delete;

    // Window events: gamepads coming and going, a button pressed while the mapper shows (selects it). True if it
    // took the event (the UI does not see it).
    bool event(const SDL_Event& event);
    // Each frame before drawing: not shown until draw() says so; ImGui's pad navigation back on (draw turns it off
    // while the mapper shows, so a press selects a button instead of pressing a widget).
    void new_frame();
    // Draws the mapper; `glyphs` is the pad-glyphs setting (auto, xbox, playstation, nintendo). True when `pad`
    // changed.
    bool draw(input::Bindings& pad, const std::string& glyphs);

private:
    enum class Style { Xbox, PlayStation, Nintendo };
    struct Mode {
        std::string text;           // "D-Input, back buttons reported (2dc8:6012)"
        bool back_buttons = false;  // its mapping has paddles
    };
    Mode mode() const;
    Style style(const std::string& glyphs) const;
    std::string button_name(input::InputCode input, Style style) const;
    bool held(input::InputCode input) const;

    SDL_Renderer* renderer_;
    SDL_Texture* front_ = nullptr;
    SDL_Texture* back_ = nullptr;
    SDL_Texture* faces_[3] = {};  // abxy, sony, bayx (Style order)
    SDL_Texture* highlight_ = nullptr;
    SDL_Gamepad* pad_ = nullptr;   // the first gamepad, for live highlights and press-to-select
    bool shown_ = false;           // drawn this frame (press-to-select only while it shows)
    input::InputCode selected_ = 0;
};

}  // namespace launcher
