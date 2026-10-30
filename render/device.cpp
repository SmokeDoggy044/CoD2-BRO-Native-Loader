// Xbox Direct3D 8 replacement: device, frame and synchronization functions. See xbox_d3d_api.h.
//
// Semantics follow the XDK and what CoD2: BRO relies on:
// - Everything the title submits is executed (or dropped) synchronously, so fences are
//   always passed and InsertCallback runs its callback at once.
// - The vertical blank callback runs on our own 60 Hz thread at DISPATCH_LEVEL, like
//   the Xbox's DPC. Titles pace their frames with it (CoD2: BRO waits on a flag that
//   its callback clears; the loader turns that spin into wait_vertical_blank_after),
//   so it must keep running.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "compiler.h"
#include "d3d_internal.h"
#include "dispatcher.h"
#include "draw.h"
#include "float_env.h"
#include "frame_pacing.h"
#include "kernel_memory.h"
#include "log.h"
#include "nv2a.h"
#include "xbox_d3d.h"
#include "xbox_d3d_api.h"

namespace render {

namespace {

using xbox::from_xbox_ptr;
using xbox::to_xbox_ptr;

constexpr double kVBlankHz = 60;  // NTSC

struct State {
    D3DConfig config;
    std::unique_ptr<Backend> backend;
    uint8_t* device = nullptr;  // our CDevice block (title-addressable)
    XbSurface* back_buffer = nullptr;
    XbSurface* depth_stencil = nullptr;
    uint32_t fence = 0;
    uint32_t swap_count = 0;
    std::atomic<bool> back_buffer_written{false};  // the CPU locked it since the last Swap
    XbVBlankData* vblank_data = nullptr;
    std::atomic<uint32_t> vblank_count{0};
    std::atomic<uint32_t> vblank_callback{0};
    std::mutex vblank_done_mutex;
    std::condition_variable vblank_done_changed;
    uint32_t vblank_done = 0;  // vertical blanks whose callback has run (vertical_blanks_done)
    XbDisplayMode display_mode{};  // from CreateDevice's parameters
    std::mutex notice_mutex;  // show_notice may come from any title thread
    std::vector<std::string> notice;
    std::chrono::steady_clock::time_point notice_until;
};
State& g = *new State;  // the vertical blank thread uses it until exit: never destroyed

template <typename T>
T& device_field(uint32_t offset) {
    return *reinterpret_cast<T*>(g.device + offset);
}

// A D3D-created surface (render target or depth buffer). Like D3D's, it has memory of
// its own: titles lock the back buffer to write into it (video playback does).
XbSurface* create_surface(uint32_t format, uint32_t width, uint32_t height, uint32_t bytes_per_pixel) {
    auto* surface = new XbSurface{};
    surface->container.resource.common = kCommonTypeSurface | kCommonD3DCreated | 1;
    const uint32_t bytes = ((width * bytes_per_pixel + 63) & ~63u) * height;
    const XbPtr memory = xbox::allocate_contiguous(bytes, 0x1000);
    if (!memory) LOG_ERROR("render", "no memory for a %ux%u frame buffer", width, height);
    surface->container.resource.data = memory & 0x0FFFFFFF;
    surface->container.format = kFormatDmaChannelA | (2u << kFormatDimensionShift) |
                                (format << kFormatFormatShift) | (1u << kFormatMipmapShift);
    const uint32_t pitch = (width * bytes_per_pixel + 63) & ~63u;
    surface->container.size =
        (width - 1) | ((height - 1) << kSizeHeightShift) | ((pitch / 64 - 1) << kSizePitchShift);
    return surface;
}

// What the back end needs to know of a surface the title draws into.
SurfaceDesc surface_desc(const XbSurface* surface) {
    SurfaceDesc desc;
    if (!surface) return desc;
    const XbPixelContainer& c = surface->container;
    desc.data = c.resource.data;
    desc.format = (c.format & kFormatFormatMask) >> kFormatFormatShift;
    if (c.size) {
        desc.width = (c.size & kSizeWidthMask) + 1;
        desc.height = ((c.size & kSizeHeightMask) >> kSizeHeightShift) + 1;
    } else {  // swizzled: log2 sizes
        desc.width = 1u << ((c.format >> kFormatUSizeShift) & 0xF);
        desc.height = 1u << ((c.format >> kFormatVSizeShift) & 0xF);
    }
    return desc;
}

// Points the back end at the device's current render target and depth buffer.
void apply_render_target() {
    const auto* target = from_xbox_ptr<XbSurface>(device_field<XbPtr>(kDeviceRenderTarget));
    const auto* depth = from_xbox_ptr<XbSurface>(device_field<XbPtr>(kDeviceDepthStencil));
    const SurfaceDesc color = surface_desc(target);
    const SurfaceDesc depth_desc = surface_desc(depth);
    platform::HostFloatEnv float_env;
    g.backend->set_render_target(color, depth ? &depth_desc : nullptr);
}

XbSurface* add_ref(XbSurface* surface) {
    if (surface) ++surface->container.resource.common;  // the count is the low 16 bits
    return surface;
}

void call_title(uint32_t function, std::initializer_list<uint32_t> args) {
    if (auto call_guest = xbox::kernel_config().call_guest)
        call_guest(function, std::data(args), static_cast<uint32_t>(args.size()));
    else
        LOG_ERROR("render", "cannot call title function %08x", function);
}

// The title's vertical blank: it paces its frames on it (30 fps = every second one).
// It runs at the display's refresh rate when that is close to NTSC's, so frames reach
// the screen at an even rate; other rates would change the title's speed, so those
// keep 60 Hz. Presenting does not wait for the display (backend.h): with a blocking
// present, its wait came on top of the title's, and in heavy scenes frames missed
// their vertical blank (50 ms frames in clusters, as the two clocks' phases drifted).
void vblank_thread(double hz) {
    if (auto attach = xbox::kernel_config().attach_kernel_thread) attach();
    using Clock = std::chrono::steady_clock;
    const auto period = std::chrono::nanoseconds(static_cast<int64_t>(1e9 / hz));
    auto next = Clock::now();
    for (;;) {
        next += period;
        std::this_thread::sleep_until(next);
        const uint32_t count = ++g.vblank_count;
        if (const uint32_t callback = g.vblank_callback.load()) {
            g.vblank_data->vblank = count;
            g.vblank_data->swap = g.swap_count;
            g.vblank_data->flags = 0;
            const uint8_t old_irql = xbox::raise_irql_to(xbox::kDispatchLevel);
            call_title(callback, {to_xbox_ptr(g.vblank_data)});  // cdecl; call_guest restores ESP
            xbox::lower_irql_to(old_irql);
        }
        {
            std::lock_guard lock(g.vblank_done_mutex);
            ++g.vblank_done;
        }
        g.vblank_done_changed.notify_all();
    }
}

// --- Device ---------------------------------------------------------------------------

uint32_t XB_STDCALL Direct3D_CreateDevice(uint32_t /*adapter*/, uint32_t /*device_type*/, XbPtr /*window*/,
                                          uint32_t /*behavior_flags*/, XbPtr parameters, XbPtr out_device) {
    const auto* pp = from_xbox_ptr<XbPresentParameters>(parameters);
    LOG_INFO("render", "CreateDevice %ux%u format %02x x%u, multisample %x, depth %u/%02x, flags %x, interval %x",
             pp->back_buffer_width, pp->back_buffer_height, pp->back_buffer_format, pp->back_buffer_count,
             pp->multisample_type, pp->enable_auto_depth_stencil, pp->auto_depth_stencil_format, pp->flags,
             pp->presentation_interval);
    if (g.device) {
        LOG_ERROR("render", "CreateDevice called twice");
        return kD3DErrInvalidCall;
    }

    BackendConfig backend_config = g.config.backend;
    backend_config.width = pp->back_buffer_width ? pp->back_buffer_width : 640;
    backend_config.height = pp->back_buffer_height ? pp->back_buffer_height : 480;
    g.back_buffer = create_surface(kFmtLinX8R8G8B8, backend_config.width, backend_config.height, 4);
    if (pp->enable_auto_depth_stencil)
        g.depth_stencil = create_surface(pp->auto_depth_stencil_format, backend_config.width, backend_config.height,
                                         pp->auto_depth_stencil_format == kFmtD16 ? 2 : 4);
    backend_config.back_buffer = surface_desc(g.back_buffer);
    backend_config.depth_stencil = surface_desc(g.depth_stencil);
    platform::HostFloatEnv float_env;
    g.backend = create_d3d9_backend(backend_config);
    if (!g.backend) {
        if (g.config.device_failed) g.config.device_failed();
        return kD3DErrInvalidCall;
    }

    g.device = new uint8_t[kDeviceSize]{};
    // Always progressive here: no interlaced or field modes.
    g.display_mode = {backend_config.width, backend_config.height, 60,
                      (pp->flags & (kPresentFlagWidescreen | kPresentFlag10x11PixelAspect)) | kPresentFlagProgressive,
                      pp->back_buffer_format};
    device_field<XbPtr>(kDeviceBackBuffer) = to_xbox_ptr(g.back_buffer);
    device_field<XbPtr>(kDeviceRenderTarget) = to_xbox_ptr(g.back_buffer);
    device_field<XbPtr>(kDeviceDepthStencil) = to_xbox_ptr(g.depth_stencil);
    g.vblank_data = new XbVBlankData{};
    reset_draw_state(g.depth_stencil != nullptr);

    if (g.config.device_pointer) *from_xbox_ptr<XbPtr>(g.config.device_pointer) = to_xbox_ptr(g.device);
    *from_xbox_ptr<XbPtr>(out_device) = to_xbox_ptr(g.device);
    const double display_hz = g.backend->refresh_rate();
    const double vblank_hz = display_hz >= 59 && display_hz <= 61 ? display_hz : kVBlankHz;
    LOG_INFO("render", "vertical blank at %.3f Hz (display %.3f Hz)", vblank_hz, display_hz);
    std::thread(vblank_thread, vblank_hz).detach();
    return kD3DOk;
}

// Keeps the current frame on screen across a title launch; nothing to keep here.
uint32_t XB_STDCALL D3DDevice_PersistDisplay() { return kD3DOk; }

// Re-creates the frame buffers with new parameters. Same size: nothing to do.
uint32_t XB_STDCALL D3DDevice_Reset(XbPtr parameters) {
    const auto* pp = from_xbox_ptr<XbPresentParameters>(parameters);
    const auto* back_buffer = g.back_buffer;
    const uint32_t width = (back_buffer->container.size & kSizeWidthMask) + 1;
    const uint32_t height = ((back_buffer->container.size & kSizeHeightMask) >> kSizeHeightShift) + 1;
    if (pp->back_buffer_width != width || pp->back_buffer_height != height)
        LOG_WARN("render", "Reset to %ux%u: resizing is not supported yet (staying %ux%u)", pp->back_buffer_width,
                 pp->back_buffer_height, width, height);
    device_field<XbPtr>(kDeviceRenderTarget) = to_xbox_ptr(g.back_buffer);
    device_field<XbPtr>(kDeviceDepthStencil) = to_xbox_ptr(g.depth_stencil);
    apply_render_target();
    return kD3DOk;
}

void XB_STDCALL D3D_SetPushBufferSize(uint32_t /*size*/, uint32_t /*kickoff_size*/) {}

void XB_STDCALL D3DDevice_GetDeviceCaps(XbPtr caps) {
    // D3DCAPS8 (0xD4 bytes). CoD2: BRO never reads it.
    std::memset(from_xbox_ptr<void>(caps), 0, 0xD4);
}

// D3DGAMMARAMP on the Xbox: BYTE red[256], green[256], blue[256]. The flags
// (D3DSGR_IMMEDIATE: now instead of at the next vertical blank) make no difference here.
void XB_STDCALL D3DDevice_SetGammaRamp(uint32_t /*flags*/, XbPtr ramp) {
    if (!g.backend || !ramp) return;
    uint8_t table[3][256];
    std::memcpy(table, from_xbox_ptr<const void>(ramp), sizeof(table));
    LOG_DEBUG("render", "SetGammaRamp: red %u %u %u, green %u %u %u, blue %u %u %u (0, 128, 255)", table[0][0],
              table[0][128], table[0][255], table[1][0], table[1][128], table[1][255], table[2][0], table[2][128],
              table[2][255]);
    g.backend->set_gamma_ramp(table);
}

// --- Surfaces -------------------------------------------------------------------------

// index -1 = the front buffer, 0 = the back buffer.
XbPtr XB_STDCALL D3DDevice_GetBackBuffer2(int32_t /*index*/) { return to_xbox_ptr(add_ref(g.back_buffer)); }

XbPtr XB_STDCALL D3DDevice_GetRenderTarget2() {
    return to_xbox_ptr(add_ref(from_xbox_ptr<XbSurface>(device_field<XbPtr>(kDeviceRenderTarget))));
}

XbPtr XB_STDCALL D3DDevice_GetDepthStencilSurface2() {
    return to_xbox_ptr(add_ref(from_xbox_ptr<XbSurface>(device_field<XbPtr>(kDeviceDepthStencil))));
}

uint32_t XB_STDCALL D3DDevice_SetRenderTarget(XbPtr target, XbPtr depth_stencil) {
    // null target = keep the current one (XDK behavior).
    if (target) device_field<XbPtr>(kDeviceRenderTarget) = target;
    device_field<XbPtr>(kDeviceDepthStencil) = depth_stencil;
    apply_render_target();
    return kD3DOk;
}

uint32_t XB_STDCALL D3DDevice_SetRenderTargetFast(XbPtr target, XbPtr depth_stencil, uint32_t /*flags*/) {
    return D3DDevice_SetRenderTarget(target, depth_stencil);
}

// --- Viewport and scissors ------------------------------------------------------------

ViewState g_view;

void XB_STDCALL D3DDevice_SetViewport(XbPtr viewport) {
    if (viewport) {
        g_view.viewport = *from_xbox_ptr<XbViewport>(viewport);
    } else {  // null = the whole render target
        const auto* target = from_xbox_ptr<XbSurface>(device_field<XbPtr>(kDeviceRenderTarget));
        g_view.viewport = {0, 0, (target->container.size & kSizeWidthMask) + 1,
                           ((target->container.size & kSizeHeightMask) >> kSizeHeightShift) + 1, 0.0f, 1.0f};
    }
}

void XB_STDCALL D3DDevice_GetViewport(XbPtr viewport) { *from_xbox_ptr<XbViewport>(viewport) = g_view.viewport; }

// Up to 8 rectangles; exclusive = draw outside them.
void XB_STDCALL D3DDevice_SetScissors(uint32_t count, uint32_t exclusive, XbPtr rects) {
    g_view.scissor_count = count < 8 ? count : 8;
    g_view.scissor_exclusive = exclusive != 0;
    if (rects) std::memcpy(g_view.scissors, from_xbox_ptr<void>(rects), g_view.scissor_count * sizeof(XbRect));
}

void XB_STDCALL D3DDevice_GetScissors(XbPtr count, XbPtr exclusive, XbPtr rects) {
    if (count) *from_xbox_ptr<uint32_t>(count) = g_view.scissor_count;
    if (exclusive) *from_xbox_ptr<uint32_t>(exclusive) = g_view.scissor_exclusive;
    if (rects) std::memcpy(from_xbox_ptr<void>(rects), g_view.scissors, g_view.scissor_count * sizeof(XbRect));
}

// Offsets all screen coordinates (D3D stores x + 0.53125, y + 0.53125 for the hardware).
void XB_STDCALL D3DDevice_SetScreenSpaceOffset(float x, float y) {
    g_view.offset_x = x;
    g_view.offset_y = y;
}

void XB_STDCALL D3DDevice_GetScreenSpaceOffset(XbPtr x, XbPtr y) {
    *from_xbox_ptr<float>(x) = g_view.offset_x;
    *from_xbox_ptr<float>(y) = g_view.offset_y;
}

// --- Frame ----------------------------------------------------------------------------

uint32_t XB_STDCALL D3DDevice_Clear(uint32_t rect_count, XbPtr /*rects*/, uint32_t flags, uint32_t color, float z,
                                    uint32_t stencil) {
    if (rect_count) {
        static std::once_flag once;
        std::call_once(once, [] { LOG_WARN("render", "Clear with rectangles: clearing everything"); });
    }
    uint32_t backend_flags = 0;
    if (flags & kClearTargetMask) backend_flags |= Backend::kClearColor;
    if (flags & kClearZBuffer) backend_flags |= Backend::kClearDepth;
    if (flags & kClearStencil) backend_flags |= Backend::kClearStencil;
    platform::HostFloatEnv float_env;
    g.backend->clear(backend_flags, color, z, stencil);
    return kD3DOk;
}

// The CPU locked the back buffer since it was last uploaded (video playback writes
// frames into it): upload before anything draws over it.
void upload_back_buffer_writes() {
    if (!g.back_buffer_written.exchange(false)) return;
    const auto& pixels = g.back_buffer->container;
    g.backend->write_back_buffer(xbox::from_xbox_ptr<void>(pixels.resource.data | 0x80000000),
                                 ((pixels.size >> kSizePitchShift) + 1) * 64);
}

// --diag: automatic marks. Every warning and error logged once frames are on screen
// (except known harmless ones) is marked by itself at the next Swap: a screenshot
// (automark_<n>.bmp), its line in marks.txt ("A<n>"), and the next frame's draw dump.
// Each message is marked once per diag folder.
constexpr const char* kKnownHarmless[] = {"vertex program EXP:", "vertex program LOG:"};
constexpr uint32_t kMaxAutoMarks = 50;  // per run; later messages are only logged

std::mutex g_auto_mutex;
std::vector<std::string> g_auto_pending;  // "category: text", guarded by g_auto_mutex
std::atomic<bool> g_frames_shown{false};
std::atomic<bool> g_frame_rate_changed{false};
// Session seconds up to which frames_last_second() reports 0 (not measured): the game
// was paused for a --diag note (the automatic profile took that for a slow spot).
std::atomic<int64_t> g_unmeasured_until{-1};

int64_t session_second() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() -
                                                            platform::session_start())
        .count();
}

