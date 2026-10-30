// The volume per kind of sound's text form and gains (settings.ini "volume", --volume). Portable.

#include <cstdio>

#include <cmath>

#include "category_volumes.h"
#include "enhanced_features.h"

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

    CHECK(CategoryVolumes{}.close_gunfire == 0.0f);
    CHECK(CategoryVolumes{}.own_gunfire == 0.0f);
    CHECK(CategoryVolumes::parse("close_gunfire=4").close_gunfire == 4.0f);
    CHECK(CategoryVolumes::parse("close_gunfire=13").close_gunfire == 0.0f);
    CHECK(CategoryVolumes::parse("close_gunfire=12").format() == "close_gunfire=12");
    CHECK(CategoryVolumes::parse("close_gunfire=0").format().empty());
    CHECK(CategoryVolumes{}.close_gain(Category::Gunfire, 100.0f) == 1.0f);
    CHECK(v.gain(Category::Gunfire) == v.gain(Category::Effects));
    CHECK(v.gain(Category::Bullets) == v.gain(Category::Effects));
    CHECK(CategoryVolumes{}.close_bullets == 0.0f);
    CHECK(std::fabs(CategoryVolumes::parse("own_gunfire=6").close_gain(Category::OwnGunfire, 0.0f) - 1.9953f) < 1e-3f);
    const CategoryVolumes g = CategoryVolumes::parse("close_gunfire=6 close_bullets=0");
    CHECK(std::fabs(g.close_gain(Category::Gunfire, 100.0f) - 1.9953f) < 1e-3f);
    CHECK(g.close_gain(Category::Gunfire, 5000.0f) == 1.0f);
    CHECK(g.close_gain(Category::Gunfire, 900.0f) > 1.0f &&
          g.close_gain(Category::Gunfire, 900.0f) < g.close_gain(Category::Gunfire, 600.0f));
    CHECK(g.close_gain(Category::Bullets, 100.0f) == 1.0f);
    CHECK(g.close_gain(Category::Effects, 100.0f) == 1.0f);

    // Enhanced sound's improvements one by one.
    using audio::EnhancedFeatures;
    using audio::Feature;
    CHECK(EnhancedFeatures{}.format().empty());
    const EnhancedFeatures f = EnhancedFeatures::parse("reverb=0 occlusion=off voices=2 bogus=0 resampler=1");
    CHECK(!f.reverb && !f.occlusion && f.voices && f.resampler && f.close_boost);
    CHECK(f.format() == "reverb=0 occlusion=0");
    CHECK(EnhancedFeatures::parse(f.format()) == f);
    CHECK(!f.has(Feature::Reverb) && f.has(Feature::CloseBoost));

    if (g_failures) return 1;
    std::printf("category_volumes_test: all passed\n");
    return 0;
}
