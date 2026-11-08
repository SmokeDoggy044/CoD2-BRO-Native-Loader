#pragma once

#include <cstdint>

namespace loader {

class SymbolTable;
class XbeImage;

// Developer and player tool. A spec START,SECONDS samples every thread's instruction
// pointer about 1000 times a second (only threads that are running), from START seconds after the session start for
// SECONDS, then logs ("profile") per thread where its time went: title code by
// Ghidra function, host code by library and symbol, kernel waits as such.
// auto[,SECONDS] (default 10) reports at each second under 80% of the frame rate aimed
// at (`frame_rate`, the configured one, until a movie changes it) once 300 frames
// were shown, wherever it comes (intro videos shift level timings), at most once a
// minute and 20 times a run; each report names the campaign mission.
// The spec comes from --profile or XB_PROFILE; nothing happens without one.
// XB_PROFILE_OUT=FILE also writes every sampled address ("tid eip count place").
void start_profiler(const char* spec, uint32_t frame_rate, const XbeImage& image, const SymbolTable& symbols);

}  // namespace loader
