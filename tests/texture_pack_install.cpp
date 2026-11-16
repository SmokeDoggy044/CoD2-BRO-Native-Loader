// Developer tool (not a test): installs texture packs the way the launcher's Texture Packs tab does, without the
// window - to check an install against tools/texture_pack/publish.py --stage install, or a download source.
//
//   texture_pack_install MODS SOURCE PACK [PACK ...]
//     SOURCE: a folder with cod_bro_textures.index + parts, an address ("https://.../", "http://127.0.0.1:N/"),
//     or "github" (the newest release of the pack repository; COD_BRO_TEXTURE_SOURCE overrides it).
//     COD_BRO_CANCEL_AFTER=BYTES cancels each install after that many bytes (resume checks).

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "texture_packs.h"

int main(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr, "usage: texture_pack_install MODS SOURCE PACK [PACK ...]\n");
        return 2;
    }
    const std::string where = argv[2];
    std::string error;
    std::optional<launcher::PackSource> source;
    if (where == "github") {
        source = launcher::newest_pack_release("SmokeDoggy044/cod_bro_tex", "cod_bro_launcher/dev", error);
    } else if (where.rfind("http", 0) == 0) {
        source = launcher::PackSource{{}, where.back() == '/' ? where : where + "/", "cod_bro_launcher/dev"};
    } else {
        source = launcher::PackSource{where, {}, "cod_bro_launcher/dev"};
    }
    if (!source) {
        std::fprintf(stderr, "source: %s\n", error.c_str());
        return 1;
    }
    const auto index = launcher::read_pack_index(*source, error);
    if (!index) {
        std::fprintf(stderr, "index: %s\n", error.c_str());
        return 1;
    }
    const char* cancel_text = std::getenv("COD_BRO_CANCEL_AFTER");
    const uint64_t cancel_after = cancel_text ? std::strtoull(cancel_text, nullptr, 10) : 0;
    int failed = 0;
    for (int i = 3; i < argc; ++i) {
        std::atomic<bool> cancel{false};
        uint64_t shown = 0;
        const bool ok = launcher::install_pack(
            *source, *index, argv[i], argv[1],
            [&](const launcher::PackProgress& p) {
                if (cancel_after && p.done >= cancel_after) cancel = true;
                if (p.done - shown >= (256u << 20) || p.done == p.total) {
                    shown = p.done;
                    std::printf("%s\n", p.message.c_str());
                    std::fflush(stdout);
                }
            },
            cancel, error);
        if (!ok) {
            std::fprintf(stderr, "%s: %s\n", argv[i], error.c_str());
            ++failed;
        }
    }
    return failed ? 1 : 0;
}
