#pragma once

// --mem-stats / --diag: how full the game's memory pools get. MemStats_Sample runs on the game thread once per
// frame (Com_Frame): it reads each pool's own counters (heaps, small heaps, stream heaps and banks, sound memory,
// corpses) and keeps the peaks per map; no I/O, no allocation after the first frames of a map. The report is for
// any thread.

#include <cstdint>
#include <string>
#include <vector>

namespace game {

void MemStats_Enable();
bool MemStats_Enabled();
void MemStats_Sample();
// Frames sampled so far (0 until the game's first frame).
uint32_t MemStats_Frames();

// Peak % of every pool per map (one column each) and the session's peak in bytes.
std::string MemStats_Report();
// One line per map finished since the last call (its pools at 90 % or more), for the host's log.
std::vector<std::string> MemStats_TakeFinishedMaps();

}  // namespace game
