#include "crash_note.h"

#include <cstdarg>
#include <cstring>

namespace platform {

namespace {
thread_local char g_note[192] = {};
}  // namespace

const char* crash_note() { return g_note; }

void CrashNote::save() { std::memcpy(before_, g_note, sizeof g_note); }

void CrashNote::set(const char* format, ...) {
    va_list args;
    va_start(args, format);
    std::vsnprintf(g_note, sizeof g_note, format, args);
    va_end(args);
}

void CrashNote::restore() { std::memcpy(g_note, before_, sizeof g_note); }

}  // namespace platform
