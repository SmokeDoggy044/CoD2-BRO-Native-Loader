#pragma once

// The renderer's back end: a window and a PC graphics API. The Xbox D3D front end
// (device.cpp) turns the title's calls into these; D3D9 (DXVK on Linux) is the first
// implementation, others (Vulkan) can follow behind the same interface.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace render {

class GfxDevice;

// A surface the title draws into (render target or depth buffer). Its memory address
// identifies it: textures at the same address show what was drawn into it.
struct SurfaceDesc {
    uint32_t data = 0;  // physical address of its memory
    uint32_t width = 0, height = 0;
    uint32_t format = 0;  // Xbox D3DFORMAT
};

// A picture shown in place of one of the title's textures (PC improvements: sharper
// fonts). Any size: the title addresses its textures in 0-1 coordinates (linear formats
// excepted, which are never replaced), so a larger copy with the same layout fits.
struct TextureReplacement {
    uint32_t width = 0, height = 0;
    std::vector<uint32_t> pixels;  // A8R8G8B8, width * height; mipmaps are made from it
};

// How to narrow one draw of UI laid out for a narrower shape than the picture's
// (BackendConfig::narrow_ui_draw): x' = a w + (x - a w) * factor.
struct UiNarrowing {
    float anchor = -1.0f;  // a, a fraction of the width; negative: not narrowed
    float factor = 1.0f;
};

struct BackendConfig {
    // Horizontal render scale: render_scale widened to the display aspect.
    float render_scale_x() const {
        return display_aspect > 0 ? render_scale * display_aspect * height / width : render_scale;
    }

    uint32_t width = 640;  // the title's back buffer size
    uint32_t height = 480;
    SurfaceDesc back_buffer;    // what present() shows
    SurfaceDesc depth_stencil;  // the device's own depth buffer (data 0 = none)
    uint32_t window_scale = 1;
    bool hidden_window = false;  // tests: draw without showing a window
    // Borderless fullscreen on the window's display (Alt+Enter switches while playing).
    bool fullscreen = false;
    // Render targets are this many times the size the title asked for (the title still
    // sees its own sizes). Above the window's size it supersamples. Vertical;
    // horizontally it grows with the display aspect. 0 = the window's height (the
    // screen's in fullscreen) over the title's.
    float render_scale = 0;
    // The shape the title's picture is meant to be shown at: 0 = its back buffer's
    // own (4:3 for 640x480); 16/9 for a title rendering widescreen anamorphically.
    // The window and every render target are widened to it.
    float display_aspect = 0;
    // With a wider display_aspect: frames without a 3D scene (menus, loading screens,
    // movies; the title lays them out for its back buffer's shape only) are shown at
    // that shape instead, centred between black bars.
    bool pillarbox_flat_frames = false;
    // With a wider display_aspect: asked per draw into the back buffer over a 3D scene;
    // while the title draws UI laid out for a narrower shape than the picture's (in-game
    // menus: the back buffer's; the HUD on an ultrawide picture: 16:9), where and how
    // much to narrow it. Draws spanning the full width are not narrowed.
    UiNarrowing (*narrow_ui_draw)() = nullptr;
    // The in-game PC menu, opened and closed with the key below Escape (` ~) and closed
    // with Escape: Dear ImGui over the picture while the title keeps running. pc_menu
    // makes its widgets each frame (ImGui calls, from present() on the title's thread)
    // and returns false to close it; pc_menu_shown is told when it opens and closes.
    // Null: no menu.
    bool (*pc_menu)() = nullptr;
    void (*pc_menu_shown)(bool open) = nullptr;
    std::string menu_font;  // a TTF for the menu; empty: Dear ImGui's own
    // Anisotropic filtering for mipmapped textures the title filters linearly (PC
    // option; the Xbox titles rarely used it). 1 = the title's own filters.
    uint32_t anisotropy = 1;
    // No tearing. Present never waits for the display either way: the title paces
    // itself on the vertical blank (device.cpp), which runs at the display's rate.
    bool vsync = true;
    // Which Direct3D 9 to draw with on Windows: DXVK's (dxvk/d3d9.dll next to the
    // loader, on Vulkan) or the system's. Auto tries DXVK first and falls back to the
    // system's when it cannot create a device. Linux always uses DXVK.
    enum class D3D9 { Auto, Dxvk, System };
    D3D9 d3d9 = D3D9::Auto;
    // PC brightness on top of the title's gamma ramp (CoD2: BRO has no setting of its
    // own): each value v becomes 255 * (ramp(v) / 255)^(1 / brightness), so black and
    // white stay and the midtones move. 1 = as on the Xbox.
    float brightness = 1.0f;
    // Asked once per texture content the title shows (render/texture_hash.h; swizzled
    // and DXT formats only) with the render scale: a replacement, or null for the
    // title's own. Called on the drawing thread.
    std::function<std::shared_ptr<const TextureReplacement>(uint64_t content, float render_scale)> replace_texture;
    // For replacements that change while the title shows the same texture: a number
    // that changes whenever replace_texture's answer for `content` may have (0 = never),
    // asked once per frame for each texture in use. Null: answers never change.
    std::function<uint32_t(uint64_t content)> replacement_version;
};

