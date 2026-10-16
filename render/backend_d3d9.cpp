// D3D9 back end: an SDL3 window and a Direct3D 9 device. On Linux d3d9 is DXVK
// (dxvk-native, which takes the SDL_Window* as its HWND); on Windows it is the system's.

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <memory>

#include "backend.h"
#include "d3d9_api.h"
#include "d3d9_draw.h"
#include "d3d9_overlay.h"
#include "log.h"

namespace render {

namespace {

template <typename T>
void release(T*& object) {
    if (object) object->Release();
    object = nullptr;
}

HWND window_handle(SDL_Window* window) {
#ifdef _WIN32
    return static_cast<HWND>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
#else
    return reinterpret_cast<HWND>(window);
#endif
}

// BGRX rows -> bottom-up 24-bit BMP.
bool write_bmp(const char* path, const uint8_t* pixels, int32_t pitch, uint32_t width, uint32_t height) {
    std::FILE* file = std::fopen(path, "wb");
    if (!file) return false;
    const uint32_t row = (width * 3 + 3) & ~3u;
    const uint32_t image = row * height;
    uint8_t header[54] = {'B', 'M'};
    auto put32 = [&](int offset, uint32_t v) { std::memcpy(header + offset, &v, 4); };
    put32(2, 54 + image);
    put32(10, 54);
    put32(14, 40);
    put32(18, width);
    put32(22, height);
    header[26] = 1;
    header[28] = 24;
    put32(34, image);
    std::fwrite(header, 1, sizeof(header), file);
    std::vector<uint8_t> line(row, 0);
    for (uint32_t y = height; y-- > 0;) {
        const uint8_t* src = pixels + static_cast<int64_t>(y) * pitch;
        for (uint32_t x = 0; x < width; ++x) std::memcpy(&line[x * 3], src + x * 4, 3);
        std::fwrite(line.data(), 1, row, file);
    }
    return std::fclose(file) == 0;
}

class D3D9Backend final : public Backend {
public:
    ~D3D9Backend() override {
        overlay_.reset();
        notice_.reset();
        draw_.reset();
        release(staging_);
        release(upload_);
        release(device_);
        release(d3d_);
        if (window_) SDL_DestroyWindow(window_);
        if (sdl_started_) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    bool init(const BackendConfig& config) {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
            LOG_ERROR("render", "SDL video: %s", SDL_GetError());
            return false;
        }
        sdl_started_ = true;
        const uint32_t scale = config.window_scale ? config.window_scale : 1;
        const uint32_t window_height = config.height * scale;
        const uint32_t window_width =
            config.display_aspect > 0 ? static_cast<uint32_t>(std::lround(window_height * config.display_aspect))
                                      : config.width * scale;
        window_ = SDL_CreateWindow("Call of Duty 2: Big Red One", static_cast<int>(window_width),
                                   static_cast<int>(window_height), SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
        if (!window_) {
            LOG_ERROR("render", "window: %s", SDL_GetError());
            return false;
        }
        if (config.fullscreen) {
            SDL_SetWindowFullscreen(window_, true);  // borderless, at the desktop's mode
            SDL_SyncWindow(window_);
        }
        int pixel_width = 0, pixel_height = 0;
        SDL_GetWindowSizeInPixels(window_, &pixel_width, &pixel_height);
        // The swap chain is as large as the largest display, and the window shows its
        // top-left corner (Present's source rectangle): the window can change size or go
        // fullscreen without a device Reset, which would lose every render target.
        swap_width_ = static_cast<uint32_t>(std::max(pixel_width, 1));
        swap_height_ = static_cast<uint32_t>(std::max(pixel_height, 1));
        int display_count = 0;
        if (SDL_DisplayID* displays = SDL_GetDisplays(&display_count)) {
            for (int i = 0; i < display_count; ++i) {
                if (const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(displays[i])) {
                    const float density = mode->pixel_density > 0 ? mode->pixel_density : 1.0f;
                    swap_width_ = std::max(swap_width_, static_cast<uint32_t>(std::lround(mode->w * density)));
                    swap_height_ = std::max(swap_height_, static_cast<uint32_t>(std::lround(mode->h * density)));
                }
            }
            SDL_free(displays);
        }
#ifndef _WIN32
        // dxvk-native supports several window systems; ours is SDL3.
        setenv("DXVK_WSI_DRIVER", "SDL3", 0);
        // DXVK's messages still go to stderr, but no <exe>_d3d9.log files land in the
        // working folder.
        setenv("DXVK_LOG_PATH", "none", 0);
        // Present without waiting for the display (see BackendConfig::vsync) but without
        // tearing: DXVK then uses the mailbox mode. (Windows composes windowed swap
        // chains, which do not tear.)
        if (config.vsync) {
            const char* user = std::getenv("DXVK_CONFIG");
            const std::string dxvk = std::string(user ? user : "") + (user && *user ? "; " : "") + "dxvk.tearFree = True";
            setenv("DXVK_CONFIG", dxvk.c_str(), 1);
        }
#endif
        d3d_ = Direct3DCreate9(D3D_SDK_VERSION);
        if (!d3d_) {
            LOG_ERROR("render", "Direct3DCreate9 failed");
            return false;
        }
        D3DADAPTER_IDENTIFIER9 id{};
        if (SUCCEEDED(d3d_->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id)))
            LOG_INFO("render", "D3D9 adapter: %s", id.Description);

        // The title's back buffer (render scale) is stretched onto the swap chain's
        // window-sized corner. Depth buffers are D3D9Draw's.
        D3DPRESENT_PARAMETERS pp{};
        pp.BackBufferWidth = swap_width_;
        pp.BackBufferHeight = swap_height_;
        pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        pp.BackBufferCount = 1;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = window_handle(window_);
        pp.Windowed = TRUE;
        pp.EnableAutoDepthStencil = FALSE;
        pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        // The title calls D3D from more than one thread (its vertical blank callback
        // and loading screens), so let the runtime serialize.
        const HRESULT hr = d3d_->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, pp.hDeviceWindow,
                                              D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED |
                                                  D3DCREATE_FPU_PRESERVE,
                                              &pp, &device_);
        if (FAILED(hr)) {
            LOG_ERROR("render", "CreateDevice failed: %08x", static_cast<unsigned>(hr));
            return false;
        }
        width_ = config.width;
        height_ = config.height;
        aspect_ = config.display_aspect > 0 ? config.display_aspect : static_cast<float>(width_) / height_;
        pillarbox_ = config.pillarbox_flat_frames && config.display_aspect > 0;
        back_buffer_data_ = config.back_buffer.data;
        BackendConfig draw_config = config;
        if (draw_config.render_scale <= 0)  // the window's (or the screen's) height
            draw_config.render_scale = static_cast<float>(std::max(pixel_height, 1)) / height_;
        draw_ = std::make_unique<D3D9Draw>(device_, draw_config);
        overlay_ = std::make_unique<D3D9Overlay>(device_);
        notice_ = std::make_unique<D3D9Overlay>(device_);
        LOG_INFO("render", "D3D9 device: %ux%u, rendering at %.2fx%.2f, window %dx%d%s, swap chain %ux%u",
                 config.width, config.height, static_cast<double>(draw_config.render_scale_x()),
                 static_cast<double>(draw_config.render_scale), pixel_width, pixel_height,
                 config.fullscreen ? " (fullscreen)" : "", swap_width_, swap_height_);
        return true;
    }

