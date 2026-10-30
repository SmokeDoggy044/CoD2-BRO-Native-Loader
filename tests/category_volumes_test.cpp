// The volume per kind of sound's text form and gains (settings.ini "volume", --volume). Portable.

#include <cstdio>

#include "category_volumes.h"

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
    using audio::Category;
    using audio::CategoryVolumes;

    const CategoryVolumes v =
        CategoryVolumes::parse("music=50 effects=80 bogus=2 dialogue=101 effects=x ambience=0");
    CHECK(v.music == 50.0f);
    CHECK(v.effects == 80.0f);
    CHECK(v.dialogue == 100.0f);
    CHECK(v.ambience == 0.0f);
    CHECK(v.format() == "effects=80 ambience=0 music=50");
    CHECK(CategoryVolumes::parse(v.format()) == v);
    CHECK(CategoryVolumes{}.format().empty());
    CHECK(CategoryVolumes::parse("") == CategoryVolumes{});

    CHECK(CategoryVolumes{}.gain(Category::Effects) == 1.0f);
    CHECK(v.gain(Category::Music) == 0.25f);
    CHECK(v.gain(Category::Interface) == v.gain(Category::Effects));
    CHECK(v.gain(Category::Dialogue) == 1.0f);
    CHECK(v.gain(Category::Ambience) == 0.0f);
    CHECK(CategoryVolumes::parse("music=0").gain(Category::Music) == 0.0f);

    if (g_failures) return 1;
    std::printf("category_volumes_test: all passed\n");
    return 0;
}
