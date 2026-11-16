#include "menu_walk.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <utility>
#include <vector>

#include "devices.h"
#include "log.h"
#include "patches.h"

namespace loader {

namespace {

namespace fs = std::filesystem;

enum class Op { Capture, Window, Item, Wait, Frames, Screen, Idle, Tap, Hold, Mouse, Click, RightClick, Wheel, Shot, State, Quit };

struct Step {
    Op op;
    float a = 0, b = 0;
    int manager = 0;
    bool at = false;  // click X Y
    input::PadState pad;
    std::string name;
    std::string text;  // the step as written (logs)
};

struct Walk {
    std::vector<Step> steps;
    fs::path dir;
    size_t next = 0;
    uint32_t frame = 0;       // presented frames since the start
    uint32_t step_start = 0;  // frame the current step began
    uint32_t calm = 0;        // frames in a row without a locked screen
    bool started = false;
    std::string shot_path;    // this frame's picture
    std::FILE* log = nullptr;
    // The pointer, handed to the menus' poll (title thread; the same thread as the frames, but kept apart anyway).
    std::mutex mutex;
    ScriptedPointer pointer;
    bool pointer_set = false;
} g_walk;

constexpr float kFps = 60.0f;
constexpr uint32_t kTapFrames = 12;
constexpr uint32_t kCalmFrames = 10;
constexpr uint32_t kTimeoutFrames = 30 * 60;

bool parse_pad(const std::string& word, input::PadState& pad) {
    struct Name {
        const char* name;
        uint16_t digital;
        int analog;
    };
    static constexpr Name kNames[] = {
        {"a", 0, input::kA},          {"b", 0, input::kB},         {"x", 0, input::kX},
        {"y", 0, input::kY},          {"black", 0, input::kBlack}, {"white", 0, input::kWhite},
        {"lt", 0, input::kLeftTrigger}, {"rt", 0, input::kRightTrigger}, {"start", input::kStart, -1},
        {"back", input::kBack, -1},   {"up", input::kDpadUp, -1},  {"down", input::kDpadDown, -1},
        {"left", input::kDpadLeft, -1}, {"right", input::kDpadRight, -1}, {"ls", input::kLeftThumb, -1},
        {"rs", input::kRightThumb, -1},
    };
    for (const Name& n : kNames) {
        if (word != n.name) continue;
        pad = {};
        pad.buttons = n.digital;
        if (n.analog >= 0) pad.analog_buttons[n.analog] = 0xff;
        return true;
    }
    return false;
}

bool parse_step(const std::string& text, Step& step) {
    std::istringstream in(text);
    std::string word;
    in >> word;
    step.text = text;
    if (word == "wait" || word == "frames") {
        step.op = word == "wait" ? Op::Wait : Op::Frames;
        return static_cast<bool>(in >> step.a) && step.a >= 0;
    }
    if (word == "screen") {
        std::string spec;
        if (!(in >> spec)) return false;
        step.op = Op::Screen;
        if (const size_t colon = spec.find(':'); colon != std::string::npos) {
            const std::string m = spec.substr(0, colon);
            step.manager = m == "front" ? 0 : m == "game" ? 1 : m == "dialog" ? 2 : -1;
            if (step.manager < 0) return false;
            spec = spec.substr(colon + 1);
        }
        char* end = nullptr;
        step.a = static_cast<float>(std::strtol(spec.c_str(), &end, 10));
        std::string button;
        if (in >> button) {
            if (!parse_pad(button, step.pad)) return false;
            step.at = true;  // tapped every second while waiting
        }
        return end && !*end && !spec.empty();
    }
    if (word == "idle" || word == "quit" || word == "rclick") {
        step.op = word == "idle" ? Op::Idle : word == "quit" ? Op::Quit : Op::RightClick;
        return true;
    }
    if (word == "hold") {
        std::string button;
        step.op = Op::Hold;
        return in >> button >> step.a && parse_pad(button, step.pad);
    }
    if (word == "capture") {
        step.op = Op::Capture;
        return static_cast<bool>(in >> step.a);
    }
    if (word == "window") {
        step.op = Op::Window;
        return static_cast<bool>(in >> step.a >> step.b);
    }
    if (word == "item") {
        step.op = Op::Item;
        return static_cast<bool>(in >> step.a);
    }
    if (word == "mouse") {
        step.op = Op::Mouse;
        return static_cast<bool>(in >> step.a >> step.b);
    }
    if (word == "click") {
        step.op = Op::Click;
        if (in >> step.a) {
            step.at = true;
            return static_cast<bool>(in >> step.b);
        }
        return true;
    }
    if (word == "wheel") {
        step.op = Op::Wheel;
        return static_cast<bool>(in >> step.a);
    }
    if (word == "shot" || word == "state") {
        step.op = word == "shot" ? Op::Shot : Op::State;
        return static_cast<bool>(in >> step.name);
    }
    if (parse_pad(word, step.pad)) {
        step.op = Op::Tap;
        return true;
    }
    return false;
}

void write_state(const std::string& name) {
    const std::string state = menu_state();
    if (std::FILE* f = std::fopen((g_walk.dir / (name + ".txt")).string().c_str(), "w")) {
        std::fputs(state.c_str(), f);
        std::fclose(f);
    }
    if (g_walk.log) {
        std::fprintf(g_walk.log, "== %s (frame %u)\n%s", name.c_str(), g_walk.frame, state.c_str());
        std::fflush(g_walk.log);
    }
}

void finish(int code, const char* why) {
    if (g_walk.log) {
        std::fprintf(g_walk.log, "== end: %s\n", why);
        std::fclose(g_walk.log);
    }
    LOG_INFO("walk", "menu walk ends: %s", why);
    std::fflush(nullptr);
    std::_Exit(code);
}

bool pointer_source(ScriptedPointer& out) {
    std::lock_guard lock(g_walk.mutex);
    if (!g_walk.pointer_set) return false;
    out = g_walk.pointer;
    g_walk.pointer.moved = false;
    g_walk.pointer.left_clicks = g_walk.pointer.right_clicks = g_walk.pointer.wheel = 0;
    return true;
}

void move_pointer(float x, float y) {
    std::lock_guard lock(g_walk.mutex);
    g_walk.pointer.x = x, g_walk.pointer.y = y;
    g_walk.pointer.window = false;
    g_walk.pointer.moved = true;
    g_walk.pointer_set = true;
}

// Runs the current step; true when it is done.
bool run(const Step& s, uint32_t elapsed) {
    switch (s.op) {
    case Op::Wait: return elapsed >= static_cast<uint32_t>(s.a * kFps);
    case Op::Frames: return elapsed >= static_cast<uint32_t>(s.a);
    case Op::Screen:
        if (active_screen(s.manager) == static_cast<int32_t>(s.a) && !active_screen_locked(s.manager) &&
            g_walk.calm >= kCalmFrames)
            return true;
        if (s.at && elapsed % 60 == 30) input::tap(s.pad);
        if (elapsed > kTimeoutFrames) {
            write_state("timeout");
            finish(4, ("timeout: " + s.text).c_str());
        }
        return false;
    case Op::Idle:
    case Op::Shot:
    case Op::State:
        if (g_walk.calm < kCalmFrames && elapsed <= kTimeoutFrames) return false;
        if (s.op == Op::Shot) g_walk.shot_path = (g_walk.dir / (s.name + ".bmp")).string();
        if (s.op != Op::Idle) write_state(s.name);
        return true;
    case Op::Tap:
        if (elapsed == 0) input::tap(s.pad);
        return elapsed >= kTapFrames;
    case Op::Hold: {
        const uint32_t frames = static_cast<uint32_t>(s.a * kFps);
        if (elapsed < frames && elapsed % 6 == 0) input::tap(s.pad);  // taps last ~0.1 s: renewed while held
        return elapsed >= frames + kTapFrames;
    }
    case Op::Item: {
        float x, y;
        if (g_walk.calm >= kCalmFrames && item_middle(static_cast<int32_t>(s.a), x, y)) {
            move_pointer(x, y);
            g_walk.step_start = g_walk.frame - 1;  // then 12 frames for the hover to select it
            const_cast<Step&>(s).op = Op::Frames;
            const_cast<Step&>(s).a = kTapFrames;
            return false;
        }
        if (elapsed > kTimeoutFrames) {
            write_state("timeout");
            finish(4, ("timeout: " + s.text).c_str());
        }
        return false;
    }
    case Op::Capture:
        if (elapsed == 0) input::finish_key_capture(static_cast<uint16_t>(s.a));
        return elapsed >= kTapFrames;
    case Op::Window:
        if (elapsed == 0) {
            move_pointer(s.a, s.b);
            std::lock_guard lock(g_walk.mutex);
            g_walk.pointer.window = true;
        }
        return elapsed >= 4;
    case Op::Mouse:
        if (elapsed == 0) move_pointer(s.a, s.b);
        return elapsed >= 4;
    case Op::Click:
    case Op::RightClick:
    case Op::Wheel:
        if (elapsed == 0 && s.at) move_pointer(s.a, s.b);
        if (elapsed == (s.at ? 3u : 0u)) {
            std::lock_guard lock(g_walk.mutex);
            g_walk.pointer_set = true;
            if (s.op == Op::Click) ++g_walk.pointer.left_clicks;
            if (s.op == Op::RightClick) ++g_walk.pointer.right_clicks;
            if (s.op == Op::Wheel) g_walk.pointer.wheel += static_cast<int>(s.a);
        }
        return elapsed >= (s.at ? 3u : 0u) + kTapFrames;
    case Op::Quit: finish(0, "quit"); return true;
    }
    return true;
}

}  // namespace

bool load_menu_walk(const std::string& script, const std::string& dir) {
    std::string text = script;
    if (!text.empty() && text[0] == '@') {
        std::ifstream in(text.substr(1));
        if (!in) {
            std::fprintf(stderr, "--menu-walk: cannot read %s\n", text.c_str() + 1);
            return false;
        }
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    std::vector<Step> steps;
    std::string line;
    for (char& c : text)
        if (c == ';') c = '\n';
    std::istringstream lines(text);
    while (std::getline(lines, line)) {
        if (const size_t hash = line.find('#'); hash != std::string::npos) line.resize(hash);
        while (!line.empty() && (line.back() == ' ' || line.back() == '\r' || line.back() == '\t')) line.pop_back();
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) line.erase(0, 1);
        if (line.empty()) continue;
        Step step{};
        if (!parse_step(line, step)) {
            std::fprintf(stderr, "--menu-walk: bad step '%s'\n", line.c_str());
            return false;
        }
        steps.push_back(std::move(step));
    }
    std::error_code error;
    fs::create_directories(dir, error);
    g_walk.dir = dir;
    g_walk.log = std::fopen((g_walk.dir / "walk.txt").string().c_str(), "w");
    if (!g_walk.log) {
        std::fprintf(stderr, "--menu-walk: cannot write %s/walk.txt\n", dir.c_str());
        return false;
    }
    g_walk.steps = std::move(steps);
    set_scripted_pointer(&pointer_source);
    return true;
}

void menu_walk_frame() {
    ++g_walk.frame;
    g_walk.calm = active_screen_locked(0) || active_screen_locked(1) || active_screen_locked(2) ? 0 : g_walk.calm + 1;
    while (g_walk.next < g_walk.steps.size()) {
        const Step& step = g_walk.steps[g_walk.next];
        if (!g_walk.started) {
            g_walk.started = true;
            g_walk.step_start = g_walk.frame;
            if (g_walk.log) std::fprintf(g_walk.log, "-- %u: %s\n", g_walk.frame, step.text.c_str());
        }
        if (!run(step, g_walk.frame - g_walk.step_start)) return;
        ++g_walk.next;
        g_walk.started = false;
        if (!g_walk.shot_path.empty()) return;  // one picture per frame
    }
    if (g_walk.next >= g_walk.steps.size() && g_walk.shot_path.empty()) finish(0, "script done");
}

std::string menu_walk_screenshot() { return std::exchange(g_walk.shot_path, {}); }

}  // namespace loader
