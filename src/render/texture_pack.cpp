#include "texture_pack.h"

#include "mod_list.h"

#include <zstd.h>

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "chacha20.h"
#include "file.h"
#include "log.h"
#include "texture_index.h"

namespace render::texture_pack {

namespace fs = std::filesystem;

namespace {

// Known -> Queued -> Ready (picture waiting) -> Delivered (the device has it), or Failed. Each Ready/Failed moves
// `generation` (the version the device compares), so a device that asks again later gets it read again.
enum class State : uint8_t { Known, Queued, Ready, Delivered, Failed };

struct Entry {
    fs::path file;    // a loose DDS, or the pack.dat holding it (stored > 0)
    uint64_t offset = 0, stored = 0, size = 0;  // packed: where in pack.dat, stored and unpacked sizes
    char kind = 't';  // packed: t, n, s (the nonce)
    std::string mod;  // folder name
    State state = State::Known;
    uint32_t generation = 0;
    std::shared_ptr<const TextureReplacement> picture;  // Ready, until handed out
};

std::mutex g_mutex;
std::condition_variable g_wake;
std::unordered_map<uint64_t, Entry> g_entries;
std::unordered_map<uint64_t, Entry> g_maps[kSurfaceMaps];  // surface maps (phase 2.2), per kind
// What the loading thread reads next: the content and 0 (colour) or 1 + the SurfaceMap.
std::deque<std::pair<uint64_t, uint8_t>> g_queue;
std::thread g_worker;
bool g_stop = false;
bool g_enabled = true;
uint32_t g_toggles = 0;                       // moves version() on every set_enabled change
std::unordered_set<uint64_t> g_uncovered;     // asked for, not covered (capped)
constexpr size_t kUncoveredLimit = 100000;

// Packed files are encrypted with a key from the original texture (platform/texture_index.h): contents with
// packed files, their keys once known, the title memory copied on the first ask (hashed on the loading thread),
// and jobs waiting for a key.
std::unordered_set<uint64_t> g_keyed;
std::unordered_map<uint64_t, std::array<uint8_t, 32>> g_keys;
std::unordered_map<uint64_t, std::vector<uint8_t>> g_originals;
std::unordered_map<uint64_t, std::vector<uint8_t>> g_waiting;  // content -> job kinds
constexpr uint8_t kKeyJob = 0xFF;

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

// A packed file's DDS bytes: read from pack.dat, decrypted, decompressed; empty when it does not unpack (a damaged
// pack, or a key from another texture: the zstd frame check fails).
std::vector<uint8_t> unpack(const Entry& e, uint64_t content, const std::array<uint8_t, 32>& key) {
    std::vector<uint8_t> stored(e.stored);
    // pack.dat can pass 4 GB: 64-bit positioned reads (platform::File), not stream seeks.
    std::error_code ec;
    const auto file = platform::File::open(e.file, platform::File::Access::Read, false, ec);
    uint32_t done = 0;
    if (!file || e.stored > UINT32_MAX ||
        !file->read_at(e.offset, stored.data(), static_cast<uint32_t>(stored.size()), done, ec) || done != stored.size())
        return {};
    const auto nonce = platform::texture_nonce(content, e.kind);
    platform::chacha20_xor(key.data(), nonce.data(), 0, stored.data(), stored.size());
    std::vector<uint8_t> data(e.size);
    const size_t n = ZSTD_decompress(data.data(), data.size(), stored.data(), stored.size());
    if (ZSTD_isError(n) || n != data.size()) return {};
    return data;
}

// The key for `content` (the caller holds `lock`; unlocked while hashing), or null: no original offered yet.
const std::array<uint8_t, 32>* key_for(uint64_t content, std::unique_lock<std::mutex>& lock) {
    if (auto k = g_keys.find(content); k != g_keys.end()) return &k->second;
    auto o = g_originals.find(content);
    if (o == g_originals.end()) return nullptr;
    const std::vector<uint8_t> original = std::move(o->second);
    g_originals.erase(o);
    lock.unlock();
    const auto key = platform::texture_key(original.data(), original.size());
    lock.lock();
    auto [k, added] = g_keys.try_emplace(content, key);
    // Jobs that waited for this key go again.
    if (auto w = g_waiting.find(content); w != g_waiting.end()) {
        for (uint8_t kind : w->second) g_queue.emplace_back(content, kind);
        g_waiting.erase(w);
    }
    return &k->second;
}

void work() {
    std::unique_lock lock(g_mutex);
    for (;;) {
        g_wake.wait(lock, [] { return g_stop || !g_queue.empty(); });
        if (g_stop) return;
        const auto [content, kind] = g_queue.front();
        g_queue.pop_front();
        if (kind == kKeyJob) {
            key_for(content, lock);
            continue;
        }
        auto& entries = kind ? g_maps[kind - 1] : g_entries;
        auto it = entries.find(content);
        if (it == entries.end()) continue;
        const Entry entry = it->second;
        std::shared_ptr<TextureReplacement> picture;
        if (entry.stored) {
            const std::array<uint8_t, 32>* found = key_for(content, lock);
            if (!found) {
                g_waiting[content].push_back(kind);
                continue;
            }
            const std::array<uint8_t, 32> key = *found;
            lock.unlock();
            const std::vector<uint8_t> data = unpack(entry, content, key);
            picture = data.empty() ? nullptr : read_dds(data);
            if (data.empty())
                LOG_WARN("mods", "%s: %016llx (%c) does not unpack with this game's texture (damaged pack, or an "
                         "edition the pack was not made from); left out",
                         entry.mod.c_str(), static_cast<unsigned long long>(content), entry.kind);
        } else {
            lock.unlock();
            picture = read_dds(entry.file);
        }
        if (picture && (kind != 0) != (picture->compression == TextureReplacement::Compression::Bc5)) picture = nullptr;
        if (!picture && !entry.stored)
            LOG_WARN("mods", "%s: not a %s DDS; left out", entry.file.string().c_str(), kind ? "BC5" : "DXT1/DXT5");
        lock.lock();
        it = entries.find(content);
        if (it == entries.end()) continue;
        it->second.picture = std::move(picture);
        it->second.state = it->second.picture ? State::Ready : State::Failed;
        ++it->second.generation;
    }
}

}  // namespace

std::shared_ptr<TextureReplacement> read_dds(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return nullptr;
    return read_dds(std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
}

std::shared_ptr<TextureReplacement> read_dds(const std::vector<uint8_t>& data) {
    if (data.size() < 128 || std::memcmp(data.data(), "DDS ", 4) != 0) return nullptr;
    const uint32_t height = read_u32(&data[12]), width = read_u32(&data[16]);
    const uint32_t mip_count = std::max(1u, read_u32(&data[28]));
    const uint32_t pixel_flags = read_u32(&data[80]);
    if (!(pixel_flags & 0x4) || !width || !height || width > 16384 || height > 16384) return nullptr;
    auto picture = std::make_shared<TextureReplacement>();
    if (std::memcmp(&data[84], "DXT1", 4) == 0) picture->compression = TextureReplacement::Compression::Dxt1;
    else if (std::memcmp(&data[84], "DXT5", 4) == 0) picture->compression = TextureReplacement::Compression::Dxt5;
    else if (std::memcmp(&data[84], "ATI2", 4) == 0 || std::memcmp(&data[84], "BC5U", 4) == 0)
        picture->compression = TextureReplacement::Compression::Bc5;
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

namespace {

// An installed pack (pack.idx + pack.dat, platform/texture_index.h); the caller holds g_mutex. Returns the files
// with surface maps; logs the pack.
size_t open_packed(const fs::path& folder, Mod& mod) {
    std::ifstream in(folder / "pack.idx", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string error;
    const auto index = platform::parse_texture_index(text, &error);
    std::error_code ec;
    const fs::path dat = folder / "pack.dat";
    if (!index || index->parts.size() != 1 || index->packs.size() != 1) {
        LOG_WARN("mods", "%s: pack.idx unreadable (%s); left out", mod.folder_name.c_str(),
                 index ? "not one pack in one part" : error.c_str());
        return 0;
    }
    if (fs::file_size(dat, ec) != index->parts[0].size) {
        LOG_WARN("mods", "%s: pack.dat missing or not the size pack.idx gives (install it again); left out",
                 mod.folder_name.c_str());
        return 0;
    }
    size_t maps = 0;
    for (const platform::TextureIndex::File& f : index->packs[0].files) {
        auto& entries = f.kind == 't' ? g_entries : g_maps[f.kind == 'n' ? 0 : 1];
        auto [it, added] = entries.try_emplace(f.id);
        if (!added) continue;
        it->second.file = dat;
        it->second.offset = f.offset;
        it->second.stored = f.stored;
        it->second.size = f.size;
        it->second.kind = f.kind;
        it->second.mod = mod.folder_name;
        g_keyed.insert(f.id);
        if (f.kind == 't') ++mod.textures;
        maps += f.kind == 'n';
    }
    LOG_INFO("mods", "%s %s%s%s: %zu textures, %zu with surface maps (packed, release %s)", mod.name.c_str(),
             mod.version.c_str(), mod.author.empty() ? "" : " by ", mod.author.c_str(), mod.textures, maps,
             index->release.c_str());
    return maps;
}

}  // namespace

std::vector<Mod> open(const fs::path& mods_dir, std::string_view mod_list) {
    close();
    std::vector<Mod> mods;
    std::error_code ec;
    std::lock_guard lock(g_mutex);
    for (const platform::ModInfo& info : platform::scan_mods(mods_dir, mod_list)) {
        Mod mod;
        mod.folder_name = info.folder;
        mod.name = info.name;
        mod.version = info.version;
        mod.author = info.author;
        if (!info.on) {
            LOG_INFO("mods", "%s: off", mod.name.c_str());
            continue;
        }
        const fs::path folder = mods_dir / info.folder;
        size_t maps = 0;
        if (fs::exists(folder / "pack.idx", ec)) {
            maps = open_packed(folder, mod);
            mods.push_back(std::move(mod));
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
        for (const auto& entry : fs::directory_iterator(folder / "maps", ec)) {
            const std::string stem = entry.path().stem().string();
            uint64_t content;
            if (entry.path().extension() != ".dds" || stem.size() < 18 || stem[16] != '_' ||
                !parse_hex(stem.substr(0, 16), content))
                continue;
            const std::string kind = stem.substr(17);
            const int k = kind == "normal" ? 0 : kind == "surface" ? 1 : -1;
            if (k < 0) continue;
            auto [it, added] = g_maps[k].try_emplace(content);
            if (!added) continue;
            it->second.file = entry.path();
            it->second.mod = mod.folder_name;
            maps += k == 0;
        }
        LOG_INFO("mods", "%s %s%s%s: %zu textures, %zu with surface maps", mod.name.c_str(), mod.version.c_str(),
                 mod.author.empty() ? "" : " by ", mod.author.c_str(), mod.textures, maps);
        mods.push_back(std::move(mod));
    }
    if (!g_entries.empty() || !g_maps[0].empty() || !g_maps[1].empty()) {
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
    for (auto& maps : g_maps) maps.clear();
    g_queue.clear();
    g_uncovered.clear();
    g_keyed.clear();
    g_keys.clear();
    g_originals.clear();
    g_waiting.clear();
}

namespace {
std::shared_ptr<const TextureReplacement> deliver(Entry& e, uint64_t content, uint8_t kind);
}  // namespace

std::shared_ptr<const TextureReplacement> replacement(uint64_t content, std::span<const uint8_t> original) {
    std::lock_guard lock(g_mutex);
    // Packed files of this content (its picture or its surface maps) need the key from these bytes: copied once,
    // hashed on the loading thread.
    if (!original.empty() && g_keyed.count(content) && !g_keys.count(content) && !g_originals.count(content)) {
        g_originals.emplace(content, std::vector<uint8_t>(original.begin(), original.end()));
        g_queue.emplace_back(content, kKeyJob);
        g_wake.notify_one();
    }
    auto it = g_entries.find(content);
    if (it == g_entries.end()) {
        if (g_uncovered.size() < kUncoveredLimit) g_uncovered.insert(content);
        return nullptr;
    }
    if (!g_enabled) return nullptr;
    return deliver(it->second, content, 0);
}

namespace {

// An entry's picture once loaded (the caller holds g_mutex): queued on first ask, handed out when ready.
std::shared_ptr<const TextureReplacement> deliver(Entry& e, uint64_t content, uint8_t kind) {
    switch (e.state) {
        case State::Known:
        case State::Delivered:
            e.state = State::Queued;
            g_queue.emplace_back(content, kind);
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

}  // namespace

std::shared_ptr<const TextureReplacement> surface_map(uint64_t content, SurfaceMap kind) {
    std::lock_guard lock(g_mutex);
    auto& maps = g_maps[static_cast<uint32_t>(kind)];
    auto it = maps.find(content);
    if (it == maps.end() || !g_enabled) return nullptr;
    return deliver(it->second, content, static_cast<uint8_t>(1 + static_cast<uint32_t>(kind)));
}

uint32_t surface_map_version(uint64_t content, SurfaceMap kind) {
    std::lock_guard lock(g_mutex);
    auto& maps = g_maps[static_cast<uint32_t>(kind)];
    auto it = maps.find(content);
    if (it == maps.end()) return 0;
    return it->second.generation + (g_toggles << 20);
}

bool has_surface_maps(uint64_t content) {
    std::lock_guard lock(g_mutex);
    return g_enabled && g_maps[0].count(content) != 0;
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
    s.maps = g_maps[0].size();
    return s;
}

std::string covering_mod(uint64_t content) {
    std::lock_guard lock(g_mutex);
    auto it = g_entries.find(content);
    return it == g_entries.end() ? std::string() : it->second.mod;
}

}  // namespace render::texture_pack