void diag_observer(platform::LogLevel level, const char* category, const char* text) {
    if (!g_frames_shown.load(std::memory_order_relaxed)) return;
    if (!std::strcmp(category, "diag") || !std::strcmp(category, "dump")) return;
    for (const char* known : kKnownHarmless)
        if (!std::strncmp(text, known, std::strlen(known))) return;
    std::lock_guard<std::mutex> lock(g_auto_mutex);
    if (g_auto_pending.size() < 32)
        g_auto_pending.push_back(std::string(level == platform::LogLevel::Error ? "error " : "") + category + ": " +
                                 text);
}

// --diag: timed screenshots, F12 marks with a note, automatic marks, F11 highlighting
// and the corner overlay (see D3DConfig).
void diagnostics() {
    using Clock = std::chrono::steady_clock;
    const std::string marks_path = g.config.diag_dir + "/marks.txt";
    // Times and mark numbers continue across a title reboot (the loader relaunches):
    // marks.txt has a line per mark, "A<n>" lines for automatic ones.
    static const auto start = platform::session_start();
    static auto next_shot = Clock::now();
    static auto flash_until = next_shot;
    static uint32_t marks = 0, auto_marks = 0, auto_marks_this_run = 0;
    static std::set<std::string> auto_marked;  // messages already in marks.txt
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        if (std::FILE* file = std::fopen(marks_path.c_str(), "r")) {
            char line[1024];
            while (std::fgets(line, sizeof(line), file)) {
                if (line[0] != 'A') {
                    ++marks;
                    continue;
                }
                auto_marks = std::max<uint32_t>(auto_marks, static_cast<uint32_t>(std::atoi(line + 1)));
                // A<n> \t <s>s \t frame <f> \t message
                const char* message = line;
                for (int tabs = 0; tabs < 3 && message; ++tabs)
                    if ((message = std::strchr(message, '\t'))) ++message;
                if (message) auto_marked.insert(std::string(message, std::strcspn(message, "\n")));
            }
            std::fclose(file);
        }
    }
    static std::string flash;
    static bool highlight = false;
    static uint32_t skipped_before = skipped_draws();
    const auto now = Clock::now();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
    bool dump = false;

    // Frame pacing: a "frames" line per second, and each stall (see FramePacing).
    static FramePacing pacing;
    static std::string fps_text;
    if (g_frame_rate_changed.exchange(false)) pacing.rate_changed();
    if (auto second = pacing.frame(std::chrono::duration<double>(now - start).count(), g.swap_count)) {
        LOG_INFO("frames", "%llds: %u fps, worst %.0f ms (frame %u), %u hitches",
                 static_cast<long long>(second->second), second->frames, second->worst_ms, second->worst_frame,
                 second->hitches);
        char text[64];
        std::snprintf(text, sizeof(text), "   fps %u  worst %.0f ms", second->frames, second->worst_ms);
        fps_text = text;
    }
    if (auto stall = pacing.take_stall())
        LOG_INFO("frames", "stall %.1f s before frame %u", stall->seconds, stall->frame);

    if (g.backend->take_mark_request()) {
        ++marks;
        const std::string shot = g.config.diag_dir + "/mark_" + std::to_string(marks) + ".bmp";
        g.backend->save_back_buffer(shot.c_str());
        LOG_INFO("diag", "MARK %u at %llds, frame %u: %s", marks, static_cast<long long>(seconds), g.swap_count,
                 shot.c_str());
        if (g.config.log_audio) g.config.log_audio();
        if (g.config.pause_audio) g.config.pause_audio(true);
        g_unmeasured_until.store(INT64_MAX, std::memory_order_relaxed);
        const auto note = g.backend->prompt({"MARK " + std::to_string(marks) + " saved - game paused",
                                             "What is wrong here? (one or two sentences)"});
        if (g.config.pause_audio) g.config.pause_audio(false);
        g_unmeasured_until.store(session_second() + 1, std::memory_order_relaxed);
        const std::string text = note ? *note : "";
        LOG_INFO("diag", "MARK %u note: %s", marks, note ? text.c_str() : "(none)");
        if (std::FILE* file = std::fopen(marks_path.c_str(), "a")) {
            std::fprintf(file, "%u\t%llds\tframe %u\t%s\n", marks, static_cast<long long>(seconds), g.swap_count,
                         text.c_str());
            std::fclose(file);
        }
        LOG_INFO("diag", "MARK %u: draws of the next frame follow", marks);
        dump = true;
        flash = "MARK " + std::to_string(marks) + (note ? " saved with note" : " saved");
        flash_until = Clock::now() + std::chrono::seconds(3);
    }

    std::vector<std::string> events;
    {
        std::lock_guard<std::mutex> lock(g_auto_mutex);
        events.swap(g_auto_pending);
    }
    std::vector<std::string> fresh;
    for (std::string& event : events)
        if (auto_marked.insert(event).second) fresh.push_back(std::move(event));
    if (!fresh.empty() && auto_marks_this_run < kMaxAutoMarks) {
        ++auto_marks;
        ++auto_marks_this_run;
        const std::string name = "automark_" + std::to_string(auto_marks) + ".bmp";
        g.backend->save_back_buffer((g.config.diag_dir + "/" + name).c_str());
        if (std::FILE* file = std::fopen(marks_path.c_str(), "a")) {
            for (const std::string& event : fresh)
                std::fprintf(file, "A%u\t%llds\tframe %u\t%s\n", auto_marks, static_cast<long long>(seconds),
                             g.swap_count, event.c_str());
            std::fclose(file);
        }
        for (const std::string& event : fresh)
            LOG_INFO("diag", "AUTO MARK %u at %llds, frame %u (%s): %s", auto_marks, static_cast<long long>(seconds),
                     g.swap_count, name.c_str(), event.c_str());
        LOG_INFO("diag", "AUTO MARK %u: draws of the next frame follow", auto_marks);
        dump = true;
        flash = "AUTO MARK " + std::to_string(auto_marks) + ": " + fresh.front().substr(0, 70);
        flash_until = Clock::now() + std::chrono::seconds(4);
    }
    if (dump) g.backend->dump_next_frame();

    if (g.backend->take_highlight_toggle()) {
        highlight = !highlight;
        g.backend->set_highlight(highlight);
        LOG_INFO("diag", "highlighting untranslated draws %s", highlight ? "on" : "off");
    }
    if (now >= next_shot) {
        next_shot = now + std::chrono::seconds(g.config.diag_seconds);
        char name[32];
        std::snprintf(name, sizeof(name), "/auto_%05lld.bmp", static_cast<long long>(seconds));
        g.backend->save_back_buffer((g.config.diag_dir + name).c_str());
    }

    std::vector<std::string> overlay = {"DIAG  marks: " + std::to_string(marks) + "  auto: " +
                                        std::to_string(auto_marks) + "   F12: mark   F11: magenta " +
                                        (highlight ? "ON" : "off") + fps_text};
    const uint32_t skipped_now = skipped_draws();
    const uint32_t unsupported = g.backend->unsupported_draws(), skipped = skipped_now - skipped_before;
    skipped_before = skipped_now;
    if (unsupported || skipped)
        overlay.push_back("this frame: " + std::to_string(unsupported) + " draws need missing features" +
                          (highlight ? " (magenta)" : "") + ", " + std::to_string(skipped) + " not drawn");
    if (now < flash_until) overlay.push_back(flash);
    g.backend->set_overlay(std::move(overlay));
    // What --diag spent here (screenshots, a paused note) is not the game's frame time.
    pacing.exclude(std::chrono::duration<double>(Clock::now() - now).count());
}