    void set_render_target(const SurfaceDesc& color, const SurfaceDesc* depth) override {
        draw_->set_render_target(color, depth);
    }

    void forget_render_target(uint32_t data) override { draw_->forget_render_target(data); }

    void clear(uint32_t flags, uint32_t color, float depth, uint32_t stencil) override {
        DWORD d3d_flags = 0;
        if (flags & kClearColor) d3d_flags |= D3DCLEAR_TARGET;
        if ((flags & kClearDepth) && draw_->has_depth()) d3d_flags |= D3DCLEAR_ZBUFFER;
        if ((flags & kClearStencil) && draw_->has_depth()) d3d_flags |= D3DCLEAR_STENCIL;
        if (d3d_flags) device_->Clear(0, nullptr, d3d_flags, color, depth, stencil);
    }

    // Through the title-sized staging_ (CPU) and upload_ (GPU) surfaces, then stretched
    // onto the back buffer at the render scale.
    void write_back_buffer(const void* pixels, uint32_t pitch) override {
        if (!staging_ &&
            FAILED(device_->CreateOffscreenPlainSurface(width_, height_, D3DFMT_X8R8G8B8, D3DPOOL_SYSTEMMEM,
                                                        &staging_, nullptr)))
            return;
        if (!upload_ &&
            FAILED(device_->CreateOffscreenPlainSurface(width_, height_, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &upload_,
                                                        nullptr)))
            return;
        D3DLOCKED_RECT locked;
        if (FAILED(staging_->LockRect(&locked, nullptr, 0))) return;
        for (uint32_t y = 0; y < height_; ++y)
            std::memcpy(static_cast<uint8_t*>(locked.pBits) + y * locked.Pitch,
                        static_cast<const uint8_t*>(pixels) + y * pitch, width_ * 4);
        staging_->UnlockRect();
        if (IDirect3DSurface9* back_buffer = draw_->target_surface(back_buffer_data_)) {
            device_->UpdateSurface(staging_, nullptr, upload_, nullptr);
            device_->StretchRect(upload_, nullptr, back_buffer, nullptr, D3DTEXF_LINEAR);
        }
    }

