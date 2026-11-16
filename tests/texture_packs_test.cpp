// The launcher's texture pack installer (src/launcher/texture_packs.h) on a made-up release in a temporary folder:
// install, resume after an interruption, an update that copies unchanged files, a damaged part, removal.

#include <atomic>
#include <chrono>
#include <thread>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "sha256.h"
#include "texture_packs.h"

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

std::string read(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

// A release of two packs over two parts ("a" spans both); `seed` changes the second file of "a".
platform::TextureIndex make_release(const fs::path& dir, unsigned seed, std::vector<std::string>& stored_a) {
    fs::create_directories(dir);
    std::mt19937 random(7);
    auto blob = [&](size_t n) {
        std::string s(n, '\0');
        for (char& c : s) c = static_cast<char>(random());
        return s;
    };
    platform::TextureIndex index;
    index.release = seed ? "0.2" : "0.1";
    std::string parts[2];
    stored_a.clear();
    for (int pack = 0; pack < 2; ++pack) {
        platform::TextureIndex::Pack p;
        p.id = pack ? "b" : "a";
        p.ini = {"name=Pack " + p.id, "version=" + index.release, "kind=textures"};
        for (int i = 0; i < 5; ++i) {
            std::string data = blob(1000 + 777 * i);
            if (pack == 0 && i == 1 && seed) data = blob(1500);  // changed in the update
            const int part = pack == 0 && i < 3 ? 0 : 1;
            platform::TextureIndex::File f;
            f.id = 0x1000u * (pack + 1) + i;
            f.kind = 't';
            f.part = static_cast<uint32_t>(part + 1);
            f.offset = parts[part].size();
            f.stored = data.size();
            f.size = data.size() * 2;
            f.sha256 = platform::sha256_hex(data);
            parts[part] += data;
            if (pack == 0) stored_a.push_back(data);
            p.files.push_back(f);
        }
        index.packs.push_back(p);
    }
    for (int part = 0; part < 2; ++part) {
        const std::string name = "cod_bro_textures.data.00" + std::to_string(part + 1);
        std::ofstream(dir / name, std::ios::binary) << parts[part];
        index.parts.push_back({name, parts[part].size(), platform::sha256_hex(parts[part])});
    }
    const std::string text = platform::format_texture_index(index);
    std::ofstream(dir / launcher::kIndexName, std::ios::binary) << text;
    std::ofstream(dir / "SHA256SUMS.txt", std::ios::binary)
        << platform::sha256_hex(text) << "  " << launcher::kIndexName << "\n";
    return index;
}

void installer_tests() {
    const fs::path root = fs::temp_directory_path() / "cod_bro_texture_packs_test";
    fs::remove_all(root);
    const fs::path mods = root / "mods";
    std::vector<std::string> stored;
    make_release(root / "r1", 0, stored);
    std::atomic<bool> cancel{false};
    std::string error;
    launcher::PackSource source{root / "r1", {}, "test"};
    auto index = launcher::read_pack_index(source, error);
    CHECK(index && index->packs.size() == 2);
    if (!index) return;
    int calls = 0;
    auto progress = [&](const launcher::PackProgress&) { ++calls; };

    // Install: pack.dat = the stored files back to back; pack.idx offsets into it; mod.ini from the index.
    CHECK(launcher::install_pack(source, *index, "a", mods, progress, cancel, error));
    std::string all;
    for (const auto& s : stored) all += s;
    CHECK(read(mods / "a" / "pack.dat") == all);
    CHECK(read(mods / "a" / "mod.ini").find("name=Pack a") != std::string::npos);
    auto installed = launcher::installed_packs(mods);
    CHECK(installed.size() == 1 && installed[0].id == "a" && installed[0].release == "0.1" && installed[0].files == 5);
    CHECK(installed.size() == 1 && installed[0].index.parts[0].sha256 == platform::sha256_hex(all));
    CHECK(!fs::exists(mods / "a.download"));
    CHECK(calls > 0);

    // Resume: a download folder holding two whole files and half of the third goes on from the third.
    {
        fs::create_directories(mods / "b.download");
        // Signature as install_pack writes it: the pack's section of the index.
        platform::TextureIndex one;
        one.release = index->release;
        one.packs.push_back(*index->find("b"));
        std::ofstream(mods / "b.download" / "download.idx", std::ios::binary) << platform::format_texture_index(one);
        const std::string part2 = read(root / "r1" / "cod_bro_textures.data.002");
        const auto& files = index->find("b")->files;
        const uint64_t keep = files[2].offset + files[2].stored / 2 - files[0].offset;
        std::ofstream(mods / "b.download" / "pack.dat", std::ios::binary) << part2.substr(files[0].offset, keep);
        CHECK(launcher::install_pack(source, *index, "b", mods, progress, cancel, error));
        const uint64_t end = files[4].offset + files[4].stored;
        CHECK(read(mods / "b" / "pack.dat") == part2.substr(files[0].offset, end - files[0].offset));
    }

    // Update to 0.2: the changed file comes from the source, the others are copied from the installed pack (the
    // source's copies are spoiled to prove it).
    std::vector<std::string> stored2;
    auto index2 = make_release(root / "r2", 1, stored2);
    {
        std::string part1 = read(root / "r2" / "cod_bro_textures.data.001");
        const auto& f0 = index2.find("a")->files[0];
        part1[f0.offset] ^= 1;  // file 0 unchanged since 0.1: must not be read from here
        std::ofstream(root / "r2" / "cod_bro_textures.data.001", std::ios::binary) << part1;
    }
    launcher::PackSource source2{root / "r2", {}, "test"};
    CHECK(launcher::install_pack(source2, index2, "a", mods, progress, cancel, error));
    std::string all2;
    for (const auto& s : stored2) all2 += s;
    CHECK(read(mods / "a" / "pack.dat") == all2);
    installed = launcher::installed_packs(mods);
    CHECK(installed.size() == 2);

    // A damaged source file: refused (after the attempts), the installed pack stays as it was.
    {
        std::string part2 = read(root / "r2" / "cod_bro_textures.data.002");
        part2[index2.find("b")->files[3].offset + 5] ^= 0x40;
        std::ofstream(root / "r2" / "cod_bro_textures.data.002", std::ios::binary) << part2;
        const std::string before = read(mods / "b" / "pack.dat");
        CHECK(!launcher::install_pack(source2, index2, "b", mods, progress, cancel, error));
        CHECK(error.find("damaged") != std::string::npos);
        CHECK(read(mods / "b" / "pack.dat") == before);
    }

    // Cancel: stops, nothing replaced.
    cancel = true;
    CHECK(!launcher::install_pack(source, *index, "a", mods, progress, cancel, error) && error == "cancelled");
    cancel = false;
    CHECK(read(mods / "a" / "pack.dat") == all2);

    // Remove.
    CHECK(launcher::remove_pack(mods, "a", error) && !fs::exists(mods / "a") && !fs::exists(mods / "a.download"));
    CHECK(launcher::installed_packs(mods).size() == 1);

    // The page's threaded jobs: a folder look-up, then an install (polled as the page does each frame).
    {
        auto wait = [](const launcher::PackTask& task) {
            for (int i = 0; i < 2000 && task.result().status == launcher::PackTask::Status::Working; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            return task.result();
        };
        auto lookup = launcher::PackTask::lookup("unused/repository", root / "r1", "test");
        const auto found = wait(*lookup);
        CHECK(found.status == launcher::PackTask::Status::Done && found.index && found.source);
        if (found.index && found.source) {
            auto job = launcher::PackTask::install(*found.source, *found.index, "a", mods);
            const auto done = wait(*job);
            CHECK(done.status == launcher::PackTask::Status::Done && done.progress.done == done.progress.total);
            CHECK(read(mods / "a" / "pack.dat") == all);
        }
    }

    // A wrong SHA256SUMS.txt: the index is refused.
    std::ofstream(root / "r1" / "SHA256SUMS.txt", std::ios::binary) << std::string(64, '0') << "  " << launcher::kIndexName << "\n";
    CHECK(!launcher::read_pack_index(source, error));
    fs::remove_all(root);
}

}  // namespace

int main() {
    installer_tests();
    if (g_failures) std::printf("%d failures\n", g_failures);
    else std::printf("texture_packs_test: all passed\n");
    return g_failures ? 1 : 0;
}
