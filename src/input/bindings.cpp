#include "bindings.h"

#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_keyboard.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace input {

namespace {

struct ActionInfo {
    const char* token;  // in settings.ini
    const char* label;
};
constexpr ActionInfo kActions[] = {
    {"move-forward", "Move forward"},
    {"move-back", "Move back"},
    {"move-left", "Move left"},
    {"move-right", "Move right"},
    {"fire", "Fire"},
    {"aim", "Aim down sights"},
    {"use", "Use"},
    {"reload", "Reload"},
    {"jump", "Jump"},
    {"crouch", "Crouch / prone"},
    {"sprint", "Sprint"},
    {"melee", "Melee"},
    {"grenade", "Throw grenade"},
    {"next-weapon", "Next weapon"},
    {"lean-left", "Lean left"},
    {"lean-right", "Lean right"},
    {"right-stick-click", "Right stick click"},
    {"dpad-down", "D-pad down"},
    {"look-up", "Look up"},
    {"look-down", "Look down"},
    {"look-left", "Look left"},
    {"look-right", "Look right"},
};
static_assert(std::size(kActions) == kActionCount);

InputCode key(SDL_Scancode code) { return static_cast<InputCode>(code); }
constexpr InputCode mouse(int button) { return static_cast<InputCode>(kMouseButton + button); }

std::string lower_dashed(const char* name) {
    std::string token;
    for (const char* c = name; *c; ++c)
        token += *c == ' ' ? '-' : static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
    return token;
}

std::string input_token(InputCode input) {
    if (input > kMouseButton && input <= kMouseButton + 5) return "mouse" + std::to_string(input - kMouseButton);
    if (input == kWheelUp) return "wheel-up";
    if (input == kWheelDown) return "wheel-down";
    if (input == kWheel) return "wheel";
    if (input == 0 || input >= SDL_SCANCODE_COUNT) return "";
    return lower_dashed(SDL_GetScancodeName(static_cast<SDL_Scancode>(input)));
}

std::optional<InputCode> input_from_token(std::string_view token) {
    if (token == "none") return InputCode{0};
    for (InputCode input : {mouse(1), mouse(2), mouse(3), mouse(4), mouse(5), kWheelUp, kWheelDown, kWheel})
        if (token == input_token(input)) return input;
    for (int code = 1; code < SDL_SCANCODE_COUNT; ++code) {
        const std::string name = input_token(static_cast<InputCode>(code));
        if (!name.empty() && token == name) return static_cast<InputCode>(code);
    }
    return std::nullopt;
}

Bindings g_bindings = default_bindings();

}  // namespace

Bindings default_bindings() {
    Bindings b;
    b[Action::MoveForward] = {key(SDL_SCANCODE_W)};
    b[Action::MoveBack] = {key(SDL_SCANCODE_S)};
    b[Action::MoveLeft] = {key(SDL_SCANCODE_A)};
    b[Action::MoveRight] = {key(SDL_SCANCODE_D)};
    b[Action::Fire] = {mouse(1)};
    b[Action::Aim] = {mouse(3)};
    b[Action::Use] = {key(SDL_SCANCODE_F)};
    b[Action::Reload] = {key(SDL_SCANCODE_R)};
    b[Action::Jump] = {key(SDL_SCANCODE_SPACE)};
    b[Action::Crouch] = {key(SDL_SCANCODE_C), key(SDL_SCANCODE_LCTRL)};
    b[Action::Sprint] = {key(SDL_SCANCODE_LSHIFT)};
    b[Action::Melee] = {key(SDL_SCANCODE_V), mouse(2)};
    b[Action::Grenade] = {key(SDL_SCANCODE_G)};
    b[Action::NextWeapon] = {key(SDL_SCANCODE_1), kWheel};
    b[Action::LeanLeft] = {key(SDL_SCANCODE_Q)};
    b[Action::LeanRight] = {key(SDL_SCANCODE_E)};
    b[Action::RightStickClick] = {key(SDL_SCANCODE_X)};
    b[Action::DpadDown] = {};
    b[Action::LookUp] = {key(SDL_SCANCODE_I)};
    b[Action::LookDown] = {key(SDL_SCANCODE_K)};
    b[Action::LookLeft] = {key(SDL_SCANCODE_J)};
    b[Action::LookRight] = {key(SDL_SCANCODE_L)};
    return b;
}

