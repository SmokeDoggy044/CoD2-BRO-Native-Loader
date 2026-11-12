#include "replay.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "log.h"

namespace xbox::replay {
namespace {

// File: "XBREPLAY" + u32 version, then records [tag u8][size u16][payload].
constexpr char kMagic[8] = {'X', 'B', 'R', 'E', 'P', 'L', 'A', 'Y'};
constexpr uint32_t kVersion = 4;  // 4: timed waits (W), voice status (S), finished packets (k, K)
constexpr char kClockTag = 'C';
constexpr char kFrameTag = 'F';
constexpr char kSettingsTag = 'O';     // record start: settings.ini text
constexpr char kCommandLineTag = 'A';  // record start: command line
constexpr char kEntityHashesTag = 'E';  // per entity (u32 each), just before its frame's 'F'

// Game state (CoD BRO campaign): level locals, and the entity table (count at +0x2aac, pointers at +0x2ab0) whose
// entities are 0x460 bytes each.
constexpr uintptr_t kLevelLocals = 0x00985370;
constexpr size_t kLevelLocalsSize = 0x2708;
constexpr uintptr_t kLevelTime = 0x0098540c;
constexpr uintptr_t kEntityTable = 0x0097a8c0;
constexpr size_t kEntitySize = 0x460;
constexpr uint32_t kMaxEntities = 4096;
// The clients (player state): level locals +0 points at level_maxclients (0x00985404) of 0x640 bytes.
constexpr uintptr_t kLevelMaxClients = 0x00985404;
constexpr size_t kClientSize = 0x640;
constexpr uint32_t kMaxClients = 4;
// g_actors: 32 slot pointers to 0x1400-byte actors.
constexpr uintptr_t kActors = 0x00985384;
constexpr uint32_t kActorSlots = 32;
constexpr size_t kActorSize = 0x1400;

struct FrameRecord {
    uint32_t frame;
    int32_t level_time;
    uint32_t entity_count;
    uint32_t level_hash;
    uint32_t entity_hash;
    uint32_t client_hash;
};

Config g_config;
std::FILE* g_file = nullptr;
std::vector<char> g_file_buffer;
thread_local bool t_bound = false;
bool g_live = true;  // replay ran out or diverged: real values from now on
uint32_t g_frame = 0;
uint32_t g_checked = 0;
uint32_t g_mismatches = 0;
uint32_t g_first_mismatch = 0;

bool on_stream_impl() { return g_file && t_bound && !(g_config.mode == Mode::Replay && g_live); }

struct ReferenceFrame {
    FrameRecord record;
    std::vector<uint32_t> entities;
};
std::vector<ReferenceFrame> g_reference;  // by frame number
bool g_reference_done = false;

// Loads another run's 'E'/'F' records (Config::reference). False if the file is no recording.
bool load_reference(const std::filesystem::path& path) {
    std::FILE* f = std::fopen(path.string().c_str(), "rb");
    if (!f) return false;
    char magic[8];
    uint32_t version = 0;
    if (std::fread(magic, 1, 8, f) != 8 || std::memcmp(magic, kMagic, 8) != 0 || std::fread(&version, 4, 1, f) != 1 ||
        version != kVersion) {
        std::fclose(f);
        return false;
    }
    std::vector<uint32_t> entities;
    for (;;) {
        uint8_t tag = 0;
        uint16_t length = 0;
        if (std::fread(&tag, 1, 1, f) != 1 || std::fread(&length, 2, 1, f) != 1) break;
        std::vector<uint8_t> payload(length);
        if (std::fread(payload.data(), 1, length, f) != length) break;
        if (static_cast<char>(tag) == kEntityHashesTag) {
            entities.assign(length / 4, 0);
            std::memcpy(entities.data(), payload.data(), length / 4 * 4);
        } else if (static_cast<char>(tag) == kFrameTag && length == sizeof(FrameRecord)) {
            ReferenceFrame r;
            std::memcpy(&r.record, payload.data(), sizeof(FrameRecord));
            r.entities = std::move(entities);
            entities.clear();
            if (r.record.frame >= g_reference.size()) g_reference.resize(r.record.frame + 1);
            g_reference[r.record.frame] = std::move(r);
        }
    }
    std::fclose(f);
    return true;
}

std::FILE* g_hashes = nullptr;  // replay: this run's own state records (Config::hashes_out)

void write_to(std::FILE* file, char tag, const void* data, size_t size) {
    const uint8_t tag_byte = static_cast<uint8_t>(tag);
    const uint16_t length = static_cast<uint16_t>(size);
    std::fwrite(&tag_byte, 1, 1, file);
    std::fwrite(&length, 2, 1, file);
    std::fwrite(data, 1, size, file);
}

void write_record(char tag, const void* data, size_t size) { write_to(g_file, tag, data, size); }

void finish();

// Replay: the records of one frame (up to its 'F'), queued per tag. The title may read a value more or fewer
// times than in the recording (a thread it waits for finishes sooner or later): extra clock reads go on from the
// last recorded value at the real clock's pace, extra other reads repeat the last value, unread ones are dropped.
struct Queue {
    std::vector<std::vector<uint8_t>> items;
    size_t next = 0;
    std::vector<uint8_t> last;
    uint32_t extra = 0;
};
Queue g_queues[256];
FrameRecord g_recorded_frame{};
bool g_have_frame = false;
int64_t g_clock_recorded = 0;  // the last clock value handed out, and the real clock then
int64_t g_clock_real = 0;
uint32_t g_count_reports = 0;

// Loads the next frame's records. False at the end of the file.
bool load_frame() {
    for (Queue& q : g_queues) {
        q.items.clear();
        q.next = 0;
        q.extra = 0;
    }
    g_have_frame = false;
    for (;;) {
        uint8_t tag = 0;
        uint16_t length = 0;
        if (std::fread(&tag, 1, 1, g_file) != 1 || std::fread(&length, 2, 1, g_file) != 1) return false;
        std::vector<uint8_t> payload(length);
        if (std::fread(payload.data(), 1, length, g_file) != length) return false;
        if (static_cast<char>(tag) == kSettingsTag || static_cast<char>(tag) == kCommandLineTag) continue;
        if (static_cast<char>(tag) == kFrameTag && length == sizeof(FrameRecord)) {
            std::memcpy(&g_recorded_frame, payload.data(), sizeof(FrameRecord));
            g_have_frame = true;
            return true;
        }
        g_queues[tag].items.push_back(std::move(payload));
    }
}

void go_live() {
    if (g_live) return;
    g_live = true;
    LOG_INFO("replay", "recording ends at frame %u", g_frame);
    finish();
}

// Replay: the next recorded block of this tag into data; false if this frame has none left (data unchanged).
bool take(char tag, void* data, size_t size) {
    Queue& q = g_queues[static_cast<uint8_t>(tag)];
    if (q.next < q.items.size() && q.items[q.next].size() == size) {
        q.last = q.items[q.next++];
        std::memcpy(data, q.last.data(), size);
        return true;
    }
    ++q.extra;
    if (q.last.size() == size) std::memcpy(data, q.last.data(), size);
    return false;
}

// Replay, at a frame's end: reads the title made more or fewer times than recorded (the first few frames).
void report_counts() {
    if (g_count_reports >= 5) return;
    std::string text;
    for (int t = 0; t < 256; ++t) {
        if (t == kEntityHashesTag) continue;  // read by frame(), not by the title
        const Queue& q = g_queues[t];
        const size_t unread = q.items.size() - q.next;
        if (!q.extra && !unread) continue;
        text += " '" + std::string(1, static_cast<char>(t)) + "' " +
                (q.extra ? "+" + std::to_string(q.extra) : "-" + std::to_string(unread));
    }
    if (text.empty()) return;
    ++g_count_reports;
    LOG_INFO("replay", "frame %u: read counts differ from the recording:%s%s", g_frame, text.c_str(),
             g_count_reports == 5 ? " (no more of these)" : "");
}

// Word-wise FNV-1a style hash (integer only: title threads run with the title's FPU modes).
uint32_t hash_words(const void* data, size_t size, uint32_t h = 2166136261u) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t i = 0;
    for (; i + 4 <= size; i += 4) {
        uint32_t word;
        std::memcpy(&word, bytes + i, 4);
        h = (h ^ word) * 16777619u;
    }
    for (; i < size; ++i) h = (h ^ bytes[i]) * 16777619u;
    return h;
}

const uint8_t* title_bytes(uintptr_t address) { return reinterpret_cast<const uint8_t*>(address); }

uint32_t entity_count() {
    uint32_t count;
    std::memcpy(&count, title_bytes(kEntityTable + 0x2aac), 4);
    return count > kMaxEntities ? kMaxEntities : count;
}

const uint8_t* entity(uint32_t index) {
    uint32_t pointer;
    std::memcpy(&pointer, title_bytes(kEntityTable + 0x2ab0 + index * 4), 4);
    return pointer ? title_bytes(pointer) : nullptr;
}

std::vector<uint32_t> g_entity_hashes;  // the last measure()'s, per entity index (0 = no entity)

FrameRecord measure() {
    FrameRecord r{};
    r.frame = g_frame;
    std::memcpy(&r.level_time, title_bytes(kLevelTime), 4);
    r.level_hash = hash_words(title_bytes(kLevelLocals), kLevelLocalsSize);
    r.entity_count = entity_count();
    g_entity_hashes.assign(r.entity_count, 0);
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < r.entity_count; ++i) {
        const uint8_t* const ent = entity(i);
        const uint32_t one = ent ? hash_words(ent, kEntitySize) : 0;
        g_entity_hashes[i] = one;
        h = (h ^ one) * 16777619u;
    }
    r.entity_hash = h;
    uint32_t clients = 0, count = 0;
    std::memcpy(&clients, title_bytes(kLevelLocals), 4);
    std::memcpy(&count, title_bytes(kLevelMaxClients), 4);
    r.client_hash = clients && count <= kMaxClients ? hash_words(title_bytes(clients), count * kClientSize) : 0;
    return r;
}

