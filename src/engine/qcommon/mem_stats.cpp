#include "mem_stats.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <utility>

#include "qcommon_core.h"
#include "title_data.h"
#include "title_host.h"

namespace game {

namespace {

constexpr size_t kMaxPools = 80;

struct Pool {
    char name[40];
    uint32_t size;  // bytes or blocks
    uint32_t value;  // now, or the peak
};

struct MapPeaks {
    std::string map;
    uint32_t frames = 0;
    std::vector<Pool> pools;  // peaks
};

struct State {
    std::mutex mutex;
    bool enabled = false;
    std::vector<MapPeaks> maps;      // in play order; the last one is the current map
    std::vector<std::string> finished;
    char current_map[64] = {};
};

State& state() {
    static State* s = new State;  // lives to the end of the process (exit handlers report from it)
    return *s;
}

uint32_t word(uint32_t address) { return title_global<uint32_t>(address); }

// The pools as they are this frame.
struct Frame {
    Pool pools[kMaxPools];
    size_t count = 0;

    // Per-map columns take what each frame shows: the pools' own peak counters (heap +0x48c, small pool +0x14)
    // run over the whole session and would carry one map's peak into the next.
    void add(const char* name, uint32_t size, uint32_t now) {
        if (count == kMaxPools || !size) return;
        Pool& pool = pools[count++];
        std::snprintf(pool.name, sizeof(pool.name), "%s", name);
        pool.size = size;
        pool.value = now;
    }

    // A Heap (0x494 bytes): region [+0x474, +0x478), capacity +0x484, used +0x488, peak +0x48c, sbrk top +0x47c.
    // The high-water row is the heap's own peak, so far in the session (inside a frame too).
    void heap(const char* name, uint32_t heap) {
        if (!heap || !word(heap + 0x484)) return;
        char label[40];
        add(name, word(heap + 0x484), word(heap + 0x488));
        std::snprintf(label, sizeof(label), "%s top", name);
        add(label, word(heap + 0x484), word(heap + 0x47c) - word(heap + 0x474));
        std::snprintf(label, sizeof(label), "%s high-water", name);
        add(label, word(heap + 0x484), word(heap + 0x48c));
    }

    // A SmallHeap: pool count +0, pools +4 (8 at most); a pool: block size +0, blocks +4, free +8, peak +0x14.
    void small_heap(const char* name, uint32_t heap) {
        if (!heap) return;
        const uint32_t pools = std::min<uint32_t>(word(heap), 8);
        for (uint32_t i = 0; i < pools; ++i) {
            const uint32_t pool = word(heap + 4 + i * 4);
            if (!pool) continue;
            char label[40];
            std::snprintf(label, sizeof(label), "%s 0x%x", name, word(pool));
            add(label, word(pool + 4), word(pool + 4) - word(pool + 8));
        }
    }