    // At the render scale, pillarboxed frames at their own shape.
    bool save_back_buffer(const char* path) override {
        IDirect3DSurface9* back_buffer = draw_->target_surface(back_buffer_data_);
        IDirect3DSurface9* narrow = nullptr;
        IDirect3DSurface9* copy = nullptr;
        D3DSURFACE_DESC desc{};
        bool ok = back_buffer && SUCCEEDED(back_buffer->GetDesc(&desc));
        if (ok && pillarboxed_) {
            desc.Width = static_cast<UINT>(std::lround(static_cast<double>(desc.Height) * width_ / height_));
            ok = SUCCEEDED(device_->CreateRenderTarget(desc.Width, desc.Height, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE,
                                                       0, FALSE, &narrow, nullptr)) &&
                 SUCCEEDED(device_->StretchRect(back_buffer, nullptr, narrow, nullptr, D3DTEXF_LINEAR));
            back_buffer = narrow;
        }
        ok = ok &&
             SUCCEEDED(device_->CreateOffscreenPlainSurface(desc.Width, desc.Height, D3DFMT_X8R8G8B8,
                                                            D3DPOOL_SYSTEMMEM, &copy, nullptr)) &&
             SUCCEEDED(device_->GetRenderTargetData(back_buffer, copy));
        D3DLOCKED_RECT locked{};
        if (ok) ok = SUCCEEDED(copy->LockRect(&locked, nullptr, D3DLOCK_READONLY));
        if (ok) {
            ok = write_bmp(path, static_cast<const uint8_t*>(locked.pBits), locked.Pitch, desc.Width, desc.Height);
            copy->UnlockRect();
        }
        release(copy);
        release(narrow);
        if (!ok) LOG_WARN("render", "cannot save the back buffer to %s", path);
        return ok;
    }

    // The title's back buffer is a render target texture like any other; it is
    // copied to the swap chain's window-sized corner to be shown, at its own shape
    // between black bars, with the overlay on top.
    void show(const std::vector<std::string>& overlay) {
        int window_width = 0, window_height = 0;
        SDL_GetWindowSizeInPixels(window_, &window_width, &window_height);
        const LONG area_width = std::clamp<LONG>(window_width, 1, static_cast<LONG>(swap_width_));
        const LONG area_height = std::clamp<LONG>(window_height, 1, static_cast<LONG>(swap_height_));
        const RECT area{0, 0, area_width, area_height};
        IDirect3DSurface9* swap_chain = nullptr;
        IDirect3DSurface9* back_buffer = draw_->target_surface(back_buffer_data_);
        if (back_buffer && SUCCEEDED(device_->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &swap_chain))) {
            // Pillarbox after a few flat frames in a row, so that a moment without the
            // scene (a fade) does not jump.
            constexpr uint32_t kPillarboxAfter = 8;
            pillarboxed_ = pillarbox_ && draw_->frame_is_flat() && draw_->flat_frames_before() >= kPillarboxAfter;
            const double aspect = pillarboxed_ ? static_cast<double>(width_) / height_ : aspect_;
            LONG width = area_width, height = std::lround(area_width / aspect);
            if (height > area_height) {
                height = area_height;
                width = std::lround(area_height * aspect);
            }
            const LONG left = (area_width - width) / 2, top = (area_height - height) / 2;
            const RECT dest{left, top, left + width, top + height};
            if (width != area_width || height != area_height)
                device_->ColorFill(swap_chain, &area, D3DCOLOR_XRGB(0, 0, 0));
            device_->StretchRect(back_buffer, nullptr, swap_chain, &dest, D3DTEXF_LINEAR);
            overlay_->draw(swap_chain, overlay, 4, 4);
            // Above the menus' button legend, the font 3 times larger at 960 lines.
            const int scale = std::max(1, static_cast<int>(area_height) / 320);
            notice_->draw(swap_chain, notice_lines_, D3D9Overlay::kCentre, static_cast<int>(area_height) * 7 / 10,
                          scale, static_cast<int>(area_width));
            swap_chain->Release();
        }
        device_->Present(&area, nullptr, nullptr, nullptr);
    }

    void present() override {
        show(overlay_lines_);
        draw_->end_frame();
    }

