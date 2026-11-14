#pragma once

// Settings widgets (Dear ImGui) drawn from the settings table (src/platform/settings_schema.h): the launcher's tabs
// and the in-game PC menu use the same labels, tooltips, ranges, sections and nesting. Values are edited as text in
// a platform::Settings (settings.ini's form); a value equal to the default is removed, so only changes are saved.

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>

#include "settings.h"
#include "settings_schema.h"

namespace ui {

// Draws one setting itself instead of the standard widget (aspect ratio, device lists, ...): true when it did;
// `changed` then says whether the value moved. Called with the setting's indent and enabled state in place.
using CustomWidget = std::function<bool(const platform::SettingDef& def, const platform::SettingUi& ui,
                                        platform::Settings& values, bool& changed)>;

struct DrawOptions {
    uint8_t require_flags = 0;  // only settings with these flags (platform::kLive: the PC menu)
    CustomWidget custom;
};

// The settings of these groups in the table's order, with their section titles; true if one changed.
bool draw_settings(std::initializer_list<platform::SettingGroup> groups, platform::Settings& values,
                   const DrawOptions& options = {});

// The setting's value (its default when unset).
std::string setting_value(const platform::Settings& values, std::string_view key);
// Sets it; the default value removes it.
void put_setting(platform::Settings& values, std::string_view key, const std::string& value);
// Whether a setting's parents (SettingUi::parent) are all on.
bool setting_enabled(const platform::Settings& values, std::string_view key);
// The (?) after a widget, with `text` as its tooltip.
void help_marker(const char* text);
// A number as settings.ini keeps it ("0.3", "12").
std::string number_text(double value);

}  // namespace ui
