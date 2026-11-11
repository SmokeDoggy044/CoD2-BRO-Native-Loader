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

// An in-process restart (own engine host 5b), after the title's threads ended and before its memory is reset: what
// our C keeps outside title memory goes back to its state before the first start (handles, heap, script stacks,
// sound and renderer additions). The host then resets title memory and runs the entry again.
void title_restart_reset();

}  // namespace game