void dump_state(const std::filesystem::path& dir) {
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    const std::string base = (dir / ("frame_" + std::to_string(g_frame))).string();
    if (std::FILE* f = std::fopen((base + "_level.bin").c_str(), "wb")) {
        std::fwrite(title_bytes(kLevelLocals), 1, kLevelLocalsSize, f);
        std::fclose(f);
    }
    uint32_t clients = 0, client_count = 0;
    std::memcpy(&clients, title_bytes(kLevelLocals), 4);
    std::memcpy(&client_count, title_bytes(kLevelMaxClients), 4);
    if (std::FILE* f = std::fopen((base + "_clients.bin").c_str(), "wb")) {
        if (clients && client_count <= kMaxClients) std::fwrite(title_bytes(clients), kClientSize, client_count, f);
        std::fclose(f);
    }
    // Per entity: u32 index, u32 address, then its bytes.
    if (std::FILE* f = std::fopen((base + "_entities.bin").c_str(), "wb")) {
        const uint32_t count = entity_count();
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t* const ent = entity(i);
            if (!ent) continue;
            const uint32_t header[2] = {i, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ent))};
            std::fwrite(header, 4, 2, f);
            std::fwrite(ent, 1, kEntitySize, f);
        }
        std::fclose(f);
    }
    // Actors (g_actors: 32 slot pointers, 0x1400 bytes each): u32 slot, u32 address, then its bytes.
    if (std::FILE* f = std::fopen((base + "_actors.bin").c_str(), "wb")) {
        for (uint32_t i = 0; i < kActorSlots; ++i) {
            uint32_t actor = 0;
            std::memcpy(&actor, title_bytes(kActors + i * 4), 4);
            if (!actor) continue;
            const uint32_t header[2] = {i, actor};
            std::fwrite(header, 4, 2, f);
            std::fwrite(title_bytes(actor), 1, kActorSize, f);
        }
        std::fclose(f);
    }
    if (std::getenv("XB_DUMP_IMAGE")) {  // developer: also the title image from 0x007b0000 (data sections) to its end
        if (std::FILE* f = std::fopen((base + "_image.bin").c_str(), "wb")) {
            std::fwrite(title_bytes(0x007b0000), 1, 0x009a0d80 - 0x007b0000, f);
            std::fclose(f);
        }
    }
    LOG_INFO("replay", "frame %u: state written to %s_*.bin", g_frame, base.c_str());
}

