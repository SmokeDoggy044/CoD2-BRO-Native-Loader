// Sampling profiler, Linux side (profiler_os.h): a thread sends a real-time signal to
// every other running thread each millisecond; the handler stores the interrupted EIP
// and thread. Title threads run with FS pointing at their fake KPCR; the handler only
// reads the signal context and calls gettid, so that does not matter.

#include "profiler_os.h"

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
#include <thread>

namespace loader::profiler_os {

namespace {

constexpr size_t kMaxSamples = size_t{1} << 21;
Sample* g_samples = nullptr;
std::atomic<size_t> g_count{0};

pid_t gettid_() { return static_cast<pid_t>(syscall(SYS_gettid)); }

// Names read while sampling, so threads that exited before the report keep theirs.
std::map<uint32_t, std::string> g_names;

std::string read_thread_name(uint32_t tid) {
    char path[64];
    std::snprintf(path, sizeof(path), "/proc/self/task/%u/comm", tid);
    char name[64] = {};
    if (std::FILE* file = std::fopen(path, "r")) {
        if (std::fgets(name, sizeof(name), file)) name[std::strcspn(name, "\n")] = '\0';
        std::fclose(file);
    }
    return name;
}

void on_sample(int, siginfo_t*, void* context) {
    const int saved_errno = errno;
    const size_t i = g_count.fetch_add(1, std::memory_order_relaxed);
    if (i < kMaxSamples) {
        const auto& gregs = static_cast<ucontext_t*>(context)->uc_mcontext.gregs;
        g_samples[i] = {static_cast<uint32_t>(gregs[REG_EIP]), static_cast<uint32_t>(gettid_())};
    }
    errno = saved_errno;
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

}  // namespace

size_t sample(double seconds, std::vector<Sample>& samples) {
    using Clock = std::chrono::steady_clock;
    g_samples = new Sample[kMaxSamples];
    const int signal = SIGRTMIN + 4;
    struct sigaction action{};
    action.sa_sigaction = on_sample;
    action.sa_flags = SA_SIGINFO | SA_RESTART;
    sigemptyset(&action.sa_mask);
    sigaction(signal, &action, nullptr);

    const pid_t pid = getpid(), self = gettid_();
    std::vector<pid_t> tids;
    auto refresh = Clock::now();
    size_t ticks = 0;
    const auto end = Clock::now() + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(seconds));
    while (Clock::now() < end) {
        if (Clock::now() >= refresh) {
            tids = threads();
            for (pid_t tid : tids)
                if (!g_names.count(tid)) g_names[tid] = read_thread_name(tid);
            refresh = Clock::now() + std::chrono::milliseconds(100);
        }
        for (pid_t tid : tids)
            if (tid != self && running(tid)) syscall(SYS_tgkill, pid, tid, signal);
        ++ticks;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    // Late signals see a full buffer and store nothing; the buffer stays allocated.
    const size_t count = std::min(g_count.exchange(kMaxSamples, std::memory_order_relaxed), kMaxSamples);
    samples.assign(g_samples, g_samples + count);
    return ticks;
}

double busy(uint32_t) { return -1; }

std::string thread_name(uint32_t tid) {
    const auto it = g_names.find(tid);
    return it != g_names.end() ? it->second : read_thread_name(tid);
}

// Code without a dynamic symbol (static functions, the loader itself) is given as the
// file and its offset there.
std::string host_place(uint32_t eip) {
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

}  // namespace loader::profiler_os
