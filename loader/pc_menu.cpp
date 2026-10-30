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
    ImGui::Separator();

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

    ImGui::Separator();
    bool enhanced = audio::enhanced();
    if (ImGui::Checkbox("Enhanced sound", &enhanced)) audio::set_enhanced(enhanced);
    ImGui::TextDisabled("Off: the sound as on the Xbox. On: reverb by the place you are in\n"
                        "and finer resampling. Switch while standing still to compare.");
    if (enhanced && ImGui::TreeNode("Advanced reverb")) {
        audio::ReverbTuning tuning = game::SndEnv_Tuning();
        if (reverb_sliders(tuning)) game::SndEnv_SetTuning(tuning);
        ImGui::TextDisabled("Room clutter: what soaks up sound besides the walls (furniture,\n"
                            "debris); lower makes rooms ring longer.");
        ImGui::TreePop();
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
        return;
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
