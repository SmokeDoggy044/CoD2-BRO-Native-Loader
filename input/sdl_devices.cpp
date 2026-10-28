// Host input devices on SDL3. See devices.h.

#include "devices.h"
#include "bindings.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <utility>
#include <mutex>

#include "float_env.h"
#include "log.h"

namespace input {

namespace {

std::mutex g_mutex;  // the port table
bool g_started = false;
SDL_Gamepad* g_ports[kPortCount] = {};  // gamepad on each port (port 0 may have none)

// Opens new gamepads into free ports and closes unplugged ones. Needs g_mutex.
void refresh_ports() {
    for (auto& pad : g_ports) {
        if (pad && !SDL_GamepadConnected(pad)) {
            LOG_INFO("input", "gamepad removed");
            SDL_CloseGamepad(pad);
            pad = nullptr;
        }
    }
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (!ids) return;
    for (int i = 0; i < count; ++i) {
        const bool open = std::any_of(std::begin(g_ports), std::end(g_ports),
                                      [&](SDL_Gamepad* pad) { return pad && SDL_GetGamepadID(pad) == ids[i]; });
        if (open) continue;
        auto* slot = std::find(std::begin(g_ports), std::end(g_ports), nullptr);
        if (slot == std::end(g_ports)) break;
        *slot = SDL_OpenGamepad(ids[i]);
        if (!*slot) {
            LOG_WARN("input", "cannot open gamepad: %s", SDL_GetError());
            continue;
        }
        const char* name = SDL_GetGamepadName(*slot);
        LOG_INFO("input", "gamepad on port %d: %s", static_cast<int>(slot - g_ports), name ? name : "?");
    }
    SDL_free(ids);
}

uint8_t analog(bool pressed) { return pressed ? 0xFF : 0; }

// Mouse: relative motion while captured, and the wheel as a short press of what it is
// bound to.
std::mutex g_mouse_mutex;
float g_mouse_dx = 0.0f, g_mouse_dy = 0.0f;
std::atomic<int64_t> g_wheel_up_until_ms{0}, g_wheel_down_until_ms{0};
std::atomic<ActionRemap> g_action_remap{nullptr};
constexpr int64_t kWheelPressMs = 80;  // long enough for the title's next poll

// Menu mode (set_menu_pointer): clicks and wheel steps since take_pointer, guarded by
// g_mouse_mutex like the motion.
std::atomic<int64_t> g_menu_until_ms{0};
constexpr int64_t kMenuLingerMs = 250;
bool g_pointer_moved = false;
int g_left_clicks = 0, g_right_clicks = 0, g_wheel_steps = 0;
// Capture the mouse when menu mode ends: it was captured before (or clicked into the
// window); F10 or focus loss clear it.
std::atomic<bool> g_recapture{false};
std::atomic<Device> g_last_device{Device::Keyboard};
std::atomic<bool> g_blocked{false};  // set_game_input_blocked
// After the menu closes, port 0 stays neutral until the keys and buttons that closed it
// are let go (Escape would pause the game, a click would fire).
std::atomic<bool> g_wait_release{false};
constexpr int kStickDeadZone = 12000, kTriggerDeadZone = 8000;  // SDL axis units
std::mutex g_tap_mutex;
PadState g_tap;  // guarded by g_tap_mutex
int64_t g_tap_until_ms = 0;

int64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

bool menu_mode() { return now_ms() < g_menu_until_ms.load(); }

void set_captured(SDL_Window* window, bool captured) {
    if (window && SDL_GetWindowRelativeMouseMode(window) != captured) {
        SDL_SetWindowRelativeMouseMode(window, captured);
        LOG_INFO("input", "mouse %s", captured ? "captured (F10 frees it)" : "freed");
    }
}

// Frees the mouse in menu mode and captures it again after; on the mouse's own events,
// so that it happens on the thread that pumps them.
void follow_menu_mode(SDL_Window* window) {
    if (!window) return;
    const bool captured = SDL_GetWindowRelativeMouseMode(window);
    if (menu_mode()) {
        if (captured) {
            set_captured(window, false);
            g_recapture = true;
        }
    } else if (!captured && g_recapture && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS)) {
        set_captured(window, true);
    }
}

// Runs on the thread that pumps window events (the renderer's).
bool SDLCALL watch_event(void*, SDL_Event* event) {
    if (g_blocked.load()) return true;  // our menu has the mouse
    switch (event->type) {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_WHEEL:
            g_last_device = Device::Keyboard;
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            g_last_device = Device::Gamepad;
            break;
        case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
            const bool trigger = event->gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER ||
                                 event->gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
            if (std::abs(static_cast<int>(event->gaxis.value)) > (trigger ? kTriggerDeadZone : kStickDeadZone))
                g_last_device = Device::Gamepad;
            break;
        }
        default: break;
    }
    switch (event->type) {
        case SDL_EVENT_MOUSE_MOTION:
            if (std::abs(event->motion.xrel) + std::abs(event->motion.yrel) >= 4.0f) g_last_device = Device::Keyboard;
            if (SDL_Window* window = SDL_GetWindowFromID(event->motion.windowID)) {
                follow_menu_mode(window);
                std::lock_guard lock(g_mouse_mutex);
                if (SDL_GetWindowRelativeMouseMode(window)) {
                    g_mouse_dx += event->motion.xrel;
                    g_mouse_dy += event->motion.yrel;
                } else {
                    g_pointer_moved = true;
                }
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            LOG_DEBUG("input", "mouse button %d down: %s, %s, %s", event->button.button, menu_mode() ? "menu" : "playing",
                      SDL_GetWindowRelativeMouseMode(SDL_GetWindowFromID(event->button.windowID)) ? "captured" : "free",
                      SDL_GetWindowFlags(SDL_GetWindowFromID(event->button.windowID)) & SDL_WINDOW_INPUT_FOCUS
                          ? "focused"
                          : "not focused");
            if (menu_mode()) {
                follow_menu_mode(SDL_GetWindowFromID(event->button.windowID));
                std::lock_guard lock(g_mouse_mutex);
                if (event->button.button == SDL_BUTTON_LEFT) ++g_left_clicks;
                if (event->button.button == SDL_BUTTON_RIGHT) ++g_right_clicks;
                g_recapture = true;  // a mouse player: look with it once the menu closes
            } else {
                set_captured(SDL_GetWindowFromID(event->button.windowID), true);
                g_recapture = true;
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (menu_mode()) {
                std::lock_guard lock(g_mouse_mutex);
                g_wheel_steps += event->wheel.y > 0.0f ? 1 : event->wheel.y < 0.0f ? -1 : 0;
            } else if (event->wheel.y != 0.0f) {
                (event->wheel.y > 0.0f ? g_wheel_up_until_ms : g_wheel_down_until_ms) = now_ms() + kWheelPressMs;
            }
            break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
            LOG_DEBUG("input", "window focus gained");
            break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            LOG_DEBUG("input", "window focus lost");
            set_captured(SDL_GetWindowFromID(event->window.windowID), false);
            g_recapture = false;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (event->key.scancode == SDL_SCANCODE_F10 && !event->key.repeat)
                if (SDL_Window* window = SDL_GetWindowFromID(event->key.windowID)) {
                    const bool capture = !SDL_GetWindowRelativeMouseMode(window);
                    set_captured(window, capture);
                    g_recapture = capture;
                }
            break;
        default: break;
    }
    return true;
}

// Adds `value` to an axis, saturating.
void add_axis(int16_t& axis, int value) {
    axis = static_cast<int16_t>(std::clamp(axis + value, -32768, 32767));
}

void read_gamepad(SDL_Gamepad* pad, PadState& state) {
    const auto button = [&](SDL_GamepadButton b) { return SDL_GetGamepadButton(pad, b); };
    const std::pair<SDL_GamepadButton, uint16_t> digital[] = {
        {SDL_GAMEPAD_BUTTON_DPAD_UP, kDpadUp},       {SDL_GAMEPAD_BUTTON_DPAD_DOWN, kDpadDown},
        {SDL_GAMEPAD_BUTTON_DPAD_LEFT, kDpadLeft},   {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, kDpadRight},
        {SDL_GAMEPAD_BUTTON_START, kStart},          {SDL_GAMEPAD_BUTTON_BACK, kBack},
        {SDL_GAMEPAD_BUTTON_LEFT_STICK, kLeftThumb}, {SDL_GAMEPAD_BUTTON_RIGHT_STICK, kRightThumb},
    };
    for (auto [b, bit] : digital)
        if (button(b)) state.buttons |= bit;
    const std::pair<SDL_GamepadButton, AnalogButton> buttons[] = {
        {SDL_GAMEPAD_BUTTON_SOUTH, kA},           {SDL_GAMEPAD_BUTTON_EAST, kB},
        {SDL_GAMEPAD_BUTTON_WEST, kX},            {SDL_GAMEPAD_BUTTON_NORTH, kY},
        {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, kBlack}, {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, kWhite},
    };
    for (auto [b, index] : buttons)
        state.analog_buttons[index] = std::max(state.analog_buttons[index], analog(button(b)));
    // Triggers: 0..32767 -> 0..255.
    const auto trigger = [&](SDL_GamepadAxis axis) {
        return static_cast<uint8_t>(std::clamp<int>(SDL_GetGamepadAxis(pad, axis), 0, 32767) >> 7);
    };
    state.analog_buttons[kLeftTrigger] =
        std::max(state.analog_buttons[kLeftTrigger], trigger(SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    state.analog_buttons[kRightTrigger] =
        std::max(state.analog_buttons[kRightTrigger], trigger(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    // Sticks: SDL's down is positive, the Xbox's up is.
    add_axis(state.thumb_lx, SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX));
    add_axis(state.thumb_ly, -SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY) - 1);
    add_axis(state.thumb_rx, SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX));
    add_axis(state.thumb_ry, -SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY) - 1);
}

void read_keyboard_mouse(PadState& state) {
    int key_count = 0;
    const bool* keys = SDL_GetKeyboardState(&key_count);
    const auto key = [&](SDL_Scancode code) { return code < key_count && keys[code]; };
    // Action keys (bindings.h), in the default layout. In menu mode the mouse points
    // instead.
    const bool menus = menu_mode();
    const SDL_MouseButtonFlags mouse = menus ? 0 : SDL_GetMouseState(nullptr, nullptr);
    const int64_t now = now_ms();
    const bool wheel_up = !menus && now < g_wheel_up_until_ms.load();
    const bool wheel_down = !menus && now < g_wheel_down_until_ms.load();
    const Bindings& bound = bindings();
    const auto held = [&](Action action) {
        for (InputCode input : bound[action]) {
            if (!input) continue;
            if (input < kMouseButton) {
                if (key(static_cast<SDL_Scancode>(input))) return true;
            } else if (input <= kMouseButton + 5) {
                if (mouse & SDL_BUTTON_MASK(input - kMouseButton)) return true;
            } else if ((input == kWheelUp || input == kWheel) && wheel_up) {
                return true;
            } else if ((input == kWheelDown || input == kWheel) && wheel_down) {
                return true;
            }
        }
        return false;
    };
    const auto stick = [&](Action minus, Action plus) { return (held(plus) - held(minus)) * 32767; };
    add_axis(state.thumb_lx, stick(Action::MoveLeft, Action::MoveRight));
    add_axis(state.thumb_ly, stick(Action::MoveBack, Action::MoveForward));
    add_axis(state.thumb_rx, stick(Action::LookLeft, Action::LookRight));
    add_axis(state.thumb_ry, stick(Action::LookDown, Action::LookUp));

    // Menu keys: fixed buttons in every layout.
    const std::pair<bool, uint16_t> menu_digital[] = {
        {key(SDL_SCANCODE_UP), kDpadUp},       {key(SDL_SCANCODE_DOWN), kDpadDown},
        {key(SDL_SCANCODE_LEFT), kDpadLeft},   {key(SDL_SCANCODE_RIGHT), kDpadRight},
        {key(SDL_SCANCODE_ESCAPE), kStart},    {key(SDL_SCANCODE_TAB), kBack},
    };
    for (auto [pressed, bit] : menu_digital)
        if (pressed) state.buttons |= bit;
    state.analog_buttons[kA] = std::max(state.analog_buttons[kA], analog(key(SDL_SCANCODE_RETURN)));
    state.analog_buttons[kB] = std::max(state.analog_buttons[kB], analog(key(SDL_SCANCODE_BACKSPACE)));

    PadState actions;
    const std::pair<Action, uint16_t> digital[] = {
        {Action::NextWeapon, kDpadUp},     {Action::DpadDown, kDpadDown},   {Action::LeanLeft, kDpadLeft},
        {Action::LeanRight, kDpadRight},   {Action::Sprint, kLeftThumb},    {Action::RightStickClick, kRightThumb},
    };
    for (auto [action, bit] : digital)
        if (held(action)) actions.buttons |= bit;
    const std::pair<Action, AnalogButton> buttons[] = {
        {Action::Use, kA},     {Action::Crouch, kB},  {Action::Reload, kX}, {Action::Jump, kY},
        {Action::Melee, kBlack}, {Action::Grenade, kWhite}, {Action::Aim, kLeftTrigger}, {Action::Fire, kRightTrigger},
    };
    for (auto [action, index] : buttons) actions.analog_buttons[index] = analog(held(action));

    if (ActionRemap remap = g_action_remap.load()) {
        PadState active;
        remap(actions, active);
        actions = active;
    }
    state.buttons |= actions.buttons;
    for (int i = 0; i < 8; ++i)
        state.analog_buttons[i] = std::max(state.analog_buttons[i], actions.analog_buttons[i]);

    std::lock_guard lock(g_tap_mutex);
    if (now_ms() < g_tap_until_ms) {
        state.buttons |= g_tap.buttons;
        for (int i = 0; i < 8; ++i) state.analog_buttons[i] = std::max(state.analog_buttons[i], g_tap.analog_buttons[i]);
    }
}

}  // namespace

bool init_devices() {
    platform::HostFloatEnv host_env;
    std::lock_guard lock(g_mutex);
    if (g_started) return true;
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        LOG_WARN("input", "SDL gamepads: %s", SDL_GetError());
        return false;
    }
    g_started = true;
    SDL_AddEventWatch(watch_event, nullptr);
    refresh_ports();
    return true;
}

