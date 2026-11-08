#pragma once

// The player's adjustments to enhanced sound's reverb (settings.ini "reverb", the launcher's
// Advanced reverb, the PC menu). Header only: the game code (the probe), the loader and the
// launcher use it without linking the audio library.
//
// Text form: "amount=-3 decay=1.2 brightness=1 reflections=0 clutter=0.07"; keys left out keep
// their defaults, unknown keys and out-of-range values are ignored.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

namespace audio {

struct ReverbTuning {
    float amount = 0.0f;       // dB on the reverb's level (-12..12)
    float decay = 1.0f;        // times the decay time (0.25..4)
    float brightness = 1.0f;   // times the high-frequency decay (0.5..2)
    float reflections = 0.0f;  // dB on the early reflections (-12..12)
    float clutter = 0.07f;     // absorption of what the probe's rays do not see (0..0.3)

    bool operator==(const ReverbTuning&) const = default;

    struct Range {
        const char* key;
        float ReverbTuning::*field;
        float min, max;
    };
    static constexpr Range kRanges[] = {
        {"amount", &ReverbTuning::amount, -12.0f, 12.0f},
        {"decay", &ReverbTuning::decay, 0.25f, 4.0f},
        {"brightness", &ReverbTuning::brightness, 0.5f, 2.0f},
        {"reflections", &ReverbTuning::reflections, -12.0f, 12.0f},
        {"clutter", &ReverbTuning::clutter, 0.0f, 0.3f},
    };

    static ReverbTuning parse(const std::string& text) {
        ReverbTuning t;
        std::istringstream words(text);
        std::string word;
        while (words >> word) {
            const size_t eq = word.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = word.substr(0, eq);
            char* end = nullptr;
            const float value = std::strtof(word.c_str() + eq + 1, &end);
            if (end == word.c_str() + eq + 1 || *end) continue;
            for (const Range& r : kRanges)
                if (key == r.key && value >= r.min && value <= r.max) t.*r.field = value;
        }
        return t;
    }

    // Only the values that differ from the defaults ("" for none).
    std::string format() const {
        const ReverbTuning d;
        std::string text;
        for (const Range& r : kRanges) {
            if (this->*r.field == d.*r.field) continue;
            char item[48];
            std::snprintf(item, sizeof(item), "%s%s=%g", text.empty() ? "" : " ", r.key, double(this->*r.field));
            text += item;
        }
        return text;
    }
};

}  // namespace audio
