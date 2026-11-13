#pragma once

// How host code (the D3D vertical blank and swap callbacks, the sound engine's packet callbacks) calls title code
// and prepares its own threads for that. Set by whoever runs the title (cod_bro_loader, our own engine cod_bro),
// independent of the kernel exports.

#include <cstdint>

namespace xbox {

struct TitleCalls {
    // Calls title `function` with `count` stdcall/cdecl arguments on the calling thread's title stack.
    void (*call_guest)(uint32_t function, const uint32_t* args, uint32_t count) = nullptr;
    // Gives the calling host thread what title code needs (FS, a thread record) before its first call_guest.
    void (*attach_thread)() = nullptr;
};

void set_title_calls(const TitleCalls& calls);
const TitleCalls& title_calls();

}  // namespace xbox
