#pragma once

// Xbox Direct3D 8 (D3D8 library, XDK 5849) replacement: the functions titles call, with
// the same signatures, on top of a Backend (D3D9 for now).
//
// The device and the resources D3D creates (back buffer, depth buffer) are ours: structs
// in title-addressable memory with the Xbox layout, because titles read their fields
// directly (inlined header code). Resources the title creates itself (XGSet*Header +
// Register) stay in title memory and are read from there.

#include <span>
#include <string>
#include <vector>

#include "backend.h"
#include "kernel.h"
#include "types.h"

namespace render {

struct D3DConfig {
    BackendConfig backend;
    // The title's D3D__pDevice global (the device pointer that inlined code reads).
    xbox::XbPtr device_pointer = 0;
    // D3D__RenderState (uint32_t[kRsCount], XDK 5849 order), which inlined code writes.
    xbox::XbPtr render_state = 0;
    // D3D_g_DeferredTextureState (uint32_t[4][32]), which inlined code writes.
    xbox::XbPtr texture_state = 0;
    // Debugging: save every Nth frame as <screenshot_dir>/frame_<n>.bmp (0 = never).
    uint32_t screenshot_interval = 0;
    std::string screenshot_dir = ".";
    // Diagnostics while playing: a screenshot every diag_seconds into diag_dir
    // (auto_<seconds>.bmp), and on the mark key (F12) mark_<n>.bmp, a note typed by the
    // user while the game is frozen (log and marks.txt) and a draw dump of the next
    // frame in the log. A corner overlay counts the marks. Empty = off.
    std::string diag_dir;
    uint32_t diag_seconds = 10;
    void (*pause_audio)(bool paused) = nullptr;  // while a mark's note is typed
    // When the window or device cannot be created (logged): the loader tells the
    // player and ends the process. Null: CreateDevice returns an error to the title.
    void (*device_failed)() = nullptr;
};

// Call before the title creates its device.
void configure(const D3DConfig& config);

// Shows `lines` to the player over the game for `seconds` (first line as a heading),
// replacing any notice shown before. Any thread.
void show_notice(std::vector<std::string> lines, double seconds);

// The replacements, by SDK function name (as XbSymbolDatabase/Ghidra name them,
// without the library prefix). The loader redirects each function it finds.
std::span<const xbox::KernelImpl> d3d_functions();

}  // namespace render
