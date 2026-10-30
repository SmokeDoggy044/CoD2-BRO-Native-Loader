// The reverb adjustments' text form (settings.ini "reverb", --reverb). Portable.

#include <cstdio>

#include "reverb_tuning.h"

namespace {

int g_failures = 0;

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            ++g_failures;                                                   \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        }                                                                   \
    } while (0)

}  // namespace

int main() {
    using audio::ReverbTuning;
    // Unknown keys, out-of-range and malformed values are ignored; the rest apply.
    const ReverbTuning t = ReverbTuning::parse("amount=-3 decay=1.5 bogus=2 brightness=9 clutter=0.1 reflections=x");
    // Compared through float variables: 32-bit x87 builds evaluate float literals at long double
    // precision (0.1f would not equal a parsed 0.1).
    const float tenth = 0.1f;
    CHECK(t.amount == -3.0f && t.decay == 1.5f && t.clutter == tenth);
    CHECK(t.brightness == 1.0f && t.reflections == 0.0f);
    // Only what differs from the defaults is written, and it reads back the same.
    CHECK(t.format() == "amount=-3 decay=1.5 clutter=0.1");
    CHECK(ReverbTuning::parse(t.format()) == t);
    CHECK(ReverbTuning{}.format().empty());
    CHECK(ReverbTuning::parse("") == ReverbTuning{});
    if (g_failures) return 1;
    std::printf("reverb_tuning_test: all passed\n");
    return 0;
}