std::atomic<uint32_t> g_frames_last_second{0};
std::atomic<uint32_t> g_frames_shown_count{0};

// Frames per whole second of session time, for frames_last_second().
void count_frame() {
    static int64_t second = -1;
    static uint32_t frames = 0;
    const int64_t now = session_second();
    if (now != second) {
        const bool measured = now == second + 1 && second > g_unmeasured_until.load(std::memory_order_relaxed);
        if (second >= 0) g_frames_last_second.store(measured ? frames : 0, std::memory_order_relaxed);
        second = now;
        frames = 0;
    }
    ++frames;
    g_frames_shown_count.fetch_add(1, std::memory_order_relaxed);
}

uint32_t XB_STDCALL D3DDevice_Swap(uint32_t /*flags*/) {
    platform::HostFloatEnv float_env;
    count_frame();
    upload_back_buffer_writes();
    g.backend->finish_picture();  // before screenshots, so they show it as it is shown
    if (!g.config.diag_dir.empty()) diagnostics();
    {
        std::lock_guard lock(g.notice_mutex);
        if (!g.notice.empty() && std::chrono::steady_clock::now() >= g.notice_until) g.notice.clear();
        g.backend->set_notice(g.notice);
    }
    g_frames_shown.store(true, std::memory_order_relaxed);
    if (g.config.screenshot_interval && g.swap_count % g.config.screenshot_interval == 0) {
        const std::string path = g.config.screenshot_dir + "/frame_" + std::to_string(g.swap_count) + ".bmp";
        g.backend->save_back_buffer(path.c_str());
    }
    g.backend->present();
    if (!g.backend->pump_events()) {
        LOG_INFO("render", "window closed");
        std::exit(0);
    }
    device_field<uint32_t>(kDeviceSwapCount) = ++g.swap_count;
    return g.swap_count;
}