void set_action_remap(ActionRemap remap) { g_action_remap = remap; }

Device last_device() { return g_last_device.load(); }

bool in_menu() { return menu_mode(); }

void set_menu_pointer() { g_menu_until_ms = now_ms() + kMenuLingerMs; }

PointerState take_pointer() {
    platform::HostFloatEnv host_env;
    PointerState pointer;
    {
        std::lock_guard lock(g_mouse_mutex);
        pointer.moved = std::exchange(g_pointer_moved, false);
        pointer.left_clicks = std::exchange(g_left_clicks, 0);
        pointer.right_clicks = std::exchange(g_right_clicks, 0);
        pointer.wheel = std::exchange(g_wheel_steps, 0);
    }
    SDL_Window* window = SDL_GetMouseFocus();
    if (g_blocked.load() || !menu_mode() || !window || !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) ||
        SDL_GetWindowRelativeMouseMode(window)) {
        if (pointer.left_clicks || pointer.right_clicks)
            LOG_DEBUG("input", "menu clicks dropped: %s",
                      !menu_mode()   ? "no menu"
                      : !window      ? "the mouse is not over the window"
                      : !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) ? "the window has no focus"
                                                                             : "the mouse is captured");
        return PointerState{};
    }
    SDL_GetMouseState(&pointer.x, &pointer.y);
    const float density = SDL_GetWindowPixelDensity(window);
    pointer.x *= density;
    pointer.y *= density;
    pointer.inside = true;
    return pointer;
}

