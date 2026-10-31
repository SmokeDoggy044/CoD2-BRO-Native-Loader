#pragma once

// The player's keyboard and mouse bindings (issue #14): which keys and mouse buttons
// give each action. The actions are the pad controls of CoD BRO's default controller
// layout (config/default.cfg; devices.h maps them onto the active layout), plus the
// sticks as move and look keys. Escape (pause), Tab (objectives) and the loader's own
// keys (F10-F12) are fixed and cannot be bound. The other menu keys (arrows, Return,
// Backspace) can: in menus they still give their buttons, in play only while unbound.
//
// Saved in settings.ini as `keys = <action>=<input> ...`, only the actions that differ
// from the defaults (all of their inputs; `<action>=none` for an action without any).
// Shared by the launcher, which edits them, and the game.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace input {

enum class Action : uint8_t {
    MoveForward, MoveBack, MoveLeft, MoveRight,
    Fire, Aim, Use, Reload, Jump, Crouch, Sprint, Melee, Grenade,
    NextWeapon, LeanLeft, LeanRight, RightStickClick, DpadDown,
    LookUp, LookDown, LookLeft, LookRight,
    Count
};
constexpr size_t kActionCount = static_cast<size_t>(Action::Count);
constexpr size_t kInputsPerAction = 2;

// An input: an SDL scancode (1-511), a mouse button or a wheel direction; 0 is none.
using InputCode = uint16_t;
constexpr InputCode kMouseButton = 0x200;  // + SDL button number (1 left, 2 middle, 3 right, 4, 5)
constexpr InputCode kWheelUp = 0x210, kWheelDown = 0x211, kWheel = 0x212;  // kWheel: either way

struct Bindings {
    std::array<std::array<InputCode, kInputsPerAction>, kActionCount> inputs{};
    std::array<InputCode, kInputsPerAction>& operator[](Action a) { return inputs[static_cast<size_t>(a)]; }
    const std::array<InputCode, kInputsPerAction>& operator[](Action a) const {
        return inputs[static_cast<size_t>(a)];
    }
    bool operator==(const Bindings&) const = default;
};

Bindings default_bindings();

// The `keys` setting: parse starts from the defaults; false (and `error` says why) for
// a word it does not know, the others still apply. format writes only the actions that
// differ from the defaults ("" when none do).
bool parse_bindings(std::string_view text, Bindings& bindings, std::string* error = nullptr);
std::string format_bindings(const Bindings& bindings);

// For the launcher: the action as shown, and what it does in the default layout.
const char* action_label(Action action);
// An input as shown ("Left Shift", "Mouse 4", "Wheel up"); "" for none.
std::string input_name(InputCode input);
// The keys that cannot be bound: Escape, Tab and the loader's own keys.
bool is_reserved(InputCode input);
// Some action has `input`.
bool is_bound(const Bindings& bindings, InputCode input);
// Sets `input` as the action's slot-th input; another action that had it loses it.
void bind(Bindings& bindings, Action action, size_t slot, InputCode input);

// A short label for the game's key prompts (text/key_prompts.h): "@lmb"/"@rmb"/"@mmb"
// for those mouse buttons, "@up"/"@down"/"@left"/"@right" for the arrow keys, else a
// short key name; "" for none.
std::string prompt_label(InputCode input);

// The bindings the game reads its keyboard and mouse with (devices.h). Set once at
// startup, before the title polls input.
void set_bindings(const Bindings& bindings);
const Bindings& bindings();

}  // namespace input
