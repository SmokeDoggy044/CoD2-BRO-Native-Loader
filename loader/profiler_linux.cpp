// Sampling profiler (see profiler.h): a thread sends a real-time signal to every
// other thread each millisecond; the handler stores the interrupted EIP and thread.
// Title threads run with FS pointing at their fake KPCR; the handler only reads the
// signal context and calls gettid, so that does not matter.

#include "profiler.h"

#include <dirent.h>
#include <dlfcn.h>
#include <signal.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
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
#include "symbols.h"
#include "xbe.h"
#include "xbox_d3d_api.h"

namespace loader {

namespace {

struct Sample {
    uint32_t eip;
    uint32_t tid;
};

constexpr size_t kMaxSamples = size_t{1} << 21;
Sample* g_samples = nullptr;
std::atomic<size_t> g_count{0};

pid_t gettid_() { return static_cast<pid_t>(syscall(SYS_gettid)); }

void on_sample(int, siginfo_t*, void* context) {
    const int saved_errno = errno;
    const size_t i = g_count.fetch_add(1, std::memory_order_relaxed);
    if (i < kMaxSamples) {
        const auto& gregs = static_cast<ucontext_t*>(context)->uc_mcontext.gregs;
        g_samples[i] = {static_cast<uint32_t>(gregs[REG_EIP]), static_cast<uint32_t>(gettid_())};
    }
    errno = saved_errno;
}

std::string thread_name(pid_t tid) {
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/self/task/%d/comm", static_cast<int>(tid));
    char name[64] = {};
    if (std::FILE* file = std::fopen(path, "r")) {
        if (std::fgets(name, sizeof(name), file)) name[std::strcspn(name, "\n")] = '\0';
        std::fclose(file);
    }
    return name;
}

// Whether the thread is running or runnable now (state R in /proc), as opposed to
// waiting in the kernel: only those are sampled, so waits do not count as time spent.
bool running(pid_t tid) {
    char path[64], text[256];
    std::snprintf(path, sizeof(path), "/proc/self/task/%d/stat", static_cast<int>(tid));
    std::FILE* file = std::fopen(path, "r");
    if (!file) return false;
    const size_t length = std::fread(text, 1, sizeof(text) - 1, file);
    std::fclose(file);
    text[length] = '\0';
    const char* end = std::strrchr(text, ')');  // the name may hold spaces
    return end && end[1] == ' ' && end[2] == 'R';
}

std::vector<pid_t> threads() {
    std::vector<pid_t> result;
    if (DIR* dir = opendir("/proc/self/task")) {
        while (dirent* entry = readdir(dir))
            if (entry->d_name[0] != '.') result.push_back(static_cast<pid_t>(std::atoi(entry->d_name)));
        closedir(dir);
    }
    return result;
}

// Where an address is: a title function, or a host library and symbol. Code without
// a dynamic symbol (static functions, the loader itself) is given as the file and
// its offset there, for addr2line.
std::string place(uint32_t eip, const XbeImage& image, const SymbolTable& symbols) {
    if (image.contains(eip)) return "title " + symbols.function_at(eip);
    Dl_info info{};
    if (dladdr(reinterpret_cast<void*>(uintptr_t{eip}), &info) && info.dli_fname) {
        const char* file = std::strrchr(info.dli_fname, '/');
        file = file ? file + 1 : info.dli_fname;
        if (info.dli_sname) return std::string(file) + ": " + info.dli_sname;
        char offset[32];
        std::snprintf(offset, sizeof(offset), " +0x%x", static_cast<unsigned>(eip - reinterpret_cast<uintptr_t>(info.dli_fbase)));
        return std::string(file) + offset;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%08x (no module)", eip);
    return buffer;
}

// start < 0: automatic, at the first second under kSlowFps once the game has shown
// kWarmupFrames (loading and the first menus are slow anyway).
constexpr uint32_t kSlowFps = 24, kWarmupFrames = 300;

void run(double start, double seconds, const XbeImage* image, const SymbolTable* symbols) {
    using Clock = std::chrono::steady_clock;
    if (start >= 0) {
        std::this_thread::sleep_until(platform::session_start() + std::chrono::duration_cast<Clock::duration>(
                                                                      std::chrono::duration<double>(start)));
    } else {
        uint32_t fps = 0;
        for (;;) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            fps = render::frames_last_second();
            if (render::frames_shown() >= kWarmupFrames && fps > 0 && fps < kSlowFps) break;
        }
        LOG_INFO("profile", "automatic start: the last second had %u frames", fps);
    }
    const int signal = SIGRTMIN + 4;
    struct sigaction action{};
    action.sa_sigaction = on_sample;
    action.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&action.sa_mask);
    sigaction(signal, &action, nullptr);
    LOG_INFO("profile", "sampling every thread for %.0f s", seconds);

