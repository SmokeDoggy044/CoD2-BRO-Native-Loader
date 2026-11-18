#include "bindings.h"

#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_gamepad.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>

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
    {"aim-toggle", "Aim down sights (toggle)"},
    {"use-reload", "Use / reload"},
    {"raise-stance", "Raise stance"},
    {"previous-weapon", "Previous weapon"},
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

// The pad inputs: settings token and name (Xbox names; the prompts draw the pad's own glyphs).
struct PadInputInfo {
    InputCode input;
    const char* token;
    const char* name;
};
constexpr InputCode pad(SDL_GamepadButton button) { return static_cast<InputCode>(kPadButton + button); }
constexpr PadInputInfo kPadInputs[] = {
    {pad(SDL_GAMEPAD_BUTTON_SOUTH), "pad-a", "A"},
    {pad(SDL_GAMEPAD_BUTTON_EAST), "pad-b", "B"},
    {pad(SDL_GAMEPAD_BUTTON_WEST), "pad-x", "X"},
    {pad(SDL_GAMEPAD_BUTTON_NORTH), "pad-y", "Y"},
    {pad(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER), "pad-lb", "LB"},
    {pad(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER), "pad-rb", "RB"},
    {kPadLeftTrigger, "pad-lt", "LT"},
    {kPadRightTrigger, "pad-rt", "RT"},
    {pad(SDL_GAMEPAD_BUTTON_LEFT_STICK), "pad-ls", "Left stick click"},
    {pad(SDL_GAMEPAD_BUTTON_RIGHT_STICK), "pad-rs", "Right stick click"},
    {pad(SDL_GAMEPAD_BUTTON_DPAD_UP), "pad-up", "D-pad up"},
    {pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN), "pad-down", "D-pad down"},
    {pad(SDL_GAMEPAD_BUTTON_DPAD_LEFT), "pad-left", "D-pad left"},
    {pad(SDL_GAMEPAD_BUTTON_DPAD_RIGHT), "pad-right", "D-pad right"},
    {pad(SDL_GAMEPAD_BUTTON_BACK), "pad-back", "Back"},
    {pad(SDL_GAMEPAD_BUTTON_START), "pad-start", "Start"},
    {pad(SDL_GAMEPAD_BUTTON_MISC1), "pad-misc", "Share"},
    {pad(SDL_GAMEPAD_BUTTON_TOUCHPAD), "pad-touchpad", "Touchpad"},
    // The back buttons (paddles) by the names Steam gives any pad's: L4 / R4 upper, L5 / R5 lower.
    {pad(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1), "pad-paddle-r1", "Back R4"},
    {pad(SDL_GAMEPAD_BUTTON_LEFT_PADDLE1), "pad-paddle-l1", "Back L4"},
    {pad(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2), "pad-paddle-r2", "Back R5"},
    {pad(SDL_GAMEPAD_BUTTON_LEFT_PADDLE2), "pad-paddle-l2", "Back L5"},
};

const PadInputInfo* pad_input_info(InputCode input) {
    for (const PadInputInfo& info : kPadInputs)
        if (info.input == input) return &info;
    return nullptr;
}

std::string input_token(InputCode input) {
    if (const PadInputInfo* info = pad_input_info(input)) return info->token;
    if (input > kMouseButton && input <= kMouseButton + 5) return "mouse" + std::to_string(input - kMouseButton);
    if (input == kWheelUp) return "wheel-up";
    if (input == kWheelDown) return "wheel-down";
    if (input == kWheel) return "wheel";
    if (input == 0 || input >= SDL_SCANCODE_COUNT) return "";
    return lower_dashed(SDL_GetScancodeName(static_cast<SDL_Scancode>(input)));
}

std::optional<InputCode> input_from_token(std::string_view token) {
    if (token == "none") return InputCode{0};
    for (const PadInputInfo& info : kPadInputs)
        if (token == info.token) return info.input;
    for (InputCode input : {mouse(1), mouse(2), mouse(3), mouse(4), mouse(5), kWheelUp, kWheelDown, kWheel})
        if (token == input_token(input)) return input;
    for (int code = 1; code < SDL_SCANCODE_COUNT; ++code) {
        const std::string name = input_token(static_cast<InputCode>(code));
        if (!name.empty() && token == name) return static_cast<InputCode>(code);
    }
    return std::nullopt;
}

std::mutex g_bindings_mutex;
Bindings g_bindings = default_bindings();  // guarded by g_bindings_mutex
Bindings g_pad_bindings = default_pad_bindings();  // guarded by g_bindings_mutex

