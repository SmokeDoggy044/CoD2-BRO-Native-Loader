// The Animations tab's scan / Extract / Replace pipeline on a synthetic game
// dump. Builds a small KAPF .cod pack holding hands, weapon and animation
// entries plus a loose animation file, exports them, then patches one edited
// asset back into an override copy. Needs no game files.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "animation.h"

namespace fs = std::filesystem;

namespace {

void put16(std::vector<uint8_t>& b, size_t at, uint16_t v) {
    b[at] = uint8_t(v & 0xff);
    b[at + 1] = uint8_t(v >> 8);
}
void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = uint8_t((v >> (8 * i)) & 0xff);
}

struct Entry {
    std::string name;
    std::string path;
    std::vector<uint8_t> data;
};

std::vector<uint8_t> bytes_of(const std::string& text) { return {text.begin(), text.end()}; }

// One section, one chunk, a directory at 0x300, strings at 0x500, payloads
// from 0x8000: exactly the layout pack.cpp and text/kapf.cpp read.
std::vector<uint8_t> make_pack(const std::vector<Entry>& entries) {
    std::vector<uint8_t> b(0x8000, 0);
    std::memcpy(b.data(), "KAPF", 4);
    put32(b, 0x0c, 1);  // sections
    constexpr uint32_t kChunks = 0x100, kDirectory = 0x300, kStrings = 0x500, kData = 0x8000;
    put16(b, 0x28 + 2, static_cast<uint16_t>(entries.size()));
    put32(b, 0x28 + 4, kChunks);
    put32(b, 0x28 + 8, kDirectory);
    put32(b, kChunks, kData);  // chunk record 0: base of all payloads
    size_t string_at = kStrings;
    size_t data_at = kData;
    for (size_t i = 0; i < entries.size(); ++i) {
        const Entry& e = entries[i];
        auto add_string = [&](const std::string& text) {
            const uint32_t at = static_cast<uint32_t>(string_at);
            std::copy(text.begin(), text.end(), b.begin() + static_cast<ptrdiff_t>(string_at));
            string_at += text.size() + 1;
            return at;
        };
        const uint32_t name_at = add_string(e.name);
        const uint32_t path_at = add_string(e.path);
        put32(b, kDirectory + i * 20 + 0, name_at);
        put32(b, kDirectory + i * 20 + 4, path_at);
        put32(b, kDirectory + i * 20 + 8, static_cast<uint32_t>(e.data.size()));
        put32(b, kDirectory + i * 20 + 12, 0);  // chunk index
        put32(b, kDirectory + i * 20 + 16, static_cast<uint32_t>(data_at - kData));
        b.resize(data_at + e.data.size(), 0);
        std::copy(e.data.begin(), e.data.end(), b.begin() + static_cast<ptrdiff_t>(data_at));
        data_at += e.data.size();
    }
    return b;
}

bool write_file(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return bool(out);
}

std::vector<uint8_t> read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const std::streamoff size = in.tellg();
    if (size <= 0) return {};
    in.seekg(0);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}

