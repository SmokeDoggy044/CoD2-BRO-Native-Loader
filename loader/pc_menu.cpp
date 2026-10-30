// The in-game PC menu (pc_menu.h). Runs on the title's thread inside the renderer's
// present(), so it only reads and asks; the title applies changes at its frame end.

#include "pc_menu.h"

#include <cmath>
#include <string>

#include "devices.h"
#include "imgui.h"
#include "log.h"
#include "patches.h"
#include "settings.h"
#include "snd_env.h"
#include "sound_engine.h"

namespace loader {

namespace {

constexpr int kGameFov = 70;  // cg_fov's default (cgame cvarTable)
constexpr int kMinFov = 50, kMaxFov = 120;
float g_fov_on_open = 0;
bool g_enhanced_on_open = false;
audio::ReverbTuning g_reverb_on_open;
audio::CategoryVolumes g_volumes_on_open;
audio::EnhancedFeatures g_features_on_open;

// The volume per kind of sound's sliders (audio::CategoryVolumes); true if one moved.
bool volume_sliders(audio::CategoryVolumes& v) {
    bool changed = false;
    const float width = ImGui::GetFontSize() * 14;
    ImGui::SetNextItemWidth(width);
    changed |= ImGui::SliderFloat("Effects volume", &v.effects, 0.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(width);
    changed |= ImGui::SliderFloat("Ambience volume", &v.ambience, 0.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(width);
    changed |= ImGui::SliderFloat("Dialogue volume", &v.dialogue, 0.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(width);
    changed |= ImGui::SliderFloat("Music volume", &v.music, 0.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
    return changed;
}

// The close-range boosts' sliders (enhanced, CategoryVolumes::close_*); true if one moved.
bool boost_sliders(audio::CategoryVolumes& v) {
    bool changed = false;
    const float width = ImGui::GetFontSize() * 14;
    ImGui::SetNextItemWidth(width);
    changed |= ImGui::SliderFloat("Others' gunfire", &v.close_gunfire, 0.0f, 12.0f, "%+.1f dB",
                                  ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(width);
    changed |= ImGui::SliderFloat("Your gunfire", &v.own_gunfire, 0.0f, 12.0f, "%+.1f dB", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(width);
    changed |= ImGui::SliderFloat("Bullet impacts and flybys", &v.close_bullets, 0.0f, 12.0f, "%+.1f dB",
                                  ImGuiSliderFlags_AlwaysClamp);
    return changed;
}

// The reverb adjustments' sliders (audio::ReverbTuning); true if one moved.
bool reverb_sliders(audio::ReverbTuning& t) {
    bool changed = false;
    const float width = ImGui::GetFontSize() * 14;
    ImGui::SetNextItemWidth(width);
    changed |= ImGui::SliderFloat("Amount", &t.amount, -12.0f, 12.0f, "%+.1f dB", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(width);
    changed |= ImGui::SliderFloat("Decay", &t.decay, 0.25f, 4.0f, "x%.2f",
                                  ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
    ImGui::SetNextItemWidth(width);
    changed |= ImGui::SliderFloat("Brightness", &t.brightness, 0.5f, 2.0f, "x%.2f",
                                  ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic);
    ImGui::SetNextItemWidth(width);
    changed |= ImGui::SliderFloat("Early reflections", &t.reflections, -12.0f, 12.0f, "%+.1f dB",
                                  ImGuiSliderFlags_AlwaysClamp);
    ImGui::SetNextItemWidth(width);
    changed |= ImGui::SliderFloat("Room clutter", &t.clutter, 0.0f, 0.3f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::Button("Default reverb")) {
        t = audio::ReverbTuning{};
        changed = true;
    }
    return changed;
}

void general_tab() {
    if (field_of_view_supported()) {
        const float asked = field_of_view();
        const float game = game_field_of_view();
        int fov = static_cast<int>(std::lround(asked > 0 ? asked : game > 0 ? game : kGameFov));
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16);
        if (ImGui::SliderInt("Field of view", &fov, kMinFov, kMaxFov, "%d", ImGuiSliderFlags_AlwaysClamp))
            set_field_of_view(static_cast<float>(fov));
        ImGui::SameLine();
        if (ImGui::Button("Default")) set_field_of_view(static_cast<float>(kGameFov));
        ImGui::TextDisabled("Degrees across a 4:3 picture, as the game counts them (default %d);\n"
                            "wider pictures see more at the sides.", kGameFov);
        if (game <= 0) ImGui::TextDisabled("Applies once a mission is running.");
    } else {
        ImGui::TextDisabled("Field of view: campaign only for now.");
    }
}

// The improvements switched off, for the log.
std::string text_or_all(const audio::EnhancedFeatures& f) {
    const std::string text = f.format();
    return text.empty() ? "all on" : text;
}

// One improvement's switch (and its help); true if it moved.
bool feature_switch(const char* label, bool& on, const char* help) {
    const bool changed = ImGui::Checkbox(label, &on);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", help);
    return changed;
}

void sound_tab() {
    ImGui::SeparatorText("Volume");
    audio::CategoryVolumes volumes = audio::category_volumes();
    if (volume_sliders(volumes)) audio::set_category_volumes(volumes);
    ImGui::TextDisabled("On top of the game's own volume setting.");

    ImGui::SeparatorText("Enhanced sound");
    bool enhanced = audio::enhanced();
    if (ImGui::Checkbox("Enhanced sound", &enhanced)) audio::set_enhanced(enhanced);
    ImGui::TextDisabled("Off: the sound as on the Xbox. On: the improvements below that are\n"
                        "switched on. Switch while standing still to compare.");
    ImGui::BeginDisabled(!enhanced);
    ImGui::Indent();
    audio::EnhancedFeatures f = audio::enhanced_features();
    bool changed = false;
    changed |= feature_switch("Finer resampling", f.resampler,
                              "OpenAL's best resampler (23rd order Sinc) instead of its default.");
    changed |= feature_switch("More voices", f.voices,
                              "Up to 192 sounds placed in the world at once instead of the Xbox's 36,\n"
                              "and effects drop fewer of their sounds in busy fights.");
    changed |= feature_switch("Reverb", f.reverb,
                              "Reverb that follows the place you are in: room size, walls, stone or\n"
                              "wood, open sky.");
    if (f.reverb && ImGui::TreeNode("Advanced reverb")) {
        audio::ReverbTuning tuning = game::SndEnv_Tuning();
        if (reverb_sliders(tuning)) game::SndEnv_SetTuning(tuning);
        ImGui::TextDisabled("Room clutter: what soaks up sound besides the walls (furniture,\n"
                            "debris); lower makes rooms ring longer.");
        ImGui::TreePop();
    }
    changed |= feature_switch("Occlusion", f.occlusion,
                              "Sounds behind walls and buildings muffled; in view, heard in full.");
    changed |= feature_switch("Close-range boost", f.close_boost,
                              "Gunfire and bullets louder near you (the game mixes them quietly).\n"
                              "0 dB = the game's own level.");
    if (f.close_boost) {
        ImGui::Indent();
        if (boost_sliders(volumes)) audio::set_category_volumes(volumes);
        ImGui::TextDisabled("Full within 15 m, fading to the game's level at 30 m\n"
                            "(your own gunfire: always full).");
        ImGui::Unindent();
    }
    if (changed) audio::set_enhanced_features(f);
    ImGui::Unindent();
    ImGui::EndDisabled();
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
    ImGui::TextDisabled("The game keeps running. ` or Escape closes this menu.");
    if (ImGui::BeginTabBar("pc_menu_tabs")) {
        if (ImGui::BeginTabItem("General")) {
            general_tab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Sound")) {
            sound_tab();
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
        g_fov_on_open = field_of_view();
        g_enhanced_on_open = audio::enhanced();
        g_reverb_on_open = game::SndEnv_Tuning();
        g_volumes_on_open = audio::category_volumes();
        g_features_on_open = audio::enhanced_features();
        return;
    }
    if (const audio::CategoryVolumes volumes = audio::category_volumes(); !(volumes == g_volumes_on_open)) {
        platform::Settings settings = platform::load_settings();
        if (const std::string text = volumes.format(); text.empty())
            settings.erase("volume");
        else
            settings.set("volume", text);
        if (platform::save_settings(settings))
            LOG_INFO("loader", "volumes saved: %s", volumes.format().c_str());
        else
            LOG_WARN("loader", "cannot save the volumes to %s", platform::settings_path().string().c_str());
    }
    if (const audio::ReverbTuning reverb = game::SndEnv_Tuning(); !(reverb == g_reverb_on_open)) {
        platform::Settings settings = platform::load_settings();
        if (const std::string text = reverb.format(); text.empty())
            settings.erase("reverb");
        else
            settings.set("reverb", text);
        if (platform::save_settings(settings))
            LOG_INFO("loader", "reverb adjustments saved: %s", reverb.format().c_str());
        else
            LOG_WARN("loader", "cannot save the reverb adjustments to %s", platform::settings_path().string().c_str());
    }
    if (const audio::EnhancedFeatures features = audio::enhanced_features(); !(features == g_features_on_open)) {
        platform::Settings settings = platform::load_settings();
        if (const std::string text = features.format(); text.empty())
            settings.erase("enhanced");
        else
            settings.set("enhanced", text);
        if (platform::save_settings(settings))
            LOG_INFO("loader", "enhanced sound's improvements saved: %s", text_or_all(features).c_str());
        else
            LOG_WARN("loader", "cannot save the enhanced sound settings to %s", platform::settings_path().string().c_str());
    }
    const bool enhanced = audio::enhanced();
    if (enhanced != g_enhanced_on_open) {
        platform::Settings settings = platform::load_settings();
        settings.set("audio", enhanced ? "enhanced" : "original");
        if (platform::save_settings(settings))
            LOG_INFO("loader", "%s sound saved", enhanced ? "enhanced" : "original");
        else
            LOG_WARN("loader", "cannot save the sound setting to %s", platform::settings_path().string().c_str());
    }
    const float fov = field_of_view();
    if (fov == g_fov_on_open) return;
    platform::Settings settings = platform::load_settings();
    if (fov <= 0 || std::lround(fov) == kGameFov)
        settings.erase("fov");
    else
        settings.set("fov", std::to_string(std::lround(fov)));
    if (platform::save_settings(settings))
        LOG_INFO("loader", "field of view %.0f saved", static_cast<double>(fov));
    else
        LOG_WARN("loader", "cannot save the field of view to %s", platform::settings_path().string().c_str());
}

}  // namespace loader