bool g_finished = false;

void finish() {
    if (g_finished || g_config.mode != Mode::Replay) return;
    g_finished = true;
    if (g_mismatches)
        LOG_WARN("replay", "%u of %u frames differed, the first at frame %u", g_mismatches, g_checked, g_first_mismatch);
    else
        LOG_INFO("replay", "all %u frames matched", g_checked);
    if (g_config.exit_at_end) std::exit(g_mismatches ? 3 : 0);
}

}  // namespace

bool read_recorded_settings(const std::filesystem::path& file, std::string& settings_text, std::string& command_line) {
    std::FILE* f = std::fopen(file.string().c_str(), "rb");
    if (!f) return false;
    char magic[8];
    uint32_t version = 0;
    bool ok = std::fread(magic, 1, 8, f) == 8 && std::memcmp(magic, kMagic, 8) == 0 && std::fread(&version, 4, 1, f) == 1 &&
              version == kVersion;
    for (int n = 0; ok && n < 2; ++n) {
        uint8_t tag = 0;
        uint16_t length = 0;
        std::string text;
        ok = std::fread(&tag, 1, 1, f) == 1 && std::fread(&length, 2, 1, f) == 1;
        if (ok) text.resize(length);
        ok = ok && std::fread(text.data(), 1, length, f) == length;
        if (ok && tag == kSettingsTag) settings_text = std::move(text);
        else if (ok && tag == kCommandLineTag) command_line = std::move(text);
        else ok = false;
    }
    std::fclose(f);
    return ok;
}

