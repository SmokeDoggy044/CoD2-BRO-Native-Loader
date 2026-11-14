#include "dev_mode.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

#include "enhanced_graphics.h"
#include "gfx_device.h"
#include "log.h"
#include "texture_pack.h"

namespace render::dev {

namespace {

constexpr uint32_t key(int f) { return 1u << (f - 1); }

bool g_shown = true;
bool g_paused = false;
size_t g_feature = 0;  // index into GraphicsFeatures::kSwitches

// Frame times over the last whole second.
struct Timing {
    using Clock = std::chrono::steady_clock;
    Clock::time_point last{}, second_start{};
    uint32_t frames = 0;
    double worst_ms = 0, sum_ms = 0;
    std::string text = "frame: measuring";

    void frame() {
        const auto now = Clock::now();
        if (last != Clock::time_point{}) {
            const double ms = std::chrono::duration<double, std::milli>(now - last).count();
            worst_ms = std::max(worst_ms, ms);
            sum_ms += ms;
            ++frames;
        } else {
            second_start = now;
        }
        last = now;
        if (now - second_start >= std::chrono::seconds(1) && frames) {
            char line[96];
            std::snprintf(line, sizeof(line), "frame %.1f ms (%u fps), worst %.1f ms", sum_ms / frames, frames,
                          worst_ms);
            text = line;
            frames = 0;
            worst_ms = sum_ms = 0;
            second_start = now;
        }
    }
} g_timing;

const char* on_off(bool on) { return on ? "ON" : "off"; }

void switch_graphics(bool enhanced, const GraphicsFeatures& features) {
    set_graphics(enhanced, features);
    LOG_INFO("dev", "graphics %s, features \"%s\"", enhanced ? "enhanced" : "original", features.format().c_str());
}

void handle_keys(uint32_t keys, void (*pause)(bool)) {
    if (keys & key(1)) g_shown = !g_shown;
    if (keys & key(2)) texture_pack::set_enabled(!texture_pack::enabled());
    if (keys & key(3)) switch_graphics(!graphics_enhanced(), graphics_features());
    if (keys & key(4)) g_feature = (g_feature + 1) % std::size(GraphicsFeatures::kSwitches);
    if (keys & key(5)) {
        GraphicsFeatures features = graphics_features();
        bool& on = features.*GraphicsFeatures::kSwitches[g_feature].field;
        on = !on;
        switch_graphics(graphics_enhanced(), features);
    }
    if ((keys & key(7)) && pause) {
        g_paused = !g_paused;
        pause(g_paused);
        LOG_INFO("dev", "game %s", g_paused ? "paused" : "resumed");
    }
}

// COD_BRO_DEV_KEYS="7@900,2@1100": F7 pressed at the 900th presented frame, F2 at the 1100th (unattended checks).
uint32_t scripted_keys() {
    static std::vector<std::pair<uint32_t, uint32_t>> script = [] {
        std::vector<std::pair<uint32_t, uint32_t>> list;
        if (const char* text = std::getenv("COD_BRO_DEV_KEYS")) {
            unsigned f = 0, at = 0;
            for (const char* p = text; std::sscanf(p, "%u@%u", &f, &at) == 2;) {
                if (f >= 1 && f <= 9) list.emplace_back(at, key(static_cast<int>(f)));
                p = std::strchr(p, ',');
                if (!p) break;
                ++p;
            }
        }
        return list;
    }();
    static uint32_t frames = 0;
    ++frames;
    uint32_t keys = 0;
    for (const auto& [at, k] : script)
        if (at == frames) keys |= k;
    return keys;
}

std::string mib(uint64_t bytes) { return std::to_string((bytes + (1u << 19)) >> 20) + " MB"; }

}  // namespace

std::vector<std::string> frame(Backend& backend, void (*pause)(bool)) {
    g_timing.frame();
    handle_keys(backend.take_function_keys() | scripted_keys(), pause);
    if (!g_shown) return {};

    std::vector<std::string> lines;
    const GraphicsFeatures features = graphics_features();
    const auto& feature = GraphicsFeatures::kSwitches[g_feature];
    const texture_pack::Stats pack = texture_pack::stats();
    lines.push_back(std::string("DEV  F1 hide  F2 mods ") + (pack.covered ? on_off(texture_pack::enabled()) : "none") +
                    "  F3 enhanced " + on_off(graphics_enhanced()) + "  F4/F5 " + feature.key + " " +
                    on_off(features.*feature.field) + "  F7 " + (g_paused ? "PAUSED" : "pause"));
    lines.push_back(g_timing.text);
    if (GfxDevice* device = backend.gfx_device()) {
        const GfxDevice::Stats s = device->stats();
        std::string line = "GPU ";
        if (s.gpu_budget) line += mib(s.gpu_used) + " of " + mib(s.gpu_budget);
        else if (s.gpu_available) line += "about " + mib(s.gpu_available) + " free";
        else line += "memory unknown";
        line += ", " + std::to_string(s.replacements) + " replacement pictures";
        lines.push_back(line);
    }
    if (pack.covered)
        lines.push_back("packs: " + std::to_string(pack.covered) + " textures, " + std::to_string(pack.loaded) +
                        " loaded, " + std::to_string(pack.pending) + " pending, " + std::to_string(pack.failed) +
                        " failed; " + std::to_string(pack.uncovered) + " drawn without one");
    return lines;
}

std::string state_text() {
    std::string text = std::string("mods ") + on_off(texture_pack::enabled()) + ", graphics " +
                       (graphics_enhanced() ? "enhanced" : "original");
    const std::string features = graphics_features().format();
    if (!features.empty()) text += " (" + features + ")";
    if (g_paused) text += ", paused";
    return text;
}

}  // namespace render::dev