// parse_bindings / parse_pad_bindings: `pad` takes pad inputs only, `keys` everything else.
bool parse_into(std::string_view text, Bindings& bindings, std::string* error, bool pad_inputs);
std::string format_against(const Bindings& bindings, const Bindings& defaults);

struct PresetInfo {
    const char* token;
    const char* label;
};
constexpr PresetInfo kPresets[] = {
    {"modern", "Modern"},
    {"layout-a", "Original A"},
    {"layout-b", "Original B"},
    {"layout-c", "Original C"},
    {"layout-d", "Original D"},
};
static_assert(std::size(kPresets) == static_cast<size_t>(PadPreset::Count));

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
    b[Action::AimToggle] = {key(SDL_SCANCODE_X)};
    return b;
}

Bindings pad_preset(PadPreset preset) {
    using A = Action;
    Bindings b;
    const auto set = [&](A action, InputCode input) {
        auto& inputs = b[action];
        (inputs[0] ? inputs[1] : inputs[0]) = input;
    };
    const InputCode a = pad(SDL_GAMEPAD_BUTTON_SOUTH), bb = pad(SDL_GAMEPAD_BUTTON_EAST),
                    x = pad(SDL_GAMEPAD_BUTTON_WEST), y = pad(SDL_GAMEPAD_BUTTON_NORTH),
                    lb = pad(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER), rb = pad(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER),
                    ls = pad(SDL_GAMEPAD_BUTTON_LEFT_STICK), rs = pad(SDL_GAMEPAD_BUTTON_RIGHT_STICK),
                    up = pad(SDL_GAMEPAD_BUTTON_DPAD_UP), down = pad(SDL_GAMEPAD_BUTTON_DPAD_DOWN),
                    left = pad(SDL_GAMEPAD_BUTTON_DPAD_LEFT), right = pad(SDL_GAMEPAD_BUTTON_DPAD_RIGHT),
                    lt = kPadLeftTrigger, rt = kPadRightTrigger;
    // The game's layouts: its Controller screen's A-D exec config/style_h, _e, _f, _g.cfg (FE_ApplyControlSettings;
    // style_a-d are another platform's). There R1/L1 = the triggers, R2/L2 = Black/White (RB/LB here), JOY6 = A,
    // CIRCLE = B, SQUARE = X, TRIANGLE = Y; their D-pad lean is the plain lean.
    switch (preset) {
        case PadPreset::Modern:
        case PadPreset::Count:
            set(A::Fire, rt), set(A::Aim, lt), set(A::Grenade, rb), set(A::Jump, a), set(A::Crouch, bb);
            set(A::UseReload, x), set(A::NextWeapon, y), set(A::Sprint, ls), set(A::Melee, rs);
            set(A::LeanLeft, left), set(A::LeanRight, right);
            break;
        case PadPreset::LayoutA:  // style_h, the game's default
            set(A::Fire, rt), set(A::Aim, lt), set(A::Use, rb), set(A::Reload, lb), set(A::Sprint, ls);
            set(A::Grenade, x), set(A::Jump, y), set(A::Crouch, bb), set(A::Melee, a);
            set(A::NextWeapon, up), set(A::LeanLeft, left), set(A::LeanRight, right), set(A::NextWeapon, down);
            break;
        case PadPreset::LayoutB:  // style_e
            set(A::Fire, rt), set(A::Grenade, lt), set(A::RaiseStance, rb), set(A::Crouch, lb), set(A::Aim, rs);
            set(A::Sprint, ls), set(A::UseReload, x), set(A::NextWeapon, y), set(A::Melee, bb), set(A::Jump, a);
            set(A::RaiseStance, up), set(A::LeanLeft, left), set(A::LeanRight, right), set(A::Crouch, down);
            break;
        case PadPreset::LayoutC:  // style_f
            set(A::Fire, rt), set(A::Grenade, lt), set(A::Use, rb), set(A::NextWeapon, lb), set(A::Aim, rs);
            set(A::Sprint, ls), set(A::Reload, x), set(A::Jump, y), set(A::Crouch, bb), set(A::Melee, a);
            set(A::NextWeapon, up), set(A::LeanLeft, left), set(A::LeanRight, right), set(A::PrevWeapon, down);
            break;
        case PadPreset::LayoutD:  // style_g
            set(A::Fire, rt), set(A::Grenade, lt), set(A::Reload, rb), set(A::NextWeapon, lb), set(A::Aim, rs);
            set(A::Crouch, ls), set(A::Melee, x), set(A::Use, y), set(A::Sprint, bb), set(A::Jump, a);
            set(A::RaiseStance, up), set(A::LeanLeft, left), set(A::LeanRight, right), set(A::Crouch, down);
            break;
    }
    return b;
}

