#pragma once

// The player's volume per kind of sound (settings.ini "volume", --volume, the launcher, the PC
// menu), on top of the game's own volume slider. A plain setting: it applies to original and
// enhanced sound alike. Header only: the loader and the launcher use it without linking the
// audio library.
//
// Text form: "effects=80 ambience=60 dialogue=100 music=50" in percent; keys left out stay at
// 100, unknown keys and out-of-range values are ignored.

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

#include "sound_device.h"

namespace audio {

struct CategoryVolumes {
    float effects = 100.0f;   // percent: gunfire, explosions, footsteps, the menus' sounds
    float ambience = 100.0f;  // the levels' background beds: distant battle, wind, rain, rooms
    float dialogue = 100.0f;  // the game's VOICE group: orders, chatter, briefings
    float music = 100.0f;

    bool operator==(const CategoryVolumes&) const = default;

    struct Key {
        const char* key;
        float CategoryVolumes::*field;
    };
    static constexpr Key kKeys[] = {
        {"effects", &CategoryVolumes::effects},
        {"ambience", &CategoryVolumes::ambience},
        {"dialogue", &CategoryVolumes::dialogue},
        {"music", &CategoryVolumes::music},
    };

    // The gain for a voice of this category: percent squared, so that the slider's middle
    // sounds about half as loud (50% = -12 dB). Interface sounds go with the effects.
    float gain(Category category) const {
        const float percent = category == Category::Dialogue   ? dialogue
                              : category == Category::Music    ? music
                              : category == Category::Ambience ? ambience
                                                               : effects;
        const float x = percent / 100.0f;
        return x * x;
    }

    static CategoryVolumes parse(const std::string& text) {
        CategoryVolumes v;
        std::istringstream words(text);
        std::string word;
        while (words >> word) {
            const size_t eq = word.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = word.substr(0, eq);
            char* end = nullptr;
            const float value = std::strtof(word.c_str() + eq + 1, &end);
            if (end == word.c_str() + eq + 1 || *end) continue;
            for (const Key& k : kKeys)
                if (key == k.key && value >= 0.0f && value <= 100.0f) v.*k.field = value;
        }
        return v;
    }

    // Only the volumes below 100 ("" for none).
    std::string format() const {
        std::string text;
        for (const Key& k : kKeys) {
            if (this->*k.field == 100.0f) continue;
            char item[32];
            std::snprintf(item, sizeof(item), "%s%s=%g", text.empty() ? "" : " ", k.key, double(this->*k.field));
            text += item;
        }
        return text;
    }
};

}  // namespace audio