    double refresh_rate() override {
        const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window_));
        if (!mode) return 0;
        if (mode->refresh_rate_numerator && mode->refresh_rate_denominator)
            return static_cast<double>(mode->refresh_rate_numerator) / mode->refresh_rate_denominator;
        return mode->refresh_rate;
    }

    void set_overlay(std::vector<std::string> lines) override { overlay_lines_ = std::move(lines); }
    void set_notice(std::vector<std::string> lines) override { notice_lines_ = std::move(lines); }

    std::optional<std::string> prompt(const std::vector<std::string>& heading) override {
        constexpr size_t kMaxText = 200, kLineChars = 76;
        const bool captured = SDL_GetWindowRelativeMouseMode(window_);
        SDL_SetWindowRelativeMouseMode(window_, false);
        SDL_StartTextInput(window_);
        std::string text;
        std::optional<std::string> result;
        bool done = false;
        while (!done) {
            SDL_Event event;
            bool have = SDL_WaitEventTimeout(&event, 100);
            while (have && !done) {
                switch (event.type) {
                    case SDL_EVENT_QUIT:
                    case SDL_EVENT_WINDOW_CLOSE_REQUESTED: closed_ = done = true; break;
                    case SDL_EVENT_TEXT_INPUT:
                        if (text.size() < kMaxText) text += event.text.text;
                        break;
                    case SDL_EVENT_KEY_DOWN:
                        if (event.key.key == SDLK_BACKSPACE) {
                            while (!text.empty() && (text.back() & 0xC0) == 0x80) text.pop_back();  // UTF-8
                            if (!text.empty()) text.pop_back();
                        } else if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
                            result = text;
                            done = true;
                        } else if (event.key.key == SDLK_ESCAPE) {
                            done = true;
                        }
                        break;
                    default: break;
                }
                have = SDL_PollEvent(&event);
            }
            std::vector<std::string> lines = heading;
            const std::string field = "> " + text + "_";
            for (size_t i = 0; i < field.size(); i += kLineChars) lines.push_back(field.substr(i, kLineChars));
            lines.push_back("Return: save   Escape: skip");
            show(lines);
        }
        SDL_StopTextInput(window_);
        // Let go of Return/Escape before the title reads the keyboard again.
        for (int i = 0; i < 100; ++i) {
            SDL_PumpEvents();
            const bool* keys = SDL_GetKeyboardState(nullptr);
            if (!keys[SDL_SCANCODE_RETURN] && !keys[SDL_SCANCODE_ESCAPE] && !keys[SDL_SCANCODE_KP_ENTER]) break;
            SDL_Delay(10);
        }
        if (captured && !closed_) SDL_SetWindowRelativeMouseMode(window_, true);
        return result;
    }

    void draw(const DrawInput& input) override {
        device_->BeginScene();
        draw_->draw(input);
        device_->EndScene();
    }

    bool pump_events() override {
        if (closed_) return false;
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) return false;
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_F12 && !event.key.repeat)
                mark_requested_ = true;
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_F11 && !event.key.repeat)
                highlight_toggled_ = true;
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && (event.key.mod & SDL_KMOD_ALT) &&
                (event.key.scancode == SDL_SCANCODE_RETURN || event.key.scancode == SDL_SCANCODE_KP_ENTER)) {
                const bool fullscreen = !(SDL_GetWindowFlags(window_) & SDL_WINDOW_FULLSCREEN);
                SDL_SetWindowFullscreen(window_, fullscreen);
                LOG_INFO("render", "%s (Alt+Enter)", fullscreen ? "fullscreen" : "windowed");
            }
        }
        return true;
    }

    bool take_mark_request() override { return std::exchange(mark_requested_, false); }
    bool take_highlight_toggle() override { return std::exchange(highlight_toggled_, false); }
    void set_highlight(bool on) override { draw_->set_highlight(on); }
    uint32_t unsupported_draws() override { return draw_->unsupported_draws(); }
    void dump_next_frame() override { draw_->dump_next_frame(); }

private:
    bool sdl_started_ = false;
    SDL_Window* window_ = nullptr;
    IDirect3D9* d3d_ = nullptr;
    IDirect3DDevice9* device_ = nullptr;
    IDirect3DSurface9* staging_ = nullptr;  // CPU-written back buffer upload
    IDirect3DSurface9* upload_ = nullptr;
    std::unique_ptr<D3D9Draw> draw_;
    uint32_t width_ = 0, height_ = 0;
    float aspect_ = 4.0f / 3.0f;                      // the picture's shape (BackendConfig::display_aspect)
    uint32_t swap_width_ = 0, swap_height_ = 0;     // the largest display's size
    uint32_t back_buffer_data_ = 0;
    bool pillarbox_ = false;    // BackendConfig::pillarbox_flat_frames
    bool pillarboxed_ = false;  // the frame shown last was
    bool mark_requested_ = false;
    bool highlight_toggled_ = false;
    bool closed_ = false;
    std::unique_ptr<D3D9Overlay> overlay_;
    std::vector<std::string> overlay_lines_;
    std::unique_ptr<D3D9Overlay> notice_;  // its own, so neither redraws its text every frame
    std::vector<std::string> notice_lines_;
};

}  // namespace

std::unique_ptr<Backend> create_d3d9_backend(const BackendConfig& config) {
    auto backend = std::make_unique<D3D9Backend>();
    if (!backend->init(config)) return nullptr;
    return backend;
}

}  // namespace render