void tap(const PadState& buttons) {
    constexpr int64_t kTapMs = 100;  // a few frames even at 30 fps
    std::lock_guard lock(g_tap_mutex);
    g_tap = buttons;
    g_tap_until_ms = now_ms() + kTapMs;
}

void take_mouse_motion(float& dx, float& dy) {
    std::lock_guard lock(g_mouse_mutex);
    dx = g_blocked.load() ? 0.0f : g_mouse_dx;
    dy = g_blocked.load() ? 0.0f : g_mouse_dy;
    g_mouse_dx = g_mouse_dy = 0.0f;
}

void set_game_input_blocked(bool blocked) {
    g_blocked = blocked;
    g_wait_release = !blocked;
    std::lock_guard lock(g_mouse_mutex);
    g_mouse_dx = g_mouse_dy = 0.0f;
    g_pointer_moved = false;
    g_left_clicks = g_right_clicks = g_wheel_steps = 0;
}

uint32_t connected_ports() {
    platform::HostFloatEnv host_env;
    std::lock_guard lock(g_mutex);
    uint32_t mask = 1;  // port 0: keyboard and mouse
    if (!g_started) return mask;
    refresh_ports();
    for (uint32_t port = 1; port < kPortCount; ++port)
        if (g_ports[port]) mask |= 1u << port;
    return mask;
}