    void sample() {
        heap("physical heap", 0x008ff638);
        heap("write-combined heap", 0x008fff60);
        heap("script zone heap", word(0x0095f4a0));
        heap("aps zone heap", word(0x0095f6d8));
        // Ours: what the full zone heaps borrowed from the physical heap (Heap_AllocFromHeap).
        add("zone heaps lent", word(0x008ff638 + 0x484), Heap_ZoneLentBytes());

        // Stream (zone load) heaps: the owner list 0x008ff4a8, count 0x008ff5a8, room for 64.
        const uint32_t stream_heaps = std::min<uint32_t>(word(0x008ff5a8), 64);
        uint32_t stream_size = 0, stream_used = 0;
        for (uint32_t i = 0; i < stream_heaps; ++i) {
            const uint32_t heap = word(0x008ff4a8 + i * 4);
            if (!heap) continue;
            stream_size += word(heap + 0x484);
            stream_used += word(heap + 0x488);
        }
        add("stream heaps (of 64)", 64, stream_heaps);
        add("stream heaps bytes", stream_size, stream_used);

        // Stream banks (0x0095f62c -> 0x2c bytes): two 64-bit sets of free banks (words 0-1, 2-3), count +0x10,
        // bank size +0x14.
        if (const uint32_t banks = word(0x0095f62c)) {
            const uint32_t count = word(banks + 0x10);
            const auto used = [&](uint32_t at) {
                return count - uint32_t(std::popcount(word(banks + at)) + std::popcount(word(banks + at + 4)));
            };
            char label[40];
            std::snprintf(label, sizeof(label), "stream banks (0x%x bytes)", word(banks + 0x14));
            add(label, count, used(0));
            add("stream banks B", count, used(8));
        }

        // Sound memory (SndMem_Init): size 0x0091f64c; 64 block sizes 0x0091f548 (bit 31 = freed) filled from both
        // ends: 0..low top (0x008bed08), high bottom (0x008bed04)..63.
        if (const uint32_t size = word(0x0091f64c)) {
            const int32_t low = title_global<int32_t>(0x008bed08);
            const int32_t high = title_global<int32_t>(0x008bed04);
            uint32_t used = 0, blocks = 0;
            const auto block = [&](int32_t i) {
                const uint32_t b = word(0x0091f548 + uint32_t(i) * 4);
                ++blocks;
                if (!(b & 0x80000000u)) used += b;
            };
            for (int32_t i = 0; i <= low && i < 64; ++i) block(i);
            for (int32_t i = std::max(high, 0); i < 64; ++i) block(i);
            add("sound memory", size, used);
            add("sound blocks", 64, blocks);
        }

        small_heap("com small", word(0x00947700));
        small_heap("sys small", word(0x0095f4a4));
        small_heap("script thread small", word(0x0095f490));
        small_heap("stream small", word(0x0095f624));
        small_heap("stream heap headers", word(0x0095f628));

        // Effects pools (apsAddPool: Mem_InitLargeBlockPools / Mem_InitSmallBlockPools): the pool set 0x00909f08
        // -> {pools, int16 capacity, int16 count}; a pool: items +0, used +4, item size +0x14.
        if (const uint32_t set = word(0x00909f08)) {
            const uint32_t pools = word(set);
            const int32_t n = std::min<int32_t>(title_global<int16_t>(set + 6), 24);
            for (int32_t i = 0; pools && i < n; ++i) {
                const uint32_t pool = word(pools + uint32_t(i) * 4);
                if (!pool) continue;
                char label[40];
                std::snprintf(label, sizeof(label), "fx pool 0x%x", word(pool + 0x14));
                add(label, word(pool), word(pool + 4));
            }
        }

        add("corpses (of 16)", 16, word(0x00985418));

        if (title_host().memory_statistics) {
            uint32_t total = 0, available = 0, reserved = 0;
            title_host().memory_statistics(&total, &available, &reserved);
            add("kernel pages", total, total - available);
        }
    }
};

// sv_mapname (cvar 0x00929c08, its string at +4), or "" before the first map.
void current_map(char (&out)[64]) {
    out[0] = 0;
    const uint32_t cvar = word(0x00929c08);
    if (!cvar) return;
    const char* const text = reinterpret_cast<const char*>(uintptr_t{word(cvar + 4)});
    if (text) std::snprintf(out, sizeof(out), "%s", text);
}

uint32_t percent(const Pool& pool) { return pool.size ? uint32_t(uint64_t(pool.value) * 1000 / pool.size) : 0; }

std::string finished_line(const MapPeaks& map) {
    std::string line = "map " + (map.map.empty() ? std::string("(front end)") : map.map) + " (" +
                       std::to_string(map.frames) + " frames), pools at 90 % or more:";
    bool any = false;
    for (const Pool& pool : map.pools) {
        if (percent(pool) < 900) continue;
        char text[96];
        std::snprintf(text, sizeof(text), " %s %u.%u %%;", pool.name, percent(pool) / 10, percent(pool) % 10);
        line += text;
        any = true;
    }
    if (!any) line += " none";
    return line;
}

}  // namespace

void MemStats_Enable() { state().enabled = true; }

bool MemStats_Enabled() { return state().enabled; }

void MemStats_Sample() {
    State& s = state();
    if (!s.enabled) return;
    Frame frame;
    frame.sample();
    char map[64];
    current_map(map);
    std::lock_guard lock(s.mutex);
    if (s.maps.empty() || std::strcmp(map, s.current_map) != 0) {
        if (!s.maps.empty()) s.finished.push_back(finished_line(s.maps.back()));
        std::memcpy(s.current_map, map, sizeof(map));
        s.maps.push_back(MapPeaks{map, 0, {}});
        s.maps.back().pools.reserve(kMaxPools);
    }
    MapPeaks& peaks = s.maps.back();
    ++peaks.frames;
    for (size_t i = 0; i < frame.count; ++i) {
        const Pool& pool = frame.pools[i];
        Pool* found = i < peaks.pools.size() && !std::strcmp(peaks.pools[i].name, pool.name) ? &peaks.pools[i] : nullptr;
        for (size_t j = 0; !found && j < peaks.pools.size(); ++j)
            if (!std::strcmp(peaks.pools[j].name, pool.name)) found = &peaks.pools[j];
        if (!found) {
            peaks.pools.push_back(pool);
            continue;
        }
        found->size = std::max(found->size, pool.size);
        found->value = std::max(found->value, pool.value);
    }
}

std::string MemStats_Report() {
    State& s = state();
    std::lock_guard lock(s.mutex);
    // Rows: every pool in first-seen order; columns: maps (a map played twice gets two columns).
    std::vector<std::string> names;
    std::vector<uint32_t> sizes;
    for (const MapPeaks& map : s.maps)
        for (const Pool& pool : map.pools) {
            const auto it = std::find(names.begin(), names.end(), pool.name);
            if (it == names.end()) {
                names.push_back(pool.name);
                sizes.push_back(pool.size);
            } else {
                sizes[size_t(it - names.begin())] = std::max(sizes[size_t(it - names.begin())], pool.size);
            }
        }
    std::string out = "Memory pools: peak % of each pool's size per map (bytes, or blocks/slots for counts)\n\n";
    char cell[96];
    std::snprintf(cell, sizeof(cell), "%-30s %11s %11s", "pool", "size", "peak");
    out += cell;
    for (const MapPeaks& map : s.maps) {
        std::snprintf(cell, sizeof(cell), " %9.9s", map.map.empty() ? "frontend" : map.map.c_str());
        out += cell;
    }
    out += "    max%\n";
    for (size_t row = 0; row < names.size(); ++row) {
        uint32_t peak = 0, best = 0;
        std::string cells;
        for (const MapPeaks& map : s.maps) {
            const Pool* pool = nullptr;
            for (const Pool& p : map.pools)
                if (names[row] == p.name) pool = &p;
            if (!pool) {
                cells += "         -";
                continue;
            }
            peak = std::max(peak, pool->value);
            best = std::max(best, percent(*pool));
            std::snprintf(cell, sizeof(cell), " %7u.%u", percent(*pool) / 10, percent(*pool) % 10);
            cells += cell;
        }
        std::snprintf(cell, sizeof(cell), "%-30s %11u %11u", names[row].c_str(), sizes[row], peak);
        out += cell + cells;
        std::snprintf(cell, sizeof(cell), "  %4u.%u\n", best / 10, best % 10);
        out += cell;
    }
    out += "\nframes per map:";
    for (const MapPeaks& map : s.maps) out += " " + (map.map.empty() ? std::string("frontend") : map.map) + "=" +
                                             std::to_string(map.frames);
    out += "\n";
    return out;
}

uint32_t MemStats_Frames() {
    State& s = state();
    std::lock_guard lock(s.mutex);
    uint32_t frames = 0;
    for (const MapPeaks& map : s.maps) frames += map.frames;
    return frames;
}

std::vector<std::string> MemStats_TakeFinishedMaps() {
    State& s = state();
    std::lock_guard lock(s.mutex);
    return std::exchange(s.finished, {});
}

}  // namespace game
