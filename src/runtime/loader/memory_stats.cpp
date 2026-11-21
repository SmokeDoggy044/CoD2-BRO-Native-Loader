#include "memory_stats.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include "kernel_memory.h"
#include "log.h"
#include "mem_stats.h"
#include "vmem.h"

namespace loader {

namespace fs = std::filesystem;
using platform::vmem::Region;

namespace {

struct Label {
    uintptr_t base;
    uint64_t size;
    std::string name;
};

// One reservation (Windows: allocation base) or mapping (POSIX), named as well as we can.
struct Group {
    std::string name;
    uintptr_t base = 0;
    uint64_t committed = 0, reserved = 0;
};

struct Picture {
    double seconds = 0;
    uint64_t free = 0, largest_free = 0, committed = 0, reserved = 0;
    uint64_t committed_by_kind[3] = {}, reserved_by_kind[3] = {};
    std::vector<Group> groups;  // largest first
    bool valid = false;
};

struct State {
    std::mutex mutex;
    fs::path file;
    bool started = false;
    std::vector<Label> labels;
    Picture first, least_free, last;
    uint64_t most_committed = 0;
};

State& state() {
    static State* s = new State;  // used by exit and crash hooks: never destroyed
    return *s;
}

double session_seconds() {
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
}

std::string hex(uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof(text), "%08llx", static_cast<unsigned long long>(value));
    return text;
}

const char* kind_name(Region::Kind kind) {
    switch (kind) {
        case Region::Kind::Private: return "private";
        case Region::Kind::Mapped: return "mapped";
        case Region::Kind::Image: return "image";
    }
    return "?";
}

std::string label_for(const std::vector<Label>& labels, const Region& region) {
    for (const Label& label : labels)
        if (region.base >= label.base && region.base - label.base < label.size) return label.name;
    if (!region.name.empty()) return region.name;
    return std::string(kind_name(region.kind)) + " @" + hex(region.allocation_base);
}

Picture take_picture(const std::vector<Label>& labels) {
    Picture picture;
    picture.seconds = session_seconds();
    std::map<std::string, Group> groups;
    platform::vmem::walk([&](const Region& region) {
        if (region.state == Region::State::Free) {
            picture.free += region.size;
            picture.largest_free = std::max(picture.largest_free, region.size);
            return;
        }
        const size_t kind = static_cast<size_t>(region.kind);
        Group& group = groups[label_for(labels, region)];
        if (group.name.empty()) {
            group.name = label_for(labels, region);
            group.base = region.allocation_base;
        }
        if (region.state == Region::State::Committed) {
            picture.committed += region.size;
            picture.committed_by_kind[kind] += region.size;
            group.committed += region.size;
        } else {
            picture.reserved += region.size;
            picture.reserved_by_kind[kind] += region.size;
            group.reserved += region.size;
        }
    });
    for (auto& [name, group] : groups) picture.groups.push_back(std::move(group));
    std::sort(picture.groups.begin(), picture.groups.end(), [](const Group& a, const Group& b) {
        return a.committed + a.reserved > b.committed + b.reserved;
    });
    picture.valid = true;
    return picture;
}

std::string mb(uint64_t bytes) {
    char text[32];
    std::snprintf(text, sizeof(text), "%8.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return text;
}

std::string describe(const char* title, const Picture& picture, size_t rows) {
    if (!picture.valid) return std::string(title) + ": none\n";
    std::string out;
    char line[256];
    std::snprintf(line, sizeof(line), "%s (at %.0f s): free %s, largest free block %s, committed %s, reserved %s\n",
                  title, picture.seconds, mb(picture.free).c_str(), mb(picture.largest_free).c_str(),
                  mb(picture.committed).c_str(), mb(picture.reserved).c_str());
    out += line;
    for (size_t kind = 0; kind < 3; ++kind) {
        std::snprintf(line, sizeof(line), "  %-8s committed %s, reserved %s\n",
                      kind_name(static_cast<Region::Kind>(kind)), mb(picture.committed_by_kind[kind]).c_str(),
                      mb(picture.reserved_by_kind[kind]).c_str());
        out += line;
    }
    if (rows) {
        std::snprintf(line, sizeof(line), "  %-44s %11s %11s   first at\n", "largest users", "committed", "reserved");
        out += line;
        for (size_t i = 0; i < std::min(rows, picture.groups.size()); ++i) {
            const Group& group = picture.groups[i];
            std::snprintf(line, sizeof(line), "  %-44.44s %s %s   %s\n", group.name.c_str(),
                          mb(group.committed).c_str(), mb(group.reserved).c_str(), hex(group.base).c_str());
            out += line;
        }
    }
    return out;
}

void sample() {
    State& s = state();
    std::vector<Label> labels;
    {
        std::lock_guard lock(s.mutex);
        labels = s.labels;
    }
    const xbox::MemoryUsage usage = xbox::memory_usage();
    if (usage.total_pages) {  // memory_init has run
        labels.push_back({usage.layout.contiguous_base, usage.layout.physical_bytes, "xbox contiguous memory"});
        labels.push_back({usage.layout.virtual_base, usage.layout.virtual_end - usage.layout.virtual_base,
                          "xbox virtual memory range"});
    }
    // From the game's first frame on: before it the Windows stub still holds its placeholder reservations
    // (win32_stub.c keeps the Xbox ranges free until memory_init takes them).
    if (!game::MemStats_Frames()) return;
    Picture picture = take_picture(labels);
    for (const std::string& line : game::MemStats_TakeFinishedMaps()) LOG_INFO("memory", "%s", line.c_str());
    std::lock_guard lock(s.mutex);
    if (!s.first.valid) s.first = picture;
    if (!s.least_free.valid || picture.free < s.least_free.free) s.least_free = picture;
    s.most_committed = std::max(s.most_committed, picture.committed);
    s.last = std::move(picture);
}

}  // namespace

void label_address_range(uintptr_t base, uint64_t size, std::string name) {
    State& s = state();
    std::lock_guard lock(s.mutex);
    s.labels.push_back({base, size, std::move(name)});
}

void start_memory_stats(const fs::path& file) {
    State& s = state();
    {
        std::lock_guard lock(s.mutex);
        if (s.started) return;
        s.started = true;
        s.file = file;
    }
    session_seconds();
    game::MemStats_Enable();
    LOG_INFO("memory", "memory statistics: %s on exit", file.string().c_str());
    std::thread([] {
        for (;;) {
            sample();
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }).detach();
}

void write_memory_stats() {
    State& s = state();
    std::string text;
    {
        std::lock_guard lock(s.mutex);
        if (!s.started) return;
    }
    text += game::MemStats_Report();
    for (const std::string& line : game::MemStats_TakeFinishedMaps()) text += "log: " + line + "\n";
    const xbox::MemoryUsage usage = xbox::memory_usage();
    char line[256];
    std::snprintf(line, sizeof(line),
                  "\nXbox memory: %u MB budget; pages used now: image %u, contiguous %u, virtual committed %u "
                  "(reserved %u) of %u\n",
                  usage.layout.physical_bytes >> 20, usage.image_pages, usage.contiguous_pages,
                  usage.virtual_committed_pages, usage.virtual_reserved_pages, usage.total_pages);
    text += line;
    std::snprintf(line, sizeof(line), "  ranges: contiguous %08x-%08x, virtual %08x-%08x\n",
                  usage.layout.contiguous_base, usage.layout.contiguous_base + usage.layout.physical_bytes,
                  usage.layout.virtual_base, usage.layout.virtual_end);
    text += line;
    {
        std::lock_guard lock(s.mutex);
        text += "\nAddress space below 4 GB (sampled every second)\n";
        std::snprintf(line, sizeof(line), "most committed at once: %s\n", mb(s.most_committed).c_str());
        text += line;
        text += describe("at the first frame", s.first, 0);
        text += describe("least free", s.least_free, 40);
        text += describe("last picture", s.last, 15);
    }
    if (std::FILE* file = std::fopen(s.file.string().c_str(), "w")) {
        std::fputs(text.c_str(), file);
        std::fclose(file);
    }
}

}  // namespace loader
