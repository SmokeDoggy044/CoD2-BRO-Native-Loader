#pragma once

// --mem-stats FILE, or --diag DIR (DIR/memory.txt): the game's pool counters (game::MemStats, sampled on the game
// thread) and a picture of the process's 32-bit address space, taken every second on a thread of its own; the
// report keeps the moment with the least free address space (what took it, by reservation). Each finished map's
// fullest pools go to the log as they happen.

#include <cstdint>
#include <filesystem>
#include <string>

namespace loader {

void start_memory_stats(const std::filesystem::path& file);
// Writes the report (again, if called again: the latest state). No-op when not started.
void write_memory_stats();
// A name for an address range in the report (e.g. the title image).
void label_address_range(uintptr_t base, uint64_t size, std::string name);

}  // namespace loader