bool start(const Config& config) {
    g_config = config;
    if (config.mode == Mode::Off) return true;
    const bool record = config.mode == Mode::Record;
    g_file = std::fopen(config.file.string().c_str(), record ? "wb" : "rb");
    if (!g_file) {
        LOG_ERROR("replay", "cannot open %s", config.file.string().c_str());
        return false;
    }
    g_file_buffer.resize(1 << 20);
    std::setvbuf(g_file, g_file_buffer.data(), _IOFBF, g_file_buffer.size());
    if (record) {
        std::fwrite(kMagic, 1, sizeof(kMagic), g_file);
        std::fwrite(&kVersion, 4, 1, g_file);
        write_record(kSettingsTag, config.settings_text.data(), std::min<size_t>(config.settings_text.size(), 0xffff));
        write_record(kCommandLineTag, config.command_line.data(), std::min<size_t>(config.command_line.size(), 0xffff));
        g_live = false;
        LOG_INFO("replay", "recording to %s", config.file.string().c_str());
        return true;
    }
    char magic[8];
    uint32_t version = 0;
    if (std::fread(magic, 1, 8, g_file) != 8 || std::memcmp(magic, kMagic, 8) != 0 ||
        std::fread(&version, 4, 1, g_file) != 1 || version != kVersion) {
        LOG_ERROR("replay", "%s is not a recording (version %u expected)", config.file.string().c_str(), kVersion);
        return false;
    }
    g_live = false;
    if (!load_frame()) {
        LOG_ERROR("replay", "%s has no frames", config.file.string().c_str());
        return false;
    }
    if (!config.reference.empty()) {
        if (!load_reference(config.reference)) {
            LOG_ERROR("replay", "%s is not a recording (version %u expected)", config.reference.string().c_str(), kVersion);
            return false;
        }
        LOG_INFO("replay", "reference: %zu frames from %s", g_reference.size(), config.reference.string().c_str());
    }
    if (!config.hashes_out.empty()) {
        g_hashes = std::fopen(config.hashes_out.string().c_str(), "wb");
        if (!g_hashes) {
            LOG_ERROR("replay", "cannot write %s", config.hashes_out.string().c_str());
            return false;
        }
        std::fwrite(kMagic, 1, sizeof(kMagic), g_hashes);
        std::fwrite(&kVersion, 4, 1, g_hashes);
    }
    LOG_INFO("replay", "replaying %s", config.file.string().c_str());
    return true;
}

void stop() {
    if (!g_file) return;
    if (g_config.mode == Mode::Replay) {
        g_live = true;
        finish();
    }
    std::fclose(g_file);
    g_file = nullptr;
}

void bind_current_thread() { t_bound = true; }

bool active() { return g_config.mode != Mode::Off; }

bool on_stream() { return on_stream_impl(); }

bool main_thread() { return t_bound; }

bool replaying() { return g_config.mode == Mode::Replay; }

bool recorded(char tag, uint32_t& v) {
    if (!on_stream() || g_config.mode != Mode::Replay) return false;
    return take(tag, &v, sizeof(v));
}

int64_t clock(int64_t real) {
    if (!on_stream_impl()) return real;
    if (g_config.mode == Mode::Record) {
        write_record(kClockTag, &real, sizeof(real));
        return real;
    }
    int64_t recorded;
    if (take(kClockTag, &recorded, sizeof(recorded))) {
        g_clock_recorded = recorded;
        g_clock_real = real;
        return recorded;
    }
    return g_clock_recorded + (real - g_clock_real);
}

uint32_t value(char tag, uint32_t real) {
    if (!on_stream_impl()) return real;
    if (g_config.mode == Mode::Record) {
        write_record(tag, &real, sizeof(real));
        return real;
    }
    uint32_t v = real;
    take(tag, &v, sizeof(v));
    return v;
}

void bytes(char tag, void* data, size_t size) {
    if (!on_stream_impl()) return;
    if (g_config.mode == Mode::Record) {
        write_record(tag, data, size);
        return;
    }
    take(tag, data, size);
}

std::string screenshot_path() {
    if (g_config.mode == Mode::Off || !t_bound) return {};
    const auto& frames = g_config.shot_frames;
    if (!std::binary_search(frames.begin(), frames.end(), g_frame)) return {};
    std::error_code ec;
    std::filesystem::create_directories(g_config.shot_dir, ec);
    const std::string path = (g_config.shot_dir / ("shot_" + std::to_string(g_frame) + ".bmp")).string();
    LOG_INFO("replay", "frame %u: picture to %s", g_frame, path.c_str());
    return path;
}

