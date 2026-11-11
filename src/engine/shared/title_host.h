#pragma once

// What our C needs from whoever runs it (own engine host step 5c): the loader today, our own executable later.
// Everything here is native host work - a thread with the title's FS block, the clock, record/replay - so the C
// above it (title_xapi.cpp, title_objects.cpp, ...) calls no kernel export. Set once before the title starts.

#include <cstdint>

#include "compiler.h"

namespace game {

struct TitleHost {
    // Starts a host thread that runs `routine(context)` (a title-callable stdcall function) on its own stack of
    // `stack_size` bytes with the title's FS block and a TLS block of `tls_size` bytes (at KTHREAD +0x28, as the
    // title's code finds it); `thread_id` gets its id (the one the FS block's ETHREAD reports). False on failure.
    bool (*start_thread)(uint32_t stack_size, uint32_t tls_size, uint32_t routine, uint32_t context,
                         uint32_t* thread_id) = nullptr;

    // Record/replay (src/runtime/xbox/replay.h): the calling thread's reads go to the stream; a replay is running;
    // the next recorded value of `tag` in this frame (replay only); record or replay one value.
    bool (*replay_on_stream)() = nullptr;
    bool (*replay_replaying)() = nullptr;
    bool (*replay_recorded)(char tag, uint32_t* value) = nullptr;
    uint32_t (*replay_value)(char tag, uint32_t real) = nullptr;
};

void set_title_host(const TitleHost& host);
const TitleHost& title_host();

// The native platform layer is on (`--own-main`): our XAPI C makes its own objects, files, threads instead of
// calling the kernel exports. Off, it keeps the kernel path that the compare harness checks against the originals.
void set_native_platform(bool on);
bool native_platform();

}  // namespace game
