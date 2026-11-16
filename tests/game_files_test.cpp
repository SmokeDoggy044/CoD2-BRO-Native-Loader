// The launcher's game files (src/launcher/game_files.h) on small made-up disc dumps: an XISO and a full image
// (game partition at the XGD2 offset), recognised by default.xbe, checked, extracted; damaged, incomplete and
// unknown copies; a folder check.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "game_files.h"
#include "sha1.h"

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
constexpr uint32_t kSector = 2048;

std::string sha1_hex(const std::string& data) {
    platform::sha1::Context c;
    platform::sha1::init(c);
    platform::sha1::update(c, reinterpret_cast<const uint8_t*>(data.data()), static_cast<uint32_t>(data.size()));
    uint8_t d[20];
    platform::sha1::final(c, d);
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    for (uint8_t b : d) out += kHex[b >> 4], out += kHex[b & 15];
    return out;
}

std::string read(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

// One directory table: entries as a right-leaning chain (each entry's right = the next).
struct Item {
    std::string name;
    uint32_t sector, size;
    bool directory;
};
std::string dir_table(const std::vector<Item>& items) {
    std::string t;
    for (size_t i = 0; i < items.size(); ++i) {
        const Item& it = items[i];
        const size_t at = t.size();
        const size_t length = (14 + it.name.size() + 3) / 4 * 4;
        const uint16_t right = i + 1 < items.size() ? static_cast<uint16_t>((at + length) / 4) : 0;
        t.append(length, '\0');
        uint16_t left = 0;
        std::memcpy(&t[at], &left, 2);
        std::memcpy(&t[at + 2], &right, 2);
        std::memcpy(&t[at + 4], &it.sector, 4);
        std::memcpy(&t[at + 8], &it.size, 4);
        t[at + 12] = it.directory ? 0x10 : 0x20;
        t[at + 13] = static_cast<char>(it.name.size());
        std::memcpy(&t[at + 14], it.name.data(), it.name.size());
    }
    t.resize(kSector, '\xff');
    return t;
}

// default.xbe, media/a.dat, media/b.dat at partition offset `base`.
void make_image(const fs::path& path, uint64_t base, const std::map<std::string, std::string>& files) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    auto put = [&](uint64_t sector, const std::string& data) {
        out.seekp(static_cast<std::streamoff>(base + sector * kSector));
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
    };
    std::string volume(kSector, '\0');
    std::memcpy(&volume[0], "MICROSOFT*XBOX*MEDIA", 20);
    const uint32_t root = 33, root_size = kSector;
    std::memcpy(&volume[20], &root, 4);
    std::memcpy(&volume[24], &root_size, 4);
    std::memcpy(&volume[0x7EC], "MICROSOFT*XBOX*MEDIA", 20);
    put(32, volume);
    const auto& xbe = files.at("default.xbe");
    const auto& a = files.at("media/a.dat");
    const auto& b = files.at("media/b.dat");
    put(33, dir_table({{"default.xbe", 40, static_cast<uint32_t>(xbe.size()), false}, {"media", 34, kSector, true}}));
    put(34, dir_table({{"a.dat", 41, static_cast<uint32_t>(a.size()), false},
                       {"b.dat", 42, static_cast<uint32_t>(b.size()), false}}));
    put(40, xbe);
    put(41, a);
    put(42, b);
}

