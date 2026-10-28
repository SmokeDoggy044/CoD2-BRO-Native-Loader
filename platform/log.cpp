#include "log.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>

namespace platform {

namespace {

std::atomic<LogLevel> g_level{LogLevel::Info};
std::mutex g_mutex;
// Set before any other thread starts (see set_session_start).
std::chrono::steady_clock::time_point g_start = std::chrono::steady_clock::now();
std::FILE* g_file = nullptr;
long g_file_bytes = 0, g_file_limit = 0;  // 0: no limit
std::atomic<LogObserver> g_observer{nullptr};

const char* level_name(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "trace";
        case LogLevel::Debug: return "debug";
        case LogLevel::Info: return "info";
        case LogLevel::Warn: return "warn";
        case LogLevel::Error: return "error";
    }
    return "?";
}

}  // namespace

void log_set_level(LogLevel level) { g_level = level; }

std::chrono::steady_clock::time_point session_start() { return g_start; }
void set_session_start(std::chrono::steady_clock::time_point start) { g_start = start; }

bool log_set_file(const char* path, long max_bytes) {
    std::FILE* file = std::fopen(path, "a");
    if (!file) return false;
    std::fseek(file, 0, SEEK_END);  // a reboot continues the file
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) std::fclose(g_file);
    g_file = file;
    g_file_bytes = std::ftell(file);
    g_file_limit = max_bytes;
    return true;
}

void log_set_observer(LogObserver observer) { g_observer = observer; }

bool log_enabled(LogLevel level) { return level >= g_level.load(std::memory_order_relaxed); }

void vlogf(LogLevel level, const char* category, const char* fmt, va_list args) {
    if (!log_enabled(level)) return;
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - g_start)
                        .count();
    char text[2048];
    std::vsnprintf(text, sizeof(text), fmt, args);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (std::FILE* out : {stderr, g_file}) {
            if (!out) continue;
            const int written = std::fprintf(out, "[%8lld.%03lld] %-5s %-8s %s\n", static_cast<long long>(ms / 1000),
                                             static_cast<long long>(ms % 1000), level_name(level), category, text);
            std::fflush(out);
            if (out != g_file || !g_file_limit) continue;
            g_file_bytes += written > 0 ? written : 0;
            if (g_file_bytes >= g_file_limit) {
                std::fprintf(g_file, "log file size limit reached (%ld bytes); later messages are not written\n",
                             g_file_limit);
                std::fclose(g_file);
                g_file = nullptr;
            }
        }
    }
    if (level >= LogLevel::Warn)
        if (LogObserver observer = g_observer.load()) observer(level, category, text);
}

void logf(LogLevel level, const char* category, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vlogf(level, category, fmt, args);
    va_end(args);
}

}  // namespace platform
