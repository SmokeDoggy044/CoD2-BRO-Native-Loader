// Keyboard and mouse bindings (src/input/bindings.h): the `keys` setting's format and the
// launcher's editing rules.

#include <cstdio>
#include <string>

#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_gamepad.h>

#include "bindings.h"

namespace {

int g_failures = 0;

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            ++g_failures;                                                   \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        }                                                                   \
    } while (0)

}  // namespace

int main() {
    using input::Action;
    const input::Bindings defaults = input::default_bindings();
    CHECK(input::format_bindings(defaults).empty());
    input::Bindings parsed;
    CHECK(input::parse_bindings("", parsed) && parsed == defaults);

    // Changed actions only, every input of each, and "none".
    input::Bindings b = defaults;
    b[Action::Fire] = {input::kMouseButton + 4, SDL_SCANCODE_KP_PLUS};
    b[Action::Jump] = {};
    b[Action::Melee] = {SDL_SCANCODE_EQUALS, input::kWheelDown};
    const std::string text = input::format_bindings(b);
    CHECK(text == "fire=mouse4 fire=keypad-+ jump=none melee== melee=wheel-down");
    CHECK(input::parse_bindings(text, parsed) && parsed == b);

    // Unknown words are reported and skipped; the rest applies.
    std::string error;
    CHECK(!input::parse_bindings("reload=t dance=x use=nokey sprint", parsed, &error));
    CHECK(error == "unknown action: dance=x");
    CHECK(parsed[Action::Reload][0] == SDL_SCANCODE_T && parsed[Action::Use] == defaults[Action::Use]);
    // Names with spaces.
    CHECK(input::parse_bindings("sprint=left-alt", parsed) && parsed[Action::Sprint][0] == SDL_SCANCODE_LALT);
    // The look keys of older settings (removed in 0.6c) are skipped, not errors.
    CHECK(input::parse_bindings("look-up=i look-left=j reload=t", parsed) && parsed[Action::Reload][0] == SDL_SCANCODE_T);

    // bind: a key moves from the action that had it; the first input is the one shown.
    b = defaults;
    input::bind(b, Action::Jump, 1, SDL_SCANCODE_C);
    CHECK(b[Action::Crouch][0] == SDL_SCANCODE_LCTRL && b[Action::Crouch][1] == 0);
    CHECK(b[Action::Jump][0] == SDL_SCANCODE_SPACE && b[Action::Jump][1] == SDL_SCANCODE_C);
    input::bind(b, Action::Jump, 0, 0);
    CHECK(b[Action::Jump][0] == SDL_SCANCODE_C && b[Action::Jump][1] == 0);

    CHECK(input::is_reserved(SDL_SCANCODE_ESCAPE) && input::is_reserved(SDL_SCANCODE_F10));
    CHECK(!input::is_reserved(SDL_SCANCODE_F) && !input::is_reserved(SDL_SCANCODE_UP));
    CHECK(!input::is_reserved(SDL_SCANCODE_RETURN) && !input::is_reserved(SDL_SCANCODE_BACKSPACE));
    // Arrow keys bound to moving: bound, and shown as arrows in the prompts.
    b = defaults;
    input::bind(b, Action::MoveForward, 1, SDL_SCANCODE_UP);
    CHECK(input::is_bound(b, SDL_SCANCODE_UP) && !input::is_bound(b, SDL_SCANCODE_DOWN) && !input::is_bound(b, 0));
    CHECK(input::prompt_label(SDL_SCANCODE_UP) == "@up" && input::prompt_label(SDL_SCANCODE_RIGHT) == "@right");
    CHECK(input::input_name(input::kMouseButton + 5) == "Mouse 5" && input::input_name(SDL_SCANCODE_LSHIFT) == "Left Shift");
    CHECK(input::prompt_label(input::kMouseButton + 1) == "@lmb" && input::prompt_label(SDL_SCANCODE_LSHIFT) == "Shift");
    CHECK(input::prompt_label(SDL_SCANCODE_KP_5) == "Num 5");

    // The gamepad (1.0.6): Modern is the default; the game's layouts A-D as direct presets.
    using input::PadPreset;
    const auto pad = [](SDL_GamepadButton button) { return static_cast<input::InputCode>(input::kPadButton + button); };
    using input::Bindings;
    const Bindings modern = input::default_pad_bindings();
    CHECK(modern[Action::Fire][0] == input::kPadRightTrigger && modern[Action::Aim][0] == input::kPadLeftTrigger);
    CHECK(modern[Action::Jump][0] == pad(SDL_GAMEPAD_BUTTON_SOUTH) && modern[Action::UseReload][0] == pad(SDL_GAMEPAD_BUTTON_WEST));
    CHECK(modern[Action::Melee][0] == pad(SDL_GAMEPAD_BUTTON_RIGHT_STICK) && modern[Action::Use][0] == 0);
    CHECK(input::format_pad_bindings(modern).empty());
    CHECK(input::matching_pad_preset(modern) == PadPreset::Modern);
    const Bindings layout_a = input::pad_preset(PadPreset::LayoutA);  // style_h: the game's default
    CHECK(layout_a[Action::Use][0] == pad(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) && layout_a[Action::Melee][0] == pad(SDL_GAMEPAD_BUTTON_SOUTH));
    CHECK(layout_a[Action::NextWeapon][1] == pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN));
    CHECK(input::matching_pad_preset(layout_a) == PadPreset::LayoutA);
    CHECK(input::pad_preset(PadPreset::LayoutD)[Action::Crouch][1] == pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN));
    for (size_t p = 0; p < static_cast<size_t>(PadPreset::Count); ++p) {  // every preset round-trips and differs
        const Bindings preset = input::pad_preset(static_cast<PadPreset>(p));
        Bindings back;
        CHECK(input::parse_pad_bindings(input::format_pad_bindings(preset), back) && back == preset);
        CHECK(input::pad_preset_from_token(input::pad_preset_token(static_cast<PadPreset>(p))) == static_cast<PadPreset>(p));
    }
    // pad = ...: pad inputs only; keys = ...: no pad inputs.
    Bindings padded;
    CHECK(input::parse_pad_bindings("grenade=pad-lb jump=pad-a jump=pad-paddle-l1", padded));
    CHECK(padded[Action::Grenade][0] == pad(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) &&
          padded[Action::Jump][1] == pad(SDL_GAMEPAD_BUTTON_LEFT_PADDLE1));
    CHECK(input::format_pad_bindings(padded) == "jump=pad-a jump=pad-paddle-l1 grenade=pad-lb");
    CHECK(!input::parse_pad_bindings("jump=space", padded, &error) && error == "unknown pad button: jump=space");
    CHECK(!input::parse_bindings("jump=pad-a", parsed, &error) && error == "unknown key: jump=pad-a");
    CHECK(input::is_reserved(pad(SDL_GAMEPAD_BUTTON_START)) && input::is_reserved(pad(SDL_GAMEPAD_BUTTON_BACK)));
    CHECK(!input::is_reserved(pad(SDL_GAMEPAD_BUTTON_SOUTH)));
    CHECK(input::prompt_label(input::kPadRightTrigger) == "@pad-rt" && input::input_name(pad(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) == "LB");
    // The new keyboard actions start unbound.
    CHECK(defaults[Action::UseReload][0] == 0 && defaults[Action::PrevWeapon][0] == 0);

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("bindings_test: all passed\n");
    return 0;
}
