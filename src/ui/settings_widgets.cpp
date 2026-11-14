#include "settings_widgets.h"

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

}  // namespace ui