void tests() {
    const fs::path root = fs::temp_directory_path() / "cod_bro_game_files_test";
    fs::remove_all(root);
    fs::create_directories(root);
    std::map<std::string, std::string> files = {
        {"default.xbe", "XBEH the game " + std::string(500, 'x')},
        {"media/a.dat", std::string(1500, 'a')},
        {"media/b.dat", "bee"}};
    std::string text = "edition standard " + sha1_hex(files["default.xbe"]) + " Standard edition\n";
    for (const auto& [path, data] : files) text += sha1_hex(data) + " " + std::to_string(data.size()) + " " + path + "\n";
    const auto table = launcher::parse_game_file_table(text);
    CHECK(table && table->editions.size() == 1 && table->editions[0].files.size() == 3);
    if (!table) return;
    std::atomic<bool> cancel{false};
    auto quiet = [](const launcher::GameFilesProgress&) {};
    std::string error;

    // XISO and a full image: recognised, every file intact.
    for (const uint64_t base : {uint64_t{0}, uint64_t{0xFD90000}}) {
        const fs::path image = root / (base ? "full.iso" : "game.xiso.iso");
        make_image(image, base, files);
        std::string why;
        auto disc = launcher::DiscImage::open(image, why);
        CHECK(disc && disc->files().size() == 3);
        launcher::GameFilesCheck check;
        CHECK(launcher::check_game_files(image, *table, check, quiet, cancel, error));
        CHECK(check.edition && check.usable() && check.files == 3);
    }

    // Extract: every file in place, no .part left, existing other files kept.
    const fs::path target = root / "assets";
    fs::create_directories(target);
    std::ofstream(target / "README.md") << "keep me";
    CHECK(launcher::extract_disc_image(root / "full.iso", &table->editions[0], target, quiet, cancel, error));
    for (const auto& [path, data] : files) CHECK(read(target / path) == data);
    CHECK(read(target / "README.md") == "keep me");
    CHECK(!fs::exists(target / "media" / "a.dat.part"));

    // The extracted folder checks out too.
    launcher::GameFilesCheck folder;
    CHECK(launcher::check_game_files(target, *table, folder, quiet, cancel, error));
    CHECK(folder.edition && folder.usable());

    // A damaged byte: found by the check, refused by the extraction (no file of that name written).
    auto damaged = files;
    damaged["media/a.dat"][700] = 'z';
    make_image(root / "damaged.iso", 0, damaged);
    launcher::GameFilesCheck bad;
    CHECK(launcher::check_game_files(root / "damaged.iso", *table, bad, quiet, cancel, error));
    CHECK(bad.edition && !bad.usable() && bad.damaged.size() == 1 && bad.damaged[0] == "media/a.dat");
    const fs::path target2 = root / "assets2";
    CHECK(!launcher::extract_disc_image(root / "damaged.iso", &table->editions[0], target2, quiet, cancel, error));
    CHECK(error.find("media/a.dat") != std::string::npos && !fs::exists(target2 / "media" / "a.dat"));

    // An incomplete copy (cut short): refused when opened.
    {
        const std::string whole = read(root / "game.xiso.iso");
        std::ofstream(root / "short.iso", std::ios::binary) << whole.substr(0, 41 * kSector + 100);
        std::string why;
        CHECK(!launcher::DiscImage::open(root / "short.iso", why) && why.find("shorter") != std::string::npos);
    }

    // Another default.xbe: unknown edition, tree check only (usable, every path there).
    auto other = files;
    other["default.xbe"] = "XBEH another print";
    make_image(root / "other.iso", 0, other);
    launcher::GameFilesCheck unknown;
    CHECK(launcher::check_game_files(root / "other.iso", *table, unknown, quiet, cancel, error));
    CHECK(!unknown.edition && unknown.usable());
    // A folder missing a file.
    fs::remove(target / "media" / "b.dat");
    launcher::GameFilesCheck missing;
    CHECK(launcher::check_game_files(target, *table, missing, quiet, cancel, error));
    CHECK(!missing.usable() && missing.missing.size() == 1 && missing.missing[0] == "media/b.dat");
    // Not an image at all.
    std::ofstream(root / "junk.iso") << std::string(100000, 'j');
    std::string why;
    CHECK(!launcher::DiscImage::open(root / "junk.iso", why));
    fs::remove_all(root);
}

}  // namespace

int main() {
    tests();
    if (g_failures) std::printf("%d failures\n", g_failures);
    else std::printf("game_files_test: all passed\n");
    return g_failures ? 1 : 0;
}