uint32_t frame_number() { return g_frame; }

void frame() {
    if (g_config.mode == Mode::Off) return;
    if (!t_bound) {
        static bool warned = false;
        if (!warned) LOG_WARN("replay", "frames are presented off the title's main thread; no state checks");
        warned = true;
        return;
    }
    ++g_frame;
    if (g_config.dump_frame && g_frame >= g_config.dump_frame && g_frame <= std::max(g_config.dump_frame, g_config.dump_last) &&
        (g_frame - g_config.dump_frame) % std::max(1u, g_config.dump_step) == 0)
        dump_state(g_config.dump_dir);
    if (g_live) return;
    const FrameRecord now = measure();
    if (!g_reference_done && g_frame < g_reference.size() && g_reference[g_frame].record.frame == g_frame &&
        std::memcmp(&g_reference[g_frame].record, &now, sizeof(now)) != 0) {
        g_reference_done = true;
        const ReferenceFrame& ref = g_reference[g_frame];
        std::string list;
        int shown = 0;
        for (size_t i = 0; i < std::max(ref.entities.size(), g_entity_hashes.size()) && shown < 16; ++i) {
            const uint32_t was = i < ref.entities.size() ? ref.entities[i] : 0;
            const uint32_t is = i < g_entity_hashes.size() ? g_entity_hashes[i] : 0;
            if (was == is) continue;
            list += " " + std::to_string(i);
            ++shown;
        }
        LOG_WARN("replay", "frame %u: first difference from the reference (level.time %d/%d%s%s); entities:%s", g_frame,
                 ref.record.level_time, now.level_time, ref.record.level_hash != now.level_hash ? ", level locals" : "",
                 ref.record.client_hash != now.client_hash ? ", clients" : "", list.c_str());
        dump_state(g_config.reference_dump_dir);
    }
    if (g_hashes) {
        write_to(g_hashes, kEntityHashesTag, g_entity_hashes.data(), g_entity_hashes.size() * 4);
        write_to(g_hashes, kFrameTag, &now, sizeof(now));
        std::fflush(g_hashes);
    }
    if (g_config.mode == Mode::Record) {
        write_record(kEntityHashesTag, g_entity_hashes.data(), g_entity_hashes.size() * 4);
        write_record(kFrameTag, &now, sizeof(now));
        std::fflush(g_file);  // a crash keeps every frame before it
        return;
    }
    if (!g_have_frame) return go_live();
    report_counts();
    const FrameRecord recorded = g_recorded_frame;
    ++g_checked;
    if (std::memcmp(&recorded, &now, sizeof(now)) != 0) {
        if (g_mismatches < 10 && recorded.entity_hash != now.entity_hash) {
            const Queue& q = g_queues[static_cast<uint8_t>(kEntityHashesTag)];
            std::string list;
            if (!q.items.empty()) {
                const std::vector<uint8_t>& item = q.items.front();
                const size_t n = std::max(item.size() / 4, g_entity_hashes.size());
                int shown = 0;
                for (size_t i = 0; i < n && shown < 12; ++i) {
                    uint32_t was = 0;
                    if (i < item.size() / 4) std::memcpy(&was, item.data() + i * 4, 4);
                    const uint32_t is = i < g_entity_hashes.size() ? g_entity_hashes[i] : 0;
                    if (was == is) continue;
                    const uint8_t* const ent = i < g_entity_hashes.size() ? entity(static_cast<uint32_t>(i)) : nullptr;
                    char text[48];
                    std::snprintf(text, sizeof(text), " %zu (%08x)", i,
                                  static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ent)));
                    list += text;
                    ++shown;
                }
            }
            LOG_WARN("replay", "frame %u: entities that differ (index, address):%s", g_frame, list.c_str());
        }
        if (!g_mismatches++) {
            g_first_mismatch = g_frame;
            LOG_WARN("replay",
                     "frame %u differs: level.time %d/%d, entities %u/%u, level hash %08x/%08x, entity hash %08x/%08x, "
                     "client hash %08x/%08x (recorded/now)",
                     g_frame, recorded.level_time, now.level_time, recorded.entity_count, now.entity_count,
                     recorded.level_hash, now.level_hash, recorded.entity_hash, now.entity_hash,
                     recorded.client_hash, now.client_hash);
        }
    }
    if (!load_frame()) go_live();
}

}  // namespace xbox::replay
