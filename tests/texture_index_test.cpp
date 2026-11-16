// Texture pack release files (platform/texture_index.h): ChaCha20 (RFC 8439 vector), incremental SHA-256, the
// index grammar, and a stored file made by tools/texture_pack/publish.py unpacked here (key, nonce, cipher, zstd).

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <zstd.h>

#include "chacha20.h"
#include "sha256.h"
#include "texture_index.h"

namespace {

int g_failures = 0;

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            ++g_failures;                                                   \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        }                                                                   \
    } while (0)

std::vector<uint8_t> from_hex(const char* hex) {
    std::vector<uint8_t> out;
    for (size_t i = 0; hex[i] && hex[i + 1]; i += 2) out.push_back(static_cast<uint8_t>(std::stoi(std::string(hex + i, 2), nullptr, 16)));
    return out;
}

void chacha_tests() {
    // RFC 8439 2.4.2: key 00..1f, nonce 00 00 00 00 00 00 00 4a 00 00 00 00, counter 1.
    uint8_t key[32], nonce[12] = {0, 0, 0, 0, 0, 0, 0, 0x4a, 0, 0, 0, 0};
    for (int i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(i);
    std::string text =
        "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen "
        "would be it.";
    std::vector<uint8_t> data(text.begin(), text.end());
    platform::chacha20_xor(key, nonce, 1, data.data(), data.size());
    const auto expected = from_hex(
        "6e2e359a2568f98041ba0728dd0d6981e97e7aec1d4360c20a27afccfd9fae0bf91b65c5524733ab8f593dabcd62b3571639d624e651"
        "52ab8f530c359f0861d807ca0dbf500d6a6156a38e088a22b65e52bc514d16ccf806818ce91ab77937365af90bbf74a35be6b40b8eed"
        "f2785e42874d");
    CHECK(data == expected);
    platform::chacha20_xor(key, nonce, 1, data.data(), data.size());
    CHECK(std::string(data.begin(), data.end()) == text);
}

void sha_tests() {
    std::string text;
    for (int i = 0; i < 1000; ++i) text += static_cast<char>(i * 7);
    platform::Sha256 h;
    for (size_t i = 0; i < text.size(); i += 37) h.update(text.substr(i, 37));
    CHECK(h.finish_hex() == platform::sha256_hex(text));
    CHECK(platform::sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

void index_tests() {
    const std::string sha(64, 'a');
    const std::string text = "cod_bro_textures 1\nrelease 0.1\npart a.001 100 " + sha + "\npart a.002 50 " + sha +
                              "\npack world\nini name=World\nini version=0.1\nfile 00000000000000ff t 1 0 60 120 " +
                              sha + "\nfile 0123456789abcdef n 2 10 40 80 " + sha + "\nfuture words ignored\n";
    std::string error;
    const auto index = platform::parse_texture_index(text, &error);
    CHECK(index.has_value());
    if (!index) {
        std::printf("  %s\n", error.c_str());
        return;
    }
    CHECK(index->release == "0.1" && index->parts.size() == 2 && index->packs.size() == 1);
    CHECK(index->packs[0].files.size() == 2 && index->packs[0].files[1].id == 0x0123456789abcdefull);
    CHECK(index->packs[0].files[1].kind == 'n' && index->packs[0].stored_bytes() == 100);
    CHECK(index->packs[0].ini_value("name") == "World" && index->find("world") && !index->find("ui"));
    const auto again = platform::parse_texture_index(platform::format_texture_index(*index));
    CHECK(again && platform::format_texture_index(*again) == platform::format_texture_index(*index));
    // Rejected: past a part's end, an unknown part, a path in a part name, another format.
    CHECK(!platform::parse_texture_index("cod_bro_textures 1\npart a 10 " + sha + "\npack p\nfile 0000000000000001 t 1 5 6 6 " + sha));
    CHECK(!platform::parse_texture_index("cod_bro_textures 1\npart a 10 " + sha + "\npack p\nfile 0000000000000001 t 2 0 1 1 " + sha));
    CHECK(!platform::parse_texture_index("cod_bro_textures 1\npart ../a 10 " + sha));
    CHECK(!platform::parse_texture_index("cod_bro_textures 2\n"));
    CHECK(!platform::parse_texture_index("hello\n"));
}

// From publish.py: original = bytes 0..255 three times, id 0123456789abcdef, kind n, the text below 8 times.
void interop_test() {
    std::vector<uint8_t> original;
    for (int r = 0; r < 3; ++r)
        for (int i = 0; i < 256; ++i) original.push_back(static_cast<uint8_t>(i));
    const auto key = platform::texture_key(original.data(), original.size());
    CHECK(platform::to_hex(key.data(), key.size()) == "44211c8fc8c01cec0a429dbceba22c79b3ec5f1072f037d244efbcc6b78644a6");
    auto stored = from_hex(
        "36622534fb6b9781aacd67eb368decfb4bf17f15a96e120d7ad0a98b3e2d81e0cd4ff622b1a88b331008ec104a9ffbdf0e0d851d");
    const auto nonce = platform::texture_nonce(0x0123456789abcdefull, 'n');
    platform::chacha20_xor(key.data(), nonce.data(), 0, stored.data(), stored.size());
    std::vector<char> raw(256);
    const size_t n = ZSTD_decompress(raw.data(), raw.size(), stored.data(), stored.size());
    CHECK(!ZSTD_isError(n) && n == 256);
    std::string expected;
    for (int i = 0; i < 8; ++i) expected += "DDS texture pack interop check. ";
    CHECK(std::string(raw.data(), raw.size()) == expected);
}

}  // namespace

int main() {
    chacha_tests();
    sha_tests();
    index_tests();
    interop_test();
    if (g_failures) std::printf("%d failures\n", g_failures);
    else std::printf("texture_index_test: all passed\n");
    return g_failures ? 1 : 0;
}
