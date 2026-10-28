// The launcher's update check and updater: version tags, the GitHub releases answer,
// SHA-256 and SHA256SUMS.txt, and swapping the files in (on a temporary folder).
// Portable (no network), so it runs on every preset.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "sha256.h"
#include "update_check.h"
#include "updater.h"

namespace {

int g_failures = 0;

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            ++g_failures;                                                   \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        }                                                                   \
    } while (0)

namespace fs = std::filesystem;

void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

void updater_tests() {
    using launcher::sha256_hex;
    // FIPS 180-4 examples, and lengths around the padding boundary (55, 56, 64 bytes).
    CHECK(sha256_hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    CHECK(sha256_hex(std::string(55, 'a')) == "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318");
    CHECK(sha256_hex(std::string(56, 'a')) == "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a");
    CHECK(sha256_hex(std::string(64, 'a')) == "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
    CHECK(sha256_hex(std::string(1000000, 'a')) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");

    const std::string sums =
        "83bf6e171f2932c91b01535ebd25d8b924cbf8846e85d99b1959060f298c60db  cod_bro-0.1.6-linux.tar.gz\r\n"
        "96DAA8F05441F389367B4DE8AFFBD3D89163823E403467A667F25ADCF820D98E *cod_bro-0.1.6-windows.zip\n";
    CHECK(launcher::checksum_for(sums, "cod_bro-0.1.6-linux.tar.gz") ==
          "83bf6e171f2932c91b01535ebd25d8b924cbf8846e85d99b1959060f298c60db");
    CHECK(launcher::checksum_for(sums, "cod_bro-0.1.6-windows.zip") ==
          "96daa8f05441f389367b4de8affbd3d89163823e403467a667f25adcf820d98e");
    CHECK(!launcher::checksum_for(sums, "cod_bro-0.1.6-linux.tar"));
    CHECK(!launcher::checksum_for("not a checksum  cod_bro-0.1.6-linux.tar.gz\n", "cod_bro-0.1.6-linux.tar.gz"));

    // An install with game files, an update with a changed, a new and an unchanged-name file.
    const fs::path root = fs::temp_directory_path() / ("cod_bro_update_test_" + std::to_string(std::rand()));
    fs::remove_all(root);
    const fs::path install = root / "install", from = root / "new", backup = root / "old";
    write(install / "cod_bro_launcher", "launcher 1");
    write(install / "lib" / "libSDL3.so.0", "sdl 1");
    write(install / "settings-by-hand.txt", "mine");
    write(install / "assets" / "default.xbe", "game");
    write(install / "assets" / "README.md", "readme 1");
    write(from / "cod_bro_launcher", "launcher 2");
    write(from / "lib" / "libSDL3.so.0", "sdl 2");
    write(from / "fonts" / "new.ttf", "font");
    write(from / "assets" / "README.md", "readme 2");
    std::string error;
    CHECK(launcher::install_files(from, install, backup, error));
    CHECK(read(install / "cod_bro_launcher") == "launcher 2");
    CHECK(read(install / "lib" / "libSDL3.so.0") == "sdl 2");
    CHECK(read(install / "fonts" / "new.ttf") == "font");
    CHECK(read(install / "settings-by-hand.txt") == "mine");
    CHECK(read(install / "assets" / "default.xbe") == "game");
    CHECK(read(install / "assets" / "README.md") == "readme 1");  // assets/ is the player's
    CHECK(read(backup / "cod_bro_launcher") == "launcher 1");
    CHECK(read(backup / "lib" / "libSDL3.so.0") == "sdl 1");

    // A file that cannot be placed (a folder is in its way): everything goes back.
    fs::remove_all(from);
    fs::remove_all(backup);
    write(from / "a_first", "new a");
    write(from / "cod_bro_launcher", "launcher 3");
    write(from / "fonts" / "new.ttf" / "x", "a file where a folder is");
    write(install / "a_first", "old a");
    CHECK(!launcher::install_files(from, install, backup, error) && !error.empty());
    CHECK(read(install / "a_first") == "old a");
    CHECK(read(install / "cod_bro_launcher") == "launcher 2");
    CHECK(read(install / "fonts" / "new.ttf") == "font");
    CHECK(read(from / "a_first") == "new a");

    CHECK(!launcher::is_release_install(install));
    write(install / "LICENSES" / "BUNDLED.txt", "x");
    CHECK(launcher::is_release_install(install));
    fs::create_directories(install / launcher::kUpdateDir / "old");
    launcher::remove_update_leftovers(install);
    CHECK(!fs::exists(install / launcher::kUpdateDir));
    fs::remove_all(root);
}

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

    updater_tests();

    if (g_failures) std::printf("%d failure(s)\n", g_failures);
    return g_failures ? 1 : 0;
}
