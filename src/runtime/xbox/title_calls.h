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

// Signals a title event handle from host code (sound packet completions, input): the loader sets the kernel's
// set_event_handle (which forwards our native handles), cod_bro our own objects (game::objects). Unset: nothing.
using EventSignal = void (*)(uint32_t handle);
void set_event_signal(EventSignal signal);
void signal_title_event(uint32_t handle);

// Raise / lower the kernel's IRQL around host-made title callbacks (the vertical-blank callback runs at
// DISPATCH_LEVEL): set by the loader; cod_bro has no IRQL (raise returns 0, lower does nothing).
struct IrqlCalls {
    uint8_t (*raise)(uint8_t new_irql) = nullptr;
    void (*lower)(uint8_t old_irql) = nullptr;
};
void set_irql_calls(const IrqlCalls& calls);
uint8_t title_raise_irql(uint8_t new_irql);
void title_lower_irql(uint8_t old_irql);

}  // namespace xbox
