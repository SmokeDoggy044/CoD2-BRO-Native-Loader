#include "texture_pack.h"

#include <algorithm>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "log.h"

namespace render::texture_pack {

namespace fs = std::filesystem;

namespace {

// Known -> Queued -> Ready (picture waiting) -> Delivered (the device has it), or Failed. Each Ready/Failed moves
// `generation` (the version the device compares), so a device that asks again later gets it read again.
enum class State : uint8_t { Known, Queued, Ready, Delivered, Failed };

struct Entry {
    fs::path file;
    std::string mod;  // folder name
    State state = State::Known;
    uint32_t generation = 0;
    std::shared_ptr<const TextureReplacement> picture;  // Ready, until handed out
};

std::mutex g_mutex;
std::condition_variable g_wake;
std::unordered_map<uint64_t, Entry> g_entries;
std::deque<uint64_t> g_queue;
std::thread g_worker;
bool g_stop = false;
bool g_enabled = true;
uint32_t g_toggles = 0;                       // moves version() on every set_enabled change
std::unordered_set<uint64_t> g_uncovered;     // asked for, not covered (capped)
constexpr size_t kUncoveredLimit = 100000;

uint32_t read_u32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

bool parse_hex(const std::string& s, uint64_t& out) {
    if (s.size() != 16) return false;
    out = 0;
    for (char c : s) {
        out <<= 4;
        if (c >= '0' && c <= '9') out |= c - '0';
        else if (c >= 'a' && c <= 'f') out |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') out |= c - 'A' + 10;
        else return false;
    }
    return true;
}

// mod.ini: key=value lines (name, version, author; others ignored).
void read_ini(const fs::path& file, Mod& mod) {
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos || line[0] == '#' || line[0] == ';') continue;
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "name") mod.name = value;
        else if (key == "version") mod.version = value;
        else if (key == "author") mod.author = value;
    }
    if (mod.name.empty()) mod.name = mod.folder_name;
}

void work() {
    std::unique_lock lock(g_mutex);
    for (;;) {
        g_wake.wait(lock, [] { return g_stop || !g_queue.empty(); });
        if (g_stop) return;
        const uint64_t content = g_queue.front();
        g_queue.pop_front();
        auto it = g_entries.find(content);
        if (it == g_entries.end()) continue;
        const fs::path file = it->second.file;
        lock.unlock();
        std::shared_ptr<const TextureReplacement> picture = read_dds(file);
        if (!picture) LOG_WARN("mods", "%s: not a DXT1/DXT5 DDS; the original texture stays", file.string().c_str());
        lock.lock();
        it = g_entries.find(content);
        if (it == g_entries.end()) continue;
        it->second.picture = std::move(picture);
        it->second.state = it->second.picture ? State::Ready : State::Failed;
        ++it->second.generation;
    }
}

}  // namespace

std::shared_ptr<TextureReplacement> read_dds(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return nullptr;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() < 128 || std::memcmp(data.data(), "DDS ", 4) != 0) return nullptr;
    const uint32_t height = read_u32(&data[12]), width = read_u32(&data[16]);
    const uint32_t mip_count = std::max(1u, read_u32(&data[28]));
    const uint32_t pixel_flags = read_u32(&data[80]);
    if (!(pixel_flags & 0x4) || !width || !height || width > 16384 || height > 16384) return nullptr;
    auto picture = std::make_shared<TextureReplacement>();
    if (std::memcmp(&data[84], "DXT1", 4) == 0) picture->compression = TextureReplacement::Compression::Dxt1;
    else if (std::memcmp(&data[84], "DXT5", 4) == 0) picture->compression = TextureReplacement::Compression::Dxt5;
    else return nullptr;
    picture->width = width;
    picture->height = height;
    size_t offset = 128;
    uint32_t w = width, h = height;
    for (uint32_t i = 0; i < mip_count && i < 16; ++i) {
        const size_t bytes = size_t{replacement_block_row_bytes(picture->compression, w)} * ((h + 3) / 4);
        if (offset + bytes > data.size()) break;
        picture->levels.emplace_back(data.begin() + offset, data.begin() + offset + bytes);
        offset += bytes;
        if (w == 1 && h == 1) break;
        w = std::max(w / 2, 1u);
        h = std::max(h / 2, 1u);
    }
    return picture->levels.empty() ? nullptr : picture;
}

