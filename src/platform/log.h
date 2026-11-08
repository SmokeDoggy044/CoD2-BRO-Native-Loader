#pragma once

#include <chrono>
#include <cstdarg>

#include "compiler.h"

namespace platform {

enum class LogLevel { Trace, Debug, Info, Warn, Error };

void log_set_level(LogLevel level);
// Also writes every line to `path` (appending). False if it cannot be opened. With
// max_bytes, stops writing to the file (stderr goes on) once it reaches that size, so
// one message repeated every frame can't fill a player's disk.
bool log_set_file(const char* path, long max_bytes = 0);
// Time origin of log timestamps and other "seconds since start" values. A relaunched
// process (the title rebooting into another XBE) sets it to the first one's.
std::chrono::steady_clock::time_point session_start();
// Only before other threads start.
void set_session_start(std::chrono::steady_clock::time_point start);
bool log_enabled(LogLevel level);
// Called with every warning and error after it is written (diagnostics), on the
// logging thread and outside the log's lock; it must not log. Null = none.
using LogObserver = void (*)(LogLevel level, const char* category, const char* text);
void log_set_observer(LogObserver observer);
void logf(LogLevel level, const char* category, const char* fmt, ...) PLATFORM_PRINTF(3, 4);
void vlogf(LogLevel level, const char* category, const char* fmt, va_list args);

}  // namespace platform

#define PLATFORM_LOG(level, category, ...)                                  \
    do {                                                                    \
        if (::platform::log_enabled(level))                                 \
            ::platform::logf(level, category, __VA_ARGS__);                 \
    } while (0)

#define LOG_TRACE(category, ...) PLATFORM_LOG(::platform::LogLevel::Trace, category, __VA_ARGS__)
#define LOG_DEBUG(category, ...) PLATFORM_LOG(::platform::LogLevel::Debug, category, __VA_ARGS__)
#define LOG_INFO(category, ...) PLATFORM_LOG(::platform::LogLevel::Info, category, __VA_ARGS__)
#define LOG_WARN(category, ...) PLATFORM_LOG(::platform::LogLevel::Warn, category, __VA_ARGS__)
#define LOG_ERROR(category, ...) PLATFORM_LOG(::platform::LogLevel::Error, category, __VA_ARGS__)
