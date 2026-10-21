// The launcher's update check: version tags and the GitHub releases answer. Portable
// (no network: only the parsing), so it runs on every preset.

#include <cstdio>
#include <string>

#include "update_check.h"

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
    using launcher::newest_tag;
    using launcher::parse_version;
    using Version = std::array<int, 3>;

    CHECK((parse_version("v0.1.5") == Version{0, 1, 5}));
    CHECK((parse_version("0.1.12") == Version{0, 1, 12}));
    CHECK((parse_version("V1.20.3") == Version{1, 20, 3}));
    CHECK(!parse_version(""));
    CHECK(!parse_version("v0.1"));
    CHECK(!parse_version("v0.1.5-beta"));
    CHECK(!parse_version("v0..5"));
    CHECK(!parse_version("latest"));
    // Numeric, not text, order: 0.1.10 is newer than 0.1.9.
    CHECK(*parse_version("v0.1.10") > *parse_version("v0.1.9"));
    CHECK(!(*parse_version("v0.1.5") > *parse_version("0.1.5")));

    CHECK(newest_tag(R"([{"url":"x","html_url":"y","tag_name":"v0.1.6","name":"a"},{"tag_name":"v0.1.5"}])") ==
          "v0.1.6");
    CHECK(newest_tag("[\n  {\n    \"tag_name\" : \"v2.0.0\",\n") == "v2.0.0");
    CHECK(!newest_tag("[]"));
    CHECK(!newest_tag(R"({"message":"API rate limit exceeded"})"));
    CHECK(!newest_tag(R"([{"tag_name":"v0.1.6)"));

    if (g_failures) std::printf("%d failure(s)\n", g_failures);
    return g_failures ? 1 : 0;
}
