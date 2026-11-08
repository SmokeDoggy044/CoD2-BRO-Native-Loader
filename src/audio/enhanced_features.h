#pragma once

// Enhanced sound's improvements, each on or off (settings.ini "enhanced", --enhanced, the
// launcher's Sound tab, the PC menu). They only apply while enhanced sound is on (--audio, the
// "Enhanced sound" switch); original sound has none of them. Header only: the game code, the
// loader and the launcher use it without linking the audio library.
//
// Text form: "resampler=1 voices=1 reverb=0 occlusion=1 close_boost=1"; keys left out stay on,
// unknown keys and values other than 0/1 (or on/off) are ignored.

#include <cstdint>
#include <sstream>
#include <string>

namespace audio {

enum class Feature : uint8_t {
    Resampler,   // the best resampler (23rd order Sinc) instead of OpenAL's default
    Voices,      // more voices than the Xbox's (192 3D) and a larger effects budget
    Reverb,      // reverb by the place around the listener
    Occlusion,   // sounds behind walls muffled
    CloseBoost,  // gunfire, bullets and the player's gun louder nearby (CategoryVolumes)
};

struct EnhancedFeatures {
    bool resampler = true;
    bool voices = true;
    bool reverb = true;
    bool occlusion = true;
    bool close_boost = true;

    bool operator==(const EnhancedFeatures&) const = default;

    struct Key {
        const char* key;
        bool EnhancedFeatures::*field;
        Feature feature;
    };
    static constexpr Key kKeys[] = {
        {"resampler", &EnhancedFeatures::resampler, Feature::Resampler},
        {"voices", &EnhancedFeatures::voices, Feature::Voices},
        {"reverb", &EnhancedFeatures::reverb, Feature::Reverb},
        {"occlusion", &EnhancedFeatures::occlusion, Feature::Occlusion},
        {"close_boost", &EnhancedFeatures::close_boost, Feature::CloseBoost},
    };

    bool has(Feature feature) const {
        for (const Key& k : kKeys)
            if (k.feature == feature) return this->*k.field;
        return false;
    }

    static EnhancedFeatures parse(const std::string& text) {
        EnhancedFeatures f;
        std::istringstream words(text);
        std::string word;
        while (words >> word) {
            const size_t eq = word.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = word.substr(0, eq), value = word.substr(eq + 1);
            const bool on = value == "1" || value == "on", off = value == "0" || value == "off";
            if (!on && !off) continue;
            for (const Key& k : kKeys)
                if (key == k.key) f.*k.field = on;
        }
        return f;
    }

    // Only the features that are off ("" for all on).
    std::string format() const {
        std::string text;
        for (const Key& k : kKeys) {
            if (this->*k.field) continue;
            if (!text.empty()) text += ' ';
            text += std::string(k.key) + "=0";
        }
        return text;
    }
};

}  // namespace audio