const char* pad_preset_token(PadPreset preset) { return kPresets[static_cast<size_t>(preset)].token; }
const char* pad_preset_label(PadPreset preset) { return kPresets[static_cast<size_t>(preset)].label; }

std::optional<PadPreset> pad_preset_from_token(std::string_view token) {
    for (size_t p = 0; p < std::size(kPresets); ++p)
        if (token == kPresets[p].token) return static_cast<PadPreset>(p);
    return std::nullopt;
}

std::optional<PadPreset> matching_pad_preset(const Bindings& bindings) {
    for (size_t p = 0; p < std::size(kPresets); ++p)
        if (pad_preset(static_cast<PadPreset>(p)) == bindings) return static_cast<PadPreset>(p);
    return std::nullopt;
}

bool parse_bindings(std::string_view text, Bindings& bindings, std::string* error, bool onto) {
    if (!onto) bindings = default_bindings();
    return parse_into(text, bindings, error, false);
}

bool parse_pad_bindings(std::string_view text, Bindings& bindings, std::string* error, bool onto) {
    if (!onto) bindings = default_pad_bindings();
    return parse_into(text, bindings, error, true);
}

namespace {

bool parse_into(std::string_view text, Bindings& bindings, std::string* error, bool pad_inputs) {
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
        // Older names: right-stick-click is the toggled aim (the default layout's R3); dpad-down did nothing.
        const std::string_view token = name == "right-stick-click" ? std::string_view("aim-toggle") : name;
        const auto action = std::find_if(std::begin(kActions), std::end(kActions),
                                         [&](const ActionInfo& a) { return token == a.token; });
        const std::optional<InputCode> input = input_from_token(word.substr(equals + 1));
        if (action == std::end(kActions)) {
            // Removed actions of older settings: the look keys (0.6c), D-pad down (0.6d).
            if (name.substr(0, 5) == "look-" || name == "dpad-down") continue;
            fail(word, "unknown action");
            continue;
        }
        if (!input || (*input && is_pad_input(*input) != pad_inputs)) {
            fail(word, pad_inputs ? "unknown pad button" : "unknown key");
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

}  // namespace

std::string format_bindings(const Bindings& bindings) { return format_against(bindings, default_bindings()); }
std::string format_pad_bindings(const Bindings& bindings) { return format_against(bindings, default_pad_bindings()); }

namespace {

std::string format_against(const Bindings& bindings, const Bindings& defaults) {
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

}  // namespace

const char* action_token(Action action) { return kActions[static_cast<size_t>(action)].token; }

std::optional<Action> action_from_token(std::string_view token) {
    for (size_t a = 0; a < kActionCount; ++a)
        if (token == kActions[a].token) return static_cast<Action>(a);
    return std::nullopt;
}

const char* action_label(Action action) { return kActions[static_cast<size_t>(action)].label; }

std::optional<InputCode> pad_input_from_token(std::string_view token) {
    for (const PadInputInfo& info : kPadInputs)
        if (token == info.token) return info.input;
    return std::nullopt;
}

std::string input_name(InputCode input) {
    if (const PadInputInfo* info = pad_input_info(input)) return info->name;
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
    for (SDL_Scancode code : {SDL_SCANCODE_ESCAPE, SDL_SCANCODE_TAB, SDL_SCANCODE_GRAVE, SDL_SCANCODE_F10,
                              SDL_SCANCODE_F11, SDL_SCANCODE_F12})  // ` opens the PC menu
        if (input == key(code)) return true;
    return input == pad(SDL_GAMEPAD_BUTTON_START) || input == pad(SDL_GAMEPAD_BUTTON_BACK) ||
           input == pad(SDL_GAMEPAD_BUTTON_GUIDE);
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
    if (const PadInputInfo* info = pad_input_info(input)) return std::string("@") + info->token;
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

void set_bindings(const Bindings& bindings) {
    std::lock_guard lock(g_bindings_mutex);
    g_bindings = bindings;
}

Bindings bindings() {
    std::lock_guard lock(g_bindings_mutex);
    return g_bindings;
}

void set_pad_bindings(const Bindings& bindings) {
    std::lock_guard lock(g_bindings_mutex);
    g_pad_bindings = bindings;
}

Bindings pad_bindings() {
    std::lock_guard lock(g_bindings_mutex);
    return g_pad_bindings;
}

}  // namespace input
