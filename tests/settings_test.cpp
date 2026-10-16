// settings.ini: parsing, writing back, and the command-line options it stands for.
// Portable (no xbox/), so it runs on every preset.

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "settings.h"

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

    if (g_failures == 0) std::printf("settings_test: all passed\n");
    return g_failures == 0 ? 0 : 1;
}
