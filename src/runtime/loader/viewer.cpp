#include "viewer.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

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
int g_since_show = -1;  // frames since the last F8 that showed a model

// COD_BRO_VIEWER_TURNTABLE="STEPS,FRAMES" (the lab): instead of turning, the model holds STEPS fixed angles
// (360/STEPS apart) for FRAMES frames each, from the pause on (30 frames after F8).
struct Turntable {
    int steps = 0, frames = 0;
} const g_turntable = [] {
    Turntable t;
    if (const char* text = std::getenv("COD_BRO_VIEWER_TURNTABLE"))
        if (std::sscanf(text, "%d,%d", &t.steps, &t.frames) != 2 || t.steps < 1 || t.frames < 1) t = {};
    return t;
}();
constexpr int kPauseAfter = 30;

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
                g_pause_in = kPauseAfter;
                g_since_show = 0;
                LOG_INFO("dev", "viewer: %s %s", model.c_str(), why.c_str());
            } else {
                g_message = model + ": " + why;
                LOG_WARN("dev", "viewer: %s", g_message.c_str());
            }
        }
    }
    if (g_pause_in >= 0 && g_pause_in-- == 0) render::dev::set_paused(true);
    if (keys & (1u << 8)) g_turning = !g_turning;  // F9
    if (g_since_show >= 0) ++g_since_show;
    if (!g_shown.empty() && g_turntable.steps) {
        const int step = std::min(std::max(g_since_show - kPauseAfter, 0) / g_turntable.frames, g_turntable.steps - 1);
        const float yaw = 360.0f * static_cast<float>(step) / static_cast<float>(g_turntable.steps);
        if (yaw != g_yaw || g_since_show == 1) {
            g_yaw = yaw;
            game::viewer_turn(g_yaw, 0);
            LOG_INFO("dev", "viewer turntable: step %d (%.0f degrees), %d frames after F8", step, g_yaw, g_since_show);
        }
        if (g_since_show == kPauseAfter + g_turntable.steps * g_turntable.frames + 2) {
            LOG_INFO("dev", "viewer turntable done");
            std::exit(0);  // the lab's run is over (its pictures are written as the frames are shown)
        }
    } else if (!g_shown.empty() && g_turning) {
        g_yaw += kDegreesPerFrame;
        if (g_yaw >= 360) g_yaw -= 360;
        game::viewer_turn(g_yaw, 0);
    }
    std::vector<std::string> lines;
    std::string line = "VIEWER  F8 next  F9 turn " + std::string(g_turning ? "ON" : "off");
    if (!g_shown.empty()) line += "  showing " + g_shown;
    lines.push_back(line);
    if (!g_models.empty())
        lines.push_back("  next " + std::to_string(g_next + 1) + "/" + std::to_string(g_models.size()) + ": " +
                        g_models[g_next]);
    if (!g_message.empty()) lines.push_back("  " + g_message);
    return lines;
}

}  // namespace loader