    const pid_t pid = getpid(), self = gettid_();
    std::map<pid_t, std::string> names;
    std::vector<pid_t> tids;
    auto refresh = Clock::now();
    size_t ticks = 0;
    const auto end = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));
    while (Clock::now() < end) {
        if (Clock::now() >= refresh) {
            tids = threads();
            for (pid_t tid : tids)
                if (!names.count(tid)) names[tid] = thread_name(tid);
            refresh = Clock::now() + std::chrono::milliseconds(100);
        }
        for (pid_t tid : tids)
            if (tid != self && running(tid)) syscall(SYS_tgkill, pid, tid, signal);
        ++ticks;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const size_t count = std::min(g_count.exchange(kMaxSamples, std::memory_order_relaxed), kMaxSamples);

    struct Thread {
        size_t samples = 0;
        std::unordered_map<std::string, size_t> places;
    };
    std::map<uint32_t, Thread> by_thread;
    std::unordered_map<uint32_t, std::string> cache;
    for (size_t i = 0; i < count; ++i) {
        const Sample& s = g_samples[i];
        auto it = cache.find(s.eip);
        if (it == cache.end()) it = cache.emplace(s.eip, place(s.eip, *image, *symbols)).first;
        Thread& t = by_thread[s.tid];
        ++t.samples;
        ++t.places[it->second];
    }
    // Every sampled address with its count, for addr2line on the loader itself.
    if (const char* out = std::getenv("XB_PROFILE_OUT")) {
        std::map<std::pair<uint32_t, uint32_t>, size_t> raw;
        for (size_t i = 0; i < count; ++i) ++raw[{g_samples[i].tid, g_samples[i].eip}];
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
    LOG_INFO("profile", "%zu ticks; per thread: busy = ticks it was running (100%% = a whole core), then "
             "where those samples were:", ticks);
    for (const auto& [tid, t] : order) {
        std::vector<std::pair<std::string, size_t>> places(t->places.begin(), t->places.end());
        std::sort(places.begin(), places.end(), [](auto& a, auto& b) { return a.second > b.second; });
        LOG_INFO("profile", "thread %u (%s): busy %.1f%%", tid, names[static_cast<pid_t>(tid)].c_str(),
                 ticks ? 100.0 * t->samples / ticks : 0.0);
        if (ticks && t->samples * 50 < ticks) continue;  // under 2%: no breakdown
        for (size_t i = 0; i < places.size() && i < 40; ++i)
            LOG_INFO("profile", "  %5.1f%%  %s", 100.0 * places[i].second / t->samples, places[i].first.c_str());
    }
}

}  // namespace

void start_profiler_from_env(const XbeImage& image, const SymbolTable& symbols) {
    const char* env = std::getenv("XB_PROFILE");
    if (!env) return;
    double start = 0, seconds = 10;
    if (std::strncmp(env, "auto", 4) == 0) {
        start = -1;
        if (env[4] == ',') seconds = std::atof(env + 5);
    } else if (std::sscanf(env, "%lf,%lf", &start, &seconds) != 2) {
        seconds = 0;
    }
    if (seconds <= 0) {
        LOG_WARN("profile", "XB_PROFILE needs START,SECONDS or auto[,SECONDS] (got %s)", env);
        return;
    }
    g_samples = new Sample[kMaxSamples];
    std::thread(run, start, seconds, &image, &symbols).detach();
}

}  // namespace loader
