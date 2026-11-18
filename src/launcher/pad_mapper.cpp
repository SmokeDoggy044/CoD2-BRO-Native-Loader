#include "pad_mapper.h"

#include <algorithm>
#include <cmath>

#include "imgui.h"
#include "log.h"

namespace launcher {

namespace {

// SDL's testcontroller art (zlib-style license, third_party/sdl_gamepad_art/README.md): PNGs as C arrays.
#include "gamepad_back.h"
#include "gamepad_button.h"
#include "gamepad_face_abxy.h"
#include "gamepad_face_bayx.h"
#include "gamepad_face_sony.h"
#include "gamepad_front.h"

// The pictures' size and where each control is on them (SDL's gamepadutils.c button_positions / axis_positions; the
// paddles are on the back picture).
constexpr float kWidth = 512, kHeight = 317;
constexpr float kBackScale = 0.42f;  // the back picture beside the front one, smaller
constexpr float kFaceX = 363, kFaceY = 118;

struct Spot {
    input::InputCode input;
    float x, y;
    bool back;
};

constexpr input::InputCode button(SDL_GamepadButton b) { return static_cast<input::InputCode>(input::kPadButton + b); }

constexpr Spot kSpots[] = {
    {button(SDL_GAMEPAD_BUTTON_SOUTH), 413, 190, false},
    {button(SDL_GAMEPAD_BUTTON_EAST), 456, 156, false},
    {button(SDL_GAMEPAD_BUTTON_WEST), 372, 159, false},
    {button(SDL_GAMEPAD_BUTTON_NORTH), 415, 127, false},
    {button(SDL_GAMEPAD_BUTTON_LEFT_STICK), 98, 177, false},
    {button(SDL_GAMEPAD_BUTTON_RIGHT_STICK), 331, 254, false},
    {button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER), 102, 65, false},
    {button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER), 421, 61, false},
    {input::kPadLeftTrigger, 116, 22, false},
    {input::kPadRightTrigger, 400, 22, false},
    {button(SDL_GAMEPAD_BUTTON_DPAD_UP), 179, 213, false},
    {button(SDL_GAMEPAD_BUTTON_DPAD_DOWN), 179, 274, false},
    {button(SDL_GAMEPAD_BUTTON_DPAD_LEFT), 141, 242, false},
    {button(SDL_GAMEPAD_BUTTON_DPAD_RIGHT), 211, 242, false},
    {button(SDL_GAMEPAD_BUTTON_BACK), 199, 157, false},
    {button(SDL_GAMEPAD_BUTTON_START), 314, 157, false},
    {button(SDL_GAMEPAD_BUTTON_MISC1), 257, 199, false},
    {button(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1), 157, 160, true},
    {button(SDL_GAMEPAD_BUTTON_LEFT_PADDLE1), 355, 160, true},
    {button(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2), 157, 200, true},
    {button(SDL_GAMEPAD_BUTTON_LEFT_PADDLE2), 355, 200, true},
};

constexpr int kTriggerHeld = 8000;  // SDL axis units, as the game's default threshold

SDL_Texture* load_png(SDL_Renderer* renderer, const unsigned char* data, unsigned int size) {
    SDL_IOStream* io = SDL_IOFromConstMem(data, size);
    SDL_Surface* surface = io ? SDL_LoadPNG_IO(io, true) : nullptr;
    if (!surface) {
        LOG_WARN("launcher", "controller picture: %s", SDL_GetError());
        return nullptr;
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    return texture;
}

ImTextureID id(SDL_Texture* texture) { return reinterpret_cast<ImTextureID>(texture); }

// The actions the picture can give a button (moving is the left stick's).
bool mappable(input::Action action) {
    return action != input::Action::MoveForward && action != input::Action::MoveBack &&
           action != input::Action::MoveLeft && action != input::Action::MoveRight;
}

}  // namespace

