#pragma once

// Our own start (own engine host, step 0.3 item 5): replaces the title's XBE entry (0x001e7bc3), XAPI's
// mainXapiStartup, XapiInitProcess, _rtinit and _cinit. Nothing of the Xbox start-up is ported (kernel patches,
// partitions, drive letters, title updates, parental check, auto power-down, LaunchDataPage handling, Xbox Live
// init, the unhandled-exception filter); what the game reads afterwards - the heap handle and the CRT's state - is
// set here as plain C, then the C++ static initializers and Sys_Main run.

#include <cstdint>

#include "compiler.h"

namespace game {

// The entry: starts the title's main thread (title_main_thread) the way the original entry did and returns.
void XB_CDECL title_start();

// The title's main thread: heap, CRT state, static initializers, Sys_Main.
uint32_t XB_STDCALL title_main_thread(uint32_t parameter);

}  // namespace game