void XB_STDCALL D3DDevice_SetVerticalBlankCallback(XbPtr callback) { g.vblank_callback = callback; }

// The original reads it from the NV2A mode registers in the device.
void XB_STDCALL D3DDevice_GetDisplayMode(XbPtr mode) { *from_xbox_ptr<XbDisplayMode>(mode) = g.display_mode; }

void XB_STDCALL D3DDevice_GetDisplayFieldStatus(XbPtr status) {
    auto* s = from_xbox_ptr<XbFieldStatus>(status);
    s->field = 3;  // progressive
    s->vblank_count = g.vblank_count;
}

// --- Raw pushbuffer data ----------------------------------------------------------------

// BeginPush(dwords) returns space for the title to write NV2A commands into; EndPush
// gets the end of what it wrote, and we execute it.
thread_local std::vector<uint32_t> t_push_buffer;

XbPtr XB_STDCALL D3DDevice_BeginPush_4(uint32_t dwords) {
    t_push_buffer.assign(dwords + 1, 0);
    return to_xbox_ptr(t_push_buffer.data());
}

void XB_STDCALL D3DDevice_EndPush(XbPtr end) {
    const auto* begin = t_push_buffer.data();
    const size_t dwords = from_xbox_ptr<uint32_t>(end) - begin;
    if (dwords > t_push_buffer.size()) {
        LOG_ERROR("render", "EndPush past the space BeginPush gave (%zu > %zu dwords)", dwords, t_push_buffer.size());
        return;
    }
    nv2a::run(begin, dwords);
}

