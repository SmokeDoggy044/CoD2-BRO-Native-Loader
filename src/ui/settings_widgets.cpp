#include "settings_widgets.h"

#include <SDL3/SDL_events.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "imgui.h"

namespace ui {
namespace {

using platform::SettingDef;
using platform::SettingType;
using platform::SettingUi;

std::vector<std::string> split(const char* text) {
    std::vector<std::string> out;
    if (!text) return out;
    const std::string_view s = text;
    for (size_t p = 0; p <= s.size();) {
        const size_t bar = std::min(s.find('|', p), s.size());
        out.emplace_back(s.substr(p, bar - p));
        p = bar + 1;
    }
    return out;
}

bool is_on(const std::string& value) { return value == "on" || value == "true"; }

// A parent condition: "key" (on) or "key=value".
bool parent_on(const platform::Settings& values, std::string_view parent) {
    const size_t eq = parent.find('=');
    if (eq == std::string_view::npos) return is_on(setting_value(values, parent));
    return setting_value(values, parent.substr(0, eq)) == parent.substr(eq + 1);
}

int depth(std::string_view key) {
    int d = 0;
    for (const SettingUi* ui = platform::setting_ui(key); ui && ui->parent; ++d) {
        std::string_view parent = ui->parent;
        ui = platform::setting_ui(parent.substr(0, parent.find('=')));
    }
    return d;
}

bool standard_widget(const SettingDef& def, const SettingUi& ui, platform::Settings& values) {
    const std::string value = setting_value(values, def.key);
    bool changed = false;
    switch (def.type) {
    case SettingType::Flag:
    case SettingType::Switch: {
        bool on = is_on(value);
        if ((changed = ImGui::Checkbox(ui.label, &on)))
            put_setting(values, def.key, def.type == SettingType::Flag ? (on ? "true" : "false") : (on ? "on" : "off"));
        break;
    }
    case SettingType::Choice: {
        const std::vector<std::string> choices = split(def.choices);
        if (ui.checkbox_value) {
            bool on = value == ui.checkbox_value;
            if ((changed = ImGui::Checkbox(ui.label, &on))) {
                std::string other;
                for (const std::string& c : choices)
                    if (c != ui.checkbox_value) other = c;
                put_setting(values, def.key, on ? ui.checkbox_value : other);
            }
            break;
        }
        std::vector<std::string> labels = split(ui.choice_labels);
        if (labels.size() != choices.size()) labels = choices;
        size_t index = 0;
        while (index < choices.size() && choices[index] != value) ++index;
        const char* preview = index < labels.size() ? labels[index].c_str() : value.c_str();
        if (ImGui::BeginCombo(ui.label, preview)) {
            for (size_t i = 0; i < choices.size(); ++i)
                if (ImGui::Selectable(labels[i].c_str(), i == index) && i != index) {
                    put_setting(values, def.key, choices[i]);
                    changed = true;
                }
            ImGui::EndCombo();
        }
        break;
    }
    case SettingType::Int: {
        int n = std::atoi(value.c_str());
        const int low = static_cast<int>(def.min), high = static_cast<int>(def.max);
        if (high - low > 1000)
            changed = ImGui::InputInt(ui.label, &n, 1, 10);
        else
            changed = ImGui::SliderInt(ui.label, &n, low, high, ui.format ? ui.format : "%d",
                                       ImGuiSliderFlags_AlwaysClamp);
        if (changed) put_setting(values, def.key, std::to_string(std::clamp(n, low, high)));
        break;
    }
    case SettingType::Float: {
        float x = value.empty() ? static_cast<float>(def.min) : std::strtof(value.c_str(), nullptr);
        ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp;
        if (ui.logarithmic) flags |= ImGuiSliderFlags_Logarithmic;
        if ((changed = ImGui::SliderFloat(ui.label, &x, static_cast<float>(def.min), static_cast<float>(def.max),
                                          ui.format ? ui.format : "%.2f", flags)))
            put_setting(values, def.key, number_text(x));
        break;
    }
    case SettingType::Text:
    case SettingType::Path: {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
        if ((changed = ImGui::InputText(ui.label, buffer, sizeof(buffer)))) {
            std::string text = buffer;
            if (platform::check_setting(def, text)) put_setting(values, def.key, text);
        }
        break;
    }
    case SettingType::Custom: break;
    }
    return changed;
}

}  // namespace

std::string number_text(double value) {
    // Sliders give floats: keep what they show (2-3 decimals), not 0.30000001.
    char text[32];
    std::snprintf(text, sizeof(text), "%.4g", std::abs(value) < 1e-6 ? 0.0 : value);
    return text;
}

std::string setting_value(const platform::Settings& values, std::string_view key) {
    if (auto v = values.get(std::string(key))) return *v;
    const SettingDef* def = platform::find_setting(key);
    return def ? def->default_value : "";
}

void put_setting(platform::Settings& values, std::string_view key, const std::string& value) {
    const SettingDef* def = platform::find_setting(key);
    const bool is_default = def && (def->type == SettingType::Float || def->type == SettingType::Int
                                        ? *def->default_value &&
                                              std::strtod(value.c_str(), nullptr) == std::strtod(def->default_value, nullptr)
                                        : value == def->default_value);
    if (is_default)
        values.erase(std::string(key));
    else
        values.set(std::string(key), value);
}

bool setting_enabled(const platform::Settings& values, std::string_view key) {
    for (const SettingUi* ui = platform::setting_ui(key); ui && ui->parent;) {
        const std::string_view parent = ui->parent;
        if (!parent_on(values, parent)) return false;
        ui = platform::setting_ui(parent.substr(0, parent.find('=')));
    }
    return true;
}

void help_marker(const char* text) {
    if (!text || !*text) return;
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

bool draw_settings(std::initializer_list<platform::SettingGroup> groups, platform::Settings& values,
                   const DrawOptions& options) {
    bool any = false;
    const char* section = nullptr;     // the section the table is in
    const char* shown = nullptr;       // the last section title drawn
    for (const SettingUi& ui : platform::setting_uis()) {
        if (ui.section) section = ui.section;
        const SettingDef* def = platform::find_setting(ui.key);
        if (!def || std::find(groups.begin(), groups.end(), def->group) == groups.end()) continue;
        if ((def->flags & options.require_flags) != options.require_flags) continue;
        if (section && section != shown) {
            ImGui::SeparatorText(section);
            shown = section;
        }
        const int indent = depth(ui.key);
        const bool enabled = setting_enabled(values, ui.key);
        ImGui::PushID(ui.key);
        for (int i = 0; i < indent; ++i) ImGui::Indent();
        ImGui::BeginDisabled(!enabled);
        bool changed = false;
        if (!(options.custom && options.custom(*def, ui, values, changed))) {
            changed = standard_widget(*def, ui, values);
            help_marker(ui.tip);
        }
        any |= changed;
        ImGui::EndDisabled();
        for (int i = 0; i < indent; ++i) ImGui::Unindent();
        ImGui::PopID();
    }
    return any;
}

bool key_table_event(KeyTable& table, input::Bindings& bindings, const void* raw, bool& changed) {
    if (!table.waiting) return false;
    const SDL_Event& event = *static_cast<const SDL_Event*>(raw);
    input::InputCode input = 0;
    switch (event.type) {
        case SDL_EVENT_KEY_DOWN:
            if (event.key.repeat) return true;
            if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                table.waiting.reset();
                return true;
            }
            input = static_cast<input::InputCode>(event.key.scancode);
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.button < 1 || event.button.button > 5) return true;
            input = static_cast<input::InputCode>(input::kMouseButton + event.button.button);
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            if (event.wheel.y == 0.0f) return true;
            input = event.wheel.y > 0.0f ? input::kWheelUp : input::kWheelDown;
            break;
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            return true;
        default:
            return false;
    }
    if (input::is_reserved(input)) {
        table.notice = input::input_name(input) + " is kept for the menus and the game's own keys.";
    } else {
        table.notice.clear();
        for (size_t a = 0; a < input::kActionCount; ++a) {
            const auto other = static_cast<input::Action>(a);
            if (other == table.waiting->first) continue;
            for (input::InputCode had : bindings[other])
                if (had == input)
                    table.notice = input::input_name(input) + " moved here from " + input::action_label(other) + ".";
        }
        input::bind(bindings, table.waiting->first, table.waiting->second, input);
        changed = true;
    }
    table.waiting.reset();
    return true;
}

