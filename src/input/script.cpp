// Scripted button presses. See script.h.

#include "script.h"

#include <chrono>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#include "log.h"

namespace input {

namespace {

using Clock = std::chrono::steady_clock;

constexpr double kTapSeconds = 0.25;  // how long a press is held unless it says (button@at+hold)

struct Press {
    double at;  // seconds after set_script
    double hold;  // seconds held
    uint16_t buttons;
    int analog;  // AnalogButton, or -1
};

std::mutex g_mutex;
std::vector<Press> g_presses;
Clock::time_point g_start;

bool parse_button(std::string_view name, Press& press) {
    struct Name {
        std::string_view name;
        uint16_t buttons;
        int analog;
    };
    static constexpr Name kNames[] = {
        {"a", 0, kA},         {"b", 0, kB},          {"x", 0, kX},          {"y", 0, kY},
        {"black", 0, kBlack}, {"white", 0, kWhite},  {"lt", 0, kLeftTrigger}, {"rt", 0, kRightTrigger},
        {"start", kStart, -1}, {"back", kBack, -1},  {"up", kDpadUp, -1},   {"down", kDpadDown, -1},
        {"left", kDpadLeft, -1}, {"right", kDpadRight, -1}, {"ls", kLeftThumb, -1}, {"rs", kRightThumb, -1},
    };
    for (const auto& n : kNames) {
        if (n.name != name) continue;
        press.buttons = n.buttons;
        press.analog = n.analog;
        return true;
    }
    return false;
}

}  // namespace

bool set_script(std::string_view script) {
    std::vector<Press> presses;
    while (!script.empty()) {
        const size_t comma = script.find(',');
        const std::string_view item = script.substr(0, comma);
        script = comma == std::string_view::npos ? std::string_view{} : script.substr(comma + 1);
        const size_t at = item.find('@');
        Press press{};
        const std::string time(at == std::string_view::npos ? std::string_view{} : item.substr(at + 1));
        char* end = nullptr;
        press.at = std::strtod(time.c_str(), &end);
        press.hold = kTapSeconds;
        if (*end == '+' && end > time.c_str()) press.hold = std::strtod(end + 1, &end);
        if (at == std::string_view::npos || time.empty() || *end || press.at < 0 || !(press.hold > 0) ||
            !parse_button(item.substr(0, at), press)) {
            LOG_ERROR("input", "bad scripted press '%.*s' (want button@seconds or button@seconds+hold)", static_cast<int>(item.size()),
                      item.data());
            return false;
        }
        presses.push_back(press);
    }
    std::lock_guard lock(g_mutex);
    g_presses = std::move(presses);
    g_start = Clock::now();
    return true;
}

void apply_script(PadState& state) {
    std::lock_guard lock(g_mutex);
    if (g_presses.empty()) return;
    const double now = std::chrono::duration<double>(Clock::now() - g_start).count();
    for (const Press& press : g_presses) {
        if (now < press.at || now >= press.at + press.hold) continue;
        state.buttons |= press.buttons;
        if (press.analog >= 0) state.analog_buttons[press.analog] = 0xFF;
    }
}

}  // namespace input
