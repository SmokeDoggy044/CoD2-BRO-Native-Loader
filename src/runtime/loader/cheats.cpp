#include "cheats.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>

#include "bindings.h"
#include "devices.h"
#include "cl.h"
#include "float_env.h"
#include "log.h"
#include "patches.h"
#include "title_data.h"

namespace loader {

namespace {

bool g_supported = false;
std::atomic<bool> g_free_camera_allowed{false};

// The camera (the game's thread: native_actions, mouse_look and CG_CalcViewValues all run there).
struct FreeCamera {
    bool flying = false;
    bool starting = false;      // set on the key: the next view hands its origin and angles over
    float origin[3] = {};
    float angles[3] = {};       // pitch, yaw, roll (degrees), as the refdef's viewangles
    uint32_t held = 0;          // the actions held, for the movement
    int32_t game_time = 0;      // cg.time at the last view: a smaller one is a new level
    std::chrono::steady_clock::time_point last;
    bool key_down = false;
};
FreeCamera g_camera;

constexpr float kSpeed = 300.0f;  // world units per second (the player runs at about 190)

bool held(uint32_t mask, input::Action action) { return mask >> static_cast<unsigned>(action) & 1; }

// The player is dead: the next snapshot's pm_type (+0x34) is 4 or more (CG_OffsetFirstPersonView's death view).
bool player_dead() {
    const uint32_t next = game::title_global<uint32_t>(0x00971aac);
    return next && *reinterpret_cast<const int32_t*>(uintptr_t{next + 0x34}) >= 4;
}

void stop_flying(const char* why) {
    if (!g_camera.flying && !g_camera.starting) return;
    g_camera.flying = g_camera.starting = false;
    game::client_pc_hooks().free_camera_active = false;
    input::set_player_frozen(false);
    LOG_INFO("cheats", "free camera off (%s)", why);
}

// ClientPcHooks::free_camera, from CG_CalcViewValues with the view it worked out.
bool XB_CDECL free_camera_view(float* origin, float* angles) {
    const int32_t game_time = game::title_global<int32_t>(0x0097253c);  // cg.time
    const bool new_level = game_time < g_camera.game_time;
    g_camera.game_time = game_time;
    if (new_level) stop_flying("a new level");
    if (g_camera.starting) {
        std::copy(origin, origin + 3, g_camera.origin);
        std::copy(angles, angles + 3, g_camera.angles);
        g_camera.angles[2] = 0.0f;
        g_camera.starting = false;
        g_camera.flying = true;
        g_camera.last = std::chrono::steady_clock::now();
        game::client_pc_hooks().free_camera_active = true;
        LOG_INFO("cheats", "free camera on at %.0f %.0f %.0f", double(origin[0]), double(origin[1]), double(origin[2]));
    }
    if (!g_camera.flying) return false;
    platform::HostFloatEnv float_env;
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::min(std::chrono::duration<float>(now - g_camera.last).count(), 0.1f);
    g_camera.last = now;
    // Along the view (pitch included) and to its right; up and down on the world's z.
    const float pitch = g_camera.angles[0] * 0.017453293f, yaw = g_camera.angles[1] * 0.017453293f;
    const float forward[3] = {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), -std::sin(pitch)};
    const float right[3] = {std::sin(yaw), -std::cos(yaw), 0.0f};
    using A = input::Action;
    const uint32_t h = g_camera.held;
    // A controller: the left stick moves, the right stick turns (up looks up), about 150 degrees a second at full.
    float lx, ly, rx, ry;
    input::free_camera_sticks(lx, ly, rx, ry);
    constexpr float kTurnRate = 150.0f;
    g_camera.angles[1] -= rx * kTurnRate * dt;
    g_camera.angles[0] = std::clamp(g_camera.angles[0] - ry * kTurnRate * dt, -89.0f, 89.0f);
    const float f = std::clamp(float(held(h, A::MoveForward)) - float(held(h, A::MoveBack)) + ly, -1.0f, 1.0f);
    const float r = std::clamp(float(held(h, A::MoveRight)) - float(held(h, A::MoveLeft)) + lx, -1.0f, 1.0f);
    const float u = float(held(h, A::Jump)) - float(held(h, A::Crouch));
    const float speed = kSpeed * (held(h, A::Sprint) ? 4.0f : 1.0f) * (held(h, A::Aim) ? 0.25f : 1.0f) * dt;
    for (int k = 0; k < 3; ++k)
        g_camera.origin[k] += (forward[k] * f + right[k] * r + (k == 2 ? u : 0.0f)) * speed;
    std::copy(g_camera.origin, g_camera.origin + 3, origin);
    std::copy(g_camera.angles, g_camera.angles + 3, angles);
    return true;
}

}  // namespace

