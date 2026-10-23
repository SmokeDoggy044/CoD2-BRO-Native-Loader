#pragma once

namespace loader {

class SymbolTable;
class XbeImage;

// Developer and player tool. A spec START,SECONDS samples every thread's instruction
// pointer about 1000 times a second (only threads that are running), from START seconds after the session start for
// SECONDS, then logs ("profile") per thread where its time went: title code by
// Ghidra function, host code by library and symbol, kernel waits as such.
// auto[,SECONDS] (default 10) starts at the first second under 24 fps once 300
// frames were shown, wherever it comes (intro videos shift level timings).
// The spec comes from --profile or XB_PROFILE; nothing happens without one.
// XB_PROFILE_OUT=FILE also writes every sampled address ("tid eip count place").
void start_profiler(const char* spec, const XbeImage& image, const SymbolTable& symbols);

}  // namespace loader
