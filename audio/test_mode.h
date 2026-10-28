#pragma once

// Test processes of the loader's comparison harness (--compare) run the DirectSound
// replacements on their own copy of the game (docs/comparison-harness.md, "Sound
// calls"). There they have no output: no OpenAL calls, no device, no mixer thread -
// voices keep the state they had at the fork, so both sides see the same play
// positions and statuses. A lock that another thread (the parent's OpenAL mixer, a
// title thread) held at the fork stays held in the copy: instead of waiting forever,
// the harness's `lock_held` hook ends the test (inconclusive, with that reason).

#include <cstdint>
#include <mutex>

namespace audio {

enum class TestLock : uint32_t { State = 1, Voice = 2, Callbacks = 3 };

struct TestMode {
    const volatile uint8_t* in_test = nullptr;  // nonzero in test processes
    void (*lock_held)(TestLock which) = nullptr;  // does not return
};
extern TestMode g_test_mode;

// Call once before the title runs (the loader, with --compare).
void set_test_mode(const volatile uint8_t* in_test, void (*lock_held)(TestLock which));

inline bool in_test() { return g_test_mode.in_test && *g_test_mode.in_test; }

// std::lock_guard, except that in a test process a lock held at the fork ends the test.
class Guard {
public:
    Guard(std::mutex& mutex, TestLock which) : mutex_(mutex) {
        if (!in_test())
            mutex_.lock();
        else if (!mutex_.try_lock())
            g_test_mode.lock_held(which);
    }
    ~Guard() { mutex_.unlock(); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;

private:
    std::mutex& mutex_;
};

}  // namespace audio
