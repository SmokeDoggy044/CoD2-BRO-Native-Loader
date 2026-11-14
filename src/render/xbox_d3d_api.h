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
    void (*log_audio)() = nullptr;               // at a mark: what is playing
    void (*frame_presented)() = nullptr;         // each presented frame, before the screenshot (record/replay)
    std::string (*frame_screenshot)() = nullptr; // after frame_presented: a path = save this frame there
    // When the window or device cannot be created (logged): the loader tells the
    // player and ends the process. Null: CreateDevice returns an error to the title.
    void (*device_failed)() = nullptr;
    // Replays: the vertical blank runs this many times faster, so the title paces its frames faster (its clock
    // reads come from the recording). 1 = real time.
    double vblank_speed = 1;
};

// Call before the title creates its device.
void configure(const D3DConfig& config);

// An in-process title restart (own engine host 5b), before the title's memory is reset: the vertical blank stops
// calling the title, everything made from title memory is forgotten, and the device is closed so the next title
// opens it again - on the same window and back end, with frame buffers from the (reset) title memory again.
void restart_title();

// The back end's kept data (Vulkan pipeline cache) written out; before the process ends (window closed, Ctrl+C).
void save_caches();

// Frames shown in the last whole second (0 before the first), and frames shown so
// far. Any thread (the profiler's automatic start reads them).
uint32_t frames_last_second();
uint32_t frames_shown();

// The title now aims at `fps` frames per second (on purpose, e.g. a movie at 30 in a
// 60 fps game): --diag counts neither the change as hitches nor the seconds at the
// lower rate as slow. Any thread.
void frame_rate_changed(uint32_t fps);
// The last frame_rate_changed rate, 0 before the first. Any thread.
uint32_t frame_rate_target();

// Vertical blanks whose title callback has run so far, and a wait until that number has
// moved past `seen` (at most `timeout_ms`). The title's frame end waits for its
// callback's flag with these instead of spinning on it. Any thread.
uint32_t vertical_blanks_done();
void wait_vertical_blank_after(uint32_t seen, uint32_t timeout_ms);

// Native frame pacing (own engine 0.4; the title's own vertical-blank wait is off then): the title's frame end
// calls pace_frame() right before it presents, which waits until the next frame may start. set_frame_limit(fps):
// at most fps frames a second (a cap, or 30 while a movie plays); 0 = the display's rate - left to the display
// when present waits for it (BackendConfig::wait_for_display), else kept by our own clock. Replays scale the rate
// by vblank_speed. Title thread.
void set_frame_limit(double fps);
void pace_frame();
// The display's refresh rate in Hz (0 before the device exists). Any thread.
double display_refresh_rate();

// Shows `lines` to the player over the game for `seconds` (first line as a heading),
// replacing any notice shown before. Any thread.
void show_notice(std::vector<std::string> lines, double seconds);

// Maps a point in the window (pixels) to the title's 4:3 screen coordinates (its
// 640x480 menus, wherever they are shown). False without a window or frame yet.
// Any thread.
bool window_to_screen(float x, float y, float& screen_x, float& screen_y);

// The replacements, by SDK function name (as XbSymbolDatabase/Ghidra name them,
// without the library prefix). The loader redirects each function it finds.
std::span<const xbox::KernelImpl> d3d_functions();

// Where the back buffer's address is kept (D3DDevice_GetBackBuffer2's result): the
// comparison harness's stand-in reads it in test processes.
const uint32_t* back_buffer_slot();

}  // namespace render