void install_cheats(bool supported, const TitlePatchConfig& config) {
    g_supported = supported;
    if (!supported) return;
    game::client_pc_hooks().free_camera = &free_camera_view;
    // Diagnostics for #44/#48 (the HUD gone until a restart): every change of what keeps the HUD back.
    game::client_pc_hooks().hud_state = [](int32_t reason, int32_t dialog) {
        static const char* const kWhy[] = {"drawn", "held back by a dialog", "held back by the root's flag",
                                           "hidden (cg_draw2D 0: the Hide HUD key)", "hidden (free camera)"};
        if (reason == 1)
            LOG_INFO("ui", "HUD %s (dialog %d)", kWhy[1], dialog);
        else
            LOG_INFO("ui", "HUD %s", kWhy[reason >= 0 && reason < 5 ? reason : 0]);
    };
    set_god_mode(config.god_mode);
    set_infinite_ammo(config.infinite_ammo);
    set_free_camera(config.free_camera);
}

void set_god_mode(bool on) {
    if (!g_supported || game::client_pc_hooks().god_mode == on) return;
    game::client_pc_hooks().god_mode = on;
    LOG_INFO("cheats", "god mode %s", on ? "on" : "off");
}

void set_infinite_ammo(bool on) {
    if (!g_supported || game::client_pc_hooks().infinite_ammo == on) return;
    game::client_pc_hooks().infinite_ammo = on;
    LOG_INFO("cheats", "infinite ammo %s", on ? "on" : "off");
}

void set_free_camera(bool on) {
    if (!g_supported || g_free_camera_allowed.exchange(on) == on) return;
    LOG_INFO("cheats", "free camera key %s", on ? "on" : "off");
}

bool god_mode() { return game::client_pc_hooks().god_mode; }
bool infinite_ammo() { return game::client_pc_hooks().infinite_ammo; }
bool free_camera() { return g_free_camera_allowed.load(); }

uint32_t cheats_filter_actions(uint32_t held_actions) {
    if (!g_supported) return held_actions;
    if (!g_free_camera_allowed.load()) stop_flying("cheat switched off");
    // Dead: the camera comes back and the player keeps every input (the death screen needs it; #42: the key while
    // dead froze the input for good).
    const bool dead = player_dead();
    if (dead) stop_flying("the player died");
    // A start no view took over within half a second (the view hook does not run): the input comes back.
    if (g_camera.starting && std::chrono::steady_clock::now() - g_camera.last > std::chrono::milliseconds(500))
        stop_flying("no view took it");
    const bool key = held(held_actions, input::Action::FreeCamera);
    if (key && !g_camera.key_down && !dead && g_free_camera_allowed.load()) {
        if (g_camera.flying || g_camera.starting)
            stop_flying("the key");
        else {
            g_camera.starting = true;  // the next view hands over where it is
            g_camera.last = std::chrono::steady_clock::now();
            input::set_player_frozen(true);
        }
    }
    g_camera.key_down = key;
    g_camera.held = held_actions;
    return g_camera.flying || g_camera.starting ? 0u : held_actions;
}

bool cheats_take_mouse(float dx, float dy, float degrees_per_count) {
    if (!g_camera.flying && !g_camera.starting) return false;
    if (!g_camera.flying) return true;  // starting: the angles come with the next view
    g_camera.angles[1] -= dx * degrees_per_count;
    g_camera.angles[0] = std::clamp(g_camera.angles[0] + dy * degrees_per_count, -89.0f, 89.0f);
    return true;
}

bool free_camera_flying() { return g_camera.flying || g_camera.starting; }

}  // namespace loader
