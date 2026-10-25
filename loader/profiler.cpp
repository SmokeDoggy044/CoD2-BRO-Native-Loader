// Sampling profiler (see profiler.h): when to start, and the report. The samples
// come from profiler_os.h.

#include "profiler.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "log.h"
#include "patches.h"
#include "profiler_os.h"
#include "symbols.h"
#include "xbe.h"
#include "xbox_d3d_api.h"

namespace loader {

namespace {

using profiler_os::Sample;

std::string place(uint32_t eip, const XbeImage& image, const SymbolTable& symbols) {
    if (image.contains(eip)) return "title " + symbols.function_at(eip);
    return profiler_os::host_place(eip);
}

// start < 0: automatic, at each second under kSlowFps once the game has shown
// kWarmupFrames (loading and the first menus are slow anyway), at most once every
// kAutoInterval seconds and kAutoReports times a run, so a whole play session's slow
// spots get a report, not just the first one.
constexpr uint32_t kSlowFps = 24, kWarmupFrames = 300, kAutoReports = 20;
constexpr auto kAutoInterval = std::chrono::seconds(60);

void report(double seconds, const XbeImage& image, const SymbolTable& symbols);

void run(double start, double seconds, const XbeImage* image, const SymbolTable* symbols) {
    using Clock = std::chrono::steady_clock;
    if (start >= 0) {
        std::this_thread::sleep_until(platform::session_start() + std::chrono::duration_cast<Clock::duration>(
                                                                      std::chrono::duration<double>(start)));
        LOG_INFO("profile", "sampling every thread for %.0f s", seconds);
        report(seconds, *image, *symbols);
        return;
    }
    for (uint32_t n = 1; n <= kAutoReports; ++n) {
        uint32_t fps = 0;
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            fps = render::frames_last_second();
            if (render::frames_shown() >= kWarmupFrames && fps > 0 && fps < kSlowFps) break;
        }
        const int32_t level = campaign_level();
        char where[32] = "";
        if (level >= 0) std::snprintf(where, sizeof(where), ", mission %d", level);
        LOG_INFO("profile", "automatic report %u of at most %u: the last second had %u frames%s; sampling every "
                 "thread for %.0f s", n, kAutoReports, fps, where, seconds);
        report(seconds, *image, *symbols);
        std::this_thread::sleep_for(kAutoInterval);
    }
}

void report(double seconds, const XbeImage& image, const SymbolTable& symbols) {
    std::vector<Sample> samples;
    const uint32_t frames_before = render::frames_shown();
    const size_t ticks = profiler_os::sample(seconds, samples);
    LOG_INFO("profile", "%.1f fps while sampling", (render::frames_shown() - frames_before) / seconds);

    struct Thread {
        size_t samples = 0;
        std::unordered_map<std::string, size_t> places;
    };
    std::map<uint32_t, Thread> by_thread;
    std::unordered_map<uint32_t, std::string> cache;
    for (const Sample& s : samples) {
        auto it = cache.find(s.eip);
        if (it == cache.end()) it = cache.emplace(s.eip, place(s.eip, image, symbols)).first;
        Thread& t = by_thread[s.tid];
        ++t.samples;
        ++t.places[it->second];
    }
    // Every sampled address with its count, for addr2line on the loader itself.
    if (const char* out = std::getenv("XB_PROFILE_OUT")) {
        std::map<std::pair<uint32_t, uint32_t>, size_t> raw;
        for (const Sample& s : samples) ++raw[{s.tid, s.eip}];
        if (std::FILE* file = std::fopen(out, "w")) {
            for (const auto& [key, n] : raw)
                std::fprintf(file, "%u %08x %zu %s\n", key.first, key.second, n, cache[key.second].c_str());
            std::fclose(file);
            LOG_INFO("profile", "raw samples in %s", out);
        }
    }

    std::vector<std::pair<uint32_t, const Thread*>> order;
    for (const auto& [tid, t] : by_thread) order.push_back({tid, &t});
    std::sort(order.begin(), order.end(), [](auto& a, auto& b) { return a.second->samples > b.second->samples; });
    LOG_INFO("profile", "%zu ticks; per thread: busy = share of the time it ran (100%% = a whole core), then "
             "where those samples were:", ticks);
    for (const auto& [tid, t] : order) {
        std::vector<std::pair<std::string, size_t>> places(t->places.begin(), t->places.end());
        std::sort(places.begin(), places.end(), [](auto& a, auto& b) { return a.second > b.second; });
        const double cpu = profiler_os::busy(tid);
        LOG_INFO("profile", "thread %u (%s): busy %.1f%%", tid, profiler_os::thread_name(tid).c_str(),
                 cpu >= 0 ? 100.0 * cpu : ticks ? 100.0 * t->samples / ticks : 0.0);
        if (ticks && t->samples * 50 < ticks) continue;  // under 2%: no breakdown
        for (size_t i = 0; i < places.size() && i < 40; ++i)
            LOG_INFO("profile", "  %5.1f%%  %s", 100.0 * places[i].second / t->samples, places[i].first.c_str());
    }
}

}  // namespace

void start_profiler(const char* spec, const XbeImage& image, const SymbolTable& symbols) {
    if (!spec) return;
    double start = 0, seconds = 10;
    if (std::strncmp(spec, "auto", 4) == 0) {
        start = -1;
        if (spec[4] == ',') seconds = std::atof(spec + 5);
    } else if (std::sscanf(spec, "%lf,%lf", &start, &seconds) != 2) {
        seconds = 0;
    }
    if (seconds <= 0) {
        LOG_WARN("profile", "profiling needs START,SECONDS or auto[,SECONDS] (got %s)", spec);
        return;
    }
    std::thread(run, start, seconds, &image, &symbols).detach();
}

}  // namespace loader
