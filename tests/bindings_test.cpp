// Keyboard and mouse bindings (input/bindings.h): the `keys` setting's format and the
// launcher's editing rules.

#include <cstdio>
#include <string>

#include <SDL3/SDL_scancode.h>

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

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("bindings_test: all passed\n");
    return 0;
}
