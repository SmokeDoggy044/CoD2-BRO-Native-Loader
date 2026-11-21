#pragma once

// What this thread is doing, for the crash report: a crash inside a library (a GPU driver) names no code of ours, so
// the calls into it that can crash set a note first (which shader, which pipeline). Memory only, no I/O: cheap next
// to the call it describes. The crash handler (crash_win32.cpp, crash_posix.cpp) prints the crashing thread's note.

#include <cstdio>

namespace platform {

// This thread's note, empty when none.
const char* crash_note();

// Sets this thread's note (printf-style, cut at 191 characters) for its lifetime, then restores the one before (so
// notes nest: "linking pipeline" around "compiling shader").
class CrashNote {
public:
    template <class... Args>
    explicit CrashNote(const char* format, Args... args) {
        save();
        if constexpr (sizeof...(Args) == 0)
            set("%s", format);
        else
            set(format, args...);
    }
    ~CrashNote() { restore(); }
    CrashNote(const CrashNote&) = delete;
    CrashNote& operator=(const CrashNote&) = delete;

private:
    void save();
    void set(const char* format, ...);
    void restore();
    char before_[192];
};

}  // namespace platform