// --- Synchronization --------------------------------------------------------------------

uint32_t XB_STDCALL D3DDevice_InsertFence() { return ++g.fence; }

void XB_STDCALL D3DDevice_BlockOnFence(uint32_t /*fence*/) {}

void XB_STDCALL D3DDevice_BlockUntilIdle() {}

// type 0 = read callback, 1 = write callback; either runs once the GPU gets there,
// which for us is now. The callback is cdecl: void (DWORD context).
void XB_STDCALL D3DDevice_InsertCallback(uint32_t /*type*/, XbPtr callback, uint32_t context) {
    if (callback) call_title(callback, {context});
}

// --- The table -------------------------------------------------------------------------

}  // namespace

const xbox::KernelImpl kDeviceFunctions[] = {
    D3D_IMPL(D3D_SetPushBufferSize),
    D3D_IMPL(D3DDevice_BlockOnFence),
    D3D_IMPL(D3DDevice_BeginPush_4),
    D3D_IMPL(D3DDevice_BlockUntilIdle),
    D3D_IMPL(D3DDevice_EndPush),
    D3D_IMPL(D3DDevice_Clear),
    D3D_IMPL(D3DDevice_GetBackBuffer2),
    D3D_IMPL(D3DDevice_GetDepthStencilSurface2),
    D3D_IMPL(D3DDevice_GetDeviceCaps),
    D3D_IMPL(D3DDevice_GetDisplayFieldStatus),
    D3D_IMPL(D3DDevice_GetDisplayMode),
    D3D_IMPL(D3DDevice_GetRenderTarget2),
    D3D_IMPL(D3DDevice_GetScissors),
    D3D_IMPL(D3DDevice_GetScreenSpaceOffset),
    D3D_IMPL(D3DDevice_GetViewport),
    D3D_IMPL(D3DDevice_SetScissors),
    D3D_IMPL(D3DDevice_SetScreenSpaceOffset),
    D3D_IMPL(D3DDevice_SetViewport),
    D3D_IMPL(D3DDevice_InsertCallback),
    D3D_IMPL(D3DDevice_InsertFence),
    D3D_IMPL(D3DDevice_PersistDisplay),
    D3D_IMPL(D3DDevice_Reset),
    D3D_IMPL(D3DDevice_SetGammaRamp),
    D3D_IMPL(D3DDevice_SetRenderTarget),
    D3D_IMPL(D3DDevice_SetRenderTargetFast),
    D3D_IMPL(D3DDevice_SetVerticalBlankCallback),
    D3D_IMPL(D3DDevice_Swap),
    D3D_IMPL(Direct3D_CreateDevice),
};
const size_t kDeviceFunctionCount = std::size(kDeviceFunctions);