PadMapper::PadMapper(SDL_Renderer* renderer) : renderer_(renderer) {
    front_ = load_png(renderer, gamepad_front_png, gamepad_front_png_len);
    back_ = load_png(renderer, gamepad_back_png, gamepad_back_png_len);
    faces_[0] = load_png(renderer, gamepad_face_abxy_png, gamepad_face_abxy_png_len);
    faces_[1] = load_png(renderer, gamepad_face_sony_png, gamepad_face_sony_png_len);
    faces_[2] = load_png(renderer, gamepad_face_bayx_png, gamepad_face_bayx_png_len);
    highlight_ = load_png(renderer, gamepad_button_png, gamepad_button_png_len);
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        LOG_WARN("launcher", "gamepads: %s", SDL_GetError());
        return;
    }
    int count = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
        if (count > 0) pad_ = SDL_OpenGamepad(ids[0]);
        SDL_free(ids);
    }
}

PadMapper::~PadMapper() {
    for (SDL_Texture* texture : {front_, back_, faces_[0], faces_[1], faces_[2], highlight_})
        if (texture) SDL_DestroyTexture(texture);
    if (pad_) SDL_CloseGamepad(pad_);
}

bool PadMapper::event(const SDL_Event& event) {
    switch (event.type) {
        case SDL_EVENT_GAMEPAD_ADDED:
            if (!pad_) pad_ = SDL_OpenGamepad(event.gdevice.which);
            return false;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (pad_ && SDL_GetGamepadID(pad_) == event.gdevice.which) {
                SDL_CloseGamepad(pad_);
                pad_ = nullptr;
                int count = 0;
                if (SDL_JoystickID* ids = SDL_GetGamepads(&count)) {
                    if (count > 0) pad_ = SDL_OpenGamepad(ids[0]);
                    SDL_free(ids);
                }
            }
            return false;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN: {
            if (!shown_) return false;
            const auto code = button(static_cast<SDL_GamepadButton>(event.gbutton.button));
            if (input::is_reserved(code)) return false;  // Start / Back / Guide keep their jobs
            selected_ = code;
            return true;
        }
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            if (!shown_ || event.gaxis.value < 16000) return false;
            if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) selected_ = input::kPadLeftTrigger;
            else if (event.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) selected_ = input::kPadRightTrigger;
            else return false;
            return true;
        default: return false;
    }
}

PadMapper::Style PadMapper::style(const std::string& glyphs) const {
    if (glyphs == "xbox") return Style::Xbox;
    if (glyphs == "playstation") return Style::PlayStation;
    if (glyphs == "nintendo") return Style::Nintendo;
    if (pad_) {
        switch (SDL_GetGamepadType(pad_)) {
            case SDL_GAMEPAD_TYPE_PS3:
            case SDL_GAMEPAD_TYPE_PS4:
            case SDL_GAMEPAD_TYPE_PS5: return Style::PlayStation;
            case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
            case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
            case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
            case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return Style::Nintendo;
            default: break;
        }
    }
    return Style::Xbox;
}

// A button by the names printed on that style of pad (bindings.h's names are the Xbox ones).
std::string PadMapper::button_name(input::InputCode input, Style style) const {
    struct Names { input::InputCode input; const char* ps; const char* nintendo; };
    static constexpr Names kNames[] = {
        {button(SDL_GAMEPAD_BUTTON_SOUTH), "Cross", "B"},
        {button(SDL_GAMEPAD_BUTTON_EAST), "Circle", "A"},
        {button(SDL_GAMEPAD_BUTTON_WEST), "Square", "Y"},
        {button(SDL_GAMEPAD_BUTTON_NORTH), "Triangle", "X"},
        {button(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER), "L1", "L"},
        {button(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER), "R1", "R"},
        {input::kPadLeftTrigger, "L2", "ZL"},
        {input::kPadRightTrigger, "R2", "ZR"},
        {button(SDL_GAMEPAD_BUTTON_LEFT_STICK), "L3", "Left stick click"},
        {button(SDL_GAMEPAD_BUTTON_RIGHT_STICK), "R3", "Right stick click"},
        {button(SDL_GAMEPAD_BUTTON_BACK), "Share", "-"},
        {button(SDL_GAMEPAD_BUTTON_START), "Options", "+"},
    };
    if (style != Style::Xbox)
        for (const Names& n : kNames)
            if (n.input == input) return style == Style::PlayStation ? n.ps : n.nintendo;
    return input::input_name(input);
}

