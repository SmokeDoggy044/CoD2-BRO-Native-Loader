#pragma once

// The player's volume per kind of sound (settings.ini "volume", --volume, the launcher, the PC
// menu), on top of the game's own volume slider. A plain setting: it applies to original and
// enhanced sound alike. Header only: the loader and the launcher use it without linking the
// audio library.
//
// Text form: "effects=80 ambience=60 dialogue=100 music=50 close_gunfire=8 close_bullets=2
// own_gunfire=3":
// volumes in percent, the close_ boosts in dB; keys left out keep their defaults, unknown keys and out-of-range values
// are ignored.

#include <cmath>
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
    // Enhanced sound only, 0 dB (none) by default - for the players to try (user 10-30): others'
    // gunshots this much louder (dB) within kNear, fading to the game's level at kFar. The game
    // mixes them at 0.04-0.23 of full (playtest 10-30: a firefight a few metres away sounded
    // quiet, also in original sound; the user liked +12).
    float close_gunfire = 0.0f;
    // The player's own shots (not placed: always in full; at +12 much too loud).
    float own_gunfire = 0.0f;
    // Bullet impacts and flybys (at +12 they drowned out the shots).
    float close_bullets = 0.0f;
    static constexpr float kNear = 600.0f, kFar = 1200.0f;  // world units: 15 m, 30 m

    bool operator==(const CategoryVolumes&) const = default;

    struct Key {
        const char* key;
        float CategoryVolumes::*field;
        float fallback, max;  // the default; values from 0 to max
    };
    static constexpr Key kKeys[] = {
        {"effects", &CategoryVolumes::effects, 100.0f, 100.0f},
        {"ambience", &CategoryVolumes::ambience, 100.0f, 100.0f},
        {"dialogue", &CategoryVolumes::dialogue, 100.0f, 100.0f},
        {"music", &CategoryVolumes::music, 100.0f, 100.0f},
        {"close_gunfire", &CategoryVolumes::close_gunfire, 0.0f, 12.0f},
        {"close_bullets", &CategoryVolumes::close_bullets, 0.0f, 12.0f},
        {"own_gunfire", &CategoryVolumes::own_gunfire, 0.0f, 12.0f},
    };

    // Enhanced: the gain on top of gain() for a sound of `category` `distance` world units away
    // (1 for the categories without a boost).
    float close_gain(Category category, float distance) const {
        const float boost = category == Category::Gunfire      ? close_gunfire
                            : category == Category::Bullets    ? close_bullets
                            : category == Category::OwnGunfire ? own_gunfire
                                                               : 0.0f;
        const float t = distance <= kNear ? 1.0f : distance >= kFar ? 0.0f : (kFar - distance) / (kFar - kNear);
        return std::pow(10.0f, boost * t / 20.0f);
    }

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
                if (key == k.key && value >= 0.0f && value <= k.max) v.*k.field = value;
        }
        return v;
    }

    // Only the values that are not the default ("" for none).
    std::string format() const {
        std::string text;
        for (const Key& k : kKeys) {
            if (this->*k.field == k.fallback) continue;
            char item[32];
            std::snprintf(item, sizeof(item), "%s%s=%g", text.empty() ? "" : " ", k.key, double(this->*k.field));
            text += item;
        }
        return text;
    }
};

}  // namespace audio
