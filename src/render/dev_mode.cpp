#include "dev_mode.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <iterator>

#include "enhanced_graphics.h"
#include "gfx_device.h"
#include "log.h"
#include "texture_pack.h"

namespace render::dev {

namespace {

constexpr uint32_t key(int f) { return 1u << (f - 1); }

bool g_shown = true;
bool g_picking = false;
std::vector<std::string> g_pick_lines;  // the last pick, for the overlay
std::string g_pick_mark;                // ... and for F12 marks
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
    if (keys & key(6)) g_picking = true;  // asked for in frame(), where the device is
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

// COD_BRO_TEXTURE_NAMES=bridge.csv (tools/texture_bridge.py: content,...,asset_path,...): the dump's names.
const std::unordered_map<uint64_t, std::string>& texture_names() {
    static const std::unordered_map<uint64_t, std::string> names = [] {
        std::unordered_map<uint64_t, std::string> map;
        const char* file = std::getenv("COD_BRO_TEXTURE_NAMES");
        if (!file || !*file) return map;
        std::ifstream in(file);
        std::string line;
        size_t column = std::string::npos;
        while (std::getline(in, line)) {
            std::vector<std::string> fields;
            for (size_t at = 0;;) {
                const size_t comma = line.find(',', at);
                fields.push_back(line.substr(at, comma == std::string::npos ? std::string::npos : comma - at));
                if (comma == std::string::npos) break;
                at = comma + 1;
            }
            if (column == std::string::npos) {  // the header
                column = std::find(fields.begin(), fields.end(), "asset_path") - fields.begin();
                continue;
            }
            if (column < fields.size() && !fields[0].empty())
                map[std::strtoull(fields[0].c_str(), nullptr, 16)] = fields[column];
        }
        LOG_INFO("dev", "%zu texture names from %s", map.size(), file);
        return map;
    }();
    return names;
}

std::string hex(uint64_t value) {
    char text[20];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

// One texture of a picked draw: hash, size, replaced by which mod, the dump's name.
std::string describe(const GfxDevice::PickHit& hit, int slot) {
    std::string text = hex(hit.content[slot]) + " " + std::to_string(hit.width[slot]) + "x" +
                       std::to_string(hit.height[slot]);
    if (const std::string mod = texture_pack::covering_mod(hit.content[slot]); !mod.empty())
        text += std::string(" [") + mod + (hit.replaced[slot] ? "" : ", not shown") + "]";
    else if (hit.replaced[slot])
        text += " [replaced]";
    const auto& names = texture_names();
    if (auto it = names.find(hit.content[slot]); it != names.end()) {
        const size_t slash = it->second.find_last_of("/\\");
        text += " " + (slash == std::string::npos ? it->second : it->second.substr(slash + 1));
    }
    return text;
}

void show_pick(const std::vector<GfxDevice::PickHit>& hits) {
    g_pick_lines.clear();
    g_pick_mark.clear();
    LOG_INFO("dev", "pick: %zu draws reached the centre pixel (drawing order)", hits.size());
    for (size_t i = 0; i < hits.size(); ++i) {
        const GfxDevice::PickHit& h = hits[i];
        std::string slots;
        for (int slot = 0; slot < 4; ++slot)
            if (h.content[slot]) slots += " t" + std::to_string(slot) + "=" + describe(h, slot);
        LOG_INFO("dev", "pick %zu: %s%s%s, %u samples:%s", i, h.depth_test ? "depth test" : "no depth test",
                 h.depth_write ? ", depth write" : "", h.blend ? ", blended" : "", h.samples,
                 slots.empty() ? " no title texture" : slots.c_str());
    }
    // World draws (depth tested) top first; the HUD (the crosshair) only when nothing else is there.
    std::vector<const GfxDevice::PickHit*> shown;
    for (auto it = hits.rbegin(); it != hits.rend() && shown.size() < 3; ++it)
        if (it->depth_test && it->content[0]) shown.push_back(&*it);
    for (auto it = hits.rbegin(); shown.empty() && it != hits.rend() && shown.size() < 3; ++it)
        if (it->content[0]) shown.push_back(&*it);
    g_pick_lines.push_back("F6 pick, top first (all in the log):");
    if (shown.empty()) g_pick_lines.push_back("  no title texture at the centre");
    for (const GfxDevice::PickHit* h : shown)
        g_pick_lines.push_back(std::string("  ") + (h->blend ? "~ " : "") + describe(*h, 0));
    if (!shown.empty()) g_pick_mark = "pick " + describe(*shown.front(), 0);
}

std::string mib(uint64_t bytes) { return std::to_string((bytes + (1u << 19)) >> 20) + " MB"; }

}  // namespace

std::vector<std::string> frame(Backend& backend, void (*pause)(bool)) {
    g_timing.frame();
    handle_keys(backend.take_function_keys() | scripted_keys(), pause);
    GfxDevice* device = backend.gfx_device();
    if (g_picking) {
        g_picking = false;
        // The crosshair: the middle of the title's picture.
        if (!device || !device->pick(320, 240)) {
            g_pick_lines = {"F6 pick: not with this renderer (Vulkan only) or already picking"};
            g_pick_mark.clear();
        }
    }
    if (device) {
        std::vector<GfxDevice::PickHit> hits;
        if (device->take_pick(hits)) show_pick(hits);
    }
    if (!g_shown) return {};

    std::vector<std::string> lines;
    const GraphicsFeatures features = graphics_features();
    const auto& feature = GraphicsFeatures::kSwitches[g_feature];
    const texture_pack::Stats pack = texture_pack::stats();
    lines.push_back(std::string("DEV  F1 hide  F2 mods ") + (pack.covered ? on_off(texture_pack::enabled()) : "none") +
                    "  F3 enhanced " + on_off(graphics_enhanced()) + "  F4/F5 " + feature.key + " " +
                    on_off(features.*feature.field) + "  F7 " + (g_paused ? "PAUSED" : "pause"));
    lines.push_back(g_timing.text);
    if (device) {
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
    lines.insert(lines.end(), g_pick_lines.begin(), g_pick_lines.end());
    return lines;
}

std::string state_text() {
    std::string text = std::string("mods ") + on_off(texture_pack::enabled()) + ", graphics " +
                       (graphics_enhanced() ? "enhanced" : "original");
    const std::string features = graphics_features().format();
    if (!features.empty()) text += " (" + features + ")";
    if (g_paused) text += ", paused";
    if (!g_pick_mark.empty()) text += ", " + g_pick_mark;
    return text;
}

}  // namespace render::dev
