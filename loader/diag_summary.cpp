#include "diag_summary.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace loader {

namespace fs = std::filesystem;

namespace {

// One log line: "[    12.345] warn  render   text" (platform/log.cpp).
struct LogLine {
    double seconds = 0;
    std::string level, category, text;
};

bool parse_line(const std::string& line, LogLine& out) {
    if (line.size() < 3 || line[0] != '[') return false;
    const size_t close = line.find(']');
    if (close == std::string::npos) return false;
    out.seconds = std::atof(line.c_str() + 1);
    size_t i = close + 1;
    auto word = [&](std::string& to) {
        while (i < line.size() && line[i] == ' ') ++i;
        const size_t begin = i;
        while (i < line.size() && line[i] != ' ') ++i;
        to = line.substr(begin, i - begin);
    };
    word(out.level);
    word(out.category);
    while (i < line.size() && line[i] == ' ') ++i;
    out.text = line.substr(i);
    return !out.level.empty();
}

struct Problem {
    double first = 0;
    uint32_t count = 0;
    std::string picture;  // an automatic mark's screenshot, if it was marked
};

bool starts_with(const std::string& text, const char* prefix) { return text.rfind(prefix, 0) == 0; }

// "frames" lines (render/frame_pacing.h): one per second, and one per stall.
struct FrameSecond {
    long second = 0;
    unsigned fps = 0, frame = 0, hitches = 0;
    double worst_ms = 0;
};
struct FrameStall {
    double at = 0, seconds = 0;
};
struct FrameTarget {
    double at = 0;
    unsigned fps = 0;  // what the title aims at from then on (a movie at 30 in a 60 fps game)
};

}  // namespace