void resource_destroyed(XbPtr /*resource*/) {}

Backend* backend() { return g.backend.get(); }

XbPtr current_render_target() { return device_field<XbPtr>(kDeviceRenderTarget); }
XbPtr current_depth_stencil() { return device_field<XbPtr>(kDeviceDepthStencil); }

bool begin_draw() {
    if (!g.backend) return false;
    platform::HostFloatEnv float_env;
    upload_back_buffer_writes();
    return true;
}

float depth_buffer_max() {
    const auto* depth = from_xbox_ptr<XbSurface>(device_field<XbPtr>(kDeviceDepthStencil));
    if (!depth) return 16777215.0f;
    const uint32_t format = (depth->container.format & kFormatFormatMask) >> kFormatFormatShift;
    return format == kFmtD16 || format == kFmtLinD16 ? 65535.0f : 16777215.0f;
}

ViewState& view_state() { return g_view; }

void texture_written(XbPtr resource) {
    if (g.back_buffer && resource == to_xbox_ptr(g.back_buffer)) {
        g.back_buffer_written = true;
    } else if (g.backend) {
        platform::HostFloatEnv float_env;
        g.backend->forget_render_target(from_xbox_ptr<XbResource>(resource)->data);
    }
}

uint32_t frames_last_second() { return g_frames_last_second.load(std::memory_order_relaxed); }
uint32_t frames_shown() { return g_frames_shown_count.load(std::memory_order_relaxed); }