bool read_port(uint32_t port, PadState& state) {
    platform::HostFloatEnv host_env;
    state = PadState{};
    if (port >= kPortCount) return false;
    std::lock_guard lock(g_mutex);
    if (g_blocked.load()) return port == 0 || g_ports[port] != nullptr;
    if (port == 0 && g_wait_release.load()) {
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if (keys[SDL_SCANCODE_ESCAPE] || keys[SDL_SCANCODE_GRAVE] || keys[SDL_SCANCODE_RETURN] ||
            keys[SDL_SCANCODE_SPACE] || SDL_GetMouseState(nullptr, nullptr))
            return true;
        g_wait_release = false;
    }
    if (g_ports[port]) read_gamepad(g_ports[port], state);
    if (port == 0) {
        read_keyboard_mouse(state);
        return true;
    }
    return g_ports[port] != nullptr;
}

void set_rumble(uint32_t port, uint16_t left, uint16_t right) {
    platform::HostFloatEnv host_env;
    if (port >= kPortCount) return;
    std::lock_guard lock(g_mutex);
    // The Xbox keeps a rumble going until the title changes it; SDL wants a duration.
    // Titles refresh it often enough that a few seconds is plenty.
    if (g_ports[port]) SDL_RumbleGamepad(g_ports[port], left, right, 5000);
}

}  // namespace input