bool draw_key_table(KeyTable& table, input::Bindings& bindings) {
    bool changed = false;
    ImGui::TextWrapped("Click a key, then press the new key or mouse button (Esc cancels, right-click clears it).");
    help_marker("Each key keeps its action whatever controller style is chosen in the game's options (styles change "
                "the gamepad only). The arrow keys, Enter and Backspace always work in the menus. Fixed: Esc (pause), "
                "Tab (objectives), ` (this menu in the game), F10 (frees the mouse), F11, F12.");
    if (!table.notice.empty()) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1), "%s", table.notice.c_str());
    if (ImGui::BeginTable("keys", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableSetupColumn("Action");
        ImGui::TableSetupColumn("Key");
        ImGui::TableSetupColumn("Other key");
        ImGui::TableHeadersRow();
        for (size_t a = 0; a < input::kActionCount; ++a) {
            const auto action = static_cast<input::Action>(a);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(input::action_label(action));
            const bool unbound = !bindings[action][0] && !bindings[action][1];
            for (size_t slot = 0; slot < input::kInputsPerAction; ++slot) {
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(a * input::kInputsPerAction + slot));
                const bool waiting = table.waiting && table.waiting->first == action && table.waiting->second == slot;
                const std::string name = input::input_name(bindings[action][slot]);
                const std::string label = waiting ? "Press a key..." : name.empty() ? "-" : name;
                if (unbound && slot == 0) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.6f, 0.3f, 1));
                if (ImGui::Button(label.c_str(), ImVec2(-FLT_MIN, 0))) {
                    table.waiting.emplace(action, slot);
                    table.notice.clear();
                }
                if (unbound && slot == 0) ImGui::PopStyleColor();
                if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                    input::bind(bindings, action, slot, 0);
                    table.waiting.reset();
                    changed = true;
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    if (ImGui::Button("Default keys")) {
        bindings = input::default_bindings();
        table.waiting.reset();
        table.notice.clear();
        changed = true;
    }
    return changed;
}

}  // namespace ui