bool parse_bindings(std::string_view text, Bindings& bindings, std::string* error) {
    bindings = default_bindings();
    std::array<size_t, kActionCount> seen{};  // inputs read per action
    bool ok = true;
    auto fail = [&](std::string_view word, const char* why) {
        if (ok && error) *error = std::string(why) + ": " + std::string(word);
        ok = false;
    };
    for (size_t at = 0; at < text.size();) {
        if (std::isspace(static_cast<unsigned char>(text[at]))) {
            ++at;
            continue;
        }
        size_t end = at;
        while (end < text.size() && !std::isspace(static_cast<unsigned char>(text[end]))) ++end;
        const std::string_view word = text.substr(at, end - at);
        at = end;
        const size_t equals = word.find('=');
        if (equals == std::string_view::npos || equals + 1 == word.size()) {
            fail(word, "not action=input");
            continue;
        }
        const std::string_view name = word.substr(0, equals);
        const auto action = std::find_if(std::begin(kActions), std::end(kActions),
                                         [&](const ActionInfo& a) { return name == a.token; });
        const std::optional<InputCode> input = input_from_token(word.substr(equals + 1));
        if (action == std::end(kActions)) {
            fail(word, "unknown action");
            continue;
        }
        if (!input) {
            fail(word, "unknown key");
            continue;
        }
        const size_t index = static_cast<size_t>(action - std::begin(kActions));
        if (seen[index] == 0) bindings.inputs[index] = {};
        if (*input && seen[index] < kInputsPerAction) bindings.inputs[index][seen[index]] = *input;
        if (*input) ++seen[index];
        else seen[index] = std::max<size_t>(seen[index], 1);  // "none": cleared, nothing added
    }
    return ok;
}

std::string format_bindings(const Bindings& bindings) {
    const Bindings defaults = default_bindings();
    std::string text;
    for (size_t a = 0; a < kActionCount; ++a) {
        if (bindings.inputs[a] == defaults.inputs[a]) continue;
        bool any = false;
        for (InputCode input : bindings.inputs[a]) {
            if (!input) continue;
            (text += text.empty() ? "" : " ") += std::string(kActions[a].token) + "=" + input_token(input);
            any = true;
        }
        if (!any) (text += text.empty() ? "" : " ") += std::string(kActions[a].token) + "=none";
    }
    return text;
}

const char* action_label(Action action) { return kActions[static_cast<size_t>(action)].label; }

std::string input_name(InputCode input) {
    if (input == mouse(1)) return "Left mouse";
    if (input == mouse(2)) return "Middle mouse";
    if (input == mouse(3)) return "Right mouse";
    if (input > kMouseButton && input <= kMouseButton + 5) return "Mouse " + std::to_string(input - kMouseButton);
    if (input == kWheelUp) return "Wheel up";
    if (input == kWheelDown) return "Wheel down";
    if (input == kWheel) return "Mouse wheel";
    if (input == 0 || input >= SDL_SCANCODE_COUNT) return "";
    return SDL_GetScancodeName(static_cast<SDL_Scancode>(input));
}

bool is_reserved(InputCode input) {
    for (SDL_Scancode code : {SDL_SCANCODE_ESCAPE, SDL_SCANCODE_TAB, SDL_SCANCODE_F10, SDL_SCANCODE_F11,
                              SDL_SCANCODE_F12})
        if (input == key(code)) return true;
    return false;
}

bool is_bound(const Bindings& bindings, InputCode input) {
    if (!input) return false;
    for (const auto& inputs : bindings.inputs)
        if (std::find(inputs.begin(), inputs.end(), input) != inputs.end()) return true;
    return false;
}

void bind(Bindings& bindings, Action action, size_t slot, InputCode input) {
    if (input)
        for (auto& inputs : bindings.inputs)
            for (InputCode& other : inputs)
                if (other == input) other = 0;
    bindings[action][slot] = input;
    for (auto& inputs : bindings.inputs)  // the first input is the one shown in prompts
        if (!inputs[0]) std::swap(inputs[0], inputs[1]);
}

std::string prompt_label(InputCode input) {
    if (input == mouse(1)) return "@lmb";
    if (input == mouse(3)) return "@rmb";
    if (input == mouse(2)) return "@mmb";
    if (input > kMouseButton && input <= kMouseButton + 5) return "M" + std::to_string(input - kMouseButton);
    if (input == kWheelUp || input == kWheelDown || input == kWheel) return "Wheel";
    if (input == key(SDL_SCANCODE_UP)) return "@up";
    if (input == key(SDL_SCANCODE_DOWN)) return "@down";
    if (input == key(SDL_SCANCODE_LEFT)) return "@left";
    if (input == key(SDL_SCANCODE_RIGHT)) return "@right";
    struct Short { SDL_Scancode code; const char* label; };
    static constexpr Short kShort[] = {
        {SDL_SCANCODE_LSHIFT, "Shift"}, {SDL_SCANCODE_RSHIFT, "Shift"}, {SDL_SCANCODE_LCTRL, "Ctrl"},
        {SDL_SCANCODE_RCTRL, "Ctrl"},   {SDL_SCANCODE_LALT, "Alt"},     {SDL_SCANCODE_RALT, "Alt"},
        {SDL_SCANCODE_CAPSLOCK, "Caps"}, {SDL_SCANCODE_DELETE, "Del"},  {SDL_SCANCODE_INSERT, "Ins"},
        {SDL_SCANCODE_PAGEUP, "PgUp"},  {SDL_SCANCODE_PAGEDOWN, "PgDn"},
    };
    for (const Short& s : kShort)
        if (input == key(s.code)) return s.label;
    std::string name = input_name(input);
    if (name.rfind("Keypad ", 0) == 0) name = "Num " + name.substr(7);
    return name;
}

void set_bindings(const Bindings& bindings) { g_bindings = bindings; }
const Bindings& bindings() { return g_bindings; }

}  // namespace input