// One draw: float4 attributes and a primitive. The back end reads everything else
// (shaders, textures, render states) from the Xbox-side state (nv2a.h,
// shader_state.h, d3d_internal.h) and translates it.
struct DrawInput {
    enum class Primitive { Invalid, Points, Lines, LineStrip, Triangles, TriangleStrip, TriangleFan };
    Primitive primitive = Primitive::Invalid;
    const float* vertices = nullptr;  // per vertex: one float4 per attribute in attribute_mask, in order
    uint32_t vertex_count = 0;
    uint32_t attribute_mask = 0;        // vertex shader inputs v0..v15 present
    const uint32_t* indices = nullptr;  // null: vertices in order
    uint32_t index_count = 0;
    float depth_max = 16777215.0f;  // the depth buffer's largest value (screen-space z range)
    // Non-zero: the vertices come from title vertex buffers that stayed the same for a
    // few frames (the world, models), identified by `vertex_key` with contents hashing to
    // `vertex_content`; the back end may keep them converted. `vertices` is null when
    // has_cached_vertices said it has them.
    uint64_t vertex_key = 0;
    uint64_t vertex_content = 0;
};

class Backend {
public:
    virtual ~Backend() = default;

    enum ClearFlags : uint32_t { kClearColor = 1, kClearDepth = 2, kClearStencil = 4 };
    // Where draws and clears go from now on; depth null = no depth buffer.
    virtual void set_render_target(const SurfaceDesc& color, const SurfaceDesc* depth) = 0;

    // The CPU writes the memory of a render target other than the back buffer: textures
    // there read the memory again instead of what was drawn.
    virtual void forget_render_target(uint32_t data) = 0;

    // Clears the whole current render target / depth buffer. color is A8R8G8B8.
    virtual void clear(uint32_t flags, uint32_t color, float depth, uint32_t stencil) = 0;

    // Replaces the back buffer's pixels with CPU-written ones (X8R8G8B8, back buffer
    // size, `pitch` bytes per row).
    virtual void write_back_buffer(const void* pixels, uint32_t pitch) = 0;

    // Saves the back buffer as a 24-bit BMP. Returns false (logged) on failure.
    virtual bool save_back_buffer(const char* path) = 0;

    // Shows the back buffer.
    virtual void present() = 0;

    // The window's display refresh rate in Hz, 0 if unknown.
    virtual double refresh_rate() = 0;

    // Draws to the current render target with the current Xbox state.
    virtual void draw(const DrawInput& input) = 0;

    // Room for the next draw's converted vertices (`attributes` float4s each) in the back
    // end's own draw buffer, so they are written once instead of copied; null: use other
    // memory. Pass the pointer as DrawInput::vertices to the next draw.
    virtual float* map_vertices(uint32_t vertex_count, uint32_t attributes) {
        (void)vertex_count;
        (void)attributes;
        return nullptr;
    }
    // Whether the back end still holds the vertices of DrawInput::vertex_key with these
    // contents, so the next draw can pass null vertices.
    virtual bool has_cached_vertices(uint64_t key, uint64_t content) {
        (void)key;
        (void)content;
        return false;
    }

    // Handles window events. Returns false once the user closed the window.
    virtual bool pump_events() = 0;

    // Diagnostics: text shown in the window's corner from now on (not in screenshots).
    virtual void set_overlay(std::vector<std::string> lines) = 0;
    // A notice for the player, in large text low in the middle of the window, shown
    // from now on (not in screenshots); empty = none.
    virtual void set_notice(std::vector<std::string> lines) = 0;
    // Shows the last frame with `heading` and a text field until the user presses
    // Return (the text) or Escape (nullopt). The title is frozen meanwhile (this runs
    // on its thread). If the window is closed, pump_events() returns false afterwards.
    virtual std::optional<std::string> prompt(const std::vector<std::string>& heading) = 0;
    // Diagnostics: true once after the user pressed the mark key (F12).
    virtual bool take_mark_request() = 0;
    // Diagnostics: true once after the user pressed the highlight key (F11).
    virtual bool take_highlight_toggle() = 0;
    // Diagnostics: draw what needs untranslated features in solid magenta.
    virtual void set_highlight(bool on) = 0;
    // Draws in the current frame so far that need untranslated features.
    virtual uint32_t unsupported_draws() = 0;
    // Logs every draw of the next frame (see D3D9Draw::dump_draw).
    virtual void dump_next_frame() = 0;
    // The title's gamma ramp (red, green, blue: 256 entries each), applied to the
    // picture shown from the next present on (the Xbox applies it in the video DAC;
    // CoD2: BRO's brightness setting). Any thread.
    virtual void set_gamma_ramp(const uint8_t (&ramp)[3][256]) = 0;
    // Maps a point in the window (pixels) to the title's 4:3 screen coordinates
    // (width x height), where its menus are laid out: they are shown pillarboxed or
    // narrowed into the middle of a wider picture, at the picture's height. False
    // before the first frame. Any thread.
    virtual bool window_to_screen(float x, float y, float& screen_x, float& screen_y) = 0;
    // Our own graphics interface on the same device and targets (gfx_device.h); null if the
    // back end has none.
    virtual GfxDevice* gfx_device() { return nullptr; }
};

// Null if the window or device cannot be created (logged).
std::unique_ptr<Backend> create_d3d9_backend(const BackendConfig& config);

}  // namespace render
