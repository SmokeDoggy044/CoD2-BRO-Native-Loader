#pragma once

// The player's keyboard and mouse bindings (issue #14): which keys and mouse buttons
// give each action. The actions are the pad controls of CoD BRO's default controller
// layout (config/default.cfg; devices.h maps them onto the active layout), plus the
// left stick as move keys (the mouse looks; the look keys went in 0.6c). Escape (pause), Tab (objectives) and the loader's own
// keys (F10-F12) are fixed and cannot be bound. The other menu keys (arrows, Return,
// Backspace) can: in menus they still give their buttons, in play only while unbound.
//
// Saved in settings.ini as `keys = <action>=<input> ...`, only the actions that differ
// from the defaults (all of their inputs; `<action>=none` for an action without any).
// Shared by the launcher, which edits them, and the game.
//
// The gamepad has bindings of its own (`pad = ...`, same format, pad inputs only): with our own engine its buttons
// and triggers give these actions directly in play - no Xbox button and no controller layout in between (the
// sticks stay analog: move and look). Start (pause) and Back (objectives) are fixed, like Escape and Tab. In menus
// the pad still works as the Xbox pad the menus were made for.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace input {

enum class Action : uint8_t {
    MoveForward, MoveBack, MoveLeft, MoveRight,
    Fire, Aim, Use, Reload, Jump, Crouch, Sprint, Melee, Grenade,
    NextWeapon, LeanLeft, LeanRight, AimToggle,
    UseReload, RaiseStance, PrevWeapon,  // 1.0.6: commands only the game's own pad layouts had
    HideHud,                             // 1.0.7: the HUD off and on again (the game's cg_draw2D)
    FreeCamera,                          // 1.0.10: the free camera on and off (the Free camera cheat; ours, the
                                         // game never sees it: game::kNativeActionCount stops before it)
    Count
};
constexpr size_t kActionCount = static_cast<size_t>(Action::Count);
constexpr size_t kInputsPerAction = 2;

// An input: an SDL scancode (1-511), a mouse button or a wheel direction; 0 is none.
using InputCode = uint16_t;
constexpr InputCode kMouseButton = 0x200;  // + SDL button number (1 left, 2 middle, 3 right, 4, 5)
constexpr InputCode kWheelUp = 0x210, kWheelDown = 0x211, kWheel = 0x212;  // kWheel: either way
// Gamepad inputs: kPadButton + SDL_GamepadButton (positions: south = A on an Xbox pad), and the triggers.
constexpr InputCode kPadButton = 0x300;
constexpr InputCode kPadLeftTrigger = 0x340, kPadRightTrigger = 0x341;
constexpr bool is_pad_input(InputCode input) { return input >= kPadButton && input <= kPadRightTrigger; }

struct Bindings {
    std::array<std::array<InputCode, kInputsPerAction>, kActionCount> inputs{};
    std::array<InputCode, kInputsPerAction>& operator[](Action a) { return inputs[static_cast<size_t>(a)]; }
    const std::array<InputCode, kInputsPerAction>& operator[](Action a) const {
        return inputs[static_cast<size_t>(a)];
    }
    bool operator==(const Bindings&) const = default;
};

Bindings default_bindings();

// The gamepad's presets: Modern (the default since 1.0.6: RT fire, LT aim, RB grenade, A jump, B crouch, X use and
// reload, Y next weapon, L3 sprint, R3 melee, D-pad left/right lean) and the game's own Xbox layouts A-D
// (config/style_h, _e, _f, _g.cfg) as direct bindings: RB/LB stand for its Black/White buttons.
enum class PadPreset : uint8_t { Modern, LayoutA, LayoutB, LayoutC, LayoutD, Count };
Bindings pad_preset(PadPreset preset);
inline Bindings default_pad_bindings() { return pad_preset(PadPreset::Modern); }
const char* pad_preset_token(PadPreset preset);  // "modern", "layout-a", ...
const char* pad_preset_label(PadPreset preset);  // "Modern", "Original A", ...
std::optional<PadPreset> pad_preset_from_token(std::string_view token);
// The preset these bindings equal, if any.
std::optional<PadPreset> matching_pad_preset(const Bindings& bindings);

// The `keys` setting: parse starts from the defaults; false (and `error` says why) for
// a word it does not know, the others still apply. format writes only the actions that
// differ from the defaults ("" when none do).
// `onto`: start from `bindings` as they are (an action named replaces its inputs) instead of the defaults.
bool parse_bindings(std::string_view text, Bindings& bindings, std::string* error = nullptr, bool onto = false);
std::string format_bindings(const Bindings& bindings);
// The `pad` setting: the same against default_pad_bindings(), pad inputs only.
bool parse_pad_bindings(std::string_view text, Bindings& bindings, std::string* error = nullptr, bool onto = false);
std::string format_pad_bindings(const Bindings& bindings);

// For the launcher: the action as shown, and what it does in the default layout.
const char* action_label(Action action);
// The action's settings.ini token ("move-forward"); the action of a token, if any.
const char* action_token(Action action);
std::optional<Action> action_from_token(std::string_view token);
// A pad input by its settings token ("pad-a", "pad-rt").
std::optional<InputCode> pad_input_from_token(std::string_view token);
// An input as shown ("Left Shift", "Mouse 4", "Wheel up"); "" for none.
std::string input_name(InputCode input);
// The inputs that cannot be bound: Escape, Tab, ` (the PC menu), the loader's own keys (F10-F12) and the pad's
// Start and Back.
bool is_reserved(InputCode input);
// Some action has `input`.
bool is_bound(const Bindings& bindings, InputCode input);
// Sets `input` as the action's slot-th input; another action that had it loses it.
void bind(Bindings& bindings, Action action, size_t slot, InputCode input);

// A short label for the game's key prompts (src/text/key_prompts.h): "@lmb"/"@rmb"/"@mmb"
// for those mouse buttons, "@up"/"@down"/"@left"/"@right" for the arrow keys, "@pad-<token>"
// for a pad input ("@pad-a", "@pad-rt"), else a short key name; "" for none.
std::string prompt_label(InputCode input);

// The bindings the game reads its keyboard and mouse with (devices.h). Set at startup
// and by the PC menu's Controls tab; any thread.
void set_bindings(const Bindings& bindings);
Bindings bindings();  // a copy: the PC menu may change them while the title polls
void set_pad_bindings(const Bindings& bindings);
Bindings pad_bindings();

}  // namespace input
