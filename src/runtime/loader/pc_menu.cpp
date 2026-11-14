// The in-game PC menu (pc_menu.h). Runs on the title's thread inside the renderer's
// present(), so it only reads and asks; the title applies changes at its frame end.

#include "pc_menu.h"

#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

#include "devices.h"
#include "enhanced_graphics.h"
#include "fe_pc.h"
#include "float_env.h"
#include "imgui.h"
#include "log.h"
#include "patches.h"
#include "settings.h"
#include "settings_schema.h"
#include "settings_widgets.h"
#include "snd_env.h"
#include "sound_engine.h"

namespace loader {

namespace {

constexpr int kGameFov = 70;  // cg_fov's default (cgame cvarTable)
platform::Settings g_on_open;  // the live settings when the menu opened (what closing saves against)

// The settings the menu changes while playing (platform::kLive), as settings.ini text, from what runs now.
platform::Settings live_values() {
    platform::Settings v;
    ui::put_setting(v, "graphics", render::graphics_enhanced() ? "enhanced" : "original");
    platform::set_combined_setting(v, "graphics-features", render::graphics_features().format());
    ui::put_setting(v, "audio", audio::enhanced() ? "enhanced" : "original");
    platform::set_combined_setting(v, "enhanced", audio::enhanced_features().format());
    platform::set_combined_setting(v, "volume", audio::category_volumes().format());
    platform::set_combined_setting(v, "reverb", game::SndEnv_Tuning().format());
    if (const float fov = field_of_view(); fov > 0) ui::put_setting(v, "fov", std::to_string(std::lround(fov)));
    ui::put_setting(v, "mouse-sensitivity", ui::number_text(mouse_sensitivity()));
    return v;
}

std::string combined(const platform::Settings& v, const char* name) {
    return platform::combined_setting(v, name).value_or("");
}

// Applies what differs between two live_values() forms.
void apply_live(const platform::Settings& before, const platform::Settings& after) {
    const auto differs = [&](const char* key) { return before.get(key) != after.get(key); };
    const auto differs_combined = [&](const char* name) { return combined(before, name) != combined(after, name); };
    if (differs("graphics") || differs_combined("graphics-features"))
        render::set_graphics(ui::setting_value(after, "graphics") == "enhanced",
                             render::GraphicsFeatures::parse(combined(after, "graphics-features")));
    if (differs("audio")) audio::set_enhanced(ui::setting_value(after, "audio") == "enhanced");
    if (differs_combined("enhanced")) audio::set_enhanced_features(audio::EnhancedFeatures::parse(combined(after, "enhanced")));
    if (differs_combined("volume")) audio::set_category_volumes(audio::CategoryVolumes::parse(combined(after, "volume")));
    if (differs_combined("reverb")) game::SndEnv_SetTuning(audio::ReverbTuning::parse(combined(after, "reverb")));
    if (differs("fov")) set_field_of_view(std::strtof(ui::setting_value(after, "fov").c_str(), nullptr));
    if (differs("mouse-sensitivity"))
        set_mouse_sensitivity(std::strtof(ui::setting_value(after, "mouse-sensitivity").c_str(), nullptr));
}

// The field of view: the game's own value until one is chosen, campaign only.
bool fov_widget(const platform::SettingUi& ui, platform::Settings& values, bool& changed) {
    if (!field_of_view_supported()) {
        ImGui::TextDisabled("Field of view: campaign only for now.");
        return true;
    }
    const float game = game_field_of_view();
    const std::optional<std::string> asked = values.get("fov");
    int fov = asked ? std::atoi(asked->c_str()) : game > 0 ? static_cast<int>(std::lround(game)) : kGameFov;
    if (ImGui::SliderInt(ui.label, &fov, 50, 120, "%d", ImGuiSliderFlags_AlwaysClamp)) {
        ui::put_setting(values, "fov", std::to_string(fov));
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Default") && asked) {
        values.set("fov", std::to_string(kGameFov));  // asked for again: back to the game's own
        changed = true;
    }
    ui::help_marker(ui.tip);
    if (game <= 0) ImGui::TextDisabled("Applies once a mission is running.");
    return true;
}

void live_tab(std::initializer_list<platform::SettingGroup> groups) {
    const platform::Settings before = live_values();
    platform::Settings values = before;
    ui::DrawOptions options;
    options.require_flags = platform::kLive;
    options.custom = [](const platform::SettingDef& def, const platform::SettingUi& ui, platform::Settings& v,
                        bool& changed) { return std::string_view(def.key) == "fov" && fov_widget(ui, v, changed); };
    ImGui::PushItemWidth(ImGui::GetFontSize() * 14);
    if (ui::draw_settings(groups, values, options)) apply_live(before, values);
    ImGui::PopItemWidth();
}

}  // namespace

bool pc_menu() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.85f);
    bool open = true;
    ImGui::Begin("PC settings", &open,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoMove);
    ImGui::TextDisabled("The game keeps running. ` or Escape closes this menu; changes are saved.");
    if (ImGui::BeginTabBar("pc_menu_tabs")) {
        using G = platform::SettingGroup;
        if (ImGui::BeginTabItem("General")) {
            live_tab({G::Display, G::Controls});
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Graphics")) {
            live_tab({G::Graphics});
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Sound")) {
            live_tab({G::Sound});
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    if (ImGui::Button("Close")) open = false;
    ImGui::End();
    return open;
}

void pc_menu_shown(bool open) {
    input::set_game_input_blocked(open);
    if (open) {
        g_on_open = live_values();
        return;
    }
    // What changed goes into settings.ini, one line per setting (the defaults removed).
    const platform::Settings now = live_values();
    platform::Settings file = platform::load_settings();
    std::string saved;
    for (const platform::SettingDef& def : platform::setting_defs()) {
        if (!(def.flags & platform::kLive) || now.get(def.key) == g_on_open.get(def.key)) continue;
        if (const std::optional<std::string> value = now.get(def.key))
            file.set(def.key, *value);
        else
            file.erase(def.key);
        saved += std::string(saved.empty() ? "" : " ") + def.key + "=" + ui::setting_value(now, def.key);
    }
    if (saved.empty()) return;
    if (platform::save_settings(file))
        LOG_INFO("loader", "PC menu saved: %s", saved.c_str());
    else
        LOG_WARN("loader", "cannot save the PC menu's settings to %s", platform::settings_path().string().c_str());
}

void install_menu_settings() {
    // Called from title threads (the front end's menus): host code under the title's float modes.
    game::PcSettingsHooks& hooks = game::pc_settings_hooks();
    hooks.graphics_enhanced = [] { return render::graphics_enhanced(); };
    hooks.set_graphics_enhanced = [](bool on) {
        platform::HostFloatEnv env;
        render::set_graphics(on, render::graphics_features());
        LOG_INFO("loader", "graphics %s (front-end menu)", on ? "enhanced" : "original");
    };
    hooks.save_graphics = [] {
        platform::HostFloatEnv env;
        const bool graphics = render::graphics_enhanced();
        platform::Settings settings = platform::load_settings();
        settings.set("graphics", graphics ? "enhanced" : "original");
        if (platform::save_settings(settings))
            LOG_INFO("loader", "%s graphics saved (front-end menu)", graphics ? "enhanced" : "original");
        else
            LOG_WARN("loader", "cannot save the graphics setting to %s", platform::settings_path().string().c_str());
    };
}

}  // namespace loader
