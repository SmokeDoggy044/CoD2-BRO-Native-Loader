// render/enhanced_graphics.h: the settings text of enhanced graphics' improvements.

#include "test_support.h"

#include "enhanced_graphics.h"

int main() {
    using render::GraphicsFeatures;
    const GraphicsFeatures defaults;
    CHECK(defaults.format().empty());
    CHECK(GraphicsFeatures::parse("") == defaults);

    GraphicsFeatures f = GraphicsFeatures::parse("fxaa=0 sharpness=0.5 vibrance=off lod_bias=-1 anisotropic=on");
    CHECK(!f.fxaa && f.anisotropic && f.sharpen && !f.vibrance && f.texture_detail);
    CHECK(f.sharpness == 0.5f && f.lod_bias == -1.0f && f.vibrance_amount == 0.0f);
    CHECK(GraphicsFeatures::parse(f.format()) == f, "%s", f.format().c_str());

    // Out of range: clamped; bad values and unknown keys: ignored.
    f = GraphicsFeatures::parse("sharpness=3 lod_bias=-9 vibrance_amount=x fxaa=maybe colour=1 sharpness");
    CHECK(f.sharpness == 1.0f && f.lod_bias == -2.0f && f.vibrance_amount == 0.0f && f.fxaa);

    if (test::g_failures) return 1;
    std::printf("graphics_features_test: ok\n");
    return 0;
}
