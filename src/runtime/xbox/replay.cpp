#include "replay.h"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

#include "kernel_memory.h"
#include "log.h"
#include "program.h"

namespace xbox::replay {
namespace {

// File: "XBREPLAY" + u32 version, then records [tag u8][size u16][payload].
constexpr char kMagic[8] = {'X', 'B', 'R', 'E', 'P', 'L', 'A', 'Y'};
// 4: timed waits (W), voice status (S), finished packets (k, K); 5: native keys (Y, y); 6: state hashes without
// pointer values (hash_state), so a recording survives the title's memory moving or being laid out differently;
// 7: keyed values ({site, value}: value(tag, real, site)), stream flushes (f, g, G), title thread starts (t), actor
// and sound voice hashes in the frame record, and an end record (Q: frames + CRC-32 of everything before it)
constexpr uint32_t kVersion = 7;
constexpr uint32_t kMaskedHashesVersion = 6;  // older recordings still play; their state is not compared
constexpr uint32_t kKeyedVersion = 7;
constexpr uint32_t kOldestVersion = 4;  // still replays (no native key records)
bool known_version(uint32_t v) { return v >= kOldestVersion && v <= kVersion; }
constexpr char kClockTag = 'C';
constexpr char kFrameTag = 'F';
constexpr char kSettingsTag = 'O';     // record start: settings.ini text
constexpr char kCommandLineTag = 'A';  // record start: command line
constexpr char kLayoutTag = 'Z';       // record start: where the title's memory was (LayoutRecord)
constexpr char kInputFormatTag = 'V';  // record start (re-recordings): the input format of the run, u32 (version())
constexpr char kEndTag = 'Q';          // last record (format 7): EndRecord
constexpr char kThreadTag = 't';       // a title thread started by the main thread (keyed by its routine)
constexpr char kCheckpointTag = 'c';   // a checkpoint saved (its name), in the frame it happened

// The title's memory ranges a recording was made with. hash_state masks values inside them, so a replay with its
// memory elsewhere also masks other values (name hashes and the like that happen to fall in a range): its state
// differences may then be false. Recordings before it have none (nothing is checked).
struct LayoutRecord {
    uint32_t contiguous_base, contiguous_bytes, virtual_base, virtual_bytes, heap_base, heap_bytes;
    bool operator==(const LayoutRecord&) const = default;
};

LayoutRecord current_layout() {
    const MemoryUsage usage = memory_usage();
    return {usage.layout.contiguous_base,
            usage.layout.physical_bytes,
            usage.layout.virtual_base,
            usage.layout.virtual_end - usage.layout.virtual_base,
            usage.layout.heap_bytes ? usage.layout.heap_base : 0,
            usage.layout.heap_bytes};
}
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
// The sound system's voice records (snd.cpp: 0xe0 bytes each from 0x00911548, the count at 0x008bed00): what plays,
// its state and requests - the title's side of sound, outside every entity.
constexpr uintptr_t kSoundVoices = 0x00911548;
constexpr uintptr_t kSoundVoiceCount = 0x008bed00;
constexpr size_t kSoundVoiceSize = 0xe0;
constexpr uint32_t kMaxSoundVoices = 256;

struct FrameRecord {
    uint32_t frame;
    int32_t level_time;
    uint32_t entity_count;
    uint32_t level_hash;
    uint32_t entity_hash;
    uint32_t client_hash;
    uint32_t actor_hash;  // format 7 on
    uint32_t voice_hash;  // format 7 on
};
constexpr size_t kOldFrameRecordSize = 24;  // formats 4-6

struct EndRecord {
    uint32_t frames;
    uint32_t crc;  // CRC-32 of the file from its first byte up to this record's tag
};

// CRC-32 (IEEE), updated as bytes are written or read.
uint32_t crc32_update(uint32_t crc, const void* data, size_t size) {
    static const auto table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    crc = ~crc;
    for (const auto* p = static_cast<const uint8_t*>(data); size--; ++p) crc = table[(crc ^ *p) & 0xff] ^ (crc >> 8);
    return ~crc;
}

Config g_config;
uint32_t g_version = kVersion;  // of the file being replayed (a recording writes kVersion)
// How the run's input was recorded (version()): the file's version, or what a re-recording kept of the original's
// (kInputFormatTag) - a format 4 recording brought to 6 still replays its keyboard through the pad state.
uint32_t g_input_version = kVersion;
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
        !known_version(version)) {
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
        } else if (static_cast<char>(tag) == kFrameTag && (length == sizeof(FrameRecord) || length == kOldFrameRecordSize)) {
            ReferenceFrame r;
            r.record = {};
            std::memcpy(&r.record, payload.data(), length);
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
std::FILE* g_rerecord = nullptr;  // replay: the new recording being written (Config::rerecord)
uint32_t g_file_crc = 0;          // record: of g_file so far; replay: of what load_frame read so far
uint32_t g_rerecord_crc = 0;
bool g_write_failed = false;

void raw_write(std::FILE* file, uint32_t& crc, const void* data, size_t size) {
    if (std::fwrite(data, 1, size, file) != size && !g_write_failed) {
        g_write_failed = true;
        LOG_ERROR("replay", "cannot write the recording (disk full?): it is incomplete from frame %u on", g_frame);
    }
    crc = crc32_update(crc, data, size);
}

void write_to(std::FILE* file, uint32_t& crc, char tag, const void* data, size_t size) {
    if (size > 0xffff) {  // the size field is 16 bits: a bigger record would make the rest of the file unreadable
        LOG_ERROR("replay", "record '%c' of %zu bytes is too big for the format", tag, size);
        std::abort();
    }
    const uint8_t tag_byte = static_cast<uint8_t>(tag);
    const uint16_t length = static_cast<uint16_t>(size);
    raw_write(file, crc, &tag_byte, 1);
    raw_write(file, crc, &length, 2);
    raw_write(file, crc, data, size);
}

void write_record(char tag, const void* data, size_t size) { write_to(g_file, g_file_crc, tag, data, size); }

// Re-recording: what the replay just handed the title, as recording mode would have written it.
void rerecord(char tag, const void* data, size_t size) {
    if (g_rerecord) write_to(g_rerecord, g_rerecord_crc, tag, data, size);
}

// The end record, then closed: a file without one was cut short (the run crashed or was killed).
void close_with_end(std::FILE*& file, uint32_t& crc, uint32_t frames) {
    if (!file) return;
    const EndRecord end{frames, crc};
    const uint8_t tag_byte = static_cast<uint8_t>(kEndTag);
    const uint16_t length = sizeof(end);
    std::fwrite(&tag_byte, 1, 1, file);
    std::fwrite(&length, 2, 1, file);
    std::fwrite(&end, 1, sizeof(end), file);
    if (std::fclose(file) != 0 && !g_write_failed) {
        g_write_failed = true;
        LOG_ERROR("replay", "cannot finish the recording file (disk full?)");
    }
    file = nullptr;
}

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
// Replay faults (finish() reports them; any = exit code 5 when the frames matched).
uint32_t g_count_frames = 0;    // frames whose reads differ in number from the recording
uint32_t g_missed_packets = 0;  // recorded sound packet completions the run never produced
uint32_t g_live_waits = 0;      // recorded waits that had to go live
uint32_t g_order = 0;           // keyed values read with another site (Fault::Order)
uint32_t g_threads = 0;         // title thread starts that differ (Fault::Thread)
bool g_layout_differs = false;  // recorded with the title's memory elsewhere: masked hashes may compare falsely
bool g_ended = false;           // format 7: the end record was found and its CRC and frame count agree
std::string g_file_problem;     // format 7: why the file is not whole (no end record, bad CRC)

// Checkpoints: the recording's (found by scan() before the replay starts) and this run's, by order.
struct Checkpoint {
    std::string name;
    uint32_t frame;
};
std::vector<Checkpoint> g_expected_checkpoints, g_checkpoints;
uint32_t g_checkpoint_faults = 0;
constexpr uint32_t kCheckpointSlack = 0;  // frames: a deterministic replay saves each one on the recorded frame

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
        if (std::fread(&tag, 1, 1, g_file) != 1 || std::fread(&length, 2, 1, g_file) != 1) {
            if (g_version >= kVersion && !g_ended && g_file_problem.empty())
                g_file_problem = "it ends without its end record (the recording run crashed or was killed)";
            return false;
        }
        std::vector<uint8_t> payload(length);
        if (std::fread(payload.data(), 1, length, g_file) != length) {
            if (g_file_problem.empty()) g_file_problem = "its last record is cut short";
            return false;
        }
        if (static_cast<char>(tag) == kEndTag) {
            EndRecord end{};
            if (length == sizeof(end)) std::memcpy(&end, payload.data(), sizeof(end));
            if (length != sizeof(end) || end.crc != g_file_crc)
                g_file_problem = "its CRC does not match (the file is damaged)";
            else
                g_ended = true;
            return false;
        }
        g_file_crc = crc32_update(g_file_crc, &tag, 1);
        g_file_crc = crc32_update(g_file_crc, &length, 2);
        g_file_crc = crc32_update(g_file_crc, payload.data(), length);
        if (static_cast<char>(tag) == kSettingsTag || static_cast<char>(tag) == kCommandLineTag) continue;
        if (static_cast<char>(tag) == kInputFormatTag) {
            if (length == sizeof(g_input_version)) std::memcpy(&g_input_version, payload.data(), length);
            continue;
        }
        if (static_cast<char>(tag) == kLayoutTag) {
            LayoutRecord recorded{};
            if (length == sizeof(recorded)) std::memcpy(&recorded, payload.data(), sizeof(recorded));
            if (const LayoutRecord now = current_layout(); !(recorded == now) && (g_layout_differs = true))
                LOG_WARN("replay",
                         "recorded with the title's memory elsewhere (contiguous %08x+%x, virtual %08x+%x, heap "
                         "%08x+%x; now %08x+%x, %08x+%x, %08x+%x): state differences may be false - record it again",
                         recorded.contiguous_base, recorded.contiguous_bytes, recorded.virtual_base,
                         recorded.virtual_bytes, recorded.heap_base, recorded.heap_bytes, now.contiguous_base,
                         now.contiguous_bytes, now.virtual_base, now.virtual_bytes, now.heap_base, now.heap_bytes);
            continue;
        }
        if (static_cast<char>(tag) == kFrameTag && (length == sizeof(FrameRecord) || length == kOldFrameRecordSize)) {
            g_recorded_frame = {};
            std::memcpy(&g_recorded_frame, payload.data(), length);
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

// Replay: a keyed value ({site, value}, format 7). Out of order: the frame's item with this site if it has one (moved
// to the front), else none (extra read); both count a fault.
bool take_keyed(char tag, uint32_t site, uint32_t& v) {
    if (!site || g_version < kKeyedVersion) return take(tag, &v, sizeof(v));
    Queue& q = g_queues[static_cast<uint8_t>(tag)];
    const auto site_of = [](const std::vector<uint8_t>& item) {
        uint32_t s = 0;
        if (item.size() == 8) std::memcpy(&s, item.data(), 4);
        return s;
    };
    if (q.next >= q.items.size() || site_of(q.items[q.next]) != site) {
        ++g_order;
        if (g_order <= 5)
            LOG_WARN("replay", "frame %u: '%c' read for %08x, the recording has %s here", g_frame, tag, site,
                     q.next < q.items.size() ? "another" : "none");
        size_t i = q.next;
        while (i < q.items.size() && site_of(q.items[i]) != site) ++i;
        if (i == q.items.size()) {
            ++q.extra;
            return false;
        }
        std::rotate(q.items.begin() + static_cast<ptrdiff_t>(q.next), q.items.begin() + static_cast<ptrdiff_t>(i),
                    q.items.begin() + static_cast<ptrdiff_t>(i) + 1);
    }
    q.last = q.items[q.next++];
    std::memcpy(&v, q.last.data() + 4, 4);
    return true;
}

// Record / re-record a keyed value (format 7 layout when keyed).
void write_keyed(void (*write)(char, const void*, size_t), char tag, uint32_t site, uint32_t v) {
    if (!site) return write(tag, &v, sizeof(v));
    const uint32_t item[2] = {site, v};
    write(tag, item, sizeof(item));
}

// Replay, at a frame's end: reads the title made more or fewer times than recorded (the first few frames).
void report_counts() {
    std::string text;
    for (int t = 0; t < 256; ++t) {
        if (t == kEntityHashesTag || t == kCheckpointTag) continue;  // not read by the title (frame(), checkpoint())
        const Queue& q = g_queues[t];
        const size_t unread = q.items.size() - q.next;
        if (!q.extra && !unread) continue;
        text += " '" + std::string(1, static_cast<char>(t)) + "' " +
                (q.extra ? "+" + std::to_string(q.extra) : "-" + std::to_string(unread));
    }
    if (text.empty()) return;
    ++g_count_frames;
    if (g_count_reports >= 5) return;
    ++g_count_reports;
    LOG_INFO("replay", "frame %u: read counts differ from the recording:%s%s", g_frame, text.c_str(),
             g_count_reports == 5 ? " (no more of these)" : "");
}

// The state hash (word-wise FNV-1a, integer only: title threads run with the title's FPU modes). Every word that
// points into the title's memory (heaps, objects, contiguous memory - xbox::in_title_memory) counts as one marker
// value, so only what the game holds is compared, not where it sits. Pointers into the title's image stay as they
// are (the image does not move).
uint32_t hash_state(const void* data, size_t size, uint32_t h = 2166136261u) {
    constexpr uint32_t kPointer = 0x9e3779b9u;
    const auto* bytes = static_cast<const uint8_t*>(data);
    size_t i = 0;
    for (; i + 4 <= size; i += 4) {
        uint32_t word;
        std::memcpy(&word, bytes + i, 4);
        if (in_title_memory(word)) word = kPointer;
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
    r.level_hash = hash_state(title_bytes(kLevelLocals), kLevelLocalsSize);
    r.entity_count = entity_count();
    g_entity_hashes.assign(r.entity_count, 0);
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < r.entity_count; ++i) {
        const uint8_t* const ent = entity(i);
        const uint32_t one = ent ? hash_state(ent, kEntitySize) : 0;
        g_entity_hashes[i] = one;
        h = (h ^ one) * 16777619u;
    }
    r.entity_hash = h;
    uint32_t clients = 0, count = 0;
    std::memcpy(&clients, title_bytes(kLevelLocals), 4);
    std::memcpy(&count, title_bytes(kLevelMaxClients), 4);
    r.client_hash = clients && count <= kMaxClients ? hash_state(title_bytes(clients), count * kClientSize) : 0;
    uint32_t a = 2166136261u;
    for (uint32_t i = 0; i < kActorSlots; ++i) {
        uint32_t actor = 0;
        std::memcpy(&actor, title_bytes(kActors + i * 4), 4);
        a = (a ^ (actor && in_title_memory(actor) ? hash_state(title_bytes(actor), kActorSize) : 0)) * 16777619u;
    }
    r.actor_hash = a;
    uint32_t voices = 0;
    std::memcpy(&voices, title_bytes(kSoundVoiceCount), 4);
    r.voice_hash = hash_state(title_bytes(kSoundVoices), std::min(voices, kMaxSoundVoices) * kSoundVoiceSize);
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

std::string clock_text(uint32_t frame) {  // the frame's time into the run at 60 frames a second, m:ss
    char text[16];
    std::snprintf(text, sizeof(text), "%u:%02u", frame / 3600, frame / 60 % 60);
    return text;
}


void finish() {
    if (g_finished || g_config.mode != Mode::Replay) return;
    g_finished = true;
    if (g_rerecord) {  // closed before end_process, which skips exit handlers (and stdio's flush)
        close_with_end(g_rerecord, g_rerecord_crc, g_frame);
        LOG_INFO("replay", "re-recorded %u frames to %s", g_frame, g_config.rerecord.string().c_str());
    }
    if (g_version < kMaskedHashesVersion) {
        LOG_WARN("replay", "%u frames played; state not compared: recorded before version %u (its hashes hold pointer "
                 "values) - record it again", g_checked, kMaskedHashesVersion);
        if (g_config.exit_at_end) platform::end_process(4);
        return;
    }
    if (g_mismatches)
        LOG_WARN("replay", "%u of %u frames differed, the first at frame %u", g_mismatches, g_checked, g_first_mismatch);
    else
        LOG_INFO("replay", "all %u frames matched", g_checked);
    if (!g_file_problem.empty()) LOG_WARN("replay", "the recording is not whole: %s", g_file_problem.c_str());
    // Checkpoints the recording saved that this run never reached (it ended or went elsewhere first).
    for (size_t i = g_checkpoints.size(); i < g_expected_checkpoints.size(); ++i) {
        ++g_checkpoint_faults;
        LOG_WARN("replay", "checkpoint '%s' (recorded at frame %u, %s) was never reached",
                 g_expected_checkpoints[i].name.c_str(), g_expected_checkpoints[i].frame,
                 clock_text(g_expected_checkpoints[i].frame).c_str());
    }
    if (!g_expected_checkpoints.empty() || !g_checkpoints.empty()) {
        LOG_INFO("replay", "checkpoints (name: recorded frame -> this run's, difference):");
        for (size_t i = 0; i < std::max(g_expected_checkpoints.size(), g_checkpoints.size()); ++i) {
            const Checkpoint* e = i < g_expected_checkpoints.size() ? &g_expected_checkpoints[i] : nullptr;
            const Checkpoint* r = i < g_checkpoints.size() ? &g_checkpoints[i] : nullptr;
            const long delta = e && r ? long(r->frame) - long(e->frame) : 0;
            LOG_INFO("replay", "  %-28s %6s -> %6s %s", (e ? e->name : r->name).c_str(),
                     e ? clock_text(e->frame).c_str() : "-", r ? clock_text(r->frame).c_str() : "-",
                     e && r ? (e->name != r->name ? "OTHER CHECKPOINT"
                               : delta == 0       ? "same frame"
                                                  : (std::to_string(delta) + " frames (" +
                                                     std::to_string(delta / 60) + " s)").c_str())
                            : (e ? "NOT REACHED" : "NOT IN THE RECORDING"));
        }
    }
    if (g_layout_differs) LOG_WARN("replay", "recorded with the title's memory laid out differently: hashes may compare falsely");
    const uint32_t faults = g_count_frames + g_missed_packets + g_live_waits + g_order + g_threads +
                            g_checkpoint_faults + (g_layout_differs ? 1 : 0) + (g_file_problem.empty() ? 0 : 1);
    if (faults)
        LOG_WARN("replay", "replay faults (the run did not see what the recording saw): %u frames with other read "
                 "counts, %u sound packets never finished, %u waits went live, %u reads out of order, %u thread "
                 "starts differ, %u checkpoints wrong or missed%s%s - not a pass", g_count_frames, g_missed_packets,
                 g_live_waits, g_order, g_threads, g_checkpoint_faults,
                 g_layout_differs ? ", memory layout differs" : "", g_file_problem.empty() ? "" : ", file not whole");
    if (g_config.exit_at_end) platform::end_process(g_mismatches ? 3 : faults ? 5 : 0);
}

}  // namespace

bool read_recorded_settings(const std::filesystem::path& file, std::string& settings_text, std::string& command_line) {
    std::FILE* f = std::fopen(file.string().c_str(), "rb");
    if (!f) return false;
    char magic[8];
    uint32_t version = 0;
    bool ok = std::fread(magic, 1, 8, f) == 8 && std::memcmp(magic, kMagic, 8) == 0 && std::fread(&version, 4, 1, f) == 1 &&
              known_version(version);
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

// Replay: the whole file read once before it plays - its checkpoints, and whether it is whole (format 7: an end
// record whose CRC and frame count match), so a damaged or cut recording is reported before anything runs.
void scan(const std::filesystem::path& path, uint32_t version) {
    std::FILE* f = std::fopen(path.string().c_str(), "rb");
    if (!f) return;
    std::vector<char> buffer(1 << 20);
    std::setvbuf(f, buffer.data(), _IOFBF, buffer.size());
    uint8_t header[12];
    uint32_t crc = 0, frames = 0;
    bool ended = false;
    if (std::fread(header, 1, 12, f) == 12) crc = crc32_update(0, header, 12);
    std::vector<uint8_t> payload;
    for (;;) {
        uint8_t tag = 0;
        uint16_t length = 0;
        if (std::fread(&tag, 1, 1, f) != 1 || std::fread(&length, 2, 1, f) != 1) break;
        payload.resize(length);
        if (std::fread(payload.data(), 1, length, f) != length) break;
        if (static_cast<char>(tag) == kEndTag) {
            EndRecord end{};
            if (length == sizeof(end)) std::memcpy(&end, payload.data(), sizeof(end));
            ended = true;
            if (length != sizeof(end) || end.crc != crc)
                g_file_problem = "its CRC does not match (the file is damaged)";
            else if (end.frames != frames)
                g_file_problem = "its end record counts " + std::to_string(end.frames) + " frames, the file has " +
                                 std::to_string(frames);
            break;
        }
        crc = crc32_update(crc, &tag, 1);
        crc = crc32_update(crc, &length, 2);
        crc = crc32_update(crc, payload.data(), length);
        if (static_cast<char>(tag) == kFrameTag) ++frames;
        if (static_cast<char>(tag) == kCheckpointTag)
            g_expected_checkpoints.push_back({std::string(payload.begin(), payload.end()), frames});
    }
    std::fclose(f);
    if (version >= kVersion && !ended && g_file_problem.empty())
        g_file_problem = "it ends without its end record (the recording run crashed or was killed)";
    if (!g_file_problem.empty()) LOG_WARN("replay", "the recording is not whole: %s", g_file_problem.c_str());
    if (version >= kVersion)
        LOG_INFO("replay", "the recording: %u frames, %zu checkpoints", frames, g_expected_checkpoints.size());
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
        g_file_crc = 0;
        raw_write(g_file, g_file_crc, kMagic, sizeof(kMagic));
        raw_write(g_file, g_file_crc, &kVersion, 4);
        write_record(kSettingsTag, config.settings_text.data(), std::min<size_t>(config.settings_text.size(), 0xffff));
        write_record(kCommandLineTag, config.command_line.data(), std::min<size_t>(config.command_line.size(), 0xffff));
        const LayoutRecord layout = current_layout();
        write_record(kLayoutTag, &layout, sizeof(layout));
        g_live = false;
        LOG_INFO("replay", "recording to %s", config.file.string().c_str());
        return true;
    }
    char magic[8];
    uint32_t version = 0;
    if (std::fread(magic, 1, 8, g_file) != 8 || std::memcmp(magic, kMagic, 8) != 0 ||
        std::fread(&version, 4, 1, g_file) != 1 || !known_version(version)) {
        LOG_ERROR("replay", "%s is not a recording (version %u expected)", config.file.string().c_str(), kVersion);
        return false;
    }
    g_version = version;
    g_input_version = version;
    g_file_crc = crc32_update(crc32_update(0, magic, 8), &version, 4);
    scan(config.file, version);
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
        uint32_t unused_crc = 0;
        raw_write(g_hashes, unused_crc, kMagic, sizeof(kMagic));
        raw_write(g_hashes, unused_crc, &kVersion, 4);
    }
    if (!config.rerecord.empty()) {
        std::string settings_text, command_line;
        read_recorded_settings(config.file, settings_text, command_line);
        g_rerecord = std::fopen(config.rerecord.string().c_str(), "wb");
        if (!g_rerecord) {
            LOG_ERROR("replay", "cannot write %s", config.rerecord.string().c_str());
            return false;
        }
        g_rerecord_crc = 0;
        raw_write(g_rerecord, g_rerecord_crc, kMagic, sizeof(kMagic));
        raw_write(g_rerecord, g_rerecord_crc, &kVersion, 4);
        rerecord(kSettingsTag, settings_text.data(), std::min<size_t>(settings_text.size(), 0xffff));
        rerecord(kCommandLineTag, command_line.data(), std::min<size_t>(command_line.size(), 0xffff));
        const LayoutRecord layout = current_layout();
        rerecord(kLayoutTag, &layout, sizeof(layout));
        rerecord(kInputFormatTag, &g_input_version, sizeof(g_input_version));
        LOG_INFO("replay", "re-recording to %s (format %u)", config.rerecord.string().c_str(), kVersion);
    }
    LOG_INFO("replay", "replaying %s", config.file.string().c_str());
    return true;
}

void stop() {
    if (g_rerecord) close_with_end(g_rerecord, g_rerecord_crc, g_frame);
    if (!g_file) return;
    if (g_config.mode == Mode::Replay) {
        g_live = true;
        finish();
        std::fclose(g_file);
        g_file = nullptr;
        return;
    }
    close_with_end(g_file, g_file_crc, g_frame);
    LOG_INFO("replay", "recorded %u frames to %s%s", g_frame, g_config.file.string().c_str(),
             g_write_failed ? " - INCOMPLETE (a write failed)" : "");
}

void bind_current_thread() { t_bound = true; }

bool active() { return g_config.mode != Mode::Off; }

bool on_stream() { return on_stream_impl(); }

bool main_thread() { return t_bound; }

bool replaying() { return g_config.mode == Mode::Replay; }

uint32_t version() { return g_input_version; }

bool recorded(char tag, uint32_t& v, uint32_t site) {
    if (!on_stream() || g_config.mode != Mode::Replay) return false;
    if (!take_keyed(tag, site, v)) return false;
    write_keyed(rerecord, tag, site, v);
    return true;
}

bool take_recorded(char tag, uint32_t& v, uint32_t site) {
    if (!on_stream() || g_config.mode != Mode::Replay) return false;
    return take_keyed(tag, site, v);
}

void rerecord_value(char tag, uint32_t v, uint32_t site) { write_keyed(rerecord, tag, site, v); }

void fault(Fault kind) {
    if (g_config.mode != Mode::Replay) return;
    if (kind == Fault::MissedPacket) ++g_missed_packets;
    if (kind == Fault::LiveWait) ++g_live_waits;
    if (kind == Fault::Order) ++g_order;
    if (kind == Fault::Thread) ++g_threads;
}

void checkpoint(const char* name) {
    if (g_config.mode == Mode::Off || !t_bound) return;
    const std::string text = name ? std::string(name).substr(0, 200) : std::string();
    LOG_INFO("replay", "checkpoint '%s' at frame %u (%s)", text.c_str(), g_frame, clock_text(g_frame).c_str());
    if (g_config.mode == Mode::Record) {
        if (on_stream_impl()) write_record(kCheckpointTag, text.data(), text.size());
        return;
    }
    rerecord(kCheckpointTag, text.data(), text.size());
    g_checkpoints.push_back({text, g_frame});
    if (g_version < kVersion) return;  // older recordings have no checkpoint records
    const size_t i = g_checkpoints.size() - 1;
    if (i >= g_expected_checkpoints.size()) {
        ++g_checkpoint_faults;
        LOG_WARN("replay", "checkpoint '%s' at frame %u: the recording has no more checkpoints - the run diverged",
                 text.c_str(), g_frame);
        return;
    }
    const Checkpoint& e = g_expected_checkpoints[i];
    const uint32_t off = g_frame > e.frame ? g_frame - e.frame : e.frame - g_frame;
    if (e.name != text || off > kCheckpointSlack) {
        ++g_checkpoint_faults;
        LOG_WARN("replay", "checkpoint '%s' at frame %u (%s): the recording saved '%s' at frame %u (%s) - the run "
                 "diverged", text.c_str(), g_frame, clock_text(g_frame).c_str(), e.name.c_str(), e.frame,
                 clock_text(e.frame).c_str());
    }
}

void thread_started(uint32_t routine) {
    if (!on_stream_impl()) return;
    if (g_config.mode == Mode::Record) return write_keyed(write_record, kThreadTag, routine, 1);
    if (g_version >= kKeyedVersion) {
        Queue& q = g_queues[static_cast<uint8_t>(kThreadTag)];
        uint32_t recorded = 0;
        if (q.next < q.items.size() && q.items[q.next].size() == 8) std::memcpy(&recorded, q.items[q.next].data(), 4);
        if (recorded != routine) {
            ++g_threads;
            LOG_WARN("replay", "frame %u: title thread %08x started; the recording %s", g_frame, routine,
                     recorded ? "started another here" : "started none here");
        } else {
            ++q.next;
        }
    }
    write_keyed(rerecord, kThreadTag, routine, 1);
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
        rerecord(kClockTag, &recorded, sizeof(recorded));
        return recorded;
    }
    const int64_t extra = g_clock_recorded + (real - g_clock_real);
    rerecord(kClockTag, &extra, sizeof(extra));
    return extra;
}

uint32_t value(char tag, uint32_t real, uint32_t site) {
    if (!on_stream_impl()) return real;
    if (g_config.mode == Mode::Record) {
        write_keyed(write_record, tag, site, real);
        return real;
    }
    uint32_t v = real;
    take_keyed(tag, site, v);
    write_keyed(rerecord, tag, site, v);
    return v;
}

void bytes(char tag, void* data, size_t size) {
    if (!on_stream_impl()) return;
    if (g_config.mode == Mode::Record) {
        write_record(tag, data, size);
        return;
    }
    take(tag, data, size);
    rerecord(tag, data, size);
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

uint32_t frames_to_next_shot() {
    const auto& frames = g_config.shot_frames;
    const auto next = std::lower_bound(frames.begin(), frames.end(), g_frame);
    return next == frames.end() ? UINT32_MAX : *next - g_frame;
}

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
        uint32_t unused_crc = 0;
        write_to(g_hashes, unused_crc, kEntityHashesTag, g_entity_hashes.data(), g_entity_hashes.size() * 4);
        write_to(g_hashes, unused_crc, kFrameTag, &now, sizeof(now));
        std::fflush(g_hashes);
    }
    if (g_rerecord) {
        rerecord(kEntityHashesTag, g_entity_hashes.data(), g_entity_hashes.size() * 4);
        rerecord(kFrameTag, &now, sizeof(now));
        if (std::fflush(g_rerecord) != 0 && !g_write_failed) {
            g_write_failed = true;
            LOG_ERROR("replay", "cannot write the re-recording (disk full?): it is incomplete from frame %u on", g_frame);
        }
    }
    if (g_config.mode == Mode::Record) {
        write_record(kEntityHashesTag, g_entity_hashes.data(), g_entity_hashes.size() * 4);
        write_record(kFrameTag, &now, sizeof(now));
        // A crash keeps every frame before it. A failed write is said at once: the player can stop and play again.
        if ((std::fflush(g_file) != 0 || std::ferror(g_file)) && !g_write_failed) {
            g_write_failed = true;
            LOG_ERROR("replay", "cannot write the recording (disk full?): it is incomplete from frame %u on", g_frame);
        }
        return;
    }
    if (!g_have_frame) return go_live();
    report_counts();
    const FrameRecord recorded = g_recorded_frame;
    ++g_checked;
    // Formats before 7 have no actor or voice hashes: only what they recorded is compared.
    const size_t compared = g_version >= kKeyedVersion ? sizeof(now) : kOldFrameRecordSize;
    if (g_version >= kMaskedHashesVersion && std::memcmp(&recorded, &now, compared) != 0) {
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
                     "client hash %08x/%08x, actor hash %08x/%08x, sound voice hash %08x/%08x (recorded/now)",
                     g_frame, recorded.level_time, now.level_time, recorded.entity_count, now.entity_count,
                     recorded.level_hash, now.level_hash, recorded.entity_hash, now.entity_hash,
                     recorded.client_hash, now.client_hash, recorded.actor_hash, now.actor_hash,
                     recorded.voice_hash, now.voice_hash);
        }
    }
    if (!load_frame()) go_live();
}

}  // namespace xbox::replay
