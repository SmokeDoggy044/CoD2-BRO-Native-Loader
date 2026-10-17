#pragma once

// --diag frame pacing: frame times (Swap to Swap, minus the time --diag itself spent)
// summed up per second of the session. A hitch is a frame over 1.4 times the average of
// the 30 before it and at least 10 ms longer - at a vblank-locked 30 fps, a frame that
// missed its vblank (50 ms). A gap of a second or more is a stall (loading, a
// breakpoint) and stays out of the per-second numbers. After a deliberate change of
// the frame rate (rate_changed: a movie at 30 fps in a 60 fps game) the frames are
// judged against the new rate only.

#include <cstdint>
#include <optional>
#include <utility>

namespace render {

class FramePacing {
public:
    struct Second {
        int64_t second = 0;          // since the session start
        uint32_t frames = 0;
        double worst_ms = 0;
        uint32_t worst_frame = 0;    // swap count of the worst frame
        uint32_t hitches = 0;
    };
    struct Stall {
        double seconds = 0;
        uint32_t frame = 0;          // the frame after the gap
    };

    static constexpr double kStallSeconds = 1.0;
    static constexpr int kHistory = 30;

    // One Swap at session time `now` (seconds). Returns the previous second when this
    // frame starts a new one; a stall before this frame is left in take_stall().
    std::optional<Second> frame(double now, uint32_t frame_number) {
        std::optional<Second> finished;
        const auto second = static_cast<int64_t>(now);
        if (have_second_ && second != current_.second) {
            if (current_.frames) finished = current_;
            have_second_ = false;
        }
        if (!have_second_) {
            current_ = Second{second};
            have_second_ = true;
        }
        if (last_) {
            const double ms = (now - *last_) * 1000.0;
            if (ms >= kStallSeconds * 1000.0) {
                stall_ = Stall{ms / 1000.0, frame_number};
                history_sum_ = 0, history_count_ = history_next_ = 0;  // judge the frames after it on their own
            } else {
                ++current_.frames;
                if (ms > current_.worst_ms) current_.worst_ms = ms, current_.worst_frame = frame_number;
                if (history_count_ >= kHistory / 2) {
                    const double average = history_sum_ / history_count_;
                    if (ms > 1.4 * average && ms > average + 10) ++current_.hitches;
                }
                push_history(ms);
            }
        }
        last_ = now;
        return finished;
    }

    // The title now waits a different number of vblanks per frame.
    void rate_changed() { history_sum_ = 0, history_count_ = history_next_ = 0; }

    std::optional<Stall> take_stall() { return std::exchange(stall_, std::nullopt); }

    // Time that should not count as a frame (a paused prompt, saving a screenshot).
    void exclude(double seconds) {
        if (last_) *last_ += seconds;
    }

private:
    void push_history(double ms) {
        if (history_count_ == kHistory) history_sum_ -= history_[history_next_];
        else ++history_count_;
        history_[history_next_] = ms;
        history_sum_ += ms;
        history_next_ = (history_next_ + 1) % kHistory;
    }

    std::optional<double> last_;
    Second current_;
    bool have_second_ = false;
    std::optional<Stall> stall_;
    double history_[kHistory] = {};
    double history_sum_ = 0;
    int history_count_ = 0, history_next_ = 0;
};

}  // namespace render