bool PadMapper::held(input::InputCode input) const {
    if (!pad_) return false;
    if (input == input::kPadLeftTrigger) return SDL_GetGamepadAxis(pad_, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > kTriggerHeld;
    if (input == input::kPadRightTrigger) return SDL_GetGamepadAxis(pad_, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > kTriggerHeld;
    return SDL_GetGamepadButton(pad_, static_cast<SDL_GamepadButton>(input - input::kPadButton));
}

void PadMapper::new_frame() {
    shown_ = false;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
}

bool PadMapper::draw(input::Bindings& pad, const std::string& glyphs) {
    shown_ = true;
    if (pad_) ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
    bool changed = false;
    const Style shown_style = style(glyphs);

    // The preset and the controller.
    const auto preset = input::matching_pad_preset(pad);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.3f);
    if (ImGui::BeginCombo("Layout", preset ? input::pad_preset_label(*preset) : "Custom")) {
        for (size_t p = 0; p < static_cast<size_t>(input::PadPreset::Count); ++p) {
            const auto candidate = static_cast<input::PadPreset>(p);
            if (ImGui::Selectable(input::pad_preset_label(candidate), preset == candidate)) {
                pad = input::pad_preset(candidate);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s", pad_ ? SDL_GetGamepadName(pad_) : "No controller connected");
    ImGui::TextWrapped("Press a button on your controller or click one in the picture, then choose what it does.");

    // Left: the front, under it the back (paddles) and the selected control's action. Right: every binding.
    const float avail = ImGui::GetContentRegionAvail().x;
    const float gap = ImGui::GetStyle().ItemSpacing.x * 2;
    const float left_w = avail * 0.56f;
    ImGui::BeginGroup();
    // As large as the width and the tab's height allow (front, a gap, the back).
    const float tall = ImGui::GetContentRegionAvail().y - ImGui::GetStyle().ItemSpacing.y * 2;
    const float scale = std::min({ImGui::GetIO().FontGlobalScale, left_w / kWidth, tall / (kHeight * (1 + kBackScale))});
    const ImVec2 front_at = ImGui::GetCursorScreenPos();
    const float front_w = kWidth * scale, front_h = kHeight * scale;
    const float back_s = scale * kBackScale;
    const ImVec2 back_at(front_at.x, front_at.y + front_h + ImGui::GetStyle().ItemSpacing.y);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    if (front_) draw->AddImage(id(front_), front_at, ImVec2(front_at.x + front_w, front_at.y + front_h));
    if (back_) draw->AddImage(id(back_), back_at, ImVec2(back_at.x + kWidth * back_s, back_at.y + kHeight * back_s));
    if (SDL_Texture* face = faces_[static_cast<int>(shown_style)]) {
        float fw = 0, fh = 0;
        SDL_GetTextureSize(face, &fw, &fh);
        const ImVec2 at(front_at.x + kFaceX * scale, front_at.y + kFaceY * scale);
        draw->AddImage(id(face), at, ImVec2(at.x + fw * scale, at.y + fh * scale));
    }

    // Each control: lit while held (green) or selected (amber); hover names it, a click selects it.
    for (const Spot& spot : kSpots) {
        const float s = spot.back ? back_s : scale;
        const ImVec2 origin = spot.back ? back_at : front_at;
        const ImVec2 centre(origin.x + spot.x * s, origin.y + spot.y * s);
        const float half = 25.0f * s * (spot.back ? 1.6f : 1.0f);
        const bool lit = held(spot.input), chosen = selected_ == spot.input;
        if (highlight_ && (lit || chosen))
            draw->AddImage(id(highlight_), ImVec2(centre.x - half, centre.y - half), ImVec2(centre.x + half, centre.y + half),
                           ImVec2(0, 0), ImVec2(1, 1), lit ? IM_COL32(10, 255, 21, 255) : IM_COL32(255, 190, 40, 255));
        ImGui::SetCursorScreenPos(ImVec2(centre.x - half * 0.8f, centre.y - half * 0.8f));
        ImGui::PushID(spot.input);
        if (ImGui::InvisibleButton("spot", ImVec2(half * 1.6f, half * 1.6f))) selected_ = spot.input;
        if (ImGui::IsItemHovered()) {
            std::string actions;
            for (size_t a = 0; a < input::kActionCount; ++a)
                for (input::InputCode in : pad.inputs[a])
                    if (in == spot.input)
                        (actions += actions.empty() ? "" : ", ") += input::action_label(static_cast<input::Action>(a));
            const bool fixed = input::is_reserved(spot.input);
            ImGui::SetTooltip("%s: %s", button_name(spot.input, shown_style).c_str(),
                              fixed ? (spot.input == button(SDL_GAMEPAD_BUTTON_START) ? "pause" : "objectives")
                                    : actions.empty() ? "nothing" : actions.c_str());
        }
        ImGui::PopID();
    }

    // The sticks: where each one is pushed (past a small dead zone), a marker over it moving with it.
    if (pad_ && highlight_) {
        struct Stick { SDL_GamepadAxis x, y; float cx, cy; };
        static constexpr Stick kSticks[] = {{SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY, 98, 177},
                                            {SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY, 331, 254}};
        for (const Stick& stick : kSticks) {
            const float dx = SDL_GetGamepadAxis(pad_, stick.x) / 32767.0f, dy = SDL_GetGamepadAxis(pad_, stick.y) / 32767.0f;
            if (dx * dx + dy * dy < 0.25f * 0.25f) continue;
            const float reach = 22.0f;  // picture pixels: about the stick's rim
            const ImVec2 at(front_at.x + (stick.cx + dx * reach) * scale, front_at.y + (stick.cy + dy * reach) * scale);
            const float half = 15.0f * scale;
            draw->AddImage(id(highlight_), ImVec2(at.x - half, at.y - half), ImVec2(at.x + half, at.y + half), ImVec2(0, 0),
                           ImVec2(1, 1), IM_COL32(10, 255, 21, 255));
        }
    }

    // Beside the back picture: the selected control and its action.
    ImGui::SetCursorScreenPos(ImVec2(back_at.x + kWidth * back_s + gap, back_at.y));
    ImGui::BeginGroup();
    if (selected_ && !input::is_reserved(selected_)) {
        int current = -1;
        for (size_t a = 0; a < input::kActionCount && current < 0; ++a)
            for (input::InputCode in : pad.inputs[a])
                if (in == selected_) current = static_cast<int>(a);
        ImGui::TextUnformatted(button_name(selected_, shown_style).c_str());
        ImGui::SetNextItemWidth(left_w - kWidth * back_s - gap);
        if (ImGui::BeginCombo("##action",
                              current >= 0 ? input::action_label(static_cast<input::Action>(current)) : "Nothing")) {
            if (ImGui::Selectable("Nothing", current < 0) && current >= 0) {
                const auto action = static_cast<input::Action>(current);
                for (input::InputCode& in : pad[action])
                    if (in == selected_) in = 0;
                if (!pad[action][0]) std::swap(pad[action][0], pad[action][1]);  // the first input is the one shown
                changed = true;
            }
            for (size_t a = 0; a < input::kActionCount; ++a) {
                const auto action = static_cast<input::Action>(a);
                if (!mappable(action)) continue;
                if (ImGui::Selectable(input::action_label(action), current == static_cast<int>(a)) &&
                    current != static_cast<int>(a)) {
                    // Its first free place, else its second; the button leaves any other action (bind).
                    input::bind(pad, action, pad[action][0] ? 1 : 0, selected_);
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
    } else {
        ImGui::TextDisabled("No button selected");
    }
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos(front_at);
    ImGui::Dummy(ImVec2(left_w, front_h + ImGui::GetStyle().ItemSpacing.y + kHeight * back_s));
    ImGui::EndGroup();

    // Right: everything at a glance (scrolls by itself).
    ImGui::SameLine(0, gap);
    ImGui::BeginChild("pad_table", ImVec2(0, front_h + ImGui::GetStyle().ItemSpacing.y + kHeight * back_s));
    if (ImGui::BeginTable("pad_bindings", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        for (size_t a = 0; a < input::kActionCount; ++a) {
            const auto action = static_cast<input::Action>(a);
            if (!mappable(action)) continue;
            std::string buttons;
            for (input::InputCode in : pad[action])
                if (in) (buttons += buttons.empty() ? "" : ", ") += button_name(in, shown_style);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(input::action_label(action));
            ImGui::TableNextColumn();
            if (buttons.empty())
                ImGui::TextDisabled("none");
            else
                ImGui::TextUnformatted(buttons.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    return changed;
}

}  // namespace launcher
