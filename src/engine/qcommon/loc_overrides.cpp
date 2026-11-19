#include "loc_overrides.h"

#include <map>
#include <memory>
#include <mutex>

#include "title_data.h"

namespace game {

namespace {

// A string table (StringTable_Load): +8 a sorted map {count, pairs {key hash, index}}, +0x10 the entry count, +0x14
// the entry pointers; an entry's text is at +8.
constexpr uint32_t kTables = 0x0095f5d8;  // rm_locStrings: 99 table slots from +4
constexpr int32_t kTableSlots = 0x63;

uint32_t word(const uint8_t* p, uint32_t offset) { return *reinterpret_cast<const uint32_t*>(p + offset); }
uint8_t* pointer(uint32_t address) { return reinterpret_cast<uint8_t*>(uintptr_t{address}); }
uint32_t address(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// Calls `visit(hash, entry)` for every entry of `table`.
template <typename Visit>
void for_each_entry(uint8_t* table, Visit visit) {
    const uint32_t pairs = word(table, 8), count = word(table, 0x10);
    const uint32_t* map = reinterpret_cast<const uint32_t*>(uintptr_t{word(table, 0xc)});
    const uint32_t* list = reinterpret_cast<const uint32_t*>(uintptr_t{word(table, 0x14)});
    if (!map || !list) return;
    for (uint32_t i = 0; i < pairs; ++i) {
        const uint32_t hash = map[i * 2], index = map[i * 2 + 1];
        if ((!hash && !index) || index >= count || !list[index]) continue;
        visit(hash, pointer(list[index]));
    }
}

template <typename Visit>
void for_each_loaded_table(Visit visit) {
    uint8_t* const tables = title_global<uint8_t*>(kTables);
    if (!tables) return;
    for (int32_t i = 0; i < kTableSlots; ++i)
        if (const uint32_t table = word(tables, 4 + static_cast<uint32_t>(i) * 4)) visit(pointer(table));
}

std::mutex g_mutex;
// The texts in force (owned strings: their addresses are what the entries point at) and the entries we pointed
// at them, with their original text.
std::shared_ptr<std::map<uint32_t, std::string>> g_texts = std::make_shared<std::map<uint32_t, std::string>>();
struct Patched {
    uint32_t hash;
    uint32_t original;
};
std::map<uint8_t*, Patched> g_patched;  // by entry
LocTextSeen g_seen = nullptr;

// `entry` (key `hash`) as `texts` says: ours, or its original if we had replaced it and no longer do.
void apply(uint8_t* entry, uint32_t hash, const std::map<uint32_t, std::string>& texts, const void* old_text) {
    uint32_t& text = *reinterpret_cast<uint32_t*>(entry + 8);
    const auto patched = g_patched.find(entry);
    // Ours only while it still points at a text of ours (a table loaded again at the same address is new).
    const bool ours = patched != g_patched.end() && patched->second.hash == hash && old_text && text == address(old_text);
    if (patched != g_patched.end() && !ours) g_patched.erase(patched);
    const auto found = texts.find(hash);
    if (found != texts.end()) {
        if (!ours) g_patched[entry] = {hash, text};
        text = address(found->second.c_str());
    } else if (ours) {
        text = patched->second.original;
        g_patched.erase(entry);
    }
}

}  // namespace

void loc_set_overrides(std::vector<std::pair<uint32_t, std::string>> overrides) {
    auto texts = std::make_shared<std::map<uint32_t, std::string>>();
    for (auto& [hash, text] : overrides) (*texts)[hash] = std::move(text);
    std::lock_guard lock(g_mutex);
    const std::shared_ptr<std::map<uint32_t, std::string>> old = g_texts;
    for_each_loaded_table([&](uint8_t* table) {
        for_each_entry(table, [&](uint32_t hash, uint8_t* entry) {
            const auto was = old->find(hash);
            if (texts->count(hash) || was != old->end())
                apply(entry, hash, *texts, was != old->end() ? was->second.c_str() : nullptr);
        });
    });
    g_texts = std::move(texts);  // the old strings go only after no entry points at them
}

void loc_table_loaded(uint8_t* table) {
    std::lock_guard lock(g_mutex);
    for_each_entry(table, [&](uint32_t hash, uint8_t* entry) {
        if (g_seen) {
            const uint32_t text = word(entry, 8);
            g_seen(hash, text ? reinterpret_cast<const char*>(uintptr_t{text}) : "");
        }
        if (g_texts->count(hash)) apply(entry, hash, *g_texts, nullptr);
    });
}

void loc_set_text_seen(LocTextSeen seen) { g_seen = seen; }

}  // namespace game