std::vector<Mod> open(const fs::path& mods_dir, const std::vector<std::string>& disabled) {
    close();
    std::vector<Mod> mods;
    std::error_code ec;
    if (!fs::is_directory(mods_dir, ec)) return mods;
    std::vector<fs::path> folders;
    for (const auto& entry : fs::directory_iterator(mods_dir, ec))
        if (entry.is_directory() && fs::exists(entry.path() / "mod.ini")) folders.push_back(entry.path());
    std::sort(folders.begin(), folders.end());
    std::lock_guard lock(g_mutex);
    for (const fs::path& folder : folders) {
        Mod mod;
        mod.folder_name = folder.filename().string();
        read_ini(folder / "mod.ini", mod);
        if (std::find(disabled.begin(), disabled.end(), mod.folder_name) != disabled.end()) {
            LOG_INFO("mods", "%s: off", mod.name.c_str());
            continue;
        }
        for (const auto& entry : fs::directory_iterator(folder / "textures", ec)) {
            uint64_t content;
            if (entry.path().extension() != ".dds" || !parse_hex(entry.path().stem().string(), content)) continue;
            auto [it, added] = g_entries.try_emplace(content);
            if (!added) continue;
            it->second.file = entry.path();
            it->second.mod = mod.folder_name;
            ++mod.textures;
        }
        LOG_INFO("mods", "%s %s%s%s: %zu textures", mod.name.c_str(), mod.version.c_str(),
                 mod.author.empty() ? "" : " by ", mod.author.c_str(), mod.textures);
        mods.push_back(std::move(mod));
    }
    if (!g_entries.empty()) {
        g_stop = false;
        g_worker = std::thread(work);
        // Joined before the thread object's own destructor runs (a joinable one would terminate the process).
        static const bool registered = [] { return std::atexit(close) == 0; }();
        (void)registered;
    }
    return mods;
}

void close() {
    {
        std::lock_guard lock(g_mutex);
        g_stop = true;
    }
    g_wake.notify_all();
    if (g_worker.joinable()) g_worker.join();
    std::lock_guard lock(g_mutex);
    g_entries.clear();
    g_queue.clear();
    g_uncovered.clear();
}

std::shared_ptr<const TextureReplacement> replacement(uint64_t content) {
    std::lock_guard lock(g_mutex);
    auto it = g_entries.find(content);
    if (it == g_entries.end()) {
        if (g_uncovered.size() < kUncoveredLimit) g_uncovered.insert(content);
        return nullptr;
    }
    if (!g_enabled) return nullptr;
    Entry& e = it->second;
    switch (e.state) {
        case State::Known:
        case State::Delivered:
            e.state = State::Queued;
            g_queue.push_back(content);
            g_wake.notify_one();
            return nullptr;
        case State::Ready: {
            // The device keeps its own copy; asked again (a new device) it is read again.
            std::shared_ptr<const TextureReplacement> picture = std::move(e.picture);
            e.state = State::Delivered;
            return picture;
        }
        default:
            return nullptr;
    }
}

uint32_t version(uint64_t content) {
    std::lock_guard lock(g_mutex);
    auto it = g_entries.find(content);
    if (it == g_entries.end()) return 0;
    // Each switch moves it (both ways), so a device holding either form asks again.
    return it->second.generation + (g_toggles << 20);
}

void set_enabled(bool enabled) {
    std::lock_guard lock(g_mutex);
    if (g_enabled == enabled) return;
    g_enabled = enabled;
    ++g_toggles;
    LOG_INFO("mods", "texture packs %s (developer mode)", enabled ? "on" : "off");
}

bool enabled() {
    std::lock_guard lock(g_mutex);
    return g_enabled;
}

Stats stats() {
    std::lock_guard lock(g_mutex);
    Stats s;
    s.covered = g_entries.size();
    for (const auto& [content, e] : g_entries) {
        if (e.state == State::Ready || e.state == State::Delivered) ++s.loaded;
        else if (e.state == State::Queued) ++s.pending;
        else if (e.state == State::Failed) ++s.failed;
    }
    s.uncovered = g_uncovered.size();
    return s;
}

std::string covering_mod(uint64_t content) {
    std::lock_guard lock(g_mutex);
    auto it = g_entries.find(content);
    return it == g_entries.end() ? std::string() : it->second.mod;
}

}  // namespace render::texture_pack
