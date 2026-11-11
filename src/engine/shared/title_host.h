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

    // The current UTC time, 100 ns units since 1601 (FILETIME).
    int64_t (*system_time)() = nullptr;

    // Title memory (addresses in the title's 32-bit space). Contiguous blocks for XPhysicalAlloc (`protect` in
    // the title's PAGE_* terms; 0 when none fits), their protection, the page budget (total and available
    // pages, bytes reserved), and plain read/write segments for our heap (0 on failure).
    uint32_t (*allocate_contiguous)(uint32_t bytes, uint32_t lowest, uint32_t highest, uint32_t alignment,
                                    uint32_t protect) = nullptr;
    void (*free_contiguous)(uint32_t address) = nullptr;
    void (*protect)(uint32_t address, uint32_t bytes, uint32_t protect) = nullptr;
    uint32_t (*protection)(uint32_t address) = nullptr;
    void (*memory_statistics)(uint32_t* total_pages, uint32_t* available_pages, uint32_t* reserved_bytes) = nullptr;
    uint32_t (*reserve)(uint32_t bytes) = nullptr;

    // An in-process restart (own engine host 5b): the title ends here and starts again in this process with `page`
    // (0x1000 bytes) as its LaunchDataPage, on the same window and devices. Called on the title's main thread;
    // does not return.
    void (*relaunch)(const uint8_t* page) = nullptr;
    // The LaunchDataPage the previous title left (its address in contiguous memory, 0 = none); XGetLaunchInfo takes
    // it and frees it with free_contiguous.
    uint32_t* launch_data_page = nullptr;

    // What the player chose (launcher/command line): the title's language number (XC_LANGUAGE: 1 English, ...)
    // and a 16:9 picture.
    uint32_t language = 1;
    bool widescreen = false;
};

void set_title_host(const TitleHost& host);
const TitleHost& title_host();

// The native platform layer is on (`--own-main`): our XAPI C makes its own objects, files, threads instead of
// calling the kernel exports. Off, it keeps the kernel path that the compare harness checks against the originals.
void set_native_platform(bool on);
bool native_platform();

}  // namespace game