std::atomic<uint32_t> g_frame_rate_target{0};

void frame_rate_changed(uint32_t fps) {
    LOG_INFO("frames", "target %u fps", fps);
    g_frame_rate_target.store(fps, std::memory_order_relaxed);
    g_frame_rate_changed = true;
}

uint32_t frame_rate_target() { return g_frame_rate_target.load(std::memory_order_relaxed); }

uint32_t vertical_blanks_done() {
    std::lock_guard lock(g.vblank_done_mutex);
    return g.vblank_done;
}

void wait_vertical_blank_after(uint32_t seen, uint32_t timeout_ms) {
    std::unique_lock lock(g.vblank_done_mutex);
    g.vblank_done_changed.wait_for(lock, std::chrono::milliseconds(timeout_ms), [seen] { return g.vblank_done != seen; });
}

void show_notice(std::vector<std::string> lines, double seconds) {
    std::lock_guard lock(g.notice_mutex);
    g.notice = std::move(lines);
    g.notice_until = std::chrono::steady_clock::now() +
                     std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(seconds));
}

bool window_to_screen(float x, float y, float& screen_x, float& screen_y) {
    return g.backend && g.backend->window_to_screen(x, y, screen_x, screen_y);
}

void configure(const D3DConfig& config) {
    g.config = config;
    if (!config.diag_dir.empty()) platform::log_set_observer(diag_observer);
    set_render_state_address(config.render_state);
    set_texture_state_address(config.texture_state);
    install_draw_handlers();
}

const uint32_t* back_buffer_slot() {
    static_assert(sizeof(XbSurface*) == sizeof(uint32_t), "a 32-bit host: pointers are title addresses");
    return reinterpret_cast<const uint32_t*>(&g.back_buffer);
}

std::span<const xbox::KernelImpl> d3d_functions() {
    static const std::vector<xbox::KernelImpl> all = [] {
        std::vector<xbox::KernelImpl> v(kDeviceFunctions, kDeviceFunctions + kDeviceFunctionCount);
        v.insert(v.end(), kResourceFunctions, kResourceFunctions + kResourceFunctionCount);
        v.insert(v.end(), kStateFunctions, kStateFunctions + kStateFunctionCount);
        v.insert(v.end(), kShaderFunctions, kShaderFunctions + kShaderFunctionCount);
        v.insert(v.end(), kTextureFunctions, kTextureFunctions + kTextureFunctionCount);
        v.insert(v.end(), kDrawFunctions, kDrawFunctions + kDrawFunctionCount);
        return v;
    }();
    return all;
}

}  // namespace render
