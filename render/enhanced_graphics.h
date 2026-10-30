#pragma once

// Enhanced graphics' improvements, each on or off, and their amounts (settings.ini
// "graphics-features", --graphics-features, the launcher's Graphics tab, the PC menu). They only
// apply while enhanced graphics is on (--graphics, the "Enhanced graphics" switch); original
// graphics has none of them: the picture as on the Xbox (at the PC's resolution). Header only:
// the loader and the launcher use it without linking the render library.
//
// Text form: "fxaa=1 sharpen=1 sharpness=0.4 ..."; switches left out stay on, amounts left out
// stay at their defaults (neutral: 0); unknown keys and bad values are ignored.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

namespace render {

struct GraphicsFeatures {
    bool fxaa = true;            // anti-aliasing of the 3D picture (FXAA), menus left alone
    bool anisotropic = true;     // anisotropic texture filtering (its level: --anisotropy)
    bool sharpen = true;         // contrast-adaptive sharpening, by `sharpness`
    bool vibrance = true;        // stronger colours where they are weak, by `vibrance_amount`
    bool texture_detail = true;  // sharper textures in the distance, by `lod_bias`
    float sharpness = 0.0f;        // 0..1
    float vibrance_amount = 0.0f;  // -1 (greyer) .. 1
    float lod_bias = 0.0f;         // -2 (sharpest) .. 0 (the game's)

    bool operator==(const GraphicsFeatures&) const = default;

    struct Switch {
        const char* key;
        bool GraphicsFeatures::*field;
    };
    static constexpr Switch kSwitches[] = {
        {"fxaa", &GraphicsFeatures::fxaa},
        {"anisotropic", &GraphicsFeatures::anisotropic},
        {"sharpen", &GraphicsFeatures::sharpen},
        {"vibrance", &GraphicsFeatures::vibrance},
        {"texture_detail", &GraphicsFeatures::texture_detail},
    };
    struct Amount {
        const char* key;
        float GraphicsFeatures::*field;
        float low, high;
    };
    static constexpr Amount kAmounts[] = {
        {"sharpness", &GraphicsFeatures::sharpness, 0.0f, 1.0f},
        {"vibrance_amount", &GraphicsFeatures::vibrance_amount, -1.0f, 1.0f},
        {"lod_bias", &GraphicsFeatures::lod_bias, -2.0f, 0.0f},
    };

    static GraphicsFeatures parse(const std::string& text) {
        GraphicsFeatures f;
        std::istringstream words(text);
        std::string word;
        while (words >> word) {
            const size_t eq = word.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = word.substr(0, eq), value = word.substr(eq + 1);
            const bool on = value == "1" || value == "on", off = value == "0" || value == "off";
            for (const Switch& s : kSwitches)
                if (key == s.key && (on || off)) f.*s.field = on;
            for (const Amount& a : kAmounts) {
                if (key != a.key) continue;
                char* end = nullptr;
                const float v = std::strtof(value.c_str(), &end);
                if (end && *end == 0 && v == v) f.*a.field = std::clamp(v, a.low, a.high);
            }
        }
        return f;
    }

    // Only what differs from the defaults ("" for none).
    std::string format() const {
        const GraphicsFeatures d;
        std::string text;
        auto add = [&text](const std::string& item) { text += (text.empty() ? "" : " ") + item; };
        for (const Switch& s : kSwitches)
            if (this->*s.field != d.*s.field) add(std::string(s.key) + (this->*s.field ? "=1" : "=0"));
        for (const Amount& a : kAmounts) {
            if (this->*a.field == d.*a.field) continue;
            char value[32];
            std::snprintf(value, sizeof(value), "%g", static_cast<double>(this->*a.field));
            add(std::string(a.key) + "=" + value);
        }
        return text;
    }
};

// The current choice (render/graphics_settings.cpp; any thread). Enhanced off: none of the
// improvements, whatever the features say.
void set_graphics(bool enhanced, const GraphicsFeatures& features);
bool graphics_enhanced();
GraphicsFeatures graphics_features();
// The features that apply now: the chosen ones with enhanced on, else all off.
GraphicsFeatures active_graphics_features();

}  // namespace render
