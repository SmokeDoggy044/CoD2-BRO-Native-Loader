// render/texture_decode.h: Xbox texture layouts as PC formats (our textures, the Xbox path's).

#include "test_support.h"

#include <vector>

#include "texture_decode.h"
#include "xbox_d3d.h"

namespace {

using namespace render;

uint32_t format_register(uint32_t format, uint32_t log_width = 0, uint32_t log_height = 0, uint32_t levels = 1) {
    return format << kFormatFormatShift | log_width << kFormatUSizeShift | log_height << kFormatVSizeShift |
           levels << kFormatMipmapShift;
}

uint32_t size_field(uint32_t width, uint32_t height, uint32_t pitch) {
    return (width - 1) | (height - 1) << kSizeHeightShift | (pitch / 64 - 1) << kSizePitchShift;
}

}  // namespace

int main() {
    using render::TextureLayout;

    // YUY2 (Y0 U Y1 V) and UYVY (U Y0 V Y1), linear, video range: white, black, BT.601 red.
    for (const uint32_t format : {0x24u, 0x25u}) {
        TextureLayout layout;
        CHECK(render::describe_texture(format_register(format), size_field(4, 1, 64), layout));
        CHECK(layout.linear && layout.levels == 1 && layout.width == 4 && layout.kind == TextureLayout::Kind::Argb);
        std::vector<uint8_t> memory(64);
        const uint8_t pairs[2][4] = {{235, 128, 16, 128}, {81, 90, 81, 240}};  // Y0 U Y1 V
        for (int p = 0; p < 2; ++p) {
            const uint8_t* q = pairs[p];
            uint8_t* out = &memory[p * 4];
            if (format == 0x24) {
                out[0] = q[0], out[1] = q[1], out[2] = q[2], out[3] = q[3];
            } else {
                out[0] = q[1], out[1] = q[0], out[2] = q[3], out[3] = q[2];
            }
        }
        uint32_t texels[4] = {};
        render::decode_level(layout, 0, memory.data(), nullptr, reinterpret_cast<uint8_t*>(texels), 16);
        CHECK(texels[0] == 0xFFFFFFFF, "%08x", texels[0]);
        CHECK(texels[1] == 0xFF000000, "%08x", texels[1]);
        CHECK(texels[2] == texels[3], "%08x %08x", texels[2], texels[3]);  // one pair, one Y
        CHECK((texels[2] >> 16 & 0xFF) >= 0xFC && (texels[2] >> 8 & 0xFF) <= 2 && (texels[2] & 0xFF) <= 2, "%08x",
              texels[2]);
    }

    // A8R8G8B8 swizzled 4x2: texels in Morton order (x0 y0 x1 ...), two levels.
    {
        TextureLayout layout;
        CHECK(render::describe_texture(format_register(0x06, 2, 1, 2), 0, layout));
        CHECK(layout.swizzled && layout.levels == 2 && layout.total_size == (8 + 2) * 4);
        std::vector<uint32_t> memory(10);
        for (uint32_t i = 0; i < 8; ++i) memory[i] = i;  // Morton index i
        uint32_t texels[8] = {};
        render::decode_level(layout, 0, reinterpret_cast<const uint8_t*>(memory.data()), nullptr,
                             reinterpret_cast<uint8_t*>(texels), 16);
        const uint32_t expected[8] = {0, 1, 4, 5, 2, 3, 6, 7};  // x bit 0, y bit 1, x bit 2
        for (int i = 0; i < 8; ++i) CHECK(texels[i] == expected[i], "texel %d: %u", i, texels[i]);
    }

    if (test::g_failures) return 1;
    std::printf("texture_decode_test: ok\n");
    return 0;
}
