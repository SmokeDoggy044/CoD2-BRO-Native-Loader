#pragma once

// What every back end (D3D9, Vulkan) shares: the SDL3 window, its events (diagnostic keys, Alt+Enter, the
// PC menu's key), the diagnostic prompt's text input, where the picture is placed in the window, the
// title's gamma ramp with the PC brightness, the overlay text's pixels and the PC menu's Dear ImGui
// context. Only drawing is the back end's.

#include <SDL3/SDL.h>

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "backend.h"

namespace render {

// A rectangle in window pixels.
struct WindowRect {
    int32_t left = 0, top = 0, right = 0, bottom = 0;
    int32_t width() const { return right - left; }
    int32_t height() const { return bottom - top; }
};

class GameWindow {
public:
    GameWindow() = default;
    ~GameWindow();
    GameWindow(const GameWindow&) = delete;
    GameWindow& operator=(const GameWindow&) = delete;

    // The window for `config` (its size, fullscreen, hidden for tests); `api_flags` the graphics API's
    // (SDL_WINDOW_VULKAN). Logged on failure.
    bool open(const BackendConfig& config, SDL_WindowFlags api_flags);
    SDL_Window* sdl() const { return window_; }
    void pixel_size(int& width, int& height) const;
    // The largest display's size in pixels (a swap chain that fits the window in any mode).
    void largest_display(uint32_t& width, uint32_t& height) const;
    double refresh_rate() const;

    // Window events (Backend::pump_events). False once the window was closed.
    bool pump_events();
    bool take_mark_request();
    bool take_highlight_toggle();
    // Backend::prompt: `show` presents the frame with these lines over it.
    std::optional<std::string> prompt(const std::vector<std::string>& heading,
                                      const std::function<void(const std::vector<std::string>&)>& show);

    // Where a picture of `aspect` (width / height) goes in the window's top-left `area` (centred, as
    // large as fits); remembered for window_to_screen.
    WindowRect place_picture(const WindowRect& area, double aspect);
    bool window_to_screen(float x, float y, float& screen_x, float& screen_y);

    // --- The PC menu (BackendConfig::pc_menu) ---
    // `init_renderer` sets up the API's Dear ImGui renderer once the context exists, `shutdown_renderer`
    // takes it down; the back end draws ImGui's draw data after menu_frame().
    void set_menu_renderer(std::function<bool()> init_renderer, std::function<void()> shutdown_renderer);
    bool menu_open() const { return menu_open_; }
    // ImGui's frame: the menu's widgets (NewFrame ... Render), after the renderer's own NewFrame.
    void menu_frame();
    // Takes down the menu's renderer and Dear ImGui (before the back end's device goes).
    void close_menu();

    // --- Gamma ---
    void set_gamma_ramp(const uint8_t (&ramp)[3][256]);
    // A changed ramp (with the PC brightness) as 256 X8R8G8B8 entries since the last call; `identity`:
    // nothing to apply. False: unchanged.
    bool take_gamma_table(uint32_t (&table)[256], bool& identity);

private:
    bool init_menu();
    void set_menu_open(bool open);

    bool sdl_started_ = false;
    SDL_Window* window_ = nullptr;
    uint32_t width_ = 640, height_ = 480;  // the title's back buffer
    bool closed_ = false;
    bool mark_requested_ = false;
    bool highlight_toggled_ = false;
    std::mutex picture_mutex_;
    WindowRect picture_{};  // guarded by picture_mutex_

    bool (*pc_menu_)() = nullptr;
    void (*pc_menu_shown_)(bool) = nullptr;
    std::string menu_font_;
    std::function<bool()> init_menu_renderer_;
    std::function<void()> shutdown_menu_renderer_;
    bool menu_ready_ = false;
    bool menu_open_ = false;
    bool menu_recapture_ = false;

    std::mutex gamma_mutex_;
    uint8_t gamma_pending_[3][256] = {};  // guarded by gamma_mutex_
    bool gamma_changed_ = false;          // guarded by gamma_mutex_
    float brightness_ = 1.0f;
};

// Diagnostics text as pixels (SDL's debug font): lines in a dark box, the first in yellow.
class TextImage {
public:
    static constexpr uint32_t kWidth = 640, kHeight = 128;  // the most text drawn at once
    TextImage();
    ~TextImage();
    TextImage(const TextImage&) = delete;
    TextImage& operator=(const TextImage&) = delete;

    // Renders the lines if they changed (returns true then); `width` x `height` = the box's size.
    bool render(const std::vector<std::string>& lines, uint32_t& width, uint32_t& height);
    // A8R8G8B8, kWidth x kHeight, `pitch()` bytes per row.
    const void* pixels() const;
    uint32_t pitch() const;
    bool ok() const { return renderer_ != nullptr; }

private:
    SDL_Surface* surface_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    std::vector<std::string> last_lines_;
    uint32_t last_width_ = 0, last_height_ = 0;
    bool rendered_ = false;
};

// A 24-bit BMP from X8R8G8B8 rows (top first). Returns false on failure.
bool write_bmp(const char* path, const uint8_t* pixels, int32_t pitch, uint32_t width, uint32_t height);

}  // namespace render
