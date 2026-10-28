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

namespace loader {

namespace {

constexpr int kGameFov = 70;  // cg_fov's default (cgame cvarTable)
constexpr int kMinFov = 50, kMaxFov = 120;
float g_fov_on_open = 0;

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
    if (ImGui::Button("Close")) open = false;
    ImGui::End();
    return open;
}

void pc_menu_shown(bool open) {
    input::set_game_input_blocked(open);
    if (open) {
        g_fov_on_open = field_of_view();
        return;
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