int failures = 0;
void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cerr << "FAIL: " << what << "\n";
        ++failures;
    }
}

}  // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "cod_bro_animation_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "sp", ec);
    fs::create_directories(root / "media", ec);

    const std::vector<Entry> entries = {
        {"viewmodel_hands_us.xbmesh", "E:\\asset_temp\\assets\\models\\viewmodel_hands_us.xbmesh",
         bytes_of("XBXM hands mesh bytes 0001")},
        {"viewmodel_m1garand.xbmesh", "E:\\asset_temp\\assets\\models\\viewmodel_m1garand.xbmesh",
         bytes_of("XBXM garand mesh bytes 002")},
        {"viewmodel_m1garand_fire.xbanim", "E:\\asset_temp\\assets\\anim\\viewmodel_m1garand_fire.xbanim",
         bytes_of("NAL fire animation clip 03")},
        {"viewmodel_hands_us.xbskel", "E:\\asset_temp\\assets\\anim\\viewmodel_hands_us.xbskel",
         bytes_of("NAL hands skeleton data 04")},
        {"player_stand_idle.xbanim", "E:\\asset_temp\\assets\\anim\\player_stand_idle.xbanim",
         bytes_of("NAL player idle clip 00005")},
        {"misc_prop.xmb", "E:\\asset_temp\\assets\\models\\misc_prop.xmb", bytes_of("XMB descriptor 0000006")},
    };
    check(write_file(root / "sp" / "test.cod", make_pack(entries)), "write synthetic .cod pack");
    check(write_file(root / "media" / "loose_anim.xbanim", bytes_of("loose NAL clip 0000007")),
          "write loose animation");

    const fs::path out = root / "mods" / "animations";
    const fs::path overrides = root / "mods" / "overrides";

    const std::vector<modding::AnimationAsset> scanned = modding::scan_animation_assets(root);
    check(scanned.size() == 7, "scan finds every packed and loose model/animation asset");
    size_t hands = 0, weapons = 0, clips = 0;
    for (const auto& row : scanned) {
        if (row.category == modding::AnimationCategory::ViewHands) ++hands;
        if (row.category == modding::AnimationCategory::Weapon) ++weapons;
        if (row.kind == modding::AnimationKind::Animation) ++clips;
        check(!row.source.empty() && row.size > 0, "scanned row has a source and a size");
    }
    check(hands == 2, "both view-hands assets classified as ViewHands");
    check(weapons == 2, "garand model and its fire animation classified as Weapon");
    check(clips == 3, "all three NAL clips found (2 packed + 1 loose)");

    const modding::AnimationJobResult exported = modding::export_animation_assets(root, out, scanned);
    check(exported.ok && exported.written == 7, "Extract writes all seven distinct assets");
    check(exported.failed == 0, "Extract has no read failures");
    for (const char* file : {"viewhands/viewmodel_hands_us.xbmesh", "viewhands/viewmodel_hands_us.xbskel",
                            "weapons/m1garand/viewmodel_m1garand.xbmesh",
                            "weapons/m1garand/viewmodel_m1garand_fire.xbanim",
                            "animations/player_stand_idle.xbanim", "models/misc_prop.xmb",
                            "animations/loose_anim.xbanim", "animations.csv", "README.txt"})
        check(fs::is_regular_file(out / file, ec), std::string("exported ") + file);

    std::vector<modding::AnimationAsset> manifest;
    std::string error;
    check(modding::read_animation_manifest(out / "animations.csv", manifest, error),
          "read animations.csv: " + error);
    check(manifest.size() == 7, "manifest lists all exported files");

    const modding::AnimationAsset* weapon_anim = nullptr;
    const modding::AnimationAsset* idle = nullptr;
    for (const auto& row : manifest) {
        if (row.output == "weapons/m1garand/viewmodel_m1garand_fire.xbanim") weapon_anim = &row;
        if (row.output == "animations/player_stand_idle.xbanim") idle = &row;
    }
    check(weapon_anim && idle, "manifest has the weapon animation and the shared idle clip");
    if (weapon_anim) check(weapon_anim->packed && weapon_anim->offset >= 0x8000 && weapon_anim->hash != 0,
                           "packed asset records its offset and content hash");
    if (idle) check(idle->group == "shared", "unclassified animation grouped as shared");

    // One same-length edit (valid patch) and one length change (must be skipped).
    std::vector<uint8_t> edited = read_file(out / "weapons/m1garand/viewmodel_m1garand_fire.xbanim");
    check(!edited.empty(), "read exported weapon animation");
    edited[0] ^= 0x5a;
    check(write_file(out / "weapons/m1garand/viewmodel_m1garand_fire.xbanim", edited), "edit weapon animation");
    std::vector<uint8_t> longer = read_file(out / "animations/player_stand_idle.xbanim");
    longer.push_back(0);
    check(write_file(out / "animations/player_stand_idle.xbanim", longer), "make idle clip the wrong size");

    const modding::AnimationJobResult replaced = modding::replace_animation_assets(root, out, overrides);
    check(replaced.ok && replaced.written == 1, "Replace patches exactly the one valid edit");
    check(replaced.failed == 1, "Replace skips the size-changed file instead of truncating it");
    check(replaced.unchanged == 5, "Replace leaves the other five assets alone");
    check(fs::is_regular_file(overrides / "sp" / "test.cod", ec), "patched .cod written to overrides");
    const std::vector<uint8_t> patched = read_file(overrides / "sp" / "test.cod");
    if (weapon_anim) {
        const uint64_t at = weapon_anim->offset;
        check(patched.size() >= at + edited.size() &&
                  std::equal(edited.begin(), edited.end(), patched.begin() + static_cast<ptrdiff_t>(at)),
              "patched pack contains the edited animation at the original offset");
    }
    check(!fs::exists(overrides / "media" / "loose_anim.xbanim", ec),
          "unmodified loose file does not need an override");

    fs::remove_all(root, ec);
    if (failures) {
        std::cerr << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "animation_test: OK (7 assets, 1 patched, 1 skipped)\n";
    return 0;
}