void write_diag_summary(const fs::path& dir) {
    std::ifstream log(dir / "log.txt");
    if (!log) return;

    // Read the session: everything after the last launch that was not a reboot.
    std::vector<LogLine> lines;
    bool rebooting = false;
    uint32_t reboots = 0;
    for (std::string text; std::getline(log, text);) {
        LogLine line;
        if (!parse_line(text, line)) continue;
        if (line.category == "dump") continue;
        if (line.category == "diag" && starts_with(line.text, "diagnostics in ")) {
            if (rebooting)
                ++reboots;
            else
                lines.clear(), reboots = 0;
        }
        rebooting = line.category == "loader" && starts_with(line.text, "reboot: starting");
        lines.push_back(std::move(line));
    }
    if (lines.empty()) return;

    std::map<std::string, Problem> errors, warnings;  // by "category: text"
    std::map<std::string, std::string> auto_pictures;  // event -> automark_<n>.bmp
    std::vector<std::string> fatal;                    // fatal/crash lines, in order
    uint32_t marks = 0, auto_marks = 0;
    bool closed = false;
    std::vector<FrameSecond> frame_seconds;
    std::vector<FrameStall> stalls;
    std::vector<FrameTarget> targets;
    for (const LogLine& line : lines) {
        if (line.category == "frames") {
            FrameSecond f;
            FrameStall stall;
            if (std::sscanf(line.text.c_str(), "%lds: %u fps, worst %lf ms (frame %u), %u hitches", &f.second, &f.fps,
                            &f.worst_ms, &f.frame, &f.hitches) == 5)
                frame_seconds.push_back(f);
            else if (std::sscanf(line.text.c_str(), "stall %lf s", &stall.seconds) == 1)
                stall.at = line.seconds, stalls.push_back(stall);
            else if (FrameTarget t; std::sscanf(line.text.c_str(), "target %u fps", &t.fps) == 1)
                t.at = line.seconds, targets.push_back(t);
            continue;
        }
        if (line.category == "diag") {
            unsigned n = 0;
            char picture[64];
            int end = -1;  // %n: only "MARK n at", not the "MARK n note:"/"MARK n:" lines
            if (std::sscanf(line.text.c_str(), "MARK %u at%n", &n, &end) == 1 && end > 0) ++marks;
            const size_t colon = line.text.find("): ");
            if (std::sscanf(line.text.c_str(), "AUTO MARK %u at %*ds, frame %*u (%63[^)])", &n, picture) == 2 &&
                colon != std::string::npos) {
                auto_marks = std::max<uint32_t>(auto_marks, n);
                auto_pictures[line.text.substr(colon + 3)] = picture;
            }
            continue;
        }
        if (line.category == "render" && line.text == "window closed") closed = true;
        if (line.category == "fatal" || line.category == "crash") {
            if (fatal.size() < 16) fatal.push_back(line.category + ": " + line.text);
            continue;
        }
        if (line.level != "warn" && line.level != "error") continue;
        const bool error = line.level == "error";
        Problem& p = (error ? errors : warnings)[line.category + ": " + line.text];
        if (!p.count++) p.first = line.seconds;
    }
    for (auto* group : {&errors, &warnings})
        for (auto& [message, p] : *group) {
            const bool error = group == &errors;
            auto it = auto_pictures.find((error ? "error " : "") + message);
            if (it != auto_pictures.end()) p.picture = it->second;
        }

    // The periodic screenshots, by second, for problems that were not marked.
    std::vector<std::pair<long, std::string>> shots;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        const std::string name = entry.path().filename().string();
        if (starts_with(name, "auto_") && name.size() > 9 && name.ends_with(".bmp"))
            shots.emplace_back(std::atol(name.c_str() + 5), name);
    }
    std::sort(shots.begin(), shots.end());
    auto nearest_shot = [&](double seconds) -> std::string {
        auto it = std::lower_bound(shots.begin(), shots.end(), std::make_pair(static_cast<long>(seconds), std::string()));
        if (it != shots.end()) return it->second + " (after)";
        return shots.empty() ? "-" : shots.back().second + " (before)";
    };

    std::FILE* out = std::fopen((dir / "summary.txt").string().c_str(), "w");
    if (!out) return;
    const double length = lines.back().seconds;
    std::fprintf(out, "Playtest summary (written when the game exited)\n\n");
    std::fprintf(out, "Length: %.0f s, %u title reboot%s\n", length, reboots, reboots == 1 ? "" : "s");
    if (!fatal.empty()) {
        std::fprintf(out, "Ended: the game stopped at %.0f s:\n", length);
        for (const std::string& f : fatal) std::fprintf(out, "  %s\n", f.c_str());
    } else if (closed) {
        std::fprintf(out, "Ended: you closed the window at %.0f s\n", length);
    } else if (lines.back().category == "loader" && starts_with(lines.back().text, "stopped by ")) {
        std::fprintf(out, "Ended: %s at %.0f s\n", lines.back().text.c_str(), length);
    } else {
        const LogLine& last = lines.back();
        std::fprintf(out, "Ended: the game exited at %.0f s (last line: %s %s)\n", length, last.category.c_str(),
                     last.text.c_str());
    }
    std::fprintf(out, "Marks: %u by you (F12), %u automatic - see marks.txt\n", marks, auto_marks);

    auto table = [&](const char* title, const std::map<std::string, Problem>& group) {
        std::vector<std::pair<std::string, Problem>> sorted(group.begin(), group.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second.first < b.second.first; });
        std::fprintf(out, "\n%s: %zu distinct\n", title, sorted.size());
        if (sorted.empty()) return;
        std::fprintf(out, "  %7s  %6s  %-28s  %s\n", "first", "count", "screenshot", "message");
        for (const auto& [message, p] : sorted) {
            const std::string picture = p.picture.empty() ? nearest_shot(p.first) : p.picture;
            std::fprintf(out, "  %6.0fs  %6u  %-28s  %s\n", p.first, p.count, picture.c_str(), message.c_str());
        }
    };
    table("Errors", errors);
    table("Warnings", warnings);

    // Frame rate. Seconds a stall begins or ends in are partly idle: left out.
    std::set<long> partial;
    for (const FrameStall& stall : stalls) {
        partial.insert(static_cast<long>(stall.at));
        partial.insert(static_cast<long>(stall.at - stall.seconds));
    }
    std::vector<FrameSecond> full;
    for (const FrameSecond& f : frame_seconds)
        if (!partial.count(f.second)) full.push_back(f);
    if (!full.empty()) {
        std::vector<unsigned> fps;
        unsigned long frames = 0, hitches = 0;
        for (const FrameSecond& f : full) fps.push_back(f.fps), frames += f.fps, hitches += f.hitches;
        std::nth_element(fps.begin(), fps.begin() + fps.size() / 2, fps.end());
        const unsigned typical = fps[fps.size() / 2];
        const unsigned slow_below = typical * 4 / 5;
        std::fprintf(out, "\nFrame rate: typical %u fps, average %.1f over %zu s\n", typical,
                     static_cast<double>(frames) / full.size(), full.size());

        // The title's own target at time t (none = no limit); a second judged by the
        // lower target at its start and end.
        auto target_at = [&](double t) {
            unsigned fps = UINT_MAX;
            for (const FrameTarget& target : targets)
                if (target.at <= t) fps = target.fps;
            return fps;
        };
        std::vector<FrameSecond> slow;
        unsigned lower_target_seconds = 0;
        for (const FrameSecond& f : full) {
            const unsigned target = std::min(target_at(static_cast<double>(f.second)), target_at(f.second + 1.0));
            if (target < typical) ++lower_target_seconds;
            if (f.fps < std::min(typical, target) * 4 / 5) slow.push_back(f);
        }
        if (lower_target_seconds)
            std::fprintf(out, "  At a lower rate on purpose (movies): %u s, judged by that rate\n",
                         lower_target_seconds);
        std::sort(slow.begin(), slow.end(), [](const auto& a, const auto& b) { return a.fps < b.fps; });
        std::fprintf(out, "  Slow seconds (under %u fps): %zu\n", slow_below, slow.size());
        if (!slow.empty()) std::fprintf(out, "  %7s  %5s  %8s  %s\n", "second", "fps", "worst ms", "screenshot");
        for (size_t i = 0; i < slow.size() && i < 10; ++i)
            std::fprintf(out, "  %6lds  %5u  %8.0f  %s\n", slow[i].second, slow[i].fps, slow[i].worst_ms,
                         nearest_shot(static_cast<double>(slow[i].second)).c_str());

        std::vector<FrameSecond> rough;
        for (const FrameSecond& f : full)
            if (f.hitches) rough.push_back(f);
        std::sort(rough.begin(), rough.end(), [](const auto& a, const auto& b) { return a.worst_ms > b.worst_ms; });
        std::fprintf(out, "  Hitches (a frame over 1.4x the recent average, e.g. a missed vblank): %lu in %zu s\n", hitches, rough.size());
        if (!rough.empty())
            std::fprintf(out, "  %7s  %8s  %7s  %7s  %s\n", "second", "worst ms", "frame", "hitches", "screenshot");
        for (size_t i = 0; i < rough.size() && i < 10; ++i)
            std::fprintf(out, "  %6lds  %8.0f  %7u  %7u  %s\n", rough[i].second, rough[i].worst_ms, rough[i].frame,
                         rough[i].hitches, nearest_shot(static_cast<double>(rough[i].second)).c_str());
    }
    if (!stalls.empty()) {
        double total = 0;
        for (const FrameStall& stall : stalls) total += stall.seconds;
        std::sort(stalls.begin(), stalls.end(), [](const auto& a, const auto& b) { return a.seconds > b.seconds; });
        std::fprintf(out, "  Stalls of 1 s or more (loading, pauses): %zu, %.0f s in all; longest:", stalls.size(), total);
        for (size_t i = 0; i < stalls.size() && i < 5; ++i)
            std::fprintf(out, " %.1f s at %.0f s%s", stalls[i].seconds, stalls[i].at, i + 1 < stalls.size() && i < 4 ? "," : "");
        std::fprintf(out, "\n");
    }
    std::fclose(out);
}

}  // namespace loader
