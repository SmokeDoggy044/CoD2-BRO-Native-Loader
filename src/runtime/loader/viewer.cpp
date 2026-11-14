#include "viewer.h"

#include <cstdint>

#include "dev_mode.h"
#include "g_viewer.h"
#include "log.h"
#include "qcommon.h"

namespace loader {

namespace {

constexpr float kDistance = 120.0f;   // game units in front of the player
constexpr float kDegreesPerFrame = 0.75f;

std::vector<std::string> g_models;
size_t g_next = 0;
std::string g_shown, g_message;
bool g_turning = true;
int g_pause_in = -1;  // frames until the game pauses after a model is shown (the client takes it up first)
float g_yaw = 0;

}  // namespace

void set_viewer_models(std::vector<std::string> models) { g_models = std::move(models); }

std::vector<std::string> viewer_frame(uint32_t keys) {
    if (keys & (1u << 7)) {  // F8
        if (g_models.empty()) {
            g_message = "no models: --viewer M1,M2";
        } else {
            const std::string& model = g_models[g_next];
            g_next = (g_next + 1) % g_models.size();
            std::string why;
            if (game::viewer_show(model.c_str(), kDistance, why)) {
                g_shown = model;
                g_message.clear();
                g_yaw = 0;
                render::dev::set_paused(false);
                game::Cvar_Set("cg_drawgun", "0");  // the player's gun would cover small models
                g_pause_in = 30;
                LOG_INFO("dev", "viewer: %s %s", model.c_str(), why.c_str());
            } else {
                g_message = model + ": " + why;
                LOG_WARN("dev", "viewer: %s", g_message.c_str());
            }
        }
    }
    if (g_pause_in >= 0 && g_pause_in-- == 0) render::dev::set_paused(true);
    if (keys & (1u << 8)) g_turning = !g_turning;  // F9
    if (!g_shown.empty() && g_turning) {
        g_yaw += kDegreesPerFrame;
        if (g_yaw >= 360) g_yaw -= 360;
        game::viewer_turn(g_yaw, 0);
    }
    std::vector<std::string> lines;
    std::string line = "VIEWER  F8 next (" + std::to_string(g_models.size()) + ")  F9 turn " + (g_turning ? "ON" : "off");
    if (!g_shown.empty()) line += "  showing " + g_shown;
    lines.push_back(line);
    if (!g_message.empty()) lines.push_back("  " + g_message);
    return lines;
}

}  // namespace loader
