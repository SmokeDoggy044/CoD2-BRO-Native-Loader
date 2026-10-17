// FramePacing (render/frame_pacing.h): per-second counts, hitches, stalls, excluded time.

#include "test_support.h"

#include <vector>

#include "frame_pacing.h"

namespace {

// A Swap every `ms` for `seconds` after `now`; returns the finished seconds.
std::vector<render::FramePacing::Second> run(render::FramePacing& p, double& now, uint32_t& frame, double ms,
                                             double seconds) {
    std::vector<render::FramePacing::Second> done;
    for (const double end = now + seconds; now + ms / 1000.0 < end + 1e-9;)
        if (auto s = p.frame(now += ms / 1000.0, ++frame)) done.push_back(*s);
    return done;
}

}  // namespace

int main() {
    render::FramePacing p;
    double now = 0.0005;
    uint32_t frame = 0;

    // Steady 30 fps: 30 frames a second, no hitches.
    auto done = run(p, now, frame, 1000.0 / 30, 3);
    CHECK(done.size() == 3);
    CHECK(done.size() == 3 && done[1].second == 1 && done[1].frames == 30 && done[1].hitches == 0,
          "%u frames, %u hitches", done[1].frames, done[1].hitches);

    // One 100 ms frame is a hitch and the worst frame of its second.
    p.frame(now += 0.1, ++frame);
    const uint32_t slow = frame;
    done = run(p, now, frame, 1000.0 / 30, 2);
    CHECK(!done.empty() && done[0].hitches == 1 && done[0].worst_frame == slow, "%u hitches",
          done.empty() ? 0 : done[0].hitches);
    CHECK(!done.empty() && done[0].worst_ms > 99 && done[0].worst_ms < 101);
    CHECK(!p.take_stall());

    // A 3 s gap is a stall, not a frame.
    p.frame(now += 3, ++frame);
    const auto stall = p.take_stall();
    CHECK(stall && stall->seconds > 2.99 && stall->seconds < 3.01 && stall->frame == frame);
    done = run(p, now, frame, 1000.0 / 30, 2);
    CHECK(!done.empty() && done[0].worst_ms < 3000);

    // Excluded time (a paused prompt) neither stalls nor hitches.
    p.exclude(5);
    p.frame(now += 5 + 1.0 / 30, ++frame);
    CHECK(!p.take_stall());
    done = run(p, now, frame, 1000.0 / 30, 2);
    CHECK(!done.empty() && done[0].hitches == 0 && done[0].worst_ms < 40);

    // 60 fps with a 40 ms frame: a hitch there too.
    run(p, now, frame, 1000.0 / 60, 2);
    p.frame(now += 0.04, ++frame);
    done = run(p, now, frame, 1000.0 / 60, 2);
    CHECK(!done.empty() && done[0].hitches == 1);

    // 30 fps on 60 Hz vblanks: a 50 ms frame (missed vblank) is a hitch, 41 ms is not.
    run(p, now, frame, 1000.0 / 30, 2);
    p.frame(now += 0.041, ++frame);
    p.frame(now += 0.025, ++frame);
    done = run(p, now, frame, 1000.0 / 30, 2);
    CHECK(!done.empty() && done[0].hitches == 0, "%u hitches", done.empty() ? 0 : done[0].hitches);
    p.frame(now += 0.050, ++frame);
    done = run(p, now, frame, 1000.0 / 30, 2);
    CHECK(!done.empty() && done[0].hitches == 1);

    // A deliberate switch from 60 to 30 fps (a movie) is no hitch.
    run(p, now, frame, 1000.0 / 60, 2);
    p.rate_changed();
    done = run(p, now, frame, 1000.0 / 30, 2);
    CHECK(!done.empty() && done[0].hitches == 0, "%u hitches", done.empty() ? 0 : done[0].hitches);

    return test::g_failures ? 1 : 0;
}
