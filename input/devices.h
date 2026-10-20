#pragma once

// Host input devices behind the XPP replacement (xbox_input.cpp), on SDL3.
//
// Port 0 is always connected: it merges the keyboard and mouse with the first PC
// gamepad, so the title always has a controller. Further gamepads take ports 1-3 in
// the order they are plugged in and keep their port until unplugged.
//
// Keyboard and mouse, on the pad buttons of CoD BRO's default layout (config/
// default.cfg); the action in brackets:
//   WASD left stick (move)          IJKL right stick (look; the mouse looks directly,
//                                   see take_mouse_motion)
//   left mouse  right trigger (fire)     right mouse  left trigger (aim down sights)
//   Space  Y (jump)    C / left Ctrl  B (crouch, prone)    R  X (reload)
//   F  A (use)         G  white (grenade)    V / middle mouse  black (melee)
//   left Shift  left stick click (sprint)    X  right stick click
//   mouse wheel / 1  D-pad up (next weapon)  Q / E  D-pad left / right (lean)
//   Escape  start (pause)    Tab  back (objectives)
//   menus: arrow keys D-pad, Return A (select), Backspace B (back)
// The action keys (all but the menu keys, Escape and Tab) give these buttons only in
// the default layout; a title can move them to the buttons its active layout uses
// for the same actions (set_action_remap).
// Clicking into the window captures the mouse; focus loss (Alt+Tab) or F10 frees it.
// While the title shows a menu (set_menu_pointer), the mouse is free and points
// instead: its buttons and wheel no longer give pad buttons, the title reads them with
// take_pointer. When the menu closes, a mouse that was captured before is captured
// again.
// Gamepads use their Xbox-style layout; LB is white, RB is black.
//
// Every function may be called from any thread. SDL updates the devices while the
// renderer pumps window events.

#include <cstdint>

namespace input {

constexpr uint32_t kPortCount = 4;

// An Xbox gamepad's controls, in XINPUT_GAMEPAD units.
struct PadState {
    uint16_t buttons = 0;           // XINPUT_GAMEPAD_* digital buttons
    uint8_t analog_buttons[8] = {};  // A B X Y black white left-trigger right-trigger
    int16_t thumb_lx = 0, thumb_ly = 0, thumb_rx = 0, thumb_ry = 0;  // up is positive
};

enum DigitalButton : uint16_t {
    kDpadUp = 0x01, kDpadDown = 0x02, kDpadLeft = 0x04, kDpadRight = 0x08,
    kStart = 0x10, kBack = 0x20, kLeftThumb = 0x40, kRightThumb = 0x80,
};
enum AnalogButton { kA, kB, kX, kY, kBlack, kWhite, kLeftTrigger, kRightTrigger };

// Starts SDL's gamepad support. False (logged) if it is unavailable; port 0 then
// still exists and reports the keyboard, if a window gets created.
bool init_devices();

// Bitmask of connected ports (bit n = port n). Picks up plugged/unplugged gamepads.
uint32_t connected_ports();

// The current state of a port. False if nothing is connected there.
bool read_port(uint32_t port, PadState& state);

// Mouse motion (counts) since the last call while the mouse is captured; 0 otherwise.
// Titles take it where they turn the view (a title-specific hook).
void take_mouse_motion(float& dx, float& dy);

// The title shows a menu that the mouse can point at; call every frame while it is
// up (menu mode ends 0.25 s after the last call).
void set_menu_pointer();

// The mouse in menu mode. Position in window pixels; clicks and wheel steps (up is
// positive) since the last call. Empty outside menu mode.
struct PointerState {
    bool inside = false;  // over the window, which has the input focus
    float x = 0, y = 0;
    bool moved = false;
    int left_clicks = 0, right_clicks = 0, wheel = 0;
};
PointerState take_pointer();

// Presses the given buttons of port 0 for a moment (long enough for the title's next
// polls), as if on the pad.
void tap(const PadState& buttons);

// Rewrites the pad state produced by the keyboard's and mouse's action keys (in the
// default layout above) into the title's active layout. Called from read_port on the
// title's polling thread; null (the default) keeps the default layout.
using ActionRemap = void (*)(const PadState& default_layout, PadState& active_layout);
void set_action_remap(ActionRemap remap);

// The device the player used last: keys, mouse buttons and mouse motion count as the
// keyboard; gamepad buttons and sticks/triggers past a dead zone as a gamepad.
enum class Device { Keyboard, Gamepad };
Device last_device();

// The title shows a menu (set_menu_pointer was called in the last 0.25 s).
bool in_menu();

// Sets a port's rumble motors (0-65535; left is the low-frequency one).
void set_rumble(uint32_t port, uint16_t left, uint16_t right);

}  // namespace input
