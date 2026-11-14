// settings.ini: parsing, writing back, and the command-line options it stands for.
// Portable (no src/runtime/xbox/), so it runs on every preset.

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "settings.h"
#include "settings_schema.h"

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
    using platform::Settings;
    const Settings s = Settings::parse(
        "# comment\n"
        "; also a comment\n"
        "  aspect = 4:3  \r\n"
        "no equals sign\n"
        "assets = /games/cod bro/a=b\n"
        "silent = true\n"
        "scale=3\n"
        "aspect = 16:9\n");  // a repeated key: the last one counts
    CHECK(s.get("aspect") == "16:9");
    CHECK(s.get("assets") == "/games/cod bro/a=b");  // split at the first '=', spaces kept
    CHECK(s.get("scale") == "3");
    CHECK(!s.get("no equals sign"));

    const std::vector<std::string> expected = {"--aspect", "16:9", "--assets", "/games/cod bro/a=b", "--silent",
                                               "--scale", "3"};
    CHECK(s.as_args() == expected);

    Settings edited = s;
    edited.set("silent", "false");  // a flag that is off adds no option
    edited.erase("assets");
    CHECK((edited.as_args() == std::vector<std::string>{"--aspect", "16:9", "--scale", "3"}));

    const auto path = std::filesystem::temp_directory_path() / "cod_bro_settings_test" / "settings.ini";
    std::filesystem::remove_all(path.parent_path());
    CHECK(platform::load_settings(path).empty());
    CHECK(platform::save_settings(s, path));  // creates the folder
    CHECK(platform::load_settings(path).as_args() == expected);
    std::filesystem::remove_all(path.parent_path());

    // The schema: checks and normal forms.
    using platform::check_setting;
    using platform::find_setting;
    std::string error;
    CHECK(check_setting(*find_setting("scale"), "3") == "3");
    CHECK(!check_setting(*find_setting("scale"), "9", &error) && error == "a whole number (1-8)");
    CHECK(check_setting(*find_setting("fxaa"), "0") == "off");
    CHECK(check_setting(*find_setting("aim-assist"), "true") == "on");
    CHECK(!check_setting(*find_setting("speakers"), "mono", &error) &&
          error == "auto, stereo, headphones, quad, 5.1 or 7.1");
    CHECK(check_setting(*find_setting("aspect"), "21:9") == "21:9");
    CHECK(!check_setting(*find_setting("aspect"), "5:4"));
    CHECK(check_setting(*find_setting("fps"), "display") && check_setting(*find_setting("fps"), "144"));
    CHECK(!check_setting(*find_setting("fps"), "20"));
    CHECK(!find_setting("no-such-setting"));
    // Every setting's default passes its own check; every part names a combined setting.
    for (const platform::SettingDef& def : platform::setting_defs()) {
        if (*def.default_value && def.type != platform::SettingType::Custom && !check_setting(def, def.default_value))
            std::printf("FAIL default of %s\n", def.key), ++g_failures;
        if (def.part_of && !find_setting(def.part_of)) std::printf("FAIL part of %s\n", def.key), ++g_failures;
        for (const platform::SettingDef& other : platform::setting_defs())
            if (&other != &def && std::string(other.key) == def.key) std::printf("FAIL twice: %s\n", def.key), ++g_failures;
    }

    // Older combined lines become one line per setting; default parts are left out.
    Settings old = Settings::parse(
        "graphics-features = sharpness=0.3 vibrance_amount=0.01 fxaa=0 anisotropic=1\n"
        "volume = close_gunfire=4 music=100\n"
        "reverb = amount=-3\n"
        "enhanced = reverb=0\n"
        "keys = melee=v\n");
    CHECK(platform::upgrade_settings(old));
    CHECK(!old.get("graphics-features") && !old.get("volume") && !old.get("reverb") && !old.get("enhanced"));
    CHECK(old.get("sharpness") == "0.3" && old.get("vibrance-amount") == "0.01" && old.get("fxaa") == "off");
    CHECK(!old.get("anisotropic") && !old.get("volume-music"));
    CHECK(old.get("close-gunfire") == "4" && old.get("reverb-amount") == "-3" && old.get("room-reverb") == "off");
    CHECK(old.get("keys") == "melee=v");
    CHECK(!platform::upgrade_settings(old));
    // ... and read back as the combined text the structs parse.
    CHECK(platform::combined_setting(old, "graphics-features") == "fxaa=off sharpness=0.3 vibrance_amount=0.01");
    CHECK(platform::combined_setting(old, "enhanced") == "reverb=off");
    CHECK(!platform::combined_setting(Settings{}, "volume"));
    platform::set_combined_setting(old, "graphics-features", "lod_bias=-1");
    CHECK(!old.get("sharpness") && !old.get("fxaa") && old.get("lod-bias") == "-1");
    platform::set_combined_setting(old, "graphics-features", "");
    CHECK(!platform::combined_setting(old, "graphics-features"));
    // The parts are options too; launcher-only settings are not passed to the game.
    Settings parts;
    parts.set("fxaa", "off");
    parts.set("check-updates", "true");
    CHECK((parts.as_args() == std::vector<std::string>{"--fxaa", "off"}));
    CHECK(platform::settings_help("cod_bro").find("--lod-bias X") != std::string::npos);

    if (g_failures == 0) std::printf("settings_test: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
